// Gate-100 same-pitch TIE: two identical notes back-to-back at gate 100 must
// sound as ONE continuous note — no note-off, no re-attack — like a held manual
// note. Before the fix the step-seq emitted NoteOff C3 + NoteOn C3 (Normal) at
// the same sample every boundary; the VoiceManager reused the still-releasing
// voice (beginRestartFade + retrigger), so the release never rendered ("kein
// Release") and each repeat re-attacked ("neue Attack").
//
// Part 1 — DETERMINISTIC event-stream assertions on the REAL StepSequencer
//          (exits non-zero if any case regresses). This is the regression guard.
// Part 2 — AUDITION: render BEFORE (hand-driven old stream) vs AFTER (real fixed
//          seq) through the REAL VoiceManager+Sampler to /tmp WAVs, each as:
//          manual note (attack+release) -> C3/C3 gate-100 loop -> stop (release).
//
// Build (see audition_sampler_follow.cpp header for the flags recipe):
//   FLAGS=build_clean/CMakeFiles/T5ynth.dir/flags.make
//   { grep -m1 CXX_DEFINES "$FLAGS"; grep -m1 CXX_INCLUDES "$FLAGS"; } \
//     | sed 's/^CXX_[A-Z]* = //' > /tmp/h.rsp
//   echo -I$PWD/build_clean/_deps/signalsmith_stretch-src >> /tmp/h.rsp
//   clang++ -std=c++17 -O2 @/tmp/h.rsp tools/audition_gate_tie.cpp \
//     build_clean/T5ynth_artefacts/Release/libT5ynth_SharedCode.a \
//     -framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio \
//     -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation \
//     -framework IOKit -framework QuartzCore -framework Security -framework WebKit \
//     -weak_framework Metal -weak_framework MetalKit -o /tmp/gate_tie
#include "JuceHeader.h"
#include "dsp/VoiceManager.h"
#include "dsp/SamplePlayer.h"
#include "dsp/BlockParams.h"
#include "sequencer/StepSequencer.h"
#include <cstdio>
#include <vector>
#include <cmath>
#include <string>
#include <fstream>

using Artic = VoiceEvent::Articulation;
using Bind  = T5ynthStepSequencer::BindMode;
static float g_tuning[128];

static const char* artStr(Artic a){
    switch(a){ case Artic::Normal: return "Normal"; case Artic::Glide: return "Glide";
               case Artic::Bind: return "Bind"; } return "?";
}

// ── Run the REAL StepSequencer, collect every emitted event over `passes` loops ──
struct Ev { int blk; VoiceEvent::Type type; int note; Artic artic; };

static std::vector<Ev> runSeq(int numSteps, const int* notes, float gate,
                              int passes, int div = 2 /*1/4*/)
{
    const double SR = 44100.0; const int BS = 512;
    T5ynthStepSequencer seq; seq.prepare(SR, BS);
    seq.setNumSteps(numSteps); seq.setBpm(120.0); seq.setDivision(div);
    for (int i = 0; i < numSteps; ++i){
        seq.setStepNote(i, notes[i]); seq.setStepEnabled(i, true);
        seq.setStepVelocity(i, 0.8f); seq.setStepBindMode(i, Bind::Off);
    }
    seq.setAllGates(gate);   // the REAL path: gate is one global control
    seq.start();

    const double stepSamp = SR * 60.0 / 120.0 * T5ynthStepSequencer::DIVISION_FACTORS[div];
    const int totalBlocks = static_cast<int>(stepSamp * numSteps * passes / BS) + 4;
    juce::AudioBuffer<float> buf(2, BS); std::vector<VoiceEvent> evs;
    std::vector<Ev> out;
    for (int b = 0; b < totalBlocks; ++b){
        buf.clear(); evs.clear();
        seq.processBlock(buf, evs);
        for (auto& e : evs) out.push_back({ b, e.type, e.note, e.artic });
    }
    return out;
}

static int countOff(const std::vector<Ev>& evs){
    int n = 0; for (auto& e : evs) if (e.type == VoiceEvent::Type::NoteOff) ++n; return n;
}

