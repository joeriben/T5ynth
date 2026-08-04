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
 * them. The same instruction allowed them to arrive without one — *„ggf noch
 * ohne UI"* — and they did; the FX card reached them in `aa868fde`, so each
 * one's parameters are on the panel now and this file's defaults are what a
 * player sees at rest, not what they are stuck with.
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
 * stops. The tremolo did this from the start; the other three now do it too, and
 * all of them can now simply ASK — each owns the `SmoothedValue` its own mix
 * rides on, so `isSmoothing()` answers the question directly. The phaser used to
 * be the exception, counting out the 50 ms a `juce::dsp::DryWetMixer` it did not
 * own would ramp for; owning its mix retired that count.
 *
 * NEITHER MODULATION EFFECT WRAPS A JUCE WIDGET any more. The chorus has its own
 * three-tap ensemble and the phaser its own six-stage all-pass chain, each for
 * the reason its own comment gives. Both dealt with the same latch on the way
 * past — a `lastOutput`-style feedback memory that `NaN * 0` keeps poisoned, and
 * which in the widgets nothing ever cleared — but they deal with it DIFFERENTLY,
 * and the difference is worth stating rather than blurring: the chorus has no
 * recursive path left at all, so a non-finite sample cannot latch there under any
 * circumstances, while the phaser still has a feedback loop and clears it when it
 * PARKS. One poisoned sample therefore still costs the phaser its output until
 * the stage is turned down, which is what `10c9d501` bought and no more. The
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
 * no per-block buffer at all now, only per-sample state — and now neither does
 * the phaser, for the same reason; only the distortion still chunks, for its
 * oversampler.
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

