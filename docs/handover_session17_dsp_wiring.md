# Session 17 Continuation — DSP-Wiring der BPM-Sync (Steps 3–6) + offene Aufräumarbeiten

**Stand: 2026-05-01, nach commit `cd9b1d52`.**
Dieses Handover ersetzt nicht das ursprüngliche
[`handover_session17_bpm_sync.md`](handover_session17_bpm_sync.md), sondern verweist darauf für die
detaillierten Specs. Hier dokumentiert ist nur **was nach heute noch offen ist**, in der
Reihenfolge, in der es angegangen werden sollte.

---

## 1. Erledigt heute (Steps 1+2 + UI-Alignment)

- **Step 1 — APVTS Layout:** `ClockMode` und `ClockDivision` Namespaces in
  [`src/dsp/BlockParams.h`](../src/dsp/BlockParams.h), 14 neue PIDs (3× LFO, 3× Drift, 1× Delay
  jeweils `*ClockMode` + `*ClockDivision`), passende `AudioParameterChoice`-Einträge in
  [`PluginProcessor::createParameterLayout()`](../src/PluginProcessor.cpp).
- **Step 2 — Frontend Design Pass:** `ClockButtonLnF` (Uhren-Icon, orange-Fill on toggle),
  `LfoSection`/`DriftSection` um `clockBtn`/`clockModeHidden`/`divisionRow` erweitert, LFO um den
  `[F/T]`-1-Cycle-Button (`modeBtn`/`modeHidden`) der die alte `Free⌄`-ComboBox ersetzt, FxPanel um
  `delayClockBtn`/`delayClockModeHidden`/`delayDivisionRow`. Visibility-Swap zwischen Rate-Row und
  Division-Row per `clockModeHidden.onChange`.
- **Step 2.5 — Cross-Row-Spaltenausrichtung** (war ein Sub-Handover,
  [`handover_session17_alignment_fix.md`](handover_session17_alignment_fix.md)): Mod-Section's
  zentraler `leftLabelWidth`/`rightLabelWidth`-Block in `SynthPanel::resized()` umfasst jetzt auch
  die `divisionRow`s; `FxPanel::resized()` erstmalig mit demselben Pattern für Delay; naïve
  `setForcedLabelWidth(36)`-Hardcodes aus `initLfo`/`initDrift` raus; Empty-Label-Shrink-Bug in
  `SliderRow::chooseLayout` behoben.

---

## 2. Offen — Hauptthread: DSP-Wiring (Steps 3–6)

Spec im Detail in
[`handover_session17_bpm_sync.md`](handover_session17_bpm_sync.md) §3.3–3.6. Reihenfolge wie dort.

### 2.1 Step 3 — Host-BPM Plumbing (`PluginProcessor`)

Ziel: in `processBlock` host-BPM und Transport-Status pro Block lesen, in zwei `std::atomic<>`
Membern ablegen, plus eine `resolveSyncBpm()`-Methode mit der dokumentierten Priorität:

```
live host > running in-app sequencer > frozen hostBpmLastSeen > seqBpm fallback
```

Neue Member auf dem Processor (`src/PluginProcessor.h`):

```cpp
std::atomic<float> hostBpmLastSeen { 0.0f };
std::atomic<bool>  hostPlayingNow  { false };
```

Pro Block (`processBlock`, ganz oben oder direkt nach BlockParams-Read):

```cpp
bool playing = false;
if (auto* ph = getPlayHead())
    if (auto pos = ph->getPosition())
        if (pos->getIsPlaying()) {
            playing = true;
            if (auto bpm = pos->getBpm())
                hostBpmLastSeen.store((float) *bpm);
        }
hostPlayingNow.store(playing);
```

