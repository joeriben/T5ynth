#include "GenerativeSequencer.h"
#include "EuclideanRhythm.h"
#include "ScaleQuantizer.h"

// ─── Timing ────────────────────────────────────────────────────────────────

T5ynthGenerativeSequencer::T5ynthGenerativeSequencer()
{
    strands[0].enabled = true;   // strand 0 always active; 1..3 are opt-in
}

double T5ynthGenerativeSequencer::stepDurationSamples() const
{
    return sampleRate_ * 60.0 / bpm_ * static_cast<double>(DIVISION_FACTORS[division_]);
}

double T5ynthGenerativeSequencer::strandStepDurationSamples(const Strand& s) const
{
    return stepDurationSamples() / juce::jmax(0.01f, s.divisionMultiplier);
}

constexpr float T5ynthGenerativeSequencer::DIVISION_FACTORS[];

void T5ynthGenerativeSequencer::prepare(double sr, int /*samplesPerBlock*/)
{
    sampleRate_ = sr;
    for (auto& s : strands)
    {
        s.samplesUntilNextStep = 0.0;
        s.samplesUntilGateOff  = -1.0;
    }
    rebuildPcSetFromScale();
    pitchField.cyclesUntilChange = pitchField.changeRate;
}

// ─── Parameter setters — route to strand 0 (legacy API) ───────────────────

void T5ynthGenerativeSequencer::setSteps(int n)
{
    auto& s = strands[0];
    n = juce::jlimit(2, MAX_STEPS, n);
    if (n != s.numSteps) { s.numSteps = n; s.patternDirty = true; }
    effectiveStepsForGui.store(s.numSteps, std::memory_order_relaxed);
}

void T5ynthGenerativeSequencer::setPulses(int p)
{
    auto& s = strands[0];
    p = juce::jlimit(1, s.numSteps, p);
    if (p != s.numPulses) { s.numPulses = p; s.patternDirty = true; }
    effectivePulsesForGui.store(s.numPulses, std::memory_order_relaxed);
}

void T5ynthGenerativeSequencer::setRotation(int r)
{
    auto& s = strands[0];
    r = s.numSteps > 0 ? ((r % s.numSteps) + s.numSteps) % s.numSteps : 0;
    if (r != s.rotation) { s.rotation = r; s.patternDirty = true; }
}

void T5ynthGenerativeSequencer::setMutation(float rate)
{
    auto& s = strands[0];
    s.mutationRate = juce::jlimit(0.0f, 1.0f, rate);
    effectiveMutationForGui.store(s.mutationRate, std::memory_order_relaxed);
}

void T5ynthGenerativeSequencer::setRange(int octaves)
{
    octaves = juce::jlimit(1, 4, octaves);
    if (octaves != rangeOctaves)
    {
        rangeOctaves = octaves;
        for (auto& s : strands) s.patternDirty = true;
    }
}

void T5ynthGenerativeSequencer::setScale(int type, int root)
{
    // If Off, default to Major for generative mode
    if (type <= 0) type = 1;
    if (type != scaleType || root != scaleRoot)
    {
        scaleType = type;
        scaleRoot = root;
        for (auto& s : strands) s.patternDirty = true;
        rebuildPcSetFromScale();
    }
}

void T5ynthGenerativeSequencer::setGate(float g)     { gate_ = juce::jlimit(0.1f, 1.0f, g); }
void T5ynthGenerativeSequencer::setBpm(double b)     { bpm_ = b; }
void T5ynthGenerativeSequencer::setDivision(int d)   { division_ = juce::jlimit(0, 4, d); }

void T5ynthGenerativeSequencer::setFixSteps(bool f)    { strands[0].fixSteps = f; }
void T5ynthGenerativeSequencer::setFixPulses(bool f)   { strands[0].fixPulses = f; }
void T5ynthGenerativeSequencer::setFixRotation(bool f) { strands[0].fixRotation = f; }
void T5ynthGenerativeSequencer::setFixMutation(bool f) { strands[0].fixMutation = f; }

void T5ynthGenerativeSequencer::setBaseMutation(float rate)
{
    auto& s = strands[0];
    s.baseMutation = juce::jlimit(0.0f, 1.0f, rate);
    if (s.fixMutation)
        s.mutationRate = s.baseMutation;
}

// ─── Per-strand setters (Phase 4) ──────────────────────────────────────────

void T5ynthGenerativeSequencer::setStrandEnabled(int idx, bool en)
{
    if (idx < 0 || idx >= MAX_STRANDS) return;
    // strand 0 is always on — guard its enabled flag.
    if (idx == 0) return;
    auto& s = strands[static_cast<size_t>(idx)];
    if (en && !s.enabled)
    {
        // Coming online — reset clocks and request fresh pattern.
        s.samplesUntilNextStep = 0.0;
        s.samplesUntilGateOff  = -1.0;
        s.currentStep          = 0;
        s.scheduledStep        = 0;
        s.cycleCount           = 0;
        if (!s.patternSeeded) s.patternDirty = true;
    }
    s.enabled = en;
}

void T5ynthGenerativeSequencer::setStrandRole(int idx, int roleIdx)
{
    if (idx < 0 || idx >= MAX_STRANDS) return;
    strands[static_cast<size_t>(idx)].role =
        static_cast<Role>(juce::jlimit(0, 3, roleIdx));
}

void T5ynthGenerativeSequencer::setStrandOctave(int idx, int shift)
{
    if (idx < 0 || idx >= MAX_STRANDS) return;
    strands[static_cast<size_t>(idx)].octaveShift = juce::jlimit(-2, 2, shift);
}

