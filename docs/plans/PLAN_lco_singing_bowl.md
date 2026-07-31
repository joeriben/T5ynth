# PLAN — `singing_bowl`

Pre-build record, written before the first orchestra line, as `CLAUDE.md` → Instrument Authoring rule 1 requires. Rule 4's comparison was rendered first and is on disk at `tools/lco_listening/singing_bowl/nearest_existing.wav`.

**Attempt budget, fixed here and not revisable later (rule 5): three.** If BJ's ear is not convinced after the third, the entry is dropped rather than optimised further.

## 1. Why this exists — the defect, measured

BJ, 2026-07-29: *„Crystal singing bowls in a cathedral"* comes out extremely dissonant, and *„Wäre gut da eine eigene Parametrisierung zu machen (oder Instrument)"*.

The prompt reaches exactly one entry and nothing else — `named_entries("crystal singing bowls in a cathedral")` → `{'instruments': ['metallic_fm'], 'adjectives': [], 'motions': []}`, via `metallic_fm`'s surface forms `singing bowl` / `klangschale`. No adjective and no motion in that sentence is in the lexicon at all.

`metallic_fm` is a Chladni free circular plate — a gong, and a good one; BJ heard it for over an hour on 2026-07-31 and judged it positively twice. Rendered at its defaults at 220 Hz its strongest partials are

    1.00   3.89   8.76   11.65   16.54   19.41   24.30

with the loudest low partial at **62 Hz under a played 220 Hz**. The body does not carry the note.

**The mismatch is objective, not taste.** `metallic_fm` computes its two ratios from Chladni's law as `1.5^p` and `2.0^p` with p its `stretch` axis, declared range [1.4, 2.4] — *"the published range for cymbals, handbells and church bells"*. A bowl's first two overtones sit at 2.83 and 5.42 (below), which would need p = 2.57 for the one operator and p = 2.44 for the other: **two different exponents, both outside the declared range.** A singing bowl lies outside the physics that entry claims. So this is a new entry, not an axis on that one — and adding a second law to `stretch` would be a knob whose meaning cannot be stated, which rule 2 forbids.

## 2. Method and sources

Modal synthesis of an axisymmetric shell, as a bank of `mode` resonators — the same substrate idiom `struck_bar`, `cymbal`, `drum_head` and `driven_metal` already use in this library, mirrored rather than invented (`CLAUDE.md` rule 6: enter through the physical model).

**S1 — Inácio, Henrique & Antunes, "The dynamics of Tibetan singing bowls", *Acta Acustica united with Acustica* 92 (2006) 637–653.** Full text read. Experimental modal identification of four bowls.

- **Table I / II, measured mode-pair frequencies and their ratios to the (2,0) fundamental:**

  | mode | Bowl 1 (934 g, 180 mm) | Bowl 2 (563 g, 152 mm) | Bowl 3 (557 g, 140 mm) | Bowl 4 |
  |---|---|---|---|---|
  | (2,0) | 1 | 1 | 1 | 1.0 |
  | (3,0) | 2.8 | 2.7 | 2.8 | 2.9 |
  | (4,0) | 5.2 | 4.8 | 5.2 | 5.7 |
  | (5,0) | 8.1 | 7.5 | 7.9 | 9.1 |
  | (6,0) | 11.6 | 10.6 | 10.9 | 13.1 |
  | (7,0) | 15.6 | 14.2 | — | 17.7 |
  | (8,0) | 19.9 | 18.1 | — | 22.6 |

  Only `(j,0)` modes matter — Bush & Terwagne below say why. **The measured spread across four real bowls IS the entry's main axis**, exactly as `metallic_fm`'s `stretch` is the published range for its own class. Nothing is fitted: the two ends are two measured bowls.

- **Every mode is a PAIR.** Table I lists `f_nA` and `f_nB` for each mode, and the paper's own model *"introduc[es] a difference (or 'split') Δω_n between the frequencies of each mode pair"*. On the fundamental, measured: Bowl 1 219.6/220.6 = **0.46 %**, Bowl 2 310.2/312.1 = **0.61 %**, Bowl 3 513.0/523.6 = **2.07 %**. As a fraction it stays roughly constant up the bank (Bowl 1: 0.46, 0.13, 0.33, 0.02, 0.38, 0.04, 0.30 % for modes 2–8). That is the bowl's warble, and it is a measurement, not a detune.

