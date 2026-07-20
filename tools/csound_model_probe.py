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

BOTH ARE MEASURED OVER THE WHOLE RANGE THE SYNTH CAN ASK FOR -- 20 Hz to 12 kHz,
the clamp in csound_orch.py -- and not over a comfortable middle. This probe
originally tested 110/220/440/880 Hz only, and that gap is the entire reason
wgclar was adopted for `clarinet` and then had to be thrown out again: inside
110-880 it looked immaculate (+1 cent, sustain 1.056), while OUTSIDE it it emits
near-pure DC above sr/10.4 -- silence, at a pitch that depends on the SAMPLE RATE
-- and plays +2792..+6864 cents below 25 Hz. A model that is only probed where it
is comfortable has not been probed. Any candidate must survive the ends.

MEASURED 2026-07-19 on Csound 6.18 (Homebrew, double samples, no STK). Cells are
the share of energy sitting on the comb n*asked_pitch; `xN` means the model is on
the comb but STARTS at the Nth harmonic, i.e. it is playing its own register N
times too high; DC means level without oscillation.

  opcode     sustain   20    25    40   110   220   440   880   2.0k  4.5k  12k
  wgclar       0.929   x5   99%   99%   99%  100%  100%  100%  100%   99%   DC
  wgbow        1.051  x10    x8    x5   99%   99%  100%   97%   11%   DC    DC
  mode         0.826  96%   88%   85%   95%   97%   94%   95%   94%   93%   0%
  streson      0.984   6%    8%   13%   33%   57%   80%   82%   64%   39%   6%
  wgflute      0.396  x13   x10   x13   84%   80%   87%   87%   77%    0%   0%
  fof          0.991  x18   x21   x13    x5  100%  100%   99%   DC    DC    DC
  wgbrass      1.266   x5    x7    x6    x6   x10    x4    x5    0%    0%  SILENT
  wgbowedbar   0.000  silent                            one-shot, not a tone
  marimba/vibes/mandol  silent with sustaining args     struck models, they decay

Reading it:

  * wgclar is in tune across the whole musical range and fails only at the ENDS
    -- its own lowest bore register below ~25 Hz, and pure DC above sr/10.4. That
    is why `clarinet` no longer uses it.
  * wgbrass "plays the bore" at every pitch, as long documented.
  * mode is the one model good over essentially the whole clamp, and it is what
    the modal path uses. It does die at 12 kHz (0% on the comb) -- the top of the
    range is NOT covered, which matters because modal keys place partials at
    RATIOS above kfreq and those ratios reach 12 kHz well before the played note
    does. Not yet addressed.
  * streson's low percentages are the EXCITER, not its tuning: `rand` is
    broadband and most of it passes through unresonated. It carries no wrong-
    register flag anywhere. Judge it with a real exciter before rejecting it.
  * fof is usable from about 220 to 880 Hz and nowhere else -- an earlier flat
    "USABLE" here was wrong.

THE COMB COLUMNS ARE MEANINGLESS FOR A MODEL THAT DECAYS. They are computed over
the whole DUR-second render, so a struck instrument whose half-life is 40 ms is
measured across ~3.96 s of noise floor. Re-run 2026-07-20 after fixing the strike
tables, `marimba` reports 0% on the comb at every pitch -- while a float render
windowed to its actual first second is dead in tune (±0 cents, 110..880 Hz).
`vibes`, which rings ten times longer, reports 100% across the entire 20 Hz..12 kHz
clamp in the same run. The tool's own sustain column already tells you which case
you are in: WHEN SUSTAIN IS NEAR ZERO, READ THE CENTS FROM A WINDOW MATCHED TO THE
DECAY, NOT FROM THIS TABLE. Same lesson as the strike tables below, one layer up --
a metric that is correct for standing tones is not thereby correct for struck ones.

THREE metrics were needed to get this table, because the first two each lied in a
different direction and nearly cost real code:

  1. Autocorrelation alone searched sr/2000..sr/40, so nothing below 40 Hz was
     measurable at all and every low probe came back as a wild error -- `mode` at
     20 Hz read +7973 cents while sitting exactly on 20.0 Hz with 96% of its
     energy in band. It also locks onto a longer period when a low harmonic is
     weak (it called wgclar -1199 at 880 Hz; the bore was textbook-correct).
  2. "Strongest bin == asked pitch" fails the opposite way on harmonic-rich
     sources: streson at 220 Hz peaks on its 3rd harmonic with ~2% at the
     fundamental and is perfectly in tune.
  3. Comb energy fixes both but has its own hole -- a model asked for 20 Hz that
     plays 320 Hz scores ~100%, since 320 is a multiple of 20. Hence n_low.

