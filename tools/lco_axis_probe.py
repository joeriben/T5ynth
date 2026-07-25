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
    .venv/bin/python tools/lco_axis_probe.py --census
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


# How many shipped entries move at EVERY register, and how many do not. Printed at
# every gate run, so it has to be a measurement and not a memory: it was written as
# 32/25 and went stale inside the same batch that wrote it, when `2bfac805` gave
# `mbira` a second mechanism and the comment kept naming mbira as an entry that stands
# still. Re-derive with `--census`, which recomputes the split over the whole lexicon
# and says outright whether this line is out of date.
_MOVE_CENSUS = (34, 24, "2026-07-25")
_MOVE_REGISTERS = (55.0, 110.0, 220.0, 440.0, 880.0, 1760.0)


def census():
    """Recount which shipped entries move at every register. ~340 renders."""
    import json
    lex = json.loads((REPO / "backend" / "dco_lexicon.json").read_text())
    still = {}
    for e in lex["techniques"]:
        bad = []
        for f in _MOVE_REGISTERS:
            y, err = M.render(e["code"], dur=4.0, freq=f)
            if y is None:
                bad.append((f, f"render: {err}"))
                continue
            r = M.measure(y, f)
            if not r["moves"]:
                bad.append((f, f"{r['centroid_motion_hz']} Hz at "
                               f"{r['motion_coherence']}"))
        if bad:
            still[e["key"]] = bad
    n = len(lex["techniques"])
    print(f"{n - len(still)} of {n} entries move at all six registers, "
          f"{len(still)} do not")
    for k, bad in still.items():
        print(f"  {k:16s} " + "  ".join(f"{f:.0f}:{d}" for f, d in bad))
    want = (n - len(still), len(still))
    if want != _MOVE_CENSUS[:2]:
        print(f"\n  STALE  _MOVE_CENSUS says {_MOVE_CENSUS[0]}/{_MOVE_CENSUS[1]} "
              f"(measured {_MOVE_CENSUS[2]}); it is now {want[0]}/{want[1]}. "
              f"Update it and every comment that quotes it.")
        return 1
    print(f"\n  _MOVE_CENSUS is current at {want[0]}/{want[1]}")
    return 0


TEXTURE = re.compile(r"^\s*;\s*movement\s*:\s*texture\b", re.I)