- **Damping**: *"dissipation is very low, with modal damping ratios typically in the range ς_n = 0.002~0.015 % (higher values pertaining to higher-order modes). However, note that these values may increase one order of magnitude, or more, depending on how the bowls are actually supported or handled."* Their simulations use 0.005 % throughout. Q = 1/(2ζ): **25 000 … 3 300**, typical 10 000, and an order of magnitude more damping when the bowl is held → down to ~330. That is the whole `ring` axis with the source's own words behind it, including the direction (higher modes die first).

- **The sentence this entry exists for:** *"The frequency relationships are mildly inharmonic, which does not affect the definite pitch of this instrument, mainly dominated by the first (2,0) shell mode."* A bowl has a **definite pitch**. That is precisely what the gong does not have, and precisely what BJ heard as missing.

**S2 — Terwagne & Bush, "Tibetan singing bowls", arXiv:1106.6348 / "The Tibetan singing bowl: acoustics and fluid dynamics".** Full text read. Four bowls, fundamentals 187/236/347/428 Hz.

- The closed form, their Eq. (5), for `m = 0`: `f(j,0) ∝ (a/R²)·(j²−1)/√(1+1/j²)` — i.e. `f_j ∝ j(j²−1)/√(j²+1)`, the in-plane ring solution, which Inácio et al. cite too. Normalised to j = 2: **1, 2.828, 5.423, 8.771, 12.866, 17.709, 23.297**. Their Fig. 5b validates it for j = 2…6 across all four bowls. It sits almost exactly on measured Bowl 4, which is why Bowl 4 is the entry's ideal end.
- *"owing to the relative squatness of the bowls and the associated high energetic penalty of modes with m ≠ 0, only modes (n,0) were excited"* — so a bank of `(j,0)` modes is the complete model, not a simplification.
- *"Due to the bowl asymmetry, two peaks separated by several Hz arise and a beating mode is heard."* Independent confirmation of the doublet.
- **Struck vs rubbed, and this is the second axis:** *"When the bowl is rubbed with a leather wrapped mallet, the lowest mode is excited along with its harmonics, an effect known as a mode 'lock in'."* Struck, a bowl gives the inharmonic shell modes above; **sung**, it gives the fundamental plus true HARMONICS. Two different spectra from one body, both sourced.

## 3. What the entry will be

A `mode` bank on the `(j,0)` series, every mode a pair split by a measured fraction, driven by an exciter that runs from a strike to a rub.

| axis | what it is | where the numbers come from |
|---|---|---|
| `bowl` | the mode series, from the flattest measured bowl to the ideal ring | S1 Table I/II, Bowl 2 ↔ Bowl 4; S2 Eq. 5 sits on the Bowl 4 end |
| `warble` | the doublet split, as a fraction of each mode | S1 Table I, 0.46 / 0.61 / 2.07 % measured on three bowls |
| `ring` | modal damping, and higher modes die first | S1: ζ 0.002…0.015 %, ×10 when handled → Q 25 000 … 330 |
| `sung` | struck (shell modes) ↔ rubbed (mode lock-in, harmonics) | S2, the lock-in passage |

Held notes must stand — the host owns note-off — which at these Q values is what the physics does anyway: at Q 10 000 and 220 Hz the 1/e decay is 14 s and T60 about 100 s, so inside this instrument's 0.1–11 s notes a struck bowl is a standing tone that darkens.

## 4. What else has to change with it

`singing bowl` and `klangschale` **move** from `metallic_fm`'s surface forms to this entry. Left where they are, the router still sends the prompt to the gong and none of the above reaches the author. `metallic_fm` keeps gong / tam-tam / anvil / bell metal, which is what it is.

## 5. The acceptance gate

Not self-scored (rule 3). One folder, same pitch, same length, one gain for the set:

- `nearest_existing.wav` — `metallic_fm` at its defaults, **already on disk**, rendered before this document was written.
- `nearest_existing_tightest.wav` — the same entry pushed as close to a bowl as its own axes allow (`stretch` 1.4, `damping` 0, `ring` 0.89), so the incumbent is represented at its best and not its default.
- `new.wav` and an HTML page where the parameter combination is visible and selectable (`tools/lco_param_page.py`), because that is the standard this project holds an instrument to.

BJ's ear decides. Measurements may accompany the result; they never decide it.

## 6. What happened — 2026-07-31

**Passed, on the second attempt of the three budgeted.** Heard at 220 Hz, 8 s, one gain for the whole set (−12.26 dB), two incumbent rows against seven candidate rows at the anchors of `ring`, `warble`, `sung` and `bowl`. BJ:

