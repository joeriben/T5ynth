#!/usr/bin/env python3
"""Audit every library instrument against the parametrisation rules.

The rules are not invented here. They are `docs/LCO_CONCEPT.md` §3 (what an
instrument IS) and §4 (the platform invariants), plus the two failure modes §7
records as already committed on this project. This file only turns them into
checks that fail out loud:

  STRUCTURE — read from `backend/lco_library.json`, no render
    S1  the entry carries parameters at all                          (§3)
    S2  every axis has a range, a default inside it, >=2 named
        anchors, and a perceptual gloss on each anchor               (§3.2, §3.3)
    S3  every axis carries a `note` saying what it does to the sound (§3.2)
    S4  every anchor value is inside the declared range, and the
        anchor_code variants name axes that exist

  RENDERED — through `tools/lco_measure.py`'s calibrated meter
    M1  the body renders clean at 55 / 220 / 1760 Hz — not silent,
        not so hot the host's clip would act on it
    M2  the sound TRACKS the keyboard across those registers        (§4 pitch)
    M3  a held note STANDS: no self-decay                           (§4 spectrum source)
    M4  the sound moves                                             (§4 movement by default)
    M5  an axis is a COLOUR control, not a volume control: the RMS
        spread across its anchors stays small                       (§4, §7.8)
    M6  an axis actually DOES something: colour, comb contrast or
        definiteness has to move across its anchors
    M7  the KEYBOARD is not a volume control either: loudness must
        not tilt across the register                          (§5 on `vibes`)

M5 is the one that has already shipped broken once (instrument 3's `damping` was
a -4 dB fader wearing a colour name), and M6 is its mirror: a control that moves
nothing is a label, not a parameter.

Two things this file deliberately does NOT do, both because the first draft did
them and both readings were wrong — the meter is an instrument and needed its own
calibration here too (`LCO_CONCEPT.md` §7.7):

  - **M2 is TRACKING, not absolute pitch.** An inharmonic bell has no single
    period, so a period-based f0 locks onto the set's quasi-period and reads
    -2773 cents against the played note. `fm_bell` is not mistuned; it is a bell.
    What §4 actually requires is that the sound follow `kfreq` — so the check
    renders 110 -> 880 Hz as a glide and CORRELATES the spectrum against the note
    and against a fixed reference, via `lco_measure.tracks`. Not "renders an octave
    and asks whether the pitch doubled", which is what this paragraph said for
    weeks: `tracks`' own docstring, quoted eight lines below, says outright that
    neither `f0` nor the centroid nor a lag search could do this job. This is the
    check that catches what §4 is aimed at — an opcode inventing its own register —
    and it is also the check that settles an inharmonic body, where the loudest
    partial and the period finder both say the wrong thing. Two entries were nearly
    rebuilt, and one nearly dropped, on a `f0` reading that this check answers.
  - **A HOT flag on a randomly-excited body is re-measured before it counts.**
    `render()`'s guard reads the true peak, and the peak of a dust/rand process
    is itself random (HANDOVER §5: the same body measured 2.89 and 0.79). So a
    HOT body is rendered three times and judged on the p99.9 — a systematic 3x
    is a finding, an occasional transient the host's soft clip catches is not.

The audit renders the DEFAULT body for M1-M4 and the `anchor_code` variants for
M5/M6 — so M5/M6 can only be judged on axes that have anchor_code. An axis
without it is reported as UNMEASURABLE rather than passing quietly.

    .venv/bin/python tools/lco_param_audit.py                 # everything
    .venv/bin/python tools/lco_param_audit.py --key string    # one entry
    .venv/bin/python tools/lco_param_audit.py --out audit.json
"""
import argparse
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

import lco_measure as M  # noqa: E402
import lco_axis_probe as _P  # noqa: E402

LIB = REPO / "backend" / "lco_library.json"

# The registers the audit sweeps. 55 Hz and 1760 Hz are where this project has
# actually found instruments falling over (fm_ep's tine folds above ~1553 Hz,
# vibes tilts 15 dB below 440, the modal bank swings at 20 Hz).
REGISTERS = (55.0, 110.0, 220.0, 440.0, 880.0, 1760.0)
# M7 is judged over the musically central three octaves, so a structural ceiling
# at the very top (fm_ep's tine folding above ~1553 Hz) is reported as its own
# finding rather than swamping the tilt reading.
TILT_REGISTERS = (110.0, 220.0, 440.0, 880.0)
TILT_OK_DB = 3.0

