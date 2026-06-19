# Handover: Launch Control XL — CC Calibration & LED Fix

## Status

MIDI Output LED feature ist fertig implementiert und kompiliert.
**Offen:** `LaunchControlXLLeds.h` enthält noch falsche CC-Nummern für Fader und Row 2.
Dieser Handover dokumentiert alle verifizierten Hardware-Fakten und listet exakt was zu ändern ist.

---

## Verifizierte CC-Nummern (physisch gemessen am Gerät)

| Steuerung | CC-Range | Status in Code |
|-----------|----------|---------------|
| Row 1 (oben) | CC **13–20** | ✓ korrekt |
| Row 2 (mitte) | CC **21–28** | ✗ falsch (Code hat 49–56) |
| Row 3 (unten) | CC **29–36** | ✓ korrekt (nach letztem Fix) |
| Fader links→rechts | CC **5–12** | ✗ falsch (Code hat 77–84) |

Das LCXL3 (Mk3 / dritte Generation) hat eine komplett andere CC-Belegung
als die Novation-Dokumentation für das Original-XL beschreibt.
Der Code basierte bisher auf der Original-XL-Doku — entsprechend sind
zwei Ranges noch falsch.

---

## Was bereits funktioniert

- Row 1: CC 13–20 → Env1 (amp) A/D/S/R + LFO1 Rate/Depth + Drift1 Rate/Depth ✓
- Row 3: CC 29–36 → Env2 (mod1) A/D/S/R + LFO2 Rate/Depth + Drift2 Rate/Depth ✓
- LED-Feedback auf CC Learn (bind/clear) ✓
- MIDI Output Device Selector in StatusBar ✓
- State Persistence ✓
- „XL Map"-Button populates kPage1 defaults ✓

---

## Zu ändernde Datei: `src/midi/LaunchControlXLLeds.h`

### 1. `ccToLedNote()` — CC→LED-Note-Mapping

**Aktueller Stand (falsch):**
```cpp
static int ccToLedNote(int cc) noexcept
{
    if (cc >= 13 && cc <= 20) return cc;   // Row 1
    if (cc >= 29 && cc <= 36) return cc;   // Row 2 ← war Row 3, jetzt Row 3 ✓
    if (cc >= 49 && cc <= 56) return cc;   // Row 3 ← FALSCH (physisch Row 2 = CC 21-28)
    if (cc >= 77 && cc <= 84) return cc;   // Faders ← FALSCH (physisch = CC 5-12)
    return -1;
}
```

**Korrekt (nach Messung):**
```cpp
static int ccToLedNote(int cc) noexcept
{
    if (cc >= 5  && cc <= 12) return cc;   // Faders (verified: CC 5-12)
    if (cc >= 13 && cc <= 20) return cc;   // Row 1  (verified: CC 13-20)
    if (cc >= 21 && cc <= 28) return cc;   // Row 2  (verified: CC 21-28)
    if (cc >= 29 && cc <= 36) return cc;   // Row 3  (verified: CC 29-36)
    return -1;
}
```

**Achtung VERIFY:** Ob die LED-Note-Nummern für CC 5–12 und 21–28 tatsächlich
mit den CC-Nummern übereinstimmen (wie bei Row 1+3) ist noch nicht am Gerät bestätigt.
Das Muster CC==LED-Note gilt für die Original-XL-Doku; beim LCXL3 ist das ungetestet.

### 2. Kommentare in CC-Ranges-Block

Ersetze den `// ── CC ranges`-Kommentarblock:
```cpp
// ── CC ranges (LCXL3, verified by hardware measurement) ─────────────────
// Faders (left→right): CC  5-12
// Row 1 top knobs:     CC 13-20
// Row 2 mid knobs:     CC 21-28
// Row 3 bottom knobs:  CC 29-36
// All on MIDI ch 9.
// LED note# == CC# for all ranges (assumed; verify Row 2 + Faders on device).
```

### 3. `kPage1[]` — Parameter-Zuordnung

**Row 2 (CC 21–28) ersetzen** — bisher CC 49–56 (falsch):
```cpp
// Row 2 — Env3 (mod2) + LFO3 + Drift3  (physical Row 2, CC 21-28)
{ "mod2_attack",   21, kColorEnv   }, { "mod2_decay",   22, kColorEnv   },
{ "mod2_sustain",  23, kColorEnv   }, { "mod2_release", 24, kColorEnv   },
{ "lfo3_rate",     25, kColorLfo   }, { "lfo3_depth",   26, kColorLfo   },
{ "drift3_rate",   27, kColorDrift }, { "drift3_depth", 28, kColorDrift },
```

**Fader-Block (CC 77–84 → CC 5–12) ersetzen:**
```cpp
// Faders — Generation | Filter | FX | Vol  (CC 5-12)
{ "gen_alpha",        5, kColorGen    }, { "resynth_amount",   6, kColorGen    },
{ "filter_cutoff",    7, kColorFilter }, { "filter_resonance", 8, kColorFilter },
{ "filter_drive",     9, kColorFilter },
{ "delay_mix",       10, kColorFx     }, { "reverb_mix",      11, kColorFx     },
{ "amp_amount",      12, kColorVol    },
```

