#!/usr/bin/env python3
"""ABX for one claim of one library entry. See docs/LCO_TEST_POLICY.md.

ABX is reserved for the single question "is this difference audible at all", and
only where nothing published already answers it. It costs about five minutes per
pair, so it is never the default -- the default is word-sound matching, which asks
whether the entry's WORDS reach their sounds.

The case it is right for is a claim with a mathematically determined answer. The
first one: `analog_osc.width` says narrow (0.12) and wide (0.88) sound alike,
because a pulse and its complement have the same magnitude spectrum. If the two
are told apart reliably the note is wrong and one of the two anchor words has no
sound of its own; if they are not, the axis is half as long as it looks. Both
outcomes rewrite the entry, which is what qualifies the test.

Twelve trials, ten correct is p < 0.02 under the null that the listener is
guessing (binomial, one-sided). The page says which trials were which only after
all twelve are answered, so nothing is learned mid-run.
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
PREROLL = 0.5


def entry_of(key):
    for t in json.loads(LEX.read_text())["techniques"]:
        if t["key"] == key:
            return t
    raise SystemExit(f"no entry {key!r}")


def set_params(body, **vals):
    import re
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
body{{font:15px system-ui;margin:0 24px 90px;max-width:820px;color:#222}}
h1{{font-size:20px;margin-bottom:4px}}
#now{{position:sticky;top:0;background:#fff;padding:12px 0;margin-bottom:6px;
border-bottom:2px solid #ccc;z-index:20;display:flex;align-items:center;gap:16px}}
#player{{width:340px;height:34px}} #nowlabel{{font-weight:bold;color:#333}}
.note{{background:#f6f8fa;border:1px solid #dfe2e5;border-radius:6px;padding:10px 13px;
font-size:13px;margin:10px 0;line-height:1.6}}
.trial{{display:flex;align-items:center;gap:10px;padding:9px 0;
border-top:0.5px solid #e3e3e3}}
.n{{width:26px;color:#888;font-size:13px}}
.clip{{cursor:pointer;border:1px solid #bbb;background:#f4f4f4;border-radius:6px;
padding:9px 16px;font-size:15px;font-weight:bold}}
.clip:hover{{background:#e7e7e7}} .clip.playing{{background:#ffca28;border-color:#f57f17}}
.pick{{cursor:pointer;border:1px solid #bbb;background:#fff;border-radius:6px;
padding:8px 14px;font-size:14px;margin-left:4px}}
.pick.on{{background:#1b5e20;color:#fff;border-color:#1b5e20}}
.sp{{flex:1}}
button.go{{font:14px system-ui;padding:9px 18px;border:1px solid #888;border-radius:6px;
background:#eee;cursor:pointer;margin-top:18px}}
#out{{margin-top:18px;font-size:14px;line-height:1.65}}
.ok{{color:#1b5e20;font-weight:bold}} .no{{color:#b71c1c;font-weight:bold}}
</style>
<div id='now'><audio id='player' controls preload='auto'></audio>
<span id='nowlabel'>&mdash; nichts &mdash;</span></div>
<h1>{title}</h1>
<div class='note'>{intro}</div>
<div id='trials'>{trials}</div>
<button class='go' id='eval'>Auswerten</button>
<div id='out'></div>
<script>
var KEY={keys}, REVEAL={reveal}, PICK={{}};
var P=document.getElementById('player'),L=document.getElementById('nowlabel'),A=null;
function clr(){{if(A){{A.classList.remove('playing');A=null;}}L.textContent='\\u2014 nichts \\u2014';}}
document.addEventListener('click',function(ev){{
  var b=ev.target.closest('.clip');
  if(b){{
    if(b===A){{P.pause();P.currentTime=0;clr();return;}}
    if(A)A.classList.remove('playing');
    A=b;b.classList.add('playing');
    L.textContent='Durchgang '+(+b.dataset.t+1)+', '+b.textContent.trim();
    P.src=b.dataset.src;P.play();return;
  }}
  var p=ev.target.closest('.pick');
  if(p){{
    PICK[p.dataset.t]=p.dataset.v;
    document.querySelectorAll('.pick[data-t="'+p.dataset.t+'"]')
      .forEach(function(q){{q.classList.toggle('on',q===p);}});
  }}
}});
P.addEventListener('ended',clr);
document.getElementById('eval').addEventListener('click',function(){{
  var n=0,right=0,miss=[];
  for(var i=0;i<KEY.length;i++){{
    var v=PICK[i]; if(!v){{miss.push(i+1);continue;}}
    n++; if(v===KEY[i])right++;
  }}
  if(miss.length){{document.getElementById('out').innerHTML=
    '<p>Noch offen: Durchgang '+miss.join(', ')+'.</p>';return;}}
  var html='<p>'+right+' von '+n+' richtig.</p>';
  if(right>=10)html+='<p class=ok>Der Unterschied ist h&ouml;rbar. Bei zw&ouml;lf '
    +'Durchg&auml;ngen tritt das durch Raten in unter 2 % der F&auml;lle ein.</p>';
  else if(right<=8)html+='<p>Das ist nicht von Raten zu unterscheiden &mdash; die beiden '
    +'sind f&uuml;r dieses Ohr, an diesem Tag, dasselbe.</p>';
  else html+='<p>Neun von zw&ouml;lf liegt zwischen den St&uuml;hlen: 7 % unter Raten, '
    +'also kein Beleg in die eine noch in die andere Richtung.</p>';
  document.getElementById('out').innerHTML=html+REVEAL;
  this.disabled=true;
}});
</script>
"""

