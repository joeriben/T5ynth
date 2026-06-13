# CLAP follow-up: curated vocabulary + controlled drift sweep

- model: `laion/larger_clap_music_and_speech`

## A — curated timbre vocabulary, pruned for separability

Hand-curated timbre/affect register (146 terms) greedily pruned (max-min / k-center on CLAP text embeddings) to 64 maximally-separated terms.

| vocab | #labels | redundancy | %pairs>0.9 |
|---|---:|---:|---:|
| curated (full) | 146 | 0.421 | 0.00 |
| curated (pruned) | 64 | 0.306 | 0.00 |

**Pruned vocabulary:** resonant, explosive, clicky, percussive, wobbling, metallic, smooth, distant, consonant, overdriven, granular, fizzy, synthetic, ringing, heavy, bowed, biting, dissonant, coarse, roomy, ceramic, gliding, evolving, static, detuned, alien, breathy, sustained, melancholic, papery, digital, liquid, anxious, fluttering, lo-fi, rubbery, atonal, crystalline, crisp, plucked, inharmonic, churning, microtonal, shrill, robotic, scratchy, degraded, warm, velvety, saturated, vocal, reverberant, glassy, piercing, analog, relaxed, polished, buzzy, grainy, euphoric, dull, throbbing, punchy, mechanical

**Sample of dropped near-synonyms:** bright, dark, brilliant, muffled, airy, mellow, murky, thin, fat, light, hollow, full, boomy, deep, shallow, massive, dense, sparse, rough, gritty, fuzzy, clean, noisy, silky, sandy, wooden, watery, plastic, stony, icy

### Re-ranked clips (pruned vocab)

**`original.wav`** — bowed (0.367), vocal (0.362), consonant (0.312), dull (0.296), warm (0.277), breathy (0.265)

**`anchor_family.wav`** — plucked (0.281), overdriven (0.235), synthetic (0.216), roomy (0.211), saturated (0.21), polished (0.209)

**`sigma0.050_iter20.wav`** — synthetic (0.453), grainy (0.444), piercing (0.429), alien (0.415), glassy (0.389), robotic (0.386)

**`sigma0.300_iter15.wav`** — dull (0.427), rubbery (0.388), vocal (0.366), alien (0.358), roomy (0.335), consonant (0.332)

**`test_sample.wav`** — overdriven (0.38), heavy (0.287), metallic (0.256), explosive (0.242), churning (0.226), distant (0.209)

## B — controlled drift sweep (cosine of each resynth to `original.wav`)

All clips are resynths of the same source. Per `RESYNTH_CALIBRATION_FINDINGS.md`, LOW sigma = strong evolution (drifts far), HIGH sigma = wash-out (stays near). A faithful CLAP ear should show cosine FALLING across iterations at low sigma and the iter-20 value RISING with sigma.

| sigma | iter01 | iter03 | iter05 | iter10 | iter15 | iter20 |
|---|---:|---:|---:|---:|---:|---:|
| 0.050 | 0.910 | 0.729 | 0.514 | 0.308 | 0.328 | 0.311 |
| 0.100 | 0.909 | 0.743 | 0.532 | 0.323 | 0.295 | 0.279 |
| 0.150 | 0.922 | 0.726 | 0.670 | 0.480 | 0.476 | 0.447 |
| 0.200 | 0.940 | 0.718 | 0.626 | 0.628 | 0.676 | 0.573 |
| 0.300 | 0.926 | 0.859 | 0.783 | 0.853 | 0.640 | 0.582 |
| 0.400 | 0.943 | 0.877 | 0.717 | 0.600 | 0.573 | 0.570 |
| 0.475 | 0.950 | 0.928 | 0.698 | 0.424 | 0.249 | 0.216 |
| 0.500 | 0.952 | 0.940 | 0.936 | 0.908 | 0.363 | 0.301 |

**Converged distance (iter20 cosine to original) vs sigma** — should rise monotonically (more wash-out = nearer original):

| sigma | 0.050 | 0.100 | 0.150 | 0.200 | 0.300 | 0.400 | 0.475 | 0.500 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| cos | 0.311 | 0.279 | 0.447 | 0.573 | 0.582 | 0.570 | 0.216 | 0.301 |
