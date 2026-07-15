#!/usr/bin/env python3
"""End-to-end render of the LCO station chain through the SHIPPED engine
(Slice 2a + Slice 2b acceptance glue). No C++ changes.

For each prompt it:
  1. authors a recipe on the deterministic path (lco_author.build_lco_response
     with a null coder == llm_route=None: the coder is used ONLY for S2 residue
     routing, and these prompts have no residue to route, so this is the pure
     deterministic core plus movement-by-default plus, for Slice 2b, the
     character-pass framework in dco_recipe.apply_character_passes);
  2. classifies the recipe by render medium and drives the MATCHING shipped path:
       * inharmonic (some non-integer h)  -> the additive-sets bank path in
         tools/audition_additive_sets.cpp (the sets-router medium). When
         inharmonic, it first asserts the shipped additive-sets router contract
         (an independent re-implementation of PromptPanel.cpp, not dco_recipe's
         own self-check);
       * harmonic (all-integer h, >=1 station) -> the baked-chain path in
         tools/audition_dco_bake.cpp JSON mode, which parses the recipe with the
         plugin's own recipeFromVar and (for any multi-keyframe recipe) bakes via
         dco::Baker::bake -> setExactFrames -> renderScan(scanLinear), i.e. the
         DCO plugin load path;
  3. reports peak / rms, the first-window vs mid-window spectral centroid, and the
     adjacent-station spectral distance (mean/max over the aligned station chain)
     as NUMBERS only -- no audibility claims (the ear is the instance, later
     slice). The engine path is named per render.

Run from the repo root:
    python3 tools/e2e_additive_sets_render.py
"""
import json
import math
import os
import subprocess
import sys
import wave

import numpy as np

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BACKEND = os.path.join(REPO, "backend")
sys.path.insert(0, BACKEND)

import lco_author as lca          # the shipping mode=="dco" entry
import dco_recipe as dr

SR = 44100
F0 = 220.0
SECS = 3.0
BAKE_SECS = 2.0          # audition_dco_bake JSON mode hardcodes 220 Hz / 2.0 s / 44100
OUTDIR = os.path.join(REPO, "tools", "e2e_additive_sets_out")

# Slice 2a acceptance batch (unchanged): pure movement, sets medium.
SLICE2A_PROMPTS = [
    "the sound of a cathedral bell, opening up",
    "saw morphing into a bell",
    "metallic shimmer settling",
    "sine",
]

# Slice 2b acceptance batch: the five texture adjectives as construction passes.
# Harmonic bases (saw/pad/strings/square) route to the baked-chain path; bell
# bases are inharmonic and route to the additive-sets path.
TEXTURE_PROMPTS = [
    "dirty saw",
    "analog pad",
    "old strings",
    "washed-out bell",
    "overdriven square",
    "dirty bell",
    "dirty old analog saw",
]


def _null_llm(user, system, max_new):
    return ""   # S2 residue router stubbed to route nothing -> deterministic core


def _sets_router_ok(recipe):
    """Independent re-implementation of the shipped PromptPanel.cpp additive-sets
    route test (does NOT trust dco_recipe): every keyframe Additive, every station
    the SAME non-empty partial count, some partial non-integer h."""
    kfs = recipe.get("keyframes") or []
    if not kfs or not all(kf.get("kind") == "additive" for kf in kfs):
        return False
    counts = [len(kf.get("partials") or []) for kf in kfs]
    if counts[0] == 0 or any(c != counts[0] for c in counts):
        return False
    return any(abs(float(p["h"]) - round(float(p["h"]))) > 1e-3
               for kf in kfs for p in kf["partials"])


def _is_inharmonic(recipe):
    return any(kf.get("kind") == "additive"
               and any(abs(float(p["h"]) - round(float(p["h"]))) > 1e-3 for p in (kf.get("partials") or []))
               for kf in (recipe.get("keyframes") or []))


def _to_sets_json(recipe):
    """recipe keyframes (all Additive stations) -> the audition tool's sets JSON."""
    sets = []
    for kf in recipe.get("keyframes") or []:
        station = [{"h": float(p["h"]), "a": float(p["a"]), "phase": float(p.get("phase", 0.0))}
                   for p in (kf.get("partials") or [])]
        sets.append(station)
    return {"sets": sets, "motion_rate_hz": float(recipe.get("motion_rate_hz", 0.5))}


