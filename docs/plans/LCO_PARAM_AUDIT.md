# The parametrisation audit — what the library actually is

**Written 2026-07-25**, on BJ's order: *„Prüfe alle vorhandenen Instrumente auf
sinnvolle Parametrisierung nach vorgegebenen Regeln. Erweitere die Instrumente
danach: beziehe Naturklänge und untypischere, weniger klischeehafte Instrumente
ein."*

The rules are not invented here — **with one exception, S4.** They are
`docs/LCO_CONCEPT.md` §3 (what an instrument IS) and §4 (the platform invariants),
plus §5's own ruling on `vibes` and the two failure modes §7 records as already
committed. This document is the record of turning them into measurements and
running them. S4 is the exception: `anchor_code` appears nowhere in
`LCO_CONCEPT.md`, so "every axis has a code exemplar" is this audit's own
requirement and is marked as such in the table below rather than given a §
it does not have.

The harness is `tools/lco_param_audit.py`; the meter under it is
`tools/lco_measure.py`, whose `--selftest` must pass before any number here means
anything.

```bash
.venv/bin/python tools/lco_measure.py --selftest
.venv/bin/python tools/lco_param_audit.py --out audit.json
```

---

## 1. The rules, as checks

| | The rule | Where it comes from | How it is measured |
|---|---|---|---|
| **S1** | An instrument carries parameters | §3 | the entry has a `params` block |
| **S2** | Every axis: a measured range, a default inside it, ≥2 named anchors, a perceptual gloss on each | §3.2, §3.3 | read from the entry |
| **S3** | Every axis carries a `note` saying what it does to the sound | §3.2 | read from the entry |
| **S4** | Every axis has a code exemplar the model can see | not in `LCO_CONCEPT.md` — this rule is the audit's own | read from the entry |
| **M1** | The body renders clean from 55 to 1760 Hz | — | not silent, not so hot the host's clip acts |
| **M2** | The sound follows the keyboard | §4 *pitch belongs to the synth* | `lco_measure.tracks` over 110→880 Hz |
| **M3** | A held note stands | §4 *the oscillator is a spectrum source* | tail/head RMS |
| **M4** | The sound moves | §4 *movement by default* | colour travel in 31 ms windows |
| **M5** | An axis is a colour control, not a volume control | §4, §7.8 | RMS spread across its anchors |
| **M6** | An axis actually does something | mirror of M5 | centroid / comb / motion spread across its anchors |
| **M7** | The keyboard is not a volume control either | §5's verdict on `vibes` | RMS tilt across 110–880 Hz |

**M7 is not a sentence in §4.** It is §5's judgement on `vibes`, whose 15 dB
register tilt is called there *„a volume control called ‚pitch'"* and *„precisely
the defect §7 failure mode 8 exists for"*. The audit only holds the library to a
ruling that already existed.

**Declared properties.** Some readings are the instrument rather than a defect in
it: an unpitched bed has no pitch to track with, a struck instrument dies. Those
are listed in the harness's `DECLARED` table with the reason and, where it is a
release, the date BJ gave it — and they still print, marked `[declared]`. An
exception list that hides readings is the same loss as a gate that cries wolf,
which is failure mode §7.4 by the other route.

---

## 2. What the meter had to be taught first

Three meters were tried for M2 and **all three gave confident wrong answers on
exactly the entries that need judging.** Each rejection is recorded in
`lco_measure.tracks`'s docstring with its number, because this is the fourth time
on this project that the meter rather than the thing produced the wrong number
(§7.7, §7.8, §7.9):

- **`f0`** locks onto the slow beat of a Q=900 inharmonic bank and answers 37.8 Hz
  for a played 110 (`struck_bar`), and answers nothing at all for a membrane. Read
  as pitch error, it called `fm_bell` mistuned by −2773 cents. `fm_bell` is not
  mistuned; it is a bell.
- **the spectral centroid** saturates on any band-limited source. `vco2` holds the
  band, so a saw has 400 harmonics at 55 Hz and 12 at 1760 and its centroid rises
  with slope 0.675 against a keyboard it follows to the cent. Read as tracking,
  that calls `saw` broken.
