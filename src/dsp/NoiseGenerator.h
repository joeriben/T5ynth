#pragma once
#include <cmath>
#include <cstdint>

/**
 * Filtered noise generator for wavetable synthesis.
 *
 * Generates white noise via xorshift64 PRNG, shapes it with a one-pole LP
 * filter ("color" control), and removes DC offset with a one-pole HP blocker.
 * Per-voice instance: each voice gets independent noise that follows the
 * voice's amplitude envelope and filter chain.
 */
class NoiseGenerator
{
public:
    NoiseGenerator() = default;

    void prepare(double sampleRate);
    void reset();

    /** Set noise color filter cutoff in Hz. Higher = brighter noise.
     *  At 20000 Hz the LP is effectively bypassed (white noise). */
    void setColorCutoff(float hz);

    /** Generate one sample of filtered noise. */
    float processSample();

private:
    double sr_ = 44100.0;

    // xorshift64 PRNG
    uint64_t rngState_ = 0x12345678ABCDEF01ULL;
    float nextWhite();

    // One-pole LP for spectral shaping
    float lpState_ = 0.0f;
    float lpCoeff_ = 1.0f; // 1.0 = no filtering (white)
    float lastCutoff_ = 20000.0f;

    // DC blocker (one-pole HP at ~5 Hz)
    float dcState_ = 0.0f;
    float dcCoeff_ = 0.0f;
};
