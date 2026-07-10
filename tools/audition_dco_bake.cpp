// Offline audition for the DCO baker (dco::Baker::bake -> WavetableOscillator).
//
// Re-proves the two lost scratchpad demos referenced in
// docs/HANDOVER_DCO_OSCILLATOR.md ("tzpwm_demo.py" = band-limited PWM,
// "morph_loop_demo.py" = a saw->square->pulse->saw morph loop), plus a third
// recipe (additive bell <-> 2-op FM) exercising the Additive/Fm2 kinds. For
// each recipe: bake -> framesToBuffer -> load into a real WavetableOscillator
// via extractContiguousFrames (the exact hook loadGeneratedAudio uses, per
// docs/HANDOVER_DCO_OSCILLATOR.md §4.5) -> render three WAVs (a manual
// out-and-back scan, a loop-wrap seam stress test, and a high-pitch
// mip/aliasing check) into tools/dco_audition_out/.
//
// Build: compile against the plugin's static lib + JUCE flags, e.g.
//   FLAGS=build_clean/CMakeFiles/T5ynth.dir/flags.make
//   { grep -m1 CXX_DEFINES "$FLAGS"; grep -m1 CXX_INCLUDES "$FLAGS"; } \
//     | sed 's/^CXX_[A-Z]* = //' > /tmp/h.rsp
//   clang++ -std=c++17 -O2 @/tmp/h.rsp tools/audition_dco_bake.cpp \
//     build_clean/T5ynth_artefacts/Release/libT5ynth_SharedCode.a \
//     -framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio \
//     -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation \
//     -framework IOKit -framework QuartzCore -framework Security -framework WebKit \
//     -weak_framework Metal -weak_framework MetalKit -o /tmp/dco_bake
// Run from the repo root (writes tools/dco_audition_out/, a relative path).
// Exits non-zero if any WAV fails the basic sanity/click checks, or if a
// loop=true recipe's motion doesn't actually close (see checkClosure()).
#include "JuceHeader.h"
#include "dsp/DcoBaker.h"
#include "dsp/WavetableOscillator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace dco;

static void writeWav(const std::string& path, const std::vector<float>& mono, int sr)
{
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v){ f.write(reinterpret_cast<char*>(&v), 4); };
    auto u16 = [&](uint16_t v){ f.write(reinterpret_cast<char*>(&v), 2); };
    uint32_t dataBytes = static_cast<uint32_t>(mono.size()) * 2u;
    f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(1); u32(static_cast<uint32_t>(sr));
    u32(static_cast<uint32_t>(sr) * 2); u16(2); u16(16);
    f.write("data", 4); u32(dataBytes);
    for (float s : mono) {
        int v = static_cast<int>(std::lround(std::max(-1.f, std::min(1.f, s)) * 32767.f));
        u16(static_cast<uint16_t>(static_cast<int16_t>(v)));
    }
}

// ─── Recipe builders (the two lost demos + one Additive/Fm2 case) ───

static Recipe makeMorphloop()
{
    Keyframe saw;    saw.kind = Keyframe::Kind::Saw;
    Keyframe square; square.kind = Keyframe::Kind::Square;
    Keyframe pulse;  pulse.kind = Keyframe::Kind::Pulse; pulse.width = 0.25f;

    Recipe r;
    r.keyframes = { saw, square, pulse }; // 0=Saw, 1=Square, 2=Pulse
    r.motion = {
        MotionSegment{ 0, 0.0f,  Curve::Lin },  // start on Saw
        MotionSegment{ 1, 0.34f, Curve::Lin },  // -> Square
        MotionSegment{ 2, 0.33f, Curve::Lin },  // -> Pulse
        MotionSegment{ 0, 0.33f, Curve::Lin },  // -> Saw (closes the loop)
    };
    r.loop = true;
    r.frames = 128;
    return r;
}

static Recipe makePwm()
{
    Keyframe wide;   wide.kind = Keyframe::Kind::Pulse; wide.width = 0.5f;
    Keyframe narrow; narrow.kind = Keyframe::Kind::Pulse; narrow.width = 0.05f;

    Recipe r;
    r.keyframes = { wide, narrow }; // 0=Pulse(0.5), 1=Pulse(0.05)
    r.motion = {
        MotionSegment{ 0, 0.0f, Curve::Lin },
        MotionSegment{ 1, 0.5f, Curve::Slow },
        MotionSegment{ 0, 0.5f, Curve::Fast },
    };
    r.loop = true;
    r.frames = 128;
    return r;
}

