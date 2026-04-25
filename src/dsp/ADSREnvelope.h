#pragma once
#include <cmath>
#include <algorithm>

/** Curve shapes for envelope stages. */
enum class CurveShape : int { Log = 0, SoftLog = 1, Lin = 2, SoftExp = 3, Exp = 4 };

/**
 * ADSR envelope with per-stage curve shaping.
 *
 * DCA behavior:
 *   Attack:  ramp from current level to velocity × amount (soft retrigger)
 *   Decay:   ramp from peak to sustain × peak
 *   Sustain: holds at sustain × peak
 *   Release: ramp to zero, then hard-zero at -80dB threshold
 *   Loop:    re-triggers Attack when sustain is reached
 *   Minimum ramp: 3ms for all stages
 *
 * Curve shapes (Log/Lin/Exp):
 *   Exp  — fast initial change, decelerates (concave, cubic)
 *   SExp — mild fast start (concave, quadratic)
 *   Lin  — constant rate (linear)
 *   SLog — mild slow start (convex, quadratic)
 *   Log  — slow initial change, accelerates (convex, cubic)
 */
class ADSREnvelope
{
public:
    ADSREnvelope() = default;

    void prepare(double sampleRate);
    void reset();

    void setAttack(float ms)      { attackMs = std::max(0.0f, ms); }
    void setDecay(float ms)       { decayMs = std::max(0.0f, ms); }
    void setSustain(float level)   { sustainLevel = std::max(0.0f, std::min(1.0f, level)); }
    void setRelease(float ms)     { releaseMs = std::max(0.0f, ms); }

    void setAttackCurve(CurveShape s)   { attackCurve = s; }
    void setDecayCurve(CurveShape s)    { decayCurve = s; }
    void setReleaseCurve(CurveShape s)  { releaseCurve = s; }

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

    CurveShape attackCurve  = CurveShape::Lin;     // index 2
    CurveShape decayCurve   = CurveShape::Lin;     // index 2
    CurveShape releaseCurve = CurveShape::Exp;     // index 4

    float currentLevel   = 0.0f;
    float targetVelocity = 1.0f;

    // Attack: progress-based
    float attackStartLevel   = 0.0f;
    float attackTarget       = 1.0f;
    int   attackSampleCount  = 0;
    int   attackTotalSamples = 1;

    // Decay: progress-based
    float decayStartLevel   = 1.0f;
    float decayTarget       = 1.0f;
    int   decaySampleCount  = 0;
    int   decayTotalSamples = 1;

    // Release: RC-discharge for Exp, progress-based for Lin/Log
    float releaseStartLevel  = 0.0f;
    float releaseTau         = 0.01f;  // time constant in seconds (releaseMs/5)
    int   releaseSampleCount  = 0;
    int   releaseTotalSamples = 1;

    double sr = 44100.0;
    bool bypassed = false;
    bool looping  = false;

    static constexpr float MIN_RAMP_SEC = 0.003f; // 3ms minimum ramp

    /** Apply curve shaping to normalized progress t ∈ [0,1]. */
    static float applyCurve(float t, CurveShape shape);
};
