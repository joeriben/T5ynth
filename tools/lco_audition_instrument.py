#!/usr/bin/env python3
"""Play one instrument as its parameters COMBINE. See docs/LCO_TEST_POLICY.md.

BEFORE any word is put on trial, the instrument itself has to be heard. A word test
asks whether `worn` reaches the sound it names; it cannot say whether the oscillator
is any good, whether a control stays usable, or where it stops being material.

And a row per control, everything else at its default, answers the wrong question.
The author model does not move one knob -- it writes a body with every parameter
set at once, and these are the same parameters the player will see in the Prompt
Orchestra field. So a control that behaves alone and falls apart in company is a
defect this page has to be able to show. What is laid out here is the parameter
space, not the parameters:

  * with two controls, the whole grid -- every value of one against every value of
    the other;
  * with three or more, a two-level factorial over ALL of them at once (the full
    2^n up to five controls; beyond that a 32-run resolution IV fraction, where
    each control is low in half the runs and high in the other half and no pair of
    controls is confounded), plus a spread of interior points from a Halton
    sequence so no two of those share a value on any axis.

Then one row of the default setting across the keyboard, because a range problem
shows there and nowhere else.

One gain for the whole page, so loudness differences between clips are the
instrument's own and nothing clips on export.

One player. A click plays, a click on another clip replaces the running one, a
click on the yellow one stops.
"""

from __future__ import annotations

import argparse
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
GRID = 5          # steps per axis when there are exactly two controls
INTERIOR = 12     # Halton points through the cube when there are three or more
PRIMES = [2, 3, 5, 7, 11, 13, 17, 19, 23]


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


def halton(i, base):
    f, r, n = 1.0, 0.0, i
    while n > 0:
        f /= base
        r += f * (n % base)
        n //= base
    return r


def factorial_runs(n):
    """Two-level runs over n controls: full 2^n to five, else a 32-run resolution IV
    fraction. The generators below are the textbook ones -- each added control is the
    parity of three base controls, which keeps every control balanced 16/16 and every
    PAIR of controls unconfounded, so a defect that only appears when two are extreme
    together still has to show up."""
    if n <= 5:
        return [[(i >> b) & 1 for b in range(n)] for i in range(2 ** n)]
    gens = [(0, 1, 2), (1, 2, 3), (0, 2, 3), (0, 1, 3), (0, 1, 2, 3)]
    runs = []
    for i in range(32):
        base = [(i >> b) & 1 for b in range(5)]
        row = list(base)
        for g in gens[: n - 5]:
            row.append(int(np.bitwise_xor.reduce([base[k] for k in g])))
        runs.append(row)
    return runs


