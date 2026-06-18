#pragma once
#include <JuceHeader.h>

/**
 * Delay effect with four routing/voicing modes (mirrors DelayType in
 * BlockParams.h; Off is handled by the caller — the delay is simply not run):
 *
 *   1 DualMono ("2M")  : two independent lines, identical time, no coupling.
 *                        The original behaviour; preset key "stereo".
 *   2 Stereo   ("St")  : L/R time offset (±kStereoSpread) → width from a mono
 *                        source. No cross-feedback. Longer side stays within
 *                        the buffer (see kMaxDelaySeconds).
 *   3 Cross            : cross-coupled feedback (L line fed from R's output and
 *                        vice versa) → echoes migrate across the stereo field.
 *   4 Tape             : dual-mono routing plus a "tape" feedback voicing —
 *                        high-pass + low-pass (Damp) band-limiting, a soft
 *                        tanh saturation that auto-limits runaway feedback,
 *                        and a subtle wow (slow read-position drift). No new
 *                        user parameter: drive scales with the signal, tone
 *                        rides the existing Damp control.
 *
 * Common controls: time (ms, smoothed), feedback, dry/wet mix, feedback-damping
 * low-pass. Damping: 0 = bright (20kHz), 1 = dark (500Hz). Mix is a true
 * crossfade: out = dry*(1-mix) + wet*mix; at mix=1 the dry path vanishes.
 */
class T5ynthDelayLine
{
public:
    T5ynthDelayLine() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void processBlock(juce::AudioBuffer<float>& buffer);
    void reset();

    /** Set delay time in milliseconds (smoothed per-sample, clamped to buffer). */
    void setTime(float ms);

    /** Set feedback amount (0-0.95, smoothed per-sample). */
    void setFeedback(float fb);

    /** Set dry/wet mix (0=dry, 1=wet). True crossfade: at mix=1 dry vanishes. */
    void setMix(float mix);

    /** Set feedback damping (0=bright 20kHz, 1=dark 500Hz). */
    void setDamp(float d);

    /** Routing/voicing mode = DelayType value (1=DualMono, 2=Stereo,
        3=Cross, 4=Tape). Off (0) is handled by the caller. */
    void setMode(int delayType);

    float getMix() const { return wetMix; }

private:
    // Local mirror of BlockParams DelayType (avoids including BlockParams here).
    enum Mode { kDualMono = 1, kStereo = 2, kCross = 3, kTape = 4 };

    // Capacity is set SR-aware in prepare(); the constructor value is a
    // placeholder that is reallocated before any audio runs.
    juce::dsp::DelayLine<float> delayLine { 96000 };

    // Feedback damping low-pass (all modes) + tape high-pass (Tape only).
    juce::dsp::IIR::Filter<float> dampFilterL, dampFilterR;
    juce::dsp::IIR::Filter<float> tapeHpL, tapeHpR;

    double sr = 44100.0;
    int   mode = kDualMono;
    float delayTimeMs = 250.0f;          // Reference default
    float targetDelaySamples = 0.0f;     // smoothing target
    float currentDelaySamples = 0.0f;    // smoothed current
    float feedback = 0.35f;              // Reference default
    float targetFeedback = 0.35f;        // smoothing target
    float wetMix = 0.3f;                 // Reference default (send amount)
    float dampFreq = 4000.0f;            // Default at damp=0.5
    float maxDelaySamples = 0.0f;        // read-position guard (set in prepare)
    bool prepared = false;

    // Tape wow: slow sine LFO phase driving a tiny read-position drift.
    float wowPhase = 0.0f;

    // Silence detection — skip processing only after output has truly decayed
    int silentOutputBlocks = 0;
    static constexpr int SILENCE_CONFIRM_BLOCKS = 4;

    void updateDampCoeffs();
};
