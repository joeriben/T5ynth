#include "GenerativeSequencer.h"
#include "EuclideanRhythm.h"
#include "ScaleQuantizer.h"

// ─── Timing ────────────────────────────────────────────────────────────────

double T5ynthGenerativeSequencer::stepDurationSamples() const
{
    return sampleRate_ * 60.0 / bpm_ * static_cast<double>(DIVISION_FACTORS[division_]);
}

constexpr float T5ynthGenerativeSequencer::DIVISION_FACTORS[];

void T5ynthGenerativeSequencer::prepare(double sr, int /*samplesPerBlock*/)
{
    sampleRate_ = sr;
    samplesUntilNextStep = 0.0;
    samplesUntilGateOff = -1.0;
}

// ─── Parameter setters ─────────────────────────────────────────────────────

void T5ynthGenerativeSequencer::setSteps(int s)
{
    s = juce::jlimit(2, MAX_STEPS, s);
    if (s != numSteps) { numSteps = s; patternDirty = true; }
    effectiveStepsForGui.store(numSteps, std::memory_order_relaxed);
}

void T5ynthGenerativeSequencer::setPulses(int p)
{
    p = juce::jlimit(1, numSteps, p);
    if (p != numPulses) { numPulses = p; patternDirty = true; }
    effectivePulsesForGui.store(numPulses, std::memory_order_relaxed);
}

void T5ynthGenerativeSequencer::setRotation(int r)
{
    r = numSteps > 0 ? ((r % numSteps) + numSteps) % numSteps : 0;
    if (r != rotation) { rotation = r; patternDirty = true; }
}

void T5ynthGenerativeSequencer::setMutation(float rate)
{
    mutationRate = juce::jlimit(0.0f, 1.0f, rate);
    effectiveMutationForGui.store(mutationRate, std::memory_order_relaxed);
}

void T5ynthGenerativeSequencer::setRange(int octaves)
{
    octaves = juce::jlimit(1, 4, octaves);
    if (octaves != rangeOctaves) { rangeOctaves = octaves; patternDirty = true; }
}

void T5ynthGenerativeSequencer::setScale(int type, int root)
{
    // If Off, default to Major for generative mode
    if (type <= 0) type = 1;
    if (type != scaleType || root != scaleRoot)
    {
        scaleType = type;
        scaleRoot = root;
        patternDirty = true;
    }
}

void T5ynthGenerativeSequencer::setGate(float g) { gate_ = juce::jlimit(0.1f, 1.0f, g); }
void T5ynthGenerativeSequencer::setBpm(double b) { bpm_ = b; }
void T5ynthGenerativeSequencer::setDivision(int d) { division_ = juce::jlimit(0, 4, d); }

void T5ynthGenerativeSequencer::setFixSteps(bool f)    { fixSteps = f; }
void T5ynthGenerativeSequencer::setFixPulses(bool f)   { fixPulses = f; }
void T5ynthGenerativeSequencer::setFixRotation(bool f) { fixRotation = f; }
void T5ynthGenerativeSequencer::setFixMutation(bool f) { fixMutation = f; }

void T5ynthGenerativeSequencer::setBaseMutation(float rate)
{
    baseMutation = juce::jlimit(0.0f, 1.0f, rate);
    if (fixMutation)
        mutationRate = baseMutation;
}

// ─── Start / Stop ──────────────────────────────────────────────────────────

void T5ynthGenerativeSequencer::start()
{
    if (!running)
    {
        running = true;
        samplesUntilNextStep = 0.0;
        currentStep = 0;
        scheduledStep = 0;
        cycleCount = 0;
        if (!patternSeeded)
            patternDirty = true;  // rebuild on start (unless seeded)
        patternSeeded = false;
    }
}

void T5ynthGenerativeSequencer::stop()
{
    running = false;
    samplesUntilGateOff = -1.0;
    currentStep = 0;
    scheduledStep = 0;
    currentStepForGui.store(-1, std::memory_order_relaxed);
}

void T5ynthGenerativeSequencer::reset()
{
    stop();
}

// ─── Pattern generation ────────────────────────────────────────────────────

