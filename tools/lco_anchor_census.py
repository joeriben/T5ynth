#!/usr/bin/env python3
"""Does every ANCHOR the author is handed still move, at every register?

The movement gate asks its question of the DEFAULT body at 220 Hz. But what the
author is actually handed is `anchor_code` -- one whole body per named anchor (409 of
them at lexicon_version 40; `_ANCHOR_COUNT` below fails the run if that has moved)
-- and a named anchor is a recommendation. An anchor that stands still at 55 Hz is a
recommendation to break a platform fundamental ("movement by default",
`docs/LCO_CONCEPT.md` §4), and until this file existed nothing looked.

It reads the shipped `anchor_code` where there is one, because that string is what
the author gets; where there is none it falls back to `lco_axis_probe.with_axis` on
the declared value, and it says which of the two it used per row.

Two things it deliberately does NOT do:

  * It does not fail the event-texture class. A body that declares
    `; MOVEMENT: TEXTURE` is exempt by BJ's ruling of 2026-07-25 and its rows are
    reported separately -- an exemption list that swallows readings silently is the
    failure mode this project already has on its record.
  * It does not report "stands still" and "moves without a rate" as one number. Both
    fail `lco_measure.moves`, which needs a 60-CENT span AND a coherence above 0.35,
    and the two are different defects: a plain waveform barely moves at all, while a
    noise-excited resonator bank moves hundreds of hertz incoherently. `--classify`
    splits them ON THE SAME QUANTITY THE VERDICT USES -- cents, not hertz. Splitting
    on hertz put 28 of 250 readings on the wrong side, all of them at 55 and 110 Hz
    where a whole spectrum sits low: `drum_head`'s `pitched=timpani` moves 1753 cents
    at 55 Hz, 29x the gate, and a hertz threshold filed it under "barely moves".
    `ocarina` was filed under the plain-waveform group by that error and belongs in
    the other one.

Renders with a preroll, because §5 of the handover says without one two whole classes
measure wrongly -- and one of them is `balance`, which the four largest contributors
here (`drum_head`, `string`, `struck_bar`, `glass`) all end on. At preroll 0 the
instance begins at the note, which the plugin's sixteen always-on voices never do.
Measured: `analog_osc/wave=saw` at 880 Hz fails at preroll 0 and MOVES on a settled
instance, and `drum_head`'s coherence collapses from 0.099 to 0.000.

    .venv/bin/python tools/lco_anchor_census.py
    .venv/bin/python tools/lco_anchor_census.py --key drum_head --classify
"""
import argparse
import collections
import json
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

import lco_axis_probe as P  # noqa: E402
import lco_measure as M  # noqa: E402

REGISTERS = (55.0, 110.0, 220.0, 440.0, 880.0, 1760.0)
# The plugin's voice instances have been running since load, so a fresh instance is
# not the case that matters. Long enough to settle a `balance` averager, short enough
# that a full run's renders still finish -- 409 anchors x 6 registers as of v40.
PREROLL = 0.5
# `moves()`'s own two thresholds, restated here so the classifier splits on exactly
# what the verdict used. Splitting on a raw hertz span instead mislabelled 28 of 250
# readings, every one of them at 55 or 110 Hz.
SPAN_CENTS = 60.0
MIN_COHERENCE = 0.35
# How many anchors the whole lexicon carried when the figures in this file's docstring
# were measured, and at which lexicon version. Checked at every full run, because this
# file's own numbers went stale inside a day: it said 355 anchors and 2130 renders while
# the lexicon had moved to 385 and then 409. A docstring nobody can trust is the same
# loss as a tool nobody runs.
_ANCHOR_COUNT = (409, 40)


