# Two finished nature sounds that the movement gate cannot pass

**Status: parked, needs BJ.** Both bodies work, both are physically derived, both hold
loudness better than most of the library. Neither ships, because I cannot pass the
movement gate on them honestly and the alternative is tuning constants until the meter's
own variance falls the right side of a threshold. That is fitting the meter, not making
the sound move, so I stopped and wrote this instead.

No Csound was taken from anywhere. Both are written from published acoustics: the
Minnaert resonance of a gas bubble in water, and flexural-wave dispersion in a thin
plate.

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

Both are complete and gate-clean on everything except `moves`. They are reproduced in full
so this is not a pointer to something lost.

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
