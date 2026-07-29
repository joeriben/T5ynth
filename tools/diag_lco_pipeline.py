#!/usr/bin/env python3
"""EMPIRICAL diagnosis of the LCO wavetable pipeline on the maintainer's own
prompts. Runs the REAL path (backend/lco_author.py + the real coder model via
pipe_inference.run_instruct — the exact call pipe_inference makes for mode=="dco")
and reports, per prompt:

  - the STATIONS the coder planned (should be 2-3 DISTINCT timbre phrases),
  - the raw GEN reply + parsed spectrum per station (did it PARSE?),
  - the resulting Additive keyframes,
  - the rendered single-cycle for each keyframe, and the normalized L2 distance
    between CONSECUTIVE station cycles: 0.0 == identical (a STANDING wave),
    1.0 == maximally different. This is the maintainer's "standing waves /
    nearly identical" claim, made falsifiable.

No fabrication: every number comes from the actual model + the actual
GEN->partials->cycle math the shipping DcoBaker uses (a*sin(h*x+phase)).

Run from repo root with the project venv:
  T5YNTH_CODER_MODEL=<coder dir>  .venv/bin/python tools/diag_lco_pipeline.py
(or let it auto-discover the installed coder). Prints a table; writes nothing.
"""
import os, sys, math
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "backend"))

import numpy as np  # noqa: E402
import lco_author  # noqa: E402
from lco_author import _plan_stations, _emit_spectrum, _spectrum_to_keyframe, author_recipe  # noqa: E402

PROMPTS = [
    "square > sine",
    "square wave > sine wave",
    "violin > piano",
    "gritty saw",
    "a clean sine morphing into a gritty analog saw",
]

FRAME = 2048


def render_cycle(kf):
    """One single cycle from an Additive keyframe, exactly as DcoBaker::renderAdditive
    sums a*sin(h*x+phase) with FLOAT h (shape NOT applied — we compare the spectral
    content the morph interpolates)."""
    x = np.linspace(0.0, 2.0 * math.pi, FRAME, endpoint=False)
    y = np.zeros(FRAME)
    for p in kf.get("partials", []):
        y += p["a"] * np.sin(p["h"] * x + p.get("phase", 0.0))
    n = np.max(np.abs(y))
    return y / n if n > 1e-9 else y


def cycle_distance(a, b):
    """Normalized L2 distance between two peak-normalized cycles (phase-naive).
    0 == identical, ~1.4 == orthogonal. <0.05 is a standing wave for our purposes."""
    return float(np.linalg.norm(a - b) / math.sqrt(len(a)))


def main():
    coder = os.environ.get("T5YNTH_CODER_MODEL") or os.environ.get("LCO_MODEL_DIR")
    if not coder:
        # Fall back to pipe_inference's resolver (auto-discovered installed coder).
        import pipe_inference
        coder = pipe_inference._resolve_coder_model_dir({})
    if not coder:
        print("NO CODER MODEL FOUND. Set T5YNTH_CODER_MODEL=<dir>.", file=sys.stderr)
        return 2
    from pipe_inference import run_instruct
    device = os.environ.get("T5YNTH_DEVICE", "mps")
    print(f"coder model: {coder}\ndevice: {device}\n")

    def lco_llm(text, system_prompt, max_new_tokens):
        # EXACTLY pipe_inference's mode=="dco" call (greedy, shipped gen params).
        return run_instruct(text, str(coder), device, system_prompt,
                            max_new_tokens=max_new_tokens)

    for prompt in PROMPTS:
        print("=" * 72)
        print(f"PROMPT: {prompt!r}")
        stations = _plan_stations(prompt, lco_llm)
        print(f"  stations planned ({len(stations)}): {stations}")
        cycles, kept = [], []
        for s in stations:
            raw = lco_llm(s, lco_author.SPECTRUM_SYS, 160)   # match _emit_spectrum's budget
            parsed = lco_author._parse_one_spectrum(raw)
            kf = _spectrum_to_keyframe(parsed) if parsed else None
            status = "PARSED" if kf else "*** UNPARSEABLE -> station DROPPED ***"
            firstline = raw.strip().splitlines()[0] if raw.strip() else "(empty)"
            print(f"    station {s!r}: {status}")
            print(f"      GEN reply: {firstline}")
            if kf:
                cycles.append(render_cycle(kf))
                kept.append(s)
        recipe, kept2 = author_recipe(prompt, lco_llm, frames=256)
        nkf = len(recipe["keyframes"])
        print(f"  -> recipe keyframes: {nkf}   (a 1-keyframe recipe is a STATIC table)")
        if len(cycles) >= 2:
            print("  consecutive station-cycle distances (0=standing, <0.05=near-identical):")
            for i in range(1, len(cycles)):
                d = cycle_distance(cycles[i - 1], cycles[i])
                verdict = "STANDING/near-identical" if d < 0.05 else ("weak" if d < 0.2 else "distinct")
                print(f"      {kept[i-1]!r} -> {kept[i]!r}:  d={d:.4f}  [{verdict}]")
        elif nkf <= 1:
            print("  -> ONE keyframe only: the table cannot morph (standing wave BY CONSTRUCTION).")
    print("=" * 72)
    return 0


if __name__ == "__main__":
    sys.exit(main())