- **a lag search over log-spectra** scores a spurious 1.0 on a short overlap: a
  saw's fundamental landing on the octave-up render's 9th harmonic reported −3800
  cents of mistracking.

What works is two hypotheses over one band, no search: do the two spectra agree
read from their own played notes, or do they agree on one absolute axis. Measured
separation on the calibration set: a saw 0.992 against 0.101, a fixed 800 Hz
register 0.004 against 1.0.

A fourth reading also had to be thrown out: **a single 220→440 pair said
`drum_head` mistracks by −205 cents**, on an instrument whose spectrum in fact
scales to within 2% over three octaves (band energies 30/30/36/3.6/0.2 % at 110 Hz
against 34/29/33/4.1/0.3 % at 880). Narrowband noise needs a long span, so
tracking is measured over three octaves.

**Closed since:** `pink_noise`'s RMS varies between renders — measured 1.68 dB at
110 Hz over four seconds, 4.7 dB over five renders at three — so two runs of the
audit disagreed about whether it has a register tilt. M7 now repeats the lowest
register three times and reports a tilt as a finding only when the across-register
spread clears twice that scatter; below it the line says NOT EVIDENCE in so many
words.

The measurement also corrected the assumption behind the worry. It is not a class
of 1/f-heavy bodies: **exactly one entry in the library does not render
deterministically**, `pink_noise`, because `pinkish` seeds itself. Every other
body — `noise`, `thunder`, `surf`, every bed — repeats bit for bit and scatters
0.00 dB. So a 0.0 in that column is the true answer rather than a broken
measurement, and the single case that needed the guard is the one that has it.

---

## 3. What the audit found

45 instruments, 84 findings, 27 declared properties. Every instrument tracks the
keyboard, every one renders clean at all six registers, and no axis was found to
be a fader in disguise — but that last clause is much weaker than it sounds, see
S4.

**This section is the original run and is kept as written; the counts in it are
that run's, not today's.** Where it stands now, **on 63 instruments: 58 findings,
28 declared**. What has closed since, each with its own commit: the register tilt of
`struck_bar` / `cymbal` / `glass` / `drum_head` (M7 is now four entries, three of
them the vowels §5 leaves with BJ and one a declared consequence); `saw` /
`square` / `pulse` / `triangle` / `pwm` and the wind family parametrised (S1: 38
of 45 → **21 of 63**); the meter's own defects behind several of these numbers (see
§2). What has NOT closed is S4 — the 7 axes with no exemplar, still §5 item 1 —
and the movement question for the vowels, which is BJ's.

Two claims in the paragraph above this one no longer hold. **`cymbal` no longer
tracks the keyboard** — M2 reads `mixed` (r_note 0.26 against r_fixed 0.244) as a
real, undeclared finding, so "every instrument tracks the keyboard" is 62 of 63.
And the audit does **not** honour `; MOVEMENT: TEXTURE`: `DECLARED` has no entry
for it, so `rain`, `crackle`, `thunder`, `cymbal` and `ice` print as undeclared M4
failures here while `lco_axis_probe.gate()` exempts them. Two harnesses, two
answers, on the invariant §4 was amended for — this one is wrong and is the one to
fix.

### S1 — 38 of 45 instruments have no parameter at all

```
additive bass_saw brass cheby chiptune clarinet crackle cymbal flute glass
harpsichord hiss noise organ pink_noise pulse pwm rain rhodes ring_mod saw
sine square strings struck_bar sub_sine supersaw surf sync theremin thunder
triangle vibraphone voice voice_ee voice_oo wind wurlitzer
```

Seven carry axes: `analog_osc` (wave, drive, fat, age), `fm_ep` (ting, ring,
hollowness, strike), `drum_head` (pitched, strikepos, tension, damping), `fm` /
`fm_bell` / `metallic_fm` (index, ring, detune), `string` (bow, pick, damp).

