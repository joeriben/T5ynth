#include "NoiseGenerator.h"

void NoiseGenerator::prepare(double sampleRate)
{
    sr_ = sampleRate;
    lpState_ = 0.0f;
    dcState_ = 0.0f;
    lastCutoff_ = 20000.0f;
    lpCoeff_ = 1.0f;
    // DC blocker: one-pole HP at ~5 Hz
    dcCoeff_ = 1.0f - std::exp(-2.0 * 3.14159265358979 * 5.0 / sr_);
}

void NoiseGenerator::reset()
{
    lpState_ = 0.0f;
    dcState_ = 0.0f;
}

void NoiseGenerator::setColorCutoff(float hz)
{
    // Skip recalc if cutoff hasn't changed meaningfully
    if (std::abs(hz - lastCutoff_) < 0.5f) return;
    lastCutoff_ = hz;

    if (hz >= 19999.0f)
    {
        lpCoeff_ = 1.0f; // bypass LP — pure white noise
    }
    else
    {
        lpCoeff_ = static_cast<float>(
            1.0 - std::exp(-2.0 * 3.14159265358979 * static_cast<double>(hz) / sr_));
    }
}

float NoiseGenerator::nextWhite()
{
    // xorshift64
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 7;
    rngState_ ^= rngState_ << 17;
    // Map to [-1, 1] float
    return static_cast<float>(static_cast<int64_t>(rngState_)) * (1.0f / 9223372036854775808.0f);
}

float NoiseGenerator::processSample()
{
    float white = nextWhite();

    // One-pole LP: y += coeff * (x - y)
    lpState_ += lpCoeff_ * (white - lpState_);

    // DC blocker: one-pole HP
    float out = lpState_ - dcState_;
    dcState_ += dcCoeff_ * (lpState_ - dcState_);

    return out;
}
