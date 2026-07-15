// Offline audition for the held-note DCO A/B PRESENCE crossfade
// (SynthVoice dual-oscillator presence ramp).
//
// The sibling audition_sampler_follow.cpp guards the sampler's buffer-content
// morph. This one guards the OTHER half of the same held-note invariant: when a
// new DCO recipe changes WHICH oscillator is audible (dcoOscAHasContent /
// dcoOscBHasContent flipping) on a held note, SynthVoice must equal-power
// crossfade the per-oscillator mix gains over the Regen XFade time — NOT hard-cut
// the oscillator that just lost its flag. Before the fix the mix step gated the
// oscillator's SYNTH on the content flag, so the instant a flag flipped the
// losing oscillator's samples vanished and the seam clicked.
//
// We drive a real SynthVoice in Wavetable mode with A = a baked harmonic
// wavetable and B = a real-time inharmonic additive bank, hold a note, then flip
// the presence flags mid-render (exactly what setDcoOscBalance does after a bake)
// and measure the seam. Two cases: the A-solo -> B-solo handoff (the classic hard
// cut) and A-solo -> dual A+B (B fades in alongside A). Each asserts the seam is
// click-free (max per-sample delta through the crossfade window stays near the
// signal's natural per-sample delta) AND that the timbre actually transitioned
// (phase-independent max cross-correlation of the pre vs post steady snippet drops
// well below 1 — a stuck fade would leave it ~1).
//
// Build: compile against the plugin's static lib + JUCE flags, e.g.
//   FLAGS=build_clean/CMakeFiles/T5ynth.dir/flags.make
//   { grep -m1 CXX_DEFINES "$FLAGS"; grep -m1 CXX_INCLUDES "$FLAGS"; } \
//     | sed 's/^CXX_[A-Z]* = //' > /tmp/h.rsp
//   echo -I$PWD/build_clean/_deps/signalsmith_stretch-src >> /tmp/h.rsp
//   clang++ -std=c++17 -O2 @/tmp/h.rsp tools/audition_dco_presence_follow.cpp \
//     build_clean/T5ynth_artefacts/Release/libT5ynth_SharedCode.a \
//     -framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio \
//     -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation \
//     -framework IOKit -framework QuartzCore -framework Security -framework WebKit \
//     -weak_framework Metal -weak_framework MetalKit -o /tmp/dco_presence_follow
// Exits non-zero if any case is not a click-free, actually-transitioning crossfade.
//
// Negative control (proves the guard has teeth AND that driftCrossfade drives the
// ramp, not a fixed declicker): rebuild with the crossfade forced to ~0 ms and it
// MUST fail with a stepped seam —
//   sed 's/200\.0f,/0.02f,/g' tools/audition_dco_presence_follow.cpp > /tmp/neg.cpp
//   (compile /tmp/neg.cpp the same way) → seamDelta jumps ~20x, verdict *** STEP ***.
#include "JuceHeader.h"
#include "dsp/SynthVoice.h"
#include "dsp/WavetableOscillator.h"
#include "dsp/BlockParams.h"

#include <cstdio>
#include <vector>
#include <cmath>
#include <string>
#include <fstream>
#include <algorithm>

static void writeWav(const std::string& path, const std::vector<float>& mono, int sr)
{
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v){ f.write(reinterpret_cast<char*>(&v), 4); };
    auto u16 = [&](uint16_t v){ f.write(reinterpret_cast<char*>(&v), 2); };
    uint32_t dataBytes = static_cast<uint32_t>(mono.size()) * 2u;
    f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(1); u32(sr); u32(sr * 2); u16(2); u16(16);
    f.write("data", 4); u32(dataBytes);
    for (float s : mono) {
        int v = static_cast<int>(std::lround(std::max(-1.f, std::min(1.f, s)) * 32767.f));
        u16(static_cast<uint16_t>(static_cast<int16_t>(v)));
    }
}

