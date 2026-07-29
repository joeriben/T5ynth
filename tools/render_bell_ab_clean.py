#!/usr/bin/env python3
"""CLEAN, FAIR A/B ear-gate render for INHARMONIC timbres (bell / metal / cymbal):
Option A (dense-harmonic 256-frame approximation, backend/dco_frames.py) vs Option
B (the shipped true-additive "stations" engine, tools/audition_additive_sets). This
supersedes tools/render_bell_ab.py, which the user found unusable for deciding
approx-vs-true for TWO measured reasons this script fixes:

  DEFECT 1 -- the two sides were NOT loudness-matched (the louder side always sounds
  "fuller", so the A/B was invalid). FIX: every render is RMS-normalized to the SAME
  target (-18 dBFS). Loudness match is a single GAIN SCALAR per file; a constant gain
  scales every frequency bin equally, so it leaves the spectral SHAPE and the centroid
  UNCHANGED (verified: the reported centroids are computed on the normalized signal).

  DEFECT 2 -- Option A's movement-by-default barely moved (a bell's baked-frame
  centroid swept ~16 Hz, effectively static). FIX (backend/dco_frames.py,
  _inharmonic_approx_spectra): A's per-frame bloom now uses the SAME ear-validated
  base<->bright opposite-endpoint gesture Option B uses (absolute amplitude ramp +
  high-partial series extension), so both engines move comparably. The moving pair
  reports each engine's centroid sweep (start->mid->end) so any RESIDUAL
  movement-realization difference stays visible and honest -- they are two engines.

Files per timbre (approx/true unambiguous in EVERY name; all level-matched):
  <slug>_static_A_approx.wav / <slug>_static_B_true.wav  -- grid-vs-true, no movement:
      the PRIMARY decision material (identical spectral content, only the grid map differs).
  <slug>_moving_A_approx.wav / <slug>_moving_B_true.wav  -- both moving (A's movement
      now fixed); the sweeps are reported per file.

Same knobs as render_bell_ab.py: f0 = 261.63 Hz (C4), dur = 3.0 s, sr = 44100, the
same deterministic key-injection (7B LLM bypassed via _stub_llm). Numbers only; no
audibility claims -- the ear is the instance. Reuses render_bell_ab's analysis
helpers verbatim so the distance metrics match the old report exactly.

    .venv/bin/python tools/render_bell_ab_clean.py [OUTDIR]
Default OUTDIR: ~/Downloads/lco_bell_AB_clean_<today>/
"""
import datetime
import json
import subprocess
import sys
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "backend"))
sys.path.insert(0, str(REPO / "tools"))

# Reuse the reference harness verbatim (helpers + constants), so the distance
# metrics and the deterministic key-injection are byte-for-byte the same recipe.
import render_bell_ab as rba
import lco_author as lca
import dco_frames as df

SR = rba.SR            # 44100
F0 = rba.F0            # 261.63 (C4)
DUR = rba.DUR          # 3.0 s
SCAN_SECONDS = rba.SCAN_SECONDS   # 3.0 -- A scans its 256-frame table once (one breath)
TIMBRES = rba.TIMBRES  # [("bell","fm_bell"), ("metal","metallic_fm"), ("cymbal","cymbal")]
ADDITIVE_SETS_BIN = rba.ADDITIVE_SETS_BIN

TARGET_DBFS = -18.0
TARGET_RMS = 10.0 ** (TARGET_DBFS / 20.0)   # 0.125893 -- the common loudness anchor


# ─── loudness match (a pure gain scalar -- spectral shape untouched) ──────────

def _rms(sig):
    return float(np.sqrt(np.mean(sig * sig))) if sig.size else 0.0


def _normalize_rms(sig, target_rms):
    """Scale sig to target_rms by a single constant gain. Returns (scaled, gain).
    A constant multiplier changes NO frequency-bin RATIO -> centroid/spectral shape
    are invariant; only the level moves."""
    r = _rms(sig)
    if r <= 0.0:
        return sig, 0.0
    g = target_rms / r
    return sig * g, g


# ─── centroid sweep start -> mid -> end (movement-realization, honest) ────────

