#pragma once
#include "DcoRecipe.h"
#include <JuceHeader.h>
#include <vector>

namespace dco {

/**
 * Deterministic offline baker: renders a Recipe's closed-form keyframe
 * spectra and its motion trajectory into single-cycle wavetable frames.
 *
 * Message/background-thread only — no RT constraints. Mirrors the existing
 * neural generate->deliver spine (author + render off-thread; the audio
 * thread only ever plays pre-baked frames via WavetableOscillator). See
 * docs/HANDOVER_DCO_OSCILLATOR.md §2.4 ("Baker contract") and §4.2.
 *
 * Every keyframe is rendered FULL BANDWIDTH up to kMaxHarmonics (the frame's
 * Nyquist) — this baker does NOT pre-band-limit per octave. WavetableOscillator
 * owns the entire anti-aliasing strategy: its generateMipLevels() derives every
 * band-limited mip level from level 0 by FFT brick-wall filtering. Any
 * band-limiting done here would just be discarded once the frames reach
 * WavetableOscillator::extractContiguousFrames.
 */
class Baker
{
public:
    static constexpr int kFrameSize = 2048;    // MUST match WavetableOscillator::FRAME_SIZE
    static constexpr int kMaxHarmonics = 1024; // frame Nyquist; engine mips handle per-note band-limiting

    /** Bake the recipe into recipe.frames (clamped [8,256]) single-cycle frames,
     *  each kFrameSize samples. Frames are globally normalized (one gain, loudest
     *  frame peaks at 0.95) as a range-safety step — note the live consumer,
     *  WavetableOscillator::extractContiguousFrames, renormalizes every frame
     *  independently, so per-frame morph level differences do not survive
     *  playback (see the note in bake()'s implementation). A recipe with no
     *  keyframes bakes to silence (zeroed frames) rather than dividing by zero. */
    static std::vector<std::vector<float>> bake(const Recipe& recipe);

    /** Convenience: lay frames end-to-end into a mono juce::AudioBuffer<float>
     *  (frames.size() * kFrameSize samples), ready for
     *  WavetableOscillator::extractContiguousFrames(buf, sr, 0, 1). */
    static juce::AudioBuffer<float> framesToBuffer(const std::vector<std::vector<float>>& frames);

private:
    // One rendered single-cycle keyframe: double precision, DC-removed, NOT
    // yet globally normalized (that happens once, across all baked frames).
    using Cycle = std::vector<double>;

    static Cycle renderKeyframe(const Keyframe& kf);
    static Cycle renderAdditive(const Keyframe& kf);
    static Cycle renderSaw();
    static Cycle renderSquare();
    static Cycle renderTriangle();
    static Cycle renderPulse(const Keyframe& kf);
    static Cycle renderFm2(const Keyframe& kf);
    static Cycle renderCheby(const Keyframe& kf);
    static Cycle renderRing(const Keyframe& kf);

    // Subtract the cycle's mean. Applied to every rendered Kind (Baker
    // contract): a no-op for the harmonic-series kinds and Fm2 (zero-mean by
    // construction — Fm2's Jacobi-Anger expansion has no constant term), and
    // load-bearing for Cheby (even orders fold in real DC at drive < 1) and
    // Ring at ratio 1 (sin(x)*sin(x) has a DC half).
    static void removeDC(Cycle& cycle);

    // Shape a local blend position a in [0,1] by the segment's motion Curve.
    static double shapeCurve(double a, Curve curve);
};

} // namespace dco
