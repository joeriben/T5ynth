#pragma once
#include <JuceHeader.h>

// ── Novation Launch Control XL — LED protocol + Page 1 default CC bindings ──
//
// IMPORTANT: All constants below must be verified against the official
// "Launch Control XL Programmer's Reference Guide" PDF (Focusrite downloads).
// The values here are derived from multiple community sources and empirical
// testing; they are believed correct for Template 1 but should be confirmed
// on the physical device before shipping.
//
// LED protocol (Template 1, MIDI channel 9 = index 8):
//   To set an LED: NoteOn ch9, note = <led_note>, vel = <color_velocity>
//   Color velocity = (16 * green_level) + red_level + kCopyFlag
//     green_level, red_level: 0 (off) … 3 (full)
//     kCopyFlag (12): write directly to LED without double-buffer swap
//   Off: vel = kColorOff  (12 = copy flag, no color bits)
//
// CC → LED note mapping (knob rows): CC# == LED note# on the XL.
// Faders: CC 77-84; their LED note numbers also match the CC numbers.
// ── VERIFY all of the above on hardware before committing to release ──

struct LaunchControlXLLeds
{
    // ── Protocol constants ──────────────────────────────────────────────────
    static constexpr int kMidiChannel = 9;   // 1-indexed (JUCE MidiMessage uses 1-based)

    // Color velocities: vel = (16*G + R + kCopyFlag), G/R in 0..3
    static constexpr int kCopyFlag    = 12;
    static constexpr int kColorOff    = 12;  // G=0, R=0, flag → LED off (registers state)

    // Generic "bound by CC Learn" color (green) — used when learn binds an
    // arbitrary CC that is not in the kPage1 default layout.
    static constexpr int kColorBound  = 60;  // G=3, R=0, flag → green full

    // Waiting-for-CC color during an active CC Learn session (amber low).
    static constexpr int kColorLearn  = 29;  // G=1, R=1, flag → amber low

    // ── Module accent colors — matched to T5ynth UI palette ─────────────────
    // XL palette: green (G=3,R=0), yellow (G=3,R=2), amber (G=3,R=3),
    //             amber-low (G=1,R=1), red-low (G=0,R=1), red (G=0,R=3).
    // UI → XL mapping:
    //   kEnvCol  (Amber FF6F00) → amber full (63)  — envelopes
    //   kLfoCol  (Amber FF6F00) → amber low  (29)  — LFOs (same palette, lower saturation)
    //   kDriftCol (Dark amber)  → red low    (13)  — drift (random/subtle → dim red)
    //   kFilterCol (Violet)     → red full   (15)  — filter (XL has no violet; red = "cut")
    //   kFxCol   (Cyan 00BCD4)  → green full (60)  — FX delay/reverb (XL has no cyan)
    //   kOscCol  (Periwinkle)   → full amber (63)  — generation controls (highest salience)
    //   Amp amount              → green low  (28)  — output level
    static constexpr int kColorGen    = 63;  // G=3, R=3 → full amber / orange — Alpha, Resynth
    static constexpr int kColorEnv    = 62;  // G=3, R=2 → yellow               — Envelope A/D/S/R
    static constexpr int kColorLfo    = 29;  // G=1, R=1 → amber low             — LFO Rate/Depth
    static constexpr int kColorDrift  = 13;  // G=0, R=1 → red low               — Drift Rate/Depth
    static constexpr int kColorFilter = 15;  // G=0, R=3 → red full              — Filter (cut)
    static constexpr int kColorFx     = 60;  // G=3, R=0 → green full            — Delay / Reverb
    static constexpr int kColorVol    = 28;  // G=1, R=0 → green low             — Amp amount

    // ── CC ranges (Template 1) ──────────────────────────────────────────────
    // Row 1 top knobs:    CC 13-20
    // Row 2 mid knobs:    CC 29-36
    // Row 3 bottom knobs: CC 49-56  (VERIFY — some sources list 45-52)
    // Faders:             CC 77-84
    // All on MIDI ch 9.

