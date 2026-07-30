#!/usr/bin/env python3
"""An HTML listening page on which the PARAMETER COMBINATION is visible and selectable.

Why this exists (BJ, 2026-07-30): asked which instruments he had approved, the honest
answer turned out to be none — what he had been given was folders of WAVs that the
assistant had chosen, at parameter values he could not see. His words: *„approved aus von
dir vorgebauten files mit intransparenter parameterlage aus einem Ordner"* and *„überhaupt
ist mir fast nichts als html-testseite mit parameterkombination vorgelegt worden"*. So the
`heard` field in `backend/dco_lexicon.json` now records `abgenommen` only over a page like
this one, and this file is the generator for it.

This is NOT `lco_listening_test.py`. That one is the blinded word→sound assignment from
`docs/LCO_TEST_POLICY.md` and puts a WORD on trial, one parameter at a time. This one puts
the INSTRUMENT in front of the listener with every axis reachable and every value shown —
what an approval needs and what the assignment test deliberately withholds.

Three properties the page must have, each because getting it wrong is what happened before:

* **The setting is always on screen.** Every stimulus names its full combination, not a
  file name. Nothing is chosen for the listener out of sight.
* **ONE gain for the whole set.** Several entries declare that a parameter moves the
  LEVEL (`analog_osc` heads its own code `; LOUDNESS: width` / `; LOUDNESS: wave` — a
  pulse gets quieter the narrower it is, and BJ ruled that this IS pulse width
  modulation). Per-file normalisation would delete exactly the property under test.
* **Long enough for the slowest thing in the code.** `analog_osc`'s `age` drives its
  drift at 0.043 Hz and its width jitter at 0.057 Hz — periods of 23 and 18 seconds. A
  4-second stimulus cannot show that parameter at all, so the default here is 12 s and
  `--dur` says so on the page.

Usage:
    .venv/bin/python tools/lco_param_page.py analog_osc
    .venv/bin/python tools/lco_param_page.py analog_osc --freq 110 --dur 16
    .venv/bin/python tools/lco_param_page.py analog_osc --axes wave,width   # smaller cross
"""
from __future__ import annotations

import argparse
import html
import itertools
import json
import re
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

import lco_measure as M  # noqa: E402

LEX = REPO / "backend" / "dco_lexicon.json"
OUTROOT = REPO / "tools" / "lco_listening"

# A parameter whose whole purpose is to be heard AGAINST another layer. Rendered alone it
# is just a differently-tuned note, which tells the listener nothing about the control.
# `analog_osc`'s own note: "shifts the pitch in cents so several layers can be detuned
# against each other". The page sums two renders for these, which is what the synth does
# with two voices.
PAIRED = {"finetune"}


def entry_of(key):
    for t in json.loads(LEX.read_text())["techniques"]:
        if t["key"] == key:
            return t
    raise SystemExit(f"no entry {key!r} in the lexicon")


# A number in the code, not a digit inside an identifier: `kbtp0 min 4.0, kbmx0` holds
# exactly one, not three.
NUM = re.compile(r"(?<![\w.])-?\d+(?:\.\d+)?")


def _split_comment(line):
    code, sep, rest = line.partition(";")
    return code, (sep + rest)


def _anchor_value(key):
    """The number an `anchor_code` key states, or `None` when it states a word.

    `analog_osc` already ships `wave=ramp  (…)` and `wave=pulse  (…)` — an axis whose
    anchors are words, not values. Those cannot be fitted, and blowing up on them with a
    bare ValueError would be a traceback where the answer is simply "this axis is not
    derivable".
    """
    try:
        return float(key.split("=", 1)[1].strip().split()[0])
    except (IndexError, ValueError):
        return None


class Ambiguous(Exception):
    """Two lines claim one axis — an authoring fault, never a reason to skip it."""


def named_site(body, name):
    """The line an axis can be written into by NAME. `None` if there is none.

    Two spellings, because the lexicon uses both and neither alone covers it.
    `analog_osc` names its variable after the axis (`kwidth = 0.02`), so a prefix match
    works; `fm3` does not (`ki2`, `kr3`, `ktrd`, `kons`) and instead DECLARES the axis in
    the line's own comment (`ki2 = 0.26 ; index 2[0.0..0.6]: …`). The comment is the
    reliable one — it is the same string the entry's `params` key carries — so it is
    tried first and the variable name is the fallback.

    Exactly one line may match, so a renamed or duplicated axis fails loudly instead of
    silently rendering the default.
    """
    lines = body.split("\n")
    decl = re.compile(rf";\s*{re.escape(name)}\s*\[")
    idx = [i for i, l in enumerate(lines) if re.match(r"^\s*k\w+\s*=", l) and decl.search(l)]
    if not idx:
        idx = [i for i, l in enumerate(lines)
               if re.match(rf"^\s*k{re.escape(name)}\s*=", l)]
    if len(idx) > 1:
        # NOT a SystemExit: `reachable()` swallows those, and a duplicated declaration
        # would then quietly demote the axis to "not in the code" — the one wrong
        # answer, since it is in the code twice.
        raise Ambiguous(f"{name!r}: {len(idx)} lines declare it, expected one")
    return idx[0] if idx else None


