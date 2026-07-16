#pragma once
#include <JuceHeader.h>
#include "../dsp/BlockParams.h"   // PID::, EnvTarget::, LfoTarget::
#include "../dsp/DriftLFO.h"      // DriftLFO::TgtFilter

// ─────────────────────────────────────────────────────────────────────────────
// Calibration migration.
//
// When a DSP transfer function's full-scale is recalibrated (e.g. cutoff
// modulation goes from ±10 to ±4 octaves), a value stored under the OLD full-scale
// would sound different under the new one. To keep existing presets / DAW sessions
// sonically IDENTICAL, every stored value authored before a calibration is rescaled
// on load by the inverse of the full-scale change. The epoch counter records "which
// calibrations a file predates"; bump kEpoch and add a Rescale for each change.
//
// Two kinds of rescale:
//   • Rescale     — unconditional: a parameter always means the recalibrated thing
//                   (e.g. the AT→Cutoff amount is always a cutoff depth).
//   • CondRescale — target-conditional: a parameter only means the recalibrated
//                   thing when a sibling TARGET param selects it (e.g. an env
//                   "amount" is only a cutoff depth when that env targets Filter;
//                   on other targets it means loudness/pitch/scan and must NOT be
//                   rescaled). The condition is read from the same file.
//
// Two application surfaces share this one table:
//   • live APVTS params  — DAW state restore + main .t5p params (PluginProcessor)
//   • stored ValueTrees  — per-slot snapshot param trees (.t5p only, MainPanel)
//
// Stamped into every save as `calibEpoch`; ABSENT on load = epoch 0 (the
// pre-calibration regime). New saves always stamp kEpoch, so a load→re-save
// cycle never double-migrates. Rescaled values are CLAMPED to the param's range
// when applied (so an old patch sweeping past the new ceiling caps at the ceiling).
// ─────────────────────────────────────────────────────────────────────────────
namespace Calibration
{
// Current calibration epoch. Bump by 1 for each new recalibration that needs a
// migration entry below.
//   Epoch 1: AT→Cutoff full-scale 10→4 octaves (Phase 1).
//   Epoch 2: env/LFO/Drift→Cutoff folded into the unified ±4-oct cutoff bus
//            (was ±10 for env/LFO/Drift); target-conditional on Filter.
//   Epoch 3: CoordinationMode choice table shrank 4→3 (2026-07-16). The
//            never-implemented no-op entries "Algebraic"(2)/"Counterpoint"(3)
//            were removed and raw index 2 became the audible Dialog mode. A
//            2/3 stored by an older file meant "behaves as Independent" —
//            keep that meaning instead of silently activating Dialog
//            (IndexRemap, not a rescale). Only the XML surfaces carry raw
//            indices; the .t5p JSON stores key strings and maps removed keys
//            via choiceFromKey's unknown-key fallback.
inline constexpr int kEpoch = 3;

struct Rescale
{
    const char* id;         // parameter ID whose stored value is rescaled
    float       factor;     // multiply the stored value by this
    int         sinceEpoch; // applied when the file's epoch < this
};

// Target-conditional rescale: only applied when the sibling target param selects
// the matching destination index.
struct CondRescale
{
    const char* id;         // value parameter ID to rescale
    float       factor;     // multiply the stored value by this
    int         sinceEpoch; // applied when the file's epoch < this
    const char* condId;     // sibling TARGET parameter ID to inspect
    int         condValue;  // rescale only when condId's selected index == this
};

// Choice-index remap: when a choice table changes MEANING (entries removed /
// repurposed), a raw stored index must be remapped, not rescaled. Any stored
// index >= minStored maps onto toIndex.
struct IndexRemap
{
    const char* id;         // choice parameter ID whose stored index is remapped
    int         minStored;  // stored indices >= this...
    int         toIndex;    // ...become this
    int         sinceEpoch; // applied when the file's epoch < this
};

// One entry per unconditionally-recalibrated parameter, in epoch order.
//   Epoch 1: AT→Cutoff full-scale 10→4 octaves. A stored amount a produced 10·a
//            octaves at full pressure; ×(10/4) keeps the same octave swing under
//            the new constant. AT→Cutoff is now part of the unified ±4 cutoff bus,
//            which keeps the same ±4 full-scale, so this entry is still correct.
inline const std::array<Rescale, 1>& rescales()
{
    static const std::array<Rescale, 1> table = { {
        { PID::aftertouchAmtCutoff, 10.0f / 4.0f, 1 },
    } };
    return table;
}

// One entry per target-conditional recalibration.
//   Epoch 2: env/LFO/Drift → Cutoff unified into the ±4-oct bus.
//     • env amount / LFO depth were ×10 octaves when targeting Filter → ×(10/4)=2.5.
//     • Drift filter offset was depth·0.3·10 = ±3 oct; now depth·1.0·4 = ±4 oct,
//       so depth ×(3/4)=0.75 preserves the ±3-oct swing (never clamps).
inline const std::array<CondRescale, 9>& condRescales()
{
    static const std::array<CondRescale, 9> table = { {
        { PID::ampAmount,   2.5f,  2, PID::ampTarget,    EnvTarget::Filter   },
        { PID::mod1Amount,  2.5f,  2, PID::mod1Target,   EnvTarget::Filter   },
        { PID::mod2Amount,  2.5f,  2, PID::mod2Target,   EnvTarget::Filter   },
        { PID::lfo1Depth,   2.5f,  2, PID::lfo1Target,   LfoTarget::Filter   },
        { PID::lfo2Depth,   2.5f,  2, PID::lfo2Target,   LfoTarget::Filter   },
        { PID::lfo3Depth,   2.5f,  2, PID::lfo3Target,   LfoTarget::Filter   },
        { PID::drift1Depth, 0.75f, 2, PID::drift1Target, DriftLFO::TgtFilter },
        { PID::drift2Depth, 0.75f, 2, PID::drift2Target, DriftLFO::TgtFilter },
        { PID::drift3Depth, 0.75f, 2, PID::drift3Target, DriftLFO::TgtFilter },
    } };
    return table;
}

// One entry per choice-table meaning change.
//   Epoch 3: CoordinationMode 4→3. Stored 2 ("Algebraic") / 3 ("Counterpoint")
//            were silent Independent fall-throughs; map them to Independent so
//            an old DAW session / snapshot does not silently activate Dialog
//            (which now owns index 2). Host automation LANES riding those old
//            indices cannot be detected or migrated — only stored state can.
//
//            KNOWN BOUNDARY (verified 2026-07-16, dev-machine-only): the table
//            meaning changed at 8843e3cc while kEpoch was still 2; the bump to
//            3 landed one commit later (ece51fe1, ~38 min after). State saved
//            with Dialog by a dev build inside that window is stamped epoch 2
//            and is byte-indistinguishable from a genuine old no-op index 2 —
//            this remap reverts it to Independent. Unresolvable retroactively;
//            the trade-off deliberately protects every pre-8843e3cc file (the
//            only kind that exists outside build_clean — no release carries
//            the window). LESSON: bump kEpoch IN the commit that changes a
//            table's meaning, never in a follow-up.
inline const std::array<IndexRemap, 1>& indexRemaps()
{
    static const std::array<IndexRemap, 1> table = { {
        { PID::genCoordinationMode, 2, CoordinationMode::Independent, 3 },
    } };
    return table;
}

// Rescale a single stored scalar value for `id`. For load paths that apply
// values one at a time from non-PID-keyed storage (the .t5p JSON), wrap each
// stored value with this. Presence-aware BY CONSTRUCTION — it is only ever
// called for a value the file actually carried, so it can never rescale a stale
// live value left over from a previous patch. Range clamping happens when the
// returned value is applied to the param (setParam → convertTo0to1).
inline float migrateScalar(const char* id, float value, int fromEpoch)
{
    if (fromEpoch >= kEpoch)
        return value;
    for (const auto& r : rescales())
        if (fromEpoch < r.sinceEpoch && juce::String(r.id) == id)
            return value * r.factor;
    return value;
}

// Target-conditional variant: rescale only when the file's sibling target value
// (`targetValue`, already resolved by the caller from the same .t5p record) equals
// the entry's condValue. Presence-aware like migrateScalar.
inline float migrateScalarCond(const char* id, float value, int fromEpoch, int targetValue)
{
    if (fromEpoch >= kEpoch)
        return value;
    for (const auto& c : condRescales())
        if (fromEpoch < c.sinceEpoch && targetValue == c.condValue && juce::String(c.id) == id)
            return value * c.factor;
    return value;
}

// Rescale the matching PARAM nodes inside a stored APVTS ValueTree — the DAW
// state tree (before replaceState) and per-slot snapshot trees. Presence-aware:
// only nodes actually in the tree are touched. Values are clamped to the param's
// range when the tree is applied to the APVTS.
inline void migrateValueTree(juce::ValueTree& tree, int fromEpoch)
{
    if (! tree.isValid() || fromEpoch >= kEpoch)
        return;

    // Unconditional rescales.
    for (const auto& r : rescales())
    {
        if (fromEpoch >= r.sinceEpoch)
            continue;
        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            auto child = tree.getChild(i);
            if (child.getProperty("id").toString() == r.id && child.hasProperty("value"))
                child.setProperty("value",
                                  static_cast<float>(child.getProperty("value")) * r.factor,
                                  nullptr);
        }
    }

