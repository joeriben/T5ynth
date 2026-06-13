# CLAP homeostasis: cybernetic control of the resynth loop

Mode #1 from `docs/SEMANTIC_LOOP_AESTHETICS.md` ("check output, correct
cybernetically / homeostasis") made literal on the existing init_audio resynth
loop. Closed loop:

- **sensor** = CLAP audio-embedding cosine of the current output to a fixed anchor
- **actuator** = `init_noise_level` (sigma) of the next generation
- **law** = proportional, `sigma = clip(BASE + Kp·(setpoint − cos), 0.05, 0.50)`
  (cos below setpoint = drifted too far → raise sigma → pull back toward prompt;
  cos above setpoint = too near → lower sigma → let it drift)

- model (ear): `laion/clap-htsat-unfused` (sine-vs-noise sanity gate passed)
- engine: `stable-audio-3-small-music` (only init_audio-capable engine), MPS
- anchor: `"warm analog bass drone"`, seed 12345, 3.0 s, 8 steps, CFG 1.0, alpha −1.0
- setpoint cos\* = 0.88, Kp = 2.5, BASE = 0.25, 12 iterations
- tool: `tools/clap_homeostasis.py`; data: `results_fixed.json`, `results_rand.json`

The same-prompt loop (anchor and loop prompt identical) is the homeostasis regime —
regulate distance-from-origin *within* a basin. Cross-prompt transport toward a
different B-prompt (cos→0.11, see `clap_followup_out`) is a different mode.

## Plant characterization — the two fixed-sigma baselines ARE the band

The right procedure is *characterize then control*: the uncontrolled fixed-sigma
loops trace the reachable band; the setpoint must sit inside it. (The first run of
this PoC used setpoint 0.5 — far below the band — and the controller correctly but
uselessly pegged sigma at its floor. That run measured the band; 0.88 is mid-band.)

### Fixed seed (`randomSeedToggle = false`) — deterministic, strong attractor

| sigma | it01 | it03 | it06 | it08 | it10 | it12 | settled (last 4) |
|---|---:|---:|---:|---:|---:|---:|---:|
| 0.05 (low)  | 0.974 | 0.959 | 0.940 | 0.863 | 0.832 | 0.837 | **0.837** |
| 0.50 (high) | 0.956 | 0.930 | 0.934 | 0.908 | 0.925 | 0.923 | **0.924** |

Clean and monotonic: low sigma drifts away (to ~0.84), high sigma stays pinned
(~0.92). sigma is a well-behaved actuator. **Band ≈ [0.837, 0.924] — narrow:** even
maximal drift over 12 round-trips only moves CLAP cosine from 0.97 to 0.84. The SA3
resynth loop at a fixed prompt is a **strong semantic attractor**; sigma is a
fine-tuning knob within a basin, not wide-range transport. (This quantifies the
loop-attractor pull the i2i signal loop also carries; the homeostat is its antidote.)

### Random seed (`seed = -1`, matches plugin "Automatic variation") — noise swamps the knob

| sigma | it01 | it03 | it05 | it08 | it10 | it12 | settled (last 4) |
|---|---:|---:|---:|---:|---:|---:|---:|
| 0.05 (low)  | 0.976 | 0.945 | 0.966 | 0.948 | 0.939 | 0.842 | 0.908 (still falling) |
| 0.50 (high) | 0.965 | 0.954 | 0.883 | 0.947 | 0.961 | 0.932 | 0.942 |

The two baselines **cross** (it05: low 0.966 > high 0.883; it10: high 0.961 > low
0.939): a fresh seed each iteration injects incoherent noise that **partially cancels
instead of compounding**, so (a) systematic drift is *delayed* — low sigma stays high
to ~it10, only breaking down at it11–12 (0.894 → 0.842) — and (b) the per-iteration
seed noise **swamps sigma's authority** over the regulated variable on a 12-iter
horizon. Contrary to the naive guess, high sigma does **not** drift more under random
seeding: the same prompt re-renders into the same region regardless of seed.

## Controlled loop (P-control)

### Fixed seed — regulation works

| iter | ctrl σ | ctrl cos | unc05 cos | unc50 cos |
|---:|---:|---:|---:|---:|
| 6 | 0.055 | 0.939 | 0.940 | 0.934 |
| 7 | 0.103 | 0.900 | 0.909 | 0.919 |
| 8 | 0.200 | 0.849 | 0.863 | 0.908 |
| 9 | 0.328 | 0.833 | 0.839 | 0.923 |
| 10 | 0.368 | 0.853 | 0.832 | 0.925 |
| 12 | 0.349 | 0.850 | 0.837 | 0.923 |

The controller **actively modulates** sigma (0.05 → 0.10 → 0.20 → 0.37) in response to
measured drift, and corrects: when cos overshot below setpoint (it08–09, 0.849 →
0.833) sigma ramped up and cos recovered (it10, 0.853). Settling |cos − 0.88| over
the last 4: **controlled 0.036 < unc0.05 0.043, unc0.50 0.043** → holds the setpoint
**better than both fixed-sigma loops**. Visible steady-state offset (~0.85 vs 0.88) is
the expected residue of pure proportional control; an integral term would remove it.

### Random seed — regulation insufficient on this horizon

Settling |cos − 0.88|: controlled 0.054, unc0.05 0.047, unc0.50 0.062 → **not better.**
With sigma's authority swamped by seed noise, the controller keeps sigma low (cos
hovered above setpoint, 0.90–0.97, almost throughout) and never gets the loop down to
0.88 within 12 iters. **Caveat:** the three loops drew *independent* random seeds, so
the precise error ranking is within run-to-run noise — but the qualitative result
(baselines tangle, sigma can't separate them) is robust across the trajectory.

## Findings

1. **The cybernetic loop closes and regulates** in the deterministic regime: a CLAP
   sensor → sigma actuator P-loop holds a mid-band CLAP-cosine setpoint better than
   either fixed-sigma extreme, with visible corrective action. Di Scipio-style
   structural coupling, made literal and measured.
2. **The SA3 same-prompt resynth loop is a strong semantic attractor.** Reachable
   CLAP-cosine band over 12 deterministic iterations is narrow (~[0.84, 0.92]); the
   loop "wants" to stay near its prompt-anchored region. Homeostasis here is
   fine-stabilisation within a basin, not wide transport.
3. **Regime matters.** Random seeding (the plugin's Automatic-variation mode) does
   *not* loosen the attractor — incoherent noise delays drift and **swamps sigma's
   authority** over a short horizon, so simple P-control on cos-to-anchor does not
   reliably regulate there.

## Limitations / next steps

- Pure P-control leaves steady-state offset → add an **integral** term (PI) with
  anti-windup for the narrow, slow plant.
- Random-seed regime needs **measurement filtering** (average cos over a window) and/or
  a **longer horizon** so the systematic drift clears the seed-noise floor before the
  actuator acts; the single-run comparison should be **averaged over seeds**.
- This is a Python PoC on the resynth harness. Plugin wiring (CLAP listener → existing
  auto-regen/drift machinery) is the separate, larger build.
