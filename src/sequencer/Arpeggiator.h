#pragma once
#include <JuceHeader.h>
#include <vector>
#include <array>

/**
 * Arpeggiator — port of useArpeggiator.ts.
 *
 * Takes a base note and expands it through chord intervals [0, 4, 7]
 * across octave range, then applies pattern ordering.
 *
 * This is NOT a held-note arpeggiator — it receives a single base note
 * and generates a chord pattern from fixed intervals (major triad).
 *
 * Musical rate divisions: 1/4, 1/8, 1/16, 1/32, 1/4T, 1/8T, 1/16T.
 */
class T5ynthArpeggiator
{
public:
    enum class Mode { Up, Down, UpDown, Random };

    /** Rate division indices (matching APVTS choice order) */
    enum RateDiv { Rate_1_4 = 0, Rate_1_8, Rate_1_16, Rate_1_32, Rate_1_4T, Rate_1_8T, Rate_1_16T };
    static constexpr float RATE_FACTORS[] = { 1.0f, 0.5f, 0.25f, 0.125f, 2.0f/3.0f, 1.0f/3.0f, 1.0f/6.0f };
    static constexpr int NUM_RATES = 7;

    T5ynthArpeggiator() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void reset();

    void setMode(Mode m);
    void setRate(int rateIndex) { rate = juce::jlimit(0, NUM_RATES - 1, rateIndex); }
    void setOctaveRange(int octaves) { octaveRange = juce::jlimit(1, 4, octaves); rebuildIntervals(); }
    void setBpm(double b) { bpm = b; }
    void setGate(float g) { gate = juce::jlimit(0.1f, 1.0f, g); }
    void setShuffle(float amount) { shuffle = juce::jlimit(0.0f, 0.75f, amount); }

    /** Set base note and start arpeggiating. */
    void setBaseNote(int midiNote, float velocity);

    /** Stop arpeggiator. */
    void stopArp();

    /** Emit note-off for any currently-sounding arp note into `midi` at
        `sampleOffset`, then clear lastPlayedNote. Does not alter active/
        pattern state — safe to call unconditionally at any transition. */
    void allNotesOff(juce::MidiBuffer& midi, int sampleOffset = 0);

    bool isActive() const { return active; }
    int getLastPlayedNote() const { return lastPlayedNote; }

private:
    // Classic single-note arpeggio: the held base note is repeated across
    // the configured octave range. The previous {0, 4, 7} chord stack
    // forced a major triad on every base note regardless of context, which
    // collided with non-tonal Pitch-Field material in the gen sequencer.
    static constexpr int CHORD_INTERVALS[] = { 0 };
    static constexpr int NUM_CHORD_NOTES = 1;

    Mode mode = Mode::Up;
    int rate = 2; // default 1/16
    int octaveRange = 1;
    int baseNote = 60;
    float baseVelocity = 0.8f;
    bool active = false;

    std::vector<int> intervals;
    int currentIndex = 0;
    double sampleRateVal = 44100.0;
    double bpm = 120.0;
    float gate = 1.0f;
    float shuffle = 0.0f;
    double samplesUntilNext = 0.0;
    double samplesUntilGateOff = -1.0;
    int lastPlayedNote = -1;

    juce::Random rng;

    void rebuildIntervals();
    void fisherYatesShuffle();
    double shuffledStepDurationSamples(double baseStepSamples, int stepIdx, int cycleLength) const;
};
