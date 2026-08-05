#include "ADSREnvelope.h"

namespace
{
    /** An anchor is MATCHED, not compared: JUCE hands a parameter's value on
     *  without snapping it to the interval, so the SLog anchor arrives as
     *  -0.49999997 as readily as -0.5 and an `==` would drop it onto the blend.
     *  A tenth of the 0.01 parameter step is far too tight to catch a neighbour
     *  and far too loose to miss an anchor. */
    inline bool atAnchor(float b, float anchor) { return std::abs(b - anchor) < 1.0e-3f; }

    /** Convex slow-start blend for u ∈ [0,4]: t, t², t³, t⁴, t⁵ at the integers,
     *  linear in u between them. Multiplies only — no pow per sample — and
     *  monotone throughout (each segment's derivative is a sum of non-negative
     *  terms). u = 2·|bend|, so the named poles are u=2 and the widest bend is
     *  u=4. The blend is never asked for an anchor: applyCurve returns those
     *  from the original expressions, because `1 - inv*inv` contracts into an
     *  fma while the blend's two-statement form does not, and the two differ in
     *  the last bit — one ulp is inaudible, but "the old shapes come back
     *  unchanged" is either true or it is a hedge. */
    inline float convexBlend(float t, float u)
    {
        const float t2 = t * t;
        if (u <= 1.0f) return t + u * (t2 - t);
        const float t3 = t2 * t;
        if (u <= 2.0f) return t2 + (u - 1.0f) * (t3 - t2);
        const float t4 = t2 * t2;
        if (u <= 3.0f) return t3 + (u - 2.0f) * (t4 - t3);
        const float t5 = t4 * t;
        return t4 + std::min(1.0f, u - 3.0f) * (t5 - t4);
    }
}

float ADSREnvelope::applyCurve(float t, float bend)
{
    const float b = clampBend(bend);
    if (atAnchor(b, 0.0f)) return t;                             // Lin

    if (b > 0.0f)                                                // concave, fast start
    {
        const float inv = 1.0f - t;
        if (atAnchor(b, 1.0f)) return 1.0f - inv * inv * inv;    // Exp
        if (atAnchor(b, 0.5f)) return 1.0f - inv * inv;          // SExp
        return 1.0f - convexBlend(inv, 2.0f * b);
    }

    if (atAnchor(b, -1.0f)) return t * t * t;                    // Log
    if (atAnchor(b, -0.5f)) return t * t;                        // SLog
    return convexBlend(t, -2.0f * b);                            // convex, slow start
}

float ADSREnvelope::applyReleaseCurve(float t, float bend)
{
    const float b = clampBend(bend);
    if (b <= 0.0f)
        return applyCurve(t, b);

    // Concave half: RC discharge e^-kt, offset and rescaled so it reaches 1.0
    // (i.e. silence) exactly at t = 1. k(b) passes through the two named anchors
    // this half inherited — k=3 at SExp (b=0.5), k=5 at Exp (b=1), the very
    // τ = release/3 and release/5 the pre-bend release used — and through k=0
    // (linear) at b=0, so the two halves meet without a step. Past the pole it
    // keeps climbing to k=8.33, which is the long quiet tail inside the set
    // release time that the old maximum could not reach.
    const float k = releaseKForBend(b);
    if (k < 1.0e-3f)          // 1 − e^-k loses its significant digits below this;
        return t;             // the shape there is linear to within 0.03% anyway
    const float floorE = std::exp(-k);
    return (1.0f - std::exp(-k * t)) / (1.0f - floorE);
}

void ADSREnvelope::prepare(double sampleRate)
{
    sr = sampleRate;
    reset();
}

void ADSREnvelope::reset()
{
    state = State::Idle;
    currentLevel = 0.0f;
    targetVelocity = 1.0f;
    attackStartLevel = 0.0f;
    attackTarget = 1.0f;
    attackSampleCount = 0;
    attackTotalSamples = 1;
    decayStartLevel = 1.0f;
    decayTarget = sustainLevel;
    decaySampleCount = 0;
    decayTotalSamples = 1;
    releaseStartLevel = 0.0f;
    releaseSampleCount = 0;
    releaseTotalSamples = 1;
    holdSampleCount = 0;
    holdTotalSamples = 1;
    noteHeld = false;
    bypassed = false;
}

void ADSREnvelope::beginAttack()
{
    // Soft retrigger from the current level — shared by noteOn and the loop cycle.
    const float peak   = targetVelocity;
    const float atkSec = std::max(attackMs / 1000.0f, MIN_RAMP_SEC);

    attackStartLevel   = currentLevel;
    attackTarget       = peak;
    attackSampleCount  = 0;
    attackTotalSamples = std::max(1, static_cast<int>(atkSec * static_cast<float>(sr)));

    // Pre-compute decay parameters (used when attack finishes)
    const float susLevel = sustainLevel * peak;
    const float decSec   = std::max(decayMs / 1000.0f, MIN_RAMP_SEC);

    decayStartLevel  = peak;
    decayTarget      = susLevel;
    decaySampleCount = 0;
    decayTotalSamples = std::max(1, static_cast<int>(decSec * static_cast<float>(sr)));

    state = State::Attack;
}

