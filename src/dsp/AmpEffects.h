#pragma once
#include <JuceHeader.h>

/**
 * The amplifier chain: distortion → chorus → phaser → tremolo.
 *
 * Built on BJ's instruction, 2026-07-31: *„Was fehlt sind Effekte, die wir bei
 * den Effekten ergänzen, ggf noch ohne UI … verzerrung, tremolo, chorus,
 * phaser."* They sit here rather than in the LRO's Csound bodies because they
 * are properties of an AMPLIFIER and not of a body — a Rhodes suitcase has ONE
 * tremolo for the whole instrument, not one per key — and because at this point
 * in the chain they serve every engine, not only the oscillator that prompted
 * them. No UI yet, by the same instruction.
 *
 * WHY THESE FOUR CLASSES SHARE A FILE. They are one chain with one contract,
 * stated once here rather than four times: **each is bypassed at its own default
 * and must cost nothing there.** Adding a parameter to a shipping synth may not
 * change a single existing preset, so `distortion` and the other three default
 * to a mix or depth of 0; `wants()` reports whether the effect is doing anything
 * at all, the caller skips the whole chain when none of them is, and each
 * `processBlock` returns immediately when its own is not. That matters here more
 * than usual: idle CPU is this project's number one historical class of bug
 * (docs/PERFORMANCE_GUIDE.md), and four always-on effects behind the voice sum
 * would be four new per-block costs on a synth playing nothing.
 *
 * BUT A GATE THAT OPENS AND SHUTS IS A CLICK, and this was measured, not
 * assumed: gating on the raw parameter swapped wet for dry in a single sample,
 * −1.29 on the phaser and −0.31 on the distortion against a signal whose own
 * largest step at 220 Hz is 0.0144. So each gate here is a gate on the SMOOTHED
 * value: the effect keeps running until its own ramp has arrived, and only then
 * stops. The tremolo did this from the start; the other three now do it too. The
 * chorus asks its own mix smoother whether it has arrived, because it owns one;
 * the phaser cannot, so it counts out the 50 ms the `DryWetMixer` inside
 * `juce::dsp::Phaser` ramps for (juce_DryWetMixer.cpp:
 * `smoothedGains.reset(sampleRate, 0.05)`).
 *
 * Of the two modulation effects only the PHASER wraps a JUCE widget. The chorus
 * has its own three-tap ensemble, for the reason its comment gives. The
 * distortion is an OVERDRIVEN AMPLIFIER rather than a curve — see its own
 * comment.
 *
 * RT contract, the same one every other module in this directory keeps:
 * `prepare()` allocates and `processBlock()` never does. Every gain that a
 * parameter can step is ramped or smoothed, because a per-block step audibly
 * zippers once an LFO rides it — the delay/reverb crossfade in PluginProcessor
 * carries the same note for the same reason. Where a stage holds per-block
 * scratch, a block LARGER than the one `prepare()` was given is processed in
 * prepared-size chunks rather than trusted: ASan caught `juce::dsp::Chorus` and
 * `Phaser` writing past their `bufferDelayTimes` / `bufferFrequency` scratch on
 * the first oversized block. The chorus no longer needs that chunking — it holds
 * no per-block buffer at all now, only per-sample state — but the phaser still
 * does, and the distortion chunks for its oversampler.
 */