def declares_texture(body):
    """Does this body declare itself an EVENT TEXTURE rather than a sustained tone?

    A declaration, not a measurement, and deliberately so. BJ, 2026-07-25, on the
    movement gate: a findable RATE in the colour track "is the right definition for a
    sustained tonal body and may be the wrong one for a stochastic texture, where being
    alive means precisely that there is no rate to find" — *„genau. Da haben wir nicht
    an Natursounds gedacht. Hiermit freigeschaltet. Ich muss ja ohnehin alle neuen
    instrumente reviewen."*

    So for a declared texture this gate stops FAILING on `moves` and starts REPORTING
    it. What it does not do is substitute a different number, because there is none:
    `lco_measure`'s two null constants record that a stationary narrowband noise bed —
    a body that does nothing whatever — travels up to 1005 cents and crests at 14.55 dB,
    while a real sweep travels 959. The populations overlap. Coherence is the only
    statistic that separates movement from variance, and dropping it leaves nothing to
    put in its place, so the liveness of this class is BJ's ear and this flag says which
    entries are in it. Everything else the gate checks — one loudness across the cube
    and the keyboard, the peak, the drift, the pitch — still applies unchanged.

    It is a comment, so it is inert in Csound, it travels with the body into the
    lexicon, and it is greppable: `grep -c 'movement: texture' backend/dco_lexicon.json`
    is the count of entries claiming the exemption."""
    return any(TEXTURE.match(l) for l in (body or "").splitlines())


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
    registers is 162 renders, roughly five times the old gate, plus one per register
    for the tilt at the actual defaults — six, or seven when `--freq` names a pitch
    outside the register list, because that pitch is forced into the list so a gate
    run there cannot check nothing.
    """
    declared = axes(body)
    texture = declares_texture(body)
    names = sorted(declared)
    # each axis stepped across ITS OWN declared range, not 0..1
    def steps_for(n):
        lo, hi = declared[n][3]
        return ([lo + (hi - lo) * i / (steps - 1) for i in range(steps)]
                if steps > 1 else [(lo + hi) / 2])
    # `freq` is FORCED into the register set. It is the pitch at which the movement
    # rule and the corner-loudness rule are read, and both were guarded by `f ==
    # freq` — so at any --freq outside the default list the gate evaluated `moves`
    # zero times, left `loudness_spread_db` at None, skipped that check too, and
    # printed PASS. Measured: the same body at --freq 220 failed three corners and
    # at --freq 330 passed, over the identical 162 renders.
    regs = sorted(set(float(f) for f in registers) | {float(freq)})
    rows, fails, elsewhere = [], [], []
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
            # Movement is measured at every register and GATED at `freq`. Not gated
            # everywhere, on measured grounds: of the 58 shipped entries at their
            # defaults, only 34 satisfy `moves` at all six registers (`--census`;
            # `_MOVE_CENSUS` above). The rest are mostly the three classes `moves`
            # itself documents as beyond it — `string` travels 2881-3402 Hz at
            # coherence 0.14-0.25 because a decaying high-Q bank's late window is a
            # noise floor, `cymbal`, `drum_head` and `pink_noise` likewise — plus
            # fixed-formant bodies (`voice`, `saw`, `sub_sine`) whose travel shrinks in
            # cents as the note rises past their formants. Gating everywhere would
            # condemn 24 long-shipped entries in the
            # name of a meter limit, which is a change of standard and BJ's to make,
            # not a defect to fix. It is REPORTED so the register dependence is
            # visible: the same body used to pass or fail purely on which registers
            # were in the list, and at a `--freq` outside the list nothing was checked
            # at all.
            r = M.measure(y, f)
            # A declared event texture is reported, never failed — see
            # `declares_texture` for whose decision that is and why no second
            # measurement replaces it. Its reference-register corners go into neither
            # list: `stats["texture"]` counts them, and putting them in `elsewhere`
            # too made the printed tally read "52 of 45".
            if not r["moves"] and not (texture and f == freq):
                where = ("moves" if f == freq else "moves elsewhere")
                (fails if f == freq else elsewhere).append(
                    (where, dict(corner, **{"Hz": f}),
                     f"travel {r['centroid_motion_hz']} Hz at coherence "
                     f"{r['motion_coherence']} — variance, not motion"))
            rows.append((dict(corner, **{"Hz": f}), r))
    if not rows:
        return False, fails, {}
    at_freq = [(c, r) for c, r in rows if c["Hz"] == freq]
    lv = [r["rms_db"] for _, r in at_freq]
    pk = max(((r["peak_p999"], c) for c, r in rows), key=lambda t: t[0])
    # Loudness travelling INSIDE a note, reported and not gated — the span alone cannot
    # tell a defect from a deliberate beat, and 15 shipped entries read above 6 dB
    # somewhere in the register range because a detune beat IS loudness moving within the
    # note: `supersaw` 14.98 dB, `strings` 13.95, `fm_bell`'s protected doublet 9.76,
    # `free_reed` 8.20, all at 220 Hz. None of those four has a stochastic source, so
    # `loudness_is_the_body` returns 1.0 for each and the movement is theirs by
    # construction — which is exactly why the span is printed with that verdict beside
    # it. Whether a beat counts against §4 is the question `LCO_PARAM_AUDIT.md` records
    # as BJ's. What this DOES do is make the class visible: `overtone_voice`'s 6.08 dB
    # step train was invisible to
    # every number this gate printed, because each corner's MEAN was steady.
    tv = max(((r["loudness_travel_db"] or 0.0, c) for c, r in rows), key=lambda t: t[0])
    # The DRIFT is gated, and unlike the span above it is not ambiguous. §4 names this
    # class outright — "a tone that fades to silence on its own is not" a spectrum
    # source — and `loudness_travel` is blind to it by construction: it detrends the dB
    # envelope, so a straight line is removed exactly. A body that SWELLS 20 dB inside a
    # held note read 0.00 dB of travel and passed this gate with every printed number
    # clean. A struck body is the one honest exception: a decay is a fade and is meant
    # to be, so a body whose `sustain` says it is decaying is judged on its travel only.
    dr = max(((abs(r["loudness_drift_db"] or 0.0), r["sustain"], c) for c, r in rows),
             key=lambda t: t[0])
    stats = {"corners": len(at_freq), "renders": len(rows),
             "loudness_spread_db": round(max(lv) - min(lv), 2) if lv else None,
             "worst_peak": pk, "worst_travel": tv, "worst_drift": dr,
             "moves_elsewhere": elsewhere}
    if texture:
        # What a declared texture reads, printed against the null so the numbers cannot
        # be mistaken for a verdict. The count of corners `moves` would have failed is
        # the honest headline: this body is exempt from that question, not passing it.
        tsp = [r["centroid_travel_cents"] for _, r in at_freq]
        stats["texture"] = {
            "would_fail_moves": sum(1 for _, r in at_freq if not r["moves"]),
            "of_corners": len(at_freq),
            "travel_cents": (min(tsp), max(tsp)) if tsp else None,
            "stationary_null_cents": M.STATIONARY_SPAN_NULL_CENTS,
            "crest_db": (min(r["crest_db"] for _, r in at_freq),
                         max(r["crest_db"] for _, r in at_freq)),
            "crest_null_db": M.STATIONARY_CREST_NULL_DB}
    sustained = [(abs(r["loudness_drift_db"] or 0.0), r["loudness_drift_db"], c)
                 for c, r in rows if r["sustain"] > 0.25]
    if sustained:
        wd = max(sustained, key=lambda t: t[0])
        stats["worst_sustained_drift"] = wd
        if wd[0] > 6.0:
            # A candidate failure is CONFIRMED on a note four times as long before it
            # counts, because one slow cycle caught inside a short window is a straight
            # line: `crackle` reads -6.33 dB over six seconds and +0.12 over twelve, and
            # `supersaw`'s slow detune beat reads +6.26 and then +2.49. A real trend goes
            # the other way and grows — an asked swell read +1.71 dB at six seconds and
            # +16.39 at forty-eight. Without this the gate condemned two shipped entries
            # for the length of its own window.
            corner = {k: v for k, v in wd[2].items() if k != "Hz"}
            b = body
            for k, v in corner.items():
                b = with_axis(b, k, v)
            y, err = M.render(b, dur=16.0, freq=wd[2]["Hz"])
            long = None if y is None else M.loudness_drift_db(y)
            stats["worst_drift_confirmed"] = long
            if long is not None and abs(long) > 6.0 and (long > 0) == (wd[1] > 0):
                fails.append(("one loudness inside the note", wd[2],
                              f"the level drifts {wd[1]:+.2f} dB from one end of a held "
                              f"note to the other and {long:+.2f} dB over a note four "
                              f"times as long, so it is a trend and not a slow cycle, "
                              f"and the body is not a decaying one"))
    if stats["loudness_spread_db"] is not None and stats["loudness_spread_db"] > 1.0:
        fails.append(("one loudness", pk[1],
                      f"{stats['loudness_spread_db']:.2f} dB across the corners"))
    if pk[0] > 2.97:
        fails.append(("headroom", pk[1],
                      f"p99.9 {pk[0]:.2f}; the host clips a body above 2.97"))
    # The register tilt at the DEFAULTS, which is the number the notes quote — so it
    # is measured on the body AS WRITTEN, at its own default values, and not on the
    # nearest grid corner. The defaults need not sit on a 3-step grid at all:
    # `overtone_voice` defaults to select 0.45, focus 0.6, press 0.45, none of them on
    # the grid, and the nearest corner (0.5, 0.5, 0.5) reads 0.06 dB where the body
    # itself reads 0.60. Its note shipped the 0.06 — a figure off by ten times, about
    # a body no one had measured. Six extra renders buy the number its own name.
    base = []
    for f in regs:
        y, err = M.render(body, dur=4.0, freq=f)
        if y is not None:
            base.append((f, M.rms_db(y)))
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
    ap.add_argument("--body", help="candidate body file")
    ap.add_argument("--census", action="store_true",
                    help="recount which shipped entries move at every register")
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
    if args.census:
        return census()
    if not args.body:
        ap.error("--body is required (or --census)")

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
        # renders/corners, NOT a remembered register count: it is the honest number
        # only while every corner rendered. When one did not, `rows` shrank and the
        # division silently reported a wrong register count as if nothing were missing,
        # so say so instead.
        _c, _r = st.get('corners', 0), st.get('renders', 0)
        _regs = (f"{_r // _c} registers" if _c and _r % _c == 0
                 else f"{_r} renders over {_c} corners — some corner did not render")
        print(f"\ngate: {_c} corners x {_regs} = "
              f"{_r} renders, loudness spread "
              f"{st.get('loudness_spread_db')} dB, register tilt "
              f"{st.get('register_tilt_db')} dB, the two crossed "
              f"{st.get('cross_spread_db')} dB, worst p99.9 "
              f"{st['worst_peak'][0]:.2f} at {st['worst_peak'][1]}"
              if st else "\ngate: nothing rendered")
        if st.get("worst_travel"):
            # Reported with its verdict, because the span alone was meaningless: a narrow
            # filter fed noise has a fluctuating envelope with nothing modulating it, and
            # a bare "15 dB" was a statement about bandwidth. `loudness_is_the_body`
            # re-renders with other seeds and says whether the body did it.
            who = M.loudness_is_the_body(body, freq=args.freq)
            verdict = ("not measured — nothing here can be reseeded" if who is None
                       else f"THE BODY does this ({who:+.3f})" if who >= M._BODY_MIN
                       else f"the noise it is made of, not the body ({who:+.3f})")
            print(f"  note  worst loudness travel INSIDE a note "
                  f"{st['worst_travel'][0]:.2f} dB at {st['worst_travel'][1]}: "
                  f"{verdict}")
        if st.get("worst_sustained_drift"):
            wd = st["worst_sustained_drift"]
            cf = st.get("worst_drift_confirmed")
            if wd[0] > 6.0 and cf is not None and not (
                    abs(cf) > 6.0 and (cf > 0) == (wd[1] > 0)):
                print(f"  note  the level drifts {wd[1]:+.2f} dB end to end at {wd[2]}, "
                      f"but only {cf:+.2f} dB over a note four times as long — one slow "
                      f"cycle, not a trend")
            elif wd[0] <= 6.0:
                print(f"  note  worst level drift end to end {wd[1]:+.2f} dB at {wd[2]}, "
                      f"on a body that is not decaying")
        if st.get("texture"):
            t = st["texture"]
            print(f"  TEXTURE  this body declares itself an event texture, so `moves` is "
                  f"REPORTED and not gated: it would have failed at {t['would_fail_moves']} "
                  f"of {t['of_corners']} corners.")
            print(f"           colour travel {t['travel_cents'][0]:.0f}.."
                  f"{t['travel_cents'][1]:.0f} cents against a "
                  f"{t['stationary_null_cents']:.0f}-cent stationary null, crest "
                  f"{t['crest_db'][0]:.2f}..{t['crest_db'][1]:.2f} dB against "
                  f"{t['crest_null_db']:.2f} — these are CONTEXT, not a pass: a bed that "
                  f"does nothing reads the same, and a real sweep reads less. Liveness "
                  f"in this class is BJ's ear (see `declares_texture`).")
        for rule, corner, detail in fails:
            print(f"  FAIL  {rule}: {corner}  {detail}")
        # Reported, not gated — see `gate`. Grouped by register, because the shape of
        # the list is the information: "everything at 1760 Hz" is a body whose
        # movement lives below its formants, and that is a different fact from a
        # scattered handful.
        el = st.get("moves_elsewhere") or []
        if el:
            by_hz = {}
            for _rule, corner, _d in el:
                by_hz[corner["Hz"]] = by_hz.get(corner["Hz"], 0) + 1
            print(f"  note  {len(el)} of {st['renders'] - st['corners']} corner-renders "
                  f"away from {args.freq:.0f} Hz do not move: "
                  + ", ".join(f"{n} at {f:.0f} Hz" for f, n in sorted(by_hz.items()))
                  + f" (reported, not gated: {_MOVE_CENSUS[1]} of the "
                    f"{_MOVE_CENSUS[0] + _MOVE_CENSUS[1]} shipped "
                    f"entries are in the same position at some register, measured "
                    f"{_MOVE_CENSUS[2]} — re-derive with --census)")
        print(("  PASS  every corner renders and holds one loudness; movement is this "
               "class's declared exemption and stands on BJ's ear, not on this run"
               if st.get("texture") else
               "  PASS  every corner renders, moves, and holds one loudness")
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
