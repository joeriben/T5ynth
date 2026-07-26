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
import math
import re
import sys
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

import lco_measure as M  # noqa: E402

# ONE protocol for every render in this file. The plugin's voices are always on, so a
# measurement settles the instrument before the note, exactly as `lco_measure.render`'s
# `preroll` was built to do. This file used to render with none, while the batch work and
# both censuses use 0.5 — so the same axis published different numbers depending on which
# tool asked, and neither tool could confirm or refute the other. Measured on the shipped
# bodies: `supersaw.spread` reads 0.80 dB over its anchors at 220 Hz with no preroll and
# 0.41 with one; `bass_saw.bite`'s centroid reads 1606.7 against 1488.0. A body being
# switched on is not a body being played.
PREROLL = 0.5

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
# The third number is the count of entries among the standing-still ones that DECLARE
# `; MOVEMENT: TEXTURE`. Without it the census was cited in every gate run as "N of the
# M shipped entries are in the same position" about entries that are not in the same
# position at all: they are exempt from that rule by BJ's ruling of 2026-07-25.
_MOVE_CENSUS = (37, 25, 7, "2026-07-25")
_MOVE_REGISTERS = (55.0, 110.0, 220.0, 440.0, 880.0, 1760.0)


def census():
    """Recount which shipped entries move at every register. ~378 renders.

    SHIPPED, not "in the lexicon". An entry can be present in `dco_lexicon.json` and
    deliberately withheld from the library (`lco_build_library.assemble`), and a withheld
    entry never reaches an author, so counting it here reported the lexicon's number as
    the product's: `noise` and `pink_noise` were quoted in every gate run as two of the
    entries that stand still, about entries no prompt can reach.
    """
    import json
    lex = json.loads((REPO / "backend" / "dco_lexicon.json").read_text())
    withheld = [e["key"] for e in lex["techniques"] if e.get("withheld")]
    shipped = [e for e in lex["techniques"] if not e.get("withheld")]
    still, exempt = {}, set()
    declared = [e["key"] for e in shipped if declares_texture(e["code"])]
    for e in shipped:
        bad = []
        for f in _MOVE_REGISTERS:
            y, err = M.render(e["code"], dur=4.0, freq=f, preroll=PREROLL)
            if y is None:
                bad.append((f, f"render: {err}"))
                continue
            r = M.measure(y, f)
            if not r["moves"]:
                bad.append((f, f"{r['centroid_motion_hz']} Hz at "
                               f"{r['motion_coherence']}"))
        if bad:
            still[e["key"]] = bad
            if declares_texture(e["code"]):
                exempt.add(e["key"])
    n = len(shipped)
    print(f"{n - len(still)} of {n} SHIPPED entries move at all six registers, "
          f"{len(still)} do not, of which {len(exempt)} declare the event-texture class "
          f"and are exempt from the rule rather than failing it"
          + (f"\n  ({len(withheld)} further entries are in the lexicon and withheld from "
             f"the library, so they are not counted: {', '.join(withheld)})"
             if withheld else ""))
    for k, bad in still.items():
        mark = "  [TEXTURE]" if k in exempt else ""
        print(f"  {k:16s}{mark} " + "  ".join(f"{f:.0f}:{d}" for f, d in bad))
    # The third census number counts declarations that are USED — declared AND standing
    # still. It is not the number of bodies carrying the declaration, and reading it that
    # way loses the ones that declare and move anyway (`bubbles` does). Both are printed
    # because only the difference tells you a declaration is currently doing nothing.
    print(f"\n  {len(declared)} bodies carry `; MOVEMENT: TEXTURE`: "
          + ", ".join(declared)
          + f"\n  {len(exempt)} of them need it here; "
          + (", ".join(sorted(set(declared) - exempt)) or "none")
          + " move at every register AT THEIR DEFAULT and would pass THIS census without"
            " it. That is not the same as the declaration doing nothing: this census reads"
            " the default body only, and `lco_anchor_census` reads every named anchor —"
            " `glass` and `struck_bar` move at their defaults and stand still at several of"
            " their anchors.")
    want = (n - len(still), len(still), len(exempt))
    if want != _MOVE_CENSUS[:3]:
        print(f"\n  STALE  _MOVE_CENSUS says {_MOVE_CENSUS[0]}/{_MOVE_CENSUS[1]}/"
              f"{_MOVE_CENSUS[2]} (measured {_MOVE_CENSUS[3]}); it is now "
              f"{want[0]}/{want[1]}/{want[2]}. Update it and every comment that quotes it.")
        return 1
    print(f"\n  _MOVE_CENSUS is current at {want[0]}/{want[1]}")
    return 0


