#include "SynthVoice.h"
#include <cstring>

namespace
{
// Per-target aftertouch normalization: one global Amt knob, scaled into each
// target's natural units so heterogeneous targets feel comparable. [0..1]-range
// targets (Scan, Reso, Noise, Env Sustain, LFO Depth) add up to `amount`
// directly; the constants below set full-scale depth for the unit-bearing ones.
constexpr float kFilterModOctaves = 10.0f;   // Cutoff: ×2^(drive·octaves)
constexpr float kAtPitchSemitones = 12.0f;   // Pitch:  up to +N semitones (ratio)
constexpr float kAtDcaGain        = 1.0f;    // DCA:    gain ×(1 + drive·this)

float applyNormalizedOffset(float baseValue, float modulationOffset)
{
    return juce::jlimit(0.0f, 1.0f, baseValue + modulationOffset);
}

// Single source of truth for "is this target driven by aftertouch?". Phase 2 is
// single-select (the aftertouchTarget int); Phase 3 swaps this to per-target
// bools — every DSP hook routes through here, so only this body changes.
bool aftertouchTargetActive(const BlockParams& p, int target)
{
    return p.aftertouchTarget == target;
}

// Normalized aftertouch drive [0..amount] for an active target, else 0.
float aftertouchDrive(const BlockParams& p, int target, float pressure)
{
    return aftertouchTargetActive(p, target)
        ? juce::jlimit(0.0f, 1.0f, pressure) * p.aftertouchAmount
        : 0.0f;
}

float computeEffectiveLfoDepth(const BlockParams& p, int target, float baseDepth,
                               float ampEnvVal, float mod1EnvVal, float mod2EnvVal)
{
    float depth = baseDepth;
    if (p.ampTarget == target)
        depth = applyNormalizedOffset(depth, ampEnvVal);
    if (p.mod1Target == target)
        depth = applyNormalizedOffset(depth, mod1EnvVal);
    if (p.mod2Target == target)
        depth = applyNormalizedOffset(depth, mod2EnvVal);
    return depth;
}

// [0..1]-range additive targets (Scan, Resonance, Noise, Env Sustain, LFO Depth).
float applyAftertouchTarget(const BlockParams& p, int target, float baseValue, float pressure)
{
    return applyNormalizedOffset(baseValue, aftertouchDrive(p, target, pressure));
}

// Cutoff: multiplicative octaves (the base ranges over the full audio band).
float applyAftertouchCutoffTarget(const BlockParams& p, float cutoffHz, float pressure)
{
    const float drive = aftertouchDrive(p, AftertouchTarget::Cutoff, pressure);
    return drive > 0.0f ? cutoffHz * std::pow(2.0f, drive * kFilterModOctaves) : cutoffHz;
}

// Pitch: adds up to kAtPitchSemitones of upward bend as a ratio offset onto the
// existing pitchMod (which is summed with 1.0 downstream before setFrequency).
float applyAftertouchPitchMod(const BlockParams& p, float pitchMod, float pressure)
{
    const float drive = aftertouchDrive(p, AftertouchTarget::Pitch, pressure);
    return drive > 0.0f
        ? pitchMod + (std::pow(2.0f, drive * kAtPitchSemitones / 12.0f) - 1.0f)
        : pitchMod;
}

// DCA: swells the VCA gain by up to ×(1 + amount·kAtDcaGain) with pressure.
float applyAftertouchDcaGain(const BlockParams& p, float gain, float pressure)
{
    return gain * (1.0f + aftertouchDrive(p, AftertouchTarget::DCA, pressure) * kAtDcaGain);
}

float computeDcaGain(const BlockParams& p, float ampEnvVal, float mod1EnvVal, float mod2EnvVal)
{
    float vca = (p.ampTarget == EnvTarget::DCA) ? ampEnvVal : 1.0f;
    if (p.mod1Target == EnvTarget::DCA) vca *= (1.0f + mod1EnvVal);
    if (p.mod2Target == EnvTarget::DCA) vca *= (1.0f + mod2EnvVal);
    return std::max(0.0f, vca);
}

// Signed per-stage velocity → time scale. velSense ∈ [-1..+1]:
//   +  harder hits LENGTHEN the stage (up to ×2 at full velocity),
//   -  harder hits SHORTEN it (down to ×0.5), 0 = no effect.
float computeVelocityTimeScale(float velSense, float velocity)
{
    if (velSense == 0.0f)
        return 1.0f;

    const float centeredVelocity = juce::jlimit(0.0f, 1.0f, velocity) * 2.0f - 1.0f;
    return std::pow(2.0f, centeredVelocity * velSense);
}

float computeVelocityTimedMs(float baseMs, float velSense, float velocity)
{
    return std::max(0.0f, baseMs * computeVelocityTimeScale(velSense, velocity));
}
}

void SynthVoice::prepare(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlockSize_ = samplesPerBlock;
    samplerBlockBuf_.resize(static_cast<size_t>(samplesPerBlock));
    osc.prepare(sampleRate, samplesPerBlock);
    sampler.prepare(sampleRate, samplesPerBlock);
    freezeEngine.prepare(sampleRate, samplesPerBlock);
    noise.prepare(sampleRate);
    ampEnv.prepare(sampleRate);
    modEnv1.prepare(sampleRate);
    modEnv2.prepare(sampleRate);
    perVoiceLfo1.prepare(sampleRate);
    perVoiceLfo2.prepare(sampleRate);
    perVoiceLfo3.prepare(sampleRate);
    perVoiceLfoBuf1_.resize(static_cast<size_t>(samplesPerBlock));
    perVoiceLfoBuf2_.resize(static_cast<size_t>(samplesPerBlock));
    perVoiceLfoBuf3_.resize(static_cast<size_t>(samplesPerBlock));
    filter.prepare(sampleRate, samplesPerBlock);
    filterLadder.prepare(sampleRate, samplesPerBlock);
    filterWarp.prepare(sampleRate, samplesPerBlock);
    filterR.prepare(sampleRate, samplesPerBlock);
    filterLadderR.prepare(sampleRate, samplesPerBlock);
    filterWarpR.prepare(sampleRate, samplesPerBlock);

    // Build + init the three oversamplers around the pre-filter tanh drive.
    // 2 channels (L+R) for stereo drive — same OS instance handles both with
    // channel-aligned internal state. Init size is SUB_BLOCK_SIZE because
    // renderBlock drives the OS in sub-block chunks.
    using Os = juce::dsp::Oversampling<float>;
    driveOs2x_ = std::make_unique<Os>(2, 1, Os::filterHalfBandPolyphaseIIR, true, false);
    driveOs4x_ = std::make_unique<Os>(2, 2, Os::filterHalfBandPolyphaseIIR, true, false);
    driveOs8x_ = std::make_unique<Os>(2, 3, Os::filterHalfBandPolyphaseIIR, true, false);
    driveOs2x_->initProcessing(static_cast<size_t>(SUB_BLOCK_SIZE));
    driveOs4x_->initProcessing(static_cast<size_t>(SUB_BLOCK_SIZE));
    driveOs8x_->initProcessing(static_cast<size_t>(SUB_BLOCK_SIZE));
}

