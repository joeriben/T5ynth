// Guard tool for the Phase-1 Csound engine voice bridge
// (docs: SPEC_phase1_csound_engine.md §3 voice bridge + §5 guard tool).
//
// Drives VoiceManager + CsoundEngine directly -- the same layer as the
// existing audition tools (audition_dco_presence_follow.cpp,
// measure_voiceleading.cpp et al) -- never through the plugin's processBlock.
// The block-splitting/pump loop below (CsoundRig::renderBlock) is a literal
// mirror of PluginProcessor.cpp's MIDI-split loop: startBlock() once per host
// block, then per event-bounded sub-segment: writeCsoundControls ->
// renderUpTo -> voiceManager.renderBlock(..., &cs), with the same
// csoundLastWritePos_ negative-carry accounting across block boundaries.
//
// Five cases (spec §5), each a hard assert -- if any fails, the CODE under
// test is wrong, not the assertion (unless the assertion itself turns out to
// contradict the spec, which would be called out separately):
//   1. NOTE-ON LATENCY   -- post-VCA onset lands within ~1 ksmps + portk lag.
//   2. RELEASE INTEGRITY -- D3: gate = voice ACTIVE, not note-held; the T5ynth
//                           ampEnv release plays out in full after note-off.
//   3. RETRIGGER         -- D8: a same-slot reused voice re-strikes via the
//                           trig-epoch channel (changed2), not a gate edge
//                           (which never falls across old->new note here).
//   4. RT CLEANLINESS    -- D4: zero additional heap blocks across a 10 s
//                           steady-state pump with note churn, post-warmup.
//   5. TIMING            -- raw CsoundEngine: a gate/trig write followed by
//                           renderUpTo produces a non-zero sample within one
//                           ksmps block (64 samples).
//
// Renders WAVs (-12 dBFS) into tools/csound_engine_out/ for BJ's ear.
//
// Build (T5ynth's standard offline-tool recipe -- see
// audition_dco_presence_follow.cpp's header for the base pattern -- plus the
// Csound framework link/rpath from tools/csound_poc.cpp / CMakeLists.txt's
// T5YNTH_CSOUND_FOUND block). This tool only #includes dsp/CsoundEngine.h
// (never <csound/csound.h> directly -- that header is pimpl'd specifically so
// callers never need the framework's own headers), so no extra -I is needed
// for csound.h; the framework is only needed at LINK time for the symbols
// CsoundEngine.cpp (already compiled into libT5ynth_SharedCode.a) calls.
//
// IMPORTANT: verify the scraped CXX_DEFINES actually contains
// -DT5YNTH_HAS_CSOUND=1 before compiling -- if the framework weren't found at
// CMake configure time this would silently compile the INERT STUB inline in
// THIS translation unit while linking the REAL CsoundEngine.o from the
// static lib: an ODR violation (stub vs real Impl/class layout) that a
// linker will not necessarily flag as an error.
//
//   FLAGS=build_clean/CMakeFiles/T5ynth.dir/flags.make
//   { grep -m1 CXX_DEFINES "$FLAGS"; grep -m1 CXX_INCLUDES "$FLAGS"; } \
//     | sed 's/^CXX_[A-Z]* = //' > /tmp/h.rsp
//   grep -o 'DT5YNTH_HAS_CSOUND=.' /tmp/h.rsp   # must print exactly "=1"
//   echo -I$PWD/build_clean/_deps/signalsmith_stretch-src >> /tmp/h.rsp
//   CSOUND_PREFIX=$(brew --prefix csound)
//   CSOUND_FW="$CSOUND_PREFIX/Frameworks"
//   mkdir -p tools/csound_engine_out
//   clang++ -std=c++17 -O2 @/tmp/h.rsp \
//     tools/audition_csound_engine.cpp \
//     build_clean/T5ynth_artefacts/Release/libT5ynth_SharedCode.a \
//     -F"$CSOUND_FW" -framework CsoundLib64 -Wl,-rpath,"$CSOUND_FW" \
//     -framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio \
//     -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation \
//     -framework IOKit -framework QuartzCore -framework Security -framework WebKit \
//     -weak_framework Metal -weak_framework MetalKit \
//     -o tools/csound_engine_out/audition_csound_engine
//   tools/csound_engine_out/audition_csound_engine

