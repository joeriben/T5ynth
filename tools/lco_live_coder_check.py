#!/usr/bin/env python3
"""Verify the LIVE production coder authors plausible spectra through the REAL
baker. Unlike tools/lco_coder_bench.py (which reimplemented the station/spectrum
PROMPTS to prove the fix in isolation), this calls the SHIPPING coder function
lco_author.author_recipe() directly -- so a PASS here is a PASS on the exact code
build_lco_response would run. The proven prompts now live in lco_author itself, so
the two must agree; this is the production-path confirmation.

Model capability is user-installed (T5ynth ships no model); here an ollama model
stands in for a capable installed coder.

  BAKE=<dco_bake> .venv/bin/python tools/lco_live_coder_check.py gemma3:27b
"""
import os, sys, json, subprocess

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "backend"))
sys.path.insert(0, os.path.dirname(__file__))
import lco_author as LA               # THE shipping coder
import lco_testbench as TB            # analyze() + gate() + _find_bake() + PROMPTS
from lco_coder_bench import ollama_llm, OUT_ROOT


def main():
    model = sys.argv[1] if len(sys.argv) > 1 else "gemma3:27b"
    bake = TB._find_bake()
    if not bake:
        print("NO dco_bake. Set BAKE=<path>.", file=sys.stderr); return 2
    outdir = os.path.join(OUT_ROOT, "LIVE_" + model.replace(":", "_").replace("/", "_"))
    os.makedirs(outdir, exist_ok=True)
    print(f"LIVE coder: lco_author.author_recipe   model: {model}\nout: {outdir}\n")

    llm = ollama_llm(model)
    npass = 0
    for i, prompt in enumerate(TB.PROMPTS, 1):
        slug = f"{i:02d}_{TB._slug(prompt)}"
        recipe, kept = LA.author_recipe(prompt, llm, None)     # <-- THE SHIPPING CODER
        jpath = os.path.join(outdir, slug + ".json")
        wpath = os.path.join(outdir, slug + ".wav")
        with open(jpath, "w") as f:
            json.dump(recipe, f)
        r = subprocess.run([bake, jpath, wpath], capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(wpath):
            print(f"[{slug}] BAKE FAILED: {r.stderr.strip()[:160]}"); continue
        an = TB.analyze(wpath)
        resp = {"resolved": {"technique": "coder", "adjectives": kept, "motion": []}, "flags": []}
        ok, notes = TB.gate(prompt, resp, an)
        nkf = len(recipe["keyframes"])
        print(f"{'PASS' if ok else '*** FAIL ***'}  [{slug}]  {prompt!r}   "
              f"stations={kept}  ({nkf} keyframe(s))")
        print(f"    trajectory: centroid spread={an['morph_range']:.2f}  flux={an['flux']:.3f}  "
              f"({'ALIVE' if an['flux'] > 0.012 else 'static'})")
        for n in notes:
            print(f"      - {n}")
        npass += 1 if ok else 0
    print(f"\n==== LIVE author_recipe @ {model}: {npass}/{len(TB.PROMPTS)} PASS the plausibility gate ====")
    print(f"WAVs: {outdir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