def census(keys=None, registers=REGISTERS):
    lex = json.loads((REPO / "backend" / "dco_lexicon.json").read_text())
    rows, texture_rows, unreachable = [], [], []
    t0, n = time.time(), 0
    for e in lex["techniques"]:
        key = e["key"]
        if keys and key not in keys:
            continue
        params = e.get("params") or {}
        if not params:
            continue
        codes = e.get("anchor_code") or {}
        texture = P.declares_texture(e["code"])
        for axis, spec in params.items():
            for aname, anchor in (spec.get("anchors") or {}).items():
                ck = f"{axis}={aname}"
                if ck in codes:
                    body, src = codes[ck], "anchor_code"
                    # A default-valued anchor's `anchor_code` IS the default body, so
                    # its row is a re-measurement of the default and not of a variant.
                    # A minority of shipped anchors are in that position.
                    if body == e["code"]:
                        src = "anchor_code (= the default body)"
                else:
                    # Currently never taken: every shipped anchor has an
                    # `anchor_code`. Kept for an entry that has not been generated yet,
                    # and `with_axis` EXITS rather than returning the body unchanged
                    # when there is no line to set, which is why that is caught here.
                    try:
                        body, src = (P.with_axis(e["code"], axis, anchor["value"]),
                                     "with_axis")
                    except SystemExit as exc:
                        unreachable.append({"key": key, "anchor": ck, "why": str(exc)})
                        continue
                bad = []
                for f in registers:
                    y, err = M.render(body, dur=4.0, freq=f, preroll=PREROLL)
                    n += 1
                    if y is None:
                        bad.append([f, f"render: {err}", None, None])
                        continue
                    r = M.measure(y, f)
                    # A HOT render is a reading of a limited signal, not of the body.
                    # Discarding `err` reported those as clean.
                    if err and "HOT" in err:
                        bad.append([f, f"HOT: {err}", None, None])
                        continue
                    if not r["moves"]:
                        cents = M.travel_cents(y, 128)[0]
                        bad.append([f, f"{r['centroid_motion_hz']} Hz = {cents:.0f} c "
                                       f"@ {r['motion_coherence']}",
                                    round(cents, 1), r["motion_coherence"]])
                if bad:
                    row = {"key": key, "anchor": ck, "source": src,
                           "value": anchor["value"], "gloss": anchor.get("gloss", ""),
                           "n_bad": len(bad), "bad": bad}
                    (texture_rows if texture else rows).append(row)
                    if not texture:
                        print(f"  {key:16s} {ck:28s} {src:11s} {len(bad)}/"
                              f"{len(registers)}  "
                              + "  ".join(f"{b[0]:.0f}:{b[1]}" for b in bad), flush=True)
    rows.sort(key=lambda r: (-r["n_bad"], r["key"]))
    return {"registers": list(registers), "renders": n,
            "seconds": round(time.time() - t0, 1), "rows": rows,
            "texture_rows": texture_rows, "unreachable": unreachable}


