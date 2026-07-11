# Handover — Aesthetic-Axes research on SA3 (copy the proven SAO design)

**Status:** ready to execute; **do not build on the box `axis_char`/`audit_t5y` data** (wrong
paradigm — see §6). Written after a session that produced no usable result by inventing new
designs instead of copying the one that already worked. Every claim below is marked
**[verified]** (read from the artifact/measured this session) or **[open]** (must be checked).

---

## 1. Goal

Characterise **aesthetically meaningful axes** for the current engine (**SA3**), by copying the
research design that already ran successfully on **Stable Audio Open / SA Small 1.0**, changing
**only the injected prompts** (better contrasting pairs). The evaluation is **by ear**, not by a
CLAP/AUC metric.

---

## 2. The proven design — the thing to copy [verified]

**File:** `tools/render_latent_morph_compare.py` @ commit `851cb2b3`
("tools: add embedding-vs-latent morph listening-study helper"). Docstring: *"listening-study
helper for Stable Audio Open 1.0."* Extracted verbatim to
`…/scratchpad/render_morph_sa3.py` (602 lines) during this session.

It is an **embedding-vs-latent morph listening study**. Per contrasting **A/B prompt pair** ×
per **seed** it renders, over the IPC backend (`PipeClient` → `backend/pipe_inference.py`):

