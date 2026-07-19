"""Do Csound's physical-model opcodes play the pitch they are asked for, and do
they SUSTAIN?

Two hard acceptance criteria before any of them can be used as an oscillator here:

  * PITCH. wgflute is already documented in csound_orch.py as unusable for this
    reason -- asked for 110/165/220/330/440/660 Hz it produced +1945/+135/+102/
    +64/+64/+47 cents, a pitch-dependent error that cannot be compensated. The
    synth owns pitch, so a model that invents its own register is out however
    good it sounds.
  * SUSTAIN. The oscillator is a spectrum source and the synth owns the amplitude
    envelope. A model that decays on its own fights the player's envelope. Measure
    the RMS of the last 20% of a 4 s note against the first 20%: near 0 means the
    model one-shots and cannot be used as a standing tone.

Neither is a timbre judgement. Timbre is judged only in the built Standalone.

MEASURED 2026-07-19 on Csound 6.18 (Homebrew, double samples, no STK):

  opcode       sustain    110Hz   220Hz   440Hz   880Hz   verdict
  wgclar        1.056       +1      +1      +1   (+1)*    USABLE
  wgbow         1.215       +1      +9     +17     +17    borderline: +17c is audible
  mode          1.313       -3      +1      +1     -14    USABLE (the modal path)
  streson       1.015       +1      +1      +1     -14    USABLE (needs an exciter)
  wgbrass       1.850    +5021   +3821   +2621    -619    REJECT: plays the bore
  wgflute       0.652    +1919     +42     +17     +17    REJECT (already documented)
  wgbowedbar    0.000   silent                            one-shot, not a tone
  marimba/vibes/mandol  silent with sustaining args       struck models, they decay

  * wgclar reads -1199 cents at 880 Hz by autocorrelation, but that is the
    METRIC, not the opcode: spectrally the fundamental is strongest at 660/880/
    1200 Hz (2nd harmonic 44-49 dB down, 3rd only 10-13 dB down -- a textbook
    cylindrical bore). Autocorrelation can lock onto a longer period when the
    2nd harmonic is that far below the 3rd. Always confirm a "mistuned" verdict
    spectrally before believing it.

gogobel/moog/pluck FAIL here on argument types, not availability -- their
signatures differ from the ones tried; fix the line before concluding anything.

PARTICLE / COLLISION models and the voice alternatives, same session:

  case             peak   sustain  crest   verdict
  fof             0.510    0.997    2.17   USABLE: sustains, and cheap -- 0.09 s
                                           to render 4 s, same as everything else
  dust2 + reson   0.334    1.029   12.05   USABLE: a Poisson stream of discrete
                                           impacts, each ringing a resonator --
                                           which is what rain and crackle ARE
  dripwater       0.306    0.000   41.29   one-shot; imaxshake did not sustain it
  shaker          1.000    0.000   23.80   one-shot
  bamboo          1.000    1.008    1.00   REJECT: "sustains" only by saturating
  tambourine      1.000    1.035    1.01   REJECT: same -- crest 1.0 is a square

Read the crest column before believing the sustain column. Perry Cook's shaker
family is driven by a shake ENERGY that decays by design; forcing it to keep
going pins the output at full scale, and a crest factor of 1.00 is not a
sustaining texture, it is a clipped one. Lowering kamp did not help (bamboo at
kamp 0.02 still measured crest 1.01) -- the saturation is inside the model.
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from scipy.io import wavfile

CS = "/opt/homebrew/bin/csound"
SR = 48000
DUR = 4.0

# name -> the opcode line producing `asig` at frequency `kfr`
MODELS = {
    "wgclar":   "asig wgclar 0.6, kfr, 0.25, 0.05, 0.2, 0.0, 0, 0, giSine",
    "wgbow":    "asig wgbow 0.6, kfr, 3.0, 0.127, 6.12, 0.01, giSine",
    "wgbrass":  "asig wgbrass 0.6, kfr, 0.4, 0.1, 6.0, 0.05, giSine",
    "wgflute":  "asig wgflute 0.6, kfr, 0.32, 0.1, 0.9, 0.05, 0, 0, giSine",
    "wgbowedbar": "asig wgbowedbar 0.6, kfr, 0.2, 3.0, 0.5",
    "mode1":    "aexc rand 0.004\n  asig mode aexc, kfr, 200",
    # a Poisson rain of impacts, each ringing a resonator -- the material shape of
    # rain and crackle. Pitch-independent, so the cents columns are meaningless
    # for it; read the sustain and crest columns.
    "dust2":    "aimp dust2 0.35, 900\n  asig reson aimp, 2400, 1800, 2",
    # CHANT formant grains -- the alternative voice idiom to saw+reson.
    "fof":      ("asig fof 0.5, kfr, 600, 0, 60, 0.003, 0.02, 0.007, "
                 "20, giSine, giWin, 3600"),
    "gogobel":  "asig gogobel 0.6, kfr, 0.9, 0.5, 0.02, giSine, 1000",
    "marimba":  "asig marimba 0.6, kfr, 0.9, 0.5, 0.02, giSine, 1000, 0.1, 0.5",
    "vibes":    "asig vibes 0.6, kfr, 0.9, 0.5, 0.02, giSine, 1000, 0.1, 0.5",
    "mandol":   "asig mandol 0.6, kfr, 0.0, 0.7, 0.4, 0.5, giSine",
    "moog":     "asig moog 0.6, kfr, 0.1, 0.9, 0.5, 0.5, 0.8, giSine, giSine, giSine",
    "pluck":    "asig pluck 0.6, kfr, kfr, 0, 1",
    "streson":  "aexc rand 0.02\n  asig streson aexc, kfr, 0.9",
}


def render(line, hz):
    orc = f"""<CsoundSynthesizer>
