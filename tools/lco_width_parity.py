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

    .venv/bin/python tools/lco_width_parity.py --out tools/lco_listening/analog_osc_width_parity \
        --rev <the commit before the rewrite>
    .venv/bin/python tools/lco_width_parity.py --out <same> --check

`--rev` matters: the reference is the OLD entry measured with TODAY's instrument. When
the instrument is corrected the reference has to be re-measured from the old entry, not
re-taken from the new one, which would quietly make the new behaviour its own target.

TWO cases are expected to deviate and the frozen numbers are deliberately NOT updated
to hide either.

`triangle`, 600 -> 675 Hz centroid and 126 -> 38 dB odd/even. The old body reached its
triangle through `kramp limit 0.5 - 0.96 * kwave + kjit, 0.02, 0.5`, whose upper clamp
sits exactly ON the triangle — so `age`'s width jitter was clipped away on one side
there and nowhere else, and the old triangle was cleaner than an aged oscillator's
triangle has any reason to be. The pulse rail never had that clamp (its square measures
27.9 dB odd/even at the same jitter). With one width the jitter reaches both rails
alike; at `age` = 0 the new triangle measures 131.3 dB odd/even, so nothing but `age` is
in it.

`pwm_moving`, rms -7.77 -> -5.32 dB. The old body's swing covered the whole 0..1 width
range and a pulse is quiet at both ends, so its mean level sat low; the new exemplar
covers 0.2..0.8 and spends its time nearer the loud middle. The movement itself is
intact — duty travel 9.1 -> 10.6 dB — and the level difference is the pulse's own law
(see the entry's `; LOUDNESS:` declaration), not a fader.

WHAT THE DUTY TRAVEL COLUMN CANNOT DO: separate the LFO from `age`. At `age` 0.35 a
STANDING square reads 10.2 dB, because `kjit` drifts slowly through the neighbourhood of
0.5 where odd/even is steepest. At `age` 0 the same square reads 0.6 dB and the moving
width still reads 9.9. So the column is a comparison against the frozen row at the same
`age`, never a verdict on its own.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
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


def entry_at(rev, key="analog_osc"):
    """The entry as it stood at a git revision. Freezing has to be able to reach back:
    the corpus is "the OLD entry measured with TODAY's instrument", and when the
    instrument is corrected the reference has to be re-measured from the old entry, not
    re-taken from the new one -- which would quietly make the new behaviour its own
    target."""
    blob = subprocess.run(["git", "show", f"{rev}:backend/dco_lexicon.json"],
                          cwd=REPO, capture_output=True, text=True, check=True).stdout
    for t in json.loads(blob)["techniques"]:
        if t["key"] == key:
            return t
    raise SystemExit(f"no entry {key!r} at {rev}")


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
        ("triangle",     anchor(e, "wave=ramp")),
        ("saw",          anchor(e, "width=end with wave=0")),
        ("square",       anchor(e, "wave=pulse")),
        ("pulse_narrow", anchor(e, "width=narrow with wave=1")),
        ("pulse_wide",   anchor(e, "width=wide with wave=1")),
        ("pwm_moving",   anchor(e, "width=moving with wave=1")),
    ]


def measure(name, body, out, write_wav):
    y, err = M.render(body, dur=DUR, freq=FREQ, preroll=PREROLL)
    if err:
        raise SystemExit(f"{name}: {err}")
    # Only when FREEZING. This used to write unconditionally, so every --check run
    # overwrote the very reference it was checking against -- the six "before" WAVs
    # were gone before anyone listened to them, and only the JSON survived.
    # `lco_width_ab.py` renders them from a git revision, which is the way back.
    if write_wav:
        sf.write(out / f"{name}.wav", np.asarray(y) / max(np.abs(y).max(), 1e-9) * 0.89,
                 M.SR, subtype="FLOAT")
    return {
        "centroid": round(M.centroid(y), 1),
        "odd_even_db": round(M.odd_even_db(y, FREQ), 1),
        "rms_db": round(M.rms_db(y), 2),
        "partials": M.partials(y, FREQ, n=12),
        # A duty that MOVES shows up as odd/even travelling over the note; a standing
        # one holds. Read it in dense short windows, NOT in thirds: a third is 1.2 s,
        # and anything above about 1 Hz averages out inside one. `odd_even_db` takes no
        # window, so the slice has to be handed to `partials` as t0/t1 -- slicing the
        # array instead left its default 0.5..3.5 s in place and silently discarded the
        # first 0.5 s of every window.
        "odd_even_track": [_oe(y, t, t + 0.15) for t in np.arange(0.3, 3.75, 0.1)],
    }


def _oe(y, t0, t1, f=FREQ):
    lin = [10 ** (v / 20.0) for v in M.partials(y, f, n=16, t0=t0, t1=t1)]
    return round(float(20 * np.log10(max(sum(lin[0::2]), 1e-12)
                                     / max(sum(lin[1::2]), 1e-12))), 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--check", action="store_true",
                    help="compare against the frozen JSON instead of writing it")
    ap.add_argument("--rev",
                    help="freeze the entry as it stood at this git revision, "
                         "measured with today's instrument")
    a = ap.parse_args()

    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    frozen = out / "parity.json"

    src = entry_at(a.rev) if a.rev else entry()
    got = {name: measure(name, body, out, write_wav=not a.check)
           for name, body in cases(src)}

    def span(m):
        t = m.get("odd_even_track") or m.get("odd_even_thirds") or [0.0]
        return round(max(t) - min(t), 1)

    if not a.check:
        frozen.write_text(json.dumps(got, indent=1) + "\n")
        for name, m in got.items():
            print(f"{name:<14} centroid {m['centroid']:>7.1f}  odd/even "
                  f"{m['odd_even_db']:>7.1f} dB  rms {m['rms_db']:>6.2f} dB  "
                  f"duty travel {span(m):>5.1f} dB")
        print(f"\nfrozen -> {frozen}")
        return 0

    # All four are compared. The first version computed the duty track and the level
    # and then read neither, so the one quantity this corpus exists to protect -- does
    # the width still MOVE, and by how much -- could be lost entirely and still print
    # `ok`: a sweep cut from 0.6 to 0.4 passed on centroid alone.
    want = json.loads(frozen.read_text())
    bad = 0
    for name in want:
        w, g = want[name], got[name]
        dc = abs(g["centroid"] - w["centroid"]) / max(w["centroid"], 1.0)
        do = abs(g["odd_even_db"] - w["odd_even_db"])
        dr = abs(g["rms_db"] - w["rms_db"])
        ds = abs(span(g) - span(w))
        ok = dc <= 0.05 and do <= 3.0 and dr <= 0.5 and ds <= 2.0
        bad += not ok
        print(f"{'ok ' if ok else 'BAD'} {name:<14} centroid {w['centroid']:>7.1f} -> "
              f"{g['centroid']:>7.1f} ({dc * 100:>4.1f}%)   odd/even {w['odd_even_db']:>6.1f}"
              f" -> {g['odd_even_db']:>6.1f}   rms {w['rms_db']:>6.2f} -> {g['rms_db']:>6.2f}"
              f"   duty travel {span(w):>5.1f} -> {span(g):>5.1f} dB")
    print(f"\n{len(want) - bad}/{len(want)} within tolerance")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
