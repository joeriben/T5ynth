#!/usr/bin/env python3
"""Validate the deviation-RELEASE law for the Resynth feedback loop.

Background (measured in the prior static sweep, saved in ab_summary.json): with
init_audio attached at ANY sigma below 0.5, a switched-in prompt B never arrives
cleanly -- the loop converges to a mush attractor (centroid stalls at ~half of
B's, timbre_B plateaus ~0.25 fixed / ~0.45 rand). The init's low-frequency
content is a hard anchor.

The design (co-developed with the user) is a 2-axis controller, gated on whether
the t5osc PARAMETERS changed -- NOT on the WAV-delta alone:

  (1) params UNCHANGED + delta falling  -> autonomous freeze -> inject noise
                                           (today's anti-convergence; unchanged)
  (2) params CHANGED                     -> RELEASE (detach init -> render the new
                                           content clean) then RE-LOCK (resynth ->
                                           the set level) once the output settles.

This script validates branch (2) on the A/B switch (the prompt change IS the
param-change event):

  static   : baseline -- keep init attached through the switch (the mush).
  release  : on the switch, ONE detached round (no init = pure prompt B = clean),
             then re-attach at the set level and loop -> does it HOLD B, or does
             the carryover drag it back to mush?  <-- the real question.

The re-lock is the crux: after the detach, init == clean B, so the carryover now
*agrees* with the B prompt (bright anchors bright) instead of fighting it (bass
vs chimes). Prediction: it holds. Measured here, not assumed.

Switching prompt_a between A and B at alpha=-1.0 is wire-faithful to the plugin's
alpha-square between two filled fields (see test_ab_resynth history / the backend
lone-prompt echo-through-null). Reuses the validated IPC client + metrics.

Usage:  .venv/bin/python tools/test_ab_resynth.py [model] [warmup] [test]
Output: tools/resynth_loop_out/ab_release_summary.json + WAVs under ab_release/.
"""

from __future__ import annotations

import json
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_init_audio import PipeClient, encode_init_audio, write_wav, corr_to  # noqa: E402
from test_resynth_loop import timbre_corr, spec_centroid, rms  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[1]
BACKEND_SCRIPT = REPO_ROOT / "backend" / "pipe_inference.py"
OUT_ROOT = Path(__file__).resolve().parent / "resynth_loop_out"

MODEL = sys.argv[1] if len(sys.argv) > 1 else "stable-audio-3-small-music"
WARMUP = int(sys.argv[2]) if len(sys.argv) > 2 else 4
TEST = int(sys.argv[3]) if len(sys.argv) > 3 else 10

PROMPT_A = "warm analog bass drone"
PROMPT_B = "bright glittering metallic chimes"
SEED = 12345
STEPS, CFG, DURATION_S = 8, 1.0, 3.0

# Resynth set-levels to re-lock at, with the plugin mapping sigma = 0.50 - 0.45*r.
# Min = the smallest detent (where the mush is worst & "B gets a chance" matters);
# Mid tests that release+re-lock generalises to a stronger anchor too.
LEVELS = [("Min", 0.05), ("Mid", 0.50)]
SEEDMODES = ["fixed", "rand"]
MODES = ["static", "release"]


def sigma_of(resynth: float) -> float:
    return 0.50 - 0.45 * resynth


def log(m: str) -> None:
    print(m, file=sys.stderr, flush=True)


