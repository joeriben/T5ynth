#!/usr/bin/env python3
"""Step 2: does the bundled pole ASSERT ITSELF as an embedding injection?

Inject the synonym-stacked rhythmic / transient DIRECTION (encode(bundle) -
encode("")) onto seed-stable SUSTAINED/tonal bases, unmasked (all 256 positions,
== the real Latent Lab knob), and sweep strength t. If the semantic sets itself
through, onset-envelope PERIODICITY rises monotonically with the rhythmic push and
CREST (transient sharpness) with the transient push -- on a base that had neither.
"""
import os, sys, time, json
import numpy as np

SCRATCH = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRATCH)
from audition_axes_mac import PipeClient, write_wav, VENV_PY, BACKEND, MODEL, DUR, STEPS, CFG
from bundle_poles import onset_env, pulse_clarity, regularity   # reuse identical DSP

OUT = os.path.join(SCRATCH, "bundle_inject_out")
os.makedirs(OUT, exist_ok=True)

RHYTHMIC = "a rhythmic, percussive, syncopated, pulsing, driving, staccato, beat-driven groove"
SUSTAINED = "a sustained, continuous, persistent, stable, held, unbroken, droning, steady tone"
TRANSIENT = "sharp, punchy, percussive, snappy, staccato, clicking, plucked transient attacks"
SMOOTH = "a smooth, soft, legato, gentle, flowing, mellow, even sustained swell"

BASES = [
    ("organ",  "a sustained organ chord"),
    ("pad",    "a warm analog synth pad"),
    ("violin", "a held violin note, sustained"),
]
# axis_key -> (pole_a, pole_b); +t injects pole_b (direction = encode(pole_b) - encode(""))
AXES = {"rhy": (SUSTAINED, RHYTHMIC), "trn": (SMOOTH, TRANSIENT)}
TS = [0.0, 0.5, 1.0, 1.5, 2.0]
SEEDS = [11, 55]


def crest(y):
    return float(np.max(np.abs(y)) / (np.sqrt(np.mean(y ** 2)) + 1e-12))


def feats(audio, sr):
    y = audio.mean(0) if audio.ndim == 2 else audio
    y = np.asarray(y, np.float64)
    env, fr = onset_env(y, sr)
    reg, npk = regularity(env, fr)
    return {"pulse": pulse_clarity(env, fr), "crest": crest(y), "reg": reg, "rate": npk / (len(y) / sr)}


client = PipeClient([str(VENV_PY), str(BACKEND)])
assert MODEL in client.info.get("models", [])
t0 = time.time(); n = 0
res = {}   # (base, axis, t) -> list of feat dicts over seeds
for bkey, base in BASES:
    for ax, (pa, pb) in AXES.items():
        for t in TS:
            fs = []
            for s in SEEDS:
                req = {"model": MODEL, "prompt_a": base, "prompt_b": base, "alpha": 0.0,
                       "track_type": "music", "modality_epoch": 0, "unmask_manipulation": True,
                       "duration": DUR, "steps": STEPS, "cfg_scale": CFG, "seed": s}
                if t > 0:
                    req["semantic_axes"] = {ax: t}
                    req["semantic_axes_poles"] = {ax: [pa, pb]}
                    req["axes_amount"] = 1.0
                r = client.request(req)
                if s == SEEDS[0]:
                    write_wav(os.path.join(OUT, f"{bkey}_{ax}_t{t:.1f}.wav"), r["audio"], r["sr"])
                fs.append(feats(r["audio"], r["sr"])); n += 1
            res[(bkey, ax, t)] = fs
        print(f"  {bkey}/{ax} swept ({time.time()-t0:.0f}s)", flush=True)
client.close()
print(f"{n} clips in {time.time()-t0:.0f}s", flush=True)


def mean_at(bkey, ax, t, name):
    return float(np.mean([f[name] for f in res[(bkey, ax, t)]]))


print("\n" + "=" * 88)
print("INJECTION SWEEP: does the bundled direction assert itself on a sustained base?")
print("  rhy -> watch PULSE (periodicity) climb from t=0 (base) to t=2 | trn -> watch CREST")
print("  mono = strictly rising across t (the injection takes over progressively)")
print("-" * 88)
for bkey, _ in BASES:
    for ax, key in (("rhy", "pulse"), ("trn", "crest")):
        vals = [mean_at(bkey, ax, t, key) for t in TS]
        mono = all(vals[i + 1] >= vals[i] - 1e-3 for i in range(len(vals) - 1))
        d = vals[-1] - vals[0]
        arrow = "  MONOTONE UP" if mono and d > 0 else ("  rises" if d > 0 else "  (no rise)")
        cells = "  ".join(f"t{t:.1f}={v:.3f}" for t, v in zip(TS, vals))
        print(f"   {bkey:7s} {ax} [{key:5s}]: {cells}   Δ={d:+.3f}{arrow}")
    print()

json.dump({f"{b}|{a}|{t}": res[(b, a, t)] for (b, a, t) in res},
          open(os.path.join(OUT, "features.json"), "w"), indent=1)
print(f"WAVs: {OUT}")
