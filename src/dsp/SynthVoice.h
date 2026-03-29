#pragma once
#include "WavetableOscillator.h"
#include "AudioLooper.h"
#include "ADSREnvelope.h"
#include "LFO.h"
#include "StateVariableFilter.h"
#include "BlockParams.h"

/**
 * Single synthesizer voice — owns all per-voice DSP state:
 * oscillator, looper, envelopes, per-voice LFOs, and filter.
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
    };

    /** Configure envelopes from block params. Call once per block before renderSample loop. */
    void configureForBlock(const BlockParams& p);

    RenderResult renderSample(const BlockParams& p, float globalLfo1Val, float globalLfo2Val);

    /** Apply block-rate filter modulation and process filter for this voice.
     *  Called once per block after all samples have been rendered into voiceBuffer. */
    void processFilter(const BlockParams& p, float lastMod1Val, float lastMod2Val,
                       float lastLfo1Val, float lastLfo2Val);

    // ── State queries ──
    bool isActive() const { return active; }
    bool isReleasing() const { return active && !noteHeld; }
    int  getCurrentNote() const { return currentNote; }
    float getAmpEnvLevel() const { return ampEnv.isIdle() ? 0.0f : lastAmpEnvLevel; }

    // ── Engine mode ──
    enum class EngineMode { Looper, Wavetable };
    void setEngineMode(EngineMode mode) { engineMode = mode; }
    EngineMode getEngineMode() const { return engineMode; }

    // ── Access to sub-components ──
    WavetableOscillator& getOsc() { return osc; }
    AudioLooper& getLooper() { return looper; }
    ADSREnvelope& getAmpEnvelope() { return ampEnv; }
    ADSREnvelope& getModEnvelope1() { return modEnv1; }
    ADSREnvelope& getModEnvelope2() { return modEnv2; }
    T5ynthFilter& getFilter() { return filter; }

    // Voice age for stealing (monotonic counter set by VoiceManager)
    uint64_t noteOnTimestamp = 0;

private:
    WavetableOscillator osc;
    AudioLooper looper;
    ADSREnvelope ampEnv;
    ADSREnvelope modEnv1;
    ADSREnvelope modEnv2;
    LFO perVoiceLfo1; // used when LFO mode == Trigger
    LFO perVoiceLfo2;
    T5ynthFilter filter;

    EngineMode engineMode = EngineMode::Looper;

    int currentNote = -1;
    float currentVelocity = 0.0f;
    bool active = false;
    bool noteHeld = false;
    float lastAmpEnvLevel = 0.0f;

    double sr = 44100.0;
};
