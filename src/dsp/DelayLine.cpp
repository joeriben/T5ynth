#include "DelayLine.h"

namespace
{
    // Tape character (Tape2/Tape3) — all fixed/auto, no user parameter.
    constexpr float  kWowHz      = 0.7f;     // slow pitch drift
    constexpr float  kFlut1Hz    = 6.3f;     // flutter: two incommensurate sines
    constexpr float  kFlut2Hz    = 9.7f;     //   summed = organic, non-repeating feel
    constexpr float  kWowDepth   = 0.0030f;  // * delay-samples
    constexpr float  kFlutDepth  = 0.0015f;  // * delay-samples, per flutter sine
    constexpr float  kTapeDrive  = 1.6f;     // soft-sat pre-gain (auto-limiting)
    constexpr float  kTapeHpHz   = 100.0f;   // high-pass in the feedback loop
    constexpr float  kPanWidth   = 0.75f;    // multi-head stereo spread (0..1)

    // Buffer capacity headroom: holds the longest single tap (the 5 s processor
    // clamp) and Tape3's 3rd head; the tape base is capped to keep 3·base inside.
    constexpr double kBufferSeconds = 6.2;

    inline float onePoleCoeff(double fc, double sr)
    {
        return 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi
                               * static_cast<float>(fc / sr));
    }
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
    // high sample rates and under modulation toward the processor's 5 s clamp.
    // setMaximumDelayInSamples reallocates using the channel count from prepare().
    const int capacity = static_cast<int>(std::ceil(kBufferSeconds * sampleRate))
                       + samplesPerBlock + 4;
    delayLine.setMaximumDelayInSamples(capacity);
    maxDelaySamples = static_cast<float>(capacity - samplesPerBlock - 2);

    currentDelaySamples = juce::jlimit(1.0f, maxDelaySamples,
                                       static_cast<float>(delayTimeMs * 0.001 * sr));
    targetDelaySamples = currentDelaySamples;
    delayLine.setDelay(currentDelaySamples);
    targetFeedback = feedback;

    updateDampCoeffs();
    // Settle each filter's internal order off-thread to match the assigned
    // 2nd-order coefficients. Assigning .coefficients is only a pointer swap and
    // does NOT update Filter::order, so without this the first audio-thread
    // processSample() would trip Filter::check() → reset() → malloc (the #1
    // BLOCKING bug class). prepareToPlay calls prepare() but never reset().
    dampFilterL.reset();
    dampFilterR.reset();

    tapeHpState = 0.0f;
    wowPhase = flut1Phase = flut2Phase = 0.0f;

    prepared = true;
}