`saw`, `square`, `pulse`, `triangle` and `pwm` are among the unparametrised, and
they are the most-played sounds in the library. §8 already names this: `square`'s
duty is nailed to 0.5 and `pulse`'s to 0.30 as structural constants, *„which is
precisely why BJ's own example („square ist sharp wenn x=y, hollow wenn x=z") was
impossible before instrument 1"*. `pwm` is worse than unparametrised — the parked
implementation gave it a `rate` axis (`_PWM_ANCHORS = {"rate": {"slow": 0.0,
"medium": 0.5, "fast": 1.0}}`) and the harvest dropped it, so a control that
existed is gone.

### S4 — 17 of the 24 existing axes have no code exemplar, so M5 and M6 cannot see them

`anchor_code` was only ever generated for the FIRST axis of each parametrised
entry (the harvest's *„First param = character axis"*). The consequence is not
cosmetic: **whether the other 17 axes are colour controls or volume controls has
never been measurable at all.**

| entry | axis with an exemplar | axes without |
|---|---|---|
| `analog_osc` | wave | drive, fat, age |
| `fm_ep` | ting | ring, hollowness, strike |
| `drum_head` | pitched | strikepos, tension, damping |
| `fm`, `fm_bell`, `metallic_fm` | index | ring, detune |
| `string` | bow | pick, damp |

Shipping a −4 dB fader under a colour name is failure mode §7.8, committed once on
this project already, on `drum_head` — the same entry that today has three
unexaminable axes.

### S3 — three of `analog_osc`'s four axes have no note

`wave`, `fat` and `age` carry anchor words and glosses but no text saying what the
axis does. The measured facts for all three are in `LCO_CONCEPT.md` §5 — the two
`vco2` segments and the exact ×0.578 seam compensation for `wave`, the beat rates
for `fat` (2¢→0.23 Hz, 7¢→0.83, 20¢→2.34, 40¢→4.67), the three separate
instabilities and their rates for `age`. That is §8's own complaint, still
standing: *„Much of the curation already exists as prose … written where the model
cannot read it."*

### M7 — the noise-driven modal family tilts loudness with the keyboard

| entry | across 110–880 Hz | per octave |
|---|---|---|
| `drum_head` | 8.9 dB | −2.98 |
| `cymbal` | 6.9 dB | −2.29 |
| `glass` | 5.5 dB | −1.82 |
| `struck_bar` | 5.4 dB | −1.71 |

`drum_head`'s −2.98 dB/octave is the documented law, uncompensated. §5 measured
`mode` under a noise exciter as emitting power ~ Q^1.020 / f^1.015 — so amplitude
~ 1/√f, which is −3.01 dB per octave. The bank was then levelled *at one pitch*
(§5: *„Set on RMS, which is what the ear compares when switching keys"*), and
nothing carries the f dependence. The other three tilt less only because their
top modes drop out against the `< 15000` gate as the register rises, which is a
second effect, not a fix.

Every one of the other 41 instruments sits inside 0.5 dB/octave. That contrast is
what makes these four a finding rather than a property of the harness.

### M7 — the vowel family has a structural ceiling at its own first formant

| entry | F1 | level by register | ceiling |
|---|---|---|---|
| `voice` (ahh) | 600 Hz | −7.7 / −8.7 / −9.5 / −10.5 / −11.0 / −11.7 / −25.5 dB at 110 / 165 / 220 / 330 / 440 / 660 / 880 | ~660 Hz |
| `voice_oo` (ooh) | 300 Hz | −9.3 / −9.3 / −9.5 / −8.3 / −22.3 / −32.0 / −18.1 | ~330 Hz |
| `voice_ee` (eee) | 270 Hz | −11.0 / −11.9 / −9.4 / −15.6 / −30.0 / −45.7 / −43.8 | ~220 Hz |

The formants stand still while the voice climbs into them, and above F1 no
harmonic can land in the first formant any more. This is the same fixed-formant
physics §6 recorded for `fmvoice` (*„only ≥ ~880 Hz — its formants are fixed, so
this is a structural floor, not a scale fix"*), here as a ceiling. Each entry's
`why` states its measured ceiling, so the author is told rather than left to find
out. **A real singer moves their formants up with pitch; this bank does not.** That
is the open question for the family, and it is a timbre change, not a level fix.

### M4 — sixteen of fifty-seven stand still, and the meter had to be fixed three times

This finding changed twice, both times because the *meter* was wrong, and the first
two readings should be distrusted rather than averaged with the third. Originally it
named two entries — `bass_saw` (7.9 Hz of colour motion) and `sub_sine` (7.0 Hz).
Then `moves()` gained a coherence term, because a span alone certifies movement on
unmodulated noise. Then `travel()` moved onto a POWER-weighted centroid, because an
amplitude-weighted centroid on a linear frequency axis let a partial 60 dB down at
18 kHz carry the verdict: a standing sine plus an inaudible whisper passed with a span
of 18.7 Hz at coherence 0.996.

Then the SPAN threshold moved from hertz to cents, because an absolute hertz bound is
the same mistake in a third place: 8 Hz is nothing on `hiss`'s 13.5 kHz centroid and a
plainly audible wobble on `sub_sine`'s 205 Hz. In cents the two populations separate
more sharply, and 60 cents fails exactly the sixteen entries the 8 Hz bound did — so
that change was verdict-neutral and only made the number mean something.

On the corrected meter — span ≥ 60 cents **and** coherence ≥ 0.35 — **16 of the 57 shipped
instruments do not move**, in two distinct groups:

The band this threshold was said to sit in is no longer empty. The claim was a noise null
whose maximum over 60 renders is 0.188 against the 0.51–1.00 that anything with a real
modulation reads. Re-measured over 54 stationary renders the null's maximum is **0.238**,
and two shipped entries now read inside the gap — `hurdy_gurdy` **0.434** and `rhodes`
0.509 — so the empty band is [0.238, 0.434] and `hurdy_gurdy` passes on 0.084 of margin.
0.35 is still above the stationary population, which is what the threshold needs; it is no
longer wide of everything on both sides, which is what "calibrated rather than chosen" was
resting on.

```
they have nothing to move (travel in cents; coherence 1.00 — coherent but tiny)
  sine 0   triangle 1   square 3   bass_saw 7   voice_ee 21   voice_oo 21
  pulse 25   sub_sine 54                        (saw and analog_osc pass, at 78)
their travel is variance, not motion (large span, coherence at or near zero)
  thunder .00/1209 cents   rain .00/217   crackle .00/273   hiss .06/173
  noise .07/247   drum_head .13/973   pink_noise .15/327   cymbal .18/797
```

The two groups are different problems. The first is the plain-waveform family: there
is genuinely nothing in a `vco2` square to move, so movement has to be *added* — which
is sound-shaping and needs BJ's word, not a unilateral fix. The second is the nature
beds, where the sound plainly does change over the note but not periodically; whether
"movement by default" is satisfied by a noise bed's wander is a question about the
fundamental's meaning, and also BJ's.

**Two instruments were built and held back for the same reason, which is what makes
this a question and not a to-do.** `glass_harp` and an earlier `cricket` both passed
every rule except this one, and in both cases the cause was physical rather than a
defect: a rubbed wineglass is nearly one sinusoid (partials 0, −48, −67, −68 dB) and a
cricket's tone is tuned to be nearly a pure sine, so on a power basis neither has a
colour that CAN move much. The cricket shipped because a real one is not one tone —
the file scrape is audible and carries the movement. The glass harp has no equivalent:
its audible movement is the beat between its two quadrupole modes, 0.30 to 0.45 deep at
0.67 to 8.00 Hz, which lives at the amplitude layer. §4 says loudness may not travel
within a standing tone, and `fm_bell`'s doublet beat is deliberately protected by
`ihp=1` — so beats are already treated as texture rather than as loudness somewhere in
this platform. Which of those two readings governs is BJ's to settle, and until he does
the entry stays out of the lexicon rather than being forced.

Recorded here rather than acted on. What the meter *cannot* say is separately relevant
to the beds: at 4 s it cannot resolve movement slower than the window, and `wind`
gusts at 0.071 Hz — a quarter of a cycle. `wind` reads 0.53 and passes; `surf` 0.64.
Two of the eight beds do move on this evidence, which is why the list above is eight
and not ten.

---

## 4. What was recovered before anything was added

The audit's first finding was not about parametrisation. `tools/lco_build_library.py`
states that the library *„inherited every idiom the old path could actually
produce rather than a smaller vocabulary wearing its name"*. It did not: the
harvest looped over the lexicon's techniques, and the parked `csound_orch.py`
carried **fifteen more instruments of its own** in `_CS_TECH_EXTRA`, each with a
`why` and its own surface forms, each reachable from a prompt in the old path,
none of them in the lexicon.

```
wind rain surf thunder hiss crackle noise pink_noise   the nature beds
voice voice_ee voice_oo                                the vowel/formant family
rhodes wurlitzer vibraphone                            the struck instruments
glass                                                  the fourth modal bank
```

`LCO_CONCEPT.md` discusses all fifteen as if they were in the library — §6 calls
the beds *„the route for the natural-sound and animal part of the library"*, §5
records the struck three as *„Instruments 4–6 — BUILT, NOT YET HEARD"* — and the
shipped path could reach none of them. Recovered in `e132a962` through
`tools/lco_recover_lost_keys.py`, which at that commit reproduced 29 of the 30
committed bodies byte-for-byte before it would emit anything new.

**That guarantee is spent, and the tool is now a false accusation.** Every
parametrisation since `e132a962` moved the bodies it compares against, so it exits
1 with *"the extraction does not reproduce ['bagpipe', 'brass', … 40 keys], so it
is not the path that produced the library"* — 40 entries named as suspect that are
simply parametrised. It did its job once; a guardian that is permanently red gets
switched off, so it should be either re-baselined against HEAD or deleted, not left
standing as though it still guarded anything.

---

## 5. Ranked, what this leaves to do

1. **`anchor_code` for the 7 axes of 65 that still have none** — down from 17. Ten
   have been declared and gated: `fm`/`fm_bell`/`metallic_fm`'s `ring` and `detune`
   (the three entries are the same body at three settings, so each shipped constant
   is one sample of the emitter's own law and three samples pin a line), `string`'s
   `pick` and `damp`, and `fm_ep`'s `hollowness` and `strike` (their own notes carry
   measured endpoints, and those pin them). Every one reproduces its shipped
   constant exactly at its default, so the entry a prompt already reaches is
   unchanged. The seven left are blocked for three different reasons, and none of
   them is work that can be finished by reading the tree:
   - **`fm_ep`'s `ring`** — nothing in the tree pins it. Its note says both ends are
     taken from real electric pianos, and `rhodes`/`wurlitzer` are `fmrhode`/
     `fmwurlie`: a different idiom with no constant in common.
   - **`drum_head`'s `strikepos`, `tension`, `damping`** — `tension` would move the
     same eight mode ratios that `pitched` already moves (`pitched` substitutes 24
     constants), and how the two interact was the emitter's. Guessing it invents an
     instrument rather than recovering one.
   - **`analog_osc`'s `drive`, `fat`, `age`** — the code path does not exist at the
     shipped defaults. The body says "drive 0: clean, no saturator stage" and
     carries ONE `vco2` copy, so declaring `drive` and `fat` means writing a
     saturator and detuned copies: new DSP, not a mapping, and BJ's call. `fat` and
     `age` also have no note at all (item 3).
2. **Parametrise the entries that carry no parameter at all — 21 left of 29.** Done
   so far: the five §8 named (`saw`/`square`/`pulse`/`triangle` duty and `pwm`'s lost
   `rate`), the wind family (`clarinet`/`flute` breath, `brass` press, `organ`
   registration), the five nature beds (`wind` speed, `rain` surface, `surf` water,
   `thunder` distance, `crackle` blaze) and the three `mode` banks (`struck_bar`,
   `glass`, `cymbal`, each with `strike` and `ring`). That batch left the library at
   37 of 58 entries, 83 axes, 302 exemplars; it now stands at **42 of 63 entries, 93
   axes, 332 exemplars**.

   Four things that batch established, all of which the next one inherits:

   - **The default must render SAMPLE-IDENTICALLY to the shipped body**, at six
     registers, and that is now part of the apply step rather than a separate check.
     Exposing a literal is a parametrisation only if the axis AT ITS DEFAULT *is* that
     literal; anything else is a new sound wearing an old name, which is BJ's call.
     Every axis is written so its default factor is exactly 1.0 in floating point
     (`8.0 ^ (0.40 - kdist)`, `1 + 1.30 * (kspeed - 0.45)`), so identity is achievable
     and a near-miss is a real defect rather than rounding.
   - **A movement failure is forgiven only if the body at HEAD fails it too, at the
     same register, proven per corner.** Assuming it nearly shipped a `wind` corner
     that stood still (coherence 0.335) where the shipped body moves (0.528).
   - **An axis on a body that modulates its own loudness must not touch that LFO.**
     Putting the first version of `wind`/`surf`/`thunder` on the gust, swell and roll
     RATE made a three-second note catch a different phase of a 0.02 Hz LFO at every
     setting: 1.99, 4.28 and 2.10 dB apart, with the worst corner in the MIDDLE. That
     is a window artefact, not a fader — and an axis that leaves the LFO alone has the
     same artefact in every corner, where it cancels.
   - **Three opcode facts**, measured, that decide where an axis can go at all:
     `reson` with iscl=2 holds its level whatever its centre or width; `tone` passes
     power in exact proportion to its corner (so 1/sqrt(fc) compensates exactly);
     `atone` obeys no such law, so no axis is put on an `atone` corner.

   Of the 21 left, three groups and one deliberate refusal:
   - **`rhodes`, `wurlitzer`, `vibraphone`** wrap `fmrhode`, `fmwurlie` and `vibes`,
     whose arguments are the model's own physical parameters and are all literals —
     stick hardness and strike position on `vibes`, the two index scalers and the
     tremolo on the FM pair. These decay rather than stand and have no `balance`, so
     the level needs care the mode banks did not.
   - **`strings`, `supersaw`, `sync`, `bass_saw`, `harpsichord`, `chiptune`,
     `theremin`, `sub_sine`, `additive`, `ring_mod`, `cheby`** — ordinary synthesis
     bodies with obvious literals (detune spread, sync ratio, partial count,
     Chebyshev order).
   - **`voice`, `voice_ee`, `voice_oo`** are blocked behind item 4, the fixed
     formants.
   - **`sine`, `noise`, `pink_noise`, `hiss` have no constant worth exposing** and
     should stay unparametrised rather than be given invented DSP. `sine` is one line;
     `noise` and `pink_noise` are the reference spectra and have nothing but an
     amplitude; `hiss` has only an `atone` corner, which is exactly the opcode whose
     level cannot be compensated by any law. Reporting them as deliberately bare is
     the correct outcome, not a gap.

   **Two findings for BJ from that batch, not acted on**, because changing either
   changes a shipped sound: `rain`'s patter measures −25.27 dB against its wash's
   −11.52, and `crackle`'s pops −34.09 dB against its sizzle's −15.41. In both, the
   thing the entry is NAMED for sits 14 to 19 dB under its own bed. It is audible —
   transients unmask — but it is a quiet layer, not the subject.
3. **`analog_osc`'s three missing notes**, moved out of §5 into the entry.
4. **The vowel family's fixed formants** — a real ceiling, and fixing it changes
   timbre, so it needs BJ's word. They are also the only three entries left with a
   register tilt that is not a declared consequence of something else.
5. **Seventeen entries stand still** against movement-by-default (§3, M4) — `ice`
   joined them. **The second question here is settled and must not be re-opened:**
   BJ released the event-texture class on 2026-07-25 („Hiermit freigeschaltet"), so a
   nature bed's non-periodic wander does not have to satisfy the movement rule — a
   body declaring `; MOVEMENT: TEXTURE` has its reading reported instead
   (`docs/LCO_CONCEPT.md` §4). Five of the seventeen hold that declaration. What is
   still BJ's: does the plain-waveform family (`sine`, `triangle`, `square`, `pulse`,
   `saw`'s relatives, `sub_sine`, `bass_saw`, the two fixed vowels) get movement
   added? That one changes how those instruments sound.
6. **Does movement-by-default hold at every register, or only at the note being
   played?** Also BJ's, and the measurement is in hand — re-derive it any time with
   `tools/lco_axis_probe.py --census`, which is also what keeps this number honest:
   of the 63 entries at their defaults **37** satisfy `moves` at all six
   registers. Most of the other 26 sit in classes the meter documents as beyond it —
   `string` travels 2881–3402 Hz at coherence 0.14–0.25 because a decaying high-Q
   bank's late window is a noise floor, and `cymbal`, `drum_head` and `pink_noise`
   likewise — plus fixed-formant bodies (`voice`, `saw`, `sub_sine`) whose travel
   shrinks in cents as the note climbs past their formants. The gate reports these
   and fails only at the asked pitch, because condemning 26 shipped entries in the
   name of a meter limit is not a defect fix.
7. **Loudness travelling INSIDE one held note** — the §4 rule that had no meter
   pointed at it. `rms_db` averages the note and `sustain` only sees a level that
   drifts one way, so a level that moves and comes back was invisible: measured on
   `overtone_voice`, a held note stepped −12.1 → −15.4 → recover → −9.5 → recover,
   6.66 dB peak to peak, while the crossed gate passed it at a 0.31 dB spread
   because every corner's MEAN was steady. `lco_measure.loudness_travel` now reads
   it (50 ms RMS in dB, linearly detrended so an exponential decay cancels exactly)
   and the gate reports the worst corner. Two things for BJ:
   - **Does a beat count?** The number cannot tell a defect from a deliberate beat,
     and 15 of 57 entries read above 6 dB somewhere in the register range because a
     detune beat IS loudness moving inside the note — at 220 Hz `supersaw` 14.98 dB,
     `strings` 13.95, `fm_bell`'s doublet 9.76 (a protected platform invariant) and
     `free_reed` 8.20. `loudness_is_the_body` settles that these four are not meter
     artefacts: none of them has a stochastic source at all, so it returns 1.0 and the
     movement is deliberate by construction. This is the same beat-versus-loudness
     question §2 records, now with a number and a verdict on it. Until it is answered
     the measurement is reported and not gated.
   - **The span is meaningless on a narrowband body, and was published anyway.** A
     narrow filter fed noise has a fluctuating envelope with nothing modulating it:
     `mode aex, 880, Q` reads 5.86 dB at Q 10, 14.92 at Q 200, 18.43 at Q 700 and
     24.14 at that Q over twelve seconds rather than four, while a deliberate 3.00 dB
     tremolo on a wide band reads 4.88. So every figure quoted here for `glass`,
     `struck_bar`, `cymbal`, `drum_head`, `mbira` or `pink_noise` was a statement about
     bandwidth. `loudness_is_the_body` re-renders with other seeds and correlates the
     envelopes: those six read −0.02 to +0.11 (their own noise), while `free_reed`,
     `surf`, `crackle`, `wind` and `thunder` read +0.62 to +1.00 and every one of them
     is SUPPOSED to move in level. The test is one-sided — above 0.35 is proof, below
     it is "not demonstrated", because a modulation whose rate comes from the pitch
     decorrelates too.
   - **A monotone swell was invisible and is now gated.** `loudness_travel` detrends
     the dB envelope, so a straight line is removed exactly: a note that swells 20 dB
     from one end to the other read 0.00 dB and passed the crossed gate with every
     printed number clean, which is the one case §4 names outright. `loudness_drift_db`
     reads it, a decaying body is judged on its travel alone, and a candidate failure
     is confirmed on a note four times as long — one slow cycle inside a short window
     is a straight line, and without that confirmation the new gate condemned `crackle`
     and `supersaw` for the length of its own window.
   - **`overtone_voice`'s 6.66 dB is a defect and not a beat**, and it does not
     yield to a constant. The lift that cancels the drone's roll-off changes the
     picked harmonic's level as the melody steps from one harmonic to the next, and
     `balance`'s 1 Hz tracker chases the step train. Measured: the ceiling at 12
     gives 6.66 dB worst-case (5 corners of 162 over 5 dB), at 30 gives 6.75 (6
     corners) while fixing the corner it was first found at, removing the lift
     entirely gives 8.47 and costs a whistle corner, and bounding `kn` to where the
     lift reaches gives 11.67. With the lift off at that one corner the level holds
     to 0.67 dB, so the lift is the mechanism — which means the fix is a different
     way of holding the whistle level, not another constant. Left at the shipped 12.
