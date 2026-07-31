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

    // RUMBLE, and it is the rail that carries it — not a tone added under the
    // note. BJ defined the word twice, 2026-07-31: „tieffrequente Störung, und
    // zwar als negativ von Rumble-filtern wie sie in plattenspielern sitzen",
    // then „Link Wray → Rumble. Oder auch LKW die eien Strasse langfahren und
    // Gläser klirren lassen aufgrund tieffrequenter Schwingungen." Both name the
    // same thing: something under the music that shakes what is above it. The
    // glasses are the audible part; the truck is not.
    //
    // The band is BJ's own definition, read off the filter he named it by: a
    // turntable's rumble is a low-frequency resonance „usually in the 10-30Hz
    // region" (Lindos Electronics, „Rumble Measurement", the measurement
    // standards DIN 45539 / IEC 98 / BS4852).
    //
    // The mechanism is the amplifier's own, in the same band, and it is the node
    // this stage already models. A rectifier has a high output impedance, so —
    // Wikipedia, „Motorboating (electronics)", citing Jones, *Valve Amplifiers*
    // (2011), Dailey, *Electronics for Guitarists* (2013) and van der Veen,
    // *Modern High-end Valve Amplifiers* (1999) — „low frequency swings in the
    // current drawn by output stages can cause voltage swings in the power
    // supply voltage which feed back to earlier stages", at 1–20 Hz and commonly
    // below 16 Hz in valve circuits. The filter that feeds the rail is a choke
    // and a capacitor, i.e. an LC network with its own resonance placed below
    // the ripple frequency, and a pulsed signal puts a large transient on it.
    // So: the rail does not only droop, it RINGS, and what kicks it is the
    // CHANGE in the current drawn, not its level. That is why this can be „in
    // der Transiente" where the bark could not: a ring is excited by a step and
    // then dies, while a level follower behind the voices only ever reports how
    // loud it is now.
    //
    // 16 Hz sits inside both bands. Two things follow from the rail wobbling
    // there, and BJ named both: the clipping thresholds travel with it, so every
    // partial above gets sidebands at ±16 Hz that decay — the glasses; and the
    // asymmetric pair leaves a mean under the note that travels with it too, so
    // there is real energy in the 10–30 Hz band — the truck.
    constexpr float kRumbleHz = 16.0f;
    constexpr double kRumbleDecaySec = 0.30;  // to −60 dB, so it is gone within a note
    constexpr float kRumbleAmt = 0.20f;       // of the rail, at a full-scale attack
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
    // The supply's LC ring, as a two-pole resonator. `r` is set from the decay
    // rather than from a Q, because what has to be right is that it is gone
    // before the note is; the input is scaled by sin(w) so that a surge of unit
    // AREA rings at unit amplitude whatever the sample rate.
    {
        const double w = juce::MathConstants<double>::twoPi * kRumbleHz / osr;
        const double r = std::pow(10.0, -3.0 / (kRumbleDecaySec * osr));
        ringA1_ = 2.0 * r * std::cos(w);
        ringA2_ = -(r * r);
        ringSin_ = std::sin(w);
    }
    load_ = 0.0f; ripPhase_ = 0.0f;
    loadPrev_ = 0.0f; ring1_ = ring2_ = 0.0;
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
    loadPrev_ = 0.0f; ring1_ = ring2_ = 0.0;
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

void T5ynthDistortion::setRumbleScale(float scale)
{
    // 2 and not 4: past scale 2.49 the pre-floor rail goes NEGATIVE (−0.214 at
    // 24 dB, 0.82 % of samples) and those samples are squashed to ±0.035, some
    // 26 dB under their surroundings. That is an audible 16 Hz gate, and an
    // audition control that can invent an artefact is worse than no control.
    ringScale_ = juce::jlimit(0.0f, 2.0f, scale);
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
            loadPrev_ = 0.0f; ring1_ = ring2_ = 0.0;
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
            // largest channel and not each channel's own — that is what makes
            // the rumble coherent across the stereo image instead of two
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
            // ghost notes. It is only there while the supply is loaded. It is
            // NOT the rumble — BJ heard a build that had only this and reported
            // „kein Rumble", and he was right: 100 Hz is a hum, two octaves
            // above the band he named.
            const float rip = droop * kRipple
                            * std::sin(juce::MathConstants<float>::twoPi * ripPhase_);
            ripPhase_ += ripInc_;
            if (ripPhase_ >= 1.0f) ripPhase_ -= 1.0f;

            // The rumble. What kicks the supply's LC ring is the current SURGE,
            // so the excitation is the RISE of the load and nothing else: a
            // sustained note draws a large current without ringing anything,
            // because a ring answers a change. Over an attack the rises sum to
            // the height of the attack, which is why a hard-struck note rumbles
            // and a soft one does not, without a second control saying so.
            // SIGNED, and that is load-bearing rather than tidy. A rectified
            // surge is strictly non-negative, so it carries a large DC term, and
            // a resonator that has to be GONE in 0.30 s cannot reject it: the
            // decay and the frequency together fix Q at 16·2π·0.30/(3·ln10) =
            // 2.18 at every sample rate, and at that Q DC passes only 7.2 dB
            // under the 16 Hz peak. Measured on a sustained tone, the ring then
            // settled on a constant +0.635 — a rail LIFT with no 16 Hz left in
            // it, +1.75 dB on the tail at 36 dB of drive — which is both the
            // opposite of a ring and a second, wrong-signed copy of the droop.
            // Signed is also the physics: an LC network answers di/dt in both
            // directions, and the one-way part of the current IS the droop,
            // already modelled above. The difference telescopes, so its mean over
            // any window is (load_now − load_then)/N ≈ 0 on a standing tone.
            const double surge = static_cast<double>(load_ - loadPrev_);
            loadPrev_ = load_;
            const double ring = ringA1_ * ring1_ + ringA2_ * ring2_ + surge * ringSin_;
            ring2_ = ring1_; ring1_ = ring;
            const float rumble = kRumbleAmt * ringScale_
                               * static_cast<float>(juce::jlimit(-1.0, 1.0, ring));

            const float rail = juce::jmax(0.05f, 1.0f - droop + rip + rumble);

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
