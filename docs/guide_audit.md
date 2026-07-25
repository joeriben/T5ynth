# T5ynth User Guide — Code Verification Audit

Date: 2026-04-07
Audited file: `resources/T5ynth_Guide.html`
Method: Every factual claim verified against source code by file and line number.

## Summary

- **Total claims verified:** 96
- **Correct:** 89
- **Wrong (fixed):** 7

## Errors Found and Fixed

| # | Section | Claim in Guide | Actual (Code) | Source | Fix |
|---|---------|---------------|---------------|--------|-----|
| 1 | Engine (Voice Count) | "equal power: each voice at 1/√N" | `1/N^0.3` (flatter curve) | VoiceManager.cpp:326 | Changed to "Gain auto-scales per voice count" |
| 2 | Setup | "Settings (gear icon in the status bar)" | Text button labeled "Settings" | StatusBar.cpp:35 | Changed to "Settings in the status bar" |
| 3 | Generation Controls | "Auto" marked as "Planned feature — not yet implemented" | Auto-regen IS implemented via drift_regen param | PluginProcessor.cpp:190, PromptPanel.cpp:261 | Removed "(planned)" note |
| 4 | Setup | SA Open 1.0 "no account needed" (after prior incorrect removal) | SA Open 1.0 requires HF token; SA Small does not | SetupWizard.cpp:25,227-231,673-678 | Restored HF token requirement for SA Open 1.0 |
| 5 | LFO Target | Link to `#mod` (Modulation Routing) | Anchor `#mod` was removed in earlier edit | — | Replaced with inline target list |
| 6 | Setup Troubleshooting | "Is the Python venv set up?" | Guide says "no Python setup needed" in same section | — | Removed contradictory venv check |
| 7 | DimExplorer | Only described left side (A-B diff) | Right side (shared dims) produces more fundamental changes | DimensionExplorer.cpp:73-76 | Added left/right zone explanation |

## Verified Correct Claims (by section)

### Setup
- [x] Python backend handled transparently (PipeInference launches subprocess)
- [x] Settings button exists (StatusBar.cpp:35)
- [x] Model download via Settings panel with progress bar (SetupWizard.h:66)
- [x] SA Small from GitHub Releases (SetupWizard.cpp:26-27)
- [x] SA Open 1.0 requires HF token (SetupWizard.cpp:25,227-231)
- [x] Models dir: macOS ~/Library/T5ynth/models/ (config.py:34-39)
- [x] Models dir: Linux ~/.local/share/T5ynth/models/ (config.py:34-39)
- [x] Auto-detect by model_index.json or model_config.json (SetupWizard.cpp:6-7)
- [x] Device auto-detect: CUDA > MPS > CPU (config.py:13-22)
- [x] Status bar shows connection status (StatusBar.h:15)
- [x] Lazy loading on first generation (config.py:59)

### Generation Controls
- [x] Alpha range -2.0 to +2.0 (PluginProcessor.cpp:98)
- [x] Alpha quadratic curve (PluginProcessor.cpp:95-110)
- [x] Magnitude range 0.001-5.0 (PluginProcessor.cpp:114)
- [x] Noise range 0.0-1.0 (PluginProcessor.cpp:117)
- [x] Duration range 0.1-47s (PluginProcessor.cpp:120)
- [x] Steps range 1-100 (PluginProcessor.cpp:122)
- [x] CFG range 1.0-15.0, default 7 (PluginProcessor.cpp:124-125)
- [x] Start Position range 0.0-1.0 (PluginProcessor.cpp:127-128)
- [x] Seed range -1 to 999999999 (PluginProcessor.cpp:130)
- [x] Random button exists (PromptPanel.h:92)
- [x] HF Boost toggle exists (PluginProcessor.cpp:411)
- [x] Three model slots (PromptPanel.cpp:95,177)
- [x] GPU/CPU toggle (PromptPanel.cpp:104-105)

### Semantic Axes
- [x] 3 dropdown slots (AxesPanel.cpp:45)
- [x] Slider range -1.0 to +1.0, step 0.002 (AxesPanel.cpp:65)
- [x] 8 axes with correct names (AxesPanel.cpp:8-18)
- [x] Backend keys correct (AxesPanel.cpp:23-30)
- [x] Pole labels match (AxesPanel.cpp:90-96, pipe_inference.py:324-333)

