// Pre-implementation CPU benchmark: how much does running Ladder/Warp at 4×
// cost vs base rate, extrapolated to worst-case polyphony (16 voices × stereo)?
// Header-only filters drive directly; SVF is excluded (it's linear, not getting
// oversampled, and needs real juce::dsp). Build: see run.sh.
#include "MoogLadderFilter.h"
#include "CutoffWarpFilter.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>
#include <cmath>

static constexpr double kSr = 48000.0;
static constexpr int    kVoices = 16;      // VoiceManager pool
static constexpr int    kChans  = 2;       // stereo per voice (worst case)

template <typename Filter>
static double nsPerSample(const char* name)
{
    Filter f; f.prepare(kSr, 256);
    f.setType(0); f.setSlope(3); f.setMix(1.0f);
    f.setResonance(0.7f); f.setInputDrive(2.0f); f.setCutoff(4000.0f);

    // Realistic-ish input so the saturators actually do work (not all zeros).
    std::vector<float> in(4096);
    for (size_t i = 0; i < in.size(); ++i)
        in[i] = 0.4f * std::sin(6.2831853 * 220.0 * i / kSr);

    const long iters = 60'000'000;
    volatile float sink = 0.0f;             // defeat dead-code elimination
    // warmup
    for (int i = 0; i < 100000; ++i) sink = f.processSample(in[i & 4095]);

    auto t0 = std::chrono::steady_clock::now();
    float acc = 0.0f;
    for (long i = 0; i < iters; ++i)
        acc += f.processSample(in[i & 4095]);
    auto t1 = std::chrono::steady_clock::now();
    sink = acc;
    const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
    std::printf("  %-7s %6.2f ns/sample\n", name, ns);
    return ns;
}

int main()
{
    std::printf("Filter cost @ %.0f kHz, 1 sample = one processSample call\n", kSr/1000);
    const double ladder = nsPerSample<MoogLadderFilter>("Ladder");
    const double warp   = nsPerSample<CutoffWarpFilter>("Warp");
    std::printf("\nWorst case = %d voices x %d ch, all running this filter:\n", kVoices, kChans);
    const double load = kVoices * kChans * kSr;     // processSample calls / second
    auto pct = [&](double ns, int os){ return ns * load * os / 1.0e9 * 100.0; };
    std::printf("  %-7s %5s %7s %7s %7s   (%% of ONE core)\n", "filter", "base", "2x", "4x", "8x");
    std::printf("  %-7s %5.1f %7.1f %7.1f %7.1f\n", "Ladder", pct(ladder,1), pct(ladder,2), pct(ladder,4), pct(ladder,8));
    std::printf("  %-7s %5.1f %7.1f %7.1f %7.1f\n", "Warp",   pct(warp,1),   pct(warp,2),   pct(warp,4),   pct(warp,8));
    std::printf("\n(4x = filter runs at 4x rate inside the already-oversampled drive region;\n"
                " the up/down resampling itself is extra but is ALREADY paid today when drive>0.)\n");
    return 0;
}