void SynthVoice::reset()
{
    osc.reset();
    sampler.reset();
    freezeEngine.reset();
    ampEnv.reset();
    modEnv1.reset();
    modEnv2.reset();
    filter.reset();
    filterLadder.reset();
    filterWarp.reset();
    filterR.reset();
    filterLadderR.reset();
    filterWarpR.reset();
    noise.reset();
    if (driveOs2x_) driveOs2x_->reset();
    if (driveOs4x_) driveOs4x_->reset();
    if (driveOs8x_) driveOs8x_->reset();
    active = false;
    noteHeld = false;
    currentNote = -1;
    aftertouch_ = 0.0f;
    lastAmpEnvLevel = 0.0f;
    lastMod1Val_ = 0.0f;
    lastMod2Val_ = 0.0f;
    lastModulatedCutoff_ = 20000.0f;
    lastModulatedResonance_ = 0.0f;
    lastModulatedScan_ = 0.0f;
    lastModulatedNoiseLevel_ = 0.0f;
    lastOutputSample_ = 0.0f;
    lastOutputSampleR_ = 0.0f;
    restartFadeTailSample_ = 0.0f;
    restartFadeTailSampleR_ = 0.0f;
    restartFadeSamplesLeft_ = 0;
    restartFadeTotalSamples_ = 1;
    samplerPreStretchNormGain_ = 1.0f;
    samplerPreStretchNormDirty_ = true;
    preStretchNormState_ = {};
}

void SynthVoice::beginRestartFade()
{
    restartFadeTailSample_  = lastOutputSample_;
    restartFadeTailSampleR_ = lastOutputSampleR_;
    restartFadeTotalSamples_ = std::max(1,
        static_cast<int>(RESTART_FADE_MS * 0.001f * static_cast<float>(sr)));
    restartFadeSamplesLeft_ = restartFadeTotalSamples_;
}

void SynthVoice::applyRestartFadeStereo(float& L, float& R)
{
    if (restartFadeSamplesLeft_ <= 0)
        return;

    const int samplesDone = restartFadeTotalSamples_ - restartFadeSamplesLeft_ + 1;
    const float t = juce::jlimit(0.0f, 1.0f,
        static_cast<float>(samplesDone) / static_cast<float>(restartFadeTotalSamples_));
    --restartFadeSamplesLeft_;

    L = restartFadeTailSample_  + (L - restartFadeTailSample_)  * t;
    R = restartFadeTailSampleR_ + (R - restartFadeTailSampleR_) * t;
}

