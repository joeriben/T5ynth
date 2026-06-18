#include "DelayLine.h"

namespace
{
    // St: L/R time offset depth. Longer side = base*(1+spread); kept small so
    // the worst-case read stays inside the buffer (see kMaxDelaySeconds).
    constexpr float  kStereoSpread     = 0.15f;
    // Tape feedback voicing (no user parameter — all fixed/auto).
    constexpr float  kTapeHpHz         = 120.0f;   // high-pass in the loop
    constexpr float  kTapeDrive        = 1.8f;     // soft-sat pre-gain
    constexpr float  kWowRateHz        = 0.6f;     // wow LFO rate
    constexpr float  kWowDepth         = 0.0025f;  // ±0.25% read-position drift
    // Buffer capacity headroom: the processor clamps modulated time to 5 s;
    // St can stretch one side by (1+kStereoSpread). Size for the worst case.
    constexpr double kMaxDelaySeconds  = 6.0;
}

void T5ynthDelayLine::prepare(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 2;

    delayLine.prepare(spec);

    // SR-aware capacity. The old fixed 96000 samples (= 2 s @48k) overflowed at
    // higher sample rates and under modulation toward the processor's 5 s clamp;
    // size for the clamp plus the St stereo-spread headroom. setMaximumDelay...
    // reallocates using the channel count established by prepare() above.
    const int capacity = static_cast<int>(std::ceil(kMaxDelaySeconds * sampleRate))
                       + samplesPerBlock + 4;
    delayLine.setMaximumDelayInSamples(capacity);
    maxDelaySamples = static_cast<float>(capacity - samplesPerBlock - 2);

    currentDelaySamples = static_cast<float>(delayTimeMs * 0.001 * sr);
    targetDelaySamples = currentDelaySamples;
    delayLine.setDelay(juce::jlimit(1.0f, maxDelaySamples, currentDelaySamples));
    targetFeedback = feedback;

    // Damping low-pass (Butterworth, Q ~= 0.707) + fixed tape high-pass.
    updateDampCoeffs();
    auto hp = juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, kTapeHpHz, 0.707f);
    tapeHpL.coefficients = hp;
    tapeHpR.coefficients = hp;

    // Settle each filter's internal order off-thread to match the freshly
    // assigned 2nd-order coefficients. Assigning .coefficients is only a
    // ReferenceCountedObjectPtr swap and does NOT update Filter::order, so
    // without this the first audio-thread processSample() would trip
    // Filter::check() → reset() → malloc — the #1 BLOCKING bug class.
    // prepareToPlay calls prepare() but never reset(), so the first block
    // would otherwise pay it.
    dampFilterL.reset();
    dampFilterR.reset();
    tapeHpL.reset();
    tapeHpR.reset();

    prepared = true;
}

