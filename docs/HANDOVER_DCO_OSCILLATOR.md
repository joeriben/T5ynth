# Handover — the DCO oscillator (T5Osc "Advanced" becomes a classical, LLM-authored oscillator)

> **Status correction 2026-07-22 — the v1 pipeline below is no longer live.** Its backend half is
> deleted: `mode:"dco"` went in `40600a0e`, so nothing produces the recipe JSON any more. `dco::Baker`
> still compiles (`CMakeLists.txt:157`) but no file in `src/` calls it. `loadDcoWavetable` survives and is
> now fed by the LCO/Csound result instead (`MainPanel.cpp:2121`, `result.lcoFramesA`). Read the status
> paragraph below as the 2026-07-10 record it is. What §§2–7 still describe accurately is the *concept*
> and the subsystem map; §5.4 (the LLM) has been re-verified against HEAD and is current, and
> `docs/DCO_LLM_GUARDRAILS.md` §1a itemises what of the router still runs.

Status: **BUILT end-to-end (2026-07-10).** The v1 pipeline exists on `main`: DCO prompt → backend
lexicon router (LLM guardrailed per `docs/DCO_LLM_GUARDRAILS.md`, Slice 3, commit `427ffef2`) →
recipe JSON → `dco::Baker`
(`src/dsp/DcoBaker.*`, Slices 1+2, commit `bcd02384`) → `loadDcoWavetable` → Wavetable engine
(Slice 4, commit `71aa57d2`). **UI correction (`acf295e7`): the Advanced view IS the DCO panel** —
a paradigm split earlier revisions of this handover under-specified (the Slice-4 MVP had wedged a
BAKE row under the neural Advanced layout instead). ADV now shows only a large multiline DCO
prompt editor (its OWN text, not bound to prompt A; Enter bakes) plus the BAKE/status row; the
model switchbox, A/B editors, injection bar, translate flag, alpha slider and RE-PROMPT module are
Easy-only, and the height/font budget is a single mode-independent constant
(`kPromptContentUnits = 20.11`), so the EASY↔ADV toggle changes neither section height nor font
scale. Sections below that say "prompt A" for the DCO input predate this. Slice 0 was executed
by an external session (`035e5cf8`/`63dc543f`) and repaired (`e90ac3d7`: font lockstep 17.25,
forced per-model Steps/CFG at the request choke point, preset seed-mode sync). Every slice passed
an adversarial opus review; the audition tool re-proved the two lost design demos
(`tools/dco_audition_out/`). §§2–7 below remain the authoritative concept + subsystem reference.
The two bake sound defects the user heard were measured and fixed post-handover: DCO strips are
now adopted bit-exact (`WavetableOscillator::setExactFrames` — the `extractContiguousFrames`
seam-ramp/renorm corrupted exact cycles by up to 0.433) and driven by a dedicated motion
transport (`setDcoMotion`, recipe `motion_rate_hz`, lexicon v2) instead of the sampler-style
auto-scan whose Loop wrap slewed back through the whole table every strip-duration ("Aussetzer
alle 2-3 Sekunden"). Held-note DCO↔neural transport flips stay click-free via the smoothedScan
fold in `syncSharedConfigFrom`; guard: `tools/audition_dco_plugin_path.cpp`.
Known v1 seams: WT frame-count/bracket edits revert the table to the last neural audio; the
wavetable display's scan cursor does not show DCO motion (`getCurrentScanPosition` returns only
`smoothedScan`); DCO bakes are not yet event-logged or preset-persisted.
Branch: `main` (the user works directly on main and commits concurrently — see §9).
Scope: this is the DCO **engine** handover. The UI-migration that frees the canvas is a separate,
nearly-finished doc: `docs/HANDOVER_OSC_EASY_MIGRATION.md` (untracked, still in the tree). Read it too.

This document is written to be self-sufficient: **no information is meant to be missing.** Every
external claim carries a `file:line` you can jump to. Where a section reconstructs a design that was
only ever in a lost scratchpad session, it says so explicitly.

---

## 0. TL;DR / orientation

- **What the DCO is.** A *non-neural, classical* oscillator that lives on the T5Osc **Advanced** ("back")
  view. Instead of a diffusion model, a **code-LLM authors a compact synthesis recipe** (keyframe
  additive spectra + a motion sequence + a loop). That recipe is **baked offline** — exactly like a
  generation — into band-limited single-cycle wavetables, and the **existing Wavetable engine plays them
  back** with the equal-power morph it already has. Perfectly loopable by construction.
- **Why it exists.** It is the critical-aesthetic *counterpart* to the neural engines: the same
  "author-by-description" gesture, but through transparent, closed-form DSP the user can inspect and
  predict — the machine-listening bias made audible by contrast (§1).
- **Where it plugs in.** Playback target = `WavetableOscillator masterOsc` (NOT `WavetableBank`, which is
  dead code — §4.0). Delivery spine = the existing `loadGeneratedAudio` path (§5). LLM = the existing
  on-board instruct model via `run_author_instruct` (§5.4). UI slot = the Advanced param grid, freed by removing Steps/CFG (§6, §4.3 of the
  EASY handover).
- **What's done (this session).** SA3 Dim-Explorer/Semantic-Axes wiring (`d05d2f45`) and the Dim-Explorer
  log-axis fix (`dc6dfa9b`) are banked. The EASY 2×2 migration is done except one removal.
- **The one thing left before the canvas is empty.** Remove Steps/CFG from the Advanced view (§4.3 of the
  EASY handover; full mechanics in §6.4 here). After that, the Advanced param grid is free for the DCO.
- **Build order.** §7: spine-first (hand-authored recipe → hear a loop, no LLM) → recipe DSL + baker →
  LLM author → UI → motion/morph.

---

## 1. Why this exists — the vision (do not lose this framing)

The instrument's north star (memory `project_critical_aesthetic_mission`): T5ynth is a *critical-aesthetic
and pedagogical* instrument-making machine that renders machine-listening **biases audible and
negotiable** — it *exposes*, it does not *correct*. The A and B prompts are equal partners
(`project_philosophy_a_b_equality`).

The DCO is the **transparent foil** to the neural engines. The neural path (SAO / SA3 / AudioLDM2) turns a
description into sound through an opaque, learned prior — you cannot predict the spectrum from the words.
The DCO turns a description into sound through **closed-form, classical synthesis** — additive partials,
band-limited classic waves, PWM, sync, ring/AM, waveshaping. Same gesture ("author a sound by describing
it"), opposite epistemics: one is a black box you negotiate with, the other is glass you can reason about.
Putting them **side by side in the same instrument, under the same author-by-description UI**, is the
pedagogical point — the contrast is the lesson.

**The user's organizing axis is T5 vs T5Osc**, not Easy/Advanced and not play/program (EASY handover §1,
and §6.6 here):
- **T5** = the neural/semantic core: prompts, A↔B blend, embeddings, Semantic Axes, the Dimension Explorer,
  the Re-Prompt self-listening loop. *The meaning of the sound.*
- **T5Osc** = the operational oscillator knobs: seed, steps, cfg, magnitude, chaos, duration. *How the
  render is executed.*

The DCO belongs to the **T5Osc** family — it is literally a new oscillator — and it takes over the
Advanced view once the last raw inference dials (Steps/CFG) move off it.

---

## 2. The DCO concept in full (authoritative reconstruction)

> **Provenance note.** The concept was designed in an earlier session whose scratchpad proofs
> (`tzpwm_demo.py` = band-limited PWM; `morph_loop_demo.py` = a saw→square→pulse→saw morph loop) are
> **permanently lost** — they were scratchpad-only, are not in git history, and there is no DCO design note
> in `docs/devlog.md`. What follows is the authoritative reconstruction from EASY handover §7 plus the
> concrete schema/algorithm detail needed to actually build it. Treat §2.4's DSL as a *proposed starting
> point*, not a frozen spec — but §2.1–2.3 are the locked concept.