def derived_site(entry, name):
    """The substitution site an axis has WITHOUT being named, read off the entry itself.

    Several entries carry no line bearing the axis' name and yet state the wiring
    perfectly clearly — by example, under `anchor_code`. `fm` ships five complete bodies
    (`index=0.13  (mellow: …)` …), each differing from its `code` in exactly one number
    on exactly one line: `kbtp0 min 4.0, kbmx0` becomes 3.0 / 5.1 / 10.0 / 13.3 / 16.0.
    Five points of one straight line, and reading it off is arithmetic, not authoring.

    So: same differing line for every anchor body, same numeric position on it, a line
    fitted through (axis value → literal). Then the check that makes this a derivation
    rather than a curve fit — **the fitted line must reproduce the SHIPPED code's own
    literal at the entry's declared default**, a point that was not among the inputs.
    `fm` predicts 4.045 where the code says 4.0, `fm_bell` 6.035 against 6.0,
    `metallic_fm` 9.022 against 9.0, `driven_metal` 0.400 against 0.400 exactly.
    Independent confirmation that the method is right: `analog_osc`'s `drive` has BOTH a
    named line and anchor bodies, and the derivation returns the identity the named line
    already implements.

    Returns `(line, literal_index, slope, intercept)`, or `None` when the anchor bodies
    are not a single-site substitution at all — `fm_ep`'s `ting`, `drum_head`'s
    `pitched` and `string`'s `bow` rewrite whole passages, so they stay fixed variants.
    """
    base = entry["code"].split("\n")
    pts, sites = [], set()
    for key, body in (entry.get("anchor_code") or {}).items():
        if not key.startswith(f"{name}="):
            continue
        value = _anchor_value(key)
        if value is None:                 # `wave=ramp …`: a word, nothing to fit
            return None
        var = body.split("\n")
        if len(var) != len(base):
            return None
        diff = [i for i in range(len(base)) if base[i] != var[i]]
        if not diff:                      # the anchor that IS the shipped default
            pts.append((value, None))
            continue
        if len(diff) > 1:
            return None
        i = diff[0]
        nb = NUM.findall(_split_comment(base[i])[0])
        nv = NUM.findall(_split_comment(var[i])[0])
        if len(nb) != len(nv):
            return None
        moved = [j for j in range(len(nb)) if nb[j] != nv[j]]
        if len(moved) != 1:
            return None
        sites.add((i, moved[0]))
        pts.append((value, float(nv[moved[0]])))
    if len(sites) != 1:
        return None
    line, j = sites.pop()
    lit0 = float(NUM.findall(_split_comment(base[line])[0])[j])
    # The default is the CHECK, so it must not also be an input, or the check is a
    # tautology. `driven_metal` anchors both its axes at their own default, and with
    # that point left in, line 166 could not fail for the one entry whose axes exist
    # only by derivation.
    dflt = entry["params"][name]["default"]
    xy = [(v, lit0 if y is None else y) for v, y in pts if v != dflt]
    xs = np.array([v for v, _ in xy])
    ys = np.array([y for _, y in xy])
    if len(xy) < 3 or len(set(xs.tolist())) < 2:
        return None
    slope, icept = np.polyfit(xs, ys, 1)
    tol = 0.01 * (ys.max() - ys.min()) + 1e-9
    if np.abs(slope * xs + icept - ys).max() > tol:
        return None
    if abs(slope * dflt + icept - lit0) > tol:
        return None                       # the shipped code disagrees: not this line
    return line, j, float(slope), float(icept)


def anchor_line(entry, name, value, line):
    """The entry's OWN text for this axis' line at this exact value, or `None`.

    Wherever the entry ships a body for the very value being rendered, that body's line
    goes in verbatim: the page is meant to play what the entry says, and a fitted or
    reformatted approximation of a line the entry states exactly is not that. Accepted
    only when the anchor body differs from the shipped code on this line alone — an
    anchor that rewrites other lines too is a different body, not this axis' value.
    """
    base = entry["code"].split("\n")
    for key, body in (entry.get("anchor_code") or {}).items():
        if not key.startswith(f"{name}=") or _anchor_value(key) != value:
            continue
        var = body.split("\n")
        if len(var) != len(base):
            continue
        diff = [i for i in range(len(base)) if base[i] != var[i]]
        if diff and diff != [line]:
            continue
        return var[line]
    return None


