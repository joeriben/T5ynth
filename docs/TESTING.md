# TESTING

This document describes the actual current state of testing in T5ynth. It is
honest about what exists and what does not. Read it before opening a PR so you
understand what is and is not verified automatically.

## 1. Current state — honest summary

T5ynth has no formal unit test suite. There is no `tests/` directory, no
`juce::UnitTest` runner, no `pytest` configuration, and no dedicated test
target in `CMakeLists.txt`. A grep for `enable_testing`, `add_test`, `ctest`
or `UnitTest` in `CMakeLists.txt` returns nothing.

CI runs a single smoke test on macOS only (see
`.github/workflows/build.yml`'s `Smoke-test app bundle` step): it launches the
built `.app`, sleeps five seconds, and confirms the process is still alive.
Nothing more.

Manual validation is the norm. The workflow is: build, run the standalone,
load the default preset, trigger a generation, and listen. T5ynth is a
personal research side-project and the testing bar reflects that. If you want
stronger guarantees, you have to add them yourself.

## 2. What CI does verify

The GitHub Actions workflow `.github/workflows/build.yml` has three build
matrix jobs (macOS, Windows, Linux) plus a tag-triggered release job. In
terms of actual verification (as opposed to "did the compile succeed"), only
the following happens:

- **All three platforms must build cleanly**, both the C++ plugin and the
  PyInstaller backend bundle. A compile error on any platform fails the
  workflow.
- **Python backend smoke test (macOS only)**, step `Verify bundled binary
  starts`: the bundled `pipe_inference` binary is started with `/dev/null` as
  stdin, wrapped in `timeout 10`, and stderr is captured. The step is written
  with `|| true`, so it cannot actually fail — it exists only to surface
  obvious startup crashes in the log. It is not a rigorous test. It verifies
  only that the binary does not immediately segfault on launch.
- **App bundle smoke test (macOS only)**, step `Smoke-test app bundle`:
  - `test -x` on the `T5ynth` binary inside the `.app`
  - `test -x` on the bundled `pipe_inference` binary inside
    `Contents/Resources/backend/`
  - `otool -L` on the main binary (printed to log, not asserted)
  - Launches `T5ynth.app/Contents/MacOS/T5ynth` in the background, sleeps 5
    seconds, then checks whether the PID is still alive. If yes, the process
    is killed and the step passes. If no, the workflow fails.

That is the entire automated verification surface. No audio is generated in
CI. No preset is loaded. No IPC round-trip is exercised. No Windows or Linux
bundle is ever launched.

## 3. Ad-hoc validation scripts in `tools/`

The `tools/` directory contains single-shot utilities used during DSP and
inference-pipeline development. They are not repeatable unit tests — most of
them hardcode paths under `~/Library/T5ynth/` and are meant to be run by hand
while debugging a specific issue. They are documented here as artefacts, not
as a test suite to run.

| File                              | Purpose                                                                                         |
|-----------------------------------|-------------------------------------------------------------------------------------------------|
| `tools/test_delay_damp.cpp`       | Standalone C++ test for the delay damping biquad lowpass. Compile with clang++/g++ and run.     |
| `tools/test_delay_damp`           | Compiled binary of the above (checked into the working tree as a build artefact).               |
| `tools/test_euler.py`             | Scheduler/sampler validation: runs the Euler scheduler path against Stable Audio Open.          |
| `tools/compare_schedulers.py`     | Step-by-step comparison of the diffusers scheduler against T5ynth's own scheduler implementation. |
| `tools/generate_reference.py`     | Generates a reference WAV using the real diffusers `StableAudioPipeline` on CPU. Intended as the "ground truth" for regression comparison. |
| `tools/validate_original.py`      | Runs the original `StableAudioPipeline` (not TorchScript) for comparison against the bundled path. |
| `tools/validate_scheduler.py`     | Compares the T5ynth scheduler logic against `diffusers.CosineDPMSolverMultistepScheduler` latent-by-latent. |
| `tools/validate_export.py`        | Layer-by-layer comparison of original model outputs vs TorchScript exports.                     |
| `tools/validate_torchscript.py`   | Runs the exported TorchScript pipeline with the same parameters as the C++ path, writes WAV for diffing. |
| `tools/export_to_torchscript.py`  | Exports Stable Audio Open 1.0 components to TorchScript. Not a test, a build helper.            |
| `tools/convert_stable_audio_small.py` | Converts `stable-audio-open-small` to diffusers layout. Not a test, a conversion helper.    |
| `tools/test_inference.cpp`        | Minimal standalone C++ diagnostic: load models, run one step, verify shapes. Not wired into the CMake build. |

Most of these were written to isolate a specific bug (e.g. scheduler drift,
shape mismatch, delay-damp not engaging) during development and never
generalised into a reusable test harness. Treat them as reference code for
how to poke at a specific subsystem, not as a pass/fail suite.

## 4. Expected manual test protocol for PRs

Before submitting a PR, at minimum perform the following steps. This is the
de-facto contract — if a reviewer hits any of these on a fresh checkout of
your branch, your PR is not ready.

1. **Release build succeeds.**
   ```
   cmake -B build_clean -DCMAKE_BUILD_TYPE=Release
   cmake --build build_clean --config Release
   ```
   Always use `build_clean/` with the Release config. Do not create alternate
   build directories.
2. **Standalone launches and stays open.** Run the built
   `T5ynth.app`/`T5ynth` binary directly. It must not crash during startup
   and must remain alive after the splash/setup logic finishes.
3. **Default preset loads without errors in the status bar.** Any red-text
   error or exception surfaced in the status bar is a regression.
4. **Trigger one generation.** Click Generate. Audio must play. This
   exercises the full JUCE → named pipe → Python → response path.
5. **Settings overlay opens. Manual overlay opens. Both render.** No missing
   widgets, no clipped text, no black rectangles.
6. **No regressions in any panel the PR touches.** Visually and aurally
   verify every panel that could possibly be affected by the changed code.
7. **If the PR touches the inference pipe**: run one full cycle of
   generation with a non-default prompt to confirm the protocol is intact.
   See `docs/IPC_PROTOCOL.md`.
8. **If the PR touches drift**: enable drift regen, wait ~10 seconds, and
   confirm a new generation arrives and crossfades cleanly without clicks or
   volume jumps.

## 5. Additional tests for specific change types

The manual protocol above is the baseline. Changes to specific subsystems
require additional verification:

- **DSP changes (envelope, filter, LFO, effects):** load a preset, play
  several notes at a range of velocities and hold times, listen for
  artefacts (clicks, zipper noise, denormals, envelope resets on retrigger).
- **IPC protocol changes:** round-trip a request manually. Launch
  `python backend/pipe_inference.py`, paste a JSON request to stdin, and
  read the `\x01` + header + PCM response from stdout. The protocol is
  specified in `docs/IPC_PROTOCOL.md`.
- **Preset format changes:** save a preset, quit the app, restart, reload
  the preset, and verify every stored parameter is preserved. The format is
  specified in `docs/PRESET_FORMAT.md`.
- **Model install / setup wizard flow:** either uninstall a model via the
  Model Manager UI to trigger the walkthrough, or temporarily hide an
  installed model with
  `mv ~/Library/T5ynth/models/<id> ~/Library/T5ynth/models/<id>.bak`
  and restore it when you are done.

## 6. How to add proper tests — recommendation

This is an opportunity area. Contributors are welcome to propose adding
proper test infrastructure. A reasonable starting point:

- **C++:** `juce::UnitTest` is the idiomatic in-tree choice. Tests would live
  under `src/tests/` (or a top-level `tests/`) and be wired via a
  `juce_add_console_app` target, or a plain `add_executable` linking the
  required JUCE modules.
- **CMake gating:** a dedicated test runner target exposed through a CMake
  option, e.g. `T5YNTH_BUILD_TESTS=OFF` by default, so the option does not
  slow down normal release builds or add dependencies to the shipping
  artefact.
- **Python:** `pytest` in `backend/tests/`. The obvious first tests:
  protocol header parsing, request/response shape, and determinism spot
  checks against a captured reference.

Until that infrastructure exists, the honest expectation is: you are
responsible for manually verifying your own change, and reviewers are
responsible for spot-checking it on their own machine.

## 7. CI smoke-test expansion — future opportunity

The current macOS-only smoke test is weak. A PR that broke Linux or Windows
at runtime but compiled cleanly would pass CI today. Stronger expansions, in
rough order of value:

- **Linux and Windows bundle launch tests.** Analogous to the macOS step:
  run the binary, wait a few seconds, kill it. Linux requires a headless
  display; `Xvfb` works. Windows requires launching without interactive
  desktop, which is practical on GitHub runners.
- **Python-only backend smoke test.** Dispatch a fake generation request
  against a lightweight stub model (or mock the pipeline object) and assert
  that the response header shape is correct. This catches regressions in
  the IPC framing without needing a real model download in CI.
- **Preset round-trip regression.** Load → save → load → compare the raw
  bytes of the resulting `.t5p` file. Catches accidental format drift.

None of these exist today. Adding any one of them would be a clear net win.

## 8. Reproducibility as a stand-in for testing

T5ynth guarantees deterministic audio output for a given
`(seed, prompts, parameters, model)` tuple. This is achieved by
monkey-patching `torchsde._randn` to use numpy's PCG64 generator, which
produces identical bitstreams on CPU, CUDA, x86 and ARM. See
`_patch_torchsde_for_determinism()` in `backend/pipe_inference.py` and the
seed-handling contract documented in `docs/IPC_PROTOCOL.md`.

The practical consequence for testing: any DSP or pipeline change can be
regression-checked by capturing a reference generation *before* the change,
applying the change, regenerating with the same seed and parameters, and
diffing the two audio buffers (bit-exact for anything upstream of audio
output). `tools/generate_reference.py` appears to be the intended mechanism
for producing such reference WAVs against the real diffusers pipeline.

This is not a test suite. It is a manual capture-and-diff workflow. But for
a project whose core output is audio, it is often the only meaningful
regression signal.