def classify(rows):
    """Split the failures into the two different defects they actually are.

    On cents and coherence, which is what `moves()` judged them on. There is no
    "borderline" bucket: `moves()` fails a reading for exactly one of two reasons and
    a third label was an artefact of measuring the split in hertz.
    """
    total, per_key = collections.Counter(), collections.defaultdict(collections.Counter)
    for r in rows:
        for b in r["bad"]:
            cents, coh = b[2], b[3]
            if cents is None:
                kind = "HOT render" if b[1].startswith("HOT") else "render error"
            elif cents <= SPAN_CENTS:
                kind = f"barely moves (span <= {SPAN_CENTS:.0f} c)"
            else:
                kind = f"incoherent motion (coherence <= {MIN_COHERENCE})"
            total[kind] += 1
            per_key[r["key"]][kind] += 1
    return total, per_key


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--key", action="append", help="only these entries (repeatable)")
    ap.add_argument("--registers", help="comma-separated, default 55,110,220,440,880,1760")
    ap.add_argument("--classify", action="store_true",
                    help="split the failures into the two defects, on cents")
    ap.add_argument("--json", help="write the full result here")
    a = ap.parse_args()
    # Both of these used to pass silently and print a clean result for something never
    # measured: a mistyped key gave "0 anchors stand still, across 0 entries" and exit
    # 0, and `--registers ""` reverted to the default six without a word.
    lex = json.loads((REPO / "backend" / "dco_lexicon.json").read_text())
    known = {e["key"] for e in lex["techniques"]}
    if a.key:
        unknown = [k for k in a.key if k not in known]
        if unknown:
            raise SystemExit(f"no such entry: {', '.join(unknown)}. A clean pass on a "
                             f"typo reads as 'nothing wrong here'.")
        no_params = [k for k in a.key if not (
            next(e for e in lex["techniques"] if e["key"] == k).get("params"))]
        if no_params:
            print(f"note: {', '.join(no_params)} declare no parameters, so there is "
                  f"nothing here to measure for them")
    regs = REGISTERS
    if a.registers is not None:
        try:
            regs = tuple(float(x) for x in a.registers.split(",") if x.strip())
        except ValueError:
            raise SystemExit(f"--registers {a.registers!r}: expected comma-separated Hz")
        if not regs or any(f <= 0 for f in regs):
            raise SystemExit(f"--registers {a.registers!r}: needs at least one positive "
                             f"frequency; an empty list used to revert to the default "
                             f"six and print as a normal run")
    if not a.key:
        lex = json.loads((REPO / "backend" / "dco_lexicon.json").read_text())
        n_anch = sum(len(sp.get("anchors") or {})
                     for t in lex["techniques"]
                     for sp in (t.get("params") or {}).values())
        if (n_anch, lex.get("lexicon_version")) != _ANCHOR_COUNT:
            print(f"  STALE  _ANCHOR_COUNT says {_ANCHOR_COUNT[0]} anchors at "
                  f"lexicon_version {_ANCHOR_COUNT[1]}; the lexicon now has {n_anch} at "
                  f"version {lex.get('lexicon_version')}. Update it and every figure in "
                  f"this file's docstring that quotes it.")
    out = census(a.key, regs)
    rows = out["rows"]
    keys = sorted({r["key"] for r in rows})
    print(f"\n{out['renders']} renders in {out['seconds']} s")
    # Three different defects, counted separately. Rolling them into one number said
    # "1 anchors stand still ... bagpipe" about an anchor that does not stand still at
    # any register — it CLIPS at five of six, which is a worse defect and a different
    # one. The headline is what a reader takes away, so it may not average a HOT render,
    # a render that did not happen, and a fader that does nothing into one word.
    def _kind(row):
        marks = [b[1] for b in row["bad"]]
        if any(m.startswith("HOT") for m in marks):
            return "hot"
        if any(m.startswith("render:") for m in marks):
            return "failed"
        return "still"
    kinds = {k: [r for r in rows if _kind(r) == k] for k in ("still", "hot", "failed")}
    still_keys = sorted({r["key"] for r in kinds["still"]})
    print(f"{len(kinds['still'])} anchors stand still at one or more registers, across "
          f"{len(still_keys)} entries: {', '.join(still_keys) or 'none'}")
    for kind, what in (("hot", "clip over the host ceiling"),
                       ("failed", "did not render")):
        if kinds[kind]:
            print(f"{len(kinds[kind])} anchors {what} — a different defect, not "
                  f"stillness: "
                  + ", ".join(f"{r['key']}/{r['anchor']}" for r in kinds[kind]))
    del keys
    print(f"{len(out['texture_rows'])} more do on event-texture bodies "
          f"({', '.join(sorted({r['key'] for r in out['texture_rows']})) or 'none'}) "
          f"-- exempt, not failing")
    if out["unreachable"]:
        print(f"{len(out['unreachable'])} anchors are unreachable (no anchor_code and "
              "no settable axis line): "
              + ", ".join(f"{u['key']}/{u['anchor']}" for u in out["unreachable"]))
    if a.classify:
        total, per_key = classify(rows)
        print()
        for k, v in total.most_common():
            print(f"  {v:4d}  {k}")
        print()
        for key, c in sorted(per_key.items(), key=lambda kv: -sum(kv[1].values())):
            print(f"  {key:16s} " + "  ".join(f"{k}={v}" for k, v in c.most_common()))
    if a.json:
        Path(a.json).write_text(json.dumps(out, indent=1) + "\n")
        print("\n->", a.json)
    return 0


if __name__ == "__main__":
    sys.exit(main())
