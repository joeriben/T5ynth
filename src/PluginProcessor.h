#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <array>
#include <mutex>
#include <memory>
#include <limits>
#include <thread>
#include "dsp/VoiceManager.h"
#include "dsp/ParamCache.h"
#include "dsp/LFO.h"
#include "dsp/DriftLFO.h"
#include "dsp/DelayLine.h"
#include "dsp/ConvolutionReverb.h"
#include "dsp/AlgorithmicReverb.h"
#include "dsp/Limiter.h"
#include "sequencer/StepSequencer.h"
#include "sequencer/GenerativeSequencer.h"
#include "sequencer/Arpeggiator.h"
#include "inference/PipeInference.h"

class T5ynthProcessor : public juce::AudioProcessor,
                        private juce::AsyncUpdater
{
public:
    T5ynthProcessor();
    ~T5ynthProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "T5ynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() { return parameters; }

    // Engine mode (read from APVTS "engine_mode": 0=Sampler, 1=Wavetable, 2=Granular)
    bool isWavetableMode() const;
    bool isFreezeMode() const;
    bool isSamplerMode() const;

    /** GUI-thread entry point for the StatusBar "Panic" button. Sets a flag
     *  that the audio thread consumes at the start of the next processBlock,
     *  matching the CC120/123 (All Notes/Sound Off) behaviour. */
    void requestMidiPanic() { midiPanicRequested.store(true, std::memory_order_release); }

    /** Builds the Duration parameter's NormalisableRange (skew + whole-second
        snapping) for a given maximum in seconds. Shared by createParameterLayout
        (registers the parameter at the global 120s max) and PromptPanel, which
        re-scopes the slider per model: 11s for SAO/AudioLDM2, 120s for SA3. */
    static juce::NormalisableRange<float> makeDurationRange(float maxSeconds);

    // Load generated audio into the engine
    void loadGeneratedAudio(const juce::AudioBuffer<float>& buffer, double sampleRate);
    /** Reload already-processed audio into sampler (no Rumble/HF/Normalize). */
    void reloadProcessedAudio(const juce::AudioBuffer<float>& processed);

    // Inference cache: raw inference audio only, no duplicate prompt/model metadata.
    struct InferenceCacheEntry
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
    };
    void setInferenceCacheCapacity(int capacity);
    void clearInferenceCache();
    bool addInferenceCacheEntry(const juce::AudioBuffer<float>& buffer, double sampleRate);
    bool playNextInferenceCacheEntry();
    bool isInferenceCacheActive() const { return inferenceCacheCapacity > 0; }
    bool isInferenceCacheFull() const { return inferenceCacheCapacity > 0
                                             && static_cast<int>(inferenceCacheEntries.size()) >= inferenceCacheCapacity; }
    int getInferenceCacheCapacity() const { return inferenceCacheCapacity; }
    int getInferenceCacheFillCount() const { return static_cast<int>(inferenceCacheEntries.size()); }
    const std::vector<InferenceCacheEntry>& getInferenceCacheEntries() const { return inferenceCacheEntries; }

    // Inference (Python subprocess)
    bool isInferenceReady() const { return pipeInference->isReady(); }
    PipeInference& getPipeInference() { return *pipeInference; }
    std::shared_ptr<PipeInference> getPipeInferencePtr() { return pipeInference; }
    bool launchPipeInference(const juce::File& backendDir);
    bool isPipeInferenceReady() const { return pipeInference->isReady(); }

    // Last device/model used for generation (for preset tagging)
    void setLastDevice(const juce::String& dev) { lastDevice = dev; }
    const juce::String& getLastDevice() const { return lastDevice; }
    void setLastModel(const juce::String& m) { lastModel = m; }
    const juce::String& getLastModel() const { return lastModel; }

    // Preset metadata (GUI-only state that must survive save/load)
    void setLastPrompts(const juce::String& a, const juce::String& b) { lastPromptA = a; lastPromptB = b; }
    const juce::String& getLastPromptA() const { return lastPromptA; }
    const juce::String& getLastPromptB() const { return lastPromptB; }

    // Durable HUMAN prompts — the single source of truth for "what the user
    // authored", independent of the live editors. The Re-Prompt loop rewrites the
    // editors in place (and so lastPromptA/B, which mirror them), and a missed
    // deactivation-restore or a buffer reload can leave a machine rewrite sitting
    // in an editor; deriving the saved prompt from the editors there is what kept
    // baking machine text into presets. This store is written ONLY by human
    // authorship — onTextChange (per pole) and preset load (both poles) — never by
    // the loop, so PresetFormat::saveToFile can always persist the human prompt
    // regardless of editor state. Per-pole setters: an A edit must not recapture a
    // rewritten B (and vice-versa).
    void setHumanPromptA(const juce::String& a) { humanPromptA = a; humanPromptsValid = true; }
    void setHumanPromptB(const juce::String& b) { humanPromptB = b; humanPromptsValid = true; }
    void setHumanPrompts(const juce::String& a, const juce::String& b)
        { humanPromptA = a; humanPromptB = b; humanPromptsValid = true; }
    void clearHumanPrompts() { humanPromptA.clear(); humanPromptB.clear(); humanPromptsValid = false; }
    bool hasHumanPrompts() const { return humanPromptsValid; }
    const juce::String& getHumanPromptA() const { return humanPromptA; }
    const juce::String& getHumanPromptB() const { return humanPromptB; }
    void setLastPresetName(const juce::String& name) { lastPresetName = name; }
    const juce::String& getLastPresetName() const { return lastPresetName; }
    void setLastTags(const juce::StringArray& tags) { lastTags = tags; }
    const juce::StringArray& getLastTags() const { return lastTags; }
    void setLastGenerationTimeMs(float ms) { lastGenerationTimeMs = ms; }
    float getLastGenerationTimeMs() const { return lastGenerationTimeMs; }

    void setLastSeed(int s) { lastSeed = s; }
    int getLastSeed() const { return lastSeed; }

    // Whether the user's seed UI is in "auto" / random mode. PromptPanel keeps
    // this in sync with its `randomSeedToggle` (Adv) and the "auto" button of
    // the Easy three-state switchbox. Preset save persists this bool so the
    // Easy-mode "auto" selection survives a round-trip — the APVTS PID::genSeed
    // param is not driven by the UI, so we can't infer the auto state from it.
    void setLastRandomSeed(bool r) { lastRandomSeed = r; }
    bool getLastRandomSeed() const { return lastRandomSeed; }

    // Research-mode injection state (GUI-only, persisted in .t5p with defaults
    // for old files: linear / 0.75 / 4.0 / 16.0 — matches the panel defaults).
    void setLastInjection(const juce::String& mode, float lateMixAmount,
                          float splitStart, float splitEnd)
    {
        lastInjectionMode = mode;
        lastLateMixAmount = lateMixAmount;
        lastSplitStart = splitStart;
        lastSplitEnd = splitEnd;
    }
    const juce::String& getLastInjectionMode() const { return lastInjectionMode; }
    float getLastLateMixAmount() const { return lastLateMixAmount; }
    float getLastSplitStart() const { return lastSplitStart; }
    float getLastSplitEnd() const { return lastSplitEnd; }

    // Semantic axes state (GUI-only, 3 slots: dropdownId + value)
    struct AxisSlotState { int dropdownId = 1; float value = 0.0f; };
    void setLastAxes(const std::array<AxisSlotState, 3>& a) { lastAxes = a; }
    const std::array<AxisSlotState, 3>& getLastAxes() const { return lastAxes; }

    void setLastEmbeddings(const std::vector<float>& a, const std::vector<float>& b) { lastEmbeddingA = a; lastEmbeddingB = b; }
    const std::vector<float>& getLastEmbeddingA() const { return lastEmbeddingA; }
    const std::vector<float>& getLastEmbeddingB() const { return lastEmbeddingB; }

    /** Get the processed audio buffer (with HF boost if enabled). */
    const juce::AudioBuffer<float>& getGeneratedAudio() const { return generatedAudioFull; }
    /** Get the raw VAE output (unmodified, for re-apply on HF toggle). */
    const juce::AudioBuffer<float>& getGeneratedAudioRaw() const { return generatedAudioRaw; }
    double getGeneratedSampleRate() const { return generatedSampleRate; }

    // Sequencer
    T5ynthStepSequencer& getStepSequencer() { return stepSequencer; }
    T5ynthGenerativeSequencer& getGenerativeSequencer() { return generativeSequencer; }
    T5ynthArpeggiator& getArpeggiator() { return arpeggiator; }
    bool assignSequencerOneShotFromCurrentRegion(int step, int slot);
    bool assignSequencerOneShotFromRegion(int step, int slot, float regionStart, float regionEnd);
    bool hasSequencerOneShotSample(int step, int slot) const;
    void clearSequencerOneShotSample(int step, int slot);
    void clearSequencerOneShotSamples();
    /** Copy a captured one-shot (audio + mode) from one slot to another.
     *  Shares the immutable sample buffer by pointer — no deep audio copy.
     *  No-op if the source is empty or src == dst. Message-thread only. */
    bool copySequencerOneShotSample(int srcStep, int srcSlot, int dstStep, int dstSlot);
    struct SequencerOneShotExport
    {
        int step = 0;
        int slot = 0;
        T5ynthStepSequencer::OneShotMode mode = T5ynthStepSequencer::OneShotMode::Normal;
        juce::String label;
        double sampleRate = 44100.0;
        juce::AudioBuffer<float> audio;
    };
    std::vector<SequencerOneShotExport> exportSequencerOneShotSamples() const;
    void importSequencerOneShotSamples(const std::vector<SequencerOneShotExport>& slots);
    bool canUseStepHoldPreview() const;
    void beginStepHoldPreview(int midiNote, float velocity = 0.8f);
    void updateStepHoldPreview(int midiNote, float velocity = 0.8f);
    void endStepHoldPreview();
    void beginComputerKeyboardNote(int midiNote, float velocity = 0.8f);
    void endComputerKeyboardNote(int midiNote);
    void allComputerKeyboardNotesOff();

    // Waveform display data
    bool hasNewWaveform() const { return newWaveformReady.load(std::memory_order_acquire); }
    void clearNewWaveformFlag() { newWaveformReady.store(false, std::memory_order_release); }
    const juce::AudioBuffer<float>& getWaveformSnapshot() const { return waveformSnapshot; }

    // JSON preset import/export (compatible with Vue reference format)
    juce::String exportJsonPreset() const;
    bool importJsonPreset(const juce::String& json);

    // Sampler/oscillator access for preset import and UI queries
    SamplePlayer& getSampler() { return masterSampler; }
    WavetableOscillator& getMasterOsc() { return masterOsc; }
    const WavetableOscillator& getMasterOscConst() const { return masterOsc; }

    /** Re-extract wavetable frames using current bracket region. */
    void reextractWavetable();

    // ── BPM-sync resolution ──
    // `hostBpmLastSeen` freezes the last live host BPM so paused-DAW behavior
    // keeps the previously-known tempo (rather than collapsing to default).
    // `hostPlayingNow` distinguishes a live transport from a frozen value.
    // Resolution priority: live host > running in-app sequencer >
    // frozen host > seqBpm fallback.
    bool seqRunningNow() const;
    float resolveSyncBpm() const;

