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

    early.prepare(sampleRate, samplesPerBlock);
    erBuffer.setSize(2, samplesPerBlock, false, true, true);   // audio thread never resizes it
    maxBlockSize = samplesPerBlock;

    prepared = true;
}

void AlgorithmicReverb::processBlock(juce::AudioBuffer<float>& buffer)
{
    if (!prepared || wetMix == 0.0f)
        return;

    const int total = buffer.getNumSamples();
    const int nch   = buffer.getNumChannels();
    if (total == 0 || nch < 1)
        return;

    // Silence detection: skip only once the output has truly decayed. Freeverb+
    // emits NOTHING until its first tap (2.9-14.3 ms depending on Room), because
    // feeding the late field from the reflections removes juce_Reverb's t=0 allpass
    // leak. A fixed four-block confirm window can therefore fall entirely inside
    // that gap on small buffers and swallow a whole event. Require the bank's
    // longest tap to have drained as well.
    const float inMag = buffer.getMagnitude(0, total);
    const bool inputSilent = inMag < 1e-6f;
    const int pendingBlocks = erEnabled
        ? static_cast<int>(std::ceil(early.longestTapSamples() / static_cast<float>(total)))
        : 0;
    if (inputSilent && silentOutputBlocks > pendingBlocks + SILENCE_CONFIRM_BLOCKS)
        return;

    const int maxChunk = maxBlockSize > 0 ? maxBlockSize : total;
    for (int off = 0; off < total; off += maxChunk)
    {
        const int len = juce::jmin(maxChunk, total - off);
        float* ptrs[2] = { buffer.getWritePointer(0) + off,
                           nch > 1 ? buffer.getWritePointer(1) + off : nullptr };
        juce::AudioBuffer<float> chunk(ptrs, nch, len);
        processChunk(chunk);
    }

    // Check output magnitude — count as silent only when input is also silent.
    float outMag = buffer.getMagnitude(0, total);
    if (outMag < 1e-6f && inputSilent)
        ++silentOutputBlocks;
    else
        silentOutputBlocks = 0;
}

void AlgorithmicReverb::processChunk(juce::AudioBuffer<float>& buffer)
{
    const int n   = buffer.getNumSamples();
    const int nch = buffer.getNumChannels();

    juce::dsp::AudioBlock<float> block(buffer);

    // The DRY must be captured from the INPUT, before the reflections replace it.
    // Pushing it after early.process() stored the reflections as the dry signal, so
    // the direct sound vanished at every mix in (0,1) and turning Mix down made the
    // reflections louder. Latent while the processor drives this at mix=1.0, but the
    // class's own contract said otherwise.
    mixer.pushDrySamples(block);

    // Freeverb+: reflections first, and the late field is fed FROM them, so the tail
    // starts after the reflection pattern rather than on top of the direct sound.
    // Freeverb keeps JUCE's untouched path.
    if (erEnabled)
    {
        early.process(buffer);                       // buffer := early reflections
        for (int ch = 0; ch < juce::jmin(nch, erBuffer.getNumChannels()); ++ch)
            erBuffer.copyFrom(ch, 0, buffer, ch, 0, n);
    }

    juce::dsp::ProcessContextReplacing<float> context(block);
    reverb.process(context);

    // Both halves belong to the WET signal, so they are summed BEFORE the mixer.
    if (erEnabled)
    {
        for (int ch = 0; ch < juce::jmin(nch, erBuffer.getNumChannels()); ++ch)
        {
            if (kLateGain != 1.0f)
                buffer.applyGain(ch, 0, n, kLateGain);
            buffer.addFrom(ch, 0, erBuffer, ch, 0, n, kErGain);
        }
    }

    mixer.mixWetSamples(block);

    // Output tone LP (Damp): one-pole per channel on the wet tail. coeff==1 is
    // fully open (no work happens audibly). RT-safe: float state only, no alloc.
    if (dampLpCoeff < 0.999f)
    {
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
}

void AlgorithmicReverb::reset()
{
    reverb.reset();
    mixer.reset();
    early.reset();
    erBuffer.clear();
    dampLpStateL = dampLpStateR = 0.0f;
}

void AlgorithmicReverb::setEarlyReflections(bool enabled)
{
    if (enabled == erEnabled)
        return;
    erEnabled = enabled;
    early.reset();          // switching voicings must not drag the old tap buffer in
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
    // Room drives the reflection geometry too, so on Freeverb+ it finally means
    // SIZE (later, wider-spaced reflections) and not just a longer decay.
    early.setRoomSize(size);
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
    early.setWidth(w);
}
