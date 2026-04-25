#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <functional>

/**
 * Step sequencer — port of useStepSequencer.ts.
 *
 * Features:
 * - Per-step: note (MIDI), velocity (0-1), gate (0.1-1), bind (bool), enabled
 * - 5 note divisions: 1/1, 1/2, 1/4, 1/8, 1/16
 * - Gate controls note duration as fraction of step duration
 * - Bind flag: pitch change without note retrigger (preceding noteOff suppressed)
 * - 10 preset patterns from reference
 * - Bar-start callback (atomic flag) for drift regen sync
 */
class T5ynthStepSequencer
{
public:
    static constexpr int MAX_STEPS = 64;

    /** Note division relative to quarter note */
    enum Division { Div_1_1 = 0, Div_1_2, Div_1_4, Div_1_8, Div_1_16 };
    static constexpr float DIVISION_FACTORS[] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f };

    static constexpr int NUM_PRESETS = 10;

    T5ynthStepSequencer() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void reset();

    /** Set the number of active steps (1-64). */
    void setNumSteps(int steps);
    int getNumSteps() const { return numSteps; }

    /** Set step data. */
    void setStepNote(int step, int midiNote);
    void setStepVelocity(int step, float velocity);
    void setStepEnabled(int step, bool enabled);
    void setStepGate(int step, float gate);
    void setStepBind(int step, bool bind);

    /** Bulk set gate for all steps. */
    void setAllGates(float gate);

    /** Set playback rate in BPM. */
    void setBpm(double newBpm) { bpm = newBpm; }

    /** Set note division. */
    void setDivision(int div) { division = juce::jlimit(0, 4, div); }
    void setShuffle(float amount) { shuffle = juce::jlimit(0.0f, 0.75f, amount); }
    void setOctaveShiftSemitones(int semitones) { octaveShiftSemitones = semitones; }

    /** Auto glide time: 50% of step duration (ms), clamped to 10-500. */
    float getGlideTime() const
    {
        double stepMs = (60.0 / bpm) * static_cast<double>(DIVISION_FACTORS[division]) * 1000.0;
        return juce::jlimit(10.0f, 500.0f, static_cast<float>(stepMs * 0.5));
    }

    /** Load a preset pattern by index (0-9). */
    void loadPreset(int index);

    /** Reset grid to uniform defaults. */
    void resetGrid();

    /** Start/stop sequencer. */
    void start() { if (!running) { running = true; samplesUntilNextStep = 0.0; currentStep = 0; scheduledStep = 0; } }
    void stop();

    /** Emit note-off for the currently-sounding step note into `midi` at
        `sampleOffset`, then clear lastPlayedNote. Does not alter running/
        position — safe to call at any transition. */
    void allNotesOff(juce::MidiBuffer& midi, int sampleOffset = 0);

    bool isRunning() const { return running; }

    int getCurrentStep() const { return currentStep; }

    /** Atomic step position for GUI polling. */
    std::atomic<int> currentStepForGui { -1 };

    /** Atomic flag: set to true when bar boundary crossed (step wraps to 0). */
    std::atomic<bool> barStartFlag { false };

    struct Step
    {
        int note = 60;
        float velocity = 0.8f;
        float gate = 0.8f;
        bool enabled = true;
        bool bind = false;
    };

    const Step& getStep(int idx) const { return steps[static_cast<size_t>(juce::jlimit(0, MAX_STEPS - 1, idx))]; }

private:
    std::array<Step, MAX_STEPS> steps;
    int numSteps = 16;
    int currentStep = 0;
    int scheduledStep = 0;
    double bpm = 120.0;
    double sampleRate = 44100.0;
    double samplesUntilNextStep = 0.0;
    double samplesUntilGateOff = -1.0; // <0 means no pending gate-off
    bool running = false;
    int lastPlayedNote = -1;
    int division = 3; // default 1/8 (index 3)
    float shuffle = 0.0f;
    int octaveShiftSemitones = 0;
    // glideTimeMs removed — now computed automatically from BPM/division

    double stepDurationSamples() const;
    double shuffledStepDurationSamples(int stepIdx) const;

    // Preset data
    struct PresetData { const char* name; const Step* steps; int count; };
    static const PresetData presetTable[NUM_PRESETS];
};
