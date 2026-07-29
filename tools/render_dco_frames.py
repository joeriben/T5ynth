#!/usr/bin/env python3
"""Ear-gate render harness for the fork-INDEPENDENT 256-frame wavetable engine
(backend/dco_frames.py). For each prompt it: authors the recipe (LLM-first entry,
with a deterministic key-plan interpreter so no model is needed), bakes the F
explicit single-cycle frames, lays them end-to-end and plays them at a test f0 as
a scanning wavetable -> a WAV, and prints a per-run frame-difference metric that
PROVES real per-frame variation (spectral centroid track + consecutive-frame
spectral delta) rather than a 2-corner interpolation.

No C++ rebuild is needed: bake happens entirely in Python. WAVs are written with
the stdlib `wave` module (16-bit PCM mono).

  .venv/bin/python tools/render_dco_frames.py [OUTDIR]

Default OUTDIR: ~/Downloads/lco_256frame_engine_<today>/
"""
import datetime
import sys
import wave
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "backend"))
import dco_frames as df
import lco_author as lca

# ─── deterministic key-plan interpreter ───────────────────────────────────────
# The synth is LLM-first (build_lco_response has NO deterministic prompt path —
# the injected model IS the entry). For an offline ear-gate we inject the KEY PLAN
# a competent interpreter would emit (catalogue reply format of dco_llm_map), so
# the RENDER exercises the real construction without a live model. These are the
# fork-INDEPENDENT ear-gate cases: harmonic, integer-ratio FM, character, texture.
INTERP_REPLIES = {
    "saw": "TECHNIQUE: saw\nADJECTIVES: none\nMOTION: none",
    "metallic FM that evolves": "TECHNIQUE: fm\nADJECTIVES: none\nMOTION: evolve",
    "dirty saw": "TECHNIQUE: saw\nADJECTIVES: dirty\nMOTION: none",
    "analog saw": "TECHNIQUE: saw\nADJECTIVES: analog\nMOTION: none",
    "distorted saw": "TECHNIQUE: saw\nADJECTIVES: distorted\nMOTION: none",
    "vibrating saw": "TECHNIQUE: saw\nADJECTIVES: none\nMOTION: vibrate",
}
MATRIX = list(INTERP_REPLIES.keys())

SR = 48000
F0 = 110.0          # test pitch (A2)
DUR = 4.0           # seconds
SCAN_SECONDS = 2.0  # seconds to scan the whole F-frame table once (2 traversals over DUR)


def _interp(user, system, max_new):
    return INTERP_REPLIES.get(user, "TECHNIQUE: none\nADJECTIVES: none\nMOTION: none")


def wavetable_render(frames, f0=F0, dur=DUR, sr=SR, scan_seconds=SCAN_SECONDS):
    """Lay the F single-cycle frames end-to-end and play them as a scanning
    wavetable at f0: the within-cycle phase advances at f0, and the frame cursor
    scans 0->F->0 once per `scan_seconds` (the table loops seamlessly, so a forward
    scan wraps click-free). Bilinear over (frame, sample). Fully vectorized."""
    F, N = frames.shape
    n = np.arange(int(sr * dur))
    t = n / sr
    ph = (f0 * t) % 1.0                       # within-cycle phase [0,1)
    fi = ((t / scan_seconds) * F) % F         # frame cursor [0,F)

    idx = ph * N
    i0 = np.floor(idx).astype(np.int64) % N
    i1 = (i0 + 1) % N
    a = idx - np.floor(idx)

    f0i = np.floor(fi).astype(np.int64) % F
    f1i = (f0i + 1) % F
    fr = fi - np.floor(fi)

    s0 = (1.0 - a) * frames[f0i, i0] + a * frames[f0i, i1]
    s1 = (1.0 - a) * frames[f1i, i0] + a * frames[f1i, i1]
    out = ((1.0 - fr) * s0 + fr * s1).astype(np.float64)

    # 10 ms raised fades so the note on/off doesn't click.
    fn = int(0.01 * sr)
    if fn > 0 and out.size > 2 * fn:
        out[:fn] *= np.linspace(0.0, 1.0, fn)
        out[-fn:] *= np.linspace(1.0, 0.0, fn)
    return out


def write_wav(path, audio, sr=SR):
    pcm = (np.clip(audio, -1.0, 1.0) * 32767.0).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(pcm.tobytes())


def _keys(resp):
    r = resp.get("resolved") or {}
    bits = [f"technique={r.get('technique')}"]
    if r.get("passes"):
        bits.append(f"passes={r['passes']}")
    if r.get("textures"):
        bits.append(f"textures={r['textures']}")
    if r.get("motion"):
        bits.append(f"motion={r['motion']}")
    return " ".join(bits)


def main():
    outdir = Path(sys.argv[1]) if len(sys.argv) > 1 else (
        Path.home() / "Downloads" / f"lco_256frame_engine_{datetime.date.today().isoformat()}")
    outdir.mkdir(parents=True, exist_ok=True)
    print(f"Rendering the fork-independent ear-gate matrix -> {outdir}\n")

    rows = []
    for prompt in MATRIX:
        resp = lca.build_lco_response(prompt, _interp, frames=None)
        recipe = df.plan_from_response(resp)
        try:
            frames = df.bake_frames(recipe)
        except NotImplementedError as e:
            print(f"[SKIP] {prompt!r}: {e}")
            continue
        metrics = df.frame_metrics(frames)
        audio = wavetable_render(frames)
        peak = float(np.max(np.abs(audio)))
        rms = float(np.sqrt(np.mean(audio ** 2)))
        slug = prompt.replace(" ", "_")
        wav_path = outdir / f"{slug}.wav"
        write_wav(wav_path, audio)
        fe = recipe["frame_engine"]
        rows.append({
            "prompt": prompt, "keys": _keys(resp), "wav": wav_path.name,
            "base": fe["base"].get("kind"), "movement": fe["movement"],
            "character": fe["character"], "texture": (fe["texture"] or {}).get("name"),
            "peak": peak, "rms": rms, "metrics": metrics,
        })

    # ─── report table ───
    print("=" * 100)
    print(f"{'prompt':26} {'base':6} {'movement':10} {'peak':>6} {'rms':>7} "
          f"{'d_max':>8} {'d_mean':>8} {'centroid_bins':>16}")
    print("-" * 100)
    for r in rows:
        m = r["metrics"]
        cen = f"{m['centroid_bin_min']:.1f}->{m['centroid_bin_max']:.1f}"
        print(f"{r['prompt']:26} {str(r['base']):6} {r['movement']:10} "
              f"{r['peak']:6.3f} {r['rms']:7.4f} "
              f"{m['consecutive_delta_max']:8.2f} {m['consecutive_delta_mean']:8.3f} {cen:>16}")
    print("=" * 100)
    print("\nper-prompt keys + files:")
    for r in rows:
        tex = f" texture={r['texture']}" if r["texture"] else ""
        chr_ = f" character={r['character']}" if r["character"] else ""
        print(f"  {r['prompt']!r}\n      keys: {r['keys']}\n      plan: base={r['base']} "
              f"movement={r['movement']}{chr_}{tex}\n      wav:  {r['wav']}")
    print(f"\nWrote {len(rows)} WAV(s) to {outdir}")
    print("frame-delta metric (d_max / d_mean over consecutive frames) > 0 proves genuine\n"
          "per-frame variation; the FM index sweep shows the largest bloom, and a plain\n"
          "harmonic base still moves via the dark<->bright ramp — no 2-corner interpolation.")


if __name__ == "__main__":
    main()
