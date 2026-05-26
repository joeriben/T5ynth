# Session 19 — SynthVoice Mod-Target-Routing aus dem Inner-Sample-Loop hebeln

**Vorbedingung: Stand main = `af812694` (perf-hoist von `tunedHz` + `setInterpolation`).**

---

## 0. Auftrag der nächsten Session

In [`src/dsp/SynthVoice.cpp`](../src/dsp/SynthVoice.cpp) feuert die Per-Sample-Loop (der `for (int i = pos; i < subBlockEnd; ++i)`-Loop ab Zeile 908) bei jeder einzelnen Sample-Iteration **eine ganze Reihe block-konstanter Vergleiche** der Form

```cpp
if (p.ampTarget  == EnvTarget::Pitch) pitchMod += ampEnvVal;
if (p.mod1Target == EnvTarget::Pitch) pitchMod += mod1EnvVal;
…
if (p.lfo3Target == LfoTarget::Pitch) pitchMod += lfo3Val;
```

`p.ampTarget` und Geschwister ändern sich nicht im Sample-Loop (`BlockParams` wird einmal pro `renderBlock` aufgebaut, `configureForBlock` setzt die Member). **Jeder dieser Vergleiche ist im Loop reiner CPU-Verlust.**

Die Aufgabe: diese Vergleiche aus dem Hot-Path bekommen, ohne das Modulations-Routing-Verhalten oder das Audio-Thread-Sicherheits-Modell zu brechen. Erwarteter Ertrag: deutlich größer als das `af812694`-Mini-Hoist (vmtl. mehrere %, aber **vorher messen** — siehe §4).

---

## 1. Was es schon gibt (NICHT nochmal anfassen)

| Commit | Was wurde gehoisted |
|---|---|
| `af812694` | `tunedHz(currentNote + octaveShift_ * 12)` und `osc.setInterpolation(p.wtSmooth)` aus dem Wavetable-per-Sample-Branch. Block-Konstanz via VoiceManager-Split-at-MIDI-Event-Dispatch verifiziert. |

Das war das **kleine** Hoist (12 Zeilen, ~0.5 % CPU im 16-Voice-Worst-Case). Dieses Handover-Dokument betrifft den **großen** Hoist.

---

## 2. Hot-Sites im Sample-Loop (alle innerhalb `for (int i = pos; i < subBlockEnd; ++i)`)

Alle Zeilennummern aktuell zu `af812694`. Bitte vor dem Refactor frisch greppen (`grep -n "EnvTarget::\|LfoTarget::" src/dsp/SynthVoice.cpp`), falls sich Zeilen verschoben haben.

| Site | Zeilen | Branches/Sample | Engine-Mode-Gate |
|---|---|---|---|
| **Sampler-mode Filter-Cutoff** (`pow`-Aufrufe!) | 672–682 | 6 + 6 `std::pow` | `samplerMode` (eigentlich sub-block, siehe §2.1) |
| **Sampler-mode VCA-Filter** mid-pre-render | 829–834 | 6 `std::pow` | `samplerMode` block-rate analysis path |
| **Freeze-mode Pitch-Mod** | 942–947 | 6 | `freezeMode` |
| **Freeze-mode Scan-Mod** | 952–957 | 6 | `freezeMode` |
| **Wavetable-mode Pitch-Mod** | 971–976 | 6 | `oscReady` |
| **Wavetable-mode Scan-Mod** | 986–991 | 6 | `oscReady` |
| **Noise-Level-Mod** (alle Engine-Modes) | 1002–1007 | 6 | (always when noise active) |
| **VCA-Routing** in `computeDcaGain()` | 45–47 | 3 | per Sample, aufgerufen aus Zeile ~1019 |

**Summe: ~45 conditional branches pro Sample** (im Worst-Case-Pfad). Bei 256-sample blocks × 16 Voices ≈ 184 k Branches/block → ~35 M/s bei 48 kHz / 256 samples. Plus die `std::pow`-Calls in 672–682 und 829–834, die ohnehin teuer sind.