# The declaration is one EXACT standalone comment line, `; MOVEMENT: TEXTURE`, in caps,
# with any explanation on the lines after it. Three reasons it is that strict, each a
# hole the loose form had. `^;\s*movement:\s*texture\b` with re.I granted the exemption
# to `; MOVEMENT: TEXTURE does not apply to this body` — a negation. It also matched the
# words inside a Csound `/* … */` block, which is why the block comments come off first.
# And an end-of-line form (`asig = anz   ; MOVEMENT: TEXTURE`) is the library's own house
# style for axis comments, so a declaration written that way was silently IGNORED with no
# diagnostic — `_mentions_texture` below now makes that an error instead.
# Uptime-dependent state. A body whose colour comes from one of these has a shape over
# the note that depends on the PLUGIN'S UPTIME, because the host's sixteen instances have
# been running since load and only `knote` is reset at note-on. `tanpura` shipped its
# entire reason for existing that way: a 3.70 s `poscil` that climbed after the attack
# offline (where the instance begins at the note) and darkened at a quarter turn.
#
# Three syntactic shapes and three behavioural classes, because the first version of this
# detector matched ONE shape of ONE class and missed the rest — including, measured, the
# `poscil:k()` form used by this project's own LFO fixture and the `balance` that the
# commit introducing the detector named as one of its two motivating opcodes:
#
#   1. free-running generators — phase at note-on is the uptime
#   2. one-shot ramps and clocks (`linseg`, `metro`, …) — they run ONCE from the
#      INSTANCE, so after a second of uptime they sit at their end value for ever. This
#      is the worst class, not a borderline one: `kdark linseg 0, 1, 1` measures a colour
#      that climbs from 330 Hz on a fresh instance and is pinned at 660 Hz on every note
#      after. Also the shape an author reaches for first, and no shipped body has it yet.
#   3. settling state (`balance`, `follow`, `rms`) — the value at note-on depends on how
#      long the averager has been running. 29 shipped bodies carry a `balance`; the eight
#      whose note this rule newly made visible are `fm`, `fm_bell`, `fm_ep`, `drum_head`,
#      `metallic_fm`, `struck_bar`, `cymbal` and `glass`. Do not read the 8 as the number
#      of bodies using `balance` — that mistake is why this sentence names both.
#
# Deliberately NOT flagged, decisions rather than oversights:
#   * a-rate NOISE (`aexq rand 0.07`) — statistically stationary, so uptime changes the
#     draw and not the note's shape.
#   * the phase of an a-rate oscillator — inaudible on its own. It becomes audible only
#     where two of them beat, and then the beat rate is a k-rate reading this does catch.
#   * anything inside a `reinit` block, which the body itself restarts at the note.
#   * a generator whose output variable is never read again: it cannot reach `asig`.
#
# Every opcode name below was checked against `csound -z3` on the build in use. The first
# version of this list shipped `bspline`, which does not exist in Csound at all, and
# omitted `rspline`, which is THE slow-random-drift opcode; it put `rms` where only an
# a-rate output would match, though `rms` has a k output and no a-rate form; and `\b`
# after `oscil` silently excluded `oscil1`/`oscil1i` — a one-shot table envelope, i.e. the
# very class the comment above calls the worst one.
_KGENS = (r"poscil3|poscil|oscil1i|oscil1|oscili|oscil3|oscil|lfo|randi|randh|rand"
          r"|phasor|jspline|rspline|dust2|dust|randomi|randomh|gauss|betarand|trirand"
          r"|pinkish|vibrato|vibr|samphold|delayk|vdelayk|portk|port")
_KONESHOT = (r"linsegr|linseg|line|expsegr|expseg|expon|transegr|transeg|cosseg|lpshold"
             r"|loopxseg|looptseg|loopseg|metro2|metro|trigger|timeinstk|timek")
_SETTLERS = r"balance2|balance|follow2|follow|rms"
# `kx poscil …`, `gkx poscil …`, `kx, ky jspline …`, `imax, kx jspline …` — the output list
# may start at i- or k-rate and may be global; the state is the same state either way.
_KGEN = re.compile(rf"^\s*g?[ik]\w+(?:\s*,\s*g?[ika]\w+)*\s+"
                   rf"({_KGENS}|{_KONESHOT}|{_SETTLERS})\b")
# a-rate outputs: one-shot envelopes and the AGCs. No a-rate generator is flagged.
_AGEN = re.compile(rf"^\s*g?a\w+(?:\s*,\s*g?a\w+)*\s+({_KONESHOT}|balance2|balance"
                   rf"|follow2|follow)\b")
# `poscil:k(...)` anywhere in an expression — including inside an `=`, which is how the
# functional syntax always arrives.
_KFUNC = re.compile(rf"\b({_KGENS}|{_KONESHOT}|{_SETTLERS}):[ka]\s*\(")
_KNOTE = re.compile(r"\bknote\b")
_REINIT = re.compile(r"^\s*reinit\s+(\w+)", re.M)
_OUTVARS = re.compile(r"^\s*((?:g?[ikaS]\w+)(?:\s*,\s*g?[ikaS]\w+)*)\s+[a-zA-Z]")

TEXTURE = re.compile(r"^;\s*MOVEMENT:\s*TEXTURE\s*$")
_MENTIONS = re.compile(r"movement\s*:\s*texture", re.I)
# The same shape for the second class this gate was not written for — a body that is
# struck or plucked and then left to ring down. Same rules as the texture declaration
# above, for the same reason: one standalone line, in caps, so it cannot be granted by
# accident or by a negation. See `declares_decay`.
DECAY = re.compile(r"^;\s*DECAY:\s*SELF\s*$")
_MENTIONS_DECAY = re.compile(r"decay\s*:\s*self", re.I)
_BLOCK = re.compile(r"/\*.*?\*/", re.S)
_STRING = re.compile(r'"[^"]*"')


