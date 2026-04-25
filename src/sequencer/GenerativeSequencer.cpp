#include "GenerativeSequencer.h"
#include "EuclideanRhythm.h"
#include "ScaleQuantizer.h"

namespace
{
int wrapPc(int pc)
{
    return ((pc % 12) + 12) % 12;
}

int positiveModulo(int value, int modulus)
{
    if (modulus <= 0) return 0;
    return ((value % modulus) + modulus) % modulus;
}

int pcDistance(int a, int b)
{
    const int d = std::abs(wrapPc(a) - wrapPc(b));
    return juce::jmin(d, 12 - d);
}

int circularDistance(int a, int b, int modulus)
{
    if (modulus <= 0) return 0;
    const int d = std::abs(positiveModulo(a, modulus) - positiveModulo(b, modulus));
    return juce::jmin(d, modulus - d);
}

int countBits12(std::uint16_t bits)
{
    int count = 0;
    for (int i = 0; i < 12; ++i)
        if ((bits >> i) & 1u)
            ++count;
    return count;
}
}

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

double T5ynthGenerativeSequencer::shuffledStrandStepDurationSamples(const Strand& s, int stepIdx) const
{
    const double base = strandStepDurationSamples(s);
    if (s.numSteps <= 1)
        return base;

    double factor = 1.0;

    if (shuffle_ > 0.0f)
    {
        if (!((s.numSteps & 1) != 0 && stepIdx == s.numSteps - 1))
        {
            const double amount = static_cast<double>(shuffle_);
            factor *= ((stepIdx & 1) == 0 ? (1.0 + amount) : (1.0 - amount));
        }
    }

    if (strandIndexOf(s) == 0)
        return base * factor;

    const auto moment = metricMomentForStep(s, stepIdx);
    const double elasticity = static_cast<double>(0.012f + 0.030f * s.mutationRate);
    double metricFactor = 1.0;
    if (moment.downbeat)         metricFactor += elasticity * 0.55;
    if (moment.groupStart)       metricFactor += elasticity;
    if (moment.anticipatesGroup) metricFactor -= elasticity * 0.70;
    if (moment.phraseEnd)        metricFactor += elasticity * (moment.path == MetricPath::OpenBreath ? 1.45 : 0.85);

    const int activeOthers = activeOtherStrandCount(s);
    if (activeOthers >= 2)
        metricFactor = 1.0 + (metricFactor - 1.0) * 0.55;
    if (s.role == Role::Anchor)
        metricFactor = 1.0 + (metricFactor - 1.0) * 0.45;

    factor *= juce::jlimit(0.84, 1.22, metricFactor);
    return base * factor;
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
    const int requested = juce::jlimit(2, MAX_STEPS, n);
    const bool userChanged = requested != s.requestedSteps;
    s.requestedSteps = requested;
    if (s.fixSteps || userChanged)
    {
        if (requested != s.numSteps)
            s.patternDirty = true;
        s.numSteps = requested;
        const int clampedPulses = juce::jlimit(1, s.numSteps, s.requestedPulses);
        if (clampedPulses != s.numPulses)
            s.numPulses = clampedPulses;
        s.rotation = positiveModulo(s.requestedRotation, s.numSteps);
    }
    effectiveStepsForGui.store(s.numSteps, std::memory_order_relaxed);
    effectivePulsesForGui.store(s.numPulses, std::memory_order_relaxed);
}

void T5ynthGenerativeSequencer::setPulses(int p)
{
    auto& s = strands[0];
    const int requested = juce::jlimit(1, MAX_STEPS, p);
    const bool userChanged = requested != s.requestedPulses;
    s.requestedPulses = requested;
    p = juce::jlimit(1, s.numSteps, requested);
    if ((s.fixPulses || userChanged) && p != s.numPulses)
    {
        s.numPulses = p;
        s.patternDirty = true;
    }
    effectivePulsesForGui.store(s.numPulses, std::memory_order_relaxed);
}

void T5ynthGenerativeSequencer::setRotation(int r)
{
    auto& s = strands[0];
    const int requested = r;
    const bool userChanged = requested != s.requestedRotation;
    s.requestedRotation = requested;
    r = positiveModulo(requested, s.numSteps);
    if ((s.fixRotation || userChanged) && r != s.rotation)
    {
        s.rotation = r;
        s.patternDirty = true;
    }
}

void T5ynthGenerativeSequencer::setMutation(float rate)
{
    auto& s = strands[0];
    s.baseMutation = juce::jlimit(0.0f, 1.0f, rate);
    s.mutationRate = s.fixMutation ? s.baseMutation : effectiveMutationFromBase(s);
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
void T5ynthGenerativeSequencer::setPrimaryTransposeSemitones(int semitones)
{
    primaryTransposeSemitones = juce::jlimit(-24, 24, semitones);
}

void T5ynthGenerativeSequencer::setFixSteps(bool f)
{
    auto& s = strands[0];
    s.fixSteps = f;
    if (f && s.numSteps != s.requestedSteps)
    {
        s.numSteps = s.requestedSteps;
        s.numPulses = juce::jlimit(1, s.numSteps, s.requestedPulses);
        s.rotation = positiveModulo(s.requestedRotation, s.numSteps);
        s.patternDirty = true;
    }
}

void T5ynthGenerativeSequencer::setFixPulses(bool f)
{
    auto& s = strands[0];
    s.fixPulses = f;
    const int requested = juce::jlimit(1, s.numSteps, s.requestedPulses);
    if (f && s.numPulses != requested)
    {
        s.numPulses = requested;
        s.patternDirty = true;
    }
}

void T5ynthGenerativeSequencer::setFixRotation(bool f)
{
    auto& s = strands[0];
    s.fixRotation = f;
    const int requested = positiveModulo(s.requestedRotation, s.numSteps);
    if (f && s.rotation != requested)
    {
        s.rotation = requested;
        s.patternDirty = true;
    }
}
void T5ynthGenerativeSequencer::setFixMutation(bool f)
{
    auto& s = strands[0];
    s.fixMutation = f;
    s.mutationRate = f ? s.baseMutation : effectiveMutationFromBase(s);
}

void T5ynthGenerativeSequencer::setBaseMutation(float rate)
{
    auto& s = strands[0];
    s.baseMutation = juce::jlimit(0.0f, 1.0f, rate);
    s.mutationRate = s.fixMutation ? s.baseMutation : effectiveMutationFromBase(s);
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
        s.priorOutputNote      = -1;
        s.previousOutputNote   = -1;
        s.cycleCount           = 0;
        if (!s.patternSeeded) s.patternDirty = true;
    }
    s.enabled = en;
}

void T5ynthGenerativeSequencer::setStrandRole(int idx, int roleIdx)
{
    if (idx < 0 || idx >= MAX_STRANDS) return;
    auto& s = strands[static_cast<size_t>(idx)];
    const auto nextRole = static_cast<Role>(juce::jlimit(0, 3, roleIdx));
    if (s.role != nextRole)
    {
        s.role = nextRole;
        s.patternDirty = true;
    }
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
    const int requested = juce::jlimit(2, MAX_STEPS, n);
    const bool userChanged = requested != s.requestedSteps;
    s.requestedSteps = requested;
    if (s.fixSteps || userChanged)
    {
        if (requested != s.numSteps)
            s.patternDirty = true;
        s.numSteps = requested;
        s.numPulses = juce::jlimit(1, s.numSteps, s.requestedPulses);
        s.rotation = positiveModulo(s.requestedRotation, s.numSteps);
    }
    if (idx == 0) effectiveStepsForGui.store(s.numSteps, std::memory_order_relaxed);
}

void T5ynthGenerativeSequencer::setStrandPulses(int idx, int n)
{
    if (idx < 0 || idx >= MAX_STRANDS) return;
    auto& s = strands[static_cast<size_t>(idx)];
    const int requested = juce::jlimit(1, MAX_STEPS, n);
    const bool userChanged = requested != s.requestedPulses;
    s.requestedPulses = requested;
    n = juce::jlimit(1, s.numSteps, requested);
    if ((s.fixPulses || userChanged) && n != s.numPulses)
    {
        s.numPulses = n;
        s.patternDirty = true;
    }
    if (idx == 0) effectivePulsesForGui.store(s.numPulses, std::memory_order_relaxed);
}

