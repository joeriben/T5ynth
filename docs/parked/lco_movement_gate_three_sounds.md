# Three sounds the movement gate stopped, and what happened to them

Two halves, with different outcomes:

- **`bubbles` and `ice` — DECIDED AND SHIPPED**, lexicon version 32, commit `07394c65`. They
  could not pass the movement gate, and the only way past it was tuning constants until the
  meter's own variance fell the right side of a threshold. BJ ruled that the gate was asking
  the wrong question of this class. Part one below is kept as it was written, because the
  measurements in it are the evidence for that ruling and for the class that now exists.
- **`waterphone` — the conflict was a measurement artefact; it ships as written.** It does
  sound at the played note — the fundamental is the loudest component of the spectrum — and it
  passes the gate. What said otherwise was `f0`, a period estimator, on a 1 : 6.267 : 17.55
  set: the artefact `tools/lco_param_audit.py` already documents for `fm_bell`. Part two is
  kept because a false conflict between two platform fundamentals, built on a plausible number
  from the wrong instrument, is worth being able to recognise again.

No Csound was taken from anywhere. All three are written from published acoustics: the
Minnaert resonance of a gas bubble in water, flexural-wave dispersion in a thin plate, and
free-cantilever mode ratios with mass loading.

# Part one: `bubbles` and `ice` — the movement gate

**Outcome, 2026-07-25.** BJ, on the question at the end of this part: *„genau. Da haben wir
nicht an Natursounds gedacht. Hiermit freigeschaltet. Ich muss ja ohnehin alle neuen
instrumente reviewen."* — the first of the three options, including its stated consequence
for the four entries already shipped that way.

What was built on that ruling, in `20e865ca`: a body may declare `; MOVEMENT: TEXTURE`, and
the gate then REPORTS `moves` instead of failing on it while everything else it checks
applies unchanged. Six entries declare it — `rain`, `crackle`, `thunder`, `cymbal`,
`bubbles`, `ice`.

It is a declaration and not a second measurement, and that is the one thing worth carrying
forward from the work below. I first tried to give the class an objective criterion and
**measured that no such criterion exists in this meter.** Over 40 renders of a STATIONARY
narrowband noise bed — a body in which nothing whatever moves — the colour travel reaches
**1005 cents** and the crest **14.55 dB**. A real swept saw, which `moves` passes at
coherence 1.00, travels **959 cents**: *less* than the bed that does nothing. The populations
overlap, so no span bound separates them in either direction, and the crest cannot do it
either (`pink_noise`, static by design, reads 15.41 dB). Both nulls are now named constants
in `tools/lco_measure.py` and the overlap is asserted in its selftest, so the threshold I
tried cannot be rediscovered as a good idea. Coherence is the only statistic in that file
that tells movement from variance; for a stochastic texture it is the wrong question; and the
liveness of this class therefore rests on BJ's ear, with the flag recording which entries are
in it.

The rest of this part is the original write-up.

## What they are

**`bubbles`** — a brook. A bubble in water is a mass of air on a spring of water and
rings at the Minnaert frequency, f = (1/2πR)·√(3γP/ρ), about 3.26/R hertz-metres: a
millimetre bubble sings near 3.3 kHz, a centimetre one near 330 Hz. It is a damped
sinusoid at Q ≈ 15 whose pitch RISES over the pulse as the bubble contracts. Three sizes
at incommensurate release rates, plus the broadband sheet of water under them. The size
distribution IS the pitch distribution, which is why fast water over stones sounds high
and a slow spring sounds low and hollow. Two axes: `flow` (releases a second) and `size`
(all three sizes together along the Minnaert relation).

**`ice`** — a frozen lake. A crack does not reach you as a click: flexural waves in a
plate are dispersive, their phase velocity going as √f, so the high components of one
impulse outrun the low ones and arrive first. Arrival time t(f) = d/(k·√f) inverts to
f(t) ∝ 1/t², and what you hear is a descending whistle. Two trains at incommensurate
rates plus the dull water-loaded knock of the impact. Two axes: `crack` (arrivals a
second) and `glide` (how far above the note each dive starts).