private:
    struct WtTraversalMapping
    {
        float extractStart = 0.0f;
        float extractEnd = 1.0f;
        float startInExtract = 0.0f;
        float loopStartInExtract = 0.0f;
        float loopEndInExtract = 1.0f;
        int regionSamples = 0;
    };

    juce::AudioProcessorValueTreeState parameters;
    ParamCache paramCache;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    WtTraversalMapping makeWtTraversalMapping(int totalSamples) const;
    WtTraversalMapping makeWtTraversalMapping(int totalSamples, float p1, float p2, float p3) const;
    void syncWavetableTraversal(double bufferSampleRate, int totalSamples);
    void updateDriftState(int numSamples, float syncBpm);
    void syncSamplerSettingsFromParametersLocked();
    bool serviceSamplerReprepare();
    void samplerReprepareThreadMain();
    void storeSamplerReprepareSource(const juce::AudioBuffer<float>& buffer,
                                     double sampleRate,
                                     float normalizeStartFrac = 0.0f,
                                     float normalizeEndFrac = 1.0f);

    // Engine (mode is stored in APVTS "engine_mode", no separate member)

    // DSP — polyphonic voice pool
    VoiceManager voiceManager;
    float tuningTable[128] {};  // MIDI note → Hz, rebuilt per-block

    // Master data holders (own the audio/frame data, voices share from these)
    WavetableOscillator masterOsc;
    SamplePlayer masterSampler;
    FreezeTextureEngine masterFreeze;
    std::thread samplerReprepareThread;
    std::atomic<bool> samplerReprepareThreadShouldExit { false };
    std::atomic<bool> samplerReprepareWorkRequested { false };
    std::mutex samplerReprepareSourceMutex;
    juce::AudioBuffer<float> samplerReprepareSourceBuffer;
    double samplerReprepareSourceRate = 44100.0;
    juce::uint64 samplerReprepareSourceVersion = 0;
    float samplerReprepareNormalizeStartFrac = 0.0f;
    float samplerReprepareNormalizeEndFrac = 1.0f;
    bool samplerReprepareSourceValid = false;

    // DSP — global (shared across voices, post-sum)
    LFO lfo1;
    LFO lfo2;
    LFO lfo3;
    DriftLFO driftLfo;
    T5ynthFilter postFilter;
    T5ynthDelayLine delay;
    ConvolutionReverb reverb;
    AlgorithmicReverb algoReverb;
    T5ynthLimiter limiter;

    // Sequencer
    T5ynthStepSequencer stepSequencer;
    T5ynthGenerativeSequencer generativeSequencer;
    T5ynthArpeggiator arpeggiator;
    bool genModeActiveInAudio = false;  // tracks which engine is currently running
    int lastGenSteps = -1, lastGenPulses = -1, lastGenRotation = -1;
    float lastGenMutation = -1.0f;
    std::array<float, 4> genStrandPan {};

    struct SequencerOneShotSample
    {
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
        juce::String label;
    };
    using SequencerOneShotSamplePtr = std::shared_ptr<const SequencerOneShotSample>;
    static constexpr int kMaxSequencerOneShotVoices =
        T5ynthStepSequencer::MAX_STEPS * T5ynthStepSequencer::ONE_SHOT_SLOTS;
    std::array<std::array<SequencerOneShotSamplePtr,
                          T5ynthStepSequencer::ONE_SHOT_SLOTS>,
               T5ynthStepSequencer::MAX_STEPS> sequencerOneShotSamples;
    struct PendingSequencerOneShot
    {
        SequencerOneShotSamplePtr sample;
        float gain = 1.0f;
        int sampleOffset = 0;
    };
    struct ActiveSequencerOneShot
    {
        SequencerOneShotSamplePtr sample;
        double position = 0.0;
        double increment = 1.0;
        float gain = 1.0f;
        int startOffset = 0;
        bool active = false;
        uint64_t age = 0;
    };
    std::array<PendingSequencerOneShot, kMaxSequencerOneShotVoices> pendingSequencerOneShots;
    std::array<ActiveSequencerOneShot, kMaxSequencerOneShotVoices> activeSequencerOneShots;
    int pendingSequencerOneShotCount = 0;
    uint64_t sequencerOneShotAgeCounter = 0;
    void queueSequencerOneShotTrigger(const T5ynthStepSequencer::OneShotTrigger& trigger);
    void renderSequencerOneShots(juce::AudioBuffer<float>& buffer);
    void stopSequencerOneShots();
    bool hasActiveSequencerOneShots() const;

    // ── One-shot sample retirement (deferred off-audio-thread release) ──────
    // CLAUDE.md JUCE-safety #4: a shared_ptr refcount reaching 0 frees the held
    // juce::AudioBuffer, and that free must never run on the audio thread. The
    // array store side (assign/clear/copy/import) already releases on the message
    // thread via std::atomic_store. But the audio thread also drops one-shot refs
    // whenever a voice ends, is stolen/reused, or is stopped; if the user cleared
    // or overwrote that slot meanwhile, the still-sounding voice can hold the LAST
    // ref, so a plain reset()/assignment there would free on the audio thread.
    //
    // Instead the audio thread MOVES the ref into this lock-free SPSC ring — a
    // move never touches the refcount, allocates, or frees — and the always-on
    // samplerReprepare worker thread drains it, performing the real release off
    // the audio thread. Same retire-bin idea as FreezeTextureEngine's
    // retiredPublished_/retiredMorphFrom_, generalised to a ring because a voice
    // ending is an audio→worker hand-off rather than a message-thread-local park.
    //
    // Sole producer: the audio thread (writes head). Sole consumer: the worker
    // thread (writes tail). Capacity comfortably exceeds the 192 maximum live
    // voices; on the (practically unreachable) full ring the producer leaves the
    // ref in place — still held, never freed — to be retired on a later pass.
    static constexpr int kOneShotRetireCapacity = 512;  // power of two, > kMaxSequencerOneShotVoices (192)
    static constexpr int kOneShotRetireMask = kOneShotRetireCapacity - 1;
    std::array<SequencerOneShotSamplePtr, kOneShotRetireCapacity> oneShotRetireBin;
    std::atomic<int> oneShotRetireHead { 0 };  // audio thread (producer) only
    std::atomic<int> oneShotRetireTail { 0 };  // worker thread (consumer) only
    bool retireOneShotSampleToBin(SequencerOneShotSamplePtr& ptr) noexcept;  // audio thread
    void drainSequencerOneShotRetireBin() noexcept;                          // worker thread

    // MPE / pitch-bend-range state (audio thread only, no atomics needed)
    // RPN 0x0000 (Pitch Bend Sensitivity): CC101=0, CC100=0, then CC6=semitones.
    // RPN 0x7F7F (RPN Null): deselects the active RPN — subsequent CC6 is ignored.
    int   midiRpnMsb_ = 0x7F;
    int   midiRpnLsb_ = 0x7F;
    float masterPitchBendRangeSemitones_ = 2.0f;   // ch1 global bend range (standard ±2)
    float notePitchBendRangeSemitones_   = 48.0f;  // ch2-16 per-note range (MPE default ±48)

    // Edge-detection for arp-toggle note-off cleanup. When arp transitions
    // false→true while a sequencer is running, the seq's currently-sounding
    // note must be flushed before arp's filter starts swallowing noteOffs.
    bool arpWasEnabled = false;

    // Inference (Python subprocess)
    std::shared_ptr<PipeInference> pipeInference = std::make_shared<PipeInference>();
    juce::String lastDevice;
    juce::String lastModel;

    // Preset metadata (stored here so preset save can access them)
    juce::String lastPresetName;
    juce::StringArray lastTags;
    juce::String lastPromptA, lastPromptB;
    juce::String humanPromptA, humanPromptB;   // durable human-authored prompts (never the loop's rewrites)
    bool humanPromptsValid = false;
    float lastGenerationTimeMs = 0.0f;
    int lastSeed = 123456789;
    bool lastRandomSeed = false;
    juce::String lastInjectionMode { "linear" };
    float lastLateMixAmount = 0.75f;
    float lastSplitStart = 4.0f;
    float lastSplitEnd = 16.0f;
    std::array<AxisSlotState, 3> lastAxes;
    std::vector<float> lastEmbeddingA, lastEmbeddingB;
    juce::AudioBuffer<float> generatedAudioFull;  // boosted buffer for engines + display
    juce::AudioBuffer<float> generatedAudioRaw;   // raw VAE output (for re-apply on toggle)
    double generatedSampleRate = 44100.0;
    int inferenceCacheCapacity = 0;
    int inferenceCachePlaybackIndex = 0;
    std::vector<InferenceCacheEntry> inferenceCacheEntries;

    /** Two-band high shelf to compensate VAE decoder HF rolloff. */
    static void applyHfBoost(juce::AudioBuffer<float>& buffer, double sampleRate);

    /** Rumble filter — 2nd-order Butterworth HP at 25 Hz, removes DC/sub-rumble from VAE output. */
    static void applyRumbleFilter(juce::AudioBuffer<float>& buffer, double sampleRate);

    // Last triggered note (for pitch modulation in block-rate section)
    int lastTriggeredNote = -1;

    // Pre-allocated LFO buffers (avoid heap alloc in processBlock)
    std::vector<float> lfo1Buffer, lfo2Buffer, lfo3Buffer;

    // Persisted LFO output for ghost display (updated every block, even during idle)
    float lastLfo1Val_ = 0.0f, lastLfo2Val_ = 0.0f, lastLfo3Val_ = 0.0f;

    // Waveform display
    juce::AudioBuffer<float> waveformSnapshot;
    std::atomic<bool> newWaveformReady { false };

    // Track loaded reverb IR / seq preset to avoid reloading every block.
    // lastSeqPreset is atomic: the audio thread applies preset changes, while
    // importJsonPreset (message thread) syncs it after restoring a custom step
    // pattern so a stale mismatch can't reload the canned preset over it.
    int lastReverbIr = -1;
    std::atomic<int> lastSeqPreset { -1 };
    // Set by setStateInformation, consumed once by the next processBlock preset
    // apply: distinguishes a DAW state-restore (preserve the saved seqSteps)
    // from a fresh launch or runtime dropdown change (adopt the preset's length).
    std::atomic<bool> seqStateRestored { false };

    // Temporary preview note from step-grid mouse-hold editing.
    bool stepHoldPreviewActive = false;
    int stepHoldPreviewNote = -1;

    // Idle detection (audio thread only — not atomic)
    int silentBlockCount = 0;
    int tailBlocks = 860;  // recalculated in prepareToPlay (~10s of reverb tail)

    // Tracks the previous-block transport state (PID::seqRunning) so the
    // processBlock can phase-align sync-mode LFO/Drift to beat 1 on a
    // stop→start transition. PID::genSeqRunning is a mode toggle (STEP vs
    // GEN engine), not transport — both engines' .start() is gated on
    // seqRunning alone. Audio thread only — not atomic.
    bool lastSeqRunning = false;

    // Per-modulator sync phase-alignment ("arm to the downbeat"). When a
    // Drift/LFO is switched Off→Sync while the STEP sequencer leads the clock,
    // snapping its phase immediately lands the cycle between beats; instead we
    // ARM it (hold it silent at phase 0) and release it on the next bar
    // downbeat so its first cycle starts on the "1". Index 0..2 = lfo1..3 /
    // drift 0..2. Audio thread only — not atomic.
    int  lastLfoClockMode[3]   { 0, 0, 0 };   // ClockMode::Off
    int  lastDriftClockMode[3] { 0, 0, 0 };
    bool lfoSyncArmed[3]       { false, false, false };
    bool driftSyncArmed[3]     { false, false, false };

    // Pre-allocated buffer for parallel reverb send (avoids heap alloc in processBlock)
    juce::AudioBuffer<float> reverbSendBuffer;