<CsOptions>
-d
</CsOptions>
<CsInstruments>
sr = {SR}
ksmps = 64
nchnls = 1
0dbfs = 1
giSine ftgen 1, 0, 65536, 10, 1
giWin  ftgen 9, 0, 4096, 19, 0.5, 0.5, 270, 0.5   ; fof grain envelope
instr 1
  kfr = {hz}
  {line}
  out asig
endin
</CsInstruments>
<CsScore>
i 1 0 {DUR}
e
</CsScore>
</CsoundSynthesizer>
"""
    # Always clean up: these renders are large, and a probe that fills the disk
    # is worse than no probe. (Learned the hard way -- an untrimmed 16-channel
    # score once wrote a single 21 GB wav here.)
    d = Path(tempfile.mkdtemp())
    try:
        csd, wav = d / "t.csd", d / "t.wav"
        csd.write_text(orc)
        r = subprocess.run([CS, "-o", str(wav), "-W", "--nodisplays", str(csd)],
                           capture_output=True, text=True, timeout=90)
        if not wav.exists():
            errs = [l for l in (r.stderr or "").splitlines() if "error" in l.lower()]
            return None, (errs[:2] or [(r.stderr or "")[-160:]])
        sr, x = wavfile.read(wav)
        if x.ndim > 1:
            x = x[:, 0]
        if x.dtype.kind == "i":
            x = x.astype(np.float64) / np.iinfo(x.dtype).max
        return (sr, x.astype(np.float64)), None
    finally:
        shutil.rmtree(d, ignore_errors=True)


def f0_of(sr, x, t0, t1):
    seg = x[int(sr * t0):int(sr * t1)]
    seg = seg - seg.mean()
    if not np.any(seg):
        return 0.0
    ac = np.correlate(seg, seg, "full")[len(seg) - 1:]
    ac = ac / ac[0]
    lo, hi = int(sr / 2000), int(sr / 40)
    return sr / (lo + int(np.argmax(ac[lo:hi])))


NOTES = (110.0, 220.0, 440.0, 880.0)
names = sys.argv[1:] or list(MODELS)
print(f"{'opcode':12s} {'sustain':>8}   " +
      "  ".join(f"{n:.0f}Hz".rjust(9) for n in NOTES))
for name in names:
    line = MODELS[name]
    cents, sustain = [], None
    fail = None
    for hz in NOTES:
        out, err = render(line, hz)
        if out is None:
            fail = err
            break
        sr, x = out
        if x.size == 0 or np.abs(x).max() < 1e-4:
            cents.append(None)
            continue
        if sustain is None:
            head = np.sqrt(np.mean(x[:int(sr * DUR * 0.2)] ** 2))
            tail = np.sqrt(np.mean(x[int(sr * DUR * 0.8):] ** 2))
            sustain = tail / (head or 1e-12)
        got = f0_of(sr, x, 0.5, 1.5)
        cents.append(1200 * np.log2(got / hz) if got > 0 else None)
    if fail:
        print(f"{name:12s} FAIL {fail}")
        continue
    cs = "  ".join(("  SILENT" if c is None else f"{c:+8.0f}").rjust(9)
                   for c in cents)
    verdict = ""
    good = [c for c in cents if c is not None]
    if good and max(abs(c) for c in good) > 20:
        verdict = "  <== MISTUNED"
    if sustain is not None and sustain < 0.15:
        verdict += "  <== DECAYS"
    print(f"{name:12s} {sustain if sustain is not None else 0:8.3f}   {cs}{verdict}")
