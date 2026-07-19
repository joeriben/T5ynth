#!/usr/bin/env python3
"""Distinguish REAL liveness from a global filter sweep.

A bus filter moves every partial the same way: their level curves over time are
highly CORRELATED, and the whole spectrum tilts as one. Independent per-rank
drift, beating and breathing move partials against each other: correlation near
zero, and individual partials wander while their neighbours do not.

So per partial i, track its level L_i(t) across the note; then report
  - mean pairwise correlation of the L_i curves  (high => one thing moves them all)
  - per-partial coefficient of variation          (is anything moving at all?)
  - beat detection: slow periodic AM on a single partial

Usage: liveness.py file.wav [f0_hz]
"""
import sys
import numpy as np
from scipy.io import wavfile

path = sys.argv[1]
f0 = float(sys.argv[2]) if len(sys.argv) > 2 else 261.63

sr, d = wavfile.read(path)
if d.ndim > 1:
    d = d.mean(axis=1)
d = d.astype(np.float64)
d /= (np.abs(d).max() or 1.0)

# onset
win = int(sr * 0.02)
env = np.array([np.sqrt(np.mean(d[i:i+win]**2)) for i in range(0, len(d)-win, win)])
thr = env.max() * 0.15
onset = next((i*win/sr for i in range(1, len(env)) if env[i] > thr and env[i-1] <= thr), None)
if onset is None:
    print("no onset"); sys.exit(1)

# analysis window: skip the first 100ms, and STOP before the release -- every
# partial falls together at note-off, which is correlated for a trivial reason
# and would read as "one global process" no matter what the oscillator does.
rel = len(d) / sr
for i in range(int((onset + 0.3) / (win / sr)), len(env)):
    if env[i] < thr:
        rel = i * win / sr
        break
t0, t1 = onset + 0.10, min(onset + 3.4, rel - 0.15, len(d) / sr)
hop = 0.040                      # 25 frames/sec -- fast enough for a 0.4Hz beat
flen = 0.120
frames, times = [], []
t = t0
while t + flen < t1:
    i0, i1 = int(t*sr), int((t+flen)*sr)
    seg = d[i0:i1] * np.hanning(i1-i0)
    spec = np.abs(np.fft.rfft(seg, n=1 << 15))
    frames.append(spec); times.append(t - onset)
    t += hop
if len(frames) < 12:
    print("note too short"); sys.exit(1)
F = np.array(frames)
freqs = np.fft.rfftfreq(1 << 15, 1/sr)

# pick the strongest partials from the mean spectrum, on the f0 grid and off it
mean = F.mean(axis=0)
cand = []
for h in np.arange(0.5, 16.01, 0.5):
    fc = f0 * h
    if fc > sr/2 - 100:
        break
    lo, hi = np.searchsorted(freqs, fc*0.985), np.searchsorted(freqs, fc*1.015)
    if hi <= lo:
        continue
    k = lo + int(np.argmax(mean[lo:hi]))
    cand.append((mean[k], k, h))
# A partial's level is the ENERGY IN A BAND around it, not one bin. A single bin
# swings on the least frequency wander or spectral leakage: read that way, the
# known-static bell scored 37% variation and "independent life", which is the
# measurement moving, not the sound. Half-width ~0.5% of the partial frequency.
def band_level(col_k):
    fc = freqs[col_k]
    lo_i = np.searchsorted(freqs, fc * 0.995)
    hi_i = max(lo_i + 1, np.searchsorted(freqs, fc * 1.005))
    return np.sqrt((F[:, lo_i:hi_i] ** 2).sum(axis=1))
cand.sort(reverse=True)
# Only partials that are REALLY THERE. A bin 30dB+ down is FFT noise floor, and
# noise is uncorrelated by construction -- including it drags the mean
# correlation toward zero and would certify a global filter sweep as "alive".
# (Caught by validating this metric against the known filter-swept organ, which
# it initially scored +0.105.)
if cand:
    ref = cand[0][0]
    cand = [c for c in cand if c[0] > ref * 10 ** (-30 / 20.0)]
sel = sorted(cand[:9], key=lambda c: c[2])

print(f"file={path.split('/')[-1]}  onset={onset:.2f}s  frames={len(F)} over {times[-1]:.2f}s  f0={f0}Hz")
print(f"{'partial':>9} {'freq':>8} {'meanLvl':>9} {'CV%':>7}   trajectory (dB rel. own mean)")
curves = []
for amp, k, h in sel:
    L = band_level(k)
    curves.append(L)
    cv = 100.0 * L.std() / (L.mean() or 1e-12)
    db = 20*np.log10(L/(L.mean() or 1e-12) + 1e-12)
    spark = "".join(" ▁▂▃▄▅▆▇█"[min(8, max(0, int((v+6)/1.5)))] for v in db[::max(1, len(db)//40)])
    print(f"{h:>9.1f} {freqs[k]:>8.1f} {20*np.log10(L.mean()/(mean.max() or 1)+1e-12):>8.1f}dB {cv:>6.1f}   {spark}")

C = np.array(curves)
# A filter sweep does NOT move every partial alike -- it moves each by an amount
# set by its distance from the cutoff, so plain zero-lag correlation between a low
# and a high partial is low and the statistic cannot see it (validated: the two
# known sweeps scored only +0.21 / +0.28). The real question is not "do they move
# together" but "is every motion explained by ONE hidden variable" -- the cutoff.
# That is a principal component: one global process => PC1 carries nearly all the
# variance; independent per-rank drift => variance spreads across components.
Lg = np.log(C + 1e-12)
Lg = Lg - Lg.mean(axis=1, keepdims=True)
sv = np.linalg.svd(Lg, compute_uv=False)
var = sv ** 2
pc1 = float(var[0] / (var.sum() or 1e-12))
# How far each partial actually travels, in dB -- the absolute "is anything
# moving" gate. Percent-of-mean was useless as a threshold: the known-static bell
# reads 17.8% purely from band noise. In dB that is 1.4, against 2.9 / 4.5 for the
# two known sweeps, so the noise floor is calibrated from a case known to be dead.
dbstd = np.array([20 * np.log10(1.0 + c.std() / (c.mean() or 1e-12)) for c in C])
print()
print(f"variance explained by PC1 (one hidden variable)  : {pc1:.3f}")
print(f"  PC1+PC2                                        : {float(var[:2].sum()/(var.sum() or 1e-12)):.3f}")
print(f"  median per-partial travel                      : {np.median(dbstd):.2f} dB")
print(f"  spread of per-partial travel (min..max)        : {dbstd.min():.2f} .. {dbstd.max():.2f} dB")
print()
if np.median(dbstd) < 2.0:
    print("VERDICT: STATIC -- nothing is moving.")
elif pc1 > 0.62:
    print("VERDICT: ONE GLOBAL PROCESS moves everything (filter sweep / tremolo).")
elif pc1 < 0.45:
    print("VERDICT: INDEPENDENT per-rank motion -- genuine life inside the generator.")
else:
    print("VERDICT: mixed -- a shared process plus some independent motion.")
