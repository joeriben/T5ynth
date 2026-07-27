#!/usr/bin/env python3
"""Build an ordering test: N stimuli that differ in ONE parameter, one player.

The question put to the listener has a right answer -- put these in order along
the attribute the entry claims -- so the result is a score and not an opinion.
That is the whole reason for choosing ordering over a rating scale: a rating asks
what something is like, and there is nothing to be wrong about.

Two runs with different scrambles, evaluated together, because one pass cannot
tell a heard order from a lucky one: with four stimuli there are 24 orders, so a
single correct run happens by chance once in 24, and twice by chance once in 576.
The second run also reads back the listener's own consistency, which is the only
thing a single-listener test can say about reliability at all.

STEP SIZE IS NOT FREE. Where a perceptual tolerance has been published for this
model class it sets the spacing, and the spacing is stated on the page next to
its source. For plucked-string decay: Jarvelainen & Tolonen, "Perceptual
Tolerances for Decay Parameters in Plucked String Synthesis", JAES 49(11), 2001,
1049-1059 -- variations of 25 to 40 % in the decay time constant are inaudible.
A set spaced inside that band is a test that cannot be passed, and building one
wastes the listener's time rather than measuring anything.

What this design can deliver with one listener: audibility, direction, and
self-consistency. What it cannot: anything about listeners in general. That
limit is printed on the page rather than left for the reader to remember.
"""

from __future__ import annotations

import argparse
import json
import random
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

import lco_measure as M  # noqa: E402

LEX = REPO / "backend" / "dco_lexicon.json"
PREROLL = 0.5          # what every tool here renders with; 0.0 does not decay


def body_of(key):
    for t in json.loads(LEX.read_text())["techniques"]:
        if t["key"] == key:
            return t["code"]
    raise SystemExit(f"no entry {key!r} in the lexicon")


def set_param(body, name, value):
    """Replace the axis line's value, leaving its comment intact.

    The axis line is `kname  = <value>   ; ...`, which is the shape the probe
    already relies on, so a body it accepts is a body this accepts.
    """
    out, hit = [], 0
    for line in body.split("\n"):
        s = line.strip()
        if s.startswith(f"k{name}") and "=" in s:
            head, _, tail = line.partition("=")
            rest = tail.split(";", 1)
            comment = f";{rest[1]}" if len(rest) > 1 else ""
            out.append(f"{head}= {value:<6.4f} {comment}".rstrip())
            hit += 1
        else:
            out.append(line)
    if hit != 1:
        raise SystemExit(f"expected exactly one `k{name} =` line, found {hit}")
    return "\n".join(out)


def decay_time(y, db=20.0, sr=M.SR):
    """Seconds from the attack peak to `db` below it, on the energy envelope."""
    w = int(0.01 * sr)
    env = np.sqrt(np.convolve(y.astype(float) ** 2, np.ones(w) / w, mode="same"))
    i0 = int(np.argmax(env))
    below = np.where(env[i0:] < env[i0] * 10 ** (-db / 20))[0]
    return None if not len(below) else float(below[0]) / sr


