#include "Limiter.h"

#include <cmath>

float OutputCeiling::shape (float x) noexcept
{
    const float a = std::abs (x);
    if (a <= kKnee)
        return x;

    // The excess above the knee, normalised to the space left below the
    // ceiling, through tanh and scaled back into that space. At a == kKnee this
    // is kKnee with slope 1, so it meets the linear region with no corner; as
    // a grows it approaches 1.0 and never reaches it.
    const float over = (a - kKnee) / (1.0f - kKnee);
    const float y    = kKnee + (1.0f - kKnee) * std::tanh (over);
    return x < 0.0f ? -y : y;
}

void OutputCeiling::scrubNonFinite (juce::AudioBuffer<float>& buffer) noexcept
{
    const int numSamples = buffer.getNumSamples();

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            if (! std::isfinite (d[i]))
                d[i] = 0.0f;
    }
}

void OutputCeiling::processBlock (juce::AudioBuffer<float>& buffer) const noexcept
{
    const int numSamples = buffer.getNumSamples();

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < numSamples; ++i)
            d[i] = shape (d[i]);
    }
}
