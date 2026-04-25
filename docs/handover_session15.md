# Session 15 Handover — 2026-04-23

## Thema: Nonlinear Filter — offene Punkte aus der Ladder/Warp-Ship

Die drei Filter-Algorithmen (SVF / Huovilainen-Ladder / Cutoff-Warp) sind seit `b9d84d67` live und der lineare Feedback-Tap ist gefixt (Resonanz kann jetzt theoretisch ringen statt nur die Färbung zu verschieben). Drei offene Baustellen aus dem Hörtest, die in dieser Session **nicht** ausreichend gelöst wurden:

---

### 1. Filter-Typ / Algorithm bei Synth-Start nicht visuell aktiv

Beim Laden eines frischen Plugins zeigt die Filter-Karte (Type-Switchbox *und/oder* Algorithm-Switchbox) keinen aktiven Button. APVTS-Default wird gesetzt (z. B. Filter-Type = LP = Index 1), aber die TextButton-Toggle-States in `SynthPanel` spiegeln das auf der ersten Paint-Seite nicht.

Versuchter Fix in `3390dbc8`:
```cpp
if (filterTypeHidden.onChange)     filterTypeHidden.onChange();
if (filterSlopeHidden.onChange)    filterSlopeHidden.onChange();
if (filterAlgHidden.onChange)      filterAlgHidden.onChange();
if (filterDriveOsHidden.onChange)  filterDriveOsHidden.onChange();
```

User-Rückmeldung: hilft nicht vollständig.

**Hypothesen für die nächste Session:**

- Reihenfolge in `SynthPanel`-Konstruktor: `filterAlgHidden.onChange = [...]` wird *nach* dem ersten Paint-Tick gesetzt? Unwahrscheinlich, aber prüfenswert.
- `ComboBoxAttachment` setzt den ausgewählten Index beim ersten `parameterChanged`-Callback — der kann *asynchron* nach dem Konstruktor feuern. Zu dem Zeitpunkt ist unser expliziter `onChange()`-Poke schon durch und die Buttons werden erst *danach* implizit aktualisiert, aber via `dontSendNotification`, so dass kein `onChange` mehr fliegt.
- Workaround: `valueChanged`-Listener auf dem APVTS-Parameter direkt registrieren statt auf dem Hidden-ComboBox — dann triggert jede APVTS-Wertänderung den Button-Sync ohne Umweg.
- Oder: nach dem Attachment *warten* (`juce::MessageManager::callAsync` o. ä.) und dann `onChange()` feuern.

**Akzeptanztest**: VST3 frisch laden → LP (oder was auch immer der Default ist) leuchtet auf dem ersten Sichtbar-Werden. SVF leuchtet. 12 dB leuchtet. 4x leuchtet. Ohne Klick, ohne Parameterbewegung.

---

### 2. Resonanz bei Ladder / Warp zu drive-sensitiv (effektiv drive-killed)

Mit `b9d84d67` ist der Feedback-Tap linear, das Saturations-Clipping findet an den Stage-Inputs statt. Resonanz rings *grundsätzlich* jetzt, aber die Interaktion mit Drive ist immer noch stark daneben:

**Konkrete User-Befunde aus dem Hörtest**:

- **Ladder**: bei ~Drive 50 % killt der Regler die Resonanz *vollständig*. Kein Unterschied mehr ob Reso = 0 oder 100 %.
- **Warp Tanh**: gleiches Verhalten wie Ladder.
- **Warp SoftClip**: praktisch *keine* Resonanz, nur ein entferntes Zwitschen, selbst bei Reso = 100 %.
- **Warp Sin**: ab Reso 0.25 aggressive, fast zu starke Resonanz; aber ab Drive **21 dB verschwindet die Resonanz komplett** (Reso 0 ≡ Reso 100 %).

**Ursachenanalyse**:

Das ist konzeptionell das klassische Analog-Moog-Verhalten, aber in der simplifizierten tanh-Stage-Form extrem ausgeprägt. Bei hohem Drive sitzt der Arbeitspunkt tief in der Sättigung, wo `tanh'(x) ≈ 0`. Effektive Loop-Gain pro Stage ist die lokale Ableitung der Sättigungsfunktion am Arbeitspunkt, und das Produkt der vier Ableitungen geht gegen 0 → keine Resonanz mehr.

- `tanh'(1.8) = 0.104` → Loop-Gain-Faktor pro Stage nur ~10 % des Nominalwerts
- `softclip'(1) = 1/(1+1)² = 0.25` — auch schon bei niedrigen Signalpegeln deutlich unter 1, deshalb SoftClip von Haus aus resonanzarm
- `sin-fold'(π/4) = cos(π/4) ≈ 0.7` — Sin hat höchste lokale Gain bei moderaten Pegeln → starke Resonanz; aber bei Drive 21 dB ist `x·π/2` so weit jenseits `π/2`, dass der Fold flat wird und `sin'` kippt

