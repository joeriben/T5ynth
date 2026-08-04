#pragma once
#include <JuceHeader.h>

/**
 * The output ceiling: memoryless, and only where there is a converter to
 * protect.
 *
 * WHAT WAS HERE BEFORE, and why it is gone. This file wrapped
 * `juce::dsp::Limiter` and called it "always on, internal safety". It is not a
 * limiter. Its `update()` (juce_Limiter.cpp) builds TWO compressors: a 4:1
 * stage at a FIXED -10 dBFS with a 200 ms release, which the "Limiter
 * Threshold" control never reaches, then the brickwall at that threshold, then
 * a makeup of 10^(7.5/40) * 10^(-threshold/20) -- +6.75 dB at the shipped
 * -3 dB. So the instrument's output level was borrowed from a compressor that
 * was permanently working across the whole voice sum, and every new note took
 * some of it away from every note already sounding.
 *
 * BJ heard that as the synth being paraphonic, 2026-08-04: *„Offensichtlich ist
 * die Filter-Envelope nicht polyphon. es verhaelt sich paraphon … ich hoere SEHR
 * deutlich etwas das alle gehaltenen noten betrifft."* He was hearing this
 * stage. `tools/measure_poly_independence.cpp` is the measurement: the filter
 * envelope is per-voice and provably so -- take the level out of this stage and
 * a held note's own partials move by exactly -0.60 dB when a second note
 * arrives, which is VoiceManager's 1/N^0.1 voice-count compensation and nothing
 * else, with a superposition residual of -23.5 dB. Leave the level in it and
 * the same held note drops 5.7 dB the instant the second note sounds, and is
 * still 3.4 dB down half a second later.
 *
 * WHY MEMORYLESS. Polyphonic means the filter and the VCA belong to the voice;
 * paraphonic means a stage is shared. A brickwall with a release time is a
 * shared stage whatever it is called: when it engages it pulls every voice down
 * and lets them back up on its own clock. The analog polysynths this
 * instrument descends from bound their output with a summing/output amplifier
 * that saturates INSTANTLY -- a big chord intermodulates and thickens, and no
 * held note is held down afterwards, because there is no afterwards. That is
 * the difference between the character large chords have on a Prophet-5 and
 * what BJ reported hearing here.
 *
 * METHOD AND SOURCE, before the first line. Memoryless soft clipping with an
 * exactly linear region: below `kKnee` the sample is returned untouched, above
 * it the excess travels a `tanh` scaled to reach the ceiling asymptotically.
 * The textbook saturator (Udo Zoelzer (ed.), DAFX: Digital Audio Effects, 2nd
 * ed., Wiley 2011, ch. 5, nonlinear processing), and the same construction
 * `T5ynthTremolo` in AmpEffects.h already uses for its rounded square. The
 * curve is C1 at the knee -- tanh'(0) = 1 on both sides, so the transition into
 * saturation has no corner in its slope -- and cannot exceed 1.0 for any finite
 * input, because tanh cannot exceed 1.
 *
 * WHY STANDALONE ONLY. BJ, 2026-08-05, on where the defect actually sits:
 * *„dann macht unsere Standalone etwas NICHT das eine DAW macht, und EXAKT das
 * ist der Fehler."* A host does not compress its master bus; it carries float,
 * lets a plugin exceed 0 dBFS, and leaves the level to the player. The
 * standalone hands its buffer to CoreAudio, where the converter clips hard, so
 * IT needs a bound -- and only it. Both used to get the same stage because both
 * run the same `processBlock`, so the plugin was carrying the standalone's
 * converter protection around with it. `AudioProcessor::wrapperType` is what
 * separates them, and PluginProcessor's master section is where the gate sits.
 *
 * WHAT IT IS NOT, measured rather than claimed, because the word "soft" oversells
 * a memoryless stage with a fixed ceiling and it is worth being exact. The knee
 * sits at 0.9 so that normal play passes bit-identical -- BJ's own requirement,
 * *„so gesetzt, dass sie im Spiel nie greift"* -- and that leaves only 0.1 of
 * amplitude for the whole curve. So it is gentle just above the knee (an input
 * of 1.0 leaves as 0.976, of 1.2 as 0.9995) and by +4.56 dBFS in it returns
 * exactly 1.0, i.e. a flat top. That is not a defect of the curve, it is
 * arithmetic: no memoryless stage can take 5 dB off a peak and leave the
 * waveform alone. A signal that arrives that hot is a GAIN STAGING problem and
 * is named as one in PluginProcessor's own gain-staging block; this stage's job
 * is to keep the converter safe, not to make an over sound good.
 *
 * NON-FINITE SAMPLES ARE A SEPARATE JOB, and it runs in every wrapper type. The
 * widget this replaces ended with an unconditional
 * `FloatVectorOperations::clip(-1, 1)` (juce_Limiter.h), so an Inf out of a
 * blown-up filter or orchestra used to arrive at the host as 1.0. Removing the
 * stage from plugin builds is deliberate for LEVEL and was not meant to hand a
 * host an Inf, so `scrubNonFinite` keeps that half. It also catches NaN, which
 * the widget's clip did not.
 *
 * NO STATE, so nothing to prepare and nothing to reset: the output for a sample
 * depends on that sample alone. That is the property this class exists for.
 */
class OutputCeiling
{
public:
    /** Below this the signal is returned bit-identical: -0.9 dBFS. */
    static constexpr float kKnee = 0.9f;

    /** The curve, memoryless. Public because a test can then check the two
        properties this stage promises -- linear below the knee, bounded by 1.0
        above it -- without going through a buffer. */
    static float shape (float x) noexcept;

    /** Replaces every non-finite sample with silence. Separate from the ceiling
        because it is needed WHEREVER this runs, plugin included -- see the
        paragraph above. */
    static void scrubNonFinite (juce::AudioBuffer<float>& buffer) noexcept;

    void processBlock (juce::AudioBuffer<float>& buffer) const noexcept;
};
