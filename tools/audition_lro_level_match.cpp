// Does the LRO leave the engine at the same level the other engines leave it at?
//
// THE OCCASION. BJ, 2026-08-05, with a recording: A:foghorn/B:violin on the T5
// oscillator, then `a fog horn 16" + a violin 8"` on the LRO at identical synth
// settings. Measured off that file, the LRO sat 12-13 dB down in rms and 14 dB
// down in peak; his second sentence was that the built-in placeholder orchestra
// is so quiet he cannot tell what waveform it is. Both readings were right, and
// the calibration in EngineCalib had certified the opposite -- because the only
// LRO it ever measured was that placeholder, against a sampler fed a synthetic
// saw quiet enough to take SamplePlayer's SUSTAINED normalise branch where every
// real T5-oscillator render (peak ~1.0) takes the PEAK-CAP one.
//
// WHAT THIS GUARDS. CsoundEngine::prepare() now renders each compiled orchestra
// once at a reference note and trims every voice onto the level a normalised
// sample lands on (CsoundEngine::kLevelTargetRmsDb / kLevelPeakCeilingDb). This
// tool drives the REAL engine -- prepare(), startBlock(), renderUpTo(),
// voiceBuffer(), oversampling and decimators included -- and asserts:
//
//   1. every orchestra lands within kTolDb of the target rms, or is peak-capped
//      at the ceiling because its crest is too high to reach the target;
//   2. nothing exceeds the peak ceiling by more than kCeilSlackDb;
//   3. the spread ACROSS orchestras stays inside kSpreadDb -- one loudness when
//      switching instruments, which is the half of this that a single global
//      constant could never have delivered (the 30 library bodies span 14 dB in
//      peak and 10 dB in rms as authored);
//   4. a silent orchestra is left untrimmed rather than amplified into noise.
//
// It is deliberately NOT a listening test and does not decide whether anything
// sounds good. It decides one objective thing: that switching engines does not
// change the loudness. BJ's ear decides the rest.
//
// Build (standalone clang++, C++17 -- CsoundEngine.h is JUCE-free ON PURPOSE so
// this links without the plugin's JUCE tree; see that header's top comment):
//
//   CSOUND_PREFIX=$(brew --prefix csound)
//   clang++ -std=c++17 -O2 -DT5YNTH_HAS_CSOUND=1 -I"$CSOUND_PREFIX/include" -Isrc \
//     tools/audition_lro_level_match.cpp src/dsp/CsoundEngine.cpp \
//     -F"$CSOUND_PREFIX/Frameworks" -framework CsoundLib64 \
//     -Wl,-rpath,"$CSOUND_PREFIX/Frameworks" \
//     -o tools/audition_lro_level_match
//   tools/audition_lro_level_match [oversampleFactor] [orchestra.csd ...]
//
// With one or more CSD paths it measures THOSE instead of the built-in cases —
// for asking what a specific preset's orchestra delivers. `-` means the built-in
// placeholder orchestra (CsoundEngine's own, i.e. prepare(orchestraText=nullptr)).

