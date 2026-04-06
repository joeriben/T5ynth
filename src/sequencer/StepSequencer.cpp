#include "StepSequencer.h"

// ─── Preset patterns (exact port from useStepSequencer.ts) ─────────────────

static constexpr T5ynthStepSequencer::Step mkStep(int semi, float vel, float gate, bool active = true, bool bind = false)
{
    return { 60 + semi, vel, gate, active, bind };
}
static constexpr T5ynthStepSequencer::Step REST = { 60, 0.0f, 0.0f, false, false };

// clang-format off

// 1. Octave Bounce — root bouncing against octave, scale tones between
//    Even steps: scale tones (1,3,5,6); odd steps: octave (12)
static constexpr T5ynthStepSequencer::Step P_OCTAVE_BOUNCE[] = {
    mkStep(0,0.90f,0.4f),  mkStep(12,0.70f,0.25f), mkStep(4,0.75f,0.4f),  mkStep(12,0.65f,0.25f),
    mkStep(7,0.85f,0.4f),  mkStep(12,0.70f,0.25f), mkStep(9,0.75f,0.4f),  mkStep(12,0.65f,0.25f),
    mkStep(0,0.90f,0.4f),  mkStep(12,0.70f,0.25f), mkStep(4,0.75f,0.4f),  mkStep(12,0.65f,0.25f),
    mkStep(7,0.85f,0.4f),  mkStep(12,0.70f,0.25f), mkStep(10,0.80f,0.5f), mkStep(12,0.65f,0.25f),
};

// 2. Wide Leap — every interval ≥ P4 (5 semitones), alternating up/down
static constexpr T5ynthStepSequencer::Step P_WIDE_LEAP[] = {
    mkStep(0,0.85f,0.5f),   mkStep(7,0.75f,0.5f),   mkStep(-12,0.90f,0.5f), mkStep(9,0.70f,0.5f),
    mkStep(-9,0.80f,0.5f),  mkStep(11,0.75f,0.5f),  mkStep(-7,0.85f,0.5f),  mkStep(12,0.70f,0.5f),
    mkStep(0,0.90f,0.5f),   mkStep(8,0.75f,0.5f),   mkStep(-10,0.80f,0.5f), mkStep(10,0.70f,0.5f),
    mkStep(-8,0.85f,0.5f),  mkStep(7,0.75f,0.5f),   mkStep(-14,0.90f,0.5f), mkStep(12,0.70f,0.5f),
};

// 3. Off-Beat Minor — natural minor (0,2,3,5,7,8,10) on odd steps only
static constexpr T5ynthStepSequencer::Step P_OFFBEAT_MINOR[] = {
    REST, mkStep(0,0.80f,0.4f),  REST, mkStep(3,0.85f,0.4f),
    REST, mkStep(5,0.80f,0.4f),  REST, mkStep(7,0.90f,0.4f),
    REST, mkStep(8,0.75f,0.4f),  REST, mkStep(10,0.85f,0.4f),
    REST, mkStep(12,0.90f,0.4f), REST, mkStep(8,0.80f,0.4f),
};

// 4. Glide Groove — stepwise motion with bind/glide between connected notes
static constexpr T5ynthStepSequencer::Step P_GLIDE_GROOVE[] = {
    mkStep(0,0.85f,0.8f), mkStep(2,0.70f,0.8f,true,true),  mkStep(3,0.70f,0.8f,true,true), mkStep(7,0.90f,0.6f),
    mkStep(5,0.70f,0.8f,true,true), mkStep(3,0.80f,0.6f),  mkStep(0,0.70f,0.8f,true,true), mkStep(2,0.70f,0.8f,true,true),
    mkStep(3,0.85f,0.8f), mkStep(5,0.70f,0.8f,true,true),  mkStep(7,0.70f,0.8f,true,true), mkStep(8,0.90f,0.6f),
    mkStep(10,0.75f,0.8f,true,true), mkStep(8,0.70f,0.8f,true,true), mkStep(7,0.80f,0.6f), mkStep(5,0.70f,0.8f,true,true),
};

