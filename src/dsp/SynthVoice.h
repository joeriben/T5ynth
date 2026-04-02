#pragma once
#include "WavetableOscillator.h"
#include "SamplePlayer.h"
#include "ADSREnvelope.h"
#include "LFO.h"
#include "StateVariableFilter.h"
#include "BlockParams.h"

/**
 * Single synthesizer voice — owns all per-voice DSP state:
 * oscillator, sample player, envelopes, per-voice LFOs, and filter.
 *
 * Signal chain: Osc → VCA (ampEnv * modulation) → Filter (SVF) → output
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

    // ── Per-sample rendering ──
    struct RenderResult {
        float sample;        // mono output (post-VCA, post-filter)
        float mod1EnvVal;    // last mod1 envelope value (for block-rate targets)
        float mod2EnvVal;    // last mod2 envelope value
        float modulatedCutoff = 20000.0f;
        float modulatedScan = 0.0f;
    };

    /** Configure envelopes from block params. Call once per block before renderSample loop. */
    void configureForBlock(const BlockParams& p);

    RenderResult renderSample(const BlockParams& p, float globalLfo1Val, float globalLfo2Val);

    /** Block-based rendering with sub-block filter coefficient updates.
     *  Writes numSamples mono samples into output. */
    void renderBlock(float* output, const BlockParams& p,
                     const float* lfo1Buf, const float* lfo2Buf, int numSamples);

    static constexpr int SUB_BLOCK_SIZE = 32;

    // Mod value accessors (for VoiceManager to capture after renderBlock)
    float getLastMod1Val() const { return lastMod1Val_; }
    float getLastMod2Val() const { return lastMod2Val_; }
    float getLastModulatedCutoff() const { return lastModulatedCutoff_; }
    float getLastModulatedScan() const { return lastModulatedScan_; }

    // ── State queries ──
    bool isActive() const { return active; }
    bool isReleasing() const { return active && !noteHeld; }
    int  getCurrentNote() const { return currentNote; }
    float getAmpEnvLevel() const { return ampEnv.isIdle() ? 0.0f : lastAmpEnvLevel; }

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
    T5ynthFilter filter;

    EngineMode engineMode = EngineMode::Sampler;

    int currentNote = -1;
    float currentVelocity = 0.0f;
    bool active = false;
    bool noteHeld = false;
    float lastAmpEnvLevel = 0.0f;
    float baseFrequency = 440.0f;

    double sr = 44100.0;

    // Cached mod values from last renderBlock (for VoiceManager capture)
    float lastMod1Val_ = 0.0f;
    float lastMod2Val_ = 0.0f;
    float lastModulatedCutoff_ = 20000.0f;
    float lastModulatedScan_ = 0.0f;
};
