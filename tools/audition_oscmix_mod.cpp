// Offline audition for OscMix (A/B DCO mix) as a per-sample Env/LFO
// modulation target -- the equal-power A/harmonic <-> B/inharmonic mix
// POSITION now moves every sample under envelope/LFO drive, not just the
// 200 ms Regen-XFade-glided recipe presence the sibling guards.
//
// The sibling audition_dco_presence_follow.cpp guards the RECIPE half of the
// dual A+B DCO oscillator (which oscillator is audible at all, glided over
// driftCrossfade on a recipe flip). This tool guards the POSITION half added
// on top of it: SynthVoice::renderBlock now computes
//   mixPos = clamp01(p.oscMix + [env/LFO OscMix contributions])
// per sample and interpolates the equal-power law itself by a smoothed
// "dualness" D (1 = law live, 0 = law inert/solo) -- so a recipe flip fades
// the LAW, not just the recipe's raw gains, and a live env/LFO can swing the
// mix position continuously without waiting on driftCrossfade at all.
//
// We drive a real SynthVoice in Wavetable mode with A = a baked harmonic
// wavetable and B = a real-time inharmonic additive bank (same banks as the
// sibling) and check four cases:
//   1. env I-attack -> H-body: knob pinned at pure A, an env with a fast
//      attack/decay routed to OscMix sweeps the position to full B on
//      attack and back to full A by the time it decays to sustain 0.
//   2. lfo mix sweep: knob centred, a slow sine LFO routed to OscMix swings
//      the position from clamped-A to clamped-B and back every period.
//   3. recipe flip under live mod: an env is still actively swinging the
//      position (mid-decay, NOT settled) when the recipe flips dual -> A
//      only mid-note. The flip must still be a click-free crossfade (the
//      held-note invariant), and once the Regen XFade settles (D -> 0) the
//      output must be provably inert to the still-live env -- no stale B.
//   4. swell regression: dual -> A-only flip with an OFF-CENTRE knob and an
//      R1 gain (dcoGainA) > 1, NO live modulation -- isolates a recipe flip's
//      own gain-law arithmetic from any env/LFO effect. This is the
//      configuration adversarial review proved swells (+2.44 dB measured)
//      under the retired PRODUCT-form per-source weight law
//      (baseGain(t) * (1 + d(t) * (eqp - 1))): a PRODUCT of two
//      independently-glided linear ramps bulges above both settled endpoints
//      whenever the base gain exceeds 1 and the knob is off-centre. The
//      sum-form law fixed here (weight = solo part + dual part * eqp, a
//      linear blend of the two recipe endpoints' laws for a FIXED mix
//      position) cannot overshoot for ANY gain -- this case must PASS, and
//      must FAIL again if a product form is reintroduced.
// Each case's phase-independent timbre check (cases 1-3) is the sibling's
// maxXcorr against dedicated pure-A-solo / pure-B-solo reference renders
// (solo takes the !dcoUseMixLaw path, so these are clean, law-free anchors).
// The click-free check is the sibling's naturalMax-relative per-sample-delta
// ceiling, applied over the whole render (case 1/2, continuous modulation)
// or over the seam+ramp window (case 3/4, a discrete recipe flip). The
// no-swell check (cases 3/4, ported from the sibling) is the sibling's
// windowPeak-vs-settled-endpoint-peak ceiling (<= endpoint * 1.05).
//
// Build: compile against the plugin's static lib + JUCE flags, e.g.
//   FLAGS=build_clean/CMakeFiles/T5ynth.dir/flags.make
//   { grep -m1 CXX_DEFINES "$FLAGS"; grep -m1 CXX_INCLUDES "$FLAGS"; } \
//     | sed 's/^CXX_[A-Z]* = //' > /tmp/h.rsp
//   echo -I$PWD/build_clean/_deps/signalsmith_stretch-src >> /tmp/h.rsp
//   clang++ -std=c++17 -O2 @/tmp/h.rsp tools/audition_oscmix_mod.cpp \
//     build_clean/T5ynth_artefacts/Release/libT5ynth_SharedCode.a \
//     -framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio \
//     -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation \
//     -framework IOKit -framework QuartzCore -framework Security -framework WebKit \
//     -weak_framework Metal -weak_framework MetalKit -o /tmp/oscmix_mod
// Exits non-zero if any case fails its assertions.
//
// Negative control (proves the guard has teeth AND that driftCrossfade drives
// the ramp, not a fixed declicker): rebuild with the crossfade forced to
// ~0 ms and every recipe-flip case (3 and 4) MUST fail with a stepped
// seam --
//   sed 's/200\.0f,/0.02f,/g' tools/audition_oscmix_mod.cpp > /tmp/neg_oscmix.cpp
//   (compile /tmp/neg_oscmix.cpp the same way) -> seamDelta/windowMax jump,
//   cases 3 and 4 report *** FAIL *** (clickFree flips false).
#include "JuceHeader.h"
#include "dsp/SynthVoice.h"
#include "dsp/WavetableOscillator.h"
#include "dsp/LFO.h"
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
// harmonics -> low per-sample slew, so B (inharmonic, richer) sets naturalMax and
// the equal-power crossfade stays under the click threshold. (Cribbed from
// audition_dco_presence_follow.cpp -- same banks, so the two guards' notion of
// "A" and "B" match.)
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
// well below 1.0 when the timbre differs.
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

