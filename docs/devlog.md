# T5ynth Development Log

## 2026-05-01 — BPM-sync + Free/Trig + Delay crossfade (v1.7.0-beta.1)

Shipped four-day continuation of session 17: the BPM-sync UI from the
previous commit (`cd9b1d52`) is now wired through the DSP, the dead
LFO Trigger mode finally retriggers, the delay's dry/wet curve is a
true crossfade, and two preset-load stickiness bugs that surfaced
during testing are fixed.

### BPM-sync wiring (Steps 3-6 of the session-17 plan)

`T5ynthProcessor::resolveSyncBpm()` is the single source of truth for
synced rates, with priority **live host > running in-app sequencer >
frozen `hostBpmLastSeen` > `seqBpm` fallback**. The host transport is
snapshotted at the top of `processBlock` into two relaxed-ordering
atomics (`hostBpmLastSeen`, `hostPlayingNow`); both are written and
read exclusively from the audio thread, so no synchronisation cost.

Effective values are derived by free helpers in `BlockParams.h`:
`ClockSync::computeRate(bpm, divIdx)` for LFO/Drift Hz and
`ClockSync::computeDelayMs(bpm, divIdx)` for delay milliseconds.
Division → factor mapping lives in `ClockDivision::kFactor[]` — the
canonical table; never duplicated.

When a `*_clock_mode` is `Sync`, the synced value is written back into
`BlockParams` (LFO) or fed directly to the DSP setter (Drift, Delay),
so downstream env→LFO-rate modulation scales relative to the synced
rate naturally instead of getting clobbered.

The slow end of the division table (`16/1`, `8/1`, `4/1`, `2/1`)
extends the slider to drift cycles spanning many bars — suited to
slow harmonic motion and long auto-regen swings. All references go
through the named enum (`ClockDivision::D1_4`) or `kCount`, so GUI
and computeRate auto-track the +4 index shift.

### LFO Free/Trigger regression

The per-voice `perVoiceLfo1/2/3` instances on `SynthVoice` were prepared
and reset on note-on by `VoiceManager`, but their output was never read.
All voices read the global free-running LFO buffer regardless of mode,
so Trigger had no audible effect.

Fix: `SynthVoice::renderBlock` takes a small per-voice LFO buffer
(allocated once in `prepare`); when `bp.lfoNTrigMode` is true it syncs
rate/waveform from the global LFO, fills the per-voice buffer, and
rebinds the function-local `lfoNBuf` pointer so all downstream readers
(filter, scan, pitch) see the retriggered signal. Free mode is
unchanged. Global FX targets (Dly Time/FB/Mix, Rev Mix) still use the
shared global LFO since the FX bus is a single signal across voices.

### Delay dry/wet crossfade

`DelayLine::processBlock` previously used `dryGain = 1 - mix * 0.3f` —
a hybrid that attenuated the dry path by 30 % at full wet, mislabelled
in comments as "parallel send-bus". At high mix the delay felt like it
was eating the signal; in external loopbacks each pass through the
dry-attenuation compounded into a downward spiral. Now a true
crossfade — `out = dry × (1 − mix) + wet × mix`, identical to the
reverb's curve — so at `mix = 1` only the delay tail (with its own
feedback) remains. Internal feedback path is unchanged; this only
touches the post-mix output line.

### Preset-load stickiness fixes

Two bugs surfaced during user testing:

1. **Clock state stuck across loads.** APVTS `replaceState` leaves
   missing-from-tree params untouched. Old sessions / presets without
   the new clock fields would inherit whatever clock state was last
   touched in the host process — Drift 1's clock would mysteriously
   stay on Sync after loading a vanilla preset. Fix in
   `setStateInformation`: parse the loaded XML into a `ValueTree`,
   walk children to find which clock PIDs are present, append PARAM
   nodes with default values for the missing ones, then `replaceState`
   atomically. No `setValueNotifyingHost` glitch between a pre-reset
   and the actual restore. `importJsonPreset` gets the same
   hasProperty fallback for each LFO / drift / delay clock pair, and
   `exportJsonPreset` writes the new fields so v1.7+ presets round-trip
   cleanly.

2. **Injection mode stuck across `.t5p` loads.** Pre-injection presets
   (saved before v1.6) didn't carry `injectionMode`; the loader left
   the panel on whatever mode (e.g. Kombi 3) was active before, and
   the resulting audio no longer matched the preset's source recording.
   Old presets now default to `linear / 0.75 / 4 / 16` directly in
   `PresetFormat::LoadResult`, so loading reproduces the original A↔B
   crossfade behaviour the preset was generated with. New presets
   continue to overwrite these with their saved values.

### Files

- `src/dsp/BlockParams.h` — `ClockSync::computeRate`/`computeDelayMs`
  helpers, prepended slow divisions, added `lfoNTrigMode` to
  `BlockParams`.
- `src/PluginProcessor.{h,cpp}` — `hostBpmLastSeen`/`hostPlayingNow`
  atomics, `seqRunningNow()`, `resolveSyncBpm()`, host-transport
  snapshot in `processBlock`, LFO/Drift/Delay sync overrides,
  ValueTree-injection in `setStateInformation`, JSON
  export/import with hasProperty fallback for the clock fields.