def set_param(body, name, value, entry=None):
    """Write `value` into the axis' own line, leaving the REST of the line intact.

    Only the one number moves. `blown_bottle` writes `kblow = 0.42 + kdrift`, and a
    substitution that rebuilt the line from its head and its comment dropped the
    `+ kdrift` — which is the entry's own wobble (`; the blow is never quite steady`),
    so the page served a standing tone for a body whose whole point is that it does not
    stand. Measured on that bug: the centroid travelled 202 Hz in the entry's own body
    and 7 Hz in what the page rendered.
    """
    lines = body.split("\n")
    i = named_site(body, name)
    site = None if i is not None else (derived_site(entry, name) if entry else None)
    if i is None:
        if site is None:
            raise SystemExit(f"{name!r}: no line names it and its anchor bodies do not "
                             f"derive one")
        i = site[0]
    verbatim = anchor_line(entry, name, value, i) if entry else None
    if verbatim is not None:
        lines[i] = verbatim
        return "\n".join(lines)
    code, comment = _split_comment(lines[i])
    if site is None:                      # a named line: its first number, after the `=`
        head, sep, tail = code.partition("=")
        m = NUM.search(tail)
        if not m:
            raise SystemExit(f"{name!r}: line {i} carries no number to write")
        lines[i] = f"{head}{sep}{tail[:m.start()]}{value:g}{tail[m.end():]}{comment}"
        return "\n".join(lines)
    _, j, slope, icept = site
    spans = [m.span() for m in NUM.finditer(code)]
    if len(spans) <= j:
        raise SystemExit(f"{name!r}: line {i} no longer carries {j + 1} numbers")
    a, b = spans[j]
    # `+ 0.0` so a least-squares intercept of -9.96e-17 does not reach the orchestra as
    # the literal `-0`.
    lines[i] = f"{code[:a]}{round(slope * value + icept, 6) + 0.0:g}{code[b:]}{comment}"
    return "\n".join(lines)


def variant_bodies(entry, name):
    """The entry's OWN alternative bodies for one axis, out of `anchor_code`.

    Used for the axes whose anchor bodies are NOT a single-site substitution and so
    cannot become a control: `fm_ep`'s `ting`, `drum_head`'s `pitched`, `string`'s
    `bow`. Those are the entry's own statement of what the word does, so rendering them
    invents nothing — but they are fixed bodies, so they can only be offered one at a
    time and never crossed.
    """
    out = {}
    for k, body in (entry.get("anchor_code") or {}).items():
        if not k.startswith(f"{name}="):
            continue
        head, _, gloss = k.partition("(")
        value = head.split("=", 1)[1].strip().split()[0]
        anchor = gloss.split(":", 1)[0].strip() if gloss else value
        out[anchor] = (value, gloss.rstrip(") ").split(":", 1)[-1].strip(), body)
    return out


def reachable(entry, name):
    """Can this axis be written into the entry's code — by name, or by derivation?

    Not every declared parameter can. Audited 2026-07-30: thirteen carry range, default
    and anchor words while the entry names no control for them and states no
    substitution, so the value sits in the code as one fixed number. Which number, and
    on what curve, the entry does not say — putting such an axis on the page means
    INVENTING its wiring, which is authoring the instrument rather than auditioning it.
    The page lists them and does not fake them. `--audit` prints the whole picture.
    """
    try:
        set_param(entry["code"], name, entry["params"][name]["default"], entry)
        return True
    except SystemExit:
        return False


# The plugin does NOT run Csound at the host rate. Since `5e65c0d4` the engine compiles
# at `sampleRate * factor` — 4 by default, a Settings control since `667d8bea` — and
# decimates each voice back with a 63-tap halfband FIR (`CsoundEngine::prepare`,
# `src/gui/PromptPanel.cpp` on the probe path: "1x is worse than the 2x this project
# measured as audibly dirty"). A page rendered at a bare 44100 therefore hands the
# listener folding the instrument does not produce — and the approval that page carries
# would be an approval of an artefact. So render at the rate the body is written for and
# decimate to the rate the ear is given, which is what the engine does.
#
# NOT the plugin's filter: `resample_poly` is a Kaiser-windowed polyphase FIR, not the
# engine's 63-tap halfband. Close enough that the page is no longer measuring its own
# decimator instead of the body — and stated on the page, because a second filter that
# is not named reads as the first one (`tools/lco_measure.py` makes the same point
# about MEASURING through a substitute decimator).
OS = 4


def render(body, dur, freq):
    rate = M.SR
    try:
        M.SR = int(rate * OS)
        y, err = M.render(body, dur=dur, freq=freq)
    finally:
        M.SR = rate
    if err:
        raise SystemExit(f"render failed: {err[:400]}")
    return resample_poly(y.astype(float), 1, OS)


