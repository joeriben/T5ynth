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
    AudioLooper& getSampler() { return masterLooper; }

private:
    juce::AudioProcessorValueTreeState parameters;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Engine (mode is stored in APVTS "engine_mode", no separate member)

    // DSP — polyphonic voice pool
    VoiceManager voiceManager;

    // Master data holders (own the audio/frame data, voices share from these)
    WavetableOscillator masterOsc;
    AudioLooper masterLooper;

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

    // DEBUG: direct sample playback + sequencer bypass
    juce::AudioBuffer<float> debugSampleBuf;
    int debugSamplePos = 0;
    double debugSampleReadPos = 0.0;
    double debugSampleSpeed = 1.0;
    double debugSeqCounter = 0.0;
    double debugSeqGateCounter = 0.0;
    int debugSeqStep = 0;

public:
    // MIDI monitor (audio thread writes, GUI reads)
    std::atomic<int> lastMidiNote { -1 };
    std::atomic<int> lastMidiVelocity { 0 };
    std::atomic<bool> lastMidiNoteOn { false };

private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(T5ynthProcessor)
};
