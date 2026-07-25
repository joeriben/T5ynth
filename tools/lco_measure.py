#!/usr/bin/env python3
"""Measure an LCO body — with a meter calibrated on signals whose answer is known.

Every number this file reports about a curated instrument was, at some point in
this project, first reported WRONG by a plausible-looking measurement:

  - an FFT-peak `f0` on a noise-excited comb measures the loudest PARTIAL, not
    the fundamental, and reports a confident, wrong pitch;
  - an autocorrelation `f0` octave-errs on a sparse harmonic series (`pluck` came
    back at -1200 and -2789 cents; there is no subharmonic there — f0/2 sits at
    -115 dB);
  - a render that CLIPPED yields plausible-but-wrong spectral numbers (-6032
    cents on a 440 Hz tone) with nothing in the output to say so.

So the measurement instrument is itself an instrument, and this one is calibrated
before it is trusted: `--selftest` renders signals whose f0, colour and comb
contrast are known in advance and asserts the meter returns them. Run it whenever
this file, `backend/lco_write.py`'s host scaffold, or the Csound version changes.
A number from an uncalibrated meter is not evidence.

The scaffold is DERIVED from `lco_write._HEAD`/`_TAIL` by substitution, and every
substitution is asserted to match exactly once — so a rename in the real host
fails this harness loudly instead of quietly measuring a different instrument.
What is measured is `asig`, the body's own output, before the host's `kgate *
kvel * kpresGain * HEADROOM` and its final `clip`.

    .venv/bin/python tools/lco_measure.py --selftest
    .venv/bin/python tools/lco_measure.py --key string [--freq 220] [--dur 4]
    .venv/bin/python tools/lco_measure.py --key string --anchor bow=bowed
    .venv/bin/python tools/lco_measure.py --key fm_bell --dump      # print the CSD
"""
import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
import soundfile as sf

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "backend"))

import lco_write as W  # noqa: E402  (needs the path above)

SR = 44100


# ─────────────────────────────────────────────────────────────────────────────
# The scaffold: the real host, with the plugin's per-voice channels stood in for
# ─────────────────────────────────────────────────────────────────────────────

def _sub(text, old, new, what):
    n = text.count(old)
    if n != 1:
        raise SystemExit(
            f"lco_measure is out of step with backend/lco_write.py: expected "
            f"exactly one {what} line ({old.strip()!r}), found {n}. The harness "
            f"would silently measure a different scaffold — fix the substitution.")
    return text.replace(old, new)


def scaffold(body, dur=4.0, freq=220.0, glide=None):
    """The authored body inside the REAL host, driven offline.

    glide=(f0, f1) sweeps the note's pitch exponentially over its whole length,
    which is how a body's behaviour across the keyboard gets measured in one
    render instead of twenty.
    """
    head = W._HEAD
    head = _sub(head, "<CsOptions>\n-n -d\n</CsOptions>",
                "<CsOptions>\n-d\n</CsOptions>", "CsOptions")
    head = _sub(head, "sr = %SR%", f"sr = {SR}", "sample rate")
    head = _sub(head, f"nchnls = {W.NCHNLS}", "nchnls = 1", "channel count")
    kfreq = (f"expon {glide[0]}, {dur}, {glide[1]}" if glide else f"= {freq}")
    for old, new, what in (
        ("  kgateraw chnget Sgate\n", "  kgateraw = 1\n", "gate"),
        ("  kfreqraw chnget Sfreq\n", f"  kfreqraw {kfreq}\n", "frequency"),
        ("  kvel     chnget Svel\n", "  kvel     = 1\n", "velocity"),
        ("  kpres    chnget Spres\n", "  kpres    = 0\n", "pressure"),
        ("  ktimb    chnget Stimb\n", "  ktimb    = 0\n", "timbre"),
        ("  ktrig    chnget Strig\n", "  ktrig    = 1\n", "trigger"),
    ):
        head = _sub(head, old, new, what)

    # asig, not aout: the body's own output is what the library curates. The host
    # then multiplies by HEADROOM and clips at 0.95, so a body has to peak above
    # ~1/HEADROOM before that safety net starts shaping it.
    tail = _sub(W._TAIL, "  outch    ivoice, aout\n", "  out      asig\n", "output")

    indented = "\n".join(("  " + l) if not l.startswith(" ") else l
                         for l in body.splitlines())
    return (head + indented + tail
            + f"i 1 0 {dur} 1\ne {dur}\n</CsScore>\n</CsoundSynthesizer>\n")


def render(body, dur=4.0, freq=220.0, glide=None, keep=None):
    """Render a body. Returns (audio, err); err is None only on a clean render.

    A silent or non-finite render is an ERROR, not a result: every spectral number
    below is meaningless on one, and the failure mode this guards is a confident
    wrong number, not a crash. (The render is float, so a loud body is preserved
    exactly rather than distorted — but one loud enough that the HOST's safety
    clip would act on it is reported too, since that clip is audible and the body
    is then not what was measured.)
    """
    csd = scaffold(body, dur, freq, glide)
    d = Path(keep or tempfile.mkdtemp())
    d.mkdir(parents=True, exist_ok=True)
    (d / "x.csd").write_text(csd)
    r = subprocess.run(["csound", "-W", "--format=float", "-o", str(d / "o.wav"),
                        "-m0", "-d", str(d / "x.csd")],
                       capture_output=True, text=True, timeout=120)
    err = re.sub(r"\x1b\[[0-9;]*m", "", r.stderr)
    if r.returncode != 0:
        bad = " | ".join(l for l in err.splitlines() if "error" in l.lower())
        return None, "csound: " + (bad[:200] or err.strip()[-200:])
    y, _ = sf.read(d / "o.wav")
    if y.ndim > 1:
        y = y[:, 0]
    if not np.isfinite(y).all():
        return None, "NaN/Inf"
    peak = float(np.abs(y).max())
    if peak < 1e-5:
        return None, "SILENT"
    if peak * W.HEADROOM > 0.95:
        return y, (f"HOT (peak {peak:.2f}; the host clips a body above "
                   f"{0.95 / W.HEADROOM:.2f})")
    return y, None


# ─────────────────────────────────────────────────────────────────────────────
# The meters
# ─────────────────────────────────────────────────────────────────────────────

def _seg(y, t0, t1):
    return y[int(SR * t0):int(SR * min(t1, len(y) / SR))]


def rms_db(y, t0=0.5, t1=3.5):
    s = _seg(y, t0, t1)
    return float(20 * np.log10(max(np.sqrt((s ** 2).mean()), 1e-12)))


