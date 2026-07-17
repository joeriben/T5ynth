#!/usr/bin/env python3
"""Deterministic idiom regression suite for the keys-path assembler.

Fast (no 7B): feeds lexicon KEYS straight into backend/csound_orch.build_orchestra
and runs each emitted orchestra through the behavioral gate (tools/csound_keys_gate).
This is the day-to-day regression harness as the vocabulary grows -- it proves
every technique/adjective/motion/morph the assembler CAN express compiles, stays
bounded, stands (or is a declared transient), and moves when it should.

It is NOT the parity gate (that is the frozen NL-prompt corpus, prompt->7B->
assembler, in tools/csound_keys_corpus.py -- a key-fed suite cannot certify that
a PROMPT reaches the right keys). This suite certifies the ASSEMBLER's DSP.

Reports per-case PASS/FAIL + measurements, then a GAP summary (lexicon keys with
no bespoke idiom yet -> they render as a generic additive/sine fallback; the
vocabulary-expansion milestones close these). Exit != 0 only on a real FAIL
(clip / silent / crash / wrong behavior), never on a documented gap.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "backend"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import csound_orch as co
import dco_recipe
from csound_keys_gate import gate, _fmt

LX = dco_recipe.load_lexicon()
TECHS = [t["key"] for t in LX["techniques"]]
ADJS = [a["key"] for a in LX["adjectives"]]
MOTIONS = [m["key"] for m in LX["motions"]]

# What the assembler currently gives a BESPOKE idiom (read from _emit_steady /
# _emit_adjectives / _emit_motion). Everything else falls to a generic fallback
# -> a documented GAP for the expansion milestones, not a failure.
BESPOKE_TECH = {"pwm", "square", "clarinet", "chiptune", "pulse", "triangle",
                "saw", "supersaw", "brass", "strings", "bass_saw", "sync",
                "fm_bell", "fm", "fm_ep", "metallic_fm", "cheby",
                "sine", "additive"}  # sine/additive are intentionally additive
INTRINSIC_MOVE_TECH = {"pwm"}  # techniques whose idiom moves on its own
BESPOKE_ADJ = {"distorted", "dirty", "aggressive", "growling", "harsh",
               "bright", "sharp", "piercing", "dark", "muddy", "dull", "warm"}
BESPOKE_MOTION = {"sweep", "evolve", "open_up", "close", "breathe", "wobble",
                  "cycle", "shimmer", "vibrate", "flutter", "tremolo"}

MORPHS = [
    ["square", "sine"], ["saw", "sine"], ["sine", "fm_bell"], ["triangle", "saw"],
    ["fm_bell", "sine"], ["pwm", "sine"], ["saw", "square", "sine"],
    ["additive", "sine"], ["metallic_fm", "sine"], ["cheby", "sine"],
]


def run():
    cases = []  # (label, keys, adj, motion, expect, is_gap)
    for t in TECHS:
        cases.append((f"tech:{t}", [t], [], None,
                      {"sustain": "stand", "move": t in INTRINSIC_MOVE_TECH},
                      t not in BESPOKE_TECH))
    for m in MORPHS:
        cases.append((f"morph:{'>'.join(m)}", m, [], None,
                      {"sustain": "stand", "move": True}, False))
    for a in ADJS:
        # every adjective must at least stay bounded on a rich base (saw); the
        # bespoke ones additionally shape it. move not required.
        cases.append((f"adj:{a}", ["saw"], [a], None,
                      {"sustain": "stand"}, a not in BESPOKE_ADJ))
    for mo in MOTIONS:
        want_move = mo in BESPOKE_MOTION
        cases.append((f"mot:{mo}", ["saw"], [], mo,
                      {"sustain": "stand", "move": want_move},
                      mo not in BESPOKE_MOTION and mo != "static"))

    passed = failed = 0
    fails, gaps = [], []
    for label, keys, adj, motion, expect, is_gap in cases:
        orc, reading = co.build_orchestra(keys, adj, motion)
        r = gate(orc, expect, label=label)
        if r["passed"]:
            passed += 1
        else:
            failed += 1
            fails.append(r)
        if is_gap:
            gaps.append(label)
        print(_fmt(r))

    print("\n" + "=" * 70)
    print(f"IDIOMS: {passed} passed, {failed} failed  (of {len(cases)} cases)")
    if gaps:
        print(f"\nGAPS ({len(gaps)}) — lexicon keys with no bespoke idiom yet "
              f"(render as generic fallback; expansion closes these):")
        print("  " + ", ".join(g.split(":", 1)[1] for g in gaps))
    if fails:
        print(f"\nFAILURES ({len(fails)}):")
        for r in fails:
            print("  " + r["label"] + ": " + "; ".join(r["failures"]))
    return failed


if __name__ == "__main__":
    sys.exit(1 if run() else 0)