PAGE = """<!doctype html><meta charset=utf-8><title>{title}</title>
<style>
body{{font:15px system-ui;margin:0 24px 90px;max-width:960px;color:#222}}
h1{{font-size:20px;margin-bottom:4px}}
h2{{font-size:15px;margin:28px 0 2px;font-weight:600}}
#now{{position:sticky;top:0;background:#fff;padding:12px 0;margin-bottom:6px;
border-bottom:2px solid #ccc;z-index:20;display:flex;align-items:center;gap:16px}}
#player{{width:340px;height:34px}} #nowlabel{{font-weight:bold;color:#333;font-size:13.5px}}
.note{{background:#f6f8fa;border:1px solid #dfe2e5;border-radius:6px;padding:10px 13px;
font-size:13px;margin:10px 0;line-height:1.65}}
.sub{{font-size:12.5px;color:#666;margin:0 0 8px;line-height:1.5}}
.row{{display:flex;gap:6px;flex-wrap:wrap;align-items:stretch}}
.clip{{cursor:pointer;border:1px solid #bbb;background:#f4f4f4;border-radius:6px;
padding:7px 11px;font-size:12.5px;line-height:1.35;text-align:center;min-width:64px}}
.clip:hover{{background:#e7e7e7}} .clip.playing{{background:#ffca28;border-color:#f57f17}}
.clip b{{display:block;font-size:13px;font-weight:600}}
.clip span{{color:#777;font-size:11px}} .clip.playing span{{color:#7a5b00}}
table{{border-collapse:collapse;margin-top:4px}}
td,th{{padding:3px 4px;text-align:center}}
th{{font-size:11.5px;color:#666;font-weight:500}}
th.rowhead{{text-align:right;padding-right:8px;white-space:nowrap}}
.legend{{font-size:11.5px;color:#888;margin:0 0 6px;font-family:ui-monospace,monospace}}
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
    clips = []

    def render(code, freq=None):
        y, err = M.render(code, dur=args.dur, freq=freq or args.freq, preroll=PREROLL)
        if y is None:
            raise SystemExit(err)
        return y

    def add(y, name, label, big, small, cls=""):
        clips.append((name, y))
        return (f"<button class='clip {cls}' data-src='{name}.wav' data-label=\"{label}\">"
                f"<b>{big}</b><span>{small}</span></button>")

    def full(vals):
        return ", ".join(f"{n} {vals.get(n, dflt[n]):g}" for n in names)

    sections = ""

    if len(names) == 2:
        a, b = names
        av = [rng[a][0] + (rng[a][1] - rng[a][0]) * i / (GRID - 1) for i in range(GRID)]
        bv = [rng[b][0] + (rng[b][1] - rng[b][0]) * i / (GRID - 1) for i in range(GRID)]
        rows = f"<tr><th></th><th colspan='{GRID}'>{b}</th></tr><tr><th></th>"
        rows += "".join(f"<th>{v:g}</th>" for v in bv) + "</tr>"
        for i, x in enumerate(av):
            rows += f"<tr><th class='rowhead'>{a} {x:g}</th>"
            for j, y in enumerate(bv):
                s = {a: x, b: y}
                rows += ("<td>" + add(render(set_params(body, **s)), f"g{i}{j}",
                                      full(s), f"{x:g}", f"{y:g}") + "</td>")
            rows += "</tr>"
        sections += (f"<h2>Beide Regler gegeneinander &mdash; {GRID}×{GRID}</h2>"
                     f"<p class='sub'>Jeder Wert von <code>{a}</code> gegen jeden Wert von "
                     f"<code>{b}</code>. Oben auf dem Knopf steht <code>{a}</code>, "
                     f"darunter <code>{b}</code>.</p><table>{rows}</table>")
    elif len(names) == 1:
        a = names[0]
        av = [rng[a][0] + (rng[a][1] - rng[a][0]) * i / (GRID - 1) for i in range(GRID)]
        row = "".join(add(render(set_params(body, **{a: v})), f"s{i}", full({a: v}),
                          f"{v:g}", "") for i, v in enumerate(av))
        sections += (f"<h2>{a} über seinen Bereich</h2>"
                     f"<p class='sub'>Dieser Klangkörper hat nur einen Regler; "
                     f"es gibt nichts zu kombinieren.</p><div class='row'>{row}</div>")
    else:
        runs = factorial_runs(len(names))
        legend = " · ".join(names)
        row = ""
        for i, r in enumerate(runs):
            s = {n: rng[n][r[k]] for k, n in enumerate(names)}
            row += add(render(set_params(body, **s)), f"f{i:02d}", full(s),
                       " · ".join(f"{s[n]:g}" for n in names), "")
        kind = ("alle " + str(2 ** len(names)) if len(names) <= 5
                else "32 aus " + str(2 ** len(names)))
        sections += (f"<h2>Alle Regler zugleich, jeweils ganz unten oder ganz oben</h2>"
                     f"<p class='sub'>{kind} Ecken des Parameterraums. Jeder Regler steht in "
                     "der Hälfte der Klänge unten und in der anderen oben, und kein Reglerpaar "
                     "ist mit einem anderen verwechselbar &mdash; ein Fehler, der nur auftritt, "
                     "wenn zwei zugleich am Anschlag stehen, muss hier auftauchen.</p>"
                     f"<p class='legend'>Reihenfolge auf dem Knopf: {legend}</p>"
                     f"<div class='row'>{row}</div>")

        row = ""
        for i in range(INTERIOR):
            s = {n: rng[n][0] + (rng[n][1] - rng[n][0]) * halton(i + 1, PRIMES[k])
                 for k, n in enumerate(names)}
            row += add(render(set_params(body, **s)), f"h{i:02d}", full(s),
                       " · ".join(f"{s[n]:.2f}" for n in names), "")
        sections += (f"<h2>Quer durch den Raum, {INTERIOR} Stellungen</h2>"
                     "<p class='sub'>Keine Ecken, sondern Werte mitten im Bereich, aus einer "
                     "Halton-Folge: keine zwei dieser Klänge teilen sich auf irgendeiner Achse "
                     "denselben Wert.</p>"
                     f"<p class='legend'>Reihenfolge auf dem Knopf: {legend}</p>"
                     f"<div class='row'>{row}</div>")

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
             "<b>Kombination</b> der Regler, nicht ein Regler nach dem anderen: der "
             "schreibende Agent bewegt keinen einzelnen Knopf, er setzt alle auf einmal, und "
             "es sind dieselben Regler, die der Spieler später im Prompt-Orchestra-Feld "
             "sieht.<br>"
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