// Max per-sample delta over [a, b) -- the click test, factored out of the
// sibling's local lambda since all four cases here share it.
static float maxDeltaIn(const std::vector<float>& v, int a, int b)
{
    float m = 0.0f;
    a = std::max(a, 0);
    b = std::min(b, (int) v.size() - 1);
    for (int i = a; i < b; ++i)
        m = std::max(m, std::abs(v[i + 1] - v[i]));
    return m;
}

// Max |sample| over [a, b) -- the sibling's swell test, factored out here
// since the recipe-flip case and the swell-regression case both need it: the
// PRODUCT-form law that used to compute the per-source weight
// (baseGain(t)*(1 + d(t)*(eqp-1))) could bulge above BOTH settled endpoints
// mid-glide whenever an oscillator is audible in both the pre- and post-flip
// states with a gain > 1 -- the sum-form law fixed here is linear-in-t for a
// fixed mix position and provably cannot, but the guard must still measure it
// directly rather than trust the derivation.
static float peakAbsIn(const std::vector<float>& v, int a, int b)
{
    float m = 0.0f;
    a = std::max(a, 0);
    b = std::min(b, (int) v.size());
    for (int i = a; i < b; ++i)
        m = std::max(m, std::abs(v[i]));
    return m;
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
    p.wtAutoScan = false;             // static scan -- steady tone, clean seam measurement
    p.baseScan = 0.0f;
    p.oscMix = 0.5f;                  // equal-power centre; overridden per case
    p.dcoGainA = 1.0f;
    p.dcoGainB = 1.0f;
    p.driftCrossfade = driftCrossfadeMs;
    return p;
}

// Steady solo reference (no OscMix routing, no recipe change): unity-gain A-only
// or B-only, held single note. Solo takes the !dcoUseMixLaw path (D pinned at 0),
// so this is a clean, law-free anchor for the phase-independent xcorr checks --
// "is the mix A-dominant or B-dominant right now" everywhere below.
static std::vector<float> renderSoloReference(bool aHas, int numBlocks, float driftCrossfadeMs, int sr)
{
    const int block = 512;

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
    p.dcoOscAHasContent = aHas;
    p.dcoOscBHasContent = !aHas;

    std::vector<float> zeros(static_cast<size_t>(block), 0.0f);
    std::vector<float> outL(static_cast<size_t>(block)), outR(static_cast<size_t>(block));
    std::vector<float> out;

    voice.configureForBlock(p);
    voice.noteOn(69, 1.0f, /*legato=*/false);
    for (int b = 0; b < numBlocks; ++b) {
        voice.configureForBlock(p);
        voice.renderBlock(outL.data(), outR.data(), p, zeros.data(), zeros.data(), zeros.data(), block);
        out.insert(out.end(), outL.begin(), outL.end());
    }
    return out;
}

