#!/usr/bin/env python3
"""A/B ear-gate render for INHARMONIC timbres (bell / metal / glass): Option A
(dense-harmonic 256-frame approximation, backend/dco_frames.py) vs Option B (the
CURRENT true-additive "stations" engine, tools/audition_additive_sets). The user
CHOOSES between the two by ear; this script produces the WAVs + the honest,
numbers-only distance material (no audibility claims — the ear is the instance).

The recipe is authored per timbre by injecting the lexicon technique KEY through
build_lco_response's own llm hook (the 7B LLM is bypassed; the deterministic key
plan a competent interpreter would emit is stubbed), so the SAME authored recipe
feeds BOTH render methods. Two comparison pairs per timbre:

  MATCHED / STATIC pair (the DECISION ear-gate — identical spectral content, the
  only difference is grid-vs-true, so it isolates the grid approximation cost):
  * A  <slug>_A_approx.wav : plan_from_response -> bake_frames (dense-harmonic
    frames of the true spectrum) -> scanned at f0. The grid approximation.
  * B  <slug>_B_true.wav   : the same static station -> the shipped
    WavetableOscillator.setAdditiveBank via the prebuilt additive_sets auditioner
    (linked against build_clean/.../libT5ynth_SharedCode.a; NO src/ edit). True
    non-integer partials.

  MOVING pair (supplementary — each engine's own movement-by-default render, so
  the movement REALIZATIONS differ too: B's stationize blooms into synthesized
  high partials, A's per-frame bloom does not — this pair's distance is the TOTAL
  option difference, NOT the isolated grid cost):
  * A  <slug>_A_moving.wav : bake_frames with movement-by-default (real per-frame
    evolution) -> scanned.
  * B  <slug>_B_moving.wav : the stationized additive chain -> additive_sets.

Both at f0 = 261.63 Hz (C4), ~3 s, sr = 44100 (the auditioner's fixed rate; A is
rendered at the same rate so the two magnitude spectra compare directly).

Reported per timbre (numbers only, no audibility claims):
  * spectral centroid of A and of B (whole-render, Hz), each pair;
  * A<->B spectral distance: log-mel cosine + magnitude-spectrum cosine + L2 of the
    L2-normalized spectra (time-averaged over the render). The STATIC-pair distance
    is the honest grid-approximation cost ("what one iota is worth"); the MOVING-pair
    distance additionally carries the movement-realization difference;
  * peak / rms of each render.

    .venv/bin/python tools/render_bell_ab.py [OUTDIR]
Default OUTDIR: ~/Downloads/lco_bell_AB_<today>/
"""
import datetime
import json
import os
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "backend"))
sys.path.insert(0, str(REPO / "tools"))

import lco_author as lca
import dco_frames as df
from render_dco_frames import wavetable_render, write_wav   # canonical A-side scan/WAV

SR = 44100          # matches audition_additive_sets (hardcoded); render A at the same rate
F0 = 261.63         # C4
DUR = 3.0           # seconds
SCAN_SECONDS = 3.0  # A scans its 256-frame table once over the render (one bloom breath)
ADDITIVE_SETS_BIN = REPO / "tools" / "e2e_additive_sets_out" / "additive_sets"

# The three genuinely-inharmonic timbres (lexicon technique keys, non-integer
# partial ratios — verified against backend/dco_lexicon.json):
#   fm_bell     struck bell   h = 1, 2.76, 5.4, 8.93
#   metallic_fm struck metal  h = 1, 1.73, 3.19, 4.51
#   cymbal      clangorous    h = 1, 2.43, 3.79 ... 31.02  (dense plate wash)
TIMBRES = [
    ("bell",   "fm_bell"),
    ("metal",  "metallic_fm"),
    ("cymbal", "cymbal"),
]