def peak_p999(y):
    """The 99.9th percentile, not the peak.

    A randomly-excited resonator (dust, rand) has a RANDOM peak: the same body
    measured 2.89 on one render and 0.79 on the next. Sizing a gain against the
    maximum of a random process fits the noise, not the instrument.
    """
    return float(np.percentile(np.abs(y), 99.9))


def sustain(y):
    """Last fifth's RMS over the first fifth's — does a held note stand?"""
    n = len(y)
    return float(np.sqrt((y[-n // 5:] ** 2).mean())
                 / max(np.sqrt((y[:n // 5] ** 2).mean()), 1e-12))


def f0(y, t0=0.5, t1=2.5):
    """Period-based, octave-safe f0 — or None when the signal has no pitch.

    Takes the SMALLEST lag whose autocorrelation is within 12% of the best in
    range and is a genuine local peak, so a 2x lag cannot win and the shoulder of
    a bigger peak cannot masquerade as one. Returns None rather than a fabricated
    number when nothing in range is periodic enough.
    """
    seg = _seg(y, t0, t1)
    if len(seg) < 1024:
        return None
    seg = seg - seg.mean()
    if np.sqrt((seg ** 2).mean()) < 1e-8:
        return None
    n = 1 << int(np.ceil(np.log2(len(seg) * 2)))
    S = np.fft.rfft(seg * np.hanning(len(seg)), n)
    ac = np.fft.irfft(np.abs(S) ** 2, n)[:len(seg)]
    if ac[0] <= 0:
        return None
    ac = ac / ac[0]
    lo, hi = max(2, int(SR / 14000)), min(len(ac) - 2, int(SR / 15))
    w = ac[lo:hi]
    if len(w) < 8:
        return None
    best = w.max()
    if best < 0.20:
        return None
    for c in np.where(w >= best * 0.88)[0]:
        i = c + lo
        if ac[i] >= ac[i - 1] and ac[i] >= ac[i + 1]:
            a, b, cc = ac[i - 1], ac[i], ac[i + 1]
            den = a - 2 * b + cc
            return float(SR / (i + (0.5 * (a - cc) / den if den else 0)))
    return None


def cents(measured, asked):
    if not measured or not asked:
        return None
    return float(1200 * np.log2(measured / asked))


def _mag(y, t0, t1):
    s = _seg(y, t0, t1)
    S = np.abs(np.fft.rfft(s * np.hanning(len(s))))
    return S, np.fft.rfftfreq(len(s), 1 / SR)


def centroid(y, t0=0.5, t1=3.5):
    """The amplitude-weighted spectral centroid — a DESCRIPTIVE colour coordinate.

    Every centroid figure quoted in the library's notes is this one. It is not the
    right weighting for an audibility CLAIM: see `centroid_audible`.
    """
    S, fq = _mag(y, t0, t1)
    return float((S * fq).sum() / max(S.sum(), 1e-12))


def centroid_audible(y, t0=0.5, t1=3.5):
    """The POWER-weighted centroid, for asking whether a listener would notice.

    An amplitude-weighted centroid on a linear frequency axis hands enormous
    leverage to quiet high-frequency energy: a partial 60 dB down at 18 kHz
    contributes 0.001 x 18000 against the fundamental's 1 x 220. So a standing
    220 Hz sine plus an inaudible 18 kHz whisper tremoloing at 0.5 Hz passed
    `moves()` with a span of 18.7 Hz at coherence 0.996 — a "movement by default"
    gate satisfied by a static tone.

    Power weighting is the standard definition anyway, and it removes the leverage
    arithmetically rather than by a threshold: that same signal now spans 0.04 Hz.
    The descriptive `centroid` is left alone because the whole library's notes quote
    it and because a colour coordinate and a perceptual claim are different
    questions. Measured over the 50 shipped bodies, power/amplitude runs 0.06 to
    1.07 with a median of 0.59, so the two are NOT interchangeable — do not compare
    a number from one against a number from the other.
    """
    S, fq = _mag(y, t0, t1)
    P = S ** 2
    return float((P * fq).sum() / max(P.sum(), 1e-12))


def partials(y, f, n=16, t0=0.5, t1=3.5):
    """Per-harmonic level in dB relative to the strongest of them."""
    S, fq = _mag(y, t0, t1)
    out = []
    for k in range(1, n + 1):
        m = (fq > f * k * 0.985) & (fq < f * k * 1.015)
        out.append(float(S[m].max()) if m.any() else 0.0)
    ref = max(out) or 1.0
    return [round(float(20 * np.log10(max(v, 1e-9) / ref)), 1) for v in out]


def comb_contrast(y, f, n=24, t0=0.5, t1=3.5):
    """How strongly the spectrum is combed at f, in dB.

    Harmonic bands against the half-way bands between them. The measure damping
    actually needs: counting "how many partials are visible" answers a different
    question and moves with the exciter's colour, not with the resonator's Q.
    Calibrated: white noise 0.1 dB, streson fb 0.995 / 0.90 / 0.575 -> 35.4 /
    22.3 / 10.7 dB.

    Two details are load-bearing, and getting either wrong reads as "the comb is
    shallow" on a resonator that is in fact ringing hard:
      - the bands are a fixed width in Hz (a fraction of the FUNDAMENTAL), not a
        percentage of each band's own centre, which would widen with k until a
        high harmonic's band is mostly the valley beside it;
      - the peak of each band, not its mean, since a comb tooth is narrow and
        averaging it against its own skirts dilutes exactly what is measured.
    Together they make the reading independent of the band width — 35.0 to 36.1 dB
    across a 7x sweep of it — which is the property that makes it a meter rather
    than a knob.
    """
    S, fq = _mag(y, t0, t1)
    half = f * 0.08
    peaks, valleys = [], []
    for k in range(1, n + 1):
        for centre, bucket in ((f * k, peaks), (f * (k + 0.5), valleys)):
            m = (fq > centre - half) & (fq < centre + half)
            if m.any():
                bucket.append(float(S[m].max()))
    if not peaks or not valleys:
        return 0.0
    return float(20 * np.log10(max(np.mean(peaks), 1e-12)
                               / max(np.mean(valleys), 1e-12)))


def logspec_rel(y, freq, lo_c=-600.0, hi_c=4200.0, cents_per_bin=20.0,
                t0=0.5, t1=3.5):
    """The magnitude spectrum on a log axis measured FROM the played note.

    The frame is what makes this a tracking meter. Expressed relative to its own
    `kfreq`, a sound that follows the keyboard has the SAME spectrum at every
    pitch — a harmonic comb, an inharmonic modal bank and a formant set alike,
    since scaling f0 is a pure translation on a log axis. One that ignores the
    keyboard has a spectrum that slides by exactly the interval played.

    The upper bound is a resolution limit. Above ~+4200 cents (five harmonics past
    the fourth octave) a 20-cent grid can no longer RESOLVE adjacent harmonics —
    the spacing 1200*log2(1+1/h) falls under two bins around h=43 — so a saw's comb
    turns into a smooth mush that correlates with itself at any shift, which is
    what read a saw's octave as 1360 of 3600 cents. The lower bound only has to
    reach under anything an oscillator puts energy into, and -1500 cents does:
    it was -600, which is above a sub-octave, and a body an octave down (`sub_sine`
    territory) then fell out of the band entirely and was reported "mixed".

    Each grid cell takes the LARGEST bin it spans, not a sample at its centre.
    This is not a refinement — a point sample makes the meter lie about most of
    the library. Vibrato smears a partial over several FFT bins, and the grid step
    at the top of the band is wider than that smear, so the point at the nominal
    frequency can land in the notch BETWEEN the two turning-point peaks: measured
    on the shipped `saw` at 880 Hz, the band around the fundamental peaks at 12349
    while the sample at exactly 880.0 Hz reads 11.5, a factor of 1073. That drove
    `r_note` for `saw` to 0.149 (0.957 with the max, 0.992 with the vibrato
    switched off), and `pulse` to within 0.032 of the meter declaring a plain pulse
    wave not to follow the keyboard. `comb_contrast` in this file already takes the
    band peak for exactly this reason.
    """
    S, fq = _mag(y, t0, t1)
    grid = np.arange(lo_c, hi_c, cents_per_bin)
    edges = freq * 2 ** ((np.append(grid, grid[-1] + cents_per_bin)
                          - cents_per_bin / 2) / 1200.0)
    idx = np.searchsorted(fq, edges)
    out = np.empty(len(grid))
    for i in range(len(grid)):
        a, b = idx[i], max(idx[i + 1], idx[i] + 1)
        seg = S[a:b]
        out[i] = seg.max() if seg.size else 0.0
    return grid, out


def _corr(a, b):
    """Pearson r, or None where there is nothing to correlate.

    None rather than a sentinel number: a -2.0 standing in for "cannot measure"
    compares as a correlation, and `tracks` would then read an unmeasurable fixed
    hypothesis as proof that the sound follows the keyboard."""
    n = min(len(a), len(b))
    if n < 8:
        return None
    a, b = a[:n] - a[:n].mean(), b[:n] - b[:n].mean()
    den = float(np.sqrt((a ** 2).sum() * (b ** 2).sum()))
    return float((a * b).sum() / den) if den > 0 else None


def tracks(y_low, y_high, f_low, f_high, cents_per_bin=20.0):
    """M2's real meter — did the spectrum come with the keyboard, or stay put?

    Two hypotheses, each scored once over the same band, no search:

      `r_note`   the two spectra agree when each is read FROM its own played note
                 — the sound follows the keyboard;
      `r_fixed`  they agree when both are read on one absolute frequency axis
                 — the sound has its own register and ignores the keyboard.

    `verdict` is whichever fits better by more than 0.1, else "mixed" — the honest
    answer for something like a fixed formant set over a tracking glottal source,
    where both are partly true.

    Two hypotheses rather than a lag search, because the lag search was tried and
    is not safe here: a short overlap at an absurd shift can score a perfect 1.0
    when one aligned spike dominates it. Measured — a `saw` whose fundamental
    landed on the 9th harmonic of the octave-up render scored r=1.0 at -3800
    cents, i.e. the meter called the plainest tracking oscillator in the library
    broken. Both correlations here run over the full band, so nothing can win by
    being narrow.

    And it is these two rather than the two obvious meters because both of those
    give confident wrong answers on exactly the entries that need judging:
      - **`f0`** locks onto the slow beat of a Q=900 inharmonic bank and reports
        37.8 Hz for a played 110 (`struck_bar`), and returns nothing at all for an
        unpitched membrane.
      - **the spectral centroid** SATURATES on any band-limited source: `vco2`
        band-limits to Nyquist, so a saw has 400 harmonics at 55 Hz and 12 at
        1760, and its centroid rises far less than proportionally — measured slope
        0.675 against a keyboard it follows to the cent.
    """
    lo_c, hi_c = -1500.0, 4200.0
    _, A = logspec_rel(y_low, f_low, lo_c, hi_c, cents_per_bin)
    _, B = logspec_rel(y_high, f_high, lo_c, hi_c, cents_per_bin)
    r_note = _corr(A, B)
    # The same two renders on ONE absolute axis, over the band the low note spans.
    lo_hz = f_low * 2 ** (lo_c / 1200.0)
    span_c = hi_c - lo_c
    _, Aa = logspec_rel(y_low, lo_hz, 0.0, span_c, cents_per_bin)
    _, Ba = logspec_rel(y_high, lo_hz, 0.0, span_c, cents_per_bin)
    r_fixed = _corr(Aa, Ba)
    if r_note is None or r_fixed is None:
        verdict = "not measurable"
    else:
        verdict = ("tracks" if r_note > r_fixed + 0.1 else
                   "fixed register" if r_fixed > r_note + 0.1 else "mixed")
    return {"asked_cents": round(float(1200 * np.log2(f_high / f_low)), 1),
            "r_note": None if r_note is None else round(r_note, 3),
            "r_fixed": None if r_fixed is None else round(r_fixed, 3),
            "verdict": verdict}


def travel(y, n=8):
    """Colour over the course of the note: the centroid of n successive windows.

    Movement by default is a platform fundamental, and a fundamental that is a
    requirement but not a measurement is a regression waiting to ship: a corpus of
    standing tones passes an implementation that cannot move at all. This is the
    objective signature — where the colour goes, and how far — so "it moves" stops
    being a matter of what somebody remembers hearing.

    The window length picks which RATE of motion is visible, and reading only one
    of them will call a moving instrument still: `pwm`'s duty sweep reads 48 Hz of
    travel in 0.5 s windows (n=8, a full LFO cycle per window, averaged flat) and
    696 Hz in 31 ms ones (n=128). A standing sine reads 0 at every window length,
    which is what makes the fast reading evidence rather than noise. `measure()`
    therefore reports both.

    Tracked on `centroid_audible`, not `centroid`: this is a claim about whether the
    sound moves, and amplitude weighting on a linear axis let an inaudible partial
    carry that claim. The span this returns is therefore NOT comparable with the
    descriptive centroid figures in the library's notes.
    """
    n = max(2, n)
    edges = np.linspace(0, len(y) / SR, n + 1)
    cs = [centroid_audible(y, a, b) for a, b in zip(edges[:-1], edges[1:])]
    return cs, (max(cs) - min(cs))


def coherence(y, n=128):
    """How much of that travel is MOVEMENT and how much is just noise variance.

    The span `travel` returns cannot tell the two apart, and on this library the
    difference decides a platform fundamental. A stochastic source's centroid
    wanders from window to window all by itself: measured, plain `rand` with
    nothing modulating it reads 830 Hz of travel at n=128, more than most of the
    genuinely moving entries, while a standing sine reads 0.4. So a movement test
    built on the span alone passes every noise bed in the library and would certify
    "movement by default" on a body that provably cannot move.

    What separates them is the SHAPE of the centroid track, not its range: a track
    driven by an LFO or an envelope has its energy in a FEW rates, a track that is
    only variance spreads it over all of them. So this is one minus the spectral
    flatness of the track — 1 for a track that moves at some definite rate, 0 for
    white wandering, and free of any assumption about WHICH rate.

    It was the lag-1 autocorrelation, which is the same idea done cheaply, and the
    cheapness cost real answers. Lag-1 on an n=128 track over 4 s samples the track
    at 32 Hz, and the autocorrelation of a component at rate f is cos(2*pi*f/32) —
    which is exactly ZERO at 8 Hz and NEGATIVE above it. A wineglass whose two
    quadrupole modes beat 8 Hz apart therefore read as having no movement at all,
    in 6 of its 27 parameter corners, while beating audibly. Flatness has no such
    null: a peak anywhere in the track spectrum counts.
    """
    cs, _ = travel(y, n)
    a = np.asarray(cs, dtype=float)
    span = float(a.max() - a.min()) if len(a) else 0.0
    a = a - a.mean()
    if len(a) < 8 or span < 1.0:
        # Under a hertz of travel there is no shape to characterise, and the
        # flatness of a float noise floor is a number about nothing. A standing
        # sine read 0.98 through this before the guard was absolute.
        return 0.0
    P = np.abs(np.fft.rfft(a * np.hanning(len(a)))) ** 2
    P = P[1:]                       # drop DC: the mean is already removed
    P = P[P > 0]
    if P.size < 4:
        return 0.0
    flat = float(np.exp(np.log(P).mean()) / P.mean())
    # A single periodogram of white noise does NOT read flatness 1: its bins are
    # exponentially distributed, whose geometric mean is exp(-gamma) = 0.5615 of
    # their arithmetic mean. Uncorrected, genuinely static noise measured 0.64 and
    # would have passed as movement. Dividing by that expectation puts white at 0
    # and a single-rate modulation at 1, which is what the number claims to mean.
    return float(max(0.0, min(1.0, 1.0 - flat / 0.5615)))


def odd_even_db(y, freq, n=16):
    """Odd-harmonic energy over even-harmonic energy, in dB.

    Two things in this library need this and nothing else can see either.

    A **bore's shape**: a cylinder passes only the odd harmonics, a cone passes
    all of them. That is the objective difference between a clarinet and a
    saxophone, and it is enormous — measured on this build, `wgclar` reads
    +44.3 dB and the library's `clarinet` +115 dB, against `sax` +4.1 dB and
    `brass` +3.2 dB. No centroid or comb reading separates them at all.

    A **pulse wave's duty cycle**: the harmonic amplitudes go as |sin(pi*k*d)|/k,
    so at d = 0.5 every even harmonic vanishes and the ratio is huge, and it
    collapses as the duty narrows. Measured on `vco2`'s own `kpw`: -126.6 dB at
    50 % duty, -5.5 dB at 35 %, -1.1 dB at 10 %. This is what makes PWM
    objectively testable — the centroid barely moves under a duty sweep (805 Hz,
    non-monotonic), which is why a duty axis judged by colour alone reads as
    inert and gets thrown away.
    """
    lin = [10 ** (v / 20.0) for v in partials(y, freq, n=n)]
    odd = sum(lin[0::2])   # partials() starts at the fundamental = harmonic 1
    even = sum(lin[1::2])
    return float(20 * np.log10(max(odd, 1e-12) / max(even, 1e-12)))


def beat(y, lo_hz=0.5, hi_hz=25.0, t0=0.5, t1=3.5, at_hz=None, f0_hz=None):
    """The slow amplitude beat: how deep it is, and how fast.

    Two sources a few cents apart beat, and that beat is the whole substance of a
    musette accordion, a bagpipe drone and a detuned analogue pad. No spectral
    meter can see it — the spectrum is the same whether the partials beat or not,
    so `centroid`, `comb_contrast` and `travel` all read a musette axis as doing
    almost nothing (measured: `bagpipe`'s detune moved the centroid by 1 Hz over
    its whole range while the beat rate went from 1.0 to 4.0 Hz). Judging such an
    axis by colour alone concludes it is inert and invites deleting it.

    Read off the rectified signal at the FULL sample rate, as the largest spectral
    component in the beat band relative to the mean level, so `depth` comes out on
    the same scale as an asked-for AM depth. Calibrated against exactly that:
    asked 0.00 / 0.10 / 0.30 / 0.60 read 0.000 / 0.100 / 0.299 / 0.599.

    The full rate is not an optimisation, it is the correctness of the whole meter.
    Rectifying a tone at f0 puts a ripple at 2*f0 (at f0 itself, for an asymmetric
    waveform) into the envelope. This used to be block-averaged to 200.455 Hz, which
    ALIASES that ripple straight into the beat band, and the block average is a
    boxcar whose rejection at 110 Hz is only 0.574 — nowhere near enough to stop it.
    Measured on plain unmodulated sines, with nothing whatever modulating them:
    55 Hz read a beat of depth 0.372 at 90.39 Hz (the 110 Hz ripple folded about
    200.455) and 110 Hz read 0.052 at 19.68 Hz (220 folded to 19.55). The second one
    landed inside the NARROW 25 Hz band too and above the 0.05 that the audit's M6
    treats as a real rate control, so no choice of band was ever going to fix it.
    At the full rate nothing folds: the ripple stays at 2*f0 where it belongs.

    `f0_hz` closes the other half. A component at or above the fundamental is not a
    beat — it is a sideband, and the spectral meters already see it — so the band is
    capped at half the fundamental when the caller knows it. Unmodulated sines then
    read 0.000 at every register instead of 0.372, and an asked 0.30 at 90 Hz reads
    0.299 rather than the 0.200 the boxcar's roll-off left of it.

    `at_hz` asks about ONE rate instead of the strongest, because the strongest hides
    the others. A rubbed wineglass has two independent amplitude motions at once —
    the two quadrupole modes beating, and the finger passing four antinodes a lap —
    and searching for the maximum reported only the beat, at a depth that did not
    budge when the pulsation axis was swept from 0 to 1. The instrument then reads
    as inert on exactly the axis it is named for. Calibrated on a sum of two AMs
    where one is deliberately the louder: asked 0.30 at 3 Hz alongside 0.45 at
    9 Hz, `at_hz=3` reads 0.299 while the plain call reports the 9 Hz line.

    It has one limit, and it is not small: a STRONGER line within about a hertz
    leaks in and is reported instead. Measured, a 0.45-deep line at a distance of
    0.4 / 0.8 / 1.2 / 1.6 Hz makes `at_hz=4.6` read 0.400 / 0.350 / 0.021 / 0.000 —
    so the answer is trustworthy from roughly 1.2 Hz of separation onward and is
    simply the neighbour below that. This is the window's own resolution and no
    choice of band fixes it; 3 s at 200 Hz gives 0.33 Hz bins and a Hann main lobe
    two bins wide. The glass harp is exactly the case that found it: asking about
    its 4.6 Hz pulsation returned 0.23 at the pulsation depth of ZERO, because its
    3.4 Hz beat sits one bin below the band edge. If two rates are that close, they
    are one measurement, and the axis must be judged by colour instead.

    The resolution is set by the window in SECONDS and not by the envelope rate, so
    it is unchanged by reading at the full rate: 3 s gives 0.33 Hz bins either way.
    """
    s = y[int(t0 * SR):int(t1 * SR)]
    if len(s) < SR // 4:
        return 0.0, 0.0
    mean = float(np.abs(s).mean())
    env = np.abs(s) - mean
    F = np.abs(np.fft.rfft(env * np.hanning(len(env))))
    fq = np.fft.rfftfreq(len(env), 1 / SR)
    if at_hz is not None:
        # one rate, widened by the window's own resolution so a slightly-off rate
        # is still found: a Hann window spreads a line over ~2 bins each side.
        w = 2.5 * SR / len(env)
        band = (fq >= at_hz - w) & (fq <= at_hz + w)
        if not band.any() or mean <= 0:
            return 0.0, 0.0
        amp = 2 * float(F[band].max()) / (len(env) / 2)
        return amp / mean, float(at_hz)
    if f0_hz:
        hi_hz = min(hi_hz, 0.5 * f0_hz)
    band = (fq >= lo_hz) & (fq <= hi_hz)
    if not band.any() or mean <= 0:
        return 0.0, 0.0
    amp = 2 * float(F[band].max()) / (len(env) / 2)
    return amp / mean, float(fq[band][np.argmax(F[band])])


def moves(y, n=128, span_cents=60.0, min_coherence=0.35):
    """Does the colour actually travel over the note? Span AND shape must agree.

    The threshold is calibrated on the shipped library rather than chosen, and the
    calibration also says what this test CANNOT do. Measured over all 50 entries at
    4 s and n=128:

      unpitched beds            0.00 - 0.18   (thunder, rain, crackle .00, hiss .06,
                                               noise .07, drum_head .13, cymbal .18)
      a deliberate modulation   0.51 - 1.00   (rhodes .51, wind .53, struck_bar .68,
                                               free_reed .83, harmonica .99, and 23
                                               entries at 1.00)

    and the null distribution over 60 renders of unmodulated noise, both bare and
    filtered, is mean 0.030, sd 0.045, MAX 0.188. Nothing whatever falls between 0.18
    and 0.51, so 0.35 sits in the middle of an empty band at nearly twice the largest
    false reading noise alone produced, which is why it is the threshold.
    But the band 0.20-0.86 is not empty in general: it holds three classes this
    meter cannot resolve at 4 s, and a reading in it means "not demonstrated in four
    seconds", never "static".

      * movement slower than the window. `wind` gusts at 0.071 Hz and reads 0.34;
        `surf` swells at 0.055 Hz and reads 0.57. Both move audibly. A quarter of a
        cycle cannot be told from a trend.
      * low-Q noise-driven mode banks, which ARE mostly filtered noise: `drum_head`
        (Q 7-20) reads 0.00, `cymbal` 0.24, against `glass` (Q 1400-2000) at 0.73.
      * bodies that decay to near silence inside the window, where the late
        centroid is a noise floor: `rhodes` 0.00, `wurlitzer` 0.37.
    """
    cs, span = travel(y, n)
    r1 = coherence(y, n)
    # The span is judged in CENTS, not hertz. An absolute hertz bound is the same
    # class of mistake as the amplitude weighting was: 8 Hz of travel is nothing on
    # `hiss`'s 13.5 kHz centroid and a plainly audible wobble on `sub_sine`'s 205 Hz.
    # Measured in cents the two populations separate more sharply than in hertz — the
    # eight bodies with nothing to move read 0 to 54 cents (sine 0, triangle 1,
    # square 3, bass_saw 7, the two fixed vowels 21, pulse 25, sub_sine 54) and
    # everything else reads 78 and up — and 60 cents fails exactly the same eight the
    # old 8 Hz bound did, so the change is verdict-neutral on the library and gives
    # the number a meaning. 60 cents of brightness is a bit over a quarter tone.
    mean = float(np.mean(cs)) if len(cs) else 0.0
    span_c = 1200 * np.log2(1 + span / mean) if mean > 1e-9 else 0.0
    return (bool(span_c > span_cents and r1 > min_coherence),
            round(span, 1), round(r1, 3))


def measure(y, asked_freq):
    f = f0(y)
    cs, span = travel(y, 8)
    _, fast = travel(y, 128)
    does_move, _, r1 = moves(y)
    # The beat is reported because a whole class of axis is a RATE and no spectral
    # meter can see one: a bagpipe's detune, a cricket's chirp rate and a frog's pulse
    # rate all move the centroid by single hertz while changing the sound completely.
    # An audit that reads only colour calls such an axis inert and invites deleting it.
    # The band reaches 120 Hz because these rates go far above a musette's few hertz,
    # and is capped at half the fundamental so the carrier's own rectification ripple
    # cannot be read back as a beat — see `beat`, which read 0.372 on a plain sine
    # before the cap and the full-rate envelope went in.
    bd, br = beat(y, lo_hz=0.5, hi_hz=120.0, f0_hz=asked_freq)
    return {"f0": None if f is None else round(f, 2),
            "cents": None if f is None else round(cents(f, asked_freq), 1),
            "centroid": round(centroid(y), 1),
            "centroid_travel_hz": round(span, 1),
            "centroid_motion_hz": round(fast, 1),
            # The span alone calls static noise the most mobile thing here.
            "motion_coherence": r1,
            "moves": does_move,
            "beat_depth": round(bd, 3),
            "beat_rate_hz": round(br, 2),
            "centroid_over_note": [round(c) for c in cs],
            "rms_db": round(rms_db(y), 2),
            "peak_p999": round(peak_p999(y), 3),
            "sustain": round(sustain(y), 3),
            "comb_db": round(comb_contrast(y, asked_freq), 1),
            "partials": partials(y, asked_freq)}


# ─────────────────────────────────────────────────────────────────────────────
# Calibration — the meter measured against known answers
# ─────────────────────────────────────────────────────────────────────────────

_SINE = "asig    poscil 0.5, kfreq * koct1, giSine"
_SAW = "asig    vco2 0.5, kfreq * koct1, 0"
# gbuzz normalises to PEAK, not RMS; the closed-form hold is what keeps a
# harmonic stack at one loudness while its harmonic count moves.
_BUZZ = """knh     = 12
kmul    = 0.7
knrm    = ((1-kmul^knh)/(1-kmul)) / sqrt((1-kmul^(2*knh))/(1-kmul*kmul)*0.5)
asig    gbuzz 0.17 * knrm, kfreq * koct1, knh, 1, kmul, giCos"""
# No energy AT the fundamental: a pitch meter that follows the loudest partial
# answers 440 here, a period-based one answers 220.
_MISSING = """a2      poscil 0.3, kfreq * koct1 * 2, giSine
a3      poscil 0.3, kfreq * koct1 * 3, giSine
asig    = a2 + a3"""
# The 31-bit generator (isel=1), which is what every body in the library uses. The
# default 16-bit one repeats every 1.365 s, and that is a real 0.73 Hz periodicity:
# `coherence` reads it as 0.358 against 0.060 for this form, correctly, since the
# signal genuinely does repeat. Calling that "static noise" in a calibration made
# the meter look wrong when it was the test signal that was moving.
_NOISE = "asig    rand 0.4, 0.5, 1"


def _comb(fb):
    # A resonator's output grows roughly as 1/(1-fb), so a fixed exciter level
    # clips the high-feedback case — and a clipped render flattens the very comb
    # this is here to measure. Scaling the drive with the feedback keeps all three
    # calibration points inside the meter's range.
    return f"""aex     rand {0.35 * (1.0 - fb):.5f}
asig    streson aex, kfreq * koct1, {fb}"""


# How many calibration cases there are. Asserted at the end so a group that stops
# running — an early `return`, a render failure that `continue`s past its check, a
# refactor that drops a block — is a FAILURE and not a quietly shorter green run.
_CASES = 45


def selftest():
    """Known signals in, known answers out. Any failure invalidates every number
    this file has ever reported.

    The tally is a LIST and not a bool on purpose. It was a bool named `ok`, and one
    of the movement cases below unpacked its own result into the same name —
    `ok, sp, r1 = moves(yr)` — so the verdict reported whether the LAST rate case
    passed and erased 17 of the 40 others. `--selftest` then printed "meter
    calibrated" and exited 0 with a real failure on screen every single run, which is
    how the looping-noise case shipped. A list cannot be silently rebound by an
    unpacking assignment, and the count is asserted at the end.
    """
    fails, total = [], []

    def check(name, cond, detail):
        total.append(name)
        if not cond:
            fails.append(name)
        print(f"  {'PASS' if cond else 'FAIL'}  {name}: {detail}")

    print("pitch (asked 220 Hz, tolerance +-8 cents)")
    for name, body in (("sine", _SINE), ("saw", _SAW), ("gbuzz", _BUZZ),
                       ("missing fundamental", _MISSING),
                       ("comb-filtered noise", _comb(0.995))):
        y, err = render(body, freq=220.0)
        if err:
            check(name, False, err)
            continue
        c = cents(f0(y), 220.0)
        check(name, c is not None and abs(c) < 8,
              "none" if c is None else f"{c:+.1f} cents")

    print("no pitch where there is none")
    y, err = render(_NOISE)
    check("white noise", err is None and f0(y) is None,
          err or ("None" if f0(y) is None else f"{f0(y):.1f} Hz — fabricated"))

    print("render guards")
    y, err = render("asig    = 0")
    check("silence is an error", err == "SILENT", err)
    y, err = render("asig    poscil 4.0, kfreq * koct1, giSine")
    check("a body the host would clip is flagged", (err or "").startswith("HOT"), err)
    y, err = render("asig    frobnicate 1, 2")
    check("a broken body is an error", (err or "").startswith("csound:"),
          (err or "no error")[:80])

    print("colour orders as it must")
    ys, _ = render(_SINE)
    yw, _ = render(_SAW)
    check("sine is darker than saw", centroid(ys) < centroid(yw),
          f"{centroid(ys):.0f} < {centroid(yw):.0f} Hz")

    print("movement is visible, and stillness is not mistaken for it")
    ymov, emov = render("""asaw    vco2 0.5, kfreq * koct1, 0
kcut    expon 300, 4.0, 8000
asig    tone asaw, kcut""")
    if emov:
        check("a sweeping body renders", False, emov)
    else:
        _, span_mov = travel(ymov)
        _, span_still = travel(ys)
        # 267 Hz, not the 2274 the amplitude-weighted centroid read on the same
        # signal. A saw's POWER sits at its fundamental, so opening a one-pole
        # lowpass from 300 Hz to 8 kHz is a large audible change that moves the power
        # centroid only modestly — power weighting is conservative here, which is the
        # price of its not being fooled by inaudible energy. What is asserted is the
        # discrimination against a standing sine's 0.00 Hz, plus the strong case
        # below where the swept band IS the energy.
        check("a sweep travels", span_mov > 200, f"{span_mov:.0f} Hz")
        check("a standing sine does not", span_still < 20, f"{span_still:.0f} Hz")

    # The trap the span alone walks into: an unmodulated noise source. Its centroid
    # wanders more than most moving instruments do, so any test that reads only the
    # span certifies movement on a body that has none. Both readings are asserted
    # here, on the same signal, because it is their DISAGREEMENT that is the meter.
    print("movement is told apart from noise variance")
    yn2, en2 = render(_NOISE)
    ylfo, elfo = render("""anz     rand 0.4
kcut    poscil 1500, 0.7
asig    reson anz, 2200 + kcut, 900, 2""")
    if en2 or elfo:
        check("the movement calibration renders", False, en2 or elfo)
    else:
        m_n, sp_n, r_n = moves(yn2)
        m_l, sp_l, r_l = moves(ylfo)
        m_s, sp_s, r_s = moves(ys)
        m_m, sp_m, r_m = moves(ymov)
        check("static noise travels but does NOT move", not m_n,
              f"span {sp_n:.0f} Hz, coherence {r_n:+.2f}")
        check("the same noise under an LFO does move", m_l,
              f"span {sp_l:.0f} Hz, coherence {r_l:+.2f}")
        check("a swept saw moves", m_m, f"span {sp_m:.0f} Hz, coherence {r_m:+.2f}")
        check("a standing sine does not move", not m_s,
              f"span {sp_s:.0f} Hz, coherence {r_s:+.2f}")
    # Movement at every RATE, because the meter this replaced had a null at 8 Hz
    # (a quarter of the 32 Hz centroid-track rate) and read a beating wineglass as
    # standing still. Any single-lag statistic has that null; these three cases
    # bracket it, and a modulation rate is the last thing a candidate should have
    # to avoid.
    for rate in (0.7, 4.0, 8.0, 13.0):
        yr, er = render(f"""anz     rand 0.4
kcut    poscil 1500, {rate}
asig    reson anz, 2200 + kcut, 900, 2""")
        if er:
            check(f"movement at {rate} Hz", False, er)
            continue
        seen, sp, r1 = moves(yr)
        check(f"movement at {rate} Hz is seen", seen,
              f"span {sp:.0f} Hz, coherence {r1:+.2f}")

    # The exploit this weighting exists to close: energy nobody can hear must not be
    # able to carry a claim about whether the sound moves.
    yin, ein = render("""kt      poscil 0.5, 0.5
a1      poscil 0.4, kfreq * koct1, giSine
a2      poscil 0.0004 * (1 + kt), 18000, giSine
asig    = a1 + a2""")
    if ein:
        check("an inaudible partial cannot carry the movement verdict", False, ein)
    else:
        m_i, sp_i, r_i = moves(yin)
        check("an inaudible partial cannot carry the movement verdict", not m_i,
              f"a standing sine plus a -60 dB 18 kHz tremolo: span {sp_i:.1f} Hz, "
              f"coherence {r_i:+.2f} (amplitude-weighted it was 18.7 Hz at +1.00)")

    yrs, ers = render("""anz     rand 0.4, 0.5, 1
kcf     expon 400, 4.0, 6000
asig    reson anz, kcf, kcf * 0.3, 2""")
    if ers:
        check("a swept band on noise travels", False, ers)
    else:
        _, sp_rs = travel(yrs)
        check("a swept band on noise travels far", sp_rs > 3000, f"{sp_rs:.0f} Hz")

    print("tracking sees what follows the keyboard and what does not")
    _FIXED = "asig    poscil 0.4, 800, giSine"          # a fixed register: ignores kfreq
    _BANK = """aex     rand 0.06, 0.5, 1
a1      mode aex, kfreq * koct1 * 1.0, 40
a2      mode aex, kfreq * koct1 * 2.14, 36
a3      mode aex, kfreq * koct1 * 3.77, 32
asig    = (a1 + a2 + a3) * 0.5"""
    # A VIBRATOED saw, because every analogue-flavoured entry in the library has a
    # slow detune on it and the un-vibratoed cases above cannot see the trap it
    # sets: the smear moves the partial off the grid point. Shipped `saw` read
    # r_note 0.149 against a keyboard it follows to the cent.
    _VIB = """kvib    poscil 0.0025, 4.7
asig    vco2 0.5, kfreq * koct1 * (1 + kvib), 0"""
    # An octave below the played note — the whole reason the band reaches to -1500.
    _SUB = "asig    poscil 0.5, kfreq * koct1 * 0.5, giSine"
    for name, body, want_tracking in (("saw (band-limited)", _SAW, True),
                                      ("saw with vibrato", _VIB, True),
                                      ("a sub-octave sine", _SUB, True),
                                      ("sine", _SINE, True),
                                      ("inharmonic modal bank", _BANK, True),
                                      ("a fixed 800 Hz register", _FIXED, False)):
        ylo, elo = render(body, freq=110.0)
        yhi, ehi = render(body, freq=880.0)
        if elo or ehi:
            check(name, False, elo or ehi)
            continue
        s = tracks(ylo, yhi, 110.0, 880.0)
        check(name, (s["verdict"] == "tracks") == want_tracking,
              f"{s['verdict']} (r_note {s['r_note']} vs r_fixed {s['r_fixed']})")

    print("odd/even separates a cylinder from a cone, and reads a duty cycle")
    # |sin(pi*k*d)|/k: at d=0.5 every even harmonic is a zero of the sine.
    for duty, lo, hi in ((0.5, 40.0, 200.0), (0.35, 2.0, 12.0), (0.10, -3.0, 3.0)):
        yd, ed = render(f"asig    vco2 0.5, kfreq * koct1, 2, {duty}")
        if ed:
            check(f"duty {duty}", False, ed)
            continue
        v = odd_even_db(yd, 220.0)
        check(f"pulse at {duty:.0%} duty", lo <= v <= hi, f"{v:+.1f} dB odd/even")
    ysq, _e1 = render("asig    poscil 0.5, kfreq * koct1, giSine")
    check("a sine has no even harmonics to speak of", odd_even_db(ysq, 220.0) > 40,
          f"{odd_even_db(ysq, 220.0):+.1f} dB")

    print("the beat meter reads back the depth it was given")
    for asked in (0.0, 0.1, 0.3, 0.6):
        yb, eb = render(f"""kam     poscil {asked / 2:.4f}, 4.0
asig    poscil 0.4 * (1 + 2 * kam), kfreq * koct1, giSine""")
        if eb:
            check(f"AM depth {asked}", False, eb)
            continue
        d, r = beat(yb)
        check(f"AM depth {asked}", abs(d - asked) < 0.02 and (asked == 0 or abs(r - 4) < 0.4),
              f"read {d:.3f} at {r:.2f} Hz")
    ybt, ebt = render("""a1      poscil 0.3, kfreq * koct1, giSine
a2      poscil 0.3, kfreq * koct1 * 1.0136, giSine
asig    = a1 + a2""")
    if ebt:
        check("two detuned sines beat", False, ebt)
    else:
        d, r = beat(ybt)
        # 1.36 % of 220 Hz is 2.99 Hz; the beat is at the difference frequency.
        check("two detuned sines beat at their difference", abs(r - 2.99) < 0.3 and d > 0.5,
              f"depth {d:.2f} at {r:.2f} Hz (asked 2.99)")
    # Two amplitude motions at once, the WEAKER one being the question. Searching
    # for the maximum answers about the loud one and says nothing about the other,
    # which is how a two-motion instrument reads as inert on one of its own axes.
    ytm, etm = render("""ka      poscil 0.150, 3.0
kb      poscil 0.225, 9.0
asig    poscil 0.4 * (1 + 2 * ka + 2 * kb), kfreq * koct1, giSine""")
    if etm:
        check("two beats at once", False, etm)
    else:
        dmax, rmax = beat(ytm)
        d3, _ = beat(ytm, at_hz=3.0)
        d9, _ = beat(ytm, at_hz=9.0)
        check("the strongest of two beats is the one reported by default",
              abs(rmax - 9.0) < 0.4 and abs(dmax - 0.45) < 0.03,
              f"read {dmax:.3f} at {rmax:.2f} Hz (asked 0.45 at 9 Hz)")
        check("at_hz finds the quieter beat the maximum hides",
              abs(d3 - 0.30) < 0.03 and abs(d9 - 0.45) < 0.03,
              f"3 Hz reads {d3:.3f} (asked 0.30), 9 Hz reads {d9:.3f} (asked 0.45)")
    # …and the limit of that, asserted rather than left to be rediscovered: a
    # stronger line about a hertz away IS the answer at 4 s.
    near, far = [], []
    for d, into in ((0.8, near), (1.6, far)):
        yl, el = render(f"""ka      poscil 0.225, {4.6 - d:.3f}
asig    poscil 0.4 * (1 + 2 * ka), kfreq * koct1, giSine""")
        into.append(None if el else beat(yl, at_hz=4.6)[0])
    if near[0] is None or far[0] is None:
        check("at_hz leakage", False, "render failed")
    else:
        check("at_hz reports a stronger neighbour under ~1 Hz away, and not beyond",
              near[0] > 0.25 and far[0] < 0.05,
              f"0.8 Hz away reads {near[0]:.3f}, 1.6 Hz away reads {far[0]:.3f} "
              f"(nothing is modulating 4.6 Hz in either)")
    # The case the meter got WRONG, and the reason it is asserted here rather than
    # trusted: nothing is modulating these, so every register must read zero. On a
    # 200 Hz block-averaged envelope 55 Hz read 0.372 at 90.39 Hz and 110 Hz read
    # 0.052 at 19.68 Hz — the rectification ripple at 2*f0, folded about the envelope
    # rate. M6 calls 0.05 a real rate control, so this fabricated a rate axis for
    # every entry. Read through `measure`, which is the path the audit uses.
    flat = []
    for f in (55.0, 82.5, 110.0, 220.0, 440.0, 880.0, 1760.0):
        yf, ef = render("asig    poscil 0.3, kfreq", freq=f)
        flat.append((f, None if ef else measure(yf, f)["beat_depth"]))
    worst = max((v, f) for f, v in flat if v is not None)
    check("an unmodulated tone has no beat at any register", worst[0] < 0.02,
          "worst %.3f at %.1f Hz (%s)"
          % (worst[0], worst[1],
             " ".join("%.0f:%.3f" % (f, v) for f, v in flat if v is not None)))
    # …and a genuine fast beat is still read at its true depth up there, which the
    # boxcar's roll-off used to cost: an asked 0.30 at 90 Hz came back as 0.200.
    yfast, efast = render("""kam     poscil 0.15, 90.0
asig    poscil 0.4 * (1 + 2 * kam), kfreq * koct1, giSine""", freq=880.0)
    if efast:
        check("a 90 Hz beat reads back its depth", False, efast)
    else:
        dfa, rfa = beat(yfast, lo_hz=0.5, hi_hz=120.0, f0_hz=880.0)
        check("a 90 Hz beat reads back its depth, well above the old envelope rate",
              abs(dfa - 0.30) < 0.03 and abs(rfa - 90.0) < 0.5,
              f"read {dfa:.3f} at {rfa:.2f} Hz (asked 0.30 at 90 Hz)")

    print("comb contrast tracks the resonator, not the exciter")
    (yn, en), (yh, eh), (yl, el) = (render(_NOISE), render(_comb(0.995)),
                                    render(_comb(0.575)))
    if en or eh or el:
        check("comb calibration renders cleanly", False, en or eh or el)
    else:
        cn, ch, cl = (comb_contrast(v, 220.0) for v in (yn, yh, yl))
        check("white noise is uncombed", abs(cn) < 2.0, f"{cn:+.1f} dB")
        check("high feedback combs hard", ch > 25, f"{ch:.1f} dB")
        check("low feedback combs less", 4 < cl < ch - 10, f"{cl:.1f} dB")

    # A calibration that silently stops running cases is as bad as one that fails:
    # the count is part of the verdict, not decoration.
    if len(total) < _CASES:
        fails.append(f"only {len(total)} of {_CASES} calibration cases ran")
        print(f"  FAIL  the calibration is incomplete: {len(total)}/{_CASES} cases")
    print(f"\n{len(total) - len(fails)}/{len(total)} cases pass")
    if fails:
        print("METER IS WRONG — do not trust it. Failed: " + ", ".join(fails))
    else:
        print("meter calibrated")
    ok = not fails
    return 0 if ok else 1


# ─────────────────────────────────────────────────────────────────────────────

def _library_body(key, anchor):
    lib = json.loads((REPO / "backend" / "lco_library.json").read_text())
    for inst in lib["instruments"]:
        if inst["key"] != key:
            continue
        if anchor:
            # The keys carry their gloss ("bow=1.0  (bowed: drawn with a bow…)"),
            # so match on any substring of one: `--anchor bowed` is the useful form.
            variants = inst.get("anchor_code") or {}
            hits = [k for k in variants if anchor.lower() in k.lower()]
            if len(hits) != 1:
                raise SystemExit(f"{key}: {anchor!r} matches {len(hits)} anchors."
                                 + "".join("\n  " + k for k in sorted(variants)))
            return variants[hits[0]]
        return inst["code"]
    raise SystemExit(f"no instrument {key!r} in backend/lco_library.json")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true",
                    help="calibrate the meter on signals with known answers")
    ap.add_argument("--key", help="library instrument key to render")
    ap.add_argument("--anchor", help="anchor variant, e.g. bow=bowed")
    ap.add_argument("--body", help="path to a body to render instead")
    ap.add_argument("--freq", type=float, default=220.0)
    ap.add_argument("--dur", type=float, default=4.0)
    ap.add_argument("--glide", help="f0:f1 — sweep the note's pitch")
    ap.add_argument("--dump", action="store_true", help="print the CSD and stop")
    ap.add_argument("--wav", help="keep the render here")
    args = ap.parse_args()

    if args.selftest:
        return selftest()
    if not (args.key or args.body):
        ap.error("--selftest, --key or --body")

    body = (Path(args.body).read_text() if args.body
            else _library_body(args.key, args.anchor))
    glide = tuple(float(x) for x in args.glide.split(":")) if args.glide else None

    if args.dump:
        print(scaffold(body, args.dur, args.freq, glide))
        return 0

    y, err = render(body, args.dur, args.freq, glide, keep=args.wav)
    if y is None:
        print(f"{args.key or args.body}: {err}", file=sys.stderr)
        return 1
    out = measure(y, args.freq)
    if err:
        out["warning"] = err
    print(json.dumps(out, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
