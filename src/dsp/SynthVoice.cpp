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
        ampEnv.noteOn(velocity);
        modEnv1.noteOn(velocity);
        modEnv2.noteOn(velocity);
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

    modEnv1.setAttack(p.mod1Attack);
    modEnv1.setDecay(p.mod1Decay);
    modEnv1.setSustain(p.mod1Sustain);
    modEnv1.setRelease(p.mod1Release);
    modEnv1.setLooping(p.mod1Loop);

    modEnv2.setAttack(p.mod2Attack);
    modEnv2.setDecay(p.mod2Decay);
    modEnv2.setSustain(p.mod2Sustain);
    modEnv2.setRelease(p.mod2Release);
    modEnv2.setLooping(p.mod2Loop);
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

    // Pitch modulation: env/LFO → pitch (target index 3 for env, 2 for LFO)
    float pitchMod = 0.0f;
    if (p.mod1Target == 3) pitchMod += mod1EnvVal;
    if (p.mod2Target == 3) pitchMod += mod2EnvVal;
    if (p.lfo1Target == 2) pitchMod += lfo1Val;
    if (p.lfo2Target == 2) pitchMod += lfo2Val;

    // Always set frequency from baseFrequency to avoid accumulation drift
    if (p.engineIsWavetable && osc.hasFrames())
        osc.setFrequency(baseFrequency * (1.0f + pitchMod));

    // Generate audio sample
    float sample = 0.0f;

    if (p.engineIsWavetable && osc.hasFrames())
    {
        // Scan modulation
        float scanMod = p.baseScan + p.driftScanOffset;
        if (p.mod1Target == 2) scanMod += mod1EnvVal;
        if (p.mod2Target == 2) scanMod += mod2EnvVal;
        if (p.lfo1Target == 1) scanMod += lfo1Val;
        if (p.lfo2Target == 1) scanMod += lfo2Val;
        float clampedScan = juce::jlimit(0.0f, 1.0f, scanMod);
        osc.setScanPosition(clampedScan);
        result.modulatedScan = clampedScan;

        sample = osc.processSample();
    }
    else if (!p.engineIsWavetable && sampler.hasAudio())
    {
        sample = sampler.processSample();
    }

    // DCA: multiplicative mod routing (reference behavior).
    // Worst case both mods→DCA at max amount: 4.0x gain. Limiter catches it.
    float vca = ampEnvVal;
    if (p.mod1Target == 0) vca *= (1.0f + mod1EnvVal);
    if (p.mod2Target == 0) vca *= (1.0f + mod2EnvVal);
    sample *= vca;

    // Per-voice filter with envelope/LFO modulation
    if (p.filterEnabled)
    {
        float cutoffMod = p.baseCutoff;

        // Keyboard tracking
        if (p.kbdTrack > 0.0f && currentNote >= 0)
            cutoffMod *= std::pow(2.0f, (static_cast<float>(currentNote) - 60.0f) / 12.0f * p.kbdTrack);

        // Mod envelope → filter (target index 1)
        // Use raw envelope (not pre-scaled by amount) for proper sweep range
        {
            constexpr float FILTER_DEPTH = 4.0f;
            float rawEnv1 = (p.mod1Amount > 0.001f) ? mod1EnvVal / p.mod1Amount : 0.0f;
            float rawEnv2 = (p.mod2Amount > 0.001f) ? mod2EnvVal / p.mod2Amount : 0.0f;

            if (p.mod1Target == 1)
                cutoffMod *= 1.0f + rawEnv1 * p.mod1Amount * FILTER_DEPTH;
            if (p.mod2Target == 1)
                cutoffMod *= 1.0f + rawEnv2 * p.mod2Amount * FILTER_DEPTH;

            // LFO → filter (target index 0)
            if (p.lfo1Target == 0) cutoffMod *= (1.0f + lfo1Val * FILTER_DEPTH);
            if (p.lfo2Target == 0) cutoffMod *= (1.0f + lfo2Val * FILTER_DEPTH);
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

