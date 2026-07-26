// Offline audition for the sampler's note-start render-path latch.
//
// A sampler note plays either by reading the buffer directly (tape-style: read
// speed IS the pitch, exact at the sample's own pitch) or through the Signalsmith
// stretcher (pitch shifted, duration preserved). Which one is used USED to be
// re-decided every block from the instantaneous pitch: a note on the sample's own
// pitch with a small vibrato routed to pitch crossed the 0.1-semitone dead zone
// several times a second, and every crossing jumped the read position by the
// stretcher's analysis window (~50 ms back, ~100 ms forward) — an audible step
// mid-note.
//
// The path is now latched at the note's first block from how far modulation can
// reach (SamplePlayer::setPitchModulationReach) and held for the whole note. The
// two halves of that claim need different test material, so each case checks one:
//
//   * PATH IDENTITY (noise source) — noise has a clean autocorrelation peak, so
//     the best-match lag against the source says which path ran: the direct read
//     at the sample's own pitch reproduces the source in place (lag 0, and here
//     sample-for-sample), the stretcher cannot. Only the DECLARED REACH differs
//     between the two cases; the played pitch is identical.
//
//   * NO MID-NOTE SWITCH (sine source) — a vibrato that crosses the dead-zone
//     boundary ~10x a second, with the largest sample-to-sample step measured
//     against the 99th percentile of the same render. A steady oscillation has
//     max/p99 ~ 1; a path switch is a lone outlier and lifts the ratio. (A sine
//     is useless for the lag test — it correlates at every period — and noise is
//     useless for the step test, hence the split.)
//
// Why the step gate sits at 2.0 and not at 1.1 — measured null hypotheses, all
// unity pitch, 220 Hz / 0.5 peak, 5 Hz vibrato:
//     stretcher,   no vibrato   -> ratio 1.00, 0 outliers, peak 0.500
//     direct read, 0.5st vibrato -> ratio 1.00, 0 outliers, peak 0.500
//     stretcher,   0.5st vibrato -> ratio 1.55, 5 outliers, peak 0.563
// The stretcher's pitch ratio is only updated per block, and each update makes
// its overlap-add ripple briefly: an 0.008 step, -42 dBFS under the peak, five
// times in 1.4 s. That is the stretcher's own behaviour on a code path every
// transposed note has always used, not a read-position jump — the defect this
// harness guards against moves the read head by ~50 ms and steps by roughly the
// signal's whole amplitude, i.e. 15-60x the natural per-sample delta.
//
// The gate was checked against the defect itself: recompiling SamplePlayer.cpp
// with the pre-fix per-block rule (`pitchPathUsesStretch_ = |semitones| >= 0.1`)
// and linking that object ahead of the plugin lib makes "own pitch + 0.5st
// vibrato" step by 0.949 on a 0.5-peak signal — ratio 36.61 — and makes "own
// pitch, reach 0.5st" report lag 0, i.e. the wrong path. Both fail; the latched
// build passes. So a green run here is not a green run over nothing.
//
// Build: compile against the plugin's static lib + JUCE flags, e.g.
//   FLAGS=build_clean/CMakeFiles/T5ynth.dir/flags.make
//   { grep -m1 CXX_DEFINES "$FLAGS"; grep -m1 CXX_INCLUDES "$FLAGS"; } \
//     | sed 's/^CXX_[A-Z]* = //' > /tmp/h.rsp
//   echo -I$PWD/build_clean/_deps/signalsmith_stretch-src >> /tmp/h.rsp
//   clang++ -std=c++17 -O2 @/tmp/h.rsp tools/audition_pitch_path_latch.cpp \
//     build_clean/T5ynth_artefacts/Release/libakroasys_SharedCode.a \
//     -framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio \
//     -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation \
//     -framework IOKit -framework QuartzCore -framework Security -framework WebKit \
//     -weak_framework Metal -weak_framework MetalKit -o /tmp/pitch_path_latch
// Exits non-zero if any case takes the wrong path or steps mid-note.
#include "JuceHeader.h"
#include "dsp/SamplePlayer.h"

#include <cstdio>
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>

static juce::AudioBuffer<float> makeTone(int sr, double seconds, double freq, float amp)
{
    int n = static_cast<int>(seconds * sr);
    juce::AudioBuffer<float> b(1, n);
    float* d = b.getWritePointer(0);
    for (int i = 0; i < n; ++i)
        d[i] = amp * static_cast<float>(std::sin(2.0 * M_PI * freq * i / sr));
    return b;
}