void ADSREnvelope::beginRelease()
{
    const float relSec = std::max(releaseMs / 1000.0f, MIN_RAMP_SEC);

    releaseStartLevel  = currentLevel;
    releaseSampleCount = 0;
    releaseTotalSamples = std::max(1, static_cast<int>(relSec * static_cast<float>(sr)));

    // Concave half: precompute the RC constants once per release so the loop
    // costs one std::exp per sample. releaseK == 0 marks the convex half, which
    // is progress-based like attack/decay. Frozen for the duration of THIS
    // release on purpose: a bend moved while a note is dying takes effect on the
    // next one instead of stepping the level mid-fall, which is what the old
    // curve switch did (it changed families between two samples, and clicked).
    releaseBendActive = releaseBend;
    releaseK = (releaseBendActive > 0.0f) ? releaseKForBend(releaseBendActive) : 0.0f;
    if (releaseK < 1.0e-3f) releaseK = 0.0f;          // see applyReleaseCurve
    releaseFloor   = (releaseK > 0.0f) ? std::exp(-releaseK) : 0.0f;
    releaseInvSpan = (releaseK > 0.0f) ? 1.0f / (1.0f - releaseFloor) : 1.0f;

    state = State::Release;
}

void ADSREnvelope::noteOn(float velocity)
{
    targetVelocity = velocity;
    bypassed = false;
    noteHeld = true;
    beginAttack();   // soft retrigger from current level
}

void ADSREnvelope::noteOff()
{
    noteHeld = false;   // ends any loop: the cycle releases to Idle, no retrigger
    if (state == State::Idle) return;
    beginRelease();
}

void ADSREnvelope::bypass()
{
    bypassed = true;
    currentLevel = 1.0f;
    state = State::Sustain;
}

float ADSREnvelope::processSample()
{
    if (bypassed)
        return 1.0f;

    switch (state)
    {
        case State::Idle:
            return 0.0f;

        case State::Attack:
        {
            attackSampleCount++;
            float t = std::min(1.0f, static_cast<float>(attackSampleCount)
                                   / static_cast<float>(attackTotalSamples));
            float shaped = applyCurve(t, attackBend);
            currentLevel = attackStartLevel + (attackTarget - attackStartLevel) * shaped;

            if (t >= 1.0f)
            {
                currentLevel = attackTarget;
                decayStartLevel = currentLevel;
                decaySampleCount = 0;
                state = State::Decay;
            }
            return currentLevel;
        }

        case State::Decay:
        {
            decaySampleCount++;
            float t = std::min(1.0f, static_cast<float>(decaySampleCount)
                                   / static_cast<float>(decayTotalSamples));
            float shaped = applyCurve(t, decayBend);
            currentLevel = decayStartLevel + (decayTarget - decayStartLevel) * shaped;

            if (t >= 1.0f)
            {
                currentLevel = decayTarget;
                if (looping)
                {
                    // Loop cycle: timed Hold at the sustain level, then Release → repeat.
                    holdSampleCount  = 0;
                    const float holdSec = std::max(holdMs / 1000.0f, 0.0f);
                    holdTotalSamples = std::max(1, static_cast<int>(holdSec * static_cast<float>(sr)));
                    state = State::Hold;
                }
                else
                    state = State::Sustain;
            }
            return currentLevel;
        }

        case State::Hold:
        {
            // Loop-only plateau at the (decayed) sustain level.
            if (! looping)            // loop switched off mid-hold → behave like Sustain
            {
                state = State::Sustain;
                return currentLevel;
            }
            holdSampleCount++;
            if (holdSampleCount >= holdTotalSamples)
                beginRelease();   // per-cycle release to zero (then retrigger)
            return currentLevel;
        }

        case State::Sustain:
            // Non-loop: hold at sustain level until noteOff. (Loop path uses Hold.)
            // If loop is switched on while parked here (toggled mid-note), enter the
            // cycle — mirrors the Release-exit gate; noteOff clears noteHeld first, so
            // this can never retrigger after release.
            if (looping && noteHeld)
                beginAttack();
            return currentLevel;

        case State::Release:
        {
            releaseSampleCount++;

            const float t = std::min(1.0f, static_cast<float>(releaseSampleCount)
                                         / static_cast<float>(releaseTotalSamples));
            if (releaseK > 0.0f)
            {
                // Concave half — the RC discharge, offset and rescaled to reach
                // zero AT the set release time. Algebraically identical to
                // releaseStartLevel · (1 − applyReleaseCurve(t, releaseBendActive)),
                // which is what the ADSR graph draws.
                currentLevel = releaseStartLevel
                             * (std::exp(-releaseK * t) - releaseFloor) * releaseInvSpan;
            }
            else
            {
                // Convex half (and linear): progress-based, like attack/decay.
                currentLevel = releaseStartLevel * (1.0f - applyCurve(t, releaseBendActive));
            }

            // Both halves land on 0 at t = 1; the threshold is the -80 dB floor
            // for a release cut short by a re-trigger, not the normal exit.
            if (t >= 1.0f || currentLevel < 1e-4f)
            {
                currentLevel = 0.0f;
                if (looping && noteHeld)
                    beginAttack();      // loop: start the next cycle from zero
                else
                    state = State::Idle;
            }
            return currentLevel;
        }
    }

    return 0.0f;
}
