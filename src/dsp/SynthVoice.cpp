#include "SynthVoice.h"
#include <cstring>

namespace
{
float applyNormalizedOffset(float baseValue, float modulationOffset)
{
    return juce::jlimit(0.0f, 1.0f, baseValue + modulationOffset);
}

float computeEffectiveLfoDepth(const BlockParams& p, int target, float baseDepth,
                               float mod1EnvVal, float mod2EnvVal)
{
    float depth = baseDepth;
    if (p.mod1Target == target)
        depth = applyNormalizedOffset(depth, mod1EnvVal);
    if (p.mod2Target == target)
        depth = applyNormalizedOffset(depth, mod2EnvVal);
    return depth;
}
}

void SynthVoice::prepare(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlockSize_ = samplesPerBlock;
    samplerBlockBuf_.resize(static_cast<size_t>(samplesPerBlock));
    osc.prepare(sampleRate, samplesPerBlock);
    sampler.prepare(sampleRate, samplesPerBlock);
    noise.prepare(sampleRate);
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
    noise.reset();
    active = false;
    noteHeld = false;
    currentNote = -1;
    lastAmpEnvLevel = 0.0f;
    samplerPreStretchNormGain_ = 1.0f;
    samplerPreStretchNormDirty_ = true;
    preStretchNormState_ = {};
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
    samplerPreStretchNormDirty_ = true;

    // Set pitch (cache base for modulation reference)
    int shiftedNote = note + octaveShift_ * 12;
    baseFrequency = tunedHz(shiftedNote);
    osc.setFrequency(baseFrequency);

    if (engineMode == EngineMode::Sampler)
    {
        double ratio = static_cast<double>(tunedHz(shiftedNote))
                     / static_cast<double>(tunedHz(60));
        sampler.setTransposeRatio(ratio);
    }
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
    int shiftedNote = note + octaveShift_ * 12;
    if (engineMode == EngineMode::Sampler)
    {
        double ratio = static_cast<double>(tunedHz(shiftedNote))
                     / static_cast<double>(tunedHz(60));
        sampler.glideToRatio(ratio, glideMs);
    }
    else
    {
        float targetFreq = tunedHz(shiftedNote);
        osc.glideToFrequency(targetFreq, glideMs);
    }
}

void SynthVoice::configureForBlock(const BlockParams& p)
{
    if (!active) return;

    octaveShift_ = p.octaveShift;

    ampEnv.setAttack(p.ampAttack);
    ampEnv.setDecay(p.ampDecay);
    ampEnv.setSustain(p.ampSustain);
    ampEnv.setRelease(p.ampRelease);
    ampEnv.setLooping(p.ampLoop);
    ampEnv.setAttackCurve(static_cast<CurveShape>(p.ampAttackCurve));
    ampEnv.setDecayCurve(static_cast<CurveShape>(p.ampDecayCurve));
    ampEnv.setReleaseCurve(static_cast<CurveShape>(p.ampReleaseCurve));
    ampVelSens_ = p.ampVelSens;

    modEnv1.setAttack(p.mod1Attack);
    modEnv1.setDecay(p.mod1Decay);
    modEnv1.setSustain(p.mod1Sustain);
    modEnv1.setRelease(p.mod1Release);
    modEnv1.setLooping(p.mod1Loop);
    modEnv1.setAttackCurve(static_cast<CurveShape>(p.mod1AttackCurve));
    modEnv1.setDecayCurve(static_cast<CurveShape>(p.mod1DecayCurve));
    modEnv1.setReleaseCurve(static_cast<CurveShape>(p.mod1ReleaseCurve));
    mod1VelSens_ = p.mod1VelSens;

    modEnv2.setAttack(p.mod2Attack);
    modEnv2.setDecay(p.mod2Decay);
    modEnv2.setSustain(p.mod2Sustain);
    modEnv2.setRelease(p.mod2Release);
    modEnv2.setLooping(p.mod2Loop);
    modEnv2.setAttackCurve(static_cast<CurveShape>(p.mod2AttackCurve));
    modEnv2.setDecayCurve(static_cast<CurveShape>(p.mod2DecayCurve));
    modEnv2.setReleaseCurve(static_cast<CurveShape>(p.mod2ReleaseCurve));
    mod2VelSens_ = p.mod2VelSens;

    updateSamplerPreStretchNorm(p);
}