#include "JuceHeader.h"
#include "dsp/VoiceManager.h"
#include "dsp/CsoundEngine.h"
#include "dsp/BlockParams.h"

#include <malloc/malloc.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

// ═══════════════════════════ shared helpers ═══════════════════════════

// Convention copied from tools/audition_dco_presence_follow.cpp / csound_poc.cpp
// (mono, 16-bit PCM, clamped to [-1,1]).
static void writeWav(const std::string& path, const std::vector<float>& mono, int sr)
{
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<char*>(&v), 2); };
    uint32_t dataBytes = static_cast<uint32_t>(mono.size()) * 2u;
    f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(1); u32(sr); u32(sr * 2); u16(2); u16(16);
    f.write("data", 4); u32(dataBytes);
    for (float s : mono)
    {
        int v = static_cast<int>(std::lround(std::max(-1.f, std::min(1.f, s)) * 32767.f));
        u16(static_cast<uint16_t>(static_cast<int16_t>(v)));
    }
}

static float peakOf(const std::vector<float>& buf)
{
    float pk = 0.0f;
    for (float s : buf) pk = std::max(pk, std::fabs(s));
    return pk;
}

// Peak-normalize to targetDbfs and write (a silent buffer writes flat zero).
static void writeWavNormalized(const std::string& path, std::vector<float> buf, int sr, float targetDbfs)
{
    const float pk = peakOf(buf);
    if (pk > 1.0e-9f)
    {
        const float targetLin = std::pow(10.0f, targetDbfs / 20.0f);
        const float gain = targetLin / pk;
        for (float& s : buf) s *= gain;
    }
    writeWav(path, buf, sr);
}

static double rmsWindow(const std::vector<float>& buf, int start, int len)
{
    start = std::max(0, start);
    const int end = std::min((int) buf.size(), start + len);
    if (end <= start) return 0.0;
    double sumSq = 0.0;
    for (int i = start; i < end; ++i) sumSq += (double) buf[(size_t) i] * (double) buf[(size_t) i];
    return std::sqrt(sumSq / (double) (end - start));
}

static malloc_statistics_t snapshotMalloc()
{
    malloc_statistics_t st;
    std::memset(&st, 0, sizeof(st));
    malloc_zone_statistics(malloc_default_zone(), &st);
    return st;
}

// ═══════════════════════════ test rig ═══════════════════════════
//
// Mirrors PluginProcessor.cpp's Csound pump wiring exactly (spec §3):
//   - startBlock(numSamples) once per host block (LFO-fill-site equivalent).
//   - per event-bounded sub-segment: writeCsoundControls -> renderUpTo ->
//     voiceManager.renderBlock(..., &cs) -- same order, same
//     samplesSinceLastWrite = renderPos - csoundLastWritePos_ accounting,
//     same negative carry across block boundaries (csoundLastWritePos_ -=
//     numSamples at the end of every block).
struct ScheduledEvent
{
    int sampleOffset = 0;   // relative to the rig's current timeline origin (see resetTimeline)
    std::function<void()> action;
};

struct CsoundRig
{
    static constexpr double SR = 48000.0;
    // Host block size. Deliberately NOT a multiple of kKsmps(64) -- 512 would
    // make every renderUpTo() land exactly on a ksmps boundary (leftover==0
    // in CsoundEngine::renderUpTo every time), leaving the carry FIFO
    // (CsoundEngine.cpp's carryBuf/carryCount, replayed by startBlock at the
    // next block) completely unexercised by every case below (adversarial-
    // review finding). 480 (a common real-world block size, 48000/100) forces
    // a 32-sample carry across every single block boundary in all five cases.
    static constexpr int BS = 480;   // host block size

    VoiceManager vm;
    CsoundEngine cs;
    BlockParams bp;
    juce::AudioBuffer<float> buf { 2, BS };
    std::vector<float> lfoZero;
    float tuning[128] {};

