#include "Arpeggiator.h"

void T5ynthArpeggiator::prepare(double sr, int /*samplesPerBlock*/)
{
    sampleRateVal = sr;
    samplesUntilNext = 0.0;
}

void T5ynthArpeggiator::rebuildIntervals()
{
    intervals.clear();

    // Expand the incoming base note across the selected octave range.
    for (int oct = 0; oct < octaveRange; ++oct)
        for (int i = 0; i < NUM_BASE_INTERVALS; ++i)
            intervals.push_back(BASE_INTERVALS[i] + oct * 12);

    // Apply pattern ordering
    switch (mode)
    {
        case Mode::Up:
            // Already in order
            break;
        case Mode::Down:
            std::reverse(intervals.begin(), intervals.end());
            break;
        case Mode::UpDown:
        {
            if (intervals.size() <= 1) break;
            auto up = intervals;
            // Down part: reverse, omitting endpoints to avoid double-stepping
            auto down = std::vector<int>(up.rbegin() + 1, up.rend() - 1);
            intervals.reserve(up.size() + down.size());
            intervals.insert(intervals.end(), down.begin(), down.end());
            break;
        }
        case Mode::Random:
            fisherYatesShuffle();
            break;
    }
}

void T5ynthArpeggiator::fisherYatesShuffle()
{
    for (int i = static_cast<int>(intervals.size()) - 1; i > 0; --i)
    {
        int j = rng.nextInt(i + 1);
        std::swap(intervals[static_cast<size_t>(i)], intervals[static_cast<size_t>(j)]);
    }
}

double T5ynthArpeggiator::shuffledStepDurationSamples(double baseStepSamples,
                                                       int stepIdx,
                                                       int cycleLength) const
{
    if (shuffle <= 0.0f || cycleLength <= 1)
        return baseStepSamples;

    if ((cycleLength & 1) != 0 && stepIdx == cycleLength - 1)
        return baseStepSamples;

    const double amount = static_cast<double>(shuffle);
    return baseStepSamples * ((stepIdx & 1) == 0 ? (1.0 + amount) : (1.0 - amount));
}

void T5ynthArpeggiator::setMode(Mode m)
{
    mode = m;
    rebuildIntervals();
}

void T5ynthArpeggiator::setBaseNote(int midiNote, float velocity)
{
    bool wasActive = active;
    baseNote = midiNote;
    baseVelocity = velocity;
    active = true;

    rebuildIntervals();

    // Only reset timing when starting fresh — don't restart on every seq step
    if (!wasActive)
    {
        currentIndex = 0;
        samplesUntilNext = 0.0;
        samplesUntilGateOff = -1.0;
    }
}

void T5ynthArpeggiator::stopArp()
{
    active = false;
    currentIndex = 0;
    samplesUntilNext = 0.0;
    samplesUntilGateOff = -1.0;
}

void T5ynthArpeggiator::allNotesOff(std::vector<VoiceEvent>& out, int sampleOffset)
{
    if (lastPlayedNote >= 0)
    {
        out.push_back({ sampleOffset, VoiceEvent::Type::NoteOff, lastPlayedNote, 0.0f,
                        VoiceEvent::Articulation::Normal, -1, 0.0f });
        lastPlayedNote = -1;
    }
    samplesUntilGateOff = -1.0;
}

void T5ynthArpeggiator::processBlock(juce::AudioBuffer<float>& buffer,
                                     std::vector<VoiceEvent>& out)
{
    if (!active || intervals.empty())
    {
        if (lastPlayedNote >= 0)
        {
            out.push_back({ 0, VoiceEvent::Type::NoteOff, lastPlayedNote, 0.0f,
                            VoiceEvent::Articulation::Normal, -1, 0.0f });
            lastPlayedNote = -1;
        }
        samplesUntilGateOff = -1.0;
        return;
    }

    // Calculate step duration in samples
    double quarterNoteSamples = sampleRateVal * 60.0 / bpm;
    double samplesPerStep = quarterNoteSamples * static_cast<double>(RATE_FACTORS[rate]);

    const int numSamples = buffer.getNumSamples();
    int samplePos = 0;

    while (samplePos < numSamples)
    {
        // Check gate-off before next step (mirrors StepSequencer pattern)
        if (samplesUntilGateOff >= 0.0 && samplesUntilGateOff <= samplesUntilNext)
        {
            int gateOffPos = juce::jmin(samplePos + static_cast<int>(samplesUntilGateOff),
                                        numSamples - 1);
            if (lastPlayedNote >= 0)
            {
                out.push_back({ gateOffPos, VoiceEvent::Type::NoteOff, lastPlayedNote, 0.0f,
                                VoiceEvent::Articulation::Normal, -1, 0.0f });
                lastPlayedNote = -1;
            }
            samplesUntilNext -= samplesUntilGateOff;
            samplePos += static_cast<int>(samplesUntilGateOff);
            samplesUntilGateOff = -1.0;
            continue;
        }

        if (samplesUntilNext <= 0.0)
        {
            // Note-off for previous (if gate didn't already end it)
            if (lastPlayedNote >= 0)
            {
                out.push_back({ samplePos, VoiceEvent::Type::NoteOff, lastPlayedNote, 0.0f,
                                VoiceEvent::Articulation::Normal, -1, 0.0f });
                lastPlayedNote = -1;
            }

            int idx = currentIndex % static_cast<int>(intervals.size());
            const double stepDur = shuffledStepDurationSamples(samplesPerStep, idx,
                                                               static_cast<int>(intervals.size()));
            int midiNote = juce::jlimit(0, 127, baseNote + intervals[static_cast<size_t>(idx)]);
            float vel = juce::jlimit(0.0f, 1.0f, baseVelocity);

            out.push_back({ samplePos, VoiceEvent::Type::NoteOn, midiNote, vel,
                            VoiceEvent::Articulation::Normal, -1, 0.0f });
            lastPlayedNote = midiNote;
            currentIndex++;

            // Schedule gate-off (skip for 100% gate — true legato, no retrigger)
            samplesUntilGateOff = (gate >= 1.0f) ? -1.0 : gate * stepDur;

            // Rebuild intervals on each cycle for random pattern freshness
            if (mode == Mode::Random && currentIndex % static_cast<int>(intervals.size()) == 0)
                fisherYatesShuffle();

            samplesUntilNext += stepDur;
        }

        // Advance by minimum of remaining step time and gate-off time
        double advance = samplesUntilNext;
        if (samplesUntilGateOff >= 0.0)
            advance = std::min(advance, samplesUntilGateOff);
        int samplesToProcess = juce::jmin(numSamples - samplePos,
                                          static_cast<int>(std::ceil(advance)));
        samplesToProcess = std::max(1, samplesToProcess);
        samplesUntilNext -= samplesToProcess;
        if (samplesUntilGateOff >= 0.0)
            samplesUntilGateOff -= samplesToProcess;
        samplePos += samplesToProcess;
    }
}

void T5ynthArpeggiator::reset()
{
    active = false;
    currentIndex = 0;
    samplesUntilNext = 0.0;
    samplesUntilGateOff = -1.0;
    lastPlayedNote = -1;
    intervals.clear();
}
