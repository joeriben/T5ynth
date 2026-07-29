#!/usr/bin/env python3
"""LCO plausibility GATE — the analysis half of the test setup.

Consumes LCO recipes (JSON: {"keyframes":[{partials:[{h,a,phase}]}], "motion":..})
and, per station keyframe, computes the actual spectral signature the shipping
DcoBaker will render (partials -> a*sin(h*x+phase) single cycle -> rFFT):

  centroid      spectral centroid in harmonic numbers (brightness)
  npart         # significant partials (> 8% of peak) — rich vs pure
  odd_even      odd-harmonic / even-harmonic energy (square/hollow = high)
  inharm        energy on NON-integer partials (glassy/metallic/bell)
  rolloff       high/low band ratio

Across stations: morph distance (0 = STANDING wave) and centroid trajectory.

Then a COARSE plausibility check against the tokens in the prompt (sine -> pure,
square -> odd-heavy, bright -> high centroid, dark/warm -> low, gritty/analog ->
some inharmonic 'deviation', a multi-waveform/arrow prompt -> >=2 DISTINCT
stations). Prints a per-prompt PASS/FAIL with the reasons — this is the gate the
LCO must clear.

Usage:
  .venv/bin/python tools/analyze_lco.py recipes_dir/            # analyze dumped recipes
  .venv/bin/python tools/analyze_lco.py --recipe path.json --prompt "square > sine"
"""
import sys, os, json, math, glob, argparse
import numpy as np

FRAME = 2048
H = np.arange(1, 64)  # harmonic bins to score


def render_cycle(kf):
    x = np.linspace(0.0, 2.0 * math.pi, FRAME, endpoint=False)
    y = np.zeros(FRAME)
    for p in kf.get("partials", []):
        y += p["a"] * np.sin(p["h"] * x + p.get("phase", 0.0))
    n = np.max(np.abs(y))
    return y / n if n > 1e-9 else y


def spectrum(cycle):
    """rFFT magnitude, normalized; index k == harmonic k."""
    mag = np.abs(np.fft.rfft(cycle))
    if mag[1:].max() > 1e-9:
        mag = mag / mag[1:].max()
    return mag


def metrics(kf):
    cyc = render_cycle(kf)
    mag = spectrum(cyc)
    k = np.arange(len(mag))
    band = mag[1:64]
    kk = k[1:64]
    e = band ** 2
    tot = e.sum() + 1e-12
    centroid = float((kk * e).sum() / tot)
    npart = int((band > 0.08).sum())
    odd = e[0::2].sum()   # harmonics 1,3,5,... (kk starts at 1)
    even = e[1::2].sum()  # harmonics 2,4,6,...
    odd_even = float(odd / (even + 1e-9))
    # inharmonicity: partials whose h is > 0.12 from the nearest integer
    inh = 0.0
    for p in kf.get("partials", []):
        frac = abs(p["h"] - round(p["h"]))
        if frac > 0.12:
            inh += p["a"] ** 2
    inharm = float(inh / (sum(p["a"] ** 2 for p in kf.get("partials", [])) + 1e-9))
    lo = e[:8].sum(); hi = e[8:].sum()
    rolloff = float(hi / (lo + 1e-9))
    return dict(centroid=centroid, npart=npart, odd_even=odd_even,
                inharm=inharm, rolloff=rolloff, cycle=cyc)


def cycle_distance(a, b):
    return float(np.linalg.norm(a - b) / math.sqrt(len(a)))


# ─── coarse plausibility expectations per prompt token ────────────────────────

