// Offline audition for held-note sample-follow (adoptSharedBuffer + declicker).
//
// Reproduces the A/B-drift regenerate seam: a held sampler voice shares buffer A,
// then the master publishes a very different buffer B and the voice adopts it
// live (adoptSharedBuffer). We render straight through the seam and measure the
// sample-to-sample discontinuity vs. the signal's natural per-sample delta, for
// both render paths (unity → bypass/processSample, pitched → Signalsmith stretch).
//
// Build: see tools/build_sampler_follow.sh (links the plugin's static lib).
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

struct SeamReport { float seamDelta; float naturalMaxDelta; int seamIndex; };

static SeamReport runCase(const std::string& label, double ratio,
                          const std::string& wavOut, int sr)
{
    const int block = 512;
    const int preBlocks = 40;    // ~0.43 s held on buffer A
    const int postBlocks = 40;   // ~0.43 s on buffer B after the live adopt

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

    const int seamIndex = static_cast<int>(out.size()); // first sample off buffer B

    // The A/B-drift regenerate handoff: master republishes, held voice adopts live.
    master.loadBuffer(bufB, sr);
    voice.adoptSharedBuffer(master);

    for (int b = 0; b < postBlocks; ++b) {
        voice.renderPitchedBlock(blockBuf.data(), block);
        out.insert(out.end(), blockBuf.begin(), blockBuf.end());
    }

    // Seam discontinuity vs. the natural per-sample delta away from the seam.
    float seamDelta = std::abs(out[seamIndex] - out[seamIndex - 1]);
    float naturalMax = 0.0f;
    for (int i = seamIndex + 400; i < static_cast<int>(out.size()) - 1; ++i)
        naturalMax = std::max(naturalMax, std::abs(out[i + 1] - out[i]));
    // Max per-sample delta THROUGH the seam + declicker decay window (~3 ms).
    float decayMax = 0.0f;
    for (int i = seamIndex - 1; i < seamIndex + 300; ++i)
        decayMax = std::max(decayMax, std::abs(out[i + 1] - out[i]));
    // What the step WOULD have been with a naive hard swap (no declick).
    float hardStep = std::abs(out[seamIndex] /*==prevOut_*/ - 0.0f); // ref only

    writeWav(wavOut, out, sr);

    printf("  [%-22s] seamDelta=%.5f  decayWindowMax=%.5f  naturalMax=%.5f  ratio=%.4f -> %s\n",
           label.c_str(), seamDelta, decayMax, naturalMax, ratio,
           (decayMax <= naturalMax * 1.5f + 1.0e-4f) ? "CLICK-FREE" : "*** STEP ***");
    (void) hardStep;
    return { seamDelta, naturalMax, seamIndex };
}

int main()
{
    const int sr = 48000;
    juce::ScopedJuceInitialiser_GUI juceInit; // safe init for any JUCE statics

    printf("Held-note sample-follow audition (A=220Hz/0.50, B=660Hz/0.85):\n");
    runCase("unity (bypass path)",   1.0,                                   "/tmp/sampler_follow_unity.wav",   sr);
    runCase("pitched +7 (stretch)",  std::pow(2.0, 7.0 / 12.0),             "/tmp/sampler_follow_pitched.wav", sr);
    printf("WAVs: /tmp/sampler_follow_unity.wav  /tmp/sampler_follow_pitched.wav\n");
    return 0;
}