void T5ynthGenerativeSequencer::setStrandRotation(int idx, int n)
{
    if (idx < 0 || idx >= MAX_STRANDS) return;
    auto& s = strands[static_cast<size_t>(idx)];
    const int requested = n;
    const bool userChanged = requested != s.requestedRotation;
    s.requestedRotation = requested;
    n = positiveModulo(requested, s.numSteps);
    if ((s.fixRotation || userChanged) && n != s.rotation)
    {
        s.rotation = n;
        s.patternDirty = true;
    }
}

void T5ynthGenerativeSequencer::setStrandMutation(int idx, float r)
{
    if (idx < 0 || idx >= MAX_STRANDS) return;
    auto& s = strands[static_cast<size_t>(idx)];
    s.baseMutation = juce::jlimit(0.0f, 1.0f, r);
    s.mutationRate = s.fixMutation ? s.baseMutation : effectiveMutationFromBase(s);
    if (idx == 0) effectiveMutationForGui.store(s.mutationRate, std::memory_order_relaxed);
}

void T5ynthGenerativeSequencer::setStrandBaseMutation(int idx, float r)
{
    if (idx < 0 || idx >= MAX_STRANDS) return;
    auto& s = strands[static_cast<size_t>(idx)];
    s.baseMutation = juce::jlimit(0.0f, 1.0f, r);
    s.mutationRate = s.fixMutation ? s.baseMutation : effectiveMutationFromBase(s);
}

void T5ynthGenerativeSequencer::setStrandFixSteps(int idx, bool f)
{
    if (idx < 0 || idx >= MAX_STRANDS) return;
    auto& s = strands[static_cast<size_t>(idx)];
    s.fixSteps = f;
    if (f && s.numSteps != s.requestedSteps)
    {
        s.numSteps = s.requestedSteps;
        s.numPulses = juce::jlimit(1, s.numSteps, s.requestedPulses);
        s.rotation = positiveModulo(s.requestedRotation, s.numSteps);
        s.patternDirty = true;
    }
}

void T5ynthGenerativeSequencer::setStrandFixPulses(int idx, bool f)
{
    if (idx < 0 || idx >= MAX_STRANDS) return;
    auto& s = strands[static_cast<size_t>(idx)];
    s.fixPulses = f;
    const int requested = juce::jlimit(1, s.numSteps, s.requestedPulses);
    if (f && s.numPulses != requested)
    {
        s.numPulses = requested;
        s.patternDirty = true;
    }
}

void T5ynthGenerativeSequencer::setStrandFixRotation(int idx, bool f)
{
    if (idx < 0 || idx >= MAX_STRANDS) return;
    auto& s = strands[static_cast<size_t>(idx)];
    s.fixRotation = f;
    const int requested = positiveModulo(s.requestedRotation, s.numSteps);
    if (f && s.rotation != requested)
    {
        s.rotation = requested;
        s.patternDirty = true;
    }
}

void T5ynthGenerativeSequencer::setStrandFixMutation(int idx, bool f)
{
    if (idx < 0 || idx >= MAX_STRANDS) return;
    auto& s = strands[static_cast<size_t>(idx)];
    s.fixMutation = f;
    s.mutationRate = f ? s.baseMutation : effectiveMutationFromBase(s);
}

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
        pitchField.cyclesUntilChange = n;
    }
}

void T5ynthGenerativeSequencer::setFieldCenterPc(int pc)
{
    pitchField.centerPc = wrapPc(pc);
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
            s.priorOutputNote      = -1;
            s.previousOutputNote   = -1;
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
        s.priorOutputNote = -1;
        s.previousOutputNote = -1;
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
            midi.addEvent(juce::MidiMessage::noteOff(midiChannelForStrand(s), s.lastPlayedNote), sampleOffset);
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
    int baseNote = baseMidiForStrand(s);

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
            // Accent if: downbeat, preceded by longest gap, or a contextual
            // metric group start (e.g. 9 = 3-3-3 / 4-4-1 depending on role).
            s.accentPattern[static_cast<size_t>(i)] =
                (i == 0) || (gapBefore >= maxGapBefore)
                || (strandIndexOf(s) != 0 && isMetricStrongStep(s, i));
        }
    }

    // Warm-up: scramble the deterministic stride walk so the initial
    // pattern is never a boring ascending scale
    for (int w = 0; w < 4; ++w)
        mutateNotes(s, 1.0f, totalDegrees, static_cast<int>(scale), baseNote);

    s.patternDirty      = false;
    enforcePulseInvariant(s);
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
    int baseNote = baseMidiForStrand(s);

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

    enforcePulseInvariant(s);
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

    for (int i = newSteps; i < MAX_STEPS; ++i)
    {
        s.eucPattern[static_cast<size_t>(i)]    = false;
        s.notePattern[static_cast<size_t>(i)]   = 0;
        s.accentPattern[static_cast<size_t>(i)] = false;
        s.degreePattern[static_cast<size_t>(i)] = 0;
    }

    s.numSteps  = newSteps;
    enforcePulseInvariant(s);
    s.stepsDriftAccum += (delta > 0) ? 1 : -1;

    int maxDrift = juce::jmax(1, rangeOctaves);
    if (s.stepsDriftAccum >= maxDrift) s.stepsDriftUp = false;
    else if (s.stepsDriftAccum <= -maxDrift) s.stepsDriftUp = true;
}

// ─── Mutation drift ─────────────────────────────────────────────────────────

void T5ynthGenerativeSequencer::applyMutationDrift(Strand& s)
{
    s.mutationDriftPhase = (s.mutationDriftPhase + 1) % (DRIFT_PERIOD * 2);
    s.mutationRate = effectiveMutationFromBase(s);
}

// ─── Surgical pulse add/remove (preserves Turing mutations) ─────────────────

