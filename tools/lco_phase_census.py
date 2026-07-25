#!/usr/bin/env python3
"""How much does each shipped instrument depend on how long the plugin has been open?

`lco_axis_probe.phase_spread` answers that for ONE body on ONE grid of four plugin
ages. Asking it of the whole library that way produced a ranking and a median that
were properties of the four ages chosen and not of the library, so this file exists
to make the question answerable at all. It cost two published, believed, withdrawn
sets of figures to learn what it takes, and all three lessons are wired in rather
than written down:

**1. One grid is not a measurement.** Three grids of the same construction -- same
settle, same span, same irregular fractions -- give medians 73 / 137 / 107 Hz over
the same 56 bodies, and reorder the tail: `sync` reads 498 on one and 1182 on
another, `glass` 67 / 289 / 298. A four-sample max-min is a draw, not a statistic.
So every body is measured on several grids and what is reported is the SPREAD ACROSS
GRIDS next to the value. A body whose three grids disagree by 4x has no single
number and this prints that instead of picking one.

**2. For a noise-excited body the question cannot be answered without a control.**
Changing the preroll changes which noise samples the note gets, so "uptime" and
"noise realisation" move together and no amount of grid care separates them. The
control is `lco_measure.reseed`: hold the age fixed and change only the seed. On
`glass`, `cymbal`, `bubbles` and `ice` the seed-only spread comes within a factor of two
of the four-age spread, so their uptime dependence cannot be told from the exciter's
dice -- NOT that the control always reads higher; on `glass` it reads 0.84x of the
four-age figure, and the verdict is "cannot separate", never "innocent". `surf` and
`wind` survive the control (240 and 33 against several hundred), and those are the two
the claim was ever really about.

**3. Never report a verdict for a body that was not rendered.** The first version
printed `free_running`'s skip list as "not uptime-dependent at all" without a single
render, and three of the seven were wrong: `noise`, `pink_noise` and `hiss` all read
above the median it published. (Their figures are deliberately not quoted here. They
are the numbers that move most with `--grids`: `noise` reads 147.0 Hz at one grid and
250.2 at three, `hiss` 98.0 and 165.4, and `pink_noise` does not render the same twice
at all -- see `not-reproducible`. Quoting a single-grid reading inside the paragraph
whose lesson is that one grid is a draw and not a statistic is the same mistake in
smaller print, and it is the one this docstring made until 2026-07-25.)

The mechanism of that first failure: `free_running`'s `_AGEN` pattern matches no a-rate
generator, so a bare `anz rand 1.0, 0.5, 1` is invisible to it by construction. Every body is rendered here, and where the detector and the
measurement disagree that disagreement is the finding.

Two more things this reads that the prose around it must not overstate. The tracked
quantity is `centroid_audible` (power-weighted), which is NOT comparable with the
descriptive centroid figures in the library's notes. And it is read UNROUNDED --
`measure()` rounds `centroid_over_note` to whole Hz, which is why `bass_saw` and
`voice_ee` (true spreads 0.287 and 0.117 Hz) once read 0.0 and got called
independent.

    .venv/bin/python tools/lco_phase_census.py
    .venv/bin/python tools/lco_phase_census.py --grids 5 --json out.json
"""
import argparse
import json
import statistics
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

import lco_axis_probe as P  # noqa: E402
import lco_measure as M  # noqa: E402

# Grids of the same construction as `lco_axis_probe._PHASES`: a settle, then four
# irregular fractions of the slowest shipped generator period. Irregular so that a
# body whose own rate divides the grid is not sampled at the same phase four times.
# The first row IS the shipped grid, so a reading here is comparable with the probe's.
_FRACTIONS = ((0.00, 0.19, 0.47, 0.81),
              (0.05, 0.29, 0.61, 0.93),
              (0.11, 0.37, 0.53, 0.71),
              (0.02, 0.41, 0.67, 0.88),
              (0.13, 0.31, 0.59, 0.77))
# How many times the uptime spread must beat the noise-seed control before the word
# "uptime" is used at all. 2.0 is a floor, not a confidence.
CONTROL_FACTOR = 2.0
# The control gets as many seed sets as the uptime side gets grids, and its published
# figure is the MEDIAN of their spreads — the same statistic, computed the same way.
# It used to be a single four-sample draw compared against a median over three grids, and
# the asymmetry decided published classes: holding everything else fixed and changing only
# the seed set, `glass` moved noise-bound -> uptime (control 240.8 -> 73.8), `bubbles`
# likewise (348.5 -> 127.9), and `ice` flipped on three of four alternative sets
# (163.6 -> 66.6 / 59.8 / 59.5). One draw against a median is not a comparison.
_SEED_SETS = ((0, 1, 2, 3),
              (4, 5, 6, 7),
              (8, 9, 10, 11),
              (12, 13, 14, 15),
              (16, 17, 18, 19))
