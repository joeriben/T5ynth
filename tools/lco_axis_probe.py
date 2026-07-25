#!/usr/bin/env python3
"""Sweep one axis of a candidate instrument and see what it actually moves.

The authoring instrument for `LCO_CONCEPT.md` §3. An axis is only a parameter if
it moves the sound and does NOT move the loudness — instrument 3 shipped with two
of its four "colour" controls also acting as faders (§7.8), and that passed every
measurement of the controls themselves. So a new axis is swept and read before it
is written into the lexicon, not after.

The convention it expects, and the reason for it: a candidate body declares each
axis as a NAMED k-variable on its own line, with its default,

    kpress  = 0.45     ; press: 0 wide and soft .. 1 narrow and reedy

so that (a) the whole axis lives in one place a model can edit by changing one
number, and (b) this tool can set it by substitution without an emitter. The seven
existing parametrised entries bake their axes into expressions instead, which is
why `anchor_code` could only ever be generated for one axis of each: there was no
single place to set.

    .venv/bin/python tools/lco_axis_probe.py --body cand.orc --axis press
    .venv/bin/python tools/lco_axis_probe.py --body cand.orc --all
    .venv/bin/python tools/lco_axis_probe.py --body cand.orc --axis musette \
        --values 0,0.5,1 --freq 220
"""
import argparse
import itertools
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

import lco_measure as M  # noqa: E402

# `kvar  = number   ; axisname: what it does` — the one shape an axis may take.
#
# The trailing `axisname:` is REQUIRED and it is what names the axis, not the
# variable. Two reasons, both found the hard way:
#
#   * A body sets plenty of k-variables that are NOT axes — `kmul = 0.86` for a
#     `gbuzz` ratio, say. Matching on `kname = number` alone swept that one to 1.0,
#     where the normalisation `1/(1-kmul)` is infinite, and 122 of 216 gate corners
#     failed to render on two bodies that were otherwise fine. A constant with no
#     `name:` comment is now left alone.
#   * The axis's human name and its variable name are not the same word and should
#     not have to be: the accordion's `kmus` is the parameter `musette`, and it is
#     the parameter name that the lexicon, the anchors and the model all use.
#
# The name is capped at 16 characters so an ordinary prose comment that happens to
# contain a colon ("a steel tongue through a narrow slit: bright") is not read as
# a declaration.
# An axis may declare its own RANGE when that range is not 0..1:
#
#     kduty   = 0.5   ; duty [0.05..0.5]: the ON fraction of the cycle
#
# Needed because a physical quantity has to stay itself. BJ's convention is that duty
# IS the ON fraction, so a `duty` axis cannot be a normalised knob that means 0.5 at
# one end — and `gate()` used to step every axis 0, 0.5, 1 whatever it was declared to
# be, which on a duty axis renders SILENCE at 0. Any axis whose range is not 0..1 was
# being gated on values it never legitimately takes.
AXIS = re.compile(r"^(?P<pad>\s*)k(?P<var>[a-z][a-z0-9]*)(?P<gap>\s+)=(?P<sp>\s*)"
                  r"(?P<val>-?\d+(?:\.\d+)?)(?P<pre>\s*;\s*)"
                  r"(?P<name>[a-z][a-z0-9 _-]{0,15})"
                  r"(?:\s*\[(?P<lo>-?\d+(?:\.\d+)?)\.\."
                  r"(?P<hi>-?\d+(?:\.\d+)?)\])?:(?P<tail>.*)$")


def axes(body):
    """{axis name: (variable, default, line index, (lo, hi))} for every declaration."""
    out = {}
    for i, line in enumerate(body.splitlines()):
        m = AXIS.match(line)
        if m:
            lo = 0.0 if m.group("lo") is None else float(m.group("lo"))
            hi = 1.0 if m.group("hi") is None else float(m.group("hi"))
            if hi <= lo:
                raise SystemExit(f"axis {m.group('name').strip()!r} declares the "
                                 f"range [{lo}..{hi}], which is empty or reversed")
            out[m.group("name").strip()] = (m.group("var"), float(m.group("val")),
                                            i, (lo, hi))
    return out