TRIAL = """<div class='trial'><span class='n'>{n}</span>
<button class='clip' data-t='{i}' data-src='{a}'>A</button>
<button class='clip' data-t='{i}' data-src='{b}'>B</button>
<button class='clip' data-t='{i}' data-src='{x}'>X</button>
<span class='sp'></span>X ist:
<button class='pick' data-t='{i}' data-v='A'>A</button>
<button class='pick' data-t='{i}' data-v='B'>B</button></div>"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--key", required=True)
    ap.add_argument("--param", required=True)
    ap.add_argument("--a", required=True, help="first anchor word")
    ap.add_argument("--b", required=True, help="second anchor word")
    ap.add_argument("--context", default="", help="k=v,... held fixed, e.g. wave=1.0")
    ap.add_argument("--freq", type=float, default=220.0)
    ap.add_argument("--dur", type=float, default=2.5)
    ap.add_argument("--trials", type=int, default=12)
    ap.add_argument("--seed", type=int, default=20260728)
    ap.add_argument("--claim", default="", help="what the entry claims, for the reveal")
    ap.add_argument("--out", default=str(REPO / "tools" / "lco_listening"))
    args = ap.parse_args()

    inst = entry_of(args.key)
    anchors = (inst.get("params") or {}).get(args.param, {}).get("anchors") or {}
    for w in (args.a, args.b):
        if w not in anchors:
            raise SystemExit(f"{args.key}.{args.param} has no anchor {w!r}; "
                             f"it has {sorted(anchors)}")
    ctx = {}
    for part in filter(None, args.context.split(",")):
        k, v = part.split("=")
        ctx[k.strip()] = float(v)

    out = Path(args.out) / f"{args.key}_{args.param}_abx"
    out.mkdir(parents=True, exist_ok=True)
    facts = {}
    for tag, word in (("a", args.a), ("b", args.b)):
        v = float(anchors[word]["value"])
        y, err = M.render(set_params(inst["code"], **{args.param: v}, **ctx),
                          dur=args.dur, freq=args.freq, preroll=PREROLL)
        if y is None:
            raise SystemExit(f"{args.param}={v} does not render: {err}")
        sf.write(out / f"{tag}.wav", y, M.SR)
        m = M.measure(y, args.freq)
        facts[tag] = (word, v, float(np.abs(y).max()), m["rms_db"], m["centroid"])

    rnd = random.Random(args.seed)
    key = [rnd.choice("AB") for _ in range(args.trials)]
    while abs(key.count("A") - args.trials / 2) > 1:      # not all one side
        key = [rnd.choice("AB") for _ in range(args.trials)]
    trials = "".join(
        TRIAL.format(n=i + 1, i=i, a="a.wav", b="b.wav",
                     x=("a.wav" if k == "A" else "b.wav"))
        for i, k in enumerate(key))

    fa, fb = facts["a"], facts["b"]
    reveal = json.dumps(
        "<h2 style='border:0;font-size:16px'>Was gepr&uuml;ft wurde</h2>"
        + (f"<p>{args.claim}</p>" if args.claim else "")
        + f"<p>A ist <b>{fa[0]}</b> ({args.param} {fa[1]:g}), B ist <b>{fb[0]}</b> "
          f"({args.param} {fb[1]:g})."
        + (" Fest dabei: " + ", ".join(f"{k} {v:g}" for k, v in ctx.items()) if ctx else "")
        + f"</p><p>Gemessen: Spitzenwert {fa[2]:.3f} gegen {fb[2]:.3f}, Pegel "
          f"{fa[3]:.2f} gegen {fb[3]:.2f} dB, spektraler Schwerpunkt {fa[4]:.0f} gegen "
          f"{fb[4]:.0f} Hz.</p>")
    intro = (f"<b>{args.key} &middot; {args.param}</b> bei {args.freq:.0f} Hz. "
             f"Zw&ouml;lf Durchg&auml;nge. In jedem sind A und B die beiden Kl&auml;nge und "
             "X ist einer von beiden &mdash; sag welcher. A und B sind in allen "
             "Durchg&auml;ngen dieselben; nur X wechselt.<br>"
             "Ein Klick spielt, ein anderer l&ouml;st den laufenden ab, Klick auf den "
             "gelben Knopf stoppt. Welche Wahl richtig war, steht erst nach dem "
             "zw&ouml;lften Durchgang da.")

    (out / "index.html").write_text(
        PAGE.format(title=f"{args.key} &middot; {args.param}: {fa[0]} gegen {fb[0]}",
                    intro=intro, trials=trials,
                    keys=json.dumps(key), reveal=reveal), encoding="utf-8")
    print(f"{out}/index.html")
    for tag in ("a", "b"):
        w, v, pk, rms, c = facts[tag]
        print(f"  {tag.upper()}  {w:<8} {args.param}={v:<6g} Spitze {pk:.3f}  "
              f"{rms:6.2f} dB  Schwerpunkt {c:.0f} Hz")
    print(f"  Reihenfolge: {''.join(key)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
