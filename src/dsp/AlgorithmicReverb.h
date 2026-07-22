#pragma once
#include <JuceHeader.h>
#include "EarlyReflections.h"

/**
 * Algorithmic reverb wrapping juce::dsp::Reverb (Freeverb).
 *
 * Two voicings share this class:
 *   Freeverb  - JUCE's reverb as-is: the Schroeder-Moorer late field only.
 *   Freeverb+ - the same late field FED FROM an early-reflection bank, i.e.
 *               Moorer's complete 1979 design. The late tail then grows out of
 *               the reflections instead of switching on as a bare wash, and Room
 *               scales the reflection geometry rather than only the decay.
 *
 * Same interface as ConvolutionReverb for drop-in use.
 */
class AlgorithmicReverb
{
public:
    AlgorithmicReverb() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void processBlock(juce::AudioBuffer<float>& buffer);
    void reset();

    /** Set dry/wet mix (0=dry, 1=wet). */
    void setMix(float mix);

    /** Set room size (0=small, 1=large). */
    void setRoomSize(float size);

    /** Set high-frequency damping (0=bright, 1=dark). */
    void setDamping(float damp);

    /** Set stereo width (0=mono, 1=full stereo). */
    void setWidth(float w);

    /** Freeverb+ (true) prepends the early-reflection bank; Freeverb (false) is
        JUCE's reverb untouched, so existing presets keep their exact voicing. */
    void setEarlyReflections(bool enabled);

private:
    juce::dsp::Reverb reverb;
    juce::dsp::DryWetMixer<float> mixer;

    // Freeverb+ front end. `erBuffer` keeps the reflections while `reverb`
    // overwrites the block in place, so the output can carry both. Sized in
    // prepare() — never resized on the audio thread.
    EarlyReflections early;
    juce::AudioBuffer<float> erBuffer;
    bool erEnabled = false;
    // A host may hand us a block LARGER than the one prepare() sized the scratch
    // buffers for. Both erBuffer and JUCE's DryWetMixer are bound to that maximum,
    // so processBlock splits such a block into chunks instead of skipping the
    // reflections — silently reverting the voicing to plain Freeverb for a block
    // is worse than doing the work twice.
    int maxBlockSize = 0;
    void processChunk(juce::AudioBuffer<float>& buffer);
    // Balance of the two halves. The reflections sit just under the tail so they
    // define the space without turning the reverb into a slapback.
    static constexpr float kErGain   = 0.80f;
    static constexpr float kLateGain = 1.00f;
    float wetMix = 0.0f;
    bool prepared = false;
    double sr = 44100.0;

    // Output tone LP driven by Damp: JUCE's internal comb damping is intentionally
    // capped (it rings if pushed past ~0.4 internal), so it darkens only weakly and
    // its top half is nearly inaudible. A one-pole LP on the wet output extends the
    // Damp control into a strong, monotone bright->dark sweep across the whole knob.
    float dampLpCoeff = 1.0f;     // 1 = fully open (no darkening)
    float dampLpStateL = 0.0f, dampLpStateR = 0.0f;

    // Silence detection — skip processing only after output has truly decayed
    int silentOutputBlocks = 0;
    static constexpr int SILENCE_CONFIRM_BLOCKS = 4;
};
