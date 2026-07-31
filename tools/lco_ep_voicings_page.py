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
    ("rhodes",          dict(tine=14.0, ting=0.55, ring=0.30, hollow=0.10, strike=0.62, decay=0.45),
     "the corpus's centre of gravity — 971 voices, far ratio 14 in 521 of them"),
    ("rhodes suitcase", dict(tine=12.0, ting=0.62, ring=0.30, hollow=0.05, strike=0.62, decay=0.45),
     "a lower tine and a hotter one — 24 voices at 11, 13.5, 13, 12"),
    ("rhodes stage",    dict(tine=14.0, ting=0.42, ring=0.30, hollow=0.05, strike=0.55, decay=0.45),
     "the softest drive of any group (level 64 against 80)"),
    ("rhodes mark",     dict(tine=14.0, ting=0.45, ring=0.30, hollow=0.05, strike=0.50, decay=0.22),
     "rings on — the one thing the 21 Mark-named patches share is a carrier that "
     "barely decays, 99→93 against everything else's 99→75"),
    ("dyno",            dict(tine=26.0, ting=0.75, ring=0.30, hollow=0.35, strike=0.72, decay=0.45),
     "157 voices, bimodal on the tine ratio — 14 in 32 and 26 in 30 — and the hottest "
     "drive of any group. That second mode is what the name means here"),
    ("wurlitzer",       dict(tine=14.0, ting=0.38, ring=0.30, hollow=0.80, strike=0.60, decay=0.33),
     "the even partials cancelled: 41 % of its modulators sit in the 2–8 band, 198 of "
     "them at exactly 2.0, against 3 % for the tine group. Weakest drive, and it rings "
     "LONGER than the tine group — the opposite of the acoustic instruments"),
    ("tine",            dict(tine=12.0, ting=0.38, ring=0.65, hollow=0.20, strike=0.58, decay=0.28),
     "99 voices at ratio 12, and the far family STAYS instead of dying (99→85, not 99→75)"),
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
<p class=n>Where these come from: <b>this is an FM electric piano, so the reference is what
FM programmers did</b>, not what a real pickup does. Every row is grouped out of the DX7
corpus by the name a programmer wrote on the voice (<code>tools/dx7_corpus.py --profile</code>).</p>
<p class=n>Three things the measurement says and the instrument world does not.
<b>The Mark numbers are not an FM distinction</b> — 21 voices out of 119,296 carry one, there
is no Mark V in the corpus at all, and what the Mark patches share is that they barely decay.
What FM programmers separated instead is <b>dyno</b> (157) and <b>suitcase</b> (24).
<b>The dyno is bimodal</b> on the tine ratio, 14 and 26, and carries the hottest drive.
And, opposite to the acoustic instruments, <b>the reed group decays LESS than the tine
group</b>, and it separates on <code>hollow</code> rather than on <code>tine</code> — both
sit at 14; its mark is a modulator at exactly twice the carrier, which is what
cancels the even partials.</p>
<p class=n>The far-ratio column is measured and transfers as a value. The other five are the
entry's reading of a measured DIRECTION — the DX7's output level rises with modulation index
but does not count cycles. Yours to overrule. The effects these instruments are inseparable
from — the cased tremolo, the phase-shifter pedal, the bark — are <b>not</b> in here.</p>
{''.join(cells)}
""")
print("→", OUT / "index.html")