static Recipe makeFmbell()
{
    Keyframe add; add.kind = Keyframe::Kind::Additive;
    add.partials = {
        Partial{ 1,  1.0f,  0.0f },
        Partial{ 4,  0.35f, 0.0f },
        Partial{ 7,  0.15f, 0.0f },
        Partial{ 10, 0.08f, 0.0f },
    };
    Keyframe fm2; fm2.kind = Keyframe::Kind::Fm2; fm2.ratio = 3.0f; fm2.index = 2.5f;

    Recipe r;
    r.keyframes = { add, fm2 }; // 0=Additive, 1=Fm2
    r.motion = {
        MotionSegment{ 0, 0.0f, Curve::Lin },
        MotionSegment{ 1, 0.5f, Curve::Lin },
        MotionSegment{ 0, 0.5f, Curve::Lin },
    };
    r.loop = true;
    r.frames = 128;
    return r;
}

// ─── Rendering: manual scan sweep through a loaded WavetableOscillator ───

static std::vector<float> renderScan(WavetableOscillator& osc, double freqHz, double seconds, int sr,
                                     const std::function<float(double)>& scanFn, int block = 64)
{
    osc.reset(); // fresh phase/scan-smoothing state; the loaded frame bank survives reset()
    osc.setFrequency(static_cast<float>(freqHz));
    const int totalSamples = static_cast<int>(seconds * sr);
    std::vector<float> out;
    out.reserve(static_cast<size_t>(totalSamples));
    int pos = 0;
    while (pos < totalSamples)
    {
        const int n = std::min(block, totalSamples - pos);
        const double progress = static_cast<double>(pos) / totalSamples;
        osc.setScanPosition(scanFn(progress));
        for (int i = 0; i < n; ++i)
            out.push_back(osc.processSample());
        pos += n;
    }
    return out;
}

static float scanOutAndBack(double p) { return static_cast<float>(p < 0.5 ? p * 2.0 : (1.0 - p) * 2.0); }
static float scanLinear(double p)     { return static_cast<float>(p); }
static float scanWrap3(double p)      { return static_cast<float>(std::fmod(p * 3.0, 1.0)); }

// ─── Assertions ───

static bool checkFiniteSanity(const std::string& label, const std::vector<float>& buf,
                              double& outRms, bool& allOk)
{
    bool anyNonFinite = false;
    float peak = 0.0f;
    double sumSq = 0.0;
    for (float s : buf)
    {
        if (!std::isfinite(s)) { anyNonFinite = true; continue; }
        peak = std::max(peak, std::abs(s));
        sumSq += static_cast<double>(s) * s;
    }
    outRms = buf.empty() ? 0.0 : std::sqrt(sumSq / static_cast<double>(buf.size()));
    // 1.05 ceiling, not a strict 1.0: WavetableOscillator's own per-octave mip
    // decimation (generateMipLevels: FFT brick-wall truncation + IFFT) can mildly
    // overshoot a band-limited frame's already-normalized peak via ordinary
    // Gibbs-phenomenon ringing (classically ~9% for one edge; a narrow pulse's
    // two close edges can add) at aggressive high-pitch mip truncation. Verified
    // engine-side and independent of this Baker: extractContiguousFrames
    // renormalizes every frame to peak 0.95 *before* generateMipLevels runs,
    // regardless of what gain the Baker applied — the Baker has no lever over a
    // downstream mip level's reconstruction peak. 1.05 still catches a real bug
    // (e.g. missing normalization, a runaway DC offset) with wide margin.
    const bool ok = !anyNonFinite && peak <= 1.05f && outRms > 0.01;
    printf("  [%-28s] finite=%s peak=%.4f rms=%.4f -> %s\n",
           label.c_str(), anyNonFinite ? "NO" : "yes", static_cast<double>(peak), outRms,
           ok ? "OK" : "*** FAIL ***");
    if (!ok) allOk = false;
    return ok;
}

