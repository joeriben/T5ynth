#include "AmpEffects.h"

namespace
{
    // Long enough that a per-block parameter step never zippers, short enough
    // that a knob still feels immediate. The delay/reverb crossfade in
    // PluginProcessor ramps across one block for the same reason; these run for
    // several, because drive and depth travel much further per unit of knob.
    constexpr double kSmoothSeconds = 0.02;

    // sin(120°) = sin(240°) negated. The three ensemble taps come out of one
    // sine and one cosine by angle addition rather than three calls to std::sin,
    // which also puts the 120° into the arithmetic where it can be read.
    constexpr double kSin120 = 0.86602540378443865;

    // ── the overdriven amplifier, and where each number comes from ──────────
    // Method and sources named before the first line, per the authoring rule.
    //
    // SAG and GHOST NOTES. Randall Aiken, „What is Sag?" and the technical Q&A
    // at aikenamps.com: a supply's own impedance means the rail voltage drops
    // with the current drawn, and the same impedance leaves ripple at TWICE the
    // mains frequency, because the supply is full-wave rectified. Aiken states
    // both halves of what players hear as sag — the droop that softens
    // transients, and the increased ripple that adds a low-frequency bloom —
    // and names ghost notes as the intermodulation of that ripple with the note
    // played, at sum and difference frequencies, i.e. not harmonically related.
    // The digital modelling of this class is reviewed in Pakarinen & Yeh, „A
    // Review of Digital Techniques for Modeling Vacuum-Tube Guitar Amplifiers",
    // Computer Music Journal 33(2), 2009, 85–100 (cited for the class; the full
    // text was not read for these constants).
    //
    // ASYMMETRY. The standard idiom, and one this project's own substrate
    // writes down: Csound's `distort1` (Hans Mikelson) carries SEPARATE shape
    // factors for the positive and negative half of the wave. A symmetric curve
    // has no even harmonics at all.
    // Two clipping thresholds rather than one offset. An ADDITIVE bias was
    // tried first and is worthless here: once the stage is driven hard the
    // offset is negligible against the signal and the curve is symmetric again
    // — measured, the even/odd balance went from the dry body's −1.0 dB to
    // −30.2, i.e. the even partials were GONE. Separate thresholds stay
    // asymmetric at every drive, which is what `distort1` does.
    constexpr float kClipPos = 0.70f;  // of the rail
    constexpr float kClipNeg = 1.30f;
    // The ripple rides the rail, and under hard clipping the output IS the
    // rail, so this is very nearly pure amplitude modulation and a percent is
    // already audible. At 0.25 it measured +3 dB OVER the fundamental on the
    // soft setting and +29.5 on the hard one — a mains hum, not a rumble.
    constexpr float kRipple = 0.04f;   // of the droop, so it only exists under load
    constexpr float kRippleHz = 100.0f;  // 50 Hz mains, full-wave rectified
    // A real supply droops; it does not collapse. Unbounded, the rail fell far
    // enough to squash the attack and then let the note come back LOUDER than
    // it started (+3.1 dB at 6 s), which is the opposite of sag.
    constexpr float kMaxDroop = 0.45f;
    constexpr double kLoadAttackSec  = 0.002;
    constexpr double kLoadReleaseSec = 0.150;
    constexpr double kDcBlockHz = 10.0;

    // The tremolo's two saturated-sine shapes and the constants that normalise
    // them back to ±1. At namespace scope, NOT function-local statics: a
    // function-local `static const double x = std::tanh(3.0)` is not folded —
    // clang emits a guard byte and takes libc++abi's static-init mutex on first
    // use, which here is the audio thread rendering the first Soft or Square
    // block, and leaves two acquire-loads inside the per-sample loop for ever
    // after. A lock on the audio thread, from two constants.
    constexpr double kTremShapeSoft = 3.0;    // the rounded square
    constexpr double kTremShapeHard = 12.0;   // the fast-edged one
    const double kTremNormSoft = std::tanh(kTremShapeSoft);
    const double kTremNormHard = std::tanh(kTremShapeHard);
}

// ── distortion ──────────────────────────────────────────────────────────────

