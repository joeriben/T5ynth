"""Item 20 asks whether the noise-rung metal family really stands still.

`motion_coherence` says they do. The waterphone showed that meter failing a corner
whose centroid travels 1.71 octaves while passing one that travels 1.11 -- it looks for
a coherent TRAJECTORY, and erratic motion reads to it as variance. So ask the question a
listener would, on the bodies item 20 names plus a control group that the gate passes:
over a 4 s note, how far does the colour wander and how much does the level swing?
"""
import json, sys
import numpy as np
sys.path.insert(0, "/Users/joerissen/ai/t5ynth/tools")
import lco_measure as M

SUSPECT = ["glass", "struck_glass", "struck_bar", "drum_head", "cymbal", "mbira",
           "handpan", "ice", "metallic_fm"]
CONTROL = ["string", "strings", "brass", "flute", "theremin", "organ", "tanpura"]
STATIC  = ["sine", "saw", "square", "triangle", "sub_sine"]

def frames(y, n=0.10):
    w = int(n * M.SR); k = len(y) // w
    f = y[:k * w].reshape(k, w)
    rms = 20 * np.log10(np.maximum(np.sqrt((f ** 2).mean(1)), 1e-12))
    sp = np.abs(np.fft.rfft(f * np.hanning(w), axis=1)) ** 2
    fr = np.fft.rfftfreq(w, 1 / M.SR)
    cen = (sp * fr).sum(1) / np.maximum(sp.sum(1), 1e-30)
    return rms, cen

lex = json.loads(open("/Users/joerissen/ai/t5ynth/backend/dco_lexicon.json").read())
by = {t["key"]: t for t in lex["techniques"]}
print(f"{'entry':>14} {'grp':>4} | {'moves':>5} {'coher':>6} | "
      f"{'octaves p10-p90':>15} {'level swing dB':>14}   (mean of 3 prerolls, 220 Hz)")
for grp, keys in (("susp", SUSPECT), ("ctrl", CONTROL), ("flat", STATIC)):
    for k in keys:
        if k not in by:
            print(f"{k:>14} {grp:>4} | not in the lexicon"); continue
        code = by[k]["code"]
        oc, sw, mv, co = [], [], [], []
        for pre in (0.5, 2.0, 5.0):
            y, _ = M.render(code, dur=4.0, freq=220.0, preroll=pre)
            if y is None:
                continue
            r = M.measure(y, 220.0)
            mv.append(r["moves"]); co.append(r["motion_coherence"])
            rms, cen = frames(y[int(0.3 * M.SR):])
            lo, hi = np.percentile(cen, 10), np.percentile(cen, 90)
            oc.append(np.log2(max(hi, 1e-9) / max(lo, 1e-9))); sw.append(rms.max() - rms.min())
        if not oc:
            print(f"{k:>14} {grp:>4} | render failed"); continue
        print(f"{k:>14} {grp:>4} | {str(all(mv)):>5} {np.mean(co):6.2f} | "
              f"{np.mean(oc):15.2f} {np.mean(sw):14.1f}")
