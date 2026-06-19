#pragma once
#include <JuceHeader.h>

// ── Novation Launch Control XL 3 (Mk3) — LED protocol + Page 1 CC bindings ──
//
// The LCXL3 has full-RGB LEDs set via SysEx (NOT the original XL's Note-On
// velocity palette). One control's LED is addressed by its "control index":
//
//   F0 00 20 29 02 15 01 53 <control-index> <R> <G> <B> F7     (R/G/B 0..127)
//
//   00 20 29 = Novation manufacturer ID, 02 15 = LCXL3 device, 01 53 = LED cmd.
//   juce::MidiMessage::createSysExMessage() adds the F0/F7, so we pass the
//   inner bytes only.
//
// DAW-mode mapping (programmer's reference p.9): control-index == CC#. The same
// indices the hardware transmits double as the LED colour indices — faders 5-12,
// encoders 13-36 (ch16), buttons 37-52 (ch1). If a control lights the WRONG LED,
// only the index mapping in ccToLedNote() needs adjusting — colors + send path stay.
// (Spec-confirmed; physical per-LED position still worth a once-over on device.)
//
// Source: Novation LCXL3 programmer's reference (DAW mode: LED SysEx p.12, layout p.9).

namespace lcxl3_detail
{
    // Pack a 7-bit RGB triplet into one int: (R<<16)|(G<<8)|B, each 0..127.
    constexpr int rgb (int r, int g, int b) noexcept { return (r << 16) | (g << 8) | b; }

    // One XL Page-1 binding. minNorm/maxNorm map the 0..127 CC onto the param's
    // normalized range (default 0..1; swap to invert a control, e.g. Alpha).
    // Kept at namespace scope (not nested in LaunchControlXLLeds) so these default
    // member initializers stay usable by kPage1's aggregate init regardless of how
    // the enclosing class's static members get instantiated — a nested struct's
    // defaults can be rejected as "needed within an incomplete enclosing class".
    struct Binding { const char* paramId; int cc; int color; float minNorm = 0.0f; float maxNorm = 1.0f; };
}

struct LaunchControlXLLeds
{
    // ── Module accent colors (7-bit RGB) — mirrored from the on-screen UI
    //    palette in src/gui/GuiHelpers.h. Each channel 0..127 (MIDI range). ──
    static constexpr int kColorOff    = lcxl3_detail::rgb(  0,   0,   0);  // LED off
    static constexpr int kColorBound  = lcxl3_detail::rgb(  0, 110,   0);  // CC-Learn bound (green)

    static constexpr int kColorGen    = lcxl3_detail::rgb( 51,  63, 117);  // Periwinkle #667EEA — generation (Alpha/Resynth)
    // ENV / LFO / DRIFT deliberately DIVERGE from the on-screen palette (which paints
    // all three amber/amber/dark-amber): on 24 identical encoder LEDs that read as a
    // uniform yellow wall. ENV stays amber (envelope identity); LFO=teal, DRIFT=blue is
    // the only triad (verified ΔE2000≥20 in normal + deuteran/protan/tritan; Machado CVD
    // on the 7-bit-quantized values; tool: tools/led_color_verify.py) that keeps ENV amber
    // AND stays distinct under colour-vision deficiency — amber↔blue rides the yellow-blue
    // axis (immune to red-green CVD), and the bright teal separates from the darker blue by
    // luminance even under tritanopia.
    static constexpr int kColorEnv    = lcxl3_detail::rgb(127,  55,   0);  // Amber — envelopes (A/D/S/R)
    static constexpr int kColorLfo    = lcxl3_detail::rgb( 12, 127, 104);  // Teal  — LFOs   (bright; lum-separated from Drift)
    static constexpr int kColorDrift  = lcxl3_detail::rgb(  0,  59, 127);  // Blue  — drift  (yellow↔blue axis vs amber)
    static constexpr int kColorFilter = lcxl3_detail::rgb( 62,  38, 127);  // Violet     #7C4DFF — filter
    static constexpr int kColorFx     = lcxl3_detail::rgb(  0,  94, 106);  // Cyan       #00BCD4 — delay/reverb
    static constexpr int kColorVol    = lcxl3_detail::rgb(  0, 100,  41);  // Green      #00C853 — amp amount

    // ── CC ranges (LCXL3, verified) ─────────────────────────────────────────
    // In DAW mode faders/encoders transmit on ch16, buttons on ch1; the CC
    // numbers are the same as Custom Mode. Control-index == CC for LEDs.
    // Faders (left→right): CC  5-12
    // Row 1 top knobs:     CC 13-20
    // Row 2 mid knobs:     CC 21-28
    // Row 3 bottom knobs:  CC 29-36
    // Bottom buttons:      upper row CC 37-44, lower row CC 45-52  (CC 52 = panic)
    // Left transport (ch1, programmer's ref p.9 DAW-mode surface):
    //   Play ▶  = CC 116 (0x74)   Record ● = CC 118 (0x76)
    //   Page ^v = CC 106/107      Track ‹› = CC 103/102   (nav — unused by T5ynth)
    //   Shift   = CC 63 on ch7    (device-managed feature control — never host-driven)

