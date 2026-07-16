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

    // Strand 0 is the ensemble's reference clock and stays rigid.
    if (strandIndexOf(s) == 0)
        return base * factor;

    // Metric elasticity (restored 2026-07-16; removed unauthorized in
    // b826a898): secondary strands breathe around their metric grouping —
    // group starts and phrase ends stretch, the step anticipating a group
    // compresses. Ground: the grouping structure itself (structural) and the
    // role (semantic — Anchor stays steadier, a crowded ensemble damps the
    // rubato so the weave keeps its lattice).
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
    // Scale 0 (key "off") is labelled "Chromatic" in the UI and the
    // quantizer tables treat it as the full 12-tone scale — so the
    // generative mode does too. (It used to coerce 0 -> Major; removed
    // 2026-07-16: the sequencer must do what the label says.)
    type = juce::jlimit(0, static_cast<int>(ScaleQuantizer::COUNT) - 1, type);
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
        s.stance               = Stance::Independent;
        stanceForGui[static_cast<size_t>(idx)].store(0, std::memory_order_relaxed);
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
    strands[static_cast<size_t>(idx)].divisionMultiplier = juce::jlimit(1.0f / 16.0f, 16.0f, mult);
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

// ─── Coordination setters ──────────────────────────────────────────────────

void T5ynthGenerativeSequencer::setCoordinationMode(int mode)
{
    const auto next = static_cast<CoordinationMode>(juce::jlimit(0, 2, mode));
    if (next != coordinationMode)
    {
        coordinationMode = next;
        // Stances exist only inside Dialog; entering OR leaving the mode
        // starts everyone from Independent (= today's behavior).
        resetStances();
    }
}