// Case 1: knob pinned at pure A (oscMix = 0), mod1 env -> OscMix with a fast
// attack/decay sweeps the position to full B during attack and back to full A
// once it decays to sustain 0 -- envelope-driven inharmonic-attack ->
// harmonic-body, the headline use case from the spec.
static bool runEnvAttackCase(float driftCrossfadeMs, const std::string& wavOut, int sr)
{
    const int block = 512;
    const int numBlocks = 80;   // ~853 ms: attack(5 ms)+decay(150 ms)+a long settled A tail

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
    p.oscMix = 0.0f;                    // knob at pure A; the env alone swings the position
    p.dcoOscAHasContent = true;
    p.dcoOscBHasContent = true;         // dual recipe -- D pinned at 1 for the whole render
    p.dcoGainA = 1.0f;
    p.dcoGainB = 1.0f;
    p.mod1Target = EnvTarget::OscMix;
    p.mod1Amount = 1.0f;
    p.mod1Attack = 5.0f;
    p.mod1Decay = 150.0f;
    p.mod1Sustain = 0.0f;
    p.mod1Release = 500.0f;

    std::vector<float> zeros(static_cast<size_t>(block), 0.0f);
    std::vector<float> outL(static_cast<size_t>(block)), outR(static_cast<size_t>(block));
    std::vector<float> out;

    voice.configureForBlock(p);
    voice.noteOn(69, 1.0f, /*legato=*/false);
    for (int b = 0; b < numBlocks; ++b) {
        voice.configureForBlock(p);
        voice.renderBlock(outL.data(), outR.data(), p, zeros.data(), zeros.data(), zeros.data(), block);
        out.insert(out.end(), outL.begin(), outL.end());
    }

    const auto pureA = renderSoloReference(true,  numBlocks, driftCrossfadeMs, sr);
    const auto pureB = renderSoloReference(false, numBlocks, driftCrossfadeMs, sr);

    const int earlyOff  = static_cast<int>(0.006 * sr);   // 6 ms: just past the 5 ms attack apex
    const int earlySnip = 1024;
    const int lateOff   = static_cast<int>(0.5 * sr);     // 500 ms: 345 ms past the 150 ms decay's end
    const int lateSnip  = 2048;

    std::vector<float> earlyWin(out.begin() + earlyOff, out.begin() + earlyOff + earlySnip);
    std::vector<float> lateWin(out.begin() + lateOff, out.begin() + lateOff + lateSnip);
    std::vector<float> pureAEarly(pureA.begin() + earlyOff, pureA.begin() + earlyOff + earlySnip);
    std::vector<float> pureBEarly(pureB.begin() + earlyOff, pureB.begin() + earlyOff + earlySnip);
    std::vector<float> pureALate(pureA.begin() + lateOff, pureA.begin() + lateOff + lateSnip);
    std::vector<float> pureBLate(pureB.begin() + lateOff, pureB.begin() + lateOff + lateSnip);

    const double xcorrEarlyA = maxXcorr(earlyWin, pureAEarly);
    const double xcorrEarlyB = maxXcorr(earlyWin, pureBEarly);
    const double xcorrLateA  = maxXcorr(lateWin, pureALate);
    const double xcorrLateB  = maxXcorr(lateWin, pureBLate);

    const bool earlyIsB = (xcorrEarlyB - xcorrEarlyA) > 0.2;
    const bool lateIsA  = (xcorrLateA - xcorrLateB) > 0.2;

    // Click-free across the WHOLE render (continuous modulation, no discrete
    // seam): naturalMax is the busier pure solo tone's own settled per-sample
    // slew (sibling's naturalMax convention), windowMax is the max slew
    // anywhere in the actual swept render.
    const int refGuard = static_cast<int>(0.05 * sr);   // 50 ms settle guard past each solo's own onset
    const float naturalMax = std::max(maxDeltaIn(pureA, refGuard, (int) pureA.size() - 1),
                                       maxDeltaIn(pureB, refGuard, (int) pureB.size() - 1));
    const float windowMax = maxDeltaIn(out, 1, (int) out.size() - 1);
    const bool clickFree = windowMax <= naturalMax * 1.5f + 1.0e-4f;

    writeWav(wavOut, out, sr);

    const bool pass = earlyIsB && lateIsA && clickFree;
    printf("  [env I-attack -> H-body   ] earlyXcorr A=%.3f B=%.3f  lateXcorr A=%.3f B=%.3f  windowMax=%.5f naturalMax=%.5f -> %s\n",
           xcorrEarlyA, xcorrEarlyB, xcorrLateA, xcorrLateB, windowMax, naturalMax,
           pass ? "PASS" : "*** FAIL ***");
    return pass;
}

