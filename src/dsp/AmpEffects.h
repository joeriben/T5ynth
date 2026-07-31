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
 * stops. The tremolo did this from the start; the other three now do it too, the
 * two JUCE widgets by counting out the 50 ms their internal `DryWetMixer` ramps
 * for (juce_DryWetMixer.cpp: `smoothedGains.reset(sampleRate, 0.05)`).
 *
 * The two modulation effects wrap `juce::dsp::Chorus` and `juce::dsp::Phaser`
 * rather than reimplementing them. The distortion is an OVERDRIVEN AMPLIFIER
 * rather than a curve — see its own comment.
 *
 * RT contract, the same one every other module in this directory keeps:
 * `prepare()` allocates and `processBlock()` never does. Every gain that a
 * parameter can step is ramped or smoothed, because a per-block step audibly
 * zippers once an LFO rides it — the delay/reverb crossfade in PluginProcessor
 * carries the same note for the same reason. A block LARGER than the one
 * `prepare()` was given is processed in prepared-size chunks rather than
 * trusted: ASan caught `juce::dsp::Chorus` and `Phaser` writing past their
 * `bufferDelayTimes` / `bufferFrequency` scratch on the first oversized block.
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
 * version — a plain symmetric `tanh`: *„das ist zu glatt, hell lang und müsste
 * außerdem — ein ungelöstes Problem — auch einen kleinen Übersteuerungs-
 * ‚Rumble' in der Transiente haben."* Measured against
 * the dry body, all three of those were in the files: the even/odd balance moved
 * by 1.4 dB and nothing else (a symmetric curve has NO even harmonics); the
 * spectral centroid over 6.5 s went 3946 → 794 Hz where the body's own went
 * 1827 → 605, and the level fell 1.0 dB where the body fell 9.9 — saturation
 * flattens the envelope AND the brightness; and nothing in the code could
 * produce a rumble at all.
 *
 * So the stage models four things an amplifier does when it is driven past what
 * its supply can hold, and each has its source in the .cpp: a rail that DROOPS
 * with the current drawn (so the clipping threshold moves, the bark ends up in
 * the attack, and the note is no longer flattened for its whole length), the
 * rectifier RIPPLE that droop makes audible (the intermodulation players call
 * ghost notes), an ASYMMETRIC pair of clipping thresholds (so there are even
 * harmonics at all: measured, the 2nd partial rises 12.7 dB over the dry body
 * and the 4th 28.6 dB) — and the RUMBLE.
 *
 * THE RUMBLE is the same rail, ringing. BJ defined the word twice after hearing
 * a build that had only the 100 Hz ripple and reporting „kein Rumble", which was
 * correct — 100 Hz is a hum, two octaves above what he meant: *„tieffrequente
 * Störung, und zwar als negativ von Rumble-filtern wie sie in plattenspielern
 * sitzen"*, then *„Link Wray → Rumble. Oder auch LKW die eien Strasse
 * langfahren und Gläser klirren lassen aufgrund tieffrequenter Schwingungen."*
 * The band is the one he named it by (a turntable's rumble is a resonance at
 * 10–30 Hz), the mechanism is the amplifier's own in that same band (a
 * rectifier's high output impedance lets current swings move the rail, 1–20 Hz;
 * the choke and cap that feed it are an LC network that a pulsed signal kicks) —
 * so the rail RINGS, and what kicks it is the CHANGE in the current drawn, not
 * its level. That is why this can sit in the transient where the bark could not:
 * a ring answers a step and then dies, while a level follower behind the voices
 * only reports how loud it is now.
 *
 * Measured against the same code with the term at zero, at mix 0.7 and 24 dB of
 * drive on the dry body: the added energy sits in the band and nowhere else —
 * +32.4 dB at 16–25 Hz, +21.8 at 10–16, +9.9 at 25–40, and +0.0, −0.1 and −0.1
 * at 80–160, 160–320 and 320–2000. It dies with the transient: the difference
 * between the two files is −28.0 dBFS over the first 0.3 s and −64.2 over the
 * next 0.3, i.e. 36 dB gone in 300 ms. And it shakes what is above it, which is
 * the half BJ's two images are actually about — the envelope's own 16 Hz
 * component rises from −20.0 to −3.9 dB.
 *
 * It is an ÜBERSTEUERUNGS-rumble in the literal sense: +1.4 dB of that shake at
 * 9 dB of drive, +9.2 at 18, +16.1 at 24 and +21.6 at 30, because the rail IS
 * the clipping threshold and moving it does little to a stage that is not
 * clipping. And on a note that does not decay it leaves nothing behind: on a
 * held 220 Hz sine the tail from two to three seconds measures −0.00 dB against
 * the same code with the term at zero, at 12, 24 and 36 dB of drive alike. The
 * rail is never lifted above its unloaded value either — measured range
 * [0.347 … 1.000] at 36 dB, and the 0.05 floor is reached on 0.00 % of samples
 * at every drive.
 *
 * It costs 2.1 dB of PEAK, and that is the price of putting real energy under
 * the note: the stage's peak output goes 0.829 → 1.059 at 12 dB of drive and
 * 0.725 → 0.948 at 24, because the asymmetric pair leaves a 16 Hz component that
 * the 10 Hz DC blocker only partly removes. Past full scale at the first of
 * those, which the always-on limiter in PluginProcessor catches — but it is a
 * real 2.1 dB and not a rounding.
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

    /** Scales the supply's ring, for the A/B on the audition page and nothing
        else — 1 is the model and is the default, 0 removes the term without
        touching anything around it. Deliberately NOT a plugin parameter: the
        amplifier chain has the twelve `fx_*` ids and no thirteenth, and how far
        a supply rings is a property of the amplifier, not a knob on it. */
    void setRumbleScale(float scale);

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
    float loadPrev_ = 0.0f, ringScale_ = 1.0f;
    /** The ring is DOUBLE, and that is a requirement rather than caution. Its
        stability margin is 1 − a1 − a2 = w² + δ², which shrinks as 1/osr²: at a
        192 kHz host it is 7.2e-8, and one float ULP beside a1 ≈ 2.0 is 1.2e-7.
        In float the ROUNDING of the two coefficients decides stability — swept
        over every integer host rate, 1489 of them (all ≥ 172795 Hz) put the pair
        at or over the edge, where the poles turn real with one at z = 1 and the
        term becomes an integrator that never returns to rest: measured at
        172795 Hz, five notes with two seconds of silence between them each came
        back 2.0 dB up with no 16 Hz left in them. It does not blow up — the
        clamp below and the rail's floor bound the output — it silently latches,
        which is worse. In double the same margin is 1.62e8 ULPs wide. */
    double ring1_ = 0.0, ring2_ = 0.0;
    double ringA1_ = 0.0, ringA2_ = 0.0, ringSin_ = 0.0;
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
 */
