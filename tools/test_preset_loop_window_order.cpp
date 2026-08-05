// A preset's loop window (P2/P3) must land where the FILE says, whatever was
// loaded before it.
//
// SamplePlayer::setLoopStart clamps against the loop end currently held and
// setLoopEnd against the loop start currently held — right for a single edit,
// wrong for a stored pair. importJsonPreset applied the file's P2 into a
// sampler that still held the PREVIOUS patch's window, so a file whose loop
// begins after the last one ended was silently pulled back to
// (previous end - 1%). Measured 2026-08-05 before the fix: 10 of the 46 presets
// in the shipped bank and 31 of the 148 on the maintainer machine loaded a
// different window depending on which preset came before them, most with
// pointsLocked set, so loadGeneratedAudio never re-bracketed and corrected it.
// Nothing about that is visible after the fact — the file is intact, the
// sampler just never used it.
//
// The fix is SamplePlayer::setLoopRegion / WaveformDisplay::setLoopRegion:
// resolve the two markers against EACH OTHER, never against what is held.
//
// WHAT THIS ASSERTS, per file window:
//   1. ORDER-INDEPENDENT — the same file loaded into a sampler already holding
//      some other window lands on the same P2/P3 as into a fresh processor.
//   2. EXACT — that window is the one the file carries. Half 1 alone would
//      pass a mutant that mangles the pair the same way every time.
//
// Two things this test does deliberately, both because the first version of it
// failed them and an adversarial pass caught it:
//
//   - The expectation is read from THE FILE, not from the values the case
//     table asked for. The first version built each preset by calling
//     setLoopRegion — the setter under test — and exporting the result, so a
//     degenerate pair was resolved BEFORE it reached the file and the importer
//     never saw one. Here the exported JSON's engine block is edited directly,
//     and the pair actually carried is parsed back out and fed to
//     expectedFor(). What the case table names is a request, not a premise.
//   - expectedFor() spells out 0.01 and 0.99 as literals rather than deriving
//     them from setLoopRegion's own bounds. Sharing the constant would make the
//     whole suite invariant under a change to it — widening the minimum window
//     would move every shipped preset's loop and stay green.
//
// WHAT IT DOES NOT REACH. Four places apply a pair; this covers two of them,
// importJsonPreset and WaveformDisplay::setLoopRegion. MainPanel::applyMarkers
// (the history-snapshot restore) and SynthPanel's bracket-drag callback both
// need an editor and are not exercised here — putting either back on the two
// single setters ships green.
//
// Build: same recipe as the other tools/*.cpp (processor-level, so it needs
// libT5ynthData.a for the plate IRs prepareToPlay reaches for):
//   FLAGS=build_clean/CMakeFiles/T5ynth.dir/flags.make
//   { grep -m1 CXX_DEFINES "$FLAGS"; grep -m1 CXX_INCLUDES "$FLAGS"; } \
//     | sed 's/^CXX_[A-Z]* = //' > /tmp/h.rsp
//   echo -I$PWD/build_clean/_deps/signalsmith_stretch-src >> /tmp/h.rsp
//   clang++ -std=c++17 -O2 @/tmp/h.rsp tools/test_preset_loop_window_order.cpp \
//     build_clean/T5ynth_artefacts/Release/libakroasys_SharedCode.a \
//     build_clean/libT5ynthData.a -framework ... -o /tmp/test_loop_window_order
// Exits non-zero on failure.
#include "JuceHeader.h"
#include "PluginProcessor.h"
#include "gui/WaveformDisplay.h"

#include <cmath>
#include <cstdio>
#include <iterator>

static int failures = 0;
static int checks   = 0;

static void expectFrac (const juce::String& what, float got, float want)
{
    ++checks;
    if (std::abs (got - want) > 1.0e-4f)
    {
        std::printf ("FAIL  %-64s got %.4f  want %.4f\n", what.toRawUTF8(), got, want);
        ++failures;
    }
}

// A window a FILE could carry. `present` false means the two keys are removed
// from the engine block altogether.
struct FileWindow { double start, end; bool present; const char* name; };

