#!/usr/bin/env python3
"""One page per named voicing of `ep_fm3`, on the same terms as
`tools/lco_param_page.py`: the parameter combination is on screen, and ONE gain
for the whole set so nothing about the level is per-file."""
import html
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

REPO = Path("/Users/joerissen/ai/t5ynth")
sys.path.insert(0, str(REPO / "tools"))
import lco_measure as M          # noqa: E402
import lco_param_page as P       # noqa: E402

OUT = REPO / "tools" / "lco_listening" / "ep_fm3_voicings"
CLIPS = OUT / "clips"
FREQ, DUR = 220.0, 7.0

V = [
    ("rhodes mark i",       dict(tine=14.0, ting=0.62, ring=0.30, hollow=0.35, strike=0.75, decay=0.38),
     "growlier and barkier — the early tines and old pickups"),
    ("rhodes mark ii",      dict(tine=18.0, ting=0.55, ring=0.45, hollow=0.30, strike=0.62, decay=0.38),
     "more bell than bark, a little brighter"),
    ("rhodes mark v",       dict(tine=20.0, ting=0.52, ring=0.45, hollow=0.28, strike=0.58, decay=0.40),
     "as the mark ii, with more clarity"),
    ("wurlitzer 200a",      dict(tine=7.0,  ting=0.75, ring=0.75, hollow=0.80, strike=0.90, decay=0.55),
     "a reed and not a tine: the metal close to the body, the bark most of the attack, "
     "and gone in half a Rhodes's time"),
    ("rhodes, softly played", dict(tine=22.0, ting=0.55, ring=0.08, hollow=0.15, strike=0.15, decay=0.30),
     "the source's own words: “glockenspiel-like, with an extremely short transient "
     "showing higher partials”"),
    ("(the entry's own defaults)", {}, "for comparison — what a bare “electric piano” gives"),
]

e = P.entry_of("ep_fm3")
CLIPS.mkdir(parents=True, exist_ok=True)


def render(over, freq=FREQ):
    b = e["code"]
    for n, p in e["params"].items():
        b = P.set_param(b, n, over.get(n, p["default"]), e)
    y, err = M.render(b, dur=DUR, freq=freq)
    if err:
        sys.exit(err)
    return np.asarray(y, dtype=float)


rows = []
for name, over, gloss in V:
    rows.append((name, over, gloss, {f: render(over, f) for f in (110.0, 220.0, 440.0)}))

peak = max(np.max(np.abs(y)) for _, _, _, ys in rows for y in ys.values())
gain = 0.85 / peak
print(f"one gain for the set: {20 * np.log10(gain):+.2f} dB")

cells = []
for name, over, gloss, ys in rows:
    slug = name.replace(" ", "_").replace(",", "").replace("(", "").replace(")", "").replace("'", "")
    files = []
    for f, y in ys.items():
        fn = f"{slug}_{int(f)}.wav"
        sf.write(str(CLIPS / fn), y * gain, 48000)
        files.append((f, fn))
    full = {n: over.get(n, p["default"]) for n, p in e["params"].items()}
    setting = "  ".join(f"<b>{html.escape(k)}</b> {v:g}" for k, v in full.items())
    players = "".join(
        f'<div class=p><span>{int(f)} Hz</span>'
        f'<audio controls preload=none src="clips/{fn}"></audio></div>' for f, fn in files)
    cells.append(f"<section><h2>{html.escape(name)}</h2>"
                 f"<p class=g>{gloss}</p><p class=s>{setting}</p>{players}</section>")

(OUT / "index.html").write_text(f"""<!doctype html><meta charset=utf-8>
<title>ep_fm3 — named voicings</title>
<style>
 body{{font:15px/1.5 -apple-system,system-ui,sans-serif;max-width:820px;margin:2rem auto;
   padding:0 1rem;background:#14161a;color:#e6e6e6}}
 h1{{font-size:1.35rem;margin-bottom:.2rem}} h2{{font-size:1.05rem;margin:0 0 .2rem}}
 section{{border:1px solid #2c3038;border-radius:8px;padding:.9rem 1rem;margin:.8rem 0;
   background:#191c21}}
 .g{{margin:.1rem 0 .5rem;color:#a8b0bb}} .s{{font:12px ui-monospace,Menlo,monospace;
   color:#7fd1b9;margin:.2rem 0 .7rem}} .s b{{color:#5aa }}
 .p{{display:flex;align-items:center;gap:.7rem;margin:.25rem 0}}
 .p span{{width:4.5rem;color:#8b93a0;font:12px ui-monospace,monospace}}
 audio{{height:32px;flex:1}} .n{{color:#a8b0bb;font-size:13px}} code{{color:#d8b46a}}
</style>
<h1>ep_fm3 — named voicings</h1>
<p class=n>{DUR:g} s, three registers, <b>one gain for the whole set</b>
({20 * np.log10(gain):+.2f} dB) — nothing here is normalised per file, so what you hear
between the rows is the entry and not the renderer.</p>
<p class=n>Where the differences come from: Pfeifle &amp; Münster, „Tone Production of the
Wurlitzer and Rhodes E-Pianos“, DAGA 2017 Kiel, 556–559 — both timbres come from the
<b>pickup</b>, not the vibrator. A Rhodes tine centred on the wedge magnet outputs at twice
the fundamental; off centre, and under a harder strike, the flux change gets asymmetric and
the harmonics come in. The inharmonic part is the brass tonebar and is strongest in the upper
register. The Wurlitzer's reed has negligible higher modes — its harmonics are the
capacitance changing differently on each excursion, which is the bark.</p>
<p class=n><b>The rows are not measured.</b> The source gives the direction of each setting;
the numbers are the entry's and yours to overrule. The effects these instruments are
inseparable from — the suitcase tremolo, the Small Stone phaser, the Wurlitzer's overdrive —
are <b>not</b> in here.</p>
{''.join(cells)}
""")
print("→", OUT / "index.html")
