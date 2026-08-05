// Parity + audition for the continuous envelope bend (2026-08-05).
//
// The bend replaced a 5-step choice. The claim that made that safe is checkable,
// not a matter of taste, so it is checked here against a FROZEN copy of the old
// curve code (oldCurve / oldRelease below, transcribed from ADSREnvelope.cpp at
// 7fb57649) rather than against the new implementation's own idea of itself:
//
//   1. attack/decay at the five anchors == the five old shapes, bit for bit
//   2. release on the convex half (Log / SLog / Lin) == the old release, bit for bit
//   3. release on the concave half is the SAME RC discharge, normalised to reach
//      zero at the set release time — reported as a difference, not asserted
//   4. every stage is monotone and hits 0 and 1 at the ends, on a fine sweep of
//      bends the anchors do not cover (that is what the whole change is for)
//
// Writes bend_sweep_release.wav / bend_sweep_attack.wav for the ear.
//   g++ -std=c++17 -O2 tools/audition_env_bend.cpp src/dsp/ADSREnvelope.cpp -o /tmp/env_bend
#include "../src/dsp/ADSREnvelope.h"
#include <cstdio>
#include <cstdint>
#include <vector>
#include <cmath>
#include <string>
#include <fstream>
#include <algorithm>

static void writeWav(const std::string& path, const std::vector<float>& mono, int sr)
{
    std::ofstream f(path, std::ios::binary);
    auto u32 = [&](uint32_t v){ f.write(reinterpret_cast<char*>(&v), 4); };
    auto u16 = [&](uint16_t v){ f.write(reinterpret_cast<char*>(&v), 2); };
    uint32_t dataBytes = static_cast<uint32_t>(mono.size()) * 2u;
    f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); u32(16); u16(1); u16(1); u32(sr); u32(sr * 2); u16(2); u16(16);
    f.write("data", 4); u32(dataBytes);
    for (float s : mono) {
        int v = static_cast<int>(std::lround(std::max(-1.f, std::min(1.f, s)) * 32767.f));
        u16(static_cast<uint16_t>(static_cast<int16_t>(v)));
    }
}

// ── The old code, frozen. Do NOT re-derive these from the new implementation. ──
static float oldCurve(float t, int shape)   // 0=Log 1=SLog 2=Lin 3=SExp 4=Exp
{
    switch (shape) {
        case 4: { float inv = 1.0f - t; return 1.0f - inv * inv * inv; }
        case 3: { float inv = 1.0f - t; return 1.0f - inv * inv; }
        case 0:  return t * t * t;
        case 1:  return t * t;
        default: return t;
    }
}
// Old release LEVEL, from a start level of 1.0, at sample n of a release of
// relSamples (progress-based below Lin, RC-discharge above it — and the RC one
// deliberately runs past relSamples until it falls under the -80 dB threshold).
static float oldReleaseLevel(int n, int relSamples, int sr, float relSec, int shape)
{
    float level;
    if (shape == 4 || shape == 3) {
        const float tau = (shape == 4) ? relSec / 5.0f : (relSec / 5.0f) * (5.0f / 3.0f);
        const float t = static_cast<float>(n) / static_cast<float>(sr);
        level = std::exp(-t / tau);
    } else {
        const float t = std::min(1.0f, static_cast<float>(n) / static_cast<float>(relSamples));
        level = 1.0f - oldCurve(t, shape);
    }
    return level < 1.0e-4f ? 0.0f : level;   // the old code's own -80 dB cutoff
}

static int failures = 0;
static void check(bool ok, const std::string& what)
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++failures;
}

