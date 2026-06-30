#include "AlgorithmicReverb.h"

void AlgorithmicReverb::prepare(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 2;

    sr = sampleRate;
    reverb.prepare(spec);
    mixer.prepare(spec);
    mixer.setWetMixProportion(wetMix);
    dampLpStateL = dampLpStateR = 0.0f;

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

    // Output tone LP (Damp): one-pole per channel on the wet tail. coeff==1 is
    // fully open (no work happens audibly). RT-safe: float state only, no alloc.
    if (dampLpCoeff < 0.999f)
    {
        const int n = buffer.getNumSamples();
        const int nch = buffer.getNumChannels();
        auto* L = buffer.getWritePointer(0);
        auto* R = nch > 1 ? buffer.getWritePointer(1) : nullptr;
        for (int i = 0; i < n; ++i)
        {
            dampLpStateL += dampLpCoeff * (L[i] - dampLpStateL);
            L[i] = dampLpStateL;
            if (R) {
                dampLpStateR += dampLpCoeff * (R[i] - dampLpStateR);
                R[i] = dampLpStateR;
            }
        }
    }

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
    dampLpStateL = dampLpStateR = 0.0f;
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
    const float knob = juce::jlimit(0.0f, 1.0f, damp);

    // JUCE's internal comb damping, kept in its well-behaved range (it rings if
    // pushed past ~0.4 internal). This gives the in-tail, frequency-dependent
    // decay character.
    auto params = reverb.getParameters();
    params.damping = knob;
    reverb.setParameters(params);

    // Plus an output tone LP for the bulk of the audible darkening: sweep the
    // cutoff log-spaced from ~20 kHz (open) down to ~1.2 kHz (dark) so the knob
    // darkens progressively across its WHOLE travel, not just the lower half.
    const float fc = 20000.0f * std::pow(1200.0f / 20000.0f, knob);
    dampLpCoeff = 1.0f - std::exp(-2.0f * juce::MathConstants<float>::pi
                                  * fc / static_cast<float>(sr));
}

void AlgorithmicReverb::setWidth(float w)
{
    auto params = reverb.getParameters();
    params.width = juce::jlimit(0.0f, 1.0f, w);
    reverb.setParameters(params);
}
