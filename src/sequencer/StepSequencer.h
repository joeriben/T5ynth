#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>

/**
 * Step sequencer for triggering notes and parameter changes.
 *
 * Supports up to 64 steps with per-step velocity, gate length, and pitch.
 * Syncs to host tempo via AudioPlayHead.
 */
class T5ynthStepSequencer
{
public:
    static constexpr int MAX_STEPS = 64;

    T5ynthStepSequencer() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void reset();

    /** Set the number of active steps (1-64). */
    void setNumSteps(int steps);

    /** Set step data. */
    void setStepNote(int step, int midiNote);
    void setStepVelocity(int step, float velocity);
    void setStepEnabled(int step, bool enabled);

    /** Set playback rate in BPM. */
    void setBpm(double newBpm) { bpm = newBpm; }

    /** Start/stop sequencer. */
    void start() { if (!running) { running = true; samplesUntilNextStep = 0.0; currentStep = 0; } }
    void stop() { running = false; }
    bool isRunning() const { return running; }

    int getCurrentStep() const { return currentStep; }

    /** Atomic step position for GUI polling (written on audio thread). */
    std::atomic<int> currentStepForGui { -1 };

private:
    struct Step
    {
        int note = 60;
        float velocity = 1.0f;
        bool enabled = true;
    };

    std::array<Step, MAX_STEPS> steps;
    int numSteps = 16;
    int currentStep = 0;
    double bpm = 120.0;
    double sampleRate = 44100.0;
    double samplesUntilNextStep = 0.0;
    bool running = false;
    int lastPlayedNote = -1;
};