/**
 * An overdriven amplifier, at 2x oversampling. Bypassed at mix == 0, the default.
 *
 * NOT AN E-PIANO BARK, and this is the load-bearing sentence: BJ ruled that out
 * on 2026-07-31 after hearing two attempts — *„es klingt nicht ab. das ist ein
 * 4-sekunden-bark. ein bark bei einem epiano klingt aber nach höchstens 1 sek
 * ab … das bark-Sample hat keine Transiente, die wird verschluckt. und kein
 * Rumble. Mein vorschlag daher, bark nicht anzubieten."* Both halves measured:
 * the stage CUT the attack contrast from the dry body's +6.8 dB to +5.3, and its
 * excess over that body GREW across the note, +3.9 → +5.0 dB (and +8.4 → +16.5
 * at 24 dB of drive), instead of dying away.
 *
 * A stage HERE can never do it, and the reason generalises past this one sound.
 * From the DX7 corpus, 842 named e-pianos: the far modulator — the bright,
 * barking part — falls at envelope rate 50 where the carrier falls at 25, and
 * drops 24 envelope points against the near modulator's 6. The bark carries its
 * OWN envelope at about twice the speed of the note. An amplifier behind the
 * voices has no envelope; it follows the LEVEL, and a saturating stage under a
 * decaying body turns a decaying sound into a standing one by construction,
 * because the body falls and the saturation holds the output up. Anything that
 * has to die faster than the note belongs in the body — for the e-piano that is
 * `ep_fm3`'s `strike` axis, which already carries it.
 *
 * IT IS AN AMPLIFIER AND NOT A CURVE, which is what BJ said about the FIRST
 * version — a plain symmetric `tanh`: *„das ist zu glatt, hell lang."* Both were
 * in the files: the even/odd balance moved by 1.4 dB and nothing else (a
 * symmetric curve has NO even harmonics), and the spectral centroid over 6.5 s
 * went 3946 → 794 Hz where the body's own went 1827 → 605 while the level fell
 * 1.0 dB against the body's 9.9 — saturation flattens the envelope AND the
 * brightness.
 *
 * So the stage models the three things an amplifier does when it is driven past
 * what its supply can hold, and each has its source in the .cpp: a rail that
 * DROOPS with the current drawn (so the clipping threshold moves, the bark ends
 * up in the attack, and the note is no longer flattened for its whole length),
 * the rectifier RIPPLE that droop makes audible (the intermodulation players
 * call ghost notes), and an ASYMMETRIC pair of clipping thresholds (so there are
 * even harmonics at all: measured, the 2nd partial rises 12.7 dB over the dry
 * body and the 4th 28.6 dB).
 *
 * THIS STAGE DOES NOT DO A RUMBLE, and nothing here should be read as promising
 * one. BJ asked for „einen kleinen Übersteuerungs-‚Rumble' in der Transiente",
 * defined the word twice (the low-frequency disturbance a turntable's rumble
 * filter removes; Link Wray; a truck making glasses rattle), heard three builds
 * and reported „kein Rumble" every time. It is not offered, not attempted again
 * here, and not listed as pending — his ruling, 2026-07-31: *„vergiss den
 * Rumble, Du kannst kein Rumble."*
 *
 * NO MAKE-UP GAIN, deliberately. The obvious 1/g (tanh's slope at zero, so
 * exact for the quiet part of the waveform) turns the top half of the control
 * into a large volume DROP: measured out/in RMS on a 0.5 sine, −4.90 dB at 12 dB
 * of drive and −27.06 dB at 36, because by then the output is a square wave
 * divided by 63. A drive knob that gets quieter as it is turned up is not a
 * drive knob. Without it the control runs the other way and stays bounded,
 * because tanh is: measured on a 0.5 sine BEFORE the supply model was added,
 * +0.07 dB at 0.5 dB of drive, +4.34 at 6, +7.20 at 12 and +8.95 at 36, where
 * the output is a square and the ceiling is the +9.0 dB a full-scale square
 * holds over that sine. The sag now takes some of that back at the attack,
 * which is what sag is.
 *
 * The dry/wet mix is taken INSIDE the oversampled domain, so both halves travel
 * the same path and arrive with the same latency. Mixing an undelayed dry
 * against the oversampler's 2.34-sample wet is a comb filter, and it was one:
 * measured −23.3 dB at 10 kHz at mix 0.5, first null exactly where
 * fs/(2·latency) puts it.
 *
 * That leaves ONE edge, and it is paid for rather than hidden. Because the dry
 * now travels through the oversampler too, engaging the stage mid-note starts a
 * filter from zero state under a signal that is already loud — measured as a
 * 0.138 step. So the first 20 ms after engaging are crossfaded against the raw
 * input, which IS the 2.34-sample misalignment for the length of that fade and
 * nothing after it. A 20 ms transient comb in place of a step: the comb is the
 * cheaper of the two, and unlike the step it does not exist at any steady
 * setting.
 */
class T5ynthDistortion
{
public:
    void prepare(double sampleRate, int samplesPerBlock);
    void reset();

    /** 0…36 dB. 0 is the default and is NOT bypass — `mix` is. */
    void setDrive(float driveDb);
    /** 0…1 wet. 0 is bypass, and is the default. */
    void setMix(float mix);

    bool wants() const noexcept { return mix_ > 0.0001f; }
    void processBlock(juce::AudioBuffer<float>& buffer);

private:
    /** True while the MIX ramp is still travelling, so the stage may not stop
        yet however its target reads. Deliberately NOT the drive: with the mix at
        zero the stage contributes nothing whatever the drive is doing, and
        including `gain_` here meant every drive automation woke the whole
        oversampler at mix 0 — measured, 102400 of 102400 samples came back
        changed from a bypassed stage. */
    bool moving() const noexcept
    {
        return wet_.isSmoothing() || dryG_.isSmoothing();
    }

