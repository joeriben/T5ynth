#!/usr/bin/env python3
"""LCO LLM-map PoC harness — side-by-side comparison of dco_recipe.author_recipe's
deterministic exact-phrase word-scan (the baseline) against the new LLM-driven
lexicon-key router (backend/dco_llm_map.llm_map_to_recipe) on prompts the
exact-phrase matcher is known to miss: metaphors ("cathedral bell"), non-
enumerated synonyms ("crystalline", "nasaler"), compositional morphs without a
literal connector word ("music box then a dark drone"), and mixed EN/DE.

Does NOT modify dco_recipe.py / dco_lexicon.json / lco_author.py /
pipe_inference.py. Reuses dco_recipe's composer/validator only through
dco_llm_map's own import of it, and reuses pipe_inference.run_instruct as the
LLM call — the SAME call pipe_inference itself makes for translate/interpret,
run here against the small general translator model (NOT the LCO coder model:
this new S1 stage is a language-UNDERSTANDING task, not a code-authoring one).

Run from repo root with the project .venv:
  DCO_BAKE=<path to compiled dco_bake>  .venv/bin/python tools/lco_llm_map_poc.py

Env vars:
  DCO_BAKE / BAKE            — the compiled tools/audition_dco_bake.cpp binary
                                (argc==3 form: recipe-json-path wav-path). DCO_BAKE
                                is the documented name for this harness; BAKE and
                                the scratchpad-glob fallback mirror
                                tools/lco_testbench.py's _find_bake() for
                                compatibility with the existing tool ecosystem. WAV
                                rendering is skipped (not a hard failure) if no
                                binary is found.
  T5YNTH_TRANSLATOR_MODEL /
  T5YNTH_TRANSLATION_MODEL   — explicit path to the mapping LLM's model dir;
                                otherwise pipe_inference._resolve_translation_model_dir
                                auto-discovers the installed translator.
  T5YNTH_DEVICE               — device for the mapping LLM, default "mps".
"""
import glob
import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "backend"))

import dco_recipe                              # noqa: E402 -- the deterministic baseline
import dco_llm_map                             # noqa: E402 -- the new LLM-routed path

OUT = os.path.join(os.path.dirname(__file__), "lco_llm_map_poc_out")

# ~10 hard prompts the exact-phrase matcher is known to miss (metaphors,
# non-enumerated synonyms, compositional morphs, mixed EN/DE), plus one
# already-literally-parseable chain ("square that morphs into a bell") kept
# as a sanity cross-check: since that prompt names two real technique surface
# forms joined by a real connector surface form, the deterministic baseline
# already chains it correctly, so it is a check that the LLM path's morph-
# chain scan-shape reproduction agrees with the baseline when the vocabulary
# IS literal, not only a "does it improve on hard cases" demo.
PROMPTS = [
    "the sound of a cathedral bell decaying",
    "something glassy and brittle",
    "a warm analog pad that slowly turns harsh",
    "wie ein anschwellendes Kirchenglockenspiel",
    "a metallic shimmer that dissolves into noise",
    "an aggressive buzzy lead",
    "plucked music box then a dark drone",
    "shimmering crystalline pad",
    "ein hohler nasaler Klang",
    "square that morphs into a bell",
]


def _find_bake():
    """Locate the compiled dco_bake tool (tools/audition_dco_bake.cpp, argc==3
    form). DCO_BAKE is this harness's documented override; BAKE and the
    scratchpad-glob fallback mirror tools/lco_testbench.py's _find_bake() so
    a binary already built for a sibling tool is picked up here too."""
    b = os.environ.get("DCO_BAKE") or os.environ.get("BAKE")
    if b and os.path.exists(b):
        return b
    for c in glob.glob("/private/tmp/claude-*/**/scratchpad/dco_bake", recursive=True):
        return c
    for c in ("/tmp/dco_bake", os.path.join(OUT, "dco_bake")):
        if os.path.exists(c):
            return c
    return None


