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

    // Silence detection
    int silentInputBlocks = 0;
    static constexpr int REVERB_TAIL_BLOCKS = 344;
};
