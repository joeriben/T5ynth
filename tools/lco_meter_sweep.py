#!/usr/bin/env python3
"""Re-audit every shipped axis with the meter that matches its body.

Two shipped compensations (vibraphone `strikepos`, rhodes `bark`) were fitted against
`lco_measure.rms_db`'s hard-coded 0.5-3.5 s window and were WORSE than no correction on
the audible note. Both were found by accident. This asks the same question of all of
them at once: for every axis of every entry, what does the audible meter say where
`rms_db` said the axis was fine?

The meter is chosen per BODY, measured not guessed (docs/plans/HANDOVER_LCO.md §5):
render 6 s at the defaults and compare the first 200 ms to 4.0-5.5 s. A drop past 12 dB
is a struck body -> judge the STRIKE (first 400 ms). Anything less is sustained -> judge
the WHOLE NOTE.

Writes JSON; prints the entries where the audible meter is over bound.
"""
import json
import sys
import time
from pathlib import Path

import numpy as np

REPO = Path("/Users/joerissen/ai/t5ynth")
sys.path.insert(0, str(REPO / "tools"))
import lco_axis_probe as P   # noqa: E402
import lco_measure as M      # noqa: E402

REGISTERS = (55.0, 110.0, 220.0, 440.0, 880.0, 1760.0)
OUT = Path(sys.argv[1] if len(sys.argv) > 1 else "meter_sweep.json")


def db(s):
    return float(20 * np.log10(max(np.sqrt((np.asarray(s) ** 2).mean()), 1e-12)))


def classify(code):
    y, _ = M.render(code, dur=6.0, freq=220.0, preroll=2.0)
    if y is None:
        return "unknown", None
    drop = db(y[:int(0.20 * M.SR)]) - db(y[int(4.0 * M.SR):int(5.5 * M.SR)])
    return ("struck" if drop > 12.0 else "sustained"), round(drop, 2)


def main():
    lex = json.loads((REPO / "backend" / "dco_lexicon.json").read_text())
    entries = [t for t in lex["techniques"] if t.get("params")]
    print(f"{len(entries)} entries with axes, lexicon_version {lex['lexicon_version']}",
          flush=True)
    res, t0 = {}, time.time()
    for i, t in enumerate(entries):
        key, code = t["key"], t["code"]
        kind, drop = classify(code)
        rec = {"class": kind, "decay_db": drop, "axes": {}}
        for axis, spec in t["params"].items():
            anchors = spec.get("anchors") or {}
            if len(anchors) < 2:
                continue
            rows = {}
            for f in REGISTERS:
                lv = {"rms": [], "whole": [], "attack": []}
                for a in anchors.values():
                    b = P.with_axis(code, axis, a["value"])
                    y, _ = M.render(b, dur=3.0, freq=f, preroll=2.0)
                    if y is None:
                        continue
                    lv["rms"].append(M.measure(y, f)["rms_db"])
                    lv["whole"].append(db(y))
                    lv["attack"].append(db(y[:int(0.40 * M.SR)]))
                rows[f] = {k: (round(max(v) - min(v), 3) if len(v) > 1 else None)
                           for k, v in lv.items()}
            rec["axes"][axis] = rows
        res[key] = rec
        print(f"  [{i+1}/{len(entries)}] {key} ({kind}, {drop} dB) "
              f"{time.time()-t0:.0f}s", flush=True)
        OUT.write_text(json.dumps(res, indent=1))

    print("\n=== where the AUDIBLE meter is over bound and rms_db was not ===", flush=True)
    hits = []
    for key, rec in res.items():
        meter = "attack" if rec["class"] == "struck" else "whole"
        for axis, rows in rec["axes"].items():
            for f, v in rows.items():
                bound = 0.5 if float(f) == 220.0 else 1.0
                aud, rms = v.get(meter), v.get("rms")
                if aud is None or rms is None:
                    continue
                if aud > bound and rms <= bound:
                    hits.append((aud - rms, key, axis, float(f), aud, rms, meter))
    hits.sort(reverse=True)
    for d, key, axis, f, aud, rms, meter in hits:
        print(f"  {key:>16}.{axis:<12} {f:6.0f} Hz   {meter} {aud:6.2f} dB   "
              f"rms_db {rms:5.2f}   (+{d:.2f})", flush=True)
    print(f"\n{len(hits)} (entry, axis, register) cells hidden by the window; "
          f"{len({h[1] for h in hits})} entries", flush=True)


if __name__ == "__main__":
    main()
