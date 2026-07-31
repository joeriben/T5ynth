#include "AmpEffects.h"

namespace
{
    // Long enough that a per-block parameter step never zippers, short enough
    // that a knob still feels immediate. The delay/reverb crossfade in
    // PluginProcessor ramps across one block for the same reason; these run for
    // several, because drive and depth travel much further per unit of knob.
    constexpr double kSmoothSeconds = 0.02;

    // juce::dsp::DryWetMixer resets its gain smoothers to 0.05 s
    // (juce_DryWetMixer.cpp), and Chorus and Phaser both own one. After their
    // mix is turned to zero they therefore need that long to actually ARRIVE at
    // dry; stopping sooner is the click this margin exists to avoid.
    constexpr double kWidgetFlushSeconds = 0.06;
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

    for (auto* s : { &gain_, &wet_, &dryG_, &engage_ })
        s->reset(sampleRate, kSmoothSeconds);
    prepared_ = true;
    setDrive(driveDb_);
    setMix(mix_);
    gain_.setCurrentAndTargetValue(gain_.getTargetValue());
    wet_.setCurrentAndTargetValue(wet_.getTargetValue());
    dryG_.setCurrentAndTargetValue(dryG_.getTargetValue());
    engage_.setCurrentAndTargetValue(1.0f);
}

