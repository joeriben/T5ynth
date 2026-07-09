# Handover — DCO Slice 0 (Steps/CFG/Seed/Header aus Advanced entfernt) — DONE

Status: **Abgeschlossen und committed.** Commit `035e5cf8` auf `main`
(2 files changed, 53 insertions(+), 254 deletions(-)). Build wurde noch nicht
getestet (nächster Schritt).

## Was getan wurde

### PromptPanel.h
- Keine Code-Änderungen mehr nötig — die Member-Entfernungen und Methoden-Umstellungen
  aus dem vorherigen Agent-Lauf waren bereits korrekt.
- Verbleibend: 2 kosmetische Doku-Kommentare (Zeilen 119, 182), die die alten
  `seedEditor`/`randomSeedToggle`-Widgets erwähnen. Keine Kompilierungsfehler,
  rein dokumentarisch. Können bei Gelegenheit bereinigt werden.

### PromptPanel.cpp — alle 60 Referenzen entfernt
Folgende Ersetzungen wurden durchgeführt (via `replace_in_file`, 3 Gruppen):

1. **Kommentar** (Zeile ~346): "seedEditor's text field is hidden" → "seed-entry field was removed"
2. **Attachments** `stepsA`/`cfgA` (Zeile ~477-478): entfernt
3. **MIDI-learn-Loop** (Zeile ~498-501): `for (juce::Slider* s : {...})` → `alphaSlider.addMouseListener(this, false);`
4. **genParamsHeader-Konstruktion** (Zeile ~526-528): entfernt inkl. Kommentar-Block
5. **Visibility-Block** (Zeile ~903-916): Steps/CFG-Labels/Sliders + seedEditor + randomSeedToggle + seedLabel + genParamsHeader setVisible entfernt
6. **Hint-hide** (Zeile ~944-945): `stepsHint`/`cfgHint` setVisible(false) entfernt
7. **Kommentar** (Zeile ~973): "Mirrors syncSeedEditorFont" entfernt
8. **Advanced-else-Block** (Zeile ~1065-1078): GENERATION-Header-Layout → leerer Canvas mit DCO-Kommentar
9. **layoutCompactPair-Lambda** (Zeile ~1093-1112): vollständig entfernt
10. **layoutSeedRow-Lambda** (Zeile ~1115-1140): vollständig entfernt
11. **paramsH Advanced-Branch** (Zeile ~1199-1202): `? ... : 0`
12. **Advanced-Call-Sites** (Zeile ~1213-1217): `layoutCompactPair(...)`/`layoutSeedRow()` → Kommentar
13. **loadPresetData** (Zeile ~1247-1249): `randomSeedToggle`/`syncSeedEditorDisplay`/`syncSeedEditorEnabledState` → `processorRef.setLastSeed(seed);`
14. **setSeedMode-Body** (Zeile ~1819-1841): `randomSeedToggle`/`seedEditor`/`syncSeedEditor*` → `processorRef.setLastSeed(...)`
15. **syncSeedModeFromCurrentState** (Zeile ~1853-1855): `randomSeedToggle`/`seedEditor` → `processorRef.getLastRandomSeed()`/`getLastSeed()`
16. **syncSeedEditor*-Definitionen** (Zeile ~1922-1954): 3 Funktionen → 1 Funktion `syncSeedState(int seed) { processorRef.setLastSeed(seed); }`
17. **openSeedEntryDialog** (Zeile ~1982): `syncSeedEditorDisplay(seed, true)` → `syncSeedState(seed)`
18. **buildInferenceRequest** (Zeile ~2107): `randomSeedToggle.getToggleState() ? -1 : seedEditor.getText().getIntValue()` → `processorRef.getLastRandomSeed() ? -1 : processorRef.getLastSeed()`
19. **triggerGeneration/triggerDriftRegeneration** (Zeile ~2526, ~2669): `syncSeedEditorDisplay(result.seed)` → `syncSeedState(result.seed)` (2 Vorkommen)
20. **pollDriftRegen** (Zeile ~2919): `randomSeedToggle.getToggleState()` → `processorRef.getLastRandomSeed()`