// Case 2: knob centred (oscMix = 0.5), lfo1 -> OscMix (sine, ~0.5 Hz, depth 1.0)
// swings the position from clamped-A to clamped-B and back every 2 s period.
static bool runLfoSweepCase(float driftCrossfadeMs, const std::string& wavOut, int sr)
{
    const int block = 512;
    const int numBlocks = 220;   // ~2.347 s -- comfortably more than one 2 s (0.5 Hz) period

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
    p.oscMix = 0.5f;                 // knob centred; the LFO alone swings the position
    p.dcoOscAHasContent = true;
    p.dcoOscBHasContent = true;
    p.dcoGainA = 1.0f;
    p.dcoGainB = 1.0f;
    p.lfo1Target = LfoTarget::OscMix;
    p.lfo1Wave = LfoWave::Sine;
    p.lfo1Rate = 0.5f;
    p.lfo1Depth = 1.0f;

    // Persistent LFO across the whole render so phase is continuous block to
    // block -- exactly how the plugin's own free-running global LFO buffer is
    // filled (PluginProcessor.cpp), unity depth (SynthVoice applies p.lfo1Depth
    // itself, same convention as SynthVoice::renderBlock's own fillPerVoice).
    LFO lfo1;
    lfo1.prepare(sr);
    lfo1.setRate(p.lfo1Rate);
    lfo1.setWaveform(LfoWave::Sine);
    lfo1.setDepth(1.0f);

    std::vector<float> lfo1Buf(static_cast<size_t>(block));
    std::vector<float> zeros(static_cast<size_t>(block), 0.0f);
    std::vector<float> outL(static_cast<size_t>(block)), outR(static_cast<size_t>(block));
    std::vector<float> out;

    voice.configureForBlock(p);
    voice.noteOn(69, 1.0f, /*legato=*/false);
    for (int b = 0; b < numBlocks; ++b) {
        for (int i = 0; i < block; ++i)
            lfo1Buf[static_cast<size_t>(i)] = lfo1.processSample();
        voice.configureForBlock(p);
        voice.renderBlock(outL.data(), outR.data(), p, lfo1Buf.data(), zeros.data(), zeros.data(), block);
        out.insert(out.end(), outL.begin(), outL.end());
    }

    const auto pureA = renderSoloReference(true,  numBlocks, driftCrossfadeMs, sr);
    const auto pureB = renderSoloReference(false, numBlocks, driftCrossfadeMs, sr);

    // B-dominant flat-top: mixPos clamps to 1.0 whenever sin(2*pi*0.5*t) >= 0.5,
    // i.e. within [166.7, 833.3] ms of the 2 s period. A-dominant flat-bottom:
    // clamps to 0.0 within [1166.7, 1833.3] ms. Snippets sit deep inside each.
    const int bOff = static_cast<int>(0.4 * sr);
    const int aOff = static_cast<int>(1.4 * sr);
    const int snip = 2048;

    std::vector<float> bWin(out.begin() + bOff, out.begin() + bOff + snip);
    std::vector<float> aWin(out.begin() + aOff, out.begin() + aOff + snip);
    std::vector<float> pureAAtB(pureA.begin() + bOff, pureA.begin() + bOff + snip);
    std::vector<float> pureBAtB(pureB.begin() + bOff, pureB.begin() + bOff + snip);
    std::vector<float> pureAAtA(pureA.begin() + aOff, pureA.begin() + aOff + snip);
    std::vector<float> pureBAtA(pureB.begin() + aOff, pureB.begin() + aOff + snip);

    const double xcorrBWinA = maxXcorr(bWin, pureAAtB);
    const double xcorrBWinB = maxXcorr(bWin, pureBAtB);
    const double xcorrAWinA = maxXcorr(aWin, pureAAtA);
    const double xcorrAWinB = maxXcorr(aWin, pureBAtA);

    const bool bWinIsB = (xcorrBWinB - xcorrBWinA) > 0.2;
    const bool aWinIsA = (xcorrAWinA - xcorrAWinB) > 0.2;

    const int refGuard = static_cast<int>(0.05 * sr);
    const float naturalMax = std::max(maxDeltaIn(pureA, refGuard, (int) pureA.size() - 1),
                                       maxDeltaIn(pureB, refGuard, (int) pureB.size() - 1));
    const float windowMax = maxDeltaIn(out, 1, (int) out.size() - 1);
    const bool clickFree = windowMax <= naturalMax * 1.5f + 1.0e-4f;

    writeWav(wavOut, out, sr);

    const bool pass = bWinIsB && aWinIsA && clickFree;
    printf("  [lfo mix sweep            ] bWinXcorr A=%.3f B=%.3f  aWinXcorr A=%.3f B=%.3f  windowMax=%.5f naturalMax=%.5f -> %s\n",
           xcorrBWinA, xcorrBWinB, xcorrAWinA, xcorrAWinB, windowMax, naturalMax,
           pass ? "PASS" : "*** FAIL ***");
    return pass;
}

