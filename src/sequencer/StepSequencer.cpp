#include "StepSequencer.h"

// ─── Preset patterns (exact port from useStepSequencer.ts) ─────────────────

static constexpr T5ynthStepSequencer::Step mkStep(int semi, float vel, float gate, bool active = true, bool glide = false)
{
    return { 60 + semi, vel, gate, active, glide };
}
static constexpr T5ynthStepSequencer::Step REST = { 60, 0.0f, 0.0f, false, false };

// clang-format off
static constexpr T5ynthStepSequencer::Step P_EASTCOAST[] = {
    mkStep(0,0.79f,0.5f), mkStep(12,0.63f,0.25f), mkStep(7,0.63f,0.5f), mkStep(12,0.63f,0.25f),
    mkStep(0,0.87f,0.5f), mkStep(12,0.63f,0.25f), mkStep(10,0.63f,0.5f), mkStep(12,0.63f,0.25f),
    mkStep(0,0.79f,0.5f), mkStep(12,0.63f,0.25f), mkStep(3,0.63f,0.5f), mkStep(12,0.63f,0.25f),
    mkStep(0,0.87f,0.5f), mkStep(12,0.63f,0.25f), mkStep(5,0.71f,0.8f), mkStep(7,0.47f,0.25f),
};
static constexpr T5ynthStepSequencer::Step P_WESTCOAST[] = {
    mkStep(0,1.0f,0.8f), mkStep(19,0.47f,1.0f), REST, mkStep(24,0.79f,0.1f), mkStep(3,0.31f,0.4f),
};
static constexpr T5ynthStepSequencer::Step P_SYNTHWAVE[] = {
    REST, mkStep(0,0.79f,0.4f), mkStep(0,0.79f,0.4f), mkStep(3,0.87f,0.4f),
    REST, mkStep(0,0.79f,0.4f), mkStep(0,0.79f,0.4f), mkStep(3,0.87f,0.4f),
    REST, mkStep(0,0.79f,0.4f), mkStep(0,0.79f,0.4f), mkStep(7,0.87f,0.4f),
    REST, mkStep(0,0.79f,0.4f), mkStep(0,0.79f,0.4f), mkStep(7,0.87f,0.4f),
};
static constexpr T5ynthStepSequencer::Step P_TECHNO[] = {
    REST, mkStep(0,0.79f,0.5f), mkStep(0,0.63f,1.0f), mkStep(12,1.0f,0.5f,true,true),
    REST, mkStep(0,0.79f,0.5f), REST, mkStep(0,0.79f,0.5f),
    mkStep(0,0.63f,1.0f), mkStep(3,1.0f,1.0f,true,true), mkStep(3,0.63f,1.0f), mkStep(-5,0.63f,0.5f,true,true),
    mkStep(0,1.0f,0.5f,true,true), REST, mkStep(12,0.63f,0.5f), REST,
};
static constexpr T5ynthStepSequencer::Step P_DUB_TECHNO[] = {
    mkStep(0,0.71f,0.1f), REST, REST, REST, REST, mkStep(0,0.86f,0.1f), REST, REST,
    REST, REST, mkStep(0,0.55f,0.1f), REST, REST, REST, REST, REST,
};
static constexpr T5ynthStepSequencer::Step P_AMBIENT[] = {
    mkStep(0,0.45f,1.0f), mkStep(0,0.48f,1.0f), mkStep(2,0.5f,1.0f), mkStep(2,0.53f,1.0f),
    mkStep(7,0.55f,1.0f), mkStep(7,0.6f,1.0f), mkStep(5,0.63f,1.0f), mkStep(5,0.65f,1.0f),
    mkStep(11,0.7f,1.0f), mkStep(11,0.73f,1.0f), mkStep(14,0.75f,1.0f), mkStep(14,0.78f,1.0f),
    mkStep(12,0.8f,1.0f), mkStep(7,0.7f,1.0f), mkStep(2,0.55f,1.0f), mkStep(0,0.4f,1.0f),
};
static constexpr T5ynthStepSequencer::Step P_IDM_GLITCH[] = {
    mkStep(6,1.0f,0.04f), mkStep(-8,0.3f,1.0f), mkStep(17,0.95f,0.12f), REST,
    mkStep(-1,0.7f,0.7f), mkStep(23,0.4f,0.03f), mkStep(-11,1.0f,0.5f), mkStep(8,0.55f,0.85f),
    REST, mkStep(-4,0.9f,1.0f),
};
static constexpr T5ynthStepSequencer::Step P_SOLAR[] = {
    mkStep(0,0.9f,0.5f), mkStep(1,0.9f,0.5f), mkStep(0,0.9f,0.5f), mkStep(0,0.9f,0.5f),
    mkStep(5,0.9f,0.5f), mkStep(6,0.9f,0.5f), mkStep(5,0.9f,0.5f), mkStep(5,0.9f,0.5f),
    mkStep(7,0.9f,0.5f), mkStep(8,0.9f,0.5f), mkStep(7,0.9f,0.5f), mkStep(7,0.9f,0.5f),
    mkStep(11,0.9f,0.5f), mkStep(12,0.9f,0.5f), mkStep(11,0.9f,0.5f), mkStep(10,0.9f,0.5f),
};
static constexpr T5ynthStepSequencer::Step P_ARPEGGIO_BASS[] = {
    mkStep(0,0.8f,0.5f), mkStep(7,0.65f,0.45f), mkStep(0,0.8f,0.5f), mkStep(6,0.65f,0.45f),
    mkStep(0,0.8f,0.5f), mkStep(7,0.65f,0.45f), mkStep(0,0.8f,0.5f), mkStep(10,0.7f,0.5f),
    mkStep(0,0.85f,0.5f), mkStep(7,0.7f,0.45f), mkStep(0,0.85f,0.5f), mkStep(6,0.7f,0.45f),
    mkStep(0,0.85f,0.5f), mkStep(7,0.7f,0.45f), mkStep(0,0.85f,0.5f), mkStep(11,0.75f,0.55f),
    mkStep(0,0.9f,0.55f), mkStep(7,0.75f,0.5f), mkStep(12,0.9f,0.55f), mkStep(6,0.75f,0.5f),
    mkStep(0,0.9f,0.55f), mkStep(7,0.75f,0.5f), mkStep(12,0.9f,0.55f), mkStep(10,0.8f,0.55f),
    mkStep(0,0.95f,0.6f), mkStep(7,0.8f,0.5f), mkStep(12,0.95f,0.6f), mkStep(6,0.8f,0.5f),
    mkStep(0,1.0f,0.6f), mkStep(11,0.85f,0.55f), mkStep(7,1.0f,0.6f), REST,
};
static constexpr T5ynthStepSequencer::Step P_TRANCE_GATE[] = {
    mkStep(0,1.0f,0.9f), mkStep(0,0.3f,0.15f), mkStep(0,0.85f,0.7f), mkStep(0,0.5f,0.3f),
    mkStep(0,1.0f,0.9f), REST, mkStep(0,0.9f,0.6f), mkStep(0,0.4f,0.2f),
};
// clang-format on