# M5: how much loudness an axis may move over its full travel. Not a platform
# rule about a note's life (HANDOVER §5 corrects that misreading) — a rule about
# a PARAMETER: instrument 3 was re-levelled until its worst full-travel swing was
# 0.24 dB at >=110 Hz, so 1.0 dB is a generous pass and 2.0 dB is a fader.
LOUD_OK_DB = 1.0
LOUD_BAD_DB = 2.0

# M6: an axis has to move one of the three things a spectrum source can move.
MOVES_COLOUR_HZ = 40.0    # spectral centroid
MOVES_COMB_DB = 1.5       # how sharply it rings
MOVES_MOTION_HZ = 15.0    # how much the colour travels within the note
# A rate control's own signature. 0.5 Hz because a musette's whole range is 1.0 to
# 4.0 Hz and half a hertz of beat is plainly audible; 0.05 of depth because the beat
# meter reads an asked 0.10 back as 0.100, so 0.05 is well clear of its noise.
MOVES_BEAT_HZ = 0.5
MOVES_BEAT_DEPTH = 0.05


# Properties that are the instrument, not a defect in it. Each one names WHY, and
# every entry here still PRINTS — marked `declared` — because the failure mode of
# an exception list is that it turns into a place to put inconvenient readings.
# A gate that certified the broken state as correct is failure mode §7.4 on this
# project's record; a gate nobody reads because it cries wolf is the same loss by
# the other route.
DECLARED = {
    "sine": {"M4": "the reference tone. A single partial with nothing to move is "
                   "what this entry IS; movement-by-default is answered by every "
                   "other entry and by what the author writes around this one."},
    # The nature beds. Unpitched by design — LCO_CONCEPT.md §6 calls them "the
    # route for the natural-sound and animal part of the library", and each `why`
    # states in as many words that the keyboard does not change them. Recovered
    # from the parked emitter, where they behaved identically.
    **{k: {"M2": "an unpitched bed: it has no pitch to follow the keyboard with, "
                 "and its `why` says so. Pitch has to come from what it is "
                 "filtered, ring-modulated or layered with."}
       for k in ("noise", "pink_noise", "wind", "rain", "surf", "thunder",
                 "hiss", "crackle")},
    # The struck instruments. A self-decay stopped being a violation on
    # 2026-07-20, when BJ released these three explicitly: "a self-decay is now a
    # CHOICE THE PROMPT MAKES" (LCO_CONCEPT.md §4). Each names its standing-tone
    # counterpart in its own `why`, so the choice is visible to the author.
    **{k: {"M3": "it is a struck instrument and dying is what it does. Released "
                 "by BJ 2026-07-20; the `why` names the standing-tone "
                 "counterpart for the other reading."}
       for k in ("rhodes", "wurlitzer", "vibraphone")},
    "vibraphone": {
        "M3": "it is a struck instrument and dying is what it does. Released by "
              "BJ 2026-07-20; `struck_bar` is the standing-tone counterpart.",
        "M7": "the residual after the model's OWN 15 dB tilt is compensated. §5 "
              "measured `vibes` at 3.52 peak at 40 Hz down to 0.58 at 440 and "
              "fitted the 0.73 exponent the code applies; +0.99 dB/octave is what "
              "is left, and a single polynomial over the whole range fits 5-7 dB "
              "worse because this is two regimes.",
    },
}


def load():
    return json.loads(LIB.read_text())


# The event-texture class, BJ 2026-07-25 („Hiermit freigeschaltet"). It is NOT an entry
# in DECLARED above, because it is not a per-entry exception: it is a property a body
# asserts in its own code and `LCO_CONCEPT.md` §4 codifies. Read from the body, so that
# adding or removing the declaration cannot leave this list behind.
#
# Until this existed the project had two movement gates giving two answers on the same
# invariant: `lco_axis_probe.gate()` exempted the five declaring entries and this file
# printed them as real undeclared M4 failures. The one that cries wolf is the one that
# stops being read.
def texture_declared(code):
    return _P.declares_texture(code or "")


