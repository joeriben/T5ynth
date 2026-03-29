#include "SynthVoice.h"

void SynthVoice::prepare(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    osc.prepare(sampleRate, samplesPerBlock);
    looper.prepare(sampleRate, samplesPerBlock);
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
    looper.reset();
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

    // Set pitch
    if (engineMode == EngineMode::Wavetable)
        osc.setFrequency(juce::MidiMessage::getMidiNoteInHertz(note));
    else
        looper.setMidiNote(note);
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
    if (engineMode == EngineMode::Wavetable)
        osc.glideToFrequency(juce::MidiMessage::getMidiNoteInHertz(note), glideMs);
    else
        looper.glideToSemitones(note - 60, glideMs);
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
        osc.setScanPosition(juce::jlimit(0.0f, 1.0f, scanMod));

        sample = osc.processSample();
    }
    else if (!p.engineIsWavetable && looper.hasAudio())
    {
        sample = looper.processSample();
    }

    // DCA: multiplicative mod routing (reference behavior).
    // Worst case both mods→DCA at max amount: 4.0x gain. Limiter catches it.
    float vca = ampEnvVal;
    if (p.mod1Target == 0) vca *= (1.0f + mod1EnvVal);
    if (p.mod2Target == 0) vca *= (1.0f + mod2EnvVal);
    sample *= vca;

    // Check if voice has finished (envelope idle after release)
    if (ampEnv.isIdle() && !noteHeld)
        active = false;

    result.sample = sample;
    return result;
}

void SynthVoice::processFilter(const BlockParams& p, float lastMod1Val, float lastMod2Val,
                                float lastLfo1Val, float lastLfo2Val)
{
    if (!p.filterEnabled) return;

    float cutoffMod = p.baseCutoff;

    // Keyboard tracking
    if (p.kbdTrack > 0.0f && currentNote >= 0)
        cutoffMod *= std::pow(2.0f, (static_cast<float>(currentNote) - 60.0f) / 12.0f * p.kbdTrack);

    // DCF Envelope: subtractive sweep (reference useModulation.ts:272-290)
    if (p.mod1Target == 1 && !modEnv1.isIdle())
    {
        float startFactor = 1.0f - p.mod1Amount;
        float peakFactor = 1.0f + p.mod1Amount * 8.0f;
        float envFactor = startFactor + (peakFactor - startFactor) * lastMod1Val;
        cutoffMod *= envFactor;
    }
    if (p.mod2Target == 1 && !modEnv2.isIdle())
    {
        float startFactor = 1.0f - p.mod2Amount;
        float peakFactor = 1.0f + p.mod2Amount * 8.0f;
        float envFactor = startFactor + (peakFactor - startFactor) * lastMod2Val;
        cutoffMod *= envFactor;
    }

    // LFO → filter: bipolar, depth-scaled
    if (p.lfo1Target == 0) cutoffMod *= (1.0f + lastLfo1Val);
    if (p.lfo2Target == 0) cutoffMod *= (1.0f + lastLfo2Val);

    cutoffMod = juce::jlimit(20.0f, 20000.0f, cutoffMod);

    filter.setCutoff(cutoffMod);
    filter.setResonance(p.baseReso);
    filter.setType(p.filterType);
    filter.setSlope(p.filterSlope);
    filter.setMix(p.filterMix);
}