| file | family | request |
|---|---|---|
| `00_endpoint_a.wav` | endpoint | `{prompt_a, cache_as: <a>}` |
| `01_endpoint_b.wav` | endpoint | `{prompt_a: prompt_b, cache_as: <b>}` |
| `02_control_decode_a.wav` | control | `{mode: "decode_cached", latent_name: <a>}` |
| `10_embedding_alpha_*.wav` | embedding morph | `{prompt_a, prompt_b, alpha}` (prompt-space LERP/**extrapolation**) |
| `20_latent_alpha_*.wav` | latent morph | `{mode: "interpolate", latent_a:<a>, latent_b:<b>, lerp_alpha}` |

Defaults [verified]: seeds `101,202`; embedding alphas `-0.5, 0.0, 0.5` (note the extrapolation
at ±0.5 past the endpoints); latent alphas `0.25, 0.5, 0.75`; duration `4.0s`; steps `20`; cfg
`7.0`; model `stable-audio-open-1.0`. All are `argparse` flags
(`--model --pair --seeds --embedding-alphas --latent-alphas --duration --steps --cfg
--skip-control-decode --dry-run --list-pairs`).

**Outputs:** WAV tree + `manifest.json` + **`listening_sheet.csv`** + `README.txt`. The listening
sheet has **empty rating columns to be filled in by ear**: `eigenstaendigkeit, kontinuitaet,
prompttreue, brauchbarkeit, direkturteil, notizen`. The README poses the listening questions
(is the latent morph audibly distinct from the embedding morph? a useful intermediate space?
still anchored between A and B? worth keeping as an artistic mode?). **This is the paradigm: a
by-ear comparison, not a metric.**

---

## 3. The improvement — injected prompts [verified source]

Replace the 6 generic pairs in `PAIR_LIBRARY` with contrasting pairs built from the **real
T5ynth preset prompts** (source of truth: the 58 preset prompts in `prompt_pool.json`, local
scratchpad + on the box; do **not** reconstruct prompts from memory). Proposed set
(copy-pasteable), ~10 tippable aesthetic opposites:

```python
PAIR_LIBRARY = [
    PromptPair("choir_demons",  "soothingly soft heavenly choirs", "demons singing in hell"),
    PromptPair("beauty_evil",   "pure beauty", "pure evil"),
    PromptPair("calm_crash",    "calm waves",
               "Huge waves harshly crashing against the shore of the bay, hard rains are falling."),
    PromptPair("hum_glass",     "ethereal hum resonance, c3", "glass breaking, sledge hammer, c3"),
    PromptPair("birds_techno",  "100 birds singing: Corvus brachyrhynchos, Larus argentatus, "
               "Passer domesticus, Melospiza, Starlings", "fast techno 909 drumtrack, 140bpm"),
    PromptPair("frost_funk",    "glowing frost blankets ancient forest, c3", "FUNKY res BASS LINE, c3"),
    PromptPair("bells_gritty",  "silver bells, c3, 140bpm", "gritty BASS, c3"),
    PromptPair("dreamy_alien",  "dreamy dream, c3", "alien spaceship landing"),
    PromptPair("ghostly_samba", "ghostly voices", "a samba group"),
    PromptPair("saw_kalimba",   "steady saw wave, c3", "hysterically laughing kalimba, c3"),
]
```

These are a proposal, not sacred — the point is contrasting, kipp-fähige pairs from the preset
world. Structure of the script otherwise **byte-identical** to the original.

---

## 4. Porting to SA3 — the two real constraints [verified]

1. **Latent-morph may not run on SA3.** `mode: "interpolate"` and `mode: "decode_cached"` require
   `hasattr(pipe, "vae")` and reject `AudioLDM2Wrapper`
   (`backend/pipe_inference.py:3398-3405`, `interpolate_and_decode`/`decode_cached` at
   `:1825`/`:1853`). SA3 runs the **native** path (`_generate_native`, `sampler_type="pingpong"`,
   `if model_name.startswith("stable-audio-3")`, `pipe_inference.py:1446-1449`). Whether the SA3
   native pipe exposes `.vae` is **[open]** — check empirically. If it does not, the `20_latent_*`
   and `02_control_decode` clips will raise; the **endpoints + `10_embedding_*` morphs still run**
   (they go through `generate`, not the latent path). Report the latent-morph limitation honestly
   rather than working around it — and note the honest alternative "latent" comparator for SA3 is
   the **Dim Explorer / PCA** mechanism (§5), not diffusion-latent LERP.

2. **SA3 native generation params differ from the SAO defaults.** SA3 small = **8 steps, cfg
   1.0, pingpong** per its model card (`pipe_inference.py:1446-1449`). Run with
   `--steps 8 --cfg 1.0`, **not** the script defaults (20 / 7.0), or the output will be
   over-guided/wrong.

3. **Model id:** the plugin status bar shows `stable-audio-3-small-sfx` [verified from user
   screenshot]; confirm against the backend's reported model list (`client.info["models"]`, or
   `--dry-run` which prints available models on mismatch) before the run.

**Where to run:** on the **Mac**, via the T5ynth dev backend (`.venv/bin/python
backend/pipe_inference.py`) — the SA3 model is installed there (the plugin uses it). The Fedora
box harness (`research/t5_interpretability/aesthetic_axes_sa3.py`) is a **different, flawed**
harness (§6) — do not use it for this.

**Suggested first command (after swapping `PAIR_LIBRARY`):**
```
.venv/bin/python tools/render_latent_morph_compare.py \
  --model stable-audio-3-small-sfx --steps 8 --cfg 1.0 --dry-run
# then drop --dry-run for the real run.
```
Note: `--skip-control-decode` skips only the `02_control` clip, **not** the `20_latent_*` morphs.
There is no flag to disable the latent family. If the SA3 native pipe lacks `.vae`, the latent
loop will raise per pair — the minimal honest change is a one-line guard that skips the latent
family when `not hasattr(pipe, "vae")` (confirm with a single latent clip first, before the full
run). The endpoints + `10_embedding_*` morphs are unaffected and are the core deliverable.

---

## 5. Related shipped artifacts (grounding, not to be re-derived) [verified]

- **`backend/services/cross_aesthetic_backend.py`** (CrossmodalLabBackend; first-add `728728ca`,
  latest `e1ccf4ea`; deleted from tree, in history). This is the **runtime** for the axis work:
  - **Semantic axes:** 21 validated + 1 experimental, **framed** poles (`'sound tonal'`/
    `'sound noisy'`, `'ceremonial music'`, …), `NEUTRAL_PROMPT = 'sound'`, each ranked by
    `d` = cosine distance between the encoded poles (tonal_noisy d=4.806 … music_soundscape
    experimental). Delta = `t·(pole − neutral)`, `t∈[-1,1]`.
  - **PCA axes:** data-driven directions of max variance in T5 conditioning space, **fitted on
    392K prompt embeddings**, stored `backend/data/pca_components.pt`; 10 labelled PCs
    (Natural/Synthetic, Sonic/Physical, Musical/Elemental, Textured/Tonal, …). Applied as direct
    768-d directions (`_apply_axes`, axes named `pc1…pcN`).
  - **Ops:** interpolation (LERP A/B), extrapolation (alpha<0 or >1), magnitude, Gaussian noise,
    per-dimension offsets, then `stable_audio.generate_from_embeddings`.
- **`979cc1d4`** "refactor(axes): replace 3 semantic + 6 PCA slots with 3 effective axis slots" —
  confirms PCA axes shipped as real UI slots.
- **`_apply_semantic_axes`** (`pipe_inference.py:1885`) is the **current** shipped semantic-axis
  path: `manipulated + (pole − neutral)·|value|·amount`, token-wise; neutral = `encode("")`
  (**empty string**, `:1903`), poles from `SEMANTIC_AXIS_POLES` (matching `AxesPanel.cpp
  kEffectiveAxes`). Note this differs from the cross_aesthetic research version (empty-string vs
  `'sound'` neutral; effective-3 vs framed-21). [open] which neutral the research design intends —
  the render study (§2) sidesteps this entirely by morphing whole prompts, not pole deltas.
- **Dim Explorer** (`src/gui/DimensionExplorer.*`) and **Semantic Axes** (`AxesPanel.*`,
  `SemanticAxes.*`) are **two separate features**: latent-dimension/PCA manipulation vs framed-pole
  deltas. The Dim Explorer is the **strong** mechanism.

---

## 6. What went wrong this session — do NOT repeat

- **Invented new designs** (`audit_t5y`, `axis_char` in the box harness
  `research/t5_interpretability/aesthetic_axes_sa3.py`) instead of copying §2. Both produced
  nothing usable.
- **`audit_t5y` used raw single-word poles + empty-string neutral** — doubly off the proven
  design (framed poles + `'sound'` neutral, ranked by cosine-d).
- **All WORKERS=8 data is seed-confounded** [verified this session]: the GPU backend's
  per-request global seeding **races** under concurrency (identical input at t=0 → 5 different
  md5s across workers). Any noise-controlled measurement **must run serial (WORKERS=1)**.
- **`axis_char` wasted ~840 of 2880 generations on bit-identical `t=0` duplicates** [verified: 6
  demo prompts × 7 redundant t=0 = 42 dupes in 144 demo wavs]. `t=0` = no push = axis-independent;
  generate the base **once per prompt**, not once per axis.
- **Unfounded claims in both directions.** "potent" then "inaudible" — both refuted. The honest,
  measured picture (waveform level, p000, fixed seed): the `(pole − neutral)` **semantic-axis**
  push is **erratic** — most axes corr ≈ 0.995 with the base (perceptually ≈ identical),
  occasionally large (`rhythmic_sustained +1`: corr 0.17). This is **one prompt's** data; do not
  over-generalise. **`spectral_flatness` is unusable on SA3 output** (collapses to ≈0.005 for
  everything incl. glass-breaking) — use ZCR/centroid if a noisiness proxy is ever needed.
- **Conflated the weak mechanism with SA3's real sensitivity.** The user demonstrated the **Dim
  Explorer** turning `"a sine wave, c3"` into dense noise at a **fixed seed** — SA3 responds
  **massively** to latent-dimension manipulation. The semantic-axis `(pole−neutral)` delta is
  just a timid mechanism; its weakness says nothing about SA3.
- **Core methodological error:** the proven design is a **by-ear morph listening study** between
  contrasting pairs; I built **CLAP/AUC metric studies on the wrong (semantic-axis) mechanism**.

---

## 7. Established facts that carry forward [verified this session]

- Serial generation is **bit-deterministic per seed** (same seed → identical decode; different
  seed → uncorrelated, corr ≈ 0.007). Fix the seed to isolate any manipulation.
- **Seed dominates** the output; the semantic-axis delta is mostly perceptually subtle.
- SA3 responds **massively** to latent/PCA-dimension manipulation (Dim Explorer, user-shown).
- SA3 native generation = pingpong sampler / 8 steps / cfg 1.0.

---

## 8. Artifact & location index

| what | where |
|---|---|
| **Proven design to copy** | `tools/render_latent_morph_compare.py` @ `851cb2b3` |
| Extracted copy (pre-edit) | `…/scratchpad/render_morph_sa3.py` (this session) |
| Runtime axis backend | `backend/services/cross_aesthetic_backend.py` @ `e1ccf4ea` (history) |
| PCA components (392K) | `backend/data/pca_components.pt` (history) |
| Shipped semantic-axis fn | `backend/pipe_inference.py:1885` (`_apply_semantic_axes`) |
| Latent-morph gate | `backend/pipe_inference.py:3398-3405`, `:1825`, `:1853` |
| SA3 native params | `backend/pipe_inference.py:1446-1449` |
| UI | `src/gui/DimensionExplorer.*`, `src/gui/AxesPanel.*`, `src/backend/SemanticAxes.*` |
| Improved prompt source (58 preset) | `prompt_pool.json` (local scratchpad + box) |
| **Flawed box harness — do not build on** | Fedora `…/research/t5_interpretability/aesthetic_axes_sa3.py` + its `axis_char.jsonl`/`audit_t5y` data |

---

## 9. Open questions [open]

1. Does the SA3 native pipe expose `.vae`? → determines whether the latent-morph half runs on SA3
   at all, or whether the Dim-Explorer/PCA path is the right "latent" comparator instead.
2. Is the semantic-axis weakness inherent to `(pole−neutral)`, a too-timid `amount`, or an
   artifact — orthogonal to the render study, only relevant if the semantic-axis feature itself is
   to be re-evaluated.
3. Should the study ALSO copy the **PCA-discovery** half (fit PCA on an improved prompt corpus
   through SA3's t5gemma conditioner → data-driven axes = the Dim Explorer's actual axes)? That is
   the powerful mechanism and arguably the real deliverable.
