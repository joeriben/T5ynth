// Epoch-8 cutoff-bus depth law, and its migration, exercised through the REAL
// code paths (ModCalib in the DSP, migrateScalar* / migrateValueTree on load).
//
// What it pins down:
//   1. The law's design points — where the retired +-4 octaves now sits on the
//      travel, and that full depth reaches the whole 20 Hz - 20 kHz span.
//   2. Sound-identity: for EVERY stored position, the migrated position produces
//      the same octave swing under the new law as the stored one did under the
//      old linear +-4 law. Held for env amounts, LFO/drift depths and the
//      bipolar AT cutoff amount (sign preserved).
//   3. Chaining across epochs: a pre-epoch-2 file takes epoch 2's factor AND
//      epoch 8's law remap, in that order, and lands back on the octave swing it
//      was authored with — including the wide sweeps epoch 2 clamped away.
//   4. Target-conditionality on the real ValueTree path: an env amount is only a
//      cutoff depth when its sibling target selects Filter; a tree carrying the
//      depth but not the target is left alone.
//   5. A file already stamped at the current epoch is untouched (no double
//      migration on load -> save -> load).
//
// Build (after a Release build of the plugin):
//   clang++ -std=c++17 -O2 @/tmp/t5_flags.rsp tools/test_cutoff_law_migration.cpp \
//     build_clean/T5ynth_artefacts/Release/libakroasys_SharedCode.a \
//     -framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio \
//     -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation \
//     -framework IOKit -framework QuartzCore -framework Security -framework WebKit \
//     -weak_framework Metal -weak_framework MetalKit -o /tmp/test_cutoff_law_migration
#include "JuceHeader.h"
#include "presets/CalibrationMigration.h"
#include "dsp/DriftLFO.h"
#include <cstdio>
#include <cmath>

static int failures = 0;

static void check(bool ok, const char* what)
{
    std::printf("  %-64s %s\n", what, ok ? "ok" : "FAIL");
    if (! ok) ++failures;
}

static void checkNear(double got, double want, double tol, const char* what)
{
    const bool ok = std::abs(got - want) <= tol;
    std::printf("  %-64s %s  (got %.4f, want %.4f)\n", what, ok ? "ok" : "FAIL", got, want);
    if (! ok) ++failures;
}

// Octaves a depth position reaches under the RETIRED law: linear onto +-4.
static double oldOctaves(double a) { return 4.0 * a; }

// Octaves a depth position reaches under the CURRENT law, as the audio path
// computes it: the source's shape (1.0 at the envelope's peak / the LFO's crest)
// times the depth curve, scaled once by the destination's full scale.
static double newOctaves(double a)
{
    return ModCalib::cutoffDepthCurve(static_cast<float>(a)) * a * ModCalib::kCutoffModOctaves;
}

static juce::ValueTree param(const char* id, float value)
{
    juce::ValueTree p("PARAM");
    p.setProperty("id", id, nullptr);
    p.setProperty("value", value, nullptr);
    return p;
}

static float valueOf(const juce::ValueTree& t, const char* id)
{
    for (int i = 0; i < t.getNumChildren(); ++i)
        if (t.getChild(i).getProperty("id").toString() == id)
            return static_cast<float>(t.getChild(i).getProperty("value"));
    return -999.0f;
}