static const FileWindow kFile[] = {
    { 0.60,   0.90,   true,  "late" },
    { 0.05,   0.20,   true,  "early" },
    { 0.00,   1.00,   true,  "whole buffer" },
    { 0.9899, 0.9999, true,  "at the far end" },
    { 0.50,   0.50,   true,  "degenerate: end == start" },
    { 0.80,   0.40,   true,  "degenerate: end below start" },
    { 0.995,  0.999,  true,  "start above 1 - minimum" },
    { -0.30,  1.40,   true,  "out of range both ways" },
    { 0.0,    0.0,    false, "keys absent from the file" },
};

// The windows a PREVIOUS patch could have left in the sampler. The first is the
// one the bank tripped over: narrow and early, so any later start clamps back.
struct Prior { float start, end; const char* name; };
static const Prior kPrior[] = {
    { 0.00f, 0.25f, "prior narrow-early" },
    { 0.80f, 0.95f, "prior late" },
    { 0.00f, 1.00f, "prior whole buffer" },
    { 0.40f, 0.60f, "prior middle" },
};

// The rule, written out rather than borrowed: clamp the start into
// [0, 1 - min], then the end into [start + min, 1]. An absent pair is
// SamplePlayer's own declared 0.0 / 1.0, which is what importJsonPreset's
// jsonFloatOr fallbacks name. Nothing here consults the held
// window — that IS the property under test.
static void expectedFor (double fileStart, double fileEnd, bool present, float& s, float& e)
{
    if (! present) { s = 0.0f; e = 1.0f; return; }
    s = juce::jlimit (0.0f, 0.99f, static_cast<float> (fileStart));
    e = juce::jlimit (s + 0.01f, 1.0f, static_cast<float> (fileEnd));
}

// Rewrite the engine block of an exported preset. The point is to put values in
// the FILE that the app's own writer would never emit — a degenerate pair, an
// out-of-range fraction, a missing key.
template <typename EditFn>
static juce::String withEngineBlock (const juce::String& json, EditFn&& edit)
{
    auto parsed = juce::JSON::parse (json);
    auto* root = parsed.getDynamicObject();
    if (root == nullptr) return json;
    juce::var engineVar = root->getProperty ("engine");   // named local: keeps the ref alive
    if (auto* engine = engineVar.getDynamicObject())
        edit (engine);
    return juce::JSON::toString (parsed);
}

// What the file ACTUALLY carries after that edit — the expectation is computed
// from this, never from the case table.
static bool engineWindowOf (const juce::String& json, double& start, double& end)
{
    auto parsed = juce::JSON::parse (json);
    auto* root = parsed.getDynamicObject();
    if (root == nullptr) return false;
    juce::var engineVar = root->getProperty ("engine");
    auto* engine = engineVar.getDynamicObject();
    if (engine == nullptr) return false;
    if (! engine->hasProperty ("loopStartFrac") || ! engine->hasProperty ("loopEndFrac"))
        return false;
    start = static_cast<double> (engine->getProperty ("loopStartFrac"));
    end   = static_cast<double> (engine->getProperty ("loopEndFrac"));
    return true;
}

