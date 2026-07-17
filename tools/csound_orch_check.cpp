// Phase 3: the hard gate for the Csound tool-library lexicon
// (SPEC_phase3_csound_lexicon.md). Standalone CLI, no JUCE, no T5ynth lib —
// links only CsoundLib64, exactly like tools/csound_poc.cpp's own Section
// A+B (RT probe) code, minus the JUCE/WavetableOscillator comparison half of
// that file. backend/csound_assembler.py's assemble() output is a
// drop-in-shaped full CSD (see that module's docstring) with a literal
// "sr = %SR%" marker where src/dsp/CsoundEngine.cpp's own buildOrchestra()
// bakes a real number via snprintf; this tool does the same substitution
// itself (a plain substring replace) before ever handing text to Csound.
//
// Modes:
//   csound_orch_check check  <file.orc>
//   csound_orch_check render <file.orc> <out.wav> [freq]
//   csound_orch_check bench  <file.orc>
//
// check:  substitutes %SR%->48000, compiles, starts, resolves all 16x6
//         contract channels, gates+triggers voice 1 for 1s, verifies
//         non-silent, no NaN/Inf, peak <= 1.0. Exit 0 only on a full pass.
// render: 3s single-voice offline render (gate on 0-2s, off at 2s, trig at
//         0s and 1s -- exercises the retrigger idiom), normalized to
//         -12 dBFS peak, written to the given WAV path. The audition path
//         for BJ's ear (SPEC: "listen-check by rendering during
//         development, not only at the end").
// bench:  microseconds per csoundPerformKsmps() call at 8 gated voices
//         (steady state, post warmup) -- PASS/FAIL against a 133us gate
//         (~10% of the 1333.33us ksmps=64/48kHz block budget).
//
// Build (standalone clang++, C++17; the Csound-only half of
// tools/csound_poc.cpp's own recipe -- see that file's header for the full
// story on this machine's Homebrew Csound install: CsoundLib64.framework is
// the only C-API artifact shipped, libcsnd6.dylib must NOT be linked):
//
//   CSOUND_PREFIX=$(brew --prefix csound)
//   clang++ -std=c++17 -O2 -I"$CSOUND_PREFIX/include" \
//     tools/csound_orch_check.cpp \
//     -F"$CSOUND_PREFIX/Frameworks" -framework CsoundLib64 \
//     -Wl,-rpath,"$CSOUND_PREFIX/Frameworks" \
//     -o tools/csound_orch_check
//   tools/csound_orch_check check tools/csound_lexicon_out/some.orc

#include <csound/csound.h>