**Row 3 (CC 29–36) bleibt wie zuletzt gefixt:**
```cpp
// Row 3 — Env2 (mod1) + LFO2 + Drift2  (physical Row 3, CC 29-36)
{ "mod1_attack",   29, kColorEnv   }, { "mod1_decay",   30, kColorEnv   },
...
```

### 4. Alten CC 49–56 Eintrag entfernen

Der Block `{ "mod2_attack", 49, ... }` bis `{ "drift3_depth", 56, ... }` wird durch
den neuen CC 21–28-Block ersetzt und muss weg.

---

## Vollständiges kPage1[] nach dem Fix

```cpp
static constexpr Binding kPage1[] = {
    // Faders — Generation | Filter | FX | Vol  (CC 5-12)
    { "gen_alpha",        5, kColorGen    }, { "resynth_amount",   6, kColorGen    },
    { "filter_cutoff",    7, kColorFilter }, { "filter_resonance", 8, kColorFilter },
    { "filter_drive",     9, kColorFilter },
    { "delay_mix",       10, kColorFx     }, { "reverb_mix",      11, kColorFx     },
    { "amp_amount",      12, kColorVol    },

    // Row 1 — Env1 (amp) + LFO1 + Drift1  (CC 13-20)
    { "amp_attack",    13, kColorEnv   }, { "amp_decay",    14, kColorEnv   },
    { "amp_sustain",   15, kColorEnv   }, { "amp_release",  16, kColorEnv   },
    { "lfo1_rate",     17, kColorLfo   }, { "lfo1_depth",   18, kColorLfo   },
    { "drift1_rate",   19, kColorDrift }, { "drift1_depth", 20, kColorDrift },

    // Row 2 — Env3 (mod2) + LFO3 + Drift3  (CC 21-28)
    { "mod2_attack",   21, kColorEnv   }, { "mod2_decay",   22, kColorEnv   },
    { "mod2_sustain",  23, kColorEnv   }, { "mod2_release", 24, kColorEnv   },
    { "lfo3_rate",     25, kColorLfo   }, { "lfo3_depth",   26, kColorLfo   },
    { "drift3_rate",   27, kColorDrift }, { "drift3_depth", 28, kColorDrift },

    // Row 3 — Env2 (mod1) + LFO2 + Drift2  (CC 29-36)
    { "mod1_attack",   29, kColorEnv   }, { "mod1_decay",   30, kColorEnv   },
    { "mod1_sustain",  31, kColorEnv   }, { "mod1_release", 32, kColorEnv   },
    { "lfo2_rate",     33, kColorLfo   }, { "lfo2_depth",   34, kColorLfo   },
    { "drift2_rate",   35, kColorDrift }, { "drift2_depth", 36, kColorDrift },
};
static constexpr int kPage1Count = static_cast<int>(std::size(kPage1));
```

---

## Offene LED-Frage nach dem Fix

Nach der Korrektur bitte am Gerät prüfen:
1. Leuchten die Fader-LEDs (CC 5–12) beim Drücken von „XL Map"?
   → Falls nicht: LED-Note für Fader ist nicht CC#, sondern ein anderer Offset.
2. Leuchten Row-2-Knob-LEDs (CC 21–28)?
   → Gleiches Problem möglich.
Falls LEDs für die neuen Ranges nicht kommen, muss eine `ccToLedNote()`-Sonderfallregel
für diese Ranges ergänzt werden (eventuell CC−Offset oder völlig andere Note-Nummern).
Das braucht Messung am Gerät mit einem MIDI-Monitor der Antwort-NoteOns verfolgt.

---

## Implementierungsstand (Commits auf `main`)

| Commit | Inhalt |
|--------|--------|
| `b445aa31` | `src/midi/LaunchControlXLLeds.h` — LED-Adapter initial |
| `72388d63` | `PluginProcessor` — open/close/send, sendLearnLed, applyXLDefault, State-Persist, getCcMappingCopy Bugfix |
| `57e0a769` | `StatusBar` + `MainPanel` — MIDI Output Combo + XL Map Button |
| `0ac7d9f7` | Modul-Akzentfarben in kPage1 |
| `780d8d28` | Row-2/3-Tausch (Teilfix — Row 2 noch auf CC 49-56, korrekt wäre 21-28) |

---

## Nächste Schritte für Opus

1. `src/midi/LaunchControlXLLeds.h` mit den oben dokumentierten Korrekturen schreiben
2. Build: `cmake --build build_clean --config Release -j$(sysctl -n hw.ncpu)`
3. Verification Agent (opus): „This code has a bug. Find it."
4. Commit: `fix(midi): correct LCXL3 CC ranges — faders CC 5-12, Row 2 CC 21-28`
5. Nutzer testen lassen: Leuchten alle 32 Knobs + 8 Fader nach „XL Map"?
6. Falls Fader-LED-Notes falsch: `ccToLedNote()` für CC 5–12 anpassen