PAGE = """<!doctype html><meta charset=utf-8><title>{key} — Parameterkombination</title>
<style>
:root{{color-scheme:light dark}}
body{{font:15px/1.55 system-ui,sans-serif;margin:0 24px 90px;max-width:940px}}
h1{{font-size:21px;margin:18px 0 2px}}
h2{{font-size:15px;margin:30px 0 8px;border-top:2px solid #8884;padding-top:12px}}
#bar{{position:sticky;top:0;background:Canvas;padding:12px 0 10px;z-index:20;
border-bottom:2px solid #8886}}
#combo{{font:600 16px ui-monospace,monospace;margin-bottom:8px}}
#player{{width:100%;height:36px}}
.note{{border:1px solid #8884;border-radius:7px;padding:10px 13px;font-size:13px;
margin:12px 0;background:#8881}}
.axis{{margin:16px 0}}
.axis .name{{font:600 14px ui-monospace,monospace;margin-bottom:2px}}
.axis .desc{{font-size:12.5px;opacity:.75;margin-bottom:7px}}
.opts{{display:flex;flex-wrap:wrap;gap:8px}}
.opt{{cursor:pointer;border:1px solid #8886;border-radius:7px;padding:8px 12px;
background:#8881;text-align:left;font:14px system-ui;min-width:150px}}
.opt:hover{{background:#8883}}
.opt[aria-pressed=true]{{background:#f9a825;border-color:#f57f17;color:#000}}
.opt b{{display:block;font-size:14px}}
.opt small{{display:block;font-size:11.5px;opacity:.8;margin-top:3px;max-width:230px}}
.opt code{{font-size:11px;opacity:.7}}
table{{border-collapse:collapse;font-size:13px;margin-top:8px}}
th,td{{border:1px solid #8886;padding:5px 9px;text-align:right}}
td:first-child,th:first-child{{text-align:left}}
code{{background:#8882;padding:1px 4px;border-radius:3px}}
</style>
<div id='bar'>
  <div id='combo'>—</div>
  <audio id='player' controls preload='none'></audio>
</div>

<h1>{key} — Parameterkombination</h1>
<div class='note'>{why}</div>

<div class='note'><b>Wie zu lesen.</b> Jede Achse unten steht auf ihren im Eintrag
<i>deklarierten</i> Ankern. Oben steht immer die vollst&auml;ndige Kombination, die gerade
klingt &mdash; nichts ist f&uuml;r dich ausgew&auml;hlt worden, was du nicht siehst.
<b>Ein einziger Pegelfaktor f&uuml;r den ganzen Satz</b> ({gain:+.2f}&nbsp;dB), keine
Normalisierung pro Datei. {loudness}Note {freq:.0f}&nbsp;Hz, {dur:.0f}&nbsp;s lang.{whydur}
<b>Gerendert bei {osrate:.0f}&nbsp;Hz und auf {rate:.0f}&nbsp;Hz heruntergesetzt</b>, weil das
Instrument es so tut: die Engine übersetzt den Csound-Text bei Hostrate&nbsp;&times;&nbsp;{os}
und dezimiert jede Stimme zurück. Bei blossen {rate:.0f}&nbsp;Hz zu rendern legt eine
Faltung über den Klang, die im Instrument nicht vorkommt. Das Filter hier ist ein
Kaiser-Polyphasen-FIR, nicht der 63-Takt-Halbband-FIR der Engine &mdash; nah genug, um
nicht das Filter statt des Körpers zu hören, aber nicht dasselbe.</div>

{axes}

{paired}

<h2>Was im Kreuz steht</h2>
<div class='note'>{ncomb} Kombinationen aus {naxes} Achsen, jede an ihren Ankern.
Gerendert direkt gegen das Csound-Ger&uuml;st, aus dem Eintrag im Lexikon &mdash; also
genau der Code, den das schreibende Modell zu sehen bekommt.</div>
{table}

<script>
var STATE={state}, DUR={dur};
var P=document.getElementById('player'), C=document.getElementById('combo');
function name(){{ return AXES.map(function(a){{return STATE[a];}}).join('__'); }}
var AXES={axes_json};
var LABEL={label_json};
function refresh(auto){{
  if(!AXES.length){{ C.textContent='\u2014 keine kreuzbare Achse: alles einzeln, unten \u2014'; return; }}
  C.textContent = AXES.map(function(a){{return a+' = '+LABEL[a][STATE[a]];}}).join('   |   ');
  var f='clips/'+name()+'.wav';
  if(P.src.indexOf(f)<0){{ P.src=f; if(auto) P.play(); }}
  // the paired buttons carry no axis, so they must be skipped: STATE[undefined] ===
  // b.dataset.val would be undefined === undefined, and all of them would read as
  // selected at once.
  document.querySelectorAll('.opt:not([data-paired])').forEach(function(b){{
    b.setAttribute('aria-pressed', STATE[b.dataset.axis]===b.dataset.val ? 'true':'false');
  }});
}}
document.addEventListener('click',function(e){{
  var b=e.target.closest('.opt'); if(!b) return;
  if(b.dataset.paired){{ P.src=b.dataset.src; P.play(); C.textContent=b.dataset.label; return; }}
  STATE[b.dataset.axis]=b.dataset.val; refresh(true);
}});
refresh(false);
</script>
"""