- `src/dsp/SynthVoice.{h,cpp}` — three per-voice LFO scratch buffers
  filled when `lfoNTrigMode` is on, function-parameter pointer rebind.
- `src/dsp/DelayLine.{h,cpp}` — true crossfade, comment cleanup.
- `src/presets/PresetFormat.h` — `LoadResult` defaults to linear /
  0.75 / 4 / 16 instead of empty/NaN.
- `Resources/T5ynth_Guide.html`, `ARCHITECTURE.md`, `CHANGELOG.md` —
  user-facing docs for the BPM-sync feature.

### Verification

Sticky-fix path was caught by a verification agent (Bug #2 in the
review): the original `setValueNotifyingHost`-based pre-reset would
have caused a brief audio-thread render of the wrong clock state
between the reset and the `replaceState`. Switched to direct
ValueTree manipulation before the swap — atomic from the audio
thread's perspective.

CI: `v1.7.0-beta.1` tagged after testing; the tag-triggered run
publishes the macOS `.pkg` and Windows installer assets to a GitHub
prerelease.

---

## 2026-04-30 — Prompt-injection modes (v1.6.0-beta.1)

Shipped six prompt-injection modes — *Linear*, *Fine*, *Layer*,
*Kombi 1*, *Kombi 2*, *Kombi 3* — accessible via a six-button row
above the Alpha slider. Linear is the historical crossfade (bit-
identical to v1.5.x); the five new modes operate on diffusion sampler
steps and on individual DiT block cross-attention layers.

### Mechanics

The non-linear modes exploit two manipulation points in the loaded
`stable-audio-open-small` pipeline, both implemented as Python-level
monkey-patches that are applied per-request and torn down at the end
of generation:

1. **A `DiTWrapper.forward` patch** that increments a closure-captured
   `state["calls"]` counter once per sampler step. Late-step modes
   compare against `transition_step = max(1, round(steps · injection_transition_at))`
   to decide whether the early or late conditioning is active.
2. **`forward_pre_hook`s on every `block.cross_attn`** that override
   the runtime cross-attention `context` kwarg with a precomputed,
   per-block tensor. The replacement is pre-projected through
   `dit.to_cond_embed` once at request start, so the hook bypasses
   projection and just swaps the post-projection context.

Mode dispatch:

- **Linear / Delta** — no hooks, no patches. Manipulate the wrapper-
  level conditioning once.
- **Fine (`late_step`)** — wrapper patch; on late steps replaces
  `kwargs["cross_attn_cond"]` with a Fine-controlled blend of A and
  B. No per-block hooks.
- **Layer (`layer_split`)** — per-block hooks; sigmoid top-hat over
  `[split_start, split_end]` with smoothness=2 (4-layer ramps at
  edges). No wrapper patch.
- **Kombi 1/2/3** — wrapper patch *and* per-block hooks together.
  The hook reads the step counter and switches between an "early"
  per-block tensor (uniform `proj_a`) and a "late" per-block tensor
  (hard-mask blend of `proj_a` and the projected `late_blend`).

### Empirical findings

- **Hard mask vs sigmoid top-hat.** `layer_split`'s sigmoid edges
  cause a 4-block-wide band to peak at w≈0.534 in the centre and
  drop to w≈0.441 at the edges. Even at slider=1 (`late_blend = pure
  B`), per-block B contribution is bounded around 50 %. For Kombi
  modes, where the band is fixed by the preset and the slider only
  controls intensity, replaced with a hard mask (w=1 inside, 0
  outside) so slider=1 is genuinely 100 % `late_blend` in the
  band's blocks.
- **High DiT blocks (12..15) have low conditioning leverage.** The
  original plan put Kombi 2 at blocks 12..16 to mirror Kombi 1's
  surface band (0..4) at the top of the stack. In listening, that
  band was nearly inaudible — the top 4 SA Open Small blocks are
  dominated by refinement work, not prompt-following. Moved Kombi 2
  to **[4, 12] (broad mid)**, where the diffusion does most of its
  conditioning work; the change turned Kombi 2 from "sounds like A"
  into the most distinct of the three Kombis.
- **Kombi 1 (surface, blocks 0..3) is structurally subtle by
  design.** Four narrow surface blocks, all twelve other blocks
  seeing pure A through every step, produces an A-dominant result.
  This is the "B as surface skin" intent. If too subtle in practice,
  candidate widenings are [0, 6] or [2, 6].
- **Kombi 3 = narrow center [6, 10]** was added as an empirical
  probe: same band width as Kombi 1, different vertical position,
  inside the high-leverage mid region. Hypothesis is that K3 may
  turn out the most aggressive of the three despite the narrowest
  band.

### UX changes

- **Per-mode slider memory.** Fine and the three Kombi modes each
  remember their own intensity-slider value, so A/B-ing modes by
  clicking buttons does not destroy the last-used position of any
  individual mode. Linear (APVTS) and Layer (own state fields)
  already had independent state.
- **Mode buttons trigger regeneration** to match the existing
  slider/drift auto-regen UX.
- **Slider scale**: the Fine/Kombi intensity slider now displays
  0–1 (was 0.5–1.0). The internal mapping onto the audible region
  of `injection_transition_at` / `late_phase_alpha` is unchanged —
  it just no longer shifts the displayed numbers. Old presets
  reload their stored value into the corresponding mode slot, but
  the rendered output may differ since the slider value is no
  longer remapped before being sent to the backend.

