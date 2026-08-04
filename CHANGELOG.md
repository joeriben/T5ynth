# Changelog

The project was released as **T5ynth** through v2.5.3. From 3.0.0 it is
**akróasys**; the repository, the preset format and the version line continue
unbroken.

## Unreleased — 3.0.0

Two things make this a major version: the instrument is renamed, and it gains a
second oscillator built on a different principle from the first.

### New: the Language-Resonant Oscillator (LRO)

A second oscillator, peer to the existing one, in which **the sounding program
itself is generated at run time by a language model from a description in
ordinary language, then compiled and executed as the instrument's voice
source.** First landed 2026-07-22 (`3728a42f`).

The architecture, stated plainly because it is the thing that is new:

1. The player types a description of an instrument ("a bowed cello", "bright
   shimmer degrading to a dark rumble"). No code, no parameters, no keyword
   vocabulary.
2. A **curated, parametrised library of synthesis code** is held beside the
   model — currently 30 instrument bodies, 51 sound-character words and 17
   motions, each entry carrying real Csound source, the synthesis method it
   implements, a published source for that method, and named parameters with
   ranges and one-line glosses (`backend/lco_library.json`).
3. **Two-turn consultation.** In the first turn the model is shown the library's
   index and *names the entries it wants opened*; nothing is matched to it by
   keyword and nothing restricts it to what it opened. In the second turn it is
   shown those entries in full and writes a complete Csound orchestra
   (`backend/lco_write.py`).
4. A **fixed host scaffold** is the only contract: sixteen voice channels, gate,
   frequency, velocity, pressure, timbre and trigger per voice, `ksmps = 64`,
   and the rule that the body shapes spectrum and timbre while the host's
   amplitude envelope owns loudness. The model writes the instrument; it never
   writes the transport, the polyphony or the envelope
   (`src/dsp/CsoundEngine.cpp`).
5. The orchestra is **compiled and performance-checked before it is allowed to
   play** — it must compile and produce non-silent output for a quarter of a
   second. A failure is fed back to the model as a repair round rather than
   falling back to anything.
6. **The player's control surface is derived from the generated program.** A
   parameter line from an opened library entry that survives into the body the
   model actually wrote becomes a knob on the panel, carrying the library's
   name, gloss and range and the model's chosen value; a line the body never
   reads does not become a knob (`src/dsp/LroControls.h`).
7. The compiled orchestra is **hot-swapped into the running audio engine**
   without an audible break, and travels inside the preset as source text.

**There is no deterministic fallback and none is wanted.** Without a language
model there is no oscillator. The library orients the model rather than
constraining it: it may combine any number of methods in one program, layer up
to three bodies, morph one into another across a note, drive one with another,
or write something the library does not contain at all.

The full technical description, in the form a third party can read and cite, is
[`docs/LRO_TECHNICAL_DISCLOSURE.md`](docs/LRO_TECHNICAL_DISCLOSURE.md).

Milestones, dated: Csound detection and engine wrapper 2026-07-16 (`3d7663a4`);
voice bridge and playable engine mode 2026-07-17 (`cf21475a`); **the language
model writes the orchestra** 2026-07-22 (`3728a42f`); the panel derives its
knobs from the written body 2026-07-30 (`a8b45824`, `0e7f9709`); per-part levels
2026-08-01 (`8c36dc13`).

### New: the authoring trace

The LRO's panel shows the whole authoring path rather than a progress bar —
nine stations covering what the model was given, which library entries it asked
to have opened, its reasoning as it streams, the orchestra it wrote, a repair
round if the first attempt failed, and whether the result compiled and played.
A station that does not appear is itself information. Hold the panel to read the
Csound the model wrote.

### New: Csound ships inside the app

Csound 6.18.1 travels in the bundle on macOS, Windows and Linux, linked
dynamically as LGPL 2.1 requires. Nothing needs installing. Linux takes the
distribution's `libcsound64-6.0` in the Debian package and has no scanned
synthesis (`scanu`/`scanu2`/`scans`).

### New: one language model for the whole instrument

Gemma 4 12B (QAT, 4-bit GGUF, run through llama.cpp) writes the LRO's Csound,
translates prompts to English and drives Re-Prompt. Alternatively an external
provider — OpenRouter, Mistral AI, IONOS, Mammouth AI, Anthropic, OpenAI, a
local Ollama, or any OpenAI-compatible endpoint — with only the text step
leaving the machine. The separately-installed 1.5B translator is gone.

### Also in 3.0.0

- **CLAP plug-in format on macOS.** The `.pkg` now installs a fourth artefact,
  `akroasys.clap`, into `/Library/Audio/Plug-Ins/CLAP` — selectable in the
  installer like the VST3 and the AU. It is the same instrument: one processor,
  one parameter set, one preset format, built from the same shared code by
  [clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions).
  Windows and Linux keep Standalone + VST3. (Not to be confused with CLAP the
  audio-text model that Re-Prompt listens with — same acronym, unrelated thing.)
- **MPE is now declared, not just implemented.** The synth has always parsed a
  full MPE zone from raw MIDI; it never told the host so. CLAP hosts and Logic
  now see it and send per-note channels.
- **Five envelopes** instead of three, with COPY/PASTE between the tabs.
- **Amp effects chain** — distortion as an overdriven amplifier, tremolo with
  four shapes, chorus and phaser, behind the voices.
- **Advanced/Easy view removed.** The toggle that remains switches oscillators.
- **Every engine's shared controls in one top bar**, with the tuning selector
  and the octave switchbox.
- **Re-Prompt listens on both oscillators** — for the LRO it renders a probe of
  the compiled orchestra and hands that to CLAP, rather than re-reading its own
  text.
- **Session log** (`.t5evt`) — opt-in recording and replay of a playing session.
- **The in-app manual rewritten** against the code, with the two oscillators as
  peers and background chapters on Csound's MUSIC-N lineage and on the code
  library.

---

## v2.5.3-beta.1 — 2026-06-30

- **Fix: CUDA on small-VRAM cards.** The prompt translator is pinned to CPU where VRAM is tight, and `T5YNTH_CUDA_FP32` forces fp32 when a card's fp16 path misbehaves.
- **Presets: cross-device presets are flagged** on the detail card, so a preset made on another machine's device announces itself before it is loaded.
- **Delay: `Tp4`** restores the legacy additive tape wobble beside the newer voicings.

## v2.5.3-beta.0 — 2026-06-29

- **Delay: Tape and BBD character presets.** The delay-type buttons become a combo box; each family carries named characters instead of numbered variants.

## v2.5.2-beta.1 / v2.5.2-beta.0 — 2026-06-26 / 2026-06-25

- **New: BBD delay mode**, with tape playback rolloff that darkens with delay time rather than only with age.
- **Delay: per-mode Damp** with an intrinsic tape baseline and an honest percentage label.
- **New: step recording.** Double-click *Step* to play notes into the grid; Space or a sustain pedal enters a rest.
- **Keyboard: the typing map reaches ~2 octaves** (`o l p ö ä ü # +`).

## v2.5.1-beta.0 — 2026-06-23

- **New: CORE MONITOR.** The generative sequencer's Strand 1 slot holds a read-only phosphor-green readout that names each pattern mutation as it fires — the operation, the result, and the rule that chose it.
- **Generative sequencer: Range as a compact switchbox**, per-strand group cards, roles and tempo multipliers in one row.

## v2.5.0-beta.1 / v2.5.0-beta.0 — 2026-06-22

- **New: the Delta panel.** Semantic Axes and the Dimension Explorer share one box behind a two-segment switch; the DimExplorer inherits the freed band and gains a binned |A−B| focus-spectrum mini-view.
- **New: looping envelopes** as a self-retriggering A→D→Hold→R cycle, with a Loop toggle in each envelope header. In loop mode Sustain becomes the hold time.
- **New: tabbed settings overlay** with the filter-oversampling control.
- **Drift: its own BPM-sync division range**, 1/4 … 64/1 — slower than the LFOs and the delay.
- **Filter: LP/HP/BP as a type toggle.**
- **Re-Prompt and Translate are gated** on the translation model actually being installed.

## v2.4.0-beta.0 — 2026-06-19

- **New: the delay is reworked** into Digital / Ping-Pong / Tape multi-head voicings.
- **New: graphical ADSR editor**, replacing the faders. Clicking a segment body cycles that stage's curve.
- **New: continuous signed per-stage velocity sensitivity.**
- **UI: module cards** for the FX Delay/Reverb sections and the generative sequencer's Euclidean controls.

## v2.3.0-beta.0 — 2026-06-12

- **New: Resynth.** A generation can start from a waveform instead of from pure noise and denoise away from there — an audio-conditioned feedback loop with an anti-convergence controller, an Off→Full slider, drift as a target, and preset/snapshot persistence.
- **Presets: a maintainer checkout writes bank presets directly**, so the live preset directory is the git checkout.

## v2.2.0-beta.1 / v2.2.0-beta.0 — 2026-06-06 / 2026-06-04

- **New: Stable Audio 3.** SA3 Small Music and Small SFX with the t5gemma text encoder, a model-metadata IPC channel, and the layer-split slider clamped to each model's own DiT block count. First landed 2026-05-26.
- **Filter: the Warp algorithm becomes true ZDF**, with per-style resonance recalibration and output makeup that holds level parity across styles.
- **Setup: per-row inline download progress**, cancel, and a `~/Downloads` pre-scan.
- **Fix: preset JSON is decoded as UTF-8**, not Latin-1 — the cause of prompt mojibake after a restart.

## v2.1.0-beta.1 / v2.1.0-beta.0 — 2026-06-03 / 2026-06-02

- **Held voices follow a new generation.** Granular and Freeze voices crossfade-morph live when inference returns, rather than being cut or frozen on the old buffer.
- **New: MIDI Panic** in the status bar.
- **Presets: "+ Bank" is decoupled from Save**, and a name that exists in another bank blocks the save rather than silently shadowing it.
- **UI: the scan playhead** is drawn in its own colour across the full waveform height.

## v2.0.2-beta.0 — 2026-05-23

- **New: curated tag vocabulary** with a click-to-add cloud and autocomplete; long-press a tag row to drag it onto the detail card.
- **Presets: edits to the UCDCAE bank fork to My Presets** with a mandatory rename, so the shipped bank cannot be edited in place.
- **Presets: session snapshots persist in `.t5p`**, and the FLAC v4 write/read path is active.

## v2.0.1-beta.0 — 2026-05-20

- **New: Semantic Axes master Amount**, a single attenuator over all three axis slots.
- **Fix: embedding-noise sigma halved** so the slider has usable resolution across its range.

## v2.0.0-beta.1 / v2.0.0-beta.0 — 2026-05-17

- **New: Easy view.** A second, compact layout for the whole instrument — generation column, oscillator, filter, LFOs and modulation — switched from the engine title. (Removed again in 3.0.0, where a single layout replaces both.)
- **New: true L/R stereo filter path.**
- **New: in-plugin sync of the UCDCAE AI Lab preset bank** from GitHub.

## v1.9.0-beta.1 — 2026-05-16

- **New: Granular engine.** Adds a third engine beside Sampler and Wavetable. Granular reuses loaded/generated sample material but renders through its own texture engine with pitch-independent grain timing, conservative texture macros, stereo spread, and sample-near defaults.
- **UI: Granular integration.** Engine selection now exposes Sampler / Wavetable / Granular, with the Granular controls aligned to the existing one-line engine menu style.
- **Fix: Granular normalisation and modulation targets.** Granular `Norm` now normalises the mono freeze input that the engine actually renders from, and Env/LFO target order is aligned around Filter / Scan / Pitch / Delay / Reverb / Noise.
- **UI: compact startup layout.** LFO, Drift, and Regenerate controls now size their menu/button cells from actual label text so the default window width does not crush Scan/Magnitude/max controls.

## v1.8.0-beta.1 — 2026-05-10

- **New: sequencer one-shot sample slots.** Each Step Sequencer step now has three compact sample slots for drum-style one-shots. Samples are copied physically from the current P1→P3 waveform region, each slot has Normal / Accent / Mute cycling, and the slots are saved inside presets together with their WAV data.
- **New: T5 oscillator snapshots.** The Generation column now has a `SNAP OFF / 1 / 2` switchbox beside the inference cache controls. Long-clicking slot 1 or 2 stores the current main sound and T5-Osc state from the beginning of the long click; short-clicking recalls the snapshot.
- **New: preset-persistent inference cache.** Presets now save cache capacity and already generated cache audio entries by default, then restore them on load so cached alternate generations survive the preset round-trip.
- **UI: compact Snap / Cache row.** Snapshot and cache controls share one switchbox-style row using the same bordered button format as the Engine controls.

## v1.7.0-beta.1 — 2026-05-01

- **New: BPM-sync for LFOs, Drift, and Delay.** Each of the three LFOs, three Drift LFOs, and the Delay-Time control now has a clock-icon toggle that swaps the free-rate slider for a musical division (`16/1` … `1/32`, including triplet `T` and quintuplet `Q` variants). The slow end (`16/1`, `8/1`, `4/1`, `2/1`) gives drift cycles spanning multiple bars — suited to slow harmonic motion and long auto-regen swings. Sync follows the host transport when the DAW is playing; in standalone or with the host transport stopped, the in-app sequencer (step or generative) takes over; if neither is running, the rate freezes at the last-seen host BPM and falls back to the Seq-BPM field as a last resort. The UI swaps the rate/time row for a division stepper when Sync is on.
- **Fix: clock-mode persisted across preset loads.** Loading a pre-v1.7 preset would inherit whatever clock state was last touched in the host process — Drift 1's clock would mysteriously stay on Sync after loading a vanilla preset. The state restore now patches missing clock-param defaults straight into the loaded ValueTree, so the swap is atomic and old presets land at Off / `1/4` as intended. Old `.t5p` JSON imports get the same explicit-default treatment.
- **Fix: injection mode persisted across `.t5p` loads.** Pre-injection presets (saved before v1.6) didn't carry the injection-mode field, so loading one would leave the panel on whatever mode (e.g. Combo 3) was active before — and the resulting audio no longer matched the preset's source. Old presets now default to `linear / 0.75 / 4 / 16` on load, reproducing the original A↔B crossfade behaviour they were generated with.
- **Fix: LFO Free / Trigger mode.** Trigger mode previously had no audible effect — per-voice LFOs were reset on note-on but their output was never read by the voice rendering, so all voices kept tracking the same free-running global LFO regardless of mode. Voices now consume their own retriggered LFO for per-voice modulation (filter, pitch, scan) when Trigger is selected. Free mode is unchanged. Global FX targets (Dly Time/FB/Mix, Rev Mix) keep using the global LFO since the FX bus is shared across voices.
- **Fix: delay dry/wet curve.** The Delay's mix knob was a hybrid that attenuated the dry path by 30 % at full wet (mislabelled as "parallel send-bus"); cranking the mix could feel like the delay was eating the signal, especially in external loopbacks. The mix is now a true crossfade — `out = dry × (1 − mix) + wet × mix`, identical to the Reverb's curve. At mix = 1 the dry path vanishes and only the delay tail (with its own feedback) remains.

## v1.6.0-beta.1 — 2026-04-30

- **New: polyphonic generative sequencer.** The Generative Sequencer grows from a single Euclidean / Turing-Machine voice to up to **five independent strands**, each with its own Div×, octave offset, dominance, role, and panning. Strands draw from a shared **Pitch Field** (12-bit pitch-class set with Static / Drift / Transform / Pivot evolution modes) so harmonic coherence is provided at the field level rather than per-event. Coordination is handled via priority displacement (DensityBudget) caps. New in-app Manual sections cover strand roles, the pitch field, and the principles behind ensemble-aware coordination. State is persisted in `.t5p` JSON and `.t5seq` files; the older single-strand format is restored as Strand 1 with the others muted.
- **New: Injection Modes.** The Generation panel grows a six-button mode row above the Alpha slider. The classical A↔B movement is now one of six modes — *Linear*, *Step-in*, *Layer*, *Combo 1*, *Combo 2*, *Combo 3* — that select different ways Impulse B enters Impulse A inside the diffusion pipeline. Linear preserves bit-for-bit compatibility with prior versions; the other five modes operate on the diffusion sampler steps and on individual DiT block cross-attention layers. See the in-app Manual §1 for the user-facing description and ARCHITECTURE.md §6.5 for the mechanics.
- **Step-in mode** — early sampler steps see Impulse A only; after a transition step the cross-attention conditioning swaps to a Step-in-controlled blend of A and B. Drives a single intensity slider (0–1) coupled to both the transition point and the late-phase blend amount. Implemented as a `DiTWrapper.forward` kwargs swap on the inner sampler.
- **Layer mode** — a two-thumb range slider defines a B-zone over the 16 DiT blocks. Per-block forward_pre_hooks override each block's cross-attention context with a sigmoid top-hat blend of A and B. Built for surgical "this impulse only on these layers" experiments.
- **Combo 1 / 2 / 3** — preset combinations of step phase × layer band, each with a hardcoded layer range and a Step-in-style intensity slider. Combo 1 = surface (blocks 0..4), Combo 2 = broad mid (blocks 4..12), Combo 3 = narrow center (blocks 6..10). Hard layer mask (no edge softening) so slider=1 is genuinely 100 % B in the band's blocks.
- **Per-mode slider memory.** Step-in and the three Combo modes each remember their own intensity-slider value, so A/B-ing them by clicking buttons does not destroy the last-used position of any individual mode. Linear and Layer already had independent state.
- **Mode buttons trigger regeneration.** Clicking any of the six mode buttons fires a fresh inference with the newly selected mode, matching the existing slider/drift auto-regen UX.
- **Slider-scale fix.** The Step-in/Combo intensity slider now displays 0–1 (was 0.5–1.0) — internal mapping onto the audible region of `injection_transition_at` / `late_phase_alpha` is unchanged. Old presets reload their stored value into the corresponding mode slot, but their effective rendering may differ since the slider value is no longer remapped before being sent to the backend.

## v1.3.0-beta.1 - 2026-04-24

- Expanded the instrument from an early beta into a fuller text-to-sound workflow with independent wavetable extraction regions, shared P1/P2/P3 traversal controls, and a clearer wavetable start-point model.
- Added session persistence for the standalone app so working states survive quit/relaunch instead of behaving like disposable test sessions.
- Reworked the preset workflow with factory presets, `INIT`, explicit P1/P2/P3 locks, auto-trim support, and cleaner preset migration/loading behavior.
- Broadened the musical control surface with a microtuning system, non-Western scales, Shruti support, sampler-mode tuning support, and a more expressive generative sequencer.
- Extended modulation and motion design with drift random waveforms, additional drift targets, sample-and-hold LFO support, ghost sliders, and a denser but more legible sequencer layout.
- Moved the user-facing setup flow toward installer-first distribution on macOS, added a Windows installer path, and tightened model/preset placement around per-user defaults with system-wide scan fallbacks.
- Improved the guided onboarding and in-app documentation with the embedded manual overlay, clearer install guidance, and a more robust download/setup flow for model assets.
- Hardened backend startup, model discovery, and download handling so failures surface more clearly and model paths behave consistently across packaged installs.
- Stabilized playback and interaction around generate/regenerate, active-voice sampler behavior, modulation editing, shutdown, and repaint-heavy GUI paths.
- Kept the core dependency baseline unchanged across the beta line: `backend/requirements.txt` and the top-level CMake `FetchContent` pins for `nlohmann/json` and `signalsmith-stretch` did not change between `v1.0.0-beta.1`, `v1.0.0-beta.2`, and `v1.0.0-beta.3`.

## Notes

- `v1.3.0-beta.1` keeps the release on the beta line while the broader rollout (VST3/AU and additional public platforms) is still unfinished; the current tagged release asset remains the macOS installer only.
- The version number stays monotonic with the older internal `1.2.x` build line and avoids another downgrade in bundle / installer versioning.