int main()
{
    std::printf("\n== 1. the law's design points ==\n");
    checkNear(newOctaves(1.00), 10.0,  0.001, "full depth reaches 10 octaves (20 Hz - 20 kHz)");
    checkNear(newOctaves(0.50),  2.0,  0.05,  "half travel stays at the retired law's 2 octaves");
    checkNear(newOctaves(0.6714), 4.0, 0.01,  "the retired +-4 octaves sits at 0.67 of the travel");
    check(newOctaves(0.0) == 0.0,                "zero depth is zero octaves");
    check(newOctaves(0.25) < oldOctaves(0.25),   "below mid-travel the curve is finer than the old law");
    check(newOctaves(0.75) > oldOctaves(0.75),   "above mid-travel it reaches further");
    {
        bool monotonic = true;
        double prev = -1.0;
        for (int i = 0; i <= 1000; ++i)
        {
            const double o = newOctaves(i / 1000.0);
            if (o < prev - 1e-9) monotonic = false;
            prev = o;
        }
        check(monotonic, "the law is monotonic over the whole travel");
    }

    std::printf("\n== 2. sound-identity of the migration (any stored position) ==\n");
    {
        bool allSame = true, everClamped = false;
        double worst = 0.0;
        for (int i = 0; i <= 1000; ++i)
        {
            const double stored  = i / 1000.0;
            const double moved   = ModCalib::migrateCutoffDepth(static_cast<float>(stored));
            worst = std::max(worst, std::abs(newOctaves(moved) - oldOctaves(stored)));
            if (moved > 1.0) everClamped = true;
        }
        allSame = worst <= 0.002;
        check(allSame,      "migrated position reproduces the stored octave swing");
        check(! everClamped, "no migrated position leaves the 0..1 control range");
        std::printf("     worst octave error over the travel: %.5f\n", worst);
    }
    {
        const float neg = ModCalib::migrateCutoffDepth(-0.8f);
        const float pos = ModCalib::migrateCutoffDepth( 0.8f);
        check(neg < 0.0f && std::abs(neg + pos) < 1.0e-6f,
              "bipolar AT amount keeps its sign and magnitude");
        check(ModCalib::migrateCutoffDepth(0.0f) == 0.0f, "zero stays zero");
    }

    std::printf("\n== 3. chaining across epochs (pre-epoch-2 file) ==\n");
    {
        // Before epoch 2 the law was linear onto +-10 octaves. Epoch 2's x2.5
        // expressed that in +-4-octave units (clamping anything past 4), epoch 8
        // then moves it onto the curve. Full depth authored back then meant ten
        // octaves and must mean ten octaves again.
        const float v = Calibration::migrateScalarCond(PID::mod1Amount, 1.0f, /*fromEpoch*/ 0,
                                                       EnvTarget::Filter);
        checkNear(newOctaves(v), 10.0, 0.01, "a pre-epoch-2 full depth is restored to 10 octaves");
        check(v <= 1.0f,                     "...without leaving the control range");

        // A pre-epoch-2 position epoch 2 could still express (0.4 -> 4 oct).
        const float w = Calibration::migrateScalarCond(PID::lfo1Depth, 0.4f, 0, LfoTarget::Filter);
        checkNear(newOctaves(w), 4.0, 0.02, "a pre-epoch-2 0.4 keeps its 4 octaves");

        // Drift's half-range also changed at epoch 2 (0.3 -> 1.0), so its factor
        // is 0.75; full depth meant +-3 octaves then and must still.
        const float d = Calibration::migrateScalarCond(PID::drift1Depth, 1.0f, 0, DriftLFO::TgtFilter);
        checkNear(newOctaves(d), 3.0, 0.02, "a pre-epoch-2 drift full depth keeps its 3 octaves");

        // AT cutoff: epoch 1 rescale x2.5 chained with the epoch 8 law.
        const float a = Calibration::migrateScalar(PID::aftertouchAmtCutoff, 0.4f, 0);
        checkNear(newOctaves(a), 4.0, 0.02, "a pre-epoch-1 AT cutoff amount keeps its octaves");
    }

    std::printf("\n== 4. the ValueTree path, target-conditional ==\n");
    {
        juce::ValueTree t("PARAMETERS");
        t.appendChild(param(PID::mod1Amount, 1.0f), nullptr);
        t.appendChild(param(PID::mod1Target, static_cast<float>(EnvTarget::Filter)), nullptr);
        t.appendChild(param(PID::mod2Amount, 1.0f), nullptr);
        t.appendChild(param(PID::mod2Target, static_cast<float>(EnvTarget::Pitch)), nullptr);
        t.appendChild(param(PID::lfo1Depth, 0.8f), nullptr);   // no lfo1Target in the tree
        t.appendChild(param(PID::aftertouchAmtCutoff, -0.5f), nullptr);
        Calibration::migrateValueTree(t, /*fromEpoch*/ 7);

        checkNear(valueOf(t, PID::mod1Amount), 0.6714, 0.001, "filter-targeted env amount is remapped");
        checkNear(valueOf(t, PID::mod2Amount), 1.0,    0.001, "pitch-targeted env amount is untouched");
        checkNear(valueOf(t, PID::lfo1Depth),  0.8,    0.001, "a depth with no target in the tree is untouched");
        checkNear(valueOf(t, PID::aftertouchAmtCutoff), -ModCalib::migrateCutoffDepth(0.5f), 0.001,
                  "AT cutoff amount is remapped unconditionally, sign kept");
    }

    std::printf("\n== 5. no double migration ==\n");
    {
        juce::ValueTree t("PARAMETERS");
        t.appendChild(param(PID::mod1Amount, 0.6714f), nullptr);
        t.appendChild(param(PID::mod1Target, static_cast<float>(EnvTarget::Filter)), nullptr);
        Calibration::migrateValueTree(t, Calibration::kEpoch);
        checkNear(valueOf(t, PID::mod1Amount), 0.6714, 0.0001, "a current-epoch tree is left alone");

        const float s = Calibration::migrateScalarCond(PID::mod1Amount, 0.6714f,
                                                       Calibration::kEpoch, EnvTarget::Filter);
        checkNear(s, 0.6714, 0.0001, "a current-epoch scalar is left alone");
    }

    std::printf("\n%s (%d failure%s)\n\n", failures ? "FAILED" : "PASSED",
                failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
