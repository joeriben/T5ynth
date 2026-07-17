#!/usr/bin/env python3
"""Objective label-fidelity checks over tools/csound_lexicon_out/ renders.

Lesson from the SA3 aesthetic-axes study: never trust labels unmeasured.
Checks (48 kHz mono WAVs, rendered by csound_orch_check: gate 0-2 s, trig at 0 and 1 s):
  A  bright vs plain vs dark spectral-centroid ordering (bell + saw_stack)
  B  register stack: BJ double-bell (16'+8') shows energy an octave below the 8'-only bell
  C  inharmonicity: bell/metal/glass/cymbal partial peaks deviate from the integer grid;
     saw_stack/square_stack/organ_tone stay ON the grid
  D  standing-tone truth (2026-07-17 envelope removal, BJ: "Huellkurven gehoeren nicht in
     den Oszillator"): every solo family render HOLDS its level for as long as the gate is
     open -- late window (1.5-1.9 s) windowed RMS stays within a generous band of the early
     window (0.3-0.7 s), no attack/decay/retrigger shape anywhere in the orchestra any more.
     Motion renders are excluded on purpose -- they wobble by design (check E covers those).
  E  motion truth, PER-MOTION metric (2026-07-17 envelope removal fallout: the old
     uniform "0.5-12Hz amplitude-modulation depth vs plain pad" check turned out to be
     confounded by the removed retrigger/generic-envelope machinery, not a genuine
     per-motion measurement -- vibrato is pure PITCH movement with ~0 amplitude
     signature by design; breathe/evolve's own rates (0.2 Hz / 0.09 Hz, read straight
     from csound_lexicon.TOOLS) are slower than one full cycle fits in the 1.6 s
     measurement window, so a broadband depth reading over 0.5-12 Hz misses them
     almost entirely). Now each motion is tested for the thing it actually does:
       - vibrato: Hilbert-transform instantaneous-frequency std of the bandpassed
         fundamental >> the motionless plain pad's (near-zero) pitch jitter.
       - shimmer: unchanged broadband amplitude-modulation depth (0.5-12 Hz) -- the
         detuned-pair beat sits well inside that band at any register.
       - breathe/evolve: single-target-frequency envelope-modulation depth AT the
         motion's own known rate_hz (a Goertzel-style sin/cos correlation, valid even
         when the window covers less than one full LFO cycle) >> the plain pad's
         modulation at that same frequency.
  F  warm < harsh in high-frequency energy share (bell + saw_stack)
All thresholds are ordinal (X > Y), not absolute — robust against level matching.
Exit 0 only if every check passes; prints a table either way.
Regenerate the input renders with: .venv/bin/python -m backend.test_csound_lexicon
Run: .venv/bin/python tools/csound_lexicon_fidelity.py
"""
import struct, sys, math, os
import numpy as np

_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
_BACKEND_DIR = os.path.join(_REPO_ROOT, "backend")
if _BACKEND_DIR not in sys.path:
    sys.path.insert(0, _BACKEND_DIR)
import csound_lexicon as lex  # noqa: E402  (sys.path fixed up above)

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "csound_lexicon_out")
SR = 48000

def load(name):
    path = os.path.join(OUT, name + ".wav")
    with open(path, "rb") as f:
        data = f.read()
    # minimal RIFF parse: find 'data' chunk, assume 16-bit mono PCM (renderer convention)
    i = data.find(b"data")
    assert i > 0, name
    n = struct.unpack("<I", data[i+4:i+8])[0]
    raw = data[i+8:i+8+n]
    x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    return x

def centroid(x, lo=0, hi=None):
    seg = x[int(0.05*SR):int(1.9*SR)]
    w = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    f = np.fft.rfftfreq(len(seg), 1.0/SR)
    if hi: m = (f >= lo) & (f <= hi); w, f = w[m], f[m]
    # Noise gate: without it, the flat quantization/render floor across ~44k bins up to
    # 24 kHz dominates the weighting for quiet low-heavy sounds (measured: it INVERTED
    # the bright/dark bell ordering — bright=904 Hz vs dark=1236 Hz raw; gated 397 vs 286).
    w = np.where(w > w.max()*0.003, w, 0.0)
    return float((w*f).sum() / max(w.sum(), 1e-12))