    std::unique_ptr<juce::dsp::Oversampling<float>> os_;
    juce::AudioBuffer<float> raw_;              ///< engage crossfade source only
    juce::SmoothedValue<float> gain_, wet_, dryG_, engage_;
    float driveDb_ = 0.0f, mix_ = 0.0f;
    // the supply model — one rail for the whole amplifier, per-channel DC block
    float load_ = 0.0f, ripPhase_ = 0.0f;
    float attCoef_ = 0.0f, relCoef_ = 0.0f, ripInc_ = 0.0f, dcPole_ = 0.0f;
    float dcX_[2] { 0.0f, 0.0f }, dcY_[2] { 0.0f, 0.0f };
    double sr_ = 44100.0;
    int maxBlock_ = 0;
    bool prepared_ = false, running_ = false;
};

/**
 * Amplitude tremolo, optionally as a stereo pan.
 *
 * `stereo` runs the two channels apart in phase: at 0 both are modulated
 * together (a Wurlitzer's tremolo, which is amplitude, because it has one
 * amplifier), at 1 they are in antiphase (a Rhodes suitcase's, which is a pan
 * between its two amplifier pairs). Bypassed at depth == 0.
 *
 * FOUR SHAPES. The classic tremolos are often square, or nearly, but triangle
 * and sine are just as much part of the family (BJ, 2026-08-01). Three of the
 * four come out of one construction — `tanh(k·sin) / tanh(k)`, the textbook
 * soft-clipping saturator applied to the sine, which is also how the rounded
 * switching of an optical/bias tremolo is usually approximated: the photocell
 * cannot step, so its "square" has a finite edge. k = 0 is the sine itself,
 * k = 3 the rounded square, k = 12 the fast-edged one. There is deliberately no
 * `sign()` square: a gain that steps by the full depth twice per cycle is the
 * click this file's contract is about, and the k = 12 edge (~T/37, so a few ms
 * at musical rates) is as square as this can be while staying continuous.
 * The triangle is the ordinary phase-domain one, sharing the sine's phase
 * convention so the shapes line up.
 */
class T5ynthTremolo
{
public:
    /** Order = mildest to hardest, and matches the APVTS choice order. */
    enum Wave { Sine = 0, Triangle, SoftSquare, Square, numWaves };

    void prepare(double sampleRate, int samplesPerBlock);
    void reset();

    void setRate(float hz);          ///< 0.1…20 Hz
    void setDepth(float depth);      ///< 0…1, 0 is bypass and the default
    void setStereo(float stereo);    ///< 0 = amplitude, 1 = full pan
    void setWave(int wave);          ///< Wave; crossfades, never steps

    bool wants() const noexcept { return depth_ > 0.0001f; }
    void processBlock(juce::AudioBuffer<float>& buffer);

private:
    /** The LFO at `phase01`, in −1…+1, with +1 = loudest for every shape. */
    static float shape(double phase01, int wave) noexcept;

    double phase_ = 0.0, inc_ = 0.0, sr_ = 44100.0;
    juce::SmoothedValue<float> depthS_, stereoS_;
    float depth_ = 0.0f, stereo_ = 0.0f, rateHz_ = 5.5f;
    // Changing shape mid-note moves the gain wherever the two curves differ at
    // the current phase, so the new shape is faded in over the same time
    // constant every other control here uses rather than swapped.
    int wave_ = Sine, waveFrom_ = Sine, wavePending_ = Sine;
    juce::SmoothedValue<float> waveMixS_;
};