// A = harmonic body: one single-cycle frame (setExactFrames pads to MIN_FRAMES,
// all identical, so scanning is a no-op and the tone is steady). Few, low
// harmonics → low per-sample slew, so B (inharmonic, richer) sets naturalMax and
// the equal-power crossfade stays under the click threshold.
static juce::AudioBuffer<float> makeHarmonicStrip()
{
    const int N = WavetableOscillator::FRAME_SIZE;
    juce::AudioBuffer<float> b(1, N);
    float* d = b.getWritePointer(0);
    for (int i = 0; i < N; ++i) {
        const double th = 2.0 * M_PI * i / N;
        d[i] = static_cast<float>(0.5 * std::sin(th) + 0.15 * std::sin(2.0 * th));
    }
    return b;
}

// B = inharmonic additive bank: non-integer partial ratios (bell/metal), a
// distinctly different, richer timbre than A.
static std::vector<WavetableOscillator::AdditivePartial> makeInharmonicBank()
{
    return {
        { 1.0f,  1.00f, 0.0f },
        { 2.7f,  0.70f, 0.0f },
        { 5.1f,  0.50f, 0.0f },
        { 8.4f,  0.30f, 0.0f },
    };
}

// Phase-independent similarity: max over lags of the normalized cross-correlation
// of two equal-length snippets. ~1.0 when they are the same waveform (any phase),
// well below 1.0 when the timbre differs. Used to confirm the crossfade actually
// moved from the pre-state timbre to the post-state timbre.
static double maxXcorr(const std::vector<float>& a, const std::vector<float>& b)
{
    const int n = static_cast<int>(std::min(a.size(), b.size()));
    const int win = n / 2;                 // correlate the first half, slide over the rest
    double na = 0.0;
    for (int i = 0; i < win; ++i) na += (double) a[i] * a[i];
    na = std::sqrt(std::max(na, 1e-12));
    double best = 0.0;
    for (int lag = 0; lag + win <= n; ++lag) {
        double dot = 0.0, nb = 0.0;
        for (int i = 0; i < win; ++i) {
            const double bv = b[i + lag];
            dot += (double) a[i] * bv;
            nb  += bv * bv;
        }
        nb = std::sqrt(std::max(nb, 1e-12));
        best = std::max(best, std::abs(dot) / (na * nb));
    }
    return best;
}

static BlockParams makeSteadyWavetableParams(float driftCrossfadeMs)
{
    BlockParams p;                    // struct defaults: filter off, noise 0, sustain 1
    p.engineMode = EngineMode::Wavetable;
    p.engineIsWavetable = true;
    p.ampAttack = 2.0f;               // reach sustain within a few ms
    p.ampDecay = 0.0f;
    p.ampSustain = 1.0f;
    p.ampRelease = 500.0f;
    p.ampAmount = 1.0f;
    p.ampTarget = EnvTarget::DCA;
    p.velAmt = 1.0f;
    p.filterEnabled = false;
    p.noiseLevel = 0.0f;
    p.wtSmooth = true;
    p.wtAutoScan = false;             // static scan — steady tone, clean seam measurement
    p.baseScan = 0.0f;
    p.oscMix = 0.5f;                  // equal-power centre for the dual case
    p.dcoGainA = 1.0f;
    p.dcoGainB = 1.0f;
    p.driftCrossfade = driftCrossfadeMs;
    return p;
}