TEXTURE_REASON = ("the event-texture class: this body declares `; MOVEMENT: TEXTURE` and "
                  "§4 reports its movement reading instead of failing on it "
                  "(BJ 2026-07-25). Its liveness is where and when its events fall, "
                  "which no statistic in `lco_measure` separates from a static bed.")


def declared_reason(key, level, code=None):
    """Why this finding is the instrument and not a defect — or None if it is a defect.

    ONE place, because the printer used to read `DECLARED[key][level]` directly and
    crashed with a KeyError the moment a declaration came from anywhere else.
    """
    reason = (DECLARED.get(key) or {}).get(level)
    if reason is None and level == "M4" and texture_declared(code):
        return TEXTURE_REASON
    return reason


def _axis_of(anchor_key):
    return anchor_key.split("=", 1)[0].strip()


def structure(inst):
    """S1-S4. Returns a list of (level, message)."""
    out = []
    params = inst.get("params") or {}
    ac = inst.get("anchor_code") or {}
    if not params:
        out.append(("S1", "no parameters at all — a fixed idiom"))
        if ac:
            out.append(("S4", f"anchor_code without params: {sorted(ac)}"))
        return out

    for name, p in sorted(params.items()):
        rng = p.get("range")
        if not (isinstance(rng, list) and len(rng) == 2):
            out.append(("S2", f"{name}: no usable range ({rng!r})"))
            rng = None
        default = p.get("default")
        if default is None:
            out.append(("S2", f"{name}: no default"))
        elif rng and not (rng[0] <= default <= rng[1]):
            out.append(("S2", f"{name}: default {default} outside range {rng}"))

        anchors = p.get("anchors") or {}
        if len(anchors) < 2:
            out.append(("S2", f"{name}: {len(anchors)} anchor(s) — an axis needs "
                              "at least two named points"))
        for aname, a in anchors.items():
            if not (a.get("gloss") or "").strip():
                out.append(("S2", f"{name}/{aname}: no perceptual gloss"))
            v = a.get("value")
            if v is None:
                out.append(("S2", f"{name}/{aname}: no value"))
            elif rng and not (rng[0] <= v <= rng[1]):
                out.append(("S4", f"{name}/{aname}: value {v} outside range {rng}"))

        if not (p.get("note") or "").strip():
            out.append(("S3", f"{name}: no note — the model is told the anchor "
                              "words but never what the axis does"))

    axes_with_code = {_axis_of(k) for k in ac}
    for a in sorted(axes_with_code - set(params)):
        out.append(("S4", f"anchor_code names axis {a!r}, which has no params entry"))
    for a in sorted(set(params) - axes_with_code):
        out.append(("S4", f"{a}: no anchor_code — the model sees the words but "
                          "never a code exemplar for this axis"))
    return out


def render_report(body, freq):
    y, err = M.render(body, dur=4.0, freq=freq)
    if y is None:
        return {"error": err}
    r = M.measure(y, freq)
    if err and err.startswith("HOT"):
        # The peak of a random-impulse process is random. Judge the level on the
        # p99.9 of three renders; keep the peaks so the reading is checkable.
        peaks, p999 = [r["peak_p999"]], [r["peak_p999"]]
        for _ in range(2):
            y2, e2 = M.render(body, dur=4.0, freq=freq)
            if y2 is None:
                continue
            peaks.append(round(float(abs(y2).max()), 3))
            p999.append(M.peak_p999(y2))
        r["hot_p999_max"] = round(max(p999), 3)
        r["hot_peaks"] = peaks
        if max(p999) * M.W.HEADROOM > 0.95:
            r["error"] = (f"HOT: p99.9 reaches {max(p999):.2f} over three renders; "
                          f"the host clips above {0.95 / M.W.HEADROOM:.2f}")
        else:
            r["note"] = (f"transient peaks only ({err}); p99.9 stays at "
                         f"{max(p999):.2f} — the host's soft clip catches these")
    elif err:
        r["error"] = err
    return r


