# PARKED: bespoke DCO A/B presence crossfade ramp

**Status:** PARKED 2026-07-15. NOT in the live code. The live path uses the
standard per-source smoothed-gain mix instead (`juce::SmoothedValue` per
oscillator gain in `SynthVoice`). This document + the accompanying
`dco_presence_ramp_bespoke.diff` preserve the bespoke implementation and the
analysis behind it so nothing is lost.

**Why parked:** the bespoke ramp re-derived audio-mixing behaviour that is a
solved problem. A click-free per-oscillator level change on a held note is done
in every DAW/soft-synth with one smoothed gain per source (each slews to its own
target). The bespoke approach instead crossfaded the *mix vector* with an
equal-power (cos/sin) law, which introduced a >unity gain overshoot that then
needed a hybrid law + a per-component re-arm test to suppress — complexity that
independent per-source gain smoothing avoids entirely (monotonic per gain ⇒ no
overshoot possible). Re-apply only if a future requirement genuinely needs the
mix-vector-crossfade semantics; otherwise the standard mix is correct.

To restore verbatim: `git apply docs/parked/dco_presence_ramp_bespoke.diff` from
the repo root (applies on top of the committed HEAD dual-osc hard-cut mix).

---

## What it solved

Held-note A/B presence hard-cut. Each `SynthVoice` sums two oscillators — `osc`
(A, harmonic wavetable) and `oscB` (B, real-time inharmonic additive). A DCO
recipe sets which is audible via `BlockParams::dcoOscAHasContent` /
`dcoOscBHasContent` (published by `T5ynthProcessor::setDcoOscBalance`). The
committed mix gated each oscillator's *synthesis* on its content flag, so when a
new recipe flipped a flag on a HELD note the losing oscillator's samples vanished
instantly — a click, violating the CLAUDE.md held-note crossfade invariant.

## The bespoke design (see the .diff for the exact code)

Per-voice presence-gain ramp over the Regen XFade time (`driftCrossfade`):

- New `BlockParams::driftCrossfade` (mirrored each block in `processBlock`) sizes
  the ramp; `setDcoOscBalance`'s 4 stores wrapped in `getCallbackLock()` so the
  flags+gains are read as a consistent group.
- `SynthVoice` state: `presGainA_/presGainB_` (emitted gains), start captures,
  `presRampT_`/`presRampInc_`, `presAAudible_/presBAudible_` (flip detection),
  `presInit_` (first-block snap), `presRampEqualPower_` (law choice).
- Decoupled *synthesis* from the audible flag (`aInvolved`/`bInvolved` =
  `hasFrames && (audible || ramping)`) so a fading-OUT oscillator keeps producing
  samples while its gain rings out — the actual fix for the hard cut.
- Target gains from the discrete audible set (`dcoTargetGains`): solo = unity,
  dual = equal-power `oscMix` × R1 energy gains.

## The expensive insight (why the naive equal-power ramp was wrong)

Interpolating the **gain vector** with cos/sin (`wv·start + w·target`,
`wv=cos(t·π/2)`, `w=sin(t·π/2)`) is power-preserving only for **uncorrelated**
signals. The start and target mixes share the SAME live `aSample`/`bSample`
(correlated), so on any transition where an oscillator is audible in BOTH states
the fading-in gain overshoots to `sqrt(start² + target²) > max(start,target)` —
e.g. A-solo→dual (m=0.5): A's gain bulges to 1.225 (peak power 1.707), driving
the pre-VCA signal above unity into the drive/filter. Numerically confirmed
(cos/sin peak gain 1.225 / power 1.707 for dual transitions; 1.000 only for the
disjoint A↔B swap).

**Bespoke remedy (hybrid law):** use cos/sin ONLY when the crossfade is
overshoot-free — i.e. for each oscillator at least one of {start, target} gain is
zero (a disjoint handoff, the buffer-morph analogy); use monotonic **linear**
otherwise. A first version keyed this off the audible *sets*, which is a valid
proxy ONLY from a canonical steady-state start; a **mid-ramp re-arm** captures an
in-flight start with both gains nonzero, so the set test wrongly re-picked cos/sin
and re-introduced the overshoot (A-solo→B-solo interrupted at 25% then →A-solo:
gain to 1.36). Final bespoke fix tested the actual start/target **vectors**
per-component (`aBothNonzero`/`bBothNonzero`).

The whole hybrid + per-component dance exists solely to tame the cos/sin
overshoot. Per-source smoothed gain has no overshoot to tame — which is why the
standard mix is the right tool and this is parked.

## Audition guard (KEPT LIVE — validates the standard mix too)

`tools/audition_dco_presence_follow.cpp` drives a real `SynthVoice` through
presence flips and asserts each seam is click-free, swell-free (peak ≤ endpoint),
and actually transitioning, incl. a mid-ramp re-arm case. Negative controls
proved teeth: `driftCrossfade≈0` ⇒ *STEP*; forcing cos/sin everywhere ⇒ *SWELL*.
This guard is engine-agnostic about the transition law, so it validates the
standard SmoothedValue mix unchanged.
