#include "LFO.h"

float LFO::nextRandom()
{
    // xorshift64 → signed → [-1, 1]
    rngState_ ^= rngState_ << 13;
    rngState_ ^= rngState_ >> 7;
    rngState_ ^= rngState_ << 17;
    return static_cast<float>(static_cast<int64_t>(rngState_))
         * (1.0f / 9223372036854775808.0f);
}

void LFO::prepare(double sampleRate)
{
    sr = sampleRate;
    phase = 0.0;
    heldValue_ = nextRandom(); // avoid silent first period for S&H
}

void LFO::reset()
{
    phase = 0.0;
    heldValue_ = nextRandom();
}

float LFO::processSample()
{
    // Armed for beat-sync: hold at phase 0, emit nothing until released.
    if (armed)
        return 0.0f;

    float output = 0.0f;

    switch (waveform)
    {
        case 0: // Sine
            output = static_cast<float>(std::sin(phase * TWO_PI));
            break;
        case 1: // Triangle
        {
            double t = phase;
            output = static_cast<float>(t < 0.5 ? (4.0 * t - 1.0) : (3.0 - 4.0 * t));
            break;
        }
        case 2: // Saw (rising)
            output = static_cast<float>(2.0 * phase - 1.0);
            break;
        case 3: // Square
            output = phase < 0.5 ? 1.0f : -1.0f;
            break;
        case 4: // Sample & Hold
            output = heldValue_;
            break;
        default:
            output = static_cast<float>(std::sin(phase * TWO_PI));
            break;
    }

    // Advance phase
    double phaseInc = static_cast<double>(rate) / sr;
    phase += phaseInc;
    if (phase >= 1.0)
    {
        phase -= 1.0;
        heldValue_ = nextRandom(); // latch new value at period boundary
    }

    return output * depth;
}

void LFO::advancePhase(int numSamples)
{
    if (armed)
        return;  // held at phase 0 until released on the downbeat

    double advance = static_cast<double>(rate) / sr * numSamples;
    phase += advance;
    phase -= std::floor(phase);  // wrap to [0, 1)
}