def tracking(body, low=110.0, high=880.0):
    """M2 — does the sound follow the keyboard, three octaves apart?

    `lco_measure.tracks` is the meter, and its own docstring records why neither
    `f0` nor the spectral centroid nor a lag search could do this job. Three
    octaves rather than one, because a narrowband-noise-driven bank genuinely
    needs a long span (`LCO_CONCEPT.md` §5 trap b): read over a single octave, the
    per-render variance of `drum_head` said -205 cents of mistracking on an
    instrument whose spectrum in fact scales to within 2% over three.
    """
    ylo, elo = M.render(body, dur=4.0, freq=low)
    yhi, ehi = M.render(body, dur=4.0, freq=high)
    if ylo is None or yhi is None:
        return {"error": elo or ehi}
    out = M.tracks(ylo, yhi, low, high)
    out["span"] = f"{low:.0f}->{high:.0f} Hz"
    return out


def audit_one(inst, registers=REGISTERS, quick=False):
    key = inst["key"]
    rep = {"key": key, "structure": structure(inst), "registers": {}, "axes": {}}

    for f in registers:
        rep["registers"][str(f)] = render_report(inst["code"], f)
    rep["tracking"] = tracking(inst["code"])

    # M7 — the register tilt. Reported in dB per octave as well as total, because
    # the shape says what it is: a clean -3 dB/octave is a noise-driven
    # resonator's Q/f power law uncompensated, not a random imbalance.
    lv = [(f, rep["registers"][str(f)].get("rms_db"))
          for f in TILT_REGISTERS if str(f) in rep["registers"]]
    lv = [(f, v) for f, v in lv if v is not None]
    if len(lv) >= 2:
        import math
        octs = math.log2(lv[-1][0] / lv[0][0])
        # The same register, rendered again, so the tilt can be told from the meter's
        # own scatter. A reading that is not larger than the noise it sits in is not
        # evidence, and until this landed no M7 number on a noise bed was: two runs of
        # this audit disagreed about whether `pink_noise` has a register tilt at all.
        # Measured, the population is not what it looked like — exactly ONE body in the
        # library does not render deterministically, because `pinkish` seeds itself:
        # `pink_noise` reads a 2.15 dB tilt against 1.68 dB of scatter at 110 Hz (4.7 dB
        # over five renders at 3 s), while every other body, noise beds included, repeats
        # bit for bit and scatters 0.00 dB. So a 0.0 here is the true answer and not a
        # broken measurement, and the one case that needs the guard is the one that has
        # it. Three extra renders, not one: a pair can agree by luck.
        again = []
        for _ in range(3):
            r2 = render_report(inst["code"], lv[0][0])
            if r2.get("rms_db") is not None:
                again.append(r2["rms_db"])
        rep["tilt"] = {"rms_db": {str(f): v for f, v in lv},
                       "spread_db": round(max(v for _, v in lv)
                                          - min(v for _, v in lv), 2),
                       "db_per_octave": round((lv[-1][1] - lv[0][1]) / octs, 2)}
        if len(again) >= 2:
            rep["tilt"]["repeat_spread_db"] = round(max(again) - min(again), 2)
            rep["tilt"]["repeat_at_hz"] = lv[0][0]

    ac = inst.get("anchor_code") or {}
    params = inst.get("params") or {}
    by_axis = {}
    for label, body in ac.items():
        by_axis.setdefault(_axis_of(label), []).append((label, body))

    for axis, variants in sorted(by_axis.items()):
        rows = []
        for label, body in sorted(variants):
            r = render_report(body, 220.0)
            r["anchor"] = label
            rows.append(r)
        good = [r for r in rows if "f0" in r]
        a = {"anchors": rows}
        if len(good) >= 2:
            def spread(k):
                vs = [r[k] for r in good if r.get(k) is not None]
                return (round(max(vs) - min(vs), 2) if len(vs) >= 2 else None)
            a["rms_spread_db"] = spread("rms_db")
            a["centroid_spread_hz"] = spread("centroid")
            a["comb_spread_db"] = spread("comb_db")
            a["motion_spread_hz"] = spread("centroid_motion_hz")
            # A whole class of axis is a RATE, and no spectral meter can see one: the
            # bagpipe's detune moved the centroid 5.7 Hz and the comb 1.2 dB over its
            # whole range and was reported as changing nothing measurable, while the
            # beat it exists for went from 1.0 to 4.0 Hz. Same for a cricket's chirp
            # rate (12 -> 50 Hz) and a frog's pulse rate (15 -> 100 Hz). M6 now asks
            # the beat meter too, so a rate control is not mistaken for a label.
            a["beat_rate_spread_hz"] = spread("beat_rate_hz")
            a["beat_depth_spread"] = spread("beat_depth")
            a["sustain_range"] = [min(r["sustain"] for r in good),
                                  max(r["sustain"] for r in good)]
        rep["axes"][axis] = a

    for axis in sorted(set(params) - set(by_axis)):
        rep["axes"][axis] = {"unmeasurable": "no anchor_code"}
    return rep