SEEDS = _SEED_SETS[0]


def _first_window(body, preroll, freq, dur=4.0):
    """The note's first colour reading, unrounded, or None if it did not render."""
    y, err = M.render(body, dur=dur, freq=freq, preroll=preroll)
    if y is None:
        return None, err
    # `travel(y, 8)` is what `measure()['centroid_over_note']` is built from, so this
    # is the same quantity the probe reads -- minus the rounding.
    cs, _ = M.travel(y, 8)
    return (cs[0] if cs else None), err


def _spread(vals):
    good = [v for v in vals if v is not None]
    if len(good) < 2:
        return None
    return max(good) - min(good)


def one(body, freq=220.0, grids=3):
    """Uptime spread per grid, and the noise-seed control at a fixed age.

    Both sides are medians over the same number of draws -- see `_SEED_SETS`. Also
    reports `repeat_hz`, the disagreement between two IDENTICAL renders, because a body
    that does not render the same twice cannot be classified at all.
    """
    out = {"grids": [], "hot": [], "render_errors": []}
    for frac in _FRACTIONS[:max(1, grids)]:
        ages = [P._SETTLE_S + f * P._SLOWEST_S for f in frac]
        vals = []
        for age in ages:
            v, err = _first_window(body, age, freq)
            if err:
                (out["hot"] if "HOT" in err else out["render_errors"]).append(err)
            vals.append(v)
        out["grids"].append({"ages_s": [round(a, 2) for a in ages],
                             "firsts_hz": [None if v is None else round(v, 2)
                                           for v in vals],
                             "spread_hz": _spread(vals)})
    # Does the body even render the same twice? Exactly one in the library does not:
    # `pinkish` seeds itself from the clock, so `pink_noise`'s readings — and with them
    # its published class — moved between runs of the identical command (five runs gave
    # 279/369/324/446/397 Hz and the class flipped three times). `lco_param_audit` already
    # carries a guard for this fact; this tool was written later and did not inherit it.
    # A verdict about phase cannot be given for a body whose renders are not repeatable.
    a, _ = _first_window(body, P._SETTLE_S, freq)
    b, _ = _first_window(body, P._SETTLE_S, freq)
    out["repeat_hz"] = None if a is None or b is None else round(abs(a - b), 1)
    # The control. Same age for every draw, so uptime is held and only the dice move.
    # As many seed sets as there are grids, median of their spreads — see `_SEED_SETS`.
    controls = []
    for seeds in _SEED_SETS[:max(1, grids)]:
        seeded = [M.reseed(body, k) for k in seeds]
        if any(s is None for s in seeded):
            controls = None           # no reachable seed: NOT the same as zero
            break
        s = _spread([_first_window(s, P._SETTLE_S, freq)[0] for s in seeded])
        if s is not None:
            controls.append(s)
    if not controls:
        out["control_hz"] = None
        out["control_lo_hz"] = out["control_hi_hz"] = None
    else:
        out["control_hz"] = statistics.median(controls)
        out["control_lo_hz"] = round(min(controls), 1)
        out["control_hi_hz"] = round(max(controls), 1)
    spreads = [g["spread_hz"] for g in out["grids"] if g["spread_hz"] is not None]
    out["uptime_hz"] = round(statistics.median(spreads), 1) if spreads else None
    out["uptime_lo_hz"] = round(min(spreads), 1) if spreads else None
    out["uptime_hi_hz"] = round(max(spreads), 1) if spreads else None
    # A ratio between two readings that are both under a hertz is arithmetic, not
    # instability: `rhodes` at 0.0 and 0.1 Hz is 5.8x and means nothing. Only bodies
    # that actually move on some grid get a ratio.
    out["grid_ratio"] = (round(max(spreads) / min(spreads), 1)
                         if spreads and min(spreads) > 0 and max(spreads) > 1.0
                         else None)
    if out["control_hz"] is not None:
        out["control_hz"] = round(out["control_hz"], 1)
    u, c = out["uptime_hz"], out["control_hz"]
    if u is None:
        out["verdict"] = "unmeasured"
    elif out["repeat_hz"] is not None and out["repeat_hz"] > 1.0:
        # Not reproducible: two identical renders already disagree by more than the
        # threshold every other comparison here is measured against.
        out["verdict"] = "not-reproducible"
    elif c is None:
        out["verdict"] = "uptime" if u > 1.0 else "steady"
    elif u > max(c, 1.0) * CONTROL_FACTOR:
        out["verdict"] = "uptime"
    elif u <= 1.0:
        out["verdict"] = "steady"
    else:
        out["verdict"] = "noise-bound"     # cannot be told from the exciter's dice
    return out


