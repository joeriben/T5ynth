#!/usr/bin/env python3
"""A/B the library's BOWED string against a waveguide bow.

BJ, 2026-08-01: *"bowed string" ist völlig daneben. "damp" verzerrt anstatt zu
dämpfen, und bow ist auch furchtbar, ein einziges Gezitter.* — and on 2026-07-30,
already in the entry's own `heard` record: *"string gestrichen ist immer noch nur
eine notlösung"*.

The incumbent is `string` at its shipped `bow=1.0` anchor. What drives it there is
`dust 1.0, 3900` — random impulses into a comb resonator. Random impulses are not
what a bow does to a string, and an irregular amplitude is exactly what a listener
hears as jitter. The entry's own text says so about its other end: *"A continuous
wall of noise into the same resonator is audibly an electric string driver:
aggressive and restless."*

METHOD AND SOURCE, written before the first orchestra line (CLAUDE.md, Instrument
Authoring rule 1):

  METHOD  digital waveguide bowed string — two delay rails carrying the travelling
          waves, a reflection filter standing in for the bridge, and a BOW TABLE
          holding the friction curve at the contact point, so the hair sticks to
          the string and slips off it. That stick-slip cycle is Helmholtz motion
          and it IS bowing. The excitation is periodic, not stochastic.
  SOURCE  Julius O. Smith III, "Efficient Simulation of the Reed-Bore and
          Bow-String Mechanisms", Proc. ICMC 1986, 275–280 — the waveguide plus
          nonlinear-junction formulation. Realised as Csound's `wgbow`, which the
          manual credits to Perry Cook; the same model underlies Cook's STK
          `Bowed`. Only the ARGUMENT LIST is read from the manual, never an
          orchestra (docs/LCO_CODE_PROVENANCE.md).

    ar wgbow kamp, kfreq, kpres, krat, kvibf, kvamp [, ifn] [, iminfreq]

RULE 4 — comparison first, code second. `--side nearest_existing` is run and its
files are on disk before any new body exists; the challengers follow.

THREE SIDES, because the second one is a verdict and not a draft. `bare_string`
is the waveguide bow on its own, which BJ heard and rejected: *"leider zu
statisch. nicht per se ein schlechter klang, intensiv, agressiv, aber nicht bowed
string."* `new` is what that produced — a bow ARM (the hair bites at the start of
the note and lets go; weight and speed keep moving afterwards) and the BOX the
string is mounted on. Keeping the rejected side means the two additions can be
HEARD as additions.

RENDERED AT 176400, NOT 44100. Every earlier A/B in this repo rendered at
`lco_measure`'s 44100, and for these two bodies that would compare a rate the
plugin does not run at: the LRO's default oversampling is 4× (index 2,
PluginProcessor.cpp:268), and at 4× the incumbent's spectral centroid is 10061 Hz
against 3534 Hz at 44100 — nearly one and a half octaves of difference in the
thing being judged. `wgbow`'s tuning moves the same way and for the same reason
its delay line is four times longer in samples.

Loudness is matched per pitch to the incumbent, because what is on trial is
whether the sound reads as a bow, not a gain.

    .venv/bin/python tools/lco_ab_string_bowed.py --side nearest_existing
    .venv/bin/python tools/lco_ab_string_bowed.py --side bare_string
    .venv/bin/python tools/lco_ab_string_bowed.py --side new
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))
sys.path.insert(0, str(REPO / "backend"))
import lco_measure as M  # noqa: E402

OUT = REPO / "tools" / "lco_ab_string_bowed"
NOTES = [("A2", 110.0), ("A3", 220.0), ("A4", 440.0)]
DUR = 4.0
PREROLL = 0.5          # the plugin's voices have been running for hours
RATE = 176400          # the LRO's default 4× at a 44.1 kHz host
# The incumbent, the bare waveguide bow BJ heard first, and the bow arm and box
# his verdict on it produced. The incumbent is always the level reference.
SIDES = ("nearest_existing", "bare_string", "new")


def incumbent() -> str:
    """`string`'s shipped `bow=1.0` anchor, verbatim from the built library."""
    lib = json.loads((REPO / "backend" / "lco_library.json").read_text())
    e = [x for x in lib["instruments"] if x["key"] == "string"][0]
    for label, code in e["anchor_code"].items():
        if label.split()[0] == "bow=1.0":
            return code
    raise SystemExit("the library no longer carries `string`'s bow=1.0 anchor")


