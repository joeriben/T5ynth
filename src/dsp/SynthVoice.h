#pragma once
#include "WavetableOscillator.h"
#include "SamplePlayer.h"
#include "ADSREnvelope.h"
#include "LFO.h"
#include "StateVariableFilter.h"
#include "MoogLadderFilter.h"
#include "CutoffWarpFilter.h"
#include "NoiseGenerator.h"
#include "BlockParams.h"
#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <vector>

/**
 * Single synthesizer voice — owns all per-voice DSP state:
 * oscillator, sample player, envelopes, per-voice LFOs, and filter.
 *
 * Signal chain: Osc/Noise → Drive (tanh, optional) → Filter (SVF) → VCA → output
 */
class SynthVoice
{
public:
    SynthVoice() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void reset();

    // ── Note lifecycle ──
    void noteOn(int note, float velocity, bool legato);
    void noteOff();
    void glideToNote(int note, float glideMs);
    /** Smooth an immediate same-voice restart by fading from the last rendered sample. */
    void beginRestartFade();

    // ── Per-sample rendering ──
    struct RenderResult {
        float sample;        // mono output (post-VCA, post-filter)
        float mod1EnvVal;    // last mod1 envelope value (for block-rate targets)
        float mod2EnvVal;    // last mod2 envelope value
        float modulatedCutoff = 20000.0f;
        float modulatedScan = 0.0f;
        float modulatedNoiseLevel = 0.0f;
    };

    /** Configure envelopes from block params. Call once per block before renderSample loop. */
    void configureForBlock(const BlockParams& p);

    RenderResult renderSample(const BlockParams& p, float globalLfo1Val, float globalLfo2Val, float globalLfo3Val);

    /** Block-based rendering with sub-block filter coefficient updates.
     *  Writes numSamples mono samples into output. */
    void renderBlock(float* output, const BlockParams& p,
                     const float* lfo1Buf, const float* lfo2Buf, const float* lfo3Buf, int numSamples);

    static constexpr int SUB_BLOCK_SIZE = 32;

    // Mod value accessors (for VoiceManager to capture after renderBlock)
    float getLastMod1Val() const { return lastMod1Val_; }
    float getLastMod2Val() const { return lastMod2Val_; }
    float getLastModulatedCutoff() const { return lastModulatedCutoff_; }
    float getLastModulatedScan() const { return lastModulatedScan_; }
    float getLastModulatedNoiseLevel() const { return lastModulatedNoiseLevel_; }

    // ── State queries ──
    bool isActive() const { return active; }
    bool isReleasing() const { return active && !noteHeld; }
    int  getCurrentNote() const { return currentNote; }
    float getAmpEnvLevel() const { return ampEnv.isIdle() ? 0.0f : lastAmpEnvLevel; }

    // ── Tuning ──
    void setTuningTable(const float* table) { tuningHz_ = table; }

    // ── Engine mode ──
    enum class EngineMode { Sampler, Wavetable };
    void setEngineMode(EngineMode mode) { engineMode = mode; }
    EngineMode getEngineMode() const { return engineMode; }

    // ── Access to sub-components ──
    WavetableOscillator& getOsc() { return osc; }
    SamplePlayer& getSampler() { return sampler; }
    ADSREnvelope& getAmpEnvelope() { return ampEnv; }
    ADSREnvelope& getModEnvelope1() { return modEnv1; }
    ADSREnvelope& getModEnvelope2() { return modEnv2; }
    T5ynthFilter& getFilter() { return filter; }
    LFO& getPerVoiceLfo1() { return perVoiceLfo1; }
    LFO& getPerVoiceLfo2() { return perVoiceLfo2; }
    LFO& getPerVoiceLfo3() { return perVoiceLfo3; }

    // Voice age for stealing (monotonic counter set by VoiceManager)
    uint64_t noteOnTimestamp = 0;

private:
    WavetableOscillator osc;
    SamplePlayer sampler;
    ADSREnvelope ampEnv;
    ADSREnvelope modEnv1;
    ADSREnvelope modEnv2;
    LFO perVoiceLfo1; // used when LFO mode == Trigger
    LFO perVoiceLfo2;
    LFO perVoiceLfo3;
    T5ynthFilter       filter;       // linear TPT SVF (low-CPU default)
    MoogLadderFilter   filterLadder; // Huovilainen nonlinear ladder
    CutoffWarpFilter   filterWarp;   // Surge-XT-style ZDF ladder + style
    // Drive oversamplers (polyphase IIR half-band). Three instances, prepared in
    // prepare(); renderBlock() picks the one matching bp.filterDriveOs.
    std::unique_ptr<juce::dsp::Oversampling<float>> driveOs2x_;  // numStages=1, 2x
    std::unique_ptr<juce::dsp::Oversampling<float>> driveOs4x_;  // numStages=2, 4x
    std::unique_ptr<juce::dsp::Oversampling<float>> driveOs8x_;  // numStages=3, 8x
    NoiseGenerator noise;

    EngineMode engineMode = EngineMode::Sampler;

    int currentNote = -1;
    int octaveShift_ = 0;
    float currentVelocity = 0.0f;
    bool active = false;
    bool noteHeld = false;
    float lastAmpEnvLevel = 0.0f;
    float lastOutputSample_ = 0.0f;
    float baseFrequency = 440.0f;
    const float* tuningHz_ = nullptr;  // set by VoiceManager per-block