void T5ynthGenerativeSequencer::setStrandDivMult(int idx, float mult)
{
    if (idx < 0 || idx >= MAX_STRANDS) return;
    strands[static_cast<size_t>(idx)].divisionMultiplier = juce::jlimit(0.125f, 8.0f, mult);
}

void T5ynthGenerativeSequencer::setStrandDominance(int idx, float d)
{
    if (idx < 0 || idx >= MAX_STRANDS) return;
    strands[static_cast<size_t>(idx)].chordToneDominance = juce::jlimit(0.0f, 1.0f, d);
}

void T5ynthGenerativeSequencer::setStrandSteps(int idx, int n)
{
    if (idx < 0 || idx >= MAX_STRANDS) return;
    auto& s = strands[static_cast<size_t>(idx)];
    n = juce::jlimit(2, MAX_STEPS, n);
    if (n != s.numSteps) { s.numSteps = n; s.patternDirty = true; }
    if (idx == 0) effectiveStepsForGui.store(s.numSteps, std::memory_order_relaxed);
}

void T5ynthGenerativeSequencer::setStrandPulses(int idx, int n)
{
    if (idx < 0 || idx >= MAX_STRANDS) return;
    auto& s = strands[static_cast<size_t>(idx)];
    n = juce::jlimit(1, s.numSteps, n);
    if (n != s.numPulses) { s.numPulses = n; s.patternDirty = true; }
    if (idx == 0) effectivePulsesForGui.store(s.numPulses, std::memory_order_relaxed);
}

void T5ynthGenerativeSequencer::setStrandRotation(int idx, int n)
{
    if (idx < 0 || idx >= MAX_STRANDS) return;
    auto& s = strands[static_cast<size_t>(idx)];
    n = s.numSteps > 0 ? ((n % s.numSteps) + s.numSteps) % s.numSteps : 0;
    if (n != s.rotation) { s.rotation = n; s.patternDirty = true; }
}

void T5ynthGenerativeSequencer::setStrandMutation(int idx, float r)
{
    if (idx < 0 || idx >= MAX_STRANDS) return;
    auto& s = strands[static_cast<size_t>(idx)];
    s.mutationRate = juce::jlimit(0.0f, 1.0f, r);
    if (idx == 0) effectiveMutationForGui.store(s.mutationRate, std::memory_order_relaxed);
}

void T5ynthGenerativeSequencer::setStrandBaseMutation(int idx, float r)
{
    if (idx < 0 || idx >= MAX_STRANDS) return;
    auto& s = strands[static_cast<size_t>(idx)];
    s.baseMutation = juce::jlimit(0.0f, 1.0f, r);
    if (s.fixMutation) s.mutationRate = s.baseMutation;
}

void T5ynthGenerativeSequencer::setStrandFixSteps   (int idx, bool f) { if (idx>=0 && idx<MAX_STRANDS) strands[static_cast<size_t>(idx)].fixSteps    = f; }
void T5ynthGenerativeSequencer::setStrandFixPulses  (int idx, bool f) { if (idx>=0 && idx<MAX_STRANDS) strands[static_cast<size_t>(idx)].fixPulses   = f; }
void T5ynthGenerativeSequencer::setStrandFixRotation(int idx, bool f) { if (idx>=0 && idx<MAX_STRANDS) strands[static_cast<size_t>(idx)].fixRotation = f; }
void T5ynthGenerativeSequencer::setStrandFixMutation(int idx, bool f) { if (idx>=0 && idx<MAX_STRANDS) strands[static_cast<size_t>(idx)].fixMutation = f; }

// ─── Pitch-field setters ───────────────────────────────────────────────────

void T5ynthGenerativeSequencer::setFieldMode(int mode)
{
    pitchField.mode = static_cast<PitchField::Mode>(juce::jlimit(0, 3, mode));
}

void T5ynthGenerativeSequencer::setFieldChangeRate(int cycles)
{
    const int n = juce::jlimit(1, 64, cycles);
    if (n != pitchField.changeRate)
    {
        pitchField.changeRate = n;
        // Only clamp the in-flight countdown down; don't reset if it's below.
        if (pitchField.cyclesUntilChange > n)
            pitchField.cyclesUntilChange = n;
    }
}

void T5ynthGenerativeSequencer::setFieldCenterPc(int pc)
{
    pitchField.centerPc = ((pc % 12) + 12) % 12;
}

void T5ynthGenerativeSequencer::setFieldPivotInterval(int semitones)
{
    pitchField.pivotInterval = juce::jlimit(1, 11, semitones);
}

// ─── Start / Stop ──────────────────────────────────────────────────────────

void T5ynthGenerativeSequencer::start()
{
    if (!running)
    {
        running = true;
        for (auto& s : strands)
        {
            s.samplesUntilNextStep = 0.0;
            s.currentStep          = 0;
            s.scheduledStep        = 0;
            s.cycleCount           = 0;
            if (!s.patternSeeded)
                s.patternDirty = true;  // rebuild on start (unless seeded)
            s.patternSeeded = false;
        }
    }
}

void T5ynthGenerativeSequencer::stop()
{
    running = false;
    for (auto& s : strands)
    {
        s.samplesUntilGateOff = -1.0;
        s.currentStep   = 0;
        s.scheduledStep = 0;
    }
    currentStepForGui.store(-1, std::memory_order_relaxed);
}

void T5ynthGenerativeSequencer::reset()
{
    stop();
}

void T5ynthGenerativeSequencer::allNotesOff(juce::MidiBuffer& midi, int sampleOffset)
{
    for (auto& s : strands)
    {
        if (s.lastPlayedNote >= 0)
        {
            midi.addEvent(juce::MidiMessage::noteOff(1, s.lastPlayedNote), sampleOffset);
            s.lastPlayedNote = -1;
        }
        s.samplesUntilGateOff = -1.0;
    }
}

