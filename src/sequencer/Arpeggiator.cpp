#include "Arpeggiator.h"

void T5ynthArpeggiator::prepare(double sr, int /*samplesPerBlock*/)
{
    sampleRate = sr;
    samplesUntilNext = 0.0;
}

void T5ynthArpeggiator::processBlock(juce::AudioBuffer<float>& buffer,
                                     juce::MidiBuffer& midi)
{
    if (heldNotes.empty())
    {
        if (lastPlayedNote >= 0)
        {
            midi.addEvent(juce::MidiMessage::noteOff(1, lastPlayedNote), 0);
            lastPlayedNote = -1;
        }
        currentIndex = 0;
        return;
    }

    // rate: 1.0 = quarter note, 0.25 = 16th note
    const double samplesPerStep = sampleRate * 60.0 / bpm * static_cast<double>(rate);
    const int numSamples = buffer.getNumSamples();
    int samplePos = 0;

    while (samplePos < numSamples)
    {
        if (samplesUntilNext <= 0.0)
        {
            // Note-off for previous
            if (lastPlayedNote >= 0)
            {
                midi.addEvent(juce::MidiMessage::noteOff(1, lastPlayedNote), samplePos);
                lastPlayedNote = -1;
            }

            // Build sorted note list
            auto sorted = heldNotes;
            if (sorted.empty()) return; // safety: notes may have been cleared

            if (mode == Mode::Up || mode == Mode::Down || mode == Mode::UpDown)
                std::sort(sorted.begin(), sorted.end(),
                          [](const HeldNote& a, const HeldNote& b) { return a.note < b.note; });

            int noteCount = static_cast<int>(sorted.size());
            int totalSteps = noteCount * octaveRange;

            // Compute which note + octave to play
            int noteIdx = 0;
            int octaveOffset = 0;

            if (mode == Mode::Up || mode == Mode::Order)
            {
                int idx = currentIndex % totalSteps;
                noteIdx = idx % noteCount;
                octaveOffset = idx / noteCount;
            }
            else if (mode == Mode::Down)
            {
                int idx = totalSteps - 1 - (currentIndex % totalSteps);
                noteIdx = idx % noteCount;
                octaveOffset = idx / noteCount;
            }
            else if (mode == Mode::UpDown)
            {
                int pingPongLen = juce::jmax(1, totalSteps * 2 - 2);
                int pp = currentIndex % pingPongLen;
                if (pp >= totalSteps)
                    pp = (pingPongLen - 1) - (pp - totalSteps);
                pp = juce::jlimit(0, totalSteps - 1, pp);
                noteIdx = pp % noteCount;
                octaveOffset = pp / noteCount;
            }
            else // Random
            {
                noteIdx = juce::Random::getSystemRandom().nextInt(noteCount);
                octaveOffset = juce::Random::getSystemRandom().nextInt(octaveRange);
            }

            int midiNote = juce::jlimit(0, 127, sorted[static_cast<size_t>(noteIdx)].note + octaveOffset * 12);
            float vel = sorted[static_cast<size_t>(noteIdx)].velocity;
            int velInt = juce::jlimit(1, 127, juce::roundToInt(vel * 127.0f));

            midi.addEvent(juce::MidiMessage::noteOn(1, midiNote,
                          static_cast<juce::uint8>(velInt)), samplePos);
            lastPlayedNote = midiNote;
            currentIndex++;

            samplesUntilNext += samplesPerStep;
        }

        int samplesToProcess = juce::jmin(numSamples - samplePos,
                                          static_cast<int>(std::ceil(samplesUntilNext)));
        samplesUntilNext -= samplesToProcess;
        samplePos += samplesToProcess;
    }
}

void T5ynthArpeggiator::reset()
{
    heldNotes.clear();
    currentIndex = 0;
    samplesUntilNext = 0.0;
    lastPlayedNote = -1;
}

void T5ynthArpeggiator::noteOn(int midiNote, float velocity)
{
    // Avoid duplicates
    for (const auto& n : heldNotes)
        if (n.note == midiNote) return;

    heldNotes.push_back({ midiNote, velocity });
}

void T5ynthArpeggiator::noteOff(int midiNote)
{
    heldNotes.erase(
        std::remove_if(heldNotes.begin(), heldNotes.end(),
                       [midiNote](const HeldNote& n) { return n.note == midiNote; }),
        heldNotes.end());

    if (heldNotes.empty())
    {
        currentIndex = 0;
        samplesUntilNext = 0.0;
    }
    else if (currentIndex >= static_cast<int>(heldNotes.size()))
    {
        currentIndex = 0;
    }
}
