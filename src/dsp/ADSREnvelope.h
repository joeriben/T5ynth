#pragma once
#include <cmath>
#include <algorithm>

/** The five NAMED points on the continuous bend axis. The stage shape itself is
 *  a float (see `bend` below); this enum survives because presets, the .t5p JSON
 *  and the choice table in BlockParams.h store a shape by key, and because the
 *  five keys still name exactly the five curves they always did. */
enum class CurveShape : int { Log = 0, SoftLog = 1, Lin = 2, SoftExp = 3, Exp = 4 };

/** Anchor index (0..4) → bend value. Log=-1, SLog=-0.5, Lin=0, SExp=+0.5, Exp=+1. */
inline constexpr float bendFromCurveIndex(int i) { return (static_cast<float>(i) - 2.0f) * 0.5f; }
inline constexpr float bendFromCurveShape(CurveShape s) { return bendFromCurveIndex(static_cast<int>(s)); }

/** Widest bend any stage can hold: one unit PAST the named poles, on the side
 *  that stage sags towards. Mirrors EnvCurve::kBendSag, which is what the
 *  parameter ranges are built from (PluginProcessor static_asserts the pair). */
inline constexpr float kMaxEnvBend = 2.0f;

/** RC steepness of the release's concave half: k(b) = 5·b^log2(5/3), the power
 *  law through k(0.5)=3 (SExp) and k(1)=5 (Exp) — the two τ the pre-bend release
 *  had. A power law rather than a quadratic through the same two points because
 *  it keeps CLIMBING past the pole instead of turning back: every doubling of b
 *  multiplies k by 5/3, so the widest bend is k = 8.33. */
inline float releaseKForBend(float b)
{
    return b <= 0.0f ? 0.0f : 5.0f * std::pow(b, 0.7369655942f);
}

/**
 * ADSR envelope with per-stage curve shaping.
 *
 * DCA behavior:
 *   Attack:  ramp from current level to velocity × amount (soft retrigger)
 *   Decay:   ramp from peak to sustain × peak
 *   Sustain: holds at sustain × peak
 *   Release: ramp to zero, reached AT the set release time on every bend
 *   Loop:    a self-retriggering complex-LFO cycle while the note is held —
 *            Attack → Decay → Hold (timed, at sustain level) → Release → repeat.
 *            Hold length comes from setHoldMs(); note-off ends the cycle and
 *            releases to Idle (no further retrigger).
 *   Minimum ramp: 3ms for all stages
 *
 * Stage shape — one continuous bend per stage:
 *   b = -2.0        convex, quintic — past the old maximum (attack only)
 *   b = -1.0  Log   slow initial change, accelerates (convex, cubic)
 *   b = -0.5  SLog  mild slow start (convex, quadratic)
 *   b =  0.0  Lin   constant rate
 *   b = +0.5  SExp  mild fast start (concave, quadratic)
 *   b = +1.0  Exp   fast initial change, decelerates (concave, cubic)
 *   b = +2.0        concave, quintic / RC k=8.33 — past the old maximum
 *                   (decay and release only)
 * The five old named shapes are exactly those five points. On attack, decay and
 * the release's convex half (Log/SLog/Lin) the curve is bit-identical to what
 * those names produced before; everything between them is new. Attack/decay
 * interpolate t → t² → t³ (multiplies only).
 *
 * ONE deliberate change to an old sound, and the reason the release could join
 * the same axis at all: SExp/Exp were an RC discharge that ran PAST the set
 * release time — τ = release/3 resp. release/5 against a -80 dB cutoff, so the
 * note took 3.07× resp. 1.84× the set time to die, and reached only -26 dB
 * resp. -43 dB at the time the fader named. The release now normalises that
 * same discharge to hit zero AT the set time (see applyReleaseCurve), which is
 * also the curve the ADSR graph has always drawn. Audible on SExp (its tail
 * below -26 dB is gone) and on loop mode with a concave release (the cycle is
 * now A+D+Hold+R, no longer stretched by the tail).
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

    void setAttackBend(float b)   { attackBend  = clampBend(b); }
    void setDecayBend(float b)    { decayBend   = clampBend(b); }
    void setReleaseBend(float b)  { releaseBend = clampBend(b); }

    void noteOn(float velocity = 1.0f);
    void noteOff();

    /** Bypass envelope (gain = 1 immediately, for non-MIDI playback). */
    void bypass();

    /** Enable looping: run a self-retriggering A→D→Hold→R cycle while held. */
    void setLooping(bool shouldLoop) { looping = shouldLoop; }

    /** Process one sample, returns gain value 0–1. */
    float processSample();

    bool isIdle() const { return state == State::Idle; }

    /** Attack/decay shaping of normalized progress t ∈ [0,1] under bend b.
     *  Pure & stateless — also used by the GUI's AdsrGraph so the drawn envelope
     *  matches the audio. */
    static float applyCurve(float t, float bend);

    /** Release shaping of normalized progress t ∈ [0,1] under bend b. Convex half
     *  (b ≤ 0) is applyCurve; concave half is the RC discharge normalised to hit
     *  1.0 exactly at t = 1, i.e. the release really ends at the set release time.
     *  Returns the FALLEN fraction: level = releaseStartLevel · (1 − result). */
    static float applyReleaseCurve(float t, float bend);

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

    static float clampBend(float b) { return std::max(-kMaxEnvBend, std::min(kMaxEnvBend, b)); }

    float attackBend  = 0.0f;    // Lin
    float decayBend   = 0.0f;    // Lin
    float releaseBend = 1.0f;    // Exp

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

    // Release: progress-based on both halves. On the concave half the RC
    // discharge's two constants are precomputed at beginRelease so the per-sample
    // cost stays one std::exp — the same as the pre-bend RC release. They express
    // applyReleaseCurve exactly (1 − shaped == (e^-kt − floor)·invSpan), which is
    // what keeps the drawn envelope and the heard one the same curve.
    // All four are (re)computed together in beginRelease; the values here are the
    // no-release-yet state, and they have to agree with each other rather than
    // with the default bend, because k > 0 is what selects the RC branch.
    float releaseStartLevel  = 0.0f;
    float releaseBendActive  = 1.0f;   // the bend THIS release runs on
    float releaseK           = 0.0f;   // RC steepness, 0 on the convex half
    float releaseFloor       = 0.0f;   // e^-k
    float releaseInvSpan     = 1.0f;   // 1/(1 − e^-k)
    int   releaseSampleCount  = 0;
    int   releaseTotalSamples = 1;

    double sr = 44100.0;
    bool bypassed = false;
    bool looping  = false;
    bool noteHeld = false;             // true between noteOn and noteOff (loop gate)

    static constexpr float MIN_RAMP_SEC = 0.003f; // 3ms minimum ramp
};
