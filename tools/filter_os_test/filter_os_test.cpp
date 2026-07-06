// Offline aliasing test for the T5ynth nonlinear filters.
//
// Hypothesis (user, 2026-06-21): the Ladder/Warp filters sound "bad" because
// they run at base rate — their in-loop saturation generates harmonics above
// Nyquist that fold back as aliasing. The linear SVF doesn't alias and sounds
// fine. This harness drives the REAL filter classes (MoogLadderFilter.h /
// CutoffWarpFilter.h) at base rate vs N× oversampled and renders WAVs so the
// aliasing can be heard and measured. The only thing that differs between the
// renders is the filter's internal sample rate.
//
// Build (see run.sh): g++ -std=c++17 -O2 -I . -I ../../src/dsp ...

#include "MoogLadderFilter.h"
#include "CutoffWarpFilter.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>

static constexpr double kBaseSr = 48000.0;

// ────────────────────────── WAV (32-bit float mono) ──────────────────────────
static void writeWavFloat(const std::string& path, const std::vector<float>& x, double sr)
{
    auto u32 = [](uint32_t v){ return v; };
    const uint32_t dataBytes = static_cast<uint32_t>(x.size() * sizeof(float));
    const uint32_t sampleRate = static_cast<uint32_t>(sr + 0.5);
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); return; }
    auto w32 = [&](uint32_t v){ std::fwrite(&v, 4, 1, f); };
    auto w16 = [&](uint16_t v){ std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f); w32(u32(36 + dataBytes)); std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f); w32(16); w16(3 /*IEEE float*/); w16(1 /*mono*/);
    w32(sampleRate); w32(sampleRate * 4); w16(4); w16(32);
    std::fwrite("data", 1, 4, f); w32(dataBytes);
    std::fwrite(x.data(), 1, dataBytes, f);
    std::fclose(f);
}

// ───────────────────── band-limited sawtooth (identical at any sr) ────────────
// Additive: harmonics only up to 18 kHz, well under both base & oversampled
// Nyquist, so the INPUT is bit-for-bit the same spectrum at every rate — any
// aliasing in the output is therefore the filter's doing, not the source.
static float bandlimitedSaw(double t, double f0, double maxHz, double amp)
{
    const int K = static_cast<int>(maxHz / f0);
    double acc = 0.0;
    for (int k = 1; k <= K; ++k)
        acc += std::sin(juce::MathConstants<double>::twoPi * k * f0 * t) / k;
    return static_cast<float>(amp * acc * (2.0 / juce::MathConstants<double>::pi));
}

// ───────────────────────── windowed-sinc FIR decimator ───────────────────────
// Builds a linear-phase low-pass at the oversampled rate (cutoff 20 kHz) and
// evaluates it ONLY at the decimation points (centered), so cost is
// outputLen×taps regardless of N. Removes the >Nyquist content of the
// oversampled render cleanly before downsampling — no decimation aliasing.
struct FirDecimator
{
    std::vector<double> h;
    int N = 1;
    void design(int oversample, double osr, double cutoffHz, int taps)
    {
        N = oversample;
        if (N == 1) return;
        if ((taps & 1) == 0) ++taps;             // force odd → exact linear phase
        h.assign(static_cast<size_t>(taps), 0.0);
        const int M = taps - 1;
        const double fc = cutoffHz / osr;        // normalized (cycles/sample)
        double sum = 0.0;
        for (int n = 0; n <= M; ++n)
        {
            const double m = n - M / 2.0;
            const double sinc = (m == 0.0) ? 2.0 * fc
                                           : std::sin(2.0 * juce::MathConstants<double>::pi * fc * m)
                                             / (juce::MathConstants<double>::pi * m);
            // Blackman-Harris window
            const double w = 0.35875
                - 0.48829 * std::cos(2.0 * juce::MathConstants<double>::pi * n / M)
                + 0.14128 * std::cos(4.0 * juce::MathConstants<double>::pi * n / M)
                - 0.01168 * std::cos(6.0 * juce::MathConstants<double>::pi * n / M);
            h[static_cast<size_t>(n)] = sinc * w;
            sum += sinc * w;
        }
        for (auto& c : h) c /= sum;              // unity DC gain
    }
    // os: oversampled signal. Returns base-rate signal of length os.size()/N.
    std::vector<float> run(const std::vector<float>& os) const
    {
        if (N == 1) return os;
        const int taps = static_cast<int>(h.size());
        const int half = taps / 2;
        const size_t outLen = os.size() / static_cast<size_t>(N);
        std::vector<float> out(outLen, 0.0f);
        for (size_t m = 0; m < outLen; ++m)
        {
            const long center = static_cast<long>(m) * N;
            double acc = 0.0;
            for (int j = 0; j < taps; ++j)
            {
                const long idx = center + (j - half);
                if (idx >= 0 && idx < static_cast<long>(os.size()))
                    acc += h[static_cast<size_t>(j)] * os[static_cast<size_t>(idx)];
            }
            out[m] = static_cast<float>(acc);
        }
        return out;
    }
};

