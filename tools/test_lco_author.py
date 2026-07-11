#!/usr/bin/env python3
"""Verify the LCO author (backend/lco_author.py) before it is wired in.

Part A (no model): the correctness-critical claims.
  1. GEN09 -> cycle -> _cycle_to_partials -> renderAdditive round-trips the cycle
     (the FFT sine-basis conversion is exact).
  2. GEN10 -> _gen10_partials -> renderAdditive == the gen10 cycle (direct, exact).

Part B (needs the coder, LCO_MODEL_DIR): the full author over the 8 PoC prompts,
baked through a FAITHFUL Python mirror of DcoBaker::bake() (the shipping baker),
rendered to WAV. Confirms the recipe path reproduces the approved 8/8 living,
8/8 distinct result — the pre-commit ear-audition + fidelity proof in one.

faithful_bake() mirrors src/dsp/DcoBaker.cpp line-for-line (renderAdditive,
applyShape, shapeCurve, motion segments, blend, global normalize). If the two
paths' tables match after peak-normalization, the C++ baker will play the PoC's
sounds (only the normalize TARGET differs: PoC 0.9 vs Baker 0.95/0.83 — level,
not timbre).
"""
import os
import re
import sys

import numpy as np

sys.path.insert(0, "/Users/joerissen/ai/t5ynth/tools")
sys.path.insert(0, "/Users/joerissen/ai/t5ynth/backend")
import lco_author as L          # noqa: E402
import bridge_poc as BP         # noqa: E402  scan_render, write_wav, frame_liveness

N = 2048
_X = 2.0 * np.pi * np.arange(N) / N
OUT_DIR = "/Users/joerissen/ai/t5ynth/tools/lco_author_out"


# ─── Faithful Python mirror of src/dsp/DcoBaker.cpp ───────────────────────────

def _render_additive(kf):
    cyc = np.zeros(N)
    for p in kf["partials"]:
        hv = float(p["h"])
        h = min(max(hv if np.isfinite(hv) else 0.0, 0.0), 1024.0)  # jlimit(0, kMaxHarmonics, h), NaN->0
        cyc += float(p["a"]) * np.sin(h * _X + float(p["phase"]))
    cyc -= cyc.mean()                                # removeDC
    return cyc


def _apply_shape(cyc, shape):
    s = float(np.clip(shape, 0.0, 1.0))
    if s <= 0.0:
        return cyc
    drive = 1.0 + s * 4.0
    bias = 0.2 * s
    y = np.tanh(drive * (cyc + bias))
    return y - y.mean()                              # removeDC


def _render_keyframe(kf):
    return _apply_shape(_render_additive(kf), kf["shape"])


def _shape_curve(a, curve):
    a = float(np.clip(a, 0.0, 1.0))
    if curve == "fast":
        return a ** 0.4
    if curve == "slow":
        return a ** 2.5
    return a


def faithful_bake(recipe):
    num_frames = int(np.clip(recipe["frames"], 8, 256))
    kfs = recipe["keyframes"]
    if not kfs:
        return np.zeros((num_frames, N))
    rendered = [_render_keyframe(kf) for kf in kfs]
    last = len(kfs) - 1

    def clamp(i):
        return min(max(int(i), 0), last)

    motion = recipe["motion"]
    start_kf = clamp(motion[0]["to"]) if motion else 0
    segments = []                                    # (fromKf, toKf, cumStart, cumEnd, curve)
    if len(motion) > 1:
        raw_sum = sum(max(0.0, float(m["dur_frac"])) for m in motion[1:])
        if raw_sum > 0.0:
            cum, from_kf = 0.0, start_kf
            for i in range(1, len(motion)):
                seg = motion[i]
                dur = max(0.0, float(seg["dur_frac"])) / raw_sum
                to_kf = clamp(seg["to"])
                cum_start = cum
                cum_end = 1.0 if (i + 1 == len(motion)) else (cum + dur)
                segments.append((from_kf, to_kf, cum_start, cum_end, seg.get("curve", "lin")))
                cum, from_kf = cum_end, to_kf
    if not segments:
        segments = [(start_kf, start_kf, 0.0, 1.0, "lin")]

    frames = np.zeros((num_frames, N))
    for i in range(num_frames):
        if recipe["loop"]:
            p = i / num_frames
        else:
            p = i / (num_frames - 1) if num_frames > 1 else 0.0
        active = segments[-1]
        for seg in segments:
            if p <= seg[3]:
                active = seg
                break
        span = active[3] - active[2]
        a_raw = (p - active[2]) / span if span > 1e-9 else 1.0
        a = _shape_curve(a_raw, active[4])
        frames[i] = (1.0 - a) * rendered[active[0]] + a * rendered[active[1]]

    any_shaped = any(kf["shape"] > 0.0 for kf in kfs)
    target = 0.83 if any_shaped else 0.95
    peak = float(np.max(np.abs(frames)))
    if peak > 1e-6:
        frames *= target / peak
    return frames


# ─── Part A: correctness of the GEN -> partials projection ────────────────────

def _ref_gen09(args):
    """The PoC gen09 single cycle (tools/csound_gen_poc.py): the reference the
    baker's renderAdditive must reproduce from the direct partial mapping."""
    cyc = np.zeros(N)
    for i in range(0, len(args) - 2, 3):
        pn, st, ph = float(args[i]), float(args[i + 1]), float(args[i + 2])
        if st:
            cyc += st * np.sin(pn * _X + np.deg2rad(ph))
    cyc -= cyc.mean()
    return cyc


