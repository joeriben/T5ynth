#include "DriftLFO.h"

float DriftLFO::nextRandom(InternalLFO& lfo)
{
    lfo.rngState ^= lfo.rngState << 13;
    lfo.rngState ^= lfo.rngState >> 7;
    lfo.rngState ^= lfo.rngState << 17;
    return static_cast<float>(static_cast<int64_t>(lfo.rngState))
         * (1.0f / 9223372036854775808.0f);
}

float DriftLFO::waveformValue(const InternalLFO& lfo)
{
    double p = lfo.phase - std::floor(lfo.phase); // normalize to 0-1
    switch (lfo.waveform)
    {
        case Sine:
            return static_cast<float>(std::sin(p * TWO_PI));
        case Triangle:
            if (p < 0.25) return static_cast<float>(p * 4.0);
            if (p < 0.75) return static_cast<float>(2.0 - p * 4.0);
            return static_cast<float>(p * 4.0 - 4.0);
        case Square:
            return p < 0.5 ? 1.0f : -1.0f;
        case Sawtooth:
            // Asymmetric ping-pong saw: fast rise (20%), slow fall (60%), fast rise (20%)
            if (p < 0.2) return static_cast<float>(p * 5.0);
            if (p < 0.8) return static_cast<float>(1.0 - (p - 0.2) * (10.0 / 3.0));
            return static_cast<float>(-1.0 + (p - 0.8) * 5.0);
        case Random:
            return lfo.heldValue;
        default:
            return static_cast<float>(std::sin(p * TWO_PI));
    }
}

float DriftLFO::halfRangeForTarget(int target)
{
    switch (target)
    {
        case TgtAlpha:     return 2.0f;   // alpha: [-2, 2]
        case TgtAxis1:     return 2.0f;   // sem_axis_1
        case TgtAxis2:     return 2.0f;   // sem_axis_2
        case TgtAxis3:     return 2.0f;   // sem_axis_3
        case TgtWtScan:    return 0.5f;   // wt_scan: [0, 1]
        case TgtFilter:    return 0.3f;   // filter cutoff: ±30% modulation depth
        case TgtPitch:     return 12.0f;  // pitch: ±12 semitones
        case TgtDelayTime: return 0.5f;   // delay time: ±50% of base
        case TgtDelayFb:   return 0.3f;   // delay feedback: ±30%
        case TgtDelayMix:  return 0.5f;   // delay mix: ±0.5
        case TgtReverbMix: return 0.5f;   // reverb mix: ±0.5
        case TgtEnv1Amt:   return 0.5f;   // ENV1 amount: ±50%
        case TgtEnv2Amt:   return 0.5f;   // ENV2 amount: ±50%
        case TgtEnv3Amt:   return 0.5f;   // ENV3 amount: ±50%
        case TgtNoise:     return 0.5f;   // noise sigma: ±0.5 (range 0–1)
        case TgtMagnitude: return 1.0f;   // magnitude: ±1.0 (range 0.001–5)
        default:           return 0.0f;   // None
    }
}

void DriftLFO::reset()
{
    for (auto& lfo : lfos)
    {
        lfo.phase = 0.0;
        lfo.heldValue = nextRandom(lfo);
    }
}

void DriftLFO::tick(double dt)
{
    if (!active)
        return;

    for (auto& lfo : lfos)
    {
        if (lfo.target == 0 || lfo.depth == 0.0f) // target 0 = None
            continue;

        lfo.phase += static_cast<double>(lfo.rate) * dt;

        // Wrap phase to prevent precision loss
        if (lfo.phase >= 1.0)
        {
            lfo.phase -= std::floor(lfo.phase);
            if (lfo.waveform == Random)
                lfo.heldValue = nextRandom(lfo);
        }
    }
}

float DriftLFO::getOffsetForTarget(int target) const
{
    if (!active || target <= 0 || target > NumTargets)
        return 0.0f;

    float totalOffset = 0.0f;
    const float hr = halfRangeForTarget(target);

    for (const auto& lfo : lfos)
    {
        if (lfo.target == target && lfo.depth != 0.0f)
        {
            float value = waveformValue(lfo);
            totalOffset += value * lfo.depth * hr;
        }
    }

    return totalOffset;
}

void DriftLFO::setLfoRate(int lfoIndex, float hz)
{
    if (lfoIndex >= 0 && lfoIndex < NUM_LFOS)
        lfos[static_cast<size_t>(lfoIndex)].rate = hz;
}

void DriftLFO::setLfoDepth(int lfoIndex, float amount)
{
    if (lfoIndex >= 0 && lfoIndex < NUM_LFOS)
        lfos[static_cast<size_t>(lfoIndex)].depth = amount;
}

void DriftLFO::setLfoTarget(int lfoIndex, int target)
{
    if (lfoIndex >= 0 && lfoIndex < NUM_LFOS)
    {
        auto& lfo = lfos[static_cast<size_t>(lfoIndex)];
        if (lfo.target != target)
        {
            lfo.phase = 0.0; // Phase reset on target change (reference behavior)
            if (lfo.waveform == Random)
                lfo.heldValue = nextRandom(lfo);
            lfo.target = target;
        }
    }
}

void DriftLFO::setLfoWaveform(int lfoIndex, int waveform)
{
    if (lfoIndex >= 0 && lfoIndex < NUM_LFOS)
    {
        auto& lfo = lfos[static_cast<size_t>(lfoIndex)];
        if (lfo.waveform != waveform)
        {
            lfo.waveform = waveform;
            lfo.phase = 0.0;
            if (lfo.waveform == Random)
                lfo.heldValue = nextRandom(lfo);
        }
    }
}