    /** Get frequency for MIDI note using tuning table (falls back to 12-TET). */
    float tunedHz(int midiNote) const
    {
        int n = std::max(0, std::min(127, midiNote));
        return (tuningHz_ != nullptr) ? tuningHz_[n]
            : static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(n));
    }

    double sr = 44100.0;

    // Velocity sensitivity (updated per block from BlockParams)
    float ampVelSens_ = 1.0f;
    float mod1VelSens_ = 1.0f;
    float mod2VelSens_ = 1.0f;
    float ampAttackBaseMs_ = 0.0f;
    float ampDecayBaseMs_ = 0.0f;
    float ampReleaseBaseMs_ = 0.0f;
    int ampAttackVelMode_ = EnvVelTimeMode::Off;
    int ampDecayVelMode_ = EnvVelTimeMode::Off;
    int ampReleaseVelMode_ = EnvVelTimeMode::Off;
    float mod1AttackBaseMs_ = 0.0f;
    float mod1DecayBaseMs_ = 0.0f;
    float mod1ReleaseBaseMs_ = 0.0f;
    int mod1AttackVelMode_ = EnvVelTimeMode::Off;
    int mod1DecayVelMode_ = EnvVelTimeMode::Off;
    int mod1ReleaseVelMode_ = EnvVelTimeMode::Off;
    float mod2AttackBaseMs_ = 0.0f;
    float mod2DecayBaseMs_ = 0.0f;
    float mod2ReleaseBaseMs_ = 0.0f;
    int mod2AttackVelMode_ = EnvVelTimeMode::Off;
    int mod2DecayVelMode_ = EnvVelTimeMode::Off;
    int mod2ReleaseVelMode_ = EnvVelTimeMode::Off;

    // Cached mod values from last renderBlock (for VoiceManager capture)
    float lastMod1Val_ = 0.0f;
    float lastMod2Val_ = 0.0f;
    float lastModulatedCutoff_ = 20000.0f;
    float lastModulatedScan_ = 0.0f;
    float lastModulatedNoiseLevel_ = 0.0f;

    // Pre-rendered sampler block (pitch-shifted via Signalsmith Stretch)
    int maxBlockSize_ = 512;
    std::vector<float> samplerBlockBuf_;

    struct PreStretchNormState
    {
        float ampAttack = -1.0f;
        float ampDecay = -1.0f;
        float ampSustain = -1.0f;
        float ampRelease = -1.0f;
        float ampAmount = -1.0f;
        float ampVelSens = -1.0f;
        bool ampLoop = false;
        int ampAttackCurve = -1;
        int ampDecayCurve = -1;
        int ampReleaseCurve = -1;
        int ampAttackVelMode = EnvVelTimeMode::Off;
        int ampDecayVelMode = EnvVelTimeMode::Off;
        int ampReleaseVelMode = EnvVelTimeMode::Off;

        int mod1Target = EnvTarget::None;
        float mod1Attack = -1.0f;
        float mod1Decay = -1.0f;
        float mod1Sustain = -1.0f;
        float mod1Release = -1.0f;
        float mod1Amount = -1.0f;
        float mod1VelSens = -1.0f;
        bool mod1Loop = false;
        int mod1AttackCurve = -1;
        int mod1DecayCurve = -1;
        int mod1ReleaseCurve = -1;
        int mod1AttackVelMode = EnvVelTimeMode::Off;
        int mod1DecayVelMode = EnvVelTimeMode::Off;
        int mod1ReleaseVelMode = EnvVelTimeMode::Off;

        int mod2Target = EnvTarget::None;
        float mod2Attack = -1.0f;
        float mod2Decay = -1.0f;
        float mod2Sustain = -1.0f;
        float mod2Release = -1.0f;
        float mod2Amount = -1.0f;
        float mod2VelSens = -1.0f;
        bool mod2Loop = false;
        int mod2AttackCurve = -1;
        int mod2DecayCurve = -1;
        int mod2ReleaseCurve = -1;
        int mod2AttackVelMode = EnvVelTimeMode::Off;
        int mod2DecayVelMode = EnvVelTimeMode::Off;
        int mod2ReleaseVelMode = EnvVelTimeMode::Off;

        float velocity = -1.0f;
        float startPos = -1.0f;
        float loopStart = -1.0f;
        float loopEnd = -1.0f;
        float startPosOffset = -999.0f;
        float crossfadeMs = -1.0f;
        int loopMode = -1;
        bool normalizeOn = false;
    };

    void updateSamplerPreStretchNorm(const BlockParams& p);
    bool preStretchNormStateMatches(const BlockParams& p) const;
    void applyVelocityTimedEnvelopeTimes();
    float applyRestartFade(float sample);

    float restartFadeTailSample_ = 0.0f;
    int restartFadeSamplesLeft_ = 0;
    int restartFadeTotalSamples_ = 1;
    static constexpr float RESTART_FADE_MS = 3.0f;

    PreStretchNormState preStretchNormState_;
    float samplerPreStretchNormGain_ = 1.0f;
    bool samplerPreStretchNormDirty_ = true;

};