**Fix-Richtungen für die neue Session** (nicht in dieser erledigt, Priorität hoch):

1. **Resonance-Compensation bei Drive**: `k_effective = k · (1 + α·drive_factor)`. Einfach aber empirisch. Evtl. style-abhängig parametrieren.
2. **Saturation-Curve-Biasing**: statt `sat(stage_input)` → `sat(stage_input / current_level) · current_level` mit `current_level = tracked_amplitude_envelope`. Hält den Arbeitspunkt der Sättigung näher an 0 → lokale Ableitung ~1 → Resonanz überlebt Drive.
3. **Per-Stage-Gain-Kompensation** (Huovilainen-Standard-Trick aus dem Paper): `y_i += g · (sat(in_i) - sat(y_i)) · (1 + k·α)`. Das kompensiert die Gain-Kompression durch Feedback direkt in den Stages.
4. **Alternative Topologie**: z. B. Runge-Kutta-Ladder aus Surges `VintageLadders.cpp` — rechenaufwendiger, aber über Drive-Bereich stabileres Resonanzverhalten.

Für Warp zusätzlich prüfen, ob pro Style eine *andere* Resonanz-Mapping-Kurve sinnvoll ist (SoftClip braucht mehr k, Sin weniger).

**Akzeptanztest**: Bei jedem Algorithmus × Style, über den gesamten Drive-Bereich (0 … 36 dB), muss Reso 0 ≠ Reso 100 % *deutlich* hörbar bleiben. Selbstoszillation bei Reso = 1.0 sollte bei niedrigem bis mittlerem Drive klingeln.

---

### 3. Preset-Save/Load-Audit für alle Filter-Parameter

Die neuen Felder (`filter_algorithm`, `filter_warp_style`, `filter_drive_os`, …) sind in `exportJsonPreset` / `importJsonPreset` explizit ergänzt, aber es wurde nicht systematisch gegen den vollständigen APVTS-Parameter-Set verifiziert, ob *alle* Filter-Parameter in beide Richtungen sauber durchlaufen.

**To-do für die neue Session**:

- Alle `PID::filter*`-Einträge aus `BlockParams.h` listen:
  ```
  filterEnabled, filterType, filterSlope, filterCutoff, filterResonance,
  filterMix, filterKbdTrack, filterDrive, filterDriveMakeup (deprecated no-op),
  filterDriveOs, filterAlgorithm, filterWarpStyle
  ```
- Für jedes Feld verifizieren:
  1. Es wird in `exportJsonPreset` geschrieben.
  2. Es wird in `importJsonPreset` gelesen, mit `hasProperty`-Fallback auf einen sinnvollen Default.
  3. Der Fallback produziert bei einem *alten* `.t5p`-File (ohne das Feld) das klanglich erwartete Verhalten — typischerweise bit-identisch zur Pre-Feature-Version.
- Bonus: ein einfacher Regressionstest, der alle Parameter zufällig setzt, speichert, neu lädt und per APVTS-Vergleich prüft, dass die Werte stimmen.

**Speziell prüfen**: `filterDriveMakeup` steht noch als APVTS-Parameter drin (als No-Op), wird aber in `importJsonPreset` nicht mehr gesetzt. Das ist okay (Param liest niemand), aber der Fallback-Pfad sollte mindestens dokumentiert sein, damit nicht jemand später denkt, das sei vergessen worden.

---

## Aktueller Stand Remote

Push bis `b9d84d67` auf `origin/codex-linux-package-ci`. Letzte fünf relevante Commits:

- `b9d84d67` fix(filter): linear feedback tap so resonance actually rings / self-oscillates
- `ab7a9f2d` docs: credit Huovilainen + Surge XT for the new filter algorithms
- `3390dbc8` ui(filter): compact header row + fix startup toggle visuals
- `d0f78364` fix(filter): ladder/warp drive makes louder, level parity with SVF
- `9e5ef952` fix(filter): proper Moog drive + Warp DC bias + half-sample delay comp

## Dateien im Fokus der nächsten Session

- `src/dsp/MoogLadderFilter.h` — Drive/Reso-Interaktion (Punkt 2)
- `src/dsp/CutoffWarpFilter.h` — dto., plus Style-spezifische Mapping-Kurven
- `src/gui/SynthPanel.cpp` — Startup-Toggle-Sync (Punkt 1)
- `src/PluginProcessor.cpp` — Preset-I/O-Audit (Punkt 3)
