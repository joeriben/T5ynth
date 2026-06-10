# Resynth loop calibration — empirical findings

Harness: `tools/test_resynth_loop.py` (reuses `test_init_audio.py`'s IPC client,
so the wire path is byte-identical to the plugin). Model
`stable-audio-3-small-music`, 8 steps, cfg 1.0, 3.0 s, **alpha −1.0** (the
plugin's single-prompt "pure A" guard — see below). Source `warm analog bass
drone` (rms 0.365, centroid 199 Hz). Four runs, each a σ sweep × 20 feedback
iterations, feeding every output back as the next `init_audio`.

Primary metric: **timbre_corr** = Pearson of the time-averaged log-magnitude
spectrum (alignment-invariant timbre similarity). The user calibrates by ear;
waveform correlation under-reports timbral stickiness. `finA` = timbre_corr of
the iter-20 output to the original.

## 0. A prerequisite bug the test exposed (in the *harness*, not the plugin)

At first every prompt produced identical audio (two opposite prompts →
timbre_corr **+1.000**). Cause: the backend's linear blend is
`(0.5−0.5α)·A + (0.5+0.5α)·B`, and for an absent prompt B it synthesizes
`2·null − A`. With the default **α = 0** this collapses to `null` =
*unconditional* — the prompt does nothing. The plugin avoids this with a guard
(commit 298faa90: lone prompt → α pinned to ±1). Sending **α = −1.0** restored
conditioning (bass centroid 199 Hz vs chimes 7095 Hz; timbre_corr −0.013). All
results below use the correct α. *This is verification that the plugin path is
right and the harness now matches it — not a plugin bug.*

## 1. What 100 % means, and what the loop does at each setting

Same-prompt loop (consistent usage), fixed seed (the `randomSeedToggle=false`
default). The loop **converges at every σ** (spec_prev→0.99 by iter 1–3) — it is
not chaotic. `finA` measures how far the converged attractor sits from the
original:

| detent (current map) | σ | finA | reading |
|---|---|---|---|
| 100 % | 0.05 | 0.37 | **strong evolution** — morphs into a *related* drone |
| 75 % | 0.16 | 0.63 | moderate evolution |
| 50 % | 0.28 | 0.79 | light evolution |
| 25 % | 0.39 | 0.92 | ≈ stays at a fresh prompt-gen (no evolution) |
| 5 % | 0.48 | 0.94 | ≈ stays at a fresh prompt-gen (pure wash-out) |

**100 % = maximum self-resynthesis**: each round re-grows mostly from the
previous output (σ 0.05 → only 5 % fresh noise), so drift accumulates and the
sound evolves into a related-but-distinct member of the prompt's family, then
settles. It is coherent, not degenerate.

The evolution response **saturates below σ≈0.12** (σ 0.05 and 0.10 both give
finA≈0.35) and **vanishes above σ≈0.40** (finA≥0.92). The discriminating band is
σ 0.12–0.35. A 5-detent slider can therefore resolve only ~3 distinct evolution
levels — and the current linear map spends them on the top three detents, where
resolution matters, leaving the bottom two as a (harmless) wash-out plateau.

## 2. The 5 % wash-out requirement — MET

Cross-prompt loop: original from prompt A (bass), loop driven by prompt B
(chimes); A↔B baseline timbre −0.013 (well separated). "A-gone" = first iter the
bass timbre falls to the A↔B floor.

| σ | resynth (current map) | A-gone @ (fixed / rand) |
|---|---|---|
| 0.30 | 44 % | >20 / >20 |
| 0.35 | 33 % | 11 / >20 |
| 0.40 | 22 % | 12 / 16 |
| 0.475 | 5 % | **6 / 5** |
| 0.50 | 0 % | 4 / 6 |

At the **current 5 % setting (σ 0.478) the old audio washes out by iter ≈ 6** —
comfortably inside the 20-iteration budget. Wash-out within 20 iters needs
σ ≳ 0.40 (resynth ≲ 22 %); below that the loop *holds* the source. This session's
earlier remap (`0.30−0.25·a` → `0.50−0.45·a`, floor σ 0.288 → 0.478) is what
fixed the user's "5 % holds for x bars" complaint: the old floor (σ 0.288) never
washed out within 20; the new floor does, fast.

## 3. The "convergent lower bound"

The loop converges at **every** σ in consistent-prompt use — there is no σ at
which it fails to settle. The meaningful lower bound is therefore not a
*coherence* limit but a *function* boundary: σ ≈ 0.40 (resynth ≈ 22 %) is where
"evolve/hold" gives way to "wash-out". The slider's floor (σ 0.478) sits just
below the σ 0.50 dead zone, where `init_audio` is ignored entirely
(test_init_audio: σ 0.50 → corr-to-source 0.03). So 5 % is **not** rigid/
nonsensical — it is the principled fast-wash-out floor, one notch above the
dead-zone cliff.

## Calibration verdict

**The σ mapping `init_noise_level = 0.50 − 0.45·resynthAmount` is empirically
validated; no change improves it.** Endpoints are optimal (σ 0.05 = max coherent
evolution; σ 0.478 = fast wash-out without falling off the dead-zone cliff), and
linear allocation places the model's ~3 resolvable evolution levels on the three
top detents. A non-linear curve only moves the unavoidable saturation-redundancy
from the (harmless) wash-out end to the (useful) evolution end — strictly worse.

## Two real, separate quality findings (NOT σ-mapping; flagged for decision)

1. **Energy/brightness drift** ("loop attractor"). Even same-prompt, the loop
   loses energy and brightens each round before settling (σ 0.05: rms 0.36→0.19
   = −47 %, centroid 199→446 Hz). The evolved sound thins out. A fix would be an
   RMS-match of each fed-back `init_audio` to the previous output — a new step in
   the C++ feedback path, out of scope for σ calibration.
2. **Seed mode shapes evolution quality.** Random seed (`randomSeedToggle=true`,
   "Automatic variation") evolves *more gently and cleaner*: finA 0.55 vs 0.37,
   rms× 0.67 vs 0.53, cen× 2.07 vs 2.25 at σ 0.05. The fixed-seed default wanders
   further but degrades more (it blends the identical noise every round). Whether
   the standing resynth loop should use a fresh seed per iteration is a behavior
   choice worth surfacing.

Artifacts (audio + per-iteration JSON) live under `tools/resynth_loop_out/` —
gitignored (164 MB of WAVs); regenerate with the harness. Each of `cross_fixed/`,
`cross_rand/`, `same_fixed/`, `same_rand/` holds `original_A.wav`,
`destination_B.wav`, `sigma*_iter*.wav`, `manifest.json`, `summary.json`.
