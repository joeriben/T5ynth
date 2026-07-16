#pragma once
#include <memory>

/**
 * Phase-1 Csound engine — one processor-owned Csound instance running 16
 * always-on, gate-sustained instruments (one per voice slot), driven purely
 * by named control channels the audio thread writes every sub-block.
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

    // Message/setup thread only (prepareToPlay). Compiles the hard-wired orchestra at this
    // sample rate, runs the warmup (D4), resolves all channel pointers. Returns false on any
    // failure (missing framework at runtime cannot happen — link-time — but compile errors
    // must be loud in DBG and leave the engine inert, never half-armed).
    bool prepare (double sampleRate, int maxBlockSize);

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

inline bool CsoundEngine::prepare (double, int) { return false; }
inline bool CsoundEngine::isReady() const { return false; }
inline void CsoundEngine::setVoiceControls (int, const VoiceControls&) {}
inline void CsoundEngine::renderUpTo (int) {}
inline void CsoundEngine::startBlock (int) {}

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
