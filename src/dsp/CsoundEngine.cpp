// CsoundEngine.cpp — Phase-1 hard-wired Csound engine (real implementation).
//
// Compiled ONLY when CMake found CsoundLib64 (target_sources gated in
// CMakeLists.txt on T5YNTH_CSOUND_FOUND, which also drives the always-defined
// T5YNTH_HAS_CSOUND=1 in that configuration). The #if below is a defensive
// belt-and-braces guard: this TU should never be compiled with the macro at
// 0 (that would be an ODR/ABI mismatch against CsoundEngine.h's inline stub),
// but if it somehow is, this file compiles to an empty translation unit
// rather than trying to #include a header that isn't on the include path.
#include "CsoundEngine.h"

#if T5YNTH_HAS_CSOUND

#include <csound/csound.h>

// csound's sysdep.h (pulled in transitively by csound.h) leaks several short
// macro names into the rest of this translation unit (LIKELY/UNLIKELY/
// ATOMIC_*/DIRSEP/ENVSEP/CS_PURE) that risk colliding with identifiers used
// elsewhere in the codebase. This TU only calls Csound's documented public C
// API, never these internal macros, so clearing them here is safe and
// confined entirely to this file (copied verbatim from
// tools/csound_poc.cpp:94-102 — proven ground truth on this machine).
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
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    // Hard-wired 12-partial bell/pad SPECTRUM (amp, ratio), adapted from the
    // BJ-approved tools/csound_poc_out/csound_strike_pad.csd. STANDING TONE
    // (BJ 2026-07-17, "Hüllkurven gehören nicht in den Oszillator. Vollständig
    // entfernen."): every partial holds at its authored amp for as long as the
    // gate is open — no strike, no per-partial decay. Amplitude SHAPE is the
    // downstream synth ADSR/VCA's job, never the oscillator's.
    //
    // NO VIBRATO (BJ 2026-07-17, "vibrato wird — selbstverständlich — aus dem
    // Vokabular genommen"). The per-partial vibHz/vibCents columns are gone.
    // The order named this file and this symbol; I kept them anyway on the
    // reasoning that "periodic motion stays", which was wrong twice over:
    // vibrato is PITCH modulation, i.e. expression, and expression belongs to
    // the synth (LFO -> Pitch), not to the oscillator — the same boundary that
    // took the envelopes out. And a fixed 0.07-0.5 Hz wobble welded into the
    // fallback tone is exactly the "always the same, slightly seasick" character
    // a player hears as unmusical, on every note the engine falls back to.
    struct Partial { double amp, ratio; };
    constexpr int kNumPartials = 12;
    constexpr Partial kPartials[kNumPartials] = {
        { 0.28, 0.56  },
        { 0.30, 1.00  },
        { 0.22, 1.19  },
        { 0.20, 1.71  },
        { 0.16, 2.00  },
        { 0.16, 2.007 },
        { 0.14, 2.74  },
        { 0.11, 3.76  },
        { 0.09, 4.07  },
        { 0.07, 5.43  },
        { 0.05, 6.98  },
        { 0.04, 8.21  },
    };

    // Builds the hard-wired orchestra: ONE numeric `instr 1`, 16 always-on
    // score instances (N=1..16=p4=voice index), 6 named channels per voice
    // (gate/freq/vel/pres/timb/trig). STANDING TONE: the trig channel is still
    // read (the 16x6 channel contract with the voice bridge is fixed) but
    // otherwise unused — every partial simply holds while the gate is open, so
    // there is no retrigger/reinit epoch. sr is baked in via snprintf at the
    // actual prepared sample rate.
    std::string buildOrchestra (double sampleRate)
    {
        std::string csd;
        char line[256];

        csd += "<CsoundSynthesizer>\n<CsOptions>\n-n -d\n</CsOptions>\n<CsInstruments>\n";
        std::snprintf(line, sizeof(line), "sr = %.0f\n", sampleRate);
        csd += line;
        std::snprintf(line, sizeof(line), "ksmps = %d\n", CsoundEngine::kKsmps);
        csd += line;
        std::snprintf(line, sizeof(line), "nchnls = %d\n", CsoundEngine::kMaxVoices);
        csd += line;
        csd += "0dbfs = 1\n\n";

        csd +=
            "; Hard-wired STANDING-TONE orchestra. gate = voice ACTIVE (incl. release),\n"
            "; not note-held (D3) -- the T5ynth ampEnv/VCA shapes attack/release\n"
            "; downstream; closing this gate on note-off would abort the release tail,\n"
            "; so it never does. Every partial holds at its authored amplitude while the\n"
            "; gate is open (no strike, no decay -- amplitude SHAPE is the synth ADSR's\n"
            "; job). The trig channel is read for the fixed 16x6 contract but is unused.\n"
            "; pres/timb are read end-to-end and mapped lightly here.\n"
            "instr 1\n"
            "  ivoice   = p4\n"
            "  Sgate    sprintf \"gate%d\", ivoice\n"
            "  Sfreq    sprintf \"freq%d\", ivoice\n"
            "  Svel     sprintf \"vel%d\", ivoice\n"
            "  Spres    sprintf \"pres%d\", ivoice\n"
            "  Stimb    sprintf \"timb%d\", ivoice\n"
            "  Strig    sprintf \"trig%d\", ivoice\n"
            "\n"
            "  kgateraw chnget Sgate\n"
            "  kfreqraw chnget Sfreq\n"
            "  kvel     chnget Svel\n"
            "  kpres    chnget Spres\n"
            "  ktimb    chnget Stimb\n"
            "  ktrigch  chnget Strig\n"
            "\n"
            // Gate: 1 ms HALF-time = pure declick for the raw engine (guard
            // tools drive it without the voice ADSR); in the plugin the DCA
            // envelope shapes amplitude outside, so this must never act as an
            // attack. 0.008 half-time audibly swallowed strike transients
            // (BJ 2026-07-17: "kein Anschlag, ein Einschwingen") — portk's
            // second arg is HALF-time, full settle ~7-10x that value.
            // Freq: NO smoothing — a new note snaps (phase-continuous in the
            // oscillators, so no click); 0.008 here was an unordered ~50 ms
            // exponential portamento (BJ: "deutliches Portamento — entfernen").
            // The voice-level SmoothedValue already smooths the actual glide
            // feature at block rate.
            "  kgate    portk kgateraw, 0.001      ; declick only, NOT an attack\n"
            "  kfreq    limit kfreqraw, 20, 12000\n"
            "\n"
            "  ; ktrigch is read above for the fixed 16x6 channel contract but is\n"
            "  ; otherwise unused: every partial below is a STANDING tone for as long\n"
            "  ; as the gate holds, so there is no retrigger/reinit epoch.\n\n";

        csd +=
            "\n"
            "  ; Phase-1 placeholder pres/timb mapping (channels exist end-to-end;\n"
            "  ; exact musical mapping is Phase 3): timbre cross-fades in the upper 6\n"
            "  ; partials' weight, pressure lifts overall presence a little.\n"
            "  ktimbHi   = 0.4 + 1.2*ktimb\n"
            "  kpresGain = 1.0 + 0.15*kpres\n\n";

        for (int i = 0; i < kNumPartials; ++i)
        {
            std::snprintf(line, sizeof(line),
                "  kfreq%-2d  = kfreq * %.4f\n", i + 1, kPartials[i].ratio);
            csd += line;

            // STANDING amplitude: the partial holds at its authored amp (no
            // strike term, no bed). Upper 6 partials still cross-fade under the
            // timbre control (ktimbHi), unchanged.
            if (i >= 6)
                std::snprintf(line, sizeof(line),
                    "  a%-2d      oscili %.4f*ktimbHi, kfreq%d\n",
                    i + 1, kPartials[i].amp, i + 1);
            else
                std::snprintf(line, sizeof(line),
                    "  a%-2d      oscili %.4f, kfreq%d\n",
                    i + 1, kPartials[i].amp, i + 1);
            csd += line;
        }

        csd +=
            "\n"
            "  asum     = a1+a2+a3+a4+a5+a6+a7+a8+a9+a10+a11+a12\n"
            "  ; headroom: the 0.2 scale bounds the worst-case simultaneous-phase\n"
            "  ; sum of all 12 standing partials (amp sum ~1.82) to <= ~0.5; the\n"
            "  ; voice's own VCA/DCA chain downstream handles the rest. kgate\n"
            "  ; (declick only) keeps an idle/inactive voice provably silent.\n"
            "  aout     = asum * kgate * kvel * kpresGain * 0.2\n"
            "  outch    ivoice, aout\n"
            "endin\n"
            "</CsInstruments>\n<CsScore>\n";

        for (int v = 1; v <= CsoundEngine::kMaxVoices; ++v)
        {
            std::snprintf(line, sizeof(line), "i 1 0 360000 %d\n", v);
            csd += line;
        }
        csd += "e 360000\n</CsScore>\n</CsoundSynthesizer>\n";
        return csd;
    }

    std::string drainMessages (CSOUND* cs)
    {
        std::string log;
        const int n = csoundGetMessageCnt(cs);
        for (int i = 0; i < n; ++i)
        {
            if (const char* msg = csoundGetFirstMessage(cs))
                log += msg;
            csoundPopFirstMessage(cs);
        }
        return log;
    }

    // Phase-3 integration: backend/csound_assembler.py's generated orchestras
    // (and tools/csound_orch_check.cpp's own copy of this exact routine) carry
    // a literal "sr = %SR%" marker instead of a hard-coded sample rate, so a
    // preset-loaded orchestra can recompile correctly after a host
    // sample-rate change (D9's early-out above already forces a full
    // recompile whenever preparedSampleRate changes — this substitution just
    // has to run fresh on every such recompile, which it does since csdText
    // is always rebuilt from the ORIGINAL orchestraText, never cached in
    // substituted form). Plain substring replace (not printf-style
    // formatting) — mirrors csound_orch_check.cpp's substituteSr() verbatim.
    // A no-op on text without the marker (the built-in orchestra never
    // contains it), so this is safe to apply unconditionally.
    std::string substituteSr (std::string text, double sampleRate)
    {
        char srBuf[32];
        std::snprintf(srBuf, sizeof(srBuf), "%.0f", sampleRate);
        const std::string marker = "%SR%";
        size_t pos = 0;
        while ((pos = text.find(marker, pos)) != std::string::npos)
        {
            text.replace(pos, marker.size(), srBuf);
            pos += std::strlen(srBuf);
        }
        return text;
    }
}

