#!/usr/bin/env python3
"""How much does each shipped instrument depend on how long the plugin has been open?

`lco_axis_probe.phase_spread` answers that for ONE body. This asks it of the whole
library, because the answer is only interesting as a distribution: a body at the
median is a body whose note-to-note variation is part of how it sounds, and a body
in the tail is one where the same note is a different sound depending on uptime.

Why it exists as its own file rather than as a number in a document: the figures
that stood in `docs/plans/HANDOVER_LCO.md` for this class were carried over from an
agent's run on a phase grid that spanned 2.7 s -- less than one period of the five
slowest generators in the library -- and an independent pass reproduced none of
them. A number nobody can re-derive is not a measurement. Run this instead of
quoting it.

**A reading of 0.0 Hz does NOT mean the body is independent of uptime.**
`lco_measure.measure` rounds the spectral centroid to whole Hz, so 0.0 means "the
spread is below the meter's own resolution". It is also one scalar: two plugin ages
can share a centroid and still not sound the same. This is a lower bound on an
audible consequence, in both directions.

    .venv/bin/python tools/lco_phase_census.py
    .venv/bin/python tools/lco_phase_census.py --freq 110 --json out.json
"""
import argparse
import json
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

import lco_axis_probe as P  # noqa: E402
import lco_measure as M  # noqa: E402


def census(freq=220.0):
    lex = json.loads((REPO / "backend" / "dco_lexicon.json").read_text())
    rows, skipped, failed = [], [], []
    t0 = time.time()
    for e in lex["techniques"]:
        key, body = e["key"], e["code"]
        free = P.free_running(body)
        if not free:
            skipped.append(key)
            continue
        got = P.phase_spread(body, freq)
        if got is None:
            failed.append(key)
            print(f"  {key:18s} FAILED to render at one or more ages", flush=True)
            continue
        spread, tracks = got
        firsts = [t[0] for t in tracks if t]
        rows.append({"key": key, "spread_hz": round(spread, 1),
                     "firsts_hz": [round(f, 1) for f in firsts],
                     "free_lines": len(free), "first_line": free[0].strip()})
        print(f"  {key:18s} {spread:8.1f} Hz   "
              f"{' '.join(f'{f:7.1f}' for f in firsts)}", flush=True)
    rows.sort(key=lambda r: -r["spread_hz"])
    return {"freq": freq, "phases_s": [round(p, 3) for p in P._PHASES],
            "slowest_s": P._SLOWEST_S, "n_entries": len(lex["techniques"]),
            "n_free_running": len(rows) + len(failed),
            "not_free_running": skipped, "render_failed": failed,
            "rows": rows, "seconds": round(time.time() - t0, 1)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--freq", type=float, default=220.0)
    ap.add_argument("--json", help="also write the full per-body result here")
    a = ap.parse_args()
    out = census(a.freq)
    rows = out["rows"]
    print(f"\n{len(rows)} of {out['n_entries']} bodies hold state the note-on does not "
          f"reset, measured at {out['freq']:.0f} Hz over four ages spanning "
          f"{out['slowest_s']:.0f} s ({out['seconds']} s)")
    print("  not uptime-dependent at all: " + ", ".join(out["not_free_running"]))
    if out["render_failed"]:
        print("  FAILED to render: " + ", ".join(out["render_failed"]))
    if rows:
        med = rows[len(rows) // 2]
        print(f"  worst {rows[0]['key']} {rows[0]['spread_hz']} Hz, "
              f"median {med['spread_hz']} Hz, best {rows[-1]['key']} "
              f"{rows[-1]['spread_hz']} Hz")
        print("  0.0 Hz is the meter's resolution floor, not independence: "
              + (", ".join(r["key"] for r in rows if r["spread_hz"] == 0.0) or "none"))
    if a.json:
        Path(a.json).write_text(json.dumps(out, indent=1) + "\n")
        print("  ->", a.json)
    return 0


if __name__ == "__main__":
    sys.exit(main())