Nothing in the 59 shipped entries makes either sound.

## What they measure

Both hold loudness to a standard the library rarely reaches:

| | loudness spread | register tilt | crossed | worst p99.9 |
|---|---|---|---|---|
| `bubbles` | 0.10 dB | 0.04 | 0.12 | 1.08 |
| `ice` | 0.15 dB | 0.08 | 0.48 | 2.05 |

That was not free. The first version of each put the axis on the event RATE with the
envelope written against time, and the level then depended on which events happened to
land in a three-second note — 3.78 dB apart for `bubbles`, 2.46 for `ice`. Writing the
envelope against the phasor's PHASE instead makes each event's duration scale as 1/rate,
so its energy scales as 1/rate and the energy per second is constant. No compensation
needed, and none applied.

## Why they do not pass

`moves` asks for span > 60 cents AND coherence > 0.35, where coherence is one minus the
corrected spectral flatness of the centroid track. A stochastic event texture fails the
second by construction: its colour track is broadband, because that is what scattered
events look like when sampled ~43 times a second.

These sounds do not stand still. Their colour travels 5 000 to 12 000 Hz — far more than
most entries in the library. The meter classifies all of it as variance.

Measured, at 220 Hz, coherence at nine corners:

    ice      as built, 4-48 arrivals/s      0.00 .. 0.30    9 of 9 fail
    ice      3-14/s, jitter 0.06            0.00 .. 0.40    8 of 9 fail
    ice      3-14/s, no jitter at all       0.00 .. 0.26    9 of 9 fail
    bubbles  8-24/s, jitter 0.06 @ 1.3 Hz   0.22 .. 0.64    1 of 9 fails
    bubbles  8-24/s, jitter 0.14 @ 0.19 Hz  0.41 .. 0.72    0 of 9 fail  <- and yet

Three separate things were tried and each is worth knowing:

1. **A coherent LFO on top does not rescue it.** Both bodies carry one (0.19 and 0.23 Hz,
   physically the distance to the cracks and the flow finding its channels). The event
   train's own centroid variance swamps it.
2. **Reducing the jitter helps, up to a point.** A fast jitter smears each local burst
   until no rate is findable; a slow deep drift leaves the train locally periodic and
   globally aperiodic. That is what got `bubbles` to 0 of 9.
3. **The rate ceiling is a hard limit, not a tuning parameter.** Above roughly 24 events a
   second the colour track cannot resolve the train at all, since it is sampled about 43
   times a second. `cricket`'s own note in the lexicon records the same limit for its wing
   closures: "well above what a centroid sampled 32 times a second resolves".

**And then `bubbles` at 0 of 9 failed the gate anyway, at 2 of 9.** The corners that my
own probe read at 0.41 and 0.50 the gate read at 0.20 and 0.32, because it renders a
different window. Coherence on a stochastic texture is not stable between windows. That is
the point at which I stopped: the remaining distance to the threshold is smaller than the
meter's own run-to-run spread, so any constant I chose next would be chosen to satisfy
noise.

## The question for BJ

Movement-by-default is a platform fundamental and I am not going to reinterpret it. But
the meter's definition of movement — a findable RATE in the colour track — is the right
definition for a sustained tonal body and may be the wrong one for a stochastic texture,
where being alive means precisely that there is no rate to find.

The library already contains four entries in exactly this position, shipped: `rain`
(coherence 0.000 at 220 Hz), `thunder` (0.000), `crackle` (0.000) and `cymbal` (0.184,
0.138, 0.000, 0.000 at 220/440/880/1760). So the class is not new. What is new is that a
NEW entry has no shipped predecessor to inherit the exemption from, and I will not grant
one to myself.

Three ways forward, none of them mine to pick:

- **Ship them as they are**, and treat "the colour travels 11 kHz but incoherently" as
  movement for the event-texture class — which would also mean saying so about the four
  entries already shipped that way.
