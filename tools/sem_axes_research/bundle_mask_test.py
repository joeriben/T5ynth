#!/usr/bin/env python3
"""Does a BUNDLED pole survive the MASKED shipping path?

Three conditions at matched base/seed/t:
  A) bundle UNMASKED  (known-strong reference)
  B) bundle MASKED    (the plugin's default _mask_pad path)  <- THE QUESTION
  C) single-word MASKED (the current shipped axis)           <- inert floor
If B tracks A -> a built bundle-axis works on the shipping path, no unmask flag needed.
If B tracks C -> the mask throttles even bundles; the flag (or a real fix) is required.
rhythmic -> pulse (periodicity), on a low-pulse base (hum). transient -> crest, on pad.
"""
import os, sys, time, json
import numpy as np

SCRATCH = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRATCH)
from audition_axes_mac import PipeClient, write_wav, VENV_PY, BACKEND, MODEL, DUR, STEPS, CFG

OUT = os.path.join(SCRATCH, "bundle_mask_test_out")
os.makedirs(OUT, exist_ok=True)

RHYTHMIC = "a rhythmic, percussive, syncopated, pulsing, driving, staccato, beat-driven groove"
SUSTAINED = "a sustained, continuous, persistent, stable, held, unbroken, droning, steady tone"
TRANSIENT = "sharp, punchy, percussive, snappy, staccato, clicking, plucked transient attacks"
SMOOTH = "a smooth, soft, legato, gentle, flowing, mellow, even sustained swell"
SEEDS = [11, 55]
TS = [0.6, 1.2]

# (label, base, feature, bundle poles, shipped single-word axis key or None)
CASES = [
    ("rhy", "a steady sustained hum", "pulse", [SUSTAINED, RHYTHMIC], "rhythmic_sustained"),
    ("trn", "a warm analog synth pad", "crest", [SMOOTH, TRANSIENT], None),
]


def onset_env(y, sr, win=1024, hop=256):
    y = np.asarray(y, np.float64)
    if len(y) < win:
        y = np.pad(y, (0, win - len(y)))
    nf = 1 + (len(y) - win) // hop
    wnd = np.hanning(win)
    S = np.empty((nf, win // 2 + 1))
    for i in range(nf):
        S[i] = np.abs(np.fft.rfft(y[i * hop:i * hop + win] * wnd))
    return np.maximum(0.0, np.diff(S, axis=0)).sum(axis=1), sr / hop


def pulse(y, sr):
    env, fr = onset_env(y, sr)
    e = env - env.mean()
    if np.allclose(e, 0):
        return 0.0
    ac = np.correlate(e, e, "full")[len(e) - 1:]; ac = ac / (ac[0] + 1e-12)
    lo, hi = int(0.2 * fr), min(int(1.5 * fr), len(ac) - 1)
    return float(np.max(ac[lo:hi])) if hi > lo else 0.0


def crest(y, sr):
    y = np.asarray(y, np.float64)
    return float(np.max(np.abs(y)) / (np.sqrt(np.mean(y ** 2)) + 1e-12))


def logspec(y, win=2048, hop=512):
    y = np.asarray(y, np.float64)
    if len(y) < win:
        y = np.pad(y, (0, win - len(y)))
    nf = 1 + (len(y) - win) // hop
    wnd = np.hanning(win)
    acc = np.zeros(win // 2 + 1)
    for i in range(nf):
        acc += np.abs(np.fft.rfft(y[i * hop:i * hop + win] * wnd))
    return np.log1p(acc / max(nf, 1))


def mono(a):
    return a.mean(0) if a.ndim == 2 else a


FEAT = {"pulse": pulse, "crest": crest}
client = PipeClient([str(VENV_PY), str(BACKEND)])
assert MODEL in client.info.get("models", [])
t0 = time.time(); n = 0
R = {}   # (label, t, cond) -> (feat_mean, corr2base_mean)


def gen(base, seed, sem=None, poles=None, unmask=True):
    req = {"model": MODEL, "prompt_a": base, "prompt_b": base, "alpha": 0.0,
           "track_type": "music", "modality_epoch": 0, "unmask_manipulation": unmask,
           "duration": DUR, "steps": STEPS, "cfg_scale": CFG, "seed": seed}
    if sem:
        req["semantic_axes"] = sem; req["axes_amount"] = 1.0
        if poles:
            req["semantic_axes_poles"] = poles
    return client.request(req)


for label, base, fname, bpoles, sw in CASES:
    ff = FEAT[fname]
    # base per seed
    base_y, base_spec = {}, {}
    for s in SEEDS:
        r = gen(base, s)
        y = mono(r["audio"]); base_y[s] = ff(y, r["sr"]); base_spec[s] = logspec(y)
        write_wav(os.path.join(OUT, f"{label}_base.wav"), r["audio"], r["sr"]) if s == SEEDS[0] else None
        n += 1
    R[(label, 0.0, "base")] = (float(np.mean(list(base_y.values()))), 1.0)
    for t in TS:
        conds = [("A_bundle_unmasked", dict(sem={label: t}, poles={label: bpoles}, unmask=True)),
                 ("B_bundle_masked", dict(sem={label: t}, poles={label: bpoles}, unmask=False))]
        if sw:
            conds.append(("C_singleword_masked", dict(sem={sw: t}, poles=None, unmask=False)))
        for cond, kw in conds:
            fs, cs = [], []
            for s in SEEDS:
                r = gen(base, s, **kw); y = mono(r["audio"])
                if s == SEEDS[0]:
                    write_wav(os.path.join(OUT, f"{label}_t{t:.1f}_{cond}.wav"), r["audio"], r["sr"])
                fs.append(ff(y, r["sr"])); cs.append(float(np.corrcoef(base_spec[s], logspec(y))[0, 1])); n += 1
            R[(label, t, cond)] = (float(np.mean(fs)), float(np.mean(cs)))
        print(f"  {label} t={t} done ({time.time()-t0:.0f}s)", flush=True)
client.close()
print(f"{n} clips in {time.time()-t0:.0f}s", flush=True)

print("\n" + "=" * 86)
print("DOES THE BUNDLE SURVIVE THE MASK?  feature (effect) + corr2base (1.0=inert)")
print("  A bundle-unmasked = strong ref | B bundle-MASKED = the question | C singleword-masked = floor")
print("-" * 86)
for label, base, fname, bpoles, sw in CASES:
    b = R[(label, 0.0, 'base')][0]
    print(f"\n  [{label}]  base {fname}={b:.3f}   (base='{base}')")
    for t in TS:
        print(f"    t={t}:")
        for cond in ("A_bundle_unmasked", "B_bundle_masked", "C_singleword_masked"):
            if (label, t, cond) in R:
                fv, cv = R[(label, t, cond)]
                print(f"       {cond:22s} {fname}={fv:6.3f}   corr2base={cv:.3f}")

json.dump({f"{k[0]}|{k[1]}|{k[2]}": R[k] for k in R}, open(os.path.join(OUT, "R.json"), "w"), indent=1)
print(f"\nWAVs: {OUT}")