struct CsoundEngine::Impl
{
    CSOUND* csound = nullptr;
    double preparedSampleRate = 0.0;
    int capacity  = 0;   // allocated voice-buffer length (>= every startBlock's numSamples)
    int blockSize = 0;   // current host block length
    int writePos  = 0;   // samples already rendered into the current block

    static constexpr int kChannelsPerVoice = 6; // gate, freq, vel, pres, timb, trig
    MYFLT* channelPtr[CsoundEngine::kMaxVoices][kChannelsPerVoice] = {};

    std::vector<float> voiceBuf[CsoundEngine::kMaxVoices];
    std::array<float, CsoundEngine::kKsmps> carryBuf[CsoundEngine::kMaxVoices] {};
    int carryCount = 0;

    // Phase-2 (spec S2): whichever orchestra text is currently compiled — empty
    // means the built-in Phase-1 orchestra. Set only on a SUCCESSFUL (re)compile,
    // right before `ready` is published; read by orchestraText() (message/
    // compile-thread only) and by prepare()'s own early-out check on the next call.
    std::string compiledOrchestraText;

    // Publish order (D9): pointers + buffers are fully set up in prepare()
    // BEFORE this is stored (release); the audio thread only ever loads it
    // (acquire) and never touches the instance before observing true — no
    // callback lock needed.
    std::atomic<bool> ready { false };

