#!/usr/bin/env python3
"""Empirical calibration harness for the Resynth feedback loop.

The plugin's Resynth slider feeds the previous generation back into SA3 as
``init_audio`` with an ``init_noise_level`` (sigma) derived from the slider.
This script runs that loop offline for a sweep of sigmas, iterating ~20 times
per sigma while feeding each output back as the next input, to calibrate the
slider->sigma mapping.

TWO MODES, because "wash out" only means something when the loop has somewhere
to go:

  cross (default) -- the calibration-relevant experiment. The original is made
      from prompt A ("warm analog bass drone"); the loop is then driven by a
      DIFFERENT prompt B ("bright glittering chimes"), feeding each output back.
      This reproduces the user's complaint ("5% holds the old sound, the new
      prompt doesn't come through") and measures the CROSSOVER: how many
      iterations until B's timbre overtakes A's. At the low-Resynth end the old
      audio must shed within <=20 iterations; at the high-Resynth end it should
      persist (the loop holds the source).

  same -- loop driven by the SAME prompt as the original. There is no prompt to
      cross to, so it characterizes the HOLD / slow-drift behavior and the
      fixed-point the loop converges to. (With a fixed seed the loop cannot
      wash out here by construction -- its fixed point IS the source family.)

Design choices, each grounded in the live code path:

  * FIXED seed (matches randomSeedToggle defaulting to false in PromptPanel).
    With a fixed seed the blended noise is identical every round, so the loop
    is the deterministic map  x_{n+1} = denoise( x_n*(1-sigma) + noise*sigma )
    and converges to a sigma-dependent fixed point. That is what the user hears
    by default.
  * steps=8, cfg=1.0 -- the real APVTS defaults and the SA3 model-card values.
  * Primary metric is PERCEPTUAL: Pearson correlation of the TIME-AVERAGED
    log-magnitude spectrum (timbre / spectral balance), which is invariant to
    temporal alignment. The user calibrates by ear; waveform correlation badly
    under-reports timbral stickiness.

Reuses the IPC scaffolding from test_init_audio (PipeClient, encode_init_audio,
write_wav, corr_to) so the wire path is byte-identical to the plugin's.

Usage:
    .venv/bin/python tools/test_resynth_loop.py [model] [iters] [mode]
        model: default stable-audio-3-small-music
        iters: default 20
        mode:  cross (default) | same

Outputs WAVs + manifest.json + summary.json into tools/resynth_loop_out/<mode>/.
"""

from __future__ import annotations

import json
import sys
import time
from pathlib import Path

import numpy as np
from scipy.signal import stft as scipy_stft

# Reuse the exact IPC client + encoders the plugin's wire path uses.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_init_audio import (  # noqa: E402
    PipeClient,
    encode_init_audio,
    write_wav,
    corr_to,
)

REPO_ROOT = Path(__file__).resolve().parents[1]
BACKEND_SCRIPT = REPO_ROOT / "backend" / "pipe_inference.py"
OUT_ROOT = Path(__file__).resolve().parent / "resynth_loop_out"

# ── Experiment config ────────────────────────────────────────────────
MODEL = sys.argv[1] if len(sys.argv) > 1 else "stable-audio-3-small-music"
ITERS = int(sys.argv[2]) if len(sys.argv) > 2 else 20
MODE = sys.argv[3] if len(sys.argv) > 3 else "cross"      # cross | same
SEEDMODE = sys.argv[4] if len(sys.argv) > 4 else "fixed"  # fixed | rand
# fixed (default, matches randomSeedToggle=false): the SAME seed-noise is blended
#   every round -> deterministic map, can lock into a degenerate fixed-noise
#   attractor. rand (matches "Automatic variation"): a fresh seed each iteration
#   -> every round is an independent SDEdit, so the prompt re-asserts cleanly.
# The loop's per-iteration seed differs; the original/destination references are
# always at the fixed SEED so the metrics have a stable anchor.

SOURCE_PROMPT = "warm analog bass drone"               # A: the carried-over original
TARGET_PROMPT = "bright glittering metallic chimes"    # B: drives the loop in cross mode
LOOP_PROMPT = TARGET_PROMPT if MODE == "cross" else SOURCE_PROMPT
SEED = 12345                                           # fixed: matches randomSeedToggle=false

