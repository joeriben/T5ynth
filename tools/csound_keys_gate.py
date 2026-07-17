#!/usr/bin/env python3
"""Behavioral gate for the DETERMINISTIC keys-path Csound assembler.

The keys-path assembler (backend/csound_orch.py) is deterministic: every opcode
it emits is authored by hand, so the platform fundamentals are guaranteed *by
construction*. This gate VERIFIES them on the real emitted orchestra (defense in
depth + regression catch as the vocabulary grows), across four layers:

  STRUCTURE  %SR% marker present and NO literal `sr = <number>`; exactly 16
             always-on score lines `i 1 0 <big> v`; compiles + resolves all
             16x6 contract channels (via `csound_orch_check check`); NO
             forbidden amplitude-envelope / glide opcode (madsr/adsr/linen/
             expon/expseg/transeg on the amp path; `portk` on kfreq beyond the
             fixed 1 ms declick) -- the only authorized amplitude shaping is a
             spectral MORPH-TO-ZERO (BJ 2026-07-18, the pseudo-envelope).
  BOUNDS     an un-normalized `probe` sweep over pitch {55..3520 Hz} at the
             LOUDEST params (vel=pres=timb=1) -- every render true-peak <= 1,
             no NaN/Inf, sustained 10 s (offline render is ~100x realtime, so
             a long sweep is cheap and catches slow filter/feedback buildup).
  CPU        `bench` median <= 133 us/ksmps (10% of the 1333 us block budget).
  BEHAVIOR   standing-tone: RMS over the gate-on region does NOT collapse
             (late >= 0.5x the sustain max) UNLESS the sound is a declared
             TRANSIENT (morph-to-zero), which must decay to ~0; MOVEMENT:
             spectral-centroid travel or flux above a floor when movement is
             expected (morph / motion / pwm). Movement-by-default is a
             documented fundamental, so it is asserted, not assumed.

Library (import `gate`) + CLI. Uses tools/csound_orch_check (built if missing).
Every csound_orch_check call has a hard subprocess timeout (a runaway orchestra
cannot hang the gate).
"""
import os
import sys
import subprocess
import tempfile
import shutil
import math
import re
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHECK = os.path.join(ROOT, "tools", "csound_orch_check")
TIMEOUT = 30  # s, hard cap per csound_orch_check invocation

# Pitch sweep for the bounds pass (musical A1..A7-ish).
SWEEP_HZ = [55.0, 110.0, 220.0, 440.0, 880.0, 1760.0, 3520.0]
PROBE_DUR = 10.0
PROBE_GATE = 9.0

# Amplitude-envelope / glide opcodes the assembler must NEVER emit (the synth
# owns envelope/glide; the only authorized amp shaping is the morph-to-zero).
_FORBIDDEN_ENV = re.compile(
    r'\b(madsr|adsr|xadsr|mxadsr|linen|linenr|expon|expseg|expsegr|transeg|transegr|adsr140)\b')
# portk/port/portk2/lag/sc_lag on kfreq beyond the fixed 1 ms declick would be a
# glide; the ONE allowed portk is `kgate portk kgateraw, 0.001` (the declick).
_PORTK_LINE = re.compile(r'\b(portk|port|portk2|lag|sc_lag|tlineto|lineto)\b')


def ensure_check():
    if os.path.exists(CHECK):
        return
    prefix = subprocess.check_output(["brew", "--prefix", "csound"], text=True).strip()
    subprocess.check_call([
        "clang++", "-std=c++17", "-O2", f"-I{prefix}/include",
        os.path.join(ROOT, "tools", "csound_orch_check.cpp"),
        f"-F{prefix}/Frameworks", "-framework", "CsoundLib64",
        f"-Wl,-rpath,{prefix}/Frameworks", "-o", CHECK])


def _run(args):
    try:
        r = subprocess.run([CHECK] + args, capture_output=True, text=True, timeout=TIMEOUT)
        return r.returncode, r.stdout, r.stderr
    except subprocess.TimeoutExpired:
        return 124, "", f"TIMEOUT after {TIMEOUT}s"


def _load(wp):
    from scipy.io import wavfile
    sr, x = wavfile.read(wp)
    if x.ndim > 1:
        x = x[:, 0]
    if x.dtype.kind in "iu":
        x = x.astype(np.float64) / np.iinfo(x.dtype).max
    return x.astype(np.float64), sr