// ── Part 1: deterministic assertions ────────────────────────────────────────
static bool testEvents()
{
    printf("=== Part 1: StepSequencer event-stream assertions ===\n");
    bool ok = true;

    // A) C3,C3 gate 100 -> TIE: zero note-offs across the loop; every note-on
    //    after the first carries Bind (continue voice, no retrigger).
    {
        int notes[2] = { 60, 60 };
        auto evs = runSeq(2, notes, 1.0f, /*passes=*/4);
        int offs = countOff(evs);
        int ons = 0, firstNormal = 0, restBind = 0, i = 0;
        for (auto& e : evs) if (e.type == VoiceEvent::Type::NoteOn){
            ++ons;
            if (i == 0) firstNormal = (e.artic == Artic::Normal);
            else        restBind   += (e.artic == Artic::Bind);
            ++i;
        }
        bool pass = offs == 0 && ons >= 4 && firstNormal && restBind == ons - 1;
        printf("  A) C3,C3 gate100  offs=%d ons=%d firstNormal=%d restBind=%d/%d  %s\n",
               offs, ons, firstNormal, restBind, ons - 1, pass ? "PASS" : "*** FAIL ***");
        ok &= pass;
    }

    // B) C3,C3 gate 50 -> still RETRIGGER: note-offs present, all note-ons Normal.
    {
        int notes[2] = { 60, 60 };
        auto evs = runSeq(2, notes, 0.5f, 4);
        int offs = countOff(evs);
        int ons = 0, normal = 0;
        for (auto& e : evs) if (e.type == VoiceEvent::Type::NoteOn){ ++ons; normal += (e.artic == Artic::Normal); }
        bool pass = offs > 0 && ons >= 4 && normal == ons;
        printf("  B) C3,C3 gate50   offs=%d ons=%d allNormal=%d  %s\n",
               offs, ons, normal == ons, pass ? "PASS" : "*** FAIL ***");
        ok &= pass;
    }

    // C) C3,D3 gate 100 -> DIFFERENT pitch, NOT a tie: note-offs present, note-ons Normal.
    {
        int notes[2] = { 60, 62 };
        auto evs = runSeq(2, notes, 1.0f, 4);
        int offs = countOff(evs);
        int ons = 0, normal = 0;
        for (auto& e : evs) if (e.type == VoiceEvent::Type::NoteOn){ ++ons; normal += (e.artic == Artic::Normal); }
        bool pass = offs > 0 && ons >= 4 && normal == ons;
        printf("  C) C3,D3 gate100  offs=%d ons=%d allNormal=%d  %s\n",
               offs, ons, normal == ons, pass ? "PASS" : "*** FAIL ***");
        ok &= pass;
    }

    // D) C3 x4 gate 100 -> continuous tie through a longer loop (no offs).
    {
        int notes[4] = { 60, 60, 60, 60 };
        auto evs = runSeq(4, notes, 1.0f, 3);
        int offs = countOff(evs);
        bool pass = offs == 0;
        printf("  D) C3x4 gate100   offs=%d  %s\n", offs, pass ? "PASS" : "*** FAIL ***");
        ok &= pass;
    }

    // E) C3,C3 gate 99 -> just below full: NOT a tie (there is a real gap), so
    //    retrigger is preserved. Guards the 0.999 threshold boundary.
    {
        int notes[2] = { 60, 60 };
        auto evs = runSeq(2, notes, 0.99f, 4);
        int offs = countOff(evs);
        int ons = 0, normal = 0;
        for (auto& e : evs) if (e.type == VoiceEvent::Type::NoteOn){ ++ons; normal += (e.artic == Artic::Normal); }
        bool pass = offs > 0 && normal == ons;
        printf("  E) C3,C3 gate99   offs=%d ons=%d allNormal=%d  %s\n",
               offs, ons, normal == ons, pass ? "PASS" : "*** FAIL ***");
        ok &= pass;
    }
    return ok;
}

// ── WAV writer (mono, 16-bit) ────────────────────────────────────────────────
static void writeWav(const std::string& path, const std::vector<float>& mono, int sr)
{
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v){ f.write(reinterpret_cast<char*>(&v), 4); };
    auto u16 = [&](uint16_t v){ f.write(reinterpret_cast<char*>(&v), 2); };
    uint32_t dataBytes = static_cast<uint32_t>(mono.size()) * 2u;
    f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(1); u32((uint32_t)sr); u32((uint32_t)sr * 2); u16(2); u16(16);
    f.write("data", 4); u32(dataBytes);
    for (float s : mono){
        int v = (int)std::lround(juce::jlimit(-1.0f, 1.0f, s) * 32767.0f);
        u16((uint16_t)(int16_t)v);
    }
}

