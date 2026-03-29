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

    // Engine mode
    enum class EngineMode { Looper, Wavetable };
    EngineMode getEngineMode() const { return engineMode; }
    void setEngineMode(EngineMode mode) { engineMode = mode; }

    // Load generated audio into the engine
    void loadGeneratedAudio(const juce::AudioBuffer<float>& buffer, double sampleRate);

    // Backend (legacy HTTP — kept for fallback)
    BackendManager& getBackendManager() { return backendManager; }
    BackendConnection& getBackendConnection() { return backendConnection; }

    // Native inference (LibTorch)
    T5ynthInference& getInference() { return inference; }
    bool loadInferenceModels(const juce::File& modelDir);
    bool isInferenceReady() const { return inference.isLoaded(); }

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

    // Looper access for preset import (loop region brackets)
    AudioLooper& getLooper() { return masterLooper; }

private:
    juce::AudioProcessorValueTreeState parameters;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Engine
    EngineMode engineMode = EngineMode::Looper;

    // DSP — polyphonic voice pool
    VoiceManager voiceManager;

    // Master data holders (own the audio/frame data, voices share from these)
    WavetableOscillator masterOsc;
    AudioLooper masterLooper;

    // DSP — global (shared across voices, post-sum)
    LFO lfo1;
    LFO lfo2;
    DriftLFO driftLfo;
    T5ynthDelayLine delay;
    ConvolutionReverb reverb;
    T5ynthLimiter limiter;

    // Sequencer
    T5ynthStepSequencer stepSequencer;
    T5ynthArpeggiator arpeggiator;

    // Backend (backendConnection destroyed before backendManager — correct order)
    BackendManager backendManager;
    BackendConnection backendConnection;

    // Native inference (LibTorch — replaces backend)
    T5ynthInference inference;

    // Last triggered note (for pitch modulation in block-rate section)
    int lastTriggeredNote = -1;

    // Waveform display
    juce::AudioBuffer<float> waveformSnapshot;
    std::atomic<bool> newWaveformReady { false };

    // Track loaded reverb IR index to avoid reloading every block
    int lastReverbIr = -1;

public:
    // MIDI monitor (audio thread writes, GUI reads)
    std::atomic<int> lastMidiNote { -1 };
    std::atomic<int> lastMidiVelocity { 0 };
    std::atomic<bool> lastMidiNoteOn { false };

private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(T5ynthProcessor)
};
