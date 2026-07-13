#!/usr/bin/env python3
"""Two things in one backend load.

PART 1 - PIN TRANSIENT: inject the synonym-stacked TRANSIENT direction onto
transient-free sustained/smooth bases, unmasked, same t-grid as the rhythm pin
(0..1.2). Crest = transient sharpness. WAVs for the ear -> transient_pin_out.

PART 2 - BEGIN UNMASK (lever #1): take a SHIPPED single-word axis from
SEMANTIC_AXIS_POLES and render it MASKED (the plugin's default, _mask_pad on) vs
UNMASKED, same base/seed/strength. If masked corr-to-base ~= 0.99 (inert) and
unmasked is far lower (strong), the shipped mask cripples the whole axis layer.
WAVs -> unmask_proof_out.
"""
import os, sys, time, json
import numpy as np

SCRATCH = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRATCH)
from audition_axes_mac import PipeClient, write_wav, features, VENV_PY, BACKEND, MODEL, DUR, STEPS, CFG

TRN_OUT = os.path.join(SCRATCH, "transient_pin_out")
UNM_OUT = os.path.join(SCRATCH, "unmask_proof_out")
os.makedirs(TRN_OUT, exist_ok=True)
os.makedirs(UNM_OUT, exist_ok=True)

TRANSIENT = "sharp, punchy, percussive, snappy, staccato, clicking, plucked transient attacks"
SMOOTH = "a smooth, soft, legato, gentle, flowing, mellow, even sustained swell"
TRN_BASES = [("organ", "a sustained organ chord"), ("pad", "a warm analog synth pad"),
             ("violin", "a held violin note, sustained"), ("drone", "a smooth sine drone")]
TS_T = [0.0, 0.3, 0.6, 0.9, 1.2]
SEEDS = [11, 55]

SHIP_AXES = ["tonal_noisy", "music_noise"]     # shipped single-word poles (SEMANTIC_AXIS_POLES)
UNM_BASE = "a warm analog synth pad"
UNM_T = 1.0


def crest(y):
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


def corr(a, b):
    return float(np.corrcoef(a, b)[0, 1])


def mono(y):
    return y.mean(0) if y.ndim == 2 else y


client = PipeClient([str(VENV_PY), str(BACKEND)])
assert MODEL in client.info.get("models", [])
t0 = time.time(); n = 0

# ---------- PART 1: pin transient ----------
trn = {}
for bkey, base in TRN_BASES:
    for t in TS_T:
        cs = []
        for s in SEEDS:
            req = {"model": MODEL, "prompt_a": base, "prompt_b": base, "alpha": 0.0,
                   "track_type": "music", "modality_epoch": 0, "unmask_manipulation": True,
                   "duration": DUR, "steps": STEPS, "cfg_scale": CFG, "seed": s}
            if t > 0:
                req["semantic_axes"] = {"trn": t}
                req["semantic_axes_poles"] = {"trn": [SMOOTH, TRANSIENT]}
                req["axes_amount"] = 1.0
            r = client.request(req)
            if s == SEEDS[0]:
                write_wav(os.path.join(TRN_OUT, f"{bkey}_t{t:.1f}.wav"), r["audio"], r["sr"])
            cs.append(crest(mono(r["audio"]))); n += 1
        trn[(bkey, t)] = float(np.mean(cs))
    print(f"  [P1] {bkey} transient swept ({time.time()-t0:.0f}s)", flush=True)

# ---------- PART 2: masked vs unmasked on a SHIPPED single-word axis ----------
unm = {}
for axis in SHIP_AXES:
    # base (t=0)
    base_specs = []
    for s in SEEDS:
        req = {"model": MODEL, "prompt_a": UNM_BASE, "prompt_b": UNM_BASE, "alpha": 0.0,
               "track_type": "music", "modality_epoch": 0,
               "duration": DUR, "steps": STEPS, "cfg_scale": CFG, "seed": s}
        r = client.request(req)
        if s == SEEDS[0]:
            write_wav(os.path.join(UNM_OUT, f"{axis}_base.wav"), r["audio"], r["sr"]); n += 1
        base_specs.append((s, logspec(mono(r["audio"])), features(r["audio"], r["sr"])))
    for cond, unmask in (("masked", False), ("unmasked", True)):
        corrs, zcrs, cens = [], [], []
        for (s, bspec, bf) in base_specs:
            req = {"model": MODEL, "prompt_a": UNM_BASE, "prompt_b": UNM_BASE, "alpha": 0.0,
                   "track_type": "music", "modality_epoch": 0, "unmask_manipulation": unmask,
                   "duration": DUR, "steps": STEPS, "cfg_scale": CFG, "seed": s,
                   "semantic_axes": {axis: UNM_T}, "axes_amount": 1.0}
            r = client.request(req)
            if s == SEEDS[0]:
                write_wav(os.path.join(UNM_OUT, f"{axis}_{cond}_t{UNM_T}.wav"), r["audio"], r["sr"])
            f = features(r["audio"], r["sr"])
            corrs.append(corr(bspec, logspec(mono(r["audio"])))); zcrs.append(f["zcr"]); cens.append(f["centroid"]); n += 1
        unm[(axis, cond)] = (float(np.mean(corrs)), float(np.mean(zcrs)), float(np.mean(cens)))
    unm[(axis, "base")] = (1.0, float(np.mean([b[2]["zcr"] for b in base_specs])),
                           float(np.mean([b[2]["centroid"] for b in base_specs])))
    print(f"  [P2] {axis} masked/unmasked done ({time.time()-t0:.0f}s)", flush=True)
client.close()
print(f"{n} clips in {time.time()-t0:.0f}s", flush=True)

print("\n" + "=" * 84)
print("PART 1 - TRANSIENT PIN: crest (transient sharpness) vs injection strength t")
print("  base = t0.0 (no transients); success = crest climbs as the transient push takes over")
print("-" * 84)
for bkey, _ in TRN_BASES:
    vals = [trn[(bkey, t)] for t in TS_T]
    d = vals[-1] - vals[0]
    cells = "  ".join(f"t{t:.1f}={v:5.1f}" for t, v in zip(TS_T, vals))
    print(f"   {bkey:7s} {cells}   Δ={d:+.1f}")

print("\n" + "=" * 84)
print(f"PART 2 - UNMASK PROOF: shipped single-word axis @t={UNM_T}, masked vs unmasked")
print("  corr2base ~1.0 = INERT (no effect) | low = strong. zcr = noisiness (tonal->noisy rises)")
print("-" * 84)
for axis in SHIP_AXES:
    b = unm[(axis, "base")]; m = unm[(axis, "masked")]; u = unm[(axis, "unmasked")]
    print(f"   {axis}")
    print(f"      base      : zcr={b[1]:.3f} cen={b[2]:6.0f}")
    print(f"      MASKED    : corr2base={m[0]:.3f}  zcr={m[1]:.3f} cen={m[2]:6.0f}   <- plugin default")
    print(f"      UNMASKED  : corr2base={u[0]:.3f}  zcr={u[1]:.3f} cen={u[2]:6.0f}   <- the fix")
    print(f"      => mask dilutes the effect {(1-m[0]):.3f} -> {(1-u[0]):.3f} spectral change "
          f"({(1-u[0])/max(1-m[0],1e-3):.0f}x stronger unmasked)")

json.dump({f"{k[0]}|{k[1]}": trn[k] for k in trn}, open(os.path.join(TRN_OUT, "crest.json"), "w"), indent=1)
print(f"\nTRANSIENT WAVs: {TRN_OUT}\nUNMASK WAVs:    {UNM_OUT}")