/**
 * A six-stage all-pass phaser, its own all-pass chain rather than a wrap of
 * `juce::dsp::Phaser`. REPLACES that widget on three measured defects, all
 * from the same multitone through the real class (60 sine partials, 59 Hz –
 * 16 kHz, bin-aligned to a 16384-point STFT, mix 0.5, rate 0.4 Hz):
 *   1. NO STEREO WIDTH AT ALL. The widget sweeps its cutoff into ONE mono
 *      buffer (`bufferFrequency`, juce_Phaser.h) and applies it to every
 *      channel through the same six filters — side-to-mid measured at
 *      exactly −600 dB (L and R bit-identical) at every setting tried. A
 *      shared cutoff cannot be widened; that is why this is a replacement
 *      and not a retune.
 *   2. THE SWEEP WAS TOO WIDE AT THE DEFAULT: 7.3 octaves of notch travel
 *      (67–10957 Hz) at the shipped Amt 0.5, and turning Amt DOWN measurably
 *      IMPROVED it — deeper notches, because they stayed in band (Amt 0.2:
 *      22.6/49.2 dB median/max depth against 15.9/36.0 at Amt 0.5).
 *   3. THE FEEDBACK KNOB RAN BACKWARDS. Turning it up FLATTENED the response
 *      (peak-over-median 3.0 → 2.0 dB going fb 0 → +0.7) while the resonance
 *      sat on the NEGATIVE half instead (3.0 → 8.9 dB going fb 0 → −0.7).
 *
 * METHOD AND SOURCES, before the first line, per the authoring rule.
 *   • Phasing is a cascade of first-order all-pass sections whose corner is
 *     swept, summed with the dry; the notches sit where the cascade's
 *     accumulated phase passes odd multiples of pi, so N stages give N/2
 *     notches. Udo Zölzer (ed.), DAFX: Digital Audio Effects, 2nd ed., Wiley
 *     2011, ch. 2 (delay-based effects / phasing). The classic circuits it
 *     names, MXR Phase 90 and EHX Small Stone, are four stages; six and
 *     eight are equally standard, and six is what this replaces, UNCHANGED,
 *     so the character does not move for a reason nobody asked for.
 *   • The section itself is the first-order TPT/ZDF all-pass: Vadim
 *     Zavalishin, The Art of VA Filter Design.
 *     `juce::dsp::FirstOrderTPTFilter<float>` in `Allpass` mode IS that
 *     section, and is what `juce::dsp::Phaser` used internally. Its four
 *     lines are TRANSCRIBED here rather than reinterpreted — `v = G(x − s);
 *     lp = v + s; s = lp + v; y = 2·lp − x`, with `G = g/(1+g)` and
 *     `g = tan(pi·f/fs)`, in that order and those types — so the stage's own
 *     sound stays identical everywhere this task does not deliberately
 *     change it. Why transcribed and not called: see ONE COEFFICIENT below.
 *
 * ONE COEFFICIENT PER CHANNEL PER SAMPLE. All six sections of a channel
 * sweep to the SAME corner, so `G` is computed once and shared. Holding six
 * `FirstOrderTPTFilter` objects instead means six `setCutoffFrequency`
 * calls, and each one is a `std::tan` — twelve per stereo sample where two
 * do the same work. Measured at 512 / 48 kHz, median of five runs: 46.97 →
 * 23.26 us/block, so ten redundant transcendentals were 50 % of the whole
 * stage's engaged cost, and the stage now costs 0.22 % of one core instead
 * of 0.44 %. BJ, 2026-08-04,
 * asked which way to take that trade: *„Klanglich identisch ist nicht
 * wichtig, klanglich gut ist wichtig."* Here it costs nothing to have both,
 * because the transcription is the SAME arithmetic in the same order.
 *
 * HOW FAR "BIT-IDENTICAL" REACHES, since that is a claim about a toolchain
 * and not only about this code. Under what this project builds with — clang
 * at `-O3`, i.e. `-ffp-contract=on`, which contracts only within one
 * expression — it is exact: 0 differing samples over 4914 configurations (6
 * sample rates × Amt 0…1 × feedback −0.95…+0.95), over 54 000 randomised
 * blocks with channel counts 0…3 and re-`prepare()` mid-stream, and at the
 * 20 Hz and 0.49·fs clamps, at 8 kHz and 192 kHz, on denormals, on ±1e38 and
 * on NaN. The compiled `2.0f*lp - y` is the same `fnmsub` instruction JUCE's
 * own `processSample` emits. Under `-ffp-contract=fast`, which GCC defaults
 * to in C++ mode and the Linux CI job therefore gets, the inline form admits
 * a contraction the six opaque calls structurally could not: `lp = v + s1`
 * and `s1 = lp + v` fuse, skipping one rounding of `v`. That is a DIFFERENT
 * result — measured worst |Δ| 8.29e-6 on DC, 4.62e-7 at the shipped
 * defaults — and a marginally more accurate one, since the skipped rounding
 * is a rounding. Nothing here needs samples to match across platforms, so
 * the flag is left alone; what is pinned is the sentence, not the compiler.
 *
 * TWO INDEPENDENT CHAINS. `apState_` is indexed BY CHANNEL, so L and R hold
 * six sections each and sweep to different corners at the same instant. That
 * is the entire fix for defect 1: the widget could not do it, because its
 * one cutoff was shared by every channel it was prepared with.
 *
 * ONE LFO, sine, with a 90-DEGREE phase offset between the two channels'
 * read of it — a CHOICE, not something either source above fixes, exactly
 * the way `T5ynthChorus`'s comment declares its own tap-to-channel pairing.
 * At 180 degrees the two channels' notches sit at opposite ends of the sweep
 * at every instant, the worst case for the mono sum (neither channel's notch
 * is ever near the other's); 90 degrees keeps the two apart for width while
 * costing the mono sum less. MEASURED at the shipped default (fb 0, Amt 0.5,
 * mix 0.5, rate 0.4 Hz): side-to-mid RMS −6.0 dB (was −600, i.e. bit-
 * identical); the mono sum's own notch depth median/max 10.6/24.1 dB against
 * the stereo channels' own 20.6/47.3 — the width costs real notch depth in
 * the mono fold-down, on the record rather than assumed.
 *
 * SWEEP GEOMETRY: logarithmic about a FIXED centre of 600 Hz (the widget's
 * own `setCentreFrequency(600.0f)` — where a Small Stone sits its sweep —
 * kept unchanged), spanning `kSpanOctaves` octaves either side at Amt 1.0,
 * LINEAR IN OCTAVES with Amt. `kSpanOctaves` = 2.0, so Amt 1.0 sweeps
 * 150–2400 Hz and Amt 0.5 sweeps 300–1200 Hz. This puts the knob's MIDDLE
 * where defect 2's own table measured best: the OLD widget's Amt 0.2 swept
 * 301–1197 Hz — verified from `Phaser::setCentreFrequency`/`update`'s own
 * arithmetic (depth 0.2 → oscVolume 0.5·0.2 = 0.1; normCentreFrequency =
 * mapFromLog10(600, 20, 20000) = 0.49237; lfo = 0.49237 ± 0.1; freq =
 * mapToLog10(lfo, 20, 20000) = 300.75 Hz / 1197.4 Hz) — the arithmetic
 * holds. That is the SAME sweep this class now puts at Amt 0.5, not a fifth
 * of the way up the knob.
 *
 * FEEDBACK. The loop wraps the WHOLE six-stage cascade once per channel (not
 * once per stage) — the same topology `juce::dsp::Phaser` uses internally
 * (`output = input − lastOutput; … lastOutput = output * feedback;`) — but
 * ADDED rather than subtracted, which is the entire fix for defect 3: with
 * every stage a true all-pass (|H(jw)| = 1 always), the closed loop's gain
 * at a frequency where the cascade's phase returns to a multiple of 2·pi is
 * 1/|1 ∓ feedback|, and JUCE's subtraction puts the resonant sign on
 * NEGATIVE feedback (measured above); addition puts it on POSITIVE, the
 * knob direction asked for. MEASURED (peak over frame median, fb 0 / +0.7 /
 * −0.7 at Amt 0.5): 3.0 → 11.2 → 1.7 dB — positive resonates, negative
 * flattens, the sign is the intended one and was not flipped. Because the
 * loop gain magnitude at any frequency is exactly |feedback| (never more,
 * since |H| = 1 exactly), clamping the parameter to ±0.95 — its unchanged
 * range — is what keeps the loop from running away; no separate runtime
 * clamp does anything a stricter parameter range would not already do.
 * "Does not run away" is the whole of what that clamp promises, and it is
 * worth saying what it still permits: the closed loop peaks at
 * 1/(1 − |feedback|), so the knob's own end is a +26 dB resonance — bounded,
 * stable, and loud. That is the sound the control exists for and not a
 * defect, but nothing in this stage attenuates it; the amp chain's limiter
 * downstream is what the level meets.
 *
 * THE FEEDBACK LOOP IS AC-COUPLED, and that is the price of the sign above
 * rather than a separate idea. Six all-passes have phase 0 at DC, so an
 * ADDED loop is a DC amplifier of exactly the same 1/(1 − fb) — measured
 * ×1.43 / ×2.50 / ×20.0 at fb +0.3 / +0.6 / +0.95, which is +19.5 dB at 2 Hz
 * and +8.1 dB at 20 Hz, crossing unity only near 55 Hz. The widget got its
 * DC ATTENUATION for free from the very subtraction that put its resonance
 * on the wrong half of the knob (1/(1 + fb) at DC, below one), so flipping
 * the sign without blocking DC would have traded a backwards knob for a
 * subsonic amplifier — a regression, and one the ear would meet as pumping
 * on the chain's limiter rather than as a sound. One pole at 20 Hz in the
 * loop (`kFbHighpassHz`) removes it: DC gain measures ×1.000 at every
 * feedback setting, while the resonance the knob exists for is untouched
 * (fb +0.7, corner parked at 600 Hz: +10.4 dB at 346 Hz and +10.2 dB at
 * 1 kHz against −2.5 to −4.6 dB off-resonance). `T5ynthDistortion` in this
 * same file blocks DC for the same reason.
 *
 * OWN MIX: a `juce::SmoothedValue` linear dry/wet, like `T5ynthChorus`'s, in
 * place of `juce::dsp::DryWetMixer`. Idle and engage copy the chorus's
 * pattern in this same file: the gate is on the SMOOTHED mix, not the raw
 * target, so the stage keeps running until its own ramp has arrived at dry,
 * and only THEN clears its state — the twelve sections and the two feedback
 * taps — once, on the transition. A channel count that changes under the
 * stage is handled too, but deliberately NOT the chorus's way: everything
 * here is per channel, so only a newly present channel is cleared and faded
 * in, and the channel that was already running is left alone. The measured
 * reason is in the .cpp at that site — re-arming the whole stage, as the
 * chorus rightly does for its one shared delay line, puts a 0.59093 step on
 * a channel that never lost its input.
 *
 * THIS IS ALSO WHAT REMOVES TWO COLD-START STEPS the widget had, both from
 * the same mechanism: `T5ynthPhaser::prepare()` used to prepare the widget
 * BEFORE the processor's first real `setMix`/`setDepth` calls arrived, which
 * left `DryWetMixer` parked at the widget's class default mix (0.5) and
 * `oscVolume` parked at its class default depth (0.5) until then — measured
 * 0.0421 on the mix, and a second, quieter case on depth: a preset whose Amt
 * is NOT 0.5 started its first engage ramping the sweep width in from that
 * phantom 0.5 over the same 50 ms, invisible only because the shipped
 * default Amt already IS 0.5. Owning the smoothers here repeats neither:
 * `setDepth`/`setFeedback` snap current-to-target on the FIRST call after
 * `prepare()` (`depthSeeded_`/`fbSeeded_` below) instead of smoothing away
 * from a value nobody set, so whatever the processor's first real call says
 * is what plays from sample one, at any Amt. MEASURED (span in octaves at
 * the first engage vs. a later one, same session): Amt 0.5 — 1.00 vs. 1.00;
 * Amt 0.2 — 0.40 vs. 0.40. Identical; no phantom ramp at either setting.
 *
 * RT CONTRACT: `processBlock()` never allocates, and this stage keeps no
 * per-block scratch of its own — so unlike the widget it replaces, a block
 * larger than the one `prepare()` was given needs no chunking; it is simply
 * walked a sample at a time, like every other per-sample stage in this file.
 * Measured with an `operator new` counter, 0 allocations on `processBlock`
 * at block sizes 0 / 1 / 64 / 3000 / 4096 and channel counts 0…3, including
 * the first oversized block after a `prepare(sr, 64)`. `prepare()` is
 * ALLOWED to allocate by the file-level contract above, and in fact does
 * not: the sections are plain floats, and the two feedback highpasses only
 * `resize(1)` a vector their constructor already sized.
 */
