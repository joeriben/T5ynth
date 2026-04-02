#pragma once
#include <cmath>

/**
 * Low-frequency oscillator with multiple waveforms.
 *
 * Runs per-sample. Waveform types: 0=Sine, 1=Triangle, 2=Saw, 3=Square, 4=S&H.
 */
class LFO
{
public:
    LFO() = default;

    void prepare(double sampleRate);
    void reset();

    /** Process one sample, returns value in [-1, 1]. */
    float processSample();

    /** Advance phase by numSamples without computing output.
     *  Keeps free-running LFOs phase-accurate during idle. */
    void advancePhase(int numSamples);

    /** Set rate in Hz. */
    void setRate(float hz) { rate = hz; }

    /** Set waveform type (0=Sine, 1=Triangle, 2=Saw, 3=Square). */
    void setWaveform(int type) { waveform = type; }

    /** Set output depth/amount (multiplier). */
    void setDepth(float d) { depth = d; }

    float getRate() const { return rate; }
    float getDepth() const { return depth; }
    int getWaveform() const { return waveform; }

private:
    double sr = 44100.0;
    double phase = 0.0;
    float rate = 1.0f;
    int waveform = 0;
    float depth = 1.0f;

    static constexpr double TWO_PI = 6.283185307179586;
};