def _reinit_lines(lines):
    """The line indices inside a `reinit` block — the body restarts these itself.

    The canonical Csound retrigger is `if changed2(ktrig) == 1 then / reinit again / endif`
    with `again:` … `rireturn` around the envelope. Those lines ARE note-relative, and
    flagging them was a false positive on the one idiom an author is most likely to reach
    for: measured, such a body's spread across instance ages is exactly 0.
    """
    labels = set(_REINIT.findall("\n".join(lines)))
    if not labels:
        return set()
    inside, out = False, set()
    for i, l in enumerate(lines):
        s = l.strip()
        if not inside and s.split(":")[0].strip() in labels and ":" in s:
            inside = True
        if inside:
            out.add(i)
        if inside and s.startswith("rireturn"):
            inside = False
    return out


def free_running(body):
    """The lines whose state at note-on the plugin does not control.

    Block comments come off first for the same reason `declares_texture` strips them: a
    disabled line is not a behaviour, and Csound's `/* … */` spans lines so no per-line
    test can see it. Strings are neutralised BEFORE the `;` split, not after — a `;` inside
    a quoted string truncated the line and hid whatever followed.
    """
    src = _BLOCK.sub("", body or "")
    lines = src.splitlines()
    skip = _reinit_lines(lines)
    hits = []
    for i, l in enumerate(lines):
        if i in skip or l.lstrip().startswith(";"):
            continue
        bare = _STRING.sub('""', l).split(";")[0]
        if _KGEN.match(bare) or _AGEN.match(bare) or _KFUNC.search(bare):
            hits.append((i, l.strip(), bare))
    # A generator whose output is never read again cannot reach `asig`, so it cannot make
    # the note depend on anything. `asig` itself is the contract and is never dead.
    out = []
    for i, raw, bare in hits:
        m = _OUTVARS.match(bare)
        names = ([n.strip() for n in m.group(1).split(",")] if m else [])
        if names and "asig" not in names:
            elsewhere = False
            for j, other in enumerate(lines):
                if j == i:
                    continue
                stripped = _STRING.sub('""', other).split(";")[0]
                if any(re.search(rf"\b{re.escape(n)}\b", stripped) for n in names):
                    elsewhere = True
                    break
            if not elsewhere:
                continue
        out.append(raw)
    return out


# Four instance ages, all SETTLED, spread over the period of the SLOWEST generator that
# ships. Two earlier versions of this grid were each wrong in a different way, and both
# were wrong by guessing instead of measuring what the library contains.
#
#   * It started at `MIN_PREROLL`, a FRESH instance, so part of what it reported was the
#     start-up transient `worst_onset` already prints two lines above it.
#   * It then used four equal 0.925 s steps, which put a body whose period divides that
#     step at the same phase four times over.
#   * Replacing them with golden-ratio spacings inside 2.7 s fixed the arithmetic and
#     broke the coverage: 2.7 s is SHORTER THAN ONE PERIOD of the five slowest shipped
#     generators (0.037-0.055 Hz), and it aliased on `hurdy_gurdy`'s 1.45 Hz wheel, where
#     all four ages landed inside 15 % of one cycle — 3x worse than the grid it replaced.
#
# So the span is the slowest shipped period (27 s at 0.037 Hz) and the four fractions of
# it are irregular, so a rate has to hit three near-integer cycle counts at once to alias.
# A preroll is rendered, so this costs about 4 s of wall clock per body; that is why it
# runs on ONE candidate body in `--gate` and never over the whole library.
_SETTLE_S = 0.25
_SLOWEST_S = 27.0
_PHASES = tuple(_SETTLE_S + f * _SLOWEST_S for f in (0.0, 0.19, 0.47, 0.81))


