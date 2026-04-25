#pragma once
#include <JuceHeader.h>

/**
 * Convolution-based reverb wrapping juce::dsp::Convolution.
 *
 * Loads impulse responses from binary data and provides dry/wet mixing.
 */
class ConvolutionReverb
{
public:
    ConvolutionReverb();

    void prepare(double sampleRate, int samplesPerBlock);
    void processBlock(juce::AudioBuffer<float>& buffer);
    void reset();

    /** Set dry/wet mix (0=dry, 1=wet). */
    void setMix(float mix);

    /** Load an impulse response from raw data (WAV or AIFF). */
    void loadImpulseResponse(const void* data, size_t size);

private:
    juce::dsp::Convolution convolution;
    juce::dsp::DryWetMixer<float> mixer;
    double sr = 44100.0;
    float wetMix = 0.0f;
    bool prepared = false;
    bool irLoaded = false;

    // Silence detection — skip FFT convolution only after output has truly decayed
    int silentOutputBlocks = 0;
    static constexpr int SILENCE_CONFIRM_BLOCKS = 4;
};
