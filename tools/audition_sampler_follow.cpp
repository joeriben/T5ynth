// Offline audition for held-note Regen XFade (morphToBufferFrom crossfade).
//
// Reproduces the A/B-drift regenerate seam: a held sampler voice shares buffer A,
// then the master publishes a very different buffer B and the voice equal-power
// crossfades A->B over the Drift Crossfade time (morphToBufferFrom). We render
// straight through the crossfade and measure the sample-to-sample discontinuity
// vs. the signal's natural per-sample delta, AND confirm the blend actually
// transitions, for both render paths (unity → bypass/processSample, pitched →
// Signalsmith stretch).
//
// Build: compile against the plugin's static lib + JUCE flags, e.g.
//   FLAGS=build_clean/CMakeFiles/T5ynth.dir/flags.make
//   { grep -m1 CXX_DEFINES "$FLAGS"; grep -m1 CXX_INCLUDES "$FLAGS"; } \
//     | sed 's/^CXX_[A-Z]* = //' > /tmp/h.rsp
//   echo -I$PWD/build_clean/_deps/signalsmith_stretch-src >> /tmp/h.rsp
//   clang++ -std=c++17 -O2 @/tmp/h.rsp tools/audition_sampler_follow.cpp \
//     build_clean/T5ynth_artefacts/Release/libT5ynth_SharedCode.a \
//     -framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio \
//     -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation \
//     -framework IOKit -framework QuartzCore -framework Security -framework WebKit \
//     -weak_framework Metal -weak_framework MetalKit -o /tmp/sampler_follow
// Exits non-zero if any case is not a click-free, actually-transitioning crossfade.
#include "JuceHeader.h"
#include "dsp/SamplePlayer.h"

#include <cstdio>
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

// A buffer of a steady tone — distinct freq/amp so a hard swap would click hard.
static juce::AudioBuffer<float> makeTone(int sr, double seconds, double freq, float amp, double phase)
{
    int n = static_cast<int>(seconds * sr);
    juce::AudioBuffer<float> b(1, n);
    float* d = b.getWritePointer(0);
    for (int i = 0; i < n; ++i)
        d[i] = amp * static_cast<float>(std::sin(2.0 * M_PI * freq * i / sr + phase));
    return b;
}

static bool runCase(const std::string& label, double ratio, float morphMs,
                    const std::string& wavOut, int sr)
{
    const int block = 512;
    const int preBlocks = 40;    // ~0.43 s held on buffer A
    const int postBlocks = 80;   // ~0.85 s on buffer B after the live crossfade

    auto bufA = makeTone(sr, 2.0, 220.0, 0.50f, 0.0);
    auto bufB = makeTone(sr, 2.0, 660.0, 0.85f, M_PI * 0.5); // very different content

    SamplePlayer master;
    master.prepare(sr, block);
    master.setLoopMode(SamplePlayer::LoopMode::Loop);
    master.setNormalize(false);
    master.loadBuffer(bufA, sr);

    SamplePlayer voice;
    voice.prepare(sr, block);
    voice.setLoopMode(SamplePlayer::LoopMode::Loop);
    voice.setNormalize(false);
    voice.shareBufferFrom(master);
    voice.setTransposeRatio(ratio);
    voice.retrigger();

    std::vector<float> out;
    std::vector<float> blockBuf(static_cast<size_t>(block));

    for (int b = 0; b < preBlocks; ++b) {
        voice.renderPitchedBlock(blockBuf.data(), block);
        out.insert(out.end(), blockBuf.begin(), blockBuf.end());
    }

    const int seamIndex = static_cast<int>(out.size()); // first crossfade sample

    // The A/B-drift regenerate handoff: master republishes, held voice crossfades
    // from A to B over morphMs (the Regen XFade). morphToBufferFrom runs on the
    // audio thread, exactly as VoiceManager::distributeSamplerBuffer drives it.
    master.loadBuffer(bufB, sr);
    voice.morphToBufferFrom(master, morphMs);

    for (int b = 0; b < postBlocks; ++b) {
        voice.renderPitchedBlock(blockBuf.data(), block);
        out.insert(out.end(), blockBuf.begin(), blockBuf.end());
    }

    const int morphSamples = (int) std::lround((double) morphMs * 0.001 * sr);
    // Crossfade window (seam → end of the equal-power ramp, + a small guard). The
    // stretch path delays the blend by the STFT latency, so cover generously.
    const int winEnd = seamIndex + morphSamples + 4096;

    // Continuity at the seam itself: alpha starts at 0 (all old buffer), so the
    // first crossfade sample continues A — there must be no step here.
    float seamDelta = std::abs(out[seamIndex] - out[seamIndex - 1]);

    // Max per-sample delta THROUGH the whole crossfade window — the click test.
    float windowMax = 0.0f;
    for (int i = seamIndex - 1; i < winEnd && i < (int) out.size() - 1; ++i)
        windowMax = std::max(windowMax, std::abs(out[i + 1] - out[i]));

    // Natural per-sample delta in steady state (pure B), the reference ceiling.
    float naturalMax = 0.0f;
    for (int i = winEnd; i < (int) out.size() - 1; ++i)
        naturalMax = std::max(naturalMax, std::abs(out[i + 1] - out[i]));

    // The crossfade must actually transition: pre-seam is A (220 Hz/0.50),
    // post-window is B (660 Hz/0.85). Confirm RMS moved toward B's level.
    auto rms = [&](int a, int b){ double s = 0; int n = 0;
        for (int i = a; i < b && i < (int) out.size(); ++i) { s += out[i]*out[i]; ++n; }
        return n ? std::sqrt(s / n) : 0.0; };
    const double rmsA = rms(seamIndex - 4000, seamIndex);
    const double rmsB = rms(winEnd, winEnd + 4000);

    writeWav(wavOut, out, sr);

    const bool clickFree = windowMax <= naturalMax * 1.5f + 1.0e-4f;
    const bool transitioned = rmsB > rmsA * 1.2;   // 0.85 vs 0.50 → clearly higher
    const bool pass = clickFree && transitioned;
    printf("  [%-22s] seamDelta=%.5f  windowMax=%.5f  naturalMax=%.5f  rmsA=%.3f rmsB=%.3f  morph=%.0fms -> %s\n",
           label.c_str(), seamDelta, windowMax, naturalMax, rmsA, rmsB, morphMs,
           pass ? "CLICK-FREE XFADE" : (clickFree ? "*** NO XFADE ***" : "*** STEP ***"));
    return pass;
}

int main()
{
    const int sr = 48000;
    juce::ScopedJuceInitialiser_GUI juceInit; // safe init for any JUCE statics

    printf("Held-note Regen-XFade audition (A=220Hz/0.50 -> B=660Hz/0.85, default 200 ms):\n");
    bool ok = true;
    ok &= runCase("unity (bypass path)",  1.0,                       200.0f, "/tmp/sampler_follow_unity.wav",   sr);
    ok &= runCase("pitched +7 (stretch)", std::pow(2.0, 7.0 / 12.0), 200.0f, "/tmp/sampler_follow_pitched.wav", sr);
    printf("WAVs: /tmp/sampler_follow_unity.wav  /tmp/sampler_follow_pitched.wav\n");
    printf("%s\n", ok ? "ALL PASS" : "*** FAIL ***");
    return ok ? 0 : 1;
}