// Case 3: mod1 env -> OscMix with a LONG decay is still actively swinging the
// position (mid-decay, not settled) when the recipe flips dual -> A-only
// mid-note. The flip must still be a click-free crossfade (held-note
// invariant), and once the Regen XFade settles (D -> 0), the output must be
// provably inert to the still-live env -- no stale B leaking through.
static bool runRecipeFlipCase(float driftCrossfadeMs, const std::string& wavOut, int sr)
{
    const int block = 512;
    const int preBlocks  = 19;   // ~202.7 ms: env is ~9.9% into a 2 s decay at the flip (mid-swing)
    const int postBlocks = 47;   // ~501.3 ms post-flip: covers the Regen XFade + settle

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
    p.oscMix = 0.0f;
    p.dcoOscAHasContent = true;
    p.dcoOscBHasContent = true;      // start dual
    p.dcoGainA = 1.0f;
    p.dcoGainB = 1.0f;
    p.mod1Target = EnvTarget::OscMix;
    p.mod1Amount = 1.0f;
    p.mod1Attack = 5.0f;
    p.mod1Decay = 2000.0f;           // long decay: still actively swinging at the flip and long after
    p.mod1Sustain = 0.0f;
    p.mod1Release = 500.0f;

    std::vector<float> zeros(static_cast<size_t>(block), 0.0f);
    std::vector<float> outL(static_cast<size_t>(block)), outR(static_cast<size_t>(block));
    std::vector<float> out;
    auto renderBlocks = [&](int nBlocks) {
        for (int b = 0; b < nBlocks; ++b) {
            voice.configureForBlock(p);
            voice.renderBlock(outL.data(), outR.data(), p, zeros.data(), zeros.data(), zeros.data(), block);
            out.insert(out.end(), outL.begin(), outL.end());
        }
    };

    voice.configureForBlock(p);
    voice.noteOn(69, 1.0f, /*legato=*/false);
    renderBlocks(preBlocks);
    const int seamIndex = static_cast<int>(out.size());   // first sample after the flip

    p.dcoOscBHasContent = false;     // the flip: dual -> A-only, mid-swing (env still decaying)
    renderBlocks(postBlocks);

    const int rampSamples = static_cast<int>(std::lround((double) driftCrossfadeMs * 0.001 * sr));
    const int winEnd = seamIndex + rampSamples + 2048;   // ramp + a small settle guard

    // Continuity at the seam -- printed the same way the sibling does.
    const float seamDelta = std::abs(out[seamIndex] - out[seamIndex - 1]);

    const auto pureA = renderSoloReference(true,  preBlocks + postBlocks, driftCrossfadeMs, sr);
    const auto pureB = renderSoloReference(false, preBlocks + postBlocks, driftCrossfadeMs, sr);
    const int refGuard = static_cast<int>(0.05 * sr);
    const float naturalMax = std::max(maxDeltaIn(pureA, refGuard, (int) pureA.size() - 1),
                                       maxDeltaIn(pureB, refGuard, (int) pureB.size() - 1));
    const float windowMax = maxDeltaIn(out, seamIndex - 1, winEnd);
    const bool clickFree = windowMax <= naturalMax * 1.5f + 1.0e-4f;

    // After the crossfade settles, the modulation must be provably inert: the
    // env (2 s decay) is still ~70% of peak at this point, so if the law were
    // still coupled, the output would still lean B (eqpB ~ 0.9). Correlating
    // with pure-A instead proves D reached 0 and gated the law -- not that
    // mixPos merely coasted near 0 on its own.
    const int settleOff  = winEnd + static_cast<int>(0.1 * sr);   // +100 ms past the ramp+guard
    const int settleSnip = 2048;
    std::vector<float> settleWin(out.begin() + settleOff, out.begin() + settleOff + settleSnip);
    std::vector<float> pureARef(pureA.begin() + settleOff, pureA.begin() + settleOff + settleSnip);
    std::vector<float> pureBRef(pureB.begin() + settleOff, pureB.begin() + settleOff + settleSnip);
    const double xcorrA = maxXcorr(settleWin, pureARef);
    const double xcorrB = maxXcorr(settleWin, pureBRef);
    const bool inertOnA = xcorrA > 0.97;

    // No level swell through the seam+ramp window (sibling's noSwell
    // convention, ported here): the ceiling is the louder of the two settled
    // endpoint peaks (pre-flip dual-with-live-env, post-flip A-solo-settled).
    const float prePeak    = peakAbsIn(out, seamIndex - 8192, seamIndex - 256);
    const float postPeak   = peakAbsIn(out, winEnd, (int) out.size());
    const float endpointPeak = std::max(prePeak, postPeak);
    const float windowPeak = peakAbsIn(out, seamIndex, winEnd);
    const bool noSwell = windowPeak <= endpointPeak * 1.05f + 1.0e-4f;

    writeWav(wavOut, out, sr);

    const bool pass = clickFree && inertOnA && noSwell;
    printf("  [recipe flip under live mod] seamDelta=%.5f windowMax=%.5f naturalMax=%.5f settleXcorr A=%.3f B=%.3f winPk=%.3f endPk=%.3f -> %s\n",
           seamDelta, windowMax, naturalMax, xcorrA, xcorrB, windowPeak, endpointPeak, pass ? "PASS" : "*** FAIL ***");
    return pass;
}