def _rms_env(x, sr, t0, t1, win=0.05):
    w = max(1, int(win * sr))
    out = []
    for i in range(int(t0 * sr), min(int(t1 * sr), len(x) - w), w):
        out.append(float(np.sqrt(np.mean(x[i:i + w] ** 2))))
    return np.array(out) if out else np.array([0.0])


def _centroid(x, sr, t, win=2048):
    i = int(t * sr)
    seg = x[i:i + win]
    if len(seg) < win:
        return 0.0
    seg = seg * np.hanning(win)
    mag = np.abs(np.fft.rfft(seg))
    f = np.fft.rfftfreq(win, 1 / sr)
    s = mag.sum()
    return float((f * mag).sum() / s) if s > 1e-9 else 0.0


def _static_checks(orc):
    fails = []
    if "%SR%" not in orc:
        fails.append("no %SR% marker (would bake a fixed rate -> detune on reload)")
    if re.search(r'^\s*sr\s*=\s*\d', orc, re.MULTILINE):
        fails.append("literal `sr = <number>` present (must be `sr = %SR%`)")
    n_score = len(re.findall(r'^\s*i\s+1\s+0\s+\d{5,}\s+\d+\s*$', orc, re.MULTILINE))
    if n_score != 16:
        fails.append(f"expected 16 always-on score lines, found {n_score}")
    if _FORBIDDEN_ENV.search(orc):
        fails.append(f"forbidden amp-envelope opcode: {_FORBIDDEN_ENV.search(orc).group(1)}")
    # portk allowed ONLY on the gate declick line; any other portk/glide = fail.
    for m in _PORTK_LINE.finditer(orc):
        line = orc[orc.rfind("\n", 0, m.start()) + 1: orc.find("\n", m.start())]
        if "kgateraw" not in line:  # the one authorized declick
            fails.append(f"glide opcode `{m.group(1)}` outside the gate declick: {line.strip()[:50]}")
            break
    return fails


