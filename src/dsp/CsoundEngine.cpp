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
#include <dlfcn.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace
{
    // ── Where Csound looks for its plugin opcodes ───────────────────────────
    //
    // Csound resolves them through a path baked in when the library was BUILT:
    // on this machine `strings CsoundLib64` shows
    // /opt/homebrew/Cellar/csound/<version>/…/Resources/Opcodes64. A copy of the
    // library inside our bundle therefore keeps reaching into a Homebrew tree
    // that exists here and nowhere else — measured 2026-07-26: with that tree
    // present 2482 opcode entries register, without it 1917, and the 565 missing
    // ones include scanu/scanu2/scans, fractalnoise, tvconv, MixerSend,
    // ftgenonce, limit1 and GEN padsynth. The author WRITES Csound and may reach
    // for any of them, so they are copied next to the library
    // (tools/bundle_csound_macos.sh) and pointed at here.
    //
    // The rule is deliberately self-validating: use the Opcodes64 that sits NEXT
    // TO the library actually loaded, and only if it is really there. A plain
    // system install has no such sibling directory, so nothing is overridden and
    // Csound's own resolution — which is correct there — is left alone.
    //
    // csoundSetOpcodedir is global to this loaded image and takes effect at the
    // next csoundCreate, hence once, before the first one. It cannot disturb
    // another plugin's Csound in the same host process: that one is a different
    // image with its own copy of this state.
    void useBundledOpcodeDirIfPresent()
    {
        static std::once_flag once;
        std::call_once (once, []
        {
            Dl_info info {};
            if (dladdr (reinterpret_cast<const void*> (&csoundCreate), &info) == 0
                || info.dli_fname == nullptr)
                return;

            std::string dir { info.dli_fname };
            const auto slash = dir.rfind ('/');
            if (slash == std::string::npos)
                return;

            dir.resize (slash);
            dir += "/Opcodes64";

            struct stat st {};
            if (stat (dir.c_str(), &st) != 0 || ! S_ISDIR (st.st_mode))
                return;

            csoundSetOpcodedir (dir.c_str());
        });
    }

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
            "  ; kvel deliberately absent (matches lco_write._TAIL): the voice\n"
            "  ; envelope's peak already tracks velocity, so a kvel factor here\n"
            "  ; made the engine scale as vel^2 where every other engine is linear.\n"
            "  aout     = asum * kgate * kpresGain * 0.2\n"
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
    // Serialises the csoundCreate/CompileCsdText/Start/Destroy sequence across
    // ALL CsoundEngine instances in this process, including the instance-free
    // offline probe. The processor's csoundLifecycleMutex_ already serialises
    // every prepare() on its two engines and was added by adversarial review
    // because concurrent prepare() was judged a race; the probe cannot reach that
    // mutex (it is static and owns no engine), so without this the probe would be
    // the one path violating an invariant the rest of the code maintains.
    //
    // Lock ORDER is always csoundLifecycleMutex_ -> this one, never the reverse:
    // prepare() is only ever called under the processor's mutex, and this mutex is
    // released before any caller could take the processor's. No cycle, no deadlock.
    std::mutex& csoundLifecycleGlobal()
    {
        static std::mutex m;
        return m;
    }

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

namespace
{
    // ── Decimation filter for source-side oversampling ──────────────────────
    //
    // Linear-phase halfband FIR, 63 taps, Kaiser(beta=8). At a 96 kHz input rate
    // it is flat to ~21 kHz and >=80 dB down from ~28 kHz; every even tap except
    // the centre is exactly zero (halfband), which is what makes 63 taps this
    // cheap. Measured against a 768 kHz reference render it lands within 2 dB of
    // an ideal brickwall on every library patch, so the residual is the
    // oversampling factor's, not the filter's. Group delay is 31 input samples
    // (0.32 ms at 96 kHz) — constant, and the LRO is never layered against
    // another engine (the mode toggle owns engineMode), so it cannot comb.
    constexpr int kDecimTaps = 63;
    constexpr float kHalfbandFir[kDecimTaps] = {
        -2.401525244e-05f,  0.000000000e+00f,  1.090362306e-04f,  0.000000000e+00f, -2.935600810e-04f,  0.000000000e+00f,
         6.381063754e-04f,  0.000000000e+00f, -1.222076003e-03f,  0.000000000e+00f,  2.145908571e-03f,  0.000000000e+00f,
        -3.534414302e-03f,  0.000000000e+00f,  5.543647017e-03f,  0.000000000e+00f, -8.376210429e-03f,  0.000000000e+00f,
         1.231558303e-02f,  0.000000000e+00f, -1.780470021e-02f,  0.000000000e+00f,  2.563768528e-02f,  0.000000000e+00f,
        -3.748938157e-02f,  0.000000000e+00f,  5.772404439e-02f,  0.000000000e+00f, -1.024425088e-01f,  0.000000000e+00f,
         3.170728721e-01f,  4.999999672e-01f,  3.170728721e-01f,  0.000000000e+00f, -1.024425088e-01f,  0.000000000e+00f,
         5.772404439e-02f,  0.000000000e+00f, -3.748938157e-02f,  0.000000000e+00f,  2.563768528e-02f,  0.000000000e+00f,
        -1.780470021e-02f,  0.000000000e+00f,  1.231558303e-02f,  0.000000000e+00f, -8.376210429e-03f,  0.000000000e+00f,
         5.543647017e-03f,  0.000000000e+00f, -3.534414302e-03f,  0.000000000e+00f,  2.145908571e-03f,  0.000000000e+00f,
        -1.222076003e-03f,  0.000000000e+00f,  6.381063754e-04f,  0.000000000e+00f, -2.935600810e-04f,  0.000000000e+00f,
         1.090362306e-04f,  0.000000000e+00f, -2.401525244e-05f,
    };

    // 2:1 decimator — one per voice per stage (4x cascades two of them).
    // RT-safe by construction: fixed storage, no allocation, no branch on the
    // sample path. State is cleared in prepare() before the engine goes ready,
    // so a recompile can never leak the previous orchestra's tail into the new
    // one's first block.
    struct Decimator2x
    {
        static constexpr unsigned kMask = 63u;   // history is 64 == kDecimTaps+1
        float hist[kMask + 1] = {};
        unsigned pos = 0;

        void reset() noexcept
        {
            for (auto& s : hist) s = 0.0f;
            pos = 0;
        }

        // Consumes TWO input samples, returns ONE band-limited output sample.
        inline float process (float a, float b) noexcept
        {
            hist[pos & kMask] = a; ++pos;
            hist[pos & kMask] = b; ++pos;
            float acc = 0.0f;
            for (int k = 0; k < kDecimTaps; ++k)
                acc += kHalfbandFir[k] * hist[(pos - 1u - (unsigned) k) & kMask];
            return acc;
        }
    };
}

struct CsoundEngine::Impl
{
    CSOUND* csound = nullptr;
    double preparedSampleRate = 0.0;   // HOST rate — what the caller asked for
    double engineSampleRate   = 0.0;   // preparedSampleRate * osFactor — what Csound runs at
    int    osFactor           = 1;     // 1, 2 or 4

    // Decimation chain, per voice. decim1 always runs when osFactor > 1;
    // decim2 only at 4x. Sized for kMaxVoices, never resized.
    Decimator2x decim1[CsoundEngine::kMaxVoices];
    Decimator2x decim2[CsoundEngine::kMaxVoices];
    // One ksmps' worth of HOST-rate samples per voice, produced by renderUpTo()
    // before the existing block/carry bookkeeping copies them out. At osFactor 1
    // that is kKsmps samples; higher factors use only the first kKsmps/osFactor.
    float decimScratch[CsoundEngine::kMaxVoices][CsoundEngine::kKsmps] {};

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
        {
            // Destroy is part of the serialized lifecycle sequence, and this one
            // can run while the offline probe (renderBareOscillator, a detached
            // thread holding no reference to this processor) is inside its own
            // create/compile — so it takes the same lock every other lifecycle
            // call takes. See csoundLifecycleGlobal().
            const std::lock_guard<std::mutex> lifecycleLock (csoundLifecycleGlobal());
            csoundDestroy(csound);
        }
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

int CsoundEngine::effectiveOversampleFactor (double sampleRate, int requested)
{
    // Only 1/2/4 are meaningful, and kKsmps must divide by the factor so a single
    // csoundPerformKsmps always yields a whole number of host samples. Anything
    // else falls back to 1 rather than producing a fractional-length block.
    if (requested != 1 && requested != 2 && requested != 4)
        requested = 1;
    if (kKsmps % requested != 0)
        requested = 1;

    // Cap the ABSOLUTE engine rate, not just the factor. At a 96 kHz host rate
    // 4x would mean 384 kHz — double the CPU again, to push away a Nyquist that
    // is already twice as far off as the 48 kHz case this was measured for. The
    // benefit saturates around 192 kHz, so a high-rate host gets the same
    // cleanliness at the factor that actually reaches it.
    while (requested > 1 && sampleRate * (double) requested > 200000.0)
        requested /= 2;

    return requested;
}

int CsoundEngine::oversampleFactor() const
{
    return impl->osFactor;
}

bool CsoundEngine::prepare (double sampleRate, int maxBlockSize, const char* orchestraText,
                            int oversampleFactor)
{
    // Phase-0 verified: MYFLT is double on this Homebrew build. A mismatch
    // here means spout would be read at the wrong width further down.
    assert(sizeof(MYFLT) == 8 &&
           "CsoundEngine assumes MYFLT==double (8 bytes), matching this machine's "
           "Homebrew Csound build (Phase-0 verified).");

    oversampleFactor = effectiveOversampleFactor (sampleRate, oversampleFactor);

    // What Csound itself is compiled and run at. Everything the CALLER sees —
    // startBlock/renderUpTo/voiceBuffer — stays in host samples at `sampleRate`.
    const double engineRate = sampleRate * (double) oversampleFactor;

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
    // The oversampling factor is part of what got COMPILED (it sets Csound's own
    // sr, and an authored orchestra may read `sr` to bound its own bandwidth), so
    // a factor change must take the full recompile path, never this early-out.
    if (impl->ready.load(std::memory_order_acquire) && impl->preparedSampleRate == sampleRate
        && impl->osFactor == oversampleFactor
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

    // Held across destroy/create/compile/start only — NOT across the warmup below,
    // which is per-instance csoundPerformKsmps and safe concurrently. Released
    // explicitly after the contract checks; every early return releases it too.
    std::unique_lock<std::mutex> lifecycleLock (csoundLifecycleGlobal());

    if (impl->csound != nullptr)
    {
        csoundDestroy(impl->csound);
        impl->csound = nullptr;
    }

    useBundledOpcodeDirIfPresent();
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

    // Both the built-in orchestra's baked-in header and an authored orchestra's
    // %SR% marker get the OVERSAMPLED rate: that is the rate Csound computes at,
    // and it is also the rate an authored patch must see when it derives its own
    // partial count or FM index from `sr` — telling it 48000 while running it at
    // 192000 would keep it bandwidth-limited to a Nyquist that no longer applies.
    const std::string rawText = requestedIsBuiltIn ? buildOrchestra(engineRate) : std::string(orchestraText);
    const std::string csdText = substituteSr(rawText, engineRate);
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

    // Compile phase over: the remaining work (channel resolution, buffer sizing,
    // the ~1.2 s warmup) touches only this instance.
    lifecycleLock.unlock();

    impl->capacity = wantedCapacity;
    for (int v = 0; v < kMaxVoices; ++v)
    {
        impl->voiceBuf[v].assign((size_t) impl->capacity, 0.0f);
        impl->carryBuf[v].fill(0.0f);
        // Cleared here, not just on construction: this same Impl is reused across
        // recompiles, and a decimator still holding the previous orchestra's tail
        // would bleed it into the new one's first 31 output samples.
        impl->decim1[v].reset();
        impl->decim2[v].reset();
        for (auto& s : impl->decimScratch[v]) s = 0.0f;
    }
    impl->carryCount = 0;
    impl->writePos   = 0;
    impl->blockSize  = 0;
    impl->osFactor   = oversampleFactor;

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
    // Warm-up durations are in SECONDS, so the block counts follow the rate
    // Csound actually runs at — at 4x, `sampleRate` here would warm up for a
    // quarter of the intended second.
    const long warmupOnBlocks = (long) std::llround(1.0 * engineRate / (double) kKsmps);
    for (long i = 0; i < warmupOnBlocks; ++i)
        csoundPerformKsmps(cs);

    for (int v = 1; v <= kMaxVoices; ++v)
        impl->setNamedChannel("gate", v, 0.0);
    const long warmupOffBlocks = (long) std::llround(0.2 * engineRate / (double) kKsmps);
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
        // Re-take the lifecycle lock dropped above: this is the one failure path
        // that destroys an instance AFTER the compile phase, and destroy belongs
        // inside the serialized sequence like every other lifecycle call.
        lifecycleLock.lock();
        csoundDestroyMessageBuffer(cs);
        csoundDestroy(cs);
        impl->csound = nullptr;
        return false;
    }

    impl->preparedSampleRate = sampleRate;
    impl->engineSampleRate   = engineRate;
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

    // 0.25 s at the rate Csound runs at, not the host rate — same reason as the
    // warm-up loops in prepare().
    const long primeBlocks = (long) std::llround(0.25 * impl->engineSampleRate / (double) kKsmps);
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
    const int osF = impl->osFactor;
    // Host-rate samples one csoundPerformKsmps yields: kKsmps at 1x, half that
    // at 2x, a quarter at 4x. prepare() guarantees kKsmps divides by osFactor,
    // so this is exact and the carry store (sized kKsmps) always has room.
    const int perPerform = kKsmps / osF;

    // Idempotent/monotonic: if writePos already covers `target` (this
    // sub-range was already rendered by an earlier call within the same
    // block), the loop body never runs.
    while (impl->writePos < target && impl->writePos < impl->blockSize)
    {
        csoundPerformKsmps(cs);
        MYFLT* spout = csoundGetSpout(cs);

        // De-interleave spout and, when oversampling, band-limit + decimate it
        // to the host rate. Everything downstream of here counts HOST samples.
        for (int v = 0; v < kMaxVoices; ++v)
        {
            float* out = impl->decimScratch[v];
            if (osF == 1)
            {
                for (int s = 0; s < kKsmps; ++s)
                    out[s] = (float) spout[(size_t) s * (size_t) kMaxVoices + (size_t) v];
            }
            else if (osF == 2)
            {
                for (int s = 0; s < perPerform; ++s)
                    out[s] = impl->decim1[v].process(
                        (float) spout[(size_t) (2 * s)     * (size_t) kMaxVoices + (size_t) v],
                        (float) spout[(size_t) (2 * s + 1) * (size_t) kMaxVoices + (size_t) v]);
            }
            else // osF == 4: two cascaded halfband stages, 192k -> 96k -> 48k
            {
                for (int s = 0; s < perPerform; ++s)
                {
                    const float a = impl->decim1[v].process(
                        (float) spout[(size_t) (4 * s)     * (size_t) kMaxVoices + (size_t) v],
                        (float) spout[(size_t) (4 * s + 1) * (size_t) kMaxVoices + (size_t) v]);
                    const float b = impl->decim1[v].process(
                        (float) spout[(size_t) (4 * s + 2) * (size_t) kMaxVoices + (size_t) v],
                        (float) spout[(size_t) (4 * s + 3) * (size_t) kMaxVoices + (size_t) v]);
                    out[s] = impl->decim2[v].process(a, b);
                }
            }
        }

        const int samplesToBlock = std::min(perPerform, impl->blockSize - impl->writePos);
        for (int v = 0; v < kMaxVoices; ++v)
        {
            float* dst = impl->voiceBuf[v].data() + impl->writePos;
            const float* src = impl->decimScratch[v];
            for (int s = 0; s < samplesToBlock; ++s)
                dst[s] = src[s];
        }

        const int leftover = perPerform - samplesToBlock;
        if (leftover > 0)
        {
            // Surplus beyond this block's end goes to the carry store,
            // replayed at the START of the next startBlock().
            for (int v = 0; v < kMaxVoices; ++v)
            {
                float* dst = impl->carryBuf[v].data();
                const float* src = impl->decimScratch[v] + samplesToBlock;
                for (int s = 0; s < leftover; ++s)
                    dst[s] = src[s];
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

std::vector<float> CsoundEngine::renderBareOscillator (const std::string& orchestraText,
                                                       double sampleRate,
                                                       double freqHz,
                                                       double seconds,
                                                       double gateOffSeconds)
{
    // `! (x > 0.0)` rather than `x <= 0.0`: it rejects NaN too, which every
    // comparison below would silently pass through into the render.
    if (orchestraText.empty() || ! (sampleRate > 0.0) || ! (seconds > 0.0)
        || ! (freqHz > 0.0))
        return {};

    // Upper bounds, not just lower ones: a caller passing a wild `seconds` would
    // overflow the block count and make reserve() throw out of a detached thread —
    // std::terminate, from a probe whose entire contract is that it cannot damage
    // anything. 60 s is far past any useful probe window.
    sampleRate = std::min(sampleRate, 192000.0);
    seconds    = std::min(seconds, 60.0);
    freqHz     = std::min(freqHz, sampleRate * 0.5);

    // Clamp INTO the window, in BLOCK units, because the loop below compares block
    // indices: below one block the gate closes before the first perform pass
    // (guaranteed silence), and at `seconds` the index equals totalBlocks, which
    // the `b < totalBlocks` loop never reaches (the gate never closes, so no
    // release is captured). Both ends need a whole block of margin, not an epsilon.
    // A window too short to hold both is left gate-open rather than silent.
    {
        const double oneBlock = (double) kKsmps / sampleRate;
        const double minGate  = oneBlock;
        const double maxGate  = seconds - oneBlock;
        gateOffSeconds = (maxGate <= minGate)
                       ? minGate
                       : std::min(std::max(gateOffSeconds, minGate), maxGate);
    }

    // Own instance, own lifetime. The lock covers create/compile/start and the
    // destroy for the same reason prepare() takes it — see csoundLifecycleGlobal().
    // It is RELEASED before the render loop: that loop touches only this instance,
    // and holding it there would stall the live orchestra swap (prepare() takes the
    // same mutex) for the whole render, delaying the sound the user is waiting for.
    std::unique_lock<std::mutex> lifecycleLock (csoundLifecycleGlobal());

    useBundledOpcodeDirIfPresent();
    CSOUND* cs = csoundCreate(nullptr);
    if (cs == nullptr)
        return {};

    // RAII: every early return below must destroy the instance, and there are
    // nine of them. A guard object is the only way that stays true when a later
    // contract check is inserted in the middle. It re-takes the lifecycle lock if
    // the render already dropped it, so destroy stays inside the serialized
    // sequence; declared AFTER the lock, it therefore runs BEFORE the lock's own
    // destructor releases it.
    struct Guard
    {
        CSOUND* cs;
        std::unique_lock<std::mutex>& lk;
        ~Guard()
        {
            if (cs == nullptr) return;
            if (! lk.owns_lock()) lk.lock();
            csoundDestroyMessageBuffer(cs);
            csoundDestroy(cs);
        }
    } guard { cs, lifecycleLock };

    csoundCreateMessageBuffer(cs, 0);
    csoundSetOption(cs, "-n");   // no audio device: this render is never heard
    csoundSetOption(cs, "-d");   // no displays

    if (csoundCompileCsdText(cs, substituteSr(orchestraText, sampleRate).c_str()) != 0)
    {
        std::fprintf(stderr, "CsoundEngine::renderBareOscillator: compile failed:\n%s\n",
                      drainMessages(cs).c_str());
        return {};
    }
    if (csoundStart(cs) != 0)
    {
        std::fprintf(stderr, "CsoundEngine::renderBareOscillator: start failed:\n%s\n",
                      drainMessages(cs).c_str());
        return {};
    }

    // The SAME three contract checks prepare() makes, for the same reasons: the
    // spout read below strides by kMaxVoices and reads MYFLT, so a mismatched
    // nchnls or MYFLT width would read out of bounds. An orchestra reaching this
    // path can come from a hand-edited .t5p, so it is not trusted here either.
    if (csoundGetSizeOfMYFLT() != (int) sizeof(MYFLT)
        || (int) csoundGetKsmps(cs) != kKsmps
        || (int) csoundGetNchnls(cs) != kMaxVoices)
    {
        std::fprintf(stderr, "CsoundEngine::renderBareOscillator: contract mismatch "
                             "(MYFLT=%d ksmps=%u nchnls=%u)\n",
                      csoundGetSizeOfMYFLT(), csoundGetKsmps(cs), csoundGetNchnls(cs));
        return {};
    }

    // Voice 1 only. Names must match prepare()'s kPrefixes order/spelling.
    MYFLT* gate = nullptr; MYFLT* freq = nullptr; MYFLT* vel  = nullptr;
    MYFLT* pres = nullptr; MYFLT* timb = nullptr; MYFLT* trig = nullptr;
    auto resolve = [cs] (const char* name, MYFLT*& out)
    {
        return csoundGetChannelPtr(cs, &out, name,
                   CSOUND_CONTROL_CHANNEL | CSOUND_INPUT_CHANNEL) == 0 && out != nullptr;
    };
    if (! (resolve("gate1", gate) && resolve("freq1", freq) && resolve("vel1", vel)
           && resolve("pres1", pres) && resolve("timb1", timb) && resolve("trig1", trig)))
    {
        std::fprintf(stderr, "CsoundEngine::renderBareOscillator: channel resolve failed\n");
        return {};
    }

    // Neutral performance controls: mid velocity, no pressure, mid timbre — the
    // probe asks what the ORCHESTRA sounds like, not what a performance does to it.
    *gate = (MYFLT) 1.0;  *freq = (MYFLT) freqHz;  *vel  = (MYFLT) 0.85;
    *pres = (MYFLT) 0.0;  *timb = (MYFLT) 0.5;     *trig = (MYFLT) 1.0;

    const long totalBlocks = (long) std::llround(seconds * sampleRate / (double) kKsmps);
    const long gateOffBlk  = (long) std::llround(gateOffSeconds * sampleRate / (double) kKsmps);

    std::vector<float> mono;
    mono.reserve((size_t) std::max(0L, totalBlocks) * (size_t) kKsmps);

    // Compile phase over — everything from here to the Guard touches only this
    // instance. Mirrors prepare()'s own unlock at the same point in its sequence.
    lifecycleLock.unlock();

    for (long b = 0; b < totalBlocks; ++b)
    {
        if (b == gateOffBlk)
            *gate = (MYFLT) 0.0;
        if (csoundPerformKsmps(cs) != 0)
            break;                       // score ended early: keep what was rendered
        const MYFLT* spout = csoundGetSpout(cs);
        for (int s = 0; s < kKsmps; ++s)
            mono.push_back((float) spout[(size_t) s * (size_t) kMaxVoices]);   // voice 1
    }

    // Drain and discard: the message buffer grows for the whole render (Csound
    // queues a per-note and an end-of-performance summary), and nothing has read
    // it since the compile check. Failures above already printed what they needed.
    (void) drainMessages(cs);

    // Peak-normalise to -12 dBFS. A render that never left the noise floor is
    // returned empty instead: normalising it would manufacture a loud signal out
    // of numerical dust and hand the ear something to hallucinate words about.
    //
    // The finiteness check is NOT redundant with the peak guard: std::max(a, NaN)
    // returns a, so a NaN would skip past `peak` untouched and be scaled, encoded
    // and posted to the analyser. The orchestra text is LLM-authored, and an
    // unstable filter or a divide by zero in it lands here — tools/
    // csound_orch_check.cpp already rejects orchestras on exactly this ground.
    float peak = 0.0f;
    for (float v : mono)
    {
        if (! std::isfinite(v))
        {
            std::fprintf(stderr, "CsoundEngine::renderBareOscillator: non-finite sample, "
                                 "discarding the render\n");
            return {};
        }
        peak = std::max(peak, std::fabs(v));
    }
    constexpr float kSilenceFloor = 1.0e-6f;
    if (mono.empty() || peak < kSilenceFloor)
        return {};

    const float scale = 0.251f / peak;   // 0.251 == -12 dBFS
    for (float& v : mono)
        v *= scale;
    return mono;
}

#endif // T5YNTH_HAS_CSOUND
