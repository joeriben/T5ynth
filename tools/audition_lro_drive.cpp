// audition_lro_drive.cpp — put an oscillator render through the REAL ladder, so
// a `drive` that is meant to overdrive the FILTER can be heard doing it.
//
// Why this exists. `analog_osc`'s drive used to be a waveshaper inside the
// Csound body, held to the clean level by two `balance` calls. BJ, 2026-08-01,
// against a Minimoog V External-Input-Volume recording: „drive macht nichts
// außer bei gleicher oder geringerer lautstärke eine rechteckwelle aus allem zu
// machen. das hat nicht mit einem Moog-Drive zu tun." The reference measures the
// opposite of what that shaper does — +16 dB of level, crest 2.31 → 1.65, and a
// centroid that FALLS from 3250 Hz to ~600 — because on that instrument the
// saturating element is the ladder's own input stage, not the oscillator.
//
// So the oscillator can only be judged with the ladder behind it, and rendering
// the Csound body alone would show a gain change and nothing else. This links
// against src/dsp/LadderFilter.h itself — the project's Huovilainen-style
// non-linear ladder, per-stage 2·Vt·tanh(x/2·Vt), Vt = 1.22 — and drives it with
// the same calls SynthVoice makes, so what is heard here is what the voice does.
//
// Deliberately NOT level-matched anywhere: that is the whole subject.
//
// Build (standalone c++, mirrors the flags CMake gives src/ — same recipe as
// tools/audition_amp_effects.cpp):
//   R=$PWD
//   DEFS=$(sed -n '14p' build_clean/CMakeFiles/T5ynth.dir/flags.make | sed 's/^CXX_DEFINES = //')
//   INCS=$(sed -n '/^CXX_INCLUDES/p' build_clean/CMakeFiles/T5ynth.dir/flags.make | sed 's/^CXX_INCLUDES = //')
//   eval c++ -std=gnu++20 -O2 $DEFS $INCS -I $R/src -Wno-everything \
//     tools/audition_lro_drive.cpp \
//     build_clean/T5ynth_artefacts/Release/libakroasys_SharedCode.a \
//     third_party/csound/macos-arm64/lib/CsoundLib64 build_clean/libT5ynthData.a \
//     -framework CoreAudio -framework CoreMIDI -framework AudioToolbox \
//     -framework Accelerate -framework WebKit -framework QuartzCore -framework Cocoa \
//     -framework Foundation -framework IOKit -framework Security -framework CoreAudioKit \
//     -framework DiscRecording -weak_framework Metal -weak_framework MetalKit \
//     -o tools/audition_lro_drive
//
// Usage:
//   audition_lro_drive <in.wav> <out.wav> [key=value ...]
// Keys, all optional, all defaulting to the parameter's SHIPPED default:
//   cutoff (Hz, default 20000)   reso (0..1, default 0)   drive_db (0..36, default 0)
//   slope  (0..3 = 6/12/18/24 dB/oct, default 3)          type (0 LP, 1 HP, 2 BP)
//   mix    (0..1, default 1)     gain (pre-ladder gain applied to the file, default 1)
//
// Prints the rms, peak and crest of input and output, because the point of the
// change is a level and a crest, and a WAV alone does not say them.

#include <JuceHeader.h>
#include "dsp/LadderFilter.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

namespace
{
    struct Stats { double rms = 0.0, peak = 0.0; };

    Stats measure (const juce::AudioBuffer<float>& b)
    {
        Stats s;
        double sum = 0.0;
        const int n = b.getNumSamples();
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < n; ++i)
            {
                const double v = b.getSample (c, i);
                sum += v * v;
                s.peak = juce::jmax (s.peak, std::abs (v));
            }
        s.rms = std::sqrt (sum / juce::jmax (1, n * b.getNumChannels()));
        return s;
    }

    void report (const char* what, const Stats& s)
    {
        const double dB = s.rms > 0.0 ? 20.0 * std::log10 (s.rms) : -999.0;
        std::printf ("  %-6s rms %8.5f (%7.2f dB)   peak %7.4f   crest %5.2f\n",
                     what, s.rms, dB, s.peak,
                     s.rms > 0.0 ? s.peak / s.rms : 0.0);
    }
}

int main (int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf ("usage: audition_lro_drive <in.wav> <out.wav> [key=value ...]\n");
        return 1;
    }

    std::map<std::string, float> kv;
    for (int i = 3; i < argc; ++i)
    {
        const std::string a { argv[i] };
        const auto eq = a.find ('=');
        if (eq == std::string::npos) { std::printf ("bad arg: %s\n", argv[i]); return 1; }
        kv[a.substr (0, eq)] = std::stof (a.substr (eq + 1));
    }
    auto get = [&] (const char* k, float dflt) {
        const auto it = kv.find (k);
        return it == kv.end() ? dflt : it->second;
    };

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    juce::File inFile  = juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]);
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (inFile));
    if (rd == nullptr) { std::printf ("cannot read %s\n", argv[1]); return 1; }

    const int  n  = (int) rd->lengthInSamples;
    const auto sr = rd->sampleRate;
    juce::AudioBuffer<float> buf (1, n);
    {
        juce::AudioBuffer<float> src ((int) rd->numChannels, n);
        rd->read (&src, 0, n, 0, true, true);
        buf.clear();
        for (int c = 0; c < src.getNumChannels(); ++c)
            buf.addFrom (0, 0, src, c, 0, n, 1.0f / (float) src.getNumChannels());
    }

    const float preGain = get ("gain", 1.0f);
    if (preGain != 1.0f)
        buf.applyGain (preGain);
    const auto before = measure (buf);

    // The same calls SynthVoice makes for FilterAlgorithm::Ladder, with the
    // shipped defaults where nothing was asked for. filter_drive stays at 0 dB
    // on purpose: what is under test is the level the OSCILLATOR delivers, and
    // a filter drive on top would hide it.
    LadderFilter lad;
    lad.prepare (sr, n);
    lad.setCutoff    (get ("cutoff", 20000.0f));
    lad.setResonance (get ("reso", 0.0f));
    lad.setType      ((int) get ("type", 0.0f));
    lad.setSlope     ((int) get ("slope", 3.0f));
    lad.setMix       (get ("mix", 1.0f));
    lad.setInputDrive (std::pow (10.0f, get ("drive_db", 0.0f) / 20.0f));

    auto* w = buf.getWritePointer (0);
    for (int i = 0; i < n; ++i)
        w[i] = lad.processSample (w[i]);

    const auto after = measure (buf);
    std::printf ("%s -> %s   (%.0f Hz, %d samples)\n", argv[1], argv[2], sr, n);
    report ("in",  before);
    report ("out", after);
    std::printf ("  level change %+.2f dB\n",
                 20.0 * std::log10 (juce::jmax (1.0e-9, after.rms)
                                    / juce::jmax (1.0e-9, before.rms)));

    juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
    outFile.deleteFile();
    std::unique_ptr<juce::FileOutputStream> os (outFile.createOutputStream());
    if (os == nullptr) { std::printf ("cannot write %s\n", argv[2]); return 1; }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> wr (
        wav.createWriterFor (os.release(), sr, 1, 24, {}, 0));
    if (wr == nullptr) { std::printf ("cannot create writer\n"); return 1; }
    wr->writeFromAudioSampleBuffer (buf, 0, n);
    return 0;
}
