# T5ynth Developer Documentation

Developer documentation for T5ynth. User-facing documentation lives in the in-app Manual (statusbar → Manual) and in `/README.md` at the repo root.

This directory contains two kinds of files: current contributor reference material, and archived Claude-session snapshots kept only for historical context. Only the "active" section below should be treated as authoritative.

---

## Active contributor documentation

Files in this directory:

- [`DEV_BUILD.md`](DEV_BUILD.md) — Cross-platform build setup (macOS, Linux, Windows 11).
- [`LINUX_INSTALLATION.md`](LINUX_INSTALLATION.md) — Linux / Fedora 42 source build path for developers and build hosts.
- [`LINUX_PACKAGING.md`](LINUX_PACKAGING.md) — Linux package-layer docs, currently Fedora RPM and Ubuntu/Debian `.deb`, using a named prebuilt isolated backend bundle.
- [`PYTORCH_BUNDLING_NOTES.md`](PYTORCH_BUNDLING_NOTES.md) — Detailed findings from freezing and packaging the Python/Torch backend on Linux, including Blackwell-specific pitfalls.
- [`CUDA_BUNDLE_TARGETS.md`](CUDA_BUNDLE_TARGETS.md) — Short target matrix for generic NVIDIA vs. Blackwell bundles and the future Windows installer.
- [`MACOS_INSTALLATION.md`](MACOS_INSTALLATION.md) — End-user macOS installer and Gatekeeper override notes.
- [`IPC_PROTOCOL.md`](IPC_PROTOCOL.md) — JUCE ↔ Python binary pipe protocol specification.
- [`ADDING_A_MODEL.md`](ADDING_A_MODEL.md) — HOWTO for adding a new inference engine.
- [`ADDING_A_MODULATION_TARGET.md`](ADDING_A_MODULATION_TARGET.md) — HOWTO for adding a new mod matrix destination.
- [`INTERFACE_DESIGN_POLICY.md`](INTERFACE_DESIGN_POLICY.md) — UI text, contrast, and control-density rules for the synth interface.
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

---

## I want to…

- …build T5ynth locally → [`DEV_BUILD.md`](DEV_BUILD.md)
- …install packaged T5ynth on Fedora / Ubuntu / Linux → [`LINUX_PACKAGING.md`](LINUX_PACKAGING.md)
- …build T5ynth from source on Fedora / Linux → [`LINUX_INSTALLATION.md`](LINUX_INSTALLATION.md)
- …build a Fedora RPM or Ubuntu/Debian `.deb` → [`LINUX_PACKAGING.md`](LINUX_PACKAGING.md)
- …understand what actually broke while freezing the PyTorch backend → [`PYTORCH_BUNDLING_NOTES.md`](PYTORCH_BUNDLING_NOTES.md)
- …see the short GPU/bundle target matrix for RTX 3080-class hosts and Windows → [`CUDA_BUNDLE_TARGETS.md`](CUDA_BUNDLE_TARGETS.md)
- …install the current macOS build → [`MACOS_INSTALLATION.md`](MACOS_INSTALLATION.md)
- …understand the code layout → [`/ARCHITECTURE.md`](../ARCHITECTURE.md)
- …add a new diffusion model → [`ADDING_A_MODEL.md`](ADDING_A_MODEL.md)
- …change how modulation routing works → [`ADDING_A_MODULATION_TARGET.md`](ADDING_A_MODULATION_TARGET.md)
- …change the GUI style or add controls → [`INTERFACE_DESIGN_POLICY.md`](INTERFACE_DESIGN_POLICY.md)
- …understand the `.t5p` format → [`PRESET_FORMAT.md`](PRESET_FORMAT.md)
- …replace the Python inference backend → [`IPC_PROTOCOL.md`](IPC_PROTOCOL.md)
- …cut a new release → [`RELEASE_PROCESS.md`](RELEASE_PROCESS.md)
- …contribute code → [`/CONTRIBUTING.md`](../CONTRIBUTING.md)
- …test a change → [`TESTING.md`](TESTING.md)
