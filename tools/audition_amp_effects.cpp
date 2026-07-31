// audition_amp_effects.cpp — put a sound through the REAL amplifier chain so it
// can be heard, not only measured.
//
// Links against src/dsp/AmpEffects.cpp itself, and drives it through the same
// calls processBlock makes, in host-sized blocks. The chain has no UI yet (BJ,
// 2026-07-31: „ggf noch ohne UI"), so without this there is no way to hear what
// twelve new parameters do — and this project's rule is that DSP is heard before
// it is believed.
//
// Build (standalone c++, mirrors the flags CMake gives src/):
//   R=$PWD
//   DEFS=$(sed -n '14p' build_clean/CMakeFiles/T5ynth.dir/flags.make | sed 's/^CXX_DEFINES = //')
//   INCS=$(sed -n '/^CXX_INCLUDES/p' build_clean/CMakeFiles/T5ynth.dir/flags.make | sed 's/^CXX_INCLUDES = //')
//   eval c++ -std=gnu++20 -O2 $DEFS $INCS -I $R/src -Wno-everything \
//     tools/audition_amp_effects.cpp src/dsp/AmpEffects.cpp \
//     build_clean/T5ynth_artefacts/Release/libakroasys_SharedCode.a \
//     third_party/csound/macos-arm64/lib/CsoundLib64 build_clean/libT5ynthData.a \
//     -framework CoreAudio -framework CoreMIDI -framework AudioToolbox \
//     -framework Accelerate -framework WebKit -framework QuartzCore -framework Cocoa \
//     -framework Foundation -framework IOKit -framework Security -framework CoreAudioKit \
//     -framework DiscRecording -weak_framework Metal -weak_framework MetalKit \
//     -o tools/audition_amp_effects
//
// Usage:
//   audition_amp_effects <in.wav> <out.wav> [key=value ...]
// Keys, all optional, all defaulting to the parameter's own OFF value:
//   dist_drive dist_mix trem_rate trem_amt trem_stereo
//   chorus_rate chorus_amt chorus_mix phaser_rate phaser_amt phaser_fb phaser_mix
//   block   (host block size, default 512 — vary it to hear the chunking hold)
//   rumble  (scales the supply's ring, default 1 = the model; 0 removes it.
//            NOT a plugin parameter — it exists so the page can A/B that one term)
//
// A mono input is widened to stereo first, because two of these four are stereo
// effects and a suitcase tremolo has nothing to say in mono.

#include <JuceHeader.h>
#include "dsp/AmpEffects.h"

#include <cstdio>
#include <map>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::fprintf(stderr, "usage: %s <in.wav> <out.wav> [key=value ...]\n", argv[0]);
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::map<std::string, float> p {
        { "dist_drive", 0.0f }, { "dist_mix", 0.0f },
        { "trem_rate", 5.5f }, { "trem_amt", 0.0f }, { "trem_stereo", 0.0f },
        { "chorus_rate", 0.8f }, { "chorus_amt", 0.35f }, { "chorus_mix", 0.0f },
        { "phaser_rate", 0.4f }, { "phaser_amt", 0.5f }, { "phaser_fb", 0.0f },
        { "phaser_mix", 0.0f }, { "block", 512.0f }, { "rumble", 1.0f },
    };
    for (int i = 3; i < argc; ++i)
    {
        const std::string a = argv[i];
        const auto eq = a.find('=');
        if (eq == std::string::npos) { std::fprintf(stderr, "not a key=value: %s\n", argv[i]); return 2; }
        const std::string k = a.substr(0, eq);
        if (p.find(k) == p.end()) { std::fprintf(stderr, "unknown key: %s\n", k.c_str()); return 2; }
        p[k] = std::stof(a.substr(eq + 1));
    }

    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd(fm.createReaderFor(juce::File(argv[1])));
    if (rd == nullptr) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }

    const int n = static_cast<int>(rd->lengthInSamples);
    const double sr = rd->sampleRate;
    juce::AudioBuffer<float> buf(2, n);
    buf.clear();
    rd->read(&buf, 0, n, 0, true, rd->numChannels > 1);
    if (rd->numChannels == 1) buf.copyFrom(1, 0, buf, 0, 0, n);   // widen: two of these are stereo

    const int block = juce::jmax(1, static_cast<int>(p["block"]));

    T5ynthDistortion dist; T5ynthChorus chorus; T5ynthPhaser phaser; T5ynthTremolo trem;
    dist.prepare(sr, block); chorus.prepare(sr, block); phaser.prepare(sr, block); trem.prepare(sr, block);
    dist.setDrive(p["dist_drive"]);   dist.setMix(p["dist_mix"]);
    dist.setRumbleScale(p["rumble"]);
    chorus.setRate(p["chorus_rate"]); chorus.setDepth(p["chorus_amt"]); chorus.setMix(p["chorus_mix"]);
    phaser.setRate(p["phaser_rate"]); phaser.setDepth(p["phaser_amt"]);
    phaser.setFeedback(p["phaser_fb"]); phaser.setMix(p["phaser_mix"]);
    trem.setRate(p["trem_rate"]);     trem.setDepth(p["trem_amt"]); trem.setStereo(p["trem_stereo"]);

    // Block by block, in the plugin's own order.
    juce::AudioBuffer<float> chunk(2, block);
    for (int start = 0; start < n; start += block)
    {
        const int len = juce::jmin(block, n - start);
        chunk.setSize(2, len, false, false, true);
        for (int ch = 0; ch < 2; ++ch) chunk.copyFrom(ch, 0, buf, ch, start, len);
        dist.processBlock(chunk);
        chorus.processBlock(chunk);
        phaser.processBlock(chunk);
        trem.processBlock(chunk);
        for (int ch = 0; ch < 2; ++ch) buf.copyFrom(ch, start, chunk, ch, 0, len);
    }

    juce::File out(argv[2]);
    out.deleteFile();
    std::unique_ptr<juce::FileOutputStream> os(out.createOutputStream());
    if (os == nullptr) { std::fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> wr(
        wav.createWriterFor(os.release(), sr, 2, 24, {}, 0));
    if (wr == nullptr) { std::fprintf(stderr, "cannot make a writer\n"); return 1; }
    wr->writeFromAudioSampleBuffer(buf, 0, n);
    wr.reset();

    std::printf("%s  %d samples @ %.0f Hz, blocks of %d, peak %.4f\n",
                argv[2], n, sr, block, buf.getMagnitude(0, n));
    return 0;
}
