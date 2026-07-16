// GUARD for the three restored GenSeq expressive layers (metric elasticity,
// stage panning, foreground velocity bias — restored 2026-07-16 after their
// unauthorized removal in b826a898) AND for strand 0's reference-voice
// contract. Drives the REAL T5ynthGenerativeSequencer for 120s and asserts:
//
//   A. Strand 0 stays a rigid reference clock: every inter-onset gap is an
//      integer multiple of the step duration (±3 samples), velocities stay
//      in the legacy 55/85/100 (±5 jitter) bands.
//   B. Secondary strands breathe: single-step inter-onset ratios vary
//      (elasticity alive) and stay inside the clamp band [0.84, 1.22].
//   C. Stage panning: every strand's pan MOVES over time, stays in ±0.95,
//      keeps its lane side (S1 left, S2 right, S3 left, S4 right), and the
//      centre actor S5 hovers around centre stage (|pan| small, both signs).
//   D. Foreground bias: secondary non-Gesture strands top out 5 below their
//      unbiased ceiling (mean+12+jitter), Gesture reaches its full ceiling.
//   E. Scale 0 (UI label "Chromatic") behaves chromatic — non-diatonic
//      pitch classes actually sound (it used to be coerced to Major).
//
// Build (see audition_sampler_follow.cpp header):
//   FLAGS=build_clean/CMakeFiles/T5ynth.dir/flags.make
//   { grep -m1 CXX_DEFINES "$FLAGS"; grep -m1 CXX_INCLUDES "$FLAGS"; } \
//     | sed 's/^CXX_[A-Z]* = //' > /tmp/h.rsp
//   echo -I$PWD/build_clean/_deps/signalsmith_stretch-src >> /tmp/h.rsp
//   clang++ -std=c++17 -O2 @/tmp/h.rsp tools/audition_genseq_expressive_layers.cpp \
//     build_clean/T5ynth_artefacts/Release/libT5ynth_SharedCode.a \
//     -framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio \
//     -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation \
//     -framework IOKit -framework QuartzCore -framework Security -framework WebKit \
//     -weak_framework Metal -weak_framework MetalKit -o /tmp/genseq_layers && /tmp/genseq_layers
#include "JuceHeader.h"
#include "sequencer/GenerativeSequencer.h"
#include "dsp/VoiceEvent.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <set>
#include <algorithm>

static int g_failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

struct Ev { long pos; int strand; int vel; float pan; };

