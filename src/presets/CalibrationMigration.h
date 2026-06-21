#pragma once
#include <JuceHeader.h>
#include "../dsp/BlockParams.h"   // PID::

// ─────────────────────────────────────────────────────────────────────────────
// Calibration migration.
//
// When a DSP transfer function's full-scale is recalibrated (e.g. AT→Cutoff goes
// from ±10 to ±4 octaves), a value stored under the OLD full-scale would sound
// different under the new one. To keep existing presets / DAW sessions sonically
// IDENTICAL, every stored value authored before a calibration is rescaled on load
// by the inverse of the full-scale change. The epoch counter records "which
// calibrations a file predates"; bump kEpoch and add a Rescale for each change.
//
// Two application surfaces share this one table:
//   • live APVTS params  — DAW state restore + main .t5p params (PluginProcessor)
//   • stored ValueTrees  — per-slot snapshot param trees (.t5p only, MainPanel)
//
// Stamped into every save as `calibEpoch`; ABSENT on load = epoch 0 (the
// pre-calibration regime). New saves always stamp kEpoch, so a load→re-save
// cycle never double-migrates.
// ─────────────────────────────────────────────────────────────────────────────
namespace Calibration
{
// Current calibration epoch. Bump by 1 for each new recalibration that needs a
// migration entry below.
inline constexpr int kEpoch = 1;

struct Rescale
{
    const char* id;         // parameter ID whose stored value is rescaled
    float       factor;     // multiply the stored value by this
    int         sinceEpoch; // applied when the file's epoch < this
};

// One entry per recalibrated parameter, in epoch order.
//   Epoch 1: AT→Cutoff full-scale 10→4 octaves (SynthVoice kFilterModOctaves). A
//            stored amount a produced 10·a octaves at full pressure; ×(10/4) keeps
//            the same octave swing under the new constant. (Clamped to the param's
//            range on apply, so the rare old patch sweeping >4 oct caps at 4.)
inline const std::array<Rescale, 1>& rescales()
{
    static const std::array<Rescale, 1> table = { {
        { PID::aftertouchAmtCutoff, 10.0f / 4.0f, 1 },
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

// Rescale the matching PARAM nodes inside a stored APVTS ValueTree — the DAW
// state tree (before replaceState) and per-slot snapshot trees. Presence-aware:
// only nodes actually in the tree are touched. Values are clamped to the param's
// range when the tree is applied to the APVTS.
inline void migrateValueTree(juce::ValueTree& tree, int fromEpoch)
{
    if (! tree.isValid() || fromEpoch >= kEpoch)
        return;
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
}
} // namespace Calibration