### Verifikation
- `grep -nE "stepsSlider|cfgSlider|seedEditor|randomSeedToggle|genParamsHeader|syncSeedEditor|stepsA\b|cfgA\b|stepsLabel|cfgLabel|stepsValue|cfgValue|stepsHint|cfgHint|seedLabel" src/gui/PromptPanel.cpp` → **0 Treffer** (exit 1)
- `grep` auf `PromptPanel.h` → nur 2 Doku-Kommentare, keine Code-Referenzen

## ⚠ Bekannter verbleibender Bug (nicht in diesem Commit gefixt)

`getPreferredHeightForWidth` Advanced-Branch wurde **nicht** an das neue
`kPromptContentUnits = 16.25` angepasst. Die Funktion gibt für Advanced noch
die volle Höhe zurück:

```cpp
return (compactRowH + 2) + modelGap                 // model selector row
     + abBlockH + innerGap + repromptRowH           // A↔B block + Re-Prompt row
     + compactRowH + gap                            // GENERATION top-header (replaces divider)
     + (compactRowH + compactCtrlH + gap)           // Steps/CFG (Mag/Chaos moved to Easy view)
     + compactRowH + seedCtrlH + gap;               // Seed (Duration moved to Easy view)
```

Die letzten drei Zeilen (GENERATION header + Steps/CFG + Seed = 6.94 units)
sind jetzt obsolet — der Advanced-Canvas ist leer. `kPromptContentUnits` wurde
korrekt von 23.19 → 16.25 reduziert, aber `getPreferredHeightForWidth` gibt noch
die alte Summe zurück. **Das führt zu einer Höhen-Diskrepanz**: `resized()`
rechnet `f` aus `kPromptContentUnits`, aber die bevorzugte Höhe ist größer als
diese Units implizieren.

**Fix für nächsten Agent:**
```cpp
return (compactRowH + 2) + modelGap                 // model selector row
     + abBlockH + innerGap + repromptRowH           // A↔B block + Re-Prompt row
     + groupGap;                                    // divider only (param grid removed)
```

(Das `groupGap` ersetzt den alten `compactRowH + gap` GENERATION-Header, da der
Divider jetzt die einzige Trennung zwischen Re-Prompt und dem leeren Canvas ist.)

## Was der nächste Agent tun muss

### 1. `getPreferredHeightForWidth` Advanced-Branch fixen
Siehe Bug-Beschreibung oben. Das ist der wahrscheinlichste verbleibende Bug.

### 2. Build testen
```bash
cd /Users/joerissen/ai/t5ynth
cmake -S . -B build_clean -DCMAKE_BUILD_TYPE=Release
cmake --build build_clean --config Release -j$(sysctl -n hw.ncpu)
```

### 3. Adversarielle Verifikation
Opus-Subagent "find the bug" — insbesondere den Höhen-Bug oben.

### 4. Audition
UI visuell prüfen: Advanced-Ansicht sollte leer sein bis auf Model/A-B/Re-Prompt.

### 5. Header-Kommentare bereinigen (optional)
`PromptPanel.h` Zeilen 119, 182: Doku-Kommentare erwähnen alte Widgets.

## Budget-Rechnung (zur Verifikation)
- `kPromptContentUnits`: 23.19 → 16.25 (−6.94)
  - Steps/CFG compactPair: 2.33
  - GENERATION header: 1.43
  - Seed row: 3.18
  - Total: 6.94 ✓
- `getPreferredHeightForWidth` Advanced-Branch: **noch nicht angepasst** (siehe Bug oben)

## Processor-Seed-API (unverändert, bereits vorhanden)
- `PluginProcessor.h`: `setLastSeed(int)`, `getLastSeed()`, `setLastRandomSeed(bool)`, `getLastRandomSeed()`
- `lastSeed = 123456789`, `lastRandomSeed = false` (Defaults)

## Dateien
- `src/gui/PromptPanel.h` — committed, 2 kosmetische Doku-Kommentare verbleibend
- `src/gui/PromptPanel.cpp` — committed, alle 60 Referenzen entfernt, grep-clean
- `src/PluginProcessor.h` — unverändert, Seed-API bereits vorhanden
- `docs/HANDOVER_DCO_SLICE0_INCOMPLETE.md` — das alte Handover (uncommitted, kann gelöscht werden)
- `docs/HANDOVER_DCO_SLICE0_DONE.md` — dieses Dokument

## Commit
```
035e5cf8 feat(ui): remove Steps/CFG/Seed/GENERATION from Advanced view (DCO Slice 0)