// Deterministic lowpassed noise: one sharp autocorrelation peak, no periodicity.
static juce::AudioBuffer<float> makeNoise(int sr, double seconds, float amp)
{
    int n = static_cast<int>(seconds * sr);
    juce::AudioBuffer<float> b(1, n);
    float* d = b.getWritePointer(0);
    uint32_t s = 0x13579bdfu;
    float z = 0.0f;
    for (int i = 0; i < n; ++i) {
        s = s * 1664525u + 1013904223u;
        const float white = (float) ((int32_t) ((s >> 8) % 20001u) - 10000) / 10000.0f;
        z = 0.6f * z + 0.4f * white;         // tame the very top octave
        d[i] = amp * z * 2.0f;
    }
    return b;
}

// Zero-crossing frequency estimate over [from, to). Coarse but ample for telling
// 220 Hz from 247 Hz — used to prove a note actually BENDS, whichever path it is on.
static double freqBetween(const std::vector<float>& out, int from, int to, int sr)
{
    int crossings = 0;
    int first = -1, last = -1;
    for (int i = std::max(1, from); i < to && i < (int) out.size(); ++i) {
        if (out[i - 1] <= 0.0f && out[i] > 0.0f) {
            if (first < 0) first = i; else last = i;
            ++crossings;
        }
    }
    if (crossings < 3 || last <= first) return 0.0;
    return (double) (crossings - 1) * (double) sr / (double) (last - first);
}

// Best-match lag of `out` against `src` over +/-maxLag. Positive lag = the output
// is behind the source (what the stretcher's analysis window does).
static int bestLag(const std::vector<float>& out, const juce::AudioBuffer<float>& src,
                   int atSample, int winLen, int maxLag, double* peakOut)
{
    const float* s  = src.getReadPointer(0);
    const int    sn = src.getNumSamples();
    double bestCorr = -2.0;
    int    best     = 0;
    for (int lag = -maxLag; lag <= maxLag; ++lag) {
        double num = 0, ea = 0, eb = 0;
        bool   ok  = true;
        for (int i = 0; i < winLen; ++i) {
            const int oi = atSample + i;
            const int si = atSample + i - lag;
            if (oi < 0 || oi >= (int) out.size() || si < 0 || si >= sn) { ok = false; break; }
            num += out[oi] * s[si];
            ea  += out[oi] * out[oi];
            eb  += s[si]   * s[si];
        }
        if (! ok) continue;
        const double denom = std::sqrt(ea * eb);
        const double corr  = denom > 1e-12 ? num / denom : 0.0;
        if (corr > bestCorr) { bestCorr = corr; best = lag; }
    }
    if (peakOut) *peakOut = bestCorr;
    return best;
}

// Calibrated against the null hypotheses in the header comment.
static constexpr float kMaxStepRatio = 2.0f;

struct CaseResult { bool pass; };

