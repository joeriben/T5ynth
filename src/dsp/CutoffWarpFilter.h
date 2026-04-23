#pragma once
#include <JuceHeader.h>
#include <cmath>

/**
 * Cutoff-Warp ladder filter.
 *
 * Surge-XT-style ZDF (zero-delay-feedback) 4-pole ladder with a per-pole
 * nonlinearity applied at the feedback input. The style selector picks which
 * saturation curve is used, opening a continuous character space beyond the
 * single tanh response of a classical Moog ladder.
 *
 * Reference topology: Surge XT src/common/dsp/filters/CutoffWarp.cpp (GPL-3).
 * Written from scratch against the standard ZDF-ladder derivation and Surge's
 * documented saturation curves — no code copied.
 *
 * Each one-pole stage is a TPT (topology-preserving transform) integrator:
 *   v = (x − s) × g / (1 + g)
 *   y = v + s
 *   s_new = y + v
 * where g = tan(π · cutoff / sr), computed once per sub-block.
 *
 * Drive topology: same as MoogLadderFilter — input gain before the ladder,
 * the style-selected saturation inside the loop does the shaping, output
 * compensation brings level back down. Upstream code skips the pre-filter
 * tanh when the Warp is the active algorithm and feeds `filter_drive` in
 * through `setInputDrive()` instead.
 *
 * Per-style resonance scaling: each saturation curve has a different local
 * slope near zero (Sin = π/2, Tanh = 1, SoftClip = 1 but drops fast, …) which
 * determines the small-signal loop gain at the resonance frequency. A style-
 * dependent k multiplier evens out the perceived "resonance strength" across
 * styles so Sin doesn't ring at r = 0.25 while SoftClip stays silent at r = 1.
 *
 * Saturation styles must all be monotonic and centered at 0 (sat(0) == 0)
 * so the feedback loop has a well-defined equilibrium. OJD was previously
 * biased and caused DC drift / glitches under modulation; it now subtracts
 * the at-zero offset to guarantee sat(0) = 0. Digital-clip has a hard corner
 * that's deliberate — that's its character.
 */
class CutoffWarpFilter
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
        s1 = s2 = s3 = s4 = 0.0f;
        y1 = y2 = y3 = y4 = 0.0f;
        xPrev = 0.0f;
    }

    void setCutoff(float hz)
    {
        if (std::abs(hz - lastCutoff) < 0.5f) return;
        lastCutoff = hz;
        updateCoeffs();
    }

    void setResonance(float r)
    {
        if (std::abs(r - lastReso) < 0.001f) return;
        lastReso = juce::jlimit(0.0f, 1.0f, r);
        k = 4.2f * lastReso;
    }

    void setType(int type)    { currentType  = juce::jlimit(0, 2, type); }
    void setSlope(int slope)  { currentSlope = juce::jlimit(0, 3, slope); }
    void setMix(float mix)    { currentMix   = juce::jlimit(0.0f, 1.0f, mix); }
    void setStyle(int style)
    {
        currentStyle = juce::jlimit(0, 5, style);
        kStyleScale  = resonanceScaleForStyle(currentStyle);
    }

    // Input gain feeds the ZDF loop's style-selected saturations. No output
    // compensation: drive is intended to raise loudness + harmonic content,
    // not self-cancel. Master volume handles overall level.
    void setInputDrive(float gain)
    {
        inDrive = juce::jmax(0.0001f, gain);
    }

    float processSample(float input)
    {
        if (currentMix < 0.001f)
            return input;

        // Pre-filter gain + half-sample input-delay compensation. Even though
        // this ZDF formulation doesn't *need* the half-sample trick for
        // stability (it's already zero-delay), averaging input with its previous
        // value notably reduces the numerical discontinuity at high resonance
        // when the feedback signal crosses through the style saturation.
        const float hot    = input * inDrive;
        const float halfIn = 0.5f * (hot + xPrev);
        xPrev = hot;

        // Feedback is linear on y4, style-scaled so resonance "strength" is
        // consistent across saturation curves (see kStyleScale). Wrapping the
        // feedback tap in a sat(·) would cap loop gain at k·1 and throttle
        // the resonant peak — character and stability instead come from the
        // per-stage style saturation on each stage's input, where bounding
        // the internal state keeps the ZDF loop well-behaved.
        const float fb = k * kStyleScale * y4;
        y1 = tptStep(s1, sat(halfIn - fb, currentStyle));
        y2 = tptStep(s2, sat(y1, currentStyle));
        y3 = tptStep(s3, sat(y2, currentStyle));
        y4 = tptStep(s4, sat(y3, currentStyle));

        // Denormal guard: add & subtract a tiny offset on the integrator state
        // so sub-normal numbers can't slow the CPU to a crawl in long decays.
        constexpr float antiDenormal = 1.0e-20f;
        s1 += antiDenormal;  s1 -= antiDenormal;
        s2 += antiDenormal;  s2 -= antiDenormal;
        s3 += antiDenormal;  s3 -= antiDenormal;
        s4 += antiDenormal;  s4 -= antiDenormal;

        // Tap gain offsets the cos(ω/2) attenuation of the half-sample input
        // averaging so the Warp sits at roughly the same level as the SVF on
        // broadband material.
        constexpr float kTapComp = 1.20f;
        const float wet = tapOutput(hot) * kTapComp;

        if (currentMix > 0.999f)
            return wet;
        return input * (1.0f - currentMix) + wet * currentMix;
    }

