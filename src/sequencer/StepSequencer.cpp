#include "StepSequencer.h"

void T5ynthStepSequencer::prepare(double sr, int /*samplesPerBlock*/)
{
    sampleRate = sr;
    samplesUntilNextStep = 0.0;
}

void T5ynthStepSequencer::processBlock(juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer& midi)
{
    // When stopped, send a final note-off and reset
    if (!running)
    {
        if (lastPlayedNote >= 0)
        {
            midi.addEvent(juce::MidiMessage::noteOff(1, lastPlayedNote), 0);
            lastPlayedNote = -1;
        }
        currentStep = 0;
        currentStepForGui.store(-1, std::memory_order_relaxed);
        return;
    }

    const double samplesPerStep = sampleRate * 60.0 / bpm / 4.0; // 16th notes
    const int numSamples = buffer.getNumSamples();
    int samplePos = 0;

    while (samplePos < numSamples)
    {
        if (samplesUntilNextStep <= 0.0)
        {
            // Note-off for previous step
            if (lastPlayedNote >= 0)
            {
                midi.addEvent(juce::MidiMessage::noteOff(1, lastPlayedNote), samplePos);
                lastPlayedNote = -1;
            }

            // Note-on for current step if enabled
            auto& step = steps[static_cast<size_t>(currentStep)];
            if (step.enabled)
            {
                int vel = juce::jlimit(1, 127, juce::roundToInt(step.velocity * 127.0f));
                midi.addEvent(juce::MidiMessage::noteOn(1, step.note,
                              static_cast<juce::uint8>(vel)), samplePos);
                lastPlayedNote = step.note;
            }

            currentStepForGui.store(currentStep, std::memory_order_relaxed);

            // Advance to next step
            currentStep = (currentStep + 1) % numSteps;
            samplesUntilNextStep += samplesPerStep;
        }

        int samplesToProcess = juce::jmin(numSamples - samplePos,
                                          static_cast<int>(std::ceil(samplesUntilNextStep)));
        samplesUntilNextStep -= samplesToProcess;
        samplePos += samplesToProcess;
    }
}

void T5ynthStepSequencer::reset()
{
    currentStep = 0;
    samplesUntilNextStep = 0.0;
    running = false;
    lastPlayedNote = -1;
    currentStepForGui.store(-1, std::memory_order_relaxed);
}

void T5ynthStepSequencer::setNumSteps(int s)
{
    numSteps = juce::jlimit(1, MAX_STEPS, s);
}

void T5ynthStepSequencer::setStepNote(int step, int midiNote)
{
    if (step >= 0 && step < MAX_STEPS)
        steps[static_cast<size_t>(step)].note = midiNote;
}

void T5ynthStepSequencer::setStepVelocity(int step, float velocity)
{
    if (step >= 0 && step < MAX_STEPS)
        steps[static_cast<size_t>(step)].velocity = juce::jlimit(0.0f, 1.0f, velocity);
}

void T5ynthStepSequencer::setStepEnabled(int step, bool enabled)
{
    if (step >= 0 && step < MAX_STEPS)
        steps[static_cast<size_t>(step)].enabled = enabled;
}