bool SynthVoice::preStretchNormStateMatches(const BlockParams& p) const
{
    auto nearlyEqual = [] (float a, float b)
    {
        return std::abs(a - b) < 1.0e-5f;
    };

    return nearlyEqual(preStretchNormState_.ampAttack, p.ampAttack)
        && nearlyEqual(preStretchNormState_.ampDecay, p.ampDecay)
        && nearlyEqual(preStretchNormState_.ampSustain, p.ampSustain)
        && nearlyEqual(preStretchNormState_.ampRelease, p.ampRelease)
        && nearlyEqual(preStretchNormState_.ampAmount, p.ampAmount)
        && nearlyEqual(preStretchNormState_.ampVelSens, p.ampVelSens)
        && preStretchNormState_.ampLoop == p.ampLoop
        && preStretchNormState_.ampAttackCurve == p.ampAttackCurve
        && preStretchNormState_.ampDecayCurve == p.ampDecayCurve
        && preStretchNormState_.ampReleaseCurve == p.ampReleaseCurve
        && preStretchNormState_.mod1Target == p.mod1Target
        && nearlyEqual(preStretchNormState_.mod1Attack, p.mod1Attack)
        && nearlyEqual(preStretchNormState_.mod1Decay, p.mod1Decay)
        && nearlyEqual(preStretchNormState_.mod1Sustain, p.mod1Sustain)
        && nearlyEqual(preStretchNormState_.mod1Release, p.mod1Release)
        && nearlyEqual(preStretchNormState_.mod1Amount, p.mod1Amount)
        && nearlyEqual(preStretchNormState_.mod1VelSens, p.mod1VelSens)
        && preStretchNormState_.mod1Loop == p.mod1Loop
        && preStretchNormState_.mod1AttackCurve == p.mod1AttackCurve
        && preStretchNormState_.mod1DecayCurve == p.mod1DecayCurve
        && preStretchNormState_.mod1ReleaseCurve == p.mod1ReleaseCurve
        && preStretchNormState_.mod2Target == p.mod2Target
        && nearlyEqual(preStretchNormState_.mod2Attack, p.mod2Attack)
        && nearlyEqual(preStretchNormState_.mod2Decay, p.mod2Decay)
        && nearlyEqual(preStretchNormState_.mod2Sustain, p.mod2Sustain)
        && nearlyEqual(preStretchNormState_.mod2Release, p.mod2Release)
        && nearlyEqual(preStretchNormState_.mod2Amount, p.mod2Amount)
        && nearlyEqual(preStretchNormState_.mod2VelSens, p.mod2VelSens)
        && preStretchNormState_.mod2Loop == p.mod2Loop
        && preStretchNormState_.mod2AttackCurve == p.mod2AttackCurve
        && preStretchNormState_.mod2DecayCurve == p.mod2DecayCurve
        && preStretchNormState_.mod2ReleaseCurve == p.mod2ReleaseCurve
        && nearlyEqual(preStretchNormState_.velocity, currentVelocity)
        && nearlyEqual(preStretchNormState_.startPos, sampler.getStartPos())
        && nearlyEqual(preStretchNormState_.loopStart, sampler.getLoopStart())
        && nearlyEqual(preStretchNormState_.loopEnd, sampler.getLoopEnd())
        && nearlyEqual(preStretchNormState_.startPosOffset, sampler.getStartPosOffset())
        && nearlyEqual(preStretchNormState_.crossfadeMs, sampler.getCrossfadeMs())
        && preStretchNormState_.loopMode == static_cast<int>(sampler.getLoopMode())
        && preStretchNormState_.normalizeOn == sampler.getNormalize();
}