### DimExplorer
- [x] 768 bars (DimensionExplorer.h:54)
- [x] Sorted by |baseValue| descending (DimensionExplorer.cpp:73-76)
- [x] Green=A, Orange=B colors (DimensionExplorer.cpp:7-8)
- [x] Two prompts shows A-B diff (DimensionExplorer.cpp:67-70)
- [x] Edited bars blue (DimensionExplorer.cpp:9)
- [x] Apply+Generate button (MainPanel.cpp:137)
- [x] Undo/Redo/Reset buttons (MainPanel.cpp:170-187)
- [x] Mini-view below Axes, click opens overlay (MainPanel.cpp:148-152)
- [x] Offsets: manipulated[:,:,idx] += val (pipe_inference.py:462-466)

### Engine Modes
- [x] Two modes: Sampler, Wavetable (PluginProcessor.cpp:135)
- [x] Signalsmith Stretch for pitch (SamplePlayer.h:139)
- [x] Loop modes: One-shot, Loop, Ping-Pong (PluginProcessor.cpp:305)
- [x] Crossfade 0-500ms (PluginProcessor.cpp:308)
- [x] Loop Optimize: Off/Low/High (PluginProcessor.cpp:312-313)
- [x] Normalize toggle (PluginProcessor.cpp:310)
- [x] Scan 0.0-1.0 (PluginProcessor.cpp:24-26)
- [x] Voice count: Mono/4/6/8/12/16 (PluginProcessor.cpp:31)
- [x] Oldest note stealing (VoiceManager.cpp:286-295)

### Filter
- [x] SVF TPT topology (StateVariableFilter.h:48)
- [x] Types: Off/LP/HP/BP (PluginProcessor.cpp:56)
- [x] Slopes: 6/12/18/24 dB (PluginProcessor.cpp:59)
- [x] Cutoff 20-20000Hz (PluginProcessor.cpp:50)
- [x] Resonance 0-1 → Q 0.5-18 (StateVariableFilter.h:42-45)
- [x] Mix 0-1 equal-power (StateVariableFilter.cpp:45-46)
- [x] Kbd Track 0-1 (PluginProcessor.cpp:65)

### Envelopes
- [x] 3 identical ADSR envelopes (SynthVoice.h:83-85)
- [x] Each has target dropdown (PluginProcessor.cpp:270-277)
- [x] DCA is a target option (PluginProcessor.cpp:272)
- [x] A 0-5000, D 0-5000, S 0-1, R 0-10000 (PluginProcessor.cpp:35-45)
- [x] Amount 0-1, Vel Sens 0-1, Loop toggle (PluginProcessor.cpp:238-265)

### LFOs
- [x] Two LFOs (PluginProcessor.h:122-123)
- [x] Rate 0.01-30Hz (PluginProcessor.cpp:168,178)
- [x] Depth 0-1 (PluginProcessor.cpp:170,180)
- [x] Waveforms: Sine/Tri/Saw/Square (PluginProcessor.cpp:174,185)
- [x] Modes: Free/Trigger (PluginProcessor.cpp:291,294)
- [x] Target dropdown (PluginProcessor.cpp:281,285)

### Drift
- [x] 3 drift LFOs (DriftLFO.h:44)
- [x] Rate 0.001-2.0Hz (PluginProcessor.cpp:197-210)
- [x] Depth 0-1 (PluginProcessor.cpp:200,206,212)
- [x] Waveforms: Sine/Tri/Saw/Square (PluginProcessor.cpp:223-235)
- [x] Drift Enable toggle (PluginProcessor.cpp:188)
- [x] Targets: Alpha, Axis 1-3, WT Scan, Filter, Pitch, Dly Time/FB/Mix, Rev Mix, ENV1-3 Amt (PluginProcessor.cpp:216-222)
- [x] Regen modes: Manual/Auto/1st Bar (PluginProcessor.cpp:190-192)
- [x] Crossfade 0-2000ms (PluginProcessor.cpp:193-195)
- [x] Ghost indicators orange (PromptPanel.cpp:286)

### Effects
- [x] Delay Off/Stereo (PluginProcessor.cpp:320)
- [x] Delay time 1-2000ms (PluginProcessor.cpp:70)
- [x] Feedback 0-0.95 (PluginProcessor.cpp:73)
- [x] Damping 0-1, 0=bright 20kHz, 1=dark 500Hz (DelayLine.h:8-9)
- [x] Mix 0-1 (PluginProcessor.cpp:76)
- [x] Reverb Off/Dark/Medium/Bright/Algo (PluginProcessor.cpp:323)
- [x] EMT 140 convolution IRs (PluginProcessor.cpp:432-434)
- [x] Algo: Room/Damping/Width all 0-1 (PluginProcessor.cpp:86,88,92)
- [x] Signal chain: Delay→Reverb→Limiter→Master (PluginProcessor.cpp:1087-1198)

