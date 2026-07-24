# Session 18 — t5-base entbundled + Download-Resume; SA3-Integration NICHT begonnen

**Stand: 2026-05-26, nach commit `5ec8ace5`.**

---

## 0. Framing-Warnung an die nächste Session

**Ursprüngliches User-Anliegen:** „Stable Audio 3 ist raus in diversen Varianten. Prüfe was das für t5ynth bedeuten könnte." Strategische Entscheidung im Lauf der Session: **Option 2 — SA3 Small als drittes Modell neben SAO 1.0/Small** (siehe [`project_asset_acquisition`](../).claude/projects/-Users-joerissen-ai-t5ynth/memory/project_asset_acquisition.md) und [`project_injection_kombi_plan`](../.claude/projects/-Users-joerissen-ai-t5ynth/memory/project_injection_kombi_plan.md)).

**Was tatsächlich passiert ist:** Eine Nebenbemerkung („t5-base könnte sogar aus dem Bundle raus, ~430 MB Installer-Ersparnis") hat sich verselbstständigt. Sieben Commits später ist t5-base entbundelt + ein robuster Downloader gebaut — und null Zeilen SA3- oder T5Gemma-Code geschrieben. Siehe [`feedback_session_focus`](../.claude/projects/-Users-joerissen-ai-t5ynth/memory/feedback_session_focus.md) für die Lektion.

**Die geleistete Arbeit ist nicht wertlos** — sie ist *Voraussetzungs-Infrastruktur*, die wir für T5Gemma und SA3 Small brauchen werden (manual-fetch-Pfad, robuster Downloader, GH-Mirror-Pattern). Aber sie war nicht das Anliegen der Session.

---

## 1. Erledigt heute (8 Commits, alle auf `main`)

### 1.1 t5-base entbundelt

| Commit | Was |
|---|---|
| `c820519b` | Backend resolved t5-base aus User-Models-Root (analog zu Modellen), bundled-Pfad nur noch Dev-Fallback. Code in [`backend/pipe_inference.py:364`](../backend/pipe_inference.py) (`_candidate_transformers_dirs`, jetzt mit `_model_search_base_dirs()`-Sweep). |
| `ef2711d6` | SetupWizard-Eintrag „T5-Base text encoder" als `downloadable: true`, Apache-2.0-Lizenz-Dialog, `isGenerationEngine: false` (Auxiliary-Asset zählt nicht für „setup complete"). `hasModelMarker` akzeptiert auch das flat-transformers-Layout (`config.json` + `tokenizer.json`/`spiece.model` + `model.safetensors`). `isEssentialDiffusersFile` akzeptiert root-level Files für flat-Repos. |
| `cea8e2a4` | `pipe_inference.spec` ohne t5-base-Helper-Block. CI-Workflow (`.github/workflows/build.yml`) ohne Pre-Build-Cache-Step und Post-Build-Verify. `tools/cache_t5_base.py` gelöscht. Bundle-Shrink: **852 MB unkomprimiert / ~430 MB im Installer**. |
| `03e17698` | Doc-Sweep über README, RELEASE_PROCESS, LINUX_INSTALLATION, ADDING_A_MODEL, PYTORCH_BUNDLING_NOTES, DEV_BUILD und THIRD_PARTY_LICENSES.txt. |

### 1.2 GitHub-Release-Mirror als Alternative zu HF (Download-Speed)

