#!/usr/bin/env python3
"""Word-sound matching for one parameter of one library entry. See docs/LCO_TEST_POLICY.md.

WHAT IS ON TRIAL IS A WORD, NOT A PARAMETER. The author model never measures
anything: it reads the library's prose -- entry descriptions, parameter notes,
anchor words -- and writes Csound from them. So the only thing worth a listener's
time is whether a word in that prose reaches the sound it names. Everything else
about a parameter is already in `lco_measure`, exactly.

Two earlier versions of this file got that wrong and are recorded in the policy so
the shape stays recognisable: the first asked for four stimuli to be ordered by
ring length (0.53 to 2.05 s, known from the envelope to the millisecond); the
second asked what changes between the ends, which tests the opcode's mechanism
rather than what the model is told about it. Neither would have changed a word in
the library, which is the test that decides whether a test is worth building.

The task here: render one stimulus per DECLARED ANCHOR, at the anchor's own value,
present them unlabelled and scrambled beside the entry's own anchor words, and
have the listener assign word to sound. The right answer is the entry's own
assignment; a mismatch means that word does not reach that sound and the entry
takes the listener's word instead. Then, and only after the assignment is given,
one free-text question -- what changes that none of these words covers -- because
an impression the entry has no name for is one the author model can never ask for.

Every run ends in a diff to backend/dco_lexicon.json or it was wasted.
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


def entry_of(key):
    for t in json.loads(LEX.read_text())["techniques"]:
        if t["key"] == key:
            return t
    raise SystemExit(f"no entry {key!r} in the lexicon")


def set_param(body, name, value):
    """Replace the axis line's value, leaving its comment intact."""
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
body{{font:15px system-ui;margin:0 24px 90px;max-width:860px;color:#222}}
h1{{font-size:20px;margin-bottom:4px}} h2{{font-size:16px;margin-top:36px;
border-top:2px solid #ddd;padding-top:12px}}
#now{{position:sticky;top:0;background:#fff;padding:12px 0;margin-bottom:6px;
border-bottom:2px solid #ccc;z-index:20;display:flex;align-items:center;gap:16px}}
#player{{width:380px;height:34px}} #nowlabel{{font-weight:bold;color:#333}}
.note{{background:#f6f8fa;border:1px solid #dfe2e5;border-radius:6px;padding:10px 13px;
font-size:13px;margin:10px 0;line-height:1.6}}
.row{{display:flex;align-items:center;gap:14px;margin:12px 0}}
.clip{{cursor:pointer;border:1px solid #bbb;background:#f4f4f4;border-radius:6px;
padding:14px 26px;font-size:18px;font-weight:bold;min-width:34px}}
.clip:hover{{background:#e7e7e7}}
.clip.playing{{background:#ffca28;border-color:#f57f17}}
select{{font:15px system-ui;padding:7px 9px;border:1px solid #bbb;border-radius:6px;
min-width:190px}}
textarea{{font:15px system-ui;padding:9px;width:100%;box-sizing:border-box;height:80px;
border:1px solid #bbb;border-radius:6px}}
button.go{{font:14px system-ui;padding:9px 18px;border:1px solid #888;border-radius:6px;
background:#eee;cursor:pointer;margin-top:16px}}
#out{{margin-top:18px;font-size:14px;line-height:1.65}}
.ok{{color:#1b5e20;font-weight:bold}} .no{{color:#b71c1c;font-weight:bold}}
table{{border-collapse:collapse;margin-top:10px;font-size:13px}}
th,td{{border:1px solid #ccc;padding:5px 9px;text-align:right}}
th{{background:#fafafa;font-weight:500}} td:first-child,th:first-child{{text-align:left}}
code{{background:#f3f3f3;padding:1px 4px;border-radius:3px}}
</style>
<div id='now'><audio id='player' controls preload='auto'></audio>
<span id='nowlabel'>&mdash; nichts &mdash;</span></div>
<h1>{title}</h1>
<div class='note'>{intro}</div>
{runs}
<h2>Fehlt ein Wort?</h2>
<div class='note'>Was &auml;ndert sich, das keines dieser W&ouml;rter abdeckt? Was hier
steht, wird Text im Eintrag &mdash; also in eigenen Worten. Ein Eindruck, f&uuml;r den der
Eintrag keinen Namen hat, ist einer, den das schreibende Modell nie anfordern kann.</div>
<textarea id='missing' autocomplete='off'></textarea>
<button class='go' id='eval'>Auswerten</button>
<div id='out'></div>
<div class='note' style='margin-top:28px'><b>Was dieser Aufbau nicht kann.</b> Ein
H&ouml;rer liefert H&ouml;rbarkeit, Richtung und die eigene Konsistenz. Eine Aussage
&uuml;ber H&ouml;rer im Allgemeinen liefert er nicht, und sie wird auch nicht behauptet.</div>
<script>
var KEY={keys}, REVEAL={reveal};
var P=document.getElementById('player'),L=document.getElementById('nowlabel'),A=null;
function clr(){{if(A){{A.classList.remove('playing');A=null;}}L.textContent='\\u2014 nichts \\u2014';}}
document.addEventListener('click',function(e){{
  var b=e.target.closest('.clip'); if(!b) return;
  if(b===A){{P.pause();P.currentTime=0;clr();return;}}
  if(A)A.classList.remove('playing');
  A=b;b.classList.add('playing');L.textContent='Klang '+b.textContent.trim();
  P.src=b.getAttribute('data-src');P.play();
}});
P.addEventListener('ended',clr);
document.getElementById('eval').addEventListener('click',function(){{
  var html='',bad=0,dup=0;
  for(var r=0;r<KEY.length;r++){{
    var picked={{}},seen={{}},line=[];
    for(var k in KEY[r]){{
      var v=document.getElementById('s'+r+k).value;
      picked[k]=v; if(v){{ if(seen[v])dup++; seen[v]=1; }}
    }}
    for(var k in KEY[r]){{
      var right=picked[k]===KEY[r][k];
      if(!right)bad++;
      line.push(k+' &rarr; '+(picked[k]||'&mdash;')
        +(right?' <span class=ok>&check;</span>'
                :' <span class=no>&ne; '+KEY[r][k]+'</span>'));
    }}
    html+='<p><b>Durchgang '+(r+1)+':</b> '+line.join(' &middot; ')+'</p>';
  }}
  if(dup)html+='<p class=no>Ein Wort mehrfach vergeben &mdash; jedes Wort geh&ouml;rt zu genau einem Klang.</p>';
  if(bad===0)html+='<p class=ok>Alle W&ouml;rter treffen ihren Klang, in beiden '
    +'Durchg&auml;ngen. Die Anker halten &mdash; der Eintrag sagt dem Modell, was es h&ouml;rt.</p>';
  else html+='<p class=no>'+bad+(bad===1?' Zuordnung weicht':' Zuordnungen weichen')+' ab. Jede davon ist '
    +'ein Wort, das seinen Klang nicht erreicht; im Eintrag steht danach deins.</p>';
  var m=document.getElementById('missing').value.trim();
  if(m)html+='<p><b>Fehlendes Wort:</b> &bdquo;'+m.replace(/</g,'&lt;')+'&ldquo;</p>';
  document.getElementById('out').innerHTML=html+REVEAL;
  this.disabled=true;
}});
</script>
"""

RUN = """<h2>Durchgang {n}</h2>
<div class='note'>Ordne jedem Klang das Wort zu, das der Eintrag daf&uuml;r vorsieht.
Ein Klick spielt, ein anderer Klang l&ouml;st den laufenden ab, Klick auf den gelben Knopf
stoppt. Jedes Wort geh&ouml;rt zu genau einem Klang.</div>
{rows}
"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--key", required=True, help="lexicon entry, e.g. plucked_wire")
    ap.add_argument("--param", required=True, help="parameter whose ANCHOR WORDS are on trial")
    ap.add_argument("--freq", type=float, default=220.0)
    ap.add_argument("--dur", type=float, default=4.5)
    ap.add_argument("--runs", type=int, default=2)
    ap.add_argument("--seed", type=int, default=20260727)
    ap.add_argument("--source", default="", help="what the opcode source says, for the reveal")
    ap.add_argument("--out", default=str(REPO / "tools" / "lco_listening"))
    args = ap.parse_args()

    inst = entry_of(args.key)
    par = (inst.get("params") or {}).get(args.param)
    if not par:
        raise SystemExit(f"{args.key} has no parameter {args.param!r}; "
                         f"it has {sorted((inst.get('params') or {}))}")
    anchors = par.get("anchors") or {}
    if len(anchors) < 3:
        raise SystemExit(f"{args.key}.{args.param} declares {len(anchors)} anchor(s). "
                         "Two is a forced choice and settles little; three or more is a "
                         "real assignment. Give the parameter its anchors first.")

    words = list(anchors)                       # the strings the author model reads
    rnd = random.Random(args.seed)
    letters = [chr(65 + i) for i in range(len(words))]
    rnd.shuffle(letters)                        # so the answer is not A B C in word order
    out = Path(args.out) / f"{args.key}_{args.param}"
    out.mkdir(parents=True, exist_ok=True)

    facts, key = [], {}
    for letter, w in zip(letters, words):
        v = float(anchors[w]["value"])
        y, err = M.render(set_param(inst["code"], args.param, v),
                          dur=args.dur, freq=args.freq, preroll=PREROLL)
        if y is None:
            raise SystemExit(f"{args.param}={v} does not render: {err}")
        sf.write(out / f"{letter}.wav", y, M.SR)
        t = decay_time(y)
        facts.append((letter, w, v, float(np.abs(y).max()), t,
                      M.centroid(y, t0=0.02, t1=0.30), M.centroid(y, t0=0.30, t1=1.60)))
        key[letter] = w

    runs, keys, seen = "", [], []
    for r in range(args.runs):
        order = sorted(letters)
        while order == sorted(letters) or order in seen:
            rnd.shuffle(order)
        seen.append(order[:])
        keys.append(key)
        opts = "".join(f"<option>{w}</option>" for w in sorted(words))
        runs += RUN.format(n=r + 1, rows="".join(
            f"<div class='row'><button class='clip' data-src='{L}.wav'>{L}</button>"
            f"<select id='s{r}{L}'><option value=''>&mdash; Wort w&auml;hlen &mdash;</option>"
            f"{opts}</select></div>" for L in order))

    peaks = {round(p, 3) for _l, _w, _v, p, _t, _c0, _c1 in facts}
    rows = "".join(
        f"<tr><td>{L}</td><td>{w}</td><td>{v:g}</td>"
        f"<td>{'&mdash;' if t is None else f'{t:.2f} s'}</td>"
        f"<td>{c0:.0f} Hz</td><td>{c1:.0f} Hz</td></tr>"
        for L, w, v, _p, t, c0, c1 in sorted(facts, key=lambda f: f[2]))
    reveal = json.dumps(
        "<h2 style='border:0'>Was der Eintrag und der Quelltext sagen</h2>"
        + (f"<p><b>Quelltext:</b> {args.source}</p>" if args.source else "")
        + "<p><b>Die Anker, wie sie heute im Eintrag stehen</b>, mit dem, was daran "
          "gemessen ist &mdash; Abklingzeit bis 20 dB unter dem Anschlag und der spektrale "
          "Schwerpunkt am Anfang und im weiteren Verlauf der Note:</p>"
          "<table><tr><th>Klang</th><th>Wort</th><th>" + args.param + "</th>"
          "<th>Abklingzeit</th><th>Schwerpunkt Anfang</th><th>Schwerpunkt sp&auml;t</th></tr>"
        + rows + "</table>"
        + "".join(f"<p><b>{w}</b> &mdash; {anchors[w].get('gloss', '')}</p>"
                  for _l, w, _v, _p, _t, _c0, _c1 in sorted(facts, key=lambda f: f[2]))
        + "<p>Welche dieser Zahlen den Eindruck tr&auml;gt, sagt keine von ihnen. "
          "Deshalb entscheidet die Zuordnung oben und nicht die Tabelle.</p>")

    intro = (f"<b>{args.key} &middot; {args.param}</b> bei {args.freq:.0f} Hz. "
             f"{len(words)} Kl&auml;nge, das sind die {len(words)} Anker, die der Eintrag "
             f"f&uuml;r diesen Regler deklariert &mdash; nicht ein gleichm&auml;&szlig;iger "
             f"Durchlauf, denn das Modell liest Anker.<br>"
             "Auf dem Pr&uuml;fstand stehen die W&ouml;rter, nicht der Regler: erreicht "
             "jedes Wort den Klang, den es benennt? "
             + ("Der Anschlag ist bei allen gleich laut (Spitzenwert "
                f"{peaks.pop():.3f}), die Lautst&auml;rke kann die Zuordnung also nicht "
                "verraten." if len(peaks) == 1 else
                "<b>Achtung:</b> die Spitzenwerte unterscheiden sich, die Lautst&auml;rke "
                "kann die Zuordnung mitverraten."))

    (out / "index.html").write_text(
        PAGE.format(title=f"{args.key} &middot; {args.param} &mdash; treffen die W&ouml;rter?",
                    intro=intro, runs=runs,
                    keys=json.dumps(keys), reveal=reveal), encoding="utf-8")

    print(f"{out}/index.html")
    for L, w, v, p, t, c0, c1 in sorted(facts, key=lambda f: f[2]):
        print(f"  {L}  {w:<10} {args.param}={v:<6g} Spitze {p:.3f}  "
              + ("T20 —     " if t is None else f"T20 {t:.2f} s  ")
              + f"Schwerpunkt {c0:.0f} -> {c1:.0f} Hz")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