// 5. Sparse Stab — 4 notes in 16 steps, short gates (staccato stabs)
static constexpr T5ynthStepSequencer::Step P_SPARSE_STAB[] = {
    mkStep(0,1.00f,0.1f), REST, REST, REST,
    mkStep(7,0.85f,0.1f), REST, REST, REST,
    mkStep(3,0.90f,0.1f), REST, REST, REST,
    mkStep(10,0.80f,0.1f),REST, REST, REST,
};

// 6. Rising Arc — ascending concave curve: semi[i] = round(24·sin(π/2·i/15))
//    Velocity crescendo from p to ff mirrors the pitch arc
static constexpr T5ynthStepSequencer::Step P_RISING_ARC[] = {
    mkStep(0,0.50f,0.9f),  mkStep(3,0.53f,0.9f),  mkStep(5,0.57f,0.9f),  mkStep(7,0.60f,0.9f),
    mkStep(10,0.63f,0.9f), mkStep(12,0.67f,0.9f),  mkStep(14,0.70f,0.9f), mkStep(16,0.73f,0.9f),
    mkStep(18,0.77f,0.9f), mkStep(19,0.80f,0.9f),  mkStep(21,0.83f,0.9f), mkStep(22,0.87f,0.9f),
    mkStep(23,0.90f,0.9f), mkStep(23,0.93f,0.9f),  mkStep(24,0.97f,0.9f), mkStep(24,1.00f,0.9f),
};

// 7. Scatter — modular stride (i·7 mod 13 → degree, mapped to semitones),
//    every 4th step is a rest for rhythmic breath
static constexpr T5ynthStepSequencer::Step P_SCATTER[] = {
    mkStep(0,0.85f,0.4f),  mkStep(12,0.75f,0.4f), mkStep(2,0.80f,0.4f),  REST,
    mkStep(4,0.85f,0.4f),  mkStep(16,0.70f,0.4f), mkStep(5,0.80f,0.4f),  REST,
    mkStep(7,0.85f,0.4f),  mkStep(19,0.75f,0.4f), mkStep(9,0.80f,0.4f),  REST,
    mkStep(11,0.90f,0.4f), mkStep(0,0.70f,0.4f),  mkStep(12,0.80f,0.4f), REST,
};

// 8. Chromatic — strict half-steps: ascending chromatic scale, turning at octave
static constexpr T5ynthStepSequencer::Step P_CHROMATIC[] = {
    mkStep(0,0.80f,0.5f),  mkStep(1,0.75f,0.5f),  mkStep(2,0.80f,0.5f),  mkStep(3,0.75f,0.5f),
    mkStep(4,0.80f,0.5f),  mkStep(5,0.75f,0.5f),  mkStep(6,0.80f,0.5f),  mkStep(7,0.75f,0.5f),
    mkStep(8,0.80f,0.5f),  mkStep(9,0.75f,0.5f),  mkStep(10,0.80f,0.5f), mkStep(11,0.75f,0.5f),
    mkStep(12,0.90f,0.5f), mkStep(11,0.70f,0.5f), mkStep(10,0.70f,0.5f), mkStep(9,0.70f,0.5f),
};

// 9. Bass Walk — classic walking bass: root–3rd–5th–approach, low register (C3 base)
static constexpr T5ynthStepSequencer::Step P_BASS_WALK[] = {
    mkStep(-12,0.90f,0.5f), mkStep(-8,0.75f,0.5f),  mkStep(-5,0.80f,0.5f),  mkStep(-3,0.70f,0.5f),
    mkStep(-2,0.75f,0.5f),  mkStep(-5,0.80f,0.5f),  mkStep(-8,0.75f,0.5f),  mkStep(-10,0.70f,0.5f),
    mkStep(-12,0.90f,0.5f), mkStep(-10,0.75f,0.5f), mkStep(-8,0.80f,0.5f),  mkStep(-7,0.75f,0.5f),
    mkStep(-5,0.85f,0.5f),  mkStep(-3,0.75f,0.5f),  mkStep(-2,0.80f,0.5f),  mkStep(-1,0.70f,0.5f),
};