void T5ynthGenerativeSequencer::addPulse(Strand& s)
{
    if (s.numPulses >= s.numSteps) return;

    auto scale = static_cast<ScaleQuantizer::Scale>(
        juce::jlimit(1, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
    int dpOct = ScaleQuantizer::degreesPerOctave(scale);
    int totalDegrees = dpOct * rangeOctaves;
    int baseNote = baseMidiForStrand(s);

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

int T5ynthGenerativeSequencer::countPulses(const Strand& s) const
{
    int count = 0;
    const int n = juce::jlimit(0, MAX_STEPS, s.numSteps);
    for (int i = 0; i < n; ++i)
        if (s.eucPattern[static_cast<size_t>(i)])
            ++count;
    return count;
}

void T5ynthGenerativeSequencer::enforcePulseInvariant(Strand& s)
{
    s.numSteps = juce::jlimit(2, MAX_STEPS, s.numSteps);
    int actual = countPulses(s);
    if (actual <= 0)
    {
        auto scale = static_cast<ScaleQuantizer::Scale>(
            juce::jlimit(1, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
        s.eucPattern[0] = true;
        s.degreePattern[0] = 0;
        s.notePattern[0] = ScaleQuantizer::degreeToMidi(0, scaleRoot, scale, baseMidiForStrand(s));
        s.accentPattern[0] = true;
        actual = 1;
    }

    for (int i = s.numSteps; i < MAX_STEPS; ++i)
    {
        s.eucPattern[static_cast<size_t>(i)]    = false;
        s.notePattern[static_cast<size_t>(i)]   = 0;
        s.accentPattern[static_cast<size_t>(i)] = false;
        s.degreePattern[static_cast<size_t>(i)] = 0;
    }

    s.numPulses = juce::jlimit(1, s.numSteps, actual);
}

float T5ynthGenerativeSequencer::effectiveMutationFromBase(const Strand& s) const
{
    if (s.fixMutation)
        return juce::jlimit(0.0f, 1.0f, s.baseMutation);

    const float phase = static_cast<float>(s.mutationDriftPhase)
                      / static_cast<float>(DRIFT_PERIOD * 2);
    const float sinVal = std::sin(phase * juce::MathConstants<float>::twoPi);
    const float multiplier = 1.0f + 0.25f * sinVal;
    return juce::jlimit(0.0f, 1.0f, std::max(0.05f, s.baseMutation * multiplier));
}

// ─── Seed from step sequencer (strand 0 only) ───────────────────────────────

void T5ynthGenerativeSequencer::seedFromSteps(const int* midiNotes,
                                               const bool* enabled,
                                               int count)
{
    auto& s = strands[0];
    count = juce::jlimit(2, MAX_STEPS, count);
    s.numSteps = count;
    s.requestedSteps = count;

    // Count active steps → pulses
    int pulseCount = 0;
    for (int i = 0; i < count; ++i)
        if (enabled[i]) pulseCount++;
    s.numPulses = juce::jmax(1, pulseCount);
    s.requestedPulses = s.numPulses;

    s.eucPattern.fill(false);
    s.notePattern.fill(0);
    s.accentPattern.fill(false);
    s.degreePattern.fill(0);

    auto scale = static_cast<ScaleQuantizer::Scale>(
        juce::jlimit(1, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
    int dpOct = ScaleQuantizer::degreesPerOctave(scale);
    int baseNote = baseMidiForStrand(s);

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
    s.baseSteps       = s.numSteps;
    s.driftCycle      = 0;
    s.pulseDriftAccum = 0;
    s.pulseDriftUp    = true;
    s.rotation        = 0;
    s.requestedRotation = 0;
    s.priorOutputNote = -1;
    s.previousOutputNote = -1;
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

// ─── Note selection ────────────────────────────────────────────────────────
//
// The PitchField is the harmonic ground truth. The older scale-degree walk
// supplies contour and register pressure only; roles then project that
// contour into the current field. A final ensemble pass rejects unjustified
// vertical minor-second clashes against currently sounding strands. Line and
// Density may keep such dissonances only as weak-position stepwise passing
// tones, i.e. when there is an actual contrapuntal reason.

int T5ynthGenerativeSequencer::strandIndexOf(const Strand& s) const
{
    for (int i = 0; i < MAX_STRANDS; ++i)
        if (&s == &strands[static_cast<size_t>(i)])
            return i;
    return 0;
}

T5ynthGenerativeSequencer::MetricPath
T5ynthGenerativeSequencer::metricPathForStrand(const Strand& s) const
{
    switch (s.role)
    {
        case Role::Anchor:
            return MetricPath::Gathering;
        case Role::Line:
            return (s.cycleCount % 8 == 7) ? MetricPath::Gathering
                                           : MetricPath::Additive;
        case Role::Density:
            return (s.cycleCount % 4 == 3) ? MetricPath::OpenBreath
                                           : MetricPath::Conversational;
        case Role::Gesture:
            return activeOtherStrandCount(s) <= 0 ? MetricPath::Conversational
                                                  : MetricPath::OpenBreath;
    }
    return MetricPath::Additive;
}

void T5ynthGenerativeSequencer::buildMetricGroups(int steps, MetricPath path,
                                                   int* groups, int* groupCount) const
{
    steps = juce::jlimit(1, MAX_STEPS, steps);
    *groupCount = 0;

    auto push = [&](int len) {
        if (len <= 0 || *groupCount >= MAX_STEPS) return;
        groups[(*groupCount)++] = len;
    };

    auto buildAdditive = [&]() {
        int remaining = steps;
        while (remaining > 0 && *groupCount < MAX_STEPS)
        {
            if (remaining == 1 && *groupCount > 0)
            {
                if (groups[*groupCount - 1] > 2)
                {
                    groups[*groupCount - 1] -= 1;
                    push(2);
                }
                else
                {
                    groups[*groupCount - 1] += 1;
                }
                remaining = 0;
            }
            else if (remaining <= 4)
            {
                push(remaining);
                remaining = 0;
            }
            else
            {
                push(3);
                remaining -= 3;
            }
        }
    };

    switch (path)
    {
        case MetricPath::Gathering:
        {
            if (steps % 3 == 0)
            {
                for (int remaining = steps; remaining > 0; remaining -= 3)
                    push(3);
            }
            else if (steps >= 10)
            {
                int remaining = steps;
                while (remaining > 0)
                {
                    if (remaining > 4) { push(4); remaining -= 4; }
                    else               { push(remaining); remaining = 0; }
                }
            }
            else
            {
                buildAdditive();
            }
            break;
        }

        case MetricPath::Additive:
            buildAdditive();
            break;

        case MetricPath::Conversational:
        {
            int remaining = steps;
            while (remaining > 0 && *groupCount < MAX_STEPS)
            {
                if (remaining > 4) { push(4); remaining -= 4; }
                else               { push(remaining); remaining = 0; }
            }
            break;
        }

        case MetricPath::OpenBreath:
        {
            int remaining = steps;
            if (remaining >= 10)      { push(5); remaining -= 5; }
            else if (remaining >= 7)  { push(4); remaining -= 4; }

            while (remaining > 0 && *groupCount < MAX_STEPS)
            {
                if (remaining == 1)      { push(1); remaining = 0; }
                else if (remaining == 4) { push(2); push(2); remaining = 0; }
                else                     { const int n = juce::jmin(3, remaining); push(n); remaining -= n; }
            }
            break;
        }
    }

    if (*groupCount <= 0)
        push(steps);
}

int T5ynthGenerativeSequencer::metricPhaseOffset(const Strand& s, MetricPath path) const
{
    const int steps = juce::jmax(1, s.numSteps);
    const int idx = strandIndexOf(s);
    switch (path)
    {
        case MetricPath::Gathering:      return 0;
        case MetricPath::Additive:       return positiveModulo(idx, steps);
        case MetricPath::Conversational: return positiveModulo(1 + idx * 2 + s.rotation, steps);
        case MetricPath::OpenBreath:     return positiveModulo(steps / 2 + idx + s.rotation, steps);
    }
    return 0;
}

T5ynthGenerativeSequencer::MetricMoment
T5ynthGenerativeSequencer::metricMomentForStep(const Strand& s, int stepIdx) const
{
    MetricMoment moment;
    moment.path = metricPathForStrand(s);
    moment.downbeat = stepIdx == 0;

    int groups[MAX_STEPS];
    int groupCount = 0;
    buildMetricGroups(s.numSteps, moment.path, groups, &groupCount);

    const int localStep = positiveModulo(stepIdx + metricPhaseOffset(s, moment.path), s.numSteps);
    int pos = 0;
    int groupIndex = 0;
    for (; groupIndex < groupCount; ++groupIndex)
    {
        const int len = groups[groupIndex];
        if (localStep >= pos && localStep < pos + len)
        {
            moment.groupLength = len;
            moment.groupStart = localStep == pos;
            moment.groupEnd = localStep == pos + len - 1;
            moment.anticipatesGroup = len > 1 && localStep == pos + len - 2;
            moment.phraseEnd = groupIndex == groupCount - 1 && moment.groupEnd;
            break;
        }
        pos += len;
    }

    moment.accent = 1.0f;
    if (moment.downbeat)   moment.accent += 0.42f;
    if (moment.groupStart) moment.accent += 0.22f;
    if (moment.groupEnd)   moment.accent -= 0.08f;
    if (moment.groupLength == 1)
        moment.accent -= 0.16f;

    moment.breath = 1.0f;
    if (moment.anticipatesGroup) moment.breath *= 0.92f;
    if (moment.groupEnd)         moment.breath *= 0.78f;
    if (moment.phraseEnd)        moment.breath *= 0.72f;
    if (moment.groupLength == 1) moment.breath *= 0.50f;
    if (moment.path == MetricPath::OpenBreath && moment.phraseEnd)
        moment.breath *= 0.60f;

    return moment;
}

bool T5ynthGenerativeSequencer::isMetricStrongStep(const Strand& s, int stepIdx) const
{
    const auto moment = metricMomentForStep(s, stepIdx);
    return moment.downbeat || moment.groupStart;
}

int T5ynthGenerativeSequencer::activeOtherStrandCount(const Strand& s) const
{
    int count = 0;
    for (const auto& other : strands)
        if (&other != &s && other.enabled && other.lastPlayedNote >= 0)
            ++count;
    return count;
}

int T5ynthGenerativeSequencer::primaryNoteForStep(int stepIdx) const
{
    const auto& primary = strands[0];
    if (!primary.enabled || primary.numSteps <= 0)
        return -1;

    stepIdx = positiveModulo(stepIdx, primary.numSteps);
    const int patternNote = primary.notePattern[static_cast<size_t>(stepIdx)];
    if (patternNote > 0)
        return juce::jlimit(0, 127, patternNote + primaryTransposeSemitones);

    int rawDegree = primary.degreePattern[static_cast<size_t>(stepIdx)];
    if (!primary.eucPattern[static_cast<size_t>(stepIdx)])
    {
        for (int off = 1; off <= primary.numSteps; ++off)
        {
            const int prev = positiveModulo(stepIdx - off, primary.numSteps);
            if (primary.eucPattern[static_cast<size_t>(prev)])
            {
                rawDegree = primary.degreePattern[static_cast<size_t>(prev)];
                break;
            }

            const int next = positiveModulo(stepIdx + off, primary.numSteps);
            if (primary.eucPattern[static_cast<size_t>(next)])
            {
                rawDegree = primary.degreePattern[static_cast<size_t>(next)];
                break;
            }
        }
    }

    auto scale = static_cast<ScaleQuantizer::Scale>(
        juce::jlimit(1, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
    const int baseNote = baseMidiForStrand(primary) + primaryTransposeSemitones;
    return juce::jlimit(0, 127,
        ScaleQuantizer::degreeToMidi(rawDegree, scaleRoot, scale, baseNote));
}

int T5ynthGenerativeSequencer::nextPrimaryPulseStep(int fromStep) const
{
    const auto& primary = strands[0];
    if (!primary.enabled || primary.numSteps <= 0)
        return 0;

    for (int off = 0; off < primary.numSteps; ++off)
    {
        const int step = positiveModulo(fromStep + off, primary.numSteps);
        if (primary.eucPattern[static_cast<size_t>(step)])
            return step;
    }

    return positiveModulo(fromStep, primary.numSteps);
}

T5ynthGenerativeSequencer::EnsembleContext
T5ynthGenerativeSequencer::ensembleContextFor(const Strand& s, int stepIdx, bool /*isStrong*/) const
{
    EnsembleContext context;
    const auto& primary = strands[0];
    if (!primary.enabled || primary.numSteps <= 0)
        return context;

    context.primaryCurrentStep = positiveModulo(primary.currentStep, primary.numSteps);
    context.primaryNextStep = nextPrimaryPulseStep(primary.scheduledStep);
    context.primaryIsActive = primary.lastPlayedNote >= 0;

    context.primaryNote = primary.lastPlayedNote >= 0 ? primary.lastPlayedNote
                        : primary.previousOutputNote >= 0 ? primary.previousOutputNote
                        : primaryNoteForStep(context.primaryCurrentStep);
    context.primaryPreviousNote = primary.priorOutputNote;
    context.primaryNextNote = primaryNoteForStep(context.primaryNextStep);
    context.hasPrimary = context.primaryNote >= 0 || context.primaryNextNote >= 0;

    const int directionFrom = primary.previousOutputNote >= 0 ? primary.previousOutputNote
                            : context.primaryNote;
    const int directionTo = context.primaryNextNote >= 0 ? context.primaryNextNote
                          : context.primaryNote;
    if (directionFrom >= 0 && directionTo >= 0)
    {
        const int delta = directionTo - directionFrom;
        context.primaryDirection = delta > 1 ? 1 : delta < -1 ? -1 : 0;
    }
    else if (primary.priorOutputNote >= 0 && primary.previousOutputNote >= 0)
    {
        const int delta = primary.previousOutputNote - primary.priorOutputNote;
        context.primaryDirection = delta > 1 ? 1 : delta < -1 ? -1 : 0;
    }

    auto primaryStrong = [&](int primaryStep) {
        primaryStep = positiveModulo(primaryStep, primary.numSteps);
        return primaryStep == 0
            || primary.accentPattern[static_cast<size_t>(primaryStep)]
            || isMetricStrongStep(primary, primaryStep);
    };
    context.primaryNowStrong = primaryStrong(context.primaryCurrentStep);
    context.primaryNextStrong = primaryStrong(context.primaryNextStep);

    const int mappedPrimaryStep = positiveModulo(
        juce::roundToInt(static_cast<float>(stepIdx) / static_cast<float>(juce::jmax(1, s.numSteps))
                         * static_cast<float>(primary.numSteps)),
        primary.numSteps);
    const int distToCurrent = circularDistance(mappedPrimaryStep, context.primaryCurrentStep,
                                               primary.numSteps);
    const int distToNext = circularDistance(mappedPrimaryStep, context.primaryNextStep,
                                            primary.numSteps);
    const int phaseDistance = juce::jmin(distToCurrent, distToNext);
    const float halfCycle = static_cast<float>(juce::jmax(1, primary.numSteps / 2));
    context.primaryPhaseAffinity = juce::jlimit(0.0f, 1.0f,
        1.0f - static_cast<float>(phaseDistance) / halfCycle);

    if (primary.accentPattern[static_cast<size_t>(mappedPrimaryStep)])
        context.primaryPhaseAffinity = juce::jmin(1.0f, context.primaryPhaseAffinity + 0.16f);

    for (const auto& other : strands)
    {
        if (!other.enabled || other.lastPlayedNote < 0)
            continue;

        ++context.soundingCount;
        context.soundingPcMask |= 1 << wrapPc(other.lastPlayedNote);
        context.lowestNote = juce::jmin(context.lowestNote, other.lastPlayedNote);
        context.highestNote = juce::jmax(context.highestNote, other.lastPlayedNote);
    }
    context.distinctPcCount = countBits12(static_cast<std::uint16_t>(context.soundingPcMask));

    return context;
}

float T5ynthGenerativeSequencer::primaryRhythmicSupport(const Strand& s, int stepIdx,
                                                         bool isStrong) const
{
    if (strandIndexOf(s) == 0)
        return 1.0f;

    const auto context = ensembleContextFor(s, stepIdx, isStrong);
    if (!context.hasPrimary)
        return 1.0f;

    const auto moment = metricMomentForStep(s, stepIdx);
    const bool structural = isStrong || moment.downbeat || moment.groupStart;
    float factor = 1.0f;

    switch (s.role)
    {
        case Role::Anchor:
            if (structural && (context.primaryNowStrong || context.primaryNextStrong
                               || context.primaryPhaseAffinity > 0.62f))
                factor *= 1.20f;
            if (!structural && context.primaryPhaseAffinity < 0.45f)
                factor *= 0.76f;
            if (moment.phraseEnd)
                factor *= 1.08f;
            break;

        case Role::Line:
            if (!structural && context.primaryPhaseAffinity > 0.72f)
                factor *= 0.72f; // leave V1's local space unless answering structurally
            if (structural && context.primaryDirection != 0)
                factor *= 1.08f;
            if (moment.anticipatesGroup && context.primaryNextStrong)
                factor *= 1.10f;
            break;

        case Role::Density:
            if (!structural && context.distinctPcCount >= 3)
                factor *= 0.52f;
            if (!structural && context.primaryPhaseAffinity > 0.70f)
                factor *= 0.72f;
            if (structural && context.distinctPcCount < 3)
                factor *= 1.12f;
            if (moment.anticipatesGroup && context.primaryNextStrong)
                factor *= 0.62f; // breathe before a likely V1 arrival
            break;

        case Role::Gesture:
            if (context.primaryPhaseAffinity > 0.70f && !structural)
                factor *= 0.64f;
            if (moment.phraseEnd)
                factor *= 1.10f;
            break;
    }

    return juce::jlimit(0.35f, 1.35f, factor);
}

float T5ynthGenerativeSequencer::contextualFireProbability(const Strand& s, int stepIdx,
                                                            bool isPulse,
                                                            bool isStrong) const
{
    if (strandIndexOf(s) == 0)
        return isPulse ? (0.90f + 0.10f * (1.0f - s.mutationRate))
                       : (s.mutationRate * 0.15f);

    const auto moment = metricMomentForStep(s, stepIdx);
    float probability = roleFireProbability(s, isPulse, isStrong);

    probability *= 0.88f + 0.12f * moment.accent;
    probability *= moment.breath;
    probability *= primaryRhythmicSupport(s, stepIdx, isStrong);

    if (isPulse && moment.groupStart)
        probability += 0.06f;
    if (!isPulse && moment.groupEnd)
        probability *= 0.70f;
    if (moment.phraseEnd && s.role != Role::Anchor)
        probability *= 0.74f;

    const int activeOthers = activeOtherStrandCount(s);
    if (activeOthers <= 0)
    {
        if (s.role == Role::Anchor || s.role == Role::Line)
            probability += isStrong ? 0.10f : 0.04f;
        else if (!isPulse)
            probability *= 0.75f;
    }
    else if (activeOthers >= 2)
    {
        switch (s.role)
        {
            case Role::Anchor:
                probability *= isStrong ? 1.04f : 0.92f;
                break;
            case Role::Line:
                probability *= isStrong ? 0.96f : 0.78f;
                break;
            case Role::Density:
                probability *= isStrong ? 0.82f : 0.48f;
                break;
            case Role::Gesture:
                probability *= isStrong ? 0.76f : 0.36f;
                break;
        }
    }

    if (activeOthers >= 3 && !isStrong)
        probability *= 0.38f;

    return juce::jlimit(0.0f, 1.0f, probability);
}

float T5ynthGenerativeSequencer::metricGateScale(const Strand& s, int stepIdx,
                                                  bool isPulse,
                                                  bool isStrong) const
{
    if (strandIndexOf(s) == 0)
        return 1.0f;

    const auto moment = metricMomentForStep(s, stepIdx);
    float scale = 1.0f;

    if (moment.groupStart && isPulse) scale *= 1.08f;
    if (moment.anticipatesGroup)      scale *= 0.90f;
    if (moment.groupEnd)              scale *= 0.84f;
    if (moment.phraseEnd)             scale *= (s.role == Role::Anchor ? 0.95f : 0.68f);
    if (isStrong && s.role == Role::Anchor)
        scale *= 1.10f;

    const int activeOthers = activeOtherStrandCount(s);
    if (activeOthers >= 2 && (s.role == Role::Density || s.role == Role::Gesture))
        scale *= 0.76f;
    if (activeOthers <= 0 && s.role == Role::Anchor)
        scale *= 1.12f;

    return juce::jlimit(0.35f, 1.25f, scale);
}

int T5ynthGenerativeSequencer::midiChannelForStrand(const Strand& s) const
{
    // Channel 2 is reserved by the step sequencer for bind/glide.
    return juce::jlimit(1, 16, 3 + strandIndexOf(s));
}

float T5ynthGenerativeSequencer::spatialTargetForStrand(const Strand& s, int stepIdx,
                                                         bool isPulse,
                                                         bool isStrong) const
{
    const int idx = strandIndexOf(s);
    static constexpr float kLanes[MAX_STRANDS] = { -0.32f, 0.34f, -0.62f, 0.66f };
    const float lane = kLanes[static_cast<size_t>(idx)];
    const float sign = lane >= 0.0f ? 1.0f : -1.0f;
    const auto moment = metricMomentForStep(s, stepIdx);

    float width = 0.45f;
    float motion = 0.06f;
    switch (s.role)
    {
        case Role::Anchor:  width = 0.24f; motion = 0.025f; break;
        case Role::Line:    width = 0.42f; motion = 0.055f; break;
        case Role::Density: width = 0.66f; motion = 0.085f; break;
        case Role::Gesture: width = 0.82f; motion = 0.120f; break;
    }

    float target = sign * width;

    if (moment.path == MetricPath::Gathering)
        target *= 0.72f;
    if (isStrong || moment.groupStart)
        target *= s.role == Role::Anchor ? 0.55f : 0.74f;
    if (moment.phraseEnd || moment.path == MetricPath::OpenBreath)
        target = sign * juce::jmin(0.92f, std::abs(target) + 0.14f + 0.08f * s.mutationRate);

    const int activeOthers = activeOtherStrandCount(s);
    if (activeOthers <= 0)
    {
        target *= 0.76f;
    }
    else if (activeOthers >= 2)
    {
        target = sign * juce::jmin(0.95f, std::abs(target) + 0.08f * static_cast<float>(activeOthers));
    }

    if (!isPulse && !isStrong && (s.role == Role::Density || s.role == Role::Gesture))
        target = sign * juce::jmin(0.95f, std::abs(target) + 0.06f);

    const float phase = static_cast<float>(s.cycleCount) * 0.61f
                      + static_cast<float>(stepIdx) * 0.37f
                      + static_cast<float>(idx) * 1.93f;
    target += std::sin(phase) * motion * (moment.groupEnd ? 1.35f : 1.0f);

    return juce::jlimit(-0.95f, 0.95f, target);
}

float T5ynthGenerativeSequencer::updateSpatialPan(Strand& s, int stepIdx,
                                                   bool isPulse,
                                                   bool isStrong)
{
    s.spatialTargetPan = spatialTargetForStrand(s, stepIdx, isPulse, isStrong);

    float slew = 0.12f;
    switch (s.role)
    {
        case Role::Anchor:  slew = 0.07f; break;
        case Role::Line:    slew = 0.13f; break;
        case Role::Density: slew = 0.20f; break;
        case Role::Gesture: slew = 0.16f; break;
    }
    if (isStrong)
        slew += 0.05f;

    if (s.cycleCount == 0 && s.scheduledStep == 0 && s.previousOutputNote < 0)
        s.spatialPan = s.spatialTargetPan;
    else
        s.spatialPan += (s.spatialTargetPan - s.spatialPan) * juce::jlimit(0.02f, 0.35f, slew);

    return juce::jlimit(-0.95f, 0.95f, s.spatialPan);
}

int T5ynthGenerativeSequencer::panControllerValue(float pan) const
{
    pan = juce::jlimit(-1.0f, 1.0f, pan);
    return juce::jlimit(0, 127, juce::roundToInt((pan * 0.5f + 0.5f) * 127.0f));
}

int T5ynthGenerativeSequencer::baseMidiForStrand(const Strand& s) const
{
    const int idx = strandIndexOf(s);
    if (idx == 0)
        return 48; // Legacy mono-gen voice: C3 base, transposed only by Seq Octave.

    int base = 60;
    switch (s.role)
    {
        case Role::Anchor:  base = 48; break;
        case Role::Line:    base = 60; break;
        case Role::Density: base = 60; break;
        case Role::Gesture: base = 72; break;
    }

    static constexpr int kStrandRegisterOffsets[MAX_STRANDS] = { 0, -7, 5, 12 };
    return juce::jlimit(24, 96, base + kStrandRegisterOffsets[static_cast<size_t>(idx)]);
}

int T5ynthGenerativeSequencer::voiceLedFieldMember(int rawPc) const
{
    const auto set = pitchField.pcSet;
    if (set == 0) return rawPc;
    rawPc = wrapPc(rawPc);
    // Search outward in pc-space for the closest field member; tie goes lower.
    for (int off = 0; off <= 6; ++off)
    {
        int below = wrapPc(rawPc - off);
        int above = (rawPc + off) % 12;
        const bool bBelow = (set >> below) & 1u;
        const bool bAbove = (set >> above) & 1u;
        if (off == 0 && bBelow) return rawPc;
        if (bBelow) return below;
        if (bAbove) return above;
    }
    return rawPc;
}

bool T5ynthGenerativeSequencer::fieldContains(int pc) const
{
    const auto set = pitchField.pcSet;
    return set == 0 || ((set >> wrapPc(pc)) & 1u);
}

int T5ynthGenerativeSequencer::fieldPcForDegree(int degree) const
{
    int pcs[12];
    int count = 0;
    const int sz = juce::jlimit(0, 12, pitchField.rowSize);

    for (int i = 0; i < sz; ++i)
    {
        const int pc = wrapPc(pitchField.row[static_cast<size_t>(i)]);
        if (!fieldContains(pc)) continue;
        bool seen = false;
        for (int j = 0; j < count; ++j)
            if (pcs[j] == pc) { seen = true; break; }
        if (!seen) pcs[count++] = pc;
    }

    for (int pc = 0; pc < 12; ++pc)
    {
        if (!fieldContains(pc)) continue;
        bool seen = false;
        for (int j = 0; j < count; ++j)
            if (pcs[j] == pc) { seen = true; break; }
        if (!seen) pcs[count++] = pc;
    }

    if (count <= 0)
        return wrapPc(scaleRoot);
    return pcs[positiveModulo(degree, count)];
}

int T5ynthGenerativeSequencer::fieldPcNearCenterByIndex(int index) const
{
    int pcs[12];
    int count = 0;
    const int center = wrapPc(pitchField.centerPc);

    auto addIfPresent = [&](int pc) {
        pc = wrapPc(pc);
        if (!fieldContains(pc)) return;
        for (int j = 0; j < count; ++j)
            if (pcs[j] == pc) return;
        pcs[count++] = pc;
    };

    addIfPresent(center);
    for (int off = 1; off <= 6; ++off)
    {
        addIfPresent(center - off);
        addIfPresent(center + off);
    }

    if (count <= 0)
        return center;
    return pcs[positiveModulo(index, count)];
}

int T5ynthGenerativeSequencer::closestMidiForPc(int pc, int anchorMidi) const
{
    pc = wrapPc(pc);
    int midi = anchorMidi - wrapPc(anchorMidi) + pc;
    while (midi - anchorMidi > 6) midi -= 12;
    while (anchorMidi - midi > 6) midi += 12;
    return juce::jlimit(0, 127, midi);
}

int T5ynthGenerativeSequencer::nearestFieldMidi(int preferredMidi, int previousMidi, int fallbackPc) const
{
    const int targetPc = voiceLedFieldMember(fallbackPc);
    int best = closestMidiForPc(targetPc, preferredMidi);
    int bestScore = 99999;
    const int target = previousMidi >= 0 ? previousMidi : preferredMidi;

    for (int midi = 0; midi <= 127; ++midi)
    {
        if (wrapPc(midi) != targetPc) continue;

        // Voice-leading may choose the octave, but it must not replace the
        // step's selected pitch class. Otherwise the line sticks to the
        // previous MIDI note even while the displayed degree changes.
        const int score = std::abs(midi - target) * 2 + std::abs(midi - preferredMidi) * 3;
        if (score < bestScore)
        {
            bestScore = score;
            best = midi;
        }
    }

    return best;
}

int T5ynthGenerativeSequencer::relatedFieldPc(int primaryMidi, Role role, int seed,
                                               bool isStrong) const
{
    if (primaryMidi < 0)
        return voiceLedFieldMember(seed);

    const int primaryPc = wrapPc(primaryMidi);
    const int centerPc = voiceLedFieldMember(pitchField.centerPc);
    const int seedPc = voiceLedFieldMember(seed);

    auto intervalCost = [&](int pc) {
        const int ic = pcDistance(pc, primaryPc);
        switch (role)
        {
            case Role::Anchor:
                if (ic == 0 || ic == 5 || ic == 7) return 0;
                if (ic == 3 || ic == 4 || ic == 8 || ic == 9) return 3;
                if (ic == 2 || ic == 10) return 8;
                if (ic == 6) return 14;
                return 22;

            case Role::Line:
                if (ic == 3 || ic == 4 || ic == 8 || ic == 9) return 0;
                if (ic == 5 || ic == 7) return 2;
                if (ic == 2 || ic == 10) return isStrong ? 6 : 3;
                if (ic == 0) return isStrong ? 8 : 5;
                if (ic == 6) return 11;
                return 14;

            case Role::Density:
                if (ic == 3 || ic == 4 || ic == 8 || ic == 9) return 0;
                if (ic == 5 || ic == 7) return 1;
                if (ic == 2 || ic == 10) return isStrong ? 7 : 2;
                if (ic == 0) return 5;
                if (ic == 6) return 10;
                return 13;

            case Role::Gesture:
                if (ic == 5 || ic == 7) return 0;
                if (ic == 3 || ic == 4 || ic == 8 || ic == 9) return 2;
                if (ic == 2 || ic == 10) return 4;
                if (ic == 6) return 6;
                return 10;
        }
        return 8;
    };

    int bestPc = seedPc;
    int bestScore = 99999;
    for (int pc = 0; pc < 12; ++pc)
    {
        if (!fieldContains(pc))
            continue;

        int score = intervalCost(pc) * 8
                  + pcDistance(pc, seedPc) * 2;
        if (role == Role::Anchor)
            score += pcDistance(pc, centerPc) * 4;
        else if (role == Role::Density)
            score += pcDistance(pc, centerPc);

        if (score < bestScore)
        {
            bestScore = score;
            bestPc = pc;
        }
    }

    return bestPc;
}

int T5ynthGenerativeSequencer::bestFieldMidiNear(int targetMidi, int contourMidi,
                                                  int previousMidi, int seedPc) const
{
    targetMidi = juce::jlimit(0, 127, targetMidi);
    contourMidi = juce::jlimit(0, 127, contourMidi);
    seedPc = voiceLedFieldMember(seedPc);

    int best = nearestFieldMidi(targetMidi, previousMidi, seedPc);
    int bestScore = 99999;
    for (int midi = 0; midi <= 127; ++midi)
    {
        if (!fieldContains(midi))
            continue;

        int score = std::abs(midi - targetMidi) * 5
                  + std::abs(midi - contourMidi) * 2
                  + pcDistance(midi, seedPc) * 3;
        if (previousMidi >= 0)
            score += std::abs(midi - previousMidi) * 3;

        if (score < bestScore)
        {
            bestScore = score;
            best = midi;
        }
    }

    return best;
}

int T5ynthGenerativeSequencer::chromaticPassingNote(int lastMidi, int seedMidi)
{
    if (lastMidi < 0)
        return seedMidi;

    int dir = 0;
    if (seedMidi > lastMidi) dir = 1;
    else if (seedMidi < lastMidi) dir = -1;
    else
    {
        std::uniform_int_distribution<int> dirDist(0, 1);
        dir = dirDist(rng) == 0 ? -1 : 1;
    }

    return juce::jlimit(0, 127, lastMidi + dir);
}

int T5ynthGenerativeSequencer::socialIntervalScore(Role role, int candidate,
                                                    const EnsembleContext& context,
                                                    bool isStrong) const
{
    if (!context.hasPrimary || context.primaryNote < 0)
        return 0;

    const int ic = pcDistance(candidate, context.primaryNote);
    switch (role)
    {
        case Role::Anchor:
        {
            int score = 0;
            if (ic == 0 || ic == 5 || ic == 7) score = 0;
            else if (ic == 3 || ic == 4 || ic == 8 || ic == 9) score = 4;
            else if (ic == 2 || ic == 10) score = 10;
            else if (ic == 6) score = 16;
            else score = 24;
            if (candidate >= context.primaryNote - 1)
                score += 7; // Anchor should normally support below V1, not compete with it.
            return score;
        }

        case Role::Line:
            if (ic == 3 || ic == 4 || ic == 8 || ic == 9) return 0;
            if (ic == 5 || ic == 7) return 2;
            if (ic == 2 || ic == 10) return isStrong ? 8 : 3;
            if (ic == 0) return isStrong ? 9 : 5;
            if (ic == 6) return 12;
            return 15;

        case Role::Density:
            if (ic == 3 || ic == 4 || ic == 8 || ic == 9) return 0;
            if (ic == 5 || ic == 7) return 1;
            if (ic == 2 || ic == 10) return isStrong ? 9 : 3;
            if (ic == 0) return context.distinctPcCount >= 3 ? 10 : 5;
            if (ic == 6) return 12;
            return 15;

        case Role::Gesture:
            if (ic == 5 || ic == 7) return 0;
            if (ic == 3 || ic == 4 || ic == 8 || ic == 9) return 3;
            if (ic == 2 || ic == 10) return 5;
            if (ic == 6) return 8;
            return 12;
    }

    return 0;
}

bool T5ynthGenerativeSequencer::hasHardSmallSecondClash(const Strand& s, int candidate) const
{
    for (const auto& other : strands)
    {
        if (&other == &s || !other.enabled || other.lastPlayedNote < 0)
            continue;

        const int pcInterval = wrapPc(candidate - other.lastPlayedNote);
        const int ic = juce::jmin(pcInterval, 12 - pcInterval);
        if (ic == 1)
            return true;
    }

    return false;
}

bool T5ynthGenerativeSequencer::isJustifiedPassingTone(const Strand& s, int candidate, bool isStrong) const
{
    if (isStrong || s.previousOutputNote < 0)
        return false;

    if (s.role != Role::Line && s.role != Role::Density)
        return false;

    return std::abs(candidate - s.previousOutputNote) <= 2;
}

int T5ynthGenerativeSequencer::ensembleAdjustedNote(const Strand& s,
                                                     const EnsembleContext& context,
                                                     int candidate,
                                                     bool isStrong,
                                                     bool allowPassing) const
{
    candidate = juce::jlimit(0, 127, candidate);

    const int socialScore = socialIntervalScore(s.role, candidate, context, isStrong);
    const int acceptableScore = s.role == Role::Anchor ? 6
                              : s.role == Role::Line ? (isStrong ? 7 : 10)
                              : s.role == Role::Density ? (isStrong ? 6 : 11)
                              : 10;
    if (!hasHardSmallSecondClash(s, candidate) && socialScore <= acceptableScore)
        return candidate;

    if (allowPassing && isJustifiedPassingTone(s, candidate, isStrong))
        return candidate;

    const int reference = s.previousOutputNote >= 0 ? s.previousOutputNote : candidate;
    int best = candidate;
    int bestScore = 99999;

    for (int midi = 0; midi <= 127; ++midi)
    {
        if (!fieldContains(midi) || hasHardSmallSecondClash(s, midi))
            continue;

        const int score = std::abs(midi - candidate) * 3
                        + std::abs(midi - reference) * 2
                        + socialIntervalScore(s.role, midi, context, isStrong) * 7;
        if (score < bestScore)
        {
            bestScore = score;
            best = midi;
        }
    }

    return bestScore < 99999 ? best : candidate;
}

float T5ynthGenerativeSequencer::roleFireProbability(const Strand& s, bool isPulse, bool isStrong) const
{
    switch (s.role)
    {
        case Role::Anchor:
            return isPulse ? (isStrong ? 0.98f : 0.88f) : 0.02f * s.mutationRate;
        case Role::Line:
            return isPulse ? (0.88f + 0.10f * (1.0f - s.mutationRate))
                           : (0.08f + 0.12f * s.mutationRate);
        case Role::Density:
            return isPulse ? 0.96f : (0.12f + 0.30f * s.mutationRate);
        case Role::Gesture:
            return isPulse ? (isStrong ? 0.72f : 0.36f)
                           : (0.01f + 0.05f * s.mutationRate);
    }
    return isPulse ? 0.9f : 0.0f;
}

int T5ynthGenerativeSequencer::roleVelocityBase(const Strand& s, bool isPulse, bool isStrong) const
{
    if (!isPulse)
    {
        switch (s.role)
        {
            case Role::Anchor:  return 44;
            case Role::Line:    return 58;
            case Role::Density: return 48;
            case Role::Gesture: return 52;
        }
    }

    switch (s.role)
    {
        case Role::Anchor:  return isStrong ? 78 : 66;
        case Role::Line:    return isStrong ? 104 : 88;
        case Role::Density: return isStrong ? 70 : 58;
        case Role::Gesture: return isStrong ? 112 : 92;
    }
    return isStrong ? 100 : 85;
}

int T5ynthGenerativeSequencer::velocityForNote(const Strand& s, int stepIdx, int note,
                                                bool isPulse, bool isStrong)
{
    if (strandIndexOf(s) == 0)
    {
        const int baseVel = !isPulse ? 55
                          : s.accentPattern[static_cast<size_t>(stepIdx)] ? 100
                          : 85;
        std::uniform_int_distribution<int> velJitter(-5, 5);
        return juce::jlimit(1, 127, baseVel + velJitter(rng));
    }

    int velocity = roleVelocityBase(s, isPulse, isStrong);
    const auto moment = metricMomentForStep(s, stepIdx);

    if (&s == &strands[0] && s.role == Role::Line)
        velocity += 8; // Voice 1 is the foreground melody.
    else if (&s != &strands[0] && s.role != Role::Gesture)
        velocity -= 5; // accompaniment should not mask the line.

    if (isPulse && s.accentPattern[static_cast<size_t>(stepIdx)])
        velocity += s.role == Role::Line ? 6 : 4;
    if (isPulse && moment.groupStart)
        velocity += juce::roundToInt((moment.accent - 1.0f) * 14.0f);
    if (moment.groupEnd && !moment.downbeat)
        velocity -= 3;
    if (moment.phraseEnd && s.role != Role::Anchor)
        velocity -= 7;

    if (!isPulse)
        velocity -= s.role == Role::Density ? 8 : 12;

    if (s.previousOutputNote >= 0)
    {
        const int melodicDelta = note - s.previousOutputNote;
        const int leap = std::abs(melodicDelta);

        if (leap == 0)
            velocity -= 6;
        else if (leap >= 7)
            velocity += s.role == Role::Gesture ? 6 : 3;

        if (s.role == Role::Line)
        {
            if (melodicDelta > 0) velocity += 3;
            else if (melodicDelta < 0) velocity -= 1;
        }
    }

    const int activeOthers = activeOtherStrandCount(s);
    if (activeOthers <= 0 && (s.role == Role::Anchor || s.role == Role::Line))
        velocity += 3;
    else if (activeOthers >= 2 && !isStrong)
        velocity -= (s.role == Role::Density || s.role == Role::Gesture) ? 8 : 3;

    const int jitterRange = s.role == Role::Density ? 3
                         : s.role == Role::Gesture ? 5
                         : 2;
    std::uniform_int_distribution<int> velJitter(-jitterRange, jitterRange);
    return juce::jlimit(1, 127, velocity + velJitter(rng));
}

float T5ynthGenerativeSequencer::roleGateFraction(const Strand& s) const
{
    if (strandIndexOf(s) == 0)
        return gate_;

    switch (s.role)
    {
        case Role::Anchor:  return juce::jlimit(0.10f, 1.35f, gate_ * 1.20f);
        case Role::Line:    return juce::jlimit(0.10f, 1.00f, gate_);
        case Role::Density: return juce::jlimit(0.08f, 0.45f, gate_ * 0.42f);
        case Role::Gesture: return juce::jlimit(0.08f, 0.70f, gate_ * 0.58f);
    }
    return gate_;
}

int T5ynthGenerativeSequencer::pickNote(Strand& s, int stepIdx, int rawDegree)
{
    auto scale = static_cast<ScaleQuantizer::Scale>(
        juce::jlimit(1, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));

    if (strandIndexOf(s) == 0)
    {
        const int patternNote = s.notePattern[static_cast<size_t>(stepIdx)];
        if (patternNote > 0)
            return juce::jlimit(0, 127, patternNote + primaryTransposeSemitones);

        const int baseNote = baseMidiForStrand(s) + primaryTransposeSemitones;
        return juce::jlimit(0, 127,
            ScaleQuantizer::degreeToMidi(rawDegree, scaleRoot, scale, baseNote));
    }

    const bool isStrong = (stepIdx == 0)
                       || s.accentPattern[static_cast<size_t>(stepIdx)]
                       || isMetricStrongStep(s, stepIdx);
    const int contourBase = baseMidiForStrand(s) + s.octaveShift * 12;
    const int contourMidi = ScaleQuantizer::degreeToMidi(rawDegree, scaleRoot, scale, contourBase);
    const auto context = ensembleContextFor(s, stepIdx, isStrong);

    std::uniform_real_distribution<float> probDist(0.0f, 1.0f);
    const bool pullToCenter = isStrong && s.chordToneDominance > 0.0f
                           && probDist(rng) < s.chordToneDominance;

    int note = contourMidi;
    bool allowPassing = false;

    switch (s.role)
    {
        case Role::Anchor:
        {
            const int anchorBase = baseMidiForStrand(s) + s.octaveShift * 12;
            const auto moment = metricMomentForStep(s, stepIdx);
            const bool settleToCenter = (stepIdx == 0 && ((s.cycleCount % 2) == 0 || pullToCenter))
                                     || (moment.groupStart && context.primaryNextStrong);
            int pc = voiceLedFieldMember(pitchField.centerPc);
            int target = anchorBase;

            if (context.hasPrimary)
            {
                const int primaryRef = (context.primaryNextNote >= 0
                                        && (context.primaryNextStrong || context.primaryNote < 0))
                                     ? context.primaryNextNote
                                     : context.primaryNote;
                pc = settleToCenter
                   ? relatedFieldPc(primaryRef, Role::Anchor, pitchField.centerPc, true)
                   : relatedFieldPc(primaryRef, Role::Anchor,
                                    fieldPcNearCenterByIndex(rawDegree + stepIdx + s.cycleCount * 2),
                                    isStrong);
                target = primaryRef - (isStrong || moment.groupStart ? 12 : 7);
            }
            else if (!settleToCenter)
            {
                pc = fieldPcNearCenterByIndex(rawDegree + stepIdx + s.cycleCount * 2);
            }

            note = nearestFieldMidi(closestMidiForPc(pc, target),
                                    s.previousOutputNote, pc);
            if (context.primaryNote >= 0 && note >= context.primaryNote - 1)
                note = juce::jlimit(0, 127, note - 12);
            if (!settleToCenter && s.previousOutputNote >= 0
                && wrapPc(note) == wrapPc(s.previousOutputNote)
                && (moment.groupStart || moment.groupEnd))
            {
                pc = context.hasPrimary
                   ? relatedFieldPc(context.primaryNote, Role::Anchor,
                                    fieldPcNearCenterByIndex(rawDegree + stepIdx + s.cycleCount * 2 + 1),
                                    isStrong)
                   : fieldPcNearCenterByIndex(rawDegree + stepIdx + s.cycleCount * 2 + 1);
                note = nearestFieldMidi(closestMidiForPc(pc, target),
                                        s.previousOutputNote, pc);
            }
            break;
        }

        case Role::Line:
        {
            const int lineSourceMidi = juce::jlimit(0, 127,
                s.notePattern[static_cast<size_t>(stepIdx)] > 0
                    ? s.notePattern[static_cast<size_t>(stepIdx)] + s.octaveShift * 12
                    : contourMidi);
            int pc = pullToCenter ? voiceLedFieldMember(pitchField.centerPc)
                                  : voiceLedFieldMember(lineSourceMidi);
            int target = lineSourceMidi;

            if (context.hasPrimary)
            {
                const int primaryTarget = context.primaryNextNote >= 0
                                        ? context.primaryNextNote
                                        : context.primaryNote;
                const int contraryOffset = context.primaryDirection > 0 ? -4
                                         : context.primaryDirection < 0 ? 4
                                         : (((rawDegree + stepIdx) & 1) ? 7 : -5);
                target = primaryTarget + contraryOffset;
                pc = relatedFieldPc(primaryTarget, Role::Line, target, isStrong);
            }

            const int preferred = closestMidiForPc(pc, target);
            note = bestFieldMidiNear(preferred, lineSourceMidi, s.previousOutputNote, pc);
            allowPassing = true;
            break;
        }

        case Role::Density:
        {
            int pc = pullToCenter
                   ? voiceLedFieldMember(pitchField.centerPc)
                   : fieldPcForDegree(rawDegree + stepIdx);
            int target = contourMidi;

            if (context.hasPrimary)
            {
                const int primaryTarget = context.primaryNextNote >= 0
                                        ? context.primaryNextNote
                                        : context.primaryNote;
                pc = relatedFieldPc(primaryTarget, Role::Density, pc, isStrong);
                target = context.distinctPcCount < 3
                       ? primaryTarget + (((rawDegree + stepIdx) & 1) ? 4 : -3)
                       : contourMidi;
            }

            const int preferred = closestMidiForPc(pc, target);
            note = (!isStrong && s.previousOutputNote >= 0)
                 ? chromaticPassingNote(s.previousOutputNote, preferred)
                 : bestFieldMidiNear(preferred, contourMidi, s.previousOutputNote, pc);
            allowPassing = !isStrong;
            break;
        }

        case Role::Gesture:
        {
            const int pc = pullToCenter ? voiceLedFieldMember(pitchField.centerPc)
                                        : fieldPcForDegree(rawDegree * 3 + stepIdx * 2);
            const int gestureBase = (((rawDegree + stepIdx) & 1) ? contourMidi + 12 : contourMidi);
            note = closestMidiForPc(pc, gestureBase);
            if (s.previousOutputNote >= 0 && std::abs(note - s.previousOutputNote) < 5)
                note = juce::jlimit(0, 127, note + (note >= s.previousOutputNote ? 12 : -12));
            break;
        }
    }

    return ensembleAdjustedNote(s, context, note, isStrong, allowPassing);
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
                midi.addEvent(juce::MidiMessage::noteOff(midiChannelForStrand(st), st.lastPlayedNote), 0);
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
                midi.addEvent(juce::MidiMessage::noteOff(midiChannelForStrand(s), s.lastPlayedNote), eventPos);
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
            s.cycleCount++;
            if (&s == &strands[0])
                advancePitchField();
        }

        // Note-off for this strand's previous note
        if (s.lastPlayedNote >= 0)
        {
            midi.addEvent(juce::MidiMessage::noteOff(midiChannelForStrand(s), s.lastPlayedNote), eventPos);
            s.lastPlayedNote = -1;
        }

        // Determine if this step should fire
        const bool isPulse = s.eucPattern[static_cast<size_t>(stepIdx)];
        const bool isStrong = (stepIdx == 0)
                           || s.accentPattern[static_cast<size_t>(stepIdx)]
                           || isMetricStrongStep(s, stepIdx);
        const float fireProb = contextualFireProbability(s, stepIdx, isPulse, isStrong);

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
            const double stepDur = shuffledStrandStepDurationSamples(s, stepIdx);
            const int channel = midiChannelForStrand(s);
            const float pan = updateSpatialPan(s, stepIdx, isPulse, isStrong);

            const int velInt = velocityForNote(s, stepIdx, note, isPulse, isStrong);
            midi.addEvent(juce::MidiMessage::controllerEvent(channel, 10, panControllerValue(pan)), eventPos);
            midi.addEvent(juce::MidiMessage::noteOn(channel, note,
                          static_cast<juce::uint8>(velInt)), eventPos);

            s.lastPlayedNote       = note;
            s.priorOutputNote      = s.previousOutputNote;
            s.previousOutputNote   = note;
            s.samplesUntilGateOff  = static_cast<double>(roleGateFraction(s)
                                      * metricGateScale(s, stepIdx, isPulse, isStrong)) * stepDur;
        }
        else
        {
            s.samplesUntilGateOff = -1.0;
        }

        s.currentStep = stepIdx;
        if (&s == &strands[0])
            currentStepForGui.store(s.currentStep, std::memory_order_relaxed);
        s.scheduledStep++;
        s.samplesUntilNextStep += shuffledStrandStepDurationSamples(s, stepIdx);
    }
}

// ─── Pitch-field evolution ─────────────────────────────────────────────────
//
// Four modes drive the shared pc-set:
//   Static    — no change
//   Drift     — swap one pc for another per tick
//   Transform — twelve-tone row operations: Tn / In / R / RI
//   Pivot     — transpose the entire pc-set by pivotInterval semitones
//
// advancePitchField() is called on strand 0's cycle boundary only.

void T5ynthGenerativeSequencer::rebuildPcSetFromScale()
{
    auto scale = static_cast<ScaleQuantizer::Scale>(
        juce::jlimit(1, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
    pitchField.pcSet = ScaleQuantizer::pcSetFromScale(scale, scaleRoot);
    pitchField.centerPc = wrapPc(scaleRoot);
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
    syncRowFromPcSet();
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
    syncRowFromPcSet();
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
                wrapPc(pitchField.row[static_cast<size_t>(i)] + amount);
    };
    auto invert = [&]() {
        for (int i = 0; i < sz; ++i)
            pitchField.row[static_cast<size_t>(i)] =
                wrapPc(12 - pitchField.row[static_cast<size_t>(i)]);
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
        newSet |= static_cast<std::uint16_t>(1u << wrapPc(pitchField.row[static_cast<size_t>(i)]));
    // Guard against collapsed row (all identical pcs) — keep previous if new is too small.
    if (countBits12(newSet) >= 3)
        pitchField.pcSet = newSet;
    // Keep centerPc consistent: if it dropped out, shift to row[0].
    if (!((pitchField.pcSet >> pitchField.centerPc) & 1u))
        pitchField.centerPc = wrapPc(pitchField.row[0]);
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

    const int sz = juce::jlimit(3, 12, pitchField.rowSize);
    for (int i = 0; i < sz; ++i)
        pitchField.row[static_cast<size_t>(i)] = wrapPc(pitchField.row[static_cast<size_t>(i)] + iv);
}

void T5ynthGenerativeSequencer::syncRowFromPcSet()
{
    int written = 0;
    const int center = wrapPc(pitchField.centerPc);

    auto writeIfPresent = [&](int pc) {
        pc = wrapPc(pc);
        if (!((pitchField.pcSet >> pc) & 1u)) return;
        for (int i = 0; i < written; ++i)
            if (pitchField.row[static_cast<size_t>(i)] == pc) return;
        pitchField.row[static_cast<size_t>(written++)] = pc;
    };

    writeIfPresent(center);
    for (int off = 1; off <= 6 && written < 12; ++off)
    {
        writeIfPresent(center - off);
        if (written < 12)
            writeIfPresent(center + off);
    }

    for (int pc = 0; pc < 12 && written < 12; ++pc)
        writeIfPresent(pc);

    pitchField.rowSize = juce::jlimit(3, 12, written);
    for (int pc = 0; written < 12 && pc < 12; ++pc)
        pitchField.row[static_cast<size_t>(written++)] = pc;
}
