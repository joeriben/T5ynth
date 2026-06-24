#pragma once
#include <JuceHeader.h>

/**
 * Delay effect with four voicings (mirrors DelayType in BlockParams.h; Off is
 * handled by the caller — the delay is simply not run). The algorithm was
 * prototyped and auditioned in tools/delay_audition.py before this port.
 *
 *   1 Digital  : clean dual-mono. Two independent lines, identical time, a
 *                damping low-pass in each feedback path. Transparent and
 *                mono-compatible. The original behaviour; preset key "stereo".
 *   2 PingPong : true ping-pong. Input summed to mono and injected into the
 *                LEFT line only; feedback cross-coupled (left output → right
 *                line, right output → left line). A mono source bounces cleanly
 *                L/R/L/R at the set time; the dry stays put.
 *   3 Tape2    : 2-head tape echo. Read taps at T and 2T summed and spread
 *                across the field; feedback from the long head through a
 *                high-pass + damping low-pass + soft tanh saturation, with a
 *                wow+flutter read-position drift scaled to the delay time.
 *   4 Tape3    : as Tape2 but three heads at T, 2T, 3T — the Roland RE-201's
 *                1:2:3 head spacing.
 *
 * Common controls: time (ms, smoothed), feedback, dry/wet mix, damping low-pass
 * (0 = bright 20kHz, 1 = dark 500Hz). Mix is a true crossfade: at mix=1 the dry
 * path vanishes.
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

    /** Routing/voicing mode = DelayType value (1=Digital, 2=PingPong,
        3=Tape2, 4=Tape3). Off (0) is handled by the caller. */
    void setMode(int delayType);

    float getMix() const { return wetMix; }

private:
    // Local mirror of BlockParams DelayType (avoids including BlockParams here).
    enum Mode { kDigital = 1, kPingPong = 2, kTape2 = 3, kTape3 = 4 };

    // Capacity is set SR-aware in prepare(); the constructor value is a
    // placeholder reallocated before any audio runs.
    juce::dsp::DelayLine<float> delayLine { 96000 };

    // Feedback damping low-pass: one per channel (Digital/PingPong); Tape uses
    // dampFilterL on its mono feedback.
    juce::dsp::IIR::Filter<float> dampFilterL, dampFilterR;

    double sr = 44100.0;
    int   mode = kDigital;
    float delayTimeMs = 250.0f;          // Reference default
    float targetDelaySamples = 0.0f;     // smoothing target
    float currentDelaySamples = 0.0f;    // smoothed current
    float feedback = 0.35f;              // Reference default
    float targetFeedback = 0.35f;        // smoothing target
    float wetMix = 0.3f;                 // Reference default (send amount)
    float dampFreq = 4000.0f;            // Default at damp=0.5
    float maxDelaySamples = 0.0f;        // read-position guard (set in prepare)
    bool prepared = false;

    // Tape character state (Tape2/Tape3 only)
    float tapeHpState = 0.0f;            // one-pole high-pass state
    float wow1Phase = 0.0f, wow2Phase = 0.0f, flutPhase = 0.0f;

    // Silence detection — skip processing only after output has truly decayed
    int silentOutputBlocks = 0;
    static constexpr int SILENCE_CONFIRM_BLOCKS = 4;

    void updateDampCoeffs();
};