    // Map CC number → LED note index. Returns -1 if CC is not an XL control.
    // Assumes CC# == LED note# for all knob rows and faders (verify on device).
    static int ccToLedNote(int cc) noexcept
    {
        if (cc >= 13 && cc <= 20) return cc;   // Row 1
        if (cc >= 29 && cc <= 36) return cc;   // Row 2
        if (cc >= 49 && cc <= 56) return cc;   // Row 3  — VERIFY
        if (cc >= 77 && cc <= 84) return cc;   // Faders — VERIFY
        return -1;
    }

    // Build a NoteOn LED-on message.
    static juce::MidiMessage ledOn(int ledNote, int colorVelocity)
    {
        return juce::MidiMessage::noteOn(kMidiChannel, ledNote,
                                         static_cast<uint8_t>(colorVelocity));
    }

    // Build a NoteOn LED-off message (vel = kColorOff to apply copy flag).
    static juce::MidiMessage ledOff(int ledNote)
    {
        return juce::MidiMessage::noteOn(kMidiChannel, ledNote,
                                         static_cast<uint8_t>(kColorOff));
    }

    // ── Page 1 default bindings: { paramId, CC, color } ───────────────────
    // Maps the agreed T5ynth parameter layout onto XL Page 1 controls.
    // Applied by PluginProcessor::applyXLDefaultBindings() on user request.
    //
    // Row 1 (CC 13-20): Env1 A/D/S/R — LFO1 Rate/Amt — Drift1 Rate/Amt
    // Row 2 (CC 29-36): Env2 A/D/S/R — LFO2 Rate/Amt — Drift2 Rate/Amt
    // Row 3 (CC 49-56): Env3 A/D/S/R — LFO3 Rate/Amt — Drift3 Rate/Amt
    // Faders (CC 77-84): Alpha — Resynth — Cutoff — Res — Drive — DlyMix — RevMix — AmpAmt
    struct Binding { const char* paramId; int cc; int color; };

    static constexpr Binding kPage1[] = {
        // Row 1 — Env1 (amp) + LFO1 + Drift1
        { "amp_attack",    13, kColorEnv   }, { "amp_decay",    14, kColorEnv   },
        { "amp_sustain",   15, kColorEnv   }, { "amp_release",  16, kColorEnv   },
        { "lfo1_rate",     17, kColorLfo   }, { "lfo1_depth",   18, kColorLfo   },
        { "drift1_rate",   19, kColorDrift }, { "drift1_depth", 20, kColorDrift },

        // Row 2 — Env2 (mod1) + LFO2 + Drift2
        { "mod1_attack",   29, kColorEnv   }, { "mod1_decay",   30, kColorEnv   },
        { "mod1_sustain",  31, kColorEnv   }, { "mod1_release", 32, kColorEnv   },
        { "lfo2_rate",     33, kColorLfo   }, { "lfo2_depth",   34, kColorLfo   },
        { "drift2_rate",   35, kColorDrift }, { "drift2_depth", 36, kColorDrift },

        // Row 3 — Env3 (mod2) + LFO3 + Drift3  (CC 49-56 — VERIFY)
        { "mod2_attack",   49, kColorEnv   }, { "mod2_decay",   50, kColorEnv   },
        { "mod2_sustain",  51, kColorEnv   }, { "mod2_release", 52, kColorEnv   },
        { "lfo3_rate",     53, kColorLfo   }, { "lfo3_depth",   54, kColorLfo   },
        { "drift3_rate",   55, kColorDrift }, { "drift3_depth", 56, kColorDrift },

        // Faders — Generation | Filter | FX | Vol
        { "gen_alpha",        77, kColorGen    }, { "resynth_amount",   78, kColorGen    },
        { "filter_cutoff",    79, kColorFilter }, { "filter_resonance", 80, kColorFilter },
        { "filter_drive",     81, kColorFilter },
        { "delay_mix",        82, kColorFx     }, { "reverb_mix",       83, kColorFx     },
        { "amp_amount",       84, kColorVol    },
    };

    static constexpr int kPage1Count = static_cast<int>(std::size(kPage1));
};