def band_share(x, lo):
    seg = x[int(0.05*SR):int(1.9*SR)]
    w = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))**2
    f = np.fft.rfftfreq(len(seg), 1.0/SR)
    return float(w[f >= lo].sum() / max(w.sum(), 1e-12))

def rms(x, a, b):
    seg = x[int(a*SR):int(b*SR)]
    return float(np.sqrt(np.mean(seg**2) + 1e-18))

def peak_freqs(x, count=12, a=0.10, b=1.0, thresh=0.02):
    seg = x[int(a*SR):int(b*SR)]
    w = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
    f = np.fft.rfftfreq(len(seg), 1.0/SR)
    # local maxima above noise floor, below 8 kHz
    idx = [i for i in range(2, len(w)-2)
           if w[i] > w[i-1] and w[i] > w[i+1] and f[i] < 8000 and w[i] > w.max()*thresh]
    idx.sort(key=lambda i: -w[i])
    picked, freqs = [], []
    for i in idx:
        if all(abs(f[i]-g) > 25 for g in freqs):
            picked.append(i); freqs.append(f[i])
        if len(freqs) >= count: break
    return sorted(freqs)

def inharmonicity(x, a=0.10, b=1.0, thresh=0.02):
    """Mean relative deviation of partial peaks from the integer grid of the lowest peak.

    Window must sit where the sound still SOUNDS (fast-decaying families need an early
    window) — measured: glass in the default window found <4 peaks and silently read as
    perfectly harmonic. Too few peaks now reads NaN = check fails loudly, never passes."""
    fr = peak_freqs(x, a=a, b=b, thresh=thresh)
    if len(fr) < 4: return float("nan")
    f0 = fr[0]
    devs = []
    for f in fr[1:]:
        r = f / f0
        devs.append(abs(r - round(r)) / max(r, 1e-9))
    return float(np.mean(devs))

def slow_mod_depth(x):
    """Depth of 0.5-12 Hz envelope modulation in the sustained window (0.3-1.9 s)."""
    seg = x[int(0.3*SR):int(1.9*SR)]
    env = np.abs(seg)
    hop = 480
    e = np.array([env[i:i+hop].mean() for i in range(0, len(env)-hop, hop)])
    e = e / max(e.mean(), 1e-12)
    w = np.abs(np.fft.rfft(e - e.mean()))
    f = np.fft.rfftfreq(len(e), hop/SR)
    m = (f >= 0.5) & (f <= 12.0)
    return float(w[m].sum() / len(e))

def _envelope_track(x, a, b, hop=480):
    """Coarse (100 Hz, given hop=480@48kHz) amplitude-envelope trajectory over [a,b]."""
    seg = x[int(a*SR):int(b*SR)]
    env = np.abs(seg)
    e = np.array([env[i:i+hop].mean() for i in range(0, len(env)-hop, hop)])
    return e, hop / SR

def target_freq_mod_depth(x, target_hz, a=0.3, b=1.9):
    """Envelope-modulation depth AT one exact frequency (Goertzel-style sin/cos
    correlation of the envelope trajectory against that frequency), normalized by
    the envelope's own mean level. Unlike a broadband FFT-bin-range depth reading,
    this stays valid even when the [a,b] window covers well under one full LFO
    cycle (breathe's 0.2 Hz / evolve's 0.09 Hz vs. a 1.6 s window) -- exactly the
    "envelope removal" fallout that broke the old uniform 0.5-12 Hz check for
    these two motions (see the module docstring's check-E paragraph)."""
    e, dt = _envelope_track(x, a, b)
    t = np.arange(len(e)) * dt
    mean = max(e.mean(), 1e-12)
    d = e - e.mean()
    c = (d * np.cos(2*np.pi*target_hz*t)).sum()
    s = (d * np.sin(2*np.pi*target_hz*t)).sum()
    mag = 2.0 * math.sqrt(c*c + s*s) / len(e)
    return float(mag / mean)

