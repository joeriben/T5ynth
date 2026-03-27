#pragma once
#include <JuceHeader.h>
#include <vector>
#include <algorithm>

/**
 * Arpeggiator that cycles through held notes.
 *
 * Modes: Up, Down, UpDown, Random, Order.
 * Syncs to host tempo.
 */
class T5ynthArpeggiator
{
public:
    enum class Mode { Up, Down, UpDown, Random, Order };

    T5ynthArpeggiator() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void reset();

    void setMode(Mode m) { mode = m; }
    void setRate(float noteDivision) { rate = noteDivision; }
    void setOctaveRange(int octaves) { octaveRange = juce::jlimit(1, 4, octaves); }
    void setBpm(double b) { bpm = b; }

    void noteOn(int midiNote, float velocity);
    void noteOff(int midiNote);

    bool isActive() const { return !heldNotes.empty(); }

private:
    struct HeldNote
    {
        int note;
        float velocity;
    };

    std::vector<HeldNote> heldNotes;
    Mode mode = Mode::Up;
    float rate = 1.0f; // Quarter note = 1.0
    int octaveRange = 1;
    int currentIndex = 0;
    double sampleRate = 44100.0;
    double bpm = 120.0;
    double samplesUntilNext = 0.0;
    int lastPlayedNote = -1;
};