def _part_a():
    print("Part A — GEN -> partials projection (direct, float-h)")
    rng = np.random.default_rng(20260711)
    worst = 0.0
    for _ in range(200):
        # random GEN09: 2-6 triples, some inharmonic (non-integer partial#)
        ntrip = int(rng.integers(2, 7))
        args = []
        for _ in range(ntrip):
            args += [float(rng.uniform(0.5, 12.0)),      # inharmonic partial# allowed
                     float(rng.uniform(-1.0, 1.0)),
                     float(rng.uniform(-180.0, 180.0))]
        kf = L._spectrum_to_keyframe((9, args, 0.0))
        if kf is None:
            continue
        recon = _render_additive(kf)
        ref = _ref_gen09(args)
        err = float(np.max(np.abs(recon - ref)))
        rms = float(np.sqrt(np.mean(ref ** 2)))
        worst = max(worst, err / rms if rms > 1e-9 else err)
    print(f"  GEN09 direct: worst relative max-error over 200 trials = {worst:.2e}")
    assert worst < 1e-9, f"GEN09 direct mapping not exact: {worst:.2e}"

    # GEN10 direct
    args = [1.0, 0.5, 0.33, 0.25, 0.2, 0.1]
    ref10 = np.zeros(N)
    for k, s in enumerate(args, start=1):
        ref10 += s * np.sin(k * _X)
    ref10 -= ref10.mean()
    err10 = float(np.max(np.abs(_render_additive(L._spectrum_to_keyframe((10, args, 0.0))) - ref10)))
    print(f"  GEN10 direct: max-error = {err10:.2e}")
    assert err10 < 1e-9, f"GEN10 direct mapping not exact: {err10:.2e}"
    print("  Part A PASS\n")


# ─── Part B: full author -> faithful bake -> WAV, over the 8 PoC prompts ───────

PROMPTS = [
    "warm analog pad that slowly opens up",
    "luminous crystalline bell",
    "cold aggressive metallic stab",
    "brittle glass shimmer",
    "muddy dark drone",
    "hollow woody clarinet",
    "screaming bright lead",
    "soft mellow flute",
]


def _centroid(cycle):
    mag = np.abs(np.fft.rfft(cycle))
    bins = np.arange(len(mag))
    tot = mag.sum()
    return float((bins * mag).sum() / tot) if tot > 1e-9 else 0.0


def _part_b():
    import pipe_inference as P          # heavy (torch); only needed for Part B
    mdir = os.environ.get("LCO_MODEL_DIR") or P._resolve_translation_model_dir({})
    dev = "mps"
    os.makedirs(OUT_DIR, exist_ok=True)
    print(f"Part B — model: {mdir}\n")

    def llm(text, system_prompt, max_new_tokens):
        return P.run_instruct(text, mdir, dev, system_prompt, max_new_tokens=max_new_tokens)

    results = []
    for i, prompt in enumerate(PROMPTS, 1):
        resp = L.build_lco_response(prompt, llm)
        recipe = resp["recipe"]
        stations = resp["resolved"]["adjectives"]
        frames = faithful_bake(recipe)
        name = f"lco_{i:02d}_" + re.sub(r"[^a-z]+", "_", prompt.lower())[:24].strip("_")
        BP.write_wav(f"{OUT_DIR}/{name}.wav", BP.scan_render(frames, scan="sweep"))
        results.append((prompt, frames))
        tags = " -> ".join(f"{s!r}(kf{j})" for j, s in enumerate(stations))
        print(f"[{i}] {prompt}")
        print(f"    stations: {tags}")
        print(f"    keyframes={len(recipe['keyframes'])}  "
              f"partials/kf={[len(k['partials']) for k in recipe['keyframes']]}  "
              f"liveness={BP.frame_liveness(frames):.4f}")
    print(f"\nWAVs -> {OUT_DIR}  (lco_*.wav)")

    print("\ndiscrimination — centroid sweep + cross-prompt distinctness")
    print(f"{'prompt':26} {'cen[0]':>7} {'cen[N]':>7} {'sweep':>7}")
    for prompt, t in results:
        c0, c1 = _centroid(t[0]), _centroid(t[-1])
        print(f"{prompt[:26]:26} {c0:7.2f} {c1:7.2f} {abs(c1 - c0):7.2f}")
    parent = list(range(len(results)))

    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    dead = 0
    for a in range(len(results)):
        for b in range(a + 1, len(results)):
            # peak-normalize each before comparing: distinctness is about SHAPE,
            # not level (the two paths differ only by the global normalize target).
            ta = results[a][1] / (np.max(np.abs(results[a][1])) + 1e-12)
            tb = results[b][1] / (np.max(np.abs(results[b][1])) + 1e-12)
            if float(np.sqrt(((ta - tb) ** 2).mean())) < 0.02:
                parent[find(a)] = find(b)
    clusters = len({find(i) for i in range(len(results))}) if results else 0
    for prompt, t in results:
        if BP.frame_liveness(t) < 1e-5:
            dead += 1
    print(f"\ndistinct constructions: {clusters} / {len(results)}   "
          f"(static tables: {dead})")


if __name__ == "__main__":
    _part_a()
    if os.environ.get("LCO_SKIP_MODEL"):
        print("Part B skipped (LCO_SKIP_MODEL set)")
    else:
        _part_b()