    ~Impl()
    {
        if (csound != nullptr)
            csoundDestroy(csound);
    }

    // Message/setup-thread-only helper (warmup): sets a channel by name.
    void setNamedChannel (const char* prefix, int voice1based, double value)
    {
        char name[16];
        std::snprintf(name, sizeof(name), "%s%d", prefix, voice1based);
        csoundSetControlChannel(csound, name, (MYFLT) value);
    }

    // Message/setup-thread-only helper: resolves and caches one channel MYFLT*.
    bool resolveChannelPtr (const char* prefix, int voice1based, MYFLT*& out)
    {
        char name[16];
        std::snprintf(name, sizeof(name), "%s%d", prefix, voice1based);
        return csoundGetChannelPtr(csound, &out, name,
            CSOUND_CONTROL_CHANNEL | CSOUND_INPUT_CHANNEL) == 0 && out != nullptr;
    }
};

CsoundEngine::CsoundEngine() : impl (std::make_unique<Impl>()) {}
CsoundEngine::~CsoundEngine() = default;

bool CsoundEngine::prepare (double sampleRate, int maxBlockSize, const char* orchestraText)
{
    // Phase-0 verified: MYFLT is double on this Homebrew build. A mismatch
    // here means spout would be read at the wrong width further down.
    assert(sizeof(MYFLT) == 8 &&
           "CsoundEngine assumes MYFLT==double (8 bytes), matching this machine's "
           "Homebrew Csound build (Phase-0 verified).");

    const int wantedCapacity = maxBlockSize > 0 ? maxBlockSize : 1;

    // Phase-2 (spec S2): nullptr means "the built-in orchestra"; compare against
    // whatever is CURRENTLY compiled (empty == built-in) so the D9 early-out just
    // below can never silently swallow a genuine orchestra-swap request — only a
    // re-prepare with the SAME text at the SAME sample rate may take that path.
    const bool requestedIsBuiltIn = (orchestraText == nullptr);
    const bool sameOrchestraAlreadyCompiled =
        requestedIsBuiltIn ? impl->compiledOrchestraText.empty()
                            : impl->compiledOrchestraText == orchestraText;

    // D9 early-out: already prepared at this sample rate with this same text.
    // Hosts call prepareToPlay repeatedly (e.g. on every play/stop); only the
    // FIRST call (or an actual sample-rate/orchestra change) may (re)compile/
    // rewarm the Csound instance. A host block-size-only change just grows the
    // (message-thread-owned) buffers -- no recompile needed.
    if (impl->ready.load(std::memory_order_acquire) && impl->preparedSampleRate == sampleRate
        && sameOrchestraAlreadyCompiled)
    {
        if (wantedCapacity > impl->capacity)
        {
            impl->ready.store(false, std::memory_order_release); // audio thread must not read mid-resize
            for (int v = 0; v < kMaxVoices; ++v)
                impl->voiceBuf[v].assign((size_t) wantedCapacity, 0.0f);
            impl->capacity  = wantedCapacity;
            impl->writePos  = 0;
            impl->blockSize = 0;
            impl->carryCount = 0;
            impl->ready.store(true, std::memory_order_release);
        }
        return true;
    }

    // (Re)compile path -- first prepare(), or a sample-rate change. Keep the
    // engine inert (never half-armed) until every step below has succeeded.
    impl->ready.store(false, std::memory_order_release);

    if (impl->csound != nullptr)
    {
        csoundDestroy(impl->csound);
        impl->csound = nullptr;
    }

    impl->csound = csoundCreate(nullptr); // NEVER in the constructor (host plugin-scan
                                           // constructs processors without prepareToPlay)
    if (impl->csound == nullptr)
    {
        std::fprintf(stderr, "CsoundEngine: csoundCreate() returned null\n");
        return false;
    }

    CSOUND* cs = impl->csound;
    csoundCreateMessageBuffer(cs, 0); // queue-only; never print straight to console
    csoundSetOption(cs, "-n");
    csoundSetOption(cs, "-d");

    const std::string rawText = requestedIsBuiltIn ? buildOrchestra(sampleRate) : std::string(orchestraText);
    const std::string csdText = substituteSr(rawText, sampleRate);
    if (csoundCompileCsdText(cs, csdText.c_str()) != 0)
    {
        std::fprintf(stderr, "CsoundEngine: compile failed:\n%s\n", drainMessages(cs).c_str());
        csoundDestroyMessageBuffer(cs);
        csoundDestroy(cs);
        impl->csound = nullptr;
        return false;
    }
    if (csoundStart(cs) != 0)
    {
        std::fprintf(stderr, "CsoundEngine: csoundStart failed:\n%s\n", drainMessages(cs).c_str());
        csoundDestroyMessageBuffer(cs);
        csoundDestroy(cs);
        impl->csound = nullptr;
        return false;
    }

    if (csoundGetSizeOfMYFLT() != (int) sizeof(MYFLT))
    {
        std::fprintf(stderr, "CsoundEngine: FATAL MYFLT size mismatch (runtime=%d, compile-time=%d)\n",
                      csoundGetSizeOfMYFLT(), (int) sizeof(MYFLT));
        csoundDestroyMessageBuffer(cs);
        csoundDestroy(cs);
        impl->csound = nullptr;
        return false;
    }
    if ((int) csoundGetKsmps(cs) != kKsmps)
    {
        std::fprintf(stderr, "CsoundEngine: FATAL ksmps mismatch (runtime=%u, expected=%d)\n",
                      csoundGetKsmps(cs), (unsigned) kKsmps);
        csoundDestroyMessageBuffer(cs);
        csoundDestroy(cs);
        impl->csound = nullptr;
        return false;
    }
    // Untrusted preset-authored orchestra text (hand-edited .t5p): chnget
    // resolves the named control channels independent of the orchestra's
    // header nchnls, so a mismatched header would still reach ready==true.
    // renderUpTo() de-interleaves spout with a hardcoded kMaxVoices stride
    // (spout[s*kMaxVoices+v]) -- a smaller runtime nchnls reads past the
    // spout buffer on the audio thread.
    if ((int) csoundGetNchnls(cs) != kMaxVoices)
    {
        std::fprintf(stderr, "CsoundEngine: FATAL nchnls mismatch (runtime=%u, expected=%d)\n",
                      csoundGetNchnls(cs), (unsigned) kMaxVoices);
        csoundDestroyMessageBuffer(cs);
        csoundDestroy(cs);
        impl->csound = nullptr;
        return false;
    }

    impl->capacity = wantedCapacity;
    for (int v = 0; v < kMaxVoices; ++v)
    {
        impl->voiceBuf[v].assign((size_t) impl->capacity, 0.0f);
        impl->carryBuf[v].fill(0.0f);
    }
    impl->carryCount = 0;
    impl->writePos   = 0;
    impl->blockSize  = 0;

    // ---- D4 warm-up: absorb the one-time lazy-init allocation residue
    // inside the first gated performKsmps passes, BEFORE the instance is
    // exposed to the audio thread. Uses name-based csoundSetControlChannel
    // (message-thread only, never on the RT path) -- cached pointers are
    // resolved AFTER this block, so warmup cannot use them yet. ----
    for (int v = 1; v <= kMaxVoices; ++v)
    {
        const double freq = 110.0 * std::pow(8.0, (double) (v - 1) / (double) (kMaxVoices - 1));
        impl->setNamedChannel("gate", v, 1.0);
        impl->setNamedChannel("freq", v, freq);
        impl->setNamedChannel("vel",  v, 0.8);
        impl->setNamedChannel("pres", v, 0.0);
        impl->setNamedChannel("timb", v, 0.5);
        impl->setNamedChannel("trig", v, 1.0);
    }
    const long warmupOnBlocks = (long) std::llround(1.0 * sampleRate / (double) kKsmps);
    for (long i = 0; i < warmupOnBlocks; ++i)
        csoundPerformKsmps(cs);

    for (int v = 1; v <= kMaxVoices; ++v)
        impl->setNamedChannel("gate", v, 0.0);
    const long warmupOffBlocks = (long) std::llround(0.2 * sampleRate / (double) kKsmps);
    for (long i = 0; i < warmupOffBlocks; ++i)
        csoundPerformKsmps(cs);

    for (int v = 1; v <= kMaxVoices; ++v)
    {
        impl->setNamedChannel("gate", v, 0.0);
        impl->setNamedChannel("freq", v, 0.0);
        impl->setNamedChannel("vel",  v, 0.0);
        impl->setNamedChannel("pres", v, 0.0);
        impl->setNamedChannel("timb", v, 0.0);
        impl->setNamedChannel("trig", v, 0.0);
    }
    // Flush the zeroed channels through one perform pass so the last-performed
    // k-state reflects a clean zero baseline before the audio thread takes over.
    // (The trig channel is inert now that the orchestra is a standing tone, but
    // zeroing every channel here still leaves a defined starting state.)
    csoundPerformKsmps(cs);

    // ---- resolve all 16x6 channel pointers ONCE, post-warmup. The audio
    // thread never calls csoundGetChannelPtr/csoundSetControlChannel (name-
    // hash lookups) -- only these cached raw MYFLT* pointers. ----
    bool allResolved = true;
    static const char* const kPrefixes[Impl::kChannelsPerVoice] = { "gate", "freq", "vel", "pres", "timb", "trig" };
    for (int v = 0; v < kMaxVoices && allResolved; ++v)
        for (int c = 0; c < Impl::kChannelsPerVoice && allResolved; ++c)
            allResolved = impl->resolveChannelPtr(kPrefixes[c], v + 1, impl->channelPtr[v][c]);

    if (!allResolved)
    {
        std::fprintf(stderr, "CsoundEngine: failed to resolve one or more channel pointers\n");
        csoundDestroyMessageBuffer(cs);
        csoundDestroy(cs);
        impl->csound = nullptr;
        return false;
    }

    impl->preparedSampleRate = sampleRate;
    // Phase-2 (spec S2): record what got compiled — empty for the built-in
    // orchestra — so the NEXT prepare() call's early-out (above) and
    // orchestraText() (Phase 5 preset save) both see the truth.
    impl->compiledOrchestraText = requestedIsBuiltIn ? std::string() : std::string(orchestraText);

    // Publish order (D9): pointers + buffers fully set up BEFORE the ready
    // flag is stored with release semantics; see the Impl::ready comment.
    impl->ready.store(true, std::memory_order_release);
    return true;
}

