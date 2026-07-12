#!/usr/bin/env python3
"""What the DCO palette can ACTUALLY do when methods are taken up, parametrized,
and COMBINED -- vs. the additive-amplitude-list the coder currently emits.

Each recipe below is hand-authored to exercise real synthesis: fm2 (ratio/index),
cheby waveshaping (order/drive), ring mod (ratio/mix), pulse (width), additive --
and to MORPH between different methods across the frame axis. Baked through the
REAL dco::Baker -> WavetableOscillator (the same binary lco_testbench uses), then
measured for RICHNESS (how many methods, how much the spectrum moves, how complex
it gets), NOT for word-match. The last entry is the plain additive "square" the
current coder would emit -- the baseline the rich recipes must beat.

This establishes the TARGET the LCO coder must author.

  BAKE=<dco_bake> .venv/bin/python tools/dco_palette_demo.py
"""
import os, sys, json, subprocess

sys.path.insert(0, os.path.dirname(__file__))
import lco_testbench as TB
import numpy as np

OUT = os.path.join(os.path.dirname(__file__), "dco_palette_out")


def _motion(n):
    """Equal one-pass morph across all n keyframes (mirrors lco_author._build_motion)."""
    if n <= 1:
        return [{"to": 0, "dur_frac": 1.0, "curve": "lin"}]
    m = [{"to": 0, "dur_frac": 0.0, "curve": "lin"}]
    for i in range(1, n):
        m.append({"to": i, "dur_frac": 1.0, "curve": "lin"})
    return m


def kf(kind, **p):
    d = {"kind": kind, "width": 0.5, "ratio": 2.0, "index": 1.0,
         "order": 2, "drive": 1.0, "mix": 1.0, "shape": 0.0}
    d.update(p)
    return d


def recipe(keyframes, rate=0.25):
    return {"keyframes": keyframes, "motion": _motion(len(keyframes)),
            "loop": False, "frames": 256, "motion_rate_hz": rate}


SQ = [{"h": 1, "a": 1.0, "phase": 0}, {"h": 3, "a": 0.33, "phase": 0},
      {"h": 5, "a": 0.2, "phase": 0}, {"h": 7, "a": 0.14, "phase": 0},
      {"h": 9, "a": 0.11, "phase": 0}]
SOFT = [{"h": 1, "a": 1.0, "phase": 0}, {"h": 2, "a": 0.5, "phase": 0},
        {"h": 3, "a": 0.3, "phase": 0}, {"h": 4, "a": 0.15, "phase": 0}]

# name -> (recipe, one-line description of the synthesis it exercises)
DEMOS = {
    "metallic_pluck": (recipe([
        kf("fm2", ratio=3, index=6.0),                 # bright inharmonic attack
        kf("cheby", order=8, drive=0.9, shape=0.1),    # waveshaped edge
        kf("additive", partials=SOFT),                 # soft decaying body
    ]), "fm2 -> cheby -> additive morph (3 methods)"),

    "evolving_metal": (recipe([
        kf("fm2", ratio=5, index=2.0),                 # closed
        kf("fm2", ratio=5, index=8.0),                 # opened up (FM index sweep)
    ]), "fm2 index 2->8 (parametrized FM movement)"),

    "ring_clang": (recipe([
        kf("ring", ratio=5, mix=0.85),                 # inharmonic clang
        kf("ring", ratio=3, mix=0.55),
        kf("additive", partials=SOFT),
    ]), "ring 5:1 -> ring 3:1 -> additive morph"),

    "cheby_open": (recipe([
        kf("cheby", order=3, drive=0.4),               # gentle
        kf("cheby", order=10, drive=0.95, shape=0.15), # aggressive
    ]), "cheby order 3->10, drive 0.4->0.95"),

    "pwm_into_fm": (recipe([
        kf("pulse", width=0.5),
        kf("pulse", width=0.08),                       # thin PWM
        kf("fm2", ratio=2, index=4.0),                 # into FM
    ]), "pulse 0.5 -> pulse 0.08 -> fm2 morph"),

    "BASELINE_additive_square": (recipe([
        kf("additive", partials=SQ),                   # what the coder emits today
    ]), "static additive square (current coder output)"),
}


def richness(recipe_dict, an):
    kinds = [k["kind"] for k in recipe_dict["keyframes"]]
    distinct = len(set(kinds))
    max_np = max(m[1] for m in an["perframe"])         # richest partial count on the sweep
    return {
        "methods": kinds,
        "distinct_methods": distinct,
        "max_partials": max_np,
        "centroid_spread": an["morph_range"],
        "flux": an["flux"],
        "rms": an["rms"],
    }


def main():
    bake = TB._find_bake()
    if not bake:
        print("NO dco_bake. Set BAKE=<path>.", file=sys.stderr); return 2
    os.makedirs(OUT, exist_ok=True)
    print(f"bake: {bake}\nout: {OUT}\n")
    print(f"{'recipe':<26} {'methods':<8} {'parts':<6} {'move(cent)':<11} {'flux':<7} {'rms'}")
    print("-" * 74)
    for name, (rec, desc) in DEMOS.items():
        jpath = os.path.join(OUT, name + ".json")
        wpath = os.path.join(OUT, name + ".wav")
        with open(jpath, "w") as f:
            json.dump(rec, f)
        r = subprocess.run([bake, jpath, wpath], capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(wpath):
            print(f"{name:<26} BAKE FAILED: {r.stderr.strip()[:120]}"); continue
        an = TB.analyze(wpath)
        rr = richness(rec, an)
        alive = "MOVING" if rr["flux"] > 0.03 else ("drift" if rr["flux"] > 0.012 else "STATIC")
        print(f"{name:<26} {rr['distinct_methods']}/{ len(rr['methods']):<6} "
              f"{rr['max_partials']:<6} {rr['centroid_spread']:<11.2f} "
              f"{rr['flux']:<7.3f} {rr['rms']:.3f}  {alive}")
        print(f"{'':<26} {desc}")
    print(f"\nWAVs: {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
