// Offline audition + shape check for the looping ADSREnvelope (A-D-Hold-R-repeat).
// ADSREnvelope is pure std → compiles standalone, no JUCE.
//   g++ -std=c++17 -O2 tools/audition_env_loop.cpp src/dsp/ADSREnvelope.cpp -o /tmp/env_audition
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

int main()
{
    const int sr = 48000;
    ADSREnvelope env; env.prepare(sr);
    env.setAttack(50.f); env.setDecay(100.f); env.setSustain(0.5f); env.setRelease(100.f);
    env.setHoldMs(200.f); env.setLooping(true);
    env.setAttackBend(bendFromCurveShape(CurveShape::Lin));
    env.setDecayBend(bendFromCurveShape(CurveShape::Lin));
    env.setReleaseBend(bendFromCurveShape(CurveShape::Lin));
    env.noteOn(1.0f);

    std::vector<float> curve, audio;
    const int heldSamples = static_cast<int>(2.0 * sr);   // 2 s held
    double ph = 0.0, inc = 2.0 * M_PI * 220.0 / sr;
    for (int i = 0; i < heldSamples; ++i) {
        float e = env.processSample();
        curve.push_back(e);
        audio.push_back(0.3f * e * static_cast<float>(std::sin(ph))); ph += inc;
    }

    env.noteOff();
    const int relSamples = static_cast<int>(0.6 * sr);
    int idleAt = -1;
    for (int i = 0; i < relSamples; ++i) {
        float e = env.processSample();
        curve.push_back(e);
        audio.push_back(0.3f * e * static_cast<float>(std::sin(ph))); ph += inc;
        if (idleAt < 0 && env.isIdle()) idleAt = i;
    }

    // Count cycles in the held phase (rising past 0.9, falling below 0.1)
    int cycles = 0; bool armed = true;
    for (int i = 0; i < heldSamples; ++i) {
        if (armed && curve[i] > 0.9f) { ++cycles; armed = false; }
        if (!armed && curve[i] < 0.1f) armed = true;
    }

    printf("=== loop shape: A=50 D=100 Hold=200 R=100 ms -> period ~450 ms ===\n");
    printf("held 2000 ms -> expected ~4-5 cycles; measured: %d\n", cycles);
    printf("max level (expect ~1.0): %.3f\n", *std::max_element(curve.begin(), curve.end()));
    printf("idle after noteOff: %s (%.1f ms)\n", idleAt >= 0 ? "YES" : "NO",
           idleAt >= 0 ? idleAt * 1000.0 / sr : -1.0);

    printf("\nfirst 520 ms @ 20 ms (expect rise->~0.5->flat hold->fall->rise):\n");
    for (int ms = 0; ms <= 520; ms += 20) {
        int idx = ms * sr / 1000;
        float v = curve[std::min(idx, (int)curve.size()-1)];
        int bars = static_cast<int>(v * 40.f);
        printf("%4dms |", ms);
        for (int b = 0; b < bars; ++b) putchar('#');
        printf(" %.2f\n", v);
    }

    writeWav("tools/env_loop_audition_out/tremolo_220hz.wav", audio, sr);
    printf("\nwrote tools/env_loop_audition_out/tremolo_220hz.wav\n");

    // ── Regression: enable Loop MID-NOTE while parked in Sustain ──
    // (verify agent found this froze at the sustain level; fix must start cycling)
    {
        ADSREnvelope t; t.prepare(sr);
        t.setAttack(5.f); t.setDecay(20.f); t.setSustain(0.5f); t.setRelease(100.f);
        t.setHoldMs(150.f); t.setLooping(false);
        t.setAttackBend(0.0f); t.setDecayBend(0.0f); t.setReleaseBend(0.0f);   // Lin
        t.noteOn(1.0f);
        for (int i = 0; i < (int)(0.2 * sr); ++i) t.processSample();   // settle in Sustain
        t.setLooping(true);                                            // toggle Loop ON mid-note
        float lo = 2.f, hi = -2.f;
        for (int i = 0; i < (int)(1.0 * sr); ++i) { float e = t.processSample(); lo = std::min(lo, e); hi = std::max(hi, e); }
        printf("\n=== toggle Loop ON mid-note (was frozen at sustain) ===\n");
        printf("post-toggle level range: [%.3f .. %.3f]  -> %s\n", lo, hi,
               (hi - lo > 0.4f) ? "CYCLING (fixed)" : "FROZEN (BUG)");
    }
    return 0;
}
