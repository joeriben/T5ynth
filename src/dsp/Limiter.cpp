#include "Limiter.h"

void T5ynthLimiter::prepare(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 2;

    limiter.prepare(spec);
    limiter.setThreshold(-3.0f);   // Reference: threshold = -3dB
    limiter.setRelease(100.0f);    // Reference: release = 100ms
    prepared = true;
}

void T5ynthLimiter::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (!prepared)
        return;

    // Skip limiter entirely when buffer is silent (no tail to process)
    if (buffer.getMagnitude(0, buffer.getNumSamples()) < 1e-6f)
        return;

    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    limiter.process(context);
}

void T5ynthLimiter::reset()
{
    limiter.reset();
}

void T5ynthLimiter::setThreshold(float dB)
{
    limiter.setThreshold(dB);
}

void T5ynthLimiter::setRelease(float ms)
{
    limiter.setRelease(ms);
}
