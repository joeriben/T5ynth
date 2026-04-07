#pragma once
#include <cmath>
#include <array>

/**
 * Drift LFO system — port of useDriftLfo.ts.
 *
 * 3 internal LFOs with selectable waveform (sine/tri/sq/saw).
 * Output = waveform(phase) * depth * halfRange (offset added to base).
 * Phase resets to 0 on target change.
 *
 * Target ranges (from reference):
 *   alpha:      [-2, 2]  halfRange = 2
 *   sem_axis_*: [-2, 2]  halfRange = 2
 *   wt_scan:    [0, 1]   halfRange = 0.5
 */
class DriftLFO
{
public:
    /** Target parameter indices. Matches APVTS choice order (0=None, 1=Alpha, etc.) */
    enum Target
    {
        TgtNone = 0,
        TgtAlpha,      // 1
        TgtAxis1,      // 2
        TgtAxis2,      // 3
        TgtAxis3,      // 4
        TgtWtScan,     // 5
        TgtFilter,     // 6
        TgtPitch,      // 7
        TgtDelayTime,  // 8
        TgtDelayFb,    // 9
        TgtDelayMix,   // 10
        TgtReverbMix,  // 11
        TgtEnv1Amt,    // 12
        TgtEnv2Amt,    // 13
        TgtEnv3Amt,    // 14
        TgtNoise,      // 15
        TgtMagnitude,  // 16
        NumTargets
    };

    /** Waveform types. Matches APVTS drift_wave choice order. */
    enum Waveform { Sine = 0, Triangle, Sawtooth, Square };

    static constexpr int NUM_LFOS = 3;

    DriftLFO() = default;

    /** Reset all phases and offsets. */
    void reset();

    /** Advance all LFOs by dt seconds. */
    void tick(double dt);

    /** Get the combined offset for a given target parameter. */
    float getOffsetForTarget(int target) const;

    /** Configure a single internal LFO. */
    void setLfoRate(int lfoIndex, float hz);
    void setLfoDepth(int lfoIndex, float amount);
    void setLfoTarget(int lfoIndex, int target);
    void setLfoWaveform(int lfoIndex, int waveform);

    /** Enable/disable the entire drift system. */
    void setEnabled(bool enabled) { active = enabled; }
    bool isEnabled() const { return active; }

    /** Regen mode: 0=Manual, 1=Auto, 2=1st Bar (bar-boundary trigger). */
    void setRegenMode(int mode) { regenMode = mode; }
    int getRegenMode() const { return regenMode; }

private:
    struct InternalLFO
    {
        double phase = 0.0;
        float rate = 0.01f;     // Hz
        float depth = 0.0f;
        int target = 0;         // APVTS index: 0=None, 1=Alpha, etc.
        int waveform = Sine;
    };

    std::array<InternalLFO, NUM_LFOS> lfos {{
        { 0.0, 0.01f,  0.0f, 0, Sine },      // Drift 1: rate=0.01 Hz
        { 0.0, 0.005f, 0.0f, 0, Triangle },   // Drift 2: rate=0.005 Hz
        { 0.0, 0.002f, 0.0f, 0, Sine },       // Drift 3: rate=0.002 Hz
    }};

    bool active = false;
    int regenMode = 0; // 0=Manual, 1=Auto, 2=1st Bar

    static constexpr double TWO_PI = 6.283185307179586;

    /** Compute waveform value for phase 0-1, returns -1..+1. */
    static float waveformValue(double phase, int type);

    /** Get half-range for a target (APVTS index: 0=None, 1=Alpha, ..., 5=WtScan). */
    static float halfRangeForTarget(int target);
};
