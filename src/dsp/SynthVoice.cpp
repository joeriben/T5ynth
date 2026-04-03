#include "SynthVoice.h"

void SynthVoice::prepare(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    osc.prepare(sampleRate, samplesPerBlock);
    sampler.prepare(sampleRate, samplesPerBlock);
    ampEnv.prepare(sampleRate);
    modEnv1.prepare(sampleRate);
    modEnv2.prepare(sampleRate);
    perVoiceLfo1.prepare(sampleRate);
    perVoiceLfo2.prepare(sampleRate);
    filter.prepare(sampleRate, samplesPerBlock);
}

void SynthVoice::reset()
{
    osc.reset();
    sampler.reset();
    filter.reset();
    active = false;
    noteHeld = false;
    currentNote = -1;
    lastAmpEnvLevel = 0.0f;
}

void SynthVoice::noteOn(int note, float velocity, bool legato)
{
    currentNote = note;
    currentVelocity = velocity;
    noteHeld = true;
    active = true;

    if (!legato)
    {
        // Apply velocity sensitivity: 0=fixed (always 1), 1=full velocity
        float ampVel  = (1.0f - ampVelSens_)  + ampVelSens_  * velocity;
        float mod1Vel = (1.0f - mod1VelSens_) + mod1VelSens_ * velocity;
        float mod2Vel = (1.0f - mod2VelSens_) + mod2VelSens_ * velocity;
        ampEnv.noteOn(ampVel);
        modEnv1.noteOn(mod1Vel);
        modEnv2.noteOn(mod2Vel);
    }

    // Set pitch (cache base for modulation reference)
    baseFrequency = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(note));
    if (engineMode == EngineMode::Wavetable)
        osc.setFrequency(baseFrequency);
    else
        sampler.setMidiNote(note);
}

void SynthVoice::noteOff()
{
    noteHeld = false;
    ampEnv.noteOff();
    modEnv1.noteOff();
    modEnv2.noteOff();
}

void SynthVoice::glideToNote(int note, float glideMs)
{
    currentNote = note;
    baseFrequency = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(note));
    if (engineMode == EngineMode::Wavetable)
        osc.glideToFrequency(baseFrequency, glideMs);
    else
        sampler.glideToSemitones(note - 60, glideMs);
}

void SynthVoice::configureForBlock(const BlockParams& p)
{
    if (!active) return;

    ampEnv.setAttack(p.ampAttack);
    ampEnv.setDecay(p.ampDecay);
    ampEnv.setSustain(p.ampSustain);
    ampEnv.setRelease(p.ampRelease);
    ampEnv.setLooping(p.ampLoop);
    ampVelSens_ = p.ampVelSens;

    modEnv1.setAttack(p.mod1Attack);
    modEnv1.setDecay(p.mod1Decay);
    modEnv1.setSustain(p.mod1Sustain);
    modEnv1.setRelease(p.mod1Release);
    modEnv1.setLooping(p.mod1Loop);
    mod1VelSens_ = p.mod1VelSens;

    modEnv2.setAttack(p.mod2Attack);
    modEnv2.setDecay(p.mod2Decay);
    modEnv2.setSustain(p.mod2Sustain);
    modEnv2.setRelease(p.mod2Release);
    modEnv2.setLooping(p.mod2Loop);
    mod2VelSens_ = p.mod2VelSens;
}