// Adapts audition_sampler_follow.cpp's discontinuity measure (windowMax vs. a
// naturalMax reference region, loose tolerance factor).
//
// A first version compared against 6x the MEDIAN delta of the whole file, per
// the original spec text. Empirically (verified with a standalone delta-dump
// over the rendered WAVs) that reference does not work for Square/Pulse
// content: median is dominated by the smooth majority of each cycle and
// wildly understates the legitimate once-per-cycle EDGE delta, which for
// these waveforms can be nearly as large as the whole file's global max —
// e.g. morphloop's actual global max (0.896) occurs in a region with no wrap
// anywhere nearby, well above 6x its own median (0.048). The 6x-median
// reference was flagging normal Square/Pulse edges as "clicks" throughout the
// file, not anything specific to the wrap.
//
// The correct reference (and what audition_sampler_follow.cpp itself does) is
// "the max delta found elsewhere in the SAME file, away from the event under
// test" — i.e. is the wrap actually worse than an ordinary edge this same
// waveform already produces during normal playback. That reference passes
// with real margin for all three recipes once measured correctly.
static bool checkLoopwrapClicks(const std::string& label, const std::vector<float>& buf, int sr, bool& allOk)
{
    if (buf.size() < 2) return true;

    // scanWrap3 wraps 1->0 at progress = 1/3 and 2/3 of the render.
    const int halfWin = static_cast<int>(0.02 * sr); // 20ms either side (scan smoothing settles in ~5ms)
    const std::vector<int> wrapSamples = {
        static_cast<int>(std::lround(static_cast<double>(buf.size()) / 3.0)),
        static_cast<int>(std::lround(static_cast<double>(buf.size()) * 2.0 / 3.0)),
    };
    std::vector<std::pair<int, int>> wrapRanges;
    for (int w : wrapSamples)
        wrapRanges.push_back({ std::max(0, w - halfWin),
                                std::min(static_cast<int>(buf.size()) - 1, w + halfWin) });
    const auto inWrapRange = [&](int i) {
        for (const auto& r : wrapRanges)
            if (i >= r.first && i <= r.second) return true;
        return false;
    };

    float wrapMax = 0.0f;
    float naturalMax = 0.0f;
    for (size_t i = 0; i + 1 < buf.size(); ++i)
    {
        const float d = std::abs(buf[i + 1] - buf[i]);
        if (inWrapRange(static_cast<int>(i))) wrapMax = std::max(wrapMax, d);
        else                                  naturalMax = std::max(naturalMax, d);
    }

    const bool ok = wrapMax <= naturalMax * 1.5f + 1.0e-4f;
    printf("  [%-28s] wrapMax=%.5f  naturalMax=%.5f  (1.5x=%.5f) -> %s\n",
           label.c_str(), static_cast<double>(wrapMax), static_cast<double>(naturalMax),
           static_cast<double>(naturalMax * 1.5f), ok ? "CLICK-FREE" : "*** CLICK ***");
    if (!ok) allOk = false;
    return ok;
}