def main() -> int:
    if not BACKEND_SCRIPT.is_file():
        log(f"ERROR: backend not found at {BACKEND_SCRIPT}")
        return 1
    out_dir = OUT_ROOT / "ab_release"
    out_dir.mkdir(parents=True, exist_ok=True)
    t0 = time.time()
    client = PipeClient([sys.executable, str(BACKEND_SCRIPT)])
    try:
        models = client.info.get("models", [])
        model = MODEL if MODEL in models else (models[0] if models else None)
        if not model:
            log("ERROR: backend discovered no models")
            return 1
        base = {"model": model, "duration": DURATION_S, "steps": STEPS,
                "cfg_scale": CFG, "alpha": -1.0}

        # Stable anchors (fixed seed) -----------------------------------------
        a = client.request({**base, "prompt_a": PROMPT_A, "seed": SEED})
        b = client.request({**base, "prompt_a": PROMPT_B, "seed": SEED})
        orig_A, dest_B, sr = a["audio"], b["audio"], a["sample_rate"]
        A_cen, B_cen = spec_centroid(orig_A, sr), spec_centroid(dest_B, sr)
        write_wav(out_dir / "orig_A.wav", orig_A, sr)
        write_wav(out_dir / "dest_B.wav", dest_B, sr)
        log(f"model={model}  A_cen={A_cen:.0f}Hz  B_cen={B_cen:.0f}Hz  "
            f"warmup={WARMUP} test={TEST}")
        log("(cen×B = output centroid / B's; ~1.0 = as bright as clean chimes. "
            "B-hold = cen×B stays high across the re-lock.)")

        def gen(prompt, seed, init=None, sigma=None):
            req = {**base, "prompt_a": prompt, "seed": seed}
            if init is not None:
                req.update({"init_audio_b64": encode_init_audio(init), "init_audio_sr": sr,
                            "init_audio_channels": init.shape[0], "init_noise_level": sigma})
            return client.request(req)["audio"]

        rows = []
        for seedmode in SEEDMODES:
            def lseed():
                return SEED if seedmode == "fixed" else -1
            for label, resynth in LEVELS:
                sig = sigma_of(resynth)
                for mode in MODES:
                    # entrench A at this level ----------------------------------
                    cur = orig_A
                    for _ in range(WARMUP):
                        cur = gen(PROMPT_A, lseed(), cur, sig)
                    entrench = timbre_corr(orig_A, cur, sr)

                    # switch to B ----------------------------------------------
                    traj = []
                    prev = cur
                    for t in range(1, TEST + 1):
                        if mode == "release" and t == 1:
                            new = gen(PROMPT_B, lseed())               # DETACH: pure B
                        else:
                            new = gen(PROMPT_B, lseed(), cur, sig)     # init attached @ level
                        cenx = spec_centroid(new, sr) / B_cen if B_cen > 1e-6 else float("nan")
                        traj.append({"t": t, "tB": timbre_corr(dest_B, new, sr),
                                     "tA": timbre_corr(orig_A, new, sr), "cenx": cenx,
                                     "delta": 1.0 - corr_to(prev, new), "rms": rms(new)})
                        if t in (1, 2, 3, TEST):
                            write_wav(out_dir / f"{seedmode}_{label}_{mode}_{t:02d}.wav", new, sr)
                        prev = cur = new

                    # B-hold = mean cen×B over the second half of the re-lock
                    hold_half = [m["cenx"] for m in traj[len(traj)//2:]]
                    hold = float(np.mean(hold_half)) if hold_half else float("nan")
                    arr = traj[0]["cenx"]   # brightness reached on the switch/detach round
                    rows.append({"seedmode": seedmode, "level": label, "resynth": resynth,
                                 "sigma": sig, "mode": mode, "entrench": entrench,
                                 "cenx_switch": arr, "cenx_hold": hold,
                                 "finB": traj[-1]["tB"], "traj": traj})
                    log(f"  {seedmode:>5} {label:>3} {mode:>7}: entrenchA={entrench:+.2f}  "
                        f"cen×B switch={arr:.2f} hold={hold:.2f}  finB={traj[-1]['tB']:+.2f}")

        with open(OUT_ROOT / "ab_release_summary.json", "w") as f:
            json.dump({"model": model, "A_cen": A_cen, "B_cen": B_cen,
                       "warmup": WARMUP, "test": TEST, "rows": rows}, f, indent=2)

        # comparison table -----------------------------------------------------
        log("\n" + "=" * 78)
        log(f"RELEASE vs STATIC   (A_cen={A_cen:.0f}  B_cen={B_cen:.0f}; cen×B 1.0 = clean B)")
        log(f"{'seed':>5} {'lvl':>3} {'mode':>7} {'σ':>5} {'cen×B@switch':>12} {'cen×B hold':>11} {'finB':>6}")
        for r in rows:
            log(f"{r['seedmode']:>5} {r['level']:>3} {r['mode']:>7} {r['sigma']:>5.2f} "
                f"{r['cenx_switch']:>12.2f} {r['cenx_hold']:>11.2f} {r['finB']:>+6.2f}")
        log("=" * 78)
        log(f"\nDone in {time.time()-t0:.0f}s. Artifacts in {out_dir}")
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    sys.exit(main())
