#!/usr/bin/env python3
"""The amplifier chain, on a page — every setting visible, one gain for the set.

The chain has no UI (BJ, 2026-07-31: „ggf noch ohne UI"), so this is the only way
to hear it. The sound is `ep_fm3`, because that is the entry that asked for these
effects, at two of its named voicings; the effects themselves are the REAL C++
classes, driven through `tools/audition_amp_effects` in host-sized blocks.

Build the binary first (its own header says how), then:
    .venv/bin/python tools/lco_amp_effects_page.py
"""
import html
import subprocess
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import lco_measure as M          # noqa: E402
import lco_param_page as P       # noqa: E402

TOOL = REPO / "tools" / "audition_amp_effects"
OUT = REPO / "tools" / "lco_listening" / "amp_effects"
CLIPS = OUT / "clips"
FREQ, DUR, SR = 220.0, 7.0, 48000

# The two voicings the settings below are actually about, from `ep_fm3`'s own
# table. Nothing here is a new sound: the point is what the amplifier does to one.
VOICINGS = {
    "tine": dict(tine=14.0, ting=0.55, ring=0.30, hollow=0.10, strike=0.62, decay=0.45),
    "reed": dict(tine=14.0, ting=0.38, ring=0.30, hollow=0.80, strike=0.60, decay=0.33),
}

# Each row: label, voicing, the tool's arguments, and what it claims to be.
SETTINGS = [
    ("dry", "tine", [],
     "the body alone — every effect at its default, which is off"),
    ("cased tremolo", "tine",
     ["trem_amt=0.6", "trem_rate=5.0", "trem_stereo=1.0"],
     "a PAN, because the cased instrument's vibrato alternates between two amplifier pairs — "
     "listen across the stereo image, not to the level"),
    ("reed tremolo", "reed",
     ["trem_amt=0.6", "trem_rate=5.5", "trem_stereo=0.0"],
     "the same control at stereo 0: one amplifier, so it is amplitude and sits in the middle"),
    ("stage + phaser", "tine",
     ["phaser_mix=0.6", "phaser_rate=0.3", "phaser_amt=0.7", "phaser_fb=0.3"],
     "slow, where a phase-shifter pedal sits — juce::dsp::Phaser at a 600 Hz centre"),
    ("bright chorus", "tine",
     ["chorus_mix=0.5", "chorus_rate=0.8", "chorus_amt=0.4"],
     "juce::dsp::Chorus, 7 ms centre delay, no feedback. It THICKENS but does not WIDEN: "
     "the widget holds one LFO and one delay line for all channels (juce_Chorus.h:157–158), "
     "so a signal that arrives identical in both channels leaves identical — measured L−R "
     "of exactly 0. The same is true of the phaser"),
    ("overdrive", "tine",
     ["dist_mix=0.7", "dist_drive=9"],
     "an overdriven AMPLIFIER: the rail droops with the current drawn, carries the "
     "rectifier\u2019s 100 Hz ripple while it is loaded, and clips asymmetrically. "
     "NOT offered as an e-piano bark \u2014 see the note above"),
    ("overdrive, driven", "tine",
     ["dist_mix=0.7", "dist_drive=24"],
     "the same stage at 24 dB instead of 9. Against the dry body the 2nd partial is up "
     "13.3 dB and the 6th 37.6 (at 9 dB: 5.8 and 23.4), and it is +9.15 dB louder over "
     "the whole note against +3.66. The sag shows as the attack contrast going the other "
     "way \u2014 8.9 dB dry, 10.8 at 9 dB, 7.4 here. <b>No rumble is claimed:</b> at this "
     "setting there is LESS below 30 Hz than in the dry body, \u221275.3 dB against \u221266.9"),
    ("cased, whole", "tine",
     ["chorus_mix=0.35", "chorus_rate=0.6", "chorus_amt=0.3",
      "trem_amt=0.5", "trem_rate=4.5", "trem_stereo=0.9"],
     "chorus into the pan — the chain in the order the plugin runs it"),
]

e = P.entry_of("ep_fm3")
CLIPS.mkdir(parents=True, exist_ok=True)


def render_dry(over):
    body = e["code"]
    for name, spec in e["params"].items():
        body = P.set_param(body, name, over.get(name, spec["default"]), e)
    y, err = M.render(body, dur=DUR, freq=FREQ)
    if err:
        sys.exit(err)
    return np.asarray(y, dtype=float)


if not TOOL.exists():
    sys.exit(f"{TOOL} fehlt — erst bauen (Bauzeile steht im Kopf der .cpp)")

dry = {name: render_dry(over) for name, over in VOICINGS.items()}
for name, y in dry.items():
    sf.write(str(CLIPS / f"_src_{name}.wav"), y, SR)

rows = []
for label, voicing, args, gloss in SETTINGS:
    slug = label.replace(" ", "_").replace(",", "").replace("+", "and")
    dst = CLIPS / f"{slug}.wav"
    r = subprocess.run([str(TOOL), str(CLIPS / f"_src_{voicing}.wav"), str(dst)] + args,
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"{label}: {r.stderr.strip()}")
    y, _ = sf.read(str(dst), always_2d=True)
    rows.append((label, voicing, args, gloss, slug, y))

