# `ep_fm3` — the A/B, and what the measurements say

Authoring rule 3: an entry is done when BJ has heard it against the closest thing the lexicon
already has, on disk. Rule 4: the incumbent is rendered **before** the new entry's first
orchestra line — `nearest_existing_metallic_fm.wav` was written at 03:07 on 2026-07-31, the
entry after it.

| file | what it is |
|---|---|
| `nearest_existing_metallic_fm.wav` | **the incumbent.** `metallic_fm` at its defaults — the nearest thing the lexicon has: the same FM family, struck, drive decaying, and BJ has heard it |
| `new_ep_fm3.wav` | `ep_fm3` at its defaults |
| `real_reference_sa3_seed{11,22,33}.wav` | SA3, „solo electric piano …" — required because this entry takes a real instrument's name (`feedback_compare_against_real_reference`). Protocol and prompt in `REFERENCE_PROMPT.txt` |

Both renders are 220 Hz, 5.51 s, **attack RMS matched** (the level the gate reads for a body
that declares `; DECAY: SELF`) and written at one shared gain, so nothing about the level is
per-file. Envelope, dB from the attack at 0 / 1 / 3 / 5 s:

    metallic_fm   -0.0  -3.4  -1.2  -10.0     (stands and beats; the dip is its two bodies)
    ep_fm3        -0.0  -0.8  -5.9  -11.2     (falls away)

The parameter page, where every axis is reachable and every value is on screen, is
`tools/lco_listening/ep_fm3_params/index.html` — `tine` × `ting` crossed, the other four swept.

## The open defect, stated before anything good about the entry

**At long `decay` settings this body still SWELLS, by up to 1.99 dB, and its own
`; DECAY: SELF` declaration is therefore false at those settings.** Over 270 sampled corners,
16 of them rise more than 1 dB before falling; the worst is `decay` 0 / `hollow` 1.0 /
`ting` 0.5 / `ring` 0. At the shipped defaults it does not happen (−0.8 dB at 1 s, −11.2 at 5).

**Why, diagnosed and not guessed.** A `foscili` at a carrier:modulator ratio of 1:1 or 1:2 has
sidebands that fold through DC and interfere with the upper ones, so its LEVEL is a function of
its index: measured at amp 0.30 and 220 Hz, 1:1 reads +0.24, −0.43, −2.88, −1.49, +0.01 dB
against a sine at index 0.5, 0.9, 1.5, 3.2, 4.2. An index envelope on such a pair is therefore
also an amplitude envelope — and at `decay` 0 the declared fall is only 0.77 dB/s, slow enough
for that rise to outrun it. All three pairs sit on the same carrier frequency, so this is
structural rather than a slip.

**Three corrections were made and each is in the body's comments with its measurement.** The
body's index swing was cut from 3.4× to the corpus's own near-modulator depth (envelope level
99 → 93, not 99 → 29), which took the worst swell from 4.55 dB to 2.87; the second body pair
was put in quadrature, which took it to 1.99 and took `hollow`'s level spread from 0.76 dB to
0.36 and `strike`'s from 1.76 to 1.16. **A fourth round would move the worst corner again
rather than remove it**, which is the signature of a construction that is wrong and not of a
bug to squash, so it was not attempted (authoring rule 5).

`lco_axis_probe --gate` does not see this at all — its swell candidate is a linear fit from
t = 1 s, so a rise peaking at 1.3 s and then swamped by the decay reads to it as a fall. Its
silence is not evidence.

## The second open defect: `tine` dies at the top of the keyboard

Measured, five values of `tine` (6, 10, 14, 22, 28) rendered at one pitch:

    220 Hz  5 of 5 different      2000 Hz  2 of 5
    880 Hz  4 of 5                2860 Hz  1 of 5 — the axis is gone
    1530 Hz 3 of 5

It was worse: the ratio was capped at half the available band, which reserved room for an index
of 1 that `ksct` already handles, and the axis was completely dead from 1530 Hz. Capped at the
band it dies at about 2860 Hz, which is where physics puts it — a 28th partial of 2860 Hz is
80 kHz. Above that the entry has five axes, not six, and the param note should say so in those
words.

## What the gate still fails

- **one loudness: 4.70 dB across the corners** against a 1.00 dB bound (read before the
  quadrature change; the per-axis spreads at 220 Hz are now `tine` 0.13, `ting` 0.43,
  `hollow` 0.36, `ring` 0.76, `strike` 1.16 dB, so this number will have fallen).
- **one loudness at every register: 4.81 dB** against 3.00 dB, likewise.

## What holds

`tine` 0.13 dB and `ting` 0.43 dB across their whole ranges. Movement at every corner and
register the gate checks. No arithmetic hazard anywhere: 256 endpoint corners × {20 Hz,
12000 Hz} × {44100, 176400} plus 768 interior corners gave zero errors, zero NaN, zero silent
renders. No i-rate reads, so the `i(kfreq)`-is-0-before-the-first-note trap does not apply.

**PASS is not the point and neither is FAIL.** The probe's own words: *„PASS means these
thresholds were satisfied. It is not a statement about the sound, and it does not stand in for
BJ having heard the entry."*
