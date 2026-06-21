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
 *   Loop:    a self-retriggering complex-LFO cycle while the note is held —
 *            Attack → Decay → Hold (timed, at sustain level) → Release → repeat.
 *            Hold length comes from setHoldMs(); note-off ends the cycle and
 *            releases to Idle (no further retrigger).
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

    /** Loop-only: time held at the sustain level before the per-cycle Release. */
    void setHoldMs(float ms)      { holdMs = std::max(0.0f, ms); }

    void setAttackCurve(CurveShape s)   { attackCurve = s; }
    void setDecayCurve(CurveShape s)    { decayCurve = s; }
    void setReleaseCurve(CurveShape s)  { releaseCurve = s; }

    void noteOn(float velocity = 1.0f);
    void noteOff();

    /** Bypass envelope (gain = 1 immediately, for non-MIDI playback). */
    void bypass();

    /** Enable looping: run a self-retriggering A→D→Hold→R cycle while held. */
    void setLooping(bool shouldLoop) { looping = shouldLoop; }

    /** Process one sample, returns gain value 0–1. */
    float processSample();

    bool isIdle() const { return state == State::Idle; }

    /** Apply curve shaping to normalized progress t ∈ [0,1]. Pure & stateless —
     *  also used by the GUI's AdsrGraph so the drawn envelope matches the audio. */
    static float applyCurve(float t, CurveShape shape);

private:
    enum class State { Idle, Attack, Decay, Hold, Sustain, Release };
    State state = State::Idle;

    /** Begin (or loop-retrigger) the Attack stage from the current level. */
    void beginAttack();
    /** Begin the Release stage from the current level. */
    void beginRelease();

    float attackMs     = 0.0f;
    float decayMs      = 0.0f;
    float sustainLevel = 1.0f;
    float releaseMs    = 0.0f;
    float holdMs       = 0.0f;     // loop-only hold at sustain level

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

    // Hold: loop-only timed plateau at the sustain level
    int   holdSampleCount  = 0;
    int   holdTotalSamples = 1;

    // Release: RC-discharge for Exp, progress-based for Lin/Log
    float releaseStartLevel  = 0.0f;
    float releaseTau         = 0.01f;  // time constant in seconds (releaseMs/5)
    int   releaseSampleCount  = 0;
    int   releaseTotalSamples = 1;

    double sr = 44100.0;
    bool bypassed = false;
    bool looping  = false;
    bool noteHeld = false;             // true between noteOn and noteOff (loop gate)

    static constexpr float MIN_RAMP_SEC = 0.003f; // 3ms minimum ramp
};