SynthVoice::RenderResult SynthVoice::renderSample(const BlockParams& p, float globalLfo1Val, float globalLfo2Val)
{
    RenderResult result = { 0.0f, 0.0f, 0.0f };

    if (!active) return result;

    // Tick envelopes
    float ampEnvVal = ampEnv.processSample() * p.ampAmount;
    float mod1EnvVal = modEnv1.processSample() * p.mod1Amount;
    float mod2EnvVal = modEnv2.processSample() * p.mod2Amount;

    lastAmpEnvLevel = ampEnvVal;
    result.mod1EnvVal = mod1EnvVal;
    result.mod2EnvVal = mod2EnvVal;

    // Use global LFO values (freerun mode — trigger mode will be added in Phase 3)
    float lfo1Val = globalLfo1Val;
    float lfo2Val = globalLfo2Val;

    // Pitch modulation
    float pitchMod = p.driftPitchOffset;
    if (p.mod1Target == EnvTarget::Pitch) pitchMod += mod1EnvVal;
    if (p.mod2Target == EnvTarget::Pitch) pitchMod += mod2EnvVal;
    if (p.lfo1Target == LfoTarget::Pitch) pitchMod += lfo1Val;
    if (p.lfo2Target == LfoTarget::Pitch) pitchMod += lfo2Val;

    // Always set frequency from baseFrequency to avoid accumulation drift
    if (p.engineIsWavetable && osc.hasFrames())
        osc.setFrequency(baseFrequency * (1.0f + pitchMod));

    // Generate audio sample
    float sample = 0.0f;

    if (p.engineIsWavetable && osc.hasFrames())
    {
        // Scan modulation
        float scanMod = p.baseScan + p.driftScanOffset;
        if (p.mod1Target == EnvTarget::Scan) scanMod += mod1EnvVal;
        if (p.mod2Target == EnvTarget::Scan) scanMod += mod2EnvVal;
        if (p.lfo1Target == LfoTarget::Scan) scanMod += lfo1Val;
        if (p.lfo2Target == LfoTarget::Scan) scanMod += lfo2Val;
        float clampedScan = juce::jlimit(0.0f, 1.0f, scanMod);
        osc.setScanPosition(clampedScan);
        result.modulatedScan = clampedScan;

        sample = osc.processSample();
    }
    else if (!p.engineIsWavetable && sampler.hasAudio())
    {
        sample = sampler.processSample();
    }

    // DCA
    float vca = ampEnvVal;
    if (p.mod1Target == EnvTarget::DCA) vca *= (1.0f + mod1EnvVal);
    if (p.mod2Target == EnvTarget::DCA) vca *= (1.0f + mod2EnvVal);
    sample *= vca;

    // Per-voice filter with envelope/LFO modulation
    if (p.filterEnabled)
    {
        float cutoffMod = p.baseCutoff;

        // Keyboard tracking
        if (p.kbdTrack > 0.0f && currentNote >= 0)
            cutoffMod *= std::pow(2.0f, (static_cast<float>(currentNote) - 60.0f) / 12.0f * p.kbdTrack);

        // Mod envelope → filter (octave-based)
        {
            constexpr float FILTER_OCTAVES = 10.0f;
            float rawEnv1 = (p.mod1Amount > 0.001f) ? mod1EnvVal / p.mod1Amount : 0.0f;
            float rawEnv2 = (p.mod2Amount > 0.001f) ? mod2EnvVal / p.mod2Amount : 0.0f;

            if (p.mod1Target == EnvTarget::Filter)
                cutoffMod *= std::pow(2.0f, rawEnv1 * p.mod1Amount * FILTER_OCTAVES);
            if (p.mod2Target == EnvTarget::Filter)
                cutoffMod *= std::pow(2.0f, rawEnv2 * p.mod2Amount * FILTER_OCTAVES);

            // LFO → filter
            if (p.lfo1Target == LfoTarget::Filter) cutoffMod *= std::pow(2.0f, lfo1Val * FILTER_OCTAVES);
            if (p.lfo2Target == LfoTarget::Filter) cutoffMod *= std::pow(2.0f, lfo2Val * FILTER_OCTAVES);

            // Drift → filter
            if (p.driftFilterOffset != 0.0f)
                cutoffMod *= std::pow(2.0f, p.driftFilterOffset * FILTER_OCTAVES);
        }

        cutoffMod = juce::jlimit(20.0f, 20000.0f, cutoffMod);
        result.modulatedCutoff = cutoffMod;

        filter.setCutoff(cutoffMod);
        filter.setResonance(p.baseReso);
        filter.setType(p.filterType);
        filter.setSlope(p.filterSlope);
        filter.setMix(p.filterMix);
        sample = filter.processSample(sample);
    }

    // Check if voice has finished (envelope idle after release)
    if (ampEnv.isIdle() && !noteHeld)
        active = false;

    result.sample = sample;
    return result;
}

