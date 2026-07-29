#!/usr/bin/env python3
"""Hear one instrument at ANY combination of its controls -- you set them, it renders.

BEFORE any word is put on trial, the instrument itself has to be heard. A word test
asks whether `worn` reaches the sound it names; it cannot say whether the oscillator
is any good, or where a control stops being usable.

And a row per control, everything else at its default, answers the wrong question.
Neither the author model nor the player moves one knob: both set them all at once,
and BJ (2026-07-28) confirmed these are the same controls the player will see in the
Prompt Orchestra field.

Two earlier layouts both failed, and both failures are the reason this file is a
server rather than a folder of files:

  * 32 corners of the cube as a flat list of seven-number settings. A defensible
    experimental design and unreadable -- a human cannot hear "0 . 0.95 . 1 . 0.05
    . -50 . 0 . 0" as a question.
  * one table per PAIR of controls. Readable, and still only ever two dimensions at
    once. BJ: *"sonst kannst DU nur 2-D-Kombis testen wie jetzt"*.

What is needed is every dimension offered SEPARATELY and the chosen settings
combined -- all seven at once, whatever the listener wants to hear. Pre-rendering
that is 3^7 files for three levels alone, so nothing is pre-rendered: the page is
served by this script, a click sends the whole current setting, Csound renders it,
and it plays. Every render is cached on disk, so a setting is slow once.

The declared anchor values sit in their rows alongside the even steps, with their
words under them, because those are what the author model reads.

One gain for the whole session, measured once at startup over a probe through the
cube, so loudness differences between settings are the instrument's own.

One player. A click plays, a click on another value replaces the running sound, a
click on the yellow one stops.

    .venv/bin/python tools/lco_audition_instrument.py --key analog_osc

The server stops itself after an hour with nobody listening.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import threading
import time
import urllib.parse
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import numpy as np
import soundfile as sf

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

import lco_measure as M  # noqa: E402

LEX = REPO / "backend" / "dco_lexicon.json"
PREROLL = 0.5
PAGE_PEAK = 0.89
STEPS = 5              # even steps offered per dimension, before the anchors are added
PROBE = 13             # Halton points used once to fix the session gain
IDLE_STOP = 3600.0     # seconds without a request before the server shuts itself down
PITCHES = (55.0, 110.0, 220.0, 440.0, 880.0, 1760.0)
PRIMES = [2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31]


def entry_of(key):
    for t in json.loads(LEX.read_text())["techniques"]:
        if t["key"] == key:
            return t
    raise SystemExit(f"no entry {key!r} in {LEX}")


def set_params(body, vals):
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


class Audition:
    def __init__(self, key, dur, out):
        self.inst = entry_of(key)
        self.body = self.inst["code"]
        self.names = list(self.inst["params"])
        self.rng = {n: self.inst["params"][n]["range"] for n in self.names}
        self.dflt = {n: self.inst["params"][n]["default"] for n in self.names}
        self.dur = dur
        self.dir = Path(out) / f"{key}_audition"
        self.dir.mkdir(parents=True, exist_ok=True)
        self.lock = threading.Lock()
        self.last = time.monotonic()
        self.gain = 1.0

    def choices(self, n):
        """The values offered for one dimension: even steps, plus every declared anchor
        at its own value, because the anchors are what the author model reads."""
        lo, hi = self.rng[n]
        vals = [lo + (hi - lo) * i / (STEPS - 1) for i in range(STEPS)]
        words = {}
        for w, a in self.inst["params"][n]["anchors"].items():
            words.setdefault(round(a["value"], 6), []).append(w)
            if not any(abs(a["value"] - v) < 1e-9 for v in vals):
                vals.append(a["value"])
        if not any(abs(self.dflt[n] - v) < 1e-9 for v in vals):
            vals.append(self.dflt[n])
        vals.sort()
        return [(v, " ".join(words.get(round(v, 6), []))) for v in vals]

    def raw(self, vals, freq):
        y, err = M.render(set_params(self.body, vals), dur=self.dur, freq=freq,
                          preroll=PREROLL)
        if y is None:
            raise RuntimeError(err)
        return y

    def calibrate(self):
        peaks = []
        for i in range(PROBE):
            v = {n: self.rng[n][0] + (self.rng[n][1] - self.rng[n][0])
                 * halton(i + 1, PRIMES[k]) for k, n in enumerate(self.names)}
            peaks.append(float(np.max(np.abs(self.raw(v, 220.0)))))
        for corner in (0, 1):
            v = {n: self.rng[n][corner] for n in self.names}
            peaks.append(float(np.max(np.abs(self.raw(v, 220.0)))))
        self.gain = PAGE_PEAK / (max(peaks) * 1.35)
        return max(peaks)

    def wav(self, vals, freq):
        key = json.dumps([[n, round(vals[n], 6)] for n in self.names] + [freq, self.dur])
        path = self.dir / ("s_" + hashlib.sha1(key.encode()).hexdigest()[:16] + ".wav")
        with self.lock:
            self.last = time.monotonic()
            if not path.exists():
                y = self.raw(vals, freq) * self.gain
                sf.write(path, np.clip(y, -0.999, 0.999).astype(np.float32), M.SR,
                         subtype="PCM_24")
        return path


PAGE = """<!doctype html><meta charset=utf-8><title>{title}</title>
<style>
body{{font:15px system-ui;margin:0 24px 60px;max-width:940px;color:#222}}
h1{{font-size:20px;margin:14px 0 4px}}
#now{{position:sticky;top:0;background:#fff;padding:11px 0 9px;margin-bottom:4px;
border-bottom:2px solid #ccc;z-index:20}}
#bar{{display:flex;align-items:center;gap:14px}}
#player{{width:320px;height:34px}}
#state{{font-size:12.5px;color:#444;font-family:ui-monospace,monospace;margin-top:6px;
line-height:1.5}}
#state b{{color:#000}}
.note{{background:#f6f8fa;border:1px solid #dfe2e5;border-radius:6px;padding:10px 13px;
font-size:13px;margin:8px 0 4px;line-height:1.65}}
.dim{{display:flex;align-items:baseline;gap:10px;margin:9px 0;flex-wrap:wrap}}
.dim > .nm{{width:92px;text-align:right;font-size:13.5px;font-weight:600;flex:none}}
.v{{cursor:pointer;border:1px solid #c4c4c4;background:#f6f6f6;border-radius:5px;
padding:5px 9px;font-size:12.5px;line-height:1.25;text-align:center;min-width:46px}}
.v:hover{{background:#ececec}}
.v.on{{background:#1e6fd9;border-color:#164f99;color:#fff}}
.v.on em{{color:#cfe0f7}}
.v em{{display:block;font-style:normal;font-size:10.5px;color:#888}}
.v.dflt{{border-style:dashed}}
a.v{{text-decoration:none;color:inherit}}
button.plain{{cursor:pointer;border:1px solid #c4c4c4;background:#fff;border-radius:5px;
padding:5px 11px;font-size:12.5px}}
button.plain:hover{{background:#f0f0f0}}
.busy #bar::after{{content:'rendert …';font-size:12.5px;color:#b26a00}}
</style>
<div id='now'>
  <div id='bar'><audio id='player' controls preload='auto'></audio>
  <button class='plain' id='again'>nochmal</button>
  <button class='plain' id='reset'>Vorgabe</button></div>
  <div id='state'></div>
</div>
<h1>{title}</h1>
<div class='note'>{intro}</div>
<div id='dims'>{dims}</div>
<script>
var NAMES={names}, DEF={defaults}, CUR=Object.assign({{}}, DEF), FREQ={freq0};
var KEY={keyjson};
var P=document.getElementById('player'), S=document.getElementById('state');
function label(){{
  return NAMES.map(function(n){{return n+' '+(+CUR[n].toFixed(4));}}).join('  ·  ')
         + '   @ ' + FREQ + ' Hz';
}}
function paint(){{
  document.querySelectorAll('.v').forEach(function(b){{
    var n=b.dataset.dim, v=parseFloat(b.dataset.val);
    b.classList.toggle('on', n==='__f__' ? v===FREQ : Math.abs(CUR[n]-v)<1e-9);
  }});
  S.innerHTML='<b>'+label()+'</b>';
}}
function play(){{
  var q=NAMES.map(function(n){{return n+'='+CUR[n];}}).join('&')
          +'&__freq__='+FREQ+'&key='+KEY;
  document.body.classList.add('busy');
  P.src='/render?'+q;
  P.play().then(function(){{document.body.classList.remove('busy');}},
                function(){{document.body.classList.remove('busy');}});
}}
document.addEventListener('click',function(e){{
  var b=e.target.closest('.v'); if(!b) return;
  var n=b.dataset.dim, v=parseFloat(b.dataset.val);
  if(n==='__f__') FREQ=v; else CUR[n]=v;
  paint(); play();
}});
document.getElementById('again').onclick=play;
document.getElementById('reset').onclick=function(){{
  CUR=Object.assign({{}}, DEF); paint(); play();
}};
paint();
</script>
"""


def build_page(a, key, keys):
    dims = ""
    for n in a.names:
        row = ""
        for v, word in a.choices(n):
            cls = "v dflt" if abs(v - a.dflt[n]) < 1e-9 else "v"
            row += (f"<button class='{cls}' data-dim='{n}' data-val='{v}'>{v:g}"
                    + (f"<em>{word}</em>" if word else "<em>&nbsp;</em>") + "</button>")
        note = a.inst["params"][n]["note"].split(". ")[0] + "."
        dims += (f"<div class='dim' title=\"{note}\"><span class='nm'>{n}</span>{row}</div>")
    row = "".join(f"<button class='v' data-dim='__f__' data-val='{f}'>{f:.0f}"
                  "<em>Hz</em></button>" for f in PITCHES)
    dims += f"<div class='dim'><span class='nm'>Tonhöhe</span>{row}</div>"
    row = "".join("<a class='v%s' href='/?key=%s'>%s<em>&nbsp;</em></a>"
                  % (" on" if k == key else "", k, k) for k in keys)
    dims += f"<div class='dim'><span class='nm'>Klangkörper</span>{row}</div>"
    intro = (f"Jede Dimension steht für sich; die Seite baut den Klang aus der "
             f"<b>Kombination aller {len(a.names)}</b>, wie Du sie einstellst. Ein Klick auf "
             "einen Wert setzt ihn und spielt sofort &mdash; alle übrigen bleiben stehen, wo "
             "sie sind. Gestrichelt umrandet ist der Vorgabewert, unter einem Wert steht sein "
             "Ankerwort, wenn der Eintrag eines dafür hat.<br>"
             "Nichts ist vorgerechnet: jede Einstellung wird beim ersten Anhören gerendert "
             "(etwa eine halbe Sekunde) und danach von der Platte gespielt. Eine Verstärkung "
             "für die ganze Sitzung &mdash; was lauter klingt, ist lauter."
             "<br><b>Das ist kein Test und hat keine richtige Antwort.</b> Die Frage ist, ob "
             "der Klangkörper über seinen ganzen Raum brauchbar bleibt.")
    return PAGE.format(title=f"{key} &mdash; durchhören", intro=intro, dims=dims,
                       names=json.dumps(a.names), defaults=json.dumps(a.dflt),
                       freq0=220.0, keyjson=json.dumps(key))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--key", default="analog_osc")
    ap.add_argument("--dur", type=float, default=4.0)
    ap.add_argument("--port", type=int, default=8731)
    ap.add_argument("--out", default=str(REPO / "tools" / "lco_listening"))
    args = ap.parse_args()

    keys = [t["key"] for t in json.loads(LEX.read_text())["techniques"]
            if t.get("params")]
    if args.key not in keys:
        raise SystemExit(f"{args.key!r} hat keine Parameter; da ist nichts zu kombinieren. "
                         f"Mit Parametern: {', '.join(keys)}")
    made, pages, guard = {}, {}, threading.Lock()

    def get(key):
        with guard:
            if key not in made:
                a = Audition(key, args.dur, args.out)
                peak = a.calibrate()
                print(f"{key}: {len(a.names)} Regler, Probenspitze {peak:.2f}, "
                      f"Verstärkung {20 * np.log10(a.gain):+.1f} dB")
                made[key] = a
                pages[key] = build_page(a, key, keys).encode("utf-8")
        return made[key], pages[key]

    a, _ = get(args.key)

    class H(BaseHTTPRequestHandler):
        def log_message(self, *_):
            pass

        def do_GET(self):
            u = urllib.parse.urlparse(self.path)
            q0 = urllib.parse.parse_qs(u.query)
            if u.path in ("/", "/index.html"):
                try:
                    _, html = get(q0.get("key", [args.key])[0])
                except Exception as exc:
                    self.send_error(404, str(exc)[:200])
                    return
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(html)))
                self.end_headers()
                self.wfile.write(html)
                return
            if u.path == "/render":
                q = urllib.parse.parse_qs(u.query)
                try:
                    inst, _ = get(q.get("key", [args.key])[0])
                    vals = {n: float(q[n][0]) for n in inst.names}
                    freq = float(q.get("__freq__", ["220"])[0])
                    data = inst.wav(vals, freq).read_bytes()
                except Exception as exc:                      # a body that will not play
                    self.send_error(500, str(exc)[:200])
                    return
                self.send_response(200)
                self.send_header("Content-Type", "audio/wav")
                self.send_header("Content-Length", str(len(data)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(data)
                return
            self.send_error(404)

    srv = ThreadingHTTPServer(("127.0.0.1", args.port), H)
    url = f"http://127.0.0.1:{args.port}/"
    print(f"{url}   {len(keys)} Klangkörper mit Reglern, umschaltbar auf der Seite")
    print(f"hält an nach {IDLE_STOP / 60:.0f} min ohne Anfrage")

    def idle():
        while True:
            time.sleep(30)
            if all(time.monotonic() - x.last > IDLE_STOP
                   for x in made.values()):
                srv.shutdown()
                return

    threading.Thread(target=idle, daemon=True).start()
    webbrowser.open(url)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    srv.server_close()
    print("angehalten")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