void T5ynthDistortion::prepare(double sampleRate, int samplesPerBlock)
{
    sr_ = sampleRate;
    maxBlock_ = juce::jmax(1, samplesPerBlock);
    // 2x, the same factor the filter's own drive defaults to. tanh's first
    // significant image is at 2*fs-f, so one doubling puts it above the band for
    // everything this stage is asked to do; 4x would cost latency the rest of
    // the chain does not compensate for.
    os_ = std::make_unique<juce::dsp::Oversampling<float>>(
        2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false);
    os_->initProcessing(static_cast<size_t>(maxBlock_));
    raw_.setSize(2, maxBlock_, false, false, true);

    // Everything the supply model needs, at the OVERSAMPLED rate it runs at.
    const double osr = sampleRate * static_cast<double>(os_->getOversamplingFactor());
    attCoef_ = static_cast<float>(1.0 - std::exp(-1.0 / (kLoadAttackSec  * osr)));
    relCoef_ = static_cast<float>(1.0 - std::exp(-1.0 / (kLoadReleaseSec * osr)));
    ripInc_  = static_cast<float>(kRippleHz / osr);
    dcPole_  = static_cast<float>(1.0 - juce::MathConstants<double>::twoPi * kDcBlockHz / osr);
    load_ = 0.0f; ripPhase_ = 0.0f;
    dcX_[0] = dcX_[1] = dcY_[0] = dcY_[1] = 0.0f;

    for (auto* s : { &gain_, &wet_, &dryG_, &engage_ })
        s->reset(sampleRate, kSmoothSeconds);
    prepared_ = true;
    setDrive(driveDb_);
    setMix(mix_);
    gain_.setCurrentAndTargetValue(gain_.getTargetValue());
    wet_.setCurrentAndTargetValue(wet_.getTargetValue());
    dryG_.setCurrentAndTargetValue(dryG_.getTargetValue());
    engage_.setCurrentAndTargetValue(1.0f);
    // A host re-prepare while the stage was engaged must not skip the engage
    // crossfade: the oversampler is new and cold, and stepping into it measured
    // 0.258 at drive 0 and 0.913 at 36 dB against the signal's own 0.0144.
    running_ = false;
}

void T5ynthDistortion::reset()
{
    if (os_) os_->reset();
    load_ = 0.0f; ripPhase_ = 0.0f;
    dcX_[0] = dcX_[1] = dcY_[0] = dcY_[1] = 0.0f;
    for (auto* s : { &gain_, &wet_, &dryG_ })
        s->setCurrentAndTargetValue(s->getTargetValue());
    engage_.setCurrentAndTargetValue(1.0f);
    running_ = false;
}

void T5ynthDistortion::setDrive(float driveDb)
{
    driveDb_ = juce::jlimit(0.0f, 36.0f, driveDb);
    gain_.setTargetValue(juce::Decibels::decibelsToGain(driveDb_));
}

void T5ynthDistortion::setMix(float mix)
{
    mix_ = juce::jlimit(0.0f, 1.0f, mix);
    wet_.setTargetValue(mix_);
    dryG_.setTargetValue(1.0f - mix_);
}