def check_plausible(prompt, mets):
    """Return (ok: bool, notes: list[str]). Coarse, honest heuristics — a station
    whose spectrum flatly contradicts its named timbre fails."""
    p = prompt.lower()
    notes = []
    ok = True
    nst = len(mets)

    # multi-waveform / arrow prompts must yield >= 2 DISTINCT stations
    arrowy = any(a in p for a in (">", "->", "→", "morph", "into", "becomes", "to "))
    named = sum(w in p for w in ("sine", "square", "saw", "triangle", "pulse",
                                 "violin", "piano", "bell", "organ", "flute"))
    if (arrowy or named >= 2):
        if nst < 2:
            ok = False; notes.append(f"prompt implies a morph but only {nst} station kept (COLLAPSED)")
        else:
            d = max(cycle_distance(mets[i - 1]["cycle"], mets[i]["cycle"]) for i in range(1, nst))
            if d < 0.05:
                ok = False; notes.append(f"stations near-identical (max d={d:.3f}) — STANDING wave")
            elif d < 0.15:
                notes.append(f"stations only weakly distinct (max d={d:.3f})")

    # per-token spectral sanity on the CLOSEST-matching station (best case)
    def any_station(pred):
        return any(pred(m) for m in mets)

    if "sine" in p and not any_station(lambda m: m["npart"] <= 2 and m["centroid"] < 3):
        ok = False; notes.append("'sine' named but no near-pure station (npart<=2, centroid<3)")
    if "square" in p and not any_station(lambda m: m["odd_even"] > 6 and m["npart"] >= 3):
        ok = False; notes.append("'square' named but no odd-heavy multi-partial station")
    if "saw" in p and not any_station(lambda m: m["npart"] >= 5 and m["odd_even"] < 4):
        notes.append("'saw' named but no rich all-harmonic station (weak)")
    if any(w in p for w in ("bright", "piercing", "brilliant")) and not any_station(lambda m: m["centroid"] > 5):
        notes.append("'bright' named but no high-centroid station (weak)")
    if any(w in p for w in ("dark", "warm", "muffled", "mellow")) and not any_station(lambda m: m["centroid"] < 4):
        notes.append("'dark/warm' named but no low-centroid station (weak)")
    if any(w in p for w in ("metallic", "glassy", "bell", "crystal")) and not any_station(lambda m: m["inharm"] > 0.1):
        notes.append("'metallic/glassy/bell' named but no inharmonic station (weak)")
    if any(w in p for w in ("gritty", "analog", "dirty")) and not any_station(lambda m: m["inharm"] > 0.03 or m["npart"] >= 6):
        notes.append("'gritty/analog' named but station is clean (no deviation/analog character)")
    return ok, notes


def analyze_recipe(prompt, recipe):
    kfs = recipe.get("keyframes", [])
    mets = [metrics(kf) for kf in kfs]
    ok, notes = check_plausible(prompt, mets)
    print(f"\nPROMPT {prompt!r}   ({len(kfs)} station keyframe(s), frames={recipe.get('frames')}, rate={recipe.get('motion_rate_hz')})")
    for i, m in enumerate(mets):
        print(f"  station {i}: centroid={m['centroid']:.2f}  npart={m['npart']:2d}  "
              f"odd/even={m['odd_even']:.1f}  inharm={m['inharm']:.2f}  rolloff={m['rolloff']:.2f}")
    if len(mets) >= 2:
        ds = [cycle_distance(mets[i - 1]["cycle"], mets[i]["cycle"]) for i in range(1, len(mets))]
        print(f"  morph distances: {[round(d,3) for d in ds]}  (0=standing)")
    verdict = "PASS" if ok else "*** FAIL ***"
    print(f"  {verdict}")
    for n in notes:
        print(f"     - {n}")
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path", nargs="?", help="dir of *.json recipes (name = prompt) ")
    ap.add_argument("--recipe"); ap.add_argument("--prompt")
    args = ap.parse_args()

    items = []
    if args.recipe:
        items.append((args.prompt or args.recipe, json.load(open(args.recipe))))
    elif args.path:
        for f in sorted(glob.glob(os.path.join(args.path, "*.json"))):
            data = json.load(open(f))
            prompt = data.get("_prompt") or os.path.splitext(os.path.basename(f))[0]
            recipe = data.get("recipe", data)
            items.append((prompt, recipe))
    else:
        ap.error("give a recipes dir or --recipe")

    npass = sum(analyze_recipe(p, r) for p, r in items)
    print(f"\n==== {npass}/{len(items)} prompts PASS the plausibility gate ====")
    return 0 if npass == len(items) else 1


if __name__ == "__main__":
    sys.exit(main())