static bool runCase(const std::string& label,
                    bool aPre, bool bPre, bool aPost, bool bPost,
                    float driftCrossfadeMs, const std::string& wavOut, int sr)
{
    const int block = 512;
    const int preBlocks = 40;    // ~0.43 s held on the pre-state
    const int postBlocks = 90;   // ~0.96 s after the presence flip (covers the ramp)

    // Masters own the published banks; the voice shares from them (exactly how
    // VoiceManager distributes masterOsc / masterOscB to a wavetable voice).
    WavetableOscillator masterA, masterB;
    masterA.prepare(sr, block);
    masterB.prepare(sr, block);
    masterA.setExactFrames(makeHarmonicStrip());
    masterB.setAdditiveBank(makeInharmonicBank());

    SynthVoice voice;
    voice.prepare(sr, block);
    voice.setEngineMode(SynthVoice::EngineMode::Wavetable);
    voice.getOsc().shareFramesFrom(masterA);
    voice.getOscB().shareFramesFrom(masterB);

    BlockParams p = makeSteadyWavetableParams(driftCrossfadeMs);
    p.dcoOscAHasContent = aPre;
    p.dcoOscBHasContent = bPre;

    std::vector<float> zeros(static_cast<size_t>(block), 0.0f);
    std::vector<float> outL(static_cast<size_t>(block));
    std::vector<float> outR(static_cast<size_t>(block));
    std::vector<float> out;

    voice.configureForBlock(p);
    voice.noteOn(69, 1.0f, /*legato=*/false);   // A4 = 440 Hz

    auto renderBlocks = [&](int nBlocks) {
        for (int b = 0; b < nBlocks; ++b) {
            voice.configureForBlock(p);
            voice.renderBlock(outL.data(), outR.data(), p,
                              zeros.data(), zeros.data(), zeros.data(), block);
            out.insert(out.end(), outL.begin(), outL.end());
        }
    };

    renderBlocks(preBlocks);
    const int seamIndex = static_cast<int>(out.size());   // first sample after the flip

    // The presence flip: a new recipe changes which oscillator the mix wants
    // audible. This is the exact 4-field state setDcoOscBalance publishes; here it
    // is a pure flag change (both banks already shared), so it isolates the
    // presence ramp from the oscillators' own buffer-content morph.
    p.dcoOscAHasContent = aPost;
    p.dcoOscBHasContent = bPost;
    renderBlocks(postBlocks);

    const int rampSamples = static_cast<int>(std::lround((double) driftCrossfadeMs * 0.001 * sr));
    const int winEnd = seamIndex + rampSamples + 2048;   // ramp + a small settle guard

    // Continuity at the seam: the ramp starts at the pre-state gains, so the first
    // post-flip sample must continue the pre-state signal — no step here.
    const float seamDelta = std::abs(out[seamIndex] - out[seamIndex - 1]);

    auto maxDelta = [&](int a, int b) {
        float m = 0.0f;
        a = std::max(a, 0);
        b = std::min(b, (int) out.size() - 1);
        for (int i = a; i < b; ++i)
            m = std::max(m, std::abs(out[i + 1] - out[i]));
        return m;
    };

    // Max per-sample delta THROUGH the crossfade window — the click test.
    const float windowMax = maxDelta(seamIndex - 1, winEnd);

    // Natural per-sample delta reference = the BUSIER of the two settled states.
    // A crossfade legitimately carries the higher-slew constituent while it fades
    // (e.g. the inharmonic B still moving at ~0.7 gain during a dual->A-solo fade-
    // out), so the ceiling must be the max over BOTH the settled pre-state and the
    // settled post-state — not the post-state alone, which for a fade-OUT is the
    // calmer signal and would false-flag legitimate motion as a step. A real hard
    // cut jumps far above either state's natural delta, so this still catches it.
    const float preNatural  = maxDelta(seamIndex - 8192, seamIndex - 256);
    const float postNatural = maxDelta(winEnd, (int) out.size() - 1);
    const float naturalMax  = std::max(preNatural, postNatural);

    // Timbre actually transitioned: compare a settled pre snippet to a settled post
    // snippet. Same waveform (stuck fade) → maxXcorr ~1; a real crossfade to a
    // different audible set → clearly below 1.
    const int snip = 4096;
    std::vector<float> preSnip(out.begin() + (seamIndex - snip), out.begin() + seamIndex);
    std::vector<float> postSnip(out.begin() + winEnd, out.begin() + std::min((int) out.size(), winEnd + snip));
    const double xcorr = maxXcorr(preSnip, postSnip);

    // No level swell: interpolating the GAIN vector with cos/sin equal-power
    // overshoots (>unity) whenever an oscillator is audible in BOTH states, because
    // the start/target mixes share the same aSample/bSample (correlated). The peak
    // |output| through the crossfade must not exceed the louder settled endpoint by
    // more than a hair — a swell here means the gain law is bulging above unity and
    // driving the pre-VCA signal into the drive/filter stages.
    auto peakAbs = [&](int a, int b) {
        float m = 0.0f;
        a = std::max(a, 0);
        b = std::min(b, (int) out.size());
        for (int i = a; i < b; ++i) m = std::max(m, std::abs(out[i]));
        return m;
    };
    const float prePeak    = peakAbs(seamIndex - 8192, seamIndex - 256);
    const float postPeak   = peakAbs(winEnd, (int) out.size());
    const float windowPeak = peakAbs(seamIndex, winEnd);
    const float endpointPeak = std::max(prePeak, postPeak);

    writeWav(wavOut, out, sr);

    const bool clickFree = windowMax <= naturalMax * 1.5f + 1.0e-4f;
    const bool transitioned = xcorr < 0.97;
    const bool noSwell = windowPeak <= endpointPeak * 1.05f + 1.0e-4f;
    const bool pass = clickFree && transitioned && noSwell;
    const char* verdict = pass ? "CLICK-FREE XFADE"
                        : !clickFree ? "*** STEP ***"
                        : !noSwell ? "*** SWELL ***"
                        : "*** NO XFADE ***";
    printf("  [%-26s] seamDelta=%.5f windowMax=%.5f naturalMax=%.5f xcorr=%.3f winPk=%.3f endPk=%.3f xfade=%.0fms -> %s\n",
           label.c_str(), seamDelta, windowMax, naturalMax, xcorr, windowPeak, endpointPeak, driftCrossfadeMs, verdict);
    return pass;
}

