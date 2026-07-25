#!/usr/bin/env python3
"""Does every ANCHOR the author is handed still move, at every register?

The movement gate asks its question of the DEFAULT body at 220 Hz. But what the
author is actually handed is `anchor_code` -- 355 whole bodies, one per named anchor
-- and a named anchor is a recommendation. An anchor that stands still at 55 Hz is a
recommendation to break a platform fundamental ("movement by default",
`docs/LCO_CONCEPT.md` §4), and until this file existed nothing looked.

It reads the shipped `anchor_code` where there is one, because that string is what
the author gets; where there is none it falls back to `lco_axis_probe.with_axis` on
the declared value, and it says which of the two it used per row.

Two things it deliberately does NOT do:

  * It does not fail the event-texture class. A body that declares
    `; MOVEMENT: TEXTURE` is exempt by BJ's ruling of 2026-07-25 and its rows are
    reported separately -- an exemption list that swallows readings silently is the
    failure mode this project already has on its record.
  * It does not distinguish "stands still" from "moves without a rate". Both fail
    `lco_measure.moves`, which needs a 60-cent span AND a coherence above 0.35, and
    the two are different defects: a plain waveform barely moves at all, while a
    noise-excited resonator bank moves hundreds of Hz incoherently. The printed row
    carries the span and the coherence so the reader can tell them apart, and
    `--classify` splits the totals that way.

    .venv/bin/python tools/lco_anchor_census.py
    .venv/bin/python tools/lco_anchor_census.py --key drum_head --classify
"""
import argparse
import collections
import json
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

import lco_axis_probe as P  # noqa: E402
import lco_measure as M  # noqa: E402

REGISTERS = (55.0, 110.0, 220.0, 440.0, 880.0, 1760.0)
# The span at which a failure stops being "it barely moves" and becomes "it moves a
# lot, incoherently". Not a threshold the verdict uses -- `moves()` owns that -- only
# a way to read 250 failures without opening each one.
LARGE_SPAN_HZ = 200.0


def census(keys=None, registers=REGISTERS):
    lex = json.loads((REPO / "backend" / "dco_lexicon.json").read_text())
    rows, texture_rows, unreachable = [], [], []
    t0, n = time.time(), 0
    for e in lex["techniques"]:
        key = e["key"]
        if keys and key not in keys:
            continue
        params = e.get("params") or {}
        if not params:
            continue
        codes = e.get("anchor_code") or {}
        texture = P.declares_texture(e["code"])
        for axis, spec in params.items():
            for aname, anchor in (spec.get("anchors") or {}).items():
                ck = f"{axis}={aname}"
                if ck in codes:
                    body, src = codes[ck], "anchor_code"
                else:
                    body, src = P.with_axis(e["code"], axis, anchor["value"]), "with_axis"
                    if body == e["code"]:
                        unreachable.append({"key": key, "anchor": ck})
                        continue
                bad = []
                for f in registers:
                    y, err = M.render(body, dur=4.0, freq=f)
                    n += 1
                    if y is None:
                        bad.append([f, f"render: {err}"])
                        continue
                    r = M.measure(y, f)
                    if not r["moves"]:
                        bad.append([f, f"{r['centroid_motion_hz']} Hz @ "
                                       f"{r['motion_coherence']}"])
                if bad:
                    row = {"key": key, "anchor": ck, "source": src,
                           "value": anchor["value"], "gloss": anchor.get("gloss", ""),
                           "n_bad": len(bad), "bad": bad}
                    (texture_rows if texture else rows).append(row)
                    if not texture:
                        print(f"  {key:16s} {ck:28s} {src:11s} {len(bad)}/"
                              f"{len(registers)}  "
                              + "  ".join(f"{f:.0f}:{d}" for f, d in bad), flush=True)
    rows.sort(key=lambda r: (-r["n_bad"], r["key"]))
    return {"registers": list(registers), "renders": n,
            "seconds": round(time.time() - t0, 1), "rows": rows,
            "texture_rows": texture_rows, "unreachable": unreachable}


def classify(rows):
    """Split the failures into the two different defects they actually are."""
    total, per_key = collections.Counter(), collections.defaultdict(collections.Counter)
    for r in rows:
        for f, msg in r["bad"]:
            if msg.startswith("render"):
                kind = "render error"
            else:
                span = float(msg.split(" Hz @ ")[0])
                kind = ("moves a lot, incoherently" if span >= LARGE_SPAN_HZ
                        else "barely moves at all" if span < LARGE_SPAN_HZ / 4
                        else "borderline")
            total[kind] += 1
            per_key[r["key"]][kind] += 1
    return total, per_key


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--key", action="append", help="only these entries (repeatable)")
    ap.add_argument("--registers", help="comma-separated, default 55,110,220,440,880,1760")
    ap.add_argument("--classify", action="store_true",
                    help="split the failures by span into the two defects")
    ap.add_argument("--json", help="write the full result here")
    a = ap.parse_args()
    regs = (tuple(float(x) for x in a.registers.split(",")) if a.registers
            else REGISTERS)
    out = census(a.key, regs)
    rows = out["rows"]
    keys = sorted({r["key"] for r in rows})
    print(f"\n{out['renders']} renders in {out['seconds']} s")
    print(f"{len(rows)} anchors stand still at one or more registers, across "
          f"{len(keys)} entries: {', '.join(keys) or 'none'}")
    print(f"{len(out['texture_rows'])} more do on event-texture bodies "
          f"({', '.join(sorted({r['key'] for r in out['texture_rows']})) or 'none'}) "
          f"-- exempt, not failing")
    if out["unreachable"]:
        print(f"{len(out['unreachable'])} anchors are unreachable (no anchor_code and "
              "no settable axis line): "
              + ", ".join(f"{u['key']}/{u['anchor']}" for u in out["unreachable"]))
    if a.classify:
        total, per_key = classify(rows)
        print()
        for k, v in total.most_common():
            print(f"  {v:4d}  {k}")
        print()
        for key, c in sorted(per_key.items(), key=lambda kv: -sum(kv[1].values())):
            print(f"  {key:16s} " + "  ".join(f"{k}={v}" for k, v in c.most_common()))
    if a.json:
        Path(a.json).write_text(json.dumps(out, indent=1) + "\n")
        print("\n->", a.json)
    return 0


if __name__ == "__main__":
    sys.exit(main())
