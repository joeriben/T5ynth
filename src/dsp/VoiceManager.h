#pragma once
#include "SynthVoice.h"
#include "BlockParams.h"
#include <array>
#include <cmath>

/**
 * Polyphonic voice manager — 8 voices with oldest-note stealing.
 *
 * Signal chain: MIDI → Voice allocation → per-voice (Osc→VCA→Filter) → sum
 * Dynamic equal-power scaling: each voice at 1/sqrt(N) where N = active voices.
 * Gain transitions are ramped over ~5ms to avoid clicks.
 */
class VoiceManager
{
public:
    static constexpr int MAX_VOICES = 16;

    VoiceManager() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void reset();
    void setBlockParams(const BlockParams& bp);

    // ── MIDI handling ──
    void noteOn(int note, float velocity, bool isBind, float glideMs,
                bool lfo1TrigMode, bool lfo2TrigMode, bool lfo3TrigMode,
                int sourceId = -1, float pan = 0.0f);
    void noteOff(int note, int sourceId = -1);
    void allNotesOff();

    // ── Drone (step-hold) handling ──
    // A drone is a user-held note (e.g. mouse-hold on a sequencer step) that
    // reserves a voice for as long as the user holds. In mono the drone takes
    // over voice 0 and suppresses seq noteOns while held. In poly the drone's
    // voice is excluded from voice stealing and same-note matching by the seq
    // path, so seq triggers happen in parallel on other voices.
    void setDroneNote(int note, float velocity, bool lfo1TrigMode, bool lfo2TrigMode, bool lfo3TrigMode);
    void clearDroneNote();
    bool hasDrone() const { return droneVoiceIndex >= 0; }
    int  getDroneNote() const { return droneNote; }

    // ── Per-block rendering ──
    struct VoiceOutput {
        float lastMod1Val = 0.0f;
        float lastMod2Val = 0.0f;
        float lastModulatedCutoff = 20000.0f;
        float lastModulatedScan = 0.0f;
        float lastModulatedNoiseLevel = 0.0f;
        int   lastTriggeredNote = -1; // for pitch modulation
        bool  hasActiveVoices = false;
    };

    /** Render all active voices into buffer (summed with 1/sqrt(N) scaling).
     *  Global LFOs are ticked externally; their per-sample values are passed in.
     *  startSample: offset into the output buffer (for sample-accurate rendering). */
    VoiceOutput renderBlock(juce::AudioBuffer<float>& buffer, const BlockParams& bp,
                            const float* lfo1Buf, const float* lfo2Buf, const float* lfo3Buf,
                            int startSample, int numSamples);

    // ── Engine data distribution ──
    void setEngineMode(SynthVoice::EngineMode mode);
    void freezeActiveSamplerVoices();
    void distributeSamplerBuffer(const SamplePlayer& master);
    void distributeWavetableFrames(const WavetableOscillator& masterOsc);

    // ── Query ──
    int getActiveVoiceCount() const;
    bool hasActiveVoices() const;

    /** Set voice limit at runtime (1=mono, 4/6/8/12/16). */
    void setVoiceLimit(int limit) { voiceLimit = juce::jlimit(1, MAX_VOICES, limit); }
    int getVoiceLimit() const { return voiceLimit; }

    /** Set tuning table pointer (128 floats, MIDI note → Hz). Must be called before noteOn/renderBlock. */
    void setTuningTable(const float* table) { tuningHz_ = table; }
    const float* getTuningTable() const { return tuningHz_; }
    bool isMono() const { return voiceLimit == 1; }

    SynthVoice& getVoice(int index) { return voices[static_cast<size_t>(index)]; }

private:
    std::array<SynthVoice, MAX_VOICES> voices;

    // Monotonic counter for voice-stealing age
    uint64_t noteOnCounter = 0;

    // Gain ramping for voice count changes
    float currentGain = 1.0f;
    float targetGain = 1.0f;
    float gainRampIncr = 0.0f;
    int   gainRampSamplesLeft = 0;

    double sr = 44100.0;
    int maxBlockSize = 512;
    int voiceLimit = 8; // runtime polyphony (1=mono)
    const float* tuningHz_ = nullptr;
    const SamplePlayer* currentSamplerMaster_ = nullptr;
    const WavetableOscillator* currentWavetableMaster_ = nullptr;
    BlockParams currentBlockParams_;
    bool hasCurrentBlockParams_ = false;

    // ── Drone (step-hold) reservation ──
    int droneVoiceIndex = -1;    // -1 = no drone active
    int droneNote       = -1;    // last pitch the drone was set to

    // Pre-allocated per-voice scratch buffers
    std::array<std::vector<float>, MAX_VOICES> voiceScratch;
    std::array<float, MAX_VOICES> voicePan {};
    std::array<int, MAX_VOICES> voiceSourceId {};

    // ── Voice allocation ──
    int findVoiceForNote(int note, int sourceId = -1) const;
    int findFreeVoice() const;
    int stealVoice() const; // oldest-note policy

    void updateGainTarget();
    int getHeldVoiceCount() const;
    static constexpr float GAIN_RAMP_MS = 5.0f;
};
