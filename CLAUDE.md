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

## Performance (BLOCKING — read `docs/PERFORMANCE_GUIDE.md` before adding GUI/DSP code)

Idle CPU regressions are the #1 historical class of bug in this project. Before adding a timer callback, a `setColour()`, a `repaint()`, an APVTS lookup in `processBlock`, or any per-sample work: consult the anti-pattern catalogue and the audioIdle gate in `docs/PERFORMANCE_GUIDE.md`. Profile with `sample(1)` (methodology in §4 of the guide) when in doubt.

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

## Platform Invariants (BLOCKING — never trade away for an unrelated task)

These are user-observable fundamentals — the instrument's flow concept, not soft preferences. Changing one is NEVER an acceptable side effect of a "stabilize" / perf / refactor / bugfix task. If a task appears to REQUIRE weakening an invariant (e.g. a race or crash seems to force it), STOP and surface the conflict — do the harder correct fix, or ask. Do not pick the convenient shortcut; a diff that locally "stabilizes" something can silently abolish a fundamental and still pass review.

- **A HELD note always plays the CURRENT sample, crossfading over the Regen XFade time.** With A/B drift active, regenerating while a note is held MUST switch the held voice to the new sample via an equal-power crossfade over the global Drift Crossfade (`driftCrossfade`, "Regen XFade", default 200 ms) — NOT a hard swap, and NOT a fixed declicker that ignores the control. All three engines crossfade-follow over that time: Wavetable (`morphToFramesFrom`), Freeze (`morphToBufferFrom`), Sampler (`morphToBufferFrom`). Do NOT re-freeze held voices onto the old buffer, and do NOT swap-then-mask the seam. Broken twice: d03c607a froze held sampler voices (restored f872c73c); a follow-up then hard-swapped + declicked, ignoring Regen XFade (fixed 86726496). Guard: `tools/audition_sampler_follow.cpp` (asserts click-free AND actually-transitioning on both render paths). RT-safe pattern on the lock-free macOS audio thread = an existing engine's morph (atomic master-snapshot read + reclaim slot, `std::atomic_load/store/exchange_explicit`); the sampler morph is audio-thread-sited with plain per-voice morph state + a single reclaim slot, like Wavetable. Mirror an existing engine — never invent a one-off.

When a commit touches a guardian path (voice/buffer distribution, engine data flow, modulation routing), call out any behavior change in the commit body and verify the relevant invariant still holds (audition + adversarial review).

## Migration & Substrate Discipline (BLOCKING — a "port X to Y" task is never a licence to rebuild X smaller)

Born from a real, expensive regression (2026-07-17): told to move the existing rich DCO/LCO **lexicon** (which already knew pwm, morph chains, dirty/overdriven, moving/evolving, inharmonic partials) onto a live **Csound** oscillator, the implementation instead **deleted the lexicon and built a from-scratch 12-word static-partial replacement**. Result: "pwm square wave" rendered as "bright square_stack 8' +vibrato" (a static Fourier square + a fake pitch vibrato — not PWM at all); dirty/overdriven/moving/morphing all silently gone. Two days of ear-testing missed it because every test prompt was a *static* timbre. The order was "switch it to Csound", not "destroy it".

Three hard rules, each independently BLOCKING:

1. **Migration preserves capability; it never reimplements a subset.** When told to port / switch / move / rebuild an existing capable system X onto a new substrate Y, the deliverable is *X's capabilities expressed on Y* — never a greenfield reimplementation that quietly drops what X could do. BEFORE writing Y: enumerate X's existing features/vocabulary in writing (grep the old code — the lexicon keys, the connectors, the techniques), and for EACH one either prove it survives on Y or surface the specific ones that can't and stop. Deleting X's implementation is the LAST step, only after parity is demonstrated — never the first. Reducing a rich system to a convenient subset is a destruction, not a migration, even when every commit builds green.