// Mid-ramp RE-ARM: flip to an intermediate state, interrupt the crossfade partway,
// then flip again — the case a single-flip test never reaches. The second ramp
// starts from a NON-canonical gain vector (both oscillators nonzero mid-fade), so a
// set-based equal-power choice would bulge the gain >unity even for a set-disjoint
// handoff. Asserts no level swell after the second flip. (A and B are level-matched
// solos, so the settled A-solo endpoints bound the legitimate peak; the first
// partial ramp is a flat equal-power A<->B and never exceeds it.)
static bool runReArmCase(const std::string& label,
                         bool aMid, bool bMid, bool aPost, bool bPost,
                         float interruptFrac, float driftCrossfadeMs,
                         const std::string& wavOut, int sr)
{
    const int block = 512;
    const int preBlocks = 40;
    const int postBlocks = 90;
    const int rampSamples = static_cast<int>(std::lround((double) driftCrossfadeMs * 0.001 * sr));

    WavetableOscillator masterA, masterB;
    masterA.prepare(sr, block);
    masterB.prepare(sr, block);
    masterA.setExactFrames(makeHarmonicStrip());
    masterB.setAdditiveBank(makeInharmonicBank());

    SynthVoice voice;
    voice.prepare(sr, block);
    voice.setEngineMode(SynthVoice::EngineMode::Wavetable);
    voice.getOsc().shareFramesFrom(masterA);
    voice.getOscB().shareFramesFrom(masterB);

    BlockParams p = makeSteadyWavetableParams(driftCrossfadeMs);
    p.dcoOscAHasContent = true;    // start A-solo
    p.dcoOscBHasContent = false;

    std::vector<float> zeros(static_cast<size_t>(block), 0.0f);
    std::vector<float> outL(static_cast<size_t>(block)), outR(static_cast<size_t>(block));
    std::vector<float> out;
    auto render = [&](int nBlocks) {
        for (int b = 0; b < nBlocks; ++b) {
            voice.configureForBlock(p);
            voice.renderBlock(outL.data(), outR.data(), p, zeros.data(), zeros.data(), zeros.data(), block);
            out.insert(out.end(), outL.begin(), outL.end());
        }
    };

    voice.configureForBlock(p);
    voice.noteOn(69, 1.0f, false);
    render(preBlocks);
    const int flip1 = static_cast<int>(out.size());

    p.dcoOscAHasContent = aMid;  p.dcoOscBHasContent = bMid;      // flip 1
    const int partialBlocks = std::max(1, static_cast<int>(std::lround(interruptFrac * rampSamples / block)));
    render(partialBlocks);
    const int flip2 = static_cast<int>(out.size());

    p.dcoOscAHasContent = aPost; p.dcoOscBHasContent = bPost;     // flip 2 (re-arm mid-ramp)
    render(postBlocks);

    auto peakAbs = [&](int a, int b) {
        float m = 0.0f; a = std::max(a, 0); b = std::min(b, (int) out.size());
        for (int i = a; i < b; ++i) m = std::max(m, std::abs(out[i]));
        return m;
    };
    const int winEnd2 = flip2 + rampSamples + 2048;
    const float prePeak  = peakAbs(flip1 - 8192, flip1 - 256);
    const float postPeak = peakAbs(winEnd2, (int) out.size());
    const float endpointPeak = std::max(prePeak, postPeak);
    const float windowPeak = peakAbs(flip2, winEnd2);
    const float flip2Seam = (flip2 > 0 && flip2 < (int) out.size())
                          ? std::abs(out[flip2] - out[flip2 - 1]) : 0.0f;

    writeWav(wavOut, out, sr);
    const bool noSwell = windowPeak <= endpointPeak * 1.05f + 1.0e-4f;
    printf("  [%-26s] flip2Seam=%.5f reArmPeak=%.3f endPk=%.3f frac=%.2f -> %s\n",
           label.c_str(), flip2Seam, windowPeak, endpointPeak, interruptFrac,
           noSwell ? "NO SWELL" : "*** SWELL ***");
    return noSwell;
}