def audit():
    """Every declared axis in the lexicon, and how — or whether — it reaches the code."""
    rows = []
    for e in json.loads(LEX.read_text())["techniques"]:
        for name in (e.get("params") or {}):
            site = derived_site(e, name)
            try:
                named = named_site(e["code"], name) is not None
            except Ambiguous as exc:
                rows.append((e["key"], name, f"MEHRDEUTIG — {exc}"))
                continue
            if named:
                how = "benannt"
            elif site:
                line, j, m, c = site
                how = (f"abgeleitet aus den Ankerkörpern: Zeile {line}, "
                       f"Zahl {j + 1} = {round(m, 6) + 0.0:g}·{name} "
                       f"{round(c, 6) + 0.0:+g}")
            elif variant_bodies(e, name):
                how = "nur feste Varianten (mehrzeilige Körper, kein Regler)"
            else:
                how = "GAR NICHT — keine Zeile, keine Ankerkörper"
            rows.append((e["key"], name, how))
    w = max(len(k) for k, _, _ in rows)
    last = None
    for key, name, how in rows:
        print(f"{key if key != last else '':{w}}  {name:12}  {how}")
        last = key
    n = sum(1 for _, _, h in rows if h.startswith("GAR NICHT"))
    print(f"\n{len(rows)} deklarierte Achsen, davon {n} ohne jede Entsprechung im Code.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("key", nargs="?",
                    help="the lexicon entry to build a page for; omit with --audit")
    ap.add_argument("--audit", action="store_true",
                    help="print every declared axis in the lexicon and how — or whether "
                         "— it reaches its entry's code, and render nothing")
    ap.add_argument("--freq", type=float, default=220.0)
    ap.add_argument("--dur", type=float, default=12.0,
                    help="note length in seconds; long by default, see the module docstring")
    ap.add_argument("--axes", default=None,
                    help="comma-separated subset of the entry's parameters to cross")
    ap.add_argument("--sweep", default="",
                    help="axes NOT crossed: each anchor rendered with the others at their "
                         "defaults. For entries whose full cross does not fit — fm3's six "
                         "axes are 5120 combinations, which is not a listening test.")
    args = ap.parse_args()
    if args.audit:
        return audit()
    if not args.key:
        ap.error("give an entry key, or --audit")
    if args.dur < 2.0:
        # the level column is measured over 0.5 … dur-0.5, which below this is an empty
        # slice and puts `nan` in the table instead of failing
        ap.error(f"--dur {args.dur} is shorter than the measurement window; use ≥ 2")

    e = entry_of(args.key)
    params = e.get("params") or {}
    if not params:
        raise SystemExit(f"{args.key!r} carries no parameters — nothing to combine")

    sweep_ax = [a.strip() for a in args.sweep.split(',') if a.strip()]
    unknown = [a for a in sweep_ax if a not in params]
    if unknown:
        raise SystemExit(f'not parameters of {args.key!r}: {unknown}')
    cross = [p for p in params if p not in PAIRED and p not in sweep_ax]
    if args.axes:
        want = [a.strip() for a in args.axes.split(",")]
        unknown = [a for a in want if a not in params]
        if unknown:
            raise SystemExit(f"not parameters of {args.key!r}: {unknown}")
        cross = [a for a in want if a not in PAIRED and a not in sweep_ax]
    paired = [p for p in params if p in PAIRED and (not args.axes or p in args.axes)]

    # An axis with neither a line naming it nor a derivable site cannot be rendered
    # without inventing its wiring. Drop it from the cross and say so on the page.
    unreachable = [a for a in cross + sweep_ax + paired if not reachable(e, a)]
    # ... unless the entry carries its own variant bodies for it, which are the entry's
    # statement and not an invention. Those rewrite whole passages rather than one
    # number, so they cannot be crossed and join the one-at-a-time section.
    variant_ax = [a for a in unreachable if variant_bodies(e, a)]
    unreachable = [a for a in unreachable if a not in variant_ax]
    cross = [a for a in cross if a not in unreachable and a not in variant_ax]
    sweep_ax = [a for a in sweep_ax if a not in unreachable and a not in variant_ax]
    paired = [a for a in paired if a not in unreachable and a not in variant_ax]
    derived = [a for a in cross + sweep_ax + paired
               if named_site(e["code"], a) is None]
    if derived:
        print(f'  Verdrahtung aus den Ankerkörpern abgeleitet: {derived}')
    if variant_ax:
        print(f'  nur als fertige Varianten-Körper im Eintrag, daher einzeln: {variant_ax}')
    if unreachable:
        print(f'  nicht im Code des Eintrags erreichbar, daher nicht gerendert: {unreachable}')
    if not (cross or variant_ax or sweep_ax or paired):
        raise SystemExit(f'{args.key!r}: keine Achse übrig, die sich vorführen ließe. '
                         f'Nicht im Code erreichbar: {unreachable or "—"}.')


    anchors = {a: list((params[a].get("anchors") or {}).items())
               for a in cross + sweep_ax + paired}
    for a, v in anchors.items():
        if not v:
            raise SystemExit(f"{a!r} declares no anchors — the page has nothing to show")

    out = OUTROOT / f"{args.key}_params"
    (out / "clips").mkdir(parents=True, exist_ok=True)

    # With no crossable axis at all -- `fm` has none, every one of its three exists only
    # as a fixed variant body -- itertools.product() still yields one empty tuple, which
    # would render a clip called "". There is simply no cross to render then.
    combos = (list(itertools.product(*[[n for n, _ in anchors[a]] for a in cross]))
              if cross else [])
    print(f"{args.key}: {len(cross)} axes crossed → {len(combos)} combinations, "
          f"{args.dur:.0f} s at {args.freq:.0f} Hz")

    rendered, rows = {}, []
    for combo in combos:
        body = e["code"]
        for ax, nm in zip(cross, combo):
            body = set_param(body, ax, dict(anchors[ax])[nm]["value"], e)
        y = render(body, args.dur, args.freq)
        rendered["__".join(combo)] = y
        rows.append((combo, M.rms_db(y, 0.5, args.dur - 0.5), M.peak_p999(y), M.centroid(y)))
        print(f"  {'  '.join(f'{a}={n}' for a, n in zip(cross, combo)):58} "
              f"{rows[-1][1]:7.2f} dBrms")

    # the swept axes: not crossed, so each anchor is rendered with every OTHER axis at
    # its declared default. Their section says so, because a value heard at one point of
    # the space is not the same claim as a value heard across it.
    sweep_files = {}
    for ax in sweep_ax:
        for nm, spec in anchors[ax]:
            body = e["code"]
            for other in cross + sweep_ax:
                if other != ax:
                    body = set_param(body, other, params[other]["default"], e)
            body = set_param(body, ax, spec["value"], e)
            y = render(body, args.dur, args.freq)
            rendered[f"sweep_{ax.replace(' ','')}_{nm.replace(' ','')}"] = y
            sweep_files[(ax, nm)] = (spec, f"clips/sweep_{ax.replace(' ','')}_{nm.replace(' ','')}.wav")
            print(f"  {ax}={nm:14} (übrige auf Vorgabe)  {M.rms_db(y,0.5,args.dur-0.5):7.2f} dBrms")

    variant_files = {}
    for ax in variant_ax:
        for nm, (val, gloss, body) in variant_bodies(e, ax).items():
            y = render(body, args.dur, args.freq)
            tag = f"variant_{ax.replace(' ','')}_{nm.replace(' ','')}"
            rendered[tag] = y
            variant_files[(ax, nm)] = ({"value": val, "gloss": gloss}, f"clips/{tag}.wav")
            print(f"  {ax}={nm:14} (Varianten-Körper)     {M.rms_db(y,0.5,args.dur-0.5):7.2f} dBrms")

    # the paired axes: two layers summed, because that is the only way this control is
    # audible at all -- see PAIRED above
    paired_files = {}
    for ax in paired:
        base = render(set_param(e["code"], ax, 0.0, e), args.dur, args.freq)
        for nm, spec in anchors[ax]:
            y = render(set_param(e["code"], ax, spec["value"], e), args.dur, args.freq)
            n = min(len(base), len(y))
            mix = 0.5 * (base[:n] + y[:n])
            rendered[f"paired_{ax}_{nm}"] = mix
            paired_files[(ax, nm)] = (spec, f"clips/paired_{ax}_{nm}.wav")
            print(f"  {ax}={nm:10} (zwei Schichten summiert)   {M.rms_db(mix,0.5,args.dur-0.5):7.2f} dBrms")

    # ONE gain for the whole set
    pk = max(float(np.percentile(np.abs(y), 99.9)) for y in rendered.values())
    g = 10 ** (-1.0 / 20) / pk
    for name, y in rendered.items():
        sf.write(str(out / "clips" / f"{name}.wav"), np.clip(y * g, -1, 1), int(M.SR))

    # ---- page ----
    def esc(s):
        return html.escape(str(s))

    # An entry may DECLARE that an axis moves the level, on its own `; LOUDNESS: <axis>`
    # line. That is the reason the set shares one gain, so the page states it from the
    # entry rather than from a sentence written for one instrument.
    loud_axes = [m.group(1).strip() for m in
                 re.finditer(r"^;\s*LOUDNESS:\s*(.+)$", e["code"], re.M)]

    def derived_note(ax):
        """Say so wherever a derived axis appears — cross, sweep or paired alike."""
        site = derived_site(e, ax) if ax in derived else None
        if not site:
            return ""
        return (f"<div class='desc'><b>Verdrahtung abgeleitet.</b> Der Eintrag hat keine "
                f"Zeile, die <code>{esc(ax)}</code> beim Namen nennt &mdash; aber unter "
                f"<code>anchor_code</code> steht pro Anker ein ganzer K&ouml;rper, und "
                f"alle unterscheiden sich vom ausgelieferten Code in genau einer Zahl in "
                f"genau einer Zeile (Zeile {site[0]}). Diese Seite spielt an jedem Anker "
                f"den K&ouml;rper des Eintrags Zeichen f&uuml;r Zeichen; kombiniert wird "
                f"&uuml;ber die Gerade durch diese Punkte "
                f"({round(site[2], 6) + 0.0:g}&middot;{esc(ax)} "
                f"{round(site[3], 6) + 0.0:+g}), die den ausgelieferten Code an seiner "
                f"eigenen Vorgabe wiedertrifft &mdash; einer Vorgabe, die nicht unter "
                f"den gefitteten Punkten steckt, sonst pr&uuml;fte sie nichts.</div>")

    axes_html = []
    for ax in cross:
        spec = params[ax]
        opts = "".join(
            f"<button class='opt' data-axis='{ax}' data-val='{esc(nm)}' aria-pressed='false'>"
            f"<b>{esc(nm)}</b><code>{ax} = {v['value']}</code>"
            f"<small>{esc(v.get('gloss',''))}</small></button>"
            for nm, v in anchors[ax])
        axes_html.append(
            f"<div class='axis'><div class='name'>{ax} &nbsp;[{spec['range'][0]} … "
            f"{spec['range'][1]}]&nbsp; Vorgabe {spec['default']}</div>"
            f"<div class='desc'>{esc(spec.get('note','')[:340])}…</div>{derived_note(ax)}"
            f"<div class='opts'>{opts}</div></div>")

    sweep_html = ""
    if sweep_files:
        blocks = []
        for ax in sweep_ax:
            items = [(k, v) for k, v in sweep_files.items() if k[0] == ax]
            opts = "".join(
                f"<button class='opt' data-paired='1' data-src='{src}' "
                f"data-label='{esc(ax)} = {esc(nm)} ({sp['value']}) — übrige Achsen auf Vorgabe'>"
                f"<b>{esc(nm)}</b><code>{esc(ax)} = {sp['value']}</code>"
                f"<small>{esc(sp.get('gloss',''))}</small></button>"
                for (a, nm), (sp, src) in items)
            blocks.append(f"<div class='axis'><div class='name'>{esc(ax)}</div>"
                          f"<div class='desc'>{esc(params[ax].get('note','')[:300])}…</div>{derived_note(ax)}"
                          f"<div class='opts'>{opts}</div></div>")
        sweep_html = ("<h2>Nicht gekreuzt: einzeln, alles andere auf Vorgabe</h2>"
            "<div class='note'><b>Warum getrennt.</b> Alle "
            + str(len(cross) + len(sweep_ax)) + " Achsen zu kreuzen w\u00e4re "
            + str(int(np.prod([len(anchors[a]) for a in cross + sweep_ax])))
            + " Kombinationen &mdash; kein H\u00f6rtest, sondern eine Halde. Gekreuzt sind "
            + ", ".join(f"<code>{esc(a)}</code>" for a in cross)
            + "; hier stehen " + ", ".join(f"<code>{esc(a)}</code>" for a in sweep_ax)
            + " einzeln, mit jeder anderen Achse auf ihrer Vorgabe. Was du hier h\u00f6rst, "
            "ist also EIN Punkt des Raums, nicht die Achse \u00fcber ihn hinweg.</div>"
            + "".join(blocks))

    variant_html = ""
    if variant_files:
        blocks = []
        for ax in variant_ax:
            opts = "".join(
                f"<button class='opt' data-paired='1' data-src='{src}' "
                f"data-label='{esc(ax)} = {esc(nm)} ({sp[chr(118)+chr(97)+chr(108)+chr(117)+chr(101)]}) — eigener Körper aus dem Eintrag'>"
                f"<b>{esc(nm)}</b><code>{esc(ax)} = {sp['value']}</code>"
                f"<small>{esc(sp.get('gloss',''))}</small></button>"
                for (a, nm), (sp, src) in variant_files.items() if a == ax)
            blocks.append(f"<div class='axis'><div class='name'>{esc(ax)}</div>"
                          f"<div class='desc'>{esc(params[ax].get('note','')[:300])}…</div>"
                          f"<div class='opts'>{opts}</div></div>")
        variant_html = ("<h2>Feste Varianten aus dem Eintrag, nicht kombinierbar</h2>"
            "<div class='note'>F&uuml;r " + ", ".join(f"<code>{esc(a)}</code>" for a in variant_ax)
            + " steht unter <code>anchor_code</code> pro Anker ein ganzer K&ouml;rper, und "
              "die schreiben ganze Passagen um statt einer Zahl. Das ist die Aussage des "
              "Eintrags selbst, also erfinde ich nichts &mdash; aber es sind FESTE "
              "K&ouml;rper und kein Regler, deshalb einzeln und nicht im Kreuz.</div>"
            + "".join(blocks))

    missing_html = ""
    if unreachable:
        missing_html = ("<h2>Deklariert, aber kein Regler: eine feste Zahl im Code</h2>"
            "<div class='note'>" + ", ".join(f"<code>{esc(a)}</code>" for a in unreachable)
            + " tr\u00e4gt im Eintrag Bereich, Vorgabe und Ankerw\u00f6rter, aber der "
            "<code>code</code> nennt die Achse nirgends und die Ankerk\u00f6rper leiten "
            "keine Stelle her. Die GR&Ouml;SSE steckt durchaus im Code &mdash; "
            "<code>fm</code>s Schwebung etwa als das feste <code>+ 1.2</code>&nbsp;Hz, um "
            "das die zweite Stimme h\u00f6her steht &mdash; nur eben als eine Zahl an "
            "einem Punkt, und welche Zahl der Regler bewegen soll und auf welcher Kurve, "
            "sagt der Eintrag nicht. Das zu entscheiden hie\u00dfe, die Verdrahtung zu "
            "ERFINDEN: das Instrument schreiben statt es vorzuspielen. Die Achse steht "
            "deshalb hier und nicht oben, und was du oben h\u00f6rst, steht auf ihrer "
            "Vorgabe.</div>")

    paired_html = ""
    if paired_files:
        blocks = []
        for ax in paired:
            opts = "".join(
                f"<button class='opt' data-paired='1' data-src='{src}' "
                f"data-label='{ax} = {esc(nm)} — zwei Schichten, 0 und {sp['value']} ct'>"
                f"<b>{esc(nm)}</b><code>{ax} = {sp['value']}</code>"
                f"<small>{esc(sp.get('gloss',''))}</small></button>"
                for (a, nm), (sp, src) in paired_files.items() if a == ax)
            blocks.append(
                f"<div class='axis'><div class='name'>{ax}</div>"
                f"<div class='desc'>{esc(params[ax].get('note','')[:300])}…</div>"
                f"{derived_note(ax)}<div class='opts'>{opts}</div></div>")
        paired_html = (
            "<h2>Getrennt: was nur gegen eine zweite Schicht h&ouml;rbar ist</h2>"
            "<div class='note'><b>Nicht im Kreuz oben, und warum.</b> "
            + ", ".join(f"<code>{a}</code>" for a in paired) +
            " verstimmt gegen eine ANDERE Schicht &mdash; allein gerendert ist das nur eine"
            " anders gestimmte Note und sagt &uuml;ber den Regler nichts. Hier sind deshalb"
            " zwei Renderings summiert, eines bei 0 und eines beim Ankerwert: das ist, was"
            " der Synth mit zwei Stimmen tut.</div>" + "".join(blocks))

    thead = "".join(f"<th>{esc(a)}</th>" for a in cross)
    tbody = "".join(
        "<tr>" + "".join(f"<td>{esc(n)}</td>" for n in combo)
        + f"<td>{r:.2f}</td><td>{p:.3f}</td><td>{c:.0f}</td></tr>"
        for combo, r, p, c in rows)

    if not cross:
        PAGE_T = PAGE.replace("""<h2>Was im Kreuz steht</h2>
<div class='note'>{ncomb} Kombinationen aus {naxes} Achsen, jede an ihren Ankern.
Gerendert direkt gegen das Csound-Ger&uuml;st, aus dem Eintrag im Lexikon &mdash; also
genau der Code, den das schreibende Modell zu sehen bekommt.</div>
{table}""", "")
    else:
        PAGE_T = PAGE

    page = PAGE_T.format(
        key=esc(args.key),
        why=esc(e.get("why", "")[:900]) + "…",
        gain=20 * np.log10(g), freq=args.freq, dur=args.dur,
        os=OS, osrate=M.SR * OS, rate=M.SR,
        # Why the default is long, stated as what it IS — a property of this generator,
        # not of the entry on screen. It used to name `analog_osc`'s 0.043 Hz on every
        # page, including `divider_organ`'s, whose slowest motion is fifteen times
        # faster; a page that explains itself with another instrument's numbers is
        # exactly the kind of thing this page exists to stop.
        whydur=" Lang, weil manche K&ouml;rper hier auf Perioden von rund 20&nbsp;Sekunden "
               "wandern (<code>analog_osc</code>s <code>age</code> mit 0,043&nbsp;Hz) und "
               "ein kurzer Reiz eine solche Achse gar nicht zeigt.",
        loudness=(
            "Dieser Eintrag erkl&auml;rt in seinem eigenen Code, dass "
            + " und ".join(f"<code>{esc(a)}</code>" for a in loud_axes)
            + " die Lautst&auml;rke bewegen (<code>; LOUDNESS:</code>), und das ist der Klang, "
              "nicht ein Fehler. " if loud_axes else
            "Was ein Parameter am Pegel tut, bleibt so h&ouml;rbar. "),
        ncomb=len(combos), naxes=len(cross),
        axes="".join(axes_html), paired=variant_html + sweep_html + missing_html + paired_html,
        state=json.dumps({a: anchors[a][0][0] for a in cross}),
        axes_json=json.dumps(cross),
        label_json=json.dumps({a: {nm: f"{nm} ({v['value']})" for nm, v in anchors[a]}
                               for a in cross}),
        table=f"<table><tr>{thead}<th>dBrms</th><th>Peak</th><th>Schwerpunkt Hz</th></tr>"
              f"{tbody}</table>")
    (out / "index.html").write_text(page, encoding="utf-8")
    print(f"\none gain for the set: {20*np.log10(g):+.2f} dB")
    print(f"→ {out/'index.html'}")


if __name__ == "__main__":
    main()