def verdicts(rep):
    """Turn the numbers into pass/warn/fail lines. One per rule, per subject."""
    out = list(rep["structure"])

    for f, r in rep["registers"].items():
        if "f0" in r or "error" not in r:
            pass
        if r.get("error") and "f0" not in r:
            out.append(("M1", f"{f} Hz: {r['error']}"))
            continue
        if r.get("error"):
            out.append(("M1", f"{f} Hz: {r['error']}"))
        s = r.get("sustain")
        if s is not None and s < 0.45:
            out.append(("M3", f"{f} Hz: does not stand — tail/head {s:.3f}"))

    tk = rep.get("tracking") or {}
    if tk.get("error"):
        out.append(("M2", f"tracking cannot be measured — {tk['error']}"))
    elif tk.get("verdict") != "tracks":
        out.append(("M2", f"over {tk['span']} the spectrum reads as "
                          f"{tk['verdict']!r}, not as following the keyboard "
                          f"(r_note {tk['r_note']} vs r_fixed {tk['r_fixed']})"))

    t = rep.get("tilt")
    if t and t["spread_db"] > TILT_OK_DB:
        # A tilt has to clear the meter's own scatter by a factor of two before it is
        # a finding. Below that the number is real but says nothing: it is the render
        # varying, not the keyboard.
        noise = t.get("repeat_spread_db")
        if noise is not None and t["spread_db"] < 2.0 * max(noise, 0.05):
            out.append(("M7", f"loudness reads {t['spread_db']:.1f} dB across "
                              f"110-880 Hz ({t['db_per_octave']:+.2f} dB/octave), but "
                              f"the same body at {t['repeat_at_hz']:.0f} Hz spreads "
                              f"{noise:.1f} dB between renders — NOT EVIDENCE of a "
                              f"tilt, the meter cannot resolve one this small here"))
        else:
            out.append(("M7", f"loudness follows the keyboard: {t['spread_db']:.1f} dB "
                              f"across 110-880 Hz ({t['db_per_octave']:+.2f} dB/octave)"
                              + ("" if noise is None else
                                 f", against {noise:.1f} dB of render-to-render "
                                 f"scatter at {t['repeat_at_hz']:.0f} Hz")))

    mid = rep["registers"].get("220.0", {})
    # `moves` and not the travel span: a stochastic source's centroid wanders on
    # its own, so the span alone passed every noise bed in the library — `noise`
    # read 830 Hz, `crackle` 1354, `surf` 2690, all with nothing modulating them.
    # A movement rule that a provably static body satisfies certifies nothing.
    if "moves" in mid and not mid["moves"]:
        out.append(("M4", f"stands still — colour motion "
                          f"{mid.get('centroid_motion_hz')} Hz at coherence "
                          f"{mid.get('motion_coherence')} (movement by default; "
                          f"travel without coherence is noise variance, not motion)"))

    for axis, a in rep["axes"].items():
        if a.get("unmeasurable"):
            out.append(("M5", f"{axis}: {a['unmeasurable']} — loudness and effect "
                              "cannot be measured"))
            continue
        bad = [r for r in a["anchors"] if "f0" not in r]
        for r in bad:
            out.append(("M1", f"{axis}/{r['anchor']}: {r.get('error')}"))
        sp = a.get("rms_spread_db")
        if sp is not None and sp >= LOUD_BAD_DB:
            out.append(("M5", f"{axis}: moves loudness {sp:.2f} dB over its "
                              "travel — this is a volume control"))
        elif sp is not None and sp > LOUD_OK_DB:
            out.append(("M5", f"{axis}: moves loudness {sp:.2f} dB over its travel"))
        moved = ((a.get("centroid_spread_hz") or 0) > MOVES_COLOUR_HZ
                 or (a.get("comb_spread_db") or 0) > MOVES_COMB_DB
                 or (a.get("motion_spread_hz") or 0) > MOVES_MOTION_HZ
                 or (a.get("beat_rate_spread_hz") or 0) > MOVES_BEAT_HZ
                 or (a.get("beat_depth_spread") or 0) > MOVES_BEAT_DEPTH)
        if a.get("centroid_spread_hz") is not None and not moved:
            out.append(("M6", f"{axis}: changes nothing measurable — centroid "
                              f"{a.get('centroid_spread_hz')} Hz, comb "
                              f"{a.get('comb_spread_db')} dB, motion "
                              f"{a.get('motion_spread_hz')} Hz, beat rate "
                              f"{a.get('beat_rate_spread_hz')} Hz, beat depth "
                              f"{a.get('beat_depth_spread')}"))
    return out