// See tools/csound_poc.cpp:88-102 / src/dsp/CsoundEngine.cpp:16-31 for why
// this #undef block exists (csound's sysdep.h leaks short macro names) --
// copied verbatim, confined to this one TU.
#undef LIKELY
#undef UNLIKELY
#undef DIRSEP
#undef ENVSEP
#undef ATOMIC_SET
#undef ATOMIC_GET
#undef ATOMIC_ADD
#undef ATOMIC_SUB
#undef CS_PURE

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    std::string readFile (const std::string& path)
    {
        std::ifstream f (path, std::ios::binary);
        if (! f)
            throw std::runtime_error ("cannot open " + path);
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    // Literal substring substitution -- see this file's header comment and
    // backend/csound_assembler.py's module docstring for why this is a plain
    // string replace, not printf-style formatting.
    std::string substituteSr (std::string text, double sampleRate)
    {
        char srBuf[32];
        std::snprintf (srBuf, sizeof (srBuf), "%.0f", sampleRate);
        const std::string marker = "%SR%";
        size_t pos = 0;
        while ((pos = text.find (marker, pos)) != std::string::npos)
        {
            text.replace (pos, marker.size(), srBuf);
            pos += std::strlen (srBuf);
        }
        return text;
    }

    std::string drainMessages (CSOUND* cs)
    {
        std::string log;
        const int n = csoundGetMessageCnt (cs);
        for (int i = 0; i < n; ++i)
        {
            if (const char* msg = csoundGetFirstMessage (cs))
                log += msg;
            csoundPopFirstMessage (cs);
        }
        return log;
    }

    void writeWav (const std::string& path, const std::vector<float>& mono, int sr)
    {
        std::ofstream f (path, std::ios::binary);
        auto u32 = [&] (uint32_t v) { f.write (reinterpret_cast<char*> (&v), 4); };
        auto u16 = [&] (uint16_t v) { f.write (reinterpret_cast<char*> (&v), 2); };
        uint32_t dataBytes = static_cast<uint32_t> (mono.size()) * 2u;
        f.write ("RIFF", 4); u32 (36 + dataBytes); f.write ("WAVE", 4);
        f.write ("fmt ", 4); u32 (16); u16 (1); u16 (1); u32 (sr); u32 (sr * 2); u16 (2); u16 (16);
        f.write ("data", 4); u32 (dataBytes);
        for (float s : mono)
        {
            int v = static_cast<int> (std::lround (std::max (-1.f, std::min (1.f, s)) * 32767.f));
            u16 (static_cast<uint16_t> (static_cast<int16_t> (v)));
        }
    }

    struct PeakRms { double peak = 0.0; double rms = 0.0; bool hasNanInf = false; };

    PeakRms measure (const std::vector<float>& buf)
    {
        PeakRms r;
        double sumSq = 0.0;
        for (float s : buf)
        {
            if (std::isnan (s) || std::isinf (s)) { r.hasNanInf = true; continue; }
            r.peak = std::max (r.peak, (double) std::fabs (s));
            sumSq += (double) s * (double) s;
        }
        r.rms = std::sqrt (sumSq / std::max<size_t> (1, buf.size()));
        return r;
    }

    double normalizeToDbfs (std::vector<float>& buf, double targetDbfs)
    {
        const PeakRms pre = measure (buf);
        if (pre.peak < 1.0e-9)
            return 1.0;
        const double targetLin = std::pow (10.0, targetDbfs / 20.0);
        const double gain = targetLin / pre.peak;
        for (float& s : buf) s = (float) (s * gain);
        return gain;
    }

    constexpr int kMaxVoices = 16;
    constexpr int kChannelsPerVoice = 6;
    constexpr const char* kPrefixes[kChannelsPerVoice] =
        { "gate", "freq", "vel", "pres", "timb", "trig" };

    // Message-thread-only helper: sets one voice's channel by name.
    void setChan (CSOUND* cs, const char* prefix, int voice1based, double value)
    {
        char name[16];
        std::snprintf (name, sizeof (name), "%s%d", prefix, voice1based);
        csoundSetControlChannel (cs, name, (MYFLT) value);
    }

    // Resolves all 16x6 contract channel pointers; returns false (with a
    // printed diagnostic) on the first one that fails to resolve.
    bool resolveAllChannels (CSOUND* cs, MYFLT* ptrs[kMaxVoices][kChannelsPerVoice])
    {
        for (int v = 0; v < kMaxVoices; ++v)
        {
            for (int c = 0; c < kChannelsPerVoice; ++c)
            {
                char name[16];
                std::snprintf (name, sizeof (name), "%s%d", kPrefixes[c], v + 1);
                MYFLT* p = nullptr;
                if (csoundGetChannelPtr (cs, &p, name,
                        CSOUND_CONTROL_CHANNEL | CSOUND_INPUT_CHANNEL) != 0 || p == nullptr)
                {
                    std::fprintf (stderr, "channel resolve FAILED: %s (voice %d)\n", name, v + 1);
                    return false;
                }
                ptrs[v][c] = p;
            }
        }
        return true;
    }

    struct CsoundHandle
    {
        CSOUND* cs = nullptr;
        bool ok = false;
        std::string diagnostics;

        explicit CsoundHandle (const std::string& csdText)
        {
            cs = csoundCreate (nullptr);
            if (cs == nullptr) { diagnostics = "csoundCreate returned null"; return; }
            csoundCreateMessageBuffer (cs, 0); // queue only, never print to console
            csoundSetOption (cs, "-n");
            csoundSetOption (cs, "-d");

            if (csoundCompileCsdText (cs, csdText.c_str()) != 0)
            {
                diagnostics = "csoundCompileCsdText failed:\n" + drainMessages (cs);
                return;
            }
            if (csoundStart (cs) != 0)
            {
                diagnostics = "csoundStart failed:\n" + drainMessages (cs);
                return;
            }
            if (csoundGetSizeOfMYFLT() != (int) sizeof (MYFLT))
            {
                diagnostics = "MYFLT size mismatch (runtime="
                    + std::to_string (csoundGetSizeOfMYFLT())
                    + ", compile-time=" + std::to_string ((int) sizeof (MYFLT)) + ")";
                return;
            }
            if ((int) csoundGetKsmps (cs) != 64)
            {
                diagnostics = "ksmps mismatch: expected 64, got "
                    + std::to_string ((unsigned) csoundGetKsmps (cs))
                    + " -- contract violation (Phase-1 SPEC)";
                return;
            }
            if ((int) csoundGetNchnls (cs) != kMaxVoices)
            {
                diagnostics = "nchnls mismatch: expected " + std::to_string (kMaxVoices)
                    + ", got " + std::to_string ((int) csoundGetNchnls (cs))
                    + " -- contract violation (Phase-1 SPEC)";
                return;
            }
            ok = true;
        }

        ~CsoundHandle()
        {
            if (cs != nullptr)
            {
                csoundDestroyMessageBuffer (cs);
                csoundDestroy (cs);
            }
        }

        CsoundHandle (const CsoundHandle&) = delete;
        CsoundHandle& operator= (const CsoundHandle&) = delete;
    };

    int runCheck (const std::string& orcPath)
    {
        std::string text;
        try { text = readFile (orcPath); }
        catch (const std::exception& e) { std::fprintf (stderr, "CHECK FAIL: %s\n", e.what()); return 1; }

        text = substituteSr (text, 48000.0);
        CsoundHandle h (text);
        if (! h.ok)
        {
            std::fprintf (stderr, "CHECK FAIL (%s): %s\n", orcPath.c_str(), h.diagnostics.c_str());
            return 1;
        }

        MYFLT* ptrs[kMaxVoices][kChannelsPerVoice] = {};
        if (! resolveAllChannels (h.cs, ptrs))
        {
            std::fprintf (stderr, "CHECK FAIL (%s): not all 16x6 contract channels resolved\n", orcPath.c_str());
            return 1;
        }

        const double sr = 48000.0;
        const uint32_t ksmps = csoundGetKsmps (h.cs);
        const uint32_t nchnls = csoundGetNchnls (h.cs);

        // Gate + trigger ONLY voice 1 (index 0); every other voice stays
        // silent (gate/freq/trig = 0) -- exactly the resting state a real
        // 16-voice instrument sits in most of the time.
        setChan (h.cs, "gate", 1, 1.0);
        setChan (h.cs, "freq", 1, 220.0);
        setChan (h.cs, "vel",  1, 0.85);
        setChan (h.cs, "pres", 1, 0.3);
        setChan (h.cs, "timb", 1, 0.6);
        setChan (h.cs, "trig", 1, 1.0);

        const long totalBlocks = (long) std::llround (1.0 * sr / ksmps);
        std::vector<float> voice1;
        voice1.reserve ((size_t) totalBlocks * ksmps);

        bool perfOk = true;
        for (long i = 0; i < totalBlocks; ++i)
        {
            const int fin = csoundPerformKsmps (h.cs);
            MYFLT* spout = csoundGetSpout (h.cs);
            for (uint32_t s = 0; s < ksmps; ++s)
                voice1.push_back ((float) spout[(size_t) s * nchnls + 0]); // outch 1 -> index 0
            if (fin != 0) { perfOk = false; break; }
        }
        if (! perfOk)
        {
            std::fprintf (stderr, "CHECK FAIL (%s): csoundPerformKsmps ended prematurely\n", orcPath.c_str());
            return 1;
        }

        const PeakRms m = measure (voice1);
        if (m.hasNanInf)
        {
            std::fprintf (stderr, "CHECK FAIL (%s): NaN/Inf in output\n", orcPath.c_str());
            return 1;
        }
        // Skip the first 40ms (attack ramp settling) before judging silence.
        const size_t skip = (size_t) std::llround (0.04 * sr);
        const PeakRms mSkip = measure (std::vector<float> (voice1.begin() + std::min (skip, voice1.size()), voice1.end()));
        if (mSkip.peak < 1.0e-4)
        {
            std::fprintf (stderr, "CHECK FAIL (%s): output is silent (peak=%.8f after 40ms)\n", orcPath.c_str(), mSkip.peak);
            return 1;
        }
        if (m.peak > 1.0)
        {
            std::fprintf (stderr, "CHECK FAIL (%s): CLIPS -- peak=%.6f > 1.0\n", orcPath.c_str(), m.peak);
            return 1;
        }

        std::printf ("CHECK OK (%s): peak=%.6f rms=%.6f (voice1, 1s @220Hz gate+trig, pres=0.3 timb=0.6)\n",
                      orcPath.c_str(), m.peak, m.rms);
        return 0;
    }

    int runRender (const std::string& orcPath, const std::string& wavPath, double freqHz)
    {
        std::string text;
        try { text = readFile (orcPath); }
        catch (const std::exception& e) { std::fprintf (stderr, "RENDER FAIL: %s\n", e.what()); return 1; }

        text = substituteSr (text, 48000.0);
        CsoundHandle h (text);
        if (! h.ok)
        {
            std::fprintf (stderr, "RENDER FAIL (%s): %s\n", orcPath.c_str(), h.diagnostics.c_str());
            return 1;
        }

        MYFLT* ptrs[kMaxVoices][kChannelsPerVoice] = {};
        if (! resolveAllChannels (h.cs, ptrs))
        {
            std::fprintf (stderr, "RENDER FAIL (%s): channel resolve failed\n", orcPath.c_str());
            return 1;
        }

        const double sr = 48000.0;
        const uint32_t ksmps = csoundGetKsmps (h.cs);
        const uint32_t nchnls = csoundGetNchnls (h.cs);
        const double durationSec = 3.0;
        const long totalBlocks = (long) std::llround (durationSec * sr / ksmps);

        setChan (h.cs, "freq", 1, freqHz);
        setChan (h.cs, "vel",  1, 0.85);
        setChan (h.cs, "pres", 1, 0.0);
        setChan (h.cs, "timb", 1, 0.5);

        std::vector<float> mono;
        mono.reserve ((size_t) totalBlocks * ksmps);
        bool gateOn = false, trig0Fired = false, trig1Fired = false;
        bool perfOk = true;

        for (long i = 0; i < totalBlocks; ++i)
        {
            const double t = (double) i * ksmps / sr;
            if (! gateOn && t >= 0.0)      { setChan (h.cs, "gate", 1, 1.0); gateOn = true; }
            if (gateOn && t >= 2.0)        { setChan (h.cs, "gate", 1, 0.0); gateOn = false; }
            if (! trig0Fired && t >= 0.0)  { setChan (h.cs, "trig", 1, 1.0); trig0Fired = true; }
            if (! trig1Fired && t >= 1.0)  { setChan (h.cs, "trig", 1, 2.0); trig1Fired = true; } // changed value -> new edge

            const int fin = csoundPerformKsmps (h.cs);
            MYFLT* spout = csoundGetSpout (h.cs);
            for (uint32_t s = 0; s < ksmps; ++s)
                mono.push_back ((float) spout[(size_t) s * nchnls + 0]);
            if (fin != 0) { perfOk = false; break; }
        }
        if (! perfOk)
        {
            std::fprintf (stderr, "RENDER FAIL (%s): csoundPerformKsmps ended prematurely\n", orcPath.c_str());
            return 1;
        }

        const PeakRms pre = measure (mono);
        if (pre.hasNanInf)
        {
            std::fprintf (stderr, "RENDER FAIL (%s): NaN/Inf in output\n", orcPath.c_str());
            return 1;
        }
        const double gain = normalizeToDbfs (mono, -12.0);
        writeWav (wavPath, mono, (int) sr);
        std::printf ("RENDER OK (%s -> %s): dur=%.2fs freq=%.1fHz prePeak=%.6f preRMS=%.6f normGain=%+.2fdB\n",
                      orcPath.c_str(), wavPath.c_str(), durationSec, freqHz, pre.peak, pre.rms, 20.0 * std::log10 (gain));
        return 0;
    }

    struct TimingStats { double medianUs = 0.0, p99Us = 0.0, maxUs = 0.0; size_t n = 0; };

    TimingStats computeStats (std::vector<double> v)
    {
        TimingStats s; s.n = v.size();
        if (v.empty()) return s;
        std::sort (v.begin(), v.end());
        s.medianUs = v[v.size() / 2];
        const size_t p99idx = (size_t) std::min<long long> ((long long) v.size() - 1,
                                   std::llround (0.99 * (double) (v.size() - 1)));
        s.p99Us = v[p99idx];
        s.maxUs = v.back();
        return s;
    }

    inline double elapsedUs (const struct timespec& t0, const struct timespec& t1)
    {
        return (double) (t1.tv_sec - t0.tv_sec) * 1.0e6 + (double) (t1.tv_nsec - t0.tv_nsec) / 1.0e3;
    }

    int runBench (const std::string& orcPath)
    {
        std::string text;
        try { text = readFile (orcPath); }
        catch (const std::exception& e) { std::fprintf (stderr, "BENCH FAIL: %s\n", e.what()); return 1; }

        text = substituteSr (text, 48000.0);
        CsoundHandle h (text);
        if (! h.ok)
        {
            std::fprintf (stderr, "BENCH FAIL (%s): %s\n", orcPath.c_str(), h.diagnostics.c_str());
            return 1;
        }

        const double sr = 48000.0;
        const uint32_t ksmps = csoundGetKsmps (h.cs);

        // Gate 8 of the 16 always-on voices (Phase-0's own bench convention).
        // NOTE: every one of the 16 `instr 1` instances executes its FULL
        // DSP graph every k-cycle regardless of gate value (Phase-1's
        // always-on-by-design contract has no gate-skip branch anywhere in
        // this orchestra) -- gating 8 vs. 16 does not change the measured
        // cost; this mirrors Phase-0's RT probe finding verbatim
        // (tools/csound_poc_out/metrics.txt: @1 voice and @8 voices timed
        // the same).
        for (int v = 1; v <= 8; ++v)
        {
            const double freq = 110.0 * std::pow (8.0, (double) (v - 1) / 7.0);
            setChan (h.cs, "gate", v, 1.0);
            setChan (h.cs, "freq", v, freq);
            setChan (h.cs, "vel",  v, 0.85);
            setChan (h.cs, "pres", v, 0.3);
            setChan (h.cs, "timb", v, 0.6);
            setChan (h.cs, "trig", v, 1.0);
        }

        const long warmupBlocks = (long) std::llround (1.0 * sr / ksmps);
        for (long i = 0; i < warmupBlocks; ++i)
            csoundPerformKsmps (h.cs);

        const long measuredBlocks = (long) std::llround (3.0 * sr / ksmps);
        std::vector<double> raw;
        raw.reserve ((size_t) measuredBlocks);
        for (long i = 0; i < measuredBlocks; ++i)
        {
            struct timespec t0 {}, t1 {};
            clock_gettime (CLOCK_MONOTONIC_RAW, &t0);
            csoundPerformKsmps (h.cs);
            clock_gettime (CLOCK_MONOTONIC_RAW, &t1);
            raw.push_back (elapsedUs (t0, t1));
        }

        const TimingStats stats = computeStats (raw);
        constexpr double kGateUs = 133.0;
        const bool pass = stats.medianUs <= kGateUs;
        std::printf ("BENCH %s (%s): n=%zu median=%.3fus p99=%.3fus max=%.3fus (gate=%.1fus, %.1f%% of budget)\n",
                      pass ? "PASS" : "FAIL", orcPath.c_str(), stats.n,
                      stats.medianUs, stats.p99Us, stats.maxUs, kGateUs, 100.0 * stats.medianUs / kGateUs);
        return pass ? 0 : 1;
    }
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf (stderr,
            "usage:\n"
            "  %s check  <file.orc>\n"
            "  %s render <file.orc> <out.wav> [freq]\n"
            "  %s bench  <file.orc>\n", argv[0], argv[0], argv[0]);
        return 2;
    }

    const std::string mode = argv[1];
    const std::string orcPath = argv[2];

    if (mode == "check")
        return runCheck (orcPath);
    if (mode == "bench")
        return runBench (orcPath);
    if (mode == "render")
    {
        if (argc < 4) { std::fprintf (stderr, "render requires <out.wav>\n"); return 2; }
        const std::string wavPath = argv[3];
        const double freq = argc >= 5 ? std::atof (argv[4]) : 220.0;
        return runRender (orcPath, wavPath, freq);
    }

    std::fprintf (stderr, "unknown mode: %s\n", mode.c_str());
    return 2;
}
