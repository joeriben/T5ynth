#pragma once
#include <JuceHeader.h>

/**
 * Algorithmic reverb wrapping juce::dsp::Reverb (Freeverb).
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

private:
    juce::dsp::Reverb reverb;
    juce::dsp::DryWetMixer<float> mixer;
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