private:
    void updateCoeffs()
    {
        // tan() blows up as we approach Nyquist, so clamp w to just below π/2.
        const double w = juce::MathConstants<double>::pi
                         * static_cast<double>(lastCutoff) / sr;
        const double wClamped = juce::jlimit(1.0e-4,
                                             juce::MathConstants<double>::halfPi - 1.0e-4,
                                             w);
        const double t = std::tan(wClamped);
        g     = static_cast<float>(t);
        gFrac = g / (1.0f + g);
    }

    float tptStep(float& s, float x)
    {
        const float v = (x - s) * gFrac;
        const float y = v + s;
        s = y + v;
        return y;
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
            case 1: return driven - lp;
            case 2: return (y2 - y4);
            default: return lp;
        }
    }

    // Per-style resonance scaling. The effective loop gain at the resonance
    // frequency is proportional to the product of four sat'(·) values at the
    // stages' operating points plus the feedback coefficient. Styles whose
    // derivative near zero is higher (Sin = π/2) reach self-oscillation at a
    // lower nominal k than styles whose derivative rolls off fast (SoftClip).
    // These multipliers were tuned by ear so r = 1 gives a clear ring / edge-
    // of-self-oscillation on every style and r ≈ 0.25 stays tame on Sin.
    static float resonanceScaleForStyle(int style)
    {
        switch (style)
        {
            case 0: return 1.00f;  // Tanh — reference
            case 1: return 1.35f;  // SoftClip — slope drops fastest, needs more k
            case 2: return 1.10f;  // OJD — slightly softer than Tanh
            case 3: return 0.65f;  // Sin — slope π/2 at zero, ring is naturally strong
            case 4: return 1.00f;  // Digital — linear up to ±1, same as Tanh there
            case 5: return 1.00f;  // Asym — behaves like Tanh near zero after bias
        }
        return 1.0f;
    }

    // Saturation functions indexed by FilterWarpStyle::*. All must be
    // monotonic, bounded, and sat(0) == 0 so the ZDF loop has a clean
    // zero equilibrium.
    static float sat(float x, int style)
    {
        switch (style)
        {
            case 0: // Tanh — symmetric soft, classic
                return std::tanh(x);

            case 1: // SoftClip — x / (1 + |x|), gentler than tanh
                return x / (1.0f + std::abs(x));

            case 2: // OJD-like — x / sqrt(1 + x²), slower approach to ±1 than
                    // tanh, more odd-harmonic content for the same saturation.
                    // Kept centered at 0 (no DC bias) so the feedback loop
                    // can't drift; asymmetry lives in "Asym" (style 5).
                return x / std::sqrt(1.0f + x * x);

            case 3: // Sin — wave-folding, FM-like timbre
            {
                const float arg = juce::jlimit(-juce::MathConstants<float>::halfPi,
                                                juce::MathConstants<float>::halfPi,
                                                x * juce::MathConstants<float>::halfPi);
                return std::sin(arg);
            }

            case 4: // Digital — hard clamp, buzzy when driven
                return juce::jlimit(-1.0f, 1.0f, x);

            case 5: // Asym — asymmetric tanh with bias-compensation so sat(0)=0,
                    // retaining 2nd-harmonic content.
            {
                constexpr float bias    = 0.25f;
                constexpr float biasOff = 0.24491866f; // std::tanh(0.25)
                return std::tanh(x + bias) - biasOff;
            }
        }
        return std::tanh(x);
    }

    double sr = 44100.0;

    float s1 = 0.0f, s2 = 0.0f, s3 = 0.0f, s4 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f, y3 = 0.0f, y4 = 0.0f;
    float xPrev = 0.0f;

    float g     = 0.0f;
    float gFrac = 0.0f;
    float k     = 0.0f;
    float kStyleScale = 1.0f;
    float inDrive = 1.0f;
    float lastCutoff = 20000.0f;
    float lastReso   = 0.0f;
    float currentMix = 1.0f;
    int   currentType  = 0;
    int   currentSlope = 3;
    int   currentStyle = 0;
};
