#pragma once
#include <JuceHeader.h>
#include "dsp/VoiceManager.h"
#include "dsp/LFO.h"
#include "dsp/DriftLFO.h"
#include "dsp/DelayLine.h"
#include "dsp/ConvolutionReverb.h"
#include "dsp/Limiter.h"
#include "sequencer/StepSequencer.h"
#include "sequencer/Arpeggiator.h"
#include "backend/BackendManager.h"
#include "backend/BackendConnection.h"
#include "inference/T5ynthInference.h"
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

    // Backend (legacy HTTP — kept for fallback)
    BackendManager& getBackendManager() { return backendManager; }
    BackendConnection& getBackendConnection() { return backendConnection; }

    // Native inference (LibTorch — deprecated)
    T5ynthInference& getInference() { return inference; }
    bool loadInferenceModels(const juce::File& modelDir);
    bool isInferenceReady() const { return pipeInference.isReady() || inference.isLoaded(); }

    // Pipe inference (Python subprocess)
    PipeInference& getPipeInference() { return pipeInference; }
    bool launchPipeInference(const juce::File& backendDir);
    bool isPipeInferenceReady() const { return pipeInference.isReady(); }

    // Last device used for generation (for preset tagging)
    void setLastDevice(const juce::String& dev) { lastDevice = dev; }
    const juce::String& getLastDevice() const { return lastDevice; }

    // Preset metadata (GUI-only state that must survive save/load)
    void setLastPrompts(const juce::String& a, const juce::String& b) { lastPromptA = a; lastPromptB = b; }
    const juce::String& getLastPromptA() const { return lastPromptA; }
    const juce::String& getLastPromptB() const { return lastPromptB; }

    void setLastSeed(int s) { lastSeed = s; }
    int getLastSeed() const { return lastSeed; }

    void setLastEmbeddings(const std::vector<float>& a, const std::vector<float>& b) { lastEmbeddingA = a; lastEmbeddingB = b; }
    const std::vector<float>& getLastEmbeddingA() const { return lastEmbeddingA; }
    const std::vector<float>& getLastEmbeddingB() const { return lastEmbeddingB; }

    /** Get the raw generated audio buffer (channel 0 snapshot for display). */
    const juce::AudioBuffer<float>& getGeneratedAudio() const { return generatedAudioFull; }
    double getGeneratedSampleRate() const { return generatedSampleRate; }

    // Sequencer
    T5ynthStepSequencer& getStepSequencer() { return stepSequencer; }
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
    T5ynthLimiter limiter;

    // Sequencer
    T5ynthStepSequencer stepSequencer;
    T5ynthArpeggiator arpeggiator;

    // Backend (backendConnection destroyed before backendManager — correct order)
    BackendManager backendManager;
    BackendConnection backendConnection;

    // Native inference (LibTorch — deprecated, kept for reference)
    T5ynthInference inference;

    // Pipe inference (Python subprocess — actual working inference)
    PipeInference pipeInference;
    juce::String lastDevice;

    // Preset metadata (stored here so preset save can access them)
    juce::String lastPromptA, lastPromptB;
    int lastSeed = 123456789;
    std::vector<float> lastEmbeddingA, lastEmbeddingB;
    juce::AudioBuffer<float> generatedAudioFull;  // full stereo buffer for preset embedding
    double generatedSampleRate = 44100.0;

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
    static constexpr int TAIL_BLOCKS = 344;  // ~4s at 44.1kHz/512

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
    };
    ModulatedValues modulatedValues;

private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(T5ynthProcessor)
};
