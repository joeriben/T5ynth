#include "ADSREnvelope.h"

void ADSREnvelope::prepare(double sampleRate)
{
    sr = sampleRate;
    state = State::Idle;
    currentLevel = 0.0f;
    bypassed = false;
}

void ADSREnvelope::noteOn(float velocity)
{
    targetVelocity = velocity;
    bypassed = false;

    // Compute attack ramp (linear, from current level to peak)
    // Reference: gain.linearRampToValueAtTime(peak, now + max(atk, 0.003))
    float peak = targetVelocity; // amount is applied externally in processBlock
    float atkSec = std::max(attackMs / 1000.0f, MIN_RAMP_SEC);
    int   atkSamples = std::max(1, static_cast<int>(atkSec * static_cast<float>(sr)));

    attackTarget = peak;
    attackIncr   = (attackTarget - currentLevel) / static_cast<float>(atkSamples);

    // Pre-compute decay parameters (used when attack finishes)
    float susLevel = sustainLevel * peak;
    float decSec   = std::max(decayMs / 1000.0f, MIN_RAMP_SEC);
    int   decSamples = std::max(1, static_cast<int>(decSec * static_cast<float>(sr)));

    decayTarget = susLevel;
    decayIncr   = (decayTarget - attackTarget) / static_cast<float>(decSamples);

    state = State::Attack;
    // Don't reset currentLevel — soft retrigger from current position
}

void ADSREnvelope::noteOff()
{
    if (state == State::Idle) return;

    // Reference: holdAndRelease — RC discharge with τ = releaseMs/5
    float relSec = std::max(releaseMs / 1000.0f, MIN_RAMP_SEC);
    releaseTau = relSec / 5.0f;
    releaseStartLevel = currentLevel;
    releaseSampleCount = 0;
    releaseTotalSamples = static_cast<int>(relSec * static_cast<float>(sr));

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
            // Linear ramp toward attackTarget (= velocity)
            currentLevel += attackIncr;

            // Check if we've reached or passed the target
            bool reached = (attackIncr >= 0.0f)
                ? (currentLevel >= attackTarget)
                : (currentLevel <= attackTarget);

            if (reached)
            {
                currentLevel = attackTarget;
                state = State::Decay;
            }
            return currentLevel;
        }

        case State::Decay:
        {
            // Linear ramp toward decayTarget (= sustain × velocity)
            currentLevel += decayIncr;

            bool reached = (decayIncr >= 0.0f)
                ? (currentLevel >= decayTarget)
                : (currentLevel <= decayTarget);

            if (reached)
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
                // Re-trigger from current level
                float peak = targetVelocity;
                float atkSec = std::max(attackMs / 1000.0f, MIN_RAMP_SEC);
                int   atkSamples = std::max(1, static_cast<int>(atkSec * static_cast<float>(sr)));
                attackTarget = peak;
                attackIncr   = (attackTarget - currentLevel) / static_cast<float>(atkSamples);

                float susLevel = sustainLevel * peak;
                float decSec   = std::max(decayMs / 1000.0f, MIN_RAMP_SEC);
                int   decSamples = std::max(1, static_cast<int>(decSec * static_cast<float>(sr)));
                decayTarget = susLevel;
                decayIncr   = (decayTarget - attackTarget) / static_cast<float>(decSamples);

                state = State::Attack;
            }
            return currentLevel;
        }

        case State::Release:
        {
            // RC-discharge: e^(-t/τ), τ = releaseMs/5
            // Reference: setTargetAtTime(0, now, τ) followed by setValueAtTime(0, now + dur)
            releaseSampleCount++;
            float t = static_cast<float>(releaseSampleCount) / static_cast<float>(sr);
            currentLevel = releaseStartLevel * std::exp(-t / releaseTau);

            // Hard-zero at end of release duration
            if (releaseSampleCount >= releaseTotalSamples)
            {
                currentLevel = 0.0f;
                state = State::Idle;
            }
            return currentLevel;
        }
    }

    return 0.0f;
}