def with_axis(body, name, value):
    lines = body.splitlines()
    found = 0
    for i, line in enumerate(lines):
        m = AXIS.match(line)
        if m and m.group("name").strip() == name:
            rng = ("" if m.group("lo") is None
                   else f"[{m.group('lo')}..{m.group('hi')}]")
            lines[i] = (f"{m.group('pad')}k{m.group('var')}{m.group('gap')}="
                        f"{m.group('sp')}{value:g}{m.group('pre')}"
                        f"{m.group('name')}{rng}:{m.group('tail')}")
            found += 1
    if found != 1:
        raise SystemExit(f"axis {name!r} is declared {found} times — an axis has to "
                         f"have exactly one place it is set")
    return "\n".join(lines)


def sweep(body, name, values, freq=220.0, dur=4.0):
    rows = []
    for v in values:
        y, err = M.render(with_axis(body, name, v), dur=dur, freq=freq)
        if y is None:
            rows.append({"value": v, "error": err})
            continue
        r = M.measure(y, freq)
        r["value"] = v
        if err:
            r["error"] = err
        rows.append(r)
    return rows


def report(name, rows, quiet=False):
    good = [r for r in rows if "rms_db" in r]
    if not quiet:
        print(f"\naxis {name!r}")
        print(f"  {'value':>7} {'rms dB':>8} {'centroid':>9} {'comb dB':>8} "
              f"{'motion':>8} {'sustain':>8}  note")
        for r in rows:
            if "rms_db" not in r:
                print(f"  {r['value']:7g} {'':>8} {'':>9} {'':>8} {'':>8} {'':>8}"
                      f"  {r['error']}")
                continue
            print(f"  {r['value']:7g} {r['rms_db']:8.2f} {r['centroid']:9.1f} "
                  f"{r['comb_db']:8.1f} {r['centroid_motion_hz']:8.1f} "
                  f"{r['sustain']:8.3f}  {r.get('error', '')}")
    if len(good) < 2:
        return None
    def sp(k):
        vs = [r[k] for r in good]
        return max(vs) - min(vs)
    verdict = {"rms_spread_db": round(sp("rms_db"), 2),
               "centroid_spread_hz": round(sp("centroid"), 1),
               "comb_spread_db": round(sp("comb_db"), 1),
               "motion_spread_hz": round(sp("centroid_motion_hz"), 1),
               # A rate control has its whole signature here and none in the colour
               # meters: a cricket's chirp rate moved 2 Hz of centroid and 0.5 dB of
               # comb over 12 -> 50 Hz of actual pulse rate, and this verdict called
               # it "moves nothing measurable" while the audit's M6 — which does read
               # the beat — passed it. The authoring tool and the audit must not
               # disagree about whether an axis works.
               "beat_rate_spread_hz": round(sp("beat_rate_hz"), 2),
               "beat_depth_spread": round(sp("beat_depth"), 3)}
    ok_loud = verdict["rms_spread_db"] <= 1.0
    moves = (verdict["centroid_spread_hz"] > 40 or verdict["comb_spread_db"] > 1.5
             or verdict["motion_spread_hz"] > 15
             or verdict["beat_rate_spread_hz"] > 0.5
             or verdict["beat_depth_spread"] > 0.05)
    verdict["verdict"] = ("colour" if ok_loud and moves else
                          "A FADER — it moves loudness" if not ok_loud and moves else
                          "moves nothing measurable" if ok_loud else
                          "A FADER and nothing else")
    if not quiet:
        print(f"  -> loudness {verdict['rms_spread_db']:+.2f} dB, colour "
              f"{verdict['centroid_spread_hz']:.0f} Hz, comb "
              f"{verdict['comb_spread_db']:.1f} dB, motion "
              f"{verdict['motion_spread_hz']:.0f} Hz, beat "
              f"{verdict['beat_rate_spread_hz']:.1f} Hz/"
              f"{verdict['beat_depth_spread']:.2f}  ==  {verdict['verdict']}")
    return verdict


