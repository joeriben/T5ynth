#pragma once
#include <JuceHeader.h>
#include <cmath>

/**
 * State-variable filter — port of useFilter.ts.
 *
 * Supports lowpass, highpass, bandpass with 12dB or 24dB slope (cascade).
 * Equal-power dry/wet crossfade: dry = cos(mix * pi/2), wet = sin(mix * pi/2).
 * Resonance: UI 0-1 mapped to Q 0.5-18 via 0.5 * pow(36, r).
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

    /** Set resonance (0-1), mapped internally to Q 0.5-18. */
    void setResonance(float r);

    /** Set filter type: 0=Lowpass, 1=Highpass, 2=Bandpass. */
    void setType(int type);

    /** Set dry/wet mix (0=fully dry / bypassed, 1=fully wet / filtered).
     *  Uses equal-power crossfade: dry = cos(mix*pi/2), wet = sin(mix*pi/2). */
    void setMix(float mix);

    /** Set filter slope: 0=12dB/oct (single), 1=24dB/oct (cascade). */
    void setSlope(int slope);

    /** Map UI resonance 0-1 to filter Q 0.5-18 (exponential). */
    static float resonanceToQ(float r)
    {
        return 0.5f * std::pow(36.0f, std::clamp(r, 0.0f, 1.0f));
    }

private:
    juce::dsp::StateVariableTPTFilter<float> filter1;
    juce::dsp::StateVariableTPTFilter<float> filter2; // second stage for 24dB cascade
    double sr = 44100.0;
    int blockSize = 512;
    float currentMix = 1.0f;
    int currentType = -1;
    int currentSlope = 0; // 0=12dB, 1=24dB
    bool prepared = false;
};