A probe that condemns working code is exactly as expensive as one that passes
broken code. Confirm any verdict here against a second metric before acting on it.

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
    # CORRECTED 2026-07-20. These three took `imp` -- a function-table number for
    # the STRIKE IMPULSE -- as the scalar 0.02, so Csound raised "No table for
    # Marimba strike" and DELETED THE NOTE on every run. The resulting silence was
    # read as "the model decays on its own" and became §6's rejection of marimba
    # and vibes in docs/LCO_CONCEPT.md. It is also why the table above records
    # them as "silent with sustaining args". Signature is
    #   kamp, kfreq, ihrd, ipos, imp, kvibf, kvamp, ivibfn, idec[, idoubles]
    # (gogobel has no idec). Re-measured with this line at --format=float:
    # marimba and vibes are dead in tune 55..880 Hz and idec plainly works;
    # gogobel plays 20 Hz whatever it is asked for, so it stays rejected.
    "gogobel":  "asig gogobel 0.6, kfr, 0.9, 0.5, giImp, 6, 0.01, giSine",
    "marimba":  "asig marimba 0.6, kfr, 0.9, 0.5, giImp, 6, 0.01, giSine, 0.5",
    "vibes":    "asig vibes 0.6, kfr, 0.9, 0.5, giImp, 6, 0.01, giSine, 0.5",
    # kpluck=0 is NO excitation: this renders exact silence, which is what the
    # "glide response inconsistent" verdict was actually looking at.
    "mandol":   "asig mandol 0.6, kfr, 0.7, 0.7, 0.4, 0.5, giSine",
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
; STRIKE IMPULSE for the Perry Cook struck models (marimba/vibes/gogobel). Their
; `imp` argument is a TABLE NUMBER, not a scalar -- passing a scalar makes Csound
; delete the note with "No table for Marimba strike", and a silent render then
; reads exactly like a model that decays on its own. That mistake is what
; rejected marimba and vibes; see failure mode 9 in docs/LCO_CONCEPT.md.
giImp  ftgen 2, 0, 256, 10, 1, 0.5, 0.3, 0.2, 0.1
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
        # PROOF OF CONTACT, before a single sample is read. Csound writes a wav
        # even when the note was DELETED at init, so "the file exists" proves
        # nothing: an init error yields a perfectly readable file full of
        # silence, and silence is indistinguishable from "this model decays on
        # its own" -- which is exactly how marimba and vibes came to be rejected
        # (failure mode 9, docs/LCO_CONCEPT.md). A negative result about an
        # opcode needs the same proof of contact as a positive one.
        blob = (r.stderr or "") + (r.stdout or "")
        bad = [l for l in blob.splitlines()
               if "INIT ERROR" in l or "PERF ERROR" in l or "note deleted" in l
               or "not found" in l.lower() or "no table" in l.lower()]
        if bad:
            return None, ["RENDER DID NOT HAPPEN -- " + bad[0].strip()] + [
                l.strip() for l in bad[1:2]]
        sr, x = wavfile.read(wav)
        if x.ndim > 1:
            x = x[:, 0]
        if x.dtype.kind == "i":
            x = x.astype(np.float64) / np.iinfo(x.dtype).max
        return (sr, x.astype(np.float64)), None
    finally:
        shutil.rmtree(d, ignore_errors=True)


def harmonic_frac(sr, x, hz, t0=1.0, t1=3.0):
    """How much energy sits on the comb n*hz -- the one test that fits BOTH kinds.

    Neither simpler metric survives this corpus on its own. Autocorrelation locks
    onto a longer period when a low harmonic is weak (it called wgclar -1199 at
    880 Hz while the bore was textbook-correct). "Strongest bin == asked pitch"
    breaks the other way on harmonic-rich sources: streson at 220 Hz peaks on its
    3rd harmonic with only 2% at the fundamental, and is perfectly in tune.

    Energy on integer multiples answers the question both of them were proxies
    for -- is the model playing the pitch it was ASKED for -- and it answers it
    the same way for a near-sinusoidal resonator and a full harmonic comb.

    Returns (fraction, n_low, nbins). n_low closes this test's OWN blind spot: a
    model asked for 20 Hz that actually plays 320 Hz scores a perfect comb
    fraction, because 320 IS a multiple of 20. So we also report the lowest
    harmonic carrying real energy -- n_low == 1 means the model is on the asked
    pitch, n_low == 16 means it transposed the whole tone up by four octaves
    while looking immaculate on the fraction alone.

    The "carries energy" threshold is 1% of the strongest comb bin, not 10%. It
    has to admit a genuinely weak fundamental -- streson at 220 Hz peaks on its
    3rd harmonic and is perfectly in tune -- while still excluding a fundamental
    that is absent rather than quiet, which for a true octave-up tone sits at
    numerical zero. 1% separates those cleanly; 10% did not, and a 10% threshold
    combined with an `n_low > 3` verdict passed a model a full octave-and-a-fifth
    sharp at every pitch.

    nbins is how many comb members exist below Nyquist. When it is 1 the register
    check is not merely passing, it is UNANSWERABLE -- at 12 kHz on a 48 kHz
    orchestra the octave above is 24 kHz, so nothing can distinguish "plays the
    asked pitch" from "plays the octave". Callers must not read n_low == 1 as a
    pass there, which is the top of the range this probe exists to cover.
    """
    seg = x[int(sr * t0):int(sr * t1)]
    if seg.size < 1024 or np.abs(seg).max() < 1e-6:
        return 0.0, 0, 0
    w = seg * np.hanning(len(seg))
    mag = np.abs(np.fft.rfft(w)) ** 2
    freq = np.fft.rfftfreq(len(w), 1.0 / sr)
    total = mag.sum()
    if total <= 0:
        return 0.0, 0, 0
    bins = []
    for n in range(1, 25):
        f = hz * n
        if f >= sr / 2:
            break
        # +-3% is wider than any tuning we would accept and narrow enough that a
        # neighbouring harmonic never falls inside it.
        bins.append(float(mag[(freq > f * 0.97) & (freq < f * 1.03)].sum()))
    if not bins:
        return 0.0, 0, 0
    strongest = max(bins)
    if strongest <= 0:
        # No comb energy at all. Returning n_low = 1 here would read as "on the
        # asked pitch" for a signal that has nothing on the comb whatsoever.
        return 0.0, 0, len(bins)
    n_low = next((i + 1 for i, b in enumerate(bins) if b >= 0.01 * strongest), 0)
    return float(sum(bins) / total), n_low, len(bins)


