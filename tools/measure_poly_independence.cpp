// Does an added note change a note that is already held?
//
// BJ, 2026-08-04: "Offensichtlich ist die Filter-Envelope nicht polyphon. es
// verhaelt sich paraphon ... ich hoere SEHR deutlich etwas das alle gehaltenen
// noten betrifft." The cutoff bus in SynthVoice is per-voice at every source
// (its own envelopes, its own note, its own aftertouch), so reading the code
// answers nothing here. What the ear reports is a COUPLING between voices, and
// a coupling is measurable without knowing where it lives: a chain of linear,
// per-voice stages is ADDITIVE, and any stage that is shared and level-
// dependent is not.
//
// So this drives the REAL T5ynthProcessor three times with the same settings:
//   A   note 60 alone, held
//   B   note 67 alone, entering at t = tB, held
//   AB  both, same onsets
// and asks two questions of the result, per 25 ms window:
//
//   A-in-mix   the least-squares amount of A present in AB, in dB:
//              <AB,A>/<A,A>. Two different pitches decorrelate over a window,
//              so this IS the held note's own level inside the chord. 0 dB
//              means the held note is untouched by the arrival of the other;
//              a step down at tB is the paraphony, in the unit the ear uses.
//   resid      ||AB - (A+B)|| / ||A+B||, the whole superposition error --
//              level AND timbre, so a stage that changes the held note's
//              COLOUR rather than its level cannot hide from it.
//
// Then it repeats the measurement with one suspect removed at a time, which is
// what turns a number into a cause: the master limiter (threshold to 0 dB),
// the level reaching it (master volume down 12 dB), and the filter envelope
// itself (ENV2 target -> none). Two couplings are KNOWN and expected, and are
// printed as reference lines rather than left for the reader to rediscover:
// VoiceManager's 1/N^0.1 voice-count compensation is -0.30 dB at the 1->2
// transition by construction, and the always-on limiter is shared by everything
// downstream of the voice sum.
//
// WHAT IT FOUND, 2026-08-04, 48 kHz / 512, wavetable engine, LP at 200 Hz with
// ENV2 -> Filter:
//
//   * The filter envelope is POLYPHONIC. With the level into the master stage
//     lowered -- either at the master volume (downstream of every voice) or at
//     the voices themselves -- a full-amount filter envelope leaves the held
//     note's first two partials at exactly -0.60 dB, which IS VoiceManager's
//     1/N^0.1 voice-count compensation at 1->2 voices, and the superposition
//     residual sits at -23.5 dB, i.e. that same -0.60 dB and nothing else.
//     Everything from the voices through the FX to the master gain is additive.
//
//   * What couples the voices is the MASTER LIMITER, and it is not a limiter.
//     `juce::dsp::Limiter` (juce_Limiter.cpp, update()) is TWO compressors: a
//     4:1 stage at a FIXED -10 dBFS threshold with a 200 ms release, which the
//     "Limiter Threshold" control does not reach at all, then the brickwall at
//     the threshold, then a makeup of 10^(7.5/40) * 10^(-threshold/20) = +6.75
//     dB at the shipped -3 dB. So the synth's output level is BORROWED from a
//     compressor that is permanently working, and every new note's filter
//     envelope pulls all of it down: measured -5.7 dB on the held note the
//     instant the second note sounds, still -3.4 dB half a second later.
//
//   * It is level-dependent, not threshold-dependent, which is what identifies
//     the fixed first stage rather than the brickwall: raising the user
//     threshold to 0 dB changes NOTHING (-5.66 dB against -5.69), lowering the
//     level 12-18 dB removes it entirely, and driving the threshold to -24 dB
//     makes it 29 dB.
//
// Build (same response-file recipe as tools/measure_note_latency.cpp):
//   FLAGS=build_clean/CMakeFiles/T5ynth.dir/flags.make
//   { grep -m1 CXX_DEFINES "$FLAGS"; grep -m1 CXX_INCLUDES "$FLAGS"; } \
//     | sed 's/^CXX_[A-Z]* = //' > /tmp/h.rsp
//   echo -I$PWD/build_clean/_deps/signalsmith_stretch-src >> /tmp/h.rsp
//   CSOUND_FW="$(brew --prefix csound)/Frameworks"
//   clang++ -std=c++17 -O2 @/tmp/h.rsp \
//     tools/measure_poly_independence.cpp \
//     build_clean/T5ynth_artefacts/Release/libT5ynth_SharedCode.a \
//     -F"$CSOUND_FW" -framework CsoundLib64 -Wl,-rpath,"$CSOUND_FW" \
//     -framework Accelerate -framework AudioToolbox -framework Cocoa -framework CoreAudio \
//     -framework CoreAudioKit -framework CoreMIDI -framework DiscRecording -framework Foundation \
//     -framework IOKit -framework QuartzCore -framework Security -framework WebKit \
//     -weak_framework Metal -weak_framework MetalKit \
//     -o tools/measure_poly_independence
//   tools/measure_poly_independence [sampleRate] [blockSize]