### Step Sequencer
- [x] UI dropdown 2-32 (SequencerPanel.cpp:226)
- [x] Per step: note/velocity/enabled/bind (StepSequencer.h:81-88)
- [x] BPM 20-300 (PluginProcessor.cpp:343)
- [x] Divisions 1/1-1/16 (PluginProcessor.cpp:348-349)
- [x] Gate 0.1-1.0 (PluginProcessor.cpp:367)
- [x] Glide 10-500ms (PluginProcessor.cpp:352-353)
- [x] Octave -2 to +2 (PluginProcessor.cpp:370-371)
- [x] 10 presets with correct names (StepSequencer.cpp:99-109)
- [x] Save/Load S/L buttons (SequencerPanel.h:51-52)
- [x] Gen pattern transfers to step seq on deactivation (PluginProcessor.cpp:698-719)

### Generative Sequencer
- [x] GEN button (SequencerPanel.h:87)
- [x] Steps 2-32 (PluginProcessor.cpp:381)
- [x] Pulses 1-32 (PluginProcessor.cpp:383)
- [x] Rotation 0-31 (PluginProcessor.cpp:385)
- [x] Mutation 0-1, default 0.80 (PluginProcessor.cpp:387-388)
- [x] Range 1-4 octaves (PluginProcessor.cpp:390)
- [x] Scale Root C-B (PluginProcessor.cpp:403)
- [x] Scale Types correct (PluginProcessor.cpp:406)
- [x] Fix toggles for all 4 params (PluginProcessor.cpp:393-400)
- [x] Bjorklund's algorithm (EuclideanRhythm.h:44-60)
- [x] Stride = max(1, steps-pulses) (GenerativeSequencer.cpp:160)
- [x] Triangle wave traversal (GenerativeSequencer.cpp:171-182)
- [x] Velocity from gaps (GenerativeSequencer.cpp:188-192)
- [x] Mutation formula: floor(rate * pulses * 0.6) (GenerativeSequencer.cpp:234-235)
- [x] Mutation breathing: sinusoidal, 16-cycle, [50%-100%] (GenerativeSequencer.cpp:377-385)
- [x] Drift period: 8 cycles (GenerativeSequencer.h:106)

### Arpeggiator
- [x] Modes: Off/Up/Down/UpDown/Random (PluginProcessor.cpp:362)
- [x] Rate: 7 divisions incl. triplets (PluginProcessor.cpp:357)
- [x] Octaves 1-4 (PluginProcessor.cpp:359)
- [x] Major triad: 0,4,7 (Arpeggiator.h:49)
- [x] UpDown: endpoints not doubled (Arpeggiator.cpp:32)

### Presets
- [x] .t5p format (PresetFormat.h:57)
- [x] Stores params + prompts + audio + embeddings + seed + device + model (PresetFormat.cpp:9-97)
- [x] Import/Export buttons (PresetPanel.h:30-31)

---

# Addendum — Resynth section (2026-07-24)

Reason: `resources/T5ynth_Guide.html` linked `<a href="#resynth">§ Resynth</a>` from the
drift-target note, but no `id="resynth"` and no Resynth section existed. Section written
(now `7. Resynth`; sections 7–17 renumbered to 8–18, TOC and the in-text `§9` / `section 17`
cross-references updated with them). Same method as the 2026-04-07 audit: every factual
claim below verified against source by file and line.

## Verified Claims — Resynth

### Control and gating
- [x] One `RESYNTH` slider in the Generation column, below the SNAP/CACHE row (MainPanel.cpp:1162-1196, layout 3962-3977)
- [x] `resynthAmount` range 0.0–1.0, step 0.05, default 0.0 (PluginProcessor.cpp:1234-1236)
- [x] No separate on/off — the slider minimum IS off (PluginProcessor.cpp:1220-1221)
- [x] Word read-out Off/Min/Subtle/Medium/Strong/Full at 0 / .05 / .25 / .50 / .75 / 1.0; other steps show a percentage (MainPanel.cpp:1175-1184)
- [x] Slider and both source buttons enabled only for SA3, else disabled + dimmed (MainPanel.cpp:1245-1260)
- [x] SA3 = model id contains "stable-audio-3" (PromptPanel.cpp:1997)
- [x] Request path drops the audio seed off SA3 even with a non-zero amount (PromptPanel.cpp:3076, comment 3052-3054)
- [x] SA3 is `model_type` "diffusion_cond_inpaint" → `generate_diffusion_cond_inpaint`, the entry point that takes `init_audio` (pipe_inference.py:3148-3152, 3185-3196)
- [x] LCO mode forces the row off + dimmed (MainPanel.cpp:1610-1614)