void T5ynthDelayLine::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (!prepared || wetMix == 0.0f)
        return;

    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0 || buffer.getNumChannels() < 1)
        return;

    // Silence detection: skip only after output has truly decayed.
    const float inMag = buffer.getMagnitude(0, numSamples);
    const bool inputSilent = inMag < 1e-6f;
    if (inputSilent && silentOutputBlocks > SILENCE_CONFIRM_BLOCKS)
        return;

    const bool stereo = buffer.getNumChannels() >= 2;
    const float dryGain = 1.0f - wetMix;
    const float smoothCoeff = 1.0f - std::exp(-1.0f / static_cast<float>(sr * 0.005));

    const int  heads = (mode == kTape3) ? 3 : (mode == kTape2) ? 2 : 1;
    const bool isTape = (mode == kTape2 || mode == kTape3);
    const bool isPingPong = (mode == kPingPong) && stereo;

    // Constant-power pan gains for tape heads, spread evenly across [-w, +w].
    // On a mono buffer the defaults (panL=1, panR=0) leave the head sum flat so
    // mono fold-down keeps full level — the spread only applies in true stereo.
    float panL[3] = { 1.0f, 1.0f, 1.0f };
    float panR[3] = { 0.0f, 0.0f, 0.0f };
    if (isTape && stereo)
    {
        for (int k = 0; k < heads; ++k)
        {
            const float p = (heads == 1) ? 0.0f
                          : -kPanWidth + (2.0f * kPanWidth) * static_cast<float>(k)
                            / static_cast<float>(heads - 1);
            const float t = (p + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            panL[k] = std::cos(t);
            panR[k] = std::sin(t);
        }
    }

    const float twoPi = juce::MathConstants<float>::twoPi;
    const float dWow = twoPi * kWowHz   / static_cast<float>(sr);
    const float dF1  = twoPi * kFlut1Hz / static_cast<float>(sr);
    const float dF2  = twoPi * kFlut2Hz / static_cast<float>(sr);
    const float aHP  = onePoleCoeff(kTapeHpHz, sr);

    auto* dataL = buffer.getWritePointer(0);
    auto* dataR = stereo ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        // Smooth delay time and feedback per-sample to avoid clicks.
        currentDelaySamples += (targetDelaySamples - currentDelaySamples) * smoothCoeff;
        feedback += (targetFeedback - feedback) * smoothCoeff;

        const float dryL = dataL[i];
        const float dryR = stereo ? dataR[i] : 0.0f;
        const float clampedT = juce::jlimit(1.0f, maxDelaySamples, currentDelaySamples);

        if (isTape)
        {
            const float mono = stereo ? 0.5f * (dryL + dryR) : dryL;

            wowPhase += dWow; flut1Phase += dF1; flut2Phase += dF2;
            if (wowPhase   > twoPi) wowPhase   -= twoPi;
            if (flut1Phase > twoPi) flut1Phase -= twoPi;
            if (flut2Phase > twoPi) flut2Phase -= twoPi;
            const float modw = (std::sin(wowPhase) * kWowDepth
                              + (std::sin(flut1Phase) + std::sin(flut2Phase)) * kFlutDepth)
                              * currentDelaySamples;

            // Cap the head spacing so the longest head (heads·base) stays inside
            // the buffer even at long times.
            const float base = juce::jmin(currentDelaySamples,
                                          maxDelaySamples / static_cast<float>(heads));

            float wetL = 0.0f, wetR = 0.0f, longest = 0.0f;
            for (int k = 0; k < heads; ++k)
            {
                const float d = juce::jlimit(1.0f, maxDelaySamples,
                                             static_cast<float>(k + 1) * base + modw);
                const bool last = (k == heads - 1);
                // Advance the read pointer exactly once per sample, on the last tap.
                const float tap = delayLine.popSample(0, d, last);
                wetL += tap * panL[k];
                wetR += tap * panR[k];
                if (last) longest = tap;
            }

            // Feedback from the long head: high-pass → damping low-pass → soft
            // saturation. tanh(x·drive)/drive keeps small-signal gain ≈ 1 so the
            // feedback feel is unchanged, while hot signals self-limit (tape).
            float f = longest * feedback;
            tapeHpState += aHP * (f - tapeHpState);
            f = f - tapeHpState;
            f = dampFilterL.processSample(f);
            f = std::tanh(f * kTapeDrive) / kTapeDrive;
            delayLine.pushSample(0, mono + f);

            dataL[i] = dryL * dryGain + wetL * wetMix;
            if (stereo)
                dataR[i] = dryR * dryGain + wetR * wetMix;
        }
        else if (isPingPong)
        {
            const float mono = 0.5f * (dryL + dryR);
            const float dl = delayLine.popSample(0, clampedT, true);
            const float dr = delayLine.popSample(1, clampedT, true);

            // Cross-coupled feedback: each line is fed from the OTHER line's
            // output; the (mono) dry is injected into the left line only.
            const float fbToL = dampFilterL.processSample(dr * feedback);
            const float fbToR = dampFilterR.processSample(dl * feedback);
            delayLine.pushSample(0, mono + fbToL);
            delayLine.pushSample(1, fbToR);

            dataL[i] = dryL * dryGain + dl * wetMix;
            dataR[i] = dryR * dryGain + dr * wetMix;
        }
        else // Digital (dual-mono); also the mono fallback for PingPong
        {
            const float dl = delayLine.popSample(0, clampedT, true);
            const float fbL = dampFilterL.processSample(dl * feedback);
            delayLine.pushSample(0, dryL + fbL);
            dataL[i] = dryL * dryGain + dl * wetMix;

            if (stereo)
            {
                const float dr = delayLine.popSample(1, clampedT, true);
                const float fbR = dampFilterR.processSample(dr * feedback);
                delayLine.pushSample(1, dryR + fbR);
                dataR[i] = dryR * dryGain + dr * wetMix;
            }
        }
    }

    // Count as silent only when input is also silent.
    const float outMag = buffer.getMagnitude(0, numSamples);
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
    tapeHpState = 0.0f;
    wowPhase = flut1Phase = flut2Phase = 0.0f;
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
    const float newFreq = 20000.0f * std::pow(500.0f / 20000.0f, d);

    // The processor calls setDamp() every block with the (usually static) damp
    // param, and updateDampCoeffs() runs makeLowPass() = a heap allocation. Skip
    // when the resolved frequency is identical (deterministic for a steady param,
    // so the equality check is exact and output-identical) — coefficients are
    // rebuilt only when the knob actually moves, never on an idle block.
    if (newFreq == dampFreq)
        return;

    dampFreq = newFreq;
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
