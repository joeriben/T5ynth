#pragma once
#include <cmath>
#include <cstdint>

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

    /** Set waveform type (0=Sine, 1=Triangle, 2=Saw, 3=Square, 4=S&H). */
    void setWaveform(int type) { waveform = type; }

    /** Set output depth/amount (multiplier). */
    void setDepth(float d) { depth = d; }

    /** Arm/disarm beat-sync start. While armed the LFO holds at phase 0 and
     *  outputs zero (no advance, no modulation); releasing lets its cycle
     *  begin from phase 0 — used to defer a sync-mode start to the
     *  sequencer's downbeat. */
    void setArmed(bool a) { armed = a; if (a) phase = 0.0; }
    bool isArmed() const { return armed; }

    float getRate() const { return rate; }
    float getDepth() const { return depth; }
    int getWaveform() const { return waveform; }

private:
    double sr = 44100.0;
    double phase = 0.0;
    float rate = 1.0f;
    int waveform = 0;
    float depth = 1.0f;
    bool armed = false;  // held silent at phase 0 until released on the downbeat

    // Sample-and-hold state (latched at each phase wrap)
    float heldValue_ = 0.0f;
    uint64_t rngState_ = 0xA1B2C3D4E5F60789ULL;
    float nextRandom(); // xorshift64 → [-1, 1]

    static constexpr double TWO_PI = 6.283185307179586;
};
