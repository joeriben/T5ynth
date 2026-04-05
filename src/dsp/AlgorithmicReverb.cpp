#include "AlgorithmicReverb.h"

void AlgorithmicReverb::prepare(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 2;

    reverb.prepare(spec);
    mixer.prepare(spec);
    mixer.setWetMixProportion(wetMix);

    // Medium hall: warm and spacious
    juce::dsp::Reverb::Parameters params;
    params.roomSize = 0.7f;
    params.damping  = 0.4f;
    params.wetLevel = 1.0f;   // mixer handles wet/dry
    params.dryLevel = 0.0f;
    params.width    = 1.0f;
    params.freezeMode = 0.0f;
    reverb.setParameters(params);

    prepared = true;
}

void AlgorithmicReverb::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (!prepared || wetMix == 0.0f)
        return;

    // Silence detection: skip only after output has truly decayed
    float inMag = buffer.getMagnitude(0, buffer.getNumSamples());
    bool inputSilent = inMag < 1e-6f;

    if (inputSilent && silentOutputBlocks > SILENCE_CONFIRM_BLOCKS)
        return;

    juce::dsp::AudioBlock<float> block(buffer);
    mixer.pushDrySamples(block);

    juce::dsp::ProcessContextReplacing<float> context(block);
    reverb.process(context);

    mixer.mixWetSamples(block);

    // Check output magnitude — count as silent only when input is also silent
    float outMag = buffer.getMagnitude(0, buffer.getNumSamples());
    if (outMag < 1e-6f && inputSilent)
        ++silentOutputBlocks;
    else
        silentOutputBlocks = 0;
}

void AlgorithmicReverb::reset()
{
    reverb.reset();
    mixer.reset();
}

void AlgorithmicReverb::setMix(float mix)
{
    wetMix = juce::jlimit(0.0f, 1.0f, mix);
    if (prepared)
        mixer.setWetMixProportion(wetMix);
}

void AlgorithmicReverb::setRoomSize(float size)
{
    auto params = reverb.getParameters();
    params.roomSize = juce::jlimit(0.0f, 1.0f, size);
    reverb.setParameters(params);
}

void AlgorithmicReverb::setDamping(float damp)
{
    auto params = reverb.getParameters();
    params.damping = juce::jlimit(0.0f, 1.0f, damp);
    reverb.setParameters(params);
}

void AlgorithmicReverb::setWidth(float w)
{
    auto params = reverb.getParameters();
    params.width = juce::jlimit(0.0f, 1.0f, w);
    reverb.setParameters(params);
}
