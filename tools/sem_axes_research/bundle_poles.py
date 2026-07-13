#!/usr/bin/env python3
"""Step 1 of the user's method: build each pole from a STACK of synonyms (not a
single word), render it at N seeds, and check (a) seed-stability of its character
and (b) whether the rhythmic/transient poles actually separate from the sustained
ones on measures that can SEE rhythm -- onset-envelope periodicity (pulse clarity)
and transient sharpness (crest factor) -- not an onset count.

If a synonym-stacked 'rhythmic' prompt reliably (seed-stably) renders periodic /
transient audio and 'sustained' reliably does not, the semantic is strong and worth
injecting (step 2). Framed music, SA3 medium, real IPC backend. numpy-only DSP.
"""
import os, sys, time, wave
import numpy as np

SCRATCH = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRATCH)
from audition_axes_mac import PipeClient, write_wav, VENV_PY, BACKEND, MODEL, DUR, STEPS, CFG

OUT = os.path.join(SCRATCH, "bundle_poles_out")
os.makedirs(OUT, exist_ok=True)
SEEDS = [11, 22, 33, 44, 55, 66]

# synonym-STACKED poles (the user's "a sustained, pertained, stable, persistent ... tone")
POLES = [
    ("rhythmic",  "music", "a rhythmic, percussive, syncopated, pulsing, driving, staccato, beat-driven groove"),
    ("transient", "music", "sharp, punchy, percussive, snappy, staccato, clicking, plucked transient attacks"),
    ("sustained", "music", "a sustained, continuous, persistent, stable, held, unbroken, droning, steady tone"),
    ("smooth",    "music", "a smooth, soft, legato, gentle, flowing, mellow, even sustained swell"),
    # literal single-concept anchors, to see if the STACK matches a plain literal prompt
    ("rhy_lit",   "music", "a drum beat at 120 bpm"),
    ("sus_lit",   "music", "a sustained organ chord"),
]


