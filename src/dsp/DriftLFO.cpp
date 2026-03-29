#include "DriftLFO.h"

float DriftLFO::waveformValue(double phase, int type)
{
    double p = phase - std::floor(phase); // normalize to 0-1
    switch (type)
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
            return p < 0.5 ? static_cast<float>(p * 2.0) : static_cast<float>(p * 2.0 - 2.0);
        default:
            return static_cast<float>(std::sin(p * TWO_PI));
    }
}

float DriftLFO::halfRangeForTarget(int target)
{
    // APVTS index: 0=None, 1=Alpha, 2=Axis1, 3=Axis2, 4=Axis3, 5=WtScan
    switch (target)
    {
        case 1: return 2.0f;  // alpha: [-2, 2]
        case 2: return 2.0f;  // sem_axis_1
        case 3: return 2.0f;  // sem_axis_2
        case 4: return 2.0f;  // sem_axis_3
        case 5: return 0.5f;  // wt_scan: [0, 1]
        default: return 0.0f; // None
    }
}

void DriftLFO::reset()
{
    for (auto& lfo : lfos)
        lfo.phase = 0.0;
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
            lfo.phase -= std::floor(lfo.phase);
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
            float value = waveformValue(lfo.phase, lfo.waveform);
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
            lfo.target = target;
        }
    }
}

void DriftLFO::setLfoWaveform(int lfoIndex, int waveform)
{
    if (lfoIndex >= 0 && lfoIndex < NUM_LFOS)
        lfos[static_cast<size_t>(lfoIndex)].waveform = waveform;
}
