#include "ADSREnvelope.h"

float ADSREnvelope::applyCurve(float t, CurveShape shape)
{
    switch (shape)
    {
        case CurveShape::Exp:
        {
            float inv = 1.0f - t;
            return 1.0f - inv * inv * inv;   // concave — fast start (cubic)
        }
        case CurveShape::SoftExp:
        {
            float inv = 1.0f - t;
            return 1.0f - inv * inv;         // concave — mild fast start (quadratic)
        }
        case CurveShape::Log:
            return t * t * t;                 // convex — slow start (cubic)
        case CurveShape::SoftLog:
            return t * t;                     // convex — mild slow start (quadratic)
        default:
            return t;                         // linear
    }
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
    bypassed = false;
}

void ADSREnvelope::noteOn(float velocity)
{
    targetVelocity = velocity;
    bypassed = false;

    // Attack ramp (from current level to peak — soft retrigger)
    float peak = targetVelocity;
    float atkSec = std::max(attackMs / 1000.0f, MIN_RAMP_SEC);

    attackStartLevel = currentLevel;
    attackTarget = peak;
    attackSampleCount = 0;
    attackTotalSamples = std::max(1, static_cast<int>(atkSec * static_cast<float>(sr)));

    // Pre-compute decay parameters (used when attack finishes)
    float susLevel = sustainLevel * peak;
    float decSec   = std::max(decayMs / 1000.0f, MIN_RAMP_SEC);

    decayStartLevel = peak;
    decayTarget = susLevel;
    decaySampleCount = 0;
    decayTotalSamples = std::max(1, static_cast<int>(decSec * static_cast<float>(sr)));

    state = State::Attack;
    // Don't reset currentLevel — soft retrigger from current position
}

void ADSREnvelope::noteOff()
{
    if (state == State::Idle) return;

    float relSec = std::max(releaseMs / 1000.0f, MIN_RAMP_SEC);

    releaseStartLevel = currentLevel;
    releaseSampleCount = 0;
    releaseTotalSamples = std::max(1, static_cast<int>(relSec * static_cast<float>(sr)));

    // RC-discharge time constant for Exp mode (original behavior)
    releaseTau = relSec / 5.0f;

    state = State::Release;
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
            float shaped = applyCurve(t, attackCurve);
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
            float shaped = applyCurve(t, decayCurve);
            currentLevel = decayStartLevel + (decayTarget - decayStartLevel) * shaped;

            if (t >= 1.0f)
            {
                currentLevel = decayTarget;
                state = State::Sustain;
            }
            return currentLevel;
        }

        case State::Sustain:
        {
            // Loop: restart attack when sustain is reached
            if (looping)
            {
                float peak = targetVelocity;
                float atkSec = std::max(attackMs / 1000.0f, MIN_RAMP_SEC);
                attackStartLevel = currentLevel;
                attackTarget = peak;
                attackSampleCount = 0;
                attackTotalSamples = std::max(1, static_cast<int>(atkSec * static_cast<float>(sr)));

                float susLevel = sustainLevel * peak;
                float decSec   = std::max(decayMs / 1000.0f, MIN_RAMP_SEC);
                decayStartLevel = peak;
                decayTarget = susLevel;
                decaySampleCount = 0;
                decayTotalSamples = std::max(1, static_cast<int>(decSec * static_cast<float>(sr)));

                state = State::Attack;
            }
            return currentLevel;
        }

        case State::Release:
        {
            releaseSampleCount++;

            if (releaseCurve == CurveShape::Exp || releaseCurve == CurveShape::SoftExp)
            {
                // RC-discharge: e^(-t/τ)
                // Exp: τ = releaseMs/5 (steep), SoftExp: τ = releaseMs/3 (gentler)
                float tau = (releaseCurve == CurveShape::Exp) ? releaseTau : releaseTau * (5.0f / 3.0f);
                float t = static_cast<float>(releaseSampleCount) / static_cast<float>(sr);
                currentLevel = releaseStartLevel * std::exp(-t / tau);
            }
            else
            {
                // Progress-based curve (Lin, SoftLog, or Log)
                float t = std::min(1.0f, static_cast<float>(releaseSampleCount)
                                       / static_cast<float>(releaseTotalSamples));
                float shaped = applyCurve(t, releaseCurve);
                currentLevel = releaseStartLevel * (1.0f - shaped);
            }

            // Threshold cutoff at -80 dB — no audible click
            if (currentLevel < 1e-4f)
            {
                currentLevel = 0.0f;
                state = State::Idle;
            }
            return currentLevel;
        }
    }

    return 0.0f;
}
