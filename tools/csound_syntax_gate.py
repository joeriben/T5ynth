#!/usr/bin/env python3
"""Compile EVERY routable key's orchestra with Csound's own parser, and flag any
key that still renders as a hand-typed partial list.

This is NOT an audio test — no rendering, no listening, no claim about how
anything sounds. Sound is judged only in the built Standalone. This gate answers
the two questions a compiler can answer and a person cannot answer reliably:

  1. Does the orchestra Csound will be handed actually parse? A typo in an
     emitted opcode line makes the engine fall back silently, and the first
     symptom is a key that plays nothing.
  2. Does the emitted code USE the substrate? CLAUDE.md's Migration & Substrate
     Discipline requires this grep by name: Csound was chosen because it does
     vco2/kpw/foscil/mode/gbuzz natively, so a key whose body is four or more
     plain oscillators at fixed ratios is the forbidden additive bank, however
     green everything else is.
  3. Does every routable key still have an idiom of its own? `_emit_steady`'s
     `else` used to degrade an unknown key to a static partial reading; now
     it emits a bare tone, because that spectrum reading WAS the additive bank.
     That turns "someone adds a key to _CS_TECH_EXTRA and forgets the branch"
     from a visible wrong spectrum into a silent plain sine. Nothing reaches the
     fallthrough today; this asserts it stays that way.

Run: .venv/bin/python tools/csound_syntax_gate.py
"""
import re
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "backend"))
import csound_orch as C  # noqa: E402

CSOUND = "/opt/homebrew/bin/csound"
# A bank is >=4 bare A-RATE oscillators summed at fixed ratios. Three is a rank
# pair plus an octave; four typed-out partials is a table someone wrote by hand.
BANK_AT = 4
PLAIN_OSC = {"oscil", "oscili", "poscil", "poscil3", "oscil3"}
OPLINE = re.compile(r"^\s*(\S+)\s+([a-zA-Z][a-zA-Z0-9_]*)\s")


def routable_keys():
    keys = set(C._TONAL_KEYS) | set(C._CS_TECH_EXTRA)
    keys |= set(C._VOICE_TECH) | set(C._NOISE_TECH) | set(C._MODAL_TECH)
    try:
        import json
        lex = json.load(open(Path(__file__).resolve().parent.parent
                             / "backend" / "dco_lexicon.json"))
        for sect in ("techniques", "technique", "keys"):
            if isinstance(lex.get(sect), dict):
                keys |= set(lex[sect])
    except Exception:
        pass
    # morph-spectrum VALUES are endpoint names, not routable techniques
    keys -= {"zero", "silence"}
    return sorted(keys)


def opcodes_of(body):
    """[(output_var, opcode)] for each opcode line."""
    ops = []
    for ln in body.splitlines():
        ln = ln.split(";")[0]
        m = OPLINE.match(ln)
        if m and m.group(2) != "=":
            ops.append((m.group(1), m.group(2)))
    return ops


FALLTHROUGH = "unrouted key: bare tone"


def main():
    bad_syntax, banks, unrouted = [], [], []
    keys = routable_keys()
    for k in keys:
        orc, _ = C.build_orchestra(technique_keys=[k])
        orc = orc.replace("%SR%", "48000")
        with tempfile.NamedTemporaryFile("w", suffix=".orc", delete=False) as f:
            f.write(orc)
            path = f.name
        r = subprocess.run([CSOUND, "--syntax-check-only", path],
                           capture_output=True, text=True)
        Path(path).unlink()
        if r.returncode != 0:
            err = [l for l in (r.stderr or "").splitlines()
                   if "error" in l.lower()][:3]
            bad_syntax.append((k, err))

        body = C._emit_steady(k, "0")
        if FALLTHROUGH in body:
            unrouted.append(k)
        ops = opcodes_of(body)
        # Only A-RATE oscillators count. A k-rate `poscil` is a control LFO -- a
        # drift, a breath, a duty wander -- and counting those called ring_mod an
        # additive bank on the strength of its two drift LFOs.
        nplain = sum(1 for out, op in ops
                     if op in PLAIN_OSC and out.startswith("a"))
        if nplain >= BANK_AT:
            banks.append((k, nplain))
        print(f"{k:16s} {'OK ' if r.returncode == 0 else 'FAIL'} "
              f"{' '.join(dict.fromkeys(op for _, op in ops))}")

    print(f"\n{len(keys)} keys checked")
    for k, err in bad_syntax:
        print(f"  SYNTAX FAIL {k}: {err}")
    for k, n in banks:
        print(f"  ADDITIVE BANK {k}: {n} plain oscillators")
    for k in unrouted:
        print(f"  NO IDIOM {k}: falls through to a bare tone -- give it a branch "
              f"in _emit_steady")
    if bad_syntax or banks or unrouted:
        return 1
    print("  all parse; none is an additive bank; every key has its own idiom")
    return 0


if __name__ == "__main__":
    sys.exit(main())