    int csoundLastWritePos_ = 0;
    int absoluteBlockStart_ = 0;   // this rig's running sample clock since the last resetTimeline()
    size_t evtCursor_ = 0;         // index into the events vector currently being scanned

    CsoundRig(float ampAttackMs, float ampDecayMs, float ampSustain, float ampReleaseMs, int voiceLimit)
        : lfoZero((size_t) BS, 0.0f)
    {
        for (int i = 0; i < 128; ++i)
            tuning[i] = (float) juce::MidiMessage::getMidiNoteInHertz(i);

        vm.prepare(SR, BS);
        vm.setTuningTable(tuning);
        vm.setEngineMode(SynthVoice::EngineMode::Csound);
        vm.setVoiceLimit(voiceLimit);

        bp.ampAttack = ampAttackMs;
        bp.ampDecay = ampDecayMs;
        bp.ampSustain = ampSustain;
        bp.ampRelease = ampReleaseMs;
        bp.velAmt = 1.0f;
        vm.setBlockParams(bp);

        if (! cs.prepare(SR, BS))
        {
            std::fprintf(stderr,
                "FATAL: CsoundEngine::prepare() failed -- framework missing at runtime, "
                "compile error, or this binary was built against the T5YNTH_HAS_CSOUND=0 "
                "stub (see this file's build-recipe warning). Aborting.\n");
            std::exit(1);
        }
    }

    void noteOn(int note, float velocity = 1.0f, float glideMs = 0.0f)
    {
        vm.noteOn(note, velocity, /*isBind=*/false, glideMs,
                  /*lfo1TrigMode=*/false, /*lfo2TrigMode=*/false, /*lfo3TrigMode=*/false,
                  /*sourceId=*/-1, /*pan=*/0.0f, /*mpeChannel=*/0);
    }

    void noteOff(int note) { vm.noteOff(note, -1); }

    // Resets this rig's local timeline bookkeeping to 0 -- call before
    // scheduling a fresh batch of events whose sampleOffsets are meant to be
    // relative to "now" rather than to the rig's construction-time clock.
    // Voice/engine STATE (active notes, CsoundEngine's internal ksmps carry)
    // is untouched -- this only resets how THIS HARNESS interprets event
    // offsets and the freq-glide "samples since last write" accounting.
    void resetTimeline()
    {
        absoluteBlockStart_ = 0;
        csoundLastWritePos_ = 0;
        evtCursor_ = 0;
    }

    // `events` must be sorted by sampleOffset and expressed relative to
    // whatever absoluteBlockStart_ was at the last resetTimeline(). evtCursor_
    // persists across calls so repeated calls with the SAME events vector
    // (the normal case: one resetTimeline() then many renderBlock() calls)
    // never re-scan or re-fire an already-consumed event. buf is fixed at BS
    // (set at construction) and never resized here, so repeated calls at
    // numSamples==BS cannot allocate.
    void renderBlock(int numSamples, const std::vector<ScheduledEvent>& events, std::vector<float>* outMono)
    {
        buf.clear();
        cs.startBlock(numSamples);

        int renderPos = 0;
        while (renderPos < numSamples)
        {
            int subEnd = numSamples;
            if (evtCursor_ < events.size())
            {
                const int rel = events[evtCursor_].sampleOffset - absoluteBlockStart_;
                subEnd = juce::jlimit(renderPos, numSamples, rel);
            }

            const int subLen = subEnd - renderPos;
            if (subLen > 0)
            {
                // Csound pump (spec §3/D2) -- identical order/accounting to
                // PluginProcessor.cpp's MIDI-split loop.
                vm.writeCsoundControls(cs, bp.performancePitchRatio, renderPos - csoundLastWritePos_);
                cs.renderUpTo(subEnd - 1);
                csoundLastWritePos_ = renderPos;
                vm.renderBlock(buf, bp, lfoZero.data() + renderPos, lfoZero.data() + renderPos,
                               lfoZero.data() + renderPos, renderPos, subLen, &cs);
            }

            while (evtCursor_ < events.size()
                   && events[evtCursor_].sampleOffset - absoluteBlockStart_ <= subEnd)
            {
                events[evtCursor_].action();
                ++evtCursor_;
            }
            renderPos = subEnd;
        }
        csoundLastWritePos_ -= numSamples;
        absoluteBlockStart_ += numSamples;

        if (outMono != nullptr)
            for (int i = 0; i < numSamples; ++i)
                outMono->push_back(0.5f * (buf.getSample(0, i) + buf.getSample(1, i)));
    }

