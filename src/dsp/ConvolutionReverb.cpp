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

    // Silence detection: skip expensive FFT convolution after tail has decayed
    float magnitude = buffer.getMagnitude(0, buffer.getNumSamples());
    if (magnitude < 1e-6f)
    {
        if (++silentInputBlocks > REVERB_TAIL_BLOCKS)
            return;
    }
    else
    {
        silentInputBlocks = 0;
    }

    juce::dsp::AudioBlock<float> block(buffer);
    mixer.pushDrySamples(block);

    juce::dsp::ProcessContextReplacing<float> context(block);
    convolution.process(context);

    mixer.mixWetSamples(block);
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
        silentInputBlocks = 0;
    }
}
