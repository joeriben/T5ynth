#include "DcoBaker.h"
#include "WavetableOscillator.h"

#include <algorithm>
#include <cmath>

namespace dco {

// The baked frame size must match the engine that plays these frames back —
// a silent mismatch here would truncate or garbage-read every frame.
static_assert(Baker::kFrameSize == WavetableOscillator::FRAME_SIZE,
              "DcoBaker frame size must match WavetableOscillator::FRAME_SIZE");

// ─── DC removal / curve shaping ───

void Baker::removeDC(Cycle& cycle)
{
    if (cycle.empty()) return;
    double mean = 0.0;
    for (double s : cycle) mean += s;
    mean /= static_cast<double>(cycle.size());
    for (double& s : cycle) s -= mean;
}

void Baker::applyShape(Cycle& cycle, float shape)
{
    // NaN-safe: jlimit(0,1,NaN) returns NaN and NaN<=0 is false, so an unguarded
    // NaN shape would poison the whole cycle (and survive bake()'s normalizer).
    // Same isfinite guard renderFm2/renderRing use before their lround.
    const double sv = std::isfinite(static_cast<double>(shape)) ? static_cast<double>(shape) : 0.0;
    const double s = juce::jlimit(0.0, 1.0, sv);
    if (s <= 0.0) return;
    // ASYMMETRIC soft-clip (tanh with a DC bias into the nonlinearity). A plain
    // symmetric tanh is odd-only -> it drives ANY rich source toward a hollow
    // square (it SUPPRESSES the even harmonics of e.g. a saw), which reads as
    // "thinner", the opposite of "distorted". The bias makes the transfer curve
    // asymmetric so the shaper generates EVEN harmonics too -> the warm, dense,
    // gritty character of real analog overdrive (tubes/diodes/transistors are
    // all asymmetric). tanh is self-bounding [-1,1] so no explicit renorm is
    // needed (the global bake normalize + per-frame engine renorm set level);
    // removeDC() strips the constant offset the bias adds -- the even HARMONICS
    // (h2,h4,... at nonzero frequency) are not DC and survive. Audition-tuned:
    // gain 6 spans gentle->hard over shape 0..1, bias 0.25*s keeps the low-shape
    // end nearly symmetric and only opens the even content as drive increases.
    const double drive = 1.0 + s * 4.0;
    const double bias  = 0.2 * s;
    for (double& x : cycle)
        x = std::tanh(drive * (x + bias));
    removeDC(cycle);
}

double Baker::shapeCurve(double a, Curve curve)
{
    a = juce::jlimit(0.0, 1.0, a);
    switch (curve)
    {
        case Curve::Lin:  return a;
        case Curve::Fast: return std::pow(a, 0.4);
        case Curve::Slow: return std::pow(a, 2.5);
    }
    return a; // unreachable for valid Curve values; keeps -Wreturn-type quiet defensively
}

// ─── Keyframe rendering (closed-form, double precision) ───
//
// Every render* function returns one kFrameSize-sample single cycle at the
// canonical phase x[n] = 2*pi*n/kFrameSize, n in [0, kFrameSize). Harmonic
// series are summed up to kMaxHarmonics (the frame's Nyquist bin) — full
// bandwidth, undecimated; see the class doc for why that's correct here.
// removeDC() is applied uniformly at the end of every render* (Baker
// contract) — see its own doc comment for why.

Baker::Cycle Baker::renderAdditive(const Keyframe& kf)
{
    // Explicit partial sum: a * sin(h*x + phase). h is clamped into
    // [1, kMaxHarmonics] per partial (each partial names its own harmonic,
    // independent of any series index).
    Cycle cyc(static_cast<size_t>(kFrameSize), 0.0);
    for (int n = 0; n < kFrameSize; ++n)
    {
        const double x = juce::MathConstants<double>::twoPi * n / kFrameSize;
        double sum = 0.0;
        for (const auto& p : kf.partials)
        {
            const int h = juce::jlimit(1, kMaxHarmonics, p.h);
            sum += static_cast<double>(p.a)
                 * std::sin(static_cast<double>(h) * x + static_cast<double>(p.phase));
        }
        cyc[static_cast<size_t>(n)] = sum;
    }
    removeDC(cyc);
    return cyc;
}

Baker::Cycle Baker::renderSaw()
{
    // Rising sawtooth Fourier series: sum_{h=1..H} (2/(pi*h)) * (-1)^(h+1) * sin(h*x).
    Cycle cyc(static_cast<size_t>(kFrameSize), 0.0);
    std::vector<double> coeff(static_cast<size_t>(kMaxHarmonics) + 1, 0.0);
    for (int h = 1; h <= kMaxHarmonics; ++h)
    {
        const double sign = (h % 2 == 1) ? 1.0 : -1.0;
        coeff[static_cast<size_t>(h)] = (2.0 / (juce::MathConstants<double>::pi * h)) * sign;
    }
    for (int n = 0; n < kFrameSize; ++n)
    {
        const double x = juce::MathConstants<double>::twoPi * n / kFrameSize;
        double sum = 0.0;
        for (int h = 1; h <= kMaxHarmonics; ++h)
            sum += coeff[static_cast<size_t>(h)] * std::sin(static_cast<double>(h) * x);
        cyc[static_cast<size_t>(n)] = sum;
    }
    removeDC(cyc);
    return cyc;
}

Baker::Cycle Baker::renderSquare()
{
    // Band-limited square: sum over odd h of (4/(pi*h)) * sin(h*x).
    Cycle cyc(static_cast<size_t>(kFrameSize), 0.0);
    std::vector<double> coeff(static_cast<size_t>(kMaxHarmonics) + 1, 0.0);
    for (int h = 1; h <= kMaxHarmonics; h += 2)
        coeff[static_cast<size_t>(h)] = 4.0 / (juce::MathConstants<double>::pi * h);
    for (int n = 0; n < kFrameSize; ++n)
    {
        const double x = juce::MathConstants<double>::twoPi * n / kFrameSize;
        double sum = 0.0;
        for (int h = 1; h <= kMaxHarmonics; h += 2)
            sum += coeff[static_cast<size_t>(h)] * std::sin(static_cast<double>(h) * x);
        cyc[static_cast<size_t>(n)] = sum;
    }
    removeDC(cyc);
    return cyc;
}

Baker::Cycle Baker::renderTriangle()
{
    // Band-limited triangle: sum over odd h of (8/pi^2) * ((-1)^((h-1)/2)/h^2) * sin(h*x).
    Cycle cyc(static_cast<size_t>(kFrameSize), 0.0);
    std::vector<double> coeff(static_cast<size_t>(kMaxHarmonics) + 1, 0.0);
    const double k = 8.0 / (juce::MathConstants<double>::pi * juce::MathConstants<double>::pi);
    bool posSign = true;
    for (int h = 1; h <= kMaxHarmonics; h += 2)
    {
        coeff[static_cast<size_t>(h)] = (posSign ? 1.0 : -1.0) * k / (static_cast<double>(h) * h);
        posSign = !posSign;
    }
    for (int n = 0; n < kFrameSize; ++n)
    {
        const double x = juce::MathConstants<double>::twoPi * n / kFrameSize;
        double sum = 0.0;
        for (int h = 1; h <= kMaxHarmonics; h += 2)
            sum += coeff[static_cast<size_t>(h)] * std::sin(static_cast<double>(h) * x);
        cyc[static_cast<size_t>(n)] = sum;
    }
    removeDC(cyc);
    return cyc;
}

Baker::Cycle Baker::renderPulse(const Keyframe& kf)
{
    // Bipolar pulse, COS-phase series: sum_{h=1..H} (4/(pi*h)) * sin(pi*h*w) * cos(h*x).
    // DC is absent from the closed form by construction; removeDC() below only
    // mops up float round-off.
    //
    // NOTE the cos-phase basis, vs. sin-phase for Saw/Square/Triangle above —
    // a Pulse<->Saw morph therefore blends across two different phase bases.
    // Accepted v1 behavior (design doc §2.4); tools/audition_dco_bake.cpp's
    // ear-check (morphloop, pwm) validates it doesn't read as broken.
    const double w = juce::jlimit(0.02, 0.98, static_cast<double>(kf.width));
    Cycle cyc(static_cast<size_t>(kFrameSize), 0.0);
    std::vector<double> coeff(static_cast<size_t>(kMaxHarmonics) + 1, 0.0);
    for (int h = 1; h <= kMaxHarmonics; ++h)
        coeff[static_cast<size_t>(h)] = (4.0 / (juce::MathConstants<double>::pi * h))
                                       * std::sin(juce::MathConstants<double>::pi * h * w);
    for (int n = 0; n < kFrameSize; ++n)
    {
        const double x = juce::MathConstants<double>::twoPi * n / kFrameSize;
        double sum = 0.0;
        for (int h = 1; h <= kMaxHarmonics; ++h)
            sum += coeff[static_cast<size_t>(h)] * std::cos(static_cast<double>(h) * x);
        cyc[static_cast<size_t>(n)] = sum;
    }
    removeDC(cyc);
    return cyc;
}

Baker::Cycle Baker::renderFm2(const Keyframe& kf)
{
    // Time-domain 2-op FM: y(x) = sin(x + I*sin(r*x)). Integer r keeps the
    // waveform exactly periodic over one fundamental cycle. Bessel sideband
    // energy above harmonic kMaxHarmonics is negligible for index<=8, ratio<=8
    // (the design doc's closed-form-2-op-FM boundary, §2.2), so this needs no
    // separate truncation/band-limiting step.
    // Clamp the float BEFORE lround: lround(NaN/huge) is UB, and a malformed
    // recipe can carry either.
    const int r = static_cast<int>(std::lround(juce::jlimit(1.0, 8.0,
        std::isfinite(kf.ratio) ? static_cast<double>(kf.ratio) : 1.0)));
    const double idx = juce::jlimit(0.0, 8.0, static_cast<double>(kf.index));
    Cycle cyc(static_cast<size_t>(kFrameSize), 0.0);
    for (int n = 0; n < kFrameSize; ++n)
    {
        const double x = juce::MathConstants<double>::twoPi * n / kFrameSize;
        cyc[static_cast<size_t>(n)] = std::sin(x + idx * std::sin(static_cast<double>(r) * x));
    }
    removeDC(cyc);
    return cyc;
}

Baker::Cycle Baker::renderCheby(const Keyframe& kf)
{
    // Chebyshev waveshaper: y(x) = T_n(d * sin(x)), T_n via the standard
    // recurrence T_0=1, T_1=t, T_k = 2*t*T_{k-1} - T_{k-2}. Even orders fold a
    // DC component into T_n(t) — removeDC() below strips it.
    const int order = juce::jlimit(2, 12, kf.order);
    const double drive = juce::jlimit(0.1, 1.0, static_cast<double>(kf.drive));
    Cycle cyc(static_cast<size_t>(kFrameSize), 0.0);
    for (int n = 0; n < kFrameSize; ++n)
    {
        const double x = juce::MathConstants<double>::twoPi * n / kFrameSize;
        const double t = drive * std::sin(x);
        double tPrev2 = 1.0;  // T_0(t)
        double tPrev1 = t;    // T_1(t)
        double tCur = tPrev1;
        for (int k = 2; k <= order; ++k)
        {
            tCur = 2.0 * t * tPrev1 - tPrev2;
            tPrev2 = tPrev1;
            tPrev1 = tCur;
        }
        cyc[static_cast<size_t>(n)] = tCur;
    }
    removeDC(cyc);
    return cyc;
}

Baker::Cycle Baker::renderRing(const Keyframe& kf)
{
    // Ring mod against a harmonic partner: y(x) = (1-m)*sin(x) + m*sin(x)*sin(r*x).
    // Integer r keeps the sum/difference tones (r+1, r-1) exact harmonics of x.
    // Same NaN-safe clamp-before-lround as renderFm2.
    const int r = static_cast<int>(std::lround(juce::jlimit(1.0, 8.0,
        std::isfinite(kf.ratio) ? static_cast<double>(kf.ratio) : 1.0)));
    const double mix = juce::jlimit(0.0, 1.0, static_cast<double>(kf.mix));
    Cycle cyc(static_cast<size_t>(kFrameSize), 0.0);
    for (int n = 0; n < kFrameSize; ++n)
    {
        const double x = juce::MathConstants<double>::twoPi * n / kFrameSize;
        const double fundamental = std::sin(x);
        const double partner = std::sin(static_cast<double>(r) * x);
        cyc[static_cast<size_t>(n)] = (1.0 - mix) * fundamental + mix * fundamental * partner;
    }
    removeDC(cyc);
    return cyc;
}

Baker::Cycle Baker::renderKeyframe(const Keyframe& kf)
{
    Cycle c;
    switch (kf.kind)
    {
        case Keyframe::Kind::Additive: c = renderAdditive(kf); break;
        case Keyframe::Kind::Saw:      c = renderSaw();        break;
        case Keyframe::Kind::Square:   c = renderSquare();     break;
        case Keyframe::Kind::Pulse:    c = renderPulse(kf);    break;
        case Keyframe::Kind::Triangle: c = renderTriangle();   break;
        case Keyframe::Kind::Fm2:      c = renderFm2(kf);      break;
        case Keyframe::Kind::Cheby:    c = renderCheby(kf);    break;
        case Keyframe::Kind::Ring:     c = renderRing(kf);     break;
        default: return Cycle(static_cast<size_t>(kFrameSize), 0.0); // unreachable; -Wreturn-type
    }
    applyShape(c, kf.shape);
    return c;
}

// ─── Motion sampling ───

std::vector<std::vector<float>> Baker::bake(const Recipe& recipe)
{
    const int numFrames = juce::jlimit(8, 256, recipe.frames);

    // No keyframes: nothing to render. N silent frames rather than indexing
    // into an empty keyframes vector — a malformed/empty recipe (it may come
    // from an LLM's output) must not crash the baker.
    if (recipe.keyframes.empty())
        return std::vector<std::vector<float>>(
            static_cast<size_t>(numFrames),
            std::vector<float>(static_cast<size_t>(kFrameSize), 0.0f));

    // Render + cache each distinct keyframe's cycle exactly once, regardless
    // of how many motion segments reference it.
    std::vector<Cycle> rendered(recipe.keyframes.size());
    for (size_t i = 0; i < recipe.keyframes.size(); ++i)
        rendered[i] = renderKeyframe(recipe.keyframes[i]);

    const int lastKf = static_cast<int>(recipe.keyframes.size()) - 1;
    const auto clampKf = [lastKf](int idx) { return juce::jlimit(0, lastKf, idx); };

    // A segment spans cumulative trajectory fraction [cumStart, cumEnd] and
    // morphs from keyframe `fromKf` to keyframe `toKf` along `curve`.
    struct Segment { int fromKf; int toKf; double cumStart; double cumEnd; Curve curve; };
    std::vector<Segment> segments;

    const int startKf = recipe.motion.empty() ? 0 : clampKf(recipe.motion[0].to);

    if (recipe.motion.size() > 1)
    {
        // Sum raw durations, excluding motion[0] (the start point, durFrac
        // ignored). Negative/zero durations clamp to 0 width — a malformed
        // segment collapses to an instant jump rather than inverting the
        // trajectory or dividing by a negative span.
        double rawSum = 0.0;
        for (size_t i = 1; i < recipe.motion.size(); ++i)
            rawSum += std::max(0.0, static_cast<double>(recipe.motion[i].durFrac));

        if (rawSum > 0.0)
        {
            double cum = 0.0;
            int fromKf = startKf;
            for (size_t i = 1; i < recipe.motion.size(); ++i)
            {
                const auto& seg = recipe.motion[i];
                const double dur = std::max(0.0, static_cast<double>(seg.durFrac)) / rawSum;
                const int toKf = clampKf(seg.to);
                const double cumStart = cum;
                // Snap the final segment's end to exactly 1.0 — running-sum
                // float drift must not leave the last frame (p==1 for a
                // non-looping bake) unmatched by any segment.
                const double cumEnd = (i + 1 == recipe.motion.size()) ? 1.0 : (cum + dur);
                segments.push_back({ fromKf, toKf, cumStart, cumEnd, seg.curve });
                cum = cumEnd;
                fromKf = toKf;
            }
        }
    }

    // Empty motion, a start-only motion (no segments), or all-zero-duration
    // segments: the whole trajectory is static at the start keyframe.
    if (segments.empty())
        segments.push_back({ startKf, startKf, 0.0, 1.0, Curve::Lin });

    // Sample the trajectory into numFrames frames. loop=true never revisits
    // p==1 within the N samples (p = i/N stops at (N-1)/N) — that's what
    // makes the wrap from frame[N-1] back to frame[0] seamless when the
    // recipe's motion closes on the start keyframe.
    std::vector<std::vector<float>> frames(static_cast<size_t>(numFrames));
    for (int i = 0; i < numFrames; ++i)
    {
        const double p = recipe.loop
            ? static_cast<double>(i) / numFrames
            : (numFrames > 1 ? static_cast<double>(i) / (numFrames - 1) : 0.0);

        const Segment* active = &segments.back();
        for (const auto& seg : segments)
        {
            if (p <= seg.cumEnd) { active = &seg; break; }
        }

        const double span = active->cumEnd - active->cumStart;
        const double aRaw = (span > 1.0e-9) ? (p - active->cumStart) / span : 1.0;
        const double a = shapeCurve(aRaw, active->curve);

        const Cycle& from = rendered[static_cast<size_t>(active->fromKf)];
        const Cycle& to   = rendered[static_cast<size_t>(active->toKf)];

        std::vector<float> frame(static_cast<size_t>(kFrameSize));
        for (int n = 0; n < kFrameSize; ++n)
        {
            const double blended = (1.0 - a) * from[static_cast<size_t>(n)] + a * to[static_cast<size_t>(n)];
            frame[static_cast<size_t>(n)] = static_cast<float>(blended);
        }
        frames[static_cast<size_t>(i)] = std::move(frame);
    }

    // Global normalization: ONE gain across ALL frames. Range safety AND morph
    // level preservation for the bit-exact DCO consumer (WavetableOscillator::
    // setExactFrames), which — unlike extractContiguousFrames — does NOT
    // per-frame renorm, so this global gain is exactly what plays.
    //
    // Shaped (waveshaped) recipes get extra headroom. Time-domain waveshaping
    // rebuilds the source's Fourier phases, and the reconstructed harmonics
    // overshoot ~15% at the waveform discontinuity through the engine's
    // band-limited mip + Catmull-Rom interpolation (measured: a driven saw
    // peaks ~1.09 from a 0.95 target — regardless of drive amount, since even a
    // little waveshaping re-phases the series). A 0.83 target keeps that
    // overshoot at ~0.95 in playback; unshaped bakes keep the fuller 0.95
    // (their reconstruction only reaches ~0.94).
    bool anyShaped = false;
    for (const auto& kf : recipe.keyframes)
        if (kf.shape > 0.0f) { anyShaped = true; break; }
    const float normTarget = anyShaped ? 0.83f : 0.95f;

    float peak = 0.0f;
    for (const auto& frame : frames)
        for (float s : frame)
            peak = std::max(peak, std::abs(s));

    if (peak > 1.0e-6f)
    {
        const float gain = normTarget / peak;
        for (auto& frame : frames)
            for (float& s : frame)
                s *= gain;
    }
    // else: an all-silent recipe (e.g. an Additive keyframe with no partials) — leave zeros.

    return frames;
}

// ─── Public API: buffer packaging ───

juce::AudioBuffer<float> Baker::framesToBuffer(const std::vector<std::vector<float>>& frames)
{
    juce::AudioBuffer<float> buf(1, static_cast<int>(frames.size()) * kFrameSize);
    buf.clear();  // JUCE mallocs without zeroing; a short frame must not leave garbage
    float* dest = buf.getWritePointer(0);
    for (size_t f = 0; f < frames.size(); ++f)
    {
        // Defensive shape guard (mirrors WavetableOscillator::snapshotLevel0Frames):
        // bake() always returns kFrameSize-sized frames, but don't read/write
        // past either buffer if a caller hands in something malformed.
        const size_t n = std::min(frames[f].size(), static_cast<size_t>(kFrameSize));
        std::copy_n(frames[f].data(), n, dest + f * static_cast<size_t>(kFrameSize));
    }
    return buf;
}

} // namespace dco