def gate(body, freq=220.0, steps=3, registers=(55, 110, 220, 440, 880, 1760)):
    """Every CORNER of the axis cube, not just the defaults. PASS/FAIL.

    Sweeping one axis at a time from the defaults leaves most of the instrument
    unmeasured, and the corners are where it breaks. Measured on the harmonica
    written for this batch: at its defaults it moved (coherence +0.99) and every
    axis read as colour, but at full breath with no press the hiss swamped the
    note and three of eight corners did not move at all — the movement fundamental
    failing in a quarter of the instrument's own range, invisible to the per-axis
    report. The fix (tying the hiss to the breath cycle that drives the reed) is
    one line; finding it needed this.

    Five questions, each the objective form of a rule in `LCO_CONCEPT.md` §4:
    does every corner render, does every corner MOVE, is the loudness the same at
    all of them, is it the same at every register, and does any corner clip.

    Every corner is asked at every REGISTER, not the corners at one pitch and the
    registers at the defaults. The combination is what breaks the rule: the bagpipe
    reported 0.12 dB across its corners and 0.24 dB across its registers, and 1.44 dB
    across the cross — quietest at bass drone with no beat at 55 Hz, loudest a
    half-turn away at 110 Hz. Two marginals both inside a bound say nothing about the
    joint, and a gate that only ever looked at marginals passed a body that violates
    its own 1.00 dB rule. The cost is real: three axes at three steps over six
    registers is 162 renders, roughly five times the old gate.
    """
    declared = axes(body)
    names = sorted(declared)
    # each axis stepped across ITS OWN declared range, not 0..1
    def steps_for(n):
        lo, hi = declared[n][3]
        return ([lo + (hi - lo) * i / (steps - 1) for i in range(steps)]
                if steps > 1 else [(lo + hi) / 2])
    regs = [float(f) for f in registers]
    rows, fails = [], []
    for combo in itertools.product(*(steps_for(n) for n in names)):
        b = body
        for k, v in zip(names, combo):
            b = with_axis(b, k, v)
        corner = dict(zip(names, combo))
        for f in regs:
            y, err = M.render(b, dur=4.0, freq=f)
            if y is None:
                fails.append(("renders", dict(corner, **{"Hz": f}), err))
                continue
            # `moves` and the spectral meters are only meaningful about the note
            # being played, so they are read at `freq`; loudness and peak are read
            # everywhere, since that is where the cross matters.
            if f == freq:
                r = M.measure(y, freq)
                if not r["moves"]:
                    fails.append(("moves", corner,
                                  f"travel {r['centroid_motion_hz']} Hz at coherence "
                                  f"{r['motion_coherence']} — variance, not motion"))
            else:
                r = {"rms_db": M.rms_db(y), "peak_p999": M.peak_p999(y)}
            rows.append((dict(corner, **{"Hz": f}), r))
    if not rows:
        return False, fails, {}
    at_freq = [(c, r) for c, r in rows if c["Hz"] == freq]
    lv = [r["rms_db"] for _, r in at_freq]
    pk = max(((r["peak_p999"], c) for c, r in rows), key=lambda t: t[0])
    stats = {"corners": len(at_freq), "renders": len(rows),
             "loudness_spread_db": round(max(lv) - min(lv), 2) if lv else None,
             "worst_peak": pk}
    if stats["loudness_spread_db"] is not None and stats["loudness_spread_db"] > 1.0:
        fails.append(("one loudness", pk[1],
                      f"{stats['loudness_spread_db']:.2f} dB across the corners"))
    if pk[0] > 2.97:
        fails.append(("headroom", pk[1],
                      f"p99.9 {pk[0]:.2f}; the host clips a body above 2.97"))
    # the register tilt at the DEFAULTS, kept because it is the number the notes
    # quote, and then the whole cross, which is the one that has to hold.
    dflt = {n: d for n, (_v, d, _i, _r) in declared.items()}
    near = min(rows, key=lambda cr: sum(abs(cr[0].get(n, 0) - dflt.get(n, 0))
                                        for n in names))[0]
    base = [(c["Hz"], r["rms_db"]) for c, r in rows
            if all(abs(c[n] - near[n]) < 1e-9 for n in names)]
    if len(base) >= 2:
        v = [x for _, x in base]
        stats["register_tilt_db"] = round(max(v) - min(v), 2)
        stats["register"] = [(f, round(x, 2)) for f, x in sorted(base)]
    allv = [(r["rms_db"], c) for c, r in rows]
    if len(allv) >= 2:
        lo, hi = min(allv, key=lambda t: t[0]), max(allv, key=lambda t: t[0])
        stats["cross_spread_db"] = round(hi[0] - lo[0], 2)
        stats["cross_quietest"], stats["cross_loudest"] = lo[1], hi[1]
        if stats["cross_spread_db"] > 3.0:
            fails.append(("one loudness at every register", hi[1],
                          f"{stats['cross_spread_db']:.2f} dB across the corners AND "
                          f"registers together — quietest {lo[0]:.2f} at {lo[1]}, "
                          f"loudest {hi[0]:.2f} at {hi[1]}"))
    return not fails, fails, stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--body", required=True, help="candidate body file")
    ap.add_argument("--axis", action="append", help="axis name (repeatable)")
    ap.add_argument("--all", action="store_true", help="every declared axis")
    ap.add_argument("--values", help="comma-separated values (default 0,0.5,1)")
    ap.add_argument("--freq", type=float, default=220.0)
    ap.add_argument("--registers", help="also sweep the register, e.g. 110,220,440,880")
    ap.add_argument("--gate", action="store_true",
                    help="PASS/FAIL over every corner of the axis cube")
    ap.add_argument("--steps", type=int, default=3,
                    help="values per axis in --gate (default 3)")
    args = ap.parse_args()

    body = Path(args.body).read_text().rstrip("\n")
    declared = axes(body)
    if not declared:
        raise SystemExit("no axis declarations found. An axis is a line of the "
                         "shape `kname  = 0.45   ; what it does`.")
    print(f"{args.body}: axes " +
          ", ".join(f"{n}={d:g}" + ("" if (lo, hi) == (0.0, 1.0) else f" [{lo:g}..{hi:g}]")
                    for n, (_v, d, _i, (lo, hi)) in sorted(declared.items())))

    y, err = M.render(body, dur=4.0, freq=args.freq)
    if y is None:
        raise SystemExit(f"the body at its defaults does not render: {err}")
    d = M.measure(y, args.freq)
    print(f"defaults @ {args.freq:.0f} Hz: rms {d['rms_db']:.2f} dB, centroid "
          f"{d['centroid']:.0f} Hz, comb {d['comb_db']:.1f} dB, motion "
          f"{d['centroid_motion_hz']:.0f} Hz, sustain {d['sustain']:.3f}"
          + (f"   [{err}]" if err else ""))

    if args.registers:
        regs = [float(x) for x in args.registers.split(",")]
        lv = []
        for f in regs:
            yr, er = M.render(body, dur=4.0, freq=f)
            lv.append((f, None if yr is None else M.rms_db(yr), er))
        print("register: " + "  ".join(
            f"{f:.0f}:{'ERR' if v is None else f'{v:+.2f}'}" for f, v, _ in lv))
        vals = [v for _, v, _ in lv if v is not None]
        if len(vals) >= 2:
            import math
            octs = math.log2(regs[-1] / regs[0])
            print(f"  -> tilt {max(vals) - min(vals):.2f} dB total, "
                  f"{(vals[-1] - vals[0]) / octs:+.2f} dB/octave")
        if len({f for f, v, e in lv if v is not None}) >= 1:
            # the render-to-render spread at ONE register, so the tilt above can be
            # told apart from measurement noise (1/f-heavy bodies vary ~1.5 dB)
            rep = []
            for _ in range(3):
                yr, _e = M.render(body, dur=4.0, freq=regs[0])
                if yr is not None:
                    rep.append(M.rms_db(yr))
            if len(rep) >= 2:
                print(f"  -> the same register measured {len(rep)}x spreads "
                      f"{max(rep) - min(rep):.2f} dB — a tilt under that is noise")

    if args.gate:
        ok, fails, st = gate(body, args.freq, args.steps)
        print(f"\ngate: {st.get('corners', 0)} corners x "
              f"{st.get('renders', 0) // max(st.get('corners', 1), 1)} registers = "
              f"{st.get('renders', 0)} renders, loudness spread "
              f"{st.get('loudness_spread_db')} dB, register tilt "
              f"{st.get('register_tilt_db')} dB, the two crossed "
              f"{st.get('cross_spread_db')} dB, worst p99.9 "
              f"{st['worst_peak'][0]:.2f} at {st['worst_peak'][1]}"
              if st else "\ngate: nothing rendered")
        for rule, corner, detail in fails:
            print(f"  FAIL  {rule}: {corner}  {detail}")
        print("  PASS  every corner renders, moves, and holds one loudness"
              if ok else f"  -> {len(fails)} failure(s)")
        return 0 if ok else 1

    names = sorted(declared) if args.all else (args.axis or [])
    values = ([float(x) for x in args.values.split(",")] if args.values
              else [0.0, 0.5, 1.0])
    for n in names:
        if n not in declared:
            raise SystemExit(f"no axis {n!r}; declared: {sorted(declared)}")
        report(n, sweep(body, n, values, args.freq))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