void T5ynthDistortion::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (!prepared_ || !os_) return;

    const int numCh = buffer.getNumChannels();
    const int total = buffer.getNumSamples();
    if (numCh <= 0 || total <= 0) return;

    // The gate is SYMMETRIC, and both sides cross into the RAW input rather than
    // into the stage's own idea of dry. That is forced by the mix living inside
    // the oversampled domain: once the dry travels through the oversampler,
    // "arrived at dry" is the bypass signal delayed 2.34 samples, so switching
    // between them is a step whichever direction it goes — measured 0.317
    // engaging with stale filter state, 0.138 with fresh state, and 0.042
    // disengaging, against the signal's own largest step of 0.0144.
    if (wants() || moving())
    {
        if (!running_)
        {
            os_->reset();
            load_ = 0.0f;
            dcX_[0] = dcX_[1] = dcY_[0] = dcY_[1] = 0.0f;
            engage_.setCurrentAndTargetValue(0.0f);
            running_ = true;
        }
        engage_.setTargetValue(1.0f);
    }
    else
    {
        if (!running_) return;
        engage_.setTargetValue(0.0f);
        if (engage_.getCurrentValue() <= 0.0f) { running_ = false; return; }
    }
    const bool fadingIn = engage_.isSmoothing()
                       || engage_.getCurrentValue() < 0.9999f;

    // Oversampling was given a maximum block at prepare(); anything larger is
    // walked in prepared-size chunks rather than handed over whole.
    for (int start = 0; start < total; start += maxBlock_)
    {
        const int n = juce::jmin(maxBlock_, total - start);
        const int rawCh = juce::jmin(numCh, raw_.getNumChannels());
        if (fadingIn)
            for (int ch = 0; ch < rawCh; ++ch)
                raw_.copyFrom(ch, 0, buffer, ch, start, n);

        juce::dsp::AudioBlock<float> block(buffer.getArrayOfWritePointers(),
                                           static_cast<size_t>(numCh),
                                           static_cast<size_t>(start),
                                           static_cast<size_t>(n));
        auto up = os_->processSamplesUp(block);

        // The oversampled block is a different length, so the smoothed values
        // are stepped once per INPUT sample and held across the factor rather
        // than advanced per oversampled sample — otherwise the ramp would finish
        // twice as fast at 2x and the smoothing time would depend on the
        // oversampling factor. Verified: a drive step settles after 995 samples
        // where kSmoothSeconds * 48000 = 960.
        const auto factor = static_cast<int>(os_->getOversamplingFactor());
        const int upN = static_cast<int>(up.getNumSamples());
        // `up` carries the channel count the oversampler was BUILT with (2),
        // which on a mono bus is one channel of scratch that processSamplesDown
        // discards. Looping over the host's count instead halves the tanh work
        // there and changes nothing at two channels.
        const int upCh = juce::jmin(numCh, static_cast<int>(up.getNumChannels()));
        for (int i = 0; i < upN; ++i)
        {
            const bool step = (i % factor == 0);
            const float g = step ? gain_.getNextValue() : gain_.getCurrentValue();
            const float w = step ? wet_.getNextValue()  : wet_.getCurrentValue();
            const float d = step ? dryG_.getNextValue() : dryG_.getCurrentValue();

            // ── the supply ──────────────────────────────────────────────────
            // ONE power supply for the whole amplifier, so the load is the
            // largest channel and not each channel's own — that is what keeps
            // the sag coherent across the stereo image instead of two
            // independent wobbles.
            float peak = 0.0f;
            for (int ch = 0; ch < upCh; ++ch)
                peak = juce::jmax(peak, std::abs(up.getSample(ch, i) * g));
            load_ += (peak - load_) * (peak > load_ ? attCoef_ : relCoef_);

            // Sag: the rail droops with the current drawn. It is not a tone
            // control laid over the output — the rail IS the clipping
            // threshold, so when it drops the stage clips harder, and when the
            // note dies away it comes back and the clipping goes with it. That
            // is why this also answers „hell lang": the bark ends up in the
            // attack rather than standing over the whole note.
            // The droop follows the LOAD and nothing else. `load_` is already the
            // envelope of |x·g|, so the drive is in it once — an earlier version
            // also scaled the droop by the gain, and taking the drive twice bent
            // the control back on itself: out/in RMS peaked at +6.76 dB by 18 dB
            // of drive and fell to +4.01 at 36, which is exactly the
            // quieter-as-you-turn-it-up shape this file rules out, and a knob
            // driving two model inputs besides.
            const float droop = kMaxDroop * load_ / (1.0f + load_);
            // …and it carries the rectifier's ripple, at twice the mains
            // frequency because the supply is full-wave rectified. The
            // intermodulation of that ripple with the note is what players call
            // ghost notes, and it is only there while the supply is loaded.
            const float rip = droop * kRipple
                            * std::sin(juce::MathConstants<float>::twoPi * ripPhase_);
            ripPhase_ += ripInc_;
            if (ripPhase_ >= 1.0f) ripPhase_ -= 1.0f;
            const float rail = juce::jmax(0.05f, 1.0f - droop + rip);

            for (int ch = 0; ch < upCh; ++ch)
            {
                const float x = up.getSample(ch, i);
                // Asymmetric, because a symmetric curve has no even harmonics
                // at all and that is what „zu glatt" was: tanh is odd, so it
                // moved the even/odd balance by 0.6 dB over the dry body and
                // nothing else. The two thresholds are fractions of the RAIL, so
                // they travel with the sag rather than being two more things to
                // set.
                const float u = x * g;
                const float thr = rail * (u >= 0.0f ? kClipPos : kClipNeg);
                float y = thr * std::tanh(u / thr);
                // An asymmetric curve puts a DC offset under the note; 10 Hz
                // one-pole highpass takes it out and leaves the ripple, which is
                // an order of magnitude above it, where it is.
                const float hp = y - dcX_[ch] + dcPole_ * dcY_[ch];
                dcX_[ch] = y; dcY_[ch] = hp;

                // Dry and wet are summed HERE, at the oversampled rate, so both
                // halves see the same down-sampling filter and arrive together.
                // Mixing an undelayed dry against this path outside the
                // oversampler combs it: measured -23.3 dB at 10 kHz at mix 0.5.
                up.setSample(ch, i, hp * w + x * d);
            }
        }
        os_->processSamplesDown(block);

        if (fadingIn)
            for (int i = 0; i < n; ++i)
            {
                const float e = engage_.getNextValue();
                for (int ch = 0; ch < rawCh; ++ch)
                {
                    float* p = buffer.getWritePointer(ch) + start;
                    p[i] = p[i] * e + raw_.getReadPointer(ch)[i] * (1.0f - e);
                }
            }
    }
}

// ── tremolo ─────────────────────────────────────────────────────────────────