// ─────────────────────────────── render cases ────────────────────────────────
template <typename Filter>
struct DriveCfg { static void set(Filter& f, float g) { f.setInputDrive(g); } };

// Cutoff sweep (exp 80 Hz→11 kHz), high resonance + drive, band-limited saw in.
// The resonant peak rings as it sweeps; its in-loop saturation throws harmonics
// past Nyquist. Base rate folds them down → descending ghost streaks.
template <typename Filter>
static std::vector<float> renderSweep(int oversample, double seconds,
                                      float reso, float drive)
{
    const double osr = kBaseSr * oversample;
    const size_t n = static_cast<size_t>(seconds * osr);
    Filter filt; filt.prepare(osr, 256);
    filt.setType(0); filt.setSlope(3); filt.setMix(1.0f);
    filt.setResonance(reso); DriveCfg<Filter>::set(filt, drive);

    std::vector<float> raw(n, 0.0f);
    const double f0 = 80.0, f1 = 11000.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t  = static_cast<double>(i) / osr;
        const double pr = static_cast<double>(i) / static_cast<double>(n);
        filt.setCutoff(static_cast<float>(f0 * std::pow(f1 / f0, pr)));
        const float in = bandlimitedSaw(t, 55.0, 18000.0, 0.25);
        raw[i] = filt.processSample(in);
    }
    FirDecimator dec; dec.design(oversample, osr, 20000.0, 1023);
    return dec.run(raw);
}

// Fixed inharmonic tone (3700 Hz) into a driven resonant filter parked on it.
// The harmonics fold onto NON-harmonic bins at base rate → measurable alias
// energy that the oversampled render doesn't have.
template <typename Filter>
static std::vector<float> renderTone(int oversample, double seconds,
                                     float reso, float drive, double toneHz)
{
    const double osr = kBaseSr * oversample;
    const size_t n = static_cast<size_t>(seconds * osr);
    Filter filt; filt.prepare(osr, 256);
    filt.setType(0); filt.setSlope(3); filt.setMix(1.0f);
    filt.setResonance(reso); DriveCfg<Filter>::set(filt, drive);
    filt.setCutoff(static_cast<float>(toneHz));

    std::vector<float> raw(n, 0.0f);
    for (size_t i = 0; i < n; ++i)
    {
        const double t  = static_cast<double>(i) / osr;
        const float in = static_cast<float>(0.5 * std::sin(juce::MathConstants<double>::twoPi * toneHz * t));
        raw[i] = filt.processSample(in);
    }
    FirDecimator dec; dec.design(oversample, osr, 20000.0, 1023);
    return dec.run(raw);
}

template <typename Filter>
static void renderAll(const std::string& name, const std::string& outDir)
{
    const int factors[] = { 1, 2, 4, 8 };
    for (int N : factors)
    {
        auto sweep = renderSweep<Filter>(N, 3.0, 0.95f, 2.0f);
        writeWavFloat(outDir + "/" + name + "_sweep_os" + std::to_string(N) + ".wav", sweep, kBaseSr);
        auto tone = renderTone<Filter>(N, 1.5, 0.90f, 4.0f, 9000.0);
        writeWavFloat(outDir + "/" + name + "_tone_os" + std::to_string(N) + ".wav", tone, kBaseSr);
        std::printf("  %-6s os%dx: sweep %zu / tone %zu samples\n", name.c_str(), N, sweep.size(), tone.size());
    }
}

int main(int argc, char** argv)
{
    const std::string outDir = (argc > 1) ? argv[1] : ".";
    std::printf("Rendering filter aliasing test → %s\n", outDir.c_str());
    renderAll<MoogLadderFilter>("ladder", outDir);
    renderAll<CutoffWarpFilter>("warp",   outDir);
    std::printf("done.\n");
    return 0;
}
