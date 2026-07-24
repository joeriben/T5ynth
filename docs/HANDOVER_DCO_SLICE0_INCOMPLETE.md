# Handover — DCO Slice 0 (Steps/CFG/Seed/Header aus Advanced entfernen) — INCOMPLETE

**Status (2026-07-24, historical).** This document records a failed,
non-compiling intermediate state (`HEAD = 6c328c43`) superseded within the
same work session by `docs/HANDOVER_DCO_SLICE0_DONE.md` — verified:
`6c328c43` (2026-07-09) is an ancestor of `035e5cf8` (2026-07-10), the commit
the DONE handover reports as the clean landing. Nothing here describes
current code; it is the record of one abandoned edit approach to a task
later completed. Superseded by `docs/HANDOVER_DCO_SLICE0_DONE.md`.

Status: **Teilweise durchgeführt, nicht kompilierbar.** Working tree hat uncommitted Änderungen
in `PromptPanel.h` (fast fertig) und `PromptPanel.cpp` (teilweise fertig, 54 Referenzen auf
entfernte Member bleiben). HEAD = `6c328c43` (clean baseline zum Rücksetzen).

## Was getan wurde

### PromptPanel.h — FAST FERTIG (kompilierbar bis auf .cpp-Referenzen)
Folgende Member wurden entfernt:
- `stepsSlider`, `cfgSlider` (MidiLearnSlider)
- `stepsLabel`, `stepsValue`, `stepsHint`, `cfgLabel`, `cfgValue`, `cfgHint` (Labels)
- `stepsA`, `cfgA` (APVTS Attachments)
- `seedLabel`, `seedEditor`, `randomSeedToggle`
- `genParamsHeader`

Folgende Methoden wurden umgestellt:
- `getSeed()` → `return processorRef.getLastSeed();`
- `isRandomSeed()` → `return processorRef.getLastRandomSeed();`
- `syncSeedEditorEnabledState/Font/Display` → ersetzt durch `syncSeedState(int seed)`
- `openSeedEntryDialog()` — Deklaration aktualisiert (schreibt in processor store)

**Verbleibend im Header:** Nur noch Kommentare, die die alten Member erwähnen (Zeilen 19, 182) —
kosmetisch, kein Kompilierungsfehler.

### PromptPanel.cpp — TEILWEISE FERTIG (NICHT kompilierbar)
Folgende Änderungen wurden durchgeführt:
1. `kPromptContentUnits`: 23.19 → 16.25 (Budget angepasst)
2. Kommentar-Block über den ContentUnits aktualisiert
3. Steps/CFG/Seed-Konstruktion (Zeilen 299-357) entfernt via `sed`
4. mouseDown MIDI-learn Cases für stepsSlider/cfgSlider (Zeilen 3250-3251) entfernt via `sed`

**NICHT durchgeführt (54 Referenzen verbleiben):**
- Attachments `stepsA`/`cfgA` (Zeile ~477-478)
- MIDI-learn for-Schleife referenziert stepsSlider/cfgSlider (Zeile ~499-500)
- `genParamsHeader` Konstruktion (Zeile ~526-528)
- Visibility-Block: setVisible für stepsLabel/stepsValue/cfgLabel/cfgValue/stepsSlider/cfgSlider/seedEditor/randomSeedToggle/seedLabel/genParamsHeader (Zeile ~914-927)
- Hint-hide: stepsHint/cfgHint setVisible(false) (Zeile ~955-956)
- `getPreferredHeightForWidth` Advanced-Branch (Zeile ~685-689)
- Layout: GENERATION-Header-Block (Zeile ~1086-1087)
- `layoutCompactPair` Lambda (Zeile ~1056-1078) — wird nicht mehr benutzt
- `layoutSeedRow` Lambda (Zeile ~1083-1105) — wird nicht mehr benutzt
- `layoutCompactPair(steps..., cfg...)` Aufruf (Zeile ~1215-1216)
- `layoutSeedRow()` Aufruf (Zeile ~1217)
- `paramsH` Advanced-Branch (Zeile ~1200-1204)
- `loadPresetData`: randomSeedToggle/syncSeedEditorDisplay/syncSeedEditorEnabledState (Zeile ~1247-1249)
- `setSeedMode`: randomSeedToggle/syncSeedEditorDisplay/syncSeedEditorEnabledState/seedEditor (Zeile ~1823-1841)
- `syncSeedModeFromCurrentState`: randomSeedToggle/seedEditor (Zeile ~1853-1855)
- `syncSeedEditorEnabledState/Font/Display` Funktionsdefinitionen (Zeile ~1922-1954)
- `openSeedEntryDialog`: syncSeedEditorDisplay (Zeile ~1982)
- `buildInferenceRequest`: randomSeedToggle/seedEditor (Zeile ~2107)
- `triggerGeneration`/`triggerDriftRegeneration`: syncSeedEditorDisplay (Zeile ~2526, ~2669)
- `pollDriftRegen`: randomSeedToggle (Zeile ~2919)
- Kommentar "seedEditor's text field is hidden" (Zeile ~346)
- Kommentar "Mirrors syncSeedEditorFont" (Zeile ~984)