float T5ynthTremolo::shape(double phase01, int wave) noexcept
{
    // Wrapped here rather than by the caller: the stereo offset pushes channel 1
    // past 1.0, and every shape below needs its phase inside one period.
    double p = phase01 - std::floor(phase01);

    if (wave == Triangle)
    {
        // Quarter-turn so it rises through zero at p = 0, like the sine.
        const double q = p + 0.25 - std::floor(p + 0.25);
        return static_cast<float>(1.0 - 4.0 * std::abs(q - 0.5));
    }

    const double s = std::sin(juce::MathConstants<double>::twoPi * p);
    if (wave == SoftSquare || wave == Square)
    {
        // tanh(k·sin) / tanh(k): saturated sine, normalised back to ±1.
        const double k = (wave == Square) ? kTremShapeHard : kTremShapeSoft;
        const double norm = (wave == Square) ? kTremNormHard : kTremNormSoft;
        return static_cast<float>(std::tanh(k * s) / norm);
    }
    return static_cast<float>(s);
}

void T5ynthTremolo::prepare(double sampleRate, int)
{
    sr_ = sampleRate;
    phase_ = 0.0;
    depthS_.reset(sampleRate, kSmoothSeconds);
    stereoS_.reset(sampleRate, kSmoothSeconds);
    waveMixS_.reset(sampleRate, kSmoothSeconds);
    setRate(rateHz_);          // the rate the player last set, not a fixed one
    depthS_.setCurrentAndTargetValue(depth_);
    stereoS_.setCurrentAndTargetValue(stereo_);
    waveFrom_ = wave_;
    wavePending_ = wave_;
    waveMixS_.setCurrentAndTargetValue(1.0f);
}

void T5ynthTremolo::reset()
{
    phase_ = 0.0;
    depthS_.setCurrentAndTargetValue(depth_);
    stereoS_.setCurrentAndTargetValue(stereo_);
    waveFrom_ = wave_;
    wavePending_ = wave_;
    waveMixS_.setCurrentAndTargetValue(1.0f);
}

void T5ynthTremolo::setRate(float hz)
{
    rateHz_ = juce::jlimit(0.05f, 20.0f, hz);
    inc_ = rateHz_ / juce::jmax(sr_, 1.0);
}

void T5ynthTremolo::setDepth(float depth)
{
    depth_ = juce::jlimit(0.0f, 1.0f, depth);
    depthS_.setTargetValue(depth_);
}

void T5ynthTremolo::setStereo(float stereo)
{
    stereo_ = juce::jlimit(0.0f, 1.0f, stereo);
    stereoS_.setTargetValue(stereo_);
}

void T5ynthTremolo::setWave(int wave)
{
    // Only RECORDS the wish. processBlock starts the crossfade, and only when
    // the previous one has finished: restarting a fade mid-flight would snap the
    // blend back to its start, which is exactly the step the fade is there to
    // avoid. Clicking through all four cells therefore walks through them.
    wavePending_ = juce::jlimit(0, static_cast<int>(numWaves) - 1, wave);
}

void T5ynthTremolo::processBlock(juce::AudioBuffer<float>& buffer)
{
    // The phase must keep running even while the depth is on its way to zero,
    // which is why `wants()` is the caller's gate and this one only leaves when
    // there is genuinely nothing to do.
    if (!wants() && depthS_.getCurrentValue() <= 0.0001f) return;

    const int numCh = buffer.getNumChannels();
    const int n = buffer.getNumSamples();
    if (numCh <= 0 || n <= 0) return;

    if (wavePending_ != wave_ && ! waveMixS_.isSmoothing())
    {
        waveFrom_ = wave_;
        wave_     = wavePending_;
        waveMixS_.setCurrentAndTargetValue(0.0f);
        waveMixS_.setTargetValue(1.0f);
    }

    for (int i = 0; i < n; ++i)
    {
        const float d = depthS_.getNextValue();
        const float s = stereoS_.getNextValue();
        const float wm = waveMixS_.getNextValue();
        const bool morphing = wm < 0.9999f;
        const double ph = phase_;
        for (int ch = 0; ch < numCh; ++ch)
        {
            // Channel 1 is offset by half of `stereo` of a cycle: at 0 the two
            // move together (amplitude tremolo), at 1 they are in antiphase (a
            // pan). Anything between is the suitcase's shallow stereo sweep.
            const double off = (ch == 1) ? 0.5 * static_cast<double>(s) : 0.0;
            float lfo = shape(ph + off, wave_);
            if (morphing)                       // only while a shape change lasts
                lfo = shape(ph + off, waveFrom_) * (1.0f - wm) + lfo * wm;
            buffer.getWritePointer(ch)[i] *= 1.0f - d * 0.5f * (1.0f - lfo);
        }
        phase_ += inc_;
        if (phase_ >= 1.0) phase_ -= 1.0;
    }
}

// ── chorus ──────────────────────────────────────────────────────────────────