public:
    // Audio idle state (audio thread writes, GUI reads for timer gating)
    std::atomic<bool> audioIdle { false };

    // Host-transport state — written from processBlock, read from
    // resolveSyncBpm() (which itself runs on the audio thread).
    std::atomic<float> hostBpmLastSeen { 0.0f };
    std::atomic<bool>  hostPlayingNow  { false };

    // MIDI panic flag — set by requestMidiPanic() on the GUI thread,
    // consumed/cleared in processBlock on the audio thread.
    std::atomic<bool>  midiPanicRequested { false };

    // MIDI monitor (audio thread writes, GUI reads)
    std::atomic<int> lastMidiNote { -1 };
    std::atomic<int> lastMidiVelocity { 0 };
    std::atomic<bool> lastMidiNoteOn { false };

    // Modulated parameter values (audio thread writes, GUI reads for ghost indicators)
    struct ModulatedValues
    {
        // NaN = no modulation active → ghost hidden
        static constexpr float NONE = std::numeric_limits<float>::quiet_NaN();
        std::atomic<float> filterCutoff { NONE };
        std::atomic<float> filterResonance { NONE };
        std::atomic<float> scanPosition { NONE };
        std::atomic<float> noiseLevel { NONE };
        std::atomic<float> lfo1Rate { NONE };
        std::atomic<float> lfo1Depth { NONE };
        std::atomic<float> lfo2Rate { NONE };
        std::atomic<float> lfo2Depth { NONE };
        std::atomic<float> lfo3Rate { NONE };
        std::atomic<float> lfo3Depth { NONE };
        std::atomic<float> delayTime { NONE };
        std::atomic<float> delayFeedback { NONE };
        std::atomic<float> delayMix { NONE };
        std::atomic<float> reverbMix { NONE };
        // Drift → Osc targets (Alpha/Noise/Magnitude = effective base+offset; Axes = offset only)
        std::atomic<float> driftAlpha { NONE };
        std::atomic<float> driftAxis1 { NONE };
        std::atomic<float> driftAxis2 { NONE };
        std::atomic<float> driftAxis3 { NONE };
        std::atomic<float> drift1Depth { NONE };
        std::atomic<float> drift2Depth { NONE };
        std::atomic<float> drift3Depth { NONE };
        std::atomic<float> driftNoise { NONE };
        std::atomic<float> driftMagnitude { NONE };
        std::atomic<float> driftResynth { NONE };
    };
    ModulatedValues modulatedValues;

    // Drift regen coordination (audio thread writes, GUI reads)
    std::atomic<bool>  driftHasOscTarget { false };
    std::atomic<int>   driftRegenMode { 0 };       // 0=Manual, 1=Auto, 2-5=max N beats
    std::atomic<float> driftRegenBpm { 120.0f };   // current BPM for cooldown calc
    std::atomic<bool>  barBoundaryFlag { false };

    // ── MIDI CC Learn (public API — call from message thread only) ──────────
    struct CcMapping
    {
        juce::String                paramId;           // APVTS param ID — message thread only
        juce::RangedAudioParameter* param   = nullptr; // resolved pointer — audio thread reads this
        float                       minNorm = 0.0f;
        float                       maxNorm = 1.0f;
    };

    void startMidiLearn(const juce::String& paramId);
    void cancelMidiLearn();
    void clearCcMapping(int cc);
    void clearAllCcMappings();
    /** Returns a copy of the mapping for the given CC (0–127). Thread-safe: acquires ccMappingLock_. */
    CcMapping getCcMappingCopy(int cc) const;
    /** Packed (seq << 32) | cc, bumped whenever an incoming mapped CC changes a param. The
     *  editor polls this so the easy-mode ENV/LFO/Drift tab follows the hardware controller.
     *  Lock-free; read seq from the high 32 bits, cc from the low 32. */
    uint64_t getMidiTouchPacked()   const noexcept { return midiTouchPacked_.load(std::memory_order_acquire); }
    bool isMidiLearnActive() const { return midiLearnActive.load(std::memory_order_relaxed); }
    /** The param waiting for a CC assignment. Message thread only. */
    const juce::String& getMidiLearnParamId() const { return midiLearnParamId; }
    /** Search all mappings for one bound to paramId. Returns CC (0–127) or -1. Message thread only. */
    int findBoundCc(const juce::String& paramId) const;

    /** Fired on the message thread whenever MIDI Learn mode changes.
     *  learning=true → learn started. learning=false, boundCc≥0 → CC was bound.
     *  learning=false, boundCc<0 → cancelled or aborted (no change to make). */
    std::function<void(bool learning, int boundCc)> onMidiLearnStateChanged;

    // ── MIDI Output (LED feedback) — message thread only ────────────────────
    void openMidiOutputDevice(const juce::String& deviceId);
    void closeMidiOutputDevice();
    const juce::String& getMidiOutputDeviceId() const { return midiOutputDeviceId_; }
    /** Enable DAW mode, overwrite the XL Page-1 CC bindings with the canonical
     *  layout, and (after a short delay) light the LEDs. Authoritative for the
     *  Page-1 CC range; bindings on other CCs are left untouched. */
    void applyXLDefaultBindings();
    /** Overwrite ONLY the XL Page-1 CC bindings in ccMappings_ (no SysEx, no LED,
     *  no DAW-mode handshake) — safe to call from any thread. Used to keep the XL
     *  device bindings alive after a preset load wipes ccMappings_. */
    void populateXLDefaultBindings();

    // ── MIDI Clock Input ─────────────────────────────────────────────────────
    bool  isMidiClockActive()  const noexcept;
    float getMidiClockBpm()    const noexcept;
    bool  isMidiClockEnabled() const noexcept;
    void  setMidiClockEnabled(bool e);


