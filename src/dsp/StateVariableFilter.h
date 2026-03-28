#pragma once
#include <JuceHeader.h>

/**
 * State-variable filter wrapping juce::dsp::StateVariableTPTFilter.
 *
 * Supports lowpass, highpass, and bandpass modes.
 */
class T5ynthFilter
{
public:
    T5ynthFilter() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void processBlock(juce::AudioBuffer<float>& buffer);
    void reset();

    /** Set cutoff frequency in Hz. */
    void setCutoff(float hz);

    /** Set resonance (0-1). */
    void setResonance(float q);

    /** Set filter type: 0=Lowpass, 1=Highpass, 2=Bandpass. */
    void setType(int type);

    /** Set dry/wet mix (0=fully dry / bypassed, 1=fully wet / filtered). */
    void setMix(float mix);

private:
    juce::dsp::StateVariableTPTFilter<float> filter;
    juce::dsp::DryWetMixer<float> mixer;
    double sr = 44100.0;
    int blockSize = 512;
    float currentMix = 1.0f;
    int currentType = -1;
    bool prepared = false;
};