const T5ynthStepSequencer::PresetData T5ynthStepSequencer::presetTable[NUM_PRESETS] = {
    { "eastcoast",    P_EASTCOAST,    16 },
    { "westcoast",    P_WESTCOAST,     5 },
    { "synthwave",    P_SYNTHWAVE,    16 },
    { "techno",       P_TECHNO,       16 },
    { "dub_techno",   P_DUB_TECHNO,   16 },
    { "ambient",      P_AMBIENT,      16 },
    { "idm_glitch",   P_IDM_GLITCH,   10 },
    { "solar",        P_SOLAR,        16 },
    { "arpeggio_bass",P_ARPEGGIO_BASS,32 },
    { "trance_gate",  P_TRANCE_GATE,   8 },
};

// ─── Implementation ────────────────────────────────────────────────────────

double T5ynthStepSequencer::stepDurationSamples() const
{
    // samplesPerStep = sampleRate * (60 / bpm) * divisionFactor
    return sampleRate * 60.0 / bpm * static_cast<double>(DIVISION_FACTORS[division]);
}

void T5ynthStepSequencer::prepare(double sr, int /*samplesPerBlock*/)
{
    sampleRate = sr;
    samplesUntilNextStep = 0.0;
    samplesUntilGateOff = -1.0;
}

