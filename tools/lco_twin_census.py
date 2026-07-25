#!/usr/bin/env python3
"""Is any entry indistinguishable from noise, or from another entry?

BJ on `rain`, 2026-07-25: „das ist offenbar nicht anders als rauschen" -- and he
was right: it measured 1.6 dB from the library's own `noise`.  Every internal
meter passed it.  So this asks the question of the whole library at once, and it
has since found the same defect twice more (`crackle` at 1.9 dB, `surf` at 4.6).

Two terms, and an entry is only a twin of another when it is close on BOTH:

  colour  the rms difference of the two long-term power spectra, each normalised
          to its own peak, in dB.  Ignores level entirely.
  rhythm  the rms difference of the two ENVELOPE modulation spectra over
          0.5-200 Hz -- where the amplitude fluctuation energy sits: a tremolo, a
          beat rate, an event rate.  Also normalised, also level-blind.

The second term is what keeps the census honest.  On colour alone the library
looks full of duplicates -- a cricket and a hurdy-gurdy sit 3.02 dB apart, an
ocarina and a struck glass 3.72 -- and every one of those pairs is 5 to 20 dB
apart in rhythm, which is where the difference actually lives.  Judged on colour
alone this tool would have condemned a dozen entries that are fine and taught
whoever fixed them to make the library WORSE.

  lco_twin_census.py [--registers 110,220,440] [--dur 4] [--json out.json]
"""
from __future__ import annotations

import argparse
import itertools
import json
import sys
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import lco_measure as M   # noqa: E402

LEX = REPO / "backend" / "dco_lexicon.json"

# The entries that ARE noise.  Everything else has to be audibly more than one.
NOISY = ("noise", "pink_noise", "hiss")
# `rain` cleared 17.1 dB and `crackle` 21.6 once they were rebuilt; the shipped
# `surf` sat at 4.6 and was a filtered noise bed with no events in it at all.
NOISE_FLOOR_DB = 10.0
# A pair is a twin only when it is close on BOTH terms.
TWIN_COLOUR_DB = 5.0
TWIN_RHYTHM_DB = 4.0


def colour(y):
    w = int(0.10 * M.SR)
    k = max(1, len(y) // w)
    f = y[:k * w].reshape(k, w) * np.hanning(w)
    sp = (np.abs(np.fft.rfft(f, axis=1)) ** 2).mean(0)
    return 10 * np.log10(np.maximum(sp / max(sp.max(), 1e-30), 1e-12))


def rhythm(y):
    n = int(0.002 * M.SR)
    k = max(2, len(y) // n)
    e = np.sqrt((y[:k * n].reshape(k, n) ** 2).mean(1)) + 1e-12
    e = np.log(e)
    e -= e.mean()
    sp = np.abs(np.fft.rfft(e * np.hanning(len(e)))) ** 2
    f = np.fft.rfftfreq(len(e), n / M.SR)
    s = sp[(f >= 0.5) & (f <= 200)]
    s = s / max(s.sum(), 1e-12)
    return 10 * np.log10(np.maximum(s, 1e-12))


def roughness(y, w=0.005):
    """How much of the sound is made of separate EVENTS: the mean absolute step
    of the log envelope.  A noise bed reads ~0.04, rebuilt `rain` 0.39."""
    n = int(w * M.SR)
    k = max(2, len(y) // n)
    e = np.sqrt((y[:k * n].reshape(k, n) ** 2).mean(1)) + 1e-12
    return float(np.abs(np.diff(np.log(e))).mean())


def rms(a, b):
    return float(np.sqrt(((a - b) ** 2).mean()))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--registers", default="110,220,440")
    ap.add_argument("--dur", type=float, default=4.0)
    ap.add_argument("--preroll", type=float, default=2.0)
    ap.add_argument("--json", default="")
    a = ap.parse_args()
    freqs = [float(x) for x in a.registers.split(",")]

    lex = json.loads(LEX.read_text())
    C, Rh, Ro = {}, {}, {}
    for t in lex["techniques"]:
        c, r, o = [], [], []
        for f in freqs:
            y, err = M.render(t["code"], dur=a.dur, freq=f, preroll=a.preroll)
            if y is None:
                print(f"  {t['key']} @ {f:.0f} Hz: {err}", file=sys.stderr)
                break
            c.append(colour(y))
            r.append(rhythm(y))
            o.append(roughness(y))
        if len(c) == len(freqs):
            C[t["key"]] = np.mean(c, 0)
            Rh[t["key"]] = np.mean(r, 0)
            Ro[t["key"]] = float(np.mean(o))
    print(f"{len(C)} entries at {a.registers} Hz, lexicon_version "
          f"{lex.get('lexicon_version')}\n")

    noisy = [n for n in NOISY if n in C]
    rows = sorted((min(rms(C[k], C[n]) for n in noisy), k)
                  for k in C if k not in NOISY)
    over = [(d, k) for d, k in rows if d < NOISE_FLOOR_DB]
    print(f"distance to the nearest noise entry ({', '.join(noisy)}):")
    for d, k in rows[:10]:
        mark = "   <-- IS noise" if d < NOISE_FLOOR_DB else ""
        print(f"   {k:>16} {d:6.2f} dB   roughness {Ro[k]:.3f}{mark}")

    twins = []
    for x, y in itertools.combinations(sorted(C), 2):
        dc, dr = rms(C[x], C[y]), rms(Rh[x], Rh[y])
        if dc < TWIN_COLOUR_DB and dr < TWIN_RHYTHM_DB:
            twins.append((dc, dr, x, y))
    print(f"\npairs that are the same sound (colour < {TWIN_COLOUR_DB} dB AND "
          f"rhythm < {TWIN_RHYTHM_DB} dB):")
    for dc, dr, x, y in sorted(twins):
        print(f"   {x:>16} <-> {y:<16} colour {dc:6.2f}  rhythm {dr:6.2f}")
    if not twins:
        print("   none")

    print("\nclosest on COLOUR alone -- shown so the rhythm column can be read "
          "next to it, not as a finding:")
    for dc, x, y in sorted((rms(C[x], C[y]), x, y)
                           for x, y in itertools.combinations(sorted(C), 2))[:10]:
        print(f"   {x:>16} <-> {y:<16} colour {dc:6.2f}  "
              f"rhythm {rms(Rh[x], Rh[y]):6.2f}")

    if a.json:
        Path(a.json).write_text(json.dumps({
            "lexicon_version": lex.get("lexicon_version"),
            "noise_distance": {k: d for d, k in rows},
            "roughness": Ro,
            "twins": [{"a": x, "b": y, "colour_db": dc, "rhythm_db": dr}
                      for dc, dr, x, y in sorted(twins)],
        }, indent=1) + "\n")

    return 1 if (over or twins) else 0


if __name__ == "__main__":
    sys.exit(main())