const std::string& CsoundEngine::orchestraText() const
{
    return impl->compiledOrchestraText;
}

void CsoundEngine::primeForTakeover (const float* epochs, const float* freqs)
{
    // Message/compile-thread only (spec S3) -- see this method's header-comment
    // for the full rationale. Must run AFTER prepare() has resolved the cached
    // channel pointers (guarded defensively below) and BEFORE this engine is
    // published as a swap target; the audio thread never calls this.
    if (! impl->ready.load(std::memory_order_acquire))
        return;

    CSOUND* cs = impl->csound;
    for (int v = 0; v < kMaxVoices; ++v)
    {
        auto& ptrs = impl->channelPtr[v];
        *ptrs[1] = (MYFLT) freqs[v];    // freq  (index matches kPrefixes order in prepare())
        *ptrs[5] = (MYFLT) epochs[v];   // trig
        // gate (ptrs[0]) is left untouched -- prepare()'s warmup already left it
        // at 0, and priming must never open the gate (that would be audible).
    }

    const long primeBlocks = (long) std::llround(0.25 * impl->preparedSampleRate / (double) kKsmps);
    for (long i = 0; i < primeBlocks; ++i)
        csoundPerformKsmps(cs);
}

bool CsoundEngine::isReady() const
{
    return impl->ready.load(std::memory_order_acquire);
}