// ── Part 2: audition render through the REAL VoiceManager + Sampler ──────────
// Metric is computed post-hoc on the OUTPUT (not the env level): the ADSR does a
// SOFT retrigger (attackStartLevel = currentLevel) so the env never dips — the
// audible "neue Attack" is the SAMPLER PLAYHEAD RESET replaying the sample's
// onset transient. So the carrier carries a bright onset transient (like a
// generated sample), and we count onset re-triggers by peak spikes in the
// loop's STEADY region (after the first onset).
struct RenderStats { int reonsets = 0; float steadyMaxPeak = 0.0f; };

static RenderStats renderScenario(const std::string& wavPath, bool useFix, int voiceLimit)
{
    const double SR = 44100.0; const int BS = 512;

    VoiceManager vm; vm.prepare(SR, BS);
    vm.setTuningTable(g_tuning);
    vm.setEngineMode(SynthVoice::EngineMode::Sampler);
    vm.setVoiceLimit(voiceLimit);

    BlockParams bp;                        // filter off, engine sampler by default
    bp.ampAttack = 2500.0f; bp.ampDecay = 1.0f; bp.ampSustain = 1.0f; bp.ampRelease = 3500.0f;
    bp.velAmt = 1.0f;
    vm.setBlockParams(bp);

    // Carrier: a bright ONSET transient (first ~45 ms: fast-decaying rich
    // harmonics) followed by a steady 130 Hz tone. On a playhead reset the onset
    // replays — that is the "neue Attack" a repeated gate-100 note produced.
    const int carrierLen = (int)(SR * 15.0);
    juce::AudioBuffer<float> carrier(1, carrierLen);
    for (int i = 0; i < carrierLen; ++i){
        const double t = i / SR;
        double body = std::sin(2.0 * M_PI * 130.81 * t);
        double onset = std::exp(-t / 0.012) *
            (std::sin(2.0 * M_PI * 523.0 * t) + 0.7 * std::sin(2.0 * M_PI * 1046.0 * t));
        carrier.setSample(0, i, (float)(0.6 * body + 0.9 * onset));
    }
    SamplePlayer master; master.prepare(SR, BS); master.loadBuffer(carrier, SR);
    vm.distributeSamplerBuffer(master, 0.0f, false);

    std::vector<float> lfo((size_t)BS, 0.0f);
    juce::AudioBuffer<float> buf(2, BS);
    std::vector<float> mono;

    auto renderMs = [&](double ms){
        int n = (int)(SR * ms / 1000.0);
        while (n > 0){
            int k = std::min(BS, n);
            buf.clear();
            vm.renderBlock(buf, bp, lfo.data(), lfo.data(), lfo.data(), 0, k);
            for (int i = 0; i < k; ++i) mono.push_back(buf.getSample(0, i));
            n -= k;
        }
    };

    // ── Phase 1: manual note — one onset, then long clean release (baseline). ──
    vm.noteOn(60, 0.9f, false, 0.0f, false, false, false, -1, 0.0f, 0);
    renderMs(3000);
    vm.noteOff(60, -1);
    renderMs(3500);

    const size_t p2Start = mono.size();

    // ── Phase 2: C3/C3 gate-100 loop (0.5 s steps, 8 steps = 4 s). ──
    if (useFix){
        // Drive the REAL, fixed sequencer.
        T5ynthStepSequencer seq; seq.prepare(SR, BS);
        seq.setNumSteps(2); seq.setBpm(120.0); seq.setDivision(2); // 1/4 -> 0.5 s/step
        for (int i = 0; i < 2; ++i){ seq.setStepNote(i, 60); seq.setStepEnabled(i, true);
            seq.setStepGate(i, 1.0f); seq.setStepVelocity(i, 0.8f); seq.setStepBindMode(i, Bind::Off); }
        seq.start();
        std::vector<VoiceEvent> evs;
        int blocks = (int)(SR * 4.0 / BS);
        for (int b = 0; b < blocks; ++b){
            buf.clear(); evs.clear();
            seq.processBlock(buf, evs);
            int pos = 0;
            for (auto& ev : evs){
                int off = juce::jlimit(0, BS, ev.sampleOffset);
                if (off > pos){ vm.renderBlock(buf, bp, lfo.data(), lfo.data(), lfo.data(), pos, off - pos); pos = off; }
                if (ev.type == VoiceEvent::Type::NoteOn){
                    bool isBind = ev.artic != Artic::Normal;
                    float gl = (ev.artic == Artic::Bind) ? 0.0f : seq.getGlideTime();
                    vm.noteOn(ev.note, ev.velocity, isBind, gl, false, false, false, ev.strandId, ev.pan, 0);
                } else vm.noteOff(ev.note, ev.strandId);
            }
            if (pos < BS) vm.renderBlock(buf, bp, lfo.data(), lfo.data(), lfo.data(), pos, BS - pos);
            for (int i = 0; i < BS; ++i) mono.push_back(buf.getSample(0, i));
        }
        seq.stop();
        { buf.clear(); std::vector<VoiceEvent> tail; seq.processBlock(buf, tail);
          for (auto& ev : tail) if (ev.type == VoiceEvent::Type::NoteOff) vm.noteOff(ev.note, ev.strandId); }
    } else {
        // Reproduce the OLD event stream by hand: fresh Normal note, then every
        // 0.5 s a NoteOff+NoteOn(Normal) at the same instant (gate-100 boundary).
        vm.noteOn(60, 0.8f, false, 0.0f, false, false, false, -1, 0.0f, 0);
        for (int step = 0; step < 8; ++step){
            renderMs(500.0);
            vm.noteOff(60, -1);
            vm.noteOn(60, 0.8f, false, 0.0f, false, false, false, -1, 0.0f, 0);
        }
    }
    const size_t p2End = mono.size();

    // ── Phase 3: stop -> release tail (should be a clean release, like Phase 1). ──
    if (!useFix) vm.noteOff(60, -1);
    renderMs(3500);

    writeWav(wavPath, mono, (int)SR);

    // ── Post-hoc metric on the OUTPUT: 10 ms peak envelope over the loop's
    //    STEADY region (skip the first 250 ms so the initial onset+attack ramp
    //    isn't counted). Each replayed onset shows as a peak well above the
    //    steady body level; count how many windows spike. ──
    RenderStats st;
    const int win = (int)(SR * 0.010);
    const size_t steadyStart = p2Start + (size_t)(SR * 0.25);
    std::vector<float> pk;
    for (size_t i = steadyStart; i + win < p2End; i += win){
        float m = 0.0f;
        for (int j = 0; j < win; ++j) m = std::max(m, std::fabs(mono[i + j]));
        pk.push_back(m);
        st.steadyMaxPeak = std::max(st.steadyMaxPeak, m);
    }
    // median steady level
    std::vector<float> sorted = pk; std::sort(sorted.begin(), sorted.end());
    float med = sorted.empty() ? 0.0f : sorted[sorted.size() / 2];
    // count rising crossings above 1.5x median (each onset replay = one spike)
    bool above = false;
    for (float v : pk){
        if (!above && v > med * 1.5f + 0.02f){ ++st.reonsets; above = true; }
        else if (above && v < med * 1.2f) above = false;
    }
    return st;
}

