# Session 16 Handover — Ladder Overdrive ≠ Minimoog

**Status: unresolved. Previous attempt made the drive character worse.**
**Priority: high. This is a perceptual-quality regression, not a bug in the abstract.**

---

## 1. What the user wants (acceptance target)

Reference hardware / software whose behaviour **must** be matched in character:

- **Minimoog Model D** — cranking the external-input level into the filter and pushing Resonance to 10 makes the filter **ROAR** in self-oscillation. The resonant peak gets **louder, richer, more harmonic** as drive increases. It does **not** disappear.
- **Softtube Model 72** (Minimoog emulation) — same animal screaming behaviour at high drive + high resonance.
- **U-he Diva VCF1** (Ladder mode, "dirty" character) — same.
- **Arturia Mini V** — same.

The target character can be summarised in three rules:

1. **At r = 1, the filter self-oscillates on every drive setting** (not just low-drive).
2. **Increasing drive at r = 1 makes the self-oscillation louder and more harmonically rich**, not quieter. It "peaks the overtones of an extreme square wave" (user's phrasing).
3. **At moderate resonance (r = 0.6-0.8) + high drive**, the resonant peak is a strong, audible formant on top of the saturated signal. Not a distant "chirp". Not "the same as r = 0".

---

## 2. What currently happens (broken)

Across three successive fixes on branch `codex-linux-package-ci`, drive behaviour has gone:

### Version A (pre-session-15) — linear feedback tap was a hard-clamped `tanh(y4)`
- Resonance slider "controlled something" but the peak never rang up.
- No self-oscillation at any drive.
- Problem: loop gain capped at `k · tanh_max = 4.2 · 1`, insufficient.

### Version B (commit `b9d84d67`) — feedback linearised to `k · y4`
- At **drive = 0 dB**: classical Moog resonance, self-oscillates at r ≈ 1. ✅
- At **drive = 12-18 dB**: resonance visibly weakens.
- At **drive = 24-36 dB**: resonance **dead**. r = 0 sounds identical to r = 1.
- Root cause: `hot = input · inDrive` reaches ±63 at 36 dB, per-stage `tanh(hot)` saturates to ±1 permanently, local slope `tanh'(63) ≈ 0`, effective loop gain `= k · (slope)⁴ → 0`.
- Drive character (timbral harmonic push) present but destroys resonance.

### Version C (current WIP, this session) — soft-limit hot to ±2.5 via `tanh` ceiling
- Code change in `src/dsp/MoogLadderFilter.h:101-103` and `src/dsp/CutoffWarpFilter.h:103-105`:
  ```cpp
  constexpr float kHotCeil = 2.5f;
  const float hotRaw = input * inDrive;
  const float hot    = kHotCeil * std::tanh(hotRaw * (1.0f / kHotCeil));
  ```
- Intent: keep per-stage tanh in a regime where its slope is non-negligible so feedback can modulate.
- **Actual result (user report)**: drive character is *gone*. No self-oscillation at any drive, no roar, resonant peak **disappears** as drive increases instead of growing. Worse than Version B.
- Root cause of this failure: the ceiling throws away the signal amplitude that should be pushing the stages into the rich harmonic saturation region. At high drive, `hot` is permanently at ±2.5, feedback can swing the tanh input, but the ladder never experiences the large-signal excitation that produces the characteristic ring.

### What the right answer looks like
- Feedback must still have authority over the signal path (Version B fails this at high drive).
- Signal must still reach large-amplitude saturation (Version C fails this).
- Both simultaneously. See section 5 for candidate algorithms.

---

## 3. Physical intuition — why a real Moog works

This is the piece the previous attempts missed.

In the analog transistor ladder:

- **Input voltage** (`V_in`) can be driven to 5-10 V peak when the drive knob is cranked.
- Each **stage is a differential pair**. Its saturation knee is `±2·V_T ≈ ±50 mV` at the transistor level — very sharp compared to the input signal.
- **The feedback signal is a current** tapped from the output, routed to the input summing node. It's at the **same voltage scale as the stages' internal signals** (tens of millivolts), **not at the input voltage scale**.
- Classical resonance condition: feedback-loop small-signal gain = 1 at ω_c, which sets `k = 4`.
- When `V_in` is huge, the stages hard-saturate. But the **feedback injection shifts the operating point of the first diff-pair** — it doesn't compete with `V_in` as a voltage subtraction in a linear summing node. It biases the transistor currents. Even when `V_in` has the stages pinned, small changes in the feedback bias produce **large swings in the stage output**, because the stage is near the steepest part of its `tanh` curve *in the time-varying reference frame of the bias point*.
- Consequence: at high drive + high resonance, the stage oscillates between its two saturation rails, and the **switching frequency is locked to the resonance frequency** by the feedback phase. That's the ROAR — self-oscillation at `ω_c` modulating a square wave at `ω_c`. Rich in harmonics.

The naive digital tanh ladder throws this away because it models the feedback as a **voltage subtraction at the stage input**, before the tanh. When the tanh is saturated, the subtraction is invisible. The analog circuit doesn't do that subtraction there.

---

## 4. What has been tried in this repo

In chronological order, all on `codex-linux-package-ci`:

- `9e5ef952` "proper Moog drive + Warp DC bias + half-sample delay comp" — introduced the pre-filter gain topology, added Stilson-Smith half-sample comp in the feedback path.
- `d0f78364` "ladder/warp drive makes louder, level parity with SVF" — level-matching work.
- `b9d84d67` "linear feedback tap so resonance actually rings / self-oscillates" — feedback went from `k · tanh(y4)` to `k · y4`. Fixed resonance at low drive. **This is Version B above.**
- (uncommitted) soft-ceil on `hot` — **Version C above, reported as worse**.

The **"per-style k-scale"** added to CutoffWarpFilter this session (0.65 Sin, 1.35 SoftClip, etc.) is an **orthogonal** tuning improvement and should probably survive independent of the drive fix — but it also needs re-tuning once the drive topology is correct.

---

## 5. Candidate algorithms (ordered by expected correctness / implementation cost)

### A. Thermal-voltage-normalized ladder (Huovilainen DAFx-04, "modified")

Replace per-stage `tanh(x)` with a thermal-voltage form:

```cpp
// Vt sets the saturation knee. Smaller Vt → harder saturation, more
// harmonics; also more "feedback authority" because the slope scales with 1/Vt.
constexpr float kVt = 0.4f;   // tune by ear, expect 0.3-1.0

// Stage output saturation:
auto satStage = [](float x) { return 2.0f * kVt * std::tanh(x / (2.0f * kVt)); };
```

Key property: `tanh(x / (2·Vt))` has slope `1/(2·Vt)` at zero, so the *local loop gain per stage at zero* is `1/(2·Vt)` — boosting `Vt < 0.5` makes the effective loop gain **larger**, compensating for the per-stage gain loss when operating at high drive.

This is the canonical Huovilainen / Stilson-Smith form. It's known to give correct Moog character in production plugins.

**Implementation**:
- Replace `std::tanh(y)` calls in `MoogLadderFilter::processSample` with `satStage(y)`.
- Keep `hot = input * inDrive` (no ceiling). Drive goes into the stages as intended.
- Tune `kVt` empirically — start at 0.5, listen to the self-oscillation at r = 1 across drive range.
- Feedback stays linear on `y4`, which is now bounded to `±2·Vt` instead of `±1`. The feedback coefficient `k` may need re-tuning (self-osc threshold shifts).

### B. Drive-inside-the-loop topology

Apply drive **after** the summing node, not before:

```cpp
const float halfIn = 0.5f * (input + xPrev);   // undriven
xPrev = input;
const float fbIn  = (halfIn - k * y4) * inDrive;  // amplify the DIFFERENCE
const float in0   = std::tanh(fbIn);           // saturate
// ... ladder stages as before
```

Key property: when feedback cancels the input (resonant condition), the difference is small and `fbIn · inDrive` is also small — the tanh is in its linear region. When feedback is in phase with input (non-resonant), `fbIn` is amplified and saturates harder.

This gives the resonance a "breathing" character on top of heavy drive. It's a known trick in some ladder emulations.

**Caveat**: changes the character vs. a real Moog (drive is externally applied, not intra-loop). Quick to try as a one-line change; worth it as a diagnostic.

### C. Cytomic-style state-variable physical ladder

Andrew Simper's analog-matched approach (cytomic.com/technical-papers/). Uses a different integrator topology and explicit `1/cosh²` factors to linearise the small-signal loop gain. Most physically accurate, most expensive.

Not recommended as a first try unless A and B both fail — the implementation complexity is high (need to derive the coefficients, test stability numerically).

### D. RK4 + Huovilainen per-stage TV

Surge XT's `VintageLadders.cpp` does this. Integrates the ladder ODE with RK4 for accurate transient response of the nonlinearity. **Expensive** — 4× the per-sample cost. In this project we have 16 voices × 48 kHz × 4× oversample for drive → ~12 MFlops/stage extra. Not obviously OK on low-end hardware, but worth testing once we know it sounds right.

---

## 6. What to do first

1. **Revert version C** (the hot-ceiling) — it's strictly worse than Version B. Keep commit `b9d84d67` as the baseline.
2. **Try algorithm A (thermal-voltage normalized stages)** first. It's the smallest change (≈5 lines in `MoogLadderFilter.h`), it's the canonical Moog algorithm, and it directly addresses the loop-gain-collapse-at-high-drive failure mode.
3. **Keep** the per-style k-scale in `CutoffWarpFilter` but re-verify by ear after (2) is working.
4. Listen test across the full drive range at r = 1 on **every** ladder style. Self-oscillation must be audible everywhere, with the character moving from "pure sine ring" at 0 dB to "harmonic roar" at 36 dB.

---

## 7. Files in scope

- `src/dsp/MoogLadderFilter.h` — primary fix target (algorithm A or B).
- `src/dsp/CutoffWarpFilter.h` — same architecture; mirror whatever lands in MoogLadder. Per-style k-scaling stays.
- `src/dsp/SynthVoice.cpp:642-671` — filter dispatch. No changes expected.
- `src/dsp/BlockParams.h:820-826` — drive dB-to-gain math. No changes expected; but the drive range (0-36 dB) can be reconsidered if algorithm A allows it.

---

## 8. Acceptance test (ear test, no scripting)

Set up a single-voice patch with:
- Engine: a clean sawtooth wavetable at C3.
- Filter: Ladder (or Warp, once Ladder passes).
- Cutoff: ~400 Hz.
- Kbd Track: 0.
- Slope: 24 dB.
- Type: LP.

Then, with sustained note held:

| Drive  | Resonance | Expected                                                              |
|--------|-----------|-----------------------------------------------------------------------|
| 0 dB   | 0         | Dull lowpass.                                                         |
| 0 dB   | 1.0       | Clean sine self-oscillation at ~400 Hz, slight saw bleed.              |
| 12 dB  | 1.0       | Louder self-oscillation, some overtones, saw audibly pushed.          |
| 24 dB  | 1.0       | **ROAR** — strong harmonic ring, formant character.                   |
| 36 dB  | 1.0       | Peak roar — gnarly, harmonics everywhere, unmistakably self-oscillating. |
| 36 dB  | 0.6       | Resonant peak audible as a formant on top of the saturated saw.       |
| 36 dB  | 0         | Saturated saw, no resonance (filter still lowpass-ish).               |

If any cell fails ("identical to r = 0", "peak disappears", "no self-osc"), the algorithm is not done.

---

## 9. Out of scope for this ticket

- Filter startup display (fixed this session, in `SynthPanel.cpp`, direct APVTS read of toggle states). Do not touch.
- Preset save/load (audited this session, all filter fields are present with graceful old-preset fallbacks). Do not touch.
