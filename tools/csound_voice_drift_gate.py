"""Does the per-voice drift do what it claims -- and only that?

Two things must both be true, and they pull in opposite directions:

  * THREE VOICES on the same note must beat. That is the whole point: a chord on
    an analogue polysynth is wide because each voice has its own oscillator board.
  * ONE VOICE alone must still stand still, because a prompt asking for a static
    tone has to get one. A drift big enough to hear on a single held note would
    have eaten the "static" escape hatch.

So measure both, on the orchestra the engine actually emits.
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from scipy.io import wavfile

sys.path.insert(0, "/Users/joerissen/ai/t5ynth/backend")
import csound_orch as C  # noqa: E402

CS = "/opt/homebrew/bin/csound"
DUR = 10.0
# --sr, and it is not cosmetic. This gate hardcoded 48000 with no way to change
# it, while a sibling gate documented `--sr 44100` in its usage line -- so
# "all three gates green at both rates" was a claim nothing could have produced.
# Passing --sr here did worse than fail: argv is the KEY LIST, so `--sr 44100`
# was read as two key names and the run exited 1 on `FAIL --sr: voices do not
# beat`, which looks like a real finding about a real key.
SR = 48000
if "--sr" in sys.argv:
    i = sys.argv.index("--sr")
    SR = int(sys.argv[i + 1])
    del sys.argv[i:i + 2]


def ctrl(nvoices, hz):
    body = "\n".join(
        f'  chnset 1, "gate{v}"\n  chnset {hz}, "freq{v}"\n'
        f'  chnset 0.8, "vel{v}"\n  chnset 0, "pres{v}"\n'
        f'  chnset 0, "timb{v}"\n  chnset 1, "trig{v}"'
        for v in range(1, nvoices + 1))
    return f"\ninstr 900\n{body}\nendin\n"


def render(key, nvoices, hz=220.0):
    orc, _ = C.build_orchestra(technique_keys=[key])
    orc = orc.replace("%SR%", str(SR)).replace("-n -d", "-d")
    orc = orc.replace("</CsInstruments>", ctrl(nvoices, hz) + "</CsInstruments>")
    keep = []
    for ln in orc.splitlines():
        m = re.match(r"^i 1 0 360000 (\d+)\s*$", ln)
        if m:
            if int(m.group(1)) <= nvoices:
                keep.append(f"i 1 0 {DUR} {m.group(1)}")
            continue
        keep.append("e" if ln.strip() == "e 360000" else ln)
    orc = "\n".join(keep).replace("<CsScore>", f"<CsScore>\ni 900 0 {DUR} 0")
    d = Path(tempfile.mkdtemp())
    try:
        csd, wav = d / "t.csd", d / "t.wav"
        csd.write_text(orc)
        subprocess.run([CS, "-o", str(wav), "-W", "--nodisplays", str(csd)],
                       capture_output=True, text=True, timeout=120)
        sr, x = wavfile.read(wav)
        if x.dtype.kind == "i":
            x = x.astype(np.float64) / np.iinfo(x.dtype).max
        # voices land on their own channels; sum them like the engine's mixer
        n = min(nvoices, x.shape[1] if x.ndim > 1 else 1)
        sig = x[:, :n].sum(axis=1) if x.ndim > 1 else x
        return sr, sig
    finally:
        shutil.rmtree(d, ignore_errors=True)   # 16-channel renders are large


def beat_index(sr, sig):
    """How much the loudness wanders -- std/mean of a 50 ms RMS envelope."""
    sig = sig[int(sr * 0.5):]
    w = int(sr * 0.05)
    env = np.array([np.sqrt(np.mean(sig[i:i + w] ** 2))
                    for i in range(0, len(sig) - w, w)])
    return float(env.std() / (env.mean() or 1e-12))


KEYS = sys.argv[1:] or ["sine", "saw", "square", "triangle", "supersaw",
                        "bass_saw", "chiptune", "pwm", "sync"]
unknown = [k for k in KEYS if k not in C._TONAL_KEYS and k not in C._CS_TECH_EXTRA]
if unknown:
    # A key name that is not in the catalogue renders an orchestra without the
    # branch under test, which measures 0.0000 and reports as a failed key --
    # a typo therefore produced a confident, specific, entirely false finding.
    sys.exit(f"not catalogue keys: {', '.join(unknown)}")
print(f"{'key':11s} {'1 voice':>9} {'3 voices':>9}   reading   (sr={SR})")
fails = []
for k in KEYS:
    b1 = beat_index(*render(k, 1))
    b3 = beat_index(*render(k, 3))
    # Some keys move on their own BY DESIGN -- pwm's duty sweep, supersaw's seven
    # detuned copies, sync's ratio sweep, chiptune's stepped duty. For those the
    # single-voice figure is supposed to be large, and the question is only
    # whether the voices differ from each other.
    if k in C._LIVE_TECH:
        note = ("self-moving by design; voices differ"
                if b3 > b1 * 1.05 else "self-moving by design; voices identical")
    elif k == "sine":
        note = "pure by design (the static escape hatch)"
        if b1 > 0.001:
            fails.append(f"{k}: escape hatch no longer static ({b1:.4f})")
    elif b3 > 0.05 and b1 < 0.02:
        note = "voices beat, single note still stands"
    elif b3 <= 0.05:
        note = "no beating between voices"
        fails.append(f"{k}: voices do not beat ({b3:.4f})")
    else:
        note = "SINGLE NOTE ALSO MOVES"
        fails.append(f"{k}: single held note moves ({b1:.4f})")
    print(f"{k:11s} {b1:9.4f} {b3:9.4f}   {note}")
for f in fails:
    print("  FAIL " + f)
sys.exit(1 if fails else 0)