void T5ynthStepSequencer::processBlock(juce::AudioBuffer<float>& buffer,
                                       juce::MidiBuffer& midi)
{
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

    const double stepDur = stepDurationSamples();
    const int numSamples = buffer.getNumSamples();
    int samplePos = 0;

    while (samplePos < numSamples)
    {
        // Check gate-off before next step
        if (samplesUntilGateOff >= 0.0 && samplesUntilGateOff <= samplesUntilNextStep)
        {
            int gateOffPos = samplePos + static_cast<int>(samplesUntilGateOff);
            gateOffPos = juce::jmin(gateOffPos, numSamples - 1);
            if (lastPlayedNote >= 0)
            {
                midi.addEvent(juce::MidiMessage::noteOff(1, lastPlayedNote), gateOffPos);
                lastPlayedNote = -1;
            }
            samplesUntilNextStep -= samplesUntilGateOff;
            samplePos += static_cast<int>(samplesUntilGateOff);
            samplesUntilGateOff = -1.0;
            continue;
        }

        if (samplesUntilNextStep <= 0.0)
        {
            // Bar boundary detection
            int stepIdx = scheduledStep % numSteps;
            if (stepIdx == 0 && scheduledStep > 0)
                barStartFlag.store(true, std::memory_order_relaxed);

            auto& step = steps[static_cast<size_t>(stepIdx)];

            // Note-off for previous (if gate didn't already end it)
            if (lastPlayedNote >= 0)
            {
                midi.addEvent(juce::MidiMessage::noteOff(1, lastPlayedNote), samplePos);
                lastPlayedNote = -1;
            }

            // Note-on for current step if enabled
            if (step.enabled)
            {
                int vel = juce::jlimit(1, 127, juce::roundToInt(step.velocity * 127.0f));

                // Encode glide flag in MIDI channel: ch1 = normal, ch2 = glide
                int channel = step.glide ? 2 : 1;
                midi.addEvent(juce::MidiMessage::noteOn(channel, step.note,
                              static_cast<juce::uint8>(vel)), samplePos);
                lastPlayedNote = step.note;

                // Schedule gate-off at step.gate * stepDuration
                samplesUntilGateOff = step.gate * stepDur;
            }
            else
            {
                samplesUntilGateOff = -1.0;
            }

            currentStep = stepIdx;
            currentStepForGui.store(currentStep, std::memory_order_relaxed);

            scheduledStep++;
            samplesUntilNextStep += stepDur;
        }

        // Advance by minimum of remaining step time and gate-off time
        double advance = samplesUntilNextStep;
        if (samplesUntilGateOff >= 0.0)
            advance = std::min(advance, samplesUntilGateOff);
        int samplesToProcess = juce::jmin(numSamples - samplePos,
                                          static_cast<int>(std::ceil(advance)));
        samplesToProcess = std::max(1, samplesToProcess);
        samplesUntilNextStep -= samplesToProcess;
        if (samplesUntilGateOff >= 0.0)
            samplesUntilGateOff -= samplesToProcess;
        samplePos += samplesToProcess;
    }
}

void T5ynthStepSequencer::stop()
{
    running = false;
    // Don't clear lastPlayedNote here — processBlock() needs it to emit noteOff.
    // processBlock() will clear it after sending the noteOff event.
    samplesUntilGateOff = -1.0;
    currentStep = 0;
    scheduledStep = 0;
    currentStepForGui.store(-1, std::memory_order_relaxed);
}

void T5ynthStepSequencer::reset()
{
    stop();
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

void T5ynthStepSequencer::setStepGate(int step, float gate)
{
    if (step >= 0 && step < MAX_STEPS)
        steps[static_cast<size_t>(step)].gate = juce::jlimit(0.1f, 1.0f, gate);
}

void T5ynthStepSequencer::setStepGlide(int step, bool glide)
{
    if (step >= 0 && step < MAX_STEPS)
        steps[static_cast<size_t>(step)].glide = glide;
}

void T5ynthStepSequencer::setAllGates(float gate)
{
    float g = juce::jlimit(0.1f, 1.0f, gate);
    for (auto& s : steps)
        s.gate = g;
}

void T5ynthStepSequencer::loadPreset(int index)
{
    if (index < 0 || index >= NUM_PRESETS) return;

    const auto& preset = presetTable[index];
    numSteps = preset.count;

    for (int i = 0; i < MAX_STEPS; ++i)
    {
        if (i < preset.count)
            steps[static_cast<size_t>(i)] = preset.steps[i];
        else
            steps[static_cast<size_t>(i)] = { 60, 0.8f, 0.8f, false, false };
    }
}

void T5ynthStepSequencer::resetGrid()
{
    for (auto& s : steps)
        s = { 60, 0.8f, 0.8f, true, false };
}