### Files

- Backend: `backend/pipe_inference.py` (mode validation, kombi
  payload, kombi install/teardown block).
- IPC: `src/inference/PipeInference.h` (`Request` fields), serialised
  via existing path in `src/inference/PipeInference.cpp:774-778`.
- UI: `src/gui/PromptPanel.h/.cpp` — six radio buttons,
  `applyModeToSlider`, mode-aware ghost / drift mapping,
  per-mode lateMix slots via `lateMixForMode` helper.
- Plan & background: `docs/INJECTION_RECTANGLE_PLAN.md`,
  `ARCHITECTURE.md` §6.5, in-app manual §1 + §17.

## 2026-04-25 — Aftertouch Deferred

Aftertouch is explicitly **not a near-term priority**. The feature remains a
possible later addition, but only after higher-value work is done.

### Why it is deferred

- There is currently **no AT path** in the synth. `PluginProcessor` only handles
  `NoteOn` / `NoteOff` in the sample-accurate MIDI split loop.
- The current render path splits the audio block at MIDI event boundaries. A
  dense AT stream would therefore increase sub-block fragmentation and
  `renderBlock()` churn.
- `VoiceManager` and `SynthVoice` currently have no dedicated per-voice pressure
  state, so **Poly AT** would not be a small UI addition; it would require new
  voice-state plumbing and careful real-time handling.

### If revisited later

Keep v1 deliberately small:
- **Channel AT only**
- one target plus amount
- targets limited to obvious real-time destinations such as `DCA`, `Filter`, or
  `Pitch`

Do not start with Poly AT, multi-target routing, or a mini mod-matrix.

## 2026-04-24 — Sampler Normalize Rework (signal-aware, linear)

Replaced the sampler's `RMS -> soft-knee tanh` normalize path with a **signal-aware, fully linear normalization stage** in `SamplePlayer`. The old approach could sound fine on some material but broke down on low-RMS choir-style samples because it tried to force a fixed RMS target and then hid the overshoot inside a nonlinearity. The new path explicitly avoids that failure mode.

### New normalize model

`SamplePlayer::normalizeBuffer()` now runs a lightweight analysis over the actual audible play region and chooses one of four modes:
- **Bypass** — near-silence / floor-guard
- **PeakCap** — already-hot material; only trim to ceiling if needed
- **Transient** — short, sparse, or high-crest material; normalize against `p99.9`
- **Sustained** — dense/tonal material; normalize against active RMS

The gain is then applied as **one linear stereo-linked multiplier**, region-limited to the measured playback span. No soft-knee, no waveshaping, no hard clip, and no scaling of unrelated buffer sections.

### Heuristics

Mode selection uses:
- duration
- 50 ms active-block ratio
- crest factor
- peak-to-`p99.9` gap
- peak headroom / near-silence guards

Targets:
- ceiling: `-1 dBFS`
- sustained target: `-18 dBFS` active RMS
- transient target: `-10 dBFS` at `p99.9`

This is intentionally conservative: difficult samples now tend to get either a sane linear lift or a cap/no-op, rather than being "normalized" into audible deformation.

### Validation

Added `tools/batch_normalize_samples.py` to mirror the C++ heuristic offline, batch-render normalized WAVs, and write CSV/Markdown reports for listening checks.

Tested against a deliberately broad corpus:
- choir pads / soft strings / fortissimo strings
- speech / interviews / agitated crowd speech
- clicks / static / crashes / piano / samba / ambience
- near-silence
- synthetic fixtures for loop-pad, bright rhythm, sub-heavy bass, and wide stereo texture

Observed behavior matched intent:
- quiet sustained material gets lifted cleanly
- sparse/transient material avoids LUFS-style over-push
- already-hot/noisy files get capped or left alone
- silence stays silence

### Build / workflow

- `cmake --build build_clean --config Release --target T5ynth_Standalone -j4`
- offline listening batches written to `/tmp/t5ynth_normalize_batch*`

## 2026-04-24 — Polyphonic Generative Sequencer (feature/polyphonic-gen-seq)

Turned the previously mono `T5ynthGenerativeSequencer` into a four-strand polyrhythmic engine. Five atomic commits on a feature branch off main; every phase builds clean as an isolated step.

### Aesthetic framing
User explicitly rejected functional harmony defaults ("keine 1625-Klischees"). No chord progressions, no I-IV-V Markov — the polyphony is coupled through a **shared Pitch-Field** (pc-set + center + optional row) that evolves under one of four modes:
- **Static** — fixed pc-set
- **Drift** (default) — one pc swaps per tick
- **Transform** — twelve-tone row ops (Tn / In / R / RI)
- **Pivot** — pc-set transposition by pivotInterval (m3 default)

### Architecture (three layers)

1. **Strand struct** holds per-pattern state (Euclidean params, playback clocks, drift counters, Turing degree-walk, fix flags). `strands[MAX_STRANDS=4]` at class level.
2. **PitchField struct** holds shared pc-set + row + evolution-mode state. Advances on strand 0's cycle boundary.
3. **pickNote()** projects a strand's raw Turing-walked scale degree into MIDI via role + metric weighting:
   - **Density role** → `chromaticFieldWalk` (±1 semitone from last note, ignores metric weighting)
   - **Others** → strong beats snap to centerPc with probability = `chordToneDominance`; otherwise `voiceLedFieldMember` picks the field's nearest pc to the raw. Weak beats that fall inside the field keep their raw pc (preserves Turing dynamics).

