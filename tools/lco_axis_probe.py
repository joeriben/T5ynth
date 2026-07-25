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

# `kname  = number   ; comment` — the one shape an axis declaration may take.
AXIS = re.compile(r"^(?P<pad>\s*)k(?P<name>[a-z][a-z0-9]*)(?P<gap>\s+)=(?P<sp>\s*)"
                  r"(?P<val>-?\d+(?:\.\d+)?)(?P<tail>\s*(?:;.*)?)$")


def axes(body):
    """{name: (default, line index)} for every axis declaration in the body."""
    out = {}
    for i, line in enumerate(body.splitlines()):
        m = AXIS.match(line)
        if m:
            out[m.group("name")] = (float(m.group("val")), i)
    return out


def with_axis(body, name, value):
    lines = body.splitlines()
    found = 0
    for i, line in enumerate(lines):
        m = AXIS.match(line)
        if m and m.group("name") == name:
            lines[i] = (f"{m.group('pad')}k{name}{m.group('gap')}={m.group('sp')}"
                        f"{value:g}{m.group('tail')}")
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
               "motion_spread_hz": round(sp("centroid_motion_hz"), 1)}
    ok_loud = verdict["rms_spread_db"] <= 1.0
    moves = (verdict["centroid_spread_hz"] > 40 or verdict["comb_spread_db"] > 1.5
             or verdict["motion_spread_hz"] > 15)
    verdict["verdict"] = ("colour" if ok_loud and moves else
                          "A FADER — it moves loudness" if not ok_loud and moves else
                          "moves nothing measurable" if ok_loud else
                          "A FADER and nothing else")
    if not quiet:
        print(f"  -> loudness {verdict['rms_spread_db']:+.2f} dB, colour "
              f"{verdict['centroid_spread_hz']:.0f} Hz, comb "
              f"{verdict['comb_spread_db']:.1f} dB, motion "
              f"{verdict['motion_spread_hz']:.0f} Hz  ==  {verdict['verdict']}")
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
    """
    names = sorted(axes(body))
    vals = [i / (steps - 1) for i in range(steps)] if steps > 1 else [0.5]
    rows, fails = [], []
    for combo in itertools.product(vals, repeat=len(names)):
        b = body
        for k, v in zip(names, combo):
            b = with_axis(b, k, v)
        y, err = M.render(b, dur=4.0, freq=freq)
        corner = dict(zip(names, combo))
        if y is None:
            fails.append(("renders", corner, err))
            continue
        r = M.measure(y, freq)
        rows.append((corner, r))
        if not r["moves"]:
            fails.append(("moves", corner,
                          f"travel {r['centroid_motion_hz']} Hz at coherence "
                          f"{r['motion_coherence']} — variance, not motion"))
    if not rows:
        return False, fails, {}
    lv = [r["rms_db"] for _, r in rows]
    pk = max(((r["peak_p999"], c) for c, r in rows), key=lambda t: t[0])
    stats = {"corners": len(rows), "loudness_spread_db": round(max(lv) - min(lv), 2),
             "worst_peak": pk}
    if stats["loudness_spread_db"] > 1.0:
        fails.append(("one loudness", pk[1],
                      f"{stats['loudness_spread_db']:.2f} dB across the corners"))
    if pk[0] > 2.97:
        fails.append(("headroom", pk[1],
                      f"p99.9 {pk[0]:.2f}; the host clips a body above 2.97"))
    reg = []
    for f in registers:
        yr, er = M.render(body, dur=4.0, freq=float(f))
        if yr is None:
            fails.append(("renders", {"register": f}, er))
        else:
            reg.append((f, M.rms_db(yr)))
    if len(reg) >= 2:
        v = [x for _, x in reg]
        stats["register_tilt_db"] = round(max(v) - min(v), 2)
        stats["register"] = [(f, round(x, 2)) for f, x in reg]
        if stats["register_tilt_db"] > 3.0:
            fails.append(("one loudness at every register", {},
                          f"{stats['register_tilt_db']:.2f} dB across "
                          f"{registers[0]}-{registers[-1]} Hz"))
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
          ", ".join(f"{n}={v:g}" for n, (v, _) in sorted(declared.items())))

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
        print(f"\ngate: {st.get('corners', 0)} corners, loudness spread "
              f"{st.get('loudness_spread_db')} dB, register tilt "
              f"{st.get('register_tilt_db')} dB, worst p99.9 "
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