def challenger(side: str) -> str:
    """A body kept as a file next to its renders, so what was heard and what was
    written can never drift apart. See the module docstring for method and source.

    `bare_string` is the waveguide bow on its own — the first thing put in front
    of BJ, and his verdict on it is why `new` exists: *"leider zu statisch. nicht
    per se ein schlechter klang, intensiv, agressiv, aber nicht bowed string."*
    It is kept as a side so the two changes it caused — a bow ARM with a catch
    and a stroke, and the BOX the string is mounted on — can be heard as changes
    rather than asserted."""
    return (OUT / f"{side}_body.orc").read_text()


def render(side: str) -> None:
    """Render one side into raw float, keyed by note. No wav is written until
    EVERY side exists, because the gain that makes them comparable is one number
    over all of the renders at once — see `write_wavs`."""
    M.SR = RATE
    body = incumbent() if side == "nearest_existing" else challenger(side)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / f"{side}_body.orc").write_text(body)

    raw = {}
    for name, f in NOTES:
        y, err = M.render(body, dur=DUR, freq=f, preroll=PREROLL)
        if err:
            raise SystemExit(f"{name}: {err}")
        raw[name] = y
        print(f"{side} {name} {f:6.1f} Hz  rms={M.rms_db(y):7.2f} dB  "
              f"peak={float(np.max(np.abs(y))):.3f}  "
              f"f0={M.f0(y):8.2f} ({M.cents(M.f0(y), f):+6.1f} c)  "
              f"comb={M.comb_contrast(y, f):6.2f}  cent={M.centroid(y):8.1f}  "
              f"travel={M.loudness_travel(y):5.2f} dB")
    np.savez(OUT / f"{side}.npz", **raw)


def write_wavs() -> None:
    """Every side to disk under ONE gain law.

    Two levellings, and they must not fight: each challenger is matched to the
    incumbent PER PITCH, so what is heard is the sound and not a gain; then a
    single factor over ALL the files brings the loudest one to 0.98. Scaling a
    file on its own — which is what an ordinary peak guard does — would undo the
    match it was applied on top of, silently, on whichever file happened to be
    the hottest."""
    sides = {s: np.load(OUT / f"{s}.npz") for s in SIDES
             if (OUT / f"{s}.npz").exists()}
    out, matched = {}, {}
    for name, f in NOTES:
        a = sides["nearest_existing"][name]
        rms_a = float(np.sqrt(np.mean(a * a)))
        for s in sides:
            b = sides[s][name]
            g = rms_a / float(np.sqrt(np.mean(b * b)))
            out[(s, name, f)] = b * g
            matched.setdefault(s, {})[name] = round(20 * float(np.log10(g)), 2)
    for s, per in matched.items():
        if s != "nearest_existing":
            print(f"{s}: matched to the incumbent by "
                  + ", ".join(f"{n} {d:+.2f} dB" for n, d in per.items()))
    top = max(float(np.max(np.abs(y))) for y in out.values())
    g = min(1.0, 0.98 / top)
    print(f"one gain over all {len(out)} files: {20 * np.log10(g):+.2f} dB "
          f"(loudest peak was {top:.3f})")
    for (side, name, f), y in out.items():
        sf.write(OUT / f"{side}__{name}_{int(f)}hz.wav", y * g, RATE,
                 subtype="PCM_24")
    (OUT / "levels.json").write_text(json.dumps(
        {"rate": RATE, "preroll_s": PREROLL, "seconds": DUR,
         "match_db": matched,
         "common_gain_db": round(20 * float(np.log10(g)), 2)}, indent=1))


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--side", required=True,
                    choices=SIDES)
    side = ap.parse_args().side
    render(side)
    if all((OUT / f"{s}.npz").exists() for s in SIDES):
        write_wavs()


if __name__ == "__main__":
    main()
