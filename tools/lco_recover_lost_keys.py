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
`body_of` is run over the library AS IT WAS AT THE HARVEST (`HARVEST_LIB` below),
and all 29 of those instruments must come back byte-identical. A negative or
positive result about a component needs proof of contact — §7.9 is on this
project's record because a rejection was once made through a call the component
never accepted.

That self-check used to read TODAY's library instead, and by 2026-07-25 it accused
41 of 63 keys of proving the extraction wrong, then refused to run. Every one of
those accusations was false, for two reasons that have nothing to do with the
extraction: 22 keys were authored after the harvest (the reed, nature and animal
families) and the parked emitter never knew them, and 19 more have been rewritten
since — parametrised, level-corrected, movement-fixed — which is the work of the
last four days, not a defect. **A known-answer test has to be pinned to the known
answer.** A moving target turns a self-check into a wolf-crier, and this project's
own rule is that the tool nobody reads is the same loss as the tool that certifies
a broken state.

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

# The known answer: the library revision this extraction must reproduce, and which
# it does reproduce 29/29 today. It is NOT the first revision of the file. The
# harvest itself (`b2d64809`) and its successor still ended each body in `aosc0`;
# `4cb92441` is the commit that renamed the output to `asig`, which is what
# `_OSC_OUT` above applies, so it is the first revision written in the format this
# code emits. Measured over the first fourteen revisions: b2d64809 0/29,
# 436d29e0 0/29, then 29/29 from 4cb92441 onwards until the bodies themselves start
# being rewritten. Pinning to the harvest commit "because that is the harvest" would
# make the self-check fail for ever on a formatting change it applies itself.
HARVEST_LIB = "4cb92441"
HARVEST_N = 29

# The parked emitter's own answer for a key it does not route: one line, tagged. It
# is how `drift_report` tells "authored after the harvest" from "rewritten since"
# without a second table that could disagree with the emitter.
UNROUTED = "unrouted key: bare tone"


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


def selfcheck(P):
    """The extraction must reproduce the library as it stood AT the harvest.

    Fatal on any mismatch, and on the count too: a self-check whose known answer has
    silently shrunk is not a self-check.
    """
    src = subprocess.run(["git", "show", f"{HARVEST_LIB}:backend/lco_library.json"],
                         cwd=REPO, capture_output=True, text=True, check=True).stdout
    lib = json.loads(src)
    same, differ = [], []
    for e in lib["instruments"]:
        got, _ = emit(P, e["key"])
        (same if got == e["code"] else differ).append(e["key"])
    print(f"self-check against {HARVEST_LIB} (the library at the harvest): "
          f"{len(same)}/{len(lib['instruments'])} reproduced byte-for-byte")
    if differ or len(lib["instruments"]) != HARVEST_N:
        for k in sorted(differ):
            print(f"  differs: {k}")
        what = (f"does not reproduce {sorted(differ)}" if differ else
                f"was pinned to {HARVEST_N} instruments and found "
                f"{len(lib['instruments'])}")
        raise SystemExit(
            f"the extraction {what} at {HARVEST_LIB}, so it is not the path that "
            f"produced the library. Do not recover anything through it until that is "
            f"understood — a body that merely looks right is how a measured, "
            f"ear-approved idiom gets quietly replaced by a guess.")
    return same


def drift_report(P, lib):
    """How far today's library has moved from what the parked emitter produces.

    Reported, never fatal. Of today's keys the emitter knows, the ones that still
    match have not been touched since the harvest and the ones that differ have been
    rewritten since — parametrised, level-corrected, movement-fixed. That is the work
    of the days after the harvest, and calling it a defect is what the old self-check
    did to 41 keys.
    """
    untouched, rewritten, unknown = [], [], []
    for e in lib["instruments"]:
        got, _ = emit(P, e["key"])
        # The emitter says so itself rather than being asked through a table: an
        # unrouted key comes back as its one-line fallback. Proof of contact.
        if not got or UNROUTED in got:
            unknown.append(e["key"])
        else:
            (untouched if got == e["code"] else rewritten).append(e["key"])
    print(f"\ntoday's library: {len(lib['instruments'])} instruments — "
          f"{len(untouched)} still byte-identical to the parked emitter, "
          f"{len(rewritten)} rewritten since the harvest, "
          f"{len(unknown)} authored after it")
    print("  rewritten since: " + (", ".join(sorted(rewritten)) or "none"))
    print("  authored after:  " + (", ".join(sorted(unknown)) or "none"))
    return untouched, rewritten, unknown


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
    print("  " + ("  ".join(missing) or "none — all fifteen were recovered"))
    if args.list:
        return 0
    print()
    selfcheck(P)
    drift_report(P, lib)
    if not missing:
        return 0

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