> ja, dsie bowls sind sehr gut! so lassen. verfiziertes instrument

The entry is in `backend/dco_lexicon.json` as `singing_bowl`; `singing bowl` and `klangschale` moved off `metallic_fm` as §4 requires, and `named_entries("crystal singing bowls in a cathedral")` now returns `['singing_bowl']` while `a large gong` still returns `['metallic_fm']`.

Five things the body ended up doing differently from the first draft, each because a measurement said so, and each written into the code at the line it belongs to: the mallet does **not** scale with Q (a fixed impulse gives a Q-independent attack over a 75× span — Q is the decay); there is **no noise exciter** (at Q 10000 a mode's bandwidth is 0.02 Hz, so noise through it is a lottery, −24.3 dB at 3 s against −33.3 at 5.7); the mallet hangs on `knote` rather than `expon` (which starts at instrument init, so under a preroll the strike is already gone); it is **deterministic** (a random mallet made the note's peak a random draw, and the axis probe then read `bowl` and `warble` as faders); and the split is the table's **irregular** pattern rather than one shared fraction (twelve long partials whose beats share a period all re-align at once — measured as an 11.6 dB pulse).

### The audition was at the wrong sample rate — found after the approval, 2026-07-31

**The page BJ heard rendered at 44100. The plugin compiles Csound at 176400** (`lroOsFactor_ { 4 }` in `src/PluginProcessor.h`, capped so the absolute engine rate stays under 200 kHz — 4× at a 44.1 kHz host). At that rate the approved body is a different sound:

| | 44100 | 176400 | |
|---|---|---|---|
| post-onset peak, 220 Hz, defaults | 2.049 | **12.48** | 6.09× |
| spectral centroid | 377 Hz | **259 Hz** | |
| peak at 110 Hz | 3.994 | **23.32** | |

Against a host clip ceiling of 2.52 transparent / 2.75 absolute, the entry as approved was hard-limited at 110, 220 and 440 Hz in the synth and only survived at 880. **So what BJ approved was not what the instrument would have played** — the timbre, not only the level: the low modes gain far more than the high ones, which is the 118 Hz of centroid.

**The cause was mine and it was in the exciter, not in `mode`.** The mallet's weight hung on `knote` as `exp(-knote / 0.0018)`, and 1.8 ms is *shorter than one control period*: how much of that pulse exists at all depends on the control rate, and the control rate follows the sample rate. Isolated by measurement — `mpulse` alone runs 1.07× across the two rates, and `interp` in place of `a()` changed nothing, so it is neither the bank nor the staircase.

**The repair is the same force, computed where a sub-millisecond force belongs:** at audio rate, as a one-pole `tone` on the contact impulse, at the 88.4 Hz corner the exponential already named. It is not a new sound. At 44100 — the rate BJ heard — the peak moves 2.049 → 2.156 (+0.4 dB) and the centroid 377 → 362 Hz. Across the two rates it now measures 1.04× with the centroid 362 against 361 Hz. It also took two thirds of the register tilt with it (12.61 dB from 110 to 880, now 6.72 at the attack) and brought the whole keyboard inside the ceiling, because how much of a 1.8 ms pulse survived one control period was a function of pitch as much as of rate.

One further change, headroom and not taste: an output scale of 0.72. The corner space still reached 2.76 at 110 Hz, over the absolute ceiling, and the project's own page generator refuses to render a body above 2.52 at all — so the entry could not be put in front of BJ at the plugin's rate. It now peaks 2.00 at the worst of 81 corners × four registers and 1.61 at its defaults, which is where the library's other mode banks sit at that rate (`struck_bar` 1.96, `cymbal` 2.26). No balance inside the body moves.

### Still open

- **Register tilt 6.72 dB total, −2.24 dB/octave** at the attack. This one really is `mode`: a resonator's gain at resonance goes with Q/f. The library's other mode banks carry it too (`driven_metal` −2.72, `drum_head` −2.99 dB/octave). Over `M7`'s bound and **declared rather than flattened**, because flattening it would be a fitted curve laid over the substrate's own model, which Instrument Authoring rule 2 forbids.
- **The re-hearing.** The approval of 2026-07-31 stands for a sound rendered at a rate the instrument does not run at. `tools/lco_listening/singing_bowl_params/index.html`, generated by `tools/lco_param_page.py`, renders at 4× the way the plugin does — that is the page the entry has to survive.