def _compile_tool(src_basename, bin_name):
    """Compile a tools/<src_basename> audition binary per the shipped header build
    line, against the already-built plugin static lib. Returns the binary path."""
    flags = os.path.join(REPO, "build_clean", "CMakeFiles", "T5ynth.dir", "flags.make")
    lib = os.path.join(REPO, "build_clean", "T5ynth_artefacts", "Release", "libT5ynth_SharedCode.a")
    src = os.path.join(REPO, "tools", src_basename)
    binp = os.path.join(OUTDIR, bin_name)
    rsp = os.path.join(OUTDIR, "flags.rsp")

    defines = includes = None
    with open(flags, "r") as f:
        for line in f:
            if defines is None and line.startswith("CXX_DEFINES"):
                defines = line.split("=", 1)[1].strip()
            elif includes is None and line.startswith("CXX_INCLUDES"):
                includes = line.split("=", 1)[1].strip()
    with open(rsp, "w") as f:
        f.write((defines or "") + "\n" + (includes or "") + "\n")

    frameworks = ["Accelerate", "AudioToolbox", "Cocoa", "CoreAudio", "CoreAudioKit",
                  "CoreMIDI", "DiscRecording", "Foundation", "IOKit", "QuartzCore",
                  "Security", "WebKit"]
    cmd = ["clang++", "-std=c++17", "-O2", "@" + rsp, src, lib]
    for fw in frameworks:
        cmd += ["-framework", fw]
    cmd += ["-weak_framework", "Metal", "-weak_framework", "MetalKit", "-o", binp]
    print(f"compiling {src_basename} ...", flush=True)
    r = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + "\n" + r.stderr + "\n")
        raise SystemExit("compile failed")
    return binp


def _read_wav(path):
    with wave.open(path, "rb") as w:
        n = w.getnframes()
        raw = w.readframes(n)
    return np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0


def _centroid(sig):
    if sig.size < 64:
        return float("nan")
    win = sig * np.hanning(sig.size)
    spec = np.abs(np.fft.rfft(win))
    freqs = np.fft.rfftfreq(sig.size, 1.0 / SR)
    tot = spec.sum()
    return float((freqs * spec).sum() / tot) if tot > 0 else float("nan")


def _adjacent_station_distance(recipe):
    """Recipe-level movement magnitude: for each adjacent station pair, the L2
    distance of the amplitude spectra matched by partial (rounded-h key, missing
    -> 0, so it is robust to any residual misalignment). Returns (mean, max) over
    the chain, or (nan, nan) for a single station. Numbers only."""
    kfs = recipe.get("keyframes") or []
    if len(kfs) < 2:
        return float("nan"), float("nan")

    def amp_map(kf):
        m = {}
        for p in (kf.get("partials") or []):
            key = round(float(p["h"]), 3)
            m[key] = m.get(key, 0.0) + float(p["a"])
        return m

    dists = []
    for a, b in zip(kfs, kfs[1:]):
        ma, mb = amp_map(a), amp_map(b)
        keys = set(ma) | set(mb)
        d = math.sqrt(sum((ma.get(k, 0.0) - mb.get(k, 0.0)) ** 2 for k in keys))
        dists.append(d)
    return (sum(dists) / len(dists), max(dists)) if dists else (float("nan"), float("nan"))