int main()
{
    const int sr = 48000;
    const char* names[5] = { "Log", "SLog", "Lin", "SExp", "Exp" };

    // ── 1. Attack/decay anchors: bit-identical to the old five shapes ──
    for (int s = 0; s < 5; ++s) {
        const float bend = bendFromCurveIndex(s);
        float maxDiff = 0.0f;
        for (int i = 0; i <= 10000; ++i) {
            const float t = static_cast<float>(i) / 10000.0f;
            maxDiff = std::max(maxDiff, std::abs(ADSREnvelope::applyCurve(t, bend) - oldCurve(t, s)));
        }
        char buf[128];
        std::snprintf(buf, sizeof buf, "A/D anchor %-4s (bend %+.1f) identical to the old shape (max diff %.3g)",
                      names[s], bend, maxDiff);
        check(maxDiff == 0.0f, buf);
    }

    // ── 2/3. Release, run through the real envelope, anchor by anchor ──
    const float relSec = 1.0f;
    const int   relSamples = static_cast<int>(relSec * sr);
    for (int s = 0; s < 5; ++s) {
        ADSREnvelope env; env.prepare(sr);
        env.setAttack(0.0f); env.setDecay(0.0f); env.setSustain(1.0f);
        env.setRelease(relSec * 1000.0f);
        env.setAttackBend(0.0f); env.setDecayBend(0.0f);
        env.setReleaseBend(bendFromCurveIndex(s));
        env.noteOn(1.0f);
        // Past BOTH minimum ramps (3 ms attack + 3 ms decay) so the release
        // really starts from 1.0, which is what the reference below assumes.
        for (int i = 0; i < sr / 50; ++i) env.processSample();
        env.noteOff();

        float maxDiff = 0.0f, atHalf = -1.0f, oldAtHalf = -1.0f, prev = 2.0f;
        int silentAt = -1; bool monotone = true;
        for (int n = 1; n <= relSamples * 4; ++n) {
            const float now = env.processSample();
            if (now > prev + 1.0e-6f) monotone = false;
            prev = now;
            if (n <= relSamples)
                maxDiff = std::max(maxDiff, std::abs(now - oldReleaseLevel(n, relSamples, sr, relSec, s)));
            if (n == relSamples / 2) { atHalf = now; oldAtHalf = oldReleaseLevel(n, relSamples, sr, relSec, s); }
            if (silentAt < 0 && env.isIdle()) silentAt = n;
        }
        char buf[256];
        if (s <= 2) {
            std::snprintf(buf, sizeof buf, "Release anchor %-4s identical to the old release (max diff %.3g)",
                          names[s], maxDiff);
            check(maxDiff == 0.0f, buf);
        } else {
            // Reported, not asserted: this is the one deliberate change. Same
            // curve, offset and rescaled to land on zero at the set time — so
            // mid-release the two levels are close, and what disappears is the
            // tail the old one still had running after the fader's own time.
            const float oldEnd = oldReleaseLevel(relSamples, relSamples, sr, relSec, s);
            const float oldSilent = (relSec / ((s == 4) ? 5.0f : 3.0f)) * std::log(1.0f / 1.0e-4f);
            std::printf("INFO  Release anchor %-4s: mid-release old=%.1f dB new=%.1f dB (%.2f dB apart); "
                        "at the SET time old=%.1f dB new=silent; fully silent old=%.2fx new=%.2fx the set time\n",
                        names[s],
                        20.0f * std::log10(std::max(oldAtHalf, 1e-9f)),
                        20.0f * std::log10(std::max(atHalf, 1e-9f)),
                        20.0f * std::log10(std::max(atHalf, 1e-9f) / std::max(oldAtHalf, 1e-9f)),
                        20.0f * std::log10(std::max(oldEnd, 1e-9f)),
                        oldSilent / relSec, static_cast<float>(silentAt) / static_cast<float>(relSamples));
        }
        std::snprintf(buf, sizeof buf, "Release anchor %-4s falls monotonically", names[s]);
        check(monotone, buf);
        std::snprintf(buf, sizeof buf, "Release anchor %-4s reaches silence AT the set time (%.3fx)",
                      names[s], static_cast<float>(silentAt) / static_cast<float>(relSamples));
        check(silentAt > 0 && silentAt <= relSamples + 2, buf);
    }

    // ── 3b. Past the named poles: what the extra unit of sag buys ──
    // The falling stages travel to +2 for a faster initial drop and, at the same
    // release time, a longer quiet tail; the attack travels to -2 for a rise that
    // stays down longer. Reported as the share of the release spent under -20 dB,
    // which is the "Fahne" (BJ) the old maximum could not reach.
    for (float b : { 0.5f, 1.0f, 1.5f, 2.0f }) {
        float tailFrom = 1.0f;
        for (int i = 0; i <= 4000; ++i) {
            const float t = static_cast<float>(i) / 4000.0f;
            if (1.0f - ADSREnvelope::applyReleaseCurve(t, b) <= 0.1f) { tailFrom = t; break; }
        }
        std::printf("INFO  Release bend %+.2f: k=%.2f, under -20 dB from %.0f%% of the release "
                    "(tail = %.0f%% of it), level at 25%% = %.1f dB\n",
                    b, releaseKForBend(b), tailFrom * 100.0f, (1.0f - tailFrom) * 100.0f,
                    20.0f * std::log10(std::max(1.0f - ADSREnvelope::applyReleaseCurve(0.25f, b), 1e-9f)));
    }
    for (float b : { -1.0f, -1.5f, -2.0f }) {
        std::printf("INFO  Attack  bend %+.2f: level at 25%%/50%%/75%% of the attack = "
                    "%.1f%% / %.1f%% / %.1f%%\n", b,
                    100.0f * ADSREnvelope::applyCurve(0.25f, b),
                    100.0f * ADSREnvelope::applyCurve(0.50f, b),
                    100.0f * ADSREnvelope::applyCurve(0.75f, b));
    }

    // ── 4. The travel BETWEEN the anchors: shape sanity on a fine sweep ──
    {
        bool ends = true, monotone = true, ordered = true;
        float worstStep = 0.0f, worstStepAt = 0.0f;
        float prevAtHalf = -1.0f, prevRelAtHalf = -1.0f;
        for (int k = -200; k <= 200; ++k) {
            const float b = static_cast<float>(k) / 100.0f;
            for (int pass = 0; pass < 2; ++pass) {
                auto f = [&](float t) { return pass == 0 ? ADSREnvelope::applyCurve(t, b)
                                                         : ADSREnvelope::applyReleaseCurve(t, b); };
                if (std::abs(f(0.0f)) > 1e-6f || std::abs(f(1.0f) - 1.0f) > 1e-5f) ends = false;
                float prev = -1.0f;
                for (int i = 0; i <= 2000; ++i) {
                    const float y = f(static_cast<float>(i) / 2000.0f);
                    if (y < prev - 1e-6f) monotone = false;
                    prev = y;
                }
            }
            // Rising bend must never make the curve LESS concave, and one step of
            // 0.01 must never jump the midpoint by more than a few percent.
            const float half = ADSREnvelope::applyCurve(0.5f, b);
            const float relHalf = ADSREnvelope::applyReleaseCurve(0.5f, b);
            if (half < prevAtHalf - 1e-6f || relHalf < prevRelAtHalf - 1e-6f) ordered = false;
            if (prevAtHalf >= 0.0f) {
                const float step = std::max(half - prevAtHalf, relHalf - prevRelAtHalf);
                if (step > worstStep) { worstStep = step; worstStepAt = b; }
            }
            prevAtHalf = half; prevRelAtHalf = relHalf;
        }
        check(ends, "every bend in -2..+2 starts at 0 and ends at 1 (both laws)");
        check(monotone, "every bend in -2..+2 is monotone (both laws)");
        check(ordered, "the axis is ordered: more bend is never less concave");
        // The travel is smooth, but not evenly fast: the release's k(b) is a
        // power law with an infinite slope at 0, so the first notch out of Lin
        // is the biggest single step on the whole axis. It is reported rather
        // than hidden behind a threshold that was chosen to pass.
        char buf[160];
        std::snprintf(buf, sizeof buf,
                      "no audible step in the axis: worst 0.01 notch moves the midpoint %.1f%% "
                      "(at bend %+.2f)", worstStep * 100.0f, worstStepAt);
        check(worstStep < 0.03f, buf);
    }

    // ── Audition: the release axis and the attack axis ──
    // Each runs the WHOLE range that stage can hold, so the last notes are the
    // territory past the old maximum: the release to +2, the attack to -2.
    for (int which = 0; which < 2; ++which) {
        std::vector<float> audio;
        double ph = 0.0; const double inc = 2.0 * M_PI * 220.0 / sr;
        for (int k = 0; k <= 9; ++k) {
            // Release: -1 … +2 in steps of 1/3. Attack: +1 … -2, same steps.
            const float step = static_cast<float>(k) / 3.0f;
            const float bend = (which == 0) ? -1.0f + step : 1.0f - step;
            ADSREnvelope env; env.prepare(sr);
            env.setSustain(1.0f);
            env.setAttack(which == 0 ? 1.0f : 900.0f);
            env.setDecay(0.0f);
            env.setRelease(which == 0 ? 900.0f : 1.0f);
            env.setAttackBend(which == 1 ? bend : 0.0f);
            env.setDecayBend(0.0f);
            env.setReleaseBend(which == 0 ? bend : 0.0f);
            env.noteOn(1.0f);
            const int hold = static_cast<int>((which == 0 ? 0.15 : 1.0) * sr);
            for (int i = 0; i < hold; ++i) {
                audio.push_back(0.3f * env.processSample() * static_cast<float>(std::sin(ph)));
                ph += inc;
            }
            env.noteOff();
            for (int i = 0; i < static_cast<int>(1.2 * sr); ++i) {
                audio.push_back(0.3f * env.processSample() * static_cast<float>(std::sin(ph)));
                ph += inc;
            }
            for (int i = 0; i < static_cast<int>(0.25 * sr); ++i) audio.push_back(0.0f);
        }
        writeWav(which == 0 ? "bend_sweep_release.wav" : "bend_sweep_attack.wav", audio, sr);
    }
    std::printf("\nWrote bend_sweep_release.wav (10 notes, bend -1 -> +2) and "
                "bend_sweep_attack.wav (10 notes, bend +1 -> -2), 1/3 steps.\n");
    std::printf("%s\n", failures == 0 ? "ALL CHECKS PASSED" : "CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
