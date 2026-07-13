# Grounding semantic axes for a new model

How to build usable semantic-axis sliders (the `AxesPanel` bipolar faders) for a
newly added generation model, without re-deriving the method each time. This is
the recipe that produced SA3's three shipped axes (`rhythmic_sustained_sa3`,
`grainy_smooth_sa3`, `dense_sparse_sa3` — commit `1d12ff5e`, 2026-07-13).

Companion research findings live in memory `project_aesthetic_axes_study`; the
runnable scripts are in `tools/sem_axes_research/`. The earlier morph-study
handover (`docs/HANDOVER_AESTHETIC_AXES_SA3.md`) is **superseded** by this doc —
it predates the bundled-pole result.

---

## The mechanism being grounded

A semantic axis injects a direction into the conditioning embedding:

```
direction = mean_pool( encode(pole_b) - encode(pole_a) )      # [1, 1, 768]
manipulated += direction * value * axes_amount
```

Applied token-wise in `_apply_semantic_axes` (`backend/pipe_inference.py`), poles
looked up from `SEMANTIC_AXIS_POLES`. On the shipping path the push is then
confined to real (non-padding) tokens by `_mask_pad` — the learned padding is
restored so the off-manifold push doesn't swamp the sentinel.

## The failure the method fixes

**Single-word poles are near-inert on the masked path.** `encode("rhythmic") -
encode("")`, mean-pooled over mostly-padding and under-scaled, is a tiny vector:
`corr-to-base ~0.999` at `t=1.0`. This is a **source** weakness, not a masking
one — masked and unmasked single-word pushes are byte-identical and both inert.
The prior PCA / single-word-axis research read this as "SA3 barely responds to
structural abstractions," which was half-true and half a measurement artifact.

---

## The recipe

### 1. Bundle synonyms into each pole
Not one word — a **stack**: `"a rhythmic, percussive, syncopated, pulsing,
staccato, beat-driven groove"` vs `"a sustained, continuous, persistent, held,
unbroken, droning tone"`. More real tokens ⇒ the mean-pooled direction survives
the mask. Measured: bundle separates ~7× better than a literal single concept
(Cohen's d **6.62** vs **0.97** on pulse). Script: `bundle_poles.py`.

### 2. Check seed-stability (a selection criterion, NOT a strength bar)
Render each pole at N seeds; a pole worth shipping renders its character
seed-stably. Metric: `spec_consistency` = mean pairwise correlation of
log-spectra across seeds (rhythmic bundle **0.914**). If a pole's character is
seed-dominated it can't be a reliable dial. **Do not** use "beats a seed change"
as a strength test — seed variance ≫ steering is normal; stability is only how
you *pick* good poles. Script: `bundle_poles.py`.

### 3. Measure with features that can SEE the effect
The prior "no rhythm" null came from measuring onset **count** / tempo
(`librosa.beat_track`), which is blind to periodicity. Use instead (numpy-only,
in the scripts):
- **pulse clarity** — peak of the normalized onset-envelope autocorrelation in
  the 40–300 BPM band → rhythm/periodicity.
- **crest factor** — peak/RMS → transient sharpness.
- **regularity** — `1/(1+CV)` of inter-onset intervals → evenness of hits.

For `rhythmic↔sustained` the bundle moves pulse **d=+6.62** and crest **d=+2.75**
— but onset **rate** (the prior research's only rhythm feature) **d=−0.66**: the
one measure they used is the one that doesn't move. `spectral_flatness` is
**unusable** on SA3 output (collapses to ≈0.005 for everything, including
glass-breaking) — read noisiness via ZCR + spectral centroid instead.

### 4. Inject and test on the SHIPPING (masked) path
It is not enough for a bundled pole to work unmasked — it must survive
`_mask_pad`, the default. Scripts: `bundle_inject.py`, `bundle_inject_rhythm.py`,
`bundle_mask_test.py`.

### 5. Decide per axis: masked-OK vs needs-unmask
The mask is **effect-specific**, neither globally harmless nor globally fatal:
- **Structural / periodicity effects survive the mask** — few real tokens carry
  them. Rhythmic on a low-periodicity base (hum) went pulse `0.19 → 0.56` masked
  ≈ `0.64` unmasked at `t=1.2` (vs `0.37` for single-word masked). → ship as a
  normal masked axis, no flag.
- **Fine attack sharpness needs the full 256-position coverage** the mask
  removes. Transient went crest `4 → ~6` masked (flat over `t`) vs `→33`
  unmasked. → needs the `unmask_manipulation` path (or an injection that doesn't
  restore padding). This is why SA3 shipped rhythm/grain/density, not transient.

Injection also needs base headroom: a base that is already periodic (most SA3
drones) shows little rhythmic movement — choose low-periodicity bases to see it.

### 6. Audition by ear (the deciding step)
Build a listening page and listen: base vs dose, masked vs unmasked, with each
cell's feature number recomputed from its own WAV so ear and metric stay
consistent. Script: `gen_masked_and_rebuild.py` → `listening_final.html`.

---

## The caveat that must stay in view (critical-aesthetic mission)

The pole-pushes are **potent but not acoustically label-faithful**: pushing
"toward tonal" changes the sound a lot without making it measurably more tonal
(|Cohen's d| ≤ 0.15 for the named feature across prompts; a weak-but-real shared
direction has 67–77% of prompts moving the same way). A concrete label therefore
names a **gesture**, not a guaranteed acoustic outcome. An abstract-gesture label
set was trialled for exactly this reason and reverted (concrete labels "machen
oft Sinn" on real presets) — see memory `project_axis_abstract_labels_reserve`.

---

## Per-model generation params

SA3 (`stable-audio-3-medium`): **8 steps, cfg 1.0, pingpong sampler** (its model
card; forced in `pipe_inference.py`). Hold the **seed fixed** to isolate the axis
from seed scatter, and run the backend **serial** for research — the GPU backend
seeds per-request and **races under concurrency** (identical input → different
output across workers). ~6 s duration gives rhythm room to be audible.

## Backend hooks the scripts use

| request field | role |
|---|---|
| `semantic_axes: {key: value}` | the **shipped** path (poles from `SEMANTIC_AXIS_POLES`) |
| `semantic_axes_poles: {key: (a,b)}` | **research-only**: inline custom (bundled) poles without editing the shipped dict |
| `unmask_manipulation: true` | **research-only**: apply the push to all 256 positions (reproduces the Fedora "Latent Lab" unmasked push) |

Both research fields are inert on the shipped path (the plugin never sets them);
`_apply_semantic_axes(..., custom_poles=None)` and `unmask_manipulation=False`
are byte-identical to shipping.

## Adding a model's axes — checklist

1. Pick 2–4 candidate bipolar directions; write **bundled** poles for each.
2. `bundle_poles.py` → keep the seed-stable ones that separate on the right feature.
3. `bundle_mask_test.py` → classify each as masked-OK or needs-unmask.
4. Audition (`gen_masked_and_rebuild.py`), decide by ear.
5. Add keys to `SEMANTIC_AXIS_POLES`; add a per-model table in `AxesPanel.cpp`
   with **globally-unique, never-reused** dropdown ids (SAO 2-9, SA3 10-12, …) —
   ids are persisted in presets, so a new model gets new ids, never a renumber.
   No preset-format version bump: ids are plain ints in the existing
   `semanticAxes` metadata (see `docs/PRESET_FORMAT.md`).