class T5ynthPhaser
{
public:
    /** The sweep's total span at Amt 1.0, either side of the fixed 600 Hz
        centre, in octaves — Amt 0.5 therefore spans ±1.0 oct (300–1200 Hz)
        and Amt 1.0 spans ±2.0 oct (150–2400 Hz). Public because the panel
        reads the span Amt is a fraction of, out of the same constant the DSP
        uses rather than a second copy of the number. */
    static constexpr float kSpanOctaves = 2.0f;

    void prepare(double sampleRate, int samplesPerBlock);
    void reset();

    void setRate(float hz);         ///< 0.02…10 Hz
    void setDepth(float depth);     ///< 0…1 ("Amt"); ± kSpanOctaves·depth octaves
    void setFeedback(float fb);     ///< -0.95…0.95; positive resonates
    void setMix(float mix);         ///< 0…1, 0 is bypass and the default

    bool wants() const noexcept { return mix_ > 0.0001f; }
    void processBlock(juce::AudioBuffer<float>& buffer);

    /** Diagnostic only: the octave span the sweep is CURRENTLY using, after
        smoothing (`kSpanOctaves` * Amt's smoothed value). Exists so "no ramp
        from a phantom default on the first engage of a session" (class
        comment, above) can be verified from outside instead of asserted;
        read-only, costs one `SmoothedValue` read, and is never called from
        `processBlock`. */
    float currentSpanOctaves() const noexcept { return kSpanOctaves * depthS_.getCurrentValue(); }

private:
    static constexpr int kStages = 6;
    static constexpr float kCentreHz = 600.0f;   // where a Small Stone sits its sweep
    static constexpr float kFbHighpassHz = 20.0f;  // see the DC paragraph above