void T5ynthGenerativeSequencer::setCoordinationCap(int cap)
{
    coordinationCap = juce::jlimit(1, MAX_STRANDS, cap);
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

void T5ynthGenerativeSequencer::allNotesOff(std::vector<VoiceEvent>& out, int sampleOffset)
{
    for (auto& s : strands)
    {
        if (s.lastPlayedNote >= 0)
        {
            out.push_back({ sampleOffset, VoiceEvent::Type::NoteOff, s.lastPlayedNote, 0.0f,
                            VoiceEvent::Articulation::Normal, strandIndexOf(s), 0.0f });
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
        juce::jlimit(0, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
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
            // V5: accent emerges from Euclidean structure alone — downbeat
            // and longest-gap-pulse, exactly as strand 0 has always done.
            s.accentPattern[static_cast<size_t>(i)] =
                (i == 0) || (gapBefore >= maxGapBefore);
        }
    }

    // Warm-up: scramble the deterministic stride walk so the initial
    // pattern is never a boring ascending scale
    for (int w = 0; w < 4; ++w)
        mutateNotes(s, 1.0f, totalDegrees, static_cast<int>(scale), baseNote, 0);

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
                                             int scaleEnum, int baseNote, int stanceDir)
{
    auto scale = static_cast<ScaleQuantizer::Scale>(scaleEnum);
    int pulseIndices[MAX_STEPS];
    int pulseCount = 0;
    for (int i = 0; i < s.numSteps; ++i)
        if (s.eucPattern[static_cast<size_t>(i)])
            pulseIndices[pulseCount++] = i;

    int numMutations = juce::jmax(1, juce::roundToInt(
        rate * static_cast<float>(pulseCount) * 0.6f));

    int lastIdx = -1, lastOld = 0, lastNew = 0, lastDir = 0, lastJump = 0;

    if (pulseCount > 0)
    {
        std::uniform_int_distribution<int> pickDist(0, pulseCount - 1);
        std::uniform_int_distribution<int> jumpDist(1, 4);
        std::uniform_int_distribution<int> dirDist(0, 1);

        for (int m = 0; m < numMutations; ++m)
        {
            int idx = pulseIndices[pickDist(rng)];
            // Dialog stance may direct the walk along/against strand 0's
            // last melodic direction; jump size stays Turing either way.
            // stanceDir == 0 (all non-Dialog paths) = free Turing direction.
            int dir = stanceDir != 0 ? stanceDir
                                     : (dirDist(rng) == 0 ? -1 : 1);
            int jump = jumpDist(rng);
            int oldNote = s.notePattern[static_cast<size_t>(idx)];
            int newDeg = s.degreePattern[static_cast<size_t>(idx)] + dir * jump;
            newDeg = ((newDeg % totalDegrees) + totalDegrees) % totalDegrees;
            s.degreePattern[static_cast<size_t>(idx)] = newDeg;
            s.notePattern[static_cast<size_t>(idx)] =
                ScaleQuantizer::degreeToMidi(newDeg, scaleRoot, scale, baseNote);
            lastIdx = idx; lastOld = oldNote; lastNew = s.notePattern[static_cast<size_t>(idx)];
            lastDir = (dir > 0 ? 1 : 0); lastJump = jump;
        }

        // A NoteJump is the lowest-priority monitor event: note mutation fires
        // almost every cycle, so it is recorded ONLY when no structural drift
        // (rotation/pulse/steps) already claimed evtOp this cycle — otherwise it
        // would mask the rarer, more salient structural changes.
        if (s.evtOp == (int) MutEvent::None && lastIdx >= 0)
        {
            s.evtOp = (int) MutEvent::NoteJump; s.evtStep = lastIdx;
            s.evtOld = lastOld; s.evtNew = lastNew; s.evtA = lastDir; s.evtB = lastJump;
        }
    }
}

static_assert (std::atomic<std::uint64_t>::is_always_lock_free,
               "mutationEventForGui must be lock-free for RT use");

std::uint64_t T5ynthGenerativeSequencer::packMutEvent (unsigned gen, int op, int step,
                                                       int oldN, int newN, int a, int b) noexcept
{
    auto u6 = [](int v){ return (std::uint64_t) juce::jlimit(0, 63,  v); };
    auto u7 = [](int v){ return (std::uint64_t) juce::jlimit(0, 127, v); };
    std::uint64_t w = (std::uint64_t) (op & 0xF);
    w |= u6 (step) << 4;
    w |= u7 (oldN) << 10;
    w |= u7 (newN) << 17;
    w |= u6 (a)    << 24;
    w |= u6 (b)    << 30;
    w |= ((std::uint64_t) gen & 0xFFFFFFFu) << 36;
    return w;
}

void T5ynthGenerativeSequencer::mutatePattern(Strand& s)
{
    if (s.mutationRate <= 0.0f) return;
    s.evtOp = (int) MutEvent::None;

    auto scale = static_cast<ScaleQuantizer::Scale>(
        juce::jlimit(0, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
    int dpOct = ScaleQuantizer::degreesPerOctave(scale);
    int totalDegrees = dpOct * rangeOctaves;
    int baseNote = baseMidiForStrand(s);

    // ── Euclidean drift: rotation + pulse count evolve per cycle ──
    applyEuclideanDrift(s);

    // ── Note mutation: Turing Machine — mutate notes per cycle ──
    mutateNotes(s, s.mutationRate, totalDegrees, static_cast<int>(scale), baseNote,
                stanceMutationDir(s));

    publishStrandToGui(s);

    // Publish ONE mutation event per cycle for the GUI monitor (strand 0 only).
    // Among structural drifts the call order in applyEuclideanDrift is ascending
    // priority — rotation, then pulse, then steps — so the last writer (the most
    // structural change of the cycle) wins; NoteJump is the guarded fallback
    // above. If that ordering changes, this priority changes with it.
    if (&s == &strands[0] && s.evtOp != (int) MutEvent::None)
        mutationEventForGui.store (
            packMutEvent (++mutationEventGen_, s.evtOp, s.evtStep,
                          s.evtOld, s.evtNew, s.evtA, s.evtB),
            std::memory_order_release);
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
            s.evtOp = (int) MutEvent::Rotate; s.evtStep = 0;
            s.evtOld = 0; s.evtNew = 0; s.evtA = rotPulses; s.evtB = driftIdx;
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

    s.evtOp   = (int) (delta > 0 ? MutEvent::StepsUp : MutEvent::StepsDown);
    s.evtStep = 0; s.evtOld = 0; s.evtNew = 0;
    s.evtA    = s.numSteps;   // old
    s.evtB    = newSteps;     // new
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
        juce::jlimit(0, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
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
    s.evtOp = (int) MutEvent::PulseAdd; s.evtStep = insertIdx;
    s.evtOld = 0; s.evtNew = s.notePattern[static_cast<size_t>(insertIdx)];
    s.evtA = bestStart; s.evtB = bestGap;
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

    int removedNote = s.notePattern[static_cast<size_t>(bestIdx)];
    s.eucPattern[static_cast<size_t>(bestIdx)]    = false;
    s.notePattern[static_cast<size_t>(bestIdx)]   = 0;
    s.degreePattern[static_cast<size_t>(bestIdx)] = 0;
    s.accentPattern[static_cast<size_t>(bestIdx)] = false;
    s.numPulses--;
    s.evtOp = (int) MutEvent::PulseRemove; s.evtStep = bestIdx;
    s.evtOld = removedNote; s.evtNew = 0; s.evtA = bestGap; s.evtB = 0;
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
            juce::jlimit(0, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
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
        juce::jlimit(0, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
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

    std::uint32_t eucMask = 0;
    for (int i = 0; i < s.numSteps && i < MAX_STEPS; ++i)
        if (s.eucPattern[static_cast<size_t>(i)]) eucMask |= (1u << i);
    eucPatternForGui.store(eucMask, std::memory_order_relaxed);
}

// ─── Note selection ────────────────────────────────────────────────────────
//
// All strands run the same pipeline (V1): Euclid → Turing degree → field PC
// (or scale-degree for strand 0) → MIDI in the strand's register. There are
// no interval-cost tables, no role-specific cost lookups, no ensemble-
// negotiation pass. WHICH notes fire stays interaction-free: pickNote and
// fireProbability never read other strands; note-level coordination lives
// only in the CoordinationMode dispatch (currently DensityBudget), post-hoc
// in processBlock. The expressive staging (metric elasticity, stage panning,
// foreground bias) MAY read ensemble state — it shapes how a note is placed
// in time/space/dynamics, never whether or which note sounds.

int T5ynthGenerativeSequencer::strandIndexOf(const Strand& s) const
{
    for (int i = 0; i < MAX_STRANDS; ++i)
        if (&s == &strands[static_cast<size_t>(i)])
            return i;
    return 0;
}

int T5ynthGenerativeSequencer::rolePriority(const Strand& s) const
{
    // Strand 0 is the reference — DensityBudget never displaces it.
    // Otherwise Role enum order doubles as priority (Anchor=0 highest,
    // Gesture=3 lowest), so a higher index drops first.
    if (strandIndexOf(s) == 0) return -1;
    return static_cast<int>(s.role);
}

// ─── Metric grouping ────────────────────────────────────────────────────────
//
// The grouping layer phrases a strand's steps into additive groups and asks,
// per step, "where am I in the phrase?". It feeds ONLY the expressive
// staging (elasticity, stage panning) — never note choice, accents or firing,
// which stay purely Euclidean (V5). Restored 2026-07-16 together with the
// elasticity/pan layers (removed unauthorized in b826a898).

int T5ynthGenerativeSequencer::activeOtherStrandCount(const Strand& s) const
{
    int count = 0;
    for (const auto& other : strands)
        if (&other != &s && other.enabled && other.lastPlayedNote >= 0)
            ++count;
    return count;
}

T5ynthGenerativeSequencer::MetricPath
T5ynthGenerativeSequencer::metricPathForStrand(const Strand& s) const
{
    // Semantic ground: the role picks the phrasing character. Line and
    // Density periodically change theirs over the cycle count; Gesture
    // phrases conversationally when alone, airily against a full stage.
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
    // Actors don't phrase in lockstep: each strand's grouping is rotated by
    // its identity (index) and its Euclidean rotation, so group boundaries
    // interlock instead of stacking.
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
    for (int groupIndex = 0; groupIndex < groupCount; ++groupIndex)
    {
        const int len = groups[groupIndex];
        if (localStep >= pos && localStep < pos + len)
        {
            moment.groupStart = localStep == pos;
            moment.groupEnd = localStep == pos + len - 1;
            moment.anticipatesGroup = len > 1 && localStep == pos + len - 2;
            moment.phraseEnd = groupIndex == groupCount - 1 && moment.groupEnd;
            break;
        }
        pos += len;
    }

    return moment;
}

bool T5ynthGenerativeSequencer::isMetricStrongStep(const Strand& s, int stepIdx) const
{
    const auto moment = metricMomentForStep(s, stepIdx);
    return moment.downbeat || moment.groupStart;
}

// ─── Dialog stances ─────────────────────────────────────────────────────────
//
// See the Stance doc in the header for the full derivation. Everything here
// is deterministic: the WHEN comes from the strand's own metric phrase ends,
// the WHAT from its pulse-drift pendulum, the direction material from
// strand 0's already-tracked previous/prior output notes.

std::uint64_t T5ynthGenerativeSequencer::packStanceEvent (unsigned gen, int strand,
                                                          int stance, int accum) noexcept
{
    std::uint64_t w = (std::uint64_t) (strand & 0x7);
    w |= ((std::uint64_t) (stance & 0x3)) << 3;
    w |= ((std::uint64_t) juce::jlimit(0, 2047, accum + 1024)) << 5;
    w |= ((std::uint64_t) gen & 0xFFFFFFFFFFFFULL) << 16;
    return w;
}

int T5ynthGenerativeSequencer::stanceMutationDir(const Strand& s) const
{
    if (coordinationMode != CoordinationMode::Dialog) return 0;
    if (strandIndexOf(s) == 0) return 0;                 // the reference has no stance
    if (s.stance == Stance::Independent) return 0;

    const auto& ref = strands[0];
    if (ref.previousOutputNote < 0 || ref.priorOutputNote < 0) return 0;
    const int d = ref.previousOutputNote - ref.priorOutputNote;
    const int refDir = (d > 0) - (d < 0);
    if (refDir == 0) return 0;                           // repeated note: free walk
    return s.stance == Stance::Follow ? refDir : -refDir;
}

void T5ynthGenerativeSequencer::updateStanceAtPhraseEnd(Strand& s)
{
    const Stance next = s.pulseDriftAccum > 0 ? Stance::Counter
                      : s.pulseDriftAccum < 0 ? Stance::Follow
                      :                         Stance::Independent;
    if (next == s.stance) return;

    s.stance = next;
    const int idx = strandIndexOf(s);
    stanceForGui[static_cast<size_t>(idx)].store(static_cast<int>(next),
                                                 std::memory_order_relaxed);
    stanceEventForGui.store(
        packStanceEvent(++stanceEventGen_, idx, static_cast<int>(next), s.pulseDriftAccum),
        std::memory_order_release);
}

void T5ynthGenerativeSequencer::resetStances()
{
    for (int i = 0; i < MAX_STRANDS; ++i)
    {
        strands[static_cast<size_t>(i)].stance = Stance::Independent;
        stanceForGui[static_cast<size_t>(i)].store(0, std::memory_order_relaxed);
    }
}

float T5ynthGenerativeSequencer::fireProbability(const Strand& s, bool isPulse) const
{
    // V4: identical formula for every strand. Symmetric to strand 0's
    // legacy branch. Inter-strand coordination happens AFTER this decision
    // (CoordinationMode dispatch in processBlock), not inside it.
    return isPulse ? (0.90f + 0.10f * (1.0f - s.mutationRate))
                   : (s.mutationRate * 0.15f);
}



float T5ynthGenerativeSequencer::spatialTargetForStrand(const Strand& s, int stepIdx,
                                                         bool isPulse,
                                                         bool isStrong) const
{
    // Stage panning (restored 2026-07-16; removed unauthorized in b826a898):
    // the strands are actors on a stage. The lane fixes an actor's SIDE, the
    // role fixes how far from centre their territory extends (width) and how
    // lively they are (motion). Metric moments move them — strong beats and
    // gathering phrases pull toward centre, phrase ends and open-breath
    // phrasing step outward. Ensemble density stages the whole cast: an actor
    // alone drifts toward centre stage, a full stage spreads everyone wider;
    // Density/Gesture ghost notes flicker at the edge of their territory.
    // A slow role-scaled oscillation keeps actors alive between cues.
    // S5 is the centre actor: no side (sign 0), so all lateral staging
    // vanishes and only the oscillation around centre stage remains.
    const int idx = strandIndexOf(s);
    static constexpr float kLanes[MAX_STRANDS] = { -0.32f, 0.34f, -0.62f, 0.66f, 0.0f };
    const float lane = kLanes[static_cast<size_t>(idx)];
    const float sign = lane > 0.0f ? 1.0f : (lane < 0.0f ? -1.0f : 0.0f);
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

    // Actors walk, they don't teleport: per-note slew toward the target,
    // with role-typed inertia (Anchor deliberate, Density agile).
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

    // Default register stagger relative to the role base. S2 is offset an
    // octave below the role base — the deep-bass slot sits at the LOWEST
    // strand number, not the highest (BJ 2026-07-16); S5 takes the fifth
    // below so the {+5, +12} upper family keeps a lower counterweight.
    static constexpr int kStrandRegisterOffsets[MAX_STRANDS] = { 0, -12, 5, 12, -7 };
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

int T5ynthGenerativeSequencer::closestMidiForPc(int pc, int anchorMidi) const
{
    pc = wrapPc(pc);
    int midi = anchorMidi - wrapPc(anchorMidi) + pc;
    while (midi - anchorMidi > 6) midi -= 12;
    while (anchorMidi - midi > 6) midi += 12;
    return juce::jlimit(0, 127, midi);
}

int T5ynthGenerativeSequencer::velocityForNote(const Strand& s, int stepIdx, bool isPulse)
{
    // Every strand uses the same accent + jitter formula (V3); the per-role
    // mean fixes a CHARACTER (Anchor steady-mid, Line slightly assertive,
    // Density quieter to fit between events, Gesture punctuating) and the
    // ranges overlap heavily — the roles are textures, not a mix hierarchy.
    int mean;
    if (strandIndexOf(s) == 0)
        mean = 92;     // strand 0 default — preserves legacy 100/85/55 spread exactly
    else
    {
        switch (s.role)
        {
            case Role::Anchor:  mean = 80; break;
            case Role::Line:    mean = 88; break;
            case Role::Density: mean = 75; break;
            case Role::Gesture: mean = 95; break;
            default:            mean = 88; break;
        }
    }

    const bool accent = s.accentPattern[static_cast<size_t>(stepIdx)];
    int baseVel;
    if (strandIndexOf(s) == 0)
    {
        // Bytewise-identical to the legacy strand-0 formula.
        baseVel = !isPulse ? 55 : (accent ? 100 : 85);
    }
    else
    {
        baseVel = !isPulse ? mean - 30
                : accent   ? mean + 12
                :            mean;

        // Foreground bias (restored 2026-07-16; removed unauthorized in
        // b826a898). Semantic ground: strand 0 IS the ensemble's foreground
        // line — the reference voice the listener follows — so accompanying
        // strands yield a step so they never mask it. Gesture is exempt:
        // punctuation must cut through to function as punctuation.
        if (s.role != Role::Gesture)
            baseVel -= 5;
    }

    std::uniform_int_distribution<int> velJitter(-5, 5);
    return juce::jlimit(1, 127, baseVel + velJitter(rng));
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
        juce::jlimit(0, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));

    if (strandIndexOf(s) == 0)
    {
        // Strand 0 is the reference (V1-V6 baseline). Bytewise-untouched.
        const int patternNote = s.notePattern[static_cast<size_t>(stepIdx)];
        if (patternNote > 0)
            return juce::jlimit(0, 127, patternNote + primaryTransposeSemitones);

        const int baseNote = baseMidiForStrand(s) + primaryTransposeSemitones;
        return juce::jlimit(0, 127,
            ScaleQuantizer::degreeToMidi(rawDegree, scaleRoot, scale, baseNote));
    }

    // Strands 1-3: same V1 pipeline, just sourced from the field instead of
    // a fixed scale. degree → fieldPcForDegree → closest MIDI in the
    // strand's register window. No cost tables, no ensemble negotiation.
    const int anchorMidi = baseMidiForStrand(s) + s.octaveShift * 12
                         + primaryTransposeSemitones;
    const bool isStrong = (stepIdx == 0)
                       || s.accentPattern[static_cast<size_t>(stepIdx)];

    int pc = fieldPcForDegree(rawDegree);

    // chordToneDominance: probabilistic snap to centerPc, but only at the
    // top of an even-numbered cycle. Below that frequency the strand walks
    // its own degree path through the field (B1 from the plan).
    if (s.chordToneDominance > 0.0f && stepIdx == 0 && (s.cycleCount & 1) == 0)
    {
        std::uniform_real_distribution<float> probDist(0.0f, 1.0f);
        if (probDist(rng) < s.chordToneDominance)
            pc = wrapPc(pitchField.centerPc);
    }

    const int targetMidi = closestMidiForPc(pc, anchorMidi);

    // Density weak-step motion toward the next landing ("sheets of sound").
    // Structural ground: a chromatic tone is a PASSING tone only if it
    // resolves on the next step — so chromatic steps are allowed solely to
    // fill a two-semitone gap. Larger distances run stepwise THROUGH THE
    // FIELD toward the landing (scalar run), a semitone gap simply arrives.
    // (The previous rule crept +-1 semitone for ANY distance <= 6, which
    // turned Density into a chromatic worm parked on non-field pitches —
    // measured 58% chromatic steps, hammering m7/M7 against sounding
    // strands. Fixed 2026-07-16.)
    if (s.role == Role::Density && !isStrong && s.previousOutputNote >= 0)
    {
        const int diff  = targetMidi - s.previousOutputNote;
        const int adiff = std::abs(diff);
        const int dir   = diff > 0 ? 1 : -1;

        if (adiff == 2)
            return juce::jlimit(0, 127, s.previousOutputNote + dir);

        if (adiff > 2 && adiff <= 9)
        {
            for (int m = s.previousOutputNote + dir; m != targetMidi; m += dir)
                if (fieldContains(wrapPc(m)))
                    return juce::jlimit(0, 127, m);
        }
    }

    return targetMidi;
}

// ─── Process Block ─────────────────────────────────────────────────────────

void T5ynthGenerativeSequencer::processBlock(juce::AudioBuffer<float>& buffer,
                                              std::vector<VoiceEvent>& out)
{
    if (!running)
    {
        for (auto& st : strands)
        {
            if (st.lastPlayedNote >= 0)
            {
                out.push_back({ 0, VoiceEvent::Type::NoteOff, st.lastPlayedNote, 0.0f,
                                VoiceEvent::Articulation::Normal, strandIndexOf(st), 0.0f });
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
                out.push_back({ eventPos, VoiceEvent::Type::NoteOff, s.lastPlayedNote, 0.0f,
                                VoiceEvent::Articulation::Normal, strandIndexOf(s), 0.0f });
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
            out.push_back({ eventPos, VoiceEvent::Type::NoteOff, s.lastPlayedNote, 0.0f,
                            VoiceEvent::Articulation::Normal, strandIndexOf(s), 0.0f });
            s.lastPlayedNote = -1;
        }

        // Accents and firing stay purely Euclidean (V5): downbeat or
        // longest-gap accent, exactly as strand 0 has always done. The
        // metric grouping additionally marks group starts as strong — but
        // ONLY for the expressive staging (pan movement below); it never
        // feeds fireProbability, pickNote or velocity.
        const bool isPulse = s.eucPattern[static_cast<size_t>(stepIdx)];
        const bool isStrong = (stepIdx == 0)
                           || s.accentPattern[static_cast<size_t>(stepIdx)]
                           || isMetricStrongStep(s, stepIdx);
        float fireProb = fireProbability(s, isPulse);

        // Dialog / Counter: shift fire propensity toward the moments the
        // reference voice is silent (its sounding state is the same signal
        // DensityBudget reads). Deterministic staging constants in the
        // spirit of the existing 0.90/0.15 family; V4's in-strand formula
        // above stays untouched — this is coordination-layer, like the
        // budget. Follow deliberately leaves firing alone (it speaks with,
        // not instead of).
        if (coordinationMode == CoordinationMode::Dialog
            && s.stance == Stance::Counter && &s != &strands[0])
        {
            fireProb = strands[0].lastPlayedNote >= 0
                     ? fireProb * 0.70f
                     : juce::jmin(1.0f, fireProb + 0.15f);
        }

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

        bool shouldFire = probDist(rng) < fireProb;

        // CoordinationMode dispatch: post-decision filter. Voice-1 principles
        // (V1-V6) operate inside a strand; this layer (plus the Dialog stance
        // hooks: fireProb adjust above, stance-directed mutation, phrase-end
        // stance update below) is the only place where strands look at each
        // other. Independent is the no-op fallback.
        if (shouldFire && coordinationMode == CoordinationMode::DensityBudget)
        {
            int activeCount = 0;
            int worstPrio = -1;
            Strand* worstStrand = nullptr;
            for (auto& other : strands)
            {
                if (!other.enabled || other.lastPlayedNote < 0 || &other == &s)
                    continue;
                ++activeCount;
                const int p = rolePriority(other);
                if (p > worstPrio) { worstPrio = p; worstStrand = &other; }
            }

            if (activeCount >= coordinationCap)
            {
                const int myPrio = rolePriority(s);
                if (worstStrand != nullptr && worstPrio > myPrio)
                {
                    // Displace the lowest-priority sounding strand so the
                    // higher-priority s can take its slot.
                    out.push_back({ eventPos, VoiceEvent::Type::NoteOff,
                                    worstStrand->lastPlayedNote, 0.0f,
                                    VoiceEvent::Articulation::Normal,
                                    strandIndexOf(*worstStrand), 0.0f });
                    worstStrand->lastPlayedNote      = -1;
                    worstStrand->samplesUntilGateOff = -1.0;
                }
                else
                {
                    // Budget full and we are at-or-below the worst; drop.
                    shouldFire = false;
                }
            }
        }

        if (shouldFire)
        {
            const int note = pickNote(s, stepIdx, rawDegree);
            const double stepDur = shuffledStrandStepDurationSamples(s, stepIdx);
            const float pan = updateSpatialPan(s, stepIdx, isPulse, isStrong);

            const int velInt = velocityForNote(s, stepIdx, isPulse);
            // Strand id + pan travel as typed fields — no MIDI channel, no CC10.
            out.push_back({ eventPos, VoiceEvent::Type::NoteOn, note,
                            juce::jlimit(0.0f, 1.0f, static_cast<float>(velInt) / 127.0f),
                            VoiceEvent::Articulation::Normal, strandIndexOf(s), pan });

            s.lastPlayedNote       = note;
            s.priorOutputNote      = s.previousOutputNote;
            s.previousOutputNote   = note;
            s.samplesUntilGateOff  = static_cast<double>(roleGateFraction(s)) * stepDur;
        }
        else
        {
            s.samplesUntilGateOff = -1.0;
        }

        // Dialog: a strand re-decides its stance when its own phrase closes
        // — effective from the next phrase (this step already sounded).
        // Runs whether or not the step fired: the phrase boundary exists
        // either way.
        if (coordinationMode == CoordinationMode::Dialog && &s != &strands[0]
            && metricMomentForStep(s, stepIdx).phraseEnd)
            updateStanceAtPhraseEnd(s);

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
        juce::jlimit(0, static_cast<int>(ScaleQuantizer::COUNT) - 1, scaleType));
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
