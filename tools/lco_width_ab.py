#!/usr/bin/env python3
"""A/B the `analog_osc` width rewrite: the entry as committed against the working tree.

Renders the same six sounds from BOTH versions of `backend/dco_lexicon.json` -- the
one at a given git revision and the one on disk -- into one folder, so the change can
be heard rather than read.

Each PAIR is scaled by a single common factor, so a level difference INSIDE a pair is
preserved (a narrow pulse is quieter than a square, and that is the sound of the thing)
while different pairs stay comparable to each other.

    .venv/bin/python tools/lco_width_ab.py --out tools/lco_listening/analog_osc_width_ab
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

import lco_measure as M          # noqa: E402
import lco_width_parity as P     # noqa: E402

FREQ = 220.0
DUR = 4.0
PREROLL = 0.5
PEAK = 0.89


def entry_at(rev):
    blob = subprocess.run(["git", "show", f"{rev}:backend/dco_lexicon.json"],
                          cwd=REPO, capture_output=True, text=True, check=True).stdout
    for t in json.loads(blob)["techniques"]:
        if t["key"] == "analog_osc":
            return t
    raise SystemExit("no analog_osc at that revision")


def render(body):
    y, err = M.render(body, dur=DUR, freq=FREQ, preroll=PREROLL)
    if err:
        raise SystemExit(err)
    return np.asarray(y)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--rev", default="HEAD")
    a = ap.parse_args()

    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)

    before = dict(P.cases(entry_at(a.rev)))
    after = dict(P.cases(P.entry()))

    print(f"{'':<14} {'before':>22}   {'after':>22}")
    for name in before:
        yb, ya = render(before[name]), render(after[name])
        g = PEAK / max(np.abs(yb).max(), np.abs(ya).max(), 1e-9)
        sf.write(out / f"{name}__before.wav", yb * g, M.SR, subtype="FLOAT")
        sf.write(out / f"{name}__after.wav", ya * g, M.SR, subtype="FLOAT")
        print(f"{name:<14} {M.centroid(yb):>9.1f} Hz {M.rms_db(yb):>7.2f} dB   "
              f"{M.centroid(ya):>9.1f} Hz {M.rms_db(ya):>7.2f} dB")
    print(f"\n{out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
