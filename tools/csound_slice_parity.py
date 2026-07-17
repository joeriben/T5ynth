#!/usr/bin/env python3
"""Objective parity gate for the Csound backend's capability slice.

The migration discipline (CLAUDE.md "Migration & Substrate Discipline") requires
that a capability be proven on the REAL pipeline output with per-capability
objective assertions that would FAIL on a static/null implementation — never a
green suite over the easy (static) cases, never a curated demo WAV. This tool is
that gate. It builds each idiom through backend/csound_orch.py (the real
assembler), renders it through tools/csound_orch_check (real Csound), and asserts
the capability's signature. It renders to a temp dir and measures — it never
presents a WAV as proof.

Two layers, both required to pass (exit 0):
  A. Substrate-idiom grep — the emitted orchestra must contain the REAL Csound
     opcode the capability was chosen for (pwm->vco2, FM->foscili, dirt->tanh,
     morph->linseg+reinit additive bank). An `oscili`-only bank is the toy
     vocabulary the rule forbids; this catches a regression to it mechanically.
  B. Rendered signature — pwm's spectrum MOVES, dirt raises THD on a sine,
     the morph's spectral centroid TRAVELS across the note AND RESETS per note.

Usage:  .venv/bin/python tools/csound_slice_parity.py
Requires numpy + scipy (the backend .venv has them) and a built
tools/csound_orch_check (this script builds it if missing).
"""
import os, sys, subprocess, tempfile, math, shutil
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "backend"))
import csound_orch as co

CHECK = os.path.join(ROOT, "tools", "csound_orch_check")
FREQ = 220
fails = []


def ensure_check():
    if os.path.exists(CHECK):
        return
    prefix = subprocess.check_output(["brew", "--prefix", "csound"], text=True).strip()
    subprocess.check_call([
        "clang++", "-std=c++17", "-O2", f"-I{prefix}/include",
        os.path.join(ROOT, "tools", "csound_orch_check.cpp"),
        f"-F{prefix}/Frameworks", "-framework", "CsoundLib64",
        f"-Wl,-rpath,{prefix}/Frameworks", "-o", CHECK])


def render(tmp, name, techs, adjs, motion, morph_sec=None):
    orc, reading = co.build_orchestra(techs, adjs, motion, morph_sec=morph_sec)
    op, wp = os.path.join(tmp, name + ".orc"), os.path.join(tmp, name + ".wav")
    open(op, "w").write(orc)
    r = subprocess.run([CHECK, "render", op, wp, str(FREQ)], capture_output=True, text=True)
    if r.returncode != 0:
        fails.append(f"{name}: render failed ({r.stdout[-160:]})")
        return None, orc, reading
    return wp, orc, reading


def load(wp):
    from scipy.io import wavfile
    sr, x = wavfile.read(wp)
    if x.ndim > 1:
        x = x[:, 0]
    if x.dtype.kind in "iu":
        x = x.astype(np.float64) / np.iinfo(x.dtype).max
    return x.astype(np.float64), sr


def centroid(x, sr, t, win=2048):
    i = int(t * sr)
    seg = x[i:i + win] * np.hanning(win)
    mag = np.abs(np.fft.rfft(seg))
    f = np.fft.rfftfreq(win, 1 / sr)
    s = mag.sum()
    return float((f * mag).sum() / s) if s > 1e-9 else 0.0


def thd(x, sr, f0, nh=12):
    w = x * np.hanning(len(x))
    mag = np.abs(np.fft.rfft(w))
    f = np.fft.rfftfreq(len(x), 1 / sr)
    def peak(fc):
        b = (f > fc - 25) & (f < fc + 25)
        return float(mag[b].max()) if b.any() else 0.0
    fund = peak(f0)
    return math.sqrt(sum(peak(n * f0) ** 2 for n in range(2, nh + 1))) / fund if fund > 1e-9 else 0.0


def check(cond, label):
    print(f"  [{'PASS' if cond else 'FAIL'}] {label}")
    if not cond:
        fails.append(label)


def main():
    ensure_check()
    tmp = tempfile.mkdtemp(prefix="csound_parity_")
    try:
        print("A. Substrate-idiom grep (real opcodes, not a toy oscili bank):")
        _, orc_pwm, _ = render(tmp, "pwm", ["pwm"], [], None)
        check("vco2" in orc_pwm and "kpw" in orc_pwm, "pwm emits vco2 + kpw (real PWM, not a static square stack)")
        _, orc_fm, _ = render(tmp, "fm", ["fm_bell"], [], None)
        check("foscili" in orc_fm, "fm_bell emits foscili (real FM)")
        _, orc_dirt, _ = render(tmp, "dirt", ["sine"], ["distorted"], None)
        check("tanh" in orc_dirt, "distorted emits a tanh waveshaper (real nonlinearity)")
        _, orc_morph, _ = render(tmp, "grepmorph", ["fm_bell", "sine"], [], None, morph_sec=0.9)
        check("linseg" in orc_morph and "reinit" in orc_morph and orc_morph.count("oscili") >= 2,
              "morph emits a per-note reinit linseg additive bank (moving spectrum)")

        print("\nB. Rendered signatures (must fail on a static impl):")
        wp, _, rd = render(tmp, "morph", ["fm_bell", "sine"], [], None, morph_sec=0.9)
        if wp:
            x, sr = load(wp)
            a, b, c = centroid(x, sr, 0.05), centroid(x, sr, 0.85), centroid(x, sr, 1.05)
            print(f"      morph[{rd}] centroid start={a:.0f} end={b:.0f} reset(after retrig)={c:.0f} Hz")
            check(a > b * 1.4, "morph centroid TRAVELS start>>end (spectral trajectory)")
            check(c > b * 1.3, "morph RESETS on the per-note retrigger (intrinsic, reproducible)")

        wp, _, _ = render(tmp, "pwmmove", ["pwm"], [], None)
        if wp:
            x, sr = load(wp)
            cs = np.array([centroid(x, sr, t) for t in np.arange(0.1, 1.9, 0.03)])
            print(f"      pwm centroid std over sustain = {cs.std():.0f} Hz (static ~ 0)")
            check(cs.std() > 30, "pwm spectrum MOVES (duty-cycle modulation)")

        wpc, _, _ = render(tmp, "clean", ["sine"], [], None)
        wpd, _, _ = render(tmp, "dirty", ["sine"], ["distorted"], None)
        if wpc and wpd:
            xc, sr = load(wpc)
            xd, _ = load(wpd)
            tc = thd(xc[int(0.5 * sr):int(1.5 * sr)], sr, FREQ)
            td = thd(xd[int(0.5 * sr):int(1.5 * sr)], sr, FREQ)
            print(f"      THD clean-sine={tc:.3f}  distorted-sine={td:.3f}")
            check(td > tc * 3 and td > 0.1, "dirt raises THD (waveshaper adds real harmonics)")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    print()
    if fails:
        print(f"PARITY GATE FAILED ({len(fails)}):")
        for f in fails:
            print("  - " + f)
        sys.exit(1)
    print("PARITY GATE PASSED — pwm/FM/dirt/morph are real Csound idioms, measured on the real pipeline.")


if __name__ == "__main__":
    main()