    // Map a CC number → LED control index. Returns -1 if not an XL control.
    // (control-index == CC# assumed; see header note.)
    static int ccToLedNote(int cc) noexcept
    {
        if (cc >= 5  && cc <= 12) return cc;   // Faders  (CC 5-12)
        if (cc >= 13 && cc <= 36) return cc;   // Encoder rows 1-3 (CC 13-36)
        if (cc >= 37 && cc <= 52) return cc;   // Buttons (CC 37-52)
        return -1;
    }

    // Build a SysEx "set LED color" message for one control.
    //   controlIndex : the control's index (== its CC#, see header note)
    //   packedRgb    : (R<<16)|(G<<8)|B, each channel 0..127
    static juce::MidiMessage ledOn(int controlIndex, int packedRgb)
    {
        const juce::uint8 body[] = {
            0x00, 0x20, 0x29, 0x02, 0x15, 0x01, 0x53,
            static_cast<juce::uint8>(controlIndex      & 0x7F),
            static_cast<juce::uint8>((packedRgb >> 16) & 0x7F),
            static_cast<juce::uint8>((packedRgb >>  8) & 0x7F),
            static_cast<juce::uint8>( packedRgb        & 0x7F)
        };
        return juce::MidiMessage::createSysExMessage(body, static_cast<int>(sizeof(body)));
    }

    // Build a SysEx LED-off message (RGB 0,0,0).
    static juce::MidiMessage ledOff(int controlIndex)
    {
        return ledOn(controlIndex, kColorOff);
    }

    // DAW-mode enable/disable. Host LED control ("Colouring the surface") is ONLY
    // honored once the device is in DAW mode — programmer's reference p.8-12; a
    // Custom Mode's LEDs cannot be recoloured by the host. Sent as Note On ch16,
    // note 0x0C, velocity 127 (enable) / 0 (disable): bytes 9F 0C 7F / 9F 0C 00.
    static juce::MidiMessage dawMode(bool enable)
    {
        return juce::MidiMessage(0x9F, 0x0C, enable ? 0x7F : 0x00);
    }

    // ── Feature controls (programmer's reference p.16-17) ───────────────────────
    // Every feature control is a CC on channel 7 (status byte B6h); On/Off = 127/0.
    // They configure the device's DAW-mode behaviour (and are reset when DAW mode is
    // disabled, except the (*)-marked persistent ones which we deliberately never set).
    static constexpr int kFcEncRow1Rel  = 0x45;  // 69 — encoder row 1 relative output
    static constexpr int kFcEncRow2Rel  = 0x48;  // 72 — encoder row 2 relative output
    static constexpr int kFcEncRow3Rel  = 0x49;  // 73 — encoder row 3 relative output
    static constexpr int kFcFaderPickup = 0x46;  // 70 — fader pickup (soft takeover)
    static constexpr int kFcTouchEvents = 0x47;  // 71 — continuous-control touch events

    // Build a feature-control message: CC on ch7 (B6h), value 0..127.
    static juce::MidiMessage featureControl(int cc, int value)
    {
        return juce::MidiMessage(0xB6, cc & 0x7F, value & 0x7F);
    }

    // Switch an encoder row to RELATIVE mode. The endless encoders default to ABSOLUTE
    // (they emit an internal 0-127 position, so the synth value JUMPS to it on the first
    // turn); relative mode emits a signed delta around 64 instead — no jump. Programmer's
    // ref p.10: B6h <RowID> 7Fh (ch7). RowID 0x45/0x48/0x49 = rows 1/2/3. In relative
    // mode a row's encoders transmit on CC (absoluteCC + 64): rows 1-3 = CC 77-84 /
    // 85-92 / 93-100, value = 64 + delta. rowIndex 0..2.
    static juce::MidiMessage encoderRelativeMode(int rowIndex, bool enable)
    {
        static constexpr int rowId[3] = { kFcEncRow1Rel, kFcEncRow2Rel, kFcEncRow3Rel };
        return featureControl(rowId[juce::jlimit(0, 2, rowIndex)], enable ? 0x7F : 0x00);
    }

    // Fader pickup (soft takeover): a physical fader does not "jump" the bound param to
    // its position on touch — it engages only once swept past the current value. Avoids
    // the analogous jump that relative mode fixes for encoders. Feature control CC 70/ch7.
    static juce::MidiMessage faderPickup(bool enable)
    {
        return featureControl(kFcFaderPickup, enable ? 0x7F : 0x00);
    }

