#pragma once
#include <JuceHeader.h>
#include <cmath>

/**
 * Huovilainen 4-pole Moog ladder filter.
 *
 * Reference: Huovilainen 2004, "Non-linear digital implementation of the moog
 * ladder filter" (DAFx-04). The DAFx-04 simplified form needs a half-sample
 * input-delay compensation in the feedback path to resonate and self-oscillate
 * correctly — without it the loop sees one extra sample of delay, poles end
 * up in the wrong position, and the filter clips at high resonance instead
 * of ringing. That compensation is built in below via `xPrev` and a 50/50
 * average between current and previous input.
 *
 * Drive topology (important): in a real Moog, gain is applied *before* the
 * ladder, the four tanh stages each saturate on that hot signal, and output
 * gain compensates on the way out. That's what gives the classic "pushed into
 * the filter" warmth — a single tanh *before* the filter (like our SVF pre-
 * filter drive) would flat-clip the signal and leave nothing for the ladder
 * to shape. So this filter takes `setInputDrive(gain)` separately and applies
 * the inverse on the output tap; upstream code feeds the `filter_drive` knob
 * in here and skips the pre-filter tanh when the ladder is active.
 *
 * Slope switch picks which ladder tap is the output (y1 = 6, y2 = 12, y3 = 18,
 * y4 = 24 dB/oct) — classic Moog multi-mode. HP = input − LP tap, BP = y2 − y4.
 *
 * API mirrors T5ynthFilter so SynthVoice treats all filter models interchangeably.
 */
class MoogLadderFilter
{
public:
    void prepare(double sampleRate, int /*samplesPerBlock*/)
    {
        sr = sampleRate;
        reset();
        updateCoeffs();
    }

    void reset()
    {
        y1 = y2 = y3 = y4 = 0.0f;
        t1 = t2 = t3 = t4 = 0.0f;
        xPrev = 0.0f;
    }

    void setCutoff(float hz)
    {
        if (std::abs(hz - lastCutoff) < 0.5f) return;
        lastCutoff = hz;
        updateCoeffs();
    }

    // Resonance 0..1 → feedback gain k = 4·r. With the half-sample comp in
    // place, k = 4 is the classical self-oscillation point — no cutoff-
    // dependent boost needed. A tiny overshoot (4.2) guarantees audible ring
    // at r = 1 on floating-point.
    void setResonance(float r)
    {
        if (std::abs(r - lastReso) < 0.001f) return;
        lastReso = juce::jlimit(0.0f, 1.0f, r);
        k = 4.2f * lastReso;
    }

    // currentType: 0 = LP, 1 = HP, 2 = BP
    void setType(int type)    { currentType  = juce::jlimit(0, 2, type); }
    void setSlope(int slope)  { currentSlope = juce::jlimit(0, 3, slope); }
    void setMix(float mix)    { currentMix   = juce::jlimit(0.0f, 1.0f, mix); }

    // Pre-filter input gain. The ladder's four tanh stages see the hot signal
    // and saturate on it; that's the audible drive character. No output comp
    // is applied: on a real Moog, drive makes the filter louder + crunchier,
    // not level-matched. Master volume handles overall level.
    void setInputDrive(float gain)
    {
        inDrive = juce::jmax(0.0001f, gain);
    }

    float processSample(float input)
    {
        if (currentMix < 0.001f)
            return input;

        // Pre-filter gain (the ladder's own nonlinearities are the drive).
        const float hot = input * inDrive;

        // Half-sample input delay compensation (Stilson-Smith / Huovilainen).
        // This is the single most important trick for correct resonance.
        const float halfIn = 0.5f * (hot + xPrev);
        xPrev = hot;

        // Feedback is linear on y4 (NOT t4). Bounding the feedback tap with a
        // tanh caps loop gain at k·1, which prevents the resonant peak from
        // ever ringing up: the filter merely "controls something" instead of
        // resonating / self-oscillating. Stability is provided by the tanh at
        // each stage input; the feedback itself needs to be free to grow for
        // the classic Moog peak and self-oscillation to appear.
        const float fbIn = halfIn - k * y4;

        // 4 stacked one-pole stages with tanh saturation at each stage input.
        // Caching tanh(y_i) between samples saves 4 tanh calls per sample.
        const float in0 = std::tanh(fbIn);
        y1 += g * (in0 - t1);  t1 = std::tanh(y1);
        y2 += g * (t1  - t2);  t2 = std::tanh(y2);
        y3 += g * (t2  - t3);  t3 = std::tanh(y3);
        y4 += g * (t3  - t4);  t4 = std::tanh(y4);

        // Tap gain compensates for the half-sample input averaging, which is
        // a cos(ω/2) FIR lowpass (−3 dB at sr/4, 0 at Nyquist). Without it
        // the ladder sits ~1.5 dB below the SVF on broadband material even at
        // matched settings. 1.20 restores rough level parity.
        constexpr float kTapComp = 1.20f;
        const float wet = tapOutput(hot) * kTapComp;

        if (currentMix > 0.999f)
            return wet;
        return input * (1.0f - currentMix) + wet * currentMix;
    }

private:
    void updateCoeffs()
    {
        // Standard Huovilainen coefficient: g = 1 − exp(−2π·fc/fs). More
        // accurate at high cutoff than the sin-based approximation.
        const double wc = 2.0 * juce::MathConstants<double>::pi
                              * static_cast<double>(lastCutoff) / sr;
        g = 1.0f - static_cast<float>(std::exp(-wc));
        g = juce::jlimit(0.0f, 0.99f, g);
    }

    float tapOutput(float driven) const
    {
        const float lp = (currentSlope == 0) ? y1
                       : (currentSlope == 1) ? y2
                       : (currentSlope == 2) ? y3
                       :                       y4;
        switch (currentType)
        {
            case 0: return lp;
            case 1: return driven - lp;   // HP on the driven input so drive
                                          // stays in the HP path too
            case 2: return (y2 - y4);
            default: return lp;
        }
    }

    double sr = 44100.0;

    float y1 = 0.0f, y2 = 0.0f, y3 = 0.0f, y4 = 0.0f;
    float t1 = 0.0f, t2 = 0.0f, t3 = 0.0f, t4 = 0.0f;
    float xPrev = 0.0f;

    float g  = 0.0f;
    float k  = 0.0f;
    float inDrive = 1.0f;
    float lastCutoff = 20000.0f;
    float lastReso   = 0.0f;
    float currentMix = 1.0f;
    int   currentType  = 0;
    int   currentSlope = 3;
};