int main()
{
    const int sr = 48000;
    juce::ScopedJuceInitialiser_GUI juceInit;   // safe init for any JUCE statics

    printf("Held-note DCO presence-XFade audition (A=harmonic wavetable, B=inharmonic additive, 200 ms):\n");
    bool ok = true;
    //                                    aPre  bPre  aPost bPost
    ok &= runCase("A-solo -> B-solo",     true, false, false, true,  200.0f,
                  "/tmp/dco_presence_solo2solo.wav", sr);
    ok &= runCase("A-solo -> dual A+B",   true, false, true,  true,  200.0f,
                  "/tmp/dco_presence_solo2dual.wav", sr);
    ok &= runCase("dual A+B -> A-solo",   true, true,  true,  false, 200.0f,
                  "/tmp/dco_presence_dual2solo.wav", sr);
    printf("Mid-ramp re-arm (interrupt the A->B crossfade partway, then flip back to A):\n");
    // Both flips are set-disjoint (A-solo<->B-solo), so a set-based law choice would
    // pick equal-power for flip2 too — but flip2 starts from a non-canonical in-flight
    // gain vector, where cos/sin overshoots. 25% interrupt is the worse overshoot.
    //                                          aMid  bMid  aPost bPost  frac
    ok &= runReArmCase("A->B, 50%, ->A",        false, true, true, false, 0.50f, 200.0f,
                       "/tmp/dco_presence_rearm_50.wav", sr);
    ok &= runReArmCase("A->B, 25%, ->A",        false, true, true, false, 0.25f, 200.0f,
                       "/tmp/dco_presence_rearm_25.wav", sr);
    printf("WAVs: /tmp/dco_presence_solo2solo.wav  /tmp/dco_presence_solo2dual.wav  /tmp/dco_presence_dual2solo.wav\n");
    printf("%s\n", ok ? "ALL PASS" : "*** FAIL ***");
    return ok ? 0 : 1;
}