### 2.1 Achtung: Sub-Block vs. Per-Sample

Der Filter-Cutoff-Block 672–682 läuft im aktuellen Code **pro Sub-Block** (alle SUB_BLOCK_SIZE = 32 samples), nicht pro Sample — das macht ihn weniger heiß, aber Hoisting der Comparison ist trotzdem ein Win (eliminiert die Branch + ermöglicht es, die `std::pow`-Calls auf "exakt jene, die wirklich modulieren" zu reduzieren statt 6 conditional). Verifiziere vor dem Refactor, ob das immer noch sub-block oder evtl. zurück per-sample ist.

---

## 3. Lösungs-Ansätze — wähle einen

Alle drei sind kompatibel mit dem Audio-Thread-Sicherheits-Modell (kein Locking, keine Allocation, kein I/O). Die Wahl ist Style + Wartbarkeits-Trade-off.

### 3.1 Variante A — Bool-Hoist (minimaler Eingriff)

In `configureForBlock(p)` (Zeile 255+): pro Target-Kombi einen `bool` Member berechnen.

```cpp
// SynthVoice.h
bool ampToPitch_ = false, mod1ToPitch_ = false, mod2ToPitch_ = false;
bool lfo1ToPitch_ = false, lfo2ToPitch_ = false, lfo3ToPitch_ = false;
// … pro Target wiederholen (Pitch, Scan, NoiseLevel, Filter, DCA)

// configureForBlock:
ampToPitch_  = (p.ampTarget  == EnvTarget::Pitch);
mod1ToPitch_ = (p.mod1Target == EnvTarget::Pitch);
// …

// Im Sample-Loop:
if (ampToPitch_)  pitchMod += ampEnvVal;   // branch jetzt auf 1-byte member, hot in L1
if (mod1ToPitch_) pitchMod += mod1EnvVal;
// …
```

**Pro:** Minimaler Diff. Compiler kann mit `__attribute__((hot))`/PGO weiter optimieren. Lesbar.
**Contra:** Branches bleiben drin (nur deren Komparator wird einfacher). Win primär dadurch, dass die L1-Hit-Rate steigt und der Compiler die Bools als Predicate-Register halten kann.
**Erwartung:** 30–50 % der theoretischen Bitmaske-Wins, dafür risikoarm.

### 3.2 Variante B — Pointer-Routing-Table

Pro Target ein kleines Array von Pointern auf die aktiven Source-Werte. Sample-Loop iteriert nur über aktive Routen.

```cpp
// Member oder Local in renderBlock:
const float* pitchSrcPtrs_[6];   int numPitchSrcs_ = 0;
const float* scanSrcPtrs_[6];    int numScanSrcs_ = 0;
// … pro Target

// Block-rate (einmal in renderBlock vor der Sample-Loop, NACH der ampEnvVal/lfo1Val
// Variablen-Deklaration — die Pointer müssen auf stabile Stack-Addressen zeigen):
float ampEnvVal = 0.0f, mod1EnvVal = 0.0f, mod2EnvVal = 0.0f;
float lfo1Val = 0.0f, lfo2Val = 0.0f, lfo3Val = 0.0f;

numPitchSrcs_ = 0;
if (p.ampTarget  == EnvTarget::Pitch) pitchSrcPtrs_[numPitchSrcs_++] = &ampEnvVal;
if (p.mod1Target == EnvTarget::Pitch) pitchSrcPtrs_[numPitchSrcs_++] = &mod1EnvVal;
// …

// Im Sample-Loop:
ampEnvVal = ampEnv.processSample();   // update am ein Ort
mod1EnvVal = …; lfo1Val = …;          // …
…
float pitchMod = p.driftPitchOffset;
for (int s = 0; s < numPitchSrcs_; ++s) pitchMod += *pitchSrcPtrs_[s];
```

