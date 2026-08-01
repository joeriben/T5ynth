#!/usr/bin/env python3
"""Hear `string`'s damp axis: what it does now, against what it did before.

BJ, 2026-08-01: *"'damp' verzerrt anstatt zu dämpfen"*, *"alles ist gut bei
Damp=0, und wird sofort falsch bei Damp > 0"* — and on 2026-08-02, after the
first repair: *"'Damp' immer noch völlig unbrauchbar. Es muss entweder richtig
verstanden werden oder auf 0 gesetzt und aus der liste genommen werden."*

WHY IT WAS WRONG, measured before anything was changed. The axis was wired to
`streson`'s feedback. That feedback is FLAT — one gain for every partial — so
lowering it does not damp the string, it removes the resonance and leaves the
broadband `dust` drive standing. The body's last stage is `balance` against a
FIXED sine, which then amplifies precisely that. At 220 Hz, 4×:

    damp   balance factor   centroid   harmonic/noise
    0.00       3.7×          3099 Hz      13.9 dB
    0.50      11.9×          4571 Hz       4.4 dB
    1.00      15.8×          4924 Hz       1.1 dB

Brighter, noisier and peakier the further it is turned — the opposite of
damping, and an exact fit to what BJ heard. The meter that certified it read
comb contrast, which is the one quantity that still behaved.

METHOD AND SOURCE, written before the change (CLAUDE.md, Instrument Authoring
rule 1):

  METHOD  damping a string is a FREQUENCY-DEPENDENT loss. The hand takes the
          highs first and the low partials keep going, which is why a muted
          string reads as dull and not as quiet. In a digital string that loss
          is the LOOP FILTER — a one-pole lowpass — and its cutoff is the
          damping control.
  SOURCE  David A. Jaffe & Julius O. Smith III, "Extensions of the
          Karplus-Strong Plucked-String Algorithm", Computer Music Journal 7(2),
          1983, 56–69. Already in this project's record: see `plucked_wire` in
          `docs/LCO_CODE_PROVENANCE.md` — "the loop filter
          `state = state·refl + y·(1−refl)` and what `refl` therefore is (a
          one-pole pole, not a per-period damping)".

`streson` exposes no loop filter, so the frequency-dependent part sits in the
`tone` already standing in front of it. That is not a fitted curve laid over the
model's output (rule 2): for a CONTINUOUSLY driven string the steady-state
spectrum is the drive spectrum shaped by the comb, so filtering the drive and
filtering inside the loop reach the same steady state. They would differ in the
decay — and this body has none, because the drive never stops and the host owns
note-off.

RENDERED AT 176400, not 44100: the LRO's default oversampling is 4×
(PluginProcessor.cpp:268), and this body's centroid reads 3098 Hz there against
1406 Hz at 44100. Judging it at 44100 would judge a rate the plugin never runs.

The `previous_*` files are the axis as BJ heard it, rendered from the wiring
that shipped in 08defb07, so the change can be HEARD as a change rather than
asserted. Every file carries one common gain — the body already holds one
loudness across the axis (0.013 dB), so no per-file levelling is needed and none
is applied.

    .venv/bin/python tools/lco_ab_string_damp.py
"""

from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))
sys.path.insert(0, str(REPO / "backend"))

import lco_measure as M  # noqa: E402

OUT = REPO / "tools" / "lco_ab_string_damp"
NOTES = [("A2", 110.0), ("A4", 440.0)]
STEPS = (0.00, 0.25, 0.50, 0.75, 1.00)
DUR = 4.0
PREROLL = 0.5
RATE = 176400
# The wiring BJ rejected, restored from the commit it shipped in rather than
# retyped, so the "before" side cannot drift away from what he actually heard.
PREVIOUS_COMMIT = "08defb07"


def body_now() -> str:
    lib = json.loads((REPO / "backend" / "lco_library.json").read_text())
    return next(i for i in lib["instruments"] if i["key"] == "string")["code"]


def body_previous() -> str:
    blob = subprocess.run(["git", "show", f"{PREVIOUS_COMMIT}:backend/lco_library.json"],
                          capture_output=True, text=True, cwd=REPO, check=True).stdout
    lib = json.loads(blob)
    return next(i for i in lib["instruments"] if i["key"] == "string")["code"]


def at_damp(body: str, value: float) -> str:
    out, n = re.subn(r"^kdamp\s+=\s+[0-9.]+", f"kdamp   = {value:.2f}", body, flags=re.M)
    if n != 1:
        raise SystemExit(f"the damp line is not where this expects it ({n} matches)")
    return out


def main() -> None:
    M.SR = RATE
    OUT.mkdir(parents=True, exist_ok=True)
    raw = {}
    for side, body in (("damp", body_now()), ("previous", body_previous())):
        (OUT / f"{side}_body.orc").write_text(body)
        for d in STEPS:
            for name, f in NOTES:
                y, err = M.render(at_damp(body, d), dur=DUR, freq=f, preroll=PREROLL)
                if err and "HOT" not in err:
                    raise SystemExit(f"{side} {d} {name}: {err}")
                raw[(side, d, name, f)] = y
                print(f"{side:9s} damp={d:.2f} {name} {f:6.1f} Hz  "
                      f"rms={M.rms_db(y):7.2f} dB  peak={float(np.max(np.abs(y))):.3f}  "
                      f"centroid={M.centroid_audible(y):7.0f} Hz"
                      f"{'   HOT: ' + err if err else ''}")
    top = max(float(np.max(np.abs(y))) for y in raw.values())
    g = min(1.0, 0.98 / top)
    print(f"\none gain over all {len(raw)} files: {20 * np.log10(g):+.2f} dB "
          f"(loudest peak was {top:.3f})")
    for (side, d, name, f), y in raw.items():
        stem = f"{'' if side == 'damp' else 'previous_'}damp_{d:.2f}__{name}_{int(f)}hz"
        sf.write(OUT / f"{stem}.wav", y * g, RATE, subtype="PCM_24")
    (OUT / "levels.json").write_text(json.dumps(
        {"rate": RATE, "preroll_s": PREROLL, "seconds": DUR,
         "previous_from": PREVIOUS_COMMIT,
         "common_gain_db": round(20 * float(np.log10(g)), 2)}, indent=1) + "\n")
    print("->", OUT)


if __name__ == "__main__":
    main()