void T5ynthChorus::prepare(double sampleRate, int samplesPerBlock)
{
    sr_ = sampleRate;

    // Sized from the geometry rather than from a round number, so the allocation
    // and the clamp in processBlock cannot drift apart: the deepest tap, plus the
    // four samples the cubic interpolator reads past it.
    const int maxDelay =
        static_cast<int>(std::ceil((kCentreMs + kSweepMs) * 0.001 * sampleRate)) + 4;
    delay_.setMaximumDelayInSamples(maxDelay);
    juce::dsp::ProcessSpec spec { sampleRate,
                                  static_cast<juce::uint32>(juce::jmax(1, samplesPerBlock)), 2 };
    delay_.prepare(spec);

    centreS_ = static_cast<float>(kCentreMs * 0.001 * sampleRate);
    sweepS_  = static_cast<float>(kSweepMs  * 0.001 * sampleRate);
    maxS_    = static_cast<float>(delay_.getMaximumDelayInSamples());
    fillLen_ = maxDelay;

    depthS_.reset(sampleRate, kSmoothSeconds);
    mixS_.reset(sampleRate, kSmoothSeconds);
    depthS_.setCurrentAndTargetValue(depth_);

    phase_ = 0.0;
    setRate(rateHz_);
    prepared_ = true;
    lastNumCh_ = 2;          // what the spec above prepared for
    armFill();
}

void T5ynthChorus::reset()
{
    if (!prepared_) return;
    delay_.reset();
    phase_ = 0.0;
    depthS_.setCurrentAndTargetValue(depth_);
    armFill();
}

void T5ynthChorus::armFill()
{
    // The line is empty, so the only truthful state is DRY, whatever the mix
    // reads. Parking the smoother AT `mix_` instead would put a step exactly
    // where the fill ends: `getNextValue()` on an arrived smoother returns the
    // target at once, so the coefficient would jump 0 → mix in one sample —
    // measured 0.14615 at +456 samples against the signal's own 0.01443, on
    // every re-`prepareToPlay` (sample-rate or buffer change) with the chorus
    // engaged. The processor re-sends the SAME mix on the next block and
    // `setTargetValue` early-returns on an unchanged target, so nothing else
    // would re-arm the ramp either.
    idle_ = true;
    fill_ = fillLen_;
    mixS_.setCurrentAndTargetValue(0.0f);
    // ONLY if the stage will actually run. Arming unconditionally makes the
    // idle gate chatter for ever at any mix inside (0, 0.0001]: `wants()` reads
    // the RAW mix and says no, but `setTargetValue` on a DIFFERENT target
    // restarts the smoother, so `isSmoothing()` says yes on the very next block
    // — the gate falls through, the stage runs a smoothing window, parks,
    // clears the line, and re-arms, for ever. Measured at 48 kHz / 512:
    // 16.4 us/block at every mix in that interval against 2.50 parked and
    // 22.3 fully engaged, i.e. a stage the code reports as bypassed at three
    // quarters of the cost of a working one, refilling its delay line 31 times
    // a second. Reachable because the value the processor reads is continuous:
    // NormalisableRange::convertFrom0to1 does not apply the parameter's 0.01
    // interval, so host automation and a state restore both land there.
    // Below the threshold the only truthful mix is dry, and leaving the
    // smoother arrived at 0 is what lets the gate latch.
    if (wants()) mixS_.setTargetValue(mix_);
}

void T5ynthChorus::setRate(float hz)
{
    rateHz_ = juce::jlimit(0.05f, 10.0f, hz);
    inc_ = static_cast<double>(rateHz_) / sr_;   // phase is in turns, not radians
}

void T5ynthChorus::setDepth(float depth)
{
    // Smoothed, not stepped: this scales the delay itself, so a per-block step
    // would move every tap by up to the full sweep between two samples.
    depth_ = juce::jlimit(0.0f, 1.0f, depth);
    depthS_.setTargetValue(depth_);
}

void T5ynthChorus::setMix(float mix)
{
    mix_ = juce::jlimit(0.0f, 1.0f, mix);
    mixS_.setTargetValue(mix_);
}