    // Self-contained convenience wrapper for cases 1-3: resets the timeline,
    // renders totalSamples (rounded up to a whole number of BS blocks, then
    // truncated back to exactly totalSamples), dispatching `events` (offsets
    // relative to sample 0 of this call).
    std::vector<float> renderRange(int totalSamples, std::vector<ScheduledEvent> events)
    {
        std::sort(events.begin(), events.end(),
                  [](const ScheduledEvent& a, const ScheduledEvent& b) { return a.sampleOffset < b.sampleOffset; });
        resetTimeline();
        std::vector<float> out;
        out.reserve((size_t) totalSamples + (size_t) BS);
        int done = 0;
        while (done < totalSamples)
        {
            renderBlock(BS, events, &out);
            done += BS;
        }
        out.resize((size_t) totalSamples);
        return out;
    }
};

// ═══════════════════════════ Case 1: NOTE-ON LATENCY ═══════════════════════════
static bool caseNoteOnLatency()
{
    printf("[1] NOTE-ON LATENCY\n");
    // Near-instant VCA (attack ~0) isolates the Csound orchestra's own onset
    // (gate portk ~8ms + ksmps=64 quantization) as the dominant visible delay.
    CsoundRig rig(0.05f, 5.0f, 1.0f, 2000.0f, CsoundEngine::kMaxVoices);

    const int noteOnAt = 1000;   // mid-block: lands inside host block 1, not block-aligned, not
                                  // ksmps(64)-aligned either
    // Bug found on review: this used to render only `8 * CsoundRig::BS` samples
    // while measuring a steady-state window at [noteOnAt+3000, noteOnAt+4000)
    // = [4000, 5000) -- with the old BS=512 that's only 4096 samples (a
    // partially/fully out-of-bounds std::vector<float> read past `out`'s
    // resize()d size, silently "passing" only by accident via whatever
    // memory happened to sit past the vector's logical end). Render an
    // explicit, comfortably-sufficient length instead of tying it to BS.
    const int totalSamples = noteOnAt + 4500;   // covers the +4000 window end with margin
    auto out = rig.renderRange(totalSamples,
        { { noteOnAt, [&] { rig.noteOn(60); } } });

    // Pre-onset must be silent (sanity: the event actually gates the timeline).
    float preOnsetPeak = 0.0f;
    for (int i = 0; i < noteOnAt; ++i) preOnsetPeak = std::max(preOnsetPeak, std::fabs(out[(size_t) i]));

    // Reference steady-state peak, well after onset settles.
    float steadyPeak = 0.0f;
    for (int i = noteOnAt + 3000; i < noteOnAt + 4000; ++i)
        steadyPeak = std::max(steadyPeak, std::fabs(out[(size_t) i]));

    const float thresh = 0.05f * steadyPeak;
    int onsetIdx = -1;
    for (int i = noteOnAt; i < (int) out.size(); ++i)
        if (std::fabs(out[(size_t) i]) > thresh) { onsetIdx = i; break; }

    const int delaySamples = (onsetIdx >= 0) ? (onsetIdx - noteOnAt) : -1;
    // spec: "< 64 + ~400 samples" (one ksmps block + the ~8ms portk lag,
    // 0.008*48000=384) -- allow a bit of slack (600) without loosening the
    // bound into meaninglessness (a whole extra host block would be ~1112).
    const bool ok = preOnsetPeak < 1.0e-6f && onsetIdx >= 0 && delaySamples >= 0 && delaySamples < 600;

    printf("    preOnsetPeak=%.2e steadyPeak=%.4f onsetDelay=%d samples (%.2f ms) -> %s\n",
           preOnsetPeak, steadyPeak, delaySamples, delaySamples * 1000.0 / CsoundRig::SR,
           ok ? "PASS" : "*** FAIL ***");

    writeWavNormalized("tools/csound_engine_out/case1_noteon_latency.wav", out, (int) CsoundRig::SR, -12.0f);
    return ok;
}