// One note. `vibratoSemis` is the pitch wobble actually applied per block (0 = none),
// `declaredReach` is what the voice tells the sampler modulation can reach.
static CaseResult runCase(const std::string& label, int sr, double transposeRatio,
                          float declaredReach, double vibratoSemis,
                          bool useNoise, bool expectStretch,
                          bool checkLag, bool checkStep)
{
    const int    block   = 512;
    const int    blocks  = 140;                 // ~1.5 s at 48k
    const double srcSecs = 4.0;                 // longer than any read: no loop seam
    const double vibHz   = 5.0;

    auto src = useNoise ? makeNoise(sr, srcSecs, 0.50f)
                        : makeTone(sr, srcSecs, 220.0, 0.50f);

    SamplePlayer master;
    master.prepare(sr, block);
    master.setLoopMode(SamplePlayer::LoopMode::Loop);
    master.setNormalize(false);
    master.loadBuffer(src, sr);

    SamplePlayer voice;
    voice.prepare(sr, block);
    voice.setLoopMode(SamplePlayer::LoopMode::Loop);
    voice.setNormalize(false);
    voice.shareBufferFrom(master);
    voice.setTransposeRatio(transposeRatio);
    // Order as SynthVoice drives it: the reach is pushed before the first render
    // block, and that block latches the path.
    voice.setPitchModulationReach(declaredReach);
    voice.retrigger();

    std::vector<float> out;
    std::vector<float> blockBuf(static_cast<size_t>(block));

    for (int b = 0; b < blocks; ++b) {
        if (vibratoSemis > 0.0) {
            const double t     = (double) (b * block) / sr;
            const double semis = vibratoSemis * std::sin(2.0 * M_PI * vibHz * t);
            voice.setPitchModulation((float) std::pow(2.0, semis / 12.0));
        }
        voice.renderPitchedBlock(blockBuf.data(), block);
        out.insert(out.end(), blockBuf.begin(), blockBuf.end());
    }

    // Skip the note's own onset (stretcher priming, any attack ramp) — the defect
    // under test is a mid-note step, and a 5 Hz vibrato still crosses the dead
    // zone ~14 times after this point.
    const int scanFrom = sr / 10;               // 100 ms

    double rms = 0.0;
    for (int i = scanFrom; i < (int) out.size(); ++i) rms += out[i] * out[i];
    rms = std::sqrt(rms / std::max(1, (int) out.size() - scanFrom));

    const bool audible = rms > 0.05;
    bool  stepOk = true;   float ratio = 0.0f, maxDelta = 0.0f, p99 = 0.0f;
    bool  pathOk = true;   int   lag = 0;       double corr = 0.0, maxErr = -1.0;

    if (checkStep) {
        std::vector<float> deltas;
        deltas.reserve(out.size());
        for (int i = scanFrom; i + 1 < (int) out.size(); ++i)
            deltas.push_back(std::abs(out[i + 1] - out[i]));
        std::sort(deltas.begin(), deltas.end());
        maxDelta = deltas.empty() ? 0.0f : deltas.back();
        p99      = deltas.empty() ? 0.0f
                 : deltas[(size_t) ((double) (deltas.size() - 1) * 0.99)];
        ratio    = p99 > 1e-6f ? maxDelta / p99 : 0.0f;
        // A read-position jump reads as an outlier far above the oscillation's own
        // slope ceiling. See the header for why 2.0 clears the stretcher's own
        // per-block ratio ripple (1.55) while a jump (15-60) fails hard.
        stepOk   = ratio <= kMaxStepRatio;
    }

    if (checkLag) {
        lag    = bestLag(out, src, sr / 2, 4096, 8192, &corr);
        pathOk = expectStretch ? (lag != 0) : (lag == 0 && corr > 0.999);
        if (! expectStretch) {
            // The direct read at the sample's own pitch is the source, in place.
            const float* s = src.getReadPointer(0);
            maxErr = 0.0;
            for (int i = scanFrom; i < (int) out.size() && i < src.getNumSamples(); ++i)
                maxErr = std::max(maxErr, (double) std::abs(out[i] - s[i]));
            pathOk = pathOk && maxErr < 1.0e-5;
        }
    }

    const bool pass = stepOk && audible && pathOk;
    printf("  [%-30s] reach=%.2fst vib=%.2fst rms=%.3f", label.c_str(),
           declaredReach, vibratoSemis, rms);
    if (checkStep) printf("  maxD=%.5f p99=%.5f ratio=%.2f", maxDelta, p99, ratio);
    if (checkLag) {
        printf("  lag=%+5d corr=%.4f", lag, corr);
        if (maxErr >= 0.0) printf(" maxErr=%.1e", maxErr);
        printf(" want=%s", expectStretch ? "stretch" : "direct");
    }
    printf(" -> %s\n", pass ? "OK"
                            : (! pathOk   ? "*** WRONG PATH ***"
                            : (! audible  ? "*** SILENT ***" : "*** STEP ***")));
    return { pass };
}