void T5ynthChorus::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (!prepared_) return;

    // The gate is on the SMOOTHED value, per this file's contract: the stage
    // keeps running until its own mix ramp has arrived at dry, never stopping on
    // the raw target.
    if (!wants() && !mixS_.isSmoothing())
    {
        // Arrived. Clear the line ONCE — it still holds whatever was in it when
        // the mix reached zero, and re-engaging a minute later would otherwise
        // fade in a fragment of a note that is long gone. A memset of a few kB
        // on the transition only, so the shut gate still costs nothing per block.
        if (!idle_) { delay_.reset(); armFill(); }
        return;
    }

    const int numCh = juce::jmin(2, buffer.getNumChannels());   // prepared for two;
                                                                //   channels past the
                                                                //   second pass through
    const int n = buffer.getNumSamples();
    if (numCh <= 0 || n <= 0) return;   // before idle_ moves: an empty block feeds nothing

    // A channel count that changed under us leaves the other line unfed and its
    // contents stale, so it is refilled rather than mixed in. Not reachable
    // through a JUCE host, which re-prepares on a bus change; reachable from a
    // tool that hands this stage mono and then stereo buffers.
    if (numCh != lastNumCh_) { delay_.reset(); armFill(); lastNumCh_ = numCh; }
    idle_ = false;

    float* ch[2] { buffer.getWritePointer(0),
                   numCh > 1 ? buffer.getWritePointer(1) : nullptr };

    for (int i = 0; i < n; ++i)
    {
        const float d = depthS_.getNextValue();
        // A CLEARED line reads zeros until it has been fed for as long as its
        // deepest tap, and the sample where a tap crosses that boundary is a
        // step — measured, an isolated 0.02357 exactly 4.96 ms after engaging,
        // against a neighbourhood of 0.0005. So the mix ramp does not start
        // until the line is full: the taps are read and thrown away for those
        // 9.4 ms, and the output is the dry it already was. The gate opens
        // ~10 ms later than it used to and does not step at all.
        const bool filling = fill_ > 0;
        const float m = filling ? 0.0f : mixS_.getNextValue();
        if (filling) --fill_;

        // One LFO, three phases 120° apart — the brigade. Both channels read the
        // same three taps; what differs is WHICH two of them each one sums.
        const double a  = juce::MathConstants<double>::twoPi * phase_;
        const double sa = std::sin(a), ca = std::cos(a);
        const double lfo[kTaps] { -0.5 * sa - kSin120 * ca,   // a − 120°
                                   sa,                        // a
                                  -0.5 * sa + kSin120 * ca }; // a + 120°

        float tau[kTaps];
        for (int t = 0; t < kTaps; ++t)
            tau[t] = juce::jlimit(1.0f, maxS_,
                                  centreS_ + sweepS_ * d * static_cast<float>(lfo[t]));

        float wet[2] { 0.0f, 0.0f };
        for (int c = 0; c < numCh; ++c)
        {
            delay_.pushSample(c, ch[c][i]);
            float tap[kTaps];
            for (int t = 0; t < kTaps; ++t)   // the read pointer advances on the last one only
                tap[t] = delay_.popSample(c, tau[t], t == kTaps - 1);

            // Left takes −120° and 0°, right 0° and +120°: the middle phase is
            // common so the image keeps a centre, the outer two are split so the
            // sides decorrelate. In mono there is no side to split, so all three
            // are summed and what is left is the thickening alone.
            wet[c] = numCh > 1 ? (c == 0 ? 0.5f * (tap[0] + tap[1])
                                         : 0.5f * (tap[1] + tap[2]))
                               : (tap[0] + tap[1] + tap[2]) * (1.0f / kTaps);
        }

        for (int c = 0; c < numCh; ++c)
            ch[c][i] += m * (wet[c] - ch[c][i]);   // linear, so mix 0.5 is the deepest comb

        phase_ += inc_;
        if (phase_ >= 1.0) phase_ -= 1.0;
    }
}

// ── phaser ──────────────────────────────────────────────────────────────────

void T5ynthPhaser::prepare(double sampleRate, int samplesPerBlock)
{
    sr_ = sampleRate;
    const juce::dsp::ProcessSpec spec1ch { sampleRate,
        static_cast<juce::uint32>(juce::jmax(1, samplesPerBlock)), 1u };
    for (auto& ch : apState_) for (auto& s : ch) s = 0.0f;
    for (auto& f : fbHp_)
    {
        f.setType(juce::dsp::FirstOrderTPTFilterType::highpass);
        f.prepare(spec1ch);
        f.setCutoffFrequency(kFbHighpassHz);   // fixed, so no per-sample tan
    }
    fbState_[0] = fbState_[1] = 0.0f;
    phase_ = 0.0;
    setRate(rateHz_);

    depthS_.reset(sampleRate, kSmoothSeconds);
    fbS_.reset(sampleRate, kSmoothSeconds);
    mixS_.reset(sampleRate, kSmoothSeconds);
    // Sane fallback only — the actual guarantee against a phantom-default
    // ramp is the "first call after prepare" latch in setDepth()/setFeedback()
    // below, which fires on whatever the processor calls next regardless of
    // what depth_/fb_ happen to hold here (the constructor default on a cold
    // start, or the last live knob position across a re-prepare).
    depthS_.setCurrentAndTargetValue(depth_);
    fbS_.setCurrentAndTargetValue(fb_);
    depthSeeded_ = false;
    fbSeeded_ = false;

    fadeInc_ = static_cast<float>(1.0 / juce::jmax(1.0, sampleRate * kSmoothSeconds));
    prepared_ = true;
    armMix();
}

void T5ynthPhaser::reset()
{
    if (!prepared_) return;
    for (auto& ch : apState_) for (auto& s : ch) s = 0.0f;
    for (auto& f : fbHp_) f.reset();
    fbState_[0] = fbState_[1] = 0.0f;
    phase_ = 0.0;
    armMix();
}