## Warum es scheiterte

1. **`replace_in_file` schlug fehl** — der SEARCH-Block matchte nicht exakt (vermutlich
   Encoding/Whitespace-Probleme bei der großen Datei).
2. **`sed`-Löschungen funktionierten**, waren aber mühsam zeilennummern-basiert.
3. **Python-Skript-Ansatz** wurde vom Benutzer abgelehnt ("Du wirst dein Skript das 2 mal
   gescheitert ist adversarial testen").
4. **Task-Resumption-Interrupts** unterbrachen wiederholt die Ausführung.

## Was der nächste Agent tun muss

### Option A: Python-Skript (empfohlen, aber adversarial testen)
Ein Python-Skript mit `str.replace` und `re.sub` für die 19 Ersetzungen. **Vorher auf einer
Kopie testen** (`cp src/gui/PromptPanel.cpp /tmp/test.cpp`), dann prüfen:
- `grep -c "stepsSlider\|cfgSlider\|seedEditor\|randomSeedToggle\|genParamsHeader\|syncSeedEditor" /tmp/test.cpp` → muss 0 sein
- Datei-Länge plausibel (nicht leer, nicht zu kurz)
- Kompilierbarkeit prüfen

Die 19 Ersetzungen sind im Conversationsverlauf dokumentiert (siehe das Python-Skript, das
ich entworfen habe). Schlüsselersetzungen:

```python
# Attachments
content = content.replace("    stepsA  = std::make_unique<Attachment>(apvts, PID::infSteps, stepsSlider);\n", "")
content = content.replace("    cfgA    = std::make_unique<Attachment>(apvts, PID::genCfg, cfgSlider);\n", "")

# MIDI-learn loop
content = content.replace("    for (juce::Slider* s : { static_cast<juce::Slider*>(&alphaSlider),\n                              static_cast<juce::Slider*>(&stepsSlider),\n                              static_cast<juce::Slider*>(&cfgSlider) })\n        s->addMouseListener(this, false);\n",
                          "    alphaSlider.addMouseListener(this, false);\n")

# ... (alle 19 Ersetzungen siehe Conversationsverlauf)
```

### Option B: `replace_in_file` mit kleineren Blöcken
Ein SEARCH/REPLACE-Block pro Änderung, nicht alle auf einmal. Bei Fehlern exakte Zeilen
mit `sed -n 'NNNNp'` prüfen.

### Option C: Datei komplett neu schreiben
`write_to_file` mit dem vollständigen korrigierten Inhalt. Risiko: bei 3200+ Zeilen
fehleranfällig, aber kontrollierbar.

## Nach den Ersetzungen

1. **Build testen:**
   ```bash
   cmake -S . -B build_clean -DCMAKE_BUILD_TYPE=Release
   cmake --build build_clean --config Release -j$(sysctl -n hw.ncpu)
   ```
2. **Adversarielle Verifikation:** Opus-Subagent "find the bug"
3. **Audition:** UI visuell prüfen (Advanced-Ansicht sollte leer sein bis auf Model/A-B/Re-Prompt)
4. **Commit:**
   ```bash
   git add src/gui/PromptPanel.h src/gui/PromptPanel.cpp
   git commit -m "feat(ui): remove Steps/CFG/Seed/GENERATION from Advanced view (DCO Slice 0)

   Frees the Advanced param grid for the DCO surface. Steps/CFG/Seed state
   now lives on the processor (setLastSeed/getLastSeed, setLastRandomSeed/
   getLastRandomSeed), driven exclusively by the Easy-mode Variation switchbox.

   Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
   ```

## Budget-Rechnung (zur Verifikation)
- `kPromptContentUnits`: 23.19 → 16.25 (−6.94)
  - Steps/CFG compactPair: compactRow + compactCtrl + gap = 1.15 + 0.9 + 0.28 = 2.33
  - GENERATION header: compactRow + gap = 1.15 + 0.28 = 1.43
  - Seed row: compactRow + seedCtrl + gap = 1.15 + 1.75 + 0.28 = 3.18
  - Total: 2.33 + 1.43 + 3.18 = 6.94 ✓
- `getPreferredHeightForWidth` Advanced-Branch: nur noch modelGap + abBlock + repromptRow + groupGap
- `paramsH` Advanced: 0 (leerer Canvas)

## Processor-Seed-API (bereits vorhanden, keine Änderung nötig)
- `PluginProcessor.h`: `setLastSeed(int)`, `getLastSeed()`, `setLastRandomSeed(bool)`, `getLastRandomSeed()`
- `lastSeed = 123456789`, `lastRandomSeed = false` (Defaults)

## Dateien
- `src/gui/PromptPanel.h` — fast fertig (nur Kommentare erwähnen alte Member)
- `src/gui/PromptPanel.cpp` — 54 Referenzen verbleiben, nicht kompilierbar
- `src/PluginProcessor.h` — unverändert, Seed-API bereits vorhanden
- HEAD = `6c328c43` — clean baseline zum Rücksetzen mit `git restore src/gui/PromptPanel.{h,cpp}`