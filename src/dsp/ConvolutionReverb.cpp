#include "ConvolutionReverb.h"

void ConvolutionReverb::prepare(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 2;

    convolution.prepare(spec);
    mixer.prepare(spec);
    mixer.setWetMixProportion(wetMix);
    prepared = true;
}

void ConvolutionReverb::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (!prepared || wetMix == 0.0f || !irLoaded)
        return;

    // Silence detection: skip only after output has truly decayed
    float inMag = buffer.getMagnitude(0, buffer.getNumSamples());
    bool inputSilent = inMag < 1e-6f;

    if (inputSilent && silentOutputBlocks > SILENCE_CONFIRM_BLOCKS)
        return;

    juce::dsp::AudioBlock<float> block(buffer);
    mixer.pushDrySamples(block);

    juce::dsp::ProcessContextReplacing<float> context(block);
    convolution.process(context);

    mixer.mixWetSamples(block);

    // Check output magnitude — count as silent only when input is also silent
    float outMag = buffer.getMagnitude(0, buffer.getNumSamples());
    if (outMag < 1e-6f && inputSilent)
        ++silentOutputBlocks;
    else
        silentOutputBlocks = 0;
}

void ConvolutionReverb::reset()
{
    convolution.reset();
    mixer.reset();
}

void ConvolutionReverb::setMix(float mix)
{
    wetMix = juce::jlimit(0.0f, 1.0f, mix);
    if (prepared)
        mixer.setWetMixProportion(wetMix);
}

void ConvolutionReverb::loadImpulseResponse(const void* data, size_t size)
{
    if (data != nullptr && size > 0)
    {
        convolution.loadImpulseResponse(data, size,
                                         juce::dsp::Convolution::Stereo::yes,
                                         juce::dsp::Convolution::Trim::yes,
                                         0);
        irLoaded = true;
        silentOutputBlocks = 0;
    }
}