def onset_env(y, sr, win=1024, hop=256):
    if len(y) < win:
        y = np.pad(y, (0, win - len(y)))
    nf = 1 + (len(y) - win) // hop
    wnd = np.hanning(win)
    S = np.empty((nf, win // 2 + 1))
    for i in range(nf):
        S[i] = np.abs(np.fft.rfft(y[i * hop:i * hop + win] * wnd))
    flux = np.maximum(0.0, np.diff(S, axis=0)).sum(axis=1)   # half-wave rectified spectral flux
    return flux, sr / hop


def pulse_clarity(env, fr):
    """peak of normalized onset-envelope autocorrelation in the 40-300 BPM band."""
    e = env - env.mean()
    if np.allclose(e, 0):
        return 0.0
    ac = np.correlate(e, e, "full")[len(e) - 1:]
    ac = ac / (ac[0] + 1e-12)
    lo, hi = int(0.2 * fr), int(1.5 * fr)                    # 40..300 BPM
    hi = min(hi, len(ac) - 1)
    return float(np.max(ac[lo:hi])) if hi > lo else 0.0


def regularity(env, fr):
    """1/(1+CV of inter-onset intervals); high = evenly spaced hits."""
    thr = env.mean() + 0.5 * env.std()
    pk = [i for i in range(1, len(env) - 1) if env[i] > thr and env[i] >= env[i - 1] and env[i] > env[i + 1]]
    if len(pk) < 3:
        return 0.0, len(pk)
    ioi = np.diff(pk) / fr
    cv = ioi.std() / (ioi.mean() + 1e-12)
    return float(1.0 / (1.0 + cv)), len(pk)


def logspec(y, win=2048, hop=512):
    if len(y) < win:
        y = np.pad(y, (0, win - len(y)))
    nf = 1 + (len(y) - win) // hop
    wnd = np.hanning(win)
    acc = np.zeros(win // 2 + 1)
    for i in range(nf):
        acc += np.abs(np.fft.rfft(y[i * hop:i * hop + win] * wnd))
    return np.log1p(acc / max(nf, 1))


def feats(audio, sr):
    y = audio.mean(0) if audio.ndim == 2 else audio
    y = np.asarray(y, np.float64)
    env, fr = onset_env(y, sr)
    pc = pulse_clarity(env, fr)
    reg, npk = regularity(env, fr)
    crest = float(np.max(np.abs(y)) / (np.sqrt(np.mean(y ** 2)) + 1e-12))
    dur = len(y) / sr
    return {"pulse": pc, "crest": crest, "reg": reg, "rate": npk / dur, "ls": logspec(y)}


client = PipeClient([str(VENV_PY), str(BACKEND)])
assert MODEL in client.info.get("models", [])
t0 = time.time()
data = {}
for key, mod, prompt in POLES:
    rows = []
    for s in SEEDS:
        req = {"model": MODEL, "prompt_a": prompt, "prompt_b": prompt, "alpha": 0.0,
               "track_type": mod, "modality_epoch": 0, "unmask_manipulation": True,
               "duration": DUR, "steps": STEPS, "cfg_scale": CFG, "seed": s}
        r = client.request(req)
        write_wav(os.path.join(OUT, f"{key}_s{s}.wav"), r["audio"], r["sr"])
        rows.append(feats(r["audio"], r["sr"]))
    data[key] = rows
    print(f"  {key:10s} done ({time.time()-t0:.0f}s)", flush=True)
client.close()


def col(key, name):
    return np.array([row[name] for row in data[key]])


def spec_consistency(key):
    L = [row["ls"] for row in data[key]]
    ds = [float(np.corrcoef(L[i], L[j])[0, 1]) for i in range(len(L)) for j in range(i + 1, len(L))]
    return float(np.mean(ds))


print("\n" + "=" * 82)
print("SEED-STABILITY of each bundled pole (across 6 seeds)")
print("  spec_consistency: mean pairwise corr of log-spectra (1=identical character every seed)")
print("  cv: coeff. of variation of the rhythm feature (low = the character is seed-robust)")
print("-" * 82)
print(f"   {'pole':10s} {'spec_cons':>10s} {'pulse cv':>9s} {'crest cv':>9s}")
for key, _, _ in POLES:
    pc, cr = col(key, "pulse"), col(key, "crest")
    print(f"   {key:10s} {spec_consistency(key):10.3f} {pc.std()/(pc.mean()+1e-9):9.2f} {cr.std()/(cr.mean()+1e-9):9.2f}")

print("\n" + "=" * 82)
print("DO THE BUNDLED POLES SEPARATE ON RHYTHM/TRANSIENT MEASURES?  mean +/- std over 6 seeds")
print("  pulse = onset-envelope periodicity (rhythm) | crest = transient sharpness | rate = onsets/s")
print("-" * 82)
print(f"   {'pole':10s} {'pulse':>14s} {'crest':>14s} {'reg':>12s} {'rate/s':>10s}")
for key, _, _ in POLES:
    pc, cr, rg, rt = col(key, "pulse"), col(key, "crest"), col(key, "reg"), col(key, "rate")
    print(f"   {key:10s} {pc.mean():6.3f}+-{pc.std():.3f}  {cr.mean():6.2f}+-{cr.std():4.2f}  "
          f"{rg.mean():5.3f}+-{rg.std():.3f} {rt.mean():6.2f}")


def sep(a, b, name):
    A, B = col(a, name), col(b, name)
    pooled = np.sqrt((A.var() + B.var()) / 2) + 1e-9
    d = (A.mean() - B.mean()) / pooled                       # Cohen's d: gap vs seed scatter
    return A.mean(), B.mean(), d


print("\n" + "=" * 82)
print("SEPARATION (Cohen's d = pole gap / within-pole seed scatter; |d|>0.8 large, >2 clean)")
print("-" * 82)
for a, b in [("rhythmic", "sustained"), ("transient", "smooth"), ("transient", "sustained"),
             ("rhy_lit", "sus_lit")]:
    for name in ("pulse", "crest", "rate"):
        ma, mb, d = sep(a, b, name)
        print(f"   {a:9s} vs {b:9s}  {name:6s}: {ma:6.3f} vs {mb:6.3f}   d={d:+5.2f}")
    print()
print(f"WAVs: {OUT}")