# The synth's own pitch clamp, ends included. See the module docstring for why
# the ends are not optional.
NOTES = (20.0, 25.0, 40.0, 110.0, 220.0, 440.0, 880.0, 2000.0, 4500.0, 12000.0)


def classify(sr, x, hz):
    """(harmonic fraction, sustain, note, register_checkable) for one pitch.

    `note` names a hard failure so a model cannot be waved through on an average:
    DC and SILENT are disqualifying wherever they appear, not soft marks.
    """
    if x.size == 0 or np.abs(x).max() < 1e-4:
        return None, 0.0, "SILENT", True
    head = np.sqrt(np.mean(x[:int(sr * DUR * 0.2)] ** 2))
    tail = np.sqrt(np.mean(x[int(sr * DUR * 0.8):] ** 2))
    sustain = float(tail / (head or 1e-12))
    # A constant offset has amplitude but no oscillation. wgclar above sr/10.4
    # reads as a healthy peak and a healthy `sustain` while being inaudible, so
    # the AC part has to be tested separately from the level.
    seg = x[int(sr * 0.5):int(sr * 1.5)]
    if seg.size and np.std(seg) < 0.02 * max(np.abs(seg).max(), 1e-12):
        return None, sustain, "DC", True
    frac, n_low, nbins = harmonic_frac(sr, x, hz)
    checkable = nbins >= 2
    if checkable and n_low > 1:
        # On the comb, but not starting on the fundamental: it is playing its
        # own register. n_low == 2 is an octave sharp and n_low == 3 an
        # octave-and-a-fifth -- both were passing while `wgflute` was rejected
        # for the same error expressed in cents.
        return None, sustain, f"x{n_low}", checkable
    return frac, sustain, "", checkable


names = sys.argv[1:] or list(MODELS)
print(f"{'opcode':12s} {'sustain':>8}   " +
      "  ".join((f"{n/1000:.1f}k" if n >= 1000 else f"{n:.0f}").rjust(7)
                for n in NOTES) + "   verdict")
for name in names:
    line = MODELS[name]
    cells, sustains, unchecked, fail = [], [], [], None
    for hz in NOTES:
        out, err = render(line, hz)
        if out is None:
            fail = err
            break
        sr, x = out
        frac, sus, note, checkable = classify(sr, x, hz)
        cells.append(note or f"{frac * 100:.0f}%")
        sustains.append(sus)
        if not checkable:
            unchecked.append(hz)
    if fail:
        print(f"{name:12s} FAIL {fail}")
        continue
    good = [float(c[:-1]) for c in cells if c.endswith("%")]
    verdict = []
    # Below half the energy off the comb, the model is not playing what it was
    # asked for -- whatever it sounds like on its own.
    if good and min(good) < 50:
        verdict.append("OFF-PITCH")
    if any(c.startswith("x") for c in cells):
        verdict.append("WRONG REGISTER")
    if any(c == "DC" for c in cells):
        verdict.append("DC/inaudible")
    if any(c == "SILENT" for c in cells):
        verdict.append("silent")
    # Sustain is a property of every pitch that produced audio, including ones
    # rejected on pitch. Restricting it to "%" cells printed 0.000 for a model
    # that sustains perfectly but is out of tune everywhere -- and in this
    # table's own legend 0.000 means "one-shot, not a tone", which is what got
    # marimba and vibes rejected. A model must not be condemned on a figure that
    # was never measured.
    live = [s for s, c in zip(sustains, cells) if c != "SILENT"]
    if live and min(live) < 0.15:
        verdict.append("DECAYS")
    if unchecked:
        verdict.append("register unverifiable at "
                       + "/".join(f"{h:.0f}" for h in unchecked))
    row = "  ".join(c.rjust(7) for c in cells)
    sus = f"{min(live):8.3f}" if live else "     n/a"
    print(f"{name:12s} {sus}   {row}   "
          + ("  <== " + ", ".join(verdict) if verdict else "usable"))