// Confirms a loop=true recipe's motion actually closes on its start keyframe.
// loop=true samples p=i/N, which never reaches p==1 within N frames (the last
// frame sits at p=(N-1)/N) — that's what makes the wrap from frame[N-1] back
// to frame[0] seamless, but it also means frame[N-1] itself is NOT quite
// frame[0] for a coarse trajectory split (e.g. morphloop's final segment is
// only 0.33 of the whole trajectory). To evaluate p==1 exactly we re-bake the
// SAME recipe with loop=false: p=i/(N-1) DOES hit p==1 exactly on the last
// frame. The two bakes each compute their own independent global-
// normalization gain (the achieved peak differs slightly because the two
// bakes sample the continuous trajectory at slightly different points), so
// we compare PEAK-NORMALIZED shapes rather than raw levels — that isolates
// "does the waveshape close" from "do two unrelated bakes happen to share a gain".
static bool checkClosure(const std::string& label, const Recipe& recipe, bool& allOk)
{
    if (!recipe.loop) return true;

    auto startFrames = Baker::bake(recipe);
    Recipe nonLoop = recipe;
    nonLoop.loop = false;
    auto endFrames = Baker::bake(nonLoop);

    if (startFrames.empty() || endFrames.empty())
    {
        printf("  [%-28s] *** FAIL: empty bake ***\n", label.c_str());
        allOk = false;
        return false;
    }

    auto peakOf = [](const std::vector<float>& f) {
        float pk = 0.0f;
        for (float s : f) pk = std::max(pk, std::abs(s));
        return pk;
    };
    const float peakStart = std::max(peakOf(startFrames.front()), 1.0e-9f);
    const float peakEnd   = std::max(peakOf(endFrames.back()), 1.0e-9f);

    const auto& a = startFrames.front();
    const auto& b = endFrames.back();
    double sumSq = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const double na = static_cast<double>(a[i]) / peakStart;
        const double nb = static_cast<double>(b[i]) / peakEnd;
        const double d = na - nb;
        sumSq += d * d;
    }
    const double rms = std::sqrt(sumSq / static_cast<double>(a.size()));
    const bool ok = rms < 1.0e-3;
    printf("  [%-28s] closure RMS (peak-normalized) = %.6f -> %s\n",
           label.c_str(), rms, ok ? "CLOSES" : "*** DOES NOT CLOSE ***");
    if (!ok) allOk = false;
    return ok;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit; // safe init for any JUCE statics
    std::filesystem::create_directories("tools/dco_audition_out");

    const int sr = 48000;
    bool allOk = true;
    std::vector<std::pair<std::string, double>> wavReport;

    struct Case { std::string name; Recipe recipe; };
    const std::vector<Case> cases = {
        { "morphloop", makeMorphloop() },
        { "pwm",       makePwm() },
        { "fmbell",    makeFmbell() },
    };

    for (const auto& c : cases)
    {
        printf("\n=== %s ===\n", c.name.c_str());

        checkClosure(c.name, c.recipe, allOk);

        auto frames = Baker::bake(c.recipe);
        bool shapeOk = (static_cast<int>(frames.size()) == c.recipe.frames);
        for (const auto& fr : frames)
            shapeOk = shapeOk && (static_cast<int>(fr.size()) == Baker::kFrameSize);
        printf("  [%-28s] bake shape: %d frames x %d samples -> %s\n",
               c.name.c_str(), static_cast<int>(frames.size()), Baker::kFrameSize,
               shapeOk ? "OK" : "*** FAIL ***");
        if (!shapeOk) allOk = false;

        auto buf = Baker::framesToBuffer(frames);

        WavetableOscillator osc;
        osc.prepare(static_cast<double>(sr), 512);
        osc.extractContiguousFrames(buf, static_cast<double>(sr), 0.0f, 1.0f);
        osc.setInterpolation(true);

        // A3_scan: 8s @ 220Hz, scan swept 0->1->0 (out-and-back over the morph).
        {
            auto out = renderScan(osc, 220.0, 8.0, sr, scanOutAndBack);
            const std::string path = "tools/dco_audition_out/" + c.name + "_A3_scan.wav";
            writeWav(path, out, sr);
            double rms = 0.0;
            checkFiniteSanity(c.name + "_A3_scan", out, rms, allOk);
            wavReport.push_back({ path, rms });
        }

        // A3_loopwrap: 6s @ 220Hz, scan swept 0->1 three times (sawtooth wrap) —
        // exposes the loop seam (frame N-1 vs frame 0).
        {
            auto out = renderScan(osc, 220.0, 6.0, sr, scanWrap3);
            const std::string path = "tools/dco_audition_out/" + c.name + "_A3_loopwrap.wav";
            writeWav(path, out, sr);
            double rms = 0.0;
            checkFiniteSanity(c.name + "_A3_loopwrap", out, rms, allOk);
            checkLoopwrapClicks(c.name + "_A3_loopwrap", out, sr, allOk);
            wavReport.push_back({ path, rms });
        }

        // C6_scan: 4s @ 1046.5Hz, scan swept 0->1 (high-pitch mip/aliasing ear-check).
        {
            auto out = renderScan(osc, 1046.5, 4.0, sr, scanLinear);
            const std::string path = "tools/dco_audition_out/" + c.name + "_C6_scan.wav";
            writeWav(path, out, sr);
            double rms = 0.0;
            checkFiniteSanity(c.name + "_C6_scan", out, rms, allOk);
            wavReport.push_back({ path, rms });
        }
    }

    printf("\n=== WAV summary ===\n");
    for (const auto& entry : wavReport)
        printf("  %-50s rms=%.4f\n", entry.first.c_str(), entry.second);

    printf("\n%s\n", allOk ? "ALL PASS" : "*** FAIL ***");
    return allOk ? 0 : 1;
}
