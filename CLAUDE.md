# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

```bash
cmake -S . -B build_clean -DCMAKE_BUILD_TYPE=Release
cmake --build build_clean --config Release -j$(sysctl -n hw.ncpu)
```

Always `build_clean/` with Release config. Artifacts at `build_clean/T5ynth_artefacts/Release/{Standalone,VST3,AU}/`.

For dev: the Python backend runs from `backend/pipe_inference.py` via the local `.venv` — no PyInstaller needed during iteration. PyInstaller is only for release builds.

## Architecture

Read `ARCHITECTURE.md` for the full code-level walkthrough. Key facts:

- **Two-process design:** JUCE C++ plugin + Python inference subprocess communicating over stdin/stdout binary protocol (`docs/IPC_PROTOCOL.md`).
- **PluginProcessor** owns everything: APVTS (single source of truth for all real-time params), DSP chain, VoiceManager (16-voice pool), PipeInference client, sequencers.
- **PluginEditor** is a thin wrapper — all UI lives in `src/gui/MainPanel` and its child panels.
- **processBlock** reads all APVTS params into a `BlockParams` struct once per block. Voice rendering never touches APVTS directly.
- **Inference is blocking** — `PipeInference::generate()` runs on a background thread in PromptPanel, never on the audio thread.
- **Engine data distribution:** Master oscillator/sampler instances on the processor, per-voice copies share buffer data via pointers. Lock-free by design.

## JUCE Safety (BLOCKING — check before every build)

1. Every `juce::Timer` subclass MUST call `stopTimer()` in its destructor, BEFORE any member is destroyed.
2. NEVER call `setLookAndFeel(nullptr)` — crashes macOS WindowServer, killing ALL GUI apps.
3. APVTS attachments (`unique_ptr<SliderAttachment>`) must be declared AFTER their target components in the `.h` file (reverse destruction order).
4. NEVER allocate memory, lock a mutex, or do file I/O on the audio thread.
5. After copying files into an app bundle, re-sign: `codesign --force --deep --sign -`.

## Key Constraints

- The Python backend exists because Stable Audio requires `torchsde`'s BrownianTree SDE sampler — no C++ equivalent exists. Do not attempt C++ inference.
- PyInstaller runtime hook (`backend/runtime_hook.py`) disables `multiprocessing.resource_tracker` to prevent fork bombs on macOS. Never add multiprocessing-using packages to runtime hooks.
- `JUCE_WEB_BROWSER=1` is required (Manual overlay). Linux needs explicit `juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS` link + `libgtk-3-dev`.

## Process

- Read ALL relevant files before writing code. State reasoning before acting.
- After every code change: spawn a verification agent (model: opus) with "This code has a bug. Find it."
- Before every build: check JUCE destruction order, audio thread safety, crash vectors.
- One concern per commit.
- Never hallucinate names — grep the repo. Canonical: "UCDCAE AI Lab".
- Never modify preset files (.t5p) or binary resources without explicit permission.
- Never add UI elements not requested.

## Documentation

- `ARCHITECTURE.md` — complete code walkthrough (directory layout, lifecycle, GUI hierarchy, voice chain, modulation routing, IPC, APVTS, binary resources)
- `docs/DEV_BUILD.md` — cross-platform build guide (system deps, Python setup, PyInstaller, common errors)
- `docs/IPC_PROTOCOL.md` — JUCE ↔ Python wire format
- `docs/ADDING_A_MODEL.md` — how to add a new inference backend
- `docs/ADDING_A_MODULATION_TARGET.md` — how to add a mod target
- `docs/PRESET_FORMAT.md` — .t5p binary format spec
- `docs/RELEASE_PROCESS.md` — release checklist and CI workflow
- `docs/devlog.md` — development history and design decisions