/**
 * A three-phase ensemble chorus: one LFO, three modulated delay taps 120° apart,
 * split across the two channels. Bypassed at mix == 0, which is the default.
 *
 * REPLACES `juce::dsp::Chorus`, on BJ's ruling of 2026-08-04 after hearing the
 * shipped one: *„Was Du da gebaut hast ist kein Chorus sondern eine Kombi
 * Pitch/Filter-Wobbel."* He is right, and structurally so, which is why this is a
 * replacement and not a retuning. That widget holds ONE delay tap driven by ONE
 * LFO, and a single swept tap summed with the dry can only be two things at the
 * same time: a Doppler pitch shift, because the instantaneous frequency ratio of
 * a moving tap is 1 − τ′(t), and one comb filter whose notches slide with it. At
 * its geometry — 7 ms of centre delay, ±10 ms of sweep (juce_Chorus.h:124, 169)
 * — τ′ alone puts the defaults at ±30 cents of vibrato and drags the whole comb
 * (first notch 71 Hz, spacing 143 Hz) across a factor of three. And it cannot
 * widen at all: the delay times are computed once per block and used for every
 * channel (juce_Chorus.h:117–130), so a signal arriving identical in both
 * channels leaves identical — measured L−R of exactly 0, already recorded in
 * tools/lco_amp_effects_page.py before this rebuild.
 *
 * METHOD AND SOURCES, before the first line, per the authoring rule.
 *   • The modulated-delay chorus, and the requirement that the interpolator be
 *     of higher than linear order — a linearly interpolated moving tap is itself
 *     a time-varying lowpass whose gain travels with the fractional delay: Jon
 *     Dattorro, „Effect Design, Part 2: Delay-Line Modulation and Chorus", J.
 *     Audio Eng. Soc. 45(10), 1997, 764–788. Hence Lagrange3rd below, which is
 *     also the order Csound's `vdelay3` interpolates at.
 *   • THREE taps at 120°: the bucket-brigade ensemble of the ARP/Solina string
 *     machine family. This project already carries that construction and BJ has
 *     already signed it off — `divider_organ`'s `ensemble` in
 *     backend/lco_library.json is three `vdelay3` taps at 6.0 ± 3.4 ms, phases
 *     0 / 1⁄3 / 2⁄3. Its geometry is the anchor for the two numbers below, so
 *     they are a measured, heard precedent rather than a fresh invention.
 *
 * WHY THREE AND NOT ONE, in one sentence: with one tap there is a single comb and
 * a single pitch line and both are audible AS THEMSELVES, and with three at 120°
 * no notch of one sits where another's does at any instant, so no single sweep
 * survives the sum and what is left is the thickness the notches leave behind.
 *
 * DECLARED, NOT ATTRIBUTED — which tap feeds which channel. Left sums the tap at
 * −120° and the one at 0°, right sums 0° and +120°: the middle phase is common,
 * so the image keeps a centre, and the outer two are split, so the sides
 * decorrelate. Nothing in either source above fixes that pairing. It is a choice,
 * and it is the one thing here that BJ's ear decides rather than a citation.
 *
 * Stereo in and out. It is prepared for two channels and touches no more than
 * two: a third and beyond pass through unchanged, where the widget this replaced
 * would have been handed them and asserted.
 */
class T5ynthChorus
{
public:
    /** The ensemble's geometry in ms — `divider_organ`'s own two numbers. Public
        because the panel shows the sweep that `Amt` is a fraction of, and a
        second copy of 3.4 living in the GUI would be a number that can drift. */
    static constexpr float kCentreMs = 6.0f;
    static constexpr float kSweepMs  = 3.4f;

    void prepare(double sampleRate, int samplesPerBlock);
    void reset();

    void setRate(float hz);        ///< 0.05…10 Hz
    void setDepth(float depth);    ///< 0…1, as a fraction of ±kSweepMs
    void setMix(float mix);        ///< 0…1, 0 is bypass and the default

    bool wants() const noexcept { return mix_ > 0.0001f; }
    void processBlock(juce::AudioBuffer<float>& buffer);

private:
    static constexpr int kTaps = 3;

    /** Go dry and refill: the line is empty, so hold the mix at zero until it
        has been fed for as long as the deepest tap, then ramp. */
    void armFill();

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> delay_ { 4 };
    juce::SmoothedValue<float> depthS_, mixS_;
    double phase_ = 0.0, inc_ = 0.0, sr_ = 44100.0;
    float centreS_ = 0.0f, sweepS_ = 0.0f, maxS_ = 0.0f;
    float rateHz_ = 0.8f, depth_ = 0.35f, mix_ = 0.0f;
    int fill_ = 0, fillLen_ = 0, lastNumCh_ = 2;
    bool prepared_ = false, idle_ = true;
};

/** juce::dsp::Phaser. Bypassed at mix == 0, which is the default. */
class T5ynthPhaser
{
public:
    void prepare(double sampleRate, int samplesPerBlock);
    void reset();

    void setRate(float hz);         ///< 0.02…10 Hz
    void setDepth(float depth);     ///< 0…1
    void setFeedback(float fb);     ///< -0.95…0.95
    void setMix(float mix);         ///< 0…1, 0 is bypass and the default

    bool wants() const noexcept { return mix_ > 0.0001f; }
    void processBlock(juce::AudioBuffer<float>& buffer);

private:
    juce::dsp::Phaser<float> phaser_;
    float mix_ = 0.0f;
    int flush_ = 0, flushLen_ = 0, maxBlock_ = 0;
    bool prepared_ = false;
    /** True from the first block that actually reaches `phaser_.process()` until
        the disengage-clear below has fired. Lets `processBlock` tell "just
        arrived at idle, clear once" apart from "already idle, nothing to do". */
    bool running_ = false;
};
