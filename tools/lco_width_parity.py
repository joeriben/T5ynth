#!/usr/bin/env python3
"""Freeze what `analog_osc` can do TODAY, so the rewrite has a target it cannot move.

`analog_osc` is about to have `wave` and `width` reorganised: one width position on
BOTH vco2 generators (imode 4's ramp character and imode 2's duty are the same
argument over the same range), `wave` left as the crossfade between them. That is a
migration, and a migration is only allowed to preserve capability -- so the corpus it
has to pass cannot be synthesised from the NEW vocabulary, which would inherit the new
implementation's blind spot by construction.

So this reads the CURRENT entry's own `anchor_code` blocks -- the entry's own claims
about what it reaches -- renders each one, and writes the measurements to JSON next to
the WAVs. Run it BEFORE the rewrite; run it again AFTER against the same JSON.

Two of the six cases can only be reached through the `pwm=standing` variant, because
the head body's hardwired swing (`kswing poscil 0.55, krate`) sweeps the duty over its
whole range at every setting: at `wave=1` the head body has no standing pulse at all.
That is itself part of what is being fixed, and the corpus records it rather than
hiding it.

    .venv/bin/python tools/lco_width_parity.py --out tools/lco_listening/analog_osc_width_parity
    .venv/bin/python tools/lco_width_parity.py --out <same> --check

ONE case is expected to deviate and the frozen numbers are deliberately NOT updated to
hide it: `triangle`, 600 -> 675 Hz centroid and 126 -> 38 dB odd/even. The old body
reached its triangle through `kramp limit 0.5 - 0.96 * kwave + kjit, 0.02, 0.5`, whose
upper clamp sits exactly ON the triangle — so `age`'s width jitter was clipped away on
one side there and nowhere else, and the old triangle was cleaner than an aged
oscillator's triangle has any reason to be. The pulse rail never had that clamp (its
square measures 27.9 dB odd/even at the same jitter). With one width the jitter now
reaches both rails alike; at `age` = 0 the new triangle measures 131.3 dB odd/even, so
nothing but `age` is in it.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

import lco_measure as M  # noqa: E402

LEX = REPO / "backend" / "dco_lexicon.json"
FREQ = 220.0
DUR = 4.0
PREROLL = 0.5


def entry(key="analog_osc"):
    for t in json.loads(LEX.read_text())["techniques"]:
        if t["key"] == key:
            return t
    raise SystemExit(f"no entry {key!r}")


def set_axis(body, name, value):
    """Rewrite the `k<name> = <value>` declaration line, keeping its comment."""
    pat = re.compile(rf"^(\s*k{name}\s+=\s*)(-?[\d.]+)(\s*;.*)?$", re.M)
    if not pat.search(body):
        raise SystemExit(f"no `k{name} =` line to set")
    return pat.sub(lambda m: f"{m.group(1)}{value}{m.group(3) or ''}", body, count=1)


def anchor(e, needle):
    hits = [k for k in e["anchor_code"] if k.startswith(needle)]
    if len(hits) != 1:
        raise SystemExit(f"{needle!r} matched {len(hits)} anchor blocks")
    return e["anchor_code"][hits[0]]


def cases(e):
    """The six things the entry claims it reaches, taken from its own anchor bodies.

    Both schemas are read here on purpose. Before the rewrite `wave` named a waveform
    and a standing pulse was only reachable through the `pwm=standing` variant; after
    it a waveform is a (`wave`, `width`) pair. The SIX SOUNDS are the constant, which
    is the whole point of a parity corpus: it is stated in what the ear gets, not in
    the vocabulary of either implementation.
    """
    if any(k.startswith("pwm=standing") for k in e["anchor_code"]):
        standing = anchor(e, "pwm=standing")
        return [
            ("triangle",     anchor(e, "wave=0 ")),
            ("saw",          anchor(e, "wave=0.5 ")),
            ("square",       set_axis(standing, "width", 0.5)),
            ("pulse_narrow", set_axis(standing, "width", 0.12)),
            ("pulse_wide",   set_axis(standing, "width", 0.88)),
            ("pwm_moving",   anchor(e, "wave=1 ")),
        ]
    return [
        ("triangle",     anchor(e, "width=symmetric with wave=0")),
        ("saw",          anchor(e, "wave=ramp")),
        ("square",       anchor(e, "width=symmetric with wave=1")),
        ("pulse_narrow", anchor(e, "width=narrow with wave=1")),
        ("pulse_wide",   anchor(e, "width=wide with wave=1")),
        ("pwm_moving",   anchor(e, "width=moving with wave=1")),
    ]


def measure(name, body, out):
    y, err = M.render(body, dur=DUR, freq=FREQ, preroll=PREROLL)
    if err:
        raise SystemExit(f"{name}: {err}")
    sf.write(out / f"{name}.wav", np.asarray(y) / max(np.abs(y).max(), 1e-9) * 0.89,
             M.SR, subtype="FLOAT")
    return {
        "centroid": round(M.centroid(y), 1),
        "odd_even_db": round(M.odd_even_db(y, FREQ), 1),
        "rms_db": round(M.rms_db(y), 2),
        "partials": M.partials(y, FREQ, n=12),
        # A duty that MOVES shows up as odd/even travelling over the note; a standing
        # one holds. Split the note in three and read the ratio in each third.
        "odd_even_thirds": [round(M.odd_even_db(y[int(a * M.SR):int(b * M.SR)], FREQ), 1)
                            for a, b in ((0.3, 1.5), (1.5, 2.7), (2.7, 3.9))],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--check", action="store_true",
                    help="compare against the frozen JSON instead of writing it")
    a = ap.parse_args()

    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    frozen = out / "parity.json"

    got = {name: measure(name, body, out) for name, body in cases(entry())}

    if not a.check:
        frozen.write_text(json.dumps(got, indent=1) + "\n")
        for name, m in got.items():
            print(f"{name:<14} centroid {m['centroid']:>7.1f}  odd/even "
                  f"{m['odd_even_db']:>7.1f} dB  rms {m['rms_db']:>6.2f} dB  "
                  f"thirds {m['odd_even_thirds']}")
        print(f"\nfrozen -> {frozen}")
        return 0

    want = json.loads(frozen.read_text())
    bad = 0
    for name in want:
        w, g = want[name], got[name]
        dc = abs(g["centroid"] - w["centroid"]) / max(w["centroid"], 1.0)
        do = abs(g["odd_even_db"] - w["odd_even_db"])
        ok = dc <= 0.05 and do <= 3.0
        bad += not ok
        print(f"{'ok ' if ok else 'BAD'} {name:<14} centroid {w['centroid']:>7.1f} -> "
              f"{g['centroid']:>7.1f} ({dc * 100:>4.1f}%)   odd/even {w['odd_even_db']:>7.1f}"
              f" -> {g['odd_even_db']:>7.1f} ({do:>5.1f} dB)")
    print(f"\n{len(want) - bad}/{len(want)} within tolerance")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
