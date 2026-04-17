# T5ynth Developer Documentation

Developer documentation for T5ynth. User-facing documentation lives in the in-app Manual (statusbar → Manual) and in `/README.md` at the repo root.

This directory contains two kinds of files: current contributor reference material, and archived Claude-session snapshots kept only for historical context. Only the "active" section below should be treated as authoritative.

---

## Active contributor documentation

Files in this directory:

- [`DEV_BUILD.md`](DEV_BUILD.md) — Cross-platform build setup (macOS, Linux, Windows 11).
- [`MACOS_INSTALLATION.md`](MACOS_INSTALLATION.md) — End-user macOS installer and Gatekeeper override notes.
- [`IPC_PROTOCOL.md`](IPC_PROTOCOL.md) — JUCE ↔ Python binary pipe protocol specification.
- [`ADDING_A_MODEL.md`](ADDING_A_MODEL.md) — HOWTO for adding a new inference engine.
- [`ADDING_A_MODULATION_TARGET.md`](ADDING_A_MODULATION_TARGET.md) — HOWTO for adding a new mod matrix destination.
- [`PRESET_FORMAT.md`](PRESET_FORMAT.md) — `.t5p` binary preset format specification.
- [`RELEASE_PROCESS.md`](RELEASE_PROCESS.md) — Tag-driven CI release flow.
- [`TESTING.md`](TESTING.md) — Current testing state and conventions.
- [`DEV_DOCS_STATUS.md`](DEV_DOCS_STATUS.md) — Meta: editor's working document tracking the dev-docs pass. Not a contributor reference.

Files elsewhere in the repo:

- [`/CONTRIBUTING.md`](../CONTRIBUTING.md) — Contributor entry point. Start here.
- [`/ARCHITECTURE.md`](../ARCHITECTURE.md) — Code-level layout walkthrough.
- [`/README.md`](../README.md) — Project overview, build summary, license.
- [`/resources/T5ynth_Guide.html`](../resources/T5ynth_Guide.html) — User manual, loaded in-app via `WebBrowserComponent`.

---

## Historical / do not rely on

The files below are archived snapshots from Claude-assisted development sessions. They document state and decisions as of when they were written and may not reflect current code. Contributors should consult the active documentation and the actual code — not these snapshots.

In `docs/`:

- `handover_session9.md` — Session 9 handover notes (macOS port bring-up, 2026-03-31).
- `handover_session10.md` — Session 10 handover notes.
- `handover_session11.md` — Session 11 handover notes.
- `handover_session12.md` — Session 12 handover notes.
- `handover_session13.md` — Session 13 handover notes (device selector, wavetable, DimExplorer design).
- `handover_distribution_session.md` — Distribution pipeline, Windows support, CI/CD, sampler latency session notes.
- `devlog.md` — General chronological development log.
- `bug_analysis_and_roadmap.md` — Point-in-time bug list and roadmap (Session 5 era).
- `guide_audit.md` — Audit of an earlier version of `resources/T5ynth_Guide.html` against source code.
- `portierung_referenz_tabelle.md` — Porting reference table (Vue/TypeScript → JUCE/C++), 3-column mapping.
- `portierung_session5_audit.md` — Self-audit of the porting table against code as of Session 5.
- `portierung_01_useAudioLooper.csv` — Porting tracking CSV: `useAudioLooper.ts`.
- `portierung_02_useWavetableOsc.csv` — Porting tracking CSV: `useWavetableOsc.ts`.
- `portierung_03_useFilter.csv` — Porting tracking CSV: `useFilter.ts`.
- `portierung_04_useEffects.csv` — Porting tracking CSV: `useEffects.ts`.
- `portierung_05_useModulation.csv` — Porting tracking CSV: `useModulation.ts`.
- `portierung_06_useDriftLfo.csv` — Porting tracking CSV: `useDriftLfo.ts`.
- `portierung_07_useStepSequencer.csv` — Porting tracking CSV: `useStepSequencer.ts`.
- `portierung_08_useArpeggiator.csv` — Porting tracking CSV: `useArpeggiator.ts`.
- `portierung_09_orchestrierung.csv` — Porting tracking CSV: overall orchestration.
- `portierung_01_useAudioLooper.ods` — Spreadsheet form of the `useAudioLooper` porting CSV.

At the repo root:

- [`/HANDOVER_AUDIOLDM2.md`](../HANDOVER_AUDIOLDM2.md) — AudioLDM2 integration session notes. Kept for historical context; treat as a snapshot, not current truth.
- [`/PYINSTALLER_DIFFUSERS_GUIDE.md`](../PYINSTALLER_DIFFUSERS_GUIDE.md) — PyInstaller + Diffusers bundling issues session notes. Kept for historical context; treat as a snapshot, not current truth.

---

## I want to…

- …build T5ynth locally → [`DEV_BUILD.md`](DEV_BUILD.md)
- …install the current macOS build → [`MACOS_INSTALLATION.md`](MACOS_INSTALLATION.md)
- …understand the code layout → [`/ARCHITECTURE.md`](../ARCHITECTURE.md)
- …add a new diffusion model → [`ADDING_A_MODEL.md`](ADDING_A_MODEL.md)
- …change how modulation routing works → [`ADDING_A_MODULATION_TARGET.md`](ADDING_A_MODULATION_TARGET.md)
- …understand the `.t5p` format → [`PRESET_FORMAT.md`](PRESET_FORMAT.md)
- …replace the Python inference backend → [`IPC_PROTOCOL.md`](IPC_PROTOCOL.md)
- …cut a new release → [`RELEASE_PROCESS.md`](RELEASE_PROCESS.md)
- …contribute code → [`/CONTRIBUTING.md`](../CONTRIBUTING.md)
- …test a change → [`TESTING.md`](TESTING.md)