void CsoundEngine::setVoiceControls (int voiceIndex, const VoiceControls& c)
{
    if (voiceIndex < 0 || voiceIndex >= kMaxVoices)
        return;
    if (! impl->ready.load(std::memory_order_acquire))
        return;

    auto& ptrs = impl->channelPtr[voiceIndex];
    *ptrs[0] = (MYFLT) c.gate;
    *ptrs[1] = (MYFLT) c.freqHz;
    *ptrs[2] = (MYFLT) c.velocity;
    *ptrs[3] = (MYFLT) c.pressure;
    *ptrs[4] = (MYFLT) c.timbre;
    *ptrs[5] = (MYFLT) c.trigEpoch;
}

void CsoundEngine::startBlock (int numSamples)
{
    if (! impl->ready.load(std::memory_order_acquire))
        return;

    int bs = numSamples;
    if (bs > impl->capacity) bs = impl->capacity; // defensive clamp; prepare()'s maxBlockSize contract should make this unreachable
    if (bs < 0) bs = 0;
    impl->blockSize = bs;
    impl->writePos  = 0;

    if (impl->carryCount > 0)
    {
        const int n = std::min(impl->carryCount, impl->blockSize);
        for (int v = 0; v < kMaxVoices; ++v)
            std::memcpy(impl->voiceBuf[v].data(), impl->carryBuf[v].data(), (size_t) n * sizeof(float));
        impl->writePos = n;

        const int remaining = impl->carryCount - n;
        if (remaining > 0)
            for (int v = 0; v < kMaxVoices; ++v)
                std::memmove(impl->carryBuf[v].data(), impl->carryBuf[v].data() + n, (size_t) remaining * sizeof(float));
        impl->carryCount = remaining;
    }
}