### processBlock scheduler
Replaced the single-strand `while (samplePos < numSamples)` loop with a multi-strand event scheduler: each iteration finds the earliest upcoming step-boundary-or-gate-off across all enabled strands and processes one event. Each strand runs its own clock at `stepDur / divisionMultiplier`, producing real polymeter (e.g. Anchor ½× alongside Density 2×). Gate-off and note-emission track per-strand `lastPlayedNote` separately; VoiceManager handles concurrent voices naturally.

### APVTS surface
~50 new params across `gen_field_*` (mode, rate, centerPc, pivot-interval), strand-0's new role/octave/div-mult/dominance (`gen_role` etc.), and strands 2–4's full 13-param set (`gen2_*`, `gen3_*`, `gen4_*`). Existing `gen_steps`/`gen_pulses`/… IDs retained — strand 0 still uses them, so old presets load with strand 0's drift/writeback behavior bit-identical for the first ~8 cycles; then the shared pc-set begins to drift (intended).

### UI
Minimal compact addition to `SequencerPanel` visible in GEN mode:
- Row 4: Field-Mode dropdown + Field-Rate slider
- Row 5: three `[ON][Role▾]` clusters for strands 2/3/4

All other per-strand parameters (steps, pulses, rotation, mutation, octave, div-mult, dominance, fix-flags) and the remaining field params (centerPc, pivot-interval) stay at the APVTS level so the panel stays tight. Full per-strand sliders + 4-lane viz were scoped out as a v2 follow-up.

### Commits on `feature/polyphonic-gen-seq`
1. `refactor(sequencer): extract per-strand state into Strand struct` — mechanical
2. `feat(sequencer): pitch field + static/drift/transform/pivot modes`
3. `feat(sequencer): metric weighting + strand roles + sheets-of-sound`
4. `feat(processor): wire polyphonic generative sequencer params`
5. `ui(sequencer): polyphonic strand controls + field block`

### Audio-thread safety
All new code uses stack-local `std::uniform_*_distribution`s, fixed-size `std::array`, atomic stores only with `memory_order_relaxed`. No allocations, no locks, no I/O in `processBlock`. Grep-verified against `new`/`malloc`/`std::cout`/`printf`/`std::mutex`/`std::lock` in the diff.

### Open follow-ups (plan-documented)
- Full 12-slot row editor for Transform mode (v1 only has the auto-seeded ascending row)
- Chromatic-density saturation parameter per Density strand (chromatic bridging density)
- Live MIDI-input → Field-adaptation (interactive listening mode)
- Per-strand pitch-field override (one strand contrasts against the shared field)
- **Inline slider values**: evaluate an `SliderRow` variant (or new component) that renders the numeric value *inside* the bar instead of as a tiny label at the right edge. Tested briefly here with `juce::Slider::LinearBar + TextBoxLeft` on the Field Rate slider — ergonomically much better than the current right-aligned readout, especially at narrow widths, but inconsistent with the rest of the panel. Applies to every slider in the app, not just this one row. Worth a dedicated pass after the v1 feature set stabilises.

## 2026-04-23 — Session 16: Ladder Drive × Resonance ROAR

Resolves the "drive kills resonance" open item from the earlier entry today. The previous tree had a Version C hot-ceiling hack (`hot = kHotCeil · tanh(raw/kHotCeil)`) that was strictly worse than Version B — it killed both the drive harmonics *and* the resonance peak. Reverted it; the fix lives at the per-stage saturation instead.

**Algorithm A — Huovilainen / Surge thermal-voltage normalised stages.** Replace plain `tanh(y)` at each ladder stage with `satStage(x) = 2·Vt · tanh(x / (2·Vt))`. Slope at zero stays 1 (self-oscillation threshold unchanged at `k = 4`), but the per-stage ceiling widens to ±2·Vt. Because `y4` is bounded by that ceiling, so is the feedback tap `k · y4`. With plain tanh (≡ Vt = 0.5) the feedback amplitude was capped at ±4.2 — not enough to swing a first stage pinned at saturation by a 36 dB hot signal through its linear region, so the resonance ring collapsed. Raising to `kVt = 1.22` (canonical Surge value for VintageLadders) gives ±10.2 of feedback swing — the exact headroom that lets the loop oscillate the drive-pinned first stage across zero each cycle. That's the mechanism behind the Minimoog-style ROAR at high drive + high resonance.

Applied to all five per-sample `tanh` calls in `MoogLadderFilter::processSample` (the pre-ladder `tanh(fbIn)` and the four per-stage `tanh(y_i)`). Mirrored into `CutoffWarpFilter`'s Tanh style (`sat(·, 0) → satStage(·)`); other styles (SoftClip / OJD / Sin / Digital / Asym) kept their original curves on this first pass — revisit per-style if the acceptance matrix falls flat for one of them.