| Commit | Was |
|---|---|
| `61af55e6` | `.github/workflows/mirror-t5-base.yml` — manuell triggerbar (`workflow_dispatch`), zieht `google-t5/t5-base` von HF, pinnt die HF-Revision, generiert SHA-256-Checksums, lädt vier Files + `checksums.txt` als Release-Assets unter Tag `assets/t5-base-v1` hoch. **Noch nicht ausgeführt — siehe §2.1.** |
| `a80c026e` | Refactor `downloadGhReleaseInThread` auf per-Model-Filelisten. Neues `GhAsset`-Struct + `kT5BaseGhFiles[]`-Array. `KnownModel` hat jetzt `ghFiles` + `ghFileCount`. Bisher hardcoded SAO-Small-Filelist ist weg (SAO wird per User-Entscheidung **nicht** gemirrored — siehe `project_asset_acquisition`). |
| `5ec8ace5` | HTTP-Range-Resume in beiden Download-Pfaden (GH + HF). 416 Range-Not-Satisfiable löscht partial + saubere Meldung (kein Endlosschleifen-Trap). Truncation-Check via `targetFile.getSize()` statt in-memory-`written` (catched disk-full). Resume-Modus unterdrückt den HF-HTML-Error-Sniff, weil 206 Partial Content per Spec niemals einen HTML-Errorbody enthalten darf. |

### 1.3 Memory-Updates

- [`project_versioning`](../.claude/projects/-Users-joerissen-ai-t5ynth/memory/project_versioning.md) — auf `v2.0.2-beta.0` aktualisiert, Win11-Install-Verifikation vermerkt.
- [`project_asset_acquisition`](../.claude/projects/-Users-joerissen-ai-t5ynth/memory/project_asset_acquisition.md) — neu, dokumentiert die Two-Track-Asset-Policy (gated → manual-fetch + SetupWizard, ungated → in-app download). **SAO 1.0/Small explizit NICHT gemirrored** (User-Entscheidung aus früherer Session, nicht neu diskutieren).
- [`feedback_session_focus`](../.claude/projects/-Users-joerissen-ai-t5ynth/memory/feedback_session_focus.md) — neu, beschreibt den Drift dieser Session und die Regel daraus.

---

## 2. Offen — Sekundär (vor SA3 erledigen, sind 10-Minuten-Sachen)

### 2.1 GitHub-Release-Mirror einmalig erzeugen + URL aktivieren

**Schritt 1**: `git push` (8 Commits aus dieser Session liegen lokal).

**Schritt 2**: Im GitHub UI: **Actions** → „Mirror t5-base from HuggingFace to GitHub Release" → **Run workflow**. Default-Inputs lassen genügt. Tag wird `assets/t5-base-v1`, HF-Revision wird auf current main gepinnt + in den Release Notes festgehalten.

**Schritt 3**: Nach erfolgreichem Run die t5-base-Row in [`SetupWizard.cpp`](../src/gui/SetupWizard.cpp) bekommt einen `ghRelease`-URL. Konkret die Zeile in `kKnownModels` (aktuell):

```cpp
    { "t5-base",                 "T5-Base text encoder",       "t5-base", nullptr,
```

→ wird zu:

```cpp
    { "t5-base",                 "T5-Base text encoder",       "t5-base",
      "https://github.com/joeriben/akroasys/releases/download/assets/t5-base-v1",
```

Eine Zeile. Build verifizieren, commit.

### 2.2 Truncation-Detection-Chip dismissen

Im Session-Verlauf wurde ein Chip-Task „Fix truncated-download miss in SetupWizard" gespawnt. Der ist mit `5ec8ace5` (Range-Resume) vollständig überschrieben. Kann gefahrlos dismissed werden.

---

## 3. Offen — Hauptthread: SA3-Integration (die eigentliche Session-Aufgabe)

Reihenfolge wie vom User vorgeschlagen: T5Gemma-Catalog-Eintrag zuerst (kleiner sauberer Test des Manual-Fetch-Pfads), dann Backend, dann SA3-Small-Catalog-Eintrag. Begründung: Backend-Validierung der Injection-Modi ist die einzige Unbekannte; davor sollten Catalog/Asset-Akquise stabil sein.

### 3.1 T5Gemma im SetupWizard registrieren

T5Gemma (`google/t5gemma-b-b-ul2`) ist **gated** auf HF (Gemma Terms of Use). Folgt damit dem SAO-Pattern, NICHT dem t5-base-Pattern:

- `downloadable: false` — kein In-App-Download. User akzeptiert Gemma-Terms im Browser auf HF, lädt manuell via `huggingface-cli download`, dann Auto-Scan oder Browse... im SetupWizard.
- `isGenerationEngine: false` — Auxiliary-Asset (Conditioner für SA3 Small/Medium), nicht selber Generation-Engine.
- License-Notice mit den **Gemma-spezifischen** prohibited-use-Klauseln, nicht copy-paste der Stability-Notice.
- `hfRepo: "google/t5gemma-b-b-ul2"`.

Eintrag in [`SetupWizard.cpp`](../src/gui/SetupWizard.cpp) `kKnownModels` direkt nach `t5-base`. Wortlaut der License-Notice ist offen — vom User abnehmen lassen, weil die Gemma-Prohibited-Use-Liste komplexer ist als Apache-2.0/Stability-CL.

### 3.2 Backend-Änderungen für SA3 Small

Im Detail in der Erst-Analyse (siehe Conversation-Logs vor commit `c820519b`). Drei harte Punkte:

**(a) T5Gemma-Conditioner-Extraktion** in [`backend/pipe_inference.py:606`](../backend/pipe_inference.py):

```python
self._t5_conditioner = None
for key, cond in model.conditioner.conditioners.items():
    if hasattr(cond, 'tokenizer') and hasattr(cond, 'model'):
        self._t5_conditioner = cond
```

Strukturell sollte das mit T5Gemma weiter funktionieren (stable-audio-tools wickelt T5Gemma in einen Conditioner mit `tokenizer`+`model`-Attributen). Aber: `max_length=128` ist hard-coded ([`pipe_inference.py:1149+`](../backend/pipe_inference.py)); T5Gemma hat eventuell ein anderes Optimum. Embedding-Dimension ändert sich.

Falls `stable-audio-tools` einen separaten `T5GemmaConditioner`-Klasse hat: paralleler `_patch_stable_audio_tools_t5_registry`-Mechanismus für `T5GemmaConditioner.T5GEMMA_MODELS` (oder wie immer das Registry-Attribut heißt). **Vor dem Implementieren in `stable-audio-tools` source schauen.**

**(b) Sampler: pingpong statt BrownianTree-SDE.** SA3 HF-Card zeigt `sampler_type="pingpong"`, default 8 Schritte, CFG 1.0. Das macht den BrownianTree-Workaround in [`backend/pipe_inference.py:499`](../backend/pipe_inference.py) (`_patch_scheduler_brownian_tree`) und den Seed-Hack ([Zeilen 98–107](../backend/pipe_inference.py)) **für SA3 obsolet**. Aber: SAO 1.0/Small bleiben aktiv und brauchen das weiter. Also nicht löschen — model-spezifisch dispatchen.

CLAUDE.md (§Key Constraints) behauptet aktuell: „The Python backend exists because Stable Audio requires `torchsde`'s BrownianTree SDE sampler". Das wird mit SA3 *zur Hälfte* unwahr. **Nicht** löschen — SAO braucht es weiter. Eine ergänzende Note in CLAUDE.md, dass dies für die SAO-Familie gilt, nicht für SA3.

**(c) DiT-Blockzahl modell-spezifisch.** `split_start` / `split_end` werden im native-Pfad auf `[0, 16]` geklemmt (siehe [`pipe_inference.py:1141+`](../backend/pipe_inference.py) im Injection-Mode-Handling) — implizite SAO-Small-Annahme. Aus `model_config.json` lesen, ans Frontend in der `MODEL_LOADED`-IPC-Message mitsenden, Plugin-UI klemmt den Range-Slider dynamisch.

### 3.3 SA3 Small im SetupWizard registrieren

Nach Backend-Stabilität. `downloadable: false` (Stability gated), `isGenerationEngine: true`, License-Notice copy-paste-bar von SAO Small (gleiche SA Community License). `hfRepo: "stabilityai/stable-audio-3-small"`.

### 3.4 Injection-Modi auf T5Gemma re-validieren

Größte Unbekannte. Die Modi `linear`, `delta`, `late_step`, `layer_split`, `kombi1`, `kombi2`, `kombi3` operieren alle auf T5-Embeddings. T5Gemma hat anderen Embedding-Vektor-Shape. **Empirisch prüfen**:

1. Linear-Modus produziert sinnvollen A↔B-Crossfade auf SA3 Small.
2. Dimension-Offsets bewegen das Klangbild messbar.
3. Kombi-Modi (per `project_injection_kombi_plan` aktiver Designpfad) produzieren erkennbar unterschiedlichen Output bei verschiedenen `split_start`/`split_end`.

Wenn (1) und (2) brechen, ist die Embedding-Math nicht trivial portierbar. Dann zurück zu Whiteboard mit dem User.

---

## 4. Architektur-Lage zum schnellen Einstieg

### 4.1 Modell-Pfad-Dispatch

In [`backend/pipe_inference.py`](../backend/pipe_inference.py):
- `find_models()` (line ~307) — scannt alle MODEL_DIR-Kandidaten, klassifiziert nach `model_index.json` (diffusers) oder `model_config.json` (native).
- `_load_native_pipeline()` (line ~657) — der Pfad für SAO Small (und künftig SA3 Small). Hier landet die T5Gemma-Adaptation.
- `_generate_native()` (line ~1141) — Generation, hier ist Injection-Modi-Math drin.

### 4.2 SetupWizard-Catalog

In [`src/gui/SetupWizard.cpp`](../src/gui/SetupWizard.cpp), Top-Level:
- `GhAsset` Struct + `kT5BaseGhFiles[]` — Vorlage für künftige Mirror-Files (T5Gemma bekommt erstmal *keinen* Mirror, weil gated und Redistribution-Pass-Through aufwendig wäre).
- `KnownModel` Struct + `kKnownModels[]` — die vier aktuellen Rows + die zwei (T5Gemma + SA3 Small) die noch dazukommen.
- `hasModelMarker()` (line ~14) — erkennt heute zwei Layouts (audio + flat-transformers). Falls SA3 ein drittes Layout mitbringt, hier ergänzen.

### 4.3 Asset-Akquise-Policy (Kern-Memory)

Siehe [`project_asset_acquisition`](../.claude/projects/-Users-joerissen-ai-t5ynth/memory/project_asset_acquisition.md). Kurz:
- **Gated** (Stability CL, Gemma Terms) → `downloadable: false`, manual-fetch.
- **Ungated** (Apache-2.0, CC-BY-NC) → `downloadable: true`, in-app.
- **T5ynth fasst HF-Auth nie an.**

### 4.4 Build-Verifikation

Standardes Pattern aus CLAUDE.md:
```bash
cmake --build build_clean --config Release -j$(sysctl -n hw.ncpu) --target T5ynth_Standalone
```
Reicht für GUI-Iteration. Vor Tag-Release: voller `cmake -S . -B build_clean -DCMAKE_BUILD_TYPE=Release && cmake --build build_clean --config Release` plus PyInstaller-Bundle.

---

## 5. Verification-Agent-Praxis

Jede C++-Änderung in dieser Session hat einen Opus-Verification-Agent durchlaufen („This code has a bug. Find it."). Drei davon fanden reale Bugs (`hasAnyInstalledModel` ohne `isGenerationEngine`-Filter, `isEssentialDiffusersFile` ohne flat-Layout-Support, 416-Range-Trap im Resume-Pfad). Pattern beibehalten — gerade für Backend-Änderungen am Inference-Pfad ist die Bug-Rate signifikant.

---

## 6. Was die nächste Session NICHT tun sollte

- t5-base weiter umorganisieren. Es ist sauber.
- Den SetupWizard kosmetisch überarbeiten („auxiliary assets" Sektion etc.) — das ist als Settings-Redesign im Memory deferred, kommt nach SA3 wenn überhaupt.
- SAO 1.0/Small auf GitHub-Mirror umstellen — User-Entscheidung dagegen, im Memory festgehalten.
- Die existierende BrownianTree-Logik löschen, weil SA3 sie nicht braucht — SAO braucht sie weiter.

Direkt mit §3.1 (T5Gemma-Catalog-Eintrag) anfangen.
