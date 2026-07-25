# The parametrisation audit — what the library actually is

**Written 2026-07-25**, on BJ's order: *„Prüfe alle vorhandenen Instrumente auf
sinnvolle Parametrisierung nach vorgegebenen Regeln. Erweitere die Instrumente
danach: beziehe Naturklänge und untypischere, weniger klischeehafte Instrumente
ein."*

The rules are not invented here. They are `docs/LCO_CONCEPT.md` §3 (what an
instrument IS) and §4 (the platform invariants), plus §5's own ruling on `vibes`
and the two failure modes §7 records as already committed. This document is the
record of turning them into measurements and running them.

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
| **S4** | Every axis has a code exemplar the model can see | §2 (`anchor_code`) | read from the entry |
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
that run's, not today's.** Where it stands now, on 57 instruments: 85 findings, 28
declared. What has closed since, each with its own commit: the register tilt of
`struck_bar` / `cymbal` / `glass` / `drum_head` (M7 is now four entries, three of
them the vowels §5 leaves with BJ and one a declared consequence); `saw` /
`square` / `pulse` / `triangle` / `pwm` and the wind family parametrised (S1: 38
of 45 → 29 of 57); the meter's own defects behind several of these numbers (see
§2). What has NOT closed is S4 — the 17 axes with no exemplar, still §5 item 1 —
and the two movement questions, which are BJ's.

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

On the corrected meter — span ≥ 60 cents **and** coherence ≥ 0.35, the threshold sitting
in an empty band between a noise null whose maximum over 60 renders is 0.188 and the
0.51–1.00 that everything with a real modulation reads — **16 of the 57 shipped
instruments do not move**, in two distinct groups:

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
records the struck three as *„Instruments 4–6 — BUILT"* — and the shipped path
could reach none of them. Recovered in `e132a962` through
`tools/lco_recover_lost_keys.py`, which reproduces 29 of the 30 committed bodies
byte-for-byte before it will emit anything new.

---

## 5. Ranked, what this leaves to do

1. **`anchor_code` for the 17 axes of 65 that have none**, so M5 and M6 can see
   them at all: `fm`, `fm_bell`, `fm_ep`, `metallic_fm` (ring/detune and friends),
   `drum_head` (strikepos/tension/damping), `string` (pick/damp) and `analog_osc`
   (drive/fat/age). Until then "no axis is a fader" is untested for those 17. They
   are the entries that bake an axis into an expression instead of declaring it, so
   each needs the body rewritten to the `k<name> = <default>  ; name [lo..hi]: …`
   form before an anchor can be generated.
2. **Parametrise the 29 entries that still carry no parameter at all.** Done so
   far: the five §8 named (`saw`/`square`/`pulse`/`triangle` duty and `pwm`'s lost
   `rate`) and the wind family (`clarinet`/`flute` breath, `brass` press, `organ`
   registration).
3. **`analog_osc`'s three missing notes**, moved out of §5 into the entry.
4. **The vowel family's fixed formants** — a real ceiling, and fixing it changes
   timbre, so it needs BJ's word. They are also the only three entries left with a
   register tilt that is not a declared consequence of something else.
5. **Sixteen entries stand still** against movement-by-default (§3, M4). Two
   questions for BJ, because both answers change how instruments sound:
   does the plain-waveform family (`sine`, `triangle`, `square`, `pulse`, `saw`'s
   relatives, `sub_sine`, `bass_saw`, the two fixed vowels) get movement added, and
   does a nature bed's non-periodic wander count as movement?
6. **Does movement-by-default hold at every register, or only at the note being
   played?** Also BJ's, and the measurement is in hand: of the 57 entries at their
   defaults only 32 satisfy `moves` at all six registers. Most of the other 25 sit
   in classes the meter documents as beyond it — `string` and `mbira` travel
   2881–11170 Hz at coherence 0.14–0.25 because a decaying high-Q bank's late
   window is a noise floor — plus fixed-formant bodies (`voice`, `saw`, `sub_sine`)
   whose travel shrinks in cents as the note climbs past their formants. The gate
   reports these and fails only at the asked pitch, because condemning 25 shipped
   entries in the name of a meter limit is not a defect fix.