int main()
{
    for (int i = 0; i < 128; ++i) g_tuning[i] = 440.0f * std::pow(2.0f, (i - 69) / 12.0f);

    bool ok = testEvents();

    printf("\n=== Part 2: audition render (real VoiceManager + Sampler) ===\n");
    auto before = renderScenario("/tmp/gate_tie_BEFORE.wav", /*useFix=*/false, /*poly=*/16);
    auto after  = renderScenario("/tmp/gate_tie_AFTER.wav",  /*useFix=*/true,  16);
    auto afterM = renderScenario("/tmp/gate_tie_AFTER_mono.wav", true, 1);
    printf("  BEFORE loop: re-onsets in steady region = %d  (each = a re-attack)\n", before.reonsets);
    printf("  AFTER  loop: re-onsets in steady region = %d  (poly — continuous tie)\n", after.reonsets);
    printf("  AFTER  loop: re-onsets in steady region = %d  (mono — continuous tie)\n", afterM.reonsets);
    printf("  WAVs: /tmp/gate_tie_BEFORE.wav  /tmp/gate_tie_AFTER.wav  /tmp/gate_tie_AFTER_mono.wav\n");

    // The fix must play ONE continuous note through the loop: no onset replays in
    // the steady region. BEFORE re-onsets every 0.5 s step (~7); AFTER = 0.
    bool cont = after.reonsets == 0 && afterM.reonsets == 0;
    bool beforeBroken = before.reonsets >= 5;   // sanity: the harness reproduces the bug
    printf("\n%s\n", (ok && cont && beforeBroken) ? "ALL PASS" : "*** FAILURES ABOVE ***");
    return (ok && cont && beforeBroken) ? 0 : 1;
}