STEPS = 8
CFG = 1.0
DURATION_S = 3.0

# init_noise_level sweep. The slider's current mapping (0.50 - 0.45*amount)
# reaches sigma in [0.05, ~0.4955]; 0.475 ~ the current 5% point. Cover the
# whole reachable band plus extra resolution at the high-sigma (low-Resynth)
# end, since that is where the wash-out requirement lives.
SIGMAS = [0.05, 0.10, 0.15, 0.20, 0.25, 0.30, 0.35, 0.40, 0.45, 0.475, 0.50]

SAVE_ITERS = {1, 3, 5, 10, 15, 20}                     # which iterations to dump as WAV

# ── Perceptual / spectral metrics ────────────────────────────────────

def _logmag(mono: np.ndarray, sr: int) -> np.ndarray:
    """Log-magnitude STFT, [freq, frames]."""
    _f, _t, Z = scipy_stft(mono, fs=sr, nperseg=2048, noverlap=1536, boundary=None)
    return np.log1p(np.abs(Z))


def timbre_corr(a: np.ndarray, b: np.ndarray, sr: int) -> float:
    """Pearson correlation of the TIME-AVERAGED log-magnitude spectrum.

    One frequency vector per clip (averaged over frames), so it is invariant to
    temporal alignment -- it asks "is this the same spectral balance / timbre".
    PRIMARY perceptual metric: the user's complaint is about timbral identity
    persisting, which is what the ear tracks.
    """
    A = _logmag(a.mean(axis=0), sr).mean(axis=1)  # avg over frames -> [freq]
    B = _logmag(b.mean(axis=0), sr).mean(axis=1)
    if A.std() < 1e-9 or B.std() < 1e-9:
        return float("nan")
    return float(np.corrcoef(A, B)[0, 1])


def spec_corr(a: np.ndarray, b: np.ndarray, sr: int) -> float:
    """Pearson of full log-magnitude spectrograms (time AND freq).

    CONVERGENCE signal: consecutive loop outputs are time-aligned, so
    spec_corr(prev,new) -> 1 marks arrival at the fixed point.
    """
    A = _logmag(a.mean(axis=0), sr)
    B = _logmag(b.mean(axis=0), sr)
    n = min(A.shape[1], B.shape[1])
    A, B = A[:, :n].ravel(), B[:, :n].ravel()
    if n < 1 or A.std() < 1e-9 or B.std() < 1e-9:
        return float("nan")
    return float(np.corrcoef(A, B)[0, 1])


def spec_centroid(audio: np.ndarray, sr: int) -> float:
    """Spectral centroid (Hz), averaged over frames -- brightness / degeneration."""
    mono = audio.mean(axis=0)
    f, _t, Z = scipy_stft(mono, fs=sr, nperseg=2048, noverlap=1536, boundary=None)
    mag = np.abs(Z).mean(axis=1)
    s = mag.sum()
    return float((f * mag).sum() / s) if s > 1e-12 else float("nan")


def rms(audio: np.ndarray) -> float:
    return float(np.sqrt(np.mean(audio.astype(np.float64) ** 2)))


# ── Main ─────────────────────────────────────────────────────────────

def log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


