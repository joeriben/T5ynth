#!/usr/bin/env python3
"""End-to-end render of the LCO station chain through the SHIPPED engine
(Slice 2a acceptance glue). No C++ changes.

For each prompt it:
  1. authors a recipe on the deterministic path (lco_author.build_lco_response
     with a null coder == llm_route=None: the coder is used ONLY for S2 residue
     routing, and these prompts have no residue to route, so this is the pure
     deterministic core plus movement-by-default);
  2. asserts the shipped additive-sets router contract (PromptPanel.cpp) when the
     recipe is inharmonic -- an independent re-implementation, not dco_recipe's
     own self-check;
  3. converts the recipe keyframes -> the sets JSON of
     tools/audition_additive_sets.cpp ({"sets":[[{h,a,phase}...]...],
     "motion_rate_hz":...});
  4. compiles that audition tool per its header build line (the plugin static lib
     is already in build_clean/) and renders a WAV through the REAL
     WavetableOscillator additive-sets path;
  5. reports peak / rms and the first-window vs mid-window spectral centroid as
     NUMBERS only -- no audibility claims (the ear is the instance, later slice).

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
OUTDIR = os.path.join(REPO, "tools", "e2e_additive_sets_out")

PROMPTS = [
    "the sound of a cathedral bell, opening up",
    "saw morphing into a bell",
    "metallic shimmer settling",
    "sine",
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


def _compile_tool():
    """Compile tools/audition_additive_sets.cpp per its own header build line
    against the already-built plugin static lib. Returns the binary path."""
    flags = os.path.join(REPO, "build_clean", "CMakeFiles", "T5ynth.dir", "flags.make")
    lib = os.path.join(REPO, "build_clean", "T5ynth_artefacts", "Release", "libT5ynth_SharedCode.a")
    src = os.path.join(REPO, "tools", "audition_additive_sets.cpp")
    binp = os.path.join(OUTDIR, "additive_sets")
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
    print("compiling audition_additive_sets ...", flush=True)
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


def main():
    os.makedirs(OUTDIR, exist_ok=True)
    binp = _compile_tool()

    print()
    print("=" * 78)
    print("E2E additive-sets render (numbers only, no audibility claims)")
    print("=" * 78)
    rows = []
    for prompt in PROMPTS:
        resp = lca.build_lco_response(prompt, _null_llm, frames=None)
        recipe = resp["recipe"]
        inh = _is_inharmonic(recipe)
        contract = _sets_router_ok(recipe)
        if inh:
            assert contract, f"router contract violated for inharmonic recipe: {prompt!r}"

        slug = "".join(c if c.isalnum() else "_" for c in prompt)[:40]
        jpath = os.path.join(OUTDIR, slug + ".json")
        wpath = os.path.join(OUTDIR, slug + ".wav")
        with open(jpath, "w") as f:
            json.dump(_to_sets_json(recipe), f)

        run = subprocess.run([binp, jpath, wpath, str(SECS), str(F0)],
                             capture_output=True, text=True)
        tool_line = (run.stdout.strip().splitlines() or [""])[-1]

        sig = _read_wav(wpath)
        peak = float(np.max(np.abs(sig))) if sig.size else 0.0
        rms = float(np.sqrt(np.mean(sig * sig))) if sig.size else 0.0
        # first vs mid window (0.5 s each), skipping the very first samples.
        w = int(0.5 * SR)
        first = sig[SR // 20: SR // 20 + w]
        mid = sig[sig.size // 2: sig.size // 2 + w]
        c_first = _centroid(first)
        c_mid = _centroid(mid)

        n_stations = len(recipe.get("keyframes") or [])
        n_part = len(recipe["keyframes"][0]["partials"]) if recipe.get("keyframes") else 0
        rows.append((prompt, inh, contract, n_stations, n_part, peak, rms, c_first, c_mid))
        print()
        print(f"PROMPT: {prompt!r}")
        print(f"  inharmonic={inh}  router_contract_ok={contract}  "
              f"stations={n_stations}  partials/station={n_part}  rate_hz={recipe.get('motion_rate_hz')}")
        print(f"  tool: {tool_line}")
        print(f"  peak={peak:.4f}  rms={rms:.4f}")
        print(f"  spectral_centroid_hz: first_window={c_first:.1f}  mid_window={c_mid:.1f}  "
              f"delta={c_mid - c_first:+.1f}")

    print()
    print("=" * 78)
    print(f"{'prompt':<44}{'sta':>4}{'part':>5}{'peak':>8}{'rms':>8}{'cent_first':>11}{'cent_mid':>10}")
    print("-" * 78)
    for (prompt, inh, contract, ns, npz, peak, rms, cf, cm) in rows:
        print(f"{prompt[:43]:<44}{ns:>4}{npz:>5}{peak:>8.4f}{rms:>8.4f}{cf:>11.1f}{cm:>10.1f}")
    print("WAVs + sets JSON written under", OUTDIR)


if __name__ == "__main__":
    main()