// Does the note actually BEND? A sustained pitch modulation is applied mid-note
// and the played frequency measured before and after. Whichever path the note is
// latched onto, the pitch must arrive — the direct read gets there by reading
// faster, the stretcher by transposing. This is the case that catches a path
// which drops pitchModFactor instead of carrying it (the direct read did exactly
// that until 2026-07-26: latched at unity, a bend simply never happened).
static CaseResult runBendCase(const std::string& label, int sr, float declaredReach,
                              double bendSemis, bool expectStretch)
{
    const int block = 512, blocks = 200, bendAtBlock = 60;
    const double freq = 220.0;

    auto src = makeTone(sr, 5.0, freq, 0.50f);

    SamplePlayer master;
    master.prepare(sr, block);
    master.setLoopMode(SamplePlayer::LoopMode::Loop);
    master.setNormalize(false);
    master.loadBuffer(src, sr);

    SamplePlayer voice;
    voice.prepare(sr, block);
    voice.setLoopMode(SamplePlayer::LoopMode::Loop);
    voice.setNormalize(false);
    voice.shareBufferFrom(master);
    voice.setTransposeRatio(1.0);
    voice.setPitchModulationReach(declaredReach);
    voice.retrigger();

    std::vector<float> out;
    std::vector<float> blockBuf(static_cast<size_t>(block));
    for (int b = 0; b < blocks; ++b) {
        voice.setPitchModulation(b < bendAtBlock
            ? 1.0f : (float) std::pow(2.0, bendSemis / 12.0));
        voice.renderPitchedBlock(blockBuf.data(), block);
        out.insert(out.end(), blockBuf.begin(), blockBuf.end());
    }

    const int bendAt = bendAtBlock * block;
    // Leave the stretcher a full analysis window (~2400 samples) to deliver.
    const double fBefore = freqBetween(out, sr / 10, bendAt - block, sr);
    const double fAfter  = freqBetween(out, bendAt + 8192, (int) out.size(), sr);
    const double want    = freq * std::pow(2.0, bendSemis / 12.0);
    const bool   ok      = fBefore > freq * 0.99 && fBefore < freq * 1.01
                        && fAfter  > want * 0.98 && fAfter  < want * 1.02;

    printf("  [%-30s] reach=%.2fst bend=%+.1fst  %.1f Hz -> %.1f Hz (want %.1f) path=%s -> %s\n",
           label.c_str(), declaredReach, bendSemis, fBefore, fAfter, want,
           expectStretch ? "stretch" : "direct", ok ? "OK" : "*** PITCH LOST ***");
    return { ok };
}

int main()
{
    const int sr = 48000;
    juce::ScopedJuceInitialiser_GUI juceInit;

    printf("Sampler note-start render-path latch (1.5 s notes at 48 kHz):\n");
    bool ok = true;

    // Path identity. Same played pitch (the sample's own), same material — only
    // the declared reach differs, and it alone decides the path.
    printf(" path identity (noise source, no modulation applied):\n");
    ok &= runCase("own pitch, no reach",        sr, 1.0, 0.00f, 0.0,  true,  false, true,  false).pass;
    ok &= runCase("own pitch, reach 0.5st",     sr, 1.0, 0.50f, 0.0,  true,  true,  true,  false).pass;

    // No mid-note switch. The 0.5-semitone vibrato crosses the 0.1-semitone dead
    // zone ~10x a second — every crossing used to jump the read position.
    printf(" no mid-note switch (sine source, vibrato applied per block):\n");
    ok &= runCase("own pitch + 0.05st vibrato", sr, 1.0, 0.05f, 0.05, false, false, false, true).pass;
    ok &= runCase("own pitch + 0.5st vibrato",  sr, 1.0, 0.50f, 0.50, false, true,  false, true).pass;

    // Transposed notes are unchanged: always the stretcher. No lag check — the
    // output is a different pitch from the source, so there is nothing for the
    // correlation to lock onto.
    printf(" transposed (unchanged behaviour):\n");
    ok &= runCase("+7 semitones",               sr, std::pow(2.0, 7.0 / 12.0),
                                                0.00f, 0.0,  false, true, false, true).pass;
    ok &= runCase("+7 semitones + 0.5st vib",   sr, std::pow(2.0, 7.0 / 12.0),
                                                0.50f, 0.50, false, true, false, true).pass;

    // The pitch must arrive on BOTH paths. Without this the harness cannot see a
    // path that silently drops its modulation — the direct read's original defect.
    printf(" the bend arrives (sustained +2st mid-note):\n");
    ok &= runBendCase("latched direct read",       sr, 0.00f, 2.0, false).pass;
    ok &= runBendCase("latched stretch",           sr, 0.50f, 2.0, true).pass;
    ok &= runBendCase("latched direct read, -5st", sr, 0.00f, -5.0, false).pass;

    printf(ok ? "ALL PASS\n" : "FAILURES PRESENT\n");
    return ok ? 0 : 1;
}
