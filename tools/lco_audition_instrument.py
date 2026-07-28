#!/usr/bin/env python3
"""Play one instrument as its controls COMBINE -- each control against each other one.

BEFORE any word is put on trial, the instrument itself has to be heard. A word test
asks whether `worn` reaches the sound it names; it cannot say whether the oscillator
is any good, or where a control stops being usable.

And a row per control, everything else at its default, answers the wrong question.
Neither the author model nor the player moves one knob: both set them all at once,
and BJ (2026-07-28) confirmed these are the same controls the player will see in the
Prompt Orchestra field. A control that behaves alone and falls apart in company is
exactly the defect this page has to be able to show.

So: one small table per PAIR of controls -- rows one control, columns the other,
everything else on its default. Seven controls make 21 tables. What is on trial in
each is whether the row control still means the same thing in every column.

A first version laid out 32 corners of the whole cube as a flat list of seven-number
settings instead. It is a defensible experimental design and it is unreadable: a
human cannot hear "0 . 0.95 . 1 . 0.05 . -50 . 0 . 0" as a question. A combination of
two named controls is a question; a preset is not.

A control that does nothing until another one switches it on is rendered with that
one switched on (`width`, `pwm` and `rate` need `wave` at pulse), or its table is
nine copies of the same saw. Where the enabling control is one of the pair, it is
left free.

One gain for the whole page, so loudness differences between clips are the
instrument's own and nothing clips on export.

One player. A click plays, a click on another clip replaces the running one, a
click on the yellow one stops.
"""

from __future__ import annotations

import argparse
import itertools
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import soundfile as sf

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

import lco_measure as M  # noqa: E402

LEX = REPO / "backend" / "dco_lexicon.json"
PREROLL = 0.5
PAGE_PEAK = 0.89
STEPS = 3         # levels per axis in a pair table
SOLO_STEPS = 5    # a body with only two controls has one table, so it can afford more

# A control that is inert until another one is set, and what sets it. Applied to every
# pair except where the enabling control is itself one of the two on trial.
CONTEXT = {
    "analog_osc": {"width": {"wave": 1.0}, "pwm": {"wave": 1.0},
                   "rate": {"wave": 1.0, "pwm": 1.0}},
}


def entry_of(key, lex=None):
    for t in json.loads((lex or LEX).read_text())["techniques"]:
        if t["key"] == key:
            return t
    raise SystemExit(f"no entry {key!r} in {lex or LEX}")


def set_params(body, **vals):
    out = body
    for name, value in vals.items():
        lines, hit = [], 0
        for line in out.split("\n"):
            if re.match(rf"k{name}\s*=", line.strip()) and ";" in line:
                head, _, tail = line.partition("=")
                lines.append(f"{head}= {value:<7.4g}{';' + tail.split(';', 1)[1]}")
                hit += 1
            else:
                lines.append(line)
        if hit != 1:
            raise SystemExit(f"expected one `k{name} =` line, found {hit}")
        out = "\n".join(lines)
    return out