`seqRunningNow()`: für die Resolution-Priorität wird ein `bool seqRunningNow() const` auf dem
Processor gebraucht. Schau zuerst, ob der Sequencer-Run-State schon irgendwo als `std::atomic<bool>`
gehalten wird (suche nach Sequencer-Play/Stop-Handlers in `SequencerPanel.cpp` oder im
StepSequencer); nur falls nicht trivial lesbar, eine kleine Wrapper-Methode hinzufügen.

```cpp
float T5ynthProcessor::resolveSyncBpm() const {
    if (hostPlayingNow.load())  return hostBpmLastSeen.load();
    if (seqRunningNow())        return seqBpm.load();
    const float h = hostBpmLastSeen.load(std::memory_order_relaxed);
    return (h > 0.0f) ? h : seqBpm.load();
}
```

Die Spec verlangt diese Priorität bewusst — siehe Resolved Decision §6.4 im
ursprünglichen Handover (DAW-Pause-Verhalten).

### 2.2 Step 4 — Sync-Rate Berechnung für LFOs

Wenn `lfo*ClockMode == sync`, effektive Rate aus BPM und Division:

```
rateHz = factor * (bpm / 60.0) / 4.0
```

(Whole note bei BPM dauert `4 * 60/bpm` Sekunden; `factor` Events pro Whole Note ⇒ Rate =
`factor / wholeNoteSeconds`.)

`factor` kommt aus `ClockDivision::kEntries[idx].factor` oder einem dort exportierten
`kFactor[]`-Array (im commit ist die Tabelle in `BlockParams.h` schon fertig; verifiziere dass das
Factor-Feld nutzbar ist — falls nicht, dort ergänzen, NICHT eine zweite Tabelle anderswo anlegen).

LFO-Rate wird heute pro Block via `lfo1.setRate(bp.lfo1Rate)` gesetzt — Position laut altem Handover
[`PluginProcessor.cpp:978`](../src/PluginProcessor.cpp). Davor einen Sync-Override:

```cpp
const int lfo1Clock = static_cast<int>(parameters.getRawParameterValue(PID::lfo1ClockMode)->load());
const float lfo1RateEff = (lfo1Clock == ClockMode::Off)
    ? bp.lfo1Rate
    : computeSyncRate(resolveSyncBpm(),
                      static_cast<int>(parameters.getRawParameterValue(PID::lfo1ClockDivision)->load()));
lfo1.setRate(lfo1RateEff);
```

`computeSyncRate(bpm, divisionIdx)` als kleine free function in `BlockParams.h` (oder einer
DSP-Utilities-Header) — wird auch von Drift wiederverwendet.

**Erst LFO 1 zum Laufen bringen**, dann das Pattern auf 2/3 anwenden.

### 2.3 Step 5 — Apply auf Drift

Identisches Pattern wie LFO. Drift's Rate wird auch pro Block gesetzt — die Stelle finden und
analog wrappen. Reuse `computeSyncRate`.

### 2.4 Step 6 — Apply auf Delay-Time

Andere Formel:

```
delayMs = (60000.0 / bpm) * (4.0 / factor)
```

`PluginProcessor.cpp:1751` (laut altem Handover) liest `baseDelayTime`. Davor:

```cpp
const int delayClock = static_cast<int>(parameters.getRawParameterValue(PID::delayClockMode)->load());
const float delayMsEff = (delayClock == ClockMode::Off)
    ? baseDelayTime
    : computeSyncDelayMs(resolveSyncBpm(),
                         static_cast<int>(parameters.getRawParameterValue(PID::delayClockDivision)->load()));
```

### 2.5 Verifikation für Steps 3–6

1. Standalone öffnen, Sequencer auf Play. LFO 1 auf Sync mit "1/4" — sollte mit dem Sequencer
   pulsen.
2. Im DAW (kein Standalone) — Plugin laden, Transport starten — LFO 1 Sync sollte der DAW-BPM
   folgen.
3. DAW-Transport stoppen, Sequencer im Plugin nicht laufen → Rate friert ein (frozen
   `hostBpmLastSeen`).