def split_declared(key, findings, code=None):
    """Findings the entry DECLARES as its own nature, and the rest.

    A declaration covers a RULE and a rule fires for more than one reason, so the
    rule level alone is not enough to match on. Checked with a deliberately broken
    body: `noise` declares M2 ("an unpitched bed, it has no pitch to follow the
    keyboard with"), and a `noise` that did not even COMPILE was filed under that
    declaration and printed with it as the reason — a syntax error absorbed as the
    instrument's nature. So a finding whose message reports a failure to MEASURE is
    never declarable, whatever the entry claims about itself.

    That is the whole guard, and it used to claim a second one it did not implement:
    "a finding whose numbers exceed what the declaration itself claims". There is no
    numeric comparison against a declaration anywhere in this file, and a prose
    declaration has no numbers to compare against — so the promise is withdrawn here
    rather than left standing as though the code kept it. The `HOT` clause in the
    unmeasurable test is also dead by construction: every M1 message is built as
    "<Hz>: <error>" or "<axis>/<anchor>: …" and cannot begin with HOT. It is kept
    because a future message format might, and it costs nothing.

    `code` is the body, so that the event-texture class can be read from the body
    itself instead of from a list here — see `texture_declared`.
    """
    real, declared = [], []
    for level, msg in findings:
        reason = declared_reason(key, level, code)
        unmeasurable = ("csound:" in msg or "cannot be measured" in msg
                        or "SILENT" in msg or msg.startswith("HOT"))
        (real if (reason is None or unmeasurable) else declared).append((level, msg))
    return real, declared


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--key", action="append", help="audit only these keys")
    ap.add_argument("--out", help="write the full JSON here")
    ap.add_argument("--params-only", action="store_true",
                    help="only entries that already carry parameters")
    args = ap.parse_args()

    lib = load()
    insts = lib["instruments"]
    if args.key:
        insts = [i for i in insts if i["key"] in set(args.key)]
    if args.params_only:
        insts = [i for i in insts if i.get("params")]

    reports = []
    for inst in insts:
        rep = audit_one(inst)
        rep["verdicts"], rep["declared"] = split_declared(inst["key"],
                                                          verdicts(rep),
                                                          inst.get("code"))
        reports.append(rep)
        mid = rep["registers"].get("220.0", {})
        tr = rep["tracking"]
        tilt = rep.get("tilt") or {}
        head = (f"{inst['key']:14s} "
                f"track={tr.get('verdict', 'error')!s:>14} "
                f"tilt={tilt.get('db_per_octave')!s:>6}dB/oct "
                f"centroid={mid.get('centroid')!s:>7} "
                f"sustain={mid.get('sustain')!s:>6} "
                f"motion={mid.get('centroid_motion_hz')!s:>7}")
        print(head, flush=True)
        for level, msg in rep["verdicts"]:
            print(f"    {level}  {msg}", flush=True)
        seen = set()
        for level, msg in rep["declared"]:
            print(f"    {level}  [declared] {msg}", flush=True)
            if level not in seen:      # the reason once per rule, not per register
                seen.add(level)
                print("           because: "
                      + (declared_reason(inst["key"], level, inst.get("code"))
                         or "NO REASON RECORDED — this is a bug in the audit, not a "
                            "property of the instrument"),
                      flush=True)

    if args.out:
        Path(args.out).write_text(json.dumps(reports, indent=1))
        print(f"\nwrote {args.out}")

    n = sum(len(r["verdicts"]) for r in reports)
    d = sum(len(r["declared"]) for r in reports)
    print(f"\n{len(reports)} instruments, {n} findings, {d} declared properties")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
