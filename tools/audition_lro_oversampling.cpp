// audition_lro_oversampling.cpp — does the LRO's source-side oversampling
// actually clean up the oscillator, and does it survive the block/carry logic?
//
// Links against the REAL src/dsp/CsoundEngine.cpp, not a copy, and drives it
// through the same calls the audio thread makes (startBlock / renderUpTo /
// voiceBuffer) at DELIBERATELY AWKWARD block sizes, so the decimator is
// exercised across block boundaries where the ksmps carry store hands samples
// from one host block to the next. A factor that only works when the block
// length happens to divide kKsmps/factor would pass a naive test and click in
// the plugin.
//
// Build (standalone clang++, C++17 — mirrors tools/csound_orch_check.cpp):
//   CSOUND_PREFIX=/opt/homebrew/opt/csound
//   clang++ -std=c++17 -O2 -DT5YNTH_HAS_CSOUND=1 -I"$CSOUND_PREFIX/include" \
//     tools/audition_lro_oversampling.cpp src/dsp/CsoundEngine.cpp \
//     -F/opt/homebrew/Frameworks -framework CsoundLib64 \
//     -Wl,-rpath,/opt/homebrew/Frameworks \
//     -o tools/audition_lro_oversampling
//   tools/audition_lro_oversampling <orchestra.csd> <freqHz> <outPrefix>
//
// Writes <outPrefix>_os1.raw / _os2.raw / _os4.raw: 32-bit float mono, host
// rate, voice 1. The aliasing measurement itself is done in Python against a
// heavily oversampled reference render (see the probe in the scratchpad) —
// this binary's job is to produce the plugin's ACTUAL output, not to judge it.

#include "../src/dsp/CsoundEngine.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace
{
    constexpr double kHostRate = 48000.0;
    constexpr double kSeconds  = 2.0;

    // Awkward on purpose: none of these divides kKsmps (64), and 37/103 do not
    // divide kKsmps/4 (16) either, so every render walks through the carry path.
    const int kBlockSizes[] = { 512, 37, 128, 103, 64, 17 };
    constexpr int kNumBlockSizes = (int) (sizeof(kBlockSizes) / sizeof(kBlockSizes[0]));

    std::string readFile (const char* path)
    {
        std::ifstream in (path, std::ios::binary);
        if (! in) return {};
        return std::string ((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    // Renders `kSeconds` of voice 1 through the engine, cycling block sizes.
    bool renderVoice1 (const std::string& orchestra, int osFactor, double freqHz,
                       std::vector<float>& out)
    {
        CsoundEngine engine;
        const int maxBlock = 512;
        // prepare() is what the player WAITS through before a new sound is
        // audible: its warm-up is defined in seconds, so its cost scales with the
        // oversampled rate. Reported because that wait is the real price of a
        // higher factor, more so than the steady-state CPU.
        const auto t0 = std::chrono::steady_clock::now();
        const bool ok = engine.prepare (kHostRate, maxBlock,
                                        orchestra.empty() ? nullptr : orchestra.c_str(), osFactor);
        const double prepareMs = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - t0).count();
        if (! ok)
        {
            std::fprintf (stderr, "prepare() failed at osFactor=%d\n", osFactor);
            return false;
        }
        std::printf ("os=%d  prepare() %.0f ms  (compiled factor %d)\n",
                     osFactor, prepareMs, engine.oversampleFactor());

        const int totalSamples = (int) (kSeconds * kHostRate);
        out.clear();
        out.reserve ((size_t) totalSamples);

        CsoundEngine::VoiceControls vc {};
        vc.gate = 1.0f; vc.freqHz = (float) freqHz; vc.velocity = 1.0f;
        vc.pressure = 0.0f; vc.timbre = 0.5f; vc.trigEpoch = 1.0f;

        int produced = 0, bsIndex = 0;
        while (produced < totalSamples)
        {
            const int bs = kBlockSizes[bsIndex++ % kNumBlockSizes];
            const int n  = std::min (bs, totalSamples - produced);

            for (int v = 0; v < CsoundEngine::kMaxVoices; ++v)
            {
                CsoundEngine::VoiceControls silent {};
                engine.setVoiceControls (v, v == 0 ? vc : silent);
            }
            engine.startBlock (n);
            // Render in two halves, as the voice bridge does when an event
            // splits a block — renderUpTo must be idempotent and monotonic.
            if (n > 1) engine.renderUpTo (n / 2 - 1);
            engine.renderUpTo (n - 1);

            const float* buf = engine.voiceBuffer (0);
            if (buf == nullptr) { std::fprintf (stderr, "voiceBuffer null\n"); return false; }
            out.insert (out.end(), buf, buf + n);
            produced += n;
        }
        return true;
    }
}

int main (int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf (stderr, "usage: %s <orchestra.csd|-> <freqHz> <outPrefix>\n", argv[0]);
        return 2;
    }
    const std::string orchestra = (std::strcmp (argv[1], "-") == 0) ? std::string() : readFile (argv[1]);
    if (std::strcmp (argv[1], "-") != 0 && orchestra.empty())
    {
        std::fprintf (stderr, "could not read %s\n", argv[1]);
        return 2;
    }
    const double freqHz = std::atof (argv[2]);
    const std::string prefix = argv[3];

    for (int osFactor : { 1, 2, 4 })
    {
        std::vector<float> samples;
        if (! renderVoice1 (orchestra, osFactor, freqHz, samples))
            return 1;

        double peak = 0.0;
        for (float s : samples) peak = std::max (peak, (double) std::fabs (s));

        const std::string path = prefix + "_os" + std::to_string (osFactor) + ".raw";
        std::ofstream o (path, std::ios::binary);
        o.write (reinterpret_cast<const char*> (samples.data()),
                 (std::streamsize) (samples.size() * sizeof (float)));
        std::printf ("os=%d  %zu samples  peak=%.6f  -> %s\n",
                     osFactor, samples.size(), peak, path.c_str());
    }
    return 0;
}