void SynthVoice::updateSamplerPreStretchNorm(const BlockParams& p)
{
    if (engineMode != EngineMode::Sampler || !sampler.hasAudio() || !sampler.getNormalize())
    {
        samplerPreStretchNormGain_ = 1.0f;
        samplerPreStretchNormDirty_ = false;
        sampler.setSourceGain(1.0f);
        preStretchNormState_.normalizeOn = sampler.getNormalize();
        return;
    }

    if (!samplerPreStretchNormDirty_ && preStretchNormStateMatches(p))
    {
        sampler.setSourceGain(samplerPreStretchNormGain_);
        return;
    }

    const int referencePathSamples = sampler.estimateReferenceLengthSamples();
    auto envWindowMs = [] (float attackMs, float decayMs, float releaseMs, bool looping)
    {
        constexpr float kHoldMs = 120.0f;
        float base = std::max(attackMs, 0.0f) + std::max(decayMs, 0.0f)
                   + (looping ? 0.0f : kHoldMs) + std::max(releaseMs, 0.0f) * 0.1f;
        return base;
    };

    float analysisMs = envWindowMs(p.ampAttack, p.ampDecay, p.ampRelease, p.ampLoop);
    if (p.mod1Target == EnvTarget::DCA)
        analysisMs = std::max(analysisMs, envWindowMs(p.mod1Attack, p.mod1Decay, p.mod1Release, p.mod1Loop));
    if (p.mod2Target == EnvTarget::DCA)
        analysisMs = std::max(analysisMs, envWindowMs(p.mod2Attack, p.mod2Decay, p.mod2Release, p.mod2Loop));

    int analysisSamples = std::max(referencePathSamples,
        static_cast<int>(std::ceil(sr * analysisMs * 0.001)));
    analysisSamples = juce::jlimit(64, static_cast<int>(sr * 3.0), analysisSamples);

    std::vector<float> dcaCurve(static_cast<size_t>(analysisSamples), 0.0f);

    ADSREnvelope ampRef;
    ADSREnvelope mod1Ref;
    ADSREnvelope mod2Ref;
    ampRef.prepare(sr);
    mod1Ref.prepare(sr);
    mod2Ref.prepare(sr);

    ampRef.setAttack(p.ampAttack);
    ampRef.setDecay(p.ampDecay);
    ampRef.setSustain(p.ampSustain);
    ampRef.setRelease(p.ampRelease);
    ampRef.setLooping(p.ampLoop);
    ampRef.setAttackCurve(static_cast<CurveShape>(p.ampAttackCurve));
    ampRef.setDecayCurve(static_cast<CurveShape>(p.ampDecayCurve));
    ampRef.setReleaseCurve(static_cast<CurveShape>(p.ampReleaseCurve));

    mod1Ref.setAttack(p.mod1Attack);
    mod1Ref.setDecay(p.mod1Decay);
    mod1Ref.setSustain(p.mod1Sustain);
    mod1Ref.setRelease(p.mod1Release);
    mod1Ref.setLooping(p.mod1Loop);
    mod1Ref.setAttackCurve(static_cast<CurveShape>(p.mod1AttackCurve));
    mod1Ref.setDecayCurve(static_cast<CurveShape>(p.mod1DecayCurve));
    mod1Ref.setReleaseCurve(static_cast<CurveShape>(p.mod1ReleaseCurve));

    mod2Ref.setAttack(p.mod2Attack);
    mod2Ref.setDecay(p.mod2Decay);
    mod2Ref.setSustain(p.mod2Sustain);
    mod2Ref.setRelease(p.mod2Release);
    mod2Ref.setLooping(p.mod2Loop);
    mod2Ref.setAttackCurve(static_cast<CurveShape>(p.mod2AttackCurve));
    mod2Ref.setDecayCurve(static_cast<CurveShape>(p.mod2DecayCurve));
    mod2Ref.setReleaseCurve(static_cast<CurveShape>(p.mod2ReleaseCurve));

    const float ampVel  = (1.0f - ampVelSens_)  + ampVelSens_  * currentVelocity;
    const float mod1Vel = (1.0f - mod1VelSens_) + mod1VelSens_ * currentVelocity;
    const float mod2Vel = (1.0f - mod2VelSens_) + mod2VelSens_ * currentVelocity;

    ampRef.noteOn(ampVel);
    mod1Ref.noteOn(mod1Vel);
    mod2Ref.noteOn(mod2Vel);

    for (int i = 0; i < analysisSamples; ++i)
    {
        const float ampEnvVal = ampRef.processSample() * p.ampAmount;
        const float mod1EnvVal = mod1Ref.processSample() * p.mod1Amount;
        const float mod2EnvVal = mod2Ref.processSample() * p.mod2Amount;

        float vca = ampEnvVal;
        if (p.mod1Target == EnvTarget::DCA) vca *= (1.0f + mod1EnvVal);
        if (p.mod2Target == EnvTarget::DCA) vca *= (1.0f + mod2EnvVal);
        dcaCurve[static_cast<size_t>(i)] = std::max(0.0f, vca);
    }

    float analysisPeak = 0.0f;
    const float analysisRms = sampler.estimatePlaybackRms(
        dcaCurve.data(), analysisSamples, &analysisPeak);

    static constexpr float kTargetPostDcaRms = 0.25f;
    static constexpr float kCeiling = 0.95f;
    samplerPreStretchNormGain_ = 1.0f;

    if (analysisRms > 1.0e-6f)
    {
        float gain = kTargetPostDcaRms / analysisRms;
        if (analysisPeak > 1.0e-6f)
            gain = std::min(gain, kCeiling / analysisPeak);
        samplerPreStretchNormGain_ = gain;
    }

    sampler.setSourceGain(samplerPreStretchNormGain_);

    preStretchNormState_.ampAttack = p.ampAttack;
    preStretchNormState_.ampDecay = p.ampDecay;
    preStretchNormState_.ampSustain = p.ampSustain;
    preStretchNormState_.ampRelease = p.ampRelease;
    preStretchNormState_.ampAmount = p.ampAmount;
    preStretchNormState_.ampVelSens = p.ampVelSens;
    preStretchNormState_.ampLoop = p.ampLoop;
    preStretchNormState_.ampAttackCurve = p.ampAttackCurve;
    preStretchNormState_.ampDecayCurve = p.ampDecayCurve;
    preStretchNormState_.ampReleaseCurve = p.ampReleaseCurve;
    preStretchNormState_.mod1Target = p.mod1Target;
    preStretchNormState_.mod1Attack = p.mod1Attack;
    preStretchNormState_.mod1Decay = p.mod1Decay;
    preStretchNormState_.mod1Sustain = p.mod1Sustain;
    preStretchNormState_.mod1Release = p.mod1Release;
    preStretchNormState_.mod1Amount = p.mod1Amount;
    preStretchNormState_.mod1VelSens = p.mod1VelSens;
    preStretchNormState_.mod1Loop = p.mod1Loop;
    preStretchNormState_.mod1AttackCurve = p.mod1AttackCurve;
    preStretchNormState_.mod1DecayCurve = p.mod1DecayCurve;
    preStretchNormState_.mod1ReleaseCurve = p.mod1ReleaseCurve;
    preStretchNormState_.mod2Target = p.mod2Target;
    preStretchNormState_.mod2Attack = p.mod2Attack;
    preStretchNormState_.mod2Decay = p.mod2Decay;
    preStretchNormState_.mod2Sustain = p.mod2Sustain;
    preStretchNormState_.mod2Release = p.mod2Release;
    preStretchNormState_.mod2Amount = p.mod2Amount;
    preStretchNormState_.mod2VelSens = p.mod2VelSens;
    preStretchNormState_.mod2Loop = p.mod2Loop;
    preStretchNormState_.mod2AttackCurve = p.mod2AttackCurve;
    preStretchNormState_.mod2DecayCurve = p.mod2DecayCurve;
    preStretchNormState_.mod2ReleaseCurve = p.mod2ReleaseCurve;
    preStretchNormState_.velocity = currentVelocity;
    preStretchNormState_.startPos = sampler.getStartPos();
    preStretchNormState_.loopStart = sampler.getLoopStart();
    preStretchNormState_.loopEnd = sampler.getLoopEnd();
    preStretchNormState_.startPosOffset = sampler.getStartPosOffset();
    preStretchNormState_.crossfadeMs = sampler.getCrossfadeMs();
    preStretchNormState_.loopMode = static_cast<int>(sampler.getLoopMode());
    preStretchNormState_.normalizeOn = sampler.getNormalize();
    samplerPreStretchNormDirty_ = false;
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
    const float lfo1Depth = computeEffectiveLfoDepth(p, EnvTarget::LFO1Depth,
                                                     p.lfo1Depth, mod1EnvVal, mod2EnvVal);
    const float lfo2Depth = computeEffectiveLfoDepth(p, EnvTarget::LFO2Depth,
                                                     p.lfo2Depth, mod1EnvVal, mod2EnvVal);
    float lfo1Val = globalLfo1Val * lfo1Depth;
    float lfo2Val = globalLfo2Val * lfo2Depth;

    // Pitch modulation
    float pitchMod = p.driftPitchOffset;
    if (p.mod1Target == EnvTarget::Pitch) pitchMod += mod1EnvVal;
    if (p.mod2Target == EnvTarget::Pitch) pitchMod += mod2EnvVal;
    if (p.lfo1Target == LfoTarget::Pitch) pitchMod += lfo1Val;
    if (p.lfo2Target == LfoTarget::Pitch) pitchMod += lfo2Val;

    // Set frequency — but skip during glide to avoid overriding it
    if (p.engineIsWavetable && osc.hasFrames() && !osc.isGliding())
    {
        baseFrequency = tunedHz(currentNote + octaveShift_ * 12);
        osc.setFrequency(baseFrequency * (1.0f + pitchMod));
    }

    // Generate audio sample
    float sample = 0.0f;

    if (p.engineIsWavetable && osc.hasFrames())
    {
        float scanBase = p.wtAutoScan ? 0.0f : p.baseScan;
        float scanMod = scanBase + p.driftScanOffset;
        if (p.mod1Target == EnvTarget::Scan) scanMod += mod1EnvVal;
        if (p.mod2Target == EnvTarget::Scan) scanMod += mod2EnvVal;
        if (p.lfo1Target == LfoTarget::Scan) scanMod += lfo1Val;
        if (p.lfo2Target == LfoTarget::Scan) scanMod += lfo2Val;
        const float clampedScan = juce::jlimit(0.0f, 1.0f, scanMod);
        osc.setScanPosition(clampedScan);

        sample = osc.processSample();
        result.modulatedScan = osc.getCurrentScanPosition();
        lastModulatedScan_ = result.modulatedScan;
    }
    else if (!p.engineIsWavetable && sampler.hasAudio())
    {
        sample = sampler.processSample();
    }

    float noiseLevel = p.noiseLevel;
    if (p.mod1Target == EnvTarget::NoiseLevel) noiseLevel += mod1EnvVal;
    if (p.mod2Target == EnvTarget::NoiseLevel) noiseLevel += mod2EnvVal;
    if (p.lfo1Target == LfoTarget::NoiseLevel) noiseLevel += lfo1Val;
    if (p.lfo2Target == LfoTarget::NoiseLevel) noiseLevel += lfo2Val;
    noiseLevel = juce::jlimit(0.0f, 1.0f, noiseLevel);
    result.modulatedNoiseLevel = noiseLevel;
    lastModulatedNoiseLevel_ = noiseLevel;

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

    bool samplerMode = (engineMode == EngineMode::Sampler) && sampler.hasAudio();
    bool oscReady = !samplerMode && osc.hasFrames();

    // ── Sampler mode: pre-render pitch-shifted block via Signalsmith Stretch ──
    if (samplerMode)
    {
        // Block-rate pitch modulation (computed at block midpoint)
        int mid = numSamples / 2;
        const float lfo1Depth = computeEffectiveLfoDepth(p, EnvTarget::LFO1Depth,
                                                         p.lfo1Depth, lastMod1Val_, lastMod2Val_);
        const float lfo2Depth = computeEffectiveLfoDepth(p, EnvTarget::LFO2Depth,
                                                         p.lfo2Depth, lastMod1Val_, lastMod2Val_);
        float pitchMod = p.driftPitchOffset;
        if (p.mod1Target == EnvTarget::Pitch) pitchMod += lastMod1Val_;
        if (p.mod2Target == EnvTarget::Pitch) pitchMod += lastMod2Val_;
        if (p.lfo1Target == LfoTarget::Pitch) pitchMod += lfo1Buf[mid] * lfo1Depth;
        if (p.lfo2Target == LfoTarget::Pitch) pitchMod += lfo2Buf[mid] * lfo2Depth;
        sampler.setPitchModulation(1.0f + pitchMod);

        sampler.renderPitchedBlock(samplerBlockBuf_.data(), numSamples);
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
            const float lfo1Depth = computeEffectiveLfoDepth(p, EnvTarget::LFO1Depth,
                                                             p.lfo1Depth, lastMod1Val_, lastMod2Val_);
            const float lfo2Depth = computeEffectiveLfoDepth(p, EnvTarget::LFO2Depth,
                                                             p.lfo2Depth, lastMod1Val_, lastMod2Val_);
            float lfo1Mid = lfo1Buf[midIdx] * lfo1Depth;
            float lfo2Mid = lfo2Buf[midIdx] * lfo2Depth;

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

        // ── Per-sample inner loop: envelopes + audio + VCA ──
        for (int i = pos; i < subBlockEnd; ++i)
        {
            float ampEnvVal = ampEnv.processSample() * p.ampAmount;
            float mod1EnvVal = modEnv1.processSample() * p.mod1Amount;
            float mod2EnvVal = modEnv2.processSample() * p.mod2Amount;
            lastAmpEnvLevel = ampEnvVal;
            lastMod1Val_ = mod1EnvVal;
            lastMod2Val_ = mod2EnvVal;

            const float lfo1Depth = computeEffectiveLfoDepth(p, EnvTarget::LFO1Depth,
                                                             p.lfo1Depth, mod1EnvVal, mod2EnvVal);
            const float lfo2Depth = computeEffectiveLfoDepth(p, EnvTarget::LFO2Depth,
                                                             p.lfo2Depth, mod1EnvVal, mod2EnvVal);
            float lfo1Val = lfo1Buf[i] * lfo1Depth;
            float lfo2Val = lfo2Buf[i] * lfo2Depth;

            float sample = 0.0f;

            if (samplerMode)
            {
                // Sampler: read from pre-rendered pitch-shifted block
                sample = samplerBlockBuf_[static_cast<size_t>(i)];
            }
            else if (oscReady)
            {
                // Wavetable: per-sample pitch modulation
                float pitchMod = p.driftPitchOffset;
                if (p.mod1Target == EnvTarget::Pitch) pitchMod += mod1EnvVal;
                if (p.mod2Target == EnvTarget::Pitch) pitchMod += mod2EnvVal;
                if (p.lfo1Target == LfoTarget::Pitch) pitchMod += lfo1Val;
                if (p.lfo2Target == LfoTarget::Pitch) pitchMod += lfo2Val;

                if (!osc.isGliding())
                {
                    baseFrequency = tunedHz(currentNote + octaveShift_ * 12);
                    osc.setFrequency(baseFrequency * (1.0f + pitchMod));
                }

                float scanBase = p.wtAutoScan ? 0.0f : p.baseScan;
                float scanMod = scanBase + p.driftScanOffset;
                if (p.mod1Target == EnvTarget::Scan) scanMod += mod1EnvVal;
                if (p.mod2Target == EnvTarget::Scan) scanMod += mod2EnvVal;
                if (p.lfo1Target == LfoTarget::Scan) scanMod += lfo1Val;
                if (p.lfo2Target == LfoTarget::Scan) scanMod += lfo2Val;
                const float clampedScan = juce::jlimit(0.0f, 1.0f, scanMod);
                osc.setScanPosition(clampedScan);
                osc.setInterpolation(p.wtSmooth);

                sample = osc.processSample();
                lastModulatedScan_ = osc.getCurrentScanPosition();
            }

            // Mix noise oscillator (goes through VCA + filter with the main signal)
            float noiseLevel = p.noiseLevel;
            if (p.mod1Target == EnvTarget::NoiseLevel) noiseLevel += mod1EnvVal;
            if (p.mod2Target == EnvTarget::NoiseLevel) noiseLevel += mod2EnvVal;
            if (p.lfo1Target == LfoTarget::NoiseLevel) noiseLevel += lfo1Val;
            if (p.lfo2Target == LfoTarget::NoiseLevel) noiseLevel += lfo2Val;
            noiseLevel = juce::jlimit(0.0f, 1.0f, noiseLevel);
            lastModulatedNoiseLevel_ = noiseLevel;
            if (noiseLevel > 0.001f)
            {
                noise.setType(static_cast<NoiseType>(p.noiseType));
                sample += noise.processSample() * noiseLevel;
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