**Pro:** Eliminiert alle Branches im Sample-Loop. Iteriert nur über tatsächlich aktive Routen — typischer User hat 1–2 Routen pro Target, also ~1–2 Iterationen statt 6 Branches.
**Contra:** Pointer-Indirection (kann Compiler-Auto-Vec verhindern). Lebensdauer-Bedingung: die Source-Variablen müssen einen stabilen Stack-Slot haben (im selben Scope wie das Pointer-Array oder als Member).
**Erwartung:** Volle theoretische Wins, plus Branch-Mispredict-Reduktion (ein einzelner kurzer Loop hat bessere Prediction als 6 hintereinander).

### 3.3 Variante C — Bitmaske + branchless sum

```cpp
// configureForBlock:
uint8_t pitchMask_ = 0;
if (p.ampTarget  == EnvTarget::Pitch) pitchMask_ |= 0x01;
if (p.mod1Target == EnvTarget::Pitch) pitchMask_ |= 0x02;
// …

// Im Sample-Loop:
float pitchMod = p.driftPitchOffset;
pitchMod += (pitchMask_ & 0x01) ? ampEnvVal : 0.0f;
// … oder branchless via multiply:
pitchMod += ((pitchMask_ >> 0) & 1) * ampEnvVal;
pitchMod += ((pitchMask_ >> 1) & 1) * mod1EnvVal;
// …
```

**Pro:** Branchless, SIMD-freundlich, der ganze Mask-Test in einem Register.
**Contra:** Multiplikation mit Null hat einen Float-Overhead. Lesbarkeit suffizient gut, aber „magisches Bit-Layout" muss dokumentiert sein.
**Erwartung:** Ähnlich wie B, aber weniger gut für seltene-Routen-Fall (man "zahlt" für alle 6, auch wenn nur 1 aktiv ist).

### 3.4 Empfehlung

**Variante B (Pointer-Routing-Table).** Beste Performance, beste Skalierung bei "wenig Routen aktiv" (= typischer Use-Case), klar lesbar. Variante A als Fallback, falls B beim ersten Profiling unerwartete Auto-Vec-Regressionen produziert.

Variante C nur, wenn du bei Profiling siehst, dass der Branch-Misprediction der dominante Faktor ist — dann ist Branchless der richtige Hammer.

---

## 4. Vorgehen (in dieser Reihenfolge)

### 4.1 MESSEN, bevor irgendwas refactored wird

Per [`docs/PERFORMANCE_GUIDE.md` §4](PERFORMANCE_GUIDE.md): `sample(1)` auf der Standalone-App während eines Worst-Case-Szenarios (16 Voices, Wavetable-Mode, alle 3 Envelopes + 3 LFOs aktiv und auf Pitch + Scan geroutet). Zwei Messpunkte:

1. **Baseline** (vor dem Refactor, ab `af812694`).
2. **Nach jeder Variante** — A, B, C einzeln, jeweils gegen Baseline.

Wenn der gemessene Win < 1 % CPU im Worst-Case ist: **abbrechen und nicht mergen**. Die Komplexität rechtfertigt sich nur bei messbarem Win. (Theoretisch erwartet: 2–4 %, aber Compiler-Auto-Opt könnte schon viel davon weghaben.)

### 4.2 Implementierungs-Reihenfolge (wenn Messung den Refactor rechtfertigt)

1. **Einen Hot-Site zuerst**, nicht alle auf einmal. Vorschlag: Wavetable-Pitch-Mod (971–976) — gut isoliert, klares Modell.
2. **Build + Bug-Hunter-Agent** ("This code has a bug. Find it." mit Fokus auf Lifetime der Source-Variablen).
3. **Re-messen** — bringt es was?
4. Wenn ja: gleiches Muster auf die anderen Sites anwenden, jeweils ein Commit pro Site (single-concern).
5. Falls nicht: revert + Versuch mit anderer Variante.

### 4.3 NICHT in diese Session ziehen

- **`computeDcaGain()`** (Zeile 38–58): hat denselben Pattern (Zeilen 45–47). Verlockend, "auch noch" mitzunehmen, aber das ist eine separate Funktion mit anderem Code-Style. Eigene Session.
- **`std::pow` selbst optimieren** (in 672–682, 829–834): das ist eine andere Optimierungs-Ebene (Lookup-Table o.Ä.). Nicht vermischen.
- **`computeEffectiveLfoDepth`** (Zeilen 569–576, 776–782, 807–813, 918–924): block-rate Routing für LFO-Depth-Modulation. Andere Heuristik (Depth, nicht Target). Eigene Session.

