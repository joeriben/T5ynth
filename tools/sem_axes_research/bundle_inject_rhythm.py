#!/usr/bin/env python3
"""Clean rhythmic-injection re-test. Inject the synonym-stacked rhythmic direction
onto LOW-periodicity bases (a drone has no existing pulse to disrupt, only room to
add), unmasked, fine t capped at 1.2 (the previous run over-pushed at t=2). If the
rhythmic semantic asserts itself, onset-envelope periodicity rises from the base's
low floor toward the rhythmic pole's own level (~0.70), monotonically in t.
Self-contained DSP so importing this triggers no other generation.
"""
import os, sys, time, json
import numpy as np

SCRATCH = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRATCH)
from audition_axes_mac import PipeClient, write_wav, VENV_PY, BACKEND, MODEL, DUR, STEPS, CFG

OUT = os.path.join(SCRATCH, "bundle_rhythm_out")
os.makedirs(OUT, exist_ok=True)

RHYTHMIC = "a rhythmic, percussive, syncopated, pulsing, driving, staccato, beat-driven groove"
SUSTAINED = "a sustained, continuous, persistent, stable, held, unbroken, droning, steady tone"
POLE_PULSE = 0.704   # the rhythmic bundle's own periodicity (step 1) = the ceiling to reach

# candidate LOW-periodicity bases (drones / washes) — base pulse reported so we see which qualify
BASES = [
    ("sine",   "a single sine drone"),
    ("noise",  "a white noise wash"),
    ("pad",    "a low held synth pad"),
    ("ambient","an ambient drone texture"),
    ("hum",    "a steady sustained hum"),
]
TS = [0.0, 0.3, 0.6, 0.9, 1.2]
SEEDS = [11, 55]


def onset_env(y, sr, win=1024, hop=256):
    if len(y) < win:
        y = np.pad(y, (0, win - len(y)))
    nf = 1 + (len(y) - win) // hop
    wnd = np.hanning(win)
    S = np.empty((nf, win // 2 + 1))
    for i in range(nf):
        S[i] = np.abs(np.fft.rfft(y[i * hop:i * hop + win] * wnd))
    return np.maximum(0.0, np.diff(S, axis=0)).sum(axis=1), sr / hop


def pulse_clarity(env, fr):
    e = env - env.mean()
    if np.allclose(e, 0):
        return 0.0
    ac = np.correlate(e, e, "full")[len(e) - 1:]
    ac = ac / (ac[0] + 1e-12)
    lo, hi = int(0.2 * fr), min(int(1.5 * fr), len(ac) - 1)
    return float(np.max(ac[lo:hi])) if hi > lo else 0.0


def regularity(env, fr):
    thr = env.mean() + 0.5 * env.std()
    pk = [i for i in range(1, len(env) - 1) if env[i] > thr and env[i] >= env[i - 1] and env[i] > env[i + 1]]
    if len(pk) < 3:
        return 0.0
    ioi = np.diff(pk) / fr
    return float(1.0 / (1.0 + ioi.std() / (ioi.mean() + 1e-12)))


def feats(audio, sr):
    y = audio.mean(0) if audio.ndim == 2 else audio
    y = np.asarray(y, np.float64)
    env, fr = onset_env(y, sr)
    return {"pulse": pulse_clarity(env, fr), "reg": regularity(env, fr)}


client = PipeClient([str(VENV_PY), str(BACKEND)])
assert MODEL in client.info.get("models", [])
t0 = time.time(); n = 0
res = {}
for bkey, base in BASES:
    for t in TS:
        fs = []
        for s in SEEDS:
            req = {"model": MODEL, "prompt_a": base, "prompt_b": base, "alpha": 0.0,
                   "track_type": "music", "modality_epoch": 0, "unmask_manipulation": True,
                   "duration": DUR, "steps": STEPS, "cfg_scale": CFG, "seed": s}
            if t > 0:
                req["semantic_axes"] = {"rhy": t}
                req["semantic_axes_poles"] = {"rhy": [SUSTAINED, RHYTHMIC]}
                req["axes_amount"] = 1.0
            r = client.request(req)
            if s == SEEDS[0]:
                write_wav(os.path.join(OUT, f"{bkey}_t{t:.1f}.wav"), r["audio"], r["sr"])
            fs.append(feats(r["audio"], r["sr"])); n += 1
        res[(bkey, t)] = fs
    print(f"  {bkey} swept ({time.time()-t0:.0f}s)", flush=True)
client.close()
print(f"{n} clips in {time.time()-t0:.0f}s", flush=True)


def m(bkey, t, name):
    return float(np.mean([f[name] for f in res[(bkey, t)]]))


print("\n" + "=" * 92)
print(f"RHYTHMIC INJECTION on low-periodicity bases  (pole ceiling pulse={POLE_PULSE:.2f})")
print("  a valid base starts LOW at t0; success = pulse climbs monotonically toward the ceiling")
print("-" * 92)
for bkey, _ in BASES:
    pv = [m(bkey, t, "pulse") for t in TS]
    base0 = pv[0]
    mono = all(pv[i + 1] >= pv[i] - 0.02 for i in range(len(pv) - 1))
    d = pv[-1] - pv[0]
    valid = "LOW base ok" if base0 < 0.35 else "base already high -> skip"
    tag = "  ** MONOTONE UP **" if (mono and d > 0.08 and base0 < 0.35) else (
        "  rises" if d > 0.05 else "  flat/none")
    cells = "  ".join(f"t{t:.1f}={v:.3f}" for t, v in zip(TS, pv))
    print(f"   {bkey:8s} [{valid:24s}] {cells}   Δ={d:+.3f}{tag}")

print("\n  regularity (secondary periodicity check), same rows:")
for bkey, _ in BASES:
    rv = [m(bkey, t, "reg") for t in TS]
    print(f"   {bkey:8s} " + "  ".join(f"t{t:.1f}={v:.2f}" for t, v in zip(TS, rv)))

json.dump({f"{b}|{t}": res[(b, t)] for (b, t) in res}, open(os.path.join(OUT, "features.json"), "w"), indent=1)
print(f"\nWAVs: {OUT}")