### Amount → denoise start
- [x] `initNoiseLevel = 0.90 − 0.85 × amount` (PromptPanel.cpp:3127); table values 0.86 / 0.69 / 0.48 / 0.26 / 0.05 follow from it
- [x] Low amount = prompt-dominant, high amount = preserves the fed-in audio (PromptPanel.cpp:3107-3126; backend semantics pipe_inference.py:2454-2457)
- [x] Any Re-Prompt stance other than Off forces `initNoiseLevel = 0.9` regardless of the slider (PromptPanel.cpp:3139-3142)

### int / ext
- [x] APVTS choice `resynthSource` {int, ext}, default 0 = Internal (PluginProcessor.cpp:1259-1261; BlockParams.h:1158-1166)
- [x] int seeds from `getGeneratedAudioRaw()` = raw model output before HF Boost (PromptPanel.cpp:3095-3103; PluginProcessor.h:321-325)
- [x] int attaches only when a prior buffer exists — first generation of a session is text-only (PromptPanel.cpp:3098, comment 3054-3057)
- [x] ext window length = the `genDuration` parameter (PromptPanel.cpp:2952, 3087)
- [x] ext window capped at `kMaxCaptureSeconds` = 12.0 s (PluginProcessor.h:824; PluginProcessor.cpp:2064, 2763)
- [x] Library `prepare_audio` resamples and pad/crops the seed to the model (pipe_inference.py:2465-2466)
- [x] ext capture peak-normalised (to 0.95) before sending (PluginProcessor.cpp:2797-2807)
- [x] Mono input is copied to both ring channels (PluginProcessor.cpp:2834)
- [x] Silence guard: peak < 1e-4 → `dest.setSize(0,0)` + false → no `init_audio` → text-only (PluginProcessor.cpp:2783-2795; PromptPanel.cpp:3083-3085)
- [x] ext button enabled only when SA3 AND `hasExternalInputAvailable()` (MainPanel.cpp:1256-1260; PluginProcessor.h:339-345), live-refreshed without a reload (MainPanel.cpp:3652-3667)
- [x] Input bus is optional: disabled / mono / stereo (PluginProcessor.cpp:2737-2743)

### Drift and the standing loop
- [x] Drift target entry "Resynth" exists, grouped with the generation targets (BlockParams.h:453-455, 465, 484)
- [x] Drift offset for Resynth = waveform × depth × half-range 0.5, i.e. ±0.5 at Depth 1.0 (DriftLFO.cpp:58, applied 102-117)
- [x] Effective = base + offset clamped to 0–1; NaN sentinel when not modulated (PluginProcessor.cpp:5384-5408)
- [x] Ghost indicator painted on the RESYNTH row (MainPanel.cpp:3690-3698)
- [x] pollDriftRegen uses the drifted value, else the slider (PromptPanel.cpp:3951-3957)
- [x] Standing trigger: SA3 + effective amount > 0.01 keeps regenerating at the Regen Mode cadence (PromptPanel.cpp:4041-4049)
- [x] Manual Regen Mode does not auto-regenerate at all (PromptPanel.cpp:3899-3902)
- [x] Release/lock: a false→true parameter-change edge detaches the seed for one round, then re-locks at the set amount (PromptPanel.cpp:4098-4134)
- [x] Loop floor `kResynthLoopFloor` = 0.05 (PromptPanel.cpp:135, applied 4131)

### Seed
- [x] VAR switchbox: none = fixed base seed, last = reuse previous, auto = new random seed (PromptPanel.cpp:326-335); sits right of Duration (PromptPanel.cpp:367-370, 1407-1412)
- [x] Default seed mode is `base` (PromptPanel.h:578); `lastSeed = 123456789`, `lastRandomSeed = false` (PluginProcessor.h:807-808)
- [x] `req.seed = getLastRandomSeed() ? -1 : getLastSeed()` (PromptPanel.cpp:2960)
- [x] Backend draws a fresh seed only when `seed < 0` (pipe_inference.py:2604-2606, native path)
- [x] Realized seed written back after each generation (PromptPanel.cpp:3477, 3633)

## Why a Resynth loop can repeat (corrected 2026-07-25 after adversarial review)

An earlier draft of this note and of the Guide claimed the loop repeats because "with VAR
none/last and an unchanged ext input the request is **byte-identical** each round." Two
independent adversarial passes (Opus, reading the current tree) refuted the byte-identity
framing and narrowed the mechanism. What is code-provable vs. what needs a model run:

**Code-provable (holds):**
- The loop sets only the Resynth amount per round and leaves the seed to VAR
  (PromptPanel.cpp:4098-4139); the sole seed source is `getLastRandomSeed() ? -1 : getLastSeed()`
  (PromptPanel.cpp:2960).
- SA3 + init_audio routes through `_generate_native`, which re-draws only for `seed < 0`
  (pipe_inference.py:2604); the legacy-SAO draw site (~3266) is never reached by SA3. So in
  `none`/`last` the same fixed seed is used verbatim every round.
- No anti-convergence / seed-perturbation controller exists anywhere in `src/` (grep empty).

**The byte-identity claim was wrong for an audible static input.** `snapshotExternalCapture`
copies the last N samples of a *live rolling ring* ending at the current write position and
peak-normalises to that window's own magnitude (PluginProcessor.cpp:2747, gain `0.95/mag`
at :2807). Successive loop rounds are seconds apart, so each captures a different,
phase-shifted, differently-scaled slice — the request is **not** byte-identical for a held
chord / static pad. The only byte-identical ext case is **silence**, where the silence guard
attaches no init_audio (PluginProcessor.cpp:2788) and the round renders deterministically
**text-only**; a pinned seed then reproduces it.

**The audible "hang" is the low-init_noise mechanism, not the seed.** At high Resynth the
denoise start is very low (Full → 0.05, PromptPanel.cpp:3127), so a spectrally-static input
dominates the diffusion and successive renders sound the same even with a fresh seed. This is
a model-behaviour statement, provable only by running (the reference measurement is
`tools/test_resynth_loop_hang.py`; note that memory's earlier "defeats the anti-convergence
controller" framing predates the controller's removal from the tree). For `int`, the fed-back
audio changes each round, so the loop generally moves; at very high Resynth it too can settle.

**Separate, seed-independent path:** when the inference cache is full, both manual and
auto-regen short-circuit into `playNextCachedInference()` (PromptPanel.cpp:3406, 3565) and
replay a fixed rotating set without generating — the most literal "stuck samples." Off by
default (capacity 0), but a preset can restore a full cache.

**Is it a defect?** For the truly fixed case (same seed, silent or high-Resynth-dominated
input) identical output is the deterministic behaviour VAR `none` promises ("no variation"),
not a malfunction. The Guide now frames it that way and names VAR `auto` as the control that
introduces variation.

## Proposed seed fix — REVIEWED AND REJECTED (2026-07-25)

A proposed fix ("while the standing loop is engaged, force `req.seed = -1` for the round") was
adversarially reviewed and rejected:
- It contradicts VAR `none`'s documented "no variation" and defeats seed determinism, which is
  load-bearing here (event-log replay via `realizedSeed` PromptPanel.cpp:3755; presets
  PresetFormat.cpp:180).
- Decisive: the unconditional `setLastSeed(result.seed)` writeback (PromptPanel.cpp:3633/3639)
  would overwrite `kBaseSeed` with the drawn random seed in the durable store while VAR still
  shows "none" (`syncSeedModeFromCurrentState` is not re-run after a generation) — the next
  manual Generate silently uses the leftover random seed, and a preset saved mid-loop reloads
  `none` → `last` with a random number.
- It over-fires (int and live-changing ext already evolve), and "input is static" is not a
  state the code can read without diffing captured buffers.
- VAR `auto` already delivers fresh-seed-per-round under user control with correct state. The
  real gap is discoverability, which the Guide now addresses.

## Pre-existing errors found while writing (since FIXED on main by 3ec03b3d)

Both were flagged here as outside the original task, then corrected on main by
`3ec03b3d docs(guide): the manual described controls that are no longer in the instrument`
(landed via the parallel akróasys-rename line, separate from this audit's branch). Recorded
for the trail; verified fixed in the current file.

| Section | Old claim in Guide | Actual (Code) | Now |
|---------|---------------|---------------|-----|
| Drift & Regenerate | Regen Mode = "Manual / Auto / max 1♩ / 4♩ / 16♩" | manual / a.s.a.p. / 1 / 2 / 4 / 8 / 16 **bars** (BlockParams.h:1088-1103; PromptPanel.cpp:3926-3935) | Table now lists the bar-based modes |
| Generation Section | "Seed −1 … 999 999 999" + a "Random" button; Easy/Advanced with a numeric seed editor | The seed editor and Random toggle were removed; the VAR switchbox (none/last/auto) is the seed control, and Advanced is the written-oscillator (LRO) panel (PromptPanel.h:147-149; PromptPanel.cpp:326-335) | VAR row + the "two views / two sound sources" section |
