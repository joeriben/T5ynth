#!/usr/bin/env python3
"""A/B `analog_osc`'s `drive` against the instrument the change replaces.

The comparison rule (CLAUDE.md, Instrument Authoring 3+4): the nearest existing
thing is rendered and on disk BEFORE a line of the new body is written, and the
listener's anchor is that render, never the author's opinion.

Here the incumbent is the entry itself, as it ships. What is being tested is one
parameter of it, so the anchor is that same parameter as it stands today, at the
same four positions, at the same pitches, through the same filter.

Two things this must NOT do, because they are the subject:

  * level-match the two sides. The old drive holds the level flat and that is the
    complaint. The reference recording (a Minimoog V External Input Volume sweep,
    BJ 2026-08-01) rises 16.00 dB from clean to driven, its crest falls 2.31 to
    1.65 and its spectral centroid falls 3250 Hz to ~600. So the numbers are
    printed and nothing is normalised.
  * render the oscillator alone. The saturating element on the reference is the
    LADDER's input stage, not the oscillator, so a Csound-only render would show
    a gain and no character at all. Every file here goes through the project's
    own LadderFilter, via tools/audition_lro_drive.

    .venv/bin/python tools/lco_ab_analog_drive.py --side nearest_existing
    .venv/bin/python tools/lco_ab_analog_drive.py --side new

Rendered at lco_measure's 44100 while the engine runs at 176400. That is a real
difference and it is harmless HERE and only here: after the change the body's
drive is a gain, which is rate-independent, and the nonlinearity that matters is
in the C++ ladder, which this runs at the file's own rate either way.
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
import lco_measure as M  # noqa: E402

# Four positions of the knob, and the pitches a bass overdrive is judged at.
DRIVES = [0.0, 0.33, 0.67, 1.0]
NOTES = [("A1", 55.0), ("A2", 110.0), ("A3", 220.0)]

# The filter the reference sound is made through: a ladder well down from wide
# open, moderate resonance. The MiniV demo's centroid sits at 600-1000 Hz once
# driven, which is a filter doing the work, not an open one.
LADDER = dict(cutoff=1200.0, reso=0.35, slope=3, type=0, mix=1.0, drive_db=0.0)

TOOL = REPO / "tools" / "audition_lro_drive"


def entry_body() -> tuple[str, dict]:
    lib = json.loads((REPO / "backend" / "lco_library.json").read_text())
    e = next(x for x in lib["instruments"] if x["key"] == "analog_osc")
    return e["code"], e["params"]


def set_params(body: str, vals: dict) -> str:
    """Rewrite the declared value of each named parameter line, nothing else."""
    import re
    out = []
    for line in body.split("\n"):
        m = re.match(r"^(\s*k\w+\s*=\s*)([-\d.]+)(\s*;\s*(\w+)\s*\[)", line)
        if m and m.group(4) in vals:
            out.append(f"{m.group(1)}{vals[m.group(4)]:.4f}{line[m.end(2):]}")
        else:
            out.append(line)
    return "\n".join(out)


def stats(y: np.ndarray, sr: int) -> tuple[float, float, float]:
    rms = float(np.sqrt(np.mean(y ** 2)))
    crest = float(np.max(np.abs(y)) / max(rms, 1e-12))
    w = y * np.hanning(len(y))
    S = np.abs(np.fft.rfft(w))
    f = np.fft.rfftfreq(len(y), 1 / sr)
    cen = float((S * f).sum() / max(S.sum(), 1e-12))
    return 20 * np.log10(max(rms, 1e-12)), crest, cen


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--side", choices=("nearest_existing", "new"), required=True,
                    help="'nearest_existing' is the entry as it ships; run it FIRST")
    ap.add_argument("--dur", type=float, default=3.0)
    ap.add_argument("--out", default=str(REPO / "tools" / "lco_ab_analog_drive"))
    a = ap.parse_args()

    outdir = Path(a.out)
    outdir.mkdir(parents=True, exist_ok=True)
    body, params = entry_body()
    if "drive" not in params:
        print("analog_osc has no `drive` parameter", file=sys.stderr)
        return 1

    print(f"{'note':>4} {'drive':>5} | {'osc dB':>7} {'crest':>5} | "
          f"{'ladder dB':>9} {'crest':>5} {'centroid':>8}")
    rows = []
    for note, freq in NOTES:
        for d in DRIVES:
            vals = {n: params[n]["default"] for n in params}
            vals["drive"] = params["drive"]["range"][0] + d * (
                params["drive"]["range"][1] - params["drive"]["range"][0])
            y, err = M.render(set_params(body, vals), dur=a.dur, freq=freq, preroll=0.25)
            if y is None:
                print(f"RENDER FAILED at {note} drive={d}: {err}", file=sys.stderr)
                return 1
            raw = outdir / f"_osc_{a.side}__{note}_d{int(d*100):03d}.wav"
            sf.write(raw, y.astype(np.float32), M.SR, subtype="FLOAT")
            odb, ocr, _ = stats(y, M.SR)

            wav = outdir / f"{a.side}__{note}_d{int(d*100):03d}.wav"
            cmd = [str(TOOL), str(raw), str(wav)] + [f"{k}={v}" for k, v in LADDER.items()]
            r = subprocess.run(cmd, capture_output=True, text=True, cwd=REPO)
            if r.returncode != 0:
                print(r.stdout + r.stderr, file=sys.stderr)
                return 1
            z, sr = sf.read(wav)
            ldb, lcr, lcen = stats(z, sr)
            print(f"{note:>4} {d:5.2f} | {odb:7.2f} {ocr:5.2f} | "
                  f"{ldb:9.2f} {lcr:5.2f} {lcen:8.0f}")
            rows.append(dict(note=note, drive=d, osc_db=odb, osc_crest=ocr,
                             lad_db=ldb, lad_crest=lcr, lad_centroid=lcen))
            raw.unlink()

    (outdir / f"{a.side}.json").write_text(json.dumps(rows, indent=1))
    for note, _ in NOTES:
        r = [x for x in rows if x["note"] == note]
        print(f"{note}: level {r[-1]['lad_db'] - r[0]['lad_db']:+6.2f} dB, "
              f"crest {r[0]['lad_crest']:.2f} -> {r[-1]['lad_crest']:.2f}, "
              f"centroid {r[0]['lad_centroid']:.0f} -> {r[-1]['lad_centroid']:.0f} Hz"
              "     (reference: +16.00 dB, 2.31 -> 1.65, 3253 -> 600)")
    print(f"\n{outdir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