// Case 4: dual -> A-only recipe flip with an OFF-CENTRE knob and an R1 gain
// > 1, NO modulation routed -- the exact configuration adversarial review
// proved swells under the retired PRODUCT-form law (per-source weight =
// baseGain(t) * (1 + d(t) * (eqp - 1)), a PRODUCT of two independently-glided
// linear ramps): dcoGainA pushes the dual-regime steady weight for A above
// unity, and an off-centre knob means the base-gain ramp and the dualness
// ramp bulge the product mid-glide even though both ramps individually are
// monotonic. gA=3.0/knob=0.79 measured +2.44 dB overshoot under that law; the
// sum-form law fixed here (per-source weight = solo part + dual part * eqp,
// a linear blend of the two recipe endpoints' laws for a FIXED mix position)
// must pass -- and must fail again if a product form is reintroduced (see
// the negative-control note above).
static bool runSwellCase(float dcoGainA, float knobOscMix, const std::string& label,
                          float driftCrossfadeMs, const std::string& wavOut, int sr)
{
    const int block = 512;
    const int preBlocks  = 40;   // ~0.43 s held on dual before the flip
    const int postBlocks = 90;   // ~0.96 s after the flip -- covers the ramp + settle

    WavetableOscillator masterA, masterB;
    masterA.prepare(sr, block);
    masterB.prepare(sr, block);
    masterA.setExactFrames(makeHarmonicStrip());
    // B is a ZERO strip here — deliberately setExactFrames, NOT
    // setAdditiveBank (the additive path renormalizes any bank to 0.95 peak,
    // which would resurrect B). With B silent the rendered output IS A's
    // weight trajectory, so the noSwell assertion measures the mix law
    // itself: under the retired product-form law (base × dualness-lerp) this
    // case fails at ~+32% window peak (A's gain bulge is no longer masked by
    // a loud B in the sum); under the sum-form law it passes at ~0%. B still
    // hasFrames(), so the dual recipe and the flip machinery are fully
    // exercised — only B's audio contribution is zeroed.
    juce::AudioBuffer<float> zeroStrip(1, WavetableOscillator::FRAME_SIZE);
    zeroStrip.clear();
    masterB.setExactFrames(zeroStrip);

    SynthVoice voice;
    voice.prepare(sr, block);
    voice.setEngineMode(SynthVoice::EngineMode::Wavetable);
    voice.getOsc().shareFramesFrom(masterA);
    voice.getOscB().shareFramesFrom(masterB);

    BlockParams p = makeSteadyWavetableParams(driftCrossfadeMs);
    p.oscMix = knobOscMix;           // off-centre; no env/LFO routed to OscMix (struct defaults: None)
    p.dcoOscAHasContent = true;
    p.dcoOscBHasContent = true;      // start dual
    p.dcoGainA = dcoGainA;
    p.dcoGainB = 1.0f;

    std::vector<float> zeros(static_cast<size_t>(block), 0.0f);
    std::vector<float> outL(static_cast<size_t>(block)), outR(static_cast<size_t>(block));
    std::vector<float> out;
    auto renderBlocks = [&](int nBlocks) {
        for (int b = 0; b < nBlocks; ++b) {
            voice.configureForBlock(p);
            voice.renderBlock(outL.data(), outR.data(), p, zeros.data(), zeros.data(), zeros.data(), block);
            out.insert(out.end(), outL.begin(), outL.end());
        }
    };

    voice.configureForBlock(p);
    voice.noteOn(69, 1.0f, /*legato=*/false);
    renderBlocks(preBlocks);
    const int seamIndex = static_cast<int>(out.size());   // first sample after the flip

    p.dcoOscBHasContent = false;     // the flip: dual -> A-only, mid-note, no modulation live
    renderBlocks(postBlocks);

    const int rampSamples = static_cast<int>(std::lround((double) driftCrossfadeMs * 0.001 * sr));
    const int winEnd = seamIndex + rampSamples + 2048;   // ramp + a small settle guard

    const float seamDelta = std::abs(out[seamIndex] - out[seamIndex - 1]);

    const auto pureA = renderSoloReference(true,  preBlocks + postBlocks, driftCrossfadeMs, sr);
    const auto pureB = renderSoloReference(false, preBlocks + postBlocks, driftCrossfadeMs, sr);
    const int refGuard = static_cast<int>(0.05 * sr);
    const float naturalMax = std::max(maxDeltaIn(pureA, refGuard, (int) pureA.size() - 1),
                                       maxDeltaIn(pureB, refGuard, (int) pureB.size() - 1));
    const float windowMax = maxDeltaIn(out, seamIndex - 1, winEnd);
    const bool clickFree = windowMax <= naturalMax * 1.5f + 1.0e-4f;

    // No level swell: the settled pre-flip dual weight for A is
    // dcoGainA * eqpA(knob), which for gA=3/knob=0.79 already sits near
    // unity by design of the case -- the ceiling is still the louder of the
    // two settled endpoints (pre-flip dual, post-flip A-solo), same
    // convention as the recipe-flip case above.
    const float prePeak    = peakAbsIn(out, seamIndex - 8192, seamIndex - 256);
    const float postPeak   = peakAbsIn(out, winEnd, (int) out.size());
    const float endpointPeak = std::max(prePeak, postPeak);
    const float windowPeak = peakAbsIn(out, seamIndex, winEnd);
    const bool noSwell = windowPeak <= endpointPeak * 1.05f + 1.0e-4f;

    writeWav(wavOut, out, sr);

    const bool pass = clickFree && noSwell;
    printf("  [%-26s] seamDelta=%.5f windowMax=%.5f naturalMax=%.5f winPk=%.3f endPk=%.3f gA=%.2f knob=%.2f -> %s\n",
           label.c_str(), seamDelta, windowMax, naturalMax, windowPeak, endpointPeak, dcoGainA, knobOscMix,
           pass ? "PASS" : "*** FAIL ***");
    return pass;
}

