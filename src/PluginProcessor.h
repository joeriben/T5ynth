#pragma once
#include <JuceHeader.h>
#include "dsp/WavetableOscillator.h"
#include "dsp/AudioLooper.h"
#include "dsp/ADSREnvelope.h"
#include "dsp/LFO.h"
#include "dsp/DriftLFO.h"
#include "dsp/StateVariableFilter.h"
#include "dsp/DelayLine.h"
#include "dsp/ConvolutionReverb.h"
#include "dsp/Limiter.h"
#include "dsp/ModulationMatrix.h"
#include "sequencer/StepSequencer.h"
#include "sequencer/Arpeggiator.h"
#include "backend/BackendManager.h"
#include "backend/BackendConnection.h"

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

    // Backend
    BackendManager& getBackendManager() { return backendManager; }
    BackendConnection& getBackendConnection() { return backendConnection; }

    // Sequencer
    T5ynthStepSequencer& getStepSequencer() { return stepSequencer; }
    T5ynthArpeggiator& getArpeggiator() { return arpeggiator; }

    // Waveform display data
    bool hasNewWaveform() const { return newWaveformReady.load(std::memory_order_acquire); }
    void clearNewWaveformFlag() { newWaveformReady.store(false, std::memory_order_release); }
    const juce::AudioBuffer<float>& getWaveformSnapshot() const { return waveformSnapshot; }

private:
    juce::AudioProcessorValueTreeState parameters;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Engine
    EngineMode engineMode = EngineMode::Looper;

    // DSP
    WavetableOscillator wavetableOsc;
    AudioLooper looper;
    ADSREnvelope ampEnvelope;
    ADSREnvelope modEnvelope1;
    ADSREnvelope modEnvelope2;
    LFO lfo1;
    LFO lfo2;
    DriftLFO driftLfo;
    T5ynthFilter filter;
    T5ynthDelayLine delay;
    ConvolutionReverb reverb;
    T5ynthLimiter limiter;
    ModulationMatrix modMatrix;

    // Sequencer
    T5ynthStepSequencer stepSequencer;
    T5ynthArpeggiator arpeggiator;

    // Backend (backendConnection destroyed before backendManager — correct order)
    BackendManager backendManager;
    BackendConnection backendConnection;

    // State
    float currentNote = -1.0f;
    float currentVelocity = 0.0f;
    bool noteIsOn = false;

    // Waveform display
    juce::AudioBuffer<float> waveformSnapshot;
    std::atomic<bool> newWaveformReady { false };

public:
    // MIDI monitor (audio thread writes, GUI reads)
    std::atomic<int> lastMidiNote { -1 };
    std::atomic<int> lastMidiVelocity { 0 };
    std::atomic<bool> lastMidiNoteOn { false };

private:

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(T5ynthProcessor)
};