2. **A substrate chosen because it already knows the domain must be USED for that knowledge.** Csound was chosen precisely because it natively does pwm (`vco2`+`kpw`), FM (`foscil`), waveshaping/dirt, and k-rate motion/morph. If you find yourself hand-authoring a lookup table that reinvents a crippled subset of what the substrate already provides (e.g. static additive partial tables in place of Csound's live oscillators), **STOP** — you are throwing away the entire reason the substrate was chosen. The LLM-routes-to-prefabricated-code guardrail means the prefabricated code must be REAL, capability-complete substrate idioms, not a toy vocabulary.

3. **Acceptance-test the CLASS the feature exists for, not the cases the implementation finds easy.** If the system's whole point is moving / evolving / dirty / morphing sounds, the test corpus MUST contain moving/evolving/dirty/morphing prompts, and an objective check that they actually move/evolve/dirty/morph (e.g. spectral centroid travels, THD rises). A corpus of only the easy (static) cases shares the implementation's blind spot and certifies nothing — a green suite over the wrong class is worse than no suite, because it reads as proof. Movement-by-default is a documented platform fundamental ([[project_lco_movement_by_default]]); a fundamental that is a requirement but not a test is a regression waiting to ship.

**Objective tripwires — because rules 1–3 as prose depend on self-recognition and by-ear honesty, which is exactly what failed here (an adversarial pass against these very rules surfaced each hole below):**

- **The trigger is objective, not a self-diagnosis.** This discipline fires whenever a commit ADDS a fresh implementation of a capability an existing module already provides, OR DELETES/replaces a module a prompt or LLM-routing path reached — regardless of whether the task was framed as migration, feature, refactor, or "delete dead code." The originating regression shipped as `feat(csound)` + `chore(csound): delete the dead … cluster`, never as a migration; a rule that waits for you to *call* it a migration cannot catch a failure whose essence is misrecognition. About to delete a `backend/` file the prompt pipeline imported? This section is active.
- **Parity = the PRIOR system's own capability corpus passing on Y — never the new implementation's green suite.** A corpus synthesized from the new vocabulary inherits its blind spot by construction (the reverted `csound_lexicon_fidelity.py` tested only the four LFO motions the implementation itself defined, and passed). Before deleting X, grep X's real capability prompts (pwm, "a morphing into b", dirty/overdriven, moving) into a frozen corpus that CANNOT be regenerated from Y; assert each capability's objective signature (pwm → duty-cycle sideband asymmetry; morph → ≥2 distinct spectral-centroid plateaus across the note; dirt → THD above the clean baseline). "My new tests are green" is not parity; by-ear "sounds fine" is not proof.
- **Two mechanical gates that fire at authoring time, either of which would have caught this on day one:** (a) the frozen old-corpus parity test above, gating deletion of the old modules; (b) a substrate-idiom grep — if the substrate was chosen for `vco2`/`kpw`/`foscil` and the emitted orchestra contains none of them (only `oscili`/`lfo`), fail. An additive `oscili` bank is precisely the toy vocabulary rule 2 forbids.
- **Name the layer that owns movement.** If the oscillator is deliberately a standing tone while [[project_lco_movement_by_default]] still requires the sound to MOVE, the morph/motion capability must be proven at the layer that DOES own it, on the same end-to-end prompt path the user drives. A per-layer "not my job" must never leave a required capability untested at every layer — resolve the standing-tone-vs-movement split in writing before shipping.

## Release (BLOCKING — applies to every `v*` tag)

Before `git push origin v*` runs, EVERY step of `docs/RELEASE_PROCESS.md` §7 must have been executed AND the evidence pasted into the conversation. No exceptions, no "I already know it works".

In addition to §7 as written:

- **Per-model smoke-test.** For every model added or whose loading path changed in the commit range since the previous tag: run `backend/pipe_inference.py` end-to-end against the actual model files the user will download, confirm a successful generation. The default-preset check in §7 step 3 does NOT cover new models.
- **Dependency pin audit.** If `backend/requirements.txt` changed, OR if a new model was added that requires a specific `stable-audio-tools` / `diffusers` / `transformers` feature: verify the pin (or absence of pin) in `requirements.txt` resolves to a version that supports the model. `pip index versions <pkg>` + reading the model config's expected kwargs is the minimum check.
- **Never force-push to a release tag** unless §11 Option A applies (no published release exists). Always prefer a new patch tag.

Diagnostic discipline when a release crashes in user hands:

- Read primary sources before hypothesizing. `gh run view <id> --log | grep <package>` is the first command, not the last.
- Do not present unverified hypotheses as causes. If you have not reproduced the bug or located the failure in source, say "hypothesis, unverified".

## Documentation

- `ARCHITECTURE.md` — complete code walkthrough (directory layout, lifecycle, GUI hierarchy, voice chain, modulation routing, IPC, APVTS, binary resources)
- `docs/DEV_BUILD.md` — cross-platform build guide (system deps, Python setup, PyInstaller, common errors)
- `docs/IPC_PROTOCOL.md` — JUCE ↔ Python wire format
- `docs/ADDING_A_MODEL.md` — how to add a new inference backend
- `docs/ADDING_A_MODULATION_TARGET.md` — how to add a mod target
- `docs/PRESET_FORMAT.md` — .t5p binary format spec
- `docs/PRESET_LIBRARY_MAINTENANCE.md` — publishing the "UCDCAE AI Lab" bank (the live preset dir on the maintainer Mac IS the git checkout; staging rules, "(mine)" policy, manifest CI)
- `docs/RELEASE_PROCESS.md` — release checklist and CI workflow
- `docs/PERFORMANCE_GUIDE.md` — anti-pattern catalogue, audioIdle gate, profiling methodology, pre-commit checklist
- `docs/devlog.md` — development history and design decisions