**Why not Algorithm B (drive-inside-the-loop) or cytomic/RK4.** Algorithm A is the canonical Huovilainen form used in production plugins (Surge XT, et al.), ~6 lines of change, no coefficient re-derivation, no stability analysis required. B would change the drive topology away from a real Moog (drive is externally applied there, not intra-loop); C and D (Cytomic SVF-physical, RK4+TV) are 4–10× the implementation cost and not obviously needed if A works.

**Level impact.** At low drive the signal doesn't hit `satStage`'s ceiling, so Vt = 1.22 output is indistinguishable from Vt = 0.5 — no regression on the `d0f78364` level parity with the SVF. At 24–36 dB the tap *is* louder by up to ~2.4×, which is the desired "drive makes it louder + crunchier" behaviour, not a bug. The `kTapComp = 1.20` is unchanged; re-tune only if the listen test reveals an audible mid-drive jump.

**Bundled along with the fix** (each its own commit, independently revertable):
- `1178002b` — startup visuals of the filter card's radio rows (TYPE / SLOPE / ALG / OS) now sync directly from APVTS instead of relying on the ComboBoxAttachment's initial `onChange` firing, which some JUCE versions skip with `dontSendNotification`. Fixes the "no active button until you click" first-paint glitch.
- `a021f083` — per-style resonance scaling for `CutoffWarp` (0.65 Sin, 1.35 SoftClip, 1.10 OJD, 1.00 Tanh/Digital/Asym). Each saturation curve has a different DC slope, so a single nominal `k` made Sin ring at r ≈ 0.25 while SoftClip stayed silent at r = 1. Tuned by ear.
- `acba990c` — full problem-statement / acceptance-test handover doc (`docs/handover_session16_filter_drive.md`) so the reasoning is recoverable.
- `5d833f55` — Algorithm A itself.

`kVt` is duplicated verbatim in `MoogLadderFilter.h` and `CutoffWarpFilter.h` (both `1.22f`) with cross-reference notes in both. Deliberately kept each filter self-contained rather than introducing a shared constants header for a single float — if a later change needs more shared state, that's the point to extract.

**Open.** Listen test in progress against the acceptance matrix in handover §8. Expected tuning range for `kVt` if adjustment is needed: 1.0 – 1.5. The preset save/load audit that was listed as open in the earlier entry today was already completed in session 15 (handover §9).

## 2026-04-23 — Nonlinear Filter Algorithms (Huovilainen + Cutoff Warp)

Added two nonlinear filter algorithms alongside the existing linear TPT SVF, selectable via a new Algorithm switchbox in the filter header:

- **Huovilainen-style Moog ladder** (`src/dsp/MoogLadderFilter.h`). 4-pole with `tanh` at each stage input and a half-sample-delay-compensated feedback path (`xPrev` + 50/50 average). The compensation is what makes the classical form actually resonate — without it the loop sees an extra sample of delay and clips instead of ringing. Slope switch taps y1..y4 (6/12/18/24 dB) for the standard Moog multi-mode.
- **Surge-XT-inspired Cutoff Warp** (`src/dsp/CutoffWarpFilter.h`). ZDF 4-pole ladder with a style-switchable per-stage saturation: Tanh / SoftClip / OJD (x/√(1+x²), centered at zero — the initial atan-with-bias version had DC drift that glitched the ZDF loop under modulation) / Sin-fold / Digital clip / Asym (bias-compensated asymmetric tanh).

Credit: Huovilainen 2004 DAFx paper for the Moog algorithm; Surge XT project (GPLv3) for the Cutoff Warp concept. Implementations written from scratch; no reference code copied. Entries added to `THIRD_PARTY_LICENSES.txt`, `resources/T5ynth_Guide.html`, and `ARCHITECTURE.md`.

**Drive topology difference.** The SVF uses the existing pre-filter `tanh` (+ optional oversampling) as its saturation character — it's LTI on its own, the drive stage *is* the character. Ladder and Warp have their own nonlinear stages, so the pre-filter `tanh` would flat-clip and leave nothing for the filter to shape. Instead the drive amount is forwarded via `setInputDrive()` as input gain only; Phase B is a no-op for them and the filter's own `tanh`/`sat(·)` stages do the work. No output compensation (`1/inDrive`) — drive makes these filters louder + crunchier like an analog Moog, not level-matched.

**Level parity.** The half-sample input averaging is a `cos(ω/2)` FIR lowpass (−3 dB at sr/4) — required for correct resonance but it pulls Ladder/Warp ~1.5 dB under the SVF on broadband material. Compensated with a fixed 1.20× tap gain.

**Feedback-tap evolution.** First ship had `k·tanh(y4)` (Ladder) and `k·sat(y4, style)` (Warp). Both saturations cap the feedback at ±1, so the loop gain ceiling of 4.2 never translated to an actual resonance peak — the reso slider "controlled something" but didn't ring. Fixed by making the feedback tap linear on `y4` and moving the per-stage saturation to each stage's input (not the feedback tap). Stability still comes from the per-stage sat bounding the internal state.

**Unresolved** (see `docs/handover_session15.md`): Ladder/Warp resonance is still too drive-sensitive — at ~50 % drive on Ladder (and >21 dB on Warp Sin) the resonance collapses entirely because the operating point sits deep in the saturation zone where the local derivative is near zero, killing effective loop gain. Also: the filter-type / algorithm radio buttons occasionally don't reflect their APVTS value on first paint (explicit `onChange()` poke after attachment did not fully fix it); and the preset save/load coverage of all filter-related params still needs an audit.

