#!/usr/bin/env python3
"""Homeostasis-mode proof-of-concept: CLAP-sensed cybernetic control of the
Resynth feedback loop.

Mode #1 from docs/SEMANTIC_LOOP_AESTHETICS.md ("check output and correct
cybernetically / homeostasis"). The Resynth loop drifts: low init-noise sigma
lets each iteration accumulate change (drifts away, slowly, to a near asymptote),
high sigma washes out toward the prompt (stays near). Here we close a control loop:

    sensor    = CLAP audio-embedding cosine of the current output to an anchor
    actuator  = init_noise_level (sigma) of the next generation
    law       = proportional: sigma = clip(BASE + Kp * (setpoint - cos), 0.05, 0.5)
                (cos below setpoint = drifted too far -> raise sigma -> pull back;
                 cos above setpoint = too near        -> lower sigma -> let it drift)

The demonstration: a CONTROLLED loop holds cos at a setpoint INSIDE the loop's
reachable band, where the UNCONTROLLED fixed-sigma loops drift to the band edges
(low sigma drifts to the lower edge, high sigma stays pinned at the upper edge).
Empirically (warm-analog-bass-drone anchor, seed 12345, 12 iters) that band is
NARROW — cos in ~[0.835, 0.922]: even maximal drift over 12 round-trips only
moves CLAP cosine from 0.97 to 0.84, so the SA3 resynth loop at a fixed prompt
is a STRONG semantic attractor (sigma is a fine-tuning knob within a basin, not
wide-range transport). The setpoint therefore defaults to 0.88 (mid-band); a
setpoint outside the band just saturates the actuator. This is Di Scipio-style
structural coupling made literal — the system regulates its own distance-from-
origin, and the band measurement quantifies the attractor pull that the i2i
signal loop also carries (the homeostat is its antidote).

NOTE on procedure: the right order is CHARACTERIZE then CONTROL. The two fixed-
sigma baselines below double as the plant characterization (they trace the band
edges); pick the setpoint inside the band they reveal. The first run of this PoC
used setpoint 0.5 (outside the band) and the controller correctly but uselessly
pegged sigma at its floor — that run measured the band; this one controls within it.

Reuses the plugin-identical IPC path (test_init_audio) for generation and the
validated CLAP ear (clap_probe, default laion/clap-htsat-unfused with the
sine-vs-noise sanity gate). Generation runs against stable-audio-3-small-music
(the only init_audio-capable engine). Read-only; writes only under --out.

Usage:
    .venv/bin/python tools/clap_homeostasis.py
    .venv/bin/python tools/clap_homeostasis.py --iters 16 --setpoint 0.45
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

import numpy as np
import torch
import torchaudio

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_init_audio import PipeClient, encode_init_audio, write_wav  # noqa: E402
import clap_probe as cp  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[1]
BACKEND_SCRIPT = REPO_ROOT / "backend" / "pipe_inference.py"

SOURCE_PROMPT = "warm analog bass drone"
SEED = 12345
STEPS, CFG, DURATION_S = 8, 1.0, 3.0
SIGMA_LO, SIGMA_HI = 0.05, 0.50


def log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


def clap_emb(model, proc, audio_cs: np.ndarray, sr: int, device: str) -> torch.Tensor:
    """CLAP audio embedding (normalized) of an [channels, samples] array."""
    mono = audio_cs.mean(axis=0).astype("float32")
    if sr != cp.CLAP_SR:
        mono = torchaudio.functional.resample(torch.from_numpy(mono), sr, cp.CLAP_SR).numpy()
    return cp.embed_audios(model, proc, [mono], device)[0]


def run_loop(client, base, anchor_audio, anchor_emb, sr, model, proc, device,
             iters, sigma_fn, label, save_dir=None, seedmode="fixed"):
    """Run the resynth loop; sigma_fn(cos_cur, it) -> sigma. Returns trajectory.

    seedmode 'fixed' (matches randomSeedToggle=false): same seed-noise blended in
    every round -> the loop converges to a deterministic fixed point (strong
    attractor). 'rand' (matches the plugin's "Automatic variation", seed=-1 ->
    backend randomizes): fresh noise each iteration -> the loop wanders, so high
    sigma no longer pins near the anchor. The anchor itself is always at the fixed
    SEED so the cosine reference is stable.
    """
    loop_seed = SEED if seedmode == "fixed" else -1
    cur = anchor_audio
    cos_cur = 1.0
    traj = []
    for it in range(1, iters + 1):
        sigma = float(np.clip(sigma_fn(cos_cur, it), SIGMA_LO, SIGMA_HI))
        res = client.request({
            **base,
            "prompt_a": SOURCE_PROMPT,
            "seed": loop_seed,
            "init_audio_b64": encode_init_audio(cur),
            "init_audio_sr": sr,
            "init_audio_channels": cur.shape[0],
            "init_noise_level": sigma,
        })
        new = res["audio"]
        cos_new = float((clap_emb(model, proc, new, sr, device) @ anchor_emb).item())
        traj.append({"iter": it, "sigma": sigma, "cos": cos_new})
        log(f"  [{label}] it{it:02d}: sigma={sigma:.3f} -> cos={cos_new:+.3f}")
        if save_dir is not None and it in {1, 4, 8, iters}:
            write_wav(save_dir / f"{seedmode}_{label}_iter{it:02d}.wav", new, sr)
        cur, cos_cur = new, cos_new
    return traj


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--iters", type=int, default=12)
    ap.add_argument("--setpoint", type=float, default=0.88,
                    help="target CLAP cosine to anchor; default 0.88 sits mid-band "
                         "(empirical reachable band ~[0.835,0.922] for the default anchor)")
    ap.add_argument("--kp", type=float, default=2.5,
                    help="proportional gain (narrow authority band -> high gain)")
    ap.add_argument("--base-sigma", type=float, default=0.25)
    ap.add_argument("--seedmode", default="fixed", choices=["fixed", "rand"],
                    help="fixed = deterministic loop (strong attractor, narrow band); "
                         "rand = fresh seed each iter (matches plugin Automatic variation)")
    ap.add_argument("--clap-model", default="laion/clap-htsat-unfused")
    ap.add_argument("--device", default="cpu", choices=["cpu", "mps", "cuda"])
    ap.add_argument("--out", default="tools/clap_homeostasis_out")
    args = ap.parse_args()

    if not BACKEND_SCRIPT.is_file():
        log(f"ERROR: backend not found at {BACKEND_SCRIPT}")
        return 1
    out_dir = REPO_ROOT / args.out
    out_dir.mkdir(parents=True, exist_ok=True)

    # --- CLAP ear ---------------------------------------------------------
    from transformers import ClapModel, ClapProcessor
    log(f"Loading CLAP {args.clap_model} on {args.device} ...")
    cl = ClapModel.from_pretrained(args.clap_model).to(args.device).eval()
    proc = ClapProcessor.from_pretrained(args.clap_model)
    cp.assert_model_sane(cl, proc, args.device)

    # --- backend ----------------------------------------------------------
    t0 = time.time()
    client = PipeClient([sys.executable, str(BACKEND_SCRIPT)])
    try:
        models = client.info.get("models", [])
        model = "stable-audio-3-small-music"
        if model not in models:
            log(f"ERROR: {model} not available; backend has {models}")
            return 1
        log(f"Backend ready. Using {model}.")
        base = {"model": model, "duration": DURATION_S, "steps": STEPS,
                "cfg_scale": CFG, "alpha": -1.0}

        # Anchor = the source generation (fixed seed). Its CLAP embedding is the
        # control reference; cosine to it is the regulated variable.
        anchor = client.request({**base, "prompt_a": SOURCE_PROMPT, "seed": SEED})
        sr = anchor["sample_rate"]
        anchor_audio = anchor["audio"]
        anchor_emb = clap_emb(cl, proc, anchor_audio, sr, args.device)
        write_wav(out_dir / "anchor.wav", anchor_audio, sr)
        log(f"Anchor generated (sr={sr}). setpoint cos*={args.setpoint}")

        SP, KP, BASE = args.setpoint, args.kp, args.base_sigma

        def controller(cos_cur, it):
            return BASE + KP * (SP - cos_cur)   # cos<SP -> raise sigma (pull back)

        sm = args.seedmode
        log(f"\n== CONTROLLED (sigma adapts to hold setpoint) [seed={sm}] ==")
        ctrl = run_loop(client, base, anchor_audio, anchor_emb, sr, cl, proc,
                        args.device, args.iters, controller, "ctrl", out_dir, seedmode=sm)
        log("\n== UNCONTROLLED sigma=0.05 (low) ==")
        unc_lo = run_loop(client, base, anchor_audio, anchor_emb, sr, cl, proc,
                          args.device, args.iters, lambda c, i: 0.05, "unc_lo", seedmode=sm)
        log("\n== UNCONTROLLED sigma=0.50 (high) ==")
        unc_hi = run_loop(client, base, anchor_audio, anchor_emb, sr, cl, proc,
                          args.device, args.iters, lambda c, i: SIGMA_HI, "unc_hi", seedmode=sm)

        # Settling error: mean |cos - setpoint| over the settled tail (last 4),
        # excluding the climb-out transient. Iteration 1 always starts at SIGMA_LO
        # because the anchor seeds cos_cur=1.0 (correct: "at anchor -> drift away"),
        # and the controller takes a few iters to climb toward BASE.
        tail_start = max(1, args.iters - 4)
        def settle(traj):
            tail = [abs(t["cos"] - SP) for t in traj[tail_start:]]
            return float(np.mean(tail)) if tail else float("nan")
        err_ctrl, err_lo, err_hi = settle(ctrl), settle(unc_lo), settle(unc_hi)

        result = {
            "setpoint": SP, "kp": KP, "base_sigma": BASE, "iters": args.iters,
            "seedmode": sm, "sample_rate": sr,
            "controlled": ctrl, "uncontrolled_lo": unc_lo, "uncontrolled_hi": unc_hi,
            "settling_error": {"controlled": err_ctrl, "unc_0.05": err_lo, "unc_0.50": err_hi},
        }
        (out_dir / f"results_{sm}.json").write_text(json.dumps(result, indent=2), encoding="utf-8")

        # --- console summary --------------------------------------------
        log("\n" + "=" * 64)
        log(f"HOMEOSTASIS  setpoint cos*={SP}  seed={sm}  (lower |err| = holds setpoint better)")
        log(f"{'iter':>4} {'ctrl_sig':>9} {'ctrl_cos':>9} {'unc05_cos':>10} {'unc50_cos':>10}")
        for k in range(args.iters):
            log(f"{k+1:>4} {ctrl[k]['sigma']:>9.3f} {ctrl[k]['cos']:>+9.3f} "
                f"{unc_lo[k]['cos']:>+10.3f} {unc_hi[k]['cos']:>+10.3f}")
        log("-" * 64)
        log(f"settling |cos-cos*| (last 4):  controlled={err_ctrl:.3f}  "
            f"unc0.05={err_lo:.3f}  unc0.50={err_hi:.3f}")
        better = err_ctrl < min(err_lo, err_hi)
        log(f"=> controlled loop holds the setpoint {'BETTER' if better else 'NOT better'} "
            "than both fixed-sigma loops")
        log("=" * 64)
        log(f"\nDone in {time.time()-t0:.0f}s. Artifacts in {out_dir}")
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    sys.exit(main())