// ═══════════════════════════ Case 2: RELEASE INTEGRITY ═══════════════════════════
static bool caseReleaseIntegrity()
{
    printf("[2] RELEASE INTEGRITY (D3: gate = active, not note-held)\n");
    const float releaseMs = 500.0f;
    CsoundRig rig(2.0f, 20.0f, 1.0f, releaseMs, CsoundEngine::kMaxVoices);

    const int noteOnAt = 0;
    const int noteOffAt = (int) (0.30 * CsoundRig::SR);          // hold 300ms, well into sustain
    const int totalSamples = (int) (0.80 * CsoundRig::SR);        // off + 500ms margin

    auto out = rig.renderRange(totalSamples,
        { { noteOnAt,  [&] { rig.noteOn(60); } },
          { noteOffAt, [&] { rig.noteOff(60); } } });

    const int preOffWindow = (int) (0.02 * CsoundRig::SR);   // 20ms window just before note-off
    double peakBeforeOff = 0.0;
    for (int i = noteOffAt - preOffWindow; i < noteOffAt; ++i)
        peakBeforeOff = std::max(peakBeforeOff, (double) std::fabs(out[(size_t) i]));

    const int measureAt = noteOffAt + (int) (0.25 * CsoundRig::SR);   // 250ms after note-off
    const double rmsAtMeasure = rmsWindow(out, measureAt - 200, 400);
    const double rmsBeforeOff  = rmsWindow(out, noteOffAt - 400, 400);

    const double relDb = 20.0 * std::log10(std::max(1.0e-12, rmsAtMeasure / std::max(1.0e-12, peakBeforeOff)));
    const bool ok = relDb > -40.0 && rmsBeforeOff > 1.0e-4;   // proves a real sustained tone existed pre-off

    printf("    peakBeforeOff=%.4f rmsBeforeOff=%.4f rmsAt+250ms=%.4f (%.1f dB rel.) -> %s\n",
           peakBeforeOff, rmsBeforeOff, rmsAtMeasure, relDb, ok ? "PASS" : "*** FAIL ***");

    writeWavNormalized("tools/csound_engine_out/case2_release_integrity.wav", out, (int) CsoundRig::SR, -12.0f);
    return ok;
}

