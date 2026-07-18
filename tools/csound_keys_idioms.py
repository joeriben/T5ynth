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

# What the assembler gives a BESPOKE idiom (read from _emit_steady / _SPECTRA /
# _emit_adjectives / _emit_motion). A key NOT listed here falls to a generic
# fallback -> a documented GAP for the expansion milestones, not a failure. These
# are FROZEN, HAND-MAINTAINED ledgers of the assembler's real coverage -- NOT
# derived from the lexicon (that would auto-hide a future key's missing idiom).
# M3 (2026-07-18) closed every gap the keys-path had: every technique now renders
# via a live opcode or a real bespoke spectrum; every adjective maps to a bounded
# _ADJ_MAP op; every non-static motion moves. A NEW key added by an expansion
# milestone WITHOUT an idiom will correctly re-surface as a GAP below.
BESPOKE_TECH = {
    # live-opcode idioms (vco2 / foscili / tanh / product)
    "pwm", "square", "clarinet", "chiptune", "pulse", "triangle",
    "saw", "supersaw", "brass", "strings", "bass_saw", "sync",
    "fm_bell", "fm", "fm_ep", "metallic_fm", "cheby", "ring_mod",
    # real bespoke additive spectra (M3: honoured lexicon "why" partial sets)
    "struck_bar", "cymbal", "organ", "flute", "harpsichord", "additive",
    # intentionally (near-)pure sine — a sub / theremin / reference tone
    "sine", "sub_sine", "theremin",
}
INTRINSIC_MOVE_TECH = {"pwm"}  # only pwm's duty moves on its own
BESPOKE_ADJ = set(co._ADJ_MAP)   # the assembler's ACTUAL adjective coverage (M3: all 51)
BESPOKE_MOTION = {"sweep", "evolve", "open_up", "close", "breathe", "wobble",
                  "cycle", "shimmer", "vibrate", "flutter", "tremolo",
                  "pingpong", "settle", "slow", "fast", "snap"}  # M3: all non-static move

MORPHS = [
    ["square", "sine"], ["saw", "sine"], ["sine", "fm_bell"], ["triangle", "saw"],
    ["fm_bell", "sine"], ["pwm", "sine"], ["saw", "square", "sine"],
    ["additive", "sine"], ["metallic_fm", "sine"], ["cheby", "sine"],
    # dense-inharmonic -> sine: must collapse spectrally WITHOUT a pitch glide
    # (the counterpart-ratio padding fix; adversarial review 2026-07-18).
    ["cymbal", "sine"], ["glass", "sine"], ["harpsichord", "sine"],
    ["struck_bar", "sine"], ["organ", "sine"], ["ring_mod", "sine"],
]

# Multi-adjective stacks on VARIED bases. The single-adjective-per-case coverage
# above shares the assembler's own blind spot: it never emits the 3+ adjective
# combos (esp. AIR-noise + resonant/metallic) that drove the final output past
# unity at bass pitches. With the bass-extended sweep (gate SWEEP_HZ down to 20 Hz)
# these exercise the global soft peak-limiter across the worst crest cases.
MULTI_ADJ = [
    ("saw", ["breathy", "resonant", "clangorous"]),        # the exact clip repro
    ("saw", ["deep", "resonant", "clangorous", "piercing", "breathy"]),
    ("square", ["breathy", "resonant", "clangorous"]),      # non-saw base, clips harder
    ("pulse", ["airy", "metallic", "bright"]),
    ("saw", ["boxy", "bright", "fat", "metallic"]),         # dense additive pile-up
    ("sub_sine", ["breathy", "resonant"]),                  # bass source + AIR crest
    ("fm_bell", ["shimmering", "icy", "brittle"]),          # AIR family on inharmonic
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
    for base, adjs in MULTI_ADJ:
        cases.append((f"multi:{base}+{'+'.join(adjs)}", [base], adjs, None,
                      {"sustain": "stand"}, False))
    # M4a noise/texture (Geräusche): csound-local techniques (not in the shared
    # lexicon TECHS), each must be bounded, STAND, and be genuinely aperiodic NOISE
    # (pitchedness < 0.4 -- the noise-class gate check, which a pitched tone fails).
    for t in sorted(co._NOISE_TECH):
        cases.append((f"noise:{t}", [t], [], None,
                      {"sustain": "stand", "move": False, "noise": True}, False))
    # M4a noise-in-MORPH: a noise texture inside a morph chain MUST survive as real
    # noise (the additive-partial morph would silently degrade it to a pitched tone
    # -- the migration-discipline capability loss the corpus caught 2026-07-18; the
    # noise-crossfade morph path fixes it). Assert each stays aperiodic / decays.
    NOISE_MORPHS = [
        ("wind>rain",      ["wind", "rain"],       {"sustain": "stand", "move": True, "noise": True}),
        ("crackle>hiss",   ["crackle", "hiss"],    {"sustain": "stand", "move": True, "noise": True}),
        ("noise>surf",     ["noise", "surf"],      {"sustain": "stand", "move": True, "noise": True}),
        ("rain>silence",   ["rain", "silence"],    {"sustain": "transient"}),   # real rain fades (not a tone)
        ("wind>sine",      ["wind", "sine"],       {"sustain": "stand", "move": True}),  # noise resolving to a tone
    ]
    for lbl, chain, exp in NOISE_MORPHS:
        cases.append((f"nzmorph:{lbl}", chain, [], None, exp, False))

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
