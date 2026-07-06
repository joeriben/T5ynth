#!/usr/bin/env python3
"""Analyze the filter aliasing renders: spectrograms of the cutoff sweeps and a
quantitative alias-energy metric for the fixed 9 kHz tone. Proof of aliasing =
the alias energy must DROP monotonically as the oversampling factor rises."""
import os, sys
import numpy as np
from scipy import signal
from scipy.io import wavfile
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

OUT = sys.argv[1] if len(sys.argv) > 1 else "out"

def load(name):
    sr, x = wavfile.read(os.path.join(OUT, name))
    return sr, x.astype(np.float64)

# ── 1. Sweep spectrograms: base vs 8× for each filter ────────────────────────
fig, axes = plt.subplots(2, 2, figsize=(13, 8), constrained_layout=True)
for row, filt in enumerate(("ladder", "warp")):
    for col, (N, lbl) in enumerate(((1, "BASE rate (48 kHz) — what ships"),
                                    (8, "8× oversampled — alias-free reference"))):
        sr, x = load(f"{filt}_sweep_os{N}.wav")
        f, t, S = signal.spectrogram(x, fs=sr, nperseg=2048, noverlap=1536, window="hann")
        SdB = 10*np.log10(S + 1e-12)
        ax = axes[row, col]
        m = ax.pcolormesh(t, f/1000, SdB, vmin=-90, vmax=-20, shading="auto", cmap="magma")
        ax.set_ylim(0, 22); ax.set_title(f"{filt.upper()}  —  {lbl}", fontsize=10)
        ax.set_ylabel("kHz"); ax.set_xlabel("s")
fig.suptitle("Cutoff sweep (80 Hz→11 kHz), reso 0.95, +6 dB drive, 24 dB LP\n"
             "Base rate: descending ghost streaks = aliased harmonics folding below Nyquist", fontsize=11)
fig.savefig(os.path.join(OUT, "aliasing_sweep.png"), dpi=110)
print("wrote aliasing_sweep.png")

# ── 2. Fixed-tone alias metric + spectra ─────────────────────────────────────
TONE = 9000.0
def alias_metric(x, sr):
    seg = x[int(0.3*sr):]                       # skip onset transient
    w = np.hanning(len(seg)); X = np.fft.rfft(seg*w)
    fr = np.fft.rfftfreq(len(seg), 1/sr); P = np.abs(X)**2
    band = (fr >= 100) & (fr <= 20000)
    harm = np.zeros_like(fr, dtype=bool)
    for k in range(1, 3):                        # 9 k & 18 k are the only in-band harmonics
        harm |= np.abs(fr - k*TONE) <= 30
    sig = P[band & harm].sum()
    alias = P[band & ~harm].sum()
    return 10*np.log10(alias/ (sig + 1e-20)), fr, P

fig2, axes2 = plt.subplots(1, 2, figsize=(13, 5), constrained_layout=True)
print(f"\n{'filter':8s} {'os1 (base)':>12s} {'os2':>10s} {'os4':>10s} {'os8':>10s}   (alias/signal, dB — lower=cleaner)")
rows = []
for col, filt in enumerate(("ladder", "warp")):
    vals = {}
    ax = axes2[col]
    for N, color in ((1, "#d62728"), (2, "#9467bd"), (4, "#ff7f0e"), (8, "#2ca02c")):
        sr, x = load(f"{filt}_tone_os{N}.wav")
        db, fr, P = alias_metric(x, sr)
        vals[N] = db
        ax.semilogy(fr/1000, P/ (P.max()+1e-20), color=color, lw=0.7,
                    label=f"os{N}×  (alias {db:+.1f} dB)")
    ax.set_xlim(0, 22); ax.set_ylim(1e-9, 2)
    ax.set_title(f"{filt.upper()} — 9 kHz tone into driven resonance"); ax.set_xlabel("kHz")
    ax.set_ylabel("norm. power"); ax.legend(fontsize=8)
    for k in (1, 2): ax.axvline(k*TONE/1000, color="k", ls=":", lw=0.5, alpha=0.4)
    print(f"{filt:8s} {vals[1]:+11.1f}  {vals[2]:+9.1f}  {vals[4]:+9.1f}  {vals[8]:+9.1f}")
    rows.append((filt, vals))
fig2.suptitle("Fixed 9 kHz tone — dotted = real harmonics (9 k, 18 k); everything else is alias/spurious", fontsize=11)
fig2.savefig(os.path.join(OUT, "aliasing_tone.png"), dpi=110)
print("\nwrote aliasing_tone.png")

print("\nImprovement base→8×:")
for filt, v in rows:
    print(f"  {filt:8s} {v[1]-v[8]:.1f} dB less alias energy")