def _render_one(prompt, sets_bin, bake_bin):
    """Author the recipe, drive the matching shipped engine path, measure. Returns
    a row dict. NUMBERS only -- no audibility claims."""
    resp = lca.build_lco_response(prompt, _null_llm, frames=None)
    recipe = resp["recipe"]
    inh = _is_inharmonic(recipe)
    contract = _sets_router_ok(recipe)

    slug = "".join(c if c.isalnum() else "_" for c in prompt)[:40]
    jpath = os.path.join(OUTDIR, slug + ".json")
    wpath = os.path.join(OUTDIR, slug + ".wav")

    if inh:
        # sets medium: assert the independent router contract, then bank path.
        assert contract, f"router contract violated for inharmonic recipe: {prompt!r}"
        engine = "additive-sets bank (WavetableOscillator.setAdditiveBank)"
        dur = SECS
        with open(jpath, "w") as f:
            json.dump(_to_sets_json(recipe), f)
        run = subprocess.run([sets_bin, jpath, wpath, str(SECS), str(F0)],
                             capture_output=True, text=True)
    else:
        # harmonic medium: full recipe JSON -> plugin recipeFromVar -> Baker::bake
        # (any multi-keyframe recipe) -> setExactFrames -> renderScan(scanLinear).
        engine = "baked chain (dco::Baker::bake -> setExactFrames -> scanLinear)"
        dur = BAKE_SECS
        with open(jpath, "w") as f:
            json.dump(recipe, f)
        run = subprocess.run([bake_bin, jpath, wpath],
                             capture_output=True, text=True)
    tool_line = ((run.stderr.strip() + " " + run.stdout.strip()).strip().splitlines() or [""])[-1]

    sig = _read_wav(wpath)
    peak = float(np.max(np.abs(sig))) if sig.size else 0.0
    rms = float(np.sqrt(np.mean(sig * sig))) if sig.size else 0.0
    w = int(0.5 * SR)                        # first vs mid window (0.5 s each)
    first = sig[SR // 20: SR // 20 + w]
    mid = sig[sig.size // 2: sig.size // 2 + w]
    c_first = _centroid(first)
    c_mid = _centroid(mid)
    d_mean, d_max = _adjacent_station_distance(recipe)

    n_stations = len(recipe.get("keyframes") or [])
    n_part = len(recipe["keyframes"][0]["partials"]) if recipe.get("keyframes") else 0
    passes = (resp.get("resolved") or {}).get("passes")

    print()
    print(f"PROMPT: {prompt!r}")
    print(f"  engine: {engine}  ({dur:.1f}s @ {F0:.0f} Hz, sr={SR})")
    print(f"  inharmonic={inh}  router_contract_ok={contract}  "
          f"stations={n_stations}  partials/station={n_part}  rate_hz={recipe.get('motion_rate_hz')}")
    if passes:
        print(f"  passes: {passes}")
    print(f"  tool: {tool_line}")
    print(f"  peak={peak:.4f}  rms={rms:.4f}")
    print(f"  spectral_centroid_hz: first_window={c_first:.1f}  mid_window={c_mid:.1f}  "
          f"delta={c_mid - c_first:+.1f}")
    print(f"  adjacent_station_dist(amp L2): mean={d_mean:.4f}  max={d_max:.4f}")
    return dict(prompt=prompt, inh=inh, contract=contract, ns=n_stations, npz=n_part,
                peak=peak, rms=rms, cf=c_first, cm=c_mid, dmean=d_mean, dmax=d_max,
                engine=("sets" if inh else "bake"))


def _print_table(title, rows):
    print()
    print("=" * 96)
    print(title)
    print(f"{'prompt':<26}{'eng':>5}{'sta':>4}{'part':>5}{'peak':>8}{'rms':>8}"
          f"{'cent_first':>11}{'cent_mid':>10}{'adj_mean':>10}{'adj_max':>9}")
    print("-" * 96)
    for r in rows:
        cf = "nan" if math.isnan(r["cf"]) else f"{r['cf']:.1f}"
        cm = "nan" if math.isnan(r["cm"]) else f"{r['cm']:.1f}"
        dm = "nan" if math.isnan(r["dmean"]) else f"{r['dmean']:.4f}"
        dx = "nan" if math.isnan(r["dmax"]) else f"{r['dmax']:.4f}"
        print(f"{r['prompt'][:25]:<26}{r['engine']:>5}{r['ns']:>4}{r['npz']:>5}"
              f"{r['peak']:>8.4f}{r['rms']:>8.4f}{cf:>11}{cm:>10}{dm:>10}{dx:>9}")


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    sets_bin = _compile_tool("audition_additive_sets.cpp", "additive_sets")
    bake_bin = _compile_tool("audition_dco_bake.cpp", "dco_bake")

    print()
    print("=" * 96)
    print("E2E render through the shipped engine (numbers only, no audibility claims)")
    print("  sets prompts  -> additive-sets bank path (audition_additive_sets)")
    print("  harmonic ones -> baked-chain path (audition_dco_bake JSON mode)")
    print("=" * 96)

    slice2a = [_render_one(p, sets_bin, bake_bin) for p in SLICE2A_PROMPTS]
    texture = [_render_one(p, sets_bin, bake_bin) for p in TEXTURE_PROMPTS]

    _print_table("Slice 2a batch (movement, sets medium)", slice2a)
    _print_table("Slice 2b batch (character passes)", texture)
    print()
    print("WAVs + recipe/sets JSON written under", OUTDIR)


if __name__ == "__main__":
    main()
