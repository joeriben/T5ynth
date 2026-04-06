#pragma once
#include <JuceHeader.h>
#include <array>
#include <limits>
#include "dsp/VoiceManager.h"
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

class T5ynthProcessor : public juce::AudioProcessor
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

    // Engine mode (read from APVTS "engine_mode": 0=Sampler, 1=Wavetable)
    bool isWavetableMode() const;
    bool isSamplerMode() const;

    // Load generated audio into the engine
    void loadGeneratedAudio(const juce::AudioBuffer<float>& buffer, double sampleRate);

    // Inference (Python subprocess)
    bool isInferenceReady() const { return pipeInference.isReady(); }
    PipeInference& getPipeInference() { return pipeInference; }
    bool launchPipeInference(const juce::File& backendDir);
    bool isPipeInferenceReady() const { return pipeInference.isReady(); }

    // Last device/model used for generation (for preset tagging)
    void setLastDevice(const juce::String& dev) { lastDevice = dev; }
    const juce::String& getLastDevice() const { return lastDevice; }
    void setLastModel(const juce::String& m) { lastModel = m; }
    const juce::String& getLastModel() const { return lastModel; }

    // Preset metadata (GUI-only state that must survive save/load)
    void setLastPrompts(const juce::String& a, const juce::String& b) { lastPromptA = a; lastPromptB = b; }
    const juce::String& getLastPromptA() const { return lastPromptA; }
    const juce::String& getLastPromptB() const { return lastPromptB; }

    void setLastSeed(int s) { lastSeed = s; }
    int getLastSeed() const { return lastSeed; }

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

    // Waveform display data
    bool hasNewWaveform() const { return newWaveformReady.load(std::memory_order_acquire); }
    void clearNewWaveformFlag() { newWaveformReady.store(false, std::memory_order_release); }
    const juce::AudioBuffer<float>& getWaveformSnapshot() const { return waveformSnapshot; }

    // JSON preset import/export (compatible with Vue reference format)
    juce::String exportJsonPreset() const;
    bool importJsonPreset(const juce::String& json);

    // Sampler access for preset import (loop region brackets)
    SamplePlayer& getSampler() { return masterSampler; }

    /** Re-extract wavetable frames using current bracket region. */
    void reextractWavetable();

private:
    juce::AudioProcessorValueTreeState parameters;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Engine (mode is stored in APVTS "engine_mode", no separate member)

    // DSP — polyphonic voice pool
    VoiceManager voiceManager;

    // Master data holders (own the audio/frame data, voices share from these)
    WavetableOscillator masterOsc;
    SamplePlayer masterSampler;

    // DSP — global (shared across voices, post-sum)
    LFO lfo1;
    LFO lfo2;
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

    // Inference (Python subprocess)
    PipeInference pipeInference;
    juce::String lastDevice;
    juce::String lastModel;

    // Preset metadata (stored here so preset save can access them)
    juce::String lastPromptA, lastPromptB;
    int lastSeed = 123456789;
    std::array<AxisSlotState, 3> lastAxes;
    std::vector<float> lastEmbeddingA, lastEmbeddingB;
    juce::AudioBuffer<float> generatedAudioFull;  // boosted buffer for engines + display
    juce::AudioBuffer<float> generatedAudioRaw;   // raw VAE output (for re-apply on toggle)
    double generatedSampleRate = 44100.0;

    /** Two-band high shelf to compensate VAE decoder HF rolloff. */
    static void applyHfBoost(juce::AudioBuffer<float>& buffer, double sampleRate);

    /** Rumble filter — 2nd-order Butterworth HP at 25 Hz, removes DC/sub-rumble from VAE output. */
    static void applyRumbleFilter(juce::AudioBuffer<float>& buffer, double sampleRate);

    // Last triggered note (for pitch modulation in block-rate section)
    int lastTriggeredNote = -1;

    // Pre-allocated LFO buffers (avoid heap alloc in processBlock)
    std::vector<float> lfo1Buffer, lfo2Buffer;

    // Waveform display
    juce::AudioBuffer<float> waveformSnapshot;
    std::atomic<bool> newWaveformReady { false };

    // Track loaded reverb IR / seq preset to avoid reloading every block
    int lastReverbIr = -1;
    int lastSeqPreset = -1;

    // Idle detection (audio thread only — not atomic)
    int silentBlockCount = 0;
    int tailBlocks = 860;  // recalculated in prepareToPlay (~10s of reverb tail)

    // Pre-allocated buffer for parallel reverb send (avoids heap alloc in processBlock)
    juce::AudioBuffer<float> reverbSendBuffer;


public:
    // Audio idle state (audio thread writes, GUI reads for timer gating)
    std::atomic<bool> audioIdle { false };

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
        std::atomic<float> scanPosition { NONE };
        std::atomic<float> lfo1Rate { NONE };
        std::atomic<float> lfo1Depth { NONE };
        std::atomic<float> lfo2Rate { NONE };
        std::atomic<float> lfo2Depth { NONE };
        std::atomic<float> delayTime { NONE };
        std::atomic<float> delayFeedback { NONE };
        std::atomic<float> delayMix { NONE };
        std::atomic<float> reverbMix { NONE };
        // Drift → Osc targets (Alpha = effective base+offset; Axes = offset only)
        std::atomic<float> driftAlpha { NONE };
        std::atomic<float> driftAxis1 { NONE };
        std::atomic<float> driftAxis2 { NONE };
        std::atomic<float> driftAxis3 { NONE };
    };
    ModulatedValues modulatedValues;

    // Drift regen coordination (audio thread writes, GUI reads)
    std::atomic<bool>  driftHasOscTarget { false };
    std::atomic<int>   driftRegenMode { 0 };       // 0=Manual, 1=Auto, 2=1stBar
    std::atomic<bool>  barBoundaryFlag { false };
    // 1st-bar sync: GUI sets pendingBarLoadReady when regen result is stored,
    // audio thread sets triggerPendingLoad at the next bar boundary.
    std::atomic<bool>  pendingBarLoadReady { false };
    std::atomic<bool>  triggerPendingLoad { false };

private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(T5ynthProcessor)
};
