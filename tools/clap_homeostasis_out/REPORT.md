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
the expected residue of pure proportional control. An integral term does **not** safely
remove it here — see the PI experiment below; the drift is asymmetric, so the integral
overshoots. Default is therefore pure P.

### Random seed — verdict flips between runs (noise-confounded)

Each loop draws *independent* seeds, so the single-run ranking is unstable:
- Run A: controlled 0.054, unc0.05 0.047, unc0.50 0.062 → *not* better (unc0.05 luckily drifted toward the setpoint).
- Run B: controlled 0.034, unc0.05 0.086, unc0.50 0.044 → better (unc0.05 stayed pinned high).

The controller is the more *consistent* one — it actively walks cos toward the setpoint
either way — but with sigma's authority swamped by seed noise, a single run cannot
settle the ranking; you would need to average over seeds. Qualitatively robust across
both runs: the baselines tangle and sigma cannot cleanly separate them.

## PI experiment — why the integral hurts (the drift is asymmetric)

To remove the P offset (fixed) and beat the seed noise (rand), I added an integral term
+ EMA measurement filter + anti-windup (`--ki 1.0 --ema 0.6`, 16 iters). It made both
regimes **worse**: fixed settling 0.066 (vs P 0.036), rand 0.028 (tied the noisy
baseline). The default is therefore pure P (`--ki 0`); PI stays an opt-in knob.

The reason is a real plant property. Compare the same actuator value (sigma=0.50)
reached two ways in the fixed regime:

| how sigma=0.50 was reached | cos |
|---|---:|
| constant from iter 1 (`unc_hi`, never drifted) | 0.922 |
| after drifting down first, then railed (PI ctrl it13–16) | 0.78–0.85 |

Same sigma, ~0.12 lower cos — the only difference is **history**. Once the sound has
drifted, max sigma only *partially* pulls it back (the drifted init_audio is ~half the
signal and holds it there); it does not recover to where constant-high sigma sits, nor
climb back over the following iterations. **Sigma brakes the drift but barely reverses
it — the loop drifts more easily than it returns.** An integral assumes accumulated
error is correctable in both directions; here downward overshoot past the setpoint is
unrecoverable, so the integral winds up and rails (sigma → 0.50, cos stuck ~0.80, below
target). Pure proportional — approach the setpoint from above and *stop* — is the right
controller for an irreversible plant.

(Aesthetic corollary: the machine's self-listening has a *direction* — its drift is not
time-symmetric. A small, concrete instance of the loop's pull toward its own attractor.)

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
   authority** over a short horizon, so the single-run P-control verdict flips between
   runs (average over seeds to settle it).
4. **The drift is asymmetric / irreversible.** Sigma brakes the drift but barely
   reverses it (same sigma=0.5 holds cos 0.92 from the anchor but only ~0.80 from a
   drifted state). So an **integral overshoots and is the wrong tool**; pure P, which
   approaches from above and stops, is correct. The machine's self-listening has a
   direction.

## Limitations / next steps

- Pure P leaves a small steady-state offset (~0.85 vs 0.88). Living with it is correct
  here — an integral overshoots the asymmetric plant (see PI experiment). A *one-sided*
  brake (only act when over the setpoint) or gain-scheduling would tighten it safely.
- Random-seed regime: average the comparison **over seeds**, and/or use a **longer
  horizon** so systematic drift clears the seed-noise floor. Measurement filtering helps
  the decision but adds lag (bad for the asymmetric plant, where a late brake overshoots).
- This is a Python PoC on the resynth harness. Plugin wiring (CLAP listener → existing
  auto-regen/drift machinery) is the separate, larger build — and the asymmetry says the
  homeostat should be designed as a *drift brake*, not a setpoint servo.