void CsoundEngine::renderUpTo (int upToSample)
{
    if (! impl->ready.load(std::memory_order_acquire))
        return;

    const int target = upToSample + 1;
    CSOUND* cs = impl->csound;

    // Idempotent/monotonic: if writePos already covers `target` (this
    // sub-range was already rendered by an earlier call within the same
    // block), the loop body never runs.
    while (impl->writePos < target && impl->writePos < impl->blockSize)
    {
        csoundPerformKsmps(cs);
        MYFLT* spout = csoundGetSpout(cs);

        const int samplesToBlock = std::min(kKsmps, impl->blockSize - impl->writePos);
        for (int v = 0; v < kMaxVoices; ++v)
        {
            float* dst = impl->voiceBuf[v].data() + impl->writePos;
            for (int s = 0; s < samplesToBlock; ++s)
                dst[s] = (float) spout[(size_t) s * (size_t) kMaxVoices + (size_t) v];
        }

        const int leftover = kKsmps - samplesToBlock;
        if (leftover > 0)
        {
            // Surplus beyond this block's end goes to the carry store,
            // replayed at the START of the next startBlock().
            for (int v = 0; v < kMaxVoices; ++v)
            {
                float* dst = impl->carryBuf[v].data();
                for (int s = 0; s < leftover; ++s)
                    dst[s] = (float) spout[(size_t) (samplesToBlock + s) * (size_t) kMaxVoices + (size_t) v];
            }
            impl->carryCount = leftover;
        }
        else
        {
            impl->carryCount = 0;
        }

        impl->writePos += samplesToBlock;
    }
}

const float* CsoundEngine::voiceBuffer (int voiceIndex) const
{
    if (voiceIndex < 0 || voiceIndex >= kMaxVoices)
        return nullptr;
    // Not-ready guard: before a successful prepare() the vectors may be empty;
    // data() would be a non-null-but-invalid base for caller pointer arithmetic.
    if (! impl->ready.load (std::memory_order_acquire))
        return nullptr;
    return impl->voiceBuf[voiceIndex].data();
}

#endif // T5YNTH_HAS_CSOUND