PAGE = """<!doctype html><meta charset=utf-8><title>{title}</title>
<style>
body{{font:15px system-ui;margin:0 24px 80px;max-width:900px;color:#222}}
h1{{font-size:20px}} h2{{font-size:16px;margin-top:34px;border-top:2px solid #ddd;padding-top:10px}}
#now{{position:sticky;top:0;background:#fff;padding:12px 0;margin-bottom:6px;
border-bottom:2px solid #ccc;z-index:20;display:flex;align-items:center;gap:16px}}
#player{{width:380px;height:34px}} #nowlabel{{font-weight:bold;color:#333}}
.note{{background:#f6f8fa;border:1px solid #dfe2e5;border-radius:6px;padding:10px 13px;
font-size:13px;margin:10px 0;line-height:1.55}}
.clip{{cursor:pointer;border:1px solid #bbb;background:#f4f4f4;border-radius:6px;
padding:14px 22px;font-size:17px;font-weight:bold;margin-right:8px}}
.clip:hover{{background:#e7e7e7}}
.clip.playing{{background:#ffca28;border-color:#f57f17}}
input[type=text]{{font:15px system-ui;padding:7px 9px;width:190px;border:1px solid #bbb;
border-radius:6px}}
button.go{{font:14px system-ui;padding:8px 16px;border:1px solid #888;border-radius:6px;
background:#eee;cursor:pointer;margin-top:14px}}
#out{{margin-top:16px;font-size:14px;line-height:1.6}}
.ok{{color:#1b5e20;font-weight:bold}} .no{{color:#b71c1c;font-weight:bold}}
small{{color:#666}}
</style>
<div id='now'><audio id='player' controls preload='auto'></audio>
<span id='nowlabel'>&mdash; nichts &mdash;</span></div>
<h1>{title}</h1>
<div class='note'>{intro}</div>
{runs}
<button class='go' id='eval'>Auswertung zeigen</button>
<div id='out'></div>
<div class='note' style='margin-top:26px'><b>Was dieser Aufbau nicht kann.</b>
Ein Hörer liefert Hörbarkeit, Richtung und die eigene Konsistenz. Eine Aussage über
Hörer im Allgemeinen liefert er nicht, und sie wird auch nicht behauptet.</div>
<script>
var KEY={keys};
var P=document.getElementById('player'),L=document.getElementById('nowlabel'),A=null;
function clr(){{if(A){{A.classList.remove('playing');A=null;}}L.textContent='\\u2014 nichts \\u2014';}}
document.addEventListener('click',function(e){{
  var b=e.target.closest('.clip'); if(!b) return;
  if(b===A){{P.pause();P.currentTime=0;clr();return;}}
  if(A)A.classList.remove('playing');
  A=b;b.classList.add('playing');L.textContent=b.textContent.trim();
  P.src=b.getAttribute('data-src');P.play();
}});
P.addEventListener('ended',clr);
function inv(a,b){{var n=0;for(var i=0;i<a.length;i++)for(var j=i+1;j<a.length;j++)
  if(b.indexOf(a[i])>b.indexOf(a[j]))n++;return n;}}
document.getElementById('eval').addEventListener('click',function(){{
  var html='',tot=0,given=[];
  for(var r=0;r<KEY.length;r++){{
    var v=document.getElementById('ans'+r).value.toUpperCase().replace(/[^A-Z]/g,'').split('');
    given.push(v.join(''));
    if(v.length!==KEY[r].length){{html+='<p>Durchgang '+(r+1)+': '+v.length+' Buchstaben statt '
      +KEY[r].length+' &mdash; nicht auswertbar.</p>';tot=-1;continue;}}
    var n=inv(v,KEY[r]);tot+=n;
    html+='<p>Durchgang '+(r+1)+': deine Reihenfolge <b>'+v.join(' ')+'</b>, richtig w&auml;re <b>'
      +KEY[r].join(' ')+'</b> &mdash; '+(n===0?'<span class=ok>fehlerfrei</span>'
      :'<span class=no>'+n+' Vertauschung'+(n===1?'':'en')+'</span>')+'</p>';
  }}
  if(tot===0)html+='<p class=ok>Beide Durchg&auml;nge fehlerfrei. Bei vier Reizen tritt das '
    +'zuf&auml;llig einmal unter 576 Versuchen ein &mdash; die Achse ist h&ouml;rbar und '
    +'monoton auf dem benannten Eindruck.</p>';
  else if(tot>0)html+='<p>Zusammen '+tot+' Vertauschung'+(tot===1?'':'en')+'. '
    +'Wo sie liegen, sagt welcher Schritt zu klein ist &mdash; nicht, dass die Achse nichts tut.</p>';
  html+='<p><small>Zuordnung: '+MAP+'</small></p>';
  document.getElementById('out').innerHTML=html;
}});
var MAP={mapping};
</script>
"""