// ─── Pattern generation ────────────────────────────────────────────────────

void T5ynthGenerativeSequencer::computeGaps(const Strand& s, int* gaps, int* gapCount) const
{
    // Find gaps between consecutive pulses (circular)
    *gapCount = 0;
    int firstPulse = -1;
    int prevPulse = -1;

    for (int i = 0; i < s.numSteps; ++i)
    {
        if (s.eucPattern[static_cast<size_t>(i)])
        {
            if (firstPulse < 0) firstPulse = i;
            if (prevPulse >= 0)
                gaps[(*gapCount)++] = i - prevPulse;
            prevPulse = i;
        }
    }
    // Wrap-around gap: from last pulse to first pulse
    if (firstPulse >= 0 && prevPulse >= 0 && *gapCount > 0)
        gaps[(*gapCount)++] = s.numSteps - prevPulse + firstPulse;
    else if (firstPulse >= 0)
        gaps[(*gapCount)++] = s.numSteps; // single pulse
}

void T5ynthGenerativeSequencer::rebuildPattern(Strand& s)
{
    // 1. Generate Euclidean rhythm
    auto fullPattern = EuclideanRhythm::generate(s.numSteps, s.numPulses, s.rotation);
    for (int i = 0; i < MAX_STEPS; ++i)
        s.eucPattern[static_cast<size_t>(i)] = (i < s.numSteps) && fullPattern[static_cast<size_t>(i)];

    // 2. Compute gaps between pulses → interval jumps
    int gaps[MAX_STEPS];
    int gapCount = 0;
    computeGaps(s, gaps, &gapCount);

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
    int stride = juce::jmax(1, s.numSteps - s.numPulses);
    // Ensure coprime with totalDegrees for maximum coverage
    while (stride > 1 && totalDegrees % stride == 0)
        stride++;
    if (stride >= totalDegrees) stride = 1;

    int pulseIdx = 0;
    s.notePattern.fill(0);
    s.accentPattern.fill(false);
    s.degreePattern.fill(0);

    // Triangle wave period: up totalDegrees-1 steps, then down totalDegrees-1 steps
    int period = juce::jmax(2, 2 * (totalDegrees - 1));

    for (int i = 0; i < s.numSteps; ++i)
    {
        if (s.eucPattern[static_cast<size_t>(i)])
        {
            // Reflected zigzag: fold position into [0, totalDegrees-1]
            int raw = pulseIdx * stride;
            int pos = raw % period;
            int degree = (pos < totalDegrees) ? pos : (period - pos);
            degree = juce::jlimit(0, totalDegrees - 1, degree);

            int midiNote = ScaleQuantizer::degreeToMidi(degree, scaleRoot, scale, baseNote);
            s.notePattern[static_cast<size_t>(i)] = midiNote;
            s.degreePattern[static_cast<size_t>(i)] = degree;

            pulseIdx++;
        }
    }

    // 5. Euclidean accents: pulse after longest gap = accented, step 0 = accented.
    //    Binary per-step flag, translated to velocity at playback.
    {
        int maxGapBefore = 1;
        for (int i = 0; i < s.numSteps; ++i)
        {
            if (!s.eucPattern[static_cast<size_t>(i)]) continue;
            int gap = 0;
            for (int j = 1; j <= s.numSteps; ++j)
            {
                int prev = ((i - j) % s.numSteps + s.numSteps) % s.numSteps;
                if (s.eucPattern[static_cast<size_t>(prev)]) break;
                gap++;
            }
            if (gap > maxGapBefore) maxGapBefore = gap;
        }

        for (int i = 0; i < s.numSteps; ++i)
        {
            if (!s.eucPattern[static_cast<size_t>(i)]) continue;
            int gapBefore = 0;
            for (int j = 1; j <= s.numSteps; ++j)
            {
                int prev = ((i - j) % s.numSteps + s.numSteps) % s.numSteps;
                if (s.eucPattern[static_cast<size_t>(prev)]) break;
                gapBefore++;
            }
            // Accent if: downbeat, or preceded by the longest gap
            s.accentPattern[static_cast<size_t>(i)] = (i == 0) || (gapBefore >= maxGapBefore);
        }
    }

    // Warm-up: scramble the deterministic stride walk so the initial
    // pattern is never a boring ascending scale
    for (int w = 0; w < 4; ++w)
        mutateNotes(s, 1.0f, totalDegrees, static_cast<int>(scale), baseNote);

    s.patternDirty      = false;
    s.basePulses        = s.numPulses;
    s.baseSteps         = s.numSteps;
    s.driftCycle        = 0;
    s.pulseDriftAccum   = 0;
    s.pulseDriftUp      = true;
    s.stepsDriftAccum   = 0;
    s.stepsDriftUp      = true;
    s.mutationDriftPhase = 0;
    publishStrandToGui(s);
}

void T5ynthGenerativeSequencer::mutateNotes(Strand& s, float rate, int totalDegrees,
                                             int scaleEnum, int baseNote)
{
    auto scale = static_cast<ScaleQuantizer::Scale>(scaleEnum);
    int pulseIndices[MAX_STEPS];
    int pulseCount = 0;
    for (int i = 0; i < s.numSteps; ++i)
        if (s.eucPattern[static_cast<size_t>(i)])
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
            int newDeg = s.degreePattern[static_cast<size_t>(idx)] + dir * jump;
            newDeg = ((newDeg % totalDegrees) + totalDegrees) % totalDegrees;
            s.degreePattern[static_cast<size_t>(idx)] = newDeg;
            s.notePattern[static_cast<size_t>(idx)] =
                ScaleQuantizer::degreeToMidi(newDeg, scaleRoot, scale, baseNote);
        }
    }
}