#include "dsp/CsoundEngine.h"
// For EngineCalib. Included rather than transcribed ON PURPOSE: a check written
// against the LRO's own target constant would certify any value of it, which is
// how the previous calibration passed while being 12.4 dB wrong. BlockParams.h is
// JUCE-free at the top and links standalone.
#include "dsp/BlockParams.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr int    kBlockSize  = 512;
    constexpr double kNoteHoldS  = 2.0;
    constexpr double kSkipS      = 0.5;    // past the body's spectral attack
    constexpr double kNoteHz     = 220.0;

    // Tolerances. Generous on purpose: this catches a BROKEN level stage, not a
    // fraction of a dB. The defect it exists for was 12.4 dB.
    constexpr double kTolDb       = 2.0;   // rms vs target, when not peak-capped
    constexpr double kCeilSlackDb = 1.5;   // peak vs ceiling, allowing for a pitch
                                           // other than the reference note
    // Spread is asserted over the bodies that CAN reach the rms target, where any
    // remaining difference would be a gain defect. A high-crest body is excluded
    // from it and held to the ceiling instead: no engine can make an 18 dB-crest
    // signal as loud as a sine without clipping it, and the sampler peak-caps such
    // material for the same reason. The two groups are then required to stay within
    // kCrestCostDb of each other -- crest costs level, but a body must never be so
    // crest-limited that switching to it reads as a broken engine. For scale: the
    // loudest-crest bodies the library actually ships (`supersaw` 14.0 dB, `cymbal`
    // 13.5 dB, measured 2026-08-05) cost about 3 dB.
    constexpr double kSpreadDb    = 2.0;
    constexpr double kCrestCostDb = 8.0;

    // ---- The sampler side of the comparison, and why it is spelled out here ----
    //
    // The whole finding was that the LRO sat 12.4 dB under the SAMPLER, so what has
    // to be asserted is the LRO's DELIVERED level -- past EngineCalib -- against
    // the sampler's delivered level. Asserting the engine's output against
    // CsoundEngine::kLevelTargetRmsDb alone cannot fail: it would certify whatever
    // that constant happens to say, which is exactly how a calibration table that
    // reads as level-matched shipped a 12.4 dB error.
    //
    // The reference is BJ's own T5-oscillator render (the one his 2026-08-05 A/B was
    // recorded from), measured over its playback region: peak 0.9451, rms -10.91
    // dBFS, crest 10.42 dB. SamplePlayer sees 0.49 dB of headroom, which is inside
    // kHotHeadroomDb (2.0), so it takes the PeakCap branch: gain = 10^(-1/20)/0.9451
    // = 0.9431, giving a buffer at -11.44 dBFS rms. EngineCalib::kSampler then makes
    // the delivered figure. Written out step by step because every term of it was
    // wrong or assumed in the calibration this replaces.
    constexpr double kRefSampleBufferRmsDb = -11.44;
    const double kSamplerDeliveredRmsDb =
        kRefSampleBufferRmsDb + 20.0 * std::log10((double) EngineCalib::kSampler);
    // 2 dB: an audible-but-small mismatch is tolerated (level JND is ~1 dB), a
    // broken level stage is not. The defect this exists for was 12.4 dB.
    constexpr double kEngineMatchDb = 2.0;

    int gFailures = 0;

    void check (bool ok, const std::string& what)
    {
        std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
        if (! ok) ++gFailures;
    }

    struct Level { double rms = 0.0; double peak = 0.0; bool silent = true; };

    // Render one held note on voice 0 through the real block/carry machinery.
    Level renderNote (CsoundEngine& eng)
    {
        const int total = (int) (kSampleRate * kNoteHoldS);
        const int skip  = (int) (kSampleRate * kSkipS);

        CsoundEngine::VoiceControls c {};
        c.gate = 1.0f; c.freqHz = (float) kNoteHz; c.velocity = 1.0f;
        c.pressure = 0.0f; c.timbre = 64.0f / 127.0f; c.trigEpoch = 1.0f;

        Level out;
        double sumSq = 0.0;
        long   n     = 0;

        for (int pos = 0; pos < total; pos += kBlockSize)
        {
            const int len = std::min(kBlockSize, total - pos);
            eng.setVoiceControls(0, c);
            eng.startBlock(len);
            eng.renderUpTo(len - 1);

            const float* buf = eng.voiceBuffer(0);
            if (buf == nullptr) continue;

            for (int s = 0; s < len; ++s)
            {
                if (pos + s < skip) continue;
                const double y = (double) buf[s];
                if (! std::isfinite(y)) continue;
                out.peak = std::max(out.peak, std::fabs(y));
                sumSq += y * y;
                ++n;
            }
        }
        out.rms    = n > 0 ? std::sqrt(sumSq / (double) n) : 0.0;
        out.silent = out.peak < 1.0e-5;
        return out;
    }

    double db (double x) { return 20.0 * std::log10(std::max(x, 1.0e-12)); }

    // The host scaffold, matching backend/lco_write.py's _HEAD/_TAIL contract:
    // 16 channels, six named control channels per voice, twelve knob channels,
    // three part levels, one numeric instr 1, sixteen always-on score instances.
    // `%SR%` is what CsoundEngine::prepare substitutes with the engine rate.
    std::string wrap (const std::string& body)
    {
        std::string csd =
            "<CsoundSynthesizer>\n<CsOptions>\n-n -d\n</CsOptions>\n<CsInstruments>\n"
            "sr = %SR%\nksmps = 64\nnchnls = 16\n0dbfs = 1\n"
            "giSine ftgen 1, 0, 65536, 10, 1\n"
            "giCos  ftgen 2, 0, 65536, 11, 1\n"
            "chnset 1.0, \"osc1vol\"\nchnset 1.0, \"osc1oct\"\n"
            "chnset 1.0, \"osc2vol\"\nchnset 1.0, \"osc2oct\"\n"
            "chnset 1.0, \"osc3vol\"\nchnset 1.0, \"osc3oct\"\n";
        for (int p = 1; p <= 3; ++p)
            for (const char* s : { "a", "b", "c", "d" })
                csd += "chnset 0.5, \"lroP" + std::to_string(p) + s + "\"\n";
        csd +=
            "instr 1\n"
            "  ivoice   = p4\n"
            "  Sgate    sprintf \"gate%d\", ivoice\n"
            "  Sfreq    sprintf \"freq%d\", ivoice\n"
            "  Svel     sprintf \"vel%d\", ivoice\n"
            "  Spres    sprintf \"pres%d\", ivoice\n"
            "  Stimb    sprintf \"timb%d\", ivoice\n"
            "  Strig    sprintf \"trig%d\", ivoice\n"
            "  kgateraw chnget Sgate\n  kfreqraw chnget Sfreq\n"
            "  kvel     chnget Svel\n  kpres    chnget Spres\n"
            "  ktimb    chnget Stimb\n  ktrig    chnget Strig\n"
            "  kvol1    chnget \"osc1vol\"\n  koct1    chnget \"osc1oct\"\n"
            "  kvol2    chnget \"osc2vol\"\n  koct2    chnget \"osc2oct\"\n"
            "  kvol3    chnget \"osc3vol\"\n  koct3    chnget \"osc3oct\"\n";
        for (int p = 1; p <= 3; ++p)
            for (const char* s : { "a", "b", "c", "d" })
                csd += std::string("  kp") + std::to_string(p) + s
                     + "      chnget \"lroP" + std::to_string(p) + s + "\"\n";
        csd +=
            "  kgate    portk kgateraw, 0.001\n"
            "  kfreq    limit kfreqraw, 20, 12000\n"
            "  kpresGain = 1.0 + 0.15 * kpres\n"
            "  knote    init 0\n"
            "  if changed2(ktrig) == 1 then\n    knote  = 0\n  endif\n"
            "  knote    = knote + 1/kr\n\n";
        csd += body;
        csd +=
            "\n  aout     = asig * kgate * kpresGain * 0.32\n"
            "  aout     clip aout, 0, 0.95, 0.85\n"
            "  outch    ivoice, aout\n"
            "endin\n</CsInstruments>\n<CsScore>\n";
        for (int v = 1; v <= 16; ++v)
            csd += "i 1 0 360000 " + std::to_string(v) + "\n";
        csd += "e 360000\n</CsScore>\n</CsoundSynthesizer>\n";
        return csd;
    }

    struct Case { const char* label; const char* body; };

    // Bodies chosen for their CREST, which is what decides whether the target is
    // reached on rms or the ceiling binds first -- and for spanning the range the
    // library actually authors (`asig` peak 0.32..1.65 as measured 2026-08-05).
    const Case kCases[] = {
        // A sine: lowest crest there is, and as authored the quietest-in-peak
        // family. Must come UP to the target, not stay where it was written.
        { "sine (low crest)",
          "  asig    poscil 0.6, (kfreq * koct1)\n" },
        // Quieter than anything the library authors (its own floor is `driven_metal`
        // at rms 0.0496 delivered): the case a fixed HEADROOM can never rescue,
        // because a factor big enough to lift it drives the hot body below into the
        // tail's clip.
        { "quiet body",
          "  asig    poscil 0.08, (kfreq * koct1)\n" },
        // A deliberately hot body: must be brought DOWN, and must not lean on the
        // tail's clip to get there.
        { "very loud body",
          "  asig    poscil 2.4, (kfreq * koct1)\n" },
        // gbuzz stack + a fixed formant resonator: the shape of BJ's own fog-horn
        // layer, which is where the 12.4 dB was measured.
        { "gbuzz + reson (fog horn)",
          "  afh     gbuzz 0.48, (kfreq * koct1), 12, 1, 0.42, giCos\n"
          "  abell   reson afh, 1200, 900, 1\n"
          "  asig    = afh * 0.92 + abell * 0.28\n" },
        // High crest: a 32-harmonic cosine stack is a band-limited impulse train,
        // all partials in phase. Its peak is far above its rms, so the CEILING
        // binds before the rms target does and this body is ALLOWED to sit under
        // the target -- which the test asks it about explicitly rather than
        // widening everybody's tolerance to cover it. (A `vco2` narrow pulse is
        // NOT this case: band-limiting smooths the edge and its measured crest
        // comes out at 1.5 dB, lower than a sine's.)
        { "impulse train (high crest)",
          "  asig    gbuzz 0.5, (kfreq * koct1), 32, 1, 0.98, giCos\n" },
        // Silence: must be left alone, never amplified.
        { "silent body",
          "  asig    = 0\n" },
    };
}