def _stub_llm(technique_key, motion="none"):
    """Deterministic key-plan injection (bypasses the 7B LLM). Returns the exact
    reply format dco_llm_map._parse_reply expects; the closed-enum guard maps the
    key to itself. motion='static' -> a single static station on BOTH paths (the
    matched grid-vs-true pair); motion='none' -> movement-by-default animates it
    (each engine's own living render)."""
    reply = f"TECHNIQUE: {technique_key}\nADJECTIVES: none\nMOTION: {motion}"

    def _llm(user, system, max_new):
        return reply
    return _llm


def _to_sets_json(recipe):
    """recipe keyframes (all additive stations) -> the auditioner's sets JSON."""
    sets = []
    for kf in recipe.get("keyframes") or []:
        sets.append([{"h": float(p["h"]), "a": float(p["a"]), "phase": float(p.get("phase", 0.0))}
                     for p in (kf.get("partials") or [])])
    return {"sets": sets, "motion_rate_hz": float(recipe.get("motion_rate_hz", 0.5))}


def _read_wav(path):
    with wave.open(str(path), "rb") as w:
        sr = w.getframerate()
        raw = w.readframes(w.getnframes())
    return np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0, sr


# ─── numbers-only analysis ────────────────────────────────────────────────────

def _avg_mag(sig, n_fft=4096, hop=1024):
    """Time-averaged magnitude spectrum (Hann STFT, mean over frames). Integrates
    out the (gentle, matched-duration) movement so the structural difference shows."""
    if sig.size < n_fft:
        n_fft = 1 << int(np.floor(np.log2(max(64, sig.size))))
        hop = max(1, n_fft // 4)
    w = np.hanning(n_fft)
    mags = []
    for start in range(0, sig.size - n_fft + 1, hop):
        mags.append(np.abs(np.fft.rfft(sig[start:start + n_fft] * w)))
    return (np.mean(mags, axis=0) if mags else np.abs(np.fft.rfft(sig[:n_fft] * w))), n_fft


def _centroid_hz(mag, sr, n_fft):
    freqs = np.fft.rfftfreq(n_fft, 1.0 / sr)
    tot = float(mag.sum())
    return float((freqs * mag).sum() / tot) if tot > 0 else float("nan")


def _hz_to_mel(f):
    return 2595.0 * np.log10(1.0 + f / 700.0)


def _mel_to_hz(m):
    return 700.0 * (10.0 ** (m / 2595.0) - 1.0)


def _mel_fb(n_mels, n_fft, sr, fmin=20.0, fmax=None):
    fmax = fmax or sr / 2.0
    edges = _mel_to_hz(np.linspace(_hz_to_mel(fmin), _hz_to_mel(fmax), n_mels + 2))
    bins = np.floor((n_fft + 1) * edges / sr).astype(int)
    fb = np.zeros((n_mels, n_fft // 2 + 1))
    for m in range(1, n_mels + 1):
        l, c, r = bins[m - 1], bins[m], bins[m + 1]
        c = max(c, l + 1)
        r = max(r, c + 1)
        for k in range(l, c):
            if 0 <= k < fb.shape[1]:
                fb[m - 1, k] = (k - l) / float(c - l)
        for k in range(c, r):
            if 0 <= k < fb.shape[1]:
                fb[m - 1, k] = (r - k) / float(r - c)
    return fb


def _logmel(mag, sr, n_fft, n_mels=64):
    return np.log(_mel_fb(n_mels, n_fft, sr) @ mag + 1e-8)


def _cos_dist(a, b):
    na, nb = float(np.linalg.norm(a)), float(np.linalg.norm(b))
    if na == 0.0 or nb == 0.0:
        return float("nan")
    return 1.0 - float(np.dot(a, b) / (na * nb))


def _l2_norm_dist(a, b):
    """L2 distance of the L2-normalized magnitude spectra (0 = identical shape, up
    to sqrt(2) = orthogonal). Scale-invariant, so it measures spectral SHAPE."""
    na, nb = float(np.linalg.norm(a)), float(np.linalg.norm(b))
    if na == 0.0 or nb == 0.0:
        return float("nan")
    return float(np.linalg.norm(a / na - b / nb))


def _line_mag(freqs_hz, amps, sr, n_fft):
    """Static line spectrum -> a synthetic |X| on the n_fft rfft grid (each line's
    amplitude dropped into its nearest bin). Above-Nyquist lines are dropped."""
    mag = np.zeros(n_fft // 2 + 1)
    df_bin = sr / n_fft
    for fhz, a in zip(freqs_hz, amps):
        if fhz <= 0.0 or fhz >= sr / 2.0:
            continue
        k = int(round(fhz / df_bin))
        if 0 <= k < mag.size:
            mag[k] += abs(float(a))
    return mag


def _static_grid_error(base_partials, f0, sr, n_fft=8192):
    """The ISOLATED grid-approximation cost: A's dense integer-grid clusters vs B's
    TRUE non-integer partials, both STATIC at f0, no movement/engine confound.
    Returns (centroid_A_hz, centroid_B_hz, logmel_cos_dist)."""
    # A: dense clusters on the integer grid (exactly what a static A frame encodes)
    a_by_k = {}
    for p in base_partials:
        for k, w, _t in df._cluster_bins(float(p["h"]), float(p["a"])):
            a_by_k[k] = a_by_k.get(k, 0.0) + w
    a_freqs = [k * f0 for k in a_by_k]
    a_amps = list(a_by_k.values())
    # B: the true partials at their real non-integer ratios
    b_freqs = [float(p["h"]) * f0 for p in base_partials]
    b_amps = [float(p["a"]) for p in base_partials]

    mA = _line_mag(a_freqs, a_amps, sr, n_fft)
    mB = _line_mag(b_freqs, b_amps, sr, n_fft)
    return (_centroid_hz(mA, sr, n_fft), _centroid_hz(mB, sr, n_fft),
            _cos_dist(_logmel(mA, sr, n_fft), _logmel(mB, sr, n_fft)))


def _peak_rms(sig):
    return (float(np.max(np.abs(sig))) if sig.size else 0.0,
            float(np.sqrt(np.mean(sig * sig))) if sig.size else 0.0)


def _render_A(resp, path, scan_seconds):
    """Option A: plan_from_response -> bake_frames (dense-harmonic grid) -> scan at
    f0 -> WAV. Returns (signal, sr, peak, rms). A static plan bakes frozen frames;
    a movement plan evolves per frame."""
    plan = df.plan_from_response(resp)
    frames = df.bake_frames(plan)
    audio = wavetable_render(frames, f0=F0, dur=DUR, sr=SR, scan_seconds=scan_seconds)
    write_wav(path, audio, sr=SR)
    sig, sr = _read_wav(path)
    return (sig, sr) + _peak_rms(sig)


def _render_B(recipe, jpath, path):
    """Option B: the recipe's additive station(s) -> the shipped true-additive
    engine (additive_sets). Returns (signal, sr, peak, rms, tool_line) or None on
    failure (reported, never src/-edited around)."""
    with open(jpath, "w") as f:
        json.dump(_to_sets_json(recipe), f)
    run = subprocess.run([str(ADDITIVE_SETS_BIN), str(jpath), str(path), str(DUR), str(F0)],
                         capture_output=True, text=True)
    tool_line = (run.stdout.strip().splitlines() or [""])[-1]
    if run.returncode != 0 or not path.is_file():
        print(f"    STOP: additive_sets B render failed (rc={run.returncode}): "
              f"{run.stderr.strip() or run.stdout.strip()}")
        return None
    sig, sr = _read_wav(path)
    return (sig, sr) + _peak_rms(sig) + (tool_line,)


def _ab_distance(sigA, srA, sigB, srB):
    """A<->B spectral distance on two same-sr renders: (centroidA, centroidB,
    logmel_cos, mag_cos, mag_L2norm). Time-averaged magnitude spectra."""
    magA, nA = _avg_mag(sigA)
    magB, nB = _avg_mag(sigB)
    return (_centroid_hz(magA, srA, nA), _centroid_hz(magB, srB, nB),
            _cos_dist(_logmel(magA, srA, nA), _logmel(magB, srB, nB)),
            _cos_dist(magA, magB), _l2_norm_dist(magA, magB))


def main():
    outdir = Path(sys.argv[1]) if len(sys.argv) > 1 else (
        Path.home() / "Downloads" / f"lco_bell_AB_{datetime.date.today().isoformat()}")
    outdir.mkdir(parents=True, exist_ok=True)
    if not ADDITIVE_SETS_BIN.is_file():
        print(f"STOP: B-side auditioner not found at {ADDITIVE_SETS_BIN}. "
              f"Compile tools/audition_additive_sets.cpp against the prebuilt lib "
              f"(no src/ edit) via tools/e2e_additive_sets_render.py first.")
        sys.exit(2)

    print(f"A/B inharmonic ear-gate -> {outdir}")
    print(f"  A = Option A (dense-harmonic bake_frames)   B = Option B (true-additive additive_sets)")
    print(f"  f0={F0} Hz (C4), {DUR}s, sr={SR}\n")

    rows = []
    for slug, tech_key in TIMBRES:
        # ── the two authored recipes: matched-static (decision pair) + moving ──
        resp_s = lca.build_lco_response(slug, _stub_llm(tech_key, "static"), frames=None)
        resp_m = lca.build_lco_response(slug, _stub_llm(tech_key, "none"), frames=None)
        base_partials = (df.plan_from_response(resp_s)["frame_engine"]["base"].get("partials") or [])

        print(f"[{slug}]  technique={tech_key}  base_partials={len(base_partials)}  "
              f"B_static_stations={len(resp_s['recipe']['keyframes'])}  "
              f"B_moving_stations={len(resp_m['recipe']['keyframes'])}")

        # ── MATCHED / STATIC pair: <slug>_A_approx.wav / <slug>_B_true.wav ──
        A = _render_A(resp_s, outdir / f"{slug}_A_approx.wav", scan_seconds=DUR)
        B = _render_B(resp_s["recipe"], outdir / f"{slug}_B_true.sets.json",
                      outdir / f"{slug}_B_true.wav")
        if B is None:
            continue
        sCenA, sCenB, s_lm, s_mc, s_l2 = _ab_distance(A[0], A[1], B[0], B[1])

        # ── MOVING pair: <slug>_A_moving.wav / <slug>_B_moving.wav ──
        Am = _render_A(resp_m, outdir / f"{slug}_A_moving.wav", scan_seconds=SCAN_SECONDS)
        Bm = _render_B(resp_m["recipe"], outdir / f"{slug}_B_moving.sets.json",
                       outdir / f"{slug}_B_moving.wav")
        if Bm is None:
            continue
        mCenA, mCenB, m_lm, m_mc, m_l2 = _ab_distance(Am[0], Am[1], Bm[0], Bm[1])

        # ── numeric cross-check: the isolated grid error from the line spectra ──
        g_cenA, g_cenB, g_lm = _static_grid_error(base_partials, F0, SR)

        rows.append(dict(slug=slug, tech=tech_key, npart=len(base_partials),
                         # static (decision) pair
                         sCenA=sCenA, sCenB=sCenB, s_lm=s_lm, s_mc=s_mc, s_l2=s_l2,
                         A_peak=A[2], A_rms=A[3], B_peak=B[2], B_rms=B[3], B_tool=B[4],
                         # moving pair
                         mCenA=mCenA, mCenB=mCenB, m_lm=m_lm, m_mc=m_mc, m_l2=m_l2,
                         Am_peak=Am[2], Am_rms=Am[3], Bm_peak=Bm[2], Bm_rms=Bm[3], Bm_tool=Bm[4],
                         # line-spectrum cross-check
                         g_cenA=g_cenA, g_cenB=g_cenB, g_lm=g_lm))

        print(f"    STATIC (decision)  A {slug}_A_approx.wav peak={A[2]:.3f} rms={A[3]:.3f} cen={sCenA:.0f}Hz | "
              f"B {slug}_B_true.wav peak={B[2]:.3f} rms={B[3]:.3f} cen={sCenB:.0f}Hz")
        print(f"        grid cost A<->B: logmel_cos={s_lm:.4f}  mag_cos={s_mc:.4f}  magL2={s_l2:.4f}   "
              f"(line-spectrum xcheck logmel_cos={g_lm:.4f}, cenΔ={g_cenA - g_cenB:+.0f}Hz)")
        print(f"    MOVING (each engine) A cen={mCenA:.0f}Hz peak={Am[2]:.3f} | B cen={mCenB:.0f}Hz peak={Bm[2]:.3f}")
        print(f"        total option diff: logmel_cos={m_lm:.4f}  mag_cos={m_mc:.4f}  magL2={m_l2:.4f}\n")

    # ── summary tables ──
    print("=" * 100)
    print("STATIC matched pair  = the DECISION ear-gate: identical spectrum, grid-vs-true ONLY")
    print(f"{'timbre':8}{'tech':13}{'A_cen_Hz':>9}{'B_cen_Hz':>9}{'cenΔ':>7}{'logmel':>8}{'mag_cos':>9}{'magL2':>8}")
    print("-" * 100)
    for r in rows:
        print(f"{r['slug']:8}{r['tech']:13}{r['sCenA']:9.0f}{r['sCenB']:9.0f}{r['sCenA']-r['sCenB']:+7.0f}"
              f"{r['s_lm']:8.4f}{r['s_mc']:9.4f}{r['s_l2']:8.4f}")
    print("=" * 100)
    print("MOVING pair  = each engine's movement-by-default (movement realizations DIFFER; total diff)")
    print(f"{'timbre':8}{'tech':13}{'A_cen_Hz':>9}{'B_cen_Hz':>9}{'cenΔ':>7}{'logmel':>8}{'mag_cos':>9}{'magL2':>8}")
    print("-" * 100)
    for r in rows:
        print(f"{r['slug']:8}{r['tech']:13}{r['mCenA']:9.0f}{r['mCenB']:9.0f}{r['mCenA']-r['mCenB']:+7.0f}"
              f"{r['m_lm']:8.4f}{r['m_mc']:9.4f}{r['m_l2']:8.4f}")
    print("=" * 100)

    manifest = outdir / "AB_metrics.txt"
    with open(manifest, "w") as f:
        f.write("A/B inharmonic ear-gate -- numbers only (no audibility claims).\n")
        f.write("A = Option A dense-harmonic approximation (bake_frames).\n")
        f.write("B = Option B true-additive engine (additive_sets, shipped setAdditiveBank).\n")
        f.write(f"f0={F0} Hz (C4), dur={DUR}s, sr={SR}.\n\n")
        f.write("STATIC pair (<slug>_A_approx.wav / <slug>_B_true.wav): identical spectral content,\n")
        f.write("  the only difference is the harmonic-grid approximation -> the honest grid cost.\n")
        f.write("MOVING pair (<slug>_A_moving.wav / <slug>_B_moving.wav): each engine's own\n")
        f.write("  movement-by-default; B's stationize blooms extra high partials A's per-frame\n")
        f.write("  bloom does not, so this distance is the TOTAL option difference, not the grid cost.\n")
        f.write("logmel_cos / mag_cos: cosine distance (0 = identical shape). magL2: L2 of L2-normalized spectra.\n\n")
        for r in rows:
            f.write(json.dumps(r) + "\n")
    print(f"\nWrote {len(rows)} timbre(s) x (static + moving A/B) + AB_metrics.txt to {outdir}")


if __name__ == "__main__":
    main()