RUN = """<h2>Durchgang {n}</h2>
<div class='note'>{task}</div>
<div>{buttons}</div>
<p style='margin-top:14px'>Reihenfolge eintippen, z.&nbsp;B. <code>C A D B</code>:
&nbsp;<input type='text' id='ans{i}' autocomplete='off'></p>
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--key", required=True, help="lexicon entry, e.g. plucked_wire")
    ap.add_argument("--param", required=True, help="axis name, e.g. refl")
    ap.add_argument("--values", required=True,
                    help="comma-separated, in the TRUE order of the attribute")
    ap.add_argument("--freq", type=float, default=220.0)
    ap.add_argument("--dur", type=float, default=4.0)
    ap.add_argument("--runs", type=int, default=2)
    ap.add_argument("--seed", type=int, default=20260727)
    ap.add_argument("--attribute", required=True,
                    help="what to order by, e.g. 'kurzes bis langes Ausklingen'")
    ap.add_argument("--hypothesis", default="", help="printed on the page, with its source")
    ap.add_argument("--out", default=str(REPO / "tools" / "lco_listening"))
    args = ap.parse_args()

    vals = [float(v) for v in args.values.split(",")]
    rnd = random.Random(args.seed)
    # The letters are dealt out at random, so the correct answer is NOT A B C D.
    # With the values taken in order the key would be alphabetical, and a listener
    # who noticed that would be answering from the label instead of the sound.
    letters = [chr(65 + i) for i in range(len(vals))]
    rnd.shuffle(letters)
    out = Path(args.out) / f"{args.key}_{args.param}"
    out.mkdir(parents=True, exist_ok=True)

    body = body_of(args.key)
    facts = []
    for letter, v in zip(letters, vals):
        y, err = M.render(set_param(body, args.param, v), dur=args.dur,
                          freq=args.freq, preroll=PREROLL)
        if y is None:
            raise SystemExit(f"{args.param}={v} does not render: {err}")
        sf.write(out / f"{letter}.wav", y, M.SR)
        facts.append((letter, v, float(np.abs(y).max()), decay_time(y)))

    keys, runs, seen = [], "", []
    for r in range(args.runs):
        order = sorted(letters)
        while order == sorted(letters) or order in seen:
            rnd.shuffle(order)           # never plain A B C D, and never a repeat of
        seen.append(order[:])            #   an earlier run's layout
        keys.append(letters)             # the answer: letters in TRUE attribute order
        runs += RUN.format(
            n=r + 1, i=r, task=f"Bringe die vier Kl&auml;nge in die Reihenfolge: "
                               f"<b>{args.attribute}</b>. Ein Klick spielt, Klick auf "
                               f"den gelben Knopf stoppt, ein anderer Klang l&ouml;st "
                               f"den laufenden ab.",
            buttons="".join(
                f"<button class='clip' data-src='{L}.wav'>{L}</button>" for L in order))

    peaks = {round(p, 3) for _l, _v, p, _t in facts}
    mapping = " &middot; ".join(
        f"{L} = {args.param} {v:g}" + (f", T20 {t:.2f} s" if t else "") for L, v, _p, t in facts)
    intro = (f"<b>{args.key} &middot; {args.param}</b> bei {args.freq:.0f} Hz, "
             f"{len(vals)} Werte von {min(vals):g} bis {max(vals):g}.<br>"
             + (f"{args.hypothesis}<br>" if args.hypothesis else "")
             + ("Der Anschlag ist bei allen Werten gleich laut (Spitzenwert "
                f"{peaks.pop():.3f}), die Lautst&auml;rke kann die Reihenfolge also "
                "nicht verraten." if len(peaks) == 1 else
                "<b>Achtung:</b> die Spitzenwerte unterscheiden sich &mdash; die "
                "Lautst&auml;rke kann die Reihenfolge mitverraten."))

    page = PAGE.format(title=f"{args.key} &middot; {args.param} &mdash; Reihenfolge",
                       intro=intro, runs=runs,
                       keys=json.dumps(keys), mapping=json.dumps(mapping))
    (out / "index.html").write_text(page, encoding="utf-8")

    print(f"{out}/index.html")
    for L, v, p, t in facts:
        print(f"  {L}  {args.param}={v:<6g} Spitze {p:.3f}  "
              + (f"T20 {t:.2f} s" if t else "T20 nicht im Ton"))
    ts = [t for _l, _v, _p, t in facts if t]
    for a, c in zip(ts, ts[1:]):
        print(f"  Schritt: {100 * (max(a, c) / min(a, c) - 1):+.0f} % Abklingzeit")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