def _resolve_translator():
    """The small GENERAL-PURPOSE instruct model dco_llm_map's one LLM call
    runs on (NOT the LCO coder model — this is a language-understanding
    routing task, the translator's own kind of job). Checks both spellings of
    the env override, then falls through to pipe_inference's own
    auto-discovery (precedence: explicit request dict > $T5YNTH_TRANSLATION_MODEL
    > auto-discovered <model root>/translation/<dir>)."""
    import pipe_inference
    override = os.environ.get("T5YNTH_TRANSLATOR_MODEL") or os.environ.get("T5YNTH_TRANSLATION_MODEL")
    request = {"model_path": override} if override else {}
    return pipe_inference._resolve_translation_model_dir(request)


def _slug(p):
    return "".join(c if c.isalnum() else "_" for c in p)[:40]


def _fmt(resolved):
    tech = resolved.get("technique") or "-"
    adjs = ",".join(resolved.get("adjectives") or []) or "-"
    mot = ",".join(resolved.get("motion") or []) or "-"
    return f"technique={tech}  adj=[{adjs}]  motion=[{mot}]"


def _unresolved_words(resp):
    return [f["word"] for f in resp.get("flags", []) if f.get("tier") == "unresolved"]


def main():
    translator = _resolve_translator()
    if not translator:
        print("NO TRANSLATOR MODEL FOUND. Set T5YNTH_TRANSLATOR_MODEL=<dir>.", file=sys.stderr)
        return 2
    from pipe_inference import run_instruct
    device = os.environ.get("T5YNTH_DEVICE", "mps")
    print(f"translator model: {translator}\ndevice: {device}\n")

    llm = lambda t, sysp, mx: run_instruct(t, str(translator), device, sysp, max_new_tokens=mx)  # noqa: E731

    bake = _find_bake()
    if not bake:
        print("NO dco_bake binary found. Set DCO_BAKE=<path>. (WAV rendering will be skipped.)\n",
              file=sys.stderr)
    os.makedirs(OUT, exist_ok=True)

    rows = []
    for i, prompt in enumerate(PROMPTS, 1):
        slug = f"{i:02d}_{_slug(prompt)}"
        print("=" * 100)
        print(f"[{slug}] PROMPT: {prompt!r}")

        baseline = dco_recipe.author_recipe(prompt)
        llm_resp = dco_llm_map.llm_map_to_recipe(prompt, llm)

        base_line = _fmt(baseline["resolved"])
        llm_line = _fmt(llm_resp["resolved"])
        print(f"  baseline : {base_line}")
        print(f"  llm-map  : {llm_line}")

        base_unresolved = _unresolved_words(baseline)
        llm_unresolved = _unresolved_words(llm_resp)
        if base_unresolved:
            print(f"    baseline not understood: {', '.join(base_unresolved)}")
        if llm_unresolved:
            print(f"    llm-map  not understood: {', '.join(llm_unresolved)}")

        base_json = os.path.join(OUT, slug + ".baseline.json")
        llm_json = os.path.join(OUT, slug + ".llm.json")
        with open(base_json, "w") as f:
            json.dump(baseline["recipe"], f, indent=1)
        with open(llm_json, "w") as f:
            json.dump(llm_resp["recipe"], f, indent=1)

        wav_status = "skipped (no dco_bake)"
        if bake:
            wav_path = os.path.join(OUT, slug + ".llm.wav")
            r = subprocess.run([bake, llm_json, wav_path], capture_output=True, text=True)
            if r.returncode == 0 and os.path.exists(wav_path):
                wav_status = wav_path
            else:
                wav_status = f"BAKE FAILED: {r.stderr.strip()[:200]}"
        print(f"  wav      : {wav_status}")

        rows.append((prompt, base_line, llm_line, wav_status))

    print("\n" + "=" * 100)
    print("SUMMARY")
    print("=" * 100)
    for prompt, base_line, llm_line, wav_status in rows:
        print(f"{prompt!r}")
        print(f"    baseline: {base_line}")
        print(f"    llm-map : {llm_line}")
        print(f"    wav     : {wav_status}")
    print(f"\nout dir: {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
