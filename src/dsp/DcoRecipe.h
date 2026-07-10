#pragma once
#include <vector>

/**
 * DCO recipe schema — the data contract between the (future) recipe author
 * (LLM or hand-authored) and DcoBaker.
 *
 * A Recipe describes a closed-form single-cycle spectrum motion: a set of
 * Keyframes (each a deterministic param->spectrum mapping) plus a Motion
 * trajectory that morphs between them across the bake's frame count. Baking
 * happens off the audio thread (DcoBaker::bake); the result feeds the
 * existing WavetableOscillator exactly like an extracted sample.
 *
 * Design doc: docs/HANDOVER_DCO_OSCILLATOR.md §2 (concept), §2.4 ("Baker
 * contract"). This header has no JUCE dependency so a recipe author / JSON
 * parser can be built and unit-tested without the plugin.
 */
namespace dco {

// One harmonic partial: harmonic number (1-based), amplitude, phase (radians).
struct Partial { int h = 1; float a = 0.0f; float phase = 0.0f; };

// A keyframe = one single-cycle spectrum with a closed-form param->spectrum map.
// The field ranges noted below are DcoBaker's contract — it clamps every field
// defensively before use — so this struct itself stays a plain data holder
// with no invariant enforcement (a recipe may come from an LLM's output).
struct Keyframe {
    enum class Kind { Additive, Saw, Square, Pulse, Triangle, Fm2, Cheby, Ring };
    Kind kind = Kind::Saw;
    std::vector<Partial> partials; // Additive only
    float width = 0.5f;   // Pulse: duty cycle, clamped [0.02, 0.98]
    float ratio = 2.0f;   // Fm2: modulator ratio (clamped to integer 1..8); Ring: partner ratio (integer 1..8)
    float index = 1.0f;   // Fm2: modulation index, clamped [0, 8]
    int   order = 2;      // Cheby: polynomial order, clamped [2, 12]
    float drive = 1.0f;   // Cheby: input amplitude into T_n, clamped [0.1, 1]
    float mix   = 1.0f;   // Ring: ring-mod wet mix vs plain fundamental, clamped [0,1]
};

// Shapes the LOCAL blend position within one motion segment (a in [0,1]).
// Fast = pow(a,0.4) (leaves the source quickly, settles into the target
// early); Slow = pow(a,2.5) (lingers near the source, arrives late).
enum class Curve { Lin, Fast, Slow };

// Motion: segment s morphs FROM the previous segment's target TO keyframe[to]
// over durFrac of the whole trajectory. First entry is the start point (durFrac 0).
struct MotionSegment { int to = 0; float durFrac = 0.0f; Curve curve = Curve::Lin; };

struct Recipe {
    std::vector<Keyframe> keyframes;
    std::vector<MotionSegment> motion;  // if empty: single static keyframe[0]
    bool loop = true;                   // loop=true recipes should END on the start keyframe
    int  frames = 128;                  // bake resolution, clamped [8, 256]
    float motionRateHz = 0.0f;          // full motion loops per second; <= 0 =
                                        // unspecified (consumer picks a fallback)
};

} // namespace dco
