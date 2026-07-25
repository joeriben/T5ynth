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
    S, fq = _mag(y, t0, t1)
    return float((S * fq).sum() / max(S.sum(), 1e-12))


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

    The band is bounded at both ends on purpose. Above ~+4200 cents (five
    harmonics past the fourth octave) a 20-cent grid can no longer RESOLVE
    adjacent harmonics — the spacing 1200*log2(1+1/h) falls under two bins around
    h=43 — so a saw's comb turns into a smooth mush that correlates with itself at
    any shift, which is what read a saw's octave as 1360 of 3600 cents. Below the
    fundamental there is nothing an oscillator should be putting energy into.
    """
    S, fq = _mag(y, t0, t1)
    grid = np.arange(lo_c, hi_c, cents_per_bin)
    return grid, np.interp(freq * 2 ** (grid / 1200.0), fq, S)


def _corr(a, b):
    n = min(len(a), len(b))
    if n < 8:
        return -2.0
    a, b = a[:n] - a[:n].mean(), b[:n] - b[:n].mean()
    den = float(np.sqrt((a ** 2).sum() * (b ** 2).sum()))
    return float((a * b).sum() / den) if den > 0 else -2.0


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
    lo_c, hi_c = -600.0, 4200.0
    _, A = logspec_rel(y_low, f_low, lo_c, hi_c, cents_per_bin)
    _, B = logspec_rel(y_high, f_high, lo_c, hi_c, cents_per_bin)
    r_note = _corr(A, B)
    # The same two renders on ONE absolute axis, over the band the low note spans.
    lo_hz = f_low * 2 ** (lo_c / 1200.0)
    span_c = hi_c - lo_c
    _, Aa = logspec_rel(y_low, lo_hz, 0.0, span_c, cents_per_bin)
    _, Ba = logspec_rel(y_high, lo_hz, 0.0, span_c, cents_per_bin)
    r_fixed = _corr(Aa, Ba)
    verdict = ("tracks" if r_note > r_fixed + 0.1 else
               "fixed register" if r_fixed > r_note + 0.1 else "mixed")
    return {"asked_cents": round(float(1200 * np.log2(f_high / f_low)), 1),
            "r_note": round(r_note, 3), "r_fixed": round(r_fixed, 3),
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
    """
    n = max(2, n)
    edges = np.linspace(0, len(y) / SR, n + 1)
    cs = [centroid(y, a, b) for a, b in zip(edges[:-1], edges[1:])]
    return cs, (max(cs) - min(cs))


def measure(y, asked_freq):
    f = f0(y)
    cs, span = travel(y, 8)
    _, fast = travel(y, 128)
    return {"f0": None if f is None else round(f, 2),
            "cents": None if f is None else round(cents(f, asked_freq), 1),
            "centroid": round(centroid(y), 1),
            "centroid_travel_hz": round(span, 1),
            "centroid_motion_hz": round(fast, 1),
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
_NOISE = "asig    rand 0.4"


def _comb(fb):
    # A resonator's output grows roughly as 1/(1-fb), so a fixed exciter level
    # clips the high-feedback case — and a clipped render flattens the very comb
    # this is here to measure. Scaling the drive with the feedback keeps all three
    # calibration points inside the meter's range.
    return f"""aex     rand {0.35 * (1.0 - fb):.5f}
asig    streson aex, kfreq * koct1, {fb}"""


def selftest():
    """Known signals in, known answers out. Any failure invalidates every number
    this file has ever reported."""
    ok = True

    def check(name, cond, detail):
        nonlocal ok
        ok = ok and cond
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
        check("a sweep travels", span_mov > 800, f"{span_mov:.0f} Hz")
        check("a standing sine does not", span_still < 20, f"{span_still:.0f} Hz")

    print("tracking sees what follows the keyboard and what does not")
    _FIXED = "asig    poscil 0.4, 800, giSine"          # a fixed register: ignores kfreq
    _BANK = """aex     rand 0.06, 0.5, 1
a1      mode aex, kfreq * koct1 * 1.0, 40
a2      mode aex, kfreq * koct1 * 2.14, 36
a3      mode aex, kfreq * koct1 * 3.77, 32
asig    = (a1 + a2 + a3) * 0.5"""
    for name, body, want_tracking in (("saw (band-limited)", _SAW, True),
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

    print("\n" + ("meter calibrated" if ok else "METER IS WRONG — do not trust it"))
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