def main() -> int:
    if not BACKEND_SCRIPT.is_file():
        log(f"ERROR: backend not found at {BACKEND_SCRIPT}")
        return 1
    out_dir = OUT_ROOT / f"{MODE}_{SEEDMODE}"
    out_dir.mkdir(parents=True, exist_ok=True)

    t0 = time.time()
    command = [sys.executable, str(BACKEND_SCRIPT)]
    log(f"Spawning backend: {' '.join(command)}")
    client = PipeClient(command)
    try:
        info = client.info
        models = info.get("models", [])
        model = MODEL if MODEL in models else (models[0] if models else None)
        if not model:
            log("ERROR: backend discovered no models")
            return 1
        log(f"Backend ready. models={models}")
        log(f"MODE={MODEL} mode={MODE}  seed={SEED} (fixed)  steps={STEPS} cfg={CFG} "
            f"dur={DURATION_S}s  iters={ITERS}")
        log(f"  source(A)='{SOURCE_PROMPT}'   loop(B)='{LOOP_PROMPT}'")

        # alpha = -1.0 == "pure A" in the backend's linear blend. CRITICAL: every
        # request here uses a single prompt (prompt_a only). The backend mirrors an
        # absent prompt_b through null (2*null - A), so the default alpha=0 blend
        # 0.5*A + 0.5*(2*null - A) collapses to NULL == unconditional, and the text
        # prompt does nothing (verified: two opposite prompts -> identical audio).
        # The plugin's buildInferenceRequest guard pins alpha to the pure end for a
        # lone prompt (commit 298faa90: req.alpha = promptB.isEmpty() ? -1 : +1).
        # We replicate that here, else the whole loop runs prompt-blind.
        base = {"model": model, "duration": DURATION_S, "steps": STEPS,
                "cfg_scale": CFG, "alpha": -1.0}

        # ── Original (prompt A, fixed seed) -- the carried-over source ──
        orig = client.request({**base, "prompt_a": SOURCE_PROMPT, "seed": SEED})
        sr = orig["sample_rate"]
        orig_audio = orig["audio"]
        write_wav(out_dir / "original_A.wav", orig_audio, sr)
        orig_rms = rms(orig_audio)
        orig_centroid = spec_centroid(orig_audio, sr)
        log(f"original(A): rms={orig_rms:.4f} centroid={orig_centroid:.0f}Hz "
            f"backend={orig['elapsed_ms']/1000:.2f}s")

        # ── Destination (loop prompt B, same seed) -- where the loop should go ──
        dest = client.request({**base, "prompt_a": LOOP_PROMPT, "seed": SEED})
        dest_audio = dest["audio"]
        write_wav(out_dir / "destination_B.wav", dest_audio, sr)
        # Baseline inherent A<->B timbre similarity. In cross mode this is the
        # value `timbre_to_A` falls toward as the loop becomes pure-B, and the
        # value `timbre_to_B` starts from. In same mode it is ~1 (B==A).
        ab_ref = timbre_corr(orig_audio, dest_audio, sr)
        log(f"A<->B baseline timbre_corr = {ab_ref:+.3f}  "
            f"({'well separated' if ab_ref < 0.4 else 'NOT well separated'})")

        # "A practically gone" = timbre_to_A fallen within 10% of the A<->B
        # baseline (out is now as A-dissimilar as a pure-B clip).
        # "B arrived"          = timbre_to_B risen within 10% of its max (~1).
        gone_thresh = ab_ref + 0.10 * (1.0 - ab_ref)
        arrived_thresh = 1.0 - 0.10 * (1.0 - ab_ref)

        manifest = {
            "model": model, "mode": MODE, "sample_rate": sr,
            "source_prompt": SOURCE_PROMPT, "loop_prompt": LOOP_PROMPT,
            "seed": SEED, "steps": STEPS, "cfg_scale": CFG, "duration_s": DURATION_S,
            "iters": ITERS, "sigmas": SIGMAS,
            "original": {"rms": orig_rms, "centroid": orig_centroid},
            "ab_baseline_timbre": ab_ref,
            "gone_threshold": gone_thresh, "arrived_threshold": arrived_thresh,
            "runs": [],
        }
        summary = []

        # ── Sigma sweep ──────────────────────────────────────────────
        for sigma in SIGMAS:
            log(f"\n══ sigma={sigma:.3f} ══")
            cur = orig_audio
            traj = []
            crossover_iter = None   # B's timbre overtakes A's
            gone_iter = None        # A practically gone
            arrived_iter = None     # B practically arrived
            converged_iter = None   # fixed point reached
            for it in range(1, ITERS + 1):
                res = client.request({
                    **base,
                    "prompt_a": LOOP_PROMPT,
                    "seed": SEED if SEEDMODE == "fixed" else -1,  # -1 => backend randomizes
                    "init_audio_b64": encode_init_audio(cur),
                    "init_audio_sr": sr,
                    "init_audio_channels": cur.shape[0],
                    "init_noise_level": sigma,
                })
                new = res["audio"]
                ta = timbre_corr(orig_audio, new, sr)   # A content (should fall)
                tb = timbre_corr(dest_audio, new, sr)   # B content (should rise)
                m = {
                    "iter": it,
                    "timbre_A": ta,
                    "timbre_B": tb,
                    "wav_A": corr_to(orig_audio, new),
                    "spec_prev": spec_corr(cur, new, sr),
                    "rms": rms(new),
                    "centroid": spec_centroid(new, sr),
                }
                traj.append(m)
                if crossover_iter is None and not np.isnan(ta) and not np.isnan(tb) \
                        and tb > ta:
                    crossover_iter = it
                if gone_iter is None and not np.isnan(ta) and ta <= gone_thresh:
                    gone_iter = it
                if arrived_iter is None and not np.isnan(tb) and tb >= arrived_thresh:
                    arrived_iter = it
                if converged_iter is None and not np.isnan(m["spec_prev"]) \
                        and m["spec_prev"] >= 0.98:
                    converged_iter = it
                if it in SAVE_ITERS or it == ITERS:
                    write_wav(out_dir / f"sigma{sigma:.3f}_iter{it:02d}.wav", new, sr)
                log(f"  it{it:02d}: timbre_A={ta:+.3f} timbre_B={tb:+.3f}  "
                    f"spec_prev={m['spec_prev']:+.3f} rms={m['rms']:.4f} cen={m['centroid']:.0f}")
                cur = new

            final = traj[-1]
            rms_ratio = final["rms"] / orig_rms if orig_rms > 1e-9 else float("nan")
            cen_ratio = (final["centroid"] / orig_centroid
                         if orig_centroid and not np.isnan(orig_centroid) else float("nan"))
            degenerate = bool(
                (not np.isnan(rms_ratio) and (rms_ratio > 4.0 or rms_ratio < 0.05)) or
                (not np.isnan(cen_ratio) and (cen_ratio > 2.5 or cen_ratio < 0.4))
            )
            row = {
                "sigma": sigma,
                "crossover_iter": crossover_iter,
                "gone_iter": gone_iter,
                "arrived_iter": arrived_iter,
                "converged_iter": converged_iter,
                "final_timbre_A": final["timbre_A"],
                "final_timbre_B": final["timbre_B"],
                "rms_ratio": rms_ratio,
                "cen_ratio": cen_ratio,
                "degenerate": degenerate,
            }
            summary.append(row)
            manifest["runs"].append({"sigma": sigma, "trajectory": traj, **row})
            log(f"  → crossover@{crossover_iter}  A-gone@{gone_iter}  "
                f"B-arrived@{arrived_iter}  conv@{converged_iter}  "
                f"final A={final['timbre_A']:+.3f} B={final['timbre_B']:+.3f}  "
                f"rms×{rms_ratio:.2f} cen×{cen_ratio:.2f}  "
                f"{'DEGENERATE' if degenerate else 'coherent'}")

        with open(out_dir / "manifest.json", "w") as f:
            json.dump(manifest, f, indent=2)
        with open(out_dir / "summary.json", "w") as f:
            json.dump({"ab_baseline_timbre": ab_ref, "gone_threshold": gone_thresh,
                       "arrived_threshold": arrived_thresh, "summary": summary}, f, indent=2)

        # ── Console summary table ────────────────────────────────────
        log("\n" + "=" * 88)
        log(f"SUMMARY  mode={MODE}  A<->B baseline timbre={ab_ref:+.3f}  "
            f"(A-gone<= {gone_thresh:+.3f}, B-arrived>= {arrived_thresh:+.3f})")
        log(f"{'sigma':>6} {'cross@':>7} {'Agone@':>7} {'Barr@':>6} {'conv@':>6} "
            f"{'finA':>6} {'finB':>6} {'rms×':>6} {'cen×':>6}  status")
        for r in summary:
            def s(x):
                return str(x) if x is not None else ">N"
            log(f"{r['sigma']:>6.3f} {s(r['crossover_iter']):>7} {s(r['gone_iter']):>7} "
                f"{s(r['arrived_iter']):>6} {s(r['converged_iter']):>6} "
                f"{r['final_timbre_A']:>+6.2f} {r['final_timbre_B']:>+6.2f} "
                f"{r['rms_ratio']:>6.2f} {r['cen_ratio']:>6.2f}  "
                f"{'DEGEN' if r['degenerate'] else 'ok'}")
        log("=" * 88)
        log(f"\nDone in {time.time()-t0:.0f}s. Artifacts in {out_dir}")
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    sys.exit(main())