    // ── Page 1 default bindings: { paramId, CC, color } ─────────────────────
    // Applied by PluginProcessor::applyXLDefaultBindings() on user request.
    //
    // Faders (CC 5-12): Alpha(inv) — Resynth — Cutoff — Res — Drive — DlyMix — RevMix — Vol
    // Alpha is inverted (minNorm=1,maxNorm=0): fader UP = A, matching the on-screen
    // FlippedVerticalSlider (A at top). Vol = master_vol (the on-screen "Vol" knob).
    // Row 1 (CC 13-20): Env1 (amp)  A/D/S/R — LFO1 Rate/Amt — Drift1 Rate/Amt
    // Row 2 (CC 21-28): Env2 (mod1) A/D/S/R — LFO2 Rate/Amt — Drift2 Rate/Amt
    // Row 3 (CC 29-36): Env3 (mod2) A/D/S/R — LFO3 Rate/Amt — Drift3 Rate/Amt
    //
    // Physical row N drives module group N — matching the easy-panel tab order
    // ENV1/2/3 = amp/mod1/mod2 (SynthPanel initEnv). Rows 2 and 3 were swapped.
    static constexpr lcxl3_detail::Binding kPage1[] = {
        // Faders — Generation | Filter | FX | Vol  (CC 5-12)
        { "gen_alpha",         5, kColorGen, 1.0f, 0.0f }, { "resynth_amount", 6, kColorGen },
        { "filter_cutoff",     7, kColorFilter }, { "filter_resonance",  8, kColorFilter },
        { "filter_drive",      9, kColorFilter },
        { "delay_mix",        10, kColorFx     }, { "reverb_mix",       11, kColorFx     },
        { "master_vol",       12, kColorVol    },

        // Row 1 — Env1 (amp) + LFO1 + Drift1  (CC 13-20)
        { "amp_attack",    13, kColorEnv   }, { "amp_decay",    14, kColorEnv   },
        { "amp_sustain",   15, kColorEnv   }, { "amp_release",  16, kColorEnv   },
        { "lfo1_rate",     17, kColorLfo   }, { "lfo1_depth",   18, kColorLfo   },
        { "drift1_rate",   19, kColorDrift }, { "drift1_depth", 20, kColorDrift },

        // Row 2 — Env2 (mod1) + LFO2 + Drift2  (CC 21-28)
        { "mod1_attack",   21, kColorEnv   }, { "mod1_decay",   22, kColorEnv   },
        { "mod1_sustain",  23, kColorEnv   }, { "mod1_release", 24, kColorEnv   },
        { "lfo2_rate",     25, kColorLfo   }, { "lfo2_depth",   26, kColorLfo   },
        { "drift2_rate",   27, kColorDrift }, { "drift2_depth", 28, kColorDrift },

        // Row 3 — Env3 (mod2) + LFO3 + Drift3  (CC 29-36)
        { "mod2_attack",   29, kColorEnv   }, { "mod2_decay",   30, kColorEnv   },
        { "mod2_sustain",  31, kColorEnv   }, { "mod2_release", 32, kColorEnv   },
        { "lfo3_rate",     33, kColorLfo   }, { "lfo3_depth",   34, kColorLfo   },
        { "drift3_rate",   35, kColorDrift }, { "drift3_depth", 36, kColorDrift },
    };

    static constexpr int kPage1Count = static_cast<int>(std::size(kPage1));

    // Canonical Page-1 paramId bound to a CC, or nullptr if the CC is not in the map.
    static const char* paramIdForCc(int cc) noexcept
    {
        for (const auto& b : kPage1)
            if (b.cc == cc) return b.paramId;
        return nullptr;
    }

    // One-time migration for presets saved before the Row-2/Row-3 fix, where the
    // two knob blocks (CC 21-28 ↔ 29-36) were swapped. Because the blocks were
    // swapped wholesale, the pre-fix paramId at a CC equals the *canonical*
    // paramId of its swap partner (CC±8); that signature uniquely identifies a
    // stale slot. Genuine user CC-learns (e.g. CC21→filter_cutoff) never match,
    // so they are returned unchanged.
    static juce::String migrateLegacyKnobParam(int cc, const juce::String& loadedParamId)
    {
        if (cc < 21 || cc > 36) return loadedParamId;        // only the swapped blocks moved
        const int partnerCc   = (cc <= 28) ? cc + 8 : cc - 8;
        const char* canonical = paramIdForCc(cc);
        const char* legacy    = paramIdForCc(partnerCc);     // == what the slot held pre-fix
        if (canonical != nullptr && legacy != nullptr && loadedParamId == legacy)
            return juce::String(canonical);
        return loadedParamId;
    }
};