### 2.1 The pipeline (locked)

```
  user text ("a hollow detuned pulse that opens up")
        │
        ▼  (offline, background — like any generation)
  on-board code-LLM ──► RECIPE  = keyframe additive spectra
                                + motion sequence (segments: →keyframe, duration, curve)
                                + loop flag
        │
        ▼  BAKER (deterministic, closed-form param→spectrum; runs off the audio thread)
  N single-cycle, band-limited wavetable frames  (2048 samples each)
        │
        ▼  existing Wavetable engine (audio thread only PLAYS + crossfades)
  sound  ──  perfectly loopable at any pitch, morph closes on keyframe[0]
```

- **Offline, not real-time.** The recipe authoring + baking happen in the background, exactly like a
  neural generation (§5.1's detached-thread pattern). The audio thread never synthesizes — it only reads
  pre-baked frames. This is why the "expensive" part (LLM + FFT band-limiting) is free of RT constraints.
- **Perfectly loopable by construction.** Every frame is one single cycle (2048 samples = one period), so
  it is seamless at any playback pitch; and the motion sequence is authored to **close on keyframe[0]**, so
  the frame-scan loop is seamless too. This is the payoff sample/diffusion generation *cannot* give — a
  neural render has a fixed length and edge discontinuities.
- **The engine already does the hard playback parts.** Band-limiting (per-octave mip levels), the
  equal-power morph between table generations, and lock-free per-voice distribution are all existing
  `WavetableOscillator` behavior (§4). The DCO is "just" an offline **producer** of frames for a
  consumer that already exists.

### 2.2 The predictability boundary (what the DCO is allowed to do)

Include ONLY techniques with a **closed-form param→spectrum map** — where the LLM can name a technique +
parameters and the baker can compute the exact spectrum deterministically:

- **Additive** (explicit partial amplitudes/phases) — the canonical keyframe representation.
- **Classic analogue waves** (sine, triangle, saw, square) as closed-form Fourier series.
- **Band-limited PWM** (pulse width → known harmonic series; this is what `tzpwm_demo.py` proved).
- **Hard sync** (master/slave ratio → closed-form spectrum).
- **Ring mod / AM** (two-operand sidebands — closed form).
- **Chebyshev waveshaping** (polynomial order → exact harmonic mapping).
- **2-operator FM** (single carrier:modulator, index → Bessel-series sidebands — closed form).

**Exclude** anything chaotic / not closed-form: multi-operator FM stacks, feedback FM. These have no
predictable param→spectrum map and defeat the whole "transparent, inspectable" premise.

### 2.3 The real risk is natural-language, not the math

The DSP is deterministic. The failure mode is **the LLM hallucinating a spectrum for an ambiguous timbre
word.** Named-synth vocabulary ("saw", "PWM 30%", "2-op FM index 3", "3rd-harmonic waveshaper") maps
cleanly and the code-LLM handles it. **Vague timbre words ("warm", "glassy", "hollow", "fat")** must
be **FLAGGED, not invented** — the LLM should refuse to fabricate a partial series from a mood word and
instead route those to a *separate analysis branch* (fit the math to measurements of real softsynths, so a
mapping is grounded in data rather than guessed). Design the LLM contract so "I don't have a closed-form
mapping for 'warm'" is a first-class, expected output — this is the single most important robustness
decision in the whole engine.

### 2.4 A concrete recipe DSL (PROPOSED starting schema — iterate freely)

This is the schema I recommend to make §2.1–2.3 buildable. It is deliberately small, JSON, and rides the
**existing** `\x03` text channel (§6-IPC) — no new wire type needed. Not gospel; the concept above is.

```jsonc
{
  "version": 1,
  "base_hz": 261.63,            // authoring reference pitch; frames are pitch-agnostic single cycles
  "keyframes": [                // each = one single-cycle spectrum, closed-form
    { "id": "k0",
      "kind": "additive",       // additive | saw | square | pulse | triangle | sync | ring | am | cheby | fm2
      "partials": [             // for kind:additive — harmonic number → amp (0..1), optional phase (rad)
        { "h": 1, "a": 1.0,  "phase": 0.0 },
        { "h": 2, "a": 0.5 },
        { "h": 3, "a": 0.33 }
      ]
    },
    { "id": "k1", "kind": "pulse",  "width": 0.30 },       // band-limited PWM
    { "id": "k2", "kind": "fm2",    "ratio": 2.0, "index": 3.0 },
    { "id": "k3", "kind": "cheby",  "order": 3, "drive": 0.7 }
  ],
  "motion": [                   // frame-scan trajectory; each segment morphs to target over `dur`
    { "to": "k0", "dur_frac": 0.0 },     // start
    { "to": "k1", "dur_frac": 0.4, "curve": "lin" },
    { "to": "k2", "dur_frac": 0.3, "curve": "exp" },
    { "to": "k3", "dur_frac": 0.2, "curve": "log" },
    { "to": "k0", "dur_frac": 0.1, "curve": "lin" }        // CLOSES on k0 → seamless loop
  ],
  "loop": true,
  "frames": 128,                // bake resolution → N frames in [8..256]; UI offers {32,64,128,256}
  "flags": [                    // LLM's honesty channel — timbre words it refused to fabricate
    { "word": "warm", "reason": "no closed-form mapping; routed to analysis branch" }
  ]
}
```

Baker contract (deterministic, off-thread):
1. For each **keyframe**, compute a closed-form single-cycle spectrum (2048-pt), IFFT to a 2048-sample
   frame. **Bake full harmonic content up to harmonic 1024** (the 2048-frame Nyquist) — do NOT
   pre-band-limit per octave; the engine derives every anti-aliased octave for you (§4.2, critical).
2. **Sample the `motion` trajectory into `frames` frames**: at each frame index, find which segment you're
   in and interpolate between the two bracketing keyframe spectra by the segment `curve`. This yields N
   single-cycle frames the engine's scan position will morph through.
3. Lay the N frames **end-to-end into one mono `AudioBuffer<float>` of length `N*2048`** and hand it to the
   existing delivery spine (§4.5 / §5). The `loop:true` + closing-on-`k0` guarantees frame `N-1`→frame `0`
   is seamless.

> Whether the baker runs in **Python** (recipe authored + baked in the backend, frames returned) or in
> **C++** (recipe returned as JSON, baked plugin-side) is an open architectural choice — §5.6 lays out both
> seams with exact hooks. Recommendation there: **author in Python (reuse the on-board LLM), bake in C++** (reuse the
> whole `loadGeneratedAudio` spine verbatim) — but both are viable and cheap.

---

## 3. Current state (exact, verified)

- **HEAD = `dc6dfa9b`.** Working tree clean except one untracked file (below). No concurrent user commit
  has absorbed anything.
- **Banked this session:**
  - `d05d2f45` — wired the Dimension Explorer + Semantic Axes for the SA3 engine (backend `_mask_pad`
    restores the learned-padding sentinel so all manipulation stays on real tokens; masked mean-pool gated
    to SA3; SAO byte-identical). Touches `backend/pipe_inference.py`, `src/gui/{MainPanel,PromptPanel}.cpp`,
    `PromptPanel.h`. **The user confirmed by ear it "works absolutely and wonderfully."**
  - `dc6dfa9b` — Dimension Explorer fixed symmetric-log Y axis (constants
    `kLogLinThresh=0.05, kLogMaxValue=50, kLogLinFrac=0.12, kMaxOffset=4`; `valueToY`/`yToValue` are a
    static symlog map ±`kMaxOffset`). Removed the per-generation `valueScaleMax_` rescale that made the
    display "flip through the gegend." Touches `src/gui/DimensionExplorer.{cpp,h}`.
- **Untracked, still in tree:** `docs/HANDOVER_OSC_EASY_MIGRATION.md` — the EASY-migration handover.
  Commit it whenever; it is a companion to this doc.
- **The EASY 2×2 migration (that doc):** DONE except one item. Duration/Variation/Magnitude/Chaos are on
  the Easy view as a correct 2×2 (`f4964bec`, `78d47ec6`); seed dialog done (`aa4b2353`); the
  Semantic Axes | Dim Explorer 2-segment switch done (`a7134bfb`); Dim-Explorer repaired
  (`bf0221fc`, then `dc6dfa9b`).
- **THE LAST BLOCKER before the DCO canvas is empty:** **§4.3 of the EASY handover — remove Steps/CFG from
  the Advanced view.** Full mechanics in §6.4 below. After that removal, the Advanced param grid exposes no
  raw inference dials and is free to host the DCO surface. **Do this first** (it's Slice 0 in §7).

---

## 4. Subsystem map A — the Wavetable engine (the DCO's playback target)

*Source: full read of `src/dsp/WavetableOscillator.{h,cpp}`, `SynthVoice.{h,cpp}`, `VoiceManager.{h,cpp}`,
`src/PluginProcessor.{h,cpp}`, `BlockParams.h`, `ParamCache.h`.*

### 4.0 `WavetableBank` is DEAD CODE — do not use it
`src/dsp/WavetableBank.{h,cpp}` is a stub. `loadFromBuffer` ignores both the buffer and its sample rate
(params commented out) and only `push_back`s the name string — `WavetableBank.cpp:13-18`. It stores only
`std::vector<juce::String> setNames` — `WavetableBank.h:35`. **Zero callers** anywhere in `src/` (grep = 0
hits). The real engine is `WavetableOscillator`. **The DCO must feed `masterOsc`, never `WavetableBank`.**

### 4.1 Data representation
- A "wavetable" = a **multi-frame morph table**, published as an immutable `MipData`:
  `std::vector<std::vector<std::vector<float>>> frames` indexed `[level][frameIdx][sample]`, plus
  `int numFrames, numLevels; uint64_t generation` — `WavetableOscillator.h:138-143`.
- **2048 samples per frame = one single cycle.** `FRAME_SIZE = 2048`, `HALF_FRAME = 1024` —
  `WavetableOscillator.h:30-31`.
- **Frame count variable, 8–256.** `MIN_FRAMES = 8` (`:33`); extraction clamps `maxFrames` to `[8,256]`
  (`WavetableOscillator.cpp:436`). UI choices `{32,64,128,256}` via the `wtFrames` param
  (`PluginProcessor.cpp:4597-4599`). If fewer than 8 frames survive, the last is duplicated up to 8
  (`WavetableOscillator.cpp:608-611`).
- **8 mip levels per frame** (band-limited copies): `NUM_MIP_LEVELS = 8` (`:32`).
- Playback frame selection = a continuous scan in `[0,1]` mapped to `[0, numFrames-1]`; non-integer
  positions crossfade between adjacent frames — `WavetableOscillator.cpp:870`.
- A **DCO producing N single-cycle band-limited waves = N frames.**

### 4.2 Band-limiting — CRITICAL: bake frame level-0 FULL, the engine derives the rest
The engine band-limits **internally** via FFT brick-wall filtering in `generateMipLevels()`
(`WavetableOscillator.cpp:240-286`):
- Level 0 = the source frames unchanged (`:247-249`).
- Levels 1–7: forward FFT of the 2048-sample frame → **zero every harmonic bin above
  `maxHarmonic = HALF_FRAME >> level`** (`:256`, `:267-271`) → IFFT. Retained harmonics per level:
  L0=1024, L1=512, L2=256, … L7=8.
- At playback the level is chosen from pitch (`rawLevel = log2(freq*FRAME_SIZE/sampleRate)`,
  `mipLevel = clamp(ceil(rawLevel),0,numLevels-1)` — `:823-831`), keeping the top harmonic near Nyquist.

**Implication for the DCO (do this exactly):** bake **frame level 0 with the full desired harmonic content
up to harmonic 1024**; `generateMipLevels` derives all anti-aliased octaves for you. **Do NOT pre-band-limit
per octave** — any anti-aliasing you bake below level-0 bandwidth is redundant and *discarded* (levels are
recomputed from level 0). The engine only ever *removes* harmonics; there is no polyBLEP, no oversampling,
no per-note RT band-limiting. The mip map IS the entire anti-aliasing strategy.

### 4.3 `morphToFramesFrom` — the crossfade-follow morph (this is a PLATFORM INVARIANT — see §8)
- `void WavetableOscillator::morphToFramesFrom(const WavetableOscillator& source)` — decl
  `WavetableOscillator.h:117`, def `:166-186`. Points `sharedSource_` at the master, copies traversal/morph
  config, **atomically loads** the master's latest published snapshot (`loadPublishedMipData()` →
  `std::atomic_load_explicit(...acquire)` — `:24-27`), republishes to its own `publishedMipData_` (release
  store, `:175`), and — if this is a genuinely new `generation` — calls `beginMorphToMipData` (`:185`).
  Generation guard: same `generation` → returns without restarting a morph (`:177-183`).
- `beginMorphToMipData` (`:98-144`): if no active data → `adoptMipData` immediately (no fade); if
  `morphTimeMs_ <= 0` → hard adopt; else set `targetMorphMipData_`, `morphAlpha_ = 0`,
  `morphIncrement_ = 1/round(morphTimeMs_*0.001*sampleRate)`, `morphActive_ = true` (`:138-143`).
- **Equal-power crossfade over the morph time** happens per-sample in `processSample`: while
  `morphActive_`, read the same phase/scan from BOTH active and target snapshots and mix with
  `dryGain = cos(alpha·π/2)`, `wetGain = sin(alpha·π/2)`, advancing `morphAlpha_` each sample; at
  `alpha >= 1` call `adoptMipData(targetMorphMipData_)` — `WavetableOscillator.cpp:834-847`.
- **Morph time source = the "Regen XFade" / Drift Crossfade param** (`driftCrossfade`, default 200 ms):
  `setMorphTimeMs(ms)` clamped `[0,2000]` (`:73`); the processor feeds `paramCache.driftCrossfade` right
  before every distribute (e.g. `PluginProcessor.cpp:4656`).
- Snapshots are `shared_ptr<const MipData>` published with release stores; readers use acquire loads; the
  old snapshot is auto-reclaimed when the last `shared_ptr` drops (no manual retire-bin for wavetables —
  unlike SamplePlayer). **This is the RT-safe pattern the DCO inherits for free.**

### 4.4 Master ↔ per-voice distribution (lock-free)
- **Master:** one `WavetableOscillator masterOsc;` on the processor — `PluginProcessor.h:352`; accessors
  `getMasterOsc()`/`getMasterOscConst()` (`:266-267`); prepared `PluginProcessor.cpp:1569`.
- **Per-voice:** each `SynthVoice` owns its own `WavetableOscillator osc;` — `SynthVoice.h:101`,
  `getOsc()` (`:86`). Voices are held by `VoiceManager`.
- **Sharing (lock-free, `shared_ptr`):** `shareFramesFrom(source)` (`:146-164`) = **hard swap** (adopt, no
  fade) for inactive/silent voices and at note-on. `morphToFramesFrom` = **crossfade** for active voices.
  Shared voices keep their own phase/freq/scan but point at the master's immutable `MipData` via
  `shared_ptr` — **no per-voice copy** of the 8×N×2048 payload.
- **`VoiceManager::distributeWavetableFrames(const WavetableOscillator& masterOsc)`** — decl
  `VoiceManager.h:96`, def `:736-746`: caches `currentWavetableMaster_`, then **active Wavetable voice →
  `morphToFramesFrom` (crossfade)**, otherwise **`shareFramesFrom` (hard adopt)** — `:741-744`.
- Note-on adopts via `shareFramesFrom(*currentWavetableMaster_)` (mono `:186-187`, poly `:296-297`).
- `processBlock` re-runs `distributeWavetableFrames` every block so late-started voices converge
  (`PluginProcessor.cpp:3237-3242`); the generation guard makes it a no-op when unchanged.

### 4.5 How table data ENTERS the engine — the DCO's exact hook
**There is NO direct frame-array setter.** The only public input surface (full API scan of
`WavetableOscillator.h:37-129`; grep for `setFrames/loadFrames/pushFrames/adoptFrames` = nothing) is two
buffer-based extractors, both ending in `generateMipLevels(frames)` (band-limit + atomic publish):
1. `extractFramesFromBuffer(buffer, bufferSR, startFrac=0, endFrac=1, maxFrames=256)` —
   `WavetableOscillator.h:43-45`, def `:433-615`. **Pitch-synchronous**: YIN pitch detection + resample
   detected periods into 2048-sample frames, or windowed fallback when unpitched.
2. `extractContiguousFrames(buffer, bufferSR, startFrac=0, endFrac=1)` — `:100-101`, def `:660-714`.
   **Contiguous**: slices the region into non-overlapping 2048-sample frames verbatim, no pitch detection.

**The canonical processor sequence the DCO must reproduce** is in `loadGeneratedAudio`
(`PluginProcessor.cpp:4381`, core at `:4630-4662`), all under `getCallbackLock()` (`:4639`):
```
masterOsc.extractContiguousFrames(feedBuffer, sr, extractStart, extractEnd);  // or extractFramesFromBuffer
syncWavetableTraversal(sr, feedBuffer.getNumSamples());                        // :4655
masterOsc.setMorphTimeMs(paramCache.driftCrossfade->load());                  // :4656
voiceManager.distributeWavetableFrames(masterOsc);                            // :4662
```
Same triple recurs in `reloadProcessedAudio` (`:4715-4743`), `reextractWavetable` (`:4813-4837`), and the
per-block convergence (`:3237-3242`).

**DCO integration recommendation (from both agents, concurring):**
- Lay the N baked 2048-sample frames **end-to-end into a mono `AudioBuffer<float>` of length `N*2048`** and
  call **`masterOsc.extractContiguousFrames(buf, sr, 0, 1)`** (exact 2048 boundaries — NOT
  `extractFramesFromBuffer`, which would re-pitch-detect and resample your clean frames). Then
  `setMorphTimeMs` + `distributeWavetableFrames`. **This reuses the entire `loadGeneratedAudio` spine
  verbatim.**
- **Caveat:** `extractContiguousFrames` still applies (a) per-frame peak-0.95 **normalization**
  (`WavetableOscillator.cpp:697-706`) and (b) a seamless-loop **linear-ramp boundary correction**
  `frame[i] += (frame[0]-frame[last])*i/2048` (`:687-691`). If your baked frames are already loop-perfect
  and level-matched, the correction is ~0 but non-null, and normalization rescales per frame.
- **If you need bit-exact baked frames** (no normalization, no ramp), the clean injection point is the
  **private** `generateMipLevels(frames)` (`WavetableOscillator.cpp:240`) — it does only FFT-mip + publish.
  Add a **new public `setFrames(std::vector<std::vector<float>>)`** that calls it, plus the matching
  `distributeWavetableFrames`. This is the one small engine change the DCO might justify (§7 Slice 2
  decides). Follow the existing atomic-publish contract exactly — do not invent a new one.

### 4.6 Playback (for completeness)
- Pitch→phase: `phase += freq*FRAME_SIZE/sampleRate`, wrapped, NaN-guarded (`:852-856`). Output pitch =
  the set frequency (`SynthVoice.cpp:923-924`).
- Interpolation: **linear within a frame** (phase axis), **Catmull-Rom cubic across frames** (scan axis)
  when `wtSmooth`/`setInterpolation` on — `WavetableOscillator.cpp:861-914`.
- Scan position: `setScanPosition(0..1)` one-pole-smoothed ~5 ms (`:819`); auto-scan
  (OneShot/Loop/PingPong) is the alternative driver (`:751-816`).

### 4.7 Engine selection
- Processor/APVTS enum: `EngineMode { Sampler=0, Wavetable=1, Freeze=2 }` — `BlockParams.h:496-505`. **The
  UI label for Freeze is "Granular"** (`:501`) — "Granular" in the UI == the Freeze engine internally; there
  is no separate Granular engine. Voice enum `SynthVoice.h:81` (3 modes).
- `engine_mode` APVTS param → voice enum in the block-param builder (`PluginProcessor.cpp:2646-2665`);
  `setEngineMode` fans out to every voice (`VoiceManager.cpp:701-705`). Predicate `isWavetableMode()`
  (`PluginProcessor.cpp:4368`).
- **The DCO plays through the Wavetable engine** — it produces frames for `masterOsc`, so selecting it is
  (at minimum) selecting Wavetable mode with DCO-baked frames. Whether the DCO is a *new engine-mode enum
  value* or *Wavetable-mode-with-a-DCO-source* is an open UI/architecture choice (§7 Slice 4).

---

## 5. Subsystem map B — the offline generate→deliver spine + IPC + the on-board LLM

*Source: full read of `src/gui/PromptPanel.cpp`, `src/inference/PipeInference.{h,cpp}`,
`backend/pipe_inference.py`, `docs/IPC_PROTOCOL.md`, `docs/ADDING_A_MODEL.md`.*

### 5.1 The generate flow (button → background thread → deliver) — the pattern to copy
- Trigger (message thread): `PromptPanel::triggerGeneration()` — `PromptPanel.cpp:2503`. Re-entrancy guard
  `if (generating || translatingPrompts_ || loopStepInFlight_) return;` (`:2508`). Cache short-circuit
  (`:2510-2514`). Builds `auto req = buildInferenceRequest();` (`:2538`, builder at `:2178+`).
- **Concurrency primitive = a detached raw `std::thread` + `juce::MessageManager::callAsync`** (NOT
  `std::async`, NOT a `juce::Thread` subclass). Spawn `std::thread([...]{...}).detach()` — `:2550`, `:2624`.
  Blocking inference on that thread: `pipePtr->generate(req)` (`:2552`). Result marshalled back:
  `MessageManager::callAsync([... result=std::move(...) ...]{...})` (`:2553`).
- **This is exactly the offline model the DCO uses**: author + bake on a detached thread, deliver on the
  message thread. Never touches the audio thread.

### 5.2 Deliver spine — `loadGeneratedAudio`
`T5ynthProcessor::loadGeneratedAudio(const juce::AudioBuffer<float>&, double sr)` — `PluginProcessor.cpp:4381`
(decl `PluginProcessor.h:75`), message thread. Called from `PromptPanel.cpp:2563` (and MainPanel sites).
Sequence: copy raw → filter/trim → auto-position loop region → **heavy prepare off the lock** (sampler
`prepareBufferLoad`, `masterOsc.extract*`, `masterFreeze.loadBuffer`) → **publish under
`getCallbackLock()`** (`:4639`): `applyPreparedBufferLoad`, `syncWavetableTraversal`, and the per-voice
fan-out `distributeSamplerBuffer` / **`distributeWavetableFrames`** (`:4662`) / `distributeFreezeBuffer`.
- Master engines: `masterOsc`, `masterSampler`, `masterFreeze`, `voiceManager` — `PluginProcessor.h:348-354`.
- **Persistent off-audio worker already exists:** `std::thread samplerReprepareThread;`
  (`PluginProcessor.h:355`, started `:297`, body `samplerReprepareThreadMain()` `:2127`, joined in dtor
  `:415-417`) — the model for any DCO-side background baking that must not touch the audio thread.

### 5.3 PipeInference IPC (the wire)
- Owned as `std::shared_ptr<PipeInference> pipeInference` — `PluginProcessor.h:475`; `getPipeInferencePtr()`
  (`:99`) hands the shared_ptr to worker threads; `launchPipeInference` (`PluginProcessor.cpp:420-423`).
- Protocol status bytes (`PipeInference.h:15-19`, `docs/IPC_PROTOCOL.md:305-317`): `\x00` error,
  `\x01` audio, `\x02` ready (handshake), `\x03` text. **`\x04`+ are UNUSED** — free for a new payload type.
- `PipeInference::Result` — `PipeInference.h:106-117`: `success`, `audio`, `sampleRate`, `generationTimeMs`,
  `seed`, `errorMessage`, and `embeddingA/embeddingB/embeddingBaseline` (768-d).
- `PipeInference::generate(const Request&)` — `PipeInference.cpp:865`. Holds `std::recursive_mutex
  stateMutex_` (`:867`) that **serializes ALL ops** (generate/translate/interpret/analyze/preload) onto the
  single pipe; auto-restarts a dead subprocess. **The Request struct has no `mode` field** → `generate()`
  never writes `"mode"` → backend defaults to `"generate"` (`pipe_inference.py:3683`). Other ops set
  `"mode"` explicitly.

### 5.4 The on-board LLM — the analogue for "LLM authors a recipe"
*(Model + entry point corrected 2026-07-22, `dd2e0373`/`9ece63b3`; line numbers in this subsection are
current as of that date. The separately-installed Qwen2.5-1.5B translator this section described is
**gone** — see the last bullet.)*

One instruct model runs in the backend and serves `translate`, `interpret` **and** `csound`. All in
`backend/pipe_inference.py`:
- Model resolution: `_resolve_coder_model_dir(request)` (`:938-990`) — `request["coder_model_path"]`, then
  `$T5YNTH_CODER_MODEL`/`$LCO_MODEL_DIR`, then `<model root>/coder/gemma-4-12b-it-qat-q4_0` by exact name,
  then a scan of `coder/`, `lco-coder/`. No fallback: no model → the request is refused (LLM-first).
  *(The old `_resolve_translation_model_dir` no longer exists; stale callers survive in `tools/`.)*
- **`run_author_instruct(request, text, system_prompt, device, max_new_tokens=None)`** — `:1205-1233`. The
  entry **for `translate` and `interpret`**: resolves the model, then dispatches on install shape — a `.gguf` goes to `run_gguf_instruct`
  (`:1018-1036`, llama.cpp, cached by `_get_gguf` `:996-1015`, all layers on GPU), a transformers directory
  (dev drop / pre-GGUF install) falls through to the older `run_instruct` (`:1101-1202`). Both build a
  `[{system},{user}]` chat-template list and decode **greedy/deterministic** (GGUF: `temperature=0.0`;
  transformers: `do_sample=False, num_beams=1`). **This is the exact primitive for "LLM authors a recipe":
  supply a DCO system prompt, feed the user's text, parse the reply.**
- **No output cap** on either path: the GGUF passes `max_tokens=-1` (run to EOS), the transformers path
  derives the ceiling from the model's own context window. A caller-supplied `max_new_tokens` is honoured;
  nothing on the Csound path sets one.
- `translate_prompt` = `run_instruct` + `TRANSLATION_SYSTEM_PROMPT` (`:1236-1238`, prompt at `:889-895`) —
  but the `translate` **wire mode** calls `run_author_instruct`, not this helper.
- Wire dispatch: `translate` (`:3548-3555`), `interpret` (caller supplies `system_prompt`, `:3562-3593`),
  `csound` (`:3605-3649`), `analyze` (CLAP "ear") — all intercepted BEFORE audio-model routing, all reply
  via `send_text` (`\x03`). **`csound` does not go through `run_author_instruct`**: it resolves the model
  itself (`:3611`) and calls `run_gguf_instruct`/`run_instruct` through its `csound_llm` closure
  (`:3618-3628`) — same model, second route. Spec: `docs/IPC_PROTOCOL.md` §3.3 covers `translate`/
  `interpret`/`analyze`; `csound` has no §3.3 entry, only the `coder_model_path` row in §3.1 (`:223`).
- C++ side: `interpret(systemPrompt, userText, maxNewTokens, device)` — `PipeInference.h:189-192`, impl
  `PipeInference.cpp:1165` (sets `"mode":"interpret"` at `:1199`). **The DCO's recipe-author step mirrors or
  extends `interpret()`.** The trailing `modelPath` argument (wire key `model_path`) is gone with the second
  model; the one model is named on the wire by `coder_model_path` when it is named at all.
- **Model qualifier (rewritten 2026-07-22).** There is no longer a small translator to upgrade. The one
  model is `google/gemma-4-12B-it-qat-q4_0-gguf`, run as a 4-bit GGUF through **llama.cpp**
  (`llama-cpp-python>=0.3.34`), not through transformers: 6.98 GB fits a 16 GB Mac where the same model in
  bf16 is 23.9 GB, and transformers has no working 4-bit path on MPS (`backend/requirements.txt:36-43`,
  `pipe_inference.py:900-904`). **The `transformers>=4.53,<5` pin still binds** — but it now constrains only
  the *audio* stack (SA3/t5gemma, diffusers, audioldm2 — `requirements.txt:1-10`); it is no longer a
  language-model blocker, because the shipped author is a GGUF and never touches transformers. (The
  transformers loader survives only for the dev-drop/pre-GGUF directory shape above — a fallback, not the
  install path, so the pin does not gate which author model can ship.) Memory
  `project_translator_qwen_upgrade` is obsolete; see `project_lco_author_model_gemma4_gguf`.

### 5.5 The full orchestration to copy
`PromptPanel::runSemanticLoopStep(const PipeInference::Result&)` — `PromptPanel.cpp:3055-3302` — is the
closest existing analogue to "author on a background thread, deliver without blocking": spawns
`std::thread` (`:3169`), calls `analyze` (`:3182`) then `interpret` (`:3195`) on it, marshals back via
`MessageManager::callAsync` (`:3214`). Re-entrancy guarded by `loopStepInFlight_`. **Clone this shape for
the DCO** (author-recipe → bake → deliver).

### 5.6 Where a NON-neural generator hooks in (two independent seams)
Both in `backend/pipe_inference.py`; canonical guide `docs/ADDING_A_MODEL.md` (structure accurate, its
inline line numbers are STALE — `_model_format` is at `296`, `load_pipeline` at `1527`).
- **Seam A — model-format registry + loader dispatch** (for a persistent "engine"): `_model_format`
  (`:296`) returns `"diffusers"|"audioldm2"|"native"|None`; `load_pipeline` (`:1527`) dispatches on
  it. Add `if fmt == "dco": return _load_dco_pipeline(...)`. The LRU cache `_loaded_pipelines` +
  `get_pipeline()` then manage it for free.
- **Seam B — request `mode` dispatch** (for a stateless op, like translate/interpret): main loop
  `for line in sys.stdin:` (`:3538`); modes handled top-down, then `mode = request.get("mode","generate")`
  (`:3683`). A `"dco"` mode slots in as `if request.get("mode")=="dco":` **before** audio-model routing
  (like `interpret`), since the recipe author reuses the on-board LLM (`run_author_instruct`) and needs no
  audio model.
- C++ request side: `req.model = getSelectedModel()` (`PromptPanel.cpp:2189`, slots `kNumModelSlots=5`,
  `PromptPanel.h:273`). A DCO can be a new **model slot** (Seam A) or a new **op** (Seam B). Backend fails
  loud on an unknown requested model (`:3283-3290`), so a DCO model dir must be discoverable by
  `find_models()`, OR use a mode instead.

### 5.7 The payload boundary — a recipe needs NO new wire type
- **Recipe-as-JSON rides the existing `\x03` text channel.** `send_text(message)`
  (`pipe_inference.py:3430-3443`) already carries arbitrary JSON — this is exactly how `analyze` returns
  `{"tags":...,"spectral":...}`. So: backend authors the JSON recipe with `run_instruct` and returns it via
  `send_text`; C++ receives it in an `interpret`-style method (parse with `juce::JSON::parse`, cf.
  `PipeInference.cpp:1304-1320`), then **bakes frames plugin-side** and feeds the `loadGeneratedAudio` spine.
- **Alternative — return baked float32 frames:** add a `\x05` frame mirroring `send_audio`
  (`pipe_inference.py:3125-3148`) + a matching C++ reader mirroring `PipeInference.cpp:960-999`, and a new
  `Result`-like struct. More work, only worth it if baking must live in Python.
- **Absence flagged:** there is **no streamed/percentage progress callback** — only
  `onStatusChanged(text,bool)` (`PromptPanel.h:141`) and `onEmbeddingsReady` (`:138`). If the DCO wants a
  bake progress bar, add a new `std::function` member following the `onStatusChanged` pattern exactly.

---

## 6. Subsystem map C — the Advanced-view UI surface (where the DCO UI goes)

*Source: full read of `src/gui/PromptPanel.{h,cpp}`, `src/gui/MainPanel.{h,cpp}`, `src/gui/GuiHelpers.h`,
`BlockParams.h`.*

### 6.1 Easy vs Advanced = one component, re-laid-out
A "view" is the SAME `PromptPanel`, re-laid-out. One `bool easyMode_` (`PromptPanel.h:403`, default
`false`); `resized()` branches on it — toggles child `setVisible()` (`PromptPanel.cpp:972-994`) and calls
different layout lambdas (`:1271-1280`). No separate child sets.
- Ownership: `MainPanel::setOscEasyMode(bool easy, bool persist)` (`MainPanel.cpp:1440-1457`) →
  `promptPanel.setEasyMode(easy)` (`PromptPanel.cpp:1718-1729`). Persisted under key `"oscEasyMode"`.
- The **"» adv." toggle button lives in MainPanel** (`oscModeToggle`, `MainPanel.h:52`; setup `:446-452`;
  laid out in the "T5 OSCILLATOR" card header `:3238-3240`), text flips `"» adv."`/`"» easy"`.

### 6.2 What's on ADVANCED right now (top→bottom)
`GENERATION header → [Steps | CFG] compact pair → Seed row.` (Visibility `PromptPanel.cpp:972-994`; layout
`:1271-1280`.)
- **Advanced-only:** `genParamsHeader` "GENERATION" (`h:494`); `stepsSlider` (`PID::infSteps`, `h:238`);
  `cfgSlider` (`PID::genCfg`, `h:238`); `seedEditor` (`h:256`) + `randomSeedToggle` "Rnd" (`h:257`) +
  `seedLabel` "Variation" (`h:241`). Attachments `stepsA`/`cfgA` (`PromptPanel.cpp:539-540`, declared
  `h:402`). MIDI-learn routing `stepsSlider→PID::infSteps`, `cfgSlider→PID::genCfg` (`:3312-3313`). The
  `layoutCompactPair(steps…, cfg…)` call is at `:1277-1278`.
- **Shared by both views (top):** model selector switchbox (`modelBtns[5]`), the A↔B block
  (`promptAEditor`/`promptBEditor`, vertical `alphaSlider`, injection-mode switchbox, translate toggle),
  the Re-Prompt module.
- **Easy-only:** `durationRow`, `magRow`, `noiseRow` (SliderRows) + Variation switchbox — laid out by
  `layoutEasyGenParamsBlock` (`:1216-1258`, the 2×2).

**So removing Steps/CFG empties the middle of the Advanced stack — that's the DCO's slot** (and the DCO
likely subsumes the "GENERATION" header).

### 6.3 Per-model Steps/CFG defaults already flow from APVTS (why removal is safe)
- `PromptPanel::defaultParamsFor(const juce::String& model)` — decl `PromptPanel.h:391-392`
  (`struct DefaultParams { float steps; float cfg; };`), def `PromptPanel.cpp:1848-1857`: SA3 → 8/1.0;
  SAO-small → 8/1.0; AudioLDM2 → 50/3.5; else → 20/7.0.
- Called on **model-click** (writes APVTS, unconditional): `PromptPanel.cpp:467-472`
  (`setValueNotifyingHost(convertTo0to1(defaults.steps/cfg))`), and in `hasHiddenActiveState()`
  (`:1751-1763`, the pulse detector).
- **Inference reads Steps/CFG straight from APVTS, not the sliders:** `PromptPanel.cpp:2167-2168`
  (`steps = PID::infSteps->load()`, `cfgScale = PID::genCfg->load()`) → `req.steps`/`req.cfgScale`
  (`:2185-2186`). **Therefore deleting the UI leaves the correct per-model values flowing automatically.**

### 6.4 §4.3 removal checklist (Slice 0 — do this first; all in `PromptPanel.{h,cpp}`)
1. Delete declarations: `stepsSlider,cfgSlider` (`h:238`); `stepsLabel,stepsValue,stepsHint,cfgLabel,cfgValue,cfgHint` (`h:239-240`); `stepsA,cfgA` from the unique_ptr list (`h:402`).
2. Delete construction (`cpp:302-318`), attachments (`cpp:539-540`), MIDI-learn listener registration
   (`cpp:560-563`) and routing (`cpp:3312-3313`).
3. Delete visibility lines (`cpp:976-979`) and hint-hide (`cpp:1017-1018`).
4. Replace the `layoutCompactPair(steps…,cfg…)` call (`cpp:1277-1278`) with the DCO layout.
5. **Font-scale lockstep (mandatory or the panel mis-scales):** the Steps/CFG block contributes exactly
   `compactRow + compactCtrl + gap = 1.15 + 0.9 + 0.28 = 2.33` units (`getPreferredHeightForWidth` Advanced
   branch line `cpp:688`). Remove that term AND drop `kPromptContentUnits` (`cpp:85`, currently `23.19`) by
   `2.33` → `20.86`, in lockstep. If the "GENERATION" header also goes, subtract another
   `compactRow + gap = 1.43` (line `:687`, header `:989`/`:1147-1150`). Whatever the DCO surface adds, add
   those units in BOTH places.
6. Keep `defaultParamsFor` (`cpp:1848-1857`) + its APVTS writer (`cpp:467-472`) — now the SOLE source of
   Steps/CFG. Decide the fate of the `hasHiddenActiveState` Steps/CFG comparison (`cpp:1751-1763`): with no
   UI to change them, they can only differ via automation/preset.

### 6.5 The font-scale lockstep rule (BLOCKING for any Advanced layout change)
Rule (verbatim, `PromptPanel.cpp:67-68`): *"Both ContentUnits MUST equal the unit sum in
getPreferredHeightForWidth so that resized()'s f = (height-2)/ContentUnits resolves back to the preferred
font."* Producer `getPreferredHeightForWidth` (`:658-690`) sums units×`f`; consumer `resized()` re-derives
`f = jlimit(10,20,(height-2)/ContentUnits)` (`:955-957`). Unit constants at `:51-62`
(`kPromptCompactRow=1.15, kPromptCompactCtrl=0.9, kPromptSeedCtrl=1.75, kPromptGap=0.28`, …). Verified
current sums: Advanced `23.19` (`:685-689`), Easy `20.11` (`:677-683`). **Every height change must update
both the branch sum and the ContentUnits constant, term-by-term.** The maintainer comments `:70-84` are the
template.

### 6.6 Reusable UI primitives for the DCO surface
- **`layoutCompactPair`** (lambda in `resized()`, `PromptPanel.cpp:1156-1178`) — the two-column
  side-by-side band; current Steps|CFG host; the natural template for a 2-knob DCO row.
- **`SliderRow`** (`src/gui/GuiHelpers.h:858+`) — the house inline-bar row (caption + horizontal fill +
  inline value). Ctor `(name, formatter, trackColor=kAccent)`; API `getSlider()`, `setTrackColor`,
  `onRightClick`→`showMidiLearnMenu`, `setGhostValue`/`tickGhost` for drift ghosts. Construction pattern
  `PromptPanel.cpp:275-300`; attach+rewire `:536-557`. The recommended primitive for labeled DCO controls.
- **Switchbox pattern** (`GuiHelpers.h:225-266`) — for any segmented DCO control (e.g. waveform/technique
  select): a row of `TextButton`s sharing a `setRadioGroupId` (unique repo-wide — model box uses `1004`,
  coupling `1006`, injection `2027`), backed by a hidden ComboBox/Slider for APVTS, `styleSwitchButton` per
  button, `paintSwitchBoxBorder(g, unionBounds)` in `paint()`. Live examples: model selector
  `PromptPanel.cpp:996-1015`; seed-mode VAR box `:1223-1239`.
- **The parent card** is "T5 OSCILLATOR" (Card 1 of the GENERATION column,
  `MainPanel.cpp:438`/`3233-3248`) = `oscHeader` + `oscModeToggle` + `PromptPanel`. **This is the T5Osc
  surface and where the DCO UI goes.** Card 2 = "Semantic Axes | Dim Explorer"; Card 3 = Generate/cache.

### 6.7 New APVTS params for the DCO (how to add)
The DCO's own controls (technique, keyframe count, motion shape, etc.) need APVTS params. Follow
`docs/ADDING_A_MODULATION_TARGET.md` conventions and the existing gen-param cluster in `BlockParams.h`
(`genAlpha/genMagnitude/genNoise/genDuration/genStart/genCfg/genSeed` at `:156-163`, `infSteps` at `:172`;
UI-label pairs like the `EngineMode` label table `:496-505`). **APVTS is the single source of truth**
(CLAUDE.md) — read them once per block into `BlockParams` in `processBlock`, never look them up per-voice
or per-sample. **Attachment declaration order:** attachments (`unique_ptr<...Attachment>`) declared AFTER
their target components in the `.h` (reverse destruction order) — CLAUDE.md JUCE-safety §3.

---

## 7. Recommended build plan (slices — each a single-concern commit)

Build the **spine first with a hand-authored recipe** so you hear a loop before touching the LLM. Commit
each slice immediately (§9). Audition by ear before every DSP commit (§9).

- **Slice 0 — free the canvas.** Execute §6.4 (remove Steps/CFG from Advanced, font-scale lockstep). Build,
  adversarial review, commit. *This is the last EASY-migration item and unblocks everything.*
- **Slice 1 — prove the playback path (NO LLM, NO baker).** Hand-write ~4 single-cycle frames in C++
  (e.g. sine, saw-ish, square-ish, pulse), lay them end-to-end into a mono `AudioBuffer<float>`
  (`4*2048`), call `masterOsc.extractContiguousFrames` + `setMorphTimeMs` + `distributeWavetableFrames`
  (§4.5). Trigger it from a temp button. **Goal: hear a seamless morph loop through the existing engine.**
  This de-risks the entire "frames → engine" boundary (incl. the normalization/ramp caveat) before any
  authoring exists. Audition by ear.
- **Slice 2 — the baker (deterministic, off-thread).** Implement the closed-form param→spectrum functions
  (§2.2) + the motion sampler (§2.4 baker contract). Decide here whether the peak-0.95 normalization + ramp
  in `extractContiguousFrames` are acceptable or whether to add the bit-exact public `setFrames`
  (§4.5). Reuse the `samplerReprepareThread`-style off-audio worker (§5.2) or the detached-thread pattern
  (§5.1). Audition band-limited PWM + a saw→square→pulse→saw morph loop (**re-prove the lost
  `tzpwm_demo`/`morph_loop_demo` results** — treat this as the acceptance test).
- **Slice 3 — the LLM author.** Add a DCO system prompt + a backend `"dco"` op (Seam B, §5.6) that calls
  `run_instruct` (§5.4) and returns the JSON recipe via `send_text` (§5.7); a C++ `interpret`-style method
  to fetch it; the `runSemanticLoopStep` orchestration shape (§5.5). **Enforce the timbre-word FLAG
  contract (§2.3)** — the LLM must refuse to fabricate spectra for mood words. Verify with adversarial
  prompts (named-vocab → clean recipe; "warm/glassy" → flagged, not hallucinated).
- **Slice 4 — the Advanced-view UI.** Build the DCO surface in the freed Advanced slot using the primitives
  in §6.6. Decide: new `EngineMode` enum value vs Wavetable-mode-with-DCO-source (§4.7). Font-scale
  lockstep (§6.5). English-only labels, self-explanatory (§9).
- **Slice 5 — motion / morph polish.** Wire the motion sequence to the scan/morph controls; ensure the loop
  closes on keyframe[0]; expose keyframe count → `wtFrames`-style resolution. Audition.

Each slice: build (`§10`) → adversarial verification agent (§9) → audition if DSP (§9) → commit.

---

## 8. Constraints & invariants (BLOCKING — read before writing code)

From `CLAUDE.md` and the platform memories. Several apply **directly** to the DCO.

- **★ HELD-NOTE LIVE-FOLLOW (platform invariant, `project_held_note_live_follow`).** A held note ALWAYS
  plays the CURRENT sample, equal-power **crossfading over the Regen XFade time** (`driftCrossfade`, default
  200 ms) — never a hard swap, never a fixed declicker that ignores the control. **The DCO reuses exactly
  this**: `distributeWavetableFrames` already does `morphToFramesFrom` (crossfade) for active voices and
  `shareFramesFrom` (hard adopt) for silent ones (§4.3–4.4). **Do not weaken this** — when the DCO
  re-bakes while a note is held, the held voice MUST crossfade to the new table over `driftCrossfade`. It
  is broken twice historically; guard tool `tools/audition_sampler_follow.cpp`. Mirror the existing morph,
  never invent a one-off.
- **JUCE safety (CLAUDE.md):** every `juce::Timer` subclass calls `stopTimer()` in its dtor before any
  member is destroyed; **NEVER `setLookAndFeel(nullptr)`** (crashes macOS WindowServer); APVTS attachments
  declared AFTER their target components in the `.h`; **never allocate / lock / do file I/O on the audio
  thread**; re-sign after copying into an app bundle.
- **Performance / idle CPU (CLAUDE.md, `docs/PERFORMANCE_GUIDE.md`):** idle-CPU regressions are the #1
  historical bug class. Before adding a timer callback, a `setColour`, a `repaint`, an APVTS lookup in
  `processBlock`, or any per-sample work → consult the anti-pattern catalogue + audioIdle gate. The DCO's
  audio-thread footprint is **only playback** (the engine already does this) — keep it that way; all
  authoring/baking is offline.
- **No silent fundamental changes (`feedback_no_silent_fundamental_changes`):** never alter/remove a
  documented platform fundamental as a side effect. If a task seems to require weakening one, STOP and
  surface it.
- **No preset/binary edits (CLAUDE.md):** never modify `.t5p` presets or binary resources without explicit
  permission.
- **Backend / venv (`feedback_venv_management`, `feedback_test_tool_ipc`):** the env is the agent's job;
  dev backend runs from `backend/pipe_inference.py` via the project `.venv` (Python 3.10.9). Backend test
  tools stay on the **IPC subprocess path** — don't refactor to direct-import.
- **Real-time short sounds (`feedback_realtime_short_sounds`):** T5ynth is a quasi-realtime synth for SHORT
  sounds (0.1–11 s, default 3 s). The DCO's single-cycle tables are inherently short; never
  generate-long-then-trim.
- **One concern per commit; commit message ends with:**
  `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.

---

## 9. Working style (HARD — from memory; violating these wastes the user's time)

- **The user is Prof. Dr. Benjamin Jörissen** (creator of T5ynth, UCDCAE AI Lab). Canonical lab string
  "UCDCAE AI Lab" — never hallucinate names; grep the repo.
- **TEST, don't theorize (`project_resynth_platform_verdict`, `feedback_dont_discard_user_hints`).** This
  session's own lesson: I wrote pages of theory that SA3 raw-dim edits would be off-manifold noise; the
  user tested and it "works absolutely and wonderfully — that was all hallucinated theory." **When the user
  names a cause/location, start and verify THERE empirically.** Never fabricate a demo/audition.
- **Layout constraints are LITERAL (`feedback_layout_constraints_literal`,
  `feedback_simplify_layout_not_cram`).** "2 rows" = 2 side-by-side pairs, NOT 4 stacked. Mirror back an
  ASCII sketch + confirm before building. If UI doesn't fit → SIMPLIFY (fewer rows), don't clamp/scale.
- **Commit cadence (`feedback_commit_cadence`, `feedback_no_softcheckpoints_no_slop`):** plan approved + go
  → execute and commit each single-concern unit immediately, no per-step confirmation, no milestone/recap
  reports.
- **Direct on main (`feedback_no_branches_pr`):** NO feature branches, NO PRs. The user commits to main
  **concurrently** (`feedback_parallel_commit_hazard`) → always verify HEAD before/after; **stage only your
  specific files, NEVER `git add -A`**; never touch his uncommitted files.
- **Never `rm` / `git reset --hard` / `git clean` on user files (`feedback_no_destructive_rm`)** — cost was
  ~30 min irretrievable work.
- **Run commands yourself (`feedback_no_terminal_dictation`)** — never tell the user to paste commands.
- **Verification is mandatory (`feedback_verification_is_mandatory`, CLAUDE.md):** after every code change,
  spawn an adversarial verification agent (model: opus) — "This code has a bug. Find it." Never skip it to
  "save tokens."
- **Audition DSP by ear BEFORE commit (`feedback_audition_dsp_before_commit`):** render a WAV and listen;
  review agents confirm intent, not quality. WAV tiles aren't playable in-client → also `open` the folder
  (Finder QuickLook) + give on-disk paths (`feedback_audio_delivery`).
- **Omit rather than ship mediocre (`feedback_quality_bar_omit_mediocre`).**
- **Delegate defined coding to a Sonnet subagent (`feedback_delegate_coding_to_sonnet`);** Opus writes the
  spec + verifies the diff.
- **Plugin UI text = English only (`feedback_synth_ui_english_only`);** labels self-explanatory, not
  cryptic-clever. Non-ASCII in JUCE UI → `fromUTF8`/`CharPointer_UTF8`; multiplier labels ASCII "2x"
  (`feedback_juce_nonascii_strings`). "Amt" in UI / "Amount" in DAW param names, never "Depth"
  (`feedback_label_amt_not_depth`).
- **No sycophancy (`feedback_no_sycophancy`); no AI-slop/marketing** — name the signal path, source-
  grounded, honest. Don't ask questions answerable from the user's own description
  (`feedback_no_obvious_questions`).
- **Kill-check before building (`feedback_kill_check_before_building`):** grep existing code in the same
  subsystem + reason value/redundancy FIRST. (This handover IS that kill-check for the DCO — the entire
  playback + delivery + LLM spine already exists; the DCO is a producer, not a rebuild.)

---

## 10. Build, verify, references

**Build (always `build_clean/`, Release):**
```bash
cmake -S . -B build_clean -DCMAKE_BUILD_TYPE=Release
cmake --build build_clean --config Release -j$(sysctl -n hw.ncpu)
```
Artifacts: `build_clean/T5ynth_artefacts/Release/{Standalone,VST3,AU}/`. Dev backend runs from
`backend/pipe_inference.py` via the project `.venv` — no PyInstaller during iteration (that's release-only).

**Verify each change:** build clean → adversarial verification agent (opus, "find the bug") → audition WAV
if DSP → check JUCE destruction order / audio-thread safety / the held-note-follow invariant → commit.

**Key files (absolute quick-jump):**
- Engine: `src/dsp/WavetableOscillator.{h,cpp}` (masterOsc; `generateMipLevels` private injection point
  `:240`; `extractContiguousFrames` `:660`; `morphToFramesFrom` `:166`). `src/dsp/WavetableBank.*` = DEAD,
  ignore.
- Voices: `src/dsp/SynthVoice.{h,cpp}` (`osc` `:101`, WT render branch `SynthVoice.cpp:908-942`),
  `src/dsp/VoiceManager.{h,cpp}` (`distributeWavetableFrames` `:736-746`).
- Processor: `src/PluginProcessor.{h,cpp}` (`masterOsc` `:352`; `loadGeneratedAudio` `:4381`, core
  `:4630-4662`; `samplerReprepareThread` `:355`; engine-mode map `:2646-2665`).
- IPC: `src/inference/PipeInference.{h,cpp}` (`Result` `:106-117`; `generate` `:865`; `interpret` `:1165`).
- Backend: `backend/pipe_inference.py` (`run_author_instruct` `:1205-1233`, `run_instruct` `:1101-1202`; `load_pipeline` `:1527`; `mode`
  dispatch `:3538+`; `send_text` `:3430-3443`).
- UI: `src/gui/PromptPanel.{h,cpp}` (Advanced surface; `defaultParamsFor` `:1848-1857`; font-scale
  `:51-88`), `src/gui/MainPanel.{h,cpp}` (T5 OSCILLATOR card, `oscModeToggle`), `src/gui/GuiHelpers.h`
  (`SliderRow` `:858`, switchbox `:225-266`).

**Docs to read:** `ARCHITECTURE.md` (§4 Voice chain / Engine data distribution, §6 Inference IPC);
`docs/IPC_PROTOCOL.md`; `docs/ADDING_A_MODEL.md` (structure accurate, line numbers stale — see §5.6);
`docs/ADDING_A_MODULATION_TARGET.md`; `docs/PERFORMANCE_GUIDE.md`; `docs/PRESET_FORMAT.md`;
`docs/HANDOVER_OSC_EASY_MIGRATION.md` (the UI-migration companion — §4.3 there == Slice 0 here).

**Relevant memories:** `project_osc_easy_migration`, `project_easy_migration`, `project_held_note_live_follow`,
`project_critical_aesthetic_mission`, `project_philosophy_a_b_equality`, `project_lco_author_model_gemma4_gguf`,
`project_inference_architecture`, plus the working-style `feedback_*` set (§9).
