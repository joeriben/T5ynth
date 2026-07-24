# Session Handover — Distribution Pipeline + Sampler Refactor (2026-04-05)

## Thema: v1.0 Distribution, Windows-Support, CI/CD, Sampler-Latenz

### Was gemacht wurde

#### 1. LibTorch + deprecated Code entfernt (0cc1578)
- 14 Dateien gelöscht: T5ynthInference, DiffusionScheduler, BackendManager, BackendConnection, GenerationRequest, SemanticAxes, ModelDownloader (alles Stubs/Legacy)
- LibTorch + SentencePiece aus CMakeLists.txt entfernt
- Build vereinfacht: `cmake -B build && cmake --build build` — kein LibTorch-Download mehr
- Fallback-Pfade in MainPanel und PromptPanel entfernt
- -1742 Zeilen

#### 2. PyInstaller-Spec + bundled binary support (984dfe4)
- `backend/pipe_inference.spec` — bündelt pipe_inference.py + torch + diffusers + torchsde
- PipeInference.cpp: `findBundledBinary()` sucht PyInstaller-Binary (dist/pipe_inference/pipe_inference), Fallback auf Python+Script (Dev-Modus)
- MainPanel: Backend-Discovery akzeptiert .py, binary, und dist/-Layout
- macOS App-Bundle: Contents/Resources/backend/ wird geprüft

#### 3. Windows-IPC (5cb3591)
- Alle `#ifdef _WIN32 → return false` Stubs ersetzt mit echten Implementierungen:
  - `launch()`: CreatePipe + CreateProcess mit STARTUPINFOW, redirected stdin/stdout
  - `shutdown()`: TerminateProcess + WaitForSingleObject + CloseHandle
  - `isChildAlive()`: GetExitCodeProcess / STILL_ACTIVE
  - `readExact()` / `writeExact()`: ReadFile/WriteFile auf Pipe-Handles
- `isConnected()` Methode abstrahiert plattformspezifische Handle-Checks
- NOMINMAX in CMakeLists.txt (verhindert windows.h min/max Macro-Kollision)
- Binärprotokoll ist identisch auf beiden Plattformen

#### 4. CI/CD — GitHub Actions (f23778e + Fixes)
- `.github/workflows/build.yml` — drei Plattformen: macOS-14 (ARM), windows-latest, ubuntu-latest
- Jede Plattform: PyInstaller bündelt Backend → CMake baut JUCE → Artefakte assembled
- CUDA torch (cu124) auf Windows + Linux, MPS auf macOS
- Linux: Swap-Erweiterung (8GB /mnt/swapfile) für PyInstaller-RAM
- Release-Job: `gh release create` bei Tag-Push (v*)
- **Alle drei Plattformen bauen grün** (Run 23989422142)

#### 5. MSVC-Kompatibilität (fd3fa12)
- 6 fehlende Includes hinzugefügt (MSVC hat keine transitiven Standard-Header):
  - SetupWizard.cpp: `<thread>`
  - StateVariableFilter.h: `<algorithm>`
  - PluginProcessor.h: `<limits>`
  - GuiHelpers.h: `<limits>`
  - SynthVoice.cpp: `<cstring>`
  - PresetFormat.cpp: `<cstring>`
- Linux apt-Pakete ergänzt: libxcomposite-dev, libxext-dev, libxrender-dev, libfontconfig1-dev
- requirements.txt: safetensors, accelerate, stable-audio-tools hinzugefügt

#### 6. .gitignore Fix (c016dd7)
- `build/` → `/build/` — verhindert dass `JUCE/extras/Build/` (case-insensitive macOS) ausgeschlossen wird
- 42 JUCE Build/CMake-Dateien nachgetrackt (waren nie im Repo)

#### 7. libcurl optional auf Windows (bab3668)
- CMake: `find_package(CURL REQUIRED)` nur auf Linux, `QUIET` sonst
- JUCE_USE_CURL konditionell via Generator-Expression
- Windows nutzt WinHTTP (JUCE-intern), macOS NSURLSession

#### 8. Auto-Scan für WavetableOscillator (f932d19)
- WavetableOscillator: Auto-Scan-Modus — Scan-Position läuft automatisch vorwärts
- Neue Methoden: setAutoScan, setAutoScanRate, setAutoScanLoop, retriggerAutoScan
- extractContiguousFrames() — kontiguöse Frame-Extraktion (nicht pitch-synchron)
- Loop-Wrapping: OneShot, Loop, PingPong
- **PROBLEM:** SynthVoice Sampler-Pfad wurde auf Osc umgeroutet → klingt wie Wavetable, nicht wie Sampler. Muss revertiert werden für den Sampler. Auto-Scan bleibt als Wavetable-Feature.

### Offene Punkte / Nächste Session

#### 1. Sampler Multisampling — HAUPTAUFGABE
**Problem:** Sampler braucht formanterhaltende Transposition ohne Latenz. Speed-Resampling (processSample) hat null Latenz aber Chipmunk-Effekt bei großen Intervallen. Signalsmith Stretch hat 50ms Latenz.