4. Sequencer dann starten → Rate switcht auf `seqBpm` (sequencer hat Vorrang über frozen host).
5. Drift / Delay analog testen.
6. Preset speichern + laden — alte Presets ohne die neuen Felder müssen weiter laden und sich auf
   `ClockMode::Off` verhalten (Step 3.7 im alten Handover).

---

## 3. Offen — Sekundäre Threads

### 3.1 Step 8 — Free/Trig Regression Bugfix (LFO)

Aus dem alten Handover §2.2: "Free/Trig funktioniert nicht". Trail:

- [`PluginProcessor.cpp:122-124`](../src/PluginProcessor.cpp) liest `lfo1Mode/lfo2Mode/lfo3Mode` →
  `lfoNTrigMode` bools.
- Diese fließen in `voiceManager.setDroneNote(...)` bei :126 und im per-Block-Update bei :1635.
- Verdacht: der Bool wird gelesen, erreicht aber nie `Lfo::setMode` / `setRetriggerOnNote` im
  `SynthVoice` — dort anfangen.

User-Vorgabe: **separater Commit, nach den Feature-Steps 3–6.** Nicht bundlen.

### 3.2 GitHub-Issue [#19](https://github.com/joeriben/T5ynth/issues/19) — `forcedValueWidth`-Inkonsistenz

Heute aufgemacht. Mod-Section LEFT-Spalte hat 3 verschiedene Value-Width-Strategien (ENV natural,
LFO 56, Drift 64); RIGHT-Spalte ist überall natural. Slider-Track-Right-Edges innerhalb der
Section sind dadurch nicht aligned. Der `forcedLabelWidth`-Side ist schon zentralisiert in
`SynthPanel::resized()`; der `forcedValueWidth`-Side wartet auf den gleichen Refactor.

Vorschlag im Issue: zwei zentrale Werte (`leftValueWidth = 64`, `rightValueWidth = 56`) im
Mod-Block setzen, die hartcodierten `setForcedValueWidth(56)`/`(64)` aus `initLfo`/`initDrift` raus.

Out of scope für heute, separates Workitem. Erst nach Steps 3–6 anfassen.

### 3.3 Step 9 — Dokumentation

- `docs/CHANGELOG.md`: BPM-Sync-Eintrag für die nächste Version (vermutlich v1.7.0-beta.1 oder
  v1.6.0-beta.2 — Versionsstrategie mit User klären).
- In-app Manual §1 (sucht in `src/manual/` oder den `BinaryData`-Resourcen): kurze Erklärung der
  Clock-Buttons + Sync-Mode.
- `ARCHITECTURE.md`: Modulation/Effects-Abschnitt um den Sync-Path ergänzen — wo `resolveSyncBpm()`
  lebt, dass Division-Faktoren in `BlockParams.h` zentral sind, dass die UI Visibility-Swap macht.

---

## 4. Was NICHT anfassen

- Sequencer-Step-Rate, Arpeggiator-Rate, `seqBpm` selbst — sind schon BPM-synced und korrekt.
- Injection-Mode-Code aus v1.6.0-beta.1.
- Preset-Format / IPC — alle neuen Felder serialisieren via APVTS automatisch, IPC bleibt
  unberührt (siehe altes Handover §3.7).
- Die UI-Layout-Änderungen aus heute (commit `cd9b1d52`) — die sind verifiziert und vom User
  abgesegnet. Issue #19 ist eine separate, zukünftige Verbesserung, nicht ein Fix der heutigen
  Arbeit.

---

## 5. Build & Test

```bash
cmake --build build_clean --config Release -j$(sysctl -n hw.ncpu)
open build_clean/T5ynth_artefacts/Release/Standalone/T5ynth.app
```

Reminder aus CLAUDE.md: nach jeder Code-Änderung Verifikations-Agent (model: opus) mit *"This code
has a bug. Find it."* spawnen. Vor jedem Build JUCE-Destruction-Order und Audio-Thread-Safety
prüfen.