// ═══════════════════════════ Case 3: RETRIGGER ═══════════════════════════
// D8: a stolen/reused voice stays isActive() across old->new note (gate never
// falls, D3), so the retrigger signal must be the trig-epoch channel
// (changed2), not a gate edge. Empirical, comparative design (measure, don't
// theorize about the exact orchestra decay-curve shape): run the SAME mono
// voice slot twice -- once with only the first note, once with a
// note-off + second note-on 50ms later, while the voice is still
// active/releasing (never idle) the whole time -- and compare energy shortly
// after the second trigger's expected onset. A real re-strike must leave
// MORE energy there than the single-note control, which only keeps decaying.
static bool caseRetrigger()
{
    printf("[3] RETRIGGER (D8: trig-epoch, not gate edge)\n");
    const float releaseMs = 500.0f;   // long: voice 0 stays active/releasing across both notes
    const int totalSamples = (int) (0.10 * CsoundRig::SR);   // 100ms

    CsoundRig rigSingle(2.0f, 20.0f, 1.0f, releaseMs, /*voiceLimit=*/1);
    auto outSingle = rigSingle.renderRange(totalSamples, { { 0, [&] { rigSingle.noteOn(60); } } });

    CsoundRig rigRetrig(2.0f, 20.0f, 1.0f, releaseMs, /*voiceLimit=*/1);
    const int noteOffAt = (int) (0.005 * CsoundRig::SR);    // 5ms
    const int secondOnAt = (int) (0.050 * CsoundRig::SR);   // 50ms -- voice 0 still active (releasing)
    auto outRetrig = rigRetrig.renderRange(totalSamples,
        { { 0,          [&] { rigRetrig.noteOn(60); } },
          { noteOffAt,  [&] { rigRetrig.noteOff(60); } },
          { secondOnAt, [&] { rigRetrig.noteOn(64); } } });   // same voice (mono), different pitch

    // Sanity: both runs must agree closely BEFORE the retrig run's note-off
    // (confirms the two rigs are configured identically -- same note, same
    // envelope, same freq -- before anything is deliberately made to
    // diverge). Deliberately NOT compared anywhere between noteOffAt and
    // secondOnAt: the retrig run is releasing there while the single-note
    // control is still sustaining, which is the whole point of the test, not
    // a discrepancy to guard against.
    const int preWindowEnd = noteOffAt - 20;   // safely before the note-off event
    const double rmsPreDivergeSingle = rmsWindow(outSingle, preWindowEnd - 100, 100);
    const double rmsPreDivergeRetrig = rmsWindow(outRetrig, preWindowEnd - 100, 100);
    const bool preMatch = std::fabs(rmsPreDivergeSingle - rmsPreDivergeRetrig)
                        < 0.25 * std::max(rmsPreDivergeSingle, rmsPreDivergeRetrig) + 1.0e-6;

    // Measurement point: 15ms after the second trigger, comfortably past one
    // ksmps block + the gate/strike onset lag.
    const int measureAt = secondOnAt + (int) (0.015 * CsoundRig::SR);
    const double rmsRetrigAfter = rmsWindow(outRetrig, measureAt - 150, 300);
    const double rmsSingleAfter = rmsWindow(outSingle, measureAt - 150, 300);

    const bool retriggered = rmsRetrigAfter > rmsSingleAfter * 1.2 + 1.0e-6;
    const bool ok = preMatch && retriggered;

    printf("    preDiverge: single=%.4f retrig=%.4f (match=%s)\n",
           rmsPreDivergeSingle, rmsPreDivergeRetrig, preMatch ? "yes" : "NO");
    printf("    +15ms post-2nd-trigger: single(control)=%.4f retrig=%.4f -> %s\n",
           rmsSingleAfter, rmsRetrigAfter, ok ? "PASS" : "*** FAIL ***");

    writeWavNormalized("tools/csound_engine_out/case3_retrigger_single.wav", outSingle, (int) CsoundRig::SR, -12.0f);
    writeWavNormalized("tools/csound_engine_out/case3_retrigger_retrig.wav", outRetrig, (int) CsoundRig::SR, -12.0f);
    return ok;
}