def phase_spread(body, freq, phases=_PHASES):
    """How much the note's own shape depends on the instance's age at note-on.

    Returns (spread_hz, per_age_centroid_tracks) or None. This is REPORTED and not gated:
    whether a body may sound different depending on how long the plugin has been open is a
    judgement about that body's character, not a rule in §4. What the gate owes is that the
    fact is visible — it was not, and `tanpura` shipped a documented behaviour that only
    happened at phase zero.

    There is deliberately NO noise floor beside this number, and the version that printed
    one was worse than none: Csound is deterministic, so two renders at the same age are
    bit-identical and the floor was structurally 0.0 for all 56 flagged bodies, which cost
    three renders each and made the printer say "inside its own noise floor" about the one
    reading — zero against zero — that is the strongest negative result there is.

    What that costs honestly: for a body with a stochastic exciter, a different instance
    age also means a different slice of the noise, and no measurement here can separate
    that from a phase dependence. Read the number as "this body's first window is not the
    same at every uptime", not as "a generator's phase does this".
    """
    tracks = []
    for p in phases:
        y, err = M.render(body, dur=4.0, freq=freq, preroll=p)
        if y is None:
            return None
        tracks.append(M.measure(y, freq)["centroid_over_note"])
    firsts = [t[0] for t in tracks if t]
    if len(firsts) < 2:
        return None      # not "0 Hz", which reads as "no dependence"
    return (max(firsts) - min(firsts)), tracks


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

    It is a comment, so it is inert in Csound, and it travels with the body into the
    lexicon. To count who holds it — and NOT with grep, which counts 18 for six entries
    because every `anchor_code` variant carries a whole copy of its body:

        .venv/bin/python -c "import json, sys; sys.path.insert(0, 'tools'); \\
        import lco_axis_probe as P; \\
        print([e['key'] for e in json.load(open('backend/dco_lexicon.json'))['techniques'] \\
               if P.declares_texture(e['code'])])"
    """
    src = _BLOCK.sub("", body or "")
    lines = [l.strip() for l in src.splitlines()]
    if any(TEXTURE.match(l) for l in lines):
        return True
    # Mentioned but not declared. Silence here is the dangerous answer: a body written in
    # the library's own end-of-line comment style would have been gated on movement like
    # a sustained tone, and its author would have had no way to tell why.
    bad = [l for l in lines if _MENTIONS.search(l)]
    if bad:
        raise SystemExit(
            "this body mentions a texture declaration but does not make one:\n"
            + "\n".join(f"    {l}" for l in bad)
            + "\nThe declaration is one standalone comment line, exactly:\n"
              "    ; MOVEMENT: TEXTURE\n"
              "with any explanation on the lines after it.")
    return False


def peak_db(r):
    """A measure dict's peak in dB, from the UNROUNDED value.

    `lco_measure.measure` rounds `peak` to three decimals for printing, which is a cliff
    at the bottom of the range this tool now judges on: a render whose true peak is
    0.0004 reads 0.000 and turns into -240 dB. `peak_exact` is stashed beside it by
    whoever made the render; the rounded value is the fallback for a dict that predates
    it, and `1e-12` the floor for a render that really is silent."""
    return 20.0 * math.log10(max(r.get("peak_exact") or r["peak"], 1e-12))


def with_exact_peak(r, y):
    """The measure dict, plus the unrounded peak of the audio it came from."""
    r["peak_exact"] = float(np.abs(y).max())
    return r


def declares_decay(body):
    """Does this body declare itself struck-and-left-to-ring rather than a standing tone?

    §4 of `LCO_CONCEPT.md` stopped requiring a standing tone on 2026-07-20 (BJ: a
    self-decay is "a choice the prompt makes", replacing the older "only where there is
    no other way"). This gate never followed, and it cannot: every loudness statistic it
    owns is read off a four-second window, which on a body that rings down measures HOW
    LONG IT RANG and calls it a level difference. Measured on `plucked_wire`, the first
    entry in the library that fades by itself: 23.12 dB across its own cube and 223.45 dB
    across cube and keyboard together, where the strike itself is identical to a
    hundredth of a dB at every corner and every register.

    So for a declared decaying body the loudness question is asked at the ATTACK — the
    note's peak — and the ring-down is REPORTED instead of failed. Three things this
    deliberately does NOT do:

      * It does not excuse a SWELL. The drift check exists for §4's named violation, and
        only a falling drift is the declared decay; a body that grows inside a held note
        still fails whatever it declares.
      * It does not touch the movement rule, the headroom check or the pitch. A decaying
        body still has to move, still may not clip, still tracks `kfreq`.
      * It does not silence the four-second numbers. They are printed beside the ones the
        verdict used, because "the level spreads 23 dB and that is the ring" is a fact
        about the entry that its author has to see. Hiding it was the alternative and it
        is how a real finding gets lost: `plucked_wire`'s `pick` axis had a genuine 4 dB
        fader in it, which is why that axis ships as 0.25..0.45 and not 0.20..0.50.

    Same declaration discipline as `declares_texture` — one exact standalone line, in
    caps — and for the same reason: a body written in the library's end-of-line comment
    style would otherwise be judged as a standing tone with no way to tell why.

        .venv/bin/python -c "import json, sys; sys.path.insert(0, 'tools'); \\
        import lco_axis_probe as P; \\
        print([e['key'] for e in json.load(open('backend/dco_lexicon.json'))['techniques'] \\
               if P.declares_decay(e['code'])])"
    """
    src = _BLOCK.sub("", body or "")
    lines = [l.strip() for l in src.splitlines()]
    if any(DECAY.match(l) for l in lines):
        return True
    bad = [l for l in lines if _MENTIONS_DECAY.search(l)]
    if bad:
        raise SystemExit(
            "this body mentions a decay declaration but does not make one:\n"
            + "\n".join(f"    {l}" for l in bad)
            + "\nThe declaration is one standalone comment line, exactly:\n"
              "    ; DECAY: SELF\n"
              "with any explanation on the lines after it.")
    return False


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
    # Whether the body declares itself struck-and-ringing is read HERE, from the body
    # itself, so that the per-axis report and `gate` cannot give two answers about the
    # same axis. They did: the gate read the attack and printed PASS while this report,
    # still on the four-second rms, called the ring-length control "A FADER — it moves
    # loudness" — and this is the tool an author runs FIRST, before the entry is written.
    decaying = declares_decay(body)
    for v in values:
        y, err = M.render(with_axis(body, name, v), dur=dur, freq=freq,
                          preroll=PREROLL)
        if y is None:
            rows.append({"value": v, "error": err})
            continue
        r = with_exact_peak(M.measure(y, freq), y)
        r["decaying"] = decaying
        r["level_db"] = peak_db(r) if decaying else r["rms_db"]
        r["value"] = v
        if err:
            r["error"] = err
        rows.append(r)
    return rows


def report(name, rows, quiet=False):
    good = [r for r in rows if "rms_db" in r]
    decaying = any(r.get("decaying") for r in good)
    head = "peak dB" if decaying else "rms dB"
    if not quiet:
        print(f"\naxis {name!r}" + ("   [DECAY: SELF — read at the note's peak, because "
                                    "a four-second rms on a ringing body measures the "
                                    "ring]" if decaying else ""))
        print(f"  {'value':>7} {head:>8} {'centroid':>9} {'comb dB':>8} "
              f"{'motion':>8} {'sustain':>8}  note")
        for r in rows:
            if "rms_db" not in r:
                print(f"  {r['value']:7g} {'':>8} {'':>9} {'':>8} {'':>8} {'':>8}"
                      f"  {r['error']}")
                continue
            print(f"  {r['value']:7g} {r['level_db']:8.2f} {r['centroid']:9.1f} "
                  f"{r['comb_db']:8.1f} {r['centroid_motion_hz']:8.1f} "
                  f"{r['sustain']:8.3f}  {r.get('error', '')}")
    if len(good) < 2:
        return None
    def sp(k):
        vs = [r[k] for r in good]
        return max(vs) - min(vs)
    verdict = {"rms_spread_db": round(sp("level_db"), 2),
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
    decaying = declares_decay(body)
    names = sorted(declared)

    def level(r):
        """The number the loudness verdicts are read from.

        A four-second RMS on a body that rings down is a measurement of the ring, not of
        the level — see `declares_decay`. A declared decaying body is judged on its
        note's PEAK instead (on a struck body, the strike); everything else keeps the
        meter it always had.

        `peak_db` and not `measure`'s `peak`, which is rounded to three decimals: every
        render below a true peak of 0.0005 rounds to exactly zero and read -240 dB, so a
        quiet body's 0.63 dB axis was reported as a 180 dB fader. The rounding still
        costs 0.43 dB at a peak of 0.01, against a 1.00 dB rule.
        """
        return peak_db(r) if decaying else r["rms_db"]
    meter = "at the note's peak" if decaying else "over the note"
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
            y, err = M.render(b, dur=4.0, freq=f, preroll=PREROLL)
            if y is None:
                fails.append(("renders", dict(corner, **{"Hz": f}), err))
                continue
            # Movement is measured at every register and GATED at `freq`. Not gated
            # everywhere, on measured grounds: of the 62 shipped entries at their
            # defaults, only 37 satisfy `moves` at all six registers (`--census`;
            # `_MOVE_CENSUS` above). The rest are mostly the three classes `moves`
            # itself documents as beyond it — `string` travels 2881-3402 Hz at
            # coherence 0.14-0.25 because a decaying high-Q bank's late window is a
            # noise floor, `cymbal`, `drum_head` and `pink_noise` likewise — plus
            # fixed-formant bodies (`voice`, `saw`, `sub_sine`) whose travel shrinks in
            # cents as the note rises past their formants. Gating everywhere would
            # condemn 20 long-shipped entries (25 fail, 5 of them declared) in the
            # name of a meter limit, which is a change of standard and BJ's to make,
            # not a defect to fix. It is REPORTED so the register dependence is
            # visible: the same body used to pass or fail purely on which registers
            # were in the list, and at a `--freq` outside the list nothing was checked
            # at all.
            r = with_exact_peak(M.measure(y, f), y)
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
    lv = [level(r) for _, r in at_freq]
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
        #
        # Every figure here is optional, and none of them may turn a verdict into a
        # traceback. `crest_db` returns None on a window with no energy in it — which is
        # exactly what a body that stops dead inside the note produces, i.e. §4's own
        # named violation — and `min()` over those raised a TypeError, so DECLARING the
        # class replaced a clean FAIL with a crash. `at_freq` can be empty too, when
        # every reference-register corner fails to render.
        def rng(key):
            v = [r[key] for _, r in at_freq if r.get(key) is not None]
            return (min(v), max(v)) if v else None

        stats["texture"] = {
            "would_fail_moves": sum(1 for _, r in at_freq if not r["moves"]),
            "of_corners": len(at_freq),
            "travel_cents": rng("centroid_travel_cents"),
            "stationary_null_cents": M.STATIONARY_SPAN_NULL_CENTS,
            "crest_db": rng("crest_db"),
            "crest_null_db": M.STATIONARY_CREST_NULL_DB}
    sustained = [(abs(r["loudness_drift_db"] or 0.0), r["loudness_drift_db"], c)
                 for c, r in rows if r["sustain"] > 0.25]
    if sustained:
        wd = max(sustained, key=lambda t: t[0])
        stats["worst_sustained_drift"] = wd

        def confirm(cand):
            """The same corner on a note four times as long.

            A candidate failure is CONFIRMED there before it counts, because one slow
            cycle caught inside a short window is a straight line: `crackle` reads
            -6.33 dB over six seconds and +0.12 over twelve, and `supersaw`'s slow detune
            beat reads +6.26 and then +2.49. A real trend goes the other way and grows —
            an asked swell read +1.71 dB at six seconds and +16.39 at forty-eight.
            Without this the gate condemned two shipped entries for the length of its own
            window."""
            b = body
            for k, v in cand[2].items():
                if k != "Hz":
                    b = with_axis(b, k, v)
            y, err = M.render(b, dur=16.0, freq=cand[2]["Hz"], preroll=PREROLL)
            return None if y is None else M.loudness_drift_db(y)

        def is_trend(cand, long):
            return long is not None and abs(long) > 6.0 and (long > 0) == (cand[1] > 0)

        def drift_fail(cand, long):
            # The tail names WHY the trend is a fault, and on a declared body that is a
            # different sentence: it is not that the body fails to decay, it is that it
            # grows, which no declaration covers.
            why = ("and a `; DECAY: SELF` declaration excuses a fall, never a swell"
                   if decaying else "and the body is not a decaying one")
            return ("one loudness inside the note", cand[2],
                    f"the level drifts {cand[1]:+.2f} dB from one end of a held note to "
                    f"the other and {long:+.2f} dB over a note four times as long, so it "
                    f"is a trend and not a slow cycle, {why}")

        if decaying:
            # The declaration excuses a FALL and nothing else — a body that SWELLS inside
            # a held note is §4's named violation whatever it says about itself. So the
            # fall and the swell get SEPARATE candidates, and this is not a nicety: with
            # one global argmax the rule was spent on whichever move was larger, and a
            # body that fell 9.87 dB at one corner while rising 8.11 dB (+41.51 dB over
            # sixteen seconds) at another passed clean, the swell never confirmed, never
            # failed, never printed.
            fall = max((t for t in sustained if t[1] < 0), key=lambda t: t[0], default=None)
            rise = max((t for t in sustained if t[1] > 0), key=lambda t: t[0], default=None)
            if fall is not None and fall[0] > 6.0:
                stats["declared_decay_drift"] = (fall[2], fall[1], confirm(fall))
            if rise is not None and rise[0] > 6.0:
                long = confirm(rise)
                stats["worst_drift_confirmed"] = long
                stats["drift_candidate"] = rise
                if is_trend(rise, long):
                    fails.append(drift_fail(rise, long))
        elif wd[0] > 6.0:
            long = confirm(wd)
            stats["worst_drift_confirmed"] = long
            stats["drift_candidate"] = wd
            if is_trend(wd, long):
                fails.append(drift_fail(wd, long))
    if stats["loudness_spread_db"] is not None and stats["loudness_spread_db"] > 1.0:
        fails.append(("one loudness", pk[1],
                      f"{stats['loudness_spread_db']:.2f} dB across the corners "
                      f"({meter})"))
    # The body AS WRITTEN, at its own defaults, at every register. Its own configuration
    # is the one that matters most and it need not sit on the cube grid at all, so these
    # renders are their own rows — see the tilt note below for the ten-fold error that
    # taught it. They join the headroom check for the same reason: `string`'s true peak
    # is 3.02 at its DEFAULTS at 55 Hz, over the host's clip, and every grid corner at
    # 55 Hz is under it, so a check that read only the grid called that body clean.
    base = []
    for f in regs:
        y, err = M.render(body, dur=4.0, freq=f, preroll=PREROLL)
        if y is not None:
            r = with_exact_peak(M.measure(y, f), y)
            # `M.rms_db(y)` and not `level(r)` in the undeclared case: `measure` rounds
            # its rms to 2 dp and the tilt has always been computed on the unrounded
            # number. Changing the meter for one class must not quietly change the
            # arithmetic for every other.
            base.append((f, level(r) if decaying else M.rms_db(y), r))
    # Headroom is judged on `peak_late` — the peak AFTER the first 50 ms — counted over
    # the whole sweep rather than read off one render, because a stochastic body's peak
    # is a random variable and the sweep is many draws of it.
    #
    # Three measures were tried here and two were wrong. p99.9 is too lenient: it let
    # `ice` through at four cracks a second. The true peak is too STRICT, and that was
    # this check's own error — it condemned `string` (3.18 at 69.3 Hz) and `ice` (3.61 at
    # 27.5 Hz), and in both cases the excursion is the body's filter starting up: one
    # sample at t = 0.004 s for `string`, 25 samples inside 100 ms for `ice`, against a
    # sustained 0.86 and 1.18. Relevelling either would have darkened a whole instrument
    # by 2 dB to tame a start-up sample the host's soft curve rounds off anyway.
    #
    # And the ceiling itself was wrong: see `lco_measure.HOST_TRANSPARENT`. 2.97 read
    # `clip`'s third argument as the limit; the fourth is where limiting begins.
    peaks = ([(r["peak_late"], r["peak"], c) for c, r in rows]
             + [(r["peak_late"], r["peak"], {"the defaults": True, "Hz": f})
                for f, _v, r in base])
    over = sorted(((pl, pk_all, c) for pl, pk_all, c in peaks if pl > M.HOST_TRANSPARENT),
                  reverse=True, key=lambda t: t[0])
    stats["over_clip"] = (len(over), over[0] if over else None)
    # The onset is reported even when nothing fails, so that a body whose start-up sample
    # is 4x its sustained level cannot be invisible here — it is just not a failure.
    # (It was invisible: this stat was written and never printed, and the onset appeared
    # only inside a headroom FAILURE string — the one case that needed no help.)
    onset = max(peaks, key=lambda t: t[1] - t[0])
    stats["worst_onset"] = (onset[1], onset[0], onset[2])
    # And what the plugin's uptime does to this body's note. Only for bodies that carry
    # state the note-on does not reset, since for anything else there is nothing to sweep.
    stats["free_running"] = free_running(body)
    stats["phase_spread"] = None
    if stats["free_running"]:
        got = phase_spread(body, float(freq))
        if got is not None:
            stats["phase_spread"] = got[0]
    if over:
        worst = over[0]
        limited = (" — past the absolute ceiling, so the host lets NONE of it out"
                   if worst[0] > M.HOST_CLIP else "")
        fails.append(("headroom", worst[2],
                      f"peak {worst[0]:.2f} after the onset at the worst of {len(over)} "
                      f"of {len(peaks)} renders, over the host's transparent ceiling "
                      f"{M.HOST_TRANSPARENT:.2f}{limited} (its true peak there is "
                      f"{worst[1]:.2f}, and the worst p99.9 anywhere {pk[0]:.2f} — this "
                      f"is judged on neither)"))
    # The register tilt at the DEFAULTS, which is the number the notes quote — so it
    # is measured on the body AS WRITTEN, at its own default values, and not on the
    # nearest grid corner. The defaults need not sit on a 3-step grid at all:
    # `overtone_voice` defaults to select 0.45, focus 0.6, press 0.45, none of them on
    # the grid, and the nearest corner (0.5, 0.5, 0.5) reads 0.06 dB where the body
    # itself reads 0.60. Its note shipped the 0.06 — a figure off by ten times, about
    # a body no one had measured. Six extra renders buy the number its own name. (The
    # renders themselves are made above, so the headroom check can see them too.)
    if len(base) >= 2:
        v = [x for _f, x, _r in base]
        stats["register_tilt_db"] = round(max(v) - min(v), 2)
        stats["register"] = [(f, round(x, 2)) for f, x, _r in sorted(base)]
    allv = [(level(r), c) for c, r in rows]
    if decaying:
        # What the four-second meter reads, kept in plain sight next to the verdict. On a
        # ringing body this IS the ring length and not a level fault — but it is also
        # where a real fader hides, so it is printed rather than dropped.
        rmsv = [r["rms_db"] for _, r in rows]
        stats["decay"] = {
            "meter": "peak (the attack)",
            "rms_cross_spread_db": round(max(rmsv) - min(rmsv), 2) if rmsv else None,
            "rms_corner_spread_db": (round(max(x) - min(x), 2)
                                     if (x := [r["rms_db"] for _, r in at_freq]) else None),
            "sustain": (round(min(s), 3), round(max(s), 3)) if (
                s := [r["sustain"] for _, r in rows if r.get("sustain") is not None]) else None}
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

    y, err = M.render(body, dur=4.0, freq=args.freq, preroll=PREROLL)
    if y is None:
        raise SystemExit(f"the body at its defaults does not render: {err}")
    d = M.measure(y, args.freq)
    print(f"defaults @ {args.freq:.0f} Hz: rms {d['rms_db']:.2f} dB, centroid "
          f"{d['centroid']:.0f} Hz, comb {d['comb_db']:.1f} dB, motion "
          f"{d['centroid_motion_hz']:.0f} Hz, sustain {d['sustain']:.3f}"
          + (f"   [{err}]" if err else ""))

    if args.registers:
        # Same meter question as in `gate`: a four-second rms on a body that rings down
        # reports how long it rang. Left unfixed here it printed a 226 dB "tilt" for a
        # wire whose strike is identical at every register — a number that reads as a
        # catastrophic defect and is a measurement of the decay.
        reg_decay = declares_decay(body)
        reg_meter = "at the attack" if reg_decay else "over the note"
        regs = [float(x) for x in args.registers.split(",")]
        lv = []
        for f in regs:
            yr, er = M.render(body, dur=4.0, freq=f, preroll=PREROLL)
            if yr is None:
                lv.append((f, None, er))
            elif reg_decay:
                lv.append((f, peak_db(with_exact_peak(M.measure(yr, f), yr)), er))
            else:
                lv.append((f, M.rms_db(yr), er))
        print(f"register ({reg_meter}): " + "  ".join(
            f"{f:.0f}:{'ERR' if v is None else f'{v:+.2f}'}" for f, v, _ in lv))
        vals = [v for _, v, _ in lv if v is not None]
        if len(vals) >= 2:
            octs = math.log2(regs[-1] / regs[0])
            print(f"  -> tilt {max(vals) - min(vals):.2f} dB total, "
                  f"{(vals[-1] - vals[0]) / octs:+.2f} dB/octave")
        if len({f for f, v, e in lv if v is not None}) >= 1:
            # the render-to-render spread at ONE register, so the tilt above can be
            # told apart from measurement noise (1/f-heavy bodies vary ~1.5 dB)
            rep = []
            for _ in range(3):
                yr, _e = M.render(body, dur=4.0, freq=regs[0], preroll=PREROLL)
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
        if st.get("worst_onset"):
            _pk, _late, _where = st["worst_onset"]
            if _late > 0 and _pk > _late * 1.5:
                print(f"  note  the onset is {_pk / _late:.1f}x the sustained peak "
                      f"({_pk:.2f} against {_late:.2f}) at {_where}. Not a failure — the "
                      f"host's clip is judged after the first {M.ONSET_S * 1000:.0f} ms, "
                      f"and a fresh instance's `balance` accounts for most of this — but "
                      f"it is what the note STARTS with, so it is audible")
        if st.get("free_running"):
            _sp = st.get("phase_spread")
            _reading = ("an unmeasured amount" if _sp is None else
                        "nothing at all — four instance ages give the same first window"
                        if _sp < 1.0 else f"{_sp:.0f} Hz")
            print(f"  note  {len(st['free_running'])} line(s) hold state the note-on does "
                  f"not reset, so this body's note may depend on how long the plugin has "
                  f"been open: over four ages spanning {_SLOWEST_S:.0f} s the first "
                  f"window's colour moves " + _reading
                  + ". Key anything that must happen AFTER the attack to `knote`, which the "
                    "host resets; a generator's phase and an averager's history it does "
                    "not. First: " + st["free_running"][0])
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
            # The candidate that was CONFIRMED, which on a declared decaying body is not
            # the global argmax: the fall and the swell are judged separately, so quoting
            # `worst_sustained_drift` here printed one corner's four-second number beside
            # another corner's sixteen-second one.
            cand = st.get("drift_candidate") or wd
            if cand[0] > 6.0 and cf is not None and not (
                    abs(cf) > 6.0 and (cf > 0) == (cand[1] > 0)):
                print(f"  note  the level drifts {cand[1]:+.2f} dB end to end at "
                      f"{cand[2]}, but only {cf:+.2f} dB over a note four times as "
                      f"long — one slow cycle, not a trend")
            elif wd[0] <= 6.0:
                # "not decaying" is about the CORNER, not the entry: only corners whose
                # `sustain` says they still stand are in this list, and a declared
                # decaying body has those too (a wire at 110 Hz barely drops in four
                # seconds). Saying it the other way told a decaying entry it was not one.
                print(f"  note  worst level drift end to end {wd[1]:+.2f} dB at {wd[2]}, "
                      + ("among the corners that still stand inside the note"
                         if st.get("decay") else "on a body that is not decaying"))
        if st.get("texture"):
            t = st["texture"]
            print(f"  TEXTURE  this body declares itself an event texture, so `moves` is "
                  f"REPORTED and not gated: it would have failed at {t['would_fail_moves']} "
                  f"of {t['of_corners']} corners.")
            trv = ("not readable" if not t["travel_cents"] else
                   f"{t['travel_cents'][0]:.0f}..{t['travel_cents'][1]:.0f} cents")
            crs = ("not readable" if not t["crest_db"] else
                   f"{t['crest_db'][0]:.2f}..{t['crest_db'][1]:.2f} dB")
            print(f"           colour travel {trv} against a "
                  f"{t['stationary_null_cents']:.0f}-cent stationary null, crest {crs} "
                  f"against {t['crest_null_db']:.2f} — these are CONTEXT, not a pass: a "
                  f"bed that does nothing reads the same, and a real sweep reads less. "
                  f"Liveness in this class is BJ's ear (see `declares_texture`).")
        if st.get("decay"):
            d = st["decay"]
            print(f"  DECAY  this body declares itself struck and left to ring, so every "
                  f"loudness verdict above was read at the ATTACK, not over the note.")
            print(f"           over a four-second note the level spreads "
                  f"{d['rms_corner_spread_db']} dB across the corners and "
                  f"{d['rms_cross_spread_db']} dB across corners and registers together — "
                  f"that is the RING LENGTH, not a fault, but it is also where a fader "
                  f"would hide, so check it against what the axes claim to do."
                  + (f" Sustain reads {d['sustain'][0]:.3f}..{d['sustain'][1]:.3f}."
                     if d.get("sustain") else ""))
            if st.get("declared_decay_drift"):
                c, short, long = st["declared_decay_drift"]
                print(f"           the level falls {short:+.2f} dB inside a held note at "
                      f"{c} and {long:+.2f} dB over one four times as long. Excused "
                      f"because it FALLS; a swell of the same size would still fail.")
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
                  + f" (reported, not gated: {_MOVE_CENSUS[1] - _MOVE_CENSUS[2]} of the "
                    f"{_MOVE_CENSUS[0] + _MOVE_CENSUS[1]} shipped entries are in the same "
                    f"position at some register — a further {_MOVE_CENSUS[2]} stand still "
                    f"too but DECLARE the event-texture class, so they are not in the same "
                    f"position and are not counted here. Measured {_MOVE_CENSUS[3]}; "
                    f"re-derive with --census)")
        if not ok:
            print(f"  -> {len(fails)} failure(s)")
        elif st.get("texture"):
            print("  PASS  every corner renders and holds one loudness; movement is this "
                  "class's declared exemption and stands on BJ's ear, not on this run")
        elif st.get("decay"):
            print("  PASS  every corner renders, moves, and is struck equally hard; the "
                  "ring-down is this class's declared exemption, the attack is not")
        else:
            print("  PASS  every corner renders, moves, and holds one loudness")
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
