#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <random>

/**
 * Generative Euclidean Sequencer — alternative to step sequencer.
 *
 * Generates both rhythm AND melody from a single Euclidean pattern:
 * - Euclidean distribution determines which steps are pulses (rhythm)
 * - Stride-based permutation maps pulses to scale degrees (melody)
 * - Turing Machine mutation evolves the pattern every cycle
 * - Per-pulse probability adds rhythmic breath
 * - Pulse count and rotation drift organically over time
 *
 * Three layers of generativity:
 * 1. Euclidean structure = seed (rhythm + melodic stride)
 * 2. Turing mutation = generator (notes drift ±1-4 scale degrees per cycle)
 * 3. Pulse probability = rhythmic breath (ghost notes, skipped pulses)
 *
 * Phase 1 refactor (polyphonic-gen-seq feature):
 * Per-pattern state has been moved into a nested Strand struct, with
 * strands[MAX_STRANDS] held at the class level. In Phase 1 only strand 0
 * is active — runtime behavior is unchanged from the mono sequencer.
 * Subsequent phases add PitchField, roles, and multi-strand scheduling.
 *
 * References:
 * - Toussaint, G.T. (2005). "The Euclidean Algorithm Generates Traditional
 *   Musical Rhythms." Proc. BRIDGES: Mathematical Connections in Art, Music
 *   and Science, pp. 47–56.
 * - Conceptual inspiration: Music Thing Modular "Turing Machine" by
 *   Tom Whitwell — LFSR with adjustable mutation probability for
 *   pattern evolution. https://musicthing.co.uk/Turing-Machine/
 */
class T5ynthGenerativeSequencer
{
public:
    static constexpr int MAX_STEPS   = 32;
    static constexpr int MAX_STRANDS = 4;

    T5ynthGenerativeSequencer();

    void prepare(double sampleRate, int samplesPerBlock);
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);
    void reset();

    // Strand-0 legacy API — routed internally to strands[0].
    void setSteps(int steps);
    void setPulses(int pulses);
    void setRotation(int rotation);
    void setMutation(float rate);
    void setFixSteps(bool fixed);
    void setFixPulses(bool fixed);
    void setFixRotation(bool fixed);
    void setFixMutation(bool fixed);
    void setBaseMutation(float rate);

    /** Seed strand 0's pattern from step sequencer data (skips Euclidean generation). */
    void seedFromSteps(const int* midiNotes, const bool* enabled, int count);

    // Per-strand setters (Phase 4 polyphonic API; idx ∈ [0, MAX_STRANDS)).
    void setStrandEnabled   (int idx, bool  en);
    void setStrandRole      (int idx, int   roleIdx);
    void setStrandOctave    (int idx, int   shift);
    void setStrandDivMult   (int idx, float mult);
    void setStrandDominance (int idx, float d);
    void setStrandSteps     (int idx, int   n);
    void setStrandPulses    (int idx, int   n);
    void setStrandRotation  (int idx, int   n);
    void setStrandMutation  (int idx, float r);
    void setStrandBaseMutation (int idx, float r);
    void setStrandFixSteps    (int idx, bool f);
    void setStrandFixPulses   (int idx, bool f);
    void setStrandFixRotation (int idx, bool f);
    void setStrandFixMutation (int idx, bool f);

    // Shared pitch-field setters.
    void setFieldMode          (int mode);       // 0..3 (FieldMode enum)
    void setFieldChangeRate    (int cycles);
    void setFieldCenterPc      (int pc);
    void setFieldPivotInterval (int semitones);

    // Shared / global
    void setRange(int octaves);
    void setScale(int scaleType, int root);
    void setGate(float gate);
    void setBpm(double bpm);
    void setDivision(int div);
    void setShuffle(float amount) { shuffle_ = juce::jlimit(0.0f, 0.75f, amount); }
    void setPrimaryTransposeSemitones(int semitones);

    void start();
    void stop();

    /** Emit note-off for every strand's currently-sounding note into `midi`
        at `sampleOffset`, then clear each strand.lastPlayedNote. Does not
        alter pattern/position — safe to call at any transition. */
    void allNotesOff(juce::MidiBuffer& midi, int sampleOffset = 0);

    bool isRunning() const { return running; }

    /** Atomic step position for GUI polling (tracks strand 0). */
    std::atomic<int> currentStepForGui { -1 };

    /** Current note pattern for GUI visualization (tracks strand 0). */
    struct StepInfo { int midiNote; bool isPulse; bool fired; };
    std::array<std::atomic<int>, MAX_STEPS> notePatternForGui{};
    std::atomic<int> numStepsForGui { 0 };

    /** Effective (post-drift) values for GUI polling (tracks strand 0). */
    std::atomic<int>   effectiveStepsForGui   { 0 };
    std::atomic<int>   effectivePulsesForGui  { 0 };
    std::atomic<float> effectiveMutationForGui{ 0.0f };