int main()
{
    constexpr double SR = 44100.0;
    constexpr int    BS = 512;
    constexpr double BPM = 120.0;
    constexpr double STEP = SR * 60.0 / BPM * 0.5; // division 3 => factor 0.5 => 11025

    T5ynthGenerativeSequencer seq;
    seq.prepare(SR, BS);
    seq.setBpm(BPM);
    seq.setDivision(3);
    seq.setGate(0.8f);
    seq.setShuffle(0.0f);
    seq.setScale(1, 0);   // C major
    seq.setRange(2);

    // Roles: S2=Anchor S3=Line S4=Density S5=Gesture (indices 1..4).
    for (int i = 1; i < T5ynthGenerativeSequencer::MAX_STRANDS; ++i)
    {
        seq.setStrandRole(i, i - 1);
        seq.setStrandEnabled(i, true);
    }
    seq.start();

    std::vector<Ev> ons;
    juce::AudioBuffer<float> buf(2, BS);
    std::vector<VoiceEvent> out;
    const long total = static_cast<long>(SR) * 120;
    for (long pos = 0; pos < total; pos += BS)
    {
        out.clear();
        seq.processBlock(buf, out);
        for (const auto& e : out)
            if (e.type == VoiceEvent::Type::NoteOn)
                ons.push_back({ pos + e.sampleOffset, e.strandId,
                                juce::roundToInt(e.velocity * 127.0f), e.pan });
    }

    // DensityBudget (default cap 3) drops/displaces low-priority notes by
    // design, so totals sit well below the theoretical 5-strand maximum.
    std::printf("collected %zu note-ons\n", ons.size());
    check(ons.size() > 1000, "enough events for statistics");

    for (int strand = 0; strand < T5ynthGenerativeSequencer::MAX_STRANDS; ++strand)
    {
        std::vector<Ev> mine;
        for (const auto& e : ons) if (e.strand == strand) mine.push_back(e);
        std::printf("strand %d: %zu note-ons\n", strand, mine.size());
        check(mine.size() > 100, "strand active");
        if (mine.size() < 2) { ++g_failures; continue; }

        // ── Timing ──
        double maxGridDev = 0.0;      // strand 0: deviation from step grid
        double minRatio = 9e9, maxRatio = -9e9; // secondaries: 1-step ratios
        int oneStepCount = 0;
        for (size_t i = 1; i < mine.size(); ++i)
        {
            const double delta = static_cast<double>(mine[i].pos - mine[i-1].pos);
            const int k = juce::jmax(1, juce::roundToInt(delta / STEP));
            if (strand == 0)
                maxGridDev = std::max(maxGridDev, std::abs(delta - k * STEP));
            else if (k == 1)
            {
                const double r = delta / STEP;
                minRatio = std::min(minRatio, r);
                maxRatio = std::max(maxRatio, r);
                ++oneStepCount;
            }
        }
        if (strand == 0)
        {
            std::printf("  grid deviation max %.2f samples\n", maxGridDev);
            check(maxGridDev <= 3.0, "S1 rigid on the step grid (no elasticity)");
        }
        else if (oneStepCount > 10)
        {
            std::printf("  1-step ratio %.4f..%.4f (n=%d)\n", minRatio, maxRatio, oneStepCount);
            check(minRatio >= 0.83 && maxRatio <= 1.23, "elastic ratios inside clamp band");
            const double spreadSamples = (maxRatio - minRatio) * STEP;
            check(spreadSamples >= (strand == 1 ? 5.0 : 10.0), "elasticity alive (timing varies)");
        }

        // ── Pan ──
        float panMin = 1.0f, panMax = -1.0f;
        std::set<int> distinctPan;
        for (const auto& e : mine)
        {
            panMin = std::min(panMin, e.pan);
            panMax = std::max(panMax, e.pan);
            distinctPan.insert(juce::roundToInt(e.pan * 10000.0f));
        }
        std::printf("  pan %.3f..%.3f (%zu distinct)\n", panMin, panMax, distinctPan.size());
        check(panMin >= -0.95f && panMax <= 0.95f, "pan bounded");
        check(distinctPan.size() > 10 && (panMax - panMin) >= 0.02f, "pan MOVES (stage alive)");
        switch (strand)
        {
            case 0: check(panMax <  0.01f, "S1 keeps left lane");            break;
            case 1: check(panMin > -0.01f, "S2 keeps right lane");           break;
            case 2: check(panMax <  0.01f, "S3 keeps left lane");            break;
            case 3: check(panMin > -0.01f, "S4 keeps right lane");           break;
            case 4: check(std::abs(panMin) <= 0.20f && std::abs(panMax) <= 0.20f
                          && panMin < 0.0f && panMax > 0.0f,
                          "S5 centre actor oscillates around centre");        break;
        }

        // ── Velocity ──
        int velMax = 0;
        bool velBandsOk = true;
        for (const auto& e : mine)
        {
            velMax = std::max(velMax, e.vel);
            if (strand == 0)
                velBandsOk = velBandsOk
                    && ((e.vel >= 50 && e.vel <= 60) || (e.vel >= 80 && e.vel <= 90)
                                                     || (e.vel >= 95 && e.vel <= 105));
        }
        std::printf("  vel max %d\n", velMax);
        if (strand == 0) check(velBandsOk, "S1 legacy velocity bands 55/85/100 +-5");
        if (strand == 1) check(velMax <= 92,  "Anchor ceiling biased (80+12-5+5)");
        if (strand == 2) check(velMax <= 100, "Line ceiling biased (88+12-5+5)");
        if (strand == 3) check(velMax <= 87,  "Density ceiling biased (75+12-5+5)");
        if (strand == 4) check(velMax >= 108 && velMax <= 112,
                               "Gesture UNbiased ceiling reached (95+12+5)");
    }

    // ── E. Chromatic scale honored (scale 0 = UI "Chromatic") ──
    {
        T5ynthGenerativeSequencer chrom;
        chrom.prepare(SR, BS);
        chrom.setBpm(BPM);
        chrom.setDivision(3);
        chrom.setGate(0.8f);
        chrom.setShuffle(0.0f);
        chrom.setScale(0, 0);   // Chromatic, root C
        chrom.setRange(2);
        chrom.setStrandRole(1, 1);   // one secondary Line strand (field path)
        chrom.setStrandEnabled(1, true);
        chrom.start();

        std::set<int> pcs0, pcs1;
        for (long pos = 0; pos < total; pos += BS)
        {
            out.clear();
            chrom.processBlock(buf, out);
            for (const auto& e : out)
                if (e.type == VoiceEvent::Type::NoteOn)
                    (e.strandId == 0 ? pcs0 : pcs1).insert(((e.note % 12) + 12) % 12);
        }
        auto nonDiatonic = [](const std::set<int>& pcs) {
            int n = 0;
            for (int pc : { 1, 3, 6, 8, 10 }) if (pcs.count(pc)) ++n;
            return n;
        };
        std::printf("chromatic: strand0 %zu pcs (%d non-diatonic), strand1 %zu pcs (%d non-diatonic)\n",
                    pcs0.size(), nonDiatonic(pcs0), pcs1.size(), nonDiatonic(pcs1));
        check(nonDiatonic(pcs0) >= 2, "Chromatic honored on strand 0 (non-diatonic pcs sound)");
        check(nonDiatonic(pcs1) >= 2, "Chromatic honored on secondary strand (field path)");
        check(pcs0.size() >= 9, "chromatic coverage broad on strand 0");
    }

    std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "ALL PASS",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures ? 1 : 0;
}