void SynthVoice::renderBlock(float* output, const BlockParams& p,
                              const float* lfo1Buf, const float* lfo2Buf, int numSamples)
{
    if (!active)
    {
        std::memset(output, 0, sizeof(float) * static_cast<size_t>(numSamples));
        return;
    }

    int pos = 0;
    while (pos < numSamples && active)
    {
        int subBlockEnd = std::min(pos + SUB_BLOCK_SIZE, numSamples);
        int subBlockLen = subBlockEnd - pos;

        // ── Sub-block boundary: update filter coefficients ONCE ──
        if (p.filterEnabled)
        {
            int midIdx = pos + subBlockLen / 2;
            float lfo1Mid = lfo1Buf[midIdx];
            float lfo2Mid = lfo2Buf[midIdx];

            float cutoffMod = p.baseCutoff;

            if (p.kbdTrack > 0.0f && currentNote >= 0)
                cutoffMod *= std::pow(2.0f, (static_cast<float>(currentNote) - 60.0f) / 12.0f * p.kbdTrack);

            constexpr float FILTER_OCTAVES = 10.0f;
            float rawEnv1 = (p.mod1Amount > 0.001f) ? lastMod1Val_ / p.mod1Amount : 0.0f;
            float rawEnv2 = (p.mod2Amount > 0.001f) ? lastMod2Val_ / p.mod2Amount : 0.0f;

            if (p.mod1Target == EnvTarget::Filter) cutoffMod *= std::pow(2.0f, rawEnv1 * p.mod1Amount * FILTER_OCTAVES);
            if (p.mod2Target == EnvTarget::Filter) cutoffMod *= std::pow(2.0f, rawEnv2 * p.mod2Amount * FILTER_OCTAVES);
            if (p.lfo1Target == LfoTarget::Filter) cutoffMod *= std::pow(2.0f, lfo1Mid * FILTER_OCTAVES);
            if (p.lfo2Target == LfoTarget::Filter) cutoffMod *= std::pow(2.0f, lfo2Mid * FILTER_OCTAVES);
            if (p.driftFilterOffset != 0.0f)
                cutoffMod *= std::pow(2.0f, p.driftFilterOffset * FILTER_OCTAVES);

            cutoffMod = juce::jlimit(20.0f, 20000.0f, cutoffMod);
            lastModulatedCutoff_ = cutoffMod;

            filter.setCutoff(cutoffMod);
            filter.setResonance(p.baseReso);
            filter.setType(p.filterType);
            filter.setSlope(p.filterSlope);
            filter.setMix(p.filterMix);
        }

        // ── Per-sample inner loop: envelopes + osc + VCA ──
        for (int i = pos; i < subBlockEnd; ++i)
        {
            float ampEnvVal = ampEnv.processSample() * p.ampAmount;
            float mod1EnvVal = modEnv1.processSample() * p.mod1Amount;
            float mod2EnvVal = modEnv2.processSample() * p.mod2Amount;
            lastAmpEnvLevel = ampEnvVal;
            lastMod1Val_ = mod1EnvVal;
            lastMod2Val_ = mod2EnvVal;

            float lfo1Val = lfo1Buf[i];
            float lfo2Val = lfo2Buf[i];

            // Pitch modulation
            float pitchMod = p.driftPitchOffset;
            if (p.mod1Target == EnvTarget::Pitch) pitchMod += mod1EnvVal;
            if (p.mod2Target == EnvTarget::Pitch) pitchMod += mod2EnvVal;
            if (p.lfo1Target == LfoTarget::Pitch) pitchMod += lfo1Val;
            if (p.lfo2Target == LfoTarget::Pitch) pitchMod += lfo2Val;

            if (p.engineIsWavetable && osc.hasFrames())
                osc.setFrequency(baseFrequency * (1.0f + pitchMod));

            // Generate sample
            float sample = 0.0f;
            if (p.engineIsWavetable && osc.hasFrames())
            {
                float scanMod = p.baseScan + p.driftScanOffset;
                if (p.mod1Target == EnvTarget::Scan) scanMod += mod1EnvVal;
                if (p.mod2Target == EnvTarget::Scan) scanMod += mod2EnvVal;
                if (p.lfo1Target == LfoTarget::Scan) scanMod += lfo1Val;
                if (p.lfo2Target == LfoTarget::Scan) scanMod += lfo2Val;
                float clampedScan = juce::jlimit(0.0f, 1.0f, scanMod);
                osc.setScanPosition(clampedScan);
                lastModulatedScan_ = clampedScan;
                sample = osc.processSample();
            }
            else if (!p.engineIsWavetable && sampler.hasAudio())
            {
                sample = sampler.processSample();
            }

            // VCA
            float vca = ampEnvVal;
            if (p.mod1Target == EnvTarget::DCA) vca *= (1.0f + mod1EnvVal);
            if (p.mod2Target == EnvTarget::DCA) vca *= (1.0f + mod2EnvVal);
            sample *= vca;

            output[i] = sample;

            if (ampEnv.isIdle() && !noteHeld)
            {
                active = false;
                for (int j = i + 1; j < numSamples; ++j)
                    output[j] = 0.0f;
                return;
            }
        }

        // ── Apply filter to sub-block (uses cached coefficients) ──
        if (p.filterEnabled)
        {
            for (int i = pos; i < subBlockEnd; ++i)
                output[i] = filter.processSample(output[i]);
        }

        pos = subBlockEnd;
    }
}