private:

    void handleAsyncUpdate() override;

    // ── MIDI Output (LED feedback) ───────────────────────────────────────────
    std::unique_ptr<juce::MidiOutput> midiOutputDevice_;
    juce::String                      midiOutputDeviceId_;
    std::atomic<bool>                 dawModeActive_ { false };  // XL in DAW mode (LEDs + ch16 controls + buttons); audio thread reads it to gate button actions
    juce::SpinLock                    midiOutputLock_;

    // One-shot timer for the deferred XL LED burst (see applyXLDefaultBindings).
    // Owned + declared AFTER the midiOutput members so it is torn down (and its
    // ~Timer stops it) before them; ~T5ynthProcessor also stops it explicitly. This
    // makes the deferred burst safe even if a host destroys the plugin off the
    // message thread — unlike an un-cancellable Timer::callAfterDelay.
    struct OneShotTimer : public juce::Timer {
        std::function<void()> fn;
        void timerCallback() override { stopTimer(); if (fn) fn(); }
    };
    OneShotTimer                      xlLedTimer_;

    void sendMidiOutputMessage(const juce::MidiMessage& msg);
    void sendLearnLed(bool learning, int boundCc);
    void lightXLLeds();   // send all Page-1 LED colours (DAW mode; deferred from XL Map)

    // XL DAW-mode buttons (CC 37-52, ch1) → transport/actions. handleXLButtonPress
    // runs on the AUDIO thread (on a button press): it raises an atomic request and
    // triggerAsyncUpdate()s — the actual setValueNotifyingHost runs on the message
    // thread in handleAsyncUpdate() (it locks, so it must never run on the audio
    // thread). Panic uses the existing midiPanicRequested atomic.
    void handleXLButtonPress(int cc);
    juce::RangedAudioParameter* seqRunningParam_    = nullptr;  // resolved once (ctor)
    juce::RangedAudioParameter* genSeqRunningParam_ = nullptr;
    std::atomic<bool>           xlSeqToggleReq_     { false };  // audio→message: toggle seq_running
    std::atomic<bool>           xlSeqModeToggleReq_ { false };  // audio→message: toggle gen_seq_running
    std::atomic<bool>           xlAutoApplyReq_     { false };  // any→message: (re)apply XL bindings (port select / preset load)

    // ── MIDI CC Learn (internals) ────────────────────────────────────────────
    std::array<CcMapping, 128> ccMappings_;
    mutable juce::SpinLock     ccMappingLock_;
    std::atomic<bool>          midiLearnActive { false };
    std::atomic<int>           midiLearnTargetCc { -1 };  // audio thread writes, message thread reads

    // Easy-mode "tab follows controller": the audio thread records the last mapped
    // CC it applied; the editor's 30 Hz timer reads it (lock-free) and switches the
    // ENV/LFO/Drift tab to match. Packed as (seq << 32) | cc in ONE word so the
    // reader always gets a consistent seq/cc pair (single writer = the audio thread).
    std::atomic<uint64_t>      midiTouchPacked_ { 0 };
    juce::String               midiLearnParamId;           // message thread only

    // ── MIDI Clock Input (internals) ─────────────────────────────────────────
    // Atomics shared between audio thread (write BPM/valid) and message thread
    // (read for GUI display and resolveSyncBpm guard).
    std::atomic<bool>  midiClockEnabled_ { false };
    std::atomic<bool>  midiClockValid_   { false };
    std::atomic<float> midiClockBpm_     { 120.0f };
    // Audio-thread-only state — no concurrent access, no atomics needed:
    uint64_t           midiClockBlockStart_   = 0;     // abs sample at start of current block
    uint64_t           midiClockLastTick_     = 0;     // abs sample of last 0xF8 tick
    uint32_t           midiClockIntervals_[24] {};     // ring buffer of inter-tick intervals
    int                midiClockTickIdx_      = 0;
    int                midiClockTickCount_    = 0;     // ticks seen (caps at 24 = 1 beat)
    bool               midiClockPrevEnabled_  = false; // edge-detect re-enable on audio thread

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(T5ynthProcessor)
};
