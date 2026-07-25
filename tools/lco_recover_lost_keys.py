#!/usr/bin/env python3
"""Recover the instruments the harvest left behind.

`backend/lco_library.json` was machine-harvested from the parked implementation's
own emitters (tag `parked/keys-path-csound-20260721`), and `tools/lco_build_library.py`
records that the library therefore "inherited every idiom the old path could
actually produce rather than a smaller vocabulary wearing its name".

That is not true, and the mechanism is one line: the harvest looped over
`backend/dco_lexicon.json`'s `techniques`. The parked `csound_orch.py` carried
FIFTEEN more instruments of its own, in `_CS_TECH_EXTRA`, each with a `why` and
its own surface forms — reachable from a prompt in the old path, absent from the
library today:

    glass  noise  pink_noise  wind  rain  surf  thunder  hiss  crackle
    voice  voice_ee  voice_oo  rhodes  wurlitzer  vibraphone

Six of them are the nature beds `LCO_CONCEPT.md` §6 calls "the route for the
natural-sound and animal part of the library", three are the vowel/formant family,
and three are the struck instruments §5 records as "Instruments 4-6 — BUILT". The
concept document discusses all fifteen as though they were in the library.

This is the Migration & Substrate Discipline case in `CLAUDE.md`, whose trigger is
deliberately objective rather than a self-diagnosis: a module the prompt path
reached was replaced by one that reaches less. So this is a recovery, not a
rewrite — the Csound comes out of the emitter that produced it, whose constants
were measured and ear-approved, and nothing here invents an idiom.

**The extraction proves itself on a known answer before it is believed.** The same
`body_of` is run over the 30 committed instruments first; 29 must come back
byte-identical (the 30th, `string`, was hand-authored after the harvest and is
expected to differ). A negative or positive result about a component needs proof
of contact — §7.9 is on this project's record because a rejection was once made
through a call the component never accepted.

    .venv/bin/python tools/lco_recover_lost_keys.py --list
    .venv/bin/python tools/lco_recover_lost_keys.py --out recovered.json
"""
import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
PARKED_TAG = "parked/keys-path-csound-20260721"

# Verbatim from the harvest that produced the committed library (the version at
# 73f91857^). Kept identical rather than reimplemented, and asserted against the
# library below: a recovered entry has to be the same KIND of thing as the 29 that
# came through, not a lookalike.
_SCAFFOLD = re.compile(
    r"^\s*(kfr\d+\s+limit|kvsum\s*=|kmix\s*=|asig\s*=\s*kvol|"
    r"aout\b|outch\b)", re.IGNORECASE)
_BODY = re.compile(r"knote\s+= knote \+ 1/kr\n(.*?)\n  aout", re.DOTALL)
_LAYER_FREQ = re.compile(r"\bkfr([123])\b")
# The mix scaffold reads `asig = kvol*aosc0`, so a stripped body ended in `aosc0`
# and contradicted the one rule the author must follow — write the final audio
# into `asig`. Only the output is renamed; internal tagged names keep their tag.
_OSC_OUT = re.compile(r"\baosc0\b")

# `string` is the single instrument added to the lexicon AFTER the harvest, by
# hand. It is expected to differ, and naming it here is what keeps the self-check
# honest: an unexplained mismatch must fail, not be absorbed by a threshold.
HAND_AUTHORED = {"string"}


def load_parked():
    src = subprocess.run(["git", "show", f"{PARKED_TAG}:backend/csound_orch.py"],
                         cwd=REPO, capture_output=True, text=True, check=True).stdout
    tmp = Path(tempfile.mkdtemp(prefix="lco_parked_"))
    (tmp / "parked_csound_orch.py").write_text(src)
    sys.path.insert(0, str(tmp))
    sys.path.insert(0, str(REPO / "backend"))
    import parked_csound_orch  # noqa: E402
    return parked_csound_orch


def body_of(orchestra):
    m = _BODY.search(orchestra)
    if not m:
        return ""
    kept = [ln.rstrip() for ln in m.group(1).splitlines()
            if ln.strip() and not _SCAFFOLD.match(ln)]
    if kept:
        pad = min(len(l) - len(l.lstrip()) for l in kept)
        kept = [l[pad:] for l in kept]
    body = _LAYER_FREQ.sub(r"(kfreq * koct\1)", "\n".join(kept))
    return _OSC_OUT.sub("asig", body)


def emit(P, key):
    orc, reading = P.build_orchestra(technique_keys=[key])
    return body_of(orc), reading


def selfcheck(P, lib):
    """The extraction must reproduce what is already committed."""
    same, differ = [], []
    for e in lib["instruments"]:
        got, _ = emit(P, e["key"])
        (same if got == e["code"] else differ).append(e["key"])
    unexplained = set(differ) - HAND_AUTHORED
    print(f"self-check: {len(same)}/{len(lib['instruments'])} committed "
          f"instruments reproduced byte-for-byte")
    for k in sorted(differ):
        print(f"  differs: {k}" + ("  (hand-authored after the harvest, expected)"
                                   if k in HAND_AUTHORED else "  <-- UNEXPLAINED"))
    if unexplained:
        raise SystemExit(
            f"the extraction does not reproduce {sorted(unexplained)}, so it is not "
            f"the path that produced the library. Do not recover anything through "
            f"it until that is understood — a body that merely looks right is how "
            f"a measured, ear-approved idiom gets quietly replaced by a guess.")
    return same


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", help="write the recovered entries here as JSON")
    ap.add_argument("--list", action="store_true",
                    help="only report which keys the harvest missed")
    args = ap.parse_args()

    P = load_parked()
    lib = json.loads((REPO / "backend" / "lco_library.json").read_text())
    have = {e["key"] for e in lib["instruments"]}
    extra = getattr(P, "_CS_TECH_EXTRA", {})
    missing = [k for k in extra if k not in have]

    print(f"{PARKED_TAG} carried {len(extra)} instruments outside the lexicon; "
          f"{len(missing)} are still missing from the library:")
    print("  " + "  ".join(missing))
    if args.list:
        return 0
    print()
    selfcheck(P, lib)

    out = []
    for key in missing:
        spec = extra[key]
        code, reading = emit(P, key)
        if not code:
            raise SystemExit(f"{key}: the emitter produced no body. Find out why "
                             f"before writing one — do not guess an idiom.")
        out.append({"key": key,
                    "surface_forms": list(spec.get("surface_forms") or []),
                    "why": spec.get("why", ""),
                    "code": code,
                    "reading": reading,
                    "recovered_from": PARKED_TAG})
        print(f"\n{'=' * 70}\n{key}  ({len(code.splitlines())} lines)  "
              f"forms={spec.get('surface_forms')}\n{spec.get('why')}\n{code}")

    if args.out:
        Path(args.out).write_text(json.dumps(out, indent=1, ensure_ascii=False))
        print(f"\nwrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