void T5ynthPhaser::armMix()
{
    idle_ = true;
    mixS_.setCurrentAndTargetValue(0.0f);
    // ONLY if the stage will actually run — the same guard, and the same
    // reason, as `T5ynthChorus::armFill()` above, whose comment carries the
    // measurement. Here the chatter costs 49.3 us/block against 2.43 parked,
    // and clears twelve sections and both feedback taps on every cycle.
    if (wants()) mixS_.setTargetValue(mix_);
    // A fresh engage ramps the mix itself from zero, which is already the fade
    // any cold filter needs; the per-channel one is only for the case the mix
    // ramp does NOT cover, a single channel appearing mid-signal.
    wetGain_[0] = wetGain_[1] = 1.0f;
    // Every channel's state was just cleared, so none of them is the "newly
    // present" case the processBlock loop exists for. Saying so here rather
    // than only in prepare() is what keeps a stage that parked while mono from
    // fading channel 1's wet in a second time on the next stereo engage, which
    // would put its wet at mix^2 against channel 0's mix — measured as a worst
    // |L-R| of 0.15222 on identical input, against the stage's own width of
    // 0.06747.
    lastNumCh_ = 2;
}

void T5ynthPhaser::setRate(float hz)
{
    rateHz_ = juce::jlimit(0.02f, 10.0f, hz);
    inc_ = static_cast<double>(rateHz_) / juce::jmax(sr_, 1.0);   // phase is in turns
}

void T5ynthPhaser::setDepth(float depth)
{
    depth_ = juce::jlimit(0.0f, 1.0f, depth);
    if (!depthSeeded_) { depthS_.setCurrentAndTargetValue(depth_); depthSeeded_ = true; }
    else depthS_.setTargetValue(depth_);
}

void T5ynthPhaser::setFeedback(float fb)
{
    fb_ = juce::jlimit(-0.95f, 0.95f, fb);
    if (!fbSeeded_) { fbS_.setCurrentAndTargetValue(fb_); fbSeeded_ = true; }
    else fbS_.setTargetValue(fb_);
}

void T5ynthPhaser::setMix(float mix)
{
    mix_ = juce::jlimit(0.0f, 1.0f, mix);
    mixS_.setTargetValue(mix_);
}