// 10. Gated Pulse — single pitch, rhythmic velocity gating (Euclidean feel)
static constexpr T5ynthStepSequencer::Step P_GATED_PULSE[] = {
    mkStep(0,1.00f,0.15f), mkStep(0,0.50f,0.15f), REST,               mkStep(0,0.80f,0.15f),
    mkStep(0,0.40f,0.15f), REST,                   mkStep(0,1.00f,0.15f), REST,
    mkStep(0,0.90f,0.15f), mkStep(0,0.45f,0.15f), REST,               mkStep(0,0.70f,0.15f),
    REST,                   mkStep(0,0.85f,0.15f), mkStep(0,0.50f,0.15f), REST,
};

// clang-format on

const T5ynthStepSequencer::PresetData T5ynthStepSequencer::presetTable[NUM_PRESETS] = {
    { "Octave Bounce",  P_OCTAVE_BOUNCE,  16 },
    { "Wide Leap",      P_WIDE_LEAP,      16 },
    { "Off-Beat Minor", P_OFFBEAT_MINOR,  16 },
    { "Glide Groove",   P_GLIDE_GROOVE,   16 },
    { "Sparse Stab",    P_SPARSE_STAB,    16 },
    { "Rising Arc",     P_RISING_ARC,     16 },
    { "Scatter",        P_SCATTER,        16 },
    { "Chromatic",      P_CHROMATIC,      16 },
    { "Bass Walk",      P_BASS_WALK,      16 },
    { "Gated Pulse",    P_GATED_PULSE,    16 },
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
        // How many samples remain in this block?
        int remaining = numSamples - samplePos;

        // Find the next event: step boundary or gate-off
        double nextEvent = samplesUntilNextStep;
        bool gateOffFirst = false;
        if (samplesUntilGateOff >= 0.0 && samplesUntilGateOff <= samplesUntilNextStep)
        {
            nextEvent = samplesUntilGateOff;
            gateOffFirst = true;
        }

        // If the next event is beyond this block, just advance to block end
        if (nextEvent > static_cast<double>(remaining))
        {
            samplesUntilNextStep -= remaining;
            if (samplesUntilGateOff >= 0.0)
                samplesUntilGateOff -= remaining;
            samplePos = numSamples;
            break;
        }

        // Advance to the event
        int advance = std::max(0, static_cast<int>(nextEvent));
        samplePos += advance;
        samplesUntilNextStep -= advance;
        if (samplesUntilGateOff >= 0.0)
            samplesUntilGateOff -= advance;

        int eventPos = juce::jmin(samplePos, numSamples - 1);

        if (gateOffFirst)
        {
            // Gate-off event
            if (lastPlayedNote >= 0)
            {
                midi.addEvent(juce::MidiMessage::noteOff(1, lastPlayedNote), eventPos);
                lastPlayedNote = -1;
            }
            samplesUntilGateOff = -1.0;
            continue;
        }

        // Step boundary event
        int stepIdx = scheduledStep % numSteps;
        if (stepIdx == 0 && scheduledStep > 0)
            barStartFlag.store(true, std::memory_order_relaxed);

        auto& step = steps[static_cast<size_t>(stepIdx)];

        // Note-off for previous — but SKIP if this step is a glide
        // (glide needs the previous voice alive to slide its pitch)
        if (lastPlayedNote >= 0 && !step.bind)
        {
            midi.addEvent(juce::MidiMessage::noteOff(1, lastPlayedNote), eventPos);
            lastPlayedNote = -1;
        }

        // Note-on for current step if enabled
        if (step.enabled)
        {
            int vel = juce::jlimit(1, 127, juce::roundToInt(step.velocity * 127.0f));
            int channel = step.bind ? 2 : 1;
            midi.addEvent(juce::MidiMessage::noteOn(channel, step.note,
                          static_cast<juce::uint8>(vel)), eventPos);
            lastPlayedNote = step.note;

            // If the NEXT step has bind, hold this note for the full step
            // (no early gate-off, so the voice stays alive for the pitch change)
            int nextIdx = (scheduledStep + 1) % numSteps;
            bool nextIsBind = steps[static_cast<size_t>(nextIdx)].bind
                           && steps[static_cast<size_t>(nextIdx)].enabled;
            samplesUntilGateOff = nextIsBind ? -1.0 : step.gate * stepDur;
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

void T5ynthStepSequencer::setStepBind(int step, bool bind)
{
    if (step >= 0 && step < MAX_STEPS)
        steps[static_cast<size_t>(step)].bind = bind;
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