PAGE = """<!doctype html><meta charset=utf-8><title>{title}</title>
<style>
body{{font:15px system-ui;margin:0 24px 90px;max-width:1080px;color:#222}}
h1{{font-size:20px;margin-bottom:4px}}
h2{{font-size:15px;margin:26px 0 6px;font-weight:600}}
#now{{position:sticky;top:0;background:#fff;padding:12px 0;margin-bottom:6px;
border-bottom:2px solid #ccc;z-index:20;display:flex;align-items:center;gap:16px}}
#player{{width:330px;height:34px}} #nowlabel{{font-weight:bold;color:#333;font-size:13px}}
.note{{background:#f6f8fa;border:1px solid #dfe2e5;border-radius:6px;padding:10px 13px;
font-size:13px;margin:10px 0;line-height:1.65}}
.sub{{font-size:12.5px;color:#666;margin:0 0 8px;line-height:1.5}}
.row{{display:flex;gap:6px;flex-wrap:wrap}}
.pairs{{display:flex;flex-wrap:wrap;gap:22px 30px}}
.pair h3{{font-size:13.5px;margin:0 0 1px;font-weight:600}}
.pair p{{font-size:11.5px;color:#8a8a8a;margin:0 0 4px}}
.clip{{cursor:pointer;border:1px solid #bbb;background:#f4f4f4;border-radius:5px;
padding:6px 9px;font-size:12.5px;line-height:1.3;text-align:center;min-width:40px}}
.clip:hover{{background:#e7e7e7}} .clip.playing{{background:#ffca28;border-color:#f57f17}}
.clip b{{display:block;font-size:13px;font-weight:600}}
.clip span{{color:#777;font-size:11px}} .clip.playing span{{color:#7a5b00}}
table{{border-collapse:collapse}}
td,th{{padding:2px 3px;text-align:center}}
th{{font-size:11.5px;color:#555;font-weight:500;white-space:nowrap}}
th.rh{{text-align:right;padding-right:7px}}
th.corner{{text-align:right;padding-right:7px;color:#999;font-style:italic}}
code{{background:#eef1f4;padding:1px 4px;border-radius:3px;font-size:12px}}
</style>
<div id='now'><audio id='player' controls preload='auto'></audio>
<span id='nowlabel'>&mdash; nichts &mdash;</span></div>
<h1>{title}</h1>
<div class='note'>{intro}</div>
{sections}
<script>
var P=document.getElementById('player'),L=document.getElementById('nowlabel'),A=null;
function clr(){{if(A){{A.classList.remove('playing');A=null;}}L.textContent='\\u2014 nichts \\u2014';}}
document.addEventListener('click',function(e){{
  var b=e.target.closest('.clip'); if(!b) return;
  if(b===A){{P.pause();P.currentTime=0;clr();return;}}
  if(A)A.classList.remove('playing');
  A=b;b.classList.add('playing');L.textContent=b.dataset.label;
  P.src=b.dataset.src;P.play();
}});
P.addEventListener('ended',clr);
</script>
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--key", required=True)
    ap.add_argument("--freq", type=float, default=220.0)
    ap.add_argument("--dur", type=float, default=4.0)
    ap.add_argument("--compare", default="",
                    help="git-rev:key,... rendered from an older lexicon for comparison")
    ap.add_argument("--out", default=str(REPO / "tools" / "lco_listening"))
    args = ap.parse_args()

    inst = entry_of(args.key)
    out = Path(args.out) / f"{args.key}_audition"
    out.mkdir(parents=True, exist_ok=True)
    body = inst["code"]
    names = list(inst["params"])
    rng = {n: inst["params"][n]["range"] for n in names}
    dflt = {n: inst["params"][n]["default"] for n in names}
    ctx = CONTEXT.get(args.key, {})
    clips = []

    def render(code, freq=None):
        y, err = M.render(code, dur=args.dur, freq=freq or args.freq, preroll=PREROLL)
        if y is None:
            raise SystemExit(err)
        return y

    def add(y, name, label, big, small="", cls=""):
        clips.append((name, y))
        return (f"<button class='clip {cls}' data-src='{name}.wav' data-label=\"{label}\">"
                f"<b>{big}</b><span>{small}</span></button>")

    def steps(n, k):
        lo, hi = rng[n]
        return [lo + (hi - lo) * i / (k - 1) for i in range(k)]

    pairs = list(itertools.combinations(names, 2))
    k = SOLO_STEPS if len(pairs) == 1 else STEPS
    sections = ""

    if not pairs:
        a = names[0]
        row = "".join(add(render(set_params(body, **{a: v})), f"s{i}",
                          f"{a} = {v:g}", f"{v:g}")
                      for i, v in enumerate(steps(a, SOLO_STEPS)))
        sections += (f"<h2>{a}</h2><p class='sub'>Dieser Klangkörper hat nur einen Regler; "
                     f"es gibt nichts zu kombinieren.</p><div class='row'>{row}</div>")
    else:
        blocks = ""
        for pi, (a, b) in enumerate(pairs):
            fixed = {}
            for who in (a, b):
                for cn, cv in ctx.get(who, {}).items():
                    if cn not in (a, b):
                        fixed[cn] = cv
            av, bv = steps(a, k), steps(b, k)
            t = (f"<tr><th class='corner'>{a} &#9585; {b}</th>"
                 + "".join(f"<th>{v:g}</th>" for v in bv) + "</tr>")
            for i, x in enumerate(av):
                t += f"<tr><th class='rh'>{x:g}</th>"
                for j, y in enumerate(bv):
                    s = dict(fixed, **{a: x, b: y})
                    lab = ", ".join(f"{n} {s.get(n, dflt[n]):g}" for n in names)
                    if fixed:
                        lab += ("   (dafür fest: "
                                + ", ".join(f"{n} {v:g}" for n, v in fixed.items()) + ")")
                    t += ("<td>" + add(render(set_params(body, **s)), f"p{pi:02d}_{i}{j}",
                                       lab, "&#9834;") + "</td>")
                t += "</tr>"
            note = ("dafür fest: " + ", ".join(f"{n} {v:g}" for n, v in fixed.items())
                    if fixed else "alles übrige auf Vorgabe")
            blocks += (f"<div class='pair'><h3>{a} × {b}</h3><p>{note}</p>"
                       f"<table>{t}</table></div>")
        sections += (f"<h2>Jeder Regler gegen jeden &mdash; {len(pairs)} "
                     f"{'Tabelle' if len(pairs) == 1 else 'Tabellen'}, je {k}×{k}</h2>"
                     "<p class='sub'>Zeilen der eine Regler, Spalten der andere, alles übrige "
                     "auf Vorgabe. Die Frage in jeder Tabelle: bedeutet der Zeilenregler in "
                     "jeder Spalte noch dasselbe? Die vollständige Einstellung steht oben "
                     "neben dem Abspieler, sobald ein Klang läuft.</p>"
                     f"<div class='pairs'>{blocks}</div>")

    row = "".join(add(render(body, freq=f), f"oct_{f:.0f}", f"Vorgabe, {f:.0f} Hz",
                      f"{f:.0f} Hz", "Vorgabe")
                  for f in (55.0, 110.0, 220.0, 440.0, 880.0, 1760.0))
    sections += ("<h2>Vorgabewerte über die Tastatur</h2>"
                 "<p class='sub'>Alle Regler auf ihrem Vorgabewert. Hier zeigt sich, ob der "
                 "Klang über den Tonumfang trägt.</p>"
                 f"<div class='row'>{row}</div>")

    for spec in filter(None, args.compare.split(",")):
        rev, _, key = spec.partition(":")
        raw = subprocess.run(["git", "show", f"{rev}:backend/dco_lexicon.json"],
                             cwd=REPO, capture_output=True, text=True, check=True).stdout
        tmp = Path(tempfile.mkdtemp()) / "lex.json"
        tmp.write_text(raw)
        old = entry_of(key, tmp)
        row = "".join(add(render(old["code"], freq=f), f"old_{key}_{f:.0f}",
                          f"ALT {key}, {f:.0f} Hz", f"{f:.0f} Hz", "alt")
                      for f in (110.0, 220.0, 440.0))
        sections += (f"<h2>Zum Vergleich: das alte <code>{key}</code></h2>"
                     "<p class='sub'>Wie es vor diesem Umbau klang, an drei Tonhöhen. "
                     "Derselbe Seitenpegel &mdash; ein Lautstärkeunterschied ist echt.</p>"
                     f"<div class='row'>{row}</div>")

    peak = max(float(np.max(np.abs(y))) for _, y in clips)
    g = PAGE_PEAK / peak
    for name, y in clips:
        sf.write(out / f"{name}.wav", (y * g).astype(np.float32), M.SR, subtype="PCM_24")

    intro = (f"<b>{args.key}</b>, {len(clips)} Klänge, je {args.dur:g} s bei "
             f"{args.freq:.0f} Hz, wo nicht anders angegeben. Geprüft wird die "
             "<b>Kombination</b> der Regler: jeder gegen jeden, in einer eigenen kleinen "
             "Tabelle. Weder der schreibende Agent noch der Spieler bewegt einen einzelnen "
             "Knopf &mdash; beide stellen alle zugleich.<br>"
             f"Die ganze Seite hat <b>eine</b> Verstärkung ({20 * np.log10(g):+.1f} dB, "
             f"höchste Spitze {peak:.2f}). Was hier lauter klingt, ist lauter."
             "<br><b>Das ist kein Test und hat keine richtige Antwort.</b> Die Frage ist, ob "
             "der Klangkörper über seinen ganzen Raum brauchbar bleibt.")
    (out / "index.html").write_text(
        PAGE.format(title=f"{args.key} &mdash; durchhören", intro=intro, sections=sections),
        encoding="utf-8")
    print(f"{out}/index.html   ({len(clips)} Klänge, Spitze {peak:.2f}, "
          f"Gain {20*np.log10(g):+.1f} dB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
