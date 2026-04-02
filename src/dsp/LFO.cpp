#include "LFO.h"

void LFO::prepare(double sampleRate)
{
    sr = sampleRate;
    phase = 0.0;
}

void LFO::reset()
{
    phase = 0.0;
}

float LFO::processSample()
{
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
        default:
            output = static_cast<float>(std::sin(phase * TWO_PI));
            break;
    }

    // Advance phase
    double phaseInc = static_cast<double>(rate) / sr;
    phase += phaseInc;
    if (phase >= 1.0)
        phase -= 1.0;

    return output * depth;
}

void LFO::advancePhase(int numSamples)
{
    double advance = static_cast<double>(rate) / sr * numSamples;
    phase += advance;
    phase -= std::floor(phase);  // wrap to [0, 1)
}