    // The six sections' own state, one `s1` per section per channel. Written
    // out here rather than held as six `juce::dsp::FirstOrderTPTFilter`s
    // because all six of a channel's sections sweep to the SAME corner and
    // that class recomputes its coefficient per instance — see the ONE
    // COEFFICIENT paragraph above.
    float apState_[2][kStages] { };
    juce::dsp::FirstOrderTPTFilter<float> fbHp_[2];   // AC-couples the feedback loop
    double phase_ = 0.0, inc_ = 0.0, sr_ = 44100.0;
    juce::SmoothedValue<float> depthS_, fbS_, mixS_;
    float depth_ = 0.5f, fb_ = 0.0f, mix_ = 0.0f, rateHz_ = 0.4f;
    float fbState_[2] { 0.0f, 0.0f };
    int lastNumCh_ = 2;         // see the newly-present-channel clear in processBlock
    float wetGain_[2] { 1.0f, 1.0f };   // the same clear's wet fade, per channel
    float fadeInc_ = 1.0f;              // one kSmoothSeconds' worth per sample
    // "First call after prepare() snaps current==target" latches: without
    // these, the very first setDepth()/setFeedback() call after a cold
    // prepare() would SMOOTH from whatever depth_/fb_ happened to hold (a
    // constructor default, possibly stale) towards the processor's real
    // value, instead of starting there. See the class comment's COLD-START
    // paragraph, above.
    bool depthSeeded_ = false, fbSeeded_ = false;
    /** Arms `mixS_` at true dry (0) with a ramp up to `mix_`, and marks the
        stage idle. Called from `prepare()`, `reset()`, and the processBlock
        idle-transition clear — the one place a fresh engage should ever ramp
        FROM, mirroring `T5ynthChorus::armFill()` in this same file. */
    void armMix();
    bool prepared_ = false, idle_ = true;
};
