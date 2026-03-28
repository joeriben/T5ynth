#pragma once
#include <cmath>
#include <algorithm>

/**
 * ADSR envelope — exact port of useModulation.ts DCA/callback envelope logic.
 *
 * DCA behavior (from reference):
 *   Attack:  linear ramp from current level to velocity × amount (soft retrigger)
 *   Decay:   linear ramp from peak to sustain × peak
 *   Sustain: holds at sustain × peak
 *   Release: RC-discharge e^(-t/τ), τ = releaseMs/5, then hard-zero
 *   Loop:    re-triggers Attack when sustain is reached
 *   Minimum ramp: 3ms for all stages
 */
class ADSREnvelope
{
public:
    ADSREnvelope() = default;

    void prepare(double sampleRate);

    void setAttack(float ms)      { attackMs = std::max(0.0f, ms); }
    void setDecay(float ms)       { decayMs = std::max(0.0f, ms); }
    void setSustain(float level)   { sustainLevel = std::clamp(level, 0.0f, 1.0f); }
    void setRelease(float ms)     { releaseMs = std::max(0.0f, ms); }

    void noteOn(float velocity = 1.0f);
    void noteOff();

    /** Bypass envelope (gain = 1 immediately, for non-MIDI playback). */
    void bypass();

    /** Enable looping: re-enter Attack when Sustain is reached. */
    void setLooping(bool shouldLoop) { looping = shouldLoop; }

    /** Process one sample, returns gain value 0–1. */
    float processSample();

    bool isIdle() const { return state == State::Idle; }

private:
    enum class State { Idle, Attack, Decay, Sustain, Release };
    State state = State::Idle;

    float attackMs     = 0.0f;
    float decayMs      = 0.0f;
    float sustainLevel = 1.0f;
    float releaseMs    = 0.0f;

    float currentLevel   = 0.0f;
    float targetVelocity = 1.0f;
    float amount         = 1.0f; // Set externally via processBlock

    // Attack/Decay: linear ramp targets and per-sample increment
    float attackTarget   = 1.0f;
    float attackIncr     = 0.0f;
    float decayTarget    = 1.0f;
    float decayIncr      = 0.0f;

    // Release: RC-discharge state
    float releaseStartLevel = 0.0f;
    float releaseTau        = 0.01f;  // time constant in seconds (releaseMs/5)
    int   releaseSampleCount = 0;
    int   releaseTotalSamples = 0;

    double sr = 44100.0;
    bool bypassed = false;
    bool looping  = false;

    static constexpr float MIN_RAMP_SEC = 0.003f; // 3ms minimum ramp
};