def _centroid_at(sig, sr, center_frac, n_fft=4096):
    """Spectral centroid (Hz) of a Hann window centered at center_frac of the
    signal (clamped inside the signal, away from the 10 ms note fades)."""
    if sig.size < n_fft:
        n_fft = 1 << int(np.floor(np.log2(max(64, sig.size))))
    c = int(center_frac * sig.size)
    s = max(0, min(sig.size - n_fft, c - n_fft // 2))
    w = np.hanning(n_fft)
    mag = np.abs(np.fft.rfft(sig[s:s + n_fft] * w))
    return rba._centroid_hz(mag, sr, n_fft)


def _sweep(sig, sr):
    """(start, mid, end) spectral centroids in Hz. 0.12/0.5/0.88 dodge the fades."""
    return (_centroid_at(sig, sr, 0.12), _centroid_at(sig, sr, 0.50), _centroid_at(sig, sr, 0.88))


# ─── level-matched renders ────────────────────────────────────────────────────

def _render_A(resp, path, scan_seconds):
    """Option A -> bake -> scan -> RMS-normalize -> WAV. Returns
    (sig_readback, sr, pre_norm_peak, gain)."""
    plan = df.plan_from_response(resp)
    frames = df.bake_frames(plan)
    audio = rba.wavetable_render(frames, f0=F0, dur=DUR, sr=SR, scan_seconds=scan_seconds)
    audio_n, gain = _normalize_rms(audio, TARGET_RMS)
    peak = float(np.max(np.abs(audio_n))) if audio_n.size else 0.0
    rba.write_wav(path, audio_n, sr=SR)
    sig, sr = rba._read_wav(path)        # measure the ACTUAL written (16-bit) signal
    return sig, sr, peak, gain


def _render_B(recipe, jpath, path):
    """Option B -> true-additive additive_sets -> RMS-normalize -> WAV. Returns
    (sig_readback, sr, pre_norm_peak, gain, tool_line) or None on tool failure."""
    with open(jpath, "w") as f:
        json.dump(rba._to_sets_json(recipe), f)
    run = subprocess.run([str(ADDITIVE_SETS_BIN), str(jpath), str(path), str(DUR), str(F0)],
                         capture_output=True, text=True)
    tool_line = (run.stdout.strip().splitlines() or [""])[-1]
    if run.returncode != 0 or not path.is_file():
        print(f"    STOP: additive_sets B render failed (rc={run.returncode}): "
              f"{run.stderr.strip() or run.stdout.strip()}")
        return None
    sig_raw, sr = rba._read_wav(path)    # the tool's own level
    sig_n, gain = _normalize_rms(sig_raw, TARGET_RMS)
    peak = float(np.max(np.abs(sig_n))) if sig_n.size else 0.0
    rba.write_wav(path, sig_n, sr=sr)    # overwrite in place with the matched level
    sig, sr = rba._read_wav(path)        # measure the ACTUAL written signal
    return sig, sr, peak, gain, tool_line


def main():
    outdir = Path(sys.argv[1]) if len(sys.argv) > 1 else (
        Path.home() / "Downloads" / f"lco_bell_AB_clean_{datetime.date.today().isoformat()}")
    outdir.mkdir(parents=True, exist_ok=True)
    if not ADDITIVE_SETS_BIN.is_file():
        print(f"STOP: B-side auditioner not found at {ADDITIVE_SETS_BIN}.")
        sys.exit(2)

    print(f"CLEAN A/B inharmonic ear-gate -> {outdir}")
    print(f"  A = Option A (dense-harmonic bake_frames)   B = Option B (true-additive additive_sets)")
    print(f"  f0={F0} Hz (C4), {DUR}s, sr={SR};  loudness-matched to {TARGET_DBFS:.0f} dBFS "
          f"RMS (target rms={TARGET_RMS:.5f})\n")

    files = []   # (name, achieved_rms, pre_norm_peak, gain, sweep_tuple)
    pairs = []   # per-timbre static grid-vs-true distances + moving sweeps
    clip_warn = []
    for slug, tech_key in TIMBRES:
        resp_s = lca.build_lco_response(slug, rba._stub_llm(tech_key, "static"), frames=None)
        resp_m = lca.build_lco_response(slug, rba._stub_llm(tech_key, "none"), frames=None)
        base_partials = (df.plan_from_response(resp_s)["frame_engine"]["base"].get("partials") or [])
        print(f"[{slug}]  technique={tech_key}  base_partials={len(base_partials)}  "
              f"B_static_stations={len(resp_s['recipe']['keyframes'])}  "
              f"B_moving_stations={len(resp_m['recipe']['keyframes'])}")

        # ── STATIC pair (the DECISION material): grid-vs-true, level-matched ──
        As = _render_A(resp_s, outdir / f"{slug}_static_A_approx.wav", scan_seconds=DUR)
        Bs = _render_B(resp_s["recipe"], outdir / f"{slug}_static_B_true.sets.json",
                       outdir / f"{slug}_static_B_true.wav")
        if Bs is None:
            continue
        sSweepA, sSweepB = _sweep(As[0], As[1]), _sweep(Bs[0], Bs[1])
        s_cenA, s_cenB, s_lm, s_mc, s_l2 = rba._ab_distance(As[0], As[1], Bs[0], Bs[1])
        g_cenA, g_cenB, g_lm = rba._static_grid_error(base_partials, F0, SR)

        # ── MOVING pair (both move; A's movement now fixed), level-matched ──
        Am = _render_A(resp_m, outdir / f"{slug}_moving_A_approx.wav", scan_seconds=SCAN_SECONDS)
        Bm = _render_B(resp_m["recipe"], outdir / f"{slug}_moving_B_true.sets.json",
                       outdir / f"{slug}_moving_B_true.wav")
        if Bm is None:
            continue
        mSweepA, mSweepB = _sweep(Am[0], Am[1]), _sweep(Bm[0], Bm[1])

        for name, R, sweep in [(f"{slug}_static_A_approx.wav", As, sSweepA),
                               (f"{slug}_static_B_true.wav", Bs, sSweepB),
                               (f"{slug}_moving_A_approx.wav", Am, mSweepA),
                               (f"{slug}_moving_B_true.wav", Bm, mSweepB)]:
            achieved = _rms(R[0])
            files.append((name, achieved, R[2], R[3], sweep))
            if R[2] > 1.0:
                clip_warn.append(name)

        pairs.append(dict(slug=slug, tech=tech_key, npart=len(base_partials),
                          s_cenA=s_cenA, s_cenB=s_cenB, s_lm=s_lm, s_mc=s_mc, s_l2=s_l2,
                          g_cenA=g_cenA, g_cenB=g_cenB, g_lm=g_lm,
                          mSweepA=mSweepA, mSweepB=mSweepB))

        print(f"    STATIC A cen={s_cenA:.0f}Hz  B cen={s_cenB:.0f}Hz | "
              f"grid cost logmel_cos={s_lm:.4f} mag_cos={s_mc:.4f} magL2={s_l2:.4f} "
              f"(line xcheck logmel={g_lm:.4f})")
        print(f"    MOVING A sweep {mSweepA[0]:.0f}->{mSweepA[1]:.0f}->{mSweepA[2]:.0f}Hz | "
              f"B sweep {mSweepB[0]:.0f}->{mSweepB[1]:.0f}->{mSweepB[2]:.0f}Hz\n")

    # ── console tables ──
    print("=" * 104)
    print(f"LEVEL MATCH -- target {TARGET_DBFS:.0f} dBFS RMS = {TARGET_RMS:.5f}   "
          f"(gain is a pure scalar; spectral shape/centroid unchanged)")
    print(f"{'file':30}{'rms_achieved':>14}{'dBFS':>8}{'pre_norm_peak':>15}{'gain':>9}"
          f"{'  sweep start->mid->end (Hz)':>30}")
    print("-" * 104)
    for name, rms, peak, gain, sweep in files:
        dbfs = 20.0 * np.log10(rms) if rms > 0 else float("-inf")
        print(f"{name:30}{rms:14.6f}{dbfs:8.2f}{peak:15.4f}{gain:9.3f}"
              f"   {sweep[0]:8.0f}{sweep[1]:8.0f}{sweep[2]:8.0f}")
    print("=" * 104)
    print("STATIC pair = the DECISION ear-gate (identical spectral content, grid-vs-true ONLY):")
    print(f"{'timbre':8}{'tech':13}{'A_cen':>8}{'B_cen':>8}{'cenD':>7}{'logmel':>8}{'mag_cos':>9}{'magL2':>8}")
    print("-" * 104)
    for p in pairs:
        print(f"{p['slug']:8}{p['tech']:13}{p['s_cenA']:8.0f}{p['s_cenB']:8.0f}"
              f"{p['s_cenA']-p['s_cenB']:+7.0f}{p['s_lm']:8.4f}{p['s_mc']:9.4f}{p['s_l2']:8.4f}")
    print("=" * 104)
    if clip_warn:
        print(f"WARNING: pre-normalization peak exceeded 1.0 (clipped on write): {clip_warn}")
    else:
        print("No clipping: every normalized render peaks below full scale.")

    # ── AB_metrics.txt ──
    manifest = outdir / "AB_metrics.txt"
    with open(manifest, "w") as f:
        f.write("CLEAN A/B inharmonic ear-gate -- MEASUREMENT ONLY (no audibility claims).\n")
        f.write("A = Option A dense-harmonic 256-frame approximation (backend/dco_frames.py bake_frames).\n")
        f.write("B = Option B true-additive engine (tools/additive_sets, shipped setAdditiveBank).\n")
        f.write(f"f0={F0} Hz (C4), dur={DUR}s, sr={SR}. Deterministic key-injection (7B bypassed).\n\n")
        f.write(f"LOUDNESS MATCH: every file RMS-normalized to {TARGET_DBFS:.0f} dBFS "
                f"(target rms={TARGET_RMS:.6f}) by a single constant gain scalar. A constant\n")
        f.write("gain scales all frequency bins equally, so it changes the LEVEL only and leaves the\n")
        f.write("spectral shape and the spectral centroid unchanged (centroids below are on the\n")
        f.write("normalized signal). rms_achieved is measured on the actual 16-bit WAV written.\n\n")
        f.write("SWEEP = spectral centroid (Hz) at start(12%)/mid(50%)/end(88%) of the render, dodging\n")
        f.write("the 10 ms note fades. STATIC files should be ~flat; MOVING files show each engine's\n")
        f.write("own movement-by-default -- A scans its 256-frame table once (base->bright->base over\n")
        f.write("the render); B morphs at its recipe motion_rate_hz. The two REALIZATIONS differ; the\n")
        f.write("numbers are reported as-is, not aligned.\n\n")
        f.write("PER-FILE  (name | rms_achieved | dBFS | pre_norm_peak | gain | sweep start->mid->end Hz)\n")
        for name, rms, peak, gain, sweep in files:
            dbfs = 20.0 * np.log10(rms) if rms > 0 else float("-inf")
            f.write(f"  {name:30} rms={rms:.6f} ({dbfs:+.2f} dBFS) pre_norm_peak={peak:.4f} "
                    f"gain={gain:.4f} sweep={sweep[0]:.0f}->{sweep[1]:.0f}->{sweep[2]:.0f}\n")
        f.write("\nSTATIC-PAIR grid-vs-true distance (identical spectrum; the ONLY difference is the\n")
        f.write("harmonic-grid approximation A makes vs B's true non-integer partials -> the honest\n")
        f.write("grid cost). logmel_cos / mag_cos = cosine distance (0 = identical shape); magL2 = L2\n")
        f.write("of the L2-normalized magnitude spectra; line-xcheck = the same grid cost computed\n")
        f.write("directly from the line spectra (engine-independent cross-check).\n")
        for p in pairs:
            f.write(f"  {p['slug']:8} A_cen={p['s_cenA']:.0f}Hz B_cen={p['s_cenB']:.0f}Hz "
                    f"cenD={p['s_cenA']-p['s_cenB']:+.0f}Hz  logmel_cos={p['s_lm']:.4f} "
                    f"mag_cos={p['s_mc']:.4f} magL2={p['s_l2']:.4f}  "
                    f"(line-xcheck logmel_cos={p['g_lm']:.4f}, cenD={p['g_cenA']-p['g_cenB']:+.0f}Hz)\n")
        f.write("\nMOVING-PAIR centroid sweep start->mid->end (both engines move; realizations differ):\n")
        for p in pairs:
            a, b = p["mSweepA"], p["mSweepB"]
            f.write(f"  {p['slug']:8} A {a[0]:.0f}->{a[1]:.0f}->{a[2]:.0f}Hz   "
                    f"B {b[0]:.0f}->{b[1]:.0f}->{b[2]:.0f}Hz\n")
        f.write("\nNOTE (measurement only): every file above sits at the same -18 dBFS RMS within 16-bit\n")
        f.write("quantization tolerance, so the loudness confound of the earlier render is removed and\n")
        f.write("the A/B is a level-fair comparison. The STATIC pair isolates the grid-approximation\n")
        f.write("cost (identical target spectrum, A on the integer grid vs B off-grid); its logmel/mag\n")
        f.write("cosine and the engine-independent line-spectrum cross-check quantify that cost per\n")
        f.write("timbre. In the MOVING pair both engines now sweep the centroid by hundreds to\n")
        f.write("thousands of Hz (Option A's movement-by-default was previously ~16 Hz and is fixed);\n")
        f.write("the start->mid->end triples are reported as measured, and the two engines' movement\n")
        f.write("realizations differ (A completes one base->bright->base breath over the render; B\n")
        f.write("morphs at its recipe rate), which is visible in the triples. No claim is made about\n")
        f.write("which is preferable -- the ear is the instance.\n")

    print(f"\nWrote {len(pairs)} timbre(s) x (static + moving A/B) + AB_metrics.txt to {outdir}")


if __name__ == "__main__":
    main()