class T5ynthTremolo
{
public:
    void prepare(double sampleRate, int samplesPerBlock);
    void reset();

    void setRate(float hz);          ///< 0.1…20 Hz
    void setDepth(float depth);      ///< 0…1, 0 is bypass and the default
    void setStereo(float stereo);    ///< 0 = amplitude, 1 = full pan

    bool wants() const noexcept { return depth_ > 0.0001f; }
    void processBlock(juce::AudioBuffer<float>& buffer);

private:
    double phase_ = 0.0, inc_ = 0.0, sr_ = 44100.0;
    juce::SmoothedValue<float> depthS_, stereoS_;
    float depth_ = 0.0f, stereo_ = 0.0f, rateHz_ = 5.5f;
};

/** juce::dsp::Chorus. Bypassed at mix == 0, which is the default. */
class T5ynthChorus
{
public:
    void prepare(double sampleRate, int samplesPerBlock);
    void reset();

    void setRate(float hz);        ///< 0.05…10 Hz
    void setDepth(float depth);    ///< 0…1
    void setMix(float mix);        ///< 0…1, 0 is bypass and the default

    bool wants() const noexcept { return mix_ > 0.0001f; }
    void processBlock(juce::AudioBuffer<float>& buffer);

private:
    juce::dsp::Chorus<float> chorus_;
    float mix_ = 0.0f;
    int flush_ = 0, flushLen_ = 0, maxBlock_ = 0;
    bool prepared_ = false;
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
};