- **Give the meter a second movement criterion** for textures, measured on something other
  than the flatness of the centroid track. The event-density measures added this session
  (`crest_db`, `event_rate_hz`) are the obvious raw material, and `crest_db` does separate
  these bodies' corners.
- **Do not ship them.** They are then two nature sounds the library does not have, and the
  reason is written down here.

## The bodies

Reproduced as they stood when this was written, which is **no longer what ships**. Do not
copy them forward: take the bodies from `backend/dco_lexicon.json`. The sentence that used to
stand here — that the shipped versions differ by the declaration comments and nothing else —
was true at `4b9d6103` and false from `c027b8d1` on. What diverged, and why it matters:

| | here | shipped | why |
|---|---|---|---|
| `bubbles` | `randi 0.14, 0.19, 2` | `randi 0.14, 0.19, 0.5, 1` | `2` is outside `randi`'s 0..1 seed range, so Csound seeded from the CLOCK: the body below renders differently every time |
| `ice` | `randi 0.30, 0.7, 2` | `randi 0.30, 0.7, 0.5, 1` | the same clock seed |
| `ice` | `kev = exp(-9 * kph)` | `… * (1 - exp(-45 * kph))` | without the second factor the envelope is non-zero at the phasor wrap: a click, and it clips |
| `ice` | `kev2 = exp(-13 * kph2)` | `… * (1 - exp(-45 * kph2))` | the same |

Two consequences for reading the tables above. The loudness figures for both bodies are one
draw of a clock-seeded render and are by construction not re-derivable; measured on the
shipped bodies, `bubbles` reads 0.04 / 0.02 / 0.07 / 1.01 and `ice` 0.09 / 0.07 / 0.16 / 1.55
where this page recorded 0.10 / 0.04 / 0.12 / 1.08 and 0.15 / 0.08 / 0.48 / 2.05. And the
`ice` below, described here as holding loudness to a standard the library rarely reaches,
**fails the gate on headroom**: true peak 4.51 against the host's ceiling, at 17 of 60
renders. Its p99.9 of 2.05 is exactly the percentile `c027b8d1` was committed to stop
trusting for that judgement.

### bubbles

```
kflow   = 0.45                              ; flow [0..1]: how fast the water is running
ksize   = 0.50                              ; size [0..1]: how wide the bubbles are
kf0     = (kfreq * koct1)
krt     = 8.0 + 16.0 * kflow                 ; releases a second in the largest stream
kj      randi 0.14, 0.19, 2
kp1     phasor krt * (1 + kj)
kp2     phasor krt * 0.577 * (1 - kj * 0.7)
kp3     phasor krt * 1.483 * (1 + kj * 0.4)
kwan    poscil 0.5, 0.23
kmn     = (3.4 - 3.0 * ksize) * (0.62 + 0.76 * (0.5 + kwan))   ; the Minnaert factor
kr1     = 1 + 0.16 * kp1
kr2     = 1 + 0.21 * kp2
kr3     = 1 + 0.12 * kp3
ab1     poscil exp(-11 * kp1) * 0.26, limit(kf0 * kmn * 1.00 * kr1, 20, 18000)
ab2     poscil exp(-14 * kp2) * 0.19, limit(kf0 * kmn * 1.94 * kr2, 20, 18000)
ab3     poscil exp(-9 * kp3) * 0.15, limit(kf0 * kmn * 0.53 * kr3, 20, 18000)
anz     rand 1.0, 0.5, 1
awt     atone anz, 1800
amix    = ab1 + ab2 + ab3 + awt * (0.06 + 0.10 * kflow)
aref    poscil 0.3000, 400
asig    balance amix, aref, 1
```

### ice