void T5ynthGenerativeSequencer::mutatePattern(Strand& s)
{
    if (s.mutationRate <= 0.0f) return;

    auto scale = static_cast<ScaleQuantizer::Scale>(
        juce::jlimit(1, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
    int dpOct = ScaleQuantizer::degreesPerOctave(scale);
    int totalDegrees = dpOct * rangeOctaves;
    int baseNote = 48;

    // ── Euclidean drift: rotation + pulse count evolve per cycle ──
    applyEuclideanDrift(s);

    // ── Note mutation: Turing Machine — mutate notes per cycle ──
    mutateNotes(s, s.mutationRate, totalDegrees, static_cast<int>(scale), baseNote);

    publishStrandToGui(s);
}

// ─── Euclidean drift ────────────────────────────────────────────────────────

void T5ynthGenerativeSequencer::applyEuclideanDrift(Strand& s)
{
    s.driftCycle++;
    int driftIdx = (s.driftCycle - 1) % DRIFT_PERIOD;

    // ── Rotation drift: Euclidean-distributed circular shifts ──
    if (!s.fixRotation && s.numSteps > 1)
    {
        int rotPulses = juce::jmax(0, juce::roundToInt(
            s.mutationRate * static_cast<float>(DRIFT_PERIOD)));
        auto rotDrift = EuclideanRhythm::generate(DRIFT_PERIOD, rotPulses, 0);
        if (rotDrift[static_cast<size_t>(driftIdx)])
        {
            auto shiftLeft = [&](auto& arr) {
                auto first = arr[0];
                for (int i = 0; i < s.numSteps - 1; ++i)
                    arr[static_cast<size_t>(i)] = arr[static_cast<size_t>(i + 1)];
                arr[static_cast<size_t>(s.numSteps - 1)] = first;
            };
            shiftLeft(s.eucPattern);
            shiftLeft(s.notePattern);
            shiftLeft(s.accentPattern);
            shiftLeft(s.degreePattern);
        }
    }

    // ── Pulse count drift ──
    if (!s.fixPulses)
    {
        int pulseDriftPulses = juce::jmax(0, juce::roundToInt(
            s.mutationRate * static_cast<float>(DRIFT_PERIOD) * 0.5f));
        auto pulseDrift = EuclideanRhythm::generate(DRIFT_PERIOD, pulseDriftPulses, 0);
        if (pulseDrift[static_cast<size_t>(driftIdx)])
        {
            int oldPulses = s.numPulses;
            if (s.pulseDriftUp)
                addPulse(s);
            else
                removePulse(s);
            // Only update accumulator if pulse count actually changed
            if (s.numPulses != oldPulses)
            {
                s.pulseDriftAccum += s.pulseDriftUp ? 1 : -1;

                int maxDrift = juce::jmax(1, rangeOctaves);
                if (s.pulseDriftAccum >= maxDrift) s.pulseDriftUp = false;
                else if (s.pulseDriftAccum <= -maxDrift) s.pulseDriftUp = true;
            }
        }
    }

    // ── Steps drift ──
    if (!s.fixSteps)
        applyStepsDrift(s);

    // ── Mutation drift ──
    if (!s.fixMutation)
        applyMutationDrift(s);
}

// ─── Steps drift ────────────────────────────────────────────────────────────

void T5ynthGenerativeSequencer::applyStepsDrift(Strand& s)
{
    int driftIdx = (s.driftCycle - 1) % DRIFT_PERIOD;

    int stepsDriftPulses = juce::jmax(0, juce::roundToInt(
        s.mutationRate * static_cast<float>(DRIFT_PERIOD) * 0.35f));
    auto stepsDrift = EuclideanRhythm::generate(DRIFT_PERIOD, stepsDriftPulses, 0);

    if (!stepsDrift[static_cast<size_t>(driftIdx)])
        return;

    int delta = s.stepsDriftUp ? 1 : -1;
    if (s.mutationRate > 0.7f)
    {
        std::uniform_int_distribution<int> magDist(1, 2);
        delta *= magDist(rng);
    }

    int newSteps = juce::jlimit(2, MAX_STEPS, s.numSteps + delta);
    if (newSteps == s.numSteps) return;

    // Preserve pulse density ratio
    float ratio = static_cast<float>(s.numPulses) / static_cast<float>(s.numSteps);

    if (newSteps > s.numSteps)
    {
        for (int i = s.numSteps; i < newSteps; ++i)
        {
            s.eucPattern[static_cast<size_t>(i)]    = false;
            s.notePattern[static_cast<size_t>(i)]   = 0;
            s.accentPattern[static_cast<size_t>(i)] = false;
            s.degreePattern[static_cast<size_t>(i)] = 0;
        }
    }

    int newPulses;
    if (newSteps < s.numSteps)
    {
        int surviving = 0;
        for (int i = 0; i < newSteps; ++i)
            if (s.eucPattern[static_cast<size_t>(i)]) surviving++;
        newPulses = juce::jmax(1, surviving);
    }
    else
    {
        newPulses = juce::jlimit(1, newSteps, juce::roundToInt(ratio * static_cast<float>(newSteps)));
    }

    s.numSteps  = newSteps;
    s.numPulses = newPulses;
    s.stepsDriftAccum += (delta > 0) ? 1 : -1;

    int maxDrift = juce::jmax(1, rangeOctaves);
    if (s.stepsDriftAccum >= maxDrift) s.stepsDriftUp = false;
    else if (s.stepsDriftAccum <= -maxDrift) s.stepsDriftUp = true;
}

// ─── Mutation drift ─────────────────────────────────────────────────────────

void T5ynthGenerativeSequencer::applyMutationDrift(Strand& s)
{
    s.mutationDriftPhase = (s.mutationDriftPhase + 1) % (DRIFT_PERIOD * 2);
    float phase = static_cast<float>(s.mutationDriftPhase) / static_cast<float>(DRIFT_PERIOD * 2);
    float sinVal = std::sin(phase * juce::MathConstants<float>::twoPi);
    // Map sin [-1..+1] to [0.75..1.25] multiplier — breathes symmetrically around user setting
    float multiplier = 1.0f + 0.25f * sinVal;
    // Additive floor (5%) prevents unfixed mutation from getting permanently stuck at 0
    float floor = s.fixMutation ? 0.0f : 0.05f;
    s.mutationRate = juce::jlimit(0.0f, 1.0f, std::max(floor, s.baseMutation * multiplier));
}

// ─── Surgical pulse add/remove (preserves Turing mutations) ─────────────────

void T5ynthGenerativeSequencer::addPulse(Strand& s)
{
    if (s.numPulses >= s.numSteps) return;

    auto scale = static_cast<ScaleQuantizer::Scale>(
        juce::jlimit(1, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
    int dpOct = ScaleQuantizer::degreesPerOctave(scale);
    int totalDegrees = dpOct * rangeOctaves;
    int baseNote = 48;

    // Find the longest gap between existing pulses → insert in its middle
    int bestStart = -1, bestGap = 0;
    for (int i = 0; i < s.numSteps; ++i)
    {
        if (!s.eucPattern[static_cast<size_t>(i)]) continue;
        int gap = 0;
        for (int j = 1; j < s.numSteps; ++j)
        {
            if (s.eucPattern[static_cast<size_t>((i + j) % s.numSteps)]) break;
            gap++;
        }
        if (gap > bestGap) { bestGap = gap; bestStart = i; }
    }
    if (bestStart < 0 || bestGap <= 0) return;

    int insertIdx = (bestStart + bestGap / 2 + 1) % s.numSteps;
    s.eucPattern[static_cast<size_t>(insertIdx)] = true;

    // Interpolate degree from neighbouring pulses + slight jitter
    int prevDeg = s.degreePattern[static_cast<size_t>(bestStart)];
    int nextIdx = (bestStart + bestGap + 1) % s.numSteps;
    int nextDeg = s.eucPattern[static_cast<size_t>(nextIdx)]
                ? s.degreePattern[static_cast<size_t>(nextIdx)] : prevDeg;
    std::uniform_int_distribution<int> jitter(-1, 1);
    int newDeg = ((prevDeg + nextDeg) / 2 + jitter(rng) + totalDegrees) % totalDegrees;

    s.degreePattern[static_cast<size_t>(insertIdx)] = newDeg;
    s.notePattern[static_cast<size_t>(insertIdx)] =
        ScaleQuantizer::degreeToMidi(newDeg, scaleRoot, scale, baseNote);
    s.accentPattern[static_cast<size_t>(insertIdx)] = false;
    s.numPulses++;
}

void T5ynthGenerativeSequencer::removePulse(Strand& s)
{
    if (s.numPulses <= 1) return;

    // Remove the pulse with the smallest surrounding gap (most crowded)
    int bestIdx = -1, bestGap = s.numSteps + 1;
    for (int i = 0; i < s.numSteps; ++i)
    {
        if (!s.eucPattern[static_cast<size_t>(i)]) continue;
        int gapBefore = 0, gapAfter = 0;
        for (int j = 1; j < s.numSteps; ++j)
        {
            if (s.eucPattern[static_cast<size_t>((i - j + s.numSteps) % s.numSteps)]) break;
            gapBefore++;
        }
        for (int j = 1; j < s.numSteps; ++j)
        {
            if (s.eucPattern[static_cast<size_t>((i + j) % s.numSteps)]) break;
            gapAfter++;
        }
        int totalGap = gapBefore + gapAfter;
        if (totalGap < bestGap) { bestGap = totalGap; bestIdx = i; }
    }
    if (bestIdx < 0) return;

    s.eucPattern[static_cast<size_t>(bestIdx)]    = false;
    s.notePattern[static_cast<size_t>(bestIdx)]   = 0;
    s.degreePattern[static_cast<size_t>(bestIdx)] = 0;
    s.accentPattern[static_cast<size_t>(bestIdx)] = false;
    s.numPulses--;
}

// ─── Seed from step sequencer (strand 0 only) ───────────────────────────────

void T5ynthGenerativeSequencer::seedFromSteps(const int* midiNotes,
                                               const bool* enabled,
                                               int count)
{
    auto& s = strands[0];
    count = juce::jlimit(2, MAX_STEPS, count);
    s.numSteps = count;

    // Count active steps → pulses
    int pulseCount = 0;
    for (int i = 0; i < count; ++i)
        if (enabled[i]) pulseCount++;
    s.numPulses = juce::jmax(1, pulseCount);

    s.eucPattern.fill(false);
    s.notePattern.fill(0);
    s.accentPattern.fill(false);
    s.degreePattern.fill(0);

    auto scale = static_cast<ScaleQuantizer::Scale>(
        juce::jlimit(1, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
    int dpOct = ScaleQuantizer::degreesPerOctave(scale);
    int baseNote = 48;

    for (int i = 0; i < count; ++i)
    {
        s.eucPattern[static_cast<size_t>(i)] = enabled[i];
        if (enabled[i] && midiNotes[i] > 0)
        {
            s.notePattern[static_cast<size_t>(i)] = midiNotes[i];
            s.accentPattern[static_cast<size_t>(i)] = false;

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
            s.degreePattern[static_cast<size_t>(i)] = octave * dpOct + bestDeg;
        }
    }

    s.basePulses      = s.numPulses;
    s.driftCycle      = 0;
    s.pulseDriftAccum = 0;
    s.pulseDriftUp    = true;
    s.rotation        = 0;
    s.patternDirty    = false;
    s.patternSeeded   = true;
    publishStrandToGui(s);
}

void T5ynthGenerativeSequencer::publishStrandToGui(const Strand& s)
{
    numStepsForGui.store(s.numSteps, std::memory_order_relaxed);
    effectiveStepsForGui.store(s.numSteps, std::memory_order_relaxed);
    effectivePulsesForGui.store(s.numPulses, std::memory_order_relaxed);
    effectiveMutationForGui.store(s.mutationRate, std::memory_order_relaxed);
    for (int i = 0; i < MAX_STEPS; ++i)
        notePatternForGui[static_cast<size_t>(i)].store(
            i < s.numSteps ? s.notePattern[static_cast<size_t>(i)] : 0,
            std::memory_order_relaxed);
}

// ─── Note selection (Phase 3) ──────────────────────────────────────────────
//
// pickNote() is the projection from the strand's abstract Turing state
// (scale-degree walk) onto concrete MIDI output through the shared
// PitchField. Metric weighting applies on strong positions; roles modulate
// the selection behavior; Density bypasses metric weighting entirely.

int T5ynthGenerativeSequencer::voiceLedFieldMember(int rawPc) const
{
    const auto set = pitchField.pcSet;
    if (set == 0) return rawPc;
    rawPc = ((rawPc % 12) + 12) % 12;
    // Search outward in pc-space for the closest field member; tie goes lower.
    for (int off = 0; off <= 6; ++off)
    {
        int below = ((rawPc - off) % 12 + 12) % 12;
        int above = (rawPc + off) % 12;
        const bool bBelow = (set >> below) & 1u;
        const bool bAbove = (set >> above) & 1u;
        if (off == 0 && bBelow) return rawPc;
        if (bBelow) return below;
        if (bAbove) return above;
    }
    return rawPc;
}

int T5ynthGenerativeSequencer::chromaticFieldWalk(int lastMidi)
{
    // Sheets-of-Sound: step ±1 semitone from last note, random direction.
    // Fully chromatic (ignores pcSet) — the point is dense linear motion.
    if (lastMidi < 0) return -1;   // caller falls back to raw pc
    const int lastPc = ((lastMidi % 12) + 12) % 12;
    std::uniform_int_distribution<int> dirDist(0, 1);
    const int dir = (dirDist(rng) == 0) ? -1 : 1;
    return ((lastPc + dir) % 12 + 12) % 12;
}

int T5ynthGenerativeSequencer::pickNote(Strand& s, int stepIdx, int rawDegree)
{
    // Project the strand's Turing-walked degree into MIDI using
    // (scaleType, scaleRoot) as the *register* source, then re-snap the
    // pitch class through the shared PitchField according to role + metric
    // weight.
    auto scale = static_cast<ScaleQuantizer::Scale>(
        juce::jlimit(1, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
    const int baseNote = 48 + s.octaveShift * 12;
    const int rawMidi   = ScaleQuantizer::degreeToMidi(rawDegree, scaleRoot, scale, baseNote);
    const int rawPc     = ((rawMidi % 12) + 12) % 12;
    const int octaveAnchor = rawMidi - rawPc;

    const bool isStrong = (stepIdx == 0) || s.accentPattern[static_cast<size_t>(stepIdx)];

    int targetPc = rawPc;

    if (s.role == Role::Density)
    {
        int walked = chromaticFieldWalk(s.lastPlayedNote);
        if (walked >= 0) targetPc = walked;
    }
    else
    {
        std::uniform_real_distribution<float> probDist(0.0f, 1.0f);
        if (isStrong && s.chordToneDominance > 0.0f
            && probDist(rng) < s.chordToneDominance)
        {
            targetPc = ((pitchField.centerPc % 12) + 12) % 12;
        }
        else if (!((pitchField.pcSet >> rawPc) & 1u))
        {
            // Raw pc lies outside the current field → snap voice-led
            targetPc = voiceLedFieldMember(rawPc);
        }
        // Else: raw pc is in field already — keep it (preserves Turing dynamics
        // when field equals scale, for backward compatibility).
    }

    return juce::jlimit(0, 127, octaveAnchor + targetPc);
}

// ─── Process Block ─────────────────────────────────────────────────────────

void T5ynthGenerativeSequencer::processBlock(juce::AudioBuffer<float>& buffer,
                                              juce::MidiBuffer& midi)
{
    if (!running)
    {
        for (auto& st : strands)
        {
            if (st.lastPlayedNote >= 0)
            {
                midi.addEvent(juce::MidiMessage::noteOff(1, st.lastPlayedNote), 0);
                st.lastPlayedNote = -1;
            }
            st.currentStep = 0;
        }
        currentStepForGui.store(-1, std::memory_order_relaxed);
        return;
    }

    // Rebuild any dirty patterns for enabled strands before processing.
    for (auto& st : strands)
        if (st.enabled && st.patternDirty)
            rebuildPattern(st);

    const int numSamples = buffer.getNumSamples();
    int samplePos = 0;

    std::uniform_real_distribution<float> probDist(0.0f, 1.0f);

    while (samplePos < numSamples)
    {
        const int remaining = numSamples - samplePos;

        // Find the earliest upcoming event (step boundary or gate-off) across
        // all enabled strands. This is the core of the polyrhythmic scheduler:
        // each strand runs its own clock at its own divisionMultiplier.
        double minEvent = static_cast<double>(remaining) + 1.0;
        Strand* who = nullptr;
        bool isGateOff = false;

        for (auto& st : strands)
        {
            if (!st.enabled) continue;
            double e = st.samplesUntilNextStep;
            bool g = false;
            if (st.samplesUntilGateOff >= 0.0 && st.samplesUntilGateOff < e)
            {
                e = st.samplesUntilGateOff;
                g = true;
            }
            if (e < minEvent) { minEvent = e; who = &st; isGateOff = g; }
        }

        if (!who || minEvent > static_cast<double>(remaining))
        {
            // No event lands inside this block — decrement all enabled strands.
            for (auto& st : strands)
            {
                if (!st.enabled) continue;
                st.samplesUntilNextStep -= remaining;
                if (st.samplesUntilGateOff >= 0.0) st.samplesUntilGateOff -= remaining;
            }
            break;
        }

        const int advance = std::max(0, static_cast<int>(minEvent));
        samplePos += advance;
        for (auto& st : strands)
        {
            if (!st.enabled) continue;
            st.samplesUntilNextStep -= advance;
            if (st.samplesUntilGateOff >= 0.0) st.samplesUntilGateOff -= advance;
        }

        const int eventPos = juce::jmin(samplePos, numSamples - 1);
        Strand& s = *who;

        if (isGateOff)
        {
            if (s.lastPlayedNote >= 0)
            {
                midi.addEvent(juce::MidiMessage::noteOff(1, s.lastPlayedNote), eventPos);
                s.lastPlayedNote = -1;
            }
            s.samplesUntilGateOff = -1.0;
            continue;
        }

        // Step boundary for this strand
        const int stepIdx = s.scheduledStep % s.numSteps;

        // Cycle boundary → mutate strand; field evolves on strand 0 only
        if (stepIdx == 0 && s.scheduledStep > 0)
        {
            mutatePattern(s);
            if (&s == &strands[0])
                advancePitchField();
        }

        // Note-off for this strand's previous note
        if (s.lastPlayedNote >= 0)
        {
            midi.addEvent(juce::MidiMessage::noteOff(1, s.lastPlayedNote), eventPos);
            s.lastPlayedNote = -1;
        }

        // Determine if this step should fire
        const bool isPulse = s.eucPattern[static_cast<size_t>(stepIdx)];
        const float fireProb = isPulse
            ? (0.90f + 0.10f * (1.0f - s.mutationRate))   // pulse: high probability
            : (s.mutationRate * 0.15f);                    // rest: ghost-note probability

        // Resolve the degree for this step. Pulses use their own degree;
        // ghost positions inherit the nearest pulse's degree (non-destructive
        // — we don't overwrite the rest slot's degreePattern entry).
        int rawDegree = s.degreePattern[static_cast<size_t>(stepIdx)];
        if (!isPulse)
        {
            for (int off = 1; off <= s.numSteps; ++off)
            {
                int prev = ((stepIdx - off) % s.numSteps + s.numSteps) % s.numSteps;
                if (s.eucPattern[static_cast<size_t>(prev)])
                    { rawDegree = s.degreePattern[static_cast<size_t>(prev)]; break; }
                int next = (stepIdx + off) % s.numSteps;
                if (s.eucPattern[static_cast<size_t>(next)])
                    { rawDegree = s.degreePattern[static_cast<size_t>(next)]; break; }
            }
        }

        const bool shouldFire = probDist(rng) < fireProb;

        if (shouldFire)
        {
            const int note = pickNote(s, stepIdx, rawDegree);

            // Velocity: accent / normal / ghost, with ±5 jitter.
            int baseVel;
            if (!isPulse)
                baseVel = 55;
            else if (s.accentPattern[static_cast<size_t>(stepIdx)])
                baseVel = 100;
            else
                baseVel = 85;

            std::uniform_int_distribution<int> velJitter(-5, 5);
            const int velInt = juce::jlimit(1, 127, baseVel + velJitter(rng));
            midi.addEvent(juce::MidiMessage::noteOn(1, note,
                          static_cast<juce::uint8>(velInt)), eventPos);

            s.lastPlayedNote       = note;
            s.samplesUntilGateOff  = static_cast<double>(gate_) * strandStepDurationSamples(s);
        }
        else
        {
            s.samplesUntilGateOff = -1.0;
        }

        s.currentStep = stepIdx;
        if (&s == &strands[0])
            currentStepForGui.store(s.currentStep, std::memory_order_relaxed);
        s.scheduledStep++;
        s.samplesUntilNextStep += strandStepDurationSamples(s);
    }
}

// ─── Pitch-field evolution ─────────────────────────────────────────────────
//
// Four modes drive the shared pc-set:
//   Static    — no change (Satie-esque modal stasis)
//   Drift     — swap one pc for another per tick (non-functional glide)
//   Transform — Webern-style row operations: Tn / In / R / RI
//   Pivot     — Coltrane-matrix shift by pivotInterval semitones
//
// advancePitchField() is called on strand 0's cycle boundary only.

void T5ynthGenerativeSequencer::rebuildPcSetFromScale()
{
    auto scale = static_cast<ScaleQuantizer::Scale>(
        juce::jlimit(1, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
    pitchField.pcSet = ScaleQuantizer::pcSetFromScale(scale, scaleRoot);
    pitchField.centerPc = ((scaleRoot % 12) + 12) % 12;
    // Seed row = ascending pcs of current scale, then fill with chromatic
    pitchField.row = {0,1,2,3,4,5,6,7,8,9,10,11};
    int written = 0;
    for (int pc = 0; pc < 12 && written < 12; ++pc)
        if ((pitchField.pcSet >> pc) & 1u)
            pitchField.row[static_cast<size_t>(written++)] = pc;
    pitchField.rowSize = juce::jmax(3, written);
    pitchField.rowTn = 0;
    pitchField.rowInverted = false;
    pitchField.rowRetrograde = false;
    pitchField.pivotAccum = 0;
}

void T5ynthGenerativeSequencer::advancePitchField()
{
    if (--pitchField.cyclesUntilChange > 0) return;
    pitchField.cyclesUntilChange = juce::jmax(1, pitchField.changeRate);

    switch (pitchField.mode)
    {
        case PitchField::Static:    break;
        case PitchField::Drift:     driftPcSet();         break;
        case PitchField::Transform: applyRowOp();         break;
        case PitchField::Pivot:     applyPivot();         break;
    }
}

void T5ynthGenerativeSequencer::driftPcSet()
{
    // Count current members and holes.
    int inPcs[12], outPcs[12];
    int nIn = 0, nOut = 0;
    for (int pc = 0; pc < 12; ++pc)
    {
        if ((pitchField.pcSet >> pc) & 1u) inPcs[nIn++] = pc;
        else                                outPcs[nOut++] = pc;
    }
    // Degenerate — can't drift if fully full or empty.
    if (nIn == 0 || nOut == 0) return;

    std::uniform_int_distribution<int> inPick(0, nIn - 1);
    std::uniform_int_distribution<int> outPick(0, nOut - 1);

    // Never remove the centerPc (keeps anchor stable).
    int removeIdx = inPick(rng);
    int attempts = 0;
    while (inPcs[removeIdx] == pitchField.centerPc && nIn > 1 && attempts < 8)
    {
        removeIdx = inPick(rng);
        ++attempts;
    }
    if (inPcs[removeIdx] == pitchField.centerPc) return;   // fallback: abort drift

    int addPc = outPcs[outPick(rng)];
    pitchField.pcSet &= static_cast<std::uint16_t>(~(1u << inPcs[removeIdx]));
    pitchField.pcSet |= static_cast<std::uint16_t>(1u << addPc);
}

void T5ynthGenerativeSequencer::applyRowOp()
{
    // Random op from {Tn, In, R, RI}. Uses existing row state.
    std::uniform_int_distribution<int> opDist(0, 3);
    std::uniform_int_distribution<int> nDist(1, 11);
    const int op = opDist(rng);
    const int n  = nDist(rng);

    const int sz = juce::jlimit(3, 12, pitchField.rowSize);

    auto transpose = [&](int amount) {
        for (int i = 0; i < sz; ++i)
            pitchField.row[static_cast<size_t>(i)] =
                (((pitchField.row[static_cast<size_t>(i)] + amount) % 12) + 12) % 12;
    };
    auto invert = [&]() {
        for (int i = 0; i < sz; ++i)
            pitchField.row[static_cast<size_t>(i)] =
                ((12 - pitchField.row[static_cast<size_t>(i)]) % 12);
    };
    auto retrograde = [&]() {
        for (int i = 0, j = sz - 1; i < j; ++i, --j)
            std::swap(pitchField.row[static_cast<size_t>(i)],
                      pitchField.row[static_cast<size_t>(j)]);
    };

    switch (op)
    {
        case 0: transpose(n); pitchField.rowTn = (pitchField.rowTn + n) % 12; break;
        case 1: invert(); transpose(n);
                pitchField.rowInverted = !pitchField.rowInverted;
                pitchField.rowTn = (pitchField.rowTn + n) % 12; break;
        case 2: retrograde();
                pitchField.rowRetrograde = !pitchField.rowRetrograde; break;
        case 3: retrograde(); invert(); transpose(n);
                pitchField.rowRetrograde = !pitchField.rowRetrograde;
                pitchField.rowInverted   = !pitchField.rowInverted;
                pitchField.rowTn         = (pitchField.rowTn + n) % 12; break;
    }
    rebuildPcSetFromRow();
}

void T5ynthGenerativeSequencer::rebuildPcSetFromRow()
{
    const int sz = juce::jlimit(3, 12, pitchField.rowSize);
    std::uint16_t newSet = 0;
    for (int i = 0; i < sz; ++i)
        newSet |= static_cast<std::uint16_t>(1u << (pitchField.row[static_cast<size_t>(i)] & 0xF));
    // Guard against collapsed row (all identical pcs) — keep previous if new is too small.
    if (__builtin_popcount(static_cast<unsigned>(newSet)) >= 3)
        pitchField.pcSet = newSet;
    // Keep centerPc consistent: if it dropped out, shift to row[0].
    if (!((pitchField.pcSet >> pitchField.centerPc) & 1u))
        pitchField.centerPc = pitchField.row[0] & 0xF;
}

void T5ynthGenerativeSequencer::applyPivot()
{
    const int iv = ((pitchField.pivotInterval % 12) + 12) % 12;
    if (iv == 0) return;

    std::uint16_t shifted = 0;
    for (int i = 0; i < 12; ++i)
    {
        int src = ((i - iv) % 12 + 12) % 12;
        if ((pitchField.pcSet >> src) & 1u)
            shifted |= static_cast<std::uint16_t>(1u << i);
    }
    pitchField.pcSet    = shifted;
    pitchField.centerPc = (pitchField.centerPc + iv) % 12;
    pitchField.pivotAccum = (pitchField.pivotAccum + iv) % 12;
}