// CoreFoundation FIRST: JuceHeader.h pulls in `using namespace juce`, after
// which MacTypes.h's `struct Point` collides with juce::Point.
#include <CoreFoundation/CoreFoundation.h>

#include "JuceHeader.h"
#include "PluginProcessor.h"
#include "dsp/BlockParams.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
    double gSampleRate = 48000.0;
    int    gBlockSize  = 512;

    constexpr int   kNoteA   = 60;      // C4, 261.63 Hz
    // A TRITONE, not a fifth. Both notes are saws, so a fifth's partials LAND ON
    // each other (B's 2nd == A's 3rd) and every reading below would be measuring
    // the interference of two partials at the same frequency rather than any
    // coupling between the voices -- measured, that alone moved the projection
    // by +-4 dB with nothing shared in the chain at all. At 2^(6/12) the two
    // harmonic series stay apart: A's 261.6 / 523.3 / 784.9 against B's 370.0 /
    // 740.0 / 1110.0, the closest pair 45 Hz apart, which a 100 ms window
    // resolves with room to spare.
    constexpr int   kNoteB   = 66;      // F#4, 369.99 Hz
    constexpr double kTotalS = 3.0;
    constexpr double kOnsetB = 1.5;     // A is fully into its sustain by then

    void pump (int ms)
    {
        CFRunLoopRunInMode (kCFRunLoopDefaultMode, (double) ms * 0.001, false);
    }

    void setParam (T5ynthProcessor& proc, const char* id, float realValue)
    {
        auto& s = proc.getValueTreeState();
        if (auto* p = s.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (realValue));
        else
            std::printf ("  !! no such parameter: %s\n", id);
    }

    // A stand-in for a generated sample: an instantly-starting band-limited saw,
    // so the engine has real harmonic content for a filter to act on and no
    // onset of its own to confuse the reading.
    juce::AudioBuffer<float> source (double sr)
    {
        const int n = (int) (sr * 2.0);
        juce::AudioBuffer<float> b (1, n);
        auto* w = b.getWritePointer (0);
        const double f0 = 220.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / sr;
            double v = 0.0;
            for (int h = 1; h <= 24; ++h)
                v += std::sin (2.0 * juce::MathConstants<double>::pi * f0 * h * t) / h;
            w[i] = (float) (0.5 * v / 1.9);
        }
        return b;
    }

    // One render. `playA`/`playB` decide which of the two notes is present;
    // both are held to the end, and the state is reset first so the three
    // renders of a configuration are comparable sample for sample.
    std::vector<float> render (T5ynthProcessor& proc, bool playA, bool playB)
    {
        proc.prepareToPlay (gSampleRate, gBlockSize);   // deterministic start state
        pump (60);

        const int totalSamples = (int) (gSampleRate * kTotalS);
        const int onsetBSample = (int) (gSampleRate * kOnsetB);

        std::vector<float> out;
        out.reserve ((size_t) totalSamples);

        juce::AudioBuffer<float> buf (2, gBlockSize);
        juce::MidiBuffer midi;

        for (int pos = 0; pos < totalSamples; pos += gBlockSize)
        {
            buf.clear();
            midi.clear();
            if (playA && pos == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, kNoteA, (juce::uint8) 100), 0);
            if (playB && onsetBSample >= pos && onsetBSample < pos + gBlockSize)
                midi.addEvent (juce::MidiMessage::noteOn (1, kNoteB, (juce::uint8) 100),
                               onsetBSample - pos);
            proc.processBlock (buf, midi);

            const auto* l = buf.getReadPointer (0);
            for (int i = 0; i < gBlockSize && pos + i < totalSamples; ++i)
                out.push_back (l[i]);
        }

        // Release both, and let the tails die before the next render.
        for (int b = 0; b < 40; ++b)
        {
            buf.clear();
            midi.clear();
            if (b == 0)
            {
                midi.addEvent (juce::MidiMessage::noteOff (1, kNoteA), 0);
                midi.addEvent (juce::MidiMessage::noteOff (1, kNoteB), 0);
            }
            proc.processBlock (buf, midi);
        }
        return out;
    }

    double rms (const std::vector<float>& v, int from, int n)
    {
        double a = 0.0;
        for (int i = from; i < from + n; ++i) a += (double) v[(size_t) i] * v[(size_t) i];
        return std::sqrt (a / n);
    }

    // The amplitude of ONE frequency in one Hann-windowed stretch. A single DFT
    // bin, computed directly, because only three frequencies are ever asked for
    // and each has to sit exactly on a partial rather than on a bin grid. This
    // is what keeps the reading about the HELD note: nothing of B's own series
    // is within a mainlobe of the frequencies asked here.
    double partialAmp (const std::vector<float>& v, int from, int n, double freq)
    {
        double re = 0.0, im = 0.0, wsum = 0.0;
        const double w = 2.0 * juce::MathConstants<double>::pi * freq / gSampleRate;
        for (int i = 0; i < n; ++i)
        {
            const double h = 0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi
                                                   * i / (n - 1));
            const double x = v[(size_t) (from + i)] * h;
            re += x * std::cos (w * i);
            im += x * std::sin (w * i);
            wsum += h;
        }
        return 2.0 * std::sqrt (re * re + im * im) / wsum;
    }

    void analyse (const char* label,
                  const std::vector<float>& a, const std::vector<float>& b,
                  const std::vector<float>& ab)
    {
        const int win  = (int) (gSampleRate * 0.100);
        const int n    = (int) juce::jmin (juce::jmin (a.size(), b.size()), ab.size());
        const int last = n - win;

        // The held note's own partials -- equal temperament, A4 = 440.
        const double f1 = 440.0 * std::pow (2.0, (kNoteA - 69) / 12.0);

        std::printf ("\n%s\n", label);
        std::printf ("   t (s)   held note's own partials, AB vs A alone (dB)"
                     "      resid    peak\n");
        std::printf ("            f1 %6.1f Hz   f2 %6.1f Hz   f3 %6.1f Hz"
                     "        (dB)   A / AB\n", f1, 2 * f1, 3 * f1);

        const double marks[] = { 0.8, 1.2, 1.35, 1.55, 1.7, 2.0, 2.4, 2.8 };
        double worst = 0.0, worstBefore = 0.0;

        for (double t : marks)
        {
            const int from = (int) (gSampleRate * t);
            if (from < 0 || from > last) continue;

            double d[3];
            for (int h = 0; h < 3; ++h)
            {
                const double aa  = partialAmp (a,  from, win, f1 * (h + 1));
                const double aba = partialAmp (ab, from, win, f1 * (h + 1));
                d[h] = (aa > 1.0e-7 && aba > 1.0e-7) ? 20.0 * std::log10 (aba / aa) : 0.0;
            }

            double res = 0.0, sum = 0.0;
            float pa = 0.0f, pab = 0.0f;
            for (int i = from; i < from + win; ++i)
            {
                const double dd = ab[(size_t) i] - (a[(size_t) i] + b[(size_t) i]);
                res += dd * dd;
                sum += (a[(size_t) i] + b[(size_t) i]) * (a[(size_t) i] + b[(size_t) i]);
                pa  = juce::jmax (pa,  std::abs (a[(size_t) i]));
                pab = juce::jmax (pab, std::abs (ab[(size_t) i]));
            }
            const double residDb = sum > 1.0e-18 ? 10.0 * std::log10 (res / sum) : -999.0;

            std::printf ("  %5.2f      %+6.2f         %+6.2f         %+6.2f"
                         "        %+6.1f   %.3f/%.3f\n",
                         t, d[0], d[1], d[2], residDb, pa, pab);

            const double m = juce::jmax (juce::jmax (std::abs (d[0]), std::abs (d[1])),
                                         std::abs (d[2]));
            if (t < kOnsetB) worstBefore = juce::jmax (worstBefore, m);
            else if (m > std::abs (worst)) worst = m;
        }

        std::printf ("  -> the HELD note's own partials move up to %.2f dB once the second"
                     " note sounds\n     (before it, the same reading is %.2f dB --"
                     " that is the tool's own floor)\n", worst, worstBefore);

        const int from = (int) (gSampleRate * 0.8);
        if (from + win < n)
            std::printf ("  -> levels: A alone %.4f rms, AB %.4f rms\n",
                         rms (a, from, win), rms (ab, from, win));
    }

    struct Config
    {
        const char* label;
        float modAmount;      // ENV2 amount
        int   modTarget;      // EnvTarget
        float limiterThresh;  // dB
        float masterVol;      // dB, -60..0 -- attenuative only
        float ampAmount = 1.0f;
        bool  csound = false;   // LRO instead of the wavetable engine
    };

    void run (T5ynthProcessor& proc, const Config& c)
    {
        if (c.csound)
        {
            proc.forceCsoundEngineMode();
            pump (400);
        }
        else
        {
            setParam (proc, PID::engineMode, 1.0f);   // Wavetable
            pump (200);
        }
        setParam (proc, PID::modEnv[0].amount, c.modAmount);
        setParam (proc, PID::modEnv[0].target, (float) c.modTarget);
        setParam (proc, PID::limiterThresh,    c.limiterThresh);
        setParam (proc, PID::masterVol,        c.masterVol);
        setParam (proc, PID::ampAmount,        c.ampAmount);
        pump (60);

        const auto a  = render (proc, true,  false);
        const auto b  = render (proc, false, true);
        const auto ab = render (proc, true,  true);
        analyse (c.label, a, b, ab);
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc > 1) gSampleRate = std::atof (argv[1]);
    if (argc > 2) gBlockSize  = std::atoi (argv[2]);

    std::printf ("does an added note change a note already held?"
                 "  (real T5ynthProcessor)\n");
    std::printf ("sample rate %.0f Hz, block %d, note A=%d at 0.00 s, note B=%d at %.2f s\n",
                 gSampleRate, gBlockSize, kNoteA, kNoteB, kOnsetB);
    std::printf ("known couplings, for reference:\n");
    std::printf ("  voice-count compensation 1/N^0.1 : 1->2 voices = %+.2f dB, by construction\n",
                 20.0 * std::log10 (1.0 / std::pow (2.0f, 0.1f)));
    std::printf ("  master limiter                   : always on, default threshold -3 dB\n");

    T5ynthProcessor proc;
    proc.setRateAndBufferSizeDetails (gSampleRate, gBlockSize);
    proc.prepareToPlay (gSampleRate, gBlockSize);
    pump (200);

    proc.loadGeneratedAudio (source (gSampleRate), gSampleRate);
    pump (600);

    setParam (proc, PID::engineMode, 1.0f);          // Wavetable: deterministic, no inference
    setParam (proc, PID::voiceCount, 5.0f);          // 16 voices
    pump (100);

    // A patch whose ONLY fast modulation is the filter envelope.
    setParam (proc, PID::ampAttack,  5.0f);
    setParam (proc, PID::ampDecay,   200.0f);
    setParam (proc, PID::ampSustain, 1.0f);
    setParam (proc, PID::ampRelease, 200.0f);
    setParam (proc, PID::ampAmount,  1.0f);
    setParam (proc, PID::ampTarget,  (float) EnvTarget::DCA);
    setParam (proc, PID::velAmt,     0.0f);          // velocity out of the picture

    setParam (proc, PID::modEnv[0].attack,  5.0f);
    setParam (proc, PID::modEnv[0].decay,   1500.0f);
    setParam (proc, PID::modEnv[0].sustain, 0.0f);
    setParam (proc, PID::modEnv[0].release, 500.0f);

    setParam (proc, PID::filterType,      1.0f);     // LP
    setParam (proc, PID::filterCutoff,    200.0f);
    setParam (proc, PID::filterResonance, 0.3f);
    setParam (proc, PID::filterKbdTrack,  0.0f);
    setParam (proc, PID::filterDrive,     0.0f);
    setParam (proc, PID::filterMix,       1.0f);

    // Everything else that could couple two voices, off.
    setParam (proc, PID::lfo1Depth, 0.0f);
    setParam (proc, PID::lfo2Depth, 0.0f);
    setParam (proc, PID::lfo3Depth, 0.0f);
    setParam (proc, PID::driftEnabled, 0.0f);
    setParam (proc, PID::delayMix,  0.0f);
    setParam (proc, PID::reverbMix, 0.0f);
    pump (100);

    const Config configs[] = {
        { "1. filter envelope at FULL amount, everything at default",
          1.0f, EnvTarget::Filter, -3.0f,   0.0f },
        { "2. same, but the master limiter cannot act (threshold 0 dB)",
          1.0f, EnvTarget::Filter,  0.0f,   0.0f },
        { "3. same as 1, but 18 dB quieter INTO the limiter (master vol -18 dB)",
          1.0f, EnvTarget::Filter, -3.0f, -18.0f },
        { "4. same as 1, but NO filter envelope (ENV2 -> none)",
          1.0f, EnvTarget::None,   -3.0f,   0.0f },
        { "5. filter envelope at half amount",
          0.5f, EnvTarget::Filter, -3.0f,   0.0f },
        { "6. no filter envelope AND no limiting -- the tool's own noise floor",
          1.0f, EnvTarget::None,    0.0f, -18.0f },
        // 7 and 8 are what turn "the loud configurations couple" into a cause.
        // 7 lowers the level at the SOURCE (the amp envelope) instead of at the
        // master, so everything downstream of the voices is untouched and only
        // how hard they hit the limiter changes. 8 does the opposite and drives
        // the limiter far harder at the same source level.
        { "7. filter envelope at FULL amount, but the VOICES are 12 dB quieter"
          " (amp amount 0.25)",
          1.0f, EnvTarget::Filter, -3.0f,   0.0f, 0.25f },
        { "8. filter envelope at FULL amount, limiter threshold -24 dB",
          1.0f, EnvTarget::Filter, -24.0f,  0.0f, 1.0f },
        // The LRO is the engine BJ reports as quiet, so the question the master
        // stage raises for it is a different one: how much of what it DOES have
        // is the makeup gain (static, keepable) and how much is compression
        // (dynamic, and the coupling). 10 against 9 answers it -- 9 lets the
        // stage act, 10 keeps the same makeup while the level stays far enough
        // below the fixed -10 dBFS first stage for it to be idle.
        { "9.  LRO (Csound engine), filter envelope at FULL amount, defaults",
          1.0f, EnvTarget::Filter, -3.0f,   0.0f, 1.0f,  true },
        { "10. LRO, same, but 18 dB below the master stage",
          1.0f, EnvTarget::Filter, -3.0f, -18.0f, 1.0f,  true },
    };

    for (const auto& c : configs)
        run (proc, c);

    std::printf ("\n");
    return 0;
}
