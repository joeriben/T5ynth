# CLAP follow-up: curated vocabulary + controlled drift sweep

- model: `laion/clap-htsat-unfused`

## A — curated timbre vocabulary, pruned for separability

Hand-curated timbre/affect register (146 terms) greedily pruned (max-min / k-center on CLAP text embeddings) to 64 maximally-separated terms.

| vocab | #labels | redundancy | %pairs>0.9 |
|---|---:|---:|---:|
| curated (full) | 146 | 0.284 | 0.00 |
| curated (pruned) | 64 | 0.212 | 0.00 |

**Pruned vocabulary:** throbbing, percussive, violent, smeared, thin, roomy, wooden, light, dissonant, melancholic, gliding, robotic, vocal, papery, distorted, staccato, coarse, dull, watery, snappy, analog, dark, sustained, ringing, airless, stuttering, serene, clean, plucked, alien, explosive, ceramic, consonant, subtle, rubbery, caressing, polished, anxious, raw, degraded, glitchy, fizzy, gritty, hi-fi, bowed, intimate, warbling, rough, glassy, fluttering, legato, distant, gentle, aggressive, playful, punchy, boxy, frantic, shallow, fat, granular, static, hollow, sandy

**Sample of dropped near-synonyms:** bright, brilliant, muffled, airy, piercing, mellow, shrill, crisp, murky, heavy, full, boomy, deep, massive, dense, sparse, smooth, grainy, fuzzy, noisy, scratchy, silky, velvety, metallic, plastic, crystalline, stony, liquid, icy, molten

### Re-ranked clips (pruned vocab)

**`original.wav`** — violent (0.508), aggressive (0.448), anxious (0.353), rough (0.343), gliding (0.303), caressing (0.282)

**`anchor_family.wav`** — percussive (0.286), boxy (0.265), fizzy (0.245), snappy (0.244), glitchy (0.231), static (0.19)

**`sigma0.050_iter20.wav`** — robotic (0.425), alien (0.405), glitchy (0.384), sandy (0.379), boxy (0.352), granular (0.348)

**`sigma0.300_iter15.wav`** — violent (0.616), aggressive (0.533), vocal (0.396), anxious (0.375), degraded (0.292), consonant (0.271)

**`test_sample.wav`** — punchy (0.336), explosive (0.251), fat (0.239), distorted (0.227), rough (0.217), dark (0.135)

## B — controlled drift sweep (cosine of each resynth to `original.wav`)

All clips are resynths of the same source. Per `RESYNTH_CALIBRATION_FINDINGS.md`, LOW sigma = strong evolution (drifts far), HIGH sigma = wash-out (stays near). A faithful CLAP ear should show cosine FALLING across iterations at low sigma and the iter-20 value RISING with sigma.

| sigma | iter01 | iter03 | iter05 | iter10 | iter15 | iter20 |
|---|---:|---:|---:|---:|---:|---:|
| 0.050 | 0.940 | 0.769 | 0.518 | 0.159 | 0.129 | 0.111 |
| 0.100 | 0.928 | 0.776 | 0.558 | 0.223 | 0.198 | 0.290 |
| 0.150 | 0.924 | 0.784 | 0.726 | 0.391 | 0.325 | 0.284 |
| 0.200 | 0.921 | 0.841 | 0.766 | 0.744 | 0.718 | 0.741 |
| 0.300 | 0.932 | 0.862 | 0.825 | 0.824 | 0.674 | 0.636 |
| 0.400 | 0.900 | 0.839 | 0.541 | 0.483 | 0.529 | 0.534 |
| 0.475 | 0.901 | 0.906 | 0.663 | 0.548 | 0.201 | 0.161 |
| 0.500 | 0.892 | 0.875 | 0.880 | 0.904 | 0.323 | 0.299 |

**Converged distance (iter20 cosine to original) vs sigma** — should rise monotonically (more wash-out = nearer original):

| sigma | 0.050 | 0.100 | 0.150 | 0.200 | 0.300 | 0.400 | 0.475 | 0.500 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| cos | 0.111 | 0.290 | 0.284 | 0.741 | 0.636 | 0.534 | 0.161 | 0.299 |
