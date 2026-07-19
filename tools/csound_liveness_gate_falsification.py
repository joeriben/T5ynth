"""Falsify the liveness gate: break the orchestra on purpose, demand it notices.

This exists because csound_morph_liveness_gate.py has been defeated SIX times,
and on every one of those occasions its own test suite was green. A gate is a
claim about what cannot pass it, and a claim nobody attacks is decoration. So
this file attacks it, and it is checked in for one specific reason: each time
the gate was repaired, the falsification was done ad hoc in a scratch directory
and thrown away, and the next round then reintroduced a fault an earlier
scratch script would have caught in one run.

Every injection edits a SANDBOX COPY of backend/csound_orch.py in a temp dir.
The repo is never written to. The gate is then run against that copy and is
required to exit non-zero.

THE AXES, and why the list of them is the actual content of this file
--------------------------------------------------------------------
Two earlier suites each shared, by construction, the blind spot of the code they
certified -- which is the failure mode to design against, because such a suite
does not merely miss things, it actively certifies:

  * The first was nine spectral injections to one amplitude injection. It
    declared sound a gate under which a fall in amplitude motion could not fail
    by any path. Its single amplitude miss was written off as "weakened, not
    stopped"; it was a total stop.
  * The second was balanced across amplitude and spectrum, but every injection
    turned a control DOWN. So it could only probe lower bounds, and the gate it
    certified had nothing but lower bounds -- a key documented as deliberately
    stationary could be handed a tremolo and nothing looked.

Both metrics are peak-to-peak SPREADS: range statistics over a 9 s render. That
tells you where the blind spots must be, and the axes follow from it:

  A  AMPLITUDE motion reduced
  S  SPECTRAL motion reduced
  R  RATE changed -- sped far past what it models, or slowed until it cannot
     complete a cycle inside the render window, which is a freeze wearing a
     running control's clothes and has the same source-code shape as health
  C  CONTOUR changed with the RANGE HELD -- a drift that steps between its
     extremes instead of gliding has an identical range statistic and is a
     different instrument. Invisible to both metrics by construction, which is
     precisely why it belongs here
  N  motion INVENTED where the code documents its absence (upper bounds)
  X  the gate's OWN exemption tables

Run: .venv/bin/python tools/csound_liveness_gate_falsification.py [--sr 44100]
Slow on purpose -- each injection is a full gate run over 39 keys x 5 pitches,
so the suite is tens of minutes per rate. It is a pre-commit tool for changes to
the gate or to the motion controls, not a per-save one.
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PY = str(ROOT / ".venv" / "bin" / "python")
SR = int(sys.argv[sys.argv.index("--sr") + 1]) if "--sr" in sys.argv else 48000

SCALED = ('        L.append(f"  kbtp{tag}   min {i0}, kbmx{tag}          '
          '; brightest the band allows")\n'
          '        L.append(f"  kbnx{tag}   = kbtp{tag} * ({i1 / i0:.4f} + '
          '{1 - i1 / i0:.4f} * "\n'
          '                 f"(1 - min(ktm{tag} / {tau}, 1))) ; darkens as it rings")\n')
CLIPPED = ('        L.append(f"  kbrr{tag}   = {i1} + {i0 - i1:.2f} * '
           '(1 - min(ktm{tag} / {tau}, 1))")\n'
           '        L.append(f"  kbnx{tag}   min kbrr{tag}, kbmx{tag}")\n')

DRIFT = ('f"  kvdr{tag}   poscil 0.0007, 0.043 + 0.0037 * ivoice, giSine, '
         'ivph{tag}"')


def revert_fm(src):
    a = src.index('elif technique in ("fm_bell", "fm", "metallic_fm"):')
    b = src.index('elif technique == "cheby":', a)
    return src[:a] + (
        'elif technique in ("fm_bell", "fm", "metallic_fm"):\n'
        '        L.append(f"  {ov}    foscili 0.5, kfreq, 1, 1.41, 6.0, '
        'giSine ; static FM")\n    ') + src[b:]


def in_branch(src, tech, old, new):
    a = src.index(f'elif technique == "{tech}":')
    b = src.index("    elif technique", a + 10)
    seg = src[a:b]
    assert old in seg, f"{old!r} not in the {tech} branch"
    return src[:a] + seg.replace(old, new) + src[b:]


def drift_contour(src):
    """Per-voice drift keeps its DEPTH and RATE, loses its CONTOUR.

    +/-0.0007 at 0.043 Hz either way, so both metrics read an identical range;
    but as a pitch factor a square jump is a stepped detune, not analogue
    microfluctuation, and gliding is _emit_vco_drift's entire stated purpose.
    """
    assert DRIFT in src
    return src.replace(DRIFT, (
        'f"  kvdw{tag}   poscil 1.0, 0.043 + 0.0037 * ivoice, giSine, '
        'ivph{tag}",\n'
        '            f"  kvdr{tag}   = 0.0007 * (kvdw{tag} > 0 ? 1 : -1)"'))


INJECTIONS = {
    # ---- A: AMPLITUDE motion reduced ----
    "A: revert the balance fix (ihp 1 -> 10)": lambda s: s.replace(
        "balance abs{tag}, abr{tag}, 1", "balance abs{tag}, abr{tag}"),
    # The doublet is now the SAME ratio pair started dtw Hz above the
    # fundamental (0bea3fd1 replaced the two-modulator-ratio scheme, whose
    # beat scaled with pitch into the roughness band). Collapsing it means
    # putting the twin's carrier back on kfreq: same ratio, same carrier as
    # its partner, so no pair can beat -- the regression this case represents.
    "A: doublet collapsed (twin carrier back on kfreq)": lambda s: s.replace(
        "foscili {a2}, kfreq + {dtw},", "foscili {a2}, kfreq,"),
    "A: doublet silenced (a2 := 0)": lambda s: s.replace(
        "abt{tag}    foscili {a2}, kfreq", "abt{tag}    foscili 0.0, kfreq"),
    "A: sub_sine divider drift nulled": lambda s: s.replace(
        "poscil 0.0006, 0.067", "poscil 0.0, 0.067"),
    "A: harpsichord choirs locked together": lambda s: s.replace(
        "khw4{tag}   poscil 0.00055, 0.094", "khw4{tag}   poscil 0.0, 0.094"),
    "A: organ ranks locked together": lambda s: in_branch(
        s, "organ", "poscil 0.00045, 0.083", "poscil 0.0, 0.083"),
    # ---- S: SPECTRAL motion reduced ----
    "S: revert the FM branch": revert_fm,
    "S: revert FM + drop from _LIVE_TECH": lambda s: revert_fm(s).replace(
        '    "fm", "fm_bell", "metallic_fm"}', "}", 1),
    "S: tau units slip x100": lambda s: s.replace(
        "min(ktm{tag} / {tau}, 1)", "min(ktm{tag} / ({tau} * 100), 1)"),
    "S: index sweep -> 2% of excursion": lambda s: s.replace(
        "{i1 / i0:.4f} + {1 - i1 / i0:.4f} * ",
        "{1 - 0.02 * (1 - i1 / i0):.4f} + {0.02 * (1 - i1 / i0):.4f} * "),
    "S: historic min-clip (freeze from 2030 Hz)":
        lambda s: s.replace(SCALED, CLIPPED),
    "S: historic min-clip, freeze from ~100 Hz":
        lambda s: s.replace(SCALED, CLIPPED).replace(
            "kbmx{tag}   limit (sr * 0.45 / kfreq - 1)",
            "kbmx{tag}   limit (sr * 0.02217 / kfreq - 1)"),
    "S: square comparator frozen above 150 Hz": lambda s: in_branch(
        s, "square", "poscil 0.012, 0.057",
        "poscil 0.012 * (kfreq < 150 ? 1 : 0), 0.057"),
    "S: pulse comparator frozen above 150 Hz": lambda s: in_branch(
        s, "pulse", "poscil 0.012, 0.057",
        "poscil 0.012 * (kfreq < 150 ? 1 : 0), 0.057"),
    "S: per-voice analogue drift nulled": lambda s: s.replace(
        "poscil 0.0007, 0.043", "poscil 0.0, 0.043"),
    "S: strings bow pressure nulled": lambda s: s.replace(
        "poscil 0.35, {rb}", "poscil 0.0, {rb}"),
    # ---- R: RATE changed, depth untouched ----
    "R: comparator drift 0.057 -> 11.4 Hz": lambda s: s.replace(
        "poscil 0.012, 0.057", "poscil 0.012, 11.4"),
    "R: comparator drift 0.057 -> 0.00057 Hz": lambda s: s.replace(
        "poscil 0.012, 0.057", "poscil 0.012, 0.00057"),
    "R: per-voice drift 0.043 -> 8.6 Hz": lambda s: s.replace(
        "poscil 0.0007, 0.043", "poscil 0.0007, 8.6"),
    "R: sub_sine divider 0.067 -> 0.00067 Hz": lambda s: s.replace(
        "poscil 0.0006, 0.067", "poscil 0.0006, 0.00067"),
    # ---- C: CONTOUR changed, range held ----
    "C: per-voice drift glides -> steps": drift_contour,
    # ---- N: motion INVENTED where the code documents its absence ----
    "N: hiss gains a 3.1 Hz tremolo": lambda s: in_branch(
        s, "hiss", 'L.append(f"  {ov}    = {ov} * 0.7")',
        'L.append(f"  khtr{tag}   poscil 0.45, 3.1")\n'
        '        L.append(f"  {ov}    = {ov} * 0.7 * (1 + khtr{tag})")'),
    # ---- X: the gate's own exemption tables ----
    "X: sine gains a 30 Hz tremolo": lambda s: s.replace(
        'L.append(f"  {ov}    poscil 0.6, kfreq            ; pure tone")',
        'L.append(f"  ktrm{tag}   poscil 0.3, 30")\n'
        '        L.append(f"  {ov}    poscil 0.6 * (1 + ktrm{tag}), kfreq")'),
}

# Injections the gate is KNOWN not to catch, each with the reason. Same
# discipline as the gate's own FLATTEN_ANYWAY: an entry is a claim, the suite
# prints all of them, and a STALE entry -- one that starts being caught -- is a
# failure, so this list cannot quietly become an excuse for a gate that has
# stopped working. Filled from measurement, not from expectation.
KNOWN_MISSED = {
    "A: organ ranks locked together":
        "three ranks wander independently and only one is nulled, so the key is "
        "genuinely still moving. Needs a per-CONTROL probe (null one line, "
        "require the OUTPUT to change), which is a different tool.",
    "A: harpsichord choirs locked together":
        "the 4' choir is locked but the fixed 0.0031 detune still beats against "
        "the 8', so the key still moves. Same per-control limit as above.",
    "S: strings bow pressure nulled":
        "three players keep independent intonation, so the ensemble still "
        "moves. Same per-control limit as above.",
}


def run(name, patch):
    d = Path(tempfile.mkdtemp())
    try:
        (d / "backend").mkdir()
        (d / "tools").mkdir()
        src = (ROOT / "backend" / "csound_orch.py").read_text()
        try:
            out = patch(src)
        except (AssertionError, ValueError) as e:
            print(f"{name:44s} INJECTION FAILED TO APPLY: {e}", flush=True)
            return None
        if out == src:
            print(f"{name:44s} INJECTION DID NOT APPLY -- re-grep", flush=True)
            return None
        (d / "backend" / "csound_orch.py").write_text(out)
        for f in ("csound_morph_liveness_gate.py", "csound_liveness_baseline.json"):
            shutil.copy(ROOT / "tools" / f, d / "tools" / f)
        p = subprocess.run(
            [PY, str(d / "tools" / "csound_morph_liveness_gate.py"),
             "--sr", str(SR)], capture_output=True, text=True, timeout=7200)
        rows = [ln for ln in p.stdout.splitlines()
                if re.search(r"(DEAD|FELL|MOVES at|renders no audio|FLATTENED|"
                             r"NOT BASELINED)", ln)]
        print(f"{name:44s} exit {p.returncode}  "
              f"{'CAUGHT' if p.returncode else '*** MISSED ***'}", flush=True)
        for r in rows[:3]:
            print(f"      {r.rstrip()[:150]}", flush=True)
        return p.returncode
    finally:
        shutil.rmtree(d, ignore_errors=True)


def main():
    print(f"=== falsifying the liveness gate at {SR} Hz "
          f"({len(INJECTIONS)} injections) ===", flush=True)
    broken, missed = [], []
    for name, patch in INJECTIONS.items():
        rc = run(name, patch)
        if rc is None:
            broken.append(name)
        elif rc == 0:
            missed.append(name)
    print(f"\n{len(INJECTIONS) - len(missed) - len(broken)}/{len(INJECTIONS)} "
          f"caught")
    fails = []
    for n in broken:
        # An injection that no longer applies is not a pass. It means the source
        # moved under it and the case has silently stopped being tested, which is
        # the same rot this whole subsystem keeps suffering.
        fails.append(f"injection no longer applies (re-grep the source): {n}")
    for n in missed:
        if n not in KNOWN_MISSED:
            fails.append(f"NOT CAUGHT and not a declared limit: {n}")
    for n in KNOWN_MISSED:
        if n not in INJECTIONS:
            fails.append(f"KNOWN_MISSED names an injection that does not exist: {n}")
        elif n not in missed:
            fails.append(f"KNOWN_MISSED entry is STALE -- now caught: {n}")
    if missed:
        print("\ndeclared limits (each is a claim, not an excuse):")
        for n in sorted(set(missed) & set(KNOWN_MISSED)):
            print(f"  {n}\n      {KNOWN_MISSED[n]}")
    if fails:
        print(f"\nFAIL: {len(fails)} problem(s)")
        for f in fails:
            print(f"  {f}")
        return 1
    print("\nOK: every injection is either caught or a declared, current limit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
