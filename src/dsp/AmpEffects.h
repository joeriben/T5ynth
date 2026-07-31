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
 * rather than reimplementing them. Distortion is `tanh`, the same shape — and
 * now, with no make-up, the same LAW — the filter's own drive already uses in
 * SynthVoice, so the two stages do not disagree about what saturation sounds
 * like here.
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
 * tanh saturation with 2x oversampling. Bypassed at mix == 0, which is the
 * default.
 *
 * NO MAKE-UP GAIN, deliberately. The obvious 1/g (tanh's slope at zero, so
 * exact for the quiet part of the waveform) turns the top half of the control
 * into a large volume DROP: measured out/in RMS on a 0.5 sine, −4.90 dB at 12 dB
 * of drive and −27.06 dB at 36, because by then the output is a square wave
 * divided by 63. A drive knob that gets quieter as it is turned up is not a
 * drive knob. Without it the control runs the other way and stays bounded,
 * because tanh is: measured on the same 0.5 sine, +0.07 dB at 0.5 dB of drive,
 * +4.34 at 6, +7.20 at 12 and +8.95 at 36, where the output is a square and the
 * ceiling is the +9.0 dB a full-scale square holds over that sine. It does
 * exactly what `SynthVoice`'s filter drive does, with the same opcode and now
 * the same law.
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
    /** True while a parameter ramp is still travelling, so the stage may not
        stop yet however its target reads. */
    bool moving() const noexcept
    {
        return gain_.isSmoothing() || wet_.isSmoothing() || dryG_.isSmoothing();
    }

    std::unique_ptr<juce::dsp::Oversampling<float>> os_;
    juce::AudioBuffer<float> raw_;              ///< engage crossfade source only
    juce::SmoothedValue<float> gain_, wet_, dryG_, engage_;
    float driveDb_ = 0.0f, mix_ = 0.0f;
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