int main()
{
    const int sr = 48000;
    juce::ScopedJuceInitialiser_GUI juceInit;   // safe init for any JUCE statics

    printf("OscMix (A/B DCO mix) per-sample Env/LFO modulation audition (A=harmonic wavetable, B=inharmonic additive, 200 ms Regen XFade):\n");
    bool ok = true;
    ok &= runEnvAttackCase(200.0f, "/tmp/oscmix_env_attack.wav", sr);
    ok &= runLfoSweepCase(200.0f, "/tmp/oscmix_lfo_sweep.wav", sr);
    ok &= runRecipeFlipCase(200.0f, "/tmp/oscmix_recipe_flip.wav", sr);
    printf("Swell regression (dual -> A-only flip, off-centre knob, R1 gain > 1, no live modulation):\n");
    ok &= runSwellCase(3.0f, 0.79f, "dual->solo swell, gA>1",   200.0f, "/tmp/oscmix_swell_gA3.wav", sr);
    ok &= runSwellCase(1.6f, 0.57f, "dual->solo swell, gA=1.6", 200.0f, "/tmp/oscmix_swell_gA1_6.wav", sr);
    printf("WAVs: /tmp/oscmix_env_attack.wav  /tmp/oscmix_lfo_sweep.wav  /tmp/oscmix_recipe_flip.wav  /tmp/oscmix_swell_gA3.wav  /tmp/oscmix_swell_gA1_6.wav\n");
    printf("%s\n", ok ? "ALL PASS" : "*** FAIL ***");
    return ok ? 0 : 1;
}
