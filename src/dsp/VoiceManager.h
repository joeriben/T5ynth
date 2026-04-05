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

    // ── MIDI handling ──
    void noteOn(int note, float velocity, bool isBind, float glideMs,
                bool lfo1TrigMode, bool lfo2TrigMode);
    void noteOff(int note);
    void allNotesOff();

    // ── Per-block rendering ──
    struct VoiceOutput {
        float lastMod1Val = 0.0f;
        float lastMod2Val = 0.0f;
        float lastModulatedCutoff = 20000.0f;
        float lastModulatedScan = 0.0f;
        int   lastTriggeredNote = -1; // for pitch modulation
        bool  hasActiveVoices = false;
    };

    /** Render all active voices into buffer (summed with 1/sqrt(N) scaling).
     *  Global LFOs are ticked externally; their per-sample values are passed in.
     *  startSample: offset into the output buffer (for sample-accurate rendering). */
    VoiceOutput renderBlock(juce::AudioBuffer<float>& buffer, const BlockParams& bp,
                            const float* lfo1Buf, const float* lfo2Buf,
                            int startSample, int numSamples);

    // ── Engine data distribution ──
    void setEngineMode(SynthVoice::EngineMode mode);
    void distributeSamplerBuffer(const SamplePlayer& master);
    void distributeWavetableFrames(const WavetableOscillator& masterOsc);

    // ── Query ──
    int getActiveVoiceCount() const;
    bool hasActiveVoices() const;

    /** Set voice limit at runtime (1=mono, 4/6/8/12/16). */
    void setVoiceLimit(int limit) { voiceLimit = juce::jlimit(1, MAX_VOICES, limit); }
    int getVoiceLimit() const { return voiceLimit; }
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

    // Pre-allocated per-voice scratch buffers
    std::array<std::vector<float>, MAX_VOICES> voiceScratch;

    // ── Voice allocation ──
    int findVoiceForNote(int note) const;
    int findFreeVoice() const;
    int stealVoice() const; // oldest-note policy

    void updateGainTarget();
    int getHeldVoiceCount() const;
    static constexpr float GAIN_RAMP_MS = 5.0f;
};