```
kcrack  = 0.45                              ; crack [0..1]: how busy the ice is
kglide  = 0.50                              ; glide [0..1]: how far each crack falls
kf0     = (kfreq * koct1)
krt     = 4.0 + 44.0 * kcrack                ; arrivals a second
kjit    randi 0.30, 0.7, 2                   ; ice cracks irregularly
kph     phasor krt * (1 + kjit)
kph2    phasor krt * 0.617 * (1 - kjit * 0.6)
kdst    poscil 0.5, 0.19
kdp     = (2 + 34 * kglide) * (0.45 + 1.10 * (0.5 + kdst))
kdv     = 1 + kdp / (1 + 46 * kph) ^ 2
kdv2    = 1 + kdp * 0.63 / (1 + 61 * kph2) ^ 2
kev     = exp(-9 * kph)                      ; near zero before the phasor wraps: no click
kev2    = exp(-13 * kph2)
ach1    poscil kev * 0.30, limit(kf0 * kdv, 20, 18000)
ach2    poscil kev2 * 0.17, limit(kf0 * kdv2, 20, 18000)
anz     rand 1.0, 0.5, 1
akn     interp kev * kev
ank     reson anz * akn, limit(kf0 * 2.2, 20, 15000), kf0 * 1.6, 2
amix    = ach1 + ach2 + ank * 0.55
aref    poscil 0.3000, 400
asig    balance amix, aref, 1
```

# Part two: `waterphone` — the played note against the movement

A different conflict, and a sharper one. This body passes the movement gate cleanly, holds
0.39 dB of loudness across its cube and the keyboard, and is the only thing in the library
that sounds like what it is. It does not sound at the played note, and it cannot be made to
without ceasing to be a waterphone.

## What it is

A steel bowl with bronze rods of unequal length brazed round its rim and WATER sealed
inside. The rods are cantilevers — clamped at one end, free at the other — and a
cantilever's mode ratios are **1 : 6.267 : 17.55 : 34.39**. Not the 1 : 2.76 : 5.40 of a
free-free bar, and nothing like a harmonic series. That is why it reads as neither pitched
metal nor a bell: `struck_bar`, `glass`, `cymbal` and `handpan` are all far closer to
harmonic. The water is the second half: as it moves it loads the rods, and a cantilever's
upper modes are much more mass-sensitive than the bowl's fundamental, so the high
inharmonic partials GLIDE while the note stays put. Two axes: `water` (how much is in the
bowl) and `bow` (how hard the rod is bowed).

## There was no conflict. The meter was wrong.

**Resolved 2026-07-25, by measurement, against everything below this heading.** The body at
×1 — the physical weighting, unchanged — sounds at the played note and always did:

```
partial levels at a 220 Hz note, band peaks, the body below as written
  played note              67.4 dB   <- the loudest bin in the whole spectrum
  cantilever 6.267x        54.1
  17.55x                   50.8
  34.39x                   38.2
```

The fundamental is 13.3 dB above the mode the paragraph below claims dominates it, and it is
the strongest component of the sound. `tools/lco_param_audit.py`'s `tracking()` — which
renders 110→880 Hz and correlates, and is the check that tool documents as the valid one for
an inharmonic set — returns `r_note 0.675, r_fixed −0.042, verdict 'tracks'`. And
`lco_axis_probe.py --gate` on it: **PASS**.

What produced the table below is `f0`, a period estimator, reading 1224 Hz (+2972 cents) on a
set whose partials are 1 : 6.267 : 17.55 : 34.39. That is not a property of the waterphone; it
is the artefact `tools/lco_param_audit.py:38-41` already records for `fm_bell` (−2773 cents,
"not mistuned; it is a bell"). I built a monotone trade-off table, a conflict between two
platform fundamentals, and three options for BJ, on top of a reading my own tools document as
invalid for exactly this class of sound — and never ran the valid check.

So: the waterphone ships at ×1 as written. Nothing needs raising the fundamental, nothing
needs a §4 exception, and there is no decision to make. The rest of this part is kept for the
lesson, not the verdict — that a plausible number from the wrong instrument reads exactly like
a finding ([[feedback_calibrate_the_meter_not_only_the_thing]]).

## The reading that produced the false conflict