def gate(orchestra, expect=None, label="orc", keep_dir=None):
    """Run the full behavioral gate on an orchestra STRING.

    expect: dict {sustain: 'stand'|'transient' (default 'stand'), move: bool
            (default False)}. Returns {passed: bool, failures: [str],
            measurements: {...}}.
    """
    ensure_check()
    expect = expect or {}
    sustain = expect.get("sustain", "stand")
    want_move = bool(expect.get("move", False))
    res = {"passed": True, "failures": [], "measurements": {}, "label": label}

    def fail(msg):
        res["passed"] = False
        res["failures"].append(msg)

    for f in _static_checks(orchestra):
        fail("STRUCT: " + f)

    tmp = keep_dir or tempfile.mkdtemp(prefix="keysgate_")
    try:
        orp = os.path.join(tmp, label + ".orc")
        open(orp, "w").write(orchestra)

        # STRUCTURE: compile + contract + 16x6 channels
        rc, out, err = _run(["check", orp])
        if rc != 0:
            fail("STRUCT: check failed -- " + (err.strip().splitlines()[-1] if err.strip() else out.strip()[-160:]))

        # CPU
        rc, out, err = _run(["bench", orp])
        mb = re.search(r"median=([\d.]+)us", out)
        res["measurements"]["bench_us"] = float(mb.group(1)) if mb else None
        if rc != 0:
            fail("CPU: bench over budget -- " + out.strip()[-120:])

        # BOUNDS sweep (loudest params, sustained)
        worst_peak = 0.0
        for hz in SWEEP_HZ:
            wp = os.path.join(tmp, f"{label}_{int(hz)}.wav")
            rc, out, err = _run(["probe", orp, wp, str(hz), "1.0", "1.0", "1.0",
                                 str(PROBE_GATE), str(PROBE_DUR)])
            pm = re.search(r"peak=([\d.]+)", out)
            pk = float(pm.group(1)) if pm else 9.9
            worst_peak = max(worst_peak, pk)
            if rc != 0:
                fail(f"BOUNDS: probe @ {hz:.0f}Hz (vel/pres/timb=1) failed -- " +
                     (err.strip().splitlines()[-1] if err.strip() else out.strip()[-120:]))
        res["measurements"]["worst_peak"] = round(worst_peak, 4)

        # BEHAVIOR: a clean single-strike probe at 220 Hz, default-ish params
        wp = os.path.join(tmp, f"{label}_behav.wav")
        rc, out, err = _run(["probe", orp, wp, "220", "0.85", "0.0", "0.5",
                             str(PROBE_GATE), str(PROBE_DUR)])
        if rc != 0:
            fail("BEHAVIOR: behavior probe failed -- " + out.strip()[-120:])
        else:
            x, sr = _load(wp)
            env = _rms_env(x, sr, 0.1, PROBE_GATE - 0.2)
            emax = float(env.max()) if len(env) else 0.0
            late = float(np.mean(env[-max(1, len(env) // 8):]))
            early = float(np.mean(env[:max(1, len(env) // 8)]))
            res["measurements"]["rms_max"] = round(emax, 4)
            res["measurements"]["rms_late"] = round(late, 4)
            res["measurements"]["rms_early"] = round(early, 4)

            if sustain == "transient":
                # morph-to-zero: must decay to near silence by the end of gate
                if late > 0.15 * max(emax, 1e-9):
                    fail(f"BEHAVIOR: declared transient but did NOT decay "
                         f"(late RMS {late:.4f} vs max {emax:.4f})")
            else:  # stand
                if emax < 1e-3:
                    fail("BEHAVIOR: sustained tone is silent")
                elif late < 0.5 * emax:
                    fail(f"BEHAVIOR: sustained tone COLLAPSES "
                         f"(late RMS {late:.4f} < 0.5x max {emax:.4f}) -- "
                         f"an unauthorized decay envelope?")

            # movement: SPECTRAL (centroid travel / std) OR AMPLITUDE (RMS-
            # envelope modulation depth). Tremolo/vibrato move the amplitude,
            # not the centroid -- either kind counts as movement.
            cs = np.array([_centroid(x, sr, t) for t in np.arange(0.1, min(PROBE_GATE - 0.3, 6.0), 0.08)])
            cs = cs[cs > 0]
            emean = float(np.mean(env)) if len(env) else 0.0
            amp_mod = float(np.std(env) / emean) if emean > 1e-9 else 0.0
            res["measurements"]["amp_mod"] = round(amp_mod, 3)
            if len(cs) >= 3:
                travel = float(cs.max()) / max(float(cs.min()), 1e-9)
                std = float(cs.std())
                res["measurements"]["centroid_travel"] = round(travel, 3)
                res["measurements"]["centroid_std_hz"] = round(std, 1)
                spectral_move = travel >= 1.15 or std >= 25
                amp_move = amp_mod >= 0.08
                if want_move and not (spectral_move or amp_move):
                    fail(f"BEHAVIOR: movement expected but static "
                         f"(centroid travel {travel:.2f}x std {std:.0f}Hz, amp_mod {amp_mod:.2f})")
    finally:
        if keep_dir is None:
            shutil.rmtree(tmp, ignore_errors=True)
    return res


def _fmt(res):
    m = res["measurements"]
    head = f"[{'PASS' if res['passed'] else 'FAIL'}] {res['label']}"
    meas = (f"peak={m.get('worst_peak')} bench={m.get('bench_us')}us "
            f"rms(max/late)={m.get('rms_max')}/{m.get('rms_late')} "
            f"centroid(travel/std)={m.get('centroid_travel')}/{m.get('centroid_std_hz')}Hz "
            f"amp_mod={m.get('amp_mod')}")
    lines = [head + "  " + meas]
    for f in res["failures"]:
        lines.append("    - " + f)
    return "\n".join(lines)


if __name__ == "__main__":
    # CLI: gate an orchestra file, or build from keys and gate.
    #   csound_keys_gate.py <file.orc> [stand|transient] [move]
    #   csound_keys_gate.py --keys pwm,square [stand|transient] [move]
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("target")
    ap.add_argument("sustain", nargs="?", default="stand")
    ap.add_argument("move", nargs="?", default="")
    ap.add_argument("--keys", action="store_true", help="target is comma/>-separated technique keys")
    args = ap.parse_args()
    if args.keys:
        sys.path.insert(0, os.path.join(ROOT, "backend"))
        import csound_orch as co
        keys = args.target.replace(">", ",").split(",")
        orc, reading = co.build_orchestra([k.strip() for k in keys], [], None)
        lbl = args.target.replace(",", "_").replace(">", "to").replace(" ", "")
    else:
        orc = open(args.target).read()
        lbl = os.path.basename(args.target)
    r = gate(orc, {"sustain": args.sustain, "move": args.move == "move"}, label=lbl)
    print(_fmt(r))
    sys.exit(0 if r["passed"] else 1)
