#include "StateVariableFilter.h"

void T5ynthFilter::prepare(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    blockSize = samplesPerBlock;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 2;

    filter1.prepare(spec);
    filter1.setCutoffFrequency(20000.0f);
    filter1.setResonance(resonanceToQ(0.0f)); // Q=0.5 at reso=0

    filter2.prepare(spec);
    filter2.setCutoffFrequency(20000.0f);
    filter2.setResonance(resonanceToQ(0.0f));

    prepared = true;
}

void T5ynthFilter::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (!prepared)
        return;

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();

    // Bypass: mix=0 means fully dry
    if (currentMix < 0.001f)
        return;

    // Equal-power crossfade gains
    const float halfPi = juce::MathConstants<float>::halfPi;
    const float wetGain = std::sin(currentMix * halfPi);
    const float dryGain = std::cos(currentMix * halfPi);

    // Fully wet: no need to keep dry copy
    if (dryGain < 0.001f)
    {
        juce::dsp::AudioBlock<float> block(buffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        filter1.process(context);

        if (currentSlope == 1)
        {
            juce::dsp::ProcessContextReplacing<float> context2(block);
            filter2.process(context2);
        }
        return;
    }

    // Mixed: process wet path into temp, blend with dry
    // Save dry copy
    juce::AudioBuffer<float> dryBuffer(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    // Filter the buffer (wet path)
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    filter1.process(context);

    if (currentSlope == 1)
    {
        juce::dsp::ProcessContextReplacing<float> context2(block);
        filter2.process(context2);
    }

    // Equal-power blend: output = dry * dryGain + wet * wetGain
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* wet = buffer.getWritePointer(ch);
        const auto* dry = dryBuffer.getReadPointer(ch);

        for (int i = 0; i < numSamples; ++i)
            wet[i] = dry[i] * dryGain + wet[i] * wetGain;
    }
}

void T5ynthFilter::reset()
{
    filter1.reset();
    filter2.reset();
}

void T5ynthFilter::setMix(float mix)
{
    currentMix = juce::jlimit(0.0f, 1.0f, mix);
}

void T5ynthFilter::setCutoff(float hz)
{
    filter1.setCutoffFrequency(hz);
    filter2.setCutoffFrequency(hz);
}

void T5ynthFilter::setResonance(float r)
{
    float q = resonanceToQ(r);
    filter1.setResonance(q);
    filter2.setResonance(q);
}

void T5ynthFilter::setType(int type)
{
    if (type == currentType) return;
    currentType = type;

    auto juceType = juce::dsp::StateVariableTPTFilterType::lowpass;
    switch (type)
    {
        case 0: juceType = juce::dsp::StateVariableTPTFilterType::lowpass; break;
        case 1: juceType = juce::dsp::StateVariableTPTFilterType::highpass; break;
        case 2: juceType = juce::dsp::StateVariableTPTFilterType::bandpass; break;
        default: break;
    }

    filter1.setType(juceType);
    filter2.setType(juceType);
}

void T5ynthFilter::setSlope(int slope)
{
    currentSlope = (slope == 1) ? 1 : 0;
}
