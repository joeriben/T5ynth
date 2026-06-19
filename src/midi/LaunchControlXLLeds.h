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
    static constexpr int kColorBound  = 60;  // G=3, R=0, flag → green full  (CC bound)
    static constexpr int kColorLearn  = 29;  // G=1, R=1, flag → amber low   (waiting for CC)

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

    // ── Page 1 default bindings: { paramId, CC } ───────────────────────────
    // Maps the agreed T5ynth parameter layout onto XL Page 1 controls.
    // Applied by PluginProcessor::applyXLDefaultBindings() on user request.
    //
    // Row 1 (CC 13-20): Env1 A/D/S/R — LFO1 Rate/Amt — Drift1 Rate/Amt
    // Row 2 (CC 29-36): Env2 A/D/S/R — LFO2 Rate/Amt — Drift2 Rate/Amt
    // Row 3 (CC 49-56): Env3 A/D/S/R — LFO3 Rate/Amt — Drift3 Rate/Amt
    // Faders (CC 77-84): Alpha — Resynth — Cutoff — Res — Drive — DlyMix — RevMix — AmpAmt
    struct Binding { const char* paramId; int cc; };

    static constexpr Binding kPage1[] = {
        // Row 1 — Env1 (amp) + LFO1 + Drift1
        { "amp_attack",   13 }, { "amp_decay",  14 },
        { "amp_sustain",  15 }, { "amp_release", 16 },
        { "lfo1_rate",    17 }, { "lfo1_depth",  18 },
        { "drift1_rate",  19 }, { "drift1_depth", 20 },

        // Row 2 — Env2 (mod1) + LFO2 + Drift2
        { "mod1_attack",  29 }, { "mod1_decay",  30 },
        { "mod1_sustain", 31 }, { "mod1_release", 32 },
        { "lfo2_rate",    33 }, { "lfo2_depth",  34 },
        { "drift2_rate",  35 }, { "drift2_depth", 36 },

        // Row 3 — Env3 (mod2) + LFO3 + Drift3  (CC 49-56 — VERIFY)
        { "mod2_attack",  49 }, { "mod2_decay",  50 },
        { "mod2_sustain", 51 }, { "mod2_release", 52 },
        { "lfo3_rate",    53 }, { "lfo3_depth",  54 },
        { "drift3_rate",  55 }, { "drift3_depth", 56 },

        // Faders — Generation | Filter | FX | Vol
        { "gen_alpha",       77 }, { "resynth_amount",  78 },
        { "filter_cutoff",   79 }, { "filter_resonance", 80 },
        { "filter_drive",    81 },
        { "delay_mix",       82 }, { "reverb_mix",       83 },
        { "amp_amount",      84 },
    };

    static constexpr int kPage1Count = static_cast<int>(std::size(kPage1));
};
