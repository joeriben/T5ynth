#pragma once
#include <JuceHeader.h>

/**
 * Delay effect — port of useEffects.ts delay subsystem.
 *
 * Stereo delay with time, feedback, dry/wet mix, and feedback damping LP filter.
 * Damping: LP filter in feedback loop with exponential mapping
 *   0 = bright (20kHz), 1 = dark (500Hz): freq = 20000 * pow(500/20000, d).
 * Dry compensation: dryGain = 1 - mix * 0.3 (when enabled).
 */
class T5ynthDelayLine
{
public:
    T5ynthDelayLine() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void processBlock(juce::AudioBuffer<float>& buffer);
    void reset();

    /** Set delay time in milliseconds. */
    void setTime(float ms);

    /** Set feedback amount (0-0.95). */
    void setFeedback(float fb);

    /** Set dry/wet mix (0=dry, 1=wet). Send amount, not crossfade. */
    void setMix(float mix);

    /** Set feedback damping (0=bright 20kHz, 1=dark 500Hz). */
    void setDamp(float d);

    float getMix() const { return wetMix; }

private:
    juce::dsp::DelayLine<float> delayLine { 220500 }; // Max ~5 seconds at 44.1kHz

    // Feedback damping: one LP filter per channel
    juce::dsp::IIR::Filter<float> dampFilterL;
    juce::dsp::IIR::Filter<float> dampFilterR;

    double sr = 44100.0;
    float delayTimeMs = 250.0f;     // Reference default
    float feedback = 0.35f;          // Reference default
    float wetMix = 0.3f;            // Reference default (send amount)
    float dampFreq = 4000.0f;       // Default at damp=0.5
    bool prepared = false;

    // Silence detection — skip per-sample processing after tail has decayed
    int silentInputBlocks = 0;
    static constexpr int DELAY_TAIL_BLOCKS = 344;  // ~4s at 44.1kHz/512

    void updateDampCoeffs();
};