def _hilbert(x):
    """Analytic signal via FFT (no scipy dependency)."""
    n = len(x)
    xf = np.fft.fft(x)
    h = np.zeros(n)
    if n % 2 == 0:
        h[0] = h[n // 2] = 1.0
        h[1:n // 2] = 2.0
    else:
        h[0] = 1.0
        h[1:(n + 1) // 2] = 2.0
    return np.fft.ifft(xf * h)

def pitch_jitter_hz(x, center_hz, a=0.3, b=1.9, halfwidth=20.0, trim_sec=0.05):
    """Std-dev of the INSTANTANEOUS frequency (Hilbert-transform phase derivative)
    of the signal bandpassed around center_hz -- a direct pitch-wobble measurement,
    the right way to see vibrato (pure frequency modulation, ~0 amplitude signature
    by design -- the old amplitude-only depth check could never have detected it
    honestly; see the module docstring's check-E paragraph)."""
    seg = x[int(a*SR):int(b*SR)]
    n = len(seg)
    xf = np.fft.rfft(seg)
    f = np.fft.rfftfreq(n, 1.0/SR)
    xf = np.where((f >= max(center_hz-halfwidth, 1.0)) & (f <= center_hz+halfwidth), xf, 0)
    filt = np.fft.irfft(xf, n=n)
    analytic = _hilbert(filt)
    phase = np.unwrap(np.angle(analytic))
    inst_hz = np.diff(phase) * SR / (2*np.pi)
    trim = int(trim_sec * SR)
    inst_hz = inst_hz[trim:-trim] if len(inst_hz) > 2*trim else inst_hz
    return float(np.std(inst_hz))

checks = []
def check(name, cond, detail):
    checks.append((name, bool(cond), detail))

# A: centroid ordering bright > plain > dark
for fam, plain in [("bell", "key_family_bell"), ("saw_stack", "key_family_saw_stack")]:
    # key_family_* have .orc only in some cases; use solo/char renders where wav exists
    pass
cb, cd = centroid(load("char_bright_bell")), centroid(load("char_dark_bell"))
check("A bell bright>dark centroid", cb > cd * 1.15, f"bright={cb:.0f}Hz dark={cd:.0f}Hz")
cbs, cds = centroid(load("char_bright_saw_stack")), centroid(load("char_dark_saw_stack"))
check("A saw bright>dark centroid", cbs > cds * 1.15, f"bright={cbs:.0f}Hz dark={cds:.0f}Hz")

# B: BJ stack has an octave-down fundamental vs the metallic 8' bell alone.
# The detail string must not index into a possibly-empty peak list — Python evaluates
# it eagerly, so a peakless (silent/regressed) render would crash the guard instead of
# producing the FAIL row it exists for.
bj = load("bj_dark_bell16_metallic_bell8"); mb = load("char_metallic_bell")
f_bj, f_mb = peak_freqs(bj, 6), peak_freqs(mb, 6)
detail_b = (f"lowest peak stack={f_bj[0]:.1f}Hz vs 8'-bell={f_mb[0]:.1f}Hz"
            if f_bj and f_mb else f"peakless render (stack={len(f_bj)} bell={len(f_mb)} peaks)")
check("B 16' layer adds octave-down energy",
      bool(f_bj and f_mb) and f_bj[0] < f_mb[0] * 0.65, detail_b)

# C: inharmonic vs harmonic families (glass decays fast → early window, lower peak gate)
inh_bell = inharmonicity(load("char_warm_bell"))
inh_glass = inharmonicity(load("solo_glass"), a=0.02, b=0.35, thresh=0.01)
harm_saw = inharmonicity(load("char_warm_saw_stack"))
check("C bell inharmonic > saw", inh_bell > harm_saw * 2 and inh_bell > 0.01,
      f"bell={inh_bell:.4f} saw={harm_saw:.4f}")
check("C glass inharmonic", inh_glass > 0.01, f"glass={inh_glass:.4f}")

# D: standing-tone truth -- every SOLO family render (no motion, no envelope: envelopes
# were removed entirely) holds its level while gated. A "generous band" (0.5x-2.0x) between
# a late window and an early window is a decay/swell guard, not a level-matching assertion --
# the additive partial tables/fm_bell's fixed index are static, but characters/MPE-epilogue
# interactions are real DSP, not silent no-ops, so exact equality is the wrong bar here.
for name in ("solo_pad", "solo_fm_bell", "solo_pluck", "solo_glass", "solo_noise_wash"):
    x = load(name)
    early, late = rms(x, 0.3, 0.7), rms(x, 1.5, 1.9)
    ratio = late / max(early, 1e-12)
    check(f"D {name} holds level (standing, no envelope)", 0.5 <= ratio <= 2.0,
          f"early={early:.4f} late={late:.4f} ratio={ratio:.3f}")

# E: motion — every motion render moves well above the MOTIONLESS plain pad, each
# tested for what it ACTUALLY does (see the module docstring's check-E paragraph for
# why a single uniform "0.5-12 Hz amplitude depth" reading stopped being honest once
# the retrigger/generic-envelope machinery was removed). Renders are 220 Hz, register
# "8'" (ratio 1.0) -> the pad's fundamental sits at 220 Hz (test_csound_lexicon.py's
# _run_render default), which is what the vibrato pitch-jitter probe is centered on.
_PLAIN_PAD = load("solo_pad")

# vibrato: pure pitch movement -- Hilbert instantaneous-frequency std of the
# bandpassed fundamental, not an amplitude metric at all.
jit_vib = pitch_jitter_hz(load("motion_vibrato_pad"), 220.0)
jit_plain = pitch_jitter_hz(_PLAIN_PAD, 220.0)
check("E vibrato moves vs plain pad (pitch jitter)",
      jit_vib > max(jit_plain * 5, 0.3),
      f"vibrato={jit_vib:.4f}Hz plain={jit_plain:.4f}Hz")

# shimmer: a detuned-pair beat -- squarely inside the existing broadband 0.5-12 Hz
# envelope-modulation-depth reading at any register, unaffected by the envelope removal.
plain_mod = slow_mod_depth(_PLAIN_PAD)
d_shim = slow_mod_depth(load("motion_shimmer_pad"))
check("E shimmer moves vs plain pad (broadband depth)", d_shim > max(plain_mod * 3, 0.05),
      f"shimmer={d_shim:.4f} plain={plain_mod:.4f}")

# breathe/evolve: slow (0.2 Hz / 0.09 Hz, straight from the lexicon) amplitude-tilt
# motions -- a single-target-frequency envelope-modulation-depth reading at their own
# known rate, valid even though under one full LFO cycle fits in the 1.6 s window.
for mot in ("breathe", "evolve"):
    target_hz = lex.TOOLS[mot]["rate_hz"]
    d = target_freq_mod_depth(load(f"motion_{mot}_pad"), target_hz)
    plain_at_rate = target_freq_mod_depth(_PLAIN_PAD, target_hz)
    check(f"E {mot} moves vs plain pad (at its own {target_hz:.2f}Hz)",
          d > max(plain_at_rate * 2.5, 0.005),
          f"{mot}={d:.4f} plain={plain_at_rate:.4f}")

# F: high-band share warm < harsh
hb_w, hb_h = band_share(load("char_warm_bell"), 3000), band_share(load("char_harsh_bell"), 3000)
check("F bell warm<harsh HF share", hb_h > hb_w * 1.3, f"warm={hb_w:.4f} harsh={hb_h:.4f}")
hs_w, hs_h = band_share(load("char_warm_saw_stack"), 3000), band_share(load("char_harsh_saw_stack"), 3000)
check("F saw warm<harsh HF share", hs_h > hs_w * 1.3, f"warm={hs_w:.4f} harsh={hs_h:.4f}")

fails = 0
print(f"{'check':44s} {'ok':4s} detail")
for n, ok, d in checks:
    print(f"{n:44s} {'PASS' if ok else 'FAIL':4s} {d}")
    fails += (not ok)
print(f"\n{len(checks)-fails}/{len(checks)} label-fidelity checks pass")
sys.exit(1 if fails else 0)