peak = max(float(np.max(np.abs(y))) for *_, y in rows)
gain = 0.85 / peak
print(f"eine Verstärkung für den ganzen Satz: {20 * np.log10(gain):+.2f} dB")

cells = []
for label, voicing, args, gloss, slug, y in rows:
    sf.write(str(CLIPS / f"{slug}.wav"), y * gain, SR)
    setting = " ".join(f"<b>{html.escape(a.split('=')[0])}</b> {a.split('=')[1]}"
                       for a in args) or "<i>alles aus</i>"
    cells.append(
        f"<section><h2>{html.escape(label)}</h2>"
        f"<p class=g>{gloss}</p>"
        f"<p class=s>{setting}</p>"
        f"<p class=v>auf <b>{voicing}</b>, {FREQ:g} Hz</p>"
        f"<audio controls preload=none src=\"clips/{slug}.wav\"></audio></section>")

(OUT / "index.html").write_text(f"""<!doctype html><meta charset=utf-8>
<title>the amplifier chain — distortion, chorus, phaser, tremolo</title>
<style>
 body{{font:15px/1.5 -apple-system,system-ui,sans-serif;max-width:820px;margin:2rem auto;
   padding:0 1rem;background:#14161a;color:#e6e6e6}}
 h1{{font-size:1.35rem;margin-bottom:.2rem}} h2{{font-size:1.05rem;margin:0 0 .2rem}}
 section{{border:1px solid #2c3038;border-radius:8px;padding:.9rem 1rem;margin:.8rem 0;
   background:#191c21}}
 .g{{margin:.1rem 0 .5rem;color:#a8b0bb}}
 .s{{font:12px ui-monospace,Menlo,monospace;color:#7fd1b9;margin:.2rem 0 .3rem}}
 .v{{font:12px ui-monospace,Menlo,monospace;color:#8b93a0;margin:0 0 .6rem}}
 audio{{height:32px;width:100%}} .n{{color:#a8b0bb;font-size:13px}}
</style>
<h1>the amplifier chain</h1>
<p class=n>{DUR:g} s, <b>one gain for the whole set</b> ({20 * np.log10(gain):+.2f} dB) —
nothing is normalised per file, so a row that is louder than the dry one is louder
because the effect made it so.</p>
<p class=n>The sound is <b>ep_fm3</b> at two of its named voicings. The effects are the real
<code>src/dsp/AmpEffects.cpp</code> classes driven in host-sized blocks by
<code>tools/audition_amp_effects</code>, in the plugin's own order:
distortion → chorus → phaser → tremolo, ahead of delay and reverb. They have no UI yet;
the LRO author reaches them by name (<code>fx_trem_stereo</code> and the other eleven).</p>
<p class=n><b>There is no bark row, and there will not be one.</b> BJ, 2026-07-31, after
hearing two attempts: „als sound ok, aber es klingt nicht ab. das ist ein 4-sekunden-bark.
ein bark bei einem epiano klingt aber nach höchstens 1 sek ab … das bark-Sample hat keine
Transiente, die wird verschluckt. und kein Rumble. Mein vorschlag daher, bark nicht
anzubieten." Both halves are in the files: the distortion CUT the attack contrast from the
dry body\u2019s +6.8 dB to +5.3, and its excess over that body GREW across the note, from
+3.9 dB to +5.0 (and +8.4 to +16.5 at the hard setting) instead of dying away.</p>
<p class=n>Why a stage here can never do it, from the corpus BJ asked for \u2014 842 named
e-pianos in the FM patch corpus: the FAR modulator, which is the bright, barking part, falls at
envelope rate <b>50</b> while the carrier falls at <b>25</b>, and drops 24 envelope points
against the near modulator\u2019s 6. The bark has its OWN envelope, about twice as fast as
the note. An amplifier behind the voices has no envelope at all \u2014 it follows the level,
and the level falls more slowly than the modulation, which is exactly why the effect grows
instead of decaying. The bark belongs in the body, where <code>ep_fm3</code> already has it
as its <code>strike</code> axis.</p>
<p class=n><b>There is no rumble either, and it is not pending.</b> BJ asked for „einen
kleinen Übersteuerungs-‚Rumble' in der Transiente", defined the word twice — the
low-frequency disturbance a turntable's rumble filter removes, Link Wray, a truck making
glasses rattle — heard three builds and reported „kein Rumble" each time. His ruling,
2026-07-31: „vergiss den Rumble, Du kannst kein Rumble." So the overdrive row below is a
sag, a ripple and an asymmetric clip, and claims nothing beyond that.</p>
<p class=n>Measured across these files, so it is not a claim about how they sound:
the cased row has an L−R of 0.0410 and the reed row 0.0000 — the one is a pan and
the other is amplitude, which is the difference the two instruments' amplifiers make. The
chorus and phaser rows are also 0.0000, and that one is a LIMIT, not a setting: see the
chorus row. And <b>dry</b> is the source times the shared gain and nothing else.</p>
{''.join(cells)}
""")
print("→", OUT / "index.html")
