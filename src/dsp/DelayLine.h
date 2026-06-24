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
 *   3 Tape     : 3-head tape echo (Roland RE-201's 1:2:3 head spacing). Read
 *                taps at T, 2T, 3T summed and spread across the field; feedback
 *                from the long head through a high-pass + damping low-pass + soft
 *                tanh saturation, with a wow+flutter read-position drift applied
 *                multiplicatively per head (excursion + pitch scale 1:2:3). The
 *                playback is ALSO rolled off (every repeat, like a real playback
 *                head) so darkening doesn't arrive only via feedback build-up.
 *   4 BBD      : bucket-brigade (analog) echo. Single tap, a steep 3-pole
 *                reconstruction low-pass (dark, ~3kHz top) + mid-focus high-pass
 *                + gentle companding grit, with a subtle clock-drift warble.
 *                Darker, grittier and less pitch-mobile than tape; mono/centred.
 *
 * Common controls: time (ms, smoothed), feedback, dry/wet mix, damping low-pass
 * (0..1 trim; per-mode top — Tape starts at a ~9kHz baseline that compounds per
 * feedback pass, Digital/PingPong stay open at 20kHz, BBD trims from its ~3kHz
 * recon top; all reach 500Hz dark). Mix is a true crossfade: mix=1 drops the dry.
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

    /** Set feedback damping trim (0=open, 1=dark). Per-mode top: Tape starts at
        an intrinsic ~9kHz baseline; Digital/PingPong fully open (20kHz); BBD from
        its ~3kHz recon top. Tape also tracks Damp on its playback rolloff. */
    void setDamp(float d);

    /** Routing/voicing mode = DelayType value (1=Digital, 2=PingPong,
        3=Tape, 4=BBD). Off (0) is handled by the caller. */
    void setMode(int delayType);

    float getMix() const { return wetMix; }

private:
    // Local mirror of BlockParams DelayType (avoids including BlockParams here).
    enum Mode { kDigital = 1, kPingPong = 2, kTape = 3, kBbd = 4 };

    // Capacity is set SR-aware in prepare(); the constructor value is a
    // placeholder reallocated before any audio runs.
    juce::dsp::DelayLine<float> delayLine { 96000 };

    // Feedback damping low-pass: one per channel (Digital/PingPong); Tape uses
    // dampFilterL on its mono feedback.
    juce::dsp::IIR::Filter<float> dampFilterL, dampFilterR;

    // Tape PLAYBACK rolloff: rolls off every repeat (incl. the first) like a real
    // playback head, so darkening isn't only a feedback build-up. Tracks Damp but
    // bottoms at a higher floor than the feedback (kTapePbFloorHz) — Tape only.
    juce::dsp::IIR::Filter<float> tapePbFilterL, tapePbFilterR;

    double sr = 44100.0;
    int   mode = kDigital;
    float delayTimeMs = 250.0f;          // Reference default
    float targetDelaySamples = 0.0f;     // smoothing target
    float currentDelaySamples = 0.0f;    // smoothed current
    float feedback = 0.35f;              // Reference default
    float targetFeedback = 0.35f;        // smoothing target
    float wetMix = 0.3f;                 // Reference default (send amount)
    float dampFreq = 4000.0f;            // resolved feedback-LP cutoff (per-mode top)
    float tapePbFreq = 4000.0f;          // resolved tape playback-LP cutoff
    float dampAmount = -1.0f;            // raw 0..1 Damp param (-1 = force first resolve)
    float maxDelaySamples = 0.0f;        // read-position guard (set in prepare)
    bool prepared = false;

    // Tape character state (Tape mode only)
    float tapeHpState = 0.0f;            // one-pole high-pass state
    float wow1Phase = 0.0f, wow2Phase = 0.0f, flutPhase = 0.0f;

    // BBD (bucket-brigade) character state (kBbd only)
    float bbdRecon1 = 0.0f, bbdRecon2 = 0.0f, bbdRecon3 = 0.0f; // 3-pole recon cascade
    float bbdHpState = 0.0f;             // mid-focus high-pass state
    float bbdClockPhase = 0.0f;          // subtle clock-drift LFO phase
    float bbdReconFreq = 3000.0f;        // recon cutoff (Damp-tracked from ~3kHz top)
    float bbdReconCoeff = 0.0f;          // one-pole coeff for the recon cascade
    float bbdHpCoeff = 0.0f;             // one-pole coeff for the mid-focus HP

    // Silence detection — skip processing only after output has truly decayed
    int silentOutputBlocks = 0;
    static constexpr int SILENCE_CONFIRM_BLOCKS = 4;

    void updateDampCoeffs();
    void updateTapePbCoeffs();   // rebuild tape playback-LP from tapePbFreq
    void recomputeDamp();   // resolve per-mode damp cutoffs from dampAmount + mode
};
