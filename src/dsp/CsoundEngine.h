#pragma once
#include <memory>
#include <string>

/**
 * Phase-1 Csound engine — one processor-owned Csound instance running 16
 * always-on, gate-sustained instruments (one per voice slot), driven purely
 * by named control channels the audio thread writes every sub-block.
 *
 * Phase 2 (SPEC_phase2_csound_swap.md) adds a swappable orchestra: prepare()
 * takes an optional orchestra-text override (nullptr = the Phase-1 built-in),
 * orchestraText() reports whichever text is currently compiled, and
 * primeForTakeover() silently settles a freshly-compiled instance's retrigger
 * state so a HELD note's later takeover doesn't audibly re-strike (S3). The
 * processor (PluginProcessor.h/.cpp) owns TWO of these and crossfades between
 * them; this class itself still only ever knows about ONE Csound instance.
 *
 * This header is compiled on EVERY platform/build and must never #include a
 * csound header (csound/sysdep.h leaks LIKELY, UNLIKELY, DIRSEP, the
 * ATOMIC_ family, and CS_PURE macro names that collide with identifiers
 * elsewhere in the codebase — the #undef hygiene block for that lives in
 * CsoundEngine.cpp,
 * copied from tools/csound_poc.cpp:94-102, confined to that one TU) nor any
 * JUCE header, so this class stays linkable from standalone offline test
 * binaries that don't pull in the plugin's JUCE tree.
 *
 * Real implementation (CsoundEngine.cpp) is only compiled when CMake found
 * CsoundLib64 (T5YNTH_HAS_CSOUND==1). Otherwise the inert stub below this
 * class (T5YNTH_HAS_CSOUND==0) makes every call a no-op / false / silence,
 * so call sites never need a CMake-level #ifdef — see SPEC §1/D5.
 */
class CsoundEngine
{
public:
    static constexpr int kMaxVoices = 16;   // fixed instrument/channel count, Phase 1
    static constexpr int kKsmps     = 64;

    CsoundEngine();
    ~CsoundEngine();

    // Message/setup thread only (prepareToPlay, or the D9 background-compile thread).
    // Compiles the orchestra at this sample rate, runs the warmup (D4), resolves all
    // channel pointers. orchestraText == nullptr (default) compiles the Phase-1
    // built-in orchestra, unchanged from before Phase 2; a non-null string is compiled
    // verbatim (a full CSD, following the Phase-1 channel/instrument contract — the
    // caller/assembler guarantees that, Phase 3; this method only cares that it
    // compiles and all 16x6 channels resolve). An explicit orchestraText always forces
    // a genuine (re)compile even at an unchanged sample rate — the D9 "already
    // prepared, just grow the buffers" fast path only ever applies when the text is
    // the SAME as whatever is currently compiled (see orchestraText() below), so an
    // orchestra-swap request can never be silently swallowed by that fast path.
    // Returns false on any failure (missing framework at runtime cannot happen —
    // link-time — but compile errors must be loud in DBG and leave the engine inert,
    // never half-armed, and must never disturb whatever was compiled before).
    bool prepare (double sampleRate, int maxBlockSize, const char* orchestraText = nullptr);

    // Currently compiled orchestra text (Phase-2 spec S2): empty when the built-in
    // orchestra is active. Message/compile-thread only (Phase 5 preset save reads
    // this off the message thread; never called from the audio thread).
    const std::string& orchestraText() const;

    bool isReady() const;

    // ---- audio thread (RT-safe: raw pointer writes + performKsmps, zero lookups/locks) ----
    struct VoiceControls { float gate, freqHz, velocity, pressure, timbre, trigEpoch; };
    void setVoiceControls (int voiceIndex, const VoiceControls&);   // writes via cached MYFLT*
    // Renders forward so that at least `upToSample+1` samples (block-relative) exist in every
    // per-voice buffer; carries ksmps surplus internally across calls and blocks.
    void renderUpTo (int upToSample);
    void startBlock (int numSamples);            // begins a new host block (resets write pos, replays carry)
    const float* voiceBuffer (int voiceIndex) const;  // block-aligned, sized maxBlockSize
    // ------------------------------------------------------------------------------------

    // Message/compile-thread only (Phase-2 spec S3): call AFTER a successful prepare()
    // and BEFORE this engine is published as a swap target. Seeds every voice's
    // trig+freq channel from the snapshot (epochs[kMaxVoices], freqs[kMaxVoices]; gates
    // stay 0, whatever prepare()'s warmup left them at), then pumps ~0.25s of ksmps,
    // discarding the output. Rationale: warmup leaves trig=0, so the FIRST real
    // channel write after a takeover would flip 0->epoch, changed2() would fire, and a
    // HELD note would audibly re-strike at the swap point. Priming fires that strike
    // silently (gate closed) and lets portk/the strike envelopes settle before the
    // real, audible gate-open ever happens. Never called on the audio thread.
    void primeForTakeover (const float* epochs, const float* freqs);

private:
    struct Impl;                 // owns CSOUND*, spout ptr, channel MYFLT* [16][6], FIFO carry
    std::unique_ptr<Impl> impl;  // null in the stub build
};

// Build-config safety default: every real TU gets T5YNTH_HAS_CSOUND=0-or-1
// from CMake's always-defined compile definition (JUCE_USE_CURL precedent,
// CMakeLists.txt), but a standalone TU that forgot to pass it should fall
// back to the safe (inert) stub rather than silently treating an undefined
// macro as 0 via -Wundef-style preprocessor arithmetic.
#ifndef T5YNTH_HAS_CSOUND
#define T5YNTH_HAS_CSOUND 0
#endif

#if !T5YNTH_HAS_CSOUND

// Inert stub: framework not found at configure time (or on this platform).
// Empty Impl keeps the pimpl's class layout identical across both build
// configurations — CsoundEngine.cpp defines the real Impl and is only
// compiled into the target when CMake found the framework.
struct CsoundEngine::Impl {};

inline CsoundEngine::CsoundEngine() = default;
inline CsoundEngine::~CsoundEngine() = default;

inline bool CsoundEngine::prepare (double, int, const char*) { return false; }
inline const std::string& CsoundEngine::orchestraText() const
{
    static const std::string kEmpty;
    return kEmpty;
}
inline bool CsoundEngine::isReady() const { return false; }
inline void CsoundEngine::setVoiceControls (int, const VoiceControls&) {}
inline void CsoundEngine::renderUpTo (int) {}
inline void CsoundEngine::startBlock (int) {}
inline void CsoundEngine::primeForTakeover (const float*, const float*) {}

inline const float* CsoundEngine::voiceBuffer (int) const
{
    // Fixed-size zero buffer, sized well above any realistic host block size
    // (JUCE/DAW blocks are typically <= 4096 samples). Documented bound: a
    // caller requesting more than kStubZeroBufSize samples from this stub
    // reads past the array. Not expected in practice — the real (non-stub)
    // engine sizes its buffers exactly to the maxBlockSize passed to
    // prepare(), and the stub's own prepare() always returns false, so no
    // well-behaved caller pulls audio from this stub in the first place.
    static constexpr int kStubZeroBufSize = 8192;
    static const float kZero[kStubZeroBufSize] = {};
    return kZero;
}

#endif // !T5YNTH_HAS_CSOUND