void T5ynthDistortion::reset()
{
    if (os_) os_->reset();
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
    if (!wants() && !moving() && !engage_.isSmoothing()) { running_ = false; return; }

    const int numCh = buffer.getNumChannels();
    const int total = buffer.getNumSamples();
    if (numCh <= 0 || total <= 0) return;

    // Re-engaging after a bypass, the oversampler's polyphase allpass chains
    // still hold the signal from whenever it last ran — a different phase of the
    // same note, mixed into the first samples of the new one: a 0.317 step
    // against the signal's own 0.0144. Zeroing them leaves the cold-filter
    // warm-up instead, 0.138, so the first 20 ms are also crossfaded in from the
    // raw input.
    if (!running_)
    {
        os_->reset();
        engage_.setCurrentAndTargetValue(0.0f);
        engage_.setTargetValue(1.0f);
        running_ = true;
    }
    const bool fadingIn = engage_.isSmoothing();

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
            for (int ch = 0; ch < upCh; ++ch)
            {
                const float x = up.getSample(ch, i);
                // Dry and wet are summed HERE, at the oversampled rate, so both
                // halves see the same down-sampling filter and arrive together.
                // Mixing an undelayed dry against this path outside the
                // oversampler combs it: measured -23.3 dB at 10 kHz at mix 0.5.
                up.setSample(ch, i, std::tanh(x * g) * w + x * d);
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

void T5ynthTremolo::prepare(double sampleRate, int)
{
    sr_ = sampleRate;
    phase_ = 0.0;
    depthS_.reset(sampleRate, kSmoothSeconds);
    stereoS_.reset(sampleRate, kSmoothSeconds);
    setRate(rateHz_);          // the rate the player last set, not a fixed one
    depthS_.setCurrentAndTargetValue(depth_);
    stereoS_.setCurrentAndTargetValue(stereo_);
}

void T5ynthTremolo::reset()
{
    phase_ = 0.0;
    depthS_.setCurrentAndTargetValue(depth_);
    stereoS_.setCurrentAndTargetValue(stereo_);
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

void T5ynthTremolo::processBlock(juce::AudioBuffer<float>& buffer)
{
    // The phase must keep running even while the depth is on its way to zero,
    // which is why `wants()` is the caller's gate and this one only leaves when
    // there is genuinely nothing to do.
    if (!wants() && depthS_.getCurrentValue() <= 0.0001f) return;

    const int numCh = buffer.getNumChannels();
    const int n = buffer.getNumSamples();
    if (numCh <= 0 || n <= 0) return;

    for (int i = 0; i < n; ++i)
    {
        const float d = depthS_.getNextValue();
        const float s = stereoS_.getNextValue();
        const double ph = phase_;
        for (int ch = 0; ch < numCh; ++ch)
        {
            // Channel 1 is offset by half of `stereo` of a cycle: at 0 the two
            // move together (amplitude tremolo), at 1 they are in antiphase (a
            // pan). Anything between is the suitcase's shallow stereo sweep.
            const double off = (ch == 1) ? 0.5 * static_cast<double>(s) : 0.0;
            const float lfo = static_cast<float>(
                std::sin(juce::MathConstants<double>::twoPi * (ph + off)));
            buffer.getWritePointer(ch)[i] *= 1.0f - d * 0.5f * (1.0f - lfo);
        }
        phase_ += inc_;
        if (phase_ >= 1.0) phase_ -= 1.0;
    }
}

// ── chorus ──────────────────────────────────────────────────────────────────

void T5ynthChorus::prepare(double sampleRate, int samplesPerBlock)
{
    maxBlock_ = juce::jmax(1, samplesPerBlock);
    juce::dsp::ProcessSpec spec { sampleRate,
                                  static_cast<juce::uint32>(maxBlock_), 2 };
    chorus_.prepare(spec);
    chorus_.setCentreDelay(7.0f);   // ms; the middle of juce::dsp::Chorus's range
    chorus_.setFeedback(0.0f);      // a chorus, not a flanger
    chorus_.setMix(mix_);
    flushLen_ = static_cast<int>(kWidgetFlushSeconds * sampleRate);
    flush_ = 0;
    prepared_ = true;
}

void T5ynthChorus::reset() { if (prepared_) chorus_.reset(); flush_ = 0; }

void T5ynthChorus::setRate(float hz)      { chorus_.setRate(juce::jlimit(0.05f, 10.0f, hz)); }
void T5ynthChorus::setDepth(float depth)  { chorus_.setDepth(juce::jlimit(0.0f, 1.0f, depth)); }

void T5ynthChorus::setMix(float mix)
{
    const float m = juce::jlimit(0.0f, 1.0f, mix);
    // Turning it off arms the flush: the wrapped DryWetMixer still has to ramp
    // its way down to dry, and stopping before it arrives is a step.
    if (m <= 0.0001f && mix_ > 0.0001f) flush_ = flushLen_;
    mix_ = m;
    chorus_.setMix(mix_);
}

void T5ynthChorus::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (!prepared_) return;
    if (!wants() && flush_ <= 0) return;

    const int numCh = buffer.getNumChannels();
    const int total = buffer.getNumSamples();
    if (numCh <= 0 || total <= 0) return;

    if (!wants()) flush_ = juce::jmax(0, flush_ - total);

    for (int start = 0; start < total; start += maxBlock_)
    {
        const int n = juce::jmin(maxBlock_, total - start);
        juce::dsp::AudioBlock<float> block(buffer.getArrayOfWritePointers(),
                                           static_cast<size_t>(numCh),
                                           static_cast<size_t>(start),
                                           static_cast<size_t>(n));
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        chorus_.process(ctx);
    }
}

// ── phaser ──────────────────────────────────────────────────────────────────

void T5ynthPhaser::prepare(double sampleRate, int samplesPerBlock)
{
    maxBlock_ = juce::jmax(1, samplesPerBlock);
    juce::dsp::ProcessSpec spec { sampleRate,
                                  static_cast<juce::uint32>(maxBlock_), 2 };
    phaser_.prepare(spec);
    phaser_.setCentreFrequency(600.0f);   // Hz; where a Small Stone sits its sweep
    phaser_.setMix(mix_);
    flushLen_ = static_cast<int>(kWidgetFlushSeconds * sampleRate);
    flush_ = 0;
    prepared_ = true;
}

void T5ynthPhaser::reset() { if (prepared_) phaser_.reset(); flush_ = 0; }

void T5ynthPhaser::setRate(float hz)      { phaser_.setRate(juce::jlimit(0.02f, 10.0f, hz)); }
void T5ynthPhaser::setDepth(float depth)  { phaser_.setDepth(juce::jlimit(0.0f, 1.0f, depth)); }
void T5ynthPhaser::setFeedback(float fb)  { phaser_.setFeedback(juce::jlimit(-0.95f, 0.95f, fb)); }

void T5ynthPhaser::setMix(float mix)
{
    const float m = juce::jlimit(0.0f, 1.0f, mix);
    if (m <= 0.0001f && mix_ > 0.0001f) flush_ = flushLen_;   // as the chorus, same reason
    mix_ = m;
    phaser_.setMix(mix_);
}

void T5ynthPhaser::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (!prepared_) return;
    if (!wants() && flush_ <= 0) return;

    const int numCh = buffer.getNumChannels();
    const int total = buffer.getNumSamples();
    if (numCh <= 0 || total <= 0) return;

    if (!wants()) flush_ = juce::jmax(0, flush_ - total);

    for (int start = 0; start < total; start += maxBlock_)
    {
        const int n = juce::jmin(maxBlock_, total - start);
        juce::dsp::AudioBlock<float> block(buffer.getArrayOfWritePointers(),
                                           static_cast<size_t>(numCh),
                                           static_cast<size_t>(start),
                                           static_cast<size_t>(n));
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        phaser_.process(ctx);
    }
}