void T5ynthPhaser::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (!prepared_) return;

    // The gate is on the SMOOTHED mix, per this file's contract: the stage
    // keeps running until its own ramp has arrived at dry, never stopping on
    // the raw target.
    if (!wants() && !mixS_.isSmoothing())
    {
        // Arrived. Clear ONCE — the twelve sections' own state and the two
        // feedback taps, which would otherwise still hold whatever was in
        // them when the mix reached zero. That also protects against the
        // same NaN latch the widget this replaces had (its `lastOutput`
        // feedback memory: NaN * 0 is still NaN, so a feedback of zero does
        // not clear a poisoned sample by itself) — `fbState_` is exactly that
        // memory here, and is zeroed in the same sweep as the filters.
        if (!idle_)
        {
            for (auto& ch : apState_) for (auto& s : ch) s = 0.0f;
            for (auto& f : fbHp_) f.reset();
            fbState_[0] = fbState_[1] = 0.0f;
            armMix();
        }
        return;
    }

    const int numCh = juce::jmin(2, buffer.getNumChannels());   // prepared for two;
                                                                //   channels past the
                                                                //   second pass through
    const int n = buffer.getNumSamples();
    if (numCh <= 0 || n <= 0)
    {
        // A buffer with NO channels is a gap for channel 0 as much as for
        // channel 1, and returning without recording it gave `lastNumCh_` a
        // floor of 1 — so the loop below could never reach c == 0 and a
        // channel-0 gap came back unfaded with its pre-gap state, measured at
        // 0.52092 against the source sine's own 0.01440. A zero-LENGTH block
        // is not that: its channels are still there and one sample behind.
        if (numCh <= 0) lastNumCh_ = 0;
        return;   // before idle_ moves: an empty block feeds nothing
    }

    // A channel that stopped being fed and comes back still holds the signal it
    // stopped on — six filters and a feedback tap, from however long ago — and
    // hands it straight to the output. So a channel index that is newly present
    // is cleared, and ONLY that one. Not reachable through a JUCE host, which
    // re-prepares on a bus change; reachable from a tool that hands this stage
    // mono and then stereo buffers, and measured there against the 220 Hz sine's
    // own largest step of 0.01453: a returning channel stepped 0.24447 at a
    // neighbourhood ratio of 4.2, a fresh second channel 0.18334 at 4.8. The
    // clear alone takes the first to 0.04799 and leaves the second untouched —
    // its state was zero already, so what is left in both is not stale content
    // but the cold start of a six-stage all-pass chain handed a live signal
    // mid-waveform. Hence `wetGain_`: the wet fade the mix ramp performs at
    // every ordinary engage, done per channel for the one case the shared mix
    // ramp cannot cover. With it, 0.01452 at ratio 1.02 and 0.01992 at 1.01 —
    // the sine's own step, and slope rather than a step.
    //
    // This is where the phaser DIVERGES from T5ynthChorus's guard rather than
    // copying it, and the divergence is the measurement's, not a preference:
    // the chorus holds ONE delay line that both channels read, so a channel
    // change invalidates the whole line and `armFill()` is right there. Every
    // piece of state here is per channel. Clearing the surviving channel too,
    // and re-arming the shared mix with it, puts a 0.59093 step (ratio 300) on
    // a channel that never lost its input — measured, after writing exactly
    // that. A shrinking count therefore clears nothing at all: the channel that
    // remains is continuous and must stay so, and the one that left is cleared
    // when and if it returns.
    for (int c = lastNumCh_; c < numCh; ++c)
    {
        for (auto& s : apState_[c]) s = 0.0f;
        fbHp_[c].reset();
        fbState_[c] = 0.0f;
        wetGain_[c] = 0.0f;   // and fade its wet in, below
    }
    lastNumCh_ = numCh;
    idle_ = false;

    float* ch[2] { buffer.getWritePointer(0),
                   numCh > 1 ? buffer.getWritePointer(1) : nullptr };
    const float nyqCap = static_cast<float>(0.49 * sr_);

    for (int i = 0; i < n; ++i)
    {
        const float d  = depthS_.getNextValue();
        const float fb = fbS_.getNextValue();
        const float m  = mixS_.getNextValue();

        // One LFO, read 90 degrees apart by the two channels — sin and cos of
        // the SAME phase, so no second oscillator and no extra call to
        // std::sin is needed for the offset (class comment, ONE LFO).
        const double a = juce::MathConstants<double>::twoPi * phase_;
        const float lfoL = static_cast<float>(std::sin(a));
        const float lfoR = static_cast<float>(std::cos(a));

        const float spanOct = kSpanOctaves * d;
        const float cornerL = juce::jlimit(20.0f, nyqCap,
                                            kCentreHz * std::pow(2.0f, spanOct * lfoL));
        const float cornerR = juce::jlimit(20.0f, nyqCap,
                                            kCentreHz * std::pow(2.0f, spanOct * lfoR));

        for (int c = 0; c < numCh; ++c)
        {
            // ONE COEFFICIENT for all six sections of this channel: they sweep
            // to the same corner by construction, so the transform that costs
            // anything — a `std::tan` — is done once here rather than six
            // times. The arithmetic below is `juce::dsp::FirstOrderTPTFilter`'s
            // own, transcribed rather than reinterpreted, so the sections stay
            // bit-identical to the ones this stage used before (verified: max
            // absolute difference 0.0 over the whole sweep).
            const float corner = (c == 1) ? cornerR : cornerL;
            const float g = static_cast<float>(
                std::tan(juce::MathConstants<double>::pi * corner / sr_));
            const float G = g / (1.0f + g);

            const float x = ch[c][i];
            // ADDED, not subtracted like juce::dsp::Phaser's own loop — the
            // sign that puts the resonance on POSITIVE feedback rather than
            // negative (class comment, FEEDBACK).
            float y = x + fb * fbState_[c];
            for (int s = 0; s < kStages; ++s)
            {
                float& s1 = apState_[c][s];
                const float v  = G * (y - s1);
                const float lp = v + s1;
                s1 = lp + v;
                y  = 2.0f * lp - y;     // allpass = 2*lowpass - input
            }
            // AC-COUPLED, and that is not decoration: six all-passes have phase
            // 0 at DC, so an ADDED loop is a DC amplifier of exactly 1/(1-fb) —
            // measured x1.43 / x2.50 / x20.0 at fb +0.3 / +0.6 / +0.95, i.e.
            // +19.5 dB at 2 Hz and +8.1 dB at 20 Hz, crossing unity only at
            // ~55 Hz. The widget this replaces got its DC attenuation for free
            // from the very subtraction that put its resonance on the wrong
            // knob half, so blocking DC in the loop is the price of fixing the
            // knob rather than a separate idea. One pole at 20 Hz: the audible
            // resonance lives at 150-2400 Hz where this is unity, and the
            // distortion in this same file blocks DC for the same reason.
            fbState_[c] = fbHp_[c].processSample(0, y);

            // wetGain_ is 1 except while a channel that just appeared fades its
            // own wet in — arithmetic rather than a branch, so the steady state
            // pays one multiply and never a misprediction.
            wetGain_[c] = juce::jmin(1.0f, wetGain_[c] + fadeInc_);
            ch[c][i] = x + m * wetGain_[c] * (y - x);   // linear, so mix 0.5 sums dry and wet equally
        }

        phase_ += inc_;
        if (phase_ >= 1.0) phase_ -= 1.0;
    }
}
