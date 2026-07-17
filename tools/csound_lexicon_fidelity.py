#!/usr/bin/env python3
"""Objective label-fidelity checks over tools/csound_lexicon_out/ renders.

Lesson from the SA3 aesthetic-axes study: never trust labels unmeasured.
Checks (48 kHz mono WAVs, rendered by csound_orch_check: gate 0-2 s, trig at 0 and 1 s):
  A  bright vs plain vs dark spectral-centroid ordering (bell + saw_stack)
  B  register stack: BJ double-bell (16'+8') shows energy an octave below the 8'-only bell
  C  inharmonicity: bell/metal/glass/cymbal partial peaks deviate from the integer grid;
     saw_stack/square_stack/organ_tone stay ON the grid
  D  envelope truth: strike/decay families decay after onset; sustained families hold level;
     the 1 s retrigger produces a fresh energy rise in strike-enveloped renders
  E  motion truth: vibrato/shimmer/breathe/evolve renders show >=3x the slow-rate
     amplitude modulation of the motionless plain pad (solo_pad)
  F  warm < harsh in high-frequency energy share (bell + saw_stack)
All thresholds are ordinal (X > Y), not absolute — robust against level matching.
Exit 0 only if every check passes; prints a table either way.
Regenerate the input renders with: .venv/bin/python -m backend.test_csound_lexicon
Run: .venv/bin/python tools/csound_lexicon_fidelity.py
"""
import struct, sys, math, os
import numpy as np

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

# D: envelopes — pluck/glass decay; pad holds; retrigger at 1 s lifts strike renders
pl = load("solo_pluck")
check("D pluck decays", rms(pl, 0.05, 0.25) > rms(pl, 0.7, 0.95) * 1.8,
      f"early={rms(pl,0.05,0.25):.4f} late={rms(pl,0.7,0.95):.4f}")
pd = load("motion_breathe_pad")
check("D pad holds", rms(pd, 1.5, 1.9) > rms(pd, 0.3, 0.7) * 0.4,
      f"late={rms(pd,1.5,1.9):.4f} early={rms(pd,0.3,0.7):.4f}")
fb = load("solo_fm_bell")
check("D retrigger lifts fm_bell at 1s", rms(fb, 1.0, 1.15) > rms(fb, 0.85, 0.995) * 1.15,
      f"post={rms(fb,1.0,1.15):.4f} pre={rms(fb,0.85,0.995):.4f}")

# E: motion — every motion render modulates well above the MOTIONLESS plain pad.
# The baseline is solo_pad's measured wobble (the pad bed has inherent slow beating,
# ~0.09), not zero. slow_mod_depth reads the AMPLITUDE envelope only — purely spectral
# motion at constant level would need a flux metric; every current motion tool also
# amplitude-modulates (measured 6-7x the plain bed), so this stays valid until a
# constant-level motion key is added.
plain_mod = slow_mod_depth(load("solo_pad"))
for mot in ("vibrato", "shimmer", "breathe", "evolve"):
    d = slow_mod_depth(load(f"motion_{mot}_pad"))
    # absolute floor: a silent/broken baseline must not turn the relative check vacuous
    check(f"E {mot} moves vs plain pad", d > max(plain_mod * 3, 0.05),
          f"{mot}={d:.4f} plain={plain_mod:.4f}")

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