void T5ynthDelayLine::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (!prepared || wetMix == 0.0f)
        return;

    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0 || buffer.getNumChannels() < 1)
        return;

    // Silence detection: skip only after output has truly decayed
    float inMag = buffer.getMagnitude(0, numSamples);
    bool inputSilent = inMag < 1e-6f;

    if (inputSilent && silentOutputBlocks > SILENCE_CONFIRM_BLOCKS)
        return;

    const bool stereo = buffer.getNumChannels() >= 2;

    // True crossfade insert: out = dry*(1-mix) + wet*mix. At mix=1 the dry path
    // vanishes entirely. Feedback path is unaffected — the delay-line input
    // always receives the un-attenuated dry plus conditioned feedback, so the
    // tail keeps growing predictably across the full mix range.
    const float dryGain = 1.0f - wetMix;

    // Per-sample smoothing coefficient (~5ms ramp at current SR)
    const float smoothCoeff = 1.0f - std::exp(-1.0f / static_cast<float>(sr * 0.005));

    // Mode flags. Stereo routings require an actual stereo buffer; on a mono
    // buffer St/Cross degrade gracefully to dual-mono (no offset, no cross).
    const bool doOffset = (mode == kStereo) && stereo;
    const bool doCross  = (mode == kCross)  && stereo;
    const bool doTape   = (mode == kTape);

    const float ratioL = doOffset ? (1.0f + kStereoSpread) : 1.0f;
    const float ratioR = doOffset ? (1.0f - kStereoSpread) : 1.0f;
    const float wowInc = juce::MathConstants<float>::twoPi * kWowRateHz
                       / static_cast<float>(sr);

    auto* dataL = buffer.getWritePointer(0);
    auto* dataR = stereo ? buffer.getWritePointer(1) : nullptr;

    // Feedback conditioning per channel: fb gain, then (Tape only) high-pass +
    // damping low-pass + soft tanh saturation. The tanh is normalised by its
    // drive so small-signal gain stays ~1.0 (feedback feel unchanged) while hot
    // signals compress — an automatic limiter that tames runaway tape feedback.
    auto conditionFb = [this, doTape](float x,
                                      juce::dsp::IIR::Filter<float>& damp,
                                      juce::dsp::IIR::Filter<float>& hp) -> float
    {
        float y = x * feedback;
        if (doTape)
        {
            y = hp.processSample(y);
            y = damp.processSample(y);
            y = std::tanh(y * kTapeDrive) / kTapeDrive;
        }
        else
        {
            y = damp.processSample(y);
        }
        return y;
    };

    for (int i = 0; i < numSamples; ++i)
    {
        // Smooth delay time and feedback per-sample to avoid clicks.
        currentDelaySamples += (targetDelaySamples - currentDelaySamples) * smoothCoeff;
        feedback += (targetFeedback - feedback) * smoothCoeff;

        // Tape wow: tiny read-position drift (skipped entirely in other modes).
        float wow = 0.0f;
        if (doTape)
        {
            wow = std::sin(wowPhase) * kWowDepth * currentDelaySamples;
            wowPhase += wowInc;
            if (wowPhase > juce::MathConstants<float>::twoPi)
                wowPhase -= juce::MathConstants<float>::twoPi;
        }

        const float dlyL = juce::jlimit(1.0f, maxDelaySamples,
                                        currentDelaySamples * ratioL + wow);
        const float outL = delayLine.popSample(0, dlyL, true);

        float outR = 0.0f;
        if (stereo)
        {
            const float dlyR = juce::jlimit(1.0f, maxDelaySamples,
                                            currentDelaySamples * ratioR + wow);
            outR = delayLine.popSample(1, dlyR, true);
        }

        const float dryL = dataL[i];
        const float dryR = stereo ? dataR[i] : 0.0f;

        // Cross routing feeds each line from the OTHER channel's output; all
        // other modes feed each line from its own output.
        const float fbSrcL = doCross ? outR : outL;
        const float fbSrcR = doCross ? outL : outR;

        delayLine.pushSample(0, dryL + conditionFb(fbSrcL, dampFilterL, tapeHpL));
        if (stereo)
            delayLine.pushSample(1, dryR + conditionFb(fbSrcR, dampFilterR, tapeHpR));

        // Crossfade insert: dry vanishes as mix → 1.
        dataL[i] = dryL * dryGain + outL * wetMix;
        if (stereo)
            dataR[i] = dryR * dryGain + outR * wetMix;
    }

    // Check output magnitude — count as silent only when input is also silent
    float outMag = buffer.getMagnitude(0, numSamples);
    if (outMag < 1e-6f && inputSilent)
        ++silentOutputBlocks;
    else
        silentOutputBlocks = 0;
}

void T5ynthDelayLine::reset()
{
    delayLine.reset();
    dampFilterL.reset();
    dampFilterR.reset();
    tapeHpL.reset();
    tapeHpR.reset();
    wowPhase = 0.0f;
    silentOutputBlocks = 0;
}

void T5ynthDelayLine::setTime(float ms)
{
    delayTimeMs = ms;
    if (prepared)
        targetDelaySamples = juce::jlimit(1.0f, maxDelaySamples,
                                          static_cast<float>(ms * 0.001 * sr));
}

void T5ynthDelayLine::setFeedback(float fb)
{
    targetFeedback = juce::jlimit(0.0f, 0.95f, fb);
}

void T5ynthDelayLine::setMix(float mix)
{
    wetMix = juce::jlimit(0.0f, 1.0f, mix);
}

void T5ynthDelayLine::setDamp(float d)
{
    // Exponential mapping: 0 = bright (20kHz), 1 = dark (500Hz)
    // freq = 20000 * pow(500/20000, d)
    d = juce::jlimit(0.0f, 1.0f, d);
    dampFreq = 20000.0f * std::pow(500.0f / 20000.0f, d);
    if (prepared)
        updateDampCoeffs();
}

void T5ynthDelayLine::setMode(int delayType)
{
    mode = delayType;
}

void T5ynthDelayLine::updateDampCoeffs()
{
    // Butterworth LP (Q ≈ 0.707) at dampFreq
    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(sr, dampFreq, 0.707f);
    dampFilterL.coefficients = coeffs;
    dampFilterR.coefficients = coeffs;
}
