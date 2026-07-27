#!/usr/bin/env python3
"""Play one instrument across every one of its parameters. See docs/LCO_TEST_POLICY.md.

BEFORE any word is put on trial, the instrument itself has to be heard. A word test
asks whether `worn` reaches the sound it names; it cannot say whether the oscillator
is any good, whether a control stays usable over its whole range, or where it stops
being material. Nothing measured here answers that either -- a level flat to 0.02 dB
says nothing about whether the thing plays.

So this page is not a test and has no right answer. It is the instrument laid out:
every parameter stepped across its declared range at one pitch, the others at their
defaults, plus whatever it replaces, plus a few octaves of the default sound.

Three things it does that are not decoration:

  * ONE gain for the whole page. Loudness differences between clips are the
    instrument's own, and nothing clips on export -- a page written at 16 bit
    silently flattened the peaks of every narrow pulse on it.
  * A parameter that only acts inside another one's region is rendered inside it
    (`width` needs `wave` at pulse), or the row is five copies of a control that
    was never switched on.
  * A parameter whose whole point is a second layer is rendered AS two layers.
    Twelve cents on a single tone with nothing to beat against is inaudible by
    construction; the same twelve cents against an undetuned copy is the sound
    the parameter exists for.

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
STEPS = 5
PAGE_PEAK = 0.89

# A parameter that only acts inside a region of another one, and the setting that
# switches it on.
CONTEXT = {"width": {"wave": 1.0}, "pwm": {"wave": 1.0}, "rate": {"wave": 1.0, "pwm": 1.0}}

# A parameter that only exists to be heard against a second, undetuned layer.
LAYERED = {"finetune"}


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
body{{font:15px system-ui;margin:0 24px 90px;max-width:920px;color:#222}}
h1{{font-size:20px;margin-bottom:4px}}
h2{{font-size:15px;margin:26px 0 2px;font-weight:600}}
#now{{position:sticky;top:0;background:#fff;padding:12px 0;margin-bottom:6px;
border-bottom:2px solid #ccc;z-index:20;display:flex;align-items:center;gap:16px}}
#player{{width:340px;height:34px}} #nowlabel{{font-weight:bold;color:#333}}
.note{{background:#f6f8fa;border:1px solid #dfe2e5;border-radius:6px;padding:10px 13px;
font-size:13px;margin:10px 0;line-height:1.65}}
.sub{{font-size:12.5px;color:#666;margin:0 0 8px;line-height:1.5}}
.row{{display:flex;gap:7px;flex-wrap:wrap;align-items:stretch}}
.clip{{cursor:pointer;border:1px solid #bbb;background:#f4f4f4;border-radius:6px;
padding:8px 13px;font-size:13px;line-height:1.35;text-align:center;min-width:74px}}
.clip:hover{{background:#e7e7e7}} .clip.playing{{background:#ffca28;border-color:#f57f17}}
.clip b{{display:block;font-size:14px}} .clip span{{color:#777;font-size:11px}}
.clip.playing span{{color:#7a5b00}}
.old{{background:#fff;border-style:dashed}}
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
    clips = []          # (name, label, big, small, cls, audio)

    def render(code, freq=None):
        y, err = M.render(code, dur=args.dur, freq=freq or args.freq, preroll=PREROLL)
        if y is None:
            raise SystemExit(err)
        return y

    def add(y, name, label, big, small, cls=""):
        clips.append((name, label, big, small, cls, y))
        return (f"<button class='clip {cls}' data-src='{name}.wav' data-label=\"{label}\">"
                f"<b>{big}</b><span>{small}</span></button>")

    sections = ""

    # the default sound over the keyboard: a range problem shows here and nowhere else
    row = "".join(add(render(body, freq=f), f"oct_{f:.0f}", f"Vorgabe, {f:.0f} Hz",
                      f"{f:.0f} Hz", "Vorgabe")
                  for f in (55.0, 110.0, 220.0, 440.0, 880.0, 1760.0))
    sections += ("<h2>Vorgabewerte über die Tastatur</h2>"
                 "<p class='sub'>Alle Regler auf ihrem Vorgabewert. Hier zeigt sich, ob der "
                 "Klang über den Tonumfang trägt.</p>"
                 f"<div class='row'>{row}</div>")

    for pn, p in inst["params"].items():
        lo, hi = p["range"]
        ctx = CONTEXT.get(pn, {})
        vals = [lo + (hi - lo) * i / (STEPS - 1) for i in range(STEPS)]
        anchors = {a["value"]: nm for nm, a in p["anchors"].items()}
        for v in anchors:
            if not any(abs(v - x) < 1e-9 for x in vals):
                vals.append(v)
        vals.sort()

        base = render(set_params(body, **{pn: lo * 0}, **ctx)) if pn in LAYERED else None
        row = ""
        for i, v in enumerate(vals):
            y = render(set_params(body, **{pn: v}, **ctx))
            if base is not None:
                y = 0.5 * (y + base)
            lab = (f"{pn} = {v:g}" + (f"  ({anchors[v]})" if v in anchors else "")
                   + ("  [" + ", ".join(f"{k} {w:g}" for k, w in ctx.items()) + "]" if ctx else "")
                   + ("  gegen eine zweite Schicht auf 0" if base is not None else ""))
            row += add(y, f"{pn}_{i}", lab, f"{v:g}", anchors.get(v, ""))

        note = p["note"].split(". ")[0] + "."
        extra = ""
        if ctx:
            extra += ("  Dabei fest: "
                      + ", ".join(f"<code>{k}</code> {w:g}" for k, w in ctx.items()) + ".")
        if base is not None:
            extra += ("  Jeder Klang ist die Summe aus zwei Schichten &mdash; eine auf 0, "
                      "eine auf dem angegebenen Wert. Allein gespielt hat dieser Regler "
                      "nichts, wogegen er wirken könnte.")
        sections += (f"<h2>{pn} &mdash; {lo:g} bis {hi:g}</h2>"
                     f"<p class='sub'>{note}{extra}</p><div class='row'>{row}</div>")

    # the one choice in this body that is mine and not the model's
    if args.key == "analog_osc":
        flat = set_params(body, wave=1.0, pwm=1.0)
        loose = flat.replace("apuls   = apuls * knrm", "apuls   = apuls * 1.0")
        if loose == flat:
            raise SystemExit("normalisation line not found")
        row = (add(render(flat), "norm_on", "PWM-Fahrt, Pegel ausgeglichen",
                   "ausgeglichen", "wie gebaut")
               + add(render(loose), "norm_off", "PWM-Fahrt, Pegel läuft mit",
                     "läuft mit", "wie vco2 es gibt", cls="old"))
        sections += ("<h2>Die eine Entscheidung, die ich getroffen habe</h2>"
                     "<p class='sub'>Ein Puls wird leiser, je schmaler er wird &mdash; auf "
                     "einem analogen Gerät sackt der Pegel während der Fahrt hörbar ab, und "
                     "das gehört zum Klang der Pulsbreitenmodulation. Ich habe ihn "
                     "ausgeglichen, weil im Lexikon alle Klangkörper eine Lautstärke haben "
                     "sollen. Beides steht hier nebeneinander.</p>"
                     f"<div class='row'>{row}</div>")

    for spec in filter(None, args.compare.split(",")):
        rev, _, key = spec.partition(":")
        raw = subprocess.run(["git", "show", f"{rev}:backend/dco_lexicon.json"],
                             cwd=REPO, capture_output=True, text=True, check=True).stdout
        tmp = Path(tempfile.mkdtemp()) / "lex.json"
        tmp.write_text(raw)
        old = entry_of(key, tmp)
        row = "".join(add(render(old["code"], freq=f), f"old_{key}_{f:.0f}",
                          f"ALT {key}, {f:.0f} Hz", f"{f:.0f} Hz", "alt", cls="old")
                      for f in (110.0, 220.0, 440.0))
        sections += (f"<h2>Zum Vergleich: das alte <code>{key}</code></h2>"
                     "<p class='sub'>Wie es vor diesem Umbau klang, an drei Tonhöhen. "
                     "Derselbe Seitenpegel &mdash; ein Lautstärkeunterschied ist echt.</p>"
                     f"<div class='row'>{row}</div>")

    peak = max(float(np.max(np.abs(y))) for *_, y in clips)
    g = PAGE_PEAK / peak
    for name, _, _, _, _, y in clips:
        sf.write(out / f"{name}.wav", (y * g).astype(np.float32), M.SR, subtype="PCM_24")

    intro = (f"<b>{args.key}</b>, {len(clips)} Klänge, je {args.dur:g} s bei "
             f"{args.freq:.0f} Hz, wo nicht anders angegeben. Jede Zeile ist ein Regler über "
             "seinen ganzen Bereich, die übrigen Regler auf Vorgabe; benannte Anker stehen "
             "unter ihrem Knopf.<br>"
             f"Die ganze Seite hat <b>eine</b> Verstärkung ({20 * np.log10(g):+.1f} dB, "
             f"höchste Spitze {peak:.2f}). Was hier lauter klingt, ist lauter."
             "<br><b>Das ist kein Test und hat keine richtige Antwort.</b> Die Frage ist, ob "
             "der Oszillator etwas taugt und ob jeder Regler über seinen Bereich brauchbar "
             "bleibt.")
    (out / "index.html").write_text(
        PAGE.format(title=f"{args.key} &mdash; durchhören", intro=intro, sections=sections),
        encoding="utf-8")
    print(f"{out}/index.html   ({len(clips)} Klänge, Spitze {peak:.2f}, Gain {20*np.log10(g):+.1f} dB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