void SynthVoice::noteOn(int note, float velocity, bool legato)
{
    currentNote = note;
    currentVelocity = velocity;
    noteHeld = true;
    active = true;
    applyVelocityTimedEnvelopeTimes();

    if (!legato)
    {
        // Peak is fixed at 1.0; envelope depth is owned by Amt. Velocity shapes
        // only the A/D/R times (applyVelocityTimedEnvelopeTimes), never the level.
        ampEnv.noteOn(1.0f);
        modEnv1.noteOn(1.0f);
        modEnv2.noteOn(1.0f);
        // Fresh note starts at neutral MPE timbre until its first CC74 arrives;
        // legato (held finger sliding to a new note) keeps the current timbre.
        timbre_ = kTimbreNeutral;
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
    else if (engineMode == EngineMode::Freeze)
    {
        double ratio = static_cast<double>(tunedHz(shiftedNote))
                     / static_cast<double>(tunedHz(60));
        freezeEngine.setTransposeRatio(ratio);
        freezeEngine.retrigger();
    }
}

void SynthVoice::noteOff()
{
    noteHeld = false;
    applyVelocityTimedEnvelopeTimes();
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
    else if (engineMode == EngineMode::Freeze)
    {
        double ratio = static_cast<double>(tunedHz(shiftedNote))
                     / static_cast<double>(tunedHz(60));
        freezeEngine.glideToRatio(ratio, glideMs);
    }
    else
    {
        float targetFreq = tunedHz(shiftedNote);
        osc.glideToFrequency(targetFreq, glideMs);
    }
}

void SynthVoice::configureForBlock(const BlockParams& p)
{
    octaveShift_ = p.octaveShift;
    ampAttackBaseMs_ = p.ampAttack;
    ampDecayBaseMs_ = p.ampDecay;
    ampReleaseBaseMs_ = p.ampRelease;
    ampAttackVelSens_ = p.ampAttackVelSens;
    ampDecayVelSens_ = p.ampDecayVelSens;
    ampReleaseVelSens_ = p.ampReleaseVelSens;
    ampEnv.setSustain(applyAftertouchTarget(p, AftertouchTarget::Env1Sustain,
                                            p.ampSustain, aftertouch_));
    ampEnv.setLooping(p.ampLoop);
    ampEnv.setAttackCurve(static_cast<CurveShape>(p.ampAttackCurve));
    ampEnv.setDecayCurve(static_cast<CurveShape>(p.ampDecayCurve));
    ampEnv.setReleaseCurve(static_cast<CurveShape>(p.ampReleaseCurve));

    mod1AttackBaseMs_ = p.mod1Attack;
    mod1DecayBaseMs_ = p.mod1Decay;
    mod1ReleaseBaseMs_ = p.mod1Release;
    mod1AttackVelSens_ = p.mod1AttackVelSens;
    mod1DecayVelSens_ = p.mod1DecayVelSens;
    mod1ReleaseVelSens_ = p.mod1ReleaseVelSens;
    modEnv1.setSustain(applyAftertouchTarget(p, AftertouchTarget::Env2Sustain,
                                             p.mod1Sustain, aftertouch_));
    modEnv1.setLooping(p.mod1Loop);
    modEnv1.setAttackCurve(static_cast<CurveShape>(p.mod1AttackCurve));
    modEnv1.setDecayCurve(static_cast<CurveShape>(p.mod1DecayCurve));
    modEnv1.setReleaseCurve(static_cast<CurveShape>(p.mod1ReleaseCurve));

    mod2AttackBaseMs_ = p.mod2Attack;
    mod2DecayBaseMs_ = p.mod2Decay;
    mod2ReleaseBaseMs_ = p.mod2Release;
    mod2AttackVelSens_ = p.mod2AttackVelSens;
    mod2DecayVelSens_ = p.mod2DecayVelSens;
    mod2ReleaseVelSens_ = p.mod2ReleaseVelSens;
    modEnv2.setSustain(applyAftertouchTarget(p, AftertouchTarget::Env3Sustain,
                                             p.mod2Sustain, aftertouch_));
    modEnv2.setLooping(p.mod2Loop);
    modEnv2.setAttackCurve(static_cast<CurveShape>(p.mod2AttackCurve));
    modEnv2.setDecayCurve(static_cast<CurveShape>(p.mod2DecayCurve));
    modEnv2.setReleaseCurve(static_cast<CurveShape>(p.mod2ReleaseCurve));

    applyVelocityTimedEnvelopeTimes();

    if (!active) return;

    updateSamplerPreStretchNorm(p);
}

void SynthVoice::applyVelocityTimedEnvelopeTimes()
{
    ampEnv.setAttack(computeVelocityTimedMs(ampAttackBaseMs_, ampAttackVelSens_, currentVelocity));
    ampEnv.setDecay(computeVelocityTimedMs(ampDecayBaseMs_, ampDecayVelSens_, currentVelocity));
    ampEnv.setRelease(computeVelocityTimedMs(ampReleaseBaseMs_, ampReleaseVelSens_, currentVelocity));

    modEnv1.setAttack(computeVelocityTimedMs(mod1AttackBaseMs_, mod1AttackVelSens_, currentVelocity));
    modEnv1.setDecay(computeVelocityTimedMs(mod1DecayBaseMs_, mod1DecayVelSens_, currentVelocity));
    modEnv1.setRelease(computeVelocityTimedMs(mod1ReleaseBaseMs_, mod1ReleaseVelSens_, currentVelocity));

    modEnv2.setAttack(computeVelocityTimedMs(mod2AttackBaseMs_, mod2AttackVelSens_, currentVelocity));
    modEnv2.setDecay(computeVelocityTimedMs(mod2DecayBaseMs_, mod2DecayVelSens_, currentVelocity));
    modEnv2.setRelease(computeVelocityTimedMs(mod2ReleaseBaseMs_, mod2ReleaseVelSens_, currentVelocity));
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
        && preStretchNormState_.ampTarget == p.ampTarget
        && preStretchNormState_.ampLoop == p.ampLoop
        && preStretchNormState_.ampAttackCurve == p.ampAttackCurve
        && preStretchNormState_.ampDecayCurve == p.ampDecayCurve
        && preStretchNormState_.ampReleaseCurve == p.ampReleaseCurve
        && nearlyEqual(preStretchNormState_.ampAttackVelSens, p.ampAttackVelSens)
        && nearlyEqual(preStretchNormState_.ampDecayVelSens, p.ampDecayVelSens)
        && nearlyEqual(preStretchNormState_.ampReleaseVelSens, p.ampReleaseVelSens)
        && preStretchNormState_.mod1Target == p.mod1Target
        && nearlyEqual(preStretchNormState_.mod1Attack, p.mod1Attack)
        && nearlyEqual(preStretchNormState_.mod1Decay, p.mod1Decay)
        && nearlyEqual(preStretchNormState_.mod1Sustain, p.mod1Sustain)
        && nearlyEqual(preStretchNormState_.mod1Release, p.mod1Release)
        && nearlyEqual(preStretchNormState_.mod1Amount, p.mod1Amount)
        && preStretchNormState_.mod1Loop == p.mod1Loop
        && preStretchNormState_.mod1AttackCurve == p.mod1AttackCurve
        && preStretchNormState_.mod1DecayCurve == p.mod1DecayCurve
        && preStretchNormState_.mod1ReleaseCurve == p.mod1ReleaseCurve
        && nearlyEqual(preStretchNormState_.mod1AttackVelSens, p.mod1AttackVelSens)
        && nearlyEqual(preStretchNormState_.mod1DecayVelSens, p.mod1DecayVelSens)
        && nearlyEqual(preStretchNormState_.mod1ReleaseVelSens, p.mod1ReleaseVelSens)
        && preStretchNormState_.mod2Target == p.mod2Target
        && nearlyEqual(preStretchNormState_.mod2Attack, p.mod2Attack)
        && nearlyEqual(preStretchNormState_.mod2Decay, p.mod2Decay)
        && nearlyEqual(preStretchNormState_.mod2Sustain, p.mod2Sustain)
        && nearlyEqual(preStretchNormState_.mod2Release, p.mod2Release)
        && nearlyEqual(preStretchNormState_.mod2Amount, p.mod2Amount)
        && preStretchNormState_.mod2Loop == p.mod2Loop
        && preStretchNormState_.mod2AttackCurve == p.mod2AttackCurve
        && preStretchNormState_.mod2DecayCurve == p.mod2DecayCurve
        && preStretchNormState_.mod2ReleaseCurve == p.mod2ReleaseCurve
        && nearlyEqual(preStretchNormState_.mod2AttackVelSens, p.mod2AttackVelSens)
        && nearlyEqual(preStretchNormState_.mod2DecayVelSens, p.mod2DecayVelSens)
        && nearlyEqual(preStretchNormState_.mod2ReleaseVelSens, p.mod2ReleaseVelSens)
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

    const float ampAttackMs = computeVelocityTimedMs(p.ampAttack, p.ampAttackVelSens, currentVelocity);
    const float ampDecayMs = computeVelocityTimedMs(p.ampDecay, p.ampDecayVelSens, currentVelocity);
    const float ampReleaseMs = computeVelocityTimedMs(p.ampRelease, p.ampReleaseVelSens, currentVelocity);
    const float mod1AttackMs = computeVelocityTimedMs(p.mod1Attack, p.mod1AttackVelSens, currentVelocity);
    const float mod1DecayMs = computeVelocityTimedMs(p.mod1Decay, p.mod1DecayVelSens, currentVelocity);
    const float mod1ReleaseMs = computeVelocityTimedMs(p.mod1Release, p.mod1ReleaseVelSens, currentVelocity);
    const float mod2AttackMs = computeVelocityTimedMs(p.mod2Attack, p.mod2AttackVelSens, currentVelocity);
    const float mod2DecayMs = computeVelocityTimedMs(p.mod2Decay, p.mod2DecayVelSens, currentVelocity);
    const float mod2ReleaseMs = computeVelocityTimedMs(p.mod2Release, p.mod2ReleaseVelSens, currentVelocity);

    float analysisMs = (p.ampTarget == EnvTarget::DCA)
        ? envWindowMs(ampAttackMs, ampDecayMs, ampReleaseMs, p.ampLoop)
        : 0.0f;
    if (p.mod1Target == EnvTarget::DCA)
        analysisMs = std::max(analysisMs, envWindowMs(mod1AttackMs, mod1DecayMs, mod1ReleaseMs, p.mod1Loop));
    if (p.mod2Target == EnvTarget::DCA)
        analysisMs = std::max(analysisMs, envWindowMs(mod2AttackMs, mod2DecayMs, mod2ReleaseMs, p.mod2Loop));

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

    ampRef.setAttack(ampAttackMs);
    ampRef.setDecay(ampDecayMs);
    ampRef.setSustain(p.ampSustain);
    ampRef.setRelease(ampReleaseMs);
    ampRef.setLooping(p.ampLoop);
    ampRef.setAttackCurve(static_cast<CurveShape>(p.ampAttackCurve));
    ampRef.setDecayCurve(static_cast<CurveShape>(p.ampDecayCurve));
    ampRef.setReleaseCurve(static_cast<CurveShape>(p.ampReleaseCurve));

    mod1Ref.setAttack(mod1AttackMs);
    mod1Ref.setDecay(mod1DecayMs);
    mod1Ref.setSustain(p.mod1Sustain);
    mod1Ref.setRelease(mod1ReleaseMs);
    mod1Ref.setLooping(p.mod1Loop);
    mod1Ref.setAttackCurve(static_cast<CurveShape>(p.mod1AttackCurve));
    mod1Ref.setDecayCurve(static_cast<CurveShape>(p.mod1DecayCurve));
    mod1Ref.setReleaseCurve(static_cast<CurveShape>(p.mod1ReleaseCurve));

    mod2Ref.setAttack(mod2AttackMs);
    mod2Ref.setDecay(mod2DecayMs);
    mod2Ref.setSustain(p.mod2Sustain);
    mod2Ref.setRelease(mod2ReleaseMs);
    mod2Ref.setLooping(p.mod2Loop);
    mod2Ref.setAttackCurve(static_cast<CurveShape>(p.mod2AttackCurve));
    mod2Ref.setDecayCurve(static_cast<CurveShape>(p.mod2DecayCurve));
    mod2Ref.setReleaseCurve(static_cast<CurveShape>(p.mod2ReleaseCurve));

    ampRef.noteOn(1.0f);
    mod1Ref.noteOn(1.0f);
    mod2Ref.noteOn(1.0f);

    for (int i = 0; i < analysisSamples; ++i)
    {
        const float ampEnvVal = ampRef.processSample() * p.ampAmount;
        const float mod1EnvVal = mod1Ref.processSample() * p.mod1Amount;
        const float mod2EnvVal = mod2Ref.processSample() * p.mod2Amount;

        dcaCurve[static_cast<size_t>(i)] = computeDcaGain(p, ampEnvVal, mod1EnvVal, mod2EnvVal);
    }

    float analysisPeak = 0.0f;
    sampler.estimatePlaybackRms(dcaCurve.data(), analysisSamples, &analysisPeak);

    static constexpr float kCeiling = 0.95f;
    samplerPreStretchNormGain_ = 1.0f;

    if (analysisPeak > kCeiling + 1.0e-6f)
    {
        // Keep Normalize from quietly turning into a loudness target.
        // The prepared buffer has already been peak-normalized, so the
        // remaining job here is just to catch rare post-DCA overs, not to
        // push sustained material down toward a fixed RMS.
        samplerPreStretchNormGain_ = kCeiling / analysisPeak;
    }

    sampler.setSourceGain(samplerPreStretchNormGain_);

    preStretchNormState_.ampAttack = p.ampAttack;
    preStretchNormState_.ampDecay = p.ampDecay;
    preStretchNormState_.ampSustain = p.ampSustain;
    preStretchNormState_.ampRelease = p.ampRelease;
    preStretchNormState_.ampAmount = p.ampAmount;
    preStretchNormState_.ampTarget = p.ampTarget;
    preStretchNormState_.ampLoop = p.ampLoop;
    preStretchNormState_.ampAttackCurve = p.ampAttackCurve;
    preStretchNormState_.ampDecayCurve = p.ampDecayCurve;
    preStretchNormState_.ampReleaseCurve = p.ampReleaseCurve;
    preStretchNormState_.ampAttackVelSens = p.ampAttackVelSens;
    preStretchNormState_.ampDecayVelSens = p.ampDecayVelSens;
    preStretchNormState_.ampReleaseVelSens = p.ampReleaseVelSens;
    preStretchNormState_.mod1Target = p.mod1Target;
    preStretchNormState_.mod1Attack = p.mod1Attack;
    preStretchNormState_.mod1Decay = p.mod1Decay;
    preStretchNormState_.mod1Sustain = p.mod1Sustain;
    preStretchNormState_.mod1Release = p.mod1Release;
    preStretchNormState_.mod1Amount = p.mod1Amount;
    preStretchNormState_.mod1Loop = p.mod1Loop;
    preStretchNormState_.mod1AttackCurve = p.mod1AttackCurve;
    preStretchNormState_.mod1DecayCurve = p.mod1DecayCurve;
    preStretchNormState_.mod1ReleaseCurve = p.mod1ReleaseCurve;
    preStretchNormState_.mod1AttackVelSens = p.mod1AttackVelSens;
    preStretchNormState_.mod1DecayVelSens = p.mod1DecayVelSens;
    preStretchNormState_.mod1ReleaseVelSens = p.mod1ReleaseVelSens;
    preStretchNormState_.mod2Target = p.mod2Target;
    preStretchNormState_.mod2Attack = p.mod2Attack;
    preStretchNormState_.mod2Decay = p.mod2Decay;
    preStretchNormState_.mod2Sustain = p.mod2Sustain;
    preStretchNormState_.mod2Release = p.mod2Release;
    preStretchNormState_.mod2Amount = p.mod2Amount;
    preStretchNormState_.mod2Loop = p.mod2Loop;
    preStretchNormState_.mod2AttackCurve = p.mod2AttackCurve;
    preStretchNormState_.mod2DecayCurve = p.mod2DecayCurve;
    preStretchNormState_.mod2ReleaseCurve = p.mod2ReleaseCurve;
    preStretchNormState_.mod2AttackVelSens = p.mod2AttackVelSens;
    preStretchNormState_.mod2DecayVelSens = p.mod2DecayVelSens;
    preStretchNormState_.mod2ReleaseVelSens = p.mod2ReleaseVelSens;
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

void SynthVoice::renderBlock(float* output, float* outputRight, const BlockParams& p,
                              const float* lfo1Buf, const float* lfo2Buf, const float* lfo3Buf, int numSamples)
{
    if (!active)
    {
        std::memset(output, 0, sizeof(float) * static_cast<size_t>(numSamples));
        if (outputRight != nullptr)
            std::memset(outputRight, 0, sizeof(float) * static_cast<size_t>(numSamples));
        return;
    }

    // Per-voice Trig-mode LFOs — sync rate/waveform from global, fill the
    // voice's own buffer (reset at note-on by VoiceManager), then steer the
    // function-local pointers so all downstream readers see the per-voice
    // signal. Free mode leaves the parameters pointing at the shared global
    // buffer.
    auto fillPerVoice = [&](LFO& l, std::vector<float>& buf, float rate, int wave) {
        l.setRate(rate);
        l.setWaveform(wave);
        l.setDepth(1.0f);
        for (int i = 0; i < numSamples; ++i)
            buf[static_cast<size_t>(i)] = l.processSample();
    };
    if (p.lfo1TrigMode) { fillPerVoice(perVoiceLfo1, perVoiceLfoBuf1_, p.lfo1Rate, p.lfo1Wave); lfo1Buf = perVoiceLfoBuf1_.data(); }
    if (p.lfo2TrigMode) { fillPerVoice(perVoiceLfo2, perVoiceLfoBuf2_, p.lfo2Rate, p.lfo2Wave); lfo2Buf = perVoiceLfoBuf2_.data(); }
    if (p.lfo3TrigMode) { fillPerVoice(perVoiceLfo3, perVoiceLfoBuf3_, p.lfo3Rate, p.lfo3Wave); lfo3Buf = perVoiceLfoBuf3_.data(); }

    // Combine global pitch-bend ratio (all voices) with per-voice MPE pitch bend.
    // For standard MIDI perVoicePitchBendSemitones_ stays 0, so this is a no-op.
    const float effectivePitchRatio = p.performancePitchRatio
        * std::pow(2.0f, perVoicePitchBendSemitones_ / 12.0f);

    bool samplerMode = (engineMode == EngineMode::Sampler) && sampler.hasAudio();
    bool freezeMode = (engineMode == EngineMode::Freeze) && freezeEngine.hasAudio();
    bool oscReady = (engineMode == EngineMode::Wavetable) && osc.hasFrames();

    // Hoist: setInterpolation is a pure setter; tunedHz is block-constant.
    float blockBaseFreqWavetable = 0.0f;
    if (oscReady)
    {
        osc.setInterpolation(p.wtSmooth);
        blockBaseFreqWavetable = tunedHz(currentNote + octaveShift_ * 12);
    }

    if (freezeMode)
    {
        freezeEngine.setTextureMode(p.freezeTexture);
        freezeEngine.setStereoWidth(p.freezeStereo);
    }

    // ── Sampler mode: pre-render pitch-shifted block via Signalsmith Stretch ──
    if (samplerMode)
    {
        // Block-rate pitch modulation (computed at block midpoint)
        int mid = numSamples / 2;
        const float lfo1Depth = applyAftertouchTarget(p, AftertouchTarget::LFO1Depth,
            computeEffectiveLfoDepth(p, EnvTarget::LFO1Depth, p.lfo1Depth,
                                     lastAmpEnvLevel, lastMod1Val_, lastMod2Val_), aftertouch_);
        const float lfo2Depth = applyAftertouchTarget(p, AftertouchTarget::LFO2Depth,
            computeEffectiveLfoDepth(p, EnvTarget::LFO2Depth, p.lfo2Depth,
                                     lastAmpEnvLevel, lastMod1Val_, lastMod2Val_), aftertouch_);
        const float lfo3Depth = applyAftertouchTarget(p, AftertouchTarget::LFO3Depth,
            computeEffectiveLfoDepth(p, EnvTarget::LFO3Depth, p.lfo3Depth,
                                     lastAmpEnvLevel, lastMod1Val_, lastMod2Val_), aftertouch_);
        float pitchMod = p.driftPitchOffset;
        if (p.ampTarget == EnvTarget::Pitch) pitchMod += lastAmpEnvLevel;
        if (p.mod1Target == EnvTarget::Pitch) pitchMod += lastMod1Val_;
        if (p.mod2Target == EnvTarget::Pitch) pitchMod += lastMod2Val_;
        if (p.lfo1Target == LfoTarget::Pitch) pitchMod += lfo1Buf[mid] * lfo1Depth;
        if (p.lfo2Target == LfoTarget::Pitch) pitchMod += lfo2Buf[mid] * lfo2Depth;
        if (p.lfo3Target == LfoTarget::Pitch) pitchMod += lfo3Buf[mid] * lfo3Depth;
        pitchMod = applyAftertouchPitchMod(p, pitchMod, aftertouch_);
        sampler.setPitchModulation(effectivePitchRatio * (1.0f + pitchMod));

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
            const float lfo1Depth = applyAftertouchTarget(p, AftertouchTarget::LFO1Depth,
                computeEffectiveLfoDepth(p, EnvTarget::LFO1Depth, p.lfo1Depth,
                                         lastAmpEnvLevel, lastMod1Val_, lastMod2Val_), aftertouch_);
            const float lfo2Depth = applyAftertouchTarget(p, AftertouchTarget::LFO2Depth,
                computeEffectiveLfoDepth(p, EnvTarget::LFO2Depth, p.lfo2Depth,
                                         lastAmpEnvLevel, lastMod1Val_, lastMod2Val_), aftertouch_);
            const float lfo3Depth = applyAftertouchTarget(p, AftertouchTarget::LFO3Depth,
                computeEffectiveLfoDepth(p, EnvTarget::LFO3Depth, p.lfo3Depth,
                                         lastAmpEnvLevel, lastMod1Val_, lastMod2Val_), aftertouch_);
            float lfo1Mid = lfo1Buf[midIdx] * lfo1Depth;
            float lfo2Mid = lfo2Buf[midIdx] * lfo2Depth;
            float lfo3Mid = lfo3Buf[midIdx] * lfo3Depth;

            float cutoffMod = p.baseCutoff;

            if (p.kbdTrack > 0.0f && currentNote >= 0)
                cutoffMod *= std::pow(2.0f, (static_cast<float>(currentNote) - 60.0f) / 12.0f * p.kbdTrack);

            constexpr float FILTER_OCTAVES = 10.0f;
            float rawAmp = (p.ampAmount > 0.001f) ? lastAmpEnvLevel / p.ampAmount : 0.0f;
            float rawEnv1 = (p.mod1Amount > 0.001f) ? lastMod1Val_ / p.mod1Amount : 0.0f;
            float rawEnv2 = (p.mod2Amount > 0.001f) ? lastMod2Val_ / p.mod2Amount : 0.0f;

            if (p.ampTarget == EnvTarget::Filter) cutoffMod *= std::pow(2.0f, rawAmp * p.ampAmount * FILTER_OCTAVES);
            if (p.mod1Target == EnvTarget::Filter) cutoffMod *= std::pow(2.0f, rawEnv1 * p.mod1Amount * FILTER_OCTAVES);
            if (p.mod2Target == EnvTarget::Filter) cutoffMod *= std::pow(2.0f, rawEnv2 * p.mod2Amount * FILTER_OCTAVES);
            if (p.lfo1Target == LfoTarget::Filter) cutoffMod *= std::pow(2.0f, lfo1Mid * FILTER_OCTAVES);
            if (p.lfo2Target == LfoTarget::Filter) cutoffMod *= std::pow(2.0f, lfo2Mid * FILTER_OCTAVES);
            if (p.lfo3Target == LfoTarget::Filter) cutoffMod *= std::pow(2.0f, lfo3Mid * FILTER_OCTAVES);
            if (p.driftFilterOffset != 0.0f)
                cutoffMod *= std::pow(2.0f, p.driftFilterOffset * FILTER_OCTAVES);
            cutoffMod = applyAftertouchCutoffTarget(p, cutoffMod, aftertouch_);

            // MPE Timbre (CC74 / the Y slide axis) → filter brightness, the
            // conventional MPE mapping. Bipolar around the neutral centre (CC64):
            // full up = +octaves, full down = −octaves. Gated so a note with no
            // timbre data (every non-MPE note) pays nothing and is unmodulated.
            // Per-block, like every other factor in this chain.
            if (timbre_ != kTimbreNeutral)
            {
                constexpr float MPE_TIMBRE_OCTAVES = 4.0f;
                cutoffMod *= std::pow(2.0f, (timbre_ - kTimbreNeutral) * 2.0f * MPE_TIMBRE_OCTAVES);
            }

            cutoffMod = juce::jlimit(20.0f, 20000.0f, cutoffMod);
            const float resonanceMod = applyAftertouchTarget(
                p, AftertouchTarget::Resonance, p.baseReso, aftertouch_);
            lastModulatedCutoff_ = cutoffMod;
            lastModulatedResonance_ = resonanceMod;

            // Configure only the active filter model — the inactive ones sit
            // idle, so touching them would just waste cycles on coefficient
            // updates that no one hears. Mirror the same coefficients to the
            // right-channel instance so L and R filter identically (same
            // cutoff/reso/type/slope), with separate internal state.
            switch (p.filterAlgorithm)
            {
                case FilterAlgorithm::SVF:
                    filter.setCutoff(cutoffMod);
                    filter.setResonance(resonanceMod);
                    filter.setType(p.filterType);
                    filter.setSlope(p.filterSlope);
                    filter.setMix(p.filterMix);
                    filterR.setCutoff(cutoffMod);
                    filterR.setResonance(resonanceMod);
                    filterR.setType(p.filterType);
                    filterR.setSlope(p.filterSlope);
                    filterR.setMix(p.filterMix);
                    break;
                case FilterAlgorithm::Ladder:
                    filterLadder.setCutoff(cutoffMod);
                    filterLadder.setResonance(resonanceMod);
                    filterLadder.setType(p.filterType);
                    filterLadder.setSlope(p.filterSlope);
                    filterLadder.setMix(p.filterMix);
                    // Drive feeds the ladder's own tanh stages (Phase B stays
                    // linear for Ladder), so the character comes from the
                    // filter saturating, not from a shortcut pre-filter tanh.
                    filterLadder.setInputDrive(p.filterDriveGain);
                    filterLadderR.setCutoff(cutoffMod);
                    filterLadderR.setResonance(resonanceMod);
                    filterLadderR.setType(p.filterType);
                    filterLadderR.setSlope(p.filterSlope);
                    filterLadderR.setMix(p.filterMix);
                    filterLadderR.setInputDrive(p.filterDriveGain);
                    break;
                case FilterAlgorithm::Warp:
                    filterWarp.setCutoff(cutoffMod);
                    filterWarp.setResonance(resonanceMod);
                    filterWarp.setType(p.filterType);
                    filterWarp.setSlope(p.filterSlope);
                    filterWarp.setMix(p.filterMix);
                    filterWarp.setStyle(p.filterWarpStyle);
                    filterWarp.setInputDrive(p.filterDriveGain);
                    filterWarpR.setCutoff(cutoffMod);
                    filterWarpR.setResonance(resonanceMod);
                    filterWarpR.setType(p.filterType);
                    filterWarpR.setSlope(p.filterSlope);
                    filterWarpR.setMix(p.filterMix);
                    filterWarpR.setStyle(p.filterWarpStyle);
                    filterWarpR.setInputDrive(p.filterDriveGain);
                    break;
            }
        }

        // ── Phase A (per sample): generate raw osc/noise and cache VCA ──
        // Drive, filter and VCA are applied below as block operations so the
        // drive can be wrapped in oversampling without pulling the filter or
        // VCA up to the oversampled rate.
        float vcaScratch[SUB_BLOCK_SIZE] {};
        float outputRBuf[SUB_BLOCK_SIZE] {};   // right-channel scratch parallel to output[pos..]
        int lastI = subBlockEnd;      // exclusive end of the filled range
        bool goingIdle = false;
        for (int i = pos; i < subBlockEnd; ++i)
        {
            float ampEnvVal = ampEnv.processSample() * p.ampAmount;
            float mod1EnvVal = modEnv1.processSample() * p.mod1Amount;
            float mod2EnvVal = modEnv2.processSample() * p.mod2Amount;
            lastAmpEnvLevel = ampEnvVal;
            lastMod1Val_ = mod1EnvVal;
            lastMod2Val_ = mod2EnvVal;

            const float lfo1Depth = applyAftertouchTarget(p, AftertouchTarget::LFO1Depth,
                computeEffectiveLfoDepth(p, EnvTarget::LFO1Depth, p.lfo1Depth,
                                         ampEnvVal, mod1EnvVal, mod2EnvVal), aftertouch_);
            const float lfo2Depth = applyAftertouchTarget(p, AftertouchTarget::LFO2Depth,
                computeEffectiveLfoDepth(p, EnvTarget::LFO2Depth, p.lfo2Depth,
                                         ampEnvVal, mod1EnvVal, mod2EnvVal), aftertouch_);
            const float lfo3Depth = applyAftertouchTarget(p, AftertouchTarget::LFO3Depth,
                computeEffectiveLfoDepth(p, EnvTarget::LFO3Depth, p.lfo3Depth,
                                         ampEnvVal, mod1EnvVal, mod2EnvVal), aftertouch_);
            float lfo1Val = lfo1Buf[i] * lfo1Depth;
            float lfo2Val = lfo2Buf[i] * lfo2Depth;
            float lfo3Val = lfo3Buf[i] * lfo3Depth;

            float sample = 0.0f;
            float sampleR = 0.0f;

            if (samplerMode)
            {
                // Sampler: read from pre-rendered pitch-shifted block (mono → duplicate to R)
                sample = samplerBlockBuf_[static_cast<size_t>(i)];
                sampleR = sample;
            }
            else if (freezeMode)
            {
                float pitchMod = p.driftPitchOffset;
                if (p.ampTarget == EnvTarget::Pitch) pitchMod += ampEnvVal;
                if (p.mod1Target == EnvTarget::Pitch) pitchMod += mod1EnvVal;
                if (p.mod2Target == EnvTarget::Pitch) pitchMod += mod2EnvVal;
                if (p.lfo1Target == LfoTarget::Pitch) pitchMod += lfo1Val;
                if (p.lfo2Target == LfoTarget::Pitch) pitchMod += lfo2Val;
                if (p.lfo3Target == LfoTarget::Pitch) pitchMod += lfo3Val;
                pitchMod = applyAftertouchPitchMod(p, pitchMod, aftertouch_);
                freezeEngine.setPitchModulation(effectivePitchRatio
                    * juce::jlimit(0.0625f, 16.0f, 1.0f + pitchMod));

                float scanMod = p.baseScan + p.driftScanOffset;
                if (p.ampTarget == EnvTarget::Scan) scanMod += ampEnvVal;
                if (p.mod1Target == EnvTarget::Scan) scanMod += mod1EnvVal;
                if (p.mod2Target == EnvTarget::Scan) scanMod += mod2EnvVal;
                if (p.lfo1Target == LfoTarget::Scan) scanMod += lfo1Val;
                if (p.lfo2Target == LfoTarget::Scan) scanMod += lfo2Val;
                if (p.lfo3Target == LfoTarget::Scan) scanMod += lfo3Val;
                scanMod = applyAftertouchTarget(p, AftertouchTarget::Scan, scanMod, aftertouch_);
                freezeEngine.setPosition(juce::jlimit(0.0f, 1.0f, scanMod));

                float freezeLeft = 0.0f;
                float freezeRight = 0.0f;
                freezeEngine.processSampleStereo(freezeLeft, freezeRight);
                sample = freezeLeft;
                sampleR = freezeRight;
                lastModulatedScan_ = freezeEngine.getCurrentPosition();
            }
            else if (oscReady)
            {
                // Wavetable: per-sample pitch modulation
                float pitchMod = p.driftPitchOffset;
                if (p.ampTarget == EnvTarget::Pitch) pitchMod += ampEnvVal;
                if (p.mod1Target == EnvTarget::Pitch) pitchMod += mod1EnvVal;
                if (p.mod2Target == EnvTarget::Pitch) pitchMod += mod2EnvVal;
                if (p.lfo1Target == LfoTarget::Pitch) pitchMod += lfo1Val;
                if (p.lfo2Target == LfoTarget::Pitch) pitchMod += lfo2Val;
                if (p.lfo3Target == LfoTarget::Pitch) pitchMod += lfo3Val;
                pitchMod = applyAftertouchPitchMod(p, pitchMod, aftertouch_);

                if (!osc.isGliding())
                {
                    baseFrequency = blockBaseFreqWavetable;
                    osc.setFrequency(baseFrequency * effectivePitchRatio * (1.0f + pitchMod));
                }

                float scanBase = p.wtAutoScan ? 0.0f : p.baseScan;
                float scanMod = scanBase + p.driftScanOffset;
                if (p.ampTarget == EnvTarget::Scan) scanMod += ampEnvVal;
                if (p.mod1Target == EnvTarget::Scan) scanMod += mod1EnvVal;
                if (p.mod2Target == EnvTarget::Scan) scanMod += mod2EnvVal;
                if (p.lfo1Target == LfoTarget::Scan) scanMod += lfo1Val;
                if (p.lfo2Target == LfoTarget::Scan) scanMod += lfo2Val;
                if (p.lfo3Target == LfoTarget::Scan) scanMod += lfo3Val;
                scanMod = applyAftertouchTarget(p, AftertouchTarget::Scan, scanMod, aftertouch_);
                const float clampedScan = juce::jlimit(0.0f, 1.0f, scanMod);
                osc.setScanPosition(clampedScan);

                sample = osc.processSample();
                sampleR = sample;
                lastModulatedScan_ = osc.getCurrentScanPosition();
            }

            // Mix noise oscillator (goes through drive + filter + VCA with the main signal)
            float noiseLevel = p.noiseLevel;
            if (p.ampTarget == EnvTarget::NoiseLevel) noiseLevel += ampEnvVal;
            if (p.mod1Target == EnvTarget::NoiseLevel) noiseLevel += mod1EnvVal;
            if (p.mod2Target == EnvTarget::NoiseLevel) noiseLevel += mod2EnvVal;
            if (p.lfo1Target == LfoTarget::NoiseLevel) noiseLevel += lfo1Val;
            if (p.lfo2Target == LfoTarget::NoiseLevel) noiseLevel += lfo2Val;
            if (p.lfo3Target == LfoTarget::NoiseLevel) noiseLevel += lfo3Val;
            // Aftertouch → Noise (additive, clamps to [0,1] internally).
            noiseLevel = applyAftertouchTarget(p, AftertouchTarget::NoiseLevel, noiseLevel, aftertouch_);
            lastModulatedNoiseLevel_ = noiseLevel;
            if (noiseLevel > 0.001f)
            {
                noise.setType(static_cast<NoiseType>(p.noiseType));
                const float n = noise.processSample() * noiseLevel;
                sample  += n;
                sampleR += n;
            }

            // Cache VCA for phase D; raw audio goes to output[i] / outputRBuf untouched.
            float vca = computeDcaGain(p, ampEnvVal, mod1EnvVal, mod2EnvVal);
            vca = applyAftertouchDcaGain(p, vca, aftertouch_);

            output[i] = sample;
            outputRBuf[i - pos] = sampleR;
            vcaScratch[i - pos] = vca;

            if (ampEnv.isIdle() && !noteHeld)
            {
                active = false;
                lastI = i + 1;
                for (int j = lastI; j < numSamples; ++j)
                {
                    output[j] = 0.0f;
                    if (outputRight != nullptr)
                        outputRight[j] = 0.0f;
                }
                goingIdle = true;
                break;
            }
        }

        const int driveLen = lastI - pos;

        // ── Phase B: drive stage ──
        // For SVF (linear filter): apply tanh as the saturation, optionally
        // oversampled — the SVF is LTI so the pre-filter tanh *is* the drive
        // character.
        // For Ladder / Warp (own nonlinearities): pre-filter tanh would flat-
        // clip the signal and leave nothing for the filter's internal stages
        // to shape. Instead the drive amount is forwarded to the filter via
        // setInputDrive() at sub-block setup time, and Phase B is a no-op.
        if (p.filterEnabled && p.filterDriveDb > 0.01f && driveLen > 0
            && p.filterAlgorithm == FilterAlgorithm::SVF)
        {
            const float driveGain = p.filterDriveGain;

            if (p.filterDriveOs == FilterDriveOs::Off)
            {
                for (int i = pos; i < lastI; ++i)
                {
                    output[i] = std::tanh(output[i] * driveGain);
                    outputRBuf[i - pos] = std::tanh(outputRBuf[i - pos] * driveGain);
                }
            }
            else
            {
                auto* os = (p.filterDriveOs == FilterDriveOs::X2) ? driveOs2x_.get()
                         : (p.filterDriveOs == FilterDriveOs::X4) ? driveOs4x_.get()
                         :                                           driveOs8x_.get();

                float* chPtrL = output + pos;
                float* chPtrR = outputRBuf;
                float* const channels[2] = { chPtrL, chPtrR };
                juce::dsp::AudioBlock<float> block(channels, 2, static_cast<size_t>(driveLen));
                juce::dsp::AudioBlock<const float> constBlock(block);
                auto upBlock = os->processSamplesUp(constBlock);
                const size_t upN = upBlock.getNumSamples();
                for (size_t ch = 0; ch < 2; ++ch)
                {
                    auto* upData = upBlock.getChannelPointer(ch);
                    for (size_t i = 0; i < upN; ++i)
                        upData[i] = std::tanh(upData[i] * driveGain);
                }
                os->processSamplesDown(block);
            }
        }

        // ── Phase C: per-sample filter (algorithm dispatch per sub-block) ──
        // Stereo when the source is stereo (freeze): L through left filter,
        // R through right filter, identical coefficients (mirrored at sub-block
        // setup), separate state.
        // Mono sources (sampler / wavetable) skip the right filter entirely —
        // that's the second-most-expensive piece of the voice. We then sync the
        // right filter state to the left so a switch into a stereo source
        // (freeze going active) inherits a sensible state instead of starting
        // cold. Phase D mirrors output[i] into the right channel.
        if (p.filterEnabled)
        {
            if (freezeMode)
            {
                switch (p.filterAlgorithm)
                {
                    case FilterAlgorithm::SVF:
                        for (int i = pos; i < lastI; ++i)
                        {
                            output[i]            = filter.processSample(output[i]);
                            outputRBuf[i - pos]  = filterR.processSample(outputRBuf[i - pos]);
                        }
                        break;
                    case FilterAlgorithm::Ladder:
                        for (int i = pos; i < lastI; ++i)
                        {
                            output[i]            = filterLadder.processSample(output[i]);
                            outputRBuf[i - pos]  = filterLadderR.processSample(outputRBuf[i - pos]);
                        }
                        break;
                    case FilterAlgorithm::Warp:
                        for (int i = pos; i < lastI; ++i)
                        {
                            output[i]            = filterWarp.processSample(output[i]);
                            outputRBuf[i - pos]  = filterWarpR.processSample(outputRBuf[i - pos]);
                        }
                        break;
                }
            }
            else
            {
                switch (p.filterAlgorithm)
                {
                    case FilterAlgorithm::SVF:
                        for (int i = pos; i < lastI; ++i)
                            output[i] = filter.processSample(output[i]);
                        filterR = filter;
                        break;
                    case FilterAlgorithm::Ladder:
                        for (int i = pos; i < lastI; ++i)
                            output[i] = filterLadder.processSample(output[i]);
                        filterLadderR = filterLadder;
                        break;
                    case FilterAlgorithm::Warp:
                        for (int i = pos; i < lastI; ++i)
                            output[i] = filterWarp.processSample(output[i]);
                        filterWarpR = filterWarp;
                        break;
                }
            }
        }

        // ── Phase D: per-sample VCA + write ──
        // Stereo source (freeze): each channel carries its independently-filtered
        // signal. Mono source: R is just a copy of L — Phase C only filtered the
        // left side. Restart fade applies the same time-ramp `t` to both channels
        // (decrements the counter once per sample-pair).
        for (int i = pos; i < lastI; ++i)
        {
            const float vca = vcaScratch[i - pos];
            float L = output[i] * vca;
            float R = freezeMode ? outputRBuf[i - pos] * vca : L;
            applyRestartFadeStereo(L, R);
            lastOutputSample_  = L;
            lastOutputSampleR_ = R;

            if (outputRight != nullptr)
            {
                output[i]      = L;
                outputRight[i] = R;
            }
            else
            {
                output[i] = 0.5f * (L + R);
            }
        }

        if (goingIdle)
            return;

        pos = subBlockEnd;
    }
}