UI: Algorithm switchbox + Warp Style combo live in the filter header row next to TYPE/SLOPE (Drive slider shortened accordingly). Drive-OS default is 4×; old .t5p files without the new fields load on Algorithm=SVF, Warp Style=Tanh, Drive OS=Off so existing sessions stay bit-identical to pre-Ladder/Warp builds.

## 2026-04-18 — Sampler Wave Cursor Reverted

Tested a live playback cursor in the sampler waveform view and reverted it. On loop-heavy material the 30 Hz smoothed marker felt visually late relative to the audio and was more distracting than helpful. If this is revisited, it should probably use a different visualization approach rather than a lagged dot/cursor over the waveform.

## 2026-04-11 — Installers + Path Architecture

### macOS .pkg Installer
Built with `pkgbuild`/`productbuild`. Four component packages: Standalone (required), VST3, AU, Support Data. The installer creates `/Library/Application Support/T5ynth/presets/` (factory presets, read-only) and `/Library/Application Support/T5ynth/models/` (scan-only). Postinstall removes quarantine flags (`xattr -cr`).

### Windows Inno Setup Installer
`installer/windows/t5ynth.iss`. Standalone + VST3 components, Start Menu entry, uninstaller. Factory presets in `C:\ProgramData\T5ynth\presets\`, models dir in `C:\ProgramData\T5ynth\models\` with `users-modify` ACL.

### Factory / User Preset Split
- Factory presets: `/Library/Application Support/T5ynth/presets/` (macOS), `C:\ProgramData\T5ynth\presets\` (Win) — installed by the installer, read-only for users.
- User presets: `~/Library/Application Support/T5ynth/presets/` (macOS), `%APPDATA%\T5ynth\presets\` (Win) — writable, created on demand.
- `PresetFormat::getFactoryPresetsDirectory()` / `getUserPresetsDirectory()` / `getAllPresetFiles()` added.

### Design Decision: Models Are Per-User
Model download target is always the per-user directory (`~/Library/Application Support/T5ynth/models/` on macOS, `%APPDATA%\T5ynth\models\` on Windows). Rationale: model licenses (Stability AI Community License, CC-BY-NC-SA 4.0) are accepted individually per user. Different users on the same machine may have different license status (e.g. commercial vs. non-commercial accounts). The system-wide path (`/Library/Application Support/` / `C:\ProgramData\`) is scanned as a read-only candidate — an admin can pre-deploy models there, but the app never writes to it.

### CI Integration
Both installers are built in CI (`build.yml`), uploaded as artifacts on every push, and included in GitHub Releases on tags. macOS: `build_pkg.sh` in the `macos` job. Windows: `choco install innosetup` + `iscc` in the `windows` job.

## 2026-03-31 — Session 8: Signal Chain Fixes + RT Safety

### Critical Bug Fixes

- **Filter never called (CRITICAL):** `SynthVoice::processFilter()` existed with full modulation logic but was never invoked anywhere. Per-voice buffers don't exist (voices are summed sample-by-sample), so a per-voice filter was architecturally impossible without major refactor. **Fix:** Added post-sum `T5ynthFilter postFilter` to `PluginProcessor`, wired between voice rendering and effects chain. Full modulation ported from the dead per-voice code: keyboard tracking (uses newest voice's note), mod envelope → cutoff (subtractive sweep with `1 ± amount × 8` peak/floor), LFO → cutoff (bipolar multiplicative). `hasActiveVoices` guard prevents stale mod values from shifting cutoff when silent.

- **Pitch accumulation (CRITICAL):** `renderSample()` did `osc.setFrequency(osc.getFrequency() * pitchFactor)` — multiplied the *current* frequency by the pitch factor each sample, causing exponential drift to 20kHz in ~100 samples. **Fix:** Added `float baseFrequency` to `SynthVoice`, cached at `noteOn()` and `glideToNote()`. Pitch modulation now uses `osc.setFrequency(baseFrequency * (1 + pitchMod))` — always absolute, never accumulating. Frequency resets cleanly when modulation stops.

### Real-Time Safety

- **Debug FILE* logging removed:** `static FILE* dbgFile` with `fopen`/`fprintf`/`fflush` on audio thread — RT violation + file handle leak. Deleted.
- **LFO buffer heap allocation removed:** `std::vector<float>` was allocated per `processBlock` call. Moved to pre-allocated member vectors resized in `prepareToPlay()`.

### Cleanup

- Renamed `Looper` → `Sampler` throughout (EngineMode enum, GUI buttons, accessor methods, preset import/export). Aligns with actual functionality.
- Added porting comparison CSVs (`docs/portierung_*.csv`) for all composables.

### Correct Signal Flow (after fix)
```
Voices (Osc/Sampler → VCA → sum with 1/sqrt(N))
  → Post-Sum Filter (kbd tracking + env→cutoff + LFO→cutoff)
  → Effects (Delay ‖ Reverb parallel send-bus)
  → Master Volume → Limiter