**Lösung:** Pre-gerenderte transponierte Buffers (Multisampling):
- Nach `loadGeneratedAudio()`: Stretch offline für Stützstellen (z.B. alle 4 Halbtöne = ~16 Buffers)
- Bei noteOn: nächsten Stützpunkt-Buffer wählen, Rest-Ratio per Speed-Resampling (max ±2 Halbtöne)
- Background-Thread, ~10s nach Generierung. Fallback auf Speed-Resampling bis fertig.

**Revert nötig:** SynthVoice.cpp renderBlock() muss den Sampler-Pfad wieder über SamplePlayer routen (nicht über Osc). Der Auto-Scan-Commit (f932d19) hat beides zusammengelegt — Sampler-Teil davon rückgängig machen, Auto-Scan für Wavetable behalten.

Betroffene Dateien:
- `src/dsp/SamplePlayer.h/cpp` — PitchZone struct, buildMultisamples(), sharePitchZonesFrom()
- `src/dsp/SynthVoice.cpp` — Sampler-Pfad revert auf renderPitchedBlock/processSample
- `src/PluginProcessor.cpp` — buildMultisamples() Aufruf + StatusBar-Feedback
- `src/dsp/VoiceManager.cpp` — PitchZone-Verteilung an Voices

#### 2. Release-Tag setzen
- v1.0.0-rc.1 Tag war gesetzt, Release-Job schlug fehl (Permission → gefixt, Tag → gelöscht)
- Neu taggen wenn Sampler-Multisampling steht
- Workflow-Permissions auf "Read and write" stehen (bereits konfiguriert)

#### 3. Repo ist public
- https://github.com/joeriben/akroasys — GPLv3, public seit 2026-04-05
- CI läuft kostenlos (keine Minutenbegrenzung)
- Keine Secrets in Git-History

#### 4. Weitere offene Punkte (nicht in dieser Session bearbeitet)
- AudioLDM2 auf feature/audioldm2-engine Branch (1 Commit, nicht gemergt)
- Sequencer-Presets überarbeiten
- Demo-Presets erstellen
- Modulation-Target-Ranges prüfen
- First-Run-Wizard automatisch bei fehlendem Modell
- HF Boost kaum hörbar (+3dB/4kHz, +4.5dB/10kHz) — Werte evtl. erhöhen
- Windows Pipe ReadFile blockiert ohne Timeout wenn Python hängt (Runtime-Bug, nicht Build)

### Geänderte Dateien (alle Commits dieser Session)

#### Gelöscht (14 Dateien)
- `src/inference/T5ynthInference.h/cpp`, `src/inference/DiffusionScheduler.h/cpp`
- `src/backend/BackendManager.h/cpp`, `src/backend/BackendConnection.h/cpp`
- `src/backend/GenerationRequest.h/cpp`, `src/backend/SemanticAxes.h/cpp`
- `src/backend/ModelDownloader.h/cpp`

#### Neu erstellt
- `.github/workflows/build.yml` — CI/CD
- `backend/pipe_inference.spec` — PyInstaller

#### Modifiziert
- `CMakeLists.txt` — LibTorch weg, CURL optional, NOMINMAX
- `.gitignore` — `/build/` statt `build/`
- `backend/requirements.txt` — safetensors, accelerate, stable-audio-tools
- `src/PluginProcessor.h/cpp` — Legacy-Code weg, kontiguöse Frame-Extraktion
- `src/gui/MainPanel.h/cpp` — Backend-Discovery, tryLoadNativeInference weg
- `src/gui/PromptPanel.cpp` — Dead includes weg, Fallback-Block weg
- `src/gui/SetupWizard.cpp` — `<thread>` include
- `src/gui/GuiHelpers.h` — `<limits>` include
- `src/dsp/StateVariableFilter.h` — `<algorithm>` include
- `src/dsp/SynthVoice.cpp` — `<cstring>`, Auto-Scan-Routing (Sampler-Teil muss revertiert werden)
- `src/dsp/WavetableOscillator.h/cpp` — Auto-Scan, extractContiguousFrames
- `src/dsp/SamplePlayer.h` — (unverändert im finalen Stand)
- `src/presets/PresetFormat.cpp` — `<cstring>` include
- `JUCE/extras/Build/` — 42 Dateien nachgetrackt

### Wichtige Erkenntnisse

1. **Sampling ist ein Spezialfall von Wavetable** — aber nur für den Oszillator-Teil. Der Sampler-Klangcharakter (Original-Audio, kein Frame-Normalisierung) darf nicht verloren gehen. Auto-Scan = Wavetable-Feature. Multisampling = Sampler-Feature.

2. **.gitignore `build/` ist gefährlich** — matcht auf macOS case-insensitive auch `Build/`. Hat JUCE CMake-Dateien aus dem Repo ausgeschlossen. Immer `/build/` (Root-only) verwenden.

3. **MSVC ist strikt bei Includes** — Clang/GCC ziehen Standard-Header transitiv über JUCE. MSVC nicht. Jede Datei braucht ihre eigenen Includes.

4. **CI vor Push lokal prüfen** — Drei Audit-Agenten laufen lassen (C++ cross-platform, CI-Workflow Simulation, Windows-spezifisch) bevor gepusht wird. Spart CI-Minuten und Iterationen.

5. **Feature-Branch nicht vergessen** — `feature/audioldm2-engine` existiert mit einem echten Commit. Nicht versehentlich dort commiten (ist in dieser Session zweimal passiert).