---

## 5. Acceptance Criteria

1. **Zero behavior change.** A/B-Test: identische Audio-Outputs zwischen Pre- und Post-Refactor-Build mit demselben Preset + MIDI. Diff-Tool: `sox preBuild.wav postBuild.wav -n stat` muss `Maximum delta:` < 1e-6 zeigen.
2. **Messbarer Win.** Mind. 1 % CPU im definierten Worst-Case-Szenario (siehe §4.1).
3. **JUCE-Safety unverändert.** Keine neuen Allokationen / Locks / I/O im Audio-Thread (per [`PERFORMANCE_GUIDE.md`](PERFORMANCE_GUIDE.md) §2).
4. **Verifikations-Agent grün:** "This code has a bug. Find it." mit explizitem Hinweis auf Lifetime-Annahmen der Source-Variable-Pointer (für Variante B).

---

## 6. Risiken & Failsafes

| Risiko | Mitigation |
|---|---|
| Pointer-Routing-Table (B): Source-Variable wird umverlegt vom Compiler, Pointer zeigt ins Nirvana | Source-Variablen als **`SynthVoice` Member** statt Locals, oder explizit nicht-inline + `volatile` (letzteres killt Perf, also lieber Member). Im Kommentar dokumentieren. |
| Mid-Block-Target-Change | Strukturell unmöglich: `configureForBlock` läuft nur an Block-Boundaries (VoiceManager.cpp:462). Selbe Garantie wie für `af812694`. |
| `BlockParams::ampTarget` etc. werden über atomic param-cache geschrieben (sequencer steps, automation) | `configureForBlock` snapshot't sie in lokale Bools/Pointer/Maske — selbst wenn der atomic mid-block flippt, sieht der Sample-Loop den Snapshot. |
| Refactor verändert Compiler-Auto-Vectorization-Heuristik (kann sowohl helfen als auch schaden) | Daher §4.1 Messung pro Variante. Theoretischer Vorteil ≠ realer Vorteil. |
| User hat 2 Voices, nicht 16 → realer Win minimal | Genau deshalb: erst messen, dann entscheiden. |

---

## 7. Referenzen

- **Pre-existing Hoist:** Commit `af812694` (`perf(voice): hoist block-constant work out of wavetable inner loop`).
- **Block-Constancy-Beweis:** [`VoiceManager.cpp:440–463`](../src/dsp/VoiceManager.cpp) — Split-at-MIDI-Event-Dispatch macht alle `BlockParams`-Fields innerhalb eines `SynthVoice::renderBlock`-Calls strukturell konstant.
- **Performance-Methodik:** [`docs/PERFORMANCE_GUIDE.md`](PERFORMANCE_GUIDE.md) — `sample(1)`-Methodik, audioIdle-Gate, Anti-Pattern-Katalog.
- **Mod-Target-Hinzufüge-Protokoll:** [`docs/ADDING_A_MODULATION_TARGET.md`](ADDING_A_MODULATION_TARGET.md) — falls der Refactor das Adding-Protokoll verändert, beide Dokumente synchron halten.
- **JUCE-Safety-Checkliste:** [`CLAUDE.md`](../CLAUDE.md) §JUCE Safety — vor jedem Build durchgehen.

---

## 8. Was diese Session NICHT macht

- Keine `computeDcaGain`-Änderung.
- Keine `std::pow`-Replacement (lookup table o.Ä.).
- Keine LFO-Depth-Modulation-Refaktorierung.
- Keine UI-Änderung.
- Keine `BlockParams`-Struktur-Änderung (Felder bleiben, nur ihre Auslese-Stelle ändert sich).
- **Kein Refactor ohne vorherige Messung.** Wenn die Messung nicht überzeugt, ist „nichts tun" das richtige Ergebnis dieser Session.