private:
    /**
     * Shared pitch material for all strands.
     *
     * A 12-bit pc-set plus an optional ordered row. Evolves via one of
     * four modes on strand 0's cycle boundary (Static/Drift/Transform/
     * Pivot), decoupled from functional-harmony chord changes.
     */
    struct PitchField
    {
        enum Mode : int { Static = 0, Drift = 1, Transform = 2, Pivot = 3 };

        // Core: 12-bit pc-set bitmask (bit n = pc n in field)
        std::uint16_t pcSet = 0;    // initialised by rebuildPcSetFromScale()
        int centerPc = 0;           // downbeat anchor pc

        // Ordered row (Transform mode); initial state = chromatic ascending
        std::array<int, 12> row = {0,1,2,3,4,5,6,7,8,9,10,11};
        int rowSize = 7;            // how many row members populate the field

        Mode mode = Drift;
        int  changeRate = 8;        // cycles between evolution ticks
        int  cyclesUntilChange = 8;

        // Transform-mode state
        int  rowTn         = 0;     // current transposition
        bool rowInverted   = false;
        bool rowRetrograde = false;

        // Pivot-mode state
        int pivotInterval = 3;      // semitones — m3 default
        int pivotAccum    = 0;      // cumulative shift since last reset
    };

    /**
     * Strand role — textural function within the polyrhythmic weave.
     * Not a Bass/Lead hierarchy; each role defines register, rhythmic
     * behavior, and how strongly it tracks the field's center pc.
     */
    enum class Role : int
    {
        Anchor  = 0,   // centerPc-stable support in the selected strand register
        Line    = 1,   // voice-led field melody
        Density = 2,   // short active motion, with weak passing tones
        Gesture = 3,   // sparse field-based punctuations with register contrast
    };

    /**
     * Internal rhythmic path. This is intentionally contextual rather than a
     * public preset parameter for now: role + step count select a distinct
     * metric attitude without widening preset migration.
     */
    enum class MetricPath : int
    {
        Gathering      = 0,  // stable common accents around the center
        Additive       = 1,  // 3/2 additive groupings (9=3-3-3, 11=3-3-3-2)
        Conversational = 2,  // offset 4-based groupings (9=4-4-1, 11=4-4-3)
        OpenBreath     = 3,  // sparse windows and caesuras
    };

    struct MetricMoment
    {
        MetricPath path = MetricPath::Additive;
        bool downbeat = false;
        bool groupStart = false;
        bool groupEnd = false;
        bool anticipatesGroup = false;
        bool phraseEnd = false;
        int groupLength = 0;
        float accent = 1.0f;
        float breath = 1.0f;
    };

    /** Per-strand pattern + playback state. */
    struct Strand
    {
        // Euclidean parameters
        int   numSteps     = 8;
        int   numPulses    = 5;
        int   rotation     = 0;
        float mutationRate = 0.15f;

        // Playback state
        int    currentStep           = 0;
        int    scheduledStep         = 0;
        double samplesUntilNextStep  = 0.0;
        double samplesUntilGateOff   = -1.0;
        int    lastPlayedNote        = -1;
        int    priorOutputNote       = -1;     // note before previousOutputNote, for melodic tendency
        int    previousOutputNote    = -1;     // survives note-offs for role voice-leading
        int    cycleCount            = 0;
        float  spatialPan            = 0.0f;   // -1..+1, carried by this strand's MIDI channel
        float  spatialTargetPan      = 0.0f;

        // Pattern data
        std::array<bool, MAX_STEPS> eucPattern{};
        std::array<int,  MAX_STEPS> notePattern{};
        std::array<bool, MAX_STEPS> accentPattern{};
        std::array<int,  MAX_STEPS> degreePattern{};
        bool patternDirty  = true;
        bool patternSeeded = false;

        // Euclidean drift state
        int  driftCycle       = 0;
        int  requestedSteps   = 8;
        int  requestedPulses  = 5;
        int  requestedRotation = 0;
        int  basePulses       = 5;
        int  pulseDriftAccum  = 0;
        bool pulseDriftUp     = true;

        // Steps drift state
        int  baseSteps        = 8;
        int  stepsDriftAccum  = 0;
        bool stepsDriftUp     = true;

        // Mutation drift state
        float baseMutation       = 0.15f;
        int   mutationDriftPhase = 0;

        // Fix flags
        bool fixSteps    = true;
        bool fixPulses   = false;
        bool fixRotation = false;
        bool fixMutation = true;

        // Strand identity (activated in Phase 3)
        bool  enabled            = false;   // strand 0 is enabled via ctor
        Role  role               = Role::Line;
        int   octaveShift        = 0;       // −2..+2
        float divisionMultiplier = 1.0f;    // 0.25 / 0.5 / 1 / 2 / 4
        float chordToneDominance = 0.0f;    // 0 = pure Turing, higher = snap to centerPc on strong beats
    };

    /**
     * Shared momentary ensemble reading.
     *
     * Secondary strands use this to read strand 0 as the primary line and
     * adapt to its recent motion, expected next pulse, and the current stack.
     */
    struct EnsembleContext
    {
        bool hasPrimary = false;
        bool primaryIsActive = false;
        int primaryNote = -1;
        int primaryPreviousNote = -1;
        int primaryNextNote = -1;
        int primaryDirection = 0;
        int primaryCurrentStep = 0;
        int primaryNextStep = 0;
        bool primaryNowStrong = false;
        bool primaryNextStrong = false;
        float primaryPhaseAffinity = 0.0f;

        int soundingCount = 0;
        int distinctPcCount = 0;
        int soundingPcMask = 0;
        int lowestNote = 128;
        int highestNote = -1;
    };

    std::array<Strand, MAX_STRANDS> strands{};
    PitchField pitchField{};

    // Timing (shared across strands)
    double bpm_         = 120.0;
    double sampleRate_  = 44100.0;
    float  gate_        = 0.8f;
    float  shuffle_     = 0.0f;
    int    division_    = 3;
    static constexpr float DIVISION_FACTORS[] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f };
    static constexpr int   DRIFT_PERIOD       = 8;  // drift evaluated over 8-cycle windows

    // Pitch context (shared)
    int rangeOctaves = 2;
    int scaleType    = 1;   // Major (not Off)
    int scaleRoot    = 0;   // C
    int primaryTransposeSemitones = 0;

    // Playback global
    bool running = false;

    // Shared RNG (accessed only from the audio thread)
    std::mt19937 rng { static_cast<std::mt19937::result_type>(std::random_device{}()) };

    // Internal helpers — all operate on a passed Strand reference.
    double stepDurationSamples() const;
    double shuffledStrandStepDurationSamples(const Strand& s, int stepIdx) const;
    void   rebuildPattern(Strand& s);
    void   mutatePattern(Strand& s);
    void   computeGaps(const Strand& s, int* gaps, int* gapCount) const;
    void   publishStrandToGui(const Strand& s);
    void   mutateNotes(Strand& s, float rate, int totalDegrees, int scaleEnum, int baseNote);
    void   applyEuclideanDrift(Strand& s);
    void   applyStepsDrift(Strand& s);
    void   applyMutationDrift(Strand& s);
    void   addPulse(Strand& s);
    void   removePulse(Strand& s);
    int    countPulses(const Strand& s) const;
    void   enforcePulseInvariant(Strand& s);
    float  effectiveMutationFromBase(const Strand& s) const;

    // Pitch-field helpers.
    void rebuildPcSetFromScale();   // initialises/resets pcSet from (scaleType, scaleRoot)
    void advancePitchField();       // called on strand-0 cycle boundary
    void driftPcSet();              // swap one pc for another
    void applyRowOp();              // random Tn / In / R / RI
    void rebuildPcSetFromRow();     // Transform mode: derive pcSet from row[0..rowSize-1]
    void applyPivot();              // rotate pcSet by pivotInterval semitones
    void syncRowFromPcSet();        // keep Transform row aligned with current pcSet

    // Per-strand rendering — Phase 3.
    double strandStepDurationSamples(const Strand& s) const;
    int    baseMidiForStrand(const Strand& s) const;
    int    strandIndexOf(const Strand& s) const;
    MetricPath metricPathForStrand(const Strand& s) const;
    void   buildMetricGroups(int steps, MetricPath path, int* groups, int* groupCount) const;
    int    metricPhaseOffset(const Strand& s, MetricPath path) const;
    MetricMoment metricMomentForStep(const Strand& s, int stepIdx) const;
    bool   isMetricStrongStep(const Strand& s, int stepIdx) const;
    int    activeOtherStrandCount(const Strand& s) const;
    int    primaryNoteForStep(int stepIdx) const;
    int    nextPrimaryPulseStep(int fromStep) const;
    EnsembleContext ensembleContextFor(const Strand& s, int stepIdx, bool isStrong) const;
    float  primaryRhythmicSupport(const Strand& s, int stepIdx, bool isStrong) const;
    float  contextualFireProbability(const Strand& s, int stepIdx, bool isPulse, bool isStrong) const;
    float  metricGateScale(const Strand& s, int stepIdx, bool isPulse, bool isStrong) const;
    int    midiChannelForStrand(const Strand& s) const;
    float  spatialTargetForStrand(const Strand& s, int stepIdx, bool isPulse, bool isStrong) const;
    float  updateSpatialPan(Strand& s, int stepIdx, bool isPulse, bool isStrong);
    int    panControllerValue(float pan) const;
    int    pickNote(Strand& s, int stepIdx, int rawDegree);
    int    voiceLedFieldMember(int rawPc) const;
    bool   fieldContains(int pc) const;
    int    fieldPcForDegree(int degree) const;
    int    fieldPcNearCenterByIndex(int index) const;
    int    closestMidiForPc(int pc, int anchorMidi) const;
    int    nearestFieldMidi(int preferredMidi, int previousMidi, int fallbackPc) const;
    int    relatedFieldPc(int primaryMidi, Role role, int seed, bool isStrong) const;
    int    bestFieldMidiNear(int targetMidi, int contourMidi, int previousMidi, int seedPc) const;
    int    chromaticPassingNote(int lastMidi, int seedMidi);
    int    socialIntervalScore(Role role, int candidate, const EnsembleContext& context, bool isStrong) const;
    int    ensembleAdjustedNote(const Strand& s, const EnsembleContext& context,
                                int candidate, bool isStrong, bool allowPassing) const;
    bool   hasHardSmallSecondClash(const Strand& s, int candidate) const;
    bool   isJustifiedPassingTone(const Strand& s, int candidate, bool isStrong) const;
    float  roleFireProbability(const Strand& s, bool isPulse, bool isStrong) const;
    int    roleVelocityBase(const Strand& s, bool isPulse, bool isStrong) const;
    int    velocityForNote(const Strand& s, int stepIdx, int note, bool isPulse, bool isStrong);
    float  roleGateFraction(const Strand& s) const;
};
