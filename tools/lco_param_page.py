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
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

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


def set_param(body, name, value):
    """Replace the axis line's value, leaving its comment intact.

    Same contract as `lco_listening_test.set_param`: exactly one `k<name> =` line must
    match, so a renamed or duplicated axis fails loudly instead of rendering the default.
    """
    out, hit = [], 0
    for line in body.split("\n"):
        s = line.strip()
        if s.startswith(f"k{name}") and "=" in s and not s.startswith(f"k{name}_"):
            head, _, tail = line.partition("=")
            rest = tail.split(";", 1)
            comment = f";{rest[1]}" if len(rest) > 1 else ""
            out.append(f"{head}= {value:<6.4f} {comment}".rstrip())
            hit += 1
        else:
            out.append(line)
    if hit != 1:
        raise SystemExit(f"expected exactly one `k{name} =` line for {name!r}, found {hit}")
    return "\n".join(out)


def render(body, dur, freq):
    y, err = M.render(body, dur=dur, freq=freq)
    if err:
        raise SystemExit(f"render failed: {err[:400]}")
    return y.astype(float)


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
Normalisierung pro Datei: dieser Eintrag erkl&auml;rt in seinem eigenen Code, dass
<code>width</code> und <code>wave</code> die Lautst&auml;rke bewegen, und das ist der
Klang, nicht ein Fehler. Note {freq:.0f}&nbsp;Hz, {dur:.0f}&nbsp;s lang &mdash; so lang,
weil <code>age</code> mit 0,043 und 0,057&nbsp;Hz wandert, also mit Perioden von 23 und
18&nbsp;Sekunden; k&uuml;rzer zeigt diese Achse gar nichts.</div>

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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("key")
    ap.add_argument("--freq", type=float, default=220.0)
    ap.add_argument("--dur", type=float, default=12.0,
                    help="note length in seconds; long by default, see the module docstring")
    ap.add_argument("--axes", default=None,
                    help="comma-separated subset of the entry's parameters to cross")
    args = ap.parse_args()

    e = entry_of(args.key)
    params = e.get("params") or {}
    if not params:
        raise SystemExit(f"{args.key!r} carries no parameters — nothing to combine")

    cross = [p for p in params if p not in PAIRED]
    if args.axes:
        want = [a.strip() for a in args.axes.split(",")]
        unknown = [a for a in want if a not in params]
        if unknown:
            raise SystemExit(f"not parameters of {args.key!r}: {unknown}")
        cross = [a for a in want if a not in PAIRED]
    paired = [p for p in params if p in PAIRED and (not args.axes or p in args.axes)]

    anchors = {a: list((params[a].get("anchors") or {}).items()) for a in cross + paired}
    for a, v in anchors.items():
        if not v:
            raise SystemExit(f"{a!r} declares no anchors — the page has nothing to show")

    out = OUTROOT / f"{args.key}_params"
    (out / "clips").mkdir(parents=True, exist_ok=True)

    combos = list(itertools.product(*[[n for n, _ in anchors[a]] for a in cross]))
    print(f"{args.key}: {len(cross)} axes crossed → {len(combos)} combinations, "
          f"{args.dur:.0f} s at {args.freq:.0f} Hz")

    rendered, rows = {}, []
    for combo in combos:
        body = e["code"]
        for ax, nm in zip(cross, combo):
            body = set_param(body, ax, dict(anchors[ax])[nm]["value"])
        y = render(body, args.dur, args.freq)
        rendered["__".join(combo)] = y
        rows.append((combo, M.rms_db(y, 0.5, args.dur - 0.5), M.peak_p999(y), M.centroid(y)))
        print(f"  {'  '.join(f'{a}={n}' for a, n in zip(cross, combo)):58} "
              f"{rows[-1][1]:7.2f} dBrms")

    # the paired axes: two layers summed, because that is the only way this control is
    # audible at all -- see PAIRED above
    paired_files = {}
    for ax in paired:
        base = render(set_param(e["code"], ax, 0.0), args.dur, args.freq)
        for nm, spec in anchors[ax]:
            y = render(set_param(e["code"], ax, spec["value"]), args.dur, args.freq)
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
            f"<div class='desc'>{esc(spec.get('note','')[:340])}…</div>"
            f"<div class='opts'>{opts}</div></div>")

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
                f"<div class='opts'>{opts}</div></div>")
        paired_html = (
            "<h2>Getrennt: was nur gegen eine zweite Schicht h&ouml;rbar ist</h2>"
            "<div class='note'><b>Nicht im Kreuz oben, und warum.</b> "
            + ", ".join(f"<code>{a}</code>" for a in paired) +
            " verstimmt gegen eine ANDERE Schicht &mdash; allein gerendert ist das nur eine"
            " anders gestimmte Note und sagt &uuml;ber den Regler nichts. Hier sind deshalb"
            " zwei Renderings summiert, eines bei 0 und eines beim Ankerwert: das ist, was"
            " der Synth mit zwei Stimmen tut.</div>" + "".join(blocks))

    thead = "".join(f"<th>{a}</th>" for a in cross)
    tbody = "".join(
        "<tr>" + "".join(f"<td>{esc(n)}</td>" for n in combo)
        + f"<td>{r:.2f}</td><td>{p:.3f}</td><td>{c:.0f}</td></tr>"
        for combo, r, p, c in rows)

    page = PAGE.format(
        key=esc(args.key),
        why=esc(e.get("why", "")[:900]) + "…",
        gain=20 * np.log10(g), freq=args.freq, dur=args.dur,
        ncomb=len(combos), naxes=len(cross),
        axes="".join(axes_html), paired=paired_html,
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