def census(freq=220.0, grids=3, keys=None):
    lex = json.loads((REPO / "backend" / "dco_lexicon.json").read_text())
    rows = []
    t0 = time.time()
    for e in lex["techniques"]:
        if keys and e["key"] not in keys:
            continue
        r = one(e["code"], freq, grids)
        r["key"] = e["key"]
        # The DETECTOR's opinion, kept beside the measurement rather than instead of
        # it. Where the two disagree, that disagreement is the finding.
        r["detector_free_lines"] = len(P.free_running(e["code"]))
        rows.append(r)
        u = "     --" if r["uptime_hz"] is None else f"{r['uptime_hz']:7.1f}"
        c = "   --" if r["control_hz"] is None else f"{r['control_hz']:5.1f}"
        gr = "  -- " if r["grid_ratio"] is None else f"{r['grid_ratio']:4.1f}x"
        print(f"  {r['key']:16s} {u} Hz  grid {gr}  seed-control {c}  "
              f"{r['verdict']:11s} detector {r['detector_free_lines']}", flush=True)
    rows.sort(key=lambda r: -(r["uptime_hz"] or 0))
    return {"freq": freq, "n_grids": min(grids, len(_FRACTIONS)),
            "slowest_s": P._SLOWEST_S, "settle_s": P._SETTLE_S,
            "span_s": round(P._SLOWEST_S * (max(_FRACTIONS[0]) - min(_FRACTIONS[0])), 2),
            "control_factor": CONTROL_FACTOR, "rows": rows,
            "seconds": round(time.time() - t0, 1)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--freq", type=float, default=220.0)
    ap.add_argument("--grids", type=int, default=3,
                    help=f"how many grids of the same construction (max {len(_FRACTIONS)})")
    ap.add_argument("--key", action="append", help="only these entries (repeatable)")
    ap.add_argument("--json", help="write the full per-body result here")
    a = ap.parse_args()
    lex = json.loads((REPO / "backend" / "dco_lexicon.json").read_text())
    known = {e["key"] for e in lex["techniques"]}
    if a.key:
        unknown = [k for k in a.key if k not in known]
        if unknown:
            raise SystemExit(f"no such entry: {', '.join(unknown)}. A silent clean pass "
                             f"on a typo is the same as a lie.")
    out = census(a.freq, a.grids, set(a.key) if a.key else None)
    rows = out["rows"]
    by = {}
    for r in rows:
        by.setdefault(r["verdict"], []).append(r["key"])
    print(f"\n{len(rows)} bodies, {out['n_grids']} grids of four ages each at "
          f"{out['freq']:.0f} Hz, plus the same number of {len(SEEDS)}-seed "
          f"controls at a fixed age "
          f"({out['seconds']} s)")
    print(f"  the grid spans {out['span_s']} s = "
          f"{100.0 * out['span_s'] / out['slowest_s']:.0f} % of the slowest shipped "
          f"period ({out['slowest_s']:.0f} s) -- the last stretch of one cycle is "
          f"never sampled")
    for v, label in (("uptime", "depend on plugin uptime beyond their own noise"),
                     ("noise-bound", "move, but not separably from the exciter's dice"),
                     ("steady", "read under 1 Hz on every grid"),
                     ("unmeasured", "did not render")):
        ks = by.get(v, [])
        print(f"  {len(ks):3d} {label}: " + (", ".join(sorted(ks)) or "none"))
    dis = [r["key"] for r in rows
           if (r["detector_free_lines"] == 0) != (r["verdict"] == "steady")]
    print(f"  {len(dis)} where `free_running` and the measurement disagree: "
          + (", ".join(sorted(dis)) or "none"))
    unstable = [r for r in rows if (r["grid_ratio"] or 0) >= 2.0]
    print(f"  {len(unstable)} whose grids disagree by 2x or more, so they have no "
          f"single number: "
          + (", ".join(f"{r['key']} {r['uptime_lo_hz']}-{r['uptime_hi_hz']}"
                       for r in sorted(unstable, key=lambda r: -(r['grid_ratio'] or 0))[:8])
             or "none"))
    hot = [r["key"] for r in rows if r["hot"]]
    if hot:
        print(f"  {len(hot)} rendered HOT at some age and their readings are of a "
              f"limited signal: " + ", ".join(sorted(hot)))
    if a.json:
        Path(a.json).write_text(json.dumps(out, indent=1) + "\n")
        print("  ->", a.json)
    return 0


if __name__ == "__main__":
    sys.exit(main())