int main (int argc, char** argv)
{
    const int osF = argc > 1 ? std::atoi(argv[1]) : 1;

    // Measure-only mode: the orchestras named on the command line, no assertions.
    // Reports what each one delivers so a preset can be held against a sample.
    if (argc > 2)
    {
        std::printf("%-38s %8s %8s %8s %8s\n", "orchestra", "trim", "rms dB", "peak dB", "crest");
        for (int i = 2; i < argc; ++i)
        {
            const std::string arg = argv[i];
            std::string text;
            if (arg != "-")
            {
                std::ifstream f(arg);
                if (! f) { std::printf("%-38s   cannot read\n", arg.c_str()); ++gFailures; continue; }
                std::ostringstream ss; ss << f.rdbuf(); text = ss.str();
            }
            CsoundEngine eng;
            if (! eng.prepare(kSampleRate, kBlockSize, arg == "-" ? nullptr : text.c_str(), osF))
            {
                std::printf("%-38s   prepare() failed\n", arg.c_str());
                ++gFailures;
                continue;
            }
            const Level lv = renderNote(eng);
            const std::string label = (arg == "-") ? "(built-in placeholder)"
                                                   : arg.substr(arg.find_last_of('/') + 1);
            std::printf("%-38s %8.3f %8.2f %8.2f %8.2f\n", label.c_str(), eng.outputTrim(),
                        db(lv.rms), db(lv.peak),
                        lv.silent ? 0.0 : db(lv.peak) - db(lv.rms));
        }
        return gFailures == 0 ? 0 : 1;
    }

    std::printf("LRO engine level match -- oversample %dx, %.0f Hz host, note %.0f Hz\n",
                CsoundEngine::effectiveOversampleFactor(kSampleRate, osF), kSampleRate, kNoteHz);
    std::printf("target rms %.1f dBFS, peak ceiling %.1f dBFS\n\n",
                (double) CsoundEngine::kLevelTargetRmsDb,
                (double) CsoundEngine::kLevelPeakCeilingDb);

    const double targetDb  = (double) CsoundEngine::kLevelTargetRmsDb;
    const double ceilingDb = (double) CsoundEngine::kLevelPeakCeilingDb;

    std::printf("%-26s %8s %8s %8s %8s\n", "orchestra", "trim", "rms dB", "peak dB", "crest");

    std::vector<double> onTargetRmsDb;      // bodies whose crest lets them reach the target
    std::vector<double> ceilingBoundRmsDb;  // bodies the peak ceiling holds back

    for (const auto& c : kCases)
    {
        CsoundEngine eng;
        if (! eng.prepare(kSampleRate, kBlockSize, wrap(c.body).c_str(), osF))
        {
            std::printf("%-26s   prepare() failed\n", c.label);
            ++gFailures;
            continue;
        }

        const Level lv   = renderNote(eng);
        const float trim = eng.outputTrim();
        const double crest = lv.silent ? 0.0 : db(lv.peak) - db(lv.rms);

        std::printf("%-26s %8.3f %8.2f %8.2f %8.2f\n",
                    c.label, trim, db(lv.rms), db(lv.peak), crest);

        const bool isSilentCase = (std::string(c.label) == "silent body");
        if (isSilentCase)
        {
            check(lv.silent, "a silent orchestra stays silent");
            check(std::fabs(trim - 1.0f) < 1.0e-6f,
                  "a silent orchestra is left untrimmed (no gain on numerical dust)");
            continue;
        }

        check(! lv.silent, std::string(c.label) + ": sounds at all");

        // Peak-capped is a legitimate outcome, not a failure: a body whose crest
        // exceeds ceiling/target cannot reach the target without clipping, and
        // SamplePlayer does exactly the same to a spiky sample. The test asks the
        // body which of the two bounds it hit and holds it to THAT one.
        const bool ceilingBinds = crest > (ceilingDb - targetDb) + 0.5;
        if (ceilingBinds)
        {
            check(db(lv.peak) > ceilingDb - kTolDb - kCeilSlackDb,
                  std::string(c.label) + ": high crest, so peak sits at the ceiling");
            ceilingBoundRmsDb.push_back(db(lv.rms));
        }
        else
        {
            check(std::fabs(db(lv.rms) - targetDb) < kTolDb,
                  std::string(c.label) + ": rms is on target");
            onTargetRmsDb.push_back(db(lv.rms));
        }

        check(db(lv.peak) < ceilingDb + kCeilSlackDb,
              std::string(c.label) + ": peak stays under the ceiling");
    }

    if (onTargetRmsDb.size() > 1)
    {
        const auto lo = *std::min_element(onTargetRmsDb.begin(), onTargetRmsDb.end());
        const auto hi = *std::max_element(onTargetRmsDb.begin(), onTargetRmsDb.end());
        std::printf("\nspread across orchestras that reach the target: %.2f dB\n", hi - lo);
        check(hi - lo < kSpreadDb,
              "one loudness across instruments (switching a body does not change the level)");

        // THE ASSERTION THE WHOLE FILE EXISTS FOR: does switching engines change the
        // loudness? Both sides past their own EngineCalib trim.
        const double lroDelivered = hi + 20.0 * std::log10((double) EngineCalib::kCsound);
        std::printf("\ndelivered rms: LRO %.2f dBFS   sampler (reference render) %.2f dBFS"
                    "   difference %+.2f dB\n",
                    lroDelivered, kSamplerDeliveredRmsDb, lroDelivered - kSamplerDeliveredRmsDb);
        check(std::fabs(lroDelivered - kSamplerDeliveredRmsDb) < kEngineMatchDb,
              "the LRO is as loud as the T5 oscillator at the same settings");
    }

    if (! ceilingBoundRmsDb.empty() && ! onTargetRmsDb.empty())
    {
        const auto quietest = *std::min_element(ceilingBoundRmsDb.begin(), ceilingBoundRmsDb.end());
        const auto onTarget = *std::max_element(onTargetRmsDb.begin(), onTargetRmsDb.end());
        std::printf("crest cost on the peak-capped bodies: %.2f dB\n", onTarget - quietest);
        check(onTarget - quietest < kCrestCostDb,
              "a high-crest body is quieter than a sine, but not by more than its crest costs");
    }

    std::printf("\n%s (%d failure%s)\n", gFailures == 0 ? "PASS" : "FAIL",
                gFailures, gFailures == 1 ? "" : "s");
    return gFailures == 0 ? 0 : 1;
}