The claim was that perceived pitch is whichever mode dominates and that on a real cantilever
that is not the first one — the second mode at 6.267× radiating far more readily. True of a
bowed rod in isolation; not true of this body, as the levels above show. On that premise,
pushing the fundamental up until `f0` finds the played note is a single monotone trade,
measured over all 36 corner-registers and the nine-corner cube at 220 Hz:

| fundamental gain | reads at the played note | no pitch found | median f0 | movement failures |
|---|---|---|---|---|
| ×1 (physical) | 0 of 36 | 14 | **5.88× the note** | 0 of 9 |
| ×4 | 5 of 36 | 15 | 1.32× | 3 of 9 |
| ×8 | 12 of 36 | 11 | 1.04× | 4 of 9 |
| ×16 | 14 of 36 | 5 | 1.02× | 5 of 9 |
| ×32 | **24 of 36** | 0 | 1.01× | **6 of 9** |

At ×1 the reading is 5.3 to 10.2 times the played note wherever it is found at all. At ×32
it is the note — and the fundamental is then about 30 dB above the cantilever modes, which
makes them decoration on a sine. That is not a waterphone; `struck_bar` and `glass` already
cover struck metal that sounds at its note.

Nothing in between works either: at every intermediate weighting the movement failures are
already there before the pitch arrives. The two requirements move in opposite directions
across the whole range, monotonically, so there is no setting to find.

## Why it read as a conflict and not as a defect

The argument was: §4 gives pitch to the synth, movement-by-default is a platform fundamental,
and this instrument's defining physics — a mode series whose second member is 6.267× the first
**and louder than it** — is incompatible with the first unless the physics is overridden,
which costs the second. Every step of that follows, and the emphasised clause is the one that
is measurably false of the body: the fundamental is 13.3 dB *above* the 6.267× mode. A
conflict between two fundamentals is the most interesting thing a measurement can report,
which is precisely why it should have been the most suspected. The single check that would have
killed it — the strongest partial in the spectrum — costs one FFT.

The two requirements do genuinely move in opposite directions once the fundamental is raised;
the tables above are accurate about that. They are answers to a question that did not need
asking.

## The body

Complete and gate-clean, reproduced in full. This is what ships.

```
kwater  = 0.45                              ; water [0..1]: how much water is in the bowl
kbow    = 0.50                              ; bow [0..1]: how hard the rod is bowed
kf0     = (kfreq * koct1)
aex     rand 0.06, 0.5, 1
kslo    poscil 0.5, 1.32                     ; the water moving in the bowl
kbnd    = 1 - (0.16 + 0.18 * kwater) * (0.5 + kslo)   ; loading, upper modes only
kq      = 220 + 900 * kbow
kn0     limit kf0, 20, 15000
an0     mode aex, kn0, kq
kn1     limit kf0 * 6.267 * kbnd, 20, 15000
an1     mode aex, kn1, kq * 1.15
kn2     limit kf0 * 17.55 * kbnd * kbnd, 20, 15000
an2     mode aex, kn2, kq * 1.30
kn3     limit kf0 * 34.39 * kbnd * kbnd * kbnd, 20, 15000
an3     mode aex, kn3, kq * 1.45
kg1     = 0.62 * 6.267 * (0.35 + 0.85 * kbow)
kg2     = 0.40 * 17.55 * (0.15 + 1.05 * kbow)
kg3     = 0.22 * 34.39 * (0.22 + 0.52 * kbow)
amix    = an0 * 1.00 + an1 * kg1 + an2 * kg2 + an3 * kg3
aref    poscil 0.3000, 400
asig    balance amix, aref, 1
```

Two things in it are worth keeping whatever is decided. `mode` hands a resonance at f/2
twice the amplitude of one at f for the same drive, so every weight here carries its own
frequency ratio — learned on `handpan`, where omitting that put the pitch 1207 cents low.
And the water's bend has a FLOOR no setting removes, for the same reason `tanpura`'s halo
does: a waterphone with no water in it is not a dry waterphone, it is a bowl with rods
brazed on, which is `cymbal`.