```

### Known Remaining RT Violations (deferred)
- `T5ynthFilter::processBlock()` allocates `dryBuffer` when mix ∈ (0,1)
- `reverbSrc` buffer allocated when both delay+reverb are enabled simultaneously
- Per-voice `processFilter()` and per-voice `T5ynthFilter filter` are now dead code

---

## 2026-03-29 — Session 6: DSP Bugfixes + LibTorch Migration Start

### DSP Bugfixes (3 remaining from Session 5 audit)
- **modPitch dead code:** Pitch modulation was accumulated in processBlock but never applied. Moved to per-sample in `SynthVoice::renderSample()` where env/LFO values are available. Applies `freq *= (1 + pitchMod)` to wavetable oscillator.
- **barStartFlag unconsumed:** StepSequencer sets flag at bar boundaries but processBlock never read it. Now consumed via `exchange(false)`.
- **DCF idle cutoff drop:** Verified already fixed — `!isIdle()` guard correctly prevents filter modulation when envelope is idle.

### Architecture Decision: LibTorch Migration
- HTTP-based Python backend (Flask + diffusers) is architecturally unacceptable for a standalone audio plugin
- Decision: migrate to LibTorch C++ inference — no Python, no HTTP, no server process
- VRAM properly freed on plugin destructor

### TorchScript Export Progress
- Created `tools/export_to_torchscript.py` — exports Stable Audio components individually
- **T5 Encoder:** Successfully exported (0.00 max diff, 438.9 MB)
- **Projection Model:** Successfully exported (0.00 max diff, 1.6 MB)
- **DiT (Diffusion Transformer):** BLOCKED — diffusers attention processor uses `repeat_interleave` with `output_size` that causes CPU/CUDA device mismatch during tracing
- **VAE Decoder:** Not yet reached (blocked by DiT)

### DiT Export — Next Steps
The fix requires replacing `StableAudioAttnProcessor2_0` with a custom processor that avoids the `output_size` argument in `repeat_interleave`, or patching diffusers locally. Input shapes confirmed: `hidden_states=[1,64,1024]`, `global_hidden_states=[1,1536]`, `encoder_attention_mask` must be bool not long.

---

## 2026-03-28 — Session 4: Reference Audit + Critical Repairs

### Full Gap Analysis Against Reference (useAudioLooper.ts, useModulation.ts, useEffects.ts, useFilter.ts, useDriftLfo.ts, crossmodal_lab.vue)
- Session 3 left 75% of features unimplemented or broken
- Delay/Reverb/Limiter parameters were defined in APVTS + GUI but **never passed to DSP classes**
- Reverb IR was never loaded — ConvolutionReverb was permanently dead
- Filter modulation double-ticked envelopes/LFOs (ran at 2× speed)
- AudioLooper could only do endless forward loop — missing one-shot, ping-pong, crossfade, normalize, loop brackets, retrigger
- ADSREnvelope used exponential decay (wrong) instead of reference's linear decay, and simple exponential release instead of RC-discharge

### Parameter Wiring (Phase 1)
- `delay.setTime/setFeedback/setMix()` now called in processBlock
- Reverb IR loaded from BinaryData in `prepareToPlay()`, mix wired, IR switching on `reverb_ir` parameter change
- `limiter.setThreshold/setRelease()` wired
- Looper: `loop_mode`, `crossfade_ms`, `normalize` wired

### Correctness Fixes (Phase 2)
- **Double-tick bug**: Filter modulation now uses captured last values from per-sample loop instead of re-calling `processSample()`
- **DCF sweep factor**: 4 → 8 to match reference (`base × (1 + amount × 8)`)
- **Alpha conversion**: UI range (-2..+2) → backend (0..1) via `alpha/2 + 0.5`
- **Fake S&H removed**: LFO waveform list reduced from 5 to 4 (S&H was not in reference, returned silence)
- **Drift LFO 3**: rate/depth/target now wired, `drift3_target` and `drift3_wave` parameters added

### AudioLooper Rewrite (from useAudioLooper.ts)
- LoopMode enum: OneShot, Loop, PingPong
- Loop start/end brackets (fractional 0–1 of buffer)
- Equal-power crossfade baked into buffer (`sin/cos` curves, max half loop length)
- Cross-correlation loop-point optimization (512-sample window, 2000-sample search)
- Palindrome buffer for ping-pong (endpoints not doubled)
- Peak normalization to 0.95
- Retrigger (hard restart from cold-start offset)
- processBlock respects play region, stops on one-shot end

### ADSREnvelope Rewrite (from useModulation.ts)
- Linear attack with soft retrigger (ramps from current level, not zero)
- Linear decay (was exponential RC — wrong curve shape)
- RC-discharge release: `e^(-t/τ)`, `τ = releaseMs/5`, hard-zero at end
- 3ms minimum ramp for all stages
- Loop re-triggers attack from sustain

### Remaining Work (identified in gap analysis, not yet done)
- Filter slope 12/24dB (parameter exists, not implemented)
- Parallel effects chain (currently serial: signal → delay → reverb)
- 13 modulation targets (currently only 4 per source)
- Delay damping filter in feedback loop
- Drift LFO waveform selection (currently sine-only)
- Delay BPM sync
- Sequencer: gate length, glide, note divisions, presets
- Arpeggiator: musical rate divisions instead of float
- UI: on-screen keyboard, preset panel, dimension explorer canvas, loop region display, recording

---

## 2026-03-28 — Session 3: Audio Pipeline Debugging + Sequencer Fix

### Critical Bug Fixed: No Sound After Generation
- **Root cause**: Looper audio was multiplied by amp envelope, which defaults to 0 without MIDI note-on. Generated audio x 0 = silence.
- **Fix**: Auto-trigger envelopes when `loadGeneratedAudio()` is called.
- **Secondary issue**: SVT filter's `setType()` was called every processBlock, found to not reset state (JUCE source confirms it only sets a variable). Actual filter silence was caused by DryWetMixer behavior — bypassed for fallback sine.
- **Base64 decode**: Original code used `MemoryBlock::fromBase64Encoding()` which is JUCE-proprietary format, NOT RFC 4648. Python's `base64.b64encode()` produces standard base64. Switched to `juce::Base64::convertFromBase64()` which is RFC 4648 compliant.
- **HTTP timeout**: Backend lazy-loads Stable Audio model on first request (10-15s). Client timeout was 5s. Increased to 120s.

### Sequencer MIDI Pipeline
- Implemented `StepSequencer::processBlock()` — sample-accurate 16th-note stepping with MIDI note generation.
- Implemented `Arpeggiator::processBlock()` — 5 modes (Up/Down/UpDown/Random/Order), multi-octave cycling.
- **Critical bug**: `start()` was called every `processBlock`, resetting `samplesUntilNextStep` to 0 each block — sequencer ran at block rate (~86 Hz) instead of BPM. Fixed: only reset on actual start transition.
- Seq and Arp now run **in series** (Seq generates notes, Arp arpeggates them + external MIDI), not mutually exclusive.

### Waveform Display
- `WaveformDisplay::setWaveform()` was never called. Wired via 30Hz timer in SynthPanel polling a snapshot buffer in PluginProcessor. Peak-preserving downsample to 1024 display points.

### GUI Fixes
- Fixed aspect ratio (3:2) — window scales proportionally.
- All ENV/LFO/Filter/Drift sections always visible (dimmed at 30% alpha when inactive) — no collapsing, predictable layout height.
- Font sizes derived from panel's own available height / total content units — everything fits at any size.
- Minimum window: 1050x700.
- Settings page as overlay (auto-scans model at known paths, shows backend status).
- Settings button injected into JUCE standalone header next to Options.
- Master volume as rotary knob.
- MIDI monitor in sequencer panel.
- Seed field as numeric text input with Random toggle.
- Fallback sine oscillator for MIDI testing without generated audio.

### Backend Connection
- `BackendManager` status ("Running") and `BackendConnection::isConnected()` were independent — synced by calling `checkHealth()` when manager reports Running.

---

## 2026-03-26/27 — Sessions 1-2: Foundation + GUI Design Pass

### Project Scaffold
- JUCE standalone + VST3 plugin with full DSP chain (wavetable oscillator, audio looper, 3 ADSR envelopes, 2 LFOs, drift LFOs, SVT filter, delay, convolution reverb, limiter).
- Python backend (Flask) wrapping Stable Audio Open 1.0 via diffusers `StableAudioPipeline`.
- `BackendManager` launches Python server as child process, polls `/health`.
- `BackendConnection` HTTP client on dedicated thread, delivers results via `MessageManager::callAsync`.

### GUI
- 2-column layout: Col1 (25%) Generation (prompts, embedding controls, axes) | Col2 (75%) Engine/Filter/Modulation/Drift.
- Footer: Sequencer + FX (Delay/Reverb) + Master Volume.
- Section colors: Filter=cyan, Modulation=orange, Drift=purple.
- Linear sliders with value display and units (no rotary knobs for parameters).
- Color-coded semantic axes (pink/blue/green) + PCA axes (6 colors).

### DSP
- Full modulation routing in processBlock: envelopes/LFOs → DCA/Filter/Scan targets.
- Filter dry/wet mix via DryWetMixer.
- Envelope looping (re-enters Attack from Sustain).
- Drift LFO re-generation (randomize rate/depth on phase wrap).

### Model Loading
- Local model path support: `~/t5ynth/models/stable-audio-open-1.0/` (symlink to HuggingFace cache or ComfyUI directory).
- Fallback: HuggingFace auto-download.
- Requires HuggingFace pipeline format (`model_index.json`) — single `.safetensors` not usable because embedding manipulation needs separate access to text encoder + projection model.

---

## Architecture Notes

### Signal Flow
```
MIDI In → Sequencer → Arpeggiator → Note Processing
                                         ↓
              Wavetable Oscillator / Audio Sampler
                    ↓ (per-voice VCA, sum with 1/sqrt(N))
              Post-Sum Filter (SVT, modulated cutoff)
                                         ↓
                    Delay ‖ Reverb (parallel send-bus) → Master Vol → Limiter → Out
```

### Audio Generation Flow
```
User clicks Generate → PromptPanel builds GenerationRequest
  → BackendConnection POST /api/cross_aesthetic/synth (JSON)
  → Python backend: T5 encode → embedding manipulation → StableAudioPipeline
  → Response: base64-encoded WAV
  → JUCE: Base64 decode → WAV parse → AudioBuffer
  → loadGeneratedAudio(): Looper + Wavetable extract + Waveform snapshot
  → Auto-trigger envelopes → Sound plays immediately
```