void T5ynthGenerativeSequencer::computeGaps(int* gaps, int* gapCount) const
{
    // Find gaps between consecutive pulses (circular)
    *gapCount = 0;
    int firstPulse = -1;
    int prevPulse = -1;

    for (int i = 0; i < numSteps; ++i)
    {
        if (eucPattern[static_cast<size_t>(i)])
        {
            if (firstPulse < 0) firstPulse = i;
            if (prevPulse >= 0)
                gaps[(*gapCount)++] = i - prevPulse;
            prevPulse = i;
        }
    }
    // Wrap-around gap: from last pulse to first pulse
    if (firstPulse >= 0 && prevPulse >= 0 && *gapCount > 0)
        gaps[(*gapCount)++] = numSteps - prevPulse + firstPulse;
    else if (firstPulse >= 0)
        gaps[(*gapCount)++] = numSteps; // single pulse
}

void T5ynthGenerativeSequencer::rebuildPattern()
{
    // 1. Generate Euclidean rhythm
    auto fullPattern = EuclideanRhythm::generate(numSteps, numPulses, rotation);
    for (int i = 0; i < MAX_STEPS; ++i)
        eucPattern[static_cast<size_t>(i)] = (i < numSteps) && fullPattern[static_cast<size_t>(i)];

    // 2. Compute gaps between pulses → interval jumps
    int gaps[MAX_STEPS];
    int gapCount = 0;
    computeGaps(gaps, &gapCount);

    // 3. Build scale degrees for melody
    auto scale = static_cast<ScaleQuantizer::Scale>(
        juce::jlimit(1, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
    int dpOct = ScaleQuantizer::degreesPerOctave(scale);
    int totalDegrees = dpOct * rangeOctaves;
    int baseNote = 48; // C3

    // 4. Assign notes using reflected stride walk (zigzag through the scale)
    //    Stride derived from Euclidean params (numSteps - numPulses).
    //    Triangle-wave traversal: ascends then descends through the range,
    //    creating bidirectional melodic motion instead of monotonic climbing.
    int stride = juce::jmax(1, numSteps - numPulses);
    // Ensure coprime with totalDegrees for maximum coverage
    while (stride > 1 && totalDegrees % stride == 0)
        stride++;
    if (stride >= totalDegrees) stride = 1;

    int pulseIdx = 0;
    notePattern.fill(0);
    velocityPattern.fill(0.0f);
    degreePattern.fill(0);

    // Triangle wave period: up totalDegrees-1 steps, then down totalDegrees-1 steps
    int period = juce::jmax(2, 2 * (totalDegrees - 1));

    for (int i = 0; i < numSteps; ++i)
    {
        if (eucPattern[static_cast<size_t>(i)])
        {
            // Reflected zigzag: fold position into [0, totalDegrees-1]
            int raw = pulseIdx * stride;
            int pos = raw % period;
            int degree = (pos < totalDegrees) ? pos : (period - pos);
            degree = juce::jlimit(0, totalDegrees - 1, degree);

            int midiNote = ScaleQuantizer::degreeToMidi(degree, scaleRoot, scale, baseNote);
            notePattern[static_cast<size_t>(i)] = midiNote;
            degreePattern[static_cast<size_t>(i)] = degree;

            // Velocity: accent based on gap length
            float gapFrac = (gapCount > 0)
                ? static_cast<float>(gaps[pulseIdx % gapCount]) / static_cast<float>(numSteps)
                : 0.5f;
            velocityPattern[static_cast<size_t>(i)] = juce::jlimit(0.4f, 1.0f, 0.55f + gapFrac * 0.6f);
            pulseIdx++;
        }
    }

    // Warm-up: scramble the deterministic stride walk so the initial
    // pattern is never a boring ascending scale
    for (int w = 0; w < 4; ++w)
        mutateNotes(1.0f, totalDegrees, static_cast<int>(scale), baseNote);

    patternDirty = false;
    basePulses = numPulses;
    baseSteps = numSteps;
    driftCycle = 0;
    pulseDriftAccum = 0;
    pulseDriftUp = true;
    stepsDriftAccum = 0;
    stepsDriftUp = true;
    mutationDriftPhase = 0;
    publishPatternToGui();
}

void T5ynthGenerativeSequencer::mutateNotes(float rate, int totalDegrees,
                                             int scaleEnum, int baseNote)
{
    auto scale = static_cast<ScaleQuantizer::Scale>(scaleEnum);
    int pulseIndices[MAX_STEPS];
    int pulseCount = 0;
    for (int i = 0; i < numSteps; ++i)
        if (eucPattern[static_cast<size_t>(i)])
            pulseIndices[pulseCount++] = i;

    int numMutations = juce::jmax(1, juce::roundToInt(
        rate * static_cast<float>(pulseCount) * 0.6f));

    if (pulseCount > 0)
    {
        std::uniform_int_distribution<int> pickDist(0, pulseCount - 1);
        std::uniform_int_distribution<int> jumpDist(1, 4);
        std::uniform_int_distribution<int> dirDist(0, 1);

        for (int m = 0; m < numMutations; ++m)
        {
            int idx = pulseIndices[pickDist(rng)];
            int dir = dirDist(rng) == 0 ? -1 : 1;
            int jump = jumpDist(rng);
            int newDeg = degreePattern[static_cast<size_t>(idx)] + dir * jump;
            newDeg = ((newDeg % totalDegrees) + totalDegrees) % totalDegrees;
            degreePattern[static_cast<size_t>(idx)] = newDeg;
            notePattern[static_cast<size_t>(idx)] =
                ScaleQuantizer::degreeToMidi(newDeg, scaleRoot, scale, baseNote);
        }
    }
}

void T5ynthGenerativeSequencer::mutatePattern()
{
    if (mutationRate <= 0.0f) return;

    auto scale = static_cast<ScaleQuantizer::Scale>(
        juce::jlimit(1, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
    int dpOct = ScaleQuantizer::degreesPerOctave(scale);
    int totalDegrees = dpOct * rangeOctaves;
    int baseNote = 48;

    // ── Euclidean drift: rotation + pulse count evolve per cycle ──
    applyEuclideanDrift();

    // ── Note mutation: Turing Machine — mutate notes per cycle ──
    mutateNotes(mutationRate, totalDegrees, static_cast<int>(scale), baseNote);

    publishPatternToGui();
}

// ─── Euclidean drift ────────────────────────────────────────────────────────

void T5ynthGenerativeSequencer::applyEuclideanDrift()
{
    driftCycle++;
    int driftIdx = (driftCycle - 1) % DRIFT_PERIOD;

    // ── Rotation drift: Euclidean-distributed circular shifts ──
    if (!fixRotation && numSteps > 1)
    {
        int rotPulses = juce::jmax(0, juce::roundToInt(
            mutationRate * static_cast<float>(DRIFT_PERIOD)));
        auto rotDrift = EuclideanRhythm::generate(DRIFT_PERIOD, rotPulses, 0);
        if (rotDrift[static_cast<size_t>(driftIdx)])
        {
            auto shiftLeft = [&](auto& arr) {
                auto first = arr[0];
                for (int i = 0; i < numSteps - 1; ++i)
                    arr[static_cast<size_t>(i)] = arr[static_cast<size_t>(i + 1)];
                arr[static_cast<size_t>(numSteps - 1)] = first;
            };
            shiftLeft(eucPattern);
            shiftLeft(notePattern);
            shiftLeft(velocityPattern);
            shiftLeft(degreePattern);
        }
    }

    // ── Pulse count drift ──
    if (!fixPulses)
    {
        int pulseDriftPulses = juce::jmax(0, juce::roundToInt(
            mutationRate * static_cast<float>(DRIFT_PERIOD) * 0.5f));
        auto pulseDrift = EuclideanRhythm::generate(DRIFT_PERIOD, pulseDriftPulses, 0);
        if (pulseDrift[static_cast<size_t>(driftIdx)])
        {
            if (pulseDriftUp)
                addPulse();
            else
                removePulse();
            pulseDriftAccum += pulseDriftUp ? 1 : -1;

            int maxDrift = juce::jmax(1, rangeOctaves);
            if (pulseDriftAccum >= maxDrift) pulseDriftUp = false;
            else if (pulseDriftAccum <= -maxDrift) pulseDriftUp = true;
        }
    }

    // ── Steps drift ──
    if (!fixSteps)
        applyStepsDrift();

    // ── Mutation drift ──
    if (!fixMutation)
        applyMutationDrift();
}

// ─── Steps drift ────────────────────────────────────────────────────────────

void T5ynthGenerativeSequencer::applyStepsDrift()
{
    int driftIdx = (driftCycle - 1) % DRIFT_PERIOD;

    int stepsDriftPulses = juce::jmax(0, juce::roundToInt(
        mutationRate * static_cast<float>(DRIFT_PERIOD) * 0.35f));
    auto stepsDrift = EuclideanRhythm::generate(DRIFT_PERIOD, stepsDriftPulses, 0);

    if (!stepsDrift[static_cast<size_t>(driftIdx)])
        return;

    int delta = stepsDriftUp ? 1 : -1;
    if (mutationRate > 0.7f)
    {
        std::uniform_int_distribution<int> magDist(1, 2);
        delta *= magDist(rng);
    }

    int newSteps = juce::jlimit(2, MAX_STEPS, numSteps + delta);
    if (newSteps == numSteps) return;

    // Preserve pulse density ratio
    float ratio = static_cast<float>(numPulses) / static_cast<float>(numSteps);

    if (newSteps > numSteps)
    {
        for (int i = numSteps; i < newSteps; ++i)
        {
            eucPattern[static_cast<size_t>(i)] = false;
            notePattern[static_cast<size_t>(i)] = 0;
            velocityPattern[static_cast<size_t>(i)] = 0.0f;
            degreePattern[static_cast<size_t>(i)] = 0;
        }
    }

    int newPulses;
    if (newSteps < numSteps)
    {
        int surviving = 0;
        for (int i = 0; i < newSteps; ++i)
            if (eucPattern[static_cast<size_t>(i)]) surviving++;
        newPulses = juce::jmax(1, surviving);
    }
    else
    {
        newPulses = juce::jlimit(1, newSteps, juce::roundToInt(ratio * static_cast<float>(newSteps)));
    }

    numSteps = newSteps;
    numPulses = newPulses;
    stepsDriftAccum += (delta > 0) ? 1 : -1;

    int maxDrift = juce::jmax(1, rangeOctaves);
    if (stepsDriftAccum >= maxDrift) stepsDriftUp = false;
    else if (stepsDriftAccum <= -maxDrift) stepsDriftUp = true;
}

// ─── Mutation drift ─────────────────────────────────────────────────────────

void T5ynthGenerativeSequencer::applyMutationDrift()
{
    mutationDriftPhase = (mutationDriftPhase + 1) % (DRIFT_PERIOD * 2);
    float phase = static_cast<float>(mutationDriftPhase) / static_cast<float>(DRIFT_PERIOD * 2);
    float sinVal = std::sin(phase * juce::MathConstants<float>::twoPi);
    // Map sin [-1..+1] to [0.5..1.0] multiplier — breathes down, never exceeds user setting
    float multiplier = 0.75f + 0.25f * sinVal;
    // Additive floor (5%) prevents unfixed mutation from getting permanently stuck at 0
    float floor = fixMutation ? 0.0f : 0.05f;
    mutationRate = juce::jlimit(0.0f, 1.0f, std::max(floor, baseMutation * multiplier));
}

// ─── Surgical pulse add/remove (preserves Turing mutations) ─────────────────

void T5ynthGenerativeSequencer::addPulse()
{
    if (numPulses >= numSteps) return;

    auto scale = static_cast<ScaleQuantizer::Scale>(
        juce::jlimit(1, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
    int dpOct = ScaleQuantizer::degreesPerOctave(scale);
    int totalDegrees = dpOct * rangeOctaves;
    int baseNote = 48;

    // Find the longest gap between existing pulses → insert in its middle
    int bestStart = -1, bestGap = 0;
    for (int i = 0; i < numSteps; ++i)
    {
        if (!eucPattern[static_cast<size_t>(i)]) continue;
        int gap = 0;
        for (int j = 1; j < numSteps; ++j)
        {
            if (eucPattern[static_cast<size_t>((i + j) % numSteps)]) break;
            gap++;
        }
        if (gap > bestGap) { bestGap = gap; bestStart = i; }
    }
    if (bestStart < 0 || bestGap <= 0) return;

    int insertIdx = (bestStart + bestGap / 2 + 1) % numSteps;
    eucPattern[static_cast<size_t>(insertIdx)] = true;

    // Interpolate degree from neighbouring pulses + slight jitter
    int prevDeg = degreePattern[static_cast<size_t>(bestStart)];
    int nextIdx = (bestStart + bestGap + 1) % numSteps;
    int nextDeg = eucPattern[static_cast<size_t>(nextIdx)]
                ? degreePattern[static_cast<size_t>(nextIdx)] : prevDeg;
    std::uniform_int_distribution<int> jitter(-1, 1);
    int newDeg = ((prevDeg + nextDeg) / 2 + jitter(rng) + totalDegrees) % totalDegrees;

    degreePattern[static_cast<size_t>(insertIdx)] = newDeg;
    notePattern[static_cast<size_t>(insertIdx)] =
        ScaleQuantizer::degreeToMidi(newDeg, scaleRoot, scale, baseNote);
    velocityPattern[static_cast<size_t>(insertIdx)] = 0.6f;
    numPulses++;
}

void T5ynthGenerativeSequencer::removePulse()
{
    if (numPulses <= 1) return;

    // Remove the pulse with the smallest surrounding gap (most crowded)
    int bestIdx = -1, bestGap = numSteps + 1;
    for (int i = 0; i < numSteps; ++i)
    {
        if (!eucPattern[static_cast<size_t>(i)]) continue;
        int gapBefore = 0, gapAfter = 0;
        for (int j = 1; j < numSteps; ++j)
        {
            if (eucPattern[static_cast<size_t>((i - j + numSteps) % numSteps)]) break;
            gapBefore++;
        }
        for (int j = 1; j < numSteps; ++j)
        {
            if (eucPattern[static_cast<size_t>((i + j) % numSteps)]) break;
            gapAfter++;
        }
        int totalGap = gapBefore + gapAfter;
        if (totalGap < bestGap) { bestGap = totalGap; bestIdx = i; }
    }
    if (bestIdx < 0) return;

    eucPattern[static_cast<size_t>(bestIdx)] = false;
    notePattern[static_cast<size_t>(bestIdx)] = 0;
    degreePattern[static_cast<size_t>(bestIdx)] = 0;
    velocityPattern[static_cast<size_t>(bestIdx)] = 0.0f;
    numPulses--;
}

// ─── Seed from step sequencer ───────────────────────────────────────────────

void T5ynthGenerativeSequencer::seedFromSteps(const int* midiNotes,
                                               const bool* enabled,
                                               int count)
{
    count = juce::jlimit(2, MAX_STEPS, count);
    numSteps = count;

    // Count active steps → pulses
    int pulseCount = 0;
    for (int i = 0; i < count; ++i)
        if (enabled[i]) pulseCount++;
    numPulses = juce::jmax(1, pulseCount);

    eucPattern.fill(false);
    notePattern.fill(0);
    velocityPattern.fill(0.0f);
    degreePattern.fill(0);

    auto scale = static_cast<ScaleQuantizer::Scale>(
        juce::jlimit(1, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
    int dpOct = ScaleQuantizer::degreesPerOctave(scale);
    int baseNote = 48;

    for (int i = 0; i < count; ++i)
    {
        eucPattern[static_cast<size_t>(i)] = enabled[i];
        if (enabled[i] && midiNotes[i] > 0)
        {
            notePattern[static_cast<size_t>(i)] = midiNotes[i];
            velocityPattern[static_cast<size_t>(i)] = 0.75f;

            // Reverse-map MIDI → scale degree for mutation
            int rel = midiNotes[i] - baseNote - scaleRoot;
            int octave = rel / 12;
            int semi = ((rel % 12) + 12) % 12;
            int bestDeg = 0, bestDist = 99;
            for (int d = 0; d < dpOct; ++d)
            {
                int dist = std::abs(ScaleQuantizer::kScaleDegrees[scale][d] - semi);
                if (dist < bestDist) { bestDist = dist; bestDeg = d; }
            }
            degreePattern[static_cast<size_t>(i)] = octave * dpOct + bestDeg;
        }
    }

    basePulses = numPulses;
    driftCycle = 0;
    pulseDriftAccum = 0;
    pulseDriftUp = true;
    rotation = 0;
    patternDirty = false;
    patternSeeded = true;
    publishPatternToGui();
}

void T5ynthGenerativeSequencer::publishPatternToGui()
{
    numStepsForGui.store(numSteps, std::memory_order_relaxed);
    effectiveStepsForGui.store(numSteps, std::memory_order_relaxed);
    effectivePulsesForGui.store(numPulses, std::memory_order_relaxed);
    effectiveMutationForGui.store(mutationRate, std::memory_order_relaxed);
    for (int i = 0; i < MAX_STEPS; ++i)
        notePatternForGui[static_cast<size_t>(i)].store(
            i < numSteps ? notePattern[static_cast<size_t>(i)] : 0,
            std::memory_order_relaxed);
}

// ─── Process Block ─────────────────────────────────────────────────────────

void T5ynthGenerativeSequencer::processBlock(juce::AudioBuffer<float>& buffer,
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

    if (patternDirty)
        rebuildPattern();

    const double stepDur = stepDurationSamples();
    const int numSamples = buffer.getNumSamples();
    int samplePos = 0;

    std::uniform_real_distribution<float> probDist(0.0f, 1.0f);

    while (samplePos < numSamples)
    {
        int remaining = numSamples - samplePos;

        double nextEvent = samplesUntilNextStep;
        bool gateOffFirst = false;
        if (samplesUntilGateOff >= 0.0 && samplesUntilGateOff <= samplesUntilNextStep)
        {
            nextEvent = samplesUntilGateOff;
            gateOffFirst = true;
        }

        if (nextEvent > static_cast<double>(remaining))
        {
            samplesUntilNextStep -= remaining;
            if (samplesUntilGateOff >= 0.0)
                samplesUntilGateOff -= remaining;
            samplePos = numSamples;
            break;
        }

        int advance = std::max(0, static_cast<int>(nextEvent));
        samplePos += advance;
        samplesUntilNextStep -= advance;
        if (samplesUntilGateOff >= 0.0)
            samplesUntilGateOff -= advance;

        int eventPos = juce::jmin(samplePos, numSamples - 1);

        if (gateOffFirst)
        {
            if (lastPlayedNote >= 0)
            {
                midi.addEvent(juce::MidiMessage::noteOff(1, lastPlayedNote), eventPos);
                lastPlayedNote = -1;
            }
            samplesUntilGateOff = -1.0;
            continue;
        }

        // Step boundary
        int stepIdx = scheduledStep % numSteps;

        // Cycle boundary → mutate
        if (stepIdx == 0 && scheduledStep > 0)
            mutatePattern();

        // Note-off for previous
        if (lastPlayedNote >= 0)
        {
            midi.addEvent(juce::MidiMessage::noteOff(1, lastPlayedNote), eventPos);
            lastPlayedNote = -1;
        }

        // Determine if this step should fire
        bool isPulse = eucPattern[static_cast<size_t>(stepIdx)];
        float fireProb = isPulse
            ? (0.90f + 0.10f * (1.0f - mutationRate))   // pulse: high probability
            : (mutationRate * 0.15f);                     // rest: ghost note probability

        bool shouldFire = (probDist(rng) < fireProb)
                       && notePattern[static_cast<size_t>(stepIdx)] > 0;

        if (shouldFire)
        {
            int note = notePattern[static_cast<size_t>(stepIdx)];
            float vel = velocityPattern[static_cast<size_t>(stepIdx)];

            // Ghost notes are quieter
            if (!isPulse) vel *= 0.4f;

            int velInt = juce::jlimit(1, 127, juce::roundToInt(vel * 127.0f));
            midi.addEvent(juce::MidiMessage::noteOn(1, note,
                          static_cast<juce::uint8>(velInt)), eventPos);
            lastPlayedNote = note;
            samplesUntilGateOff = static_cast<double>(gate_) * stepDur;
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
