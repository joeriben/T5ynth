#!/usr/bin/env python3
"""Deterministic regression suite for the M2 multi-oscillator architecture.

Fast (no 7B): feeds oscillator specs straight into build_orchestra(oscs=...) and
gates each emitted orchestra. Certifies the up-to-3-oscillator machinery:
per-osc morph chains, per-osc volume, the loudness-stable weighted mix, and the
morph-to-zero transient (the one authorized pseudo-envelope). Complements
csound_keys_idioms.py (single-osc DSP) and csound_keys_corpus.py (prompt->7B
parity). Includes the WORST-CASE CPU case (3 simultaneous morph banks) benched
against the 133 us/ksmps budget on the 16-voice always-on instrument.

Exit != 0 on any real FAIL (clip / silent-when-standing / didn't-decay-when-
transient / didn't-move-when-moving / over CPU budget).
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "backend"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import csound_orch as co
from csound_keys_gate import gate, _fmt

# (label, build_orchestra kwargs, expect)
CASES = [
    # single osc, back-compat sanity through the oscs= path
    ("1osc saw",
     dict(oscs=[{"chain": ["saw"], "vol": 1.0}]),
     {"sustain": "stand", "move": False}),
    # two layers: fat saw + sub, the classic stack
    ("2osc saw@1 + sub@0.6",
     dict(oscs=[{"chain": ["saw"], "vol": 1.0}, {"chain": ["sub_sine"], "vol": 0.6}]),
     {"sustain": "stand", "move": False}),
    # three layers at FULL volume — worst-case bounds (mix must stay < 1)
    ("3osc saw+square+sub @1,1,1",
     dict(oscs=[{"chain": ["saw"], "vol": 1.0}, {"chain": ["square"], "vol": 1.0},
                {"chain": ["sub_sine"], "vol": 1.0}]),
     {"sustain": "stand", "move": False}),
    # per-osc morph chain: osc1 morphs glass->sine while osc2 holds a saw
    ("2osc morph(glass>sine)@1 + saw@0.4",
     dict(oscs=[{"chain": ["glass", "sine"], "vol": 1.0}, {"chain": ["saw"], "vol": 0.4}]),
     {"sustain": "stand", "move": True}),
    # morph-to-zero: a plucked transient that fades to nothing (pseudo-env)
    ("1osc morph-to-zero saw>silence",
     dict(oscs=[{"chain": ["saw", "silence"], "vol": 1.0}]),
     {"sustain": "transient", "move": True}),
    ("1osc morph-to-zero fm_bell>silence",
     dict(oscs=[{"chain": ["fm_bell", "silence"], "vol": 1.0}]),
     {"sustain": "transient", "move": True}),
    # a transient layer ON TOP of a sustaining body: overall sound STANDS (the
    # sustaining osc dominates the late window) but has a percussive attack
    ("2osc sustain(saw)@1 + transient(bell>silence)@0.9",
     dict(oscs=[{"chain": ["saw"], "vol": 1.0}, {"chain": ["fm_bell", "silence"], "vol": 0.9}]),
     {"sustain": "stand"}),
    # global motion over a 3-osc stack (movement by default across the mix)
    ("3osc + global evolve",
     dict(oscs=[{"chain": ["saw"], "vol": 1.0}, {"chain": ["square"], "vol": 0.7},
                {"chain": ["sub_sine"], "vol": 0.5}], motion_key="evolve"),
     {"sustain": "stand", "move": True}),
    # per-osc vol=0 layer is harmless (contributes nothing, still bounded)
    ("2osc saw@1 + square@0.0",
     dict(oscs=[{"chain": ["saw"], "vol": 1.0}, {"chain": ["square"], "vol": 0.0}]),
     {"sustain": "stand", "move": False}),
    # an all-silent oscillator is dropped; the remaining saw stands
    ("2osc saw@1 + silence@1 (dropped)",
     dict(oscs=[{"chain": ["saw"], "vol": 1.0}, {"chain": ["silence"], "vol": 1.0}]),
     {"sustain": "stand", "move": False}),
    # WORST-CASE CPU: three simultaneous morph banks, all moving
    ("CPU 3osc all-morph (glass>sine + saw>square + additive>bell)",
     dict(oscs=[{"chain": ["glass", "sine"], "vol": 1.0},
                {"chain": ["saw", "square"], "vol": 0.8},
                {"chain": ["additive", "fm_bell"], "vol": 0.6}]),
     {"sustain": "stand", "move": True}),
]

CPU_BUDGET_US = 133.0  # 10% of the 1333 us block budget (same as the gate's bench)


def run():
    passed = failed = 0
    fails = []
    worst_bench = 0.0
    for label, kw, expect in CASES:
        orc, reading = co.build_orchestra(**kw)
        r = gate(orc, expect, label=label[:40])
        b = r["measurements"].get("bench_us") or 0.0
        worst_bench = max(worst_bench, b)
        if r["passed"]:
            passed += 1
        else:
            failed += 1
            fails.append(r)
        print(_fmt(r))

    print("\n" + "=" * 70)
    print(f"MULTI-OSC: {passed} passed, {failed} failed  (of {len(CASES)} cases)")
    print(f"worst CPU across cases: {worst_bench:.1f} us/ksmps  (budget {CPU_BUDGET_US:.0f})")
    if worst_bench > CPU_BUDGET_US:
        print("  !! CPU BUDGET EXCEEDED")
        failed += 1
    if fails:
        print(f"\nFAILURES ({len(fails)}):")
        for r in fails:
            print("  " + r["label"] + ": " + "; ".join(r["failures"]))
    return failed


if __name__ == "__main__":
    sys.exit(1 if run() else 0)