    // Choice-index remaps: a stored index whose table entry was removed or
    // repurposed keeps its OLD meaning.
    for (const auto& m : indexRemaps())
    {
        if (fromEpoch >= m.sinceEpoch)
            continue;
        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            auto child = tree.getChild(i);
            if (child.getProperty("id").toString() == m.id && child.hasProperty("value")
                && juce::roundToInt(static_cast<double>(child.getProperty("value"))) >= m.minStored)
                child.setProperty("value", static_cast<float>(m.toIndex), nullptr);
        }
    }

    // Target-conditional rescales: apply only when the sibling target param in the
    // same tree selects the matching destination (e.g. an env amount is only a
    // cutoff depth when that env targets Filter).
    for (const auto& c : condRescales())
    {
        if (fromEpoch >= c.sinceEpoch)
            continue;

        int targetIndex = -1;
        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            auto child = tree.getChild(i);
            if (child.getProperty("id").toString() == c.condId)
            {
                targetIndex = juce::roundToInt(static_cast<double>(child.getProperty("value")));
                break;
            }
        }
        if (targetIndex != c.condValue)
            continue;

        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            auto child = tree.getChild(i);
            if (child.getProperty("id").toString() == c.id && child.hasProperty("value"))
                child.setProperty("value",
                                  static_cast<float>(child.getProperty("value")) * c.factor,
                                  nullptr);
        }
    }
}
} // namespace Calibration