// ═══════════════════════════ Case 4: RT CLEANLINESS ═══════════════════════════
// D4: post-warmup, a steady-state pump with note churn must not move the heap
// (blocks_in_use) at all. Everything the HARNESS itself touches in the
// measured region is pre-built/reserved before the "before" snapshot, so a
// failure here can only be the DSP code under test (VoiceManager/SynthVoice/
// CsoundEngine), not this tool.
static bool caseRtCleanliness()
{
    printf("[4] RT CLEANLINESS (D4: zero malloc delta across a 10s churn pump)\n");
    CsoundRig rig(3.0f, 30.0f, 0.8f, 150.0f, CsoundEngine::kMaxVoices);

    // Warm-up phase 1: gate a handful of voices on, so any of THIS harness's
    // or VoiceManager/SynthVoice's own one-time lazy allocations (separate
    // from CsoundEngine's own D4 warmup, already done inside cs.prepare())
    // happen before the snapshot, not during the measured window.
    {
        std::vector<ScheduledEvent> warmupOn;
        warmupOn.reserve(4);
        for (int i = 0; i < 4; ++i)
            warmupOn.push_back({ i * 2000, [&rig, i] { rig.noteOn(60 + i); } });
        rig.resetTimeline();
        const int total = 3 * CsoundRig::BS + 8192;
        for (int done = 0; done < total; done += CsoundRig::BS)
            rig.renderBlock(CsoundRig::BS, warmupOn, nullptr);
    }
    // Warm-up phase 2: release them.
    {
        std::vector<ScheduledEvent> warmupOff;
        warmupOff.reserve(4);
        for (int i = 0; i < 4; ++i)
            warmupOff.push_back({ i * 100, [&rig, i] { rig.noteOff(60 + i); } });
        rig.resetTimeline();
        const int total = 2 * CsoundRig::BS + 8192;
        for (int done = 0; done < total; done += CsoundRig::BS)
            rig.renderBlock(CsoundRig::BS, warmupOff, nullptr);
    }

    // Steady-state churn schedule: a new note every ~100ms, rotating pitch
    // (VoiceManager's free-voice-first allocation rotates the voice slot
    // too), for 10s. Pre-built entirely before the "before" snapshot -- no
    // allocation happens inside the measured loop below.
    static int churnNote = 60;   // function-local static: no capture needed, single main() run
    const int churnPeriod = (int) (0.10 * CsoundRig::SR);   // 100ms
    const int totalSamples = 10 * (int) CsoundRig::SR;
    const int numChurnEvents = totalSamples / churnPeriod + 2;
    std::vector<ScheduledEvent> churnEvents;
    churnEvents.reserve((size_t) numChurnEvents);
    for (int i = 0; i < numChurnEvents; ++i)
    {
        churnEvents.push_back({ i * churnPeriod, [&rig]
        {
            rig.noteOff(churnNote);
            churnNote = 55 + (churnNote - 55 + 3) % 12;
            rig.noteOn(churnNote);
        } });
    }
    rig.resetTimeline();

    const malloc_statistics_t before = snapshotMalloc();

    for (int done = 0; done < totalSamples; done += CsoundRig::BS)
        rig.renderBlock(CsoundRig::BS, churnEvents, nullptr);

    const malloc_statistics_t after = snapshotMalloc();

    const long blocksDelta = (long) after.blocks_in_use - (long) before.blocks_in_use;
    const bool ok = blocksDelta == 0;

    printf("    blocks_in_use before=%u after=%u delta=%ld -> %s\n",
           before.blocks_in_use, after.blocks_in_use, blocksDelta, ok ? "PASS" : "*** FAIL ***");
    return ok;
}

// ═══════════════════════════ Case 5: TIMING (raw CsoundEngine) ═══════════════════════════
static bool caseTiming()
{
    printf("[5] TIMING (raw CsoundEngine: gate/trig write -> renderUpTo)\n");
    CsoundEngine cs;
    if (! cs.prepare(CsoundRig::SR, CsoundRig::BS))
    {
        printf("    cs.prepare() failed -> *** FAIL ***\n");
        return false;
    }

    CsoundEngine::VoiceControls c { /*gate=*/1.0f, /*freqHz=*/440.0f, /*velocity=*/1.0f,
                                    /*pressure=*/0.0f, /*timbre=*/0.5f, /*trigEpoch=*/1.0f };
    cs.setVoiceControls(0, c);
    cs.startBlock(CsoundEngine::kKsmps);
    cs.renderUpTo(CsoundEngine::kKsmps - 1);

    const float* v0 = cs.voiceBuffer(0);
    int firstNonZero = -1;
    const float eps = 1.0e-4f;
    if (v0 != nullptr)
        for (int i = 0; i < CsoundEngine::kKsmps; ++i)
            if (std::fabs(v0[i]) > eps) { firstNonZero = i; break; }

    const bool ok = firstNonZero >= 0 && firstNonZero < CsoundEngine::kKsmps;
    printf("    firstNonZero=%d (ksmps=%d) -> %s\n", firstNonZero, CsoundEngine::kKsmps,
           ok ? "PASS" : "*** FAIL ***");
    return ok;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // safe init for any JUCE statics

    printf("Phase-1 Csound engine voice bridge -- guard tool (spec sections 3+5)\n");
    printf("SR=%.0f BS=%d kMaxVoices=%d kKsmps=%d\n\n",
           CsoundRig::SR, CsoundRig::BS, CsoundEngine::kMaxVoices, CsoundEngine::kKsmps);

    bool ok = true;
    ok &= caseNoteOnLatency();
    ok &= caseReleaseIntegrity();
    ok &= caseRetrigger();
    ok &= caseRtCleanliness();
    ok &= caseTiming();

    printf("\nWAVs: tools/csound_engine_out/case{1,2,3}_*.wav\n");
    printf("%s\n", ok ? "ALL PASS" : "*** FAIL ***");
    return ok ? 0 : 1;
}