static juce::String presetCarrying (const FileWindow& w)
{
    T5ynthProcessor proc;
    proc.prepareToPlay (48000.0, 512);
    const auto base = proc.exportJsonPreset();
    return withEngineBlock (base, [&] (juce::DynamicObject* engine)
    {
        if (w.present)
        {
            engine->setProperty ("loopStartFrac", w.start);
            engine->setProperty ("loopEndFrac",   w.end);
        }
        else
        {
            engine->removeProperty ("loopStartFrac");
            engine->removeProperty ("loopEndFrac");
        }
    });
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    for (const auto& f : kFile)
    {
        const auto json = presetCarrying (f);

        // The premise: does the file really carry what the case asked for? If
        // an edit silently failed, every assertion below would be measuring
        // the exporter's own well-formed pair instead.
        double carriedStart = 0.0, carriedEnd = 1.0;
        const bool carried = engineWindowOf (json, carriedStart, carriedEnd);
        if (carried != f.present)
        {
            std::printf ("FAIL  premise: '%s' — file %s the loop keys\n",
                         f.name, carried ? "carries" : "does not carry");
            ++failures;
            continue;
        }
        if (carried && (std::abs (carriedStart - f.start) > 1.0e-9
                     || std::abs (carriedEnd   - f.end)   > 1.0e-9))
        {
            std::printf ("FAIL  premise: '%s' — asked (%.6f, %.6f), file carries (%.6f, %.6f)\n",
                         f.name, f.start, f.end, carriedStart, carriedEnd);
            ++failures;
            continue;
        }

        float wantStart = 0.0f, wantEnd = 1.0f;
        expectedFor (carriedStart, carriedEnd, carried, wantStart, wantEnd);

        // The reference: nothing was loaded before, so no held window can have
        // influenced the result.
        T5ynthProcessor fresh;
        fresh.prepareToPlay (48000.0, 512);
        if (! fresh.importJsonPreset (json))
        {
            std::printf ("FAIL  import refused the preset for '%s'\n", f.name);
            ++failures;
            continue;
        }
        expectFrac (juce::String (f.name) + " P2 fresh", fresh.getSampler().getLoopStart(), wantStart);
        expectFrac (juce::String (f.name) + " P3 fresh", fresh.getSampler().getLoopEnd(),   wantEnd);

        for (const auto& p : kPrior)
        {
            T5ynthProcessor proc;
            proc.prepareToPlay (48000.0, 512);
            {
                const juce::ScopedLock sl (proc.getCallbackLock());
                proc.getSampler().setLoopRegion (p.start, p.end);
            }
            proc.importJsonPreset (json);

            const juce::String t = juce::String (f.name) + " after " + p.name;
            expectFrac (t + " P2", proc.getSampler().getLoopStart(), wantStart);
            expectFrac (t + " P3", proc.getSampler().getLoopEnd(),   wantEnd);
        }

        // A preset older than the wavetable extraction region falls back to
        // P2/P3 as they LANDED, not as the file named them — which is only a
        // distinction at all for the degenerate and out-of-range rows, and is
        // why the importer reads them back off the sampler.
        {
            const auto noWt = withEngineBlock (json, [] (juce::DynamicObject* engine)
            {
                engine->removeProperty ("wtExtractStart");
                engine->removeProperty ("wtExtractEnd");
            });
            T5ynthProcessor proc;
            proc.prepareToPlay (48000.0, 512);
            {
                const juce::ScopedLock sl (proc.getCallbackLock());
                proc.getSampler().setLoopRegion (0.00f, 0.25f);
                proc.getSampler().setWtExtractStart (0.30f);
                proc.getSampler().setWtExtractEnd (0.35f);
            }
            proc.importJsonPreset (noWt);
            const juce::String t = juce::String (f.name) + " wtExtract absent";
            expectFrac (t + " start", proc.getSampler().getWtExtractStart(), wantStart);
            expectFrac (t + " end",   proc.getSampler().getWtExtractEnd(),   wantEnd);
        }
    }

    // The display mirrors the sampler's window in SynthPanel's new-waveform
    // sync, and whatever it ends up drawing is what the next bracket drag sends
    // back. So it has to be order-independent too. (Called directly: driving it
    // through SynthPanel would need an editor.)
    for (const auto& f : kFile)
    {
        if (! f.present) continue;          // the display always receives a pair

        float wantStart = 0.0f, wantEnd = 1.0f;
        expectedFor (f.start, f.end, true, wantStart, wantEnd);

        for (const auto& p : kPrior)
        {
            WaveformDisplay wd;
            wd.setSize (600, 200);
            wd.setLoopRegion (p.start, p.end);
            wd.setLoopRegion (static_cast<float> (f.start), static_cast<float> (f.end));

            const juce::String t = juce::String ("display ") + f.name + " after " + p.name;
            expectFrac (t + " P2", wd.getLoopStart(), wantStart);
            expectFrac (t + " P3", wd.getLoopEnd(),   wantEnd);
        }
    }

    std::printf ("\n%d checks over %d file windows x %d prior windows, %d failures\n",
                 checks, (int) std::size (kFile), (int) std::size (kPrior), failures);
    return failures == 0 ? 0 : 1;
}
