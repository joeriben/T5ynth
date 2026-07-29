#!/usr/bin/env python3
"""E2E proof: REAL Qwen2.5-7B-Instruct -> build_lco_response -> dco_bake WAV.

Drives EXACTLY the surface pipe_inference.py mode=="dco" drives:
  lco_llm = run_instruct(text, <7B dir>, <device>, system_prompt, max_new_tokens=..)
  resp    = build_lco_response(text, lco_llm, frames=None)   # None == the plugin's
                                                             # frames=request.get("frames")
                                                             # when the GUI sends none,
                                                             # so this PROVES the 256 default.
No mocks. No invented prompts.
"""
import json, os, subprocess, sys, wave
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", "..", "..", "Users", "joerissen", "ai", "t5ynth", "backend"))
# robust absolute backend path
BACKEND = "/Users/joerissen/ai/t5ynth/backend"
if BACKEND not in sys.path:
    sys.path.insert(0, BACKEND)

import dco_recipe
from lco_author import build_lco_response
from pipe_inference import run_instruct

MODEL_7B = os.path.expanduser("~/Library/T5ynth/models/qwen2.5-7b-instruct")
DEVICE = os.environ.get("T5YNTH_DEVICE", "mps")
BAKE = "/Users/joerissen/ai/t5ynth/tools/e2e_additive_sets_out/dco_bake"
OUTDIR = os.path.expanduser("~/Downloads/lco_llm_verdrahtung_2026-07-15")
os.makedirs(OUTDIR, exist_ok=True)

# EXACTLY the two verbatim suite prompts (from tools/test_dco_author.py fixtures).
# The elephant prompt is intentionally omitted: /tmp/elephant_7b.json contains only
# the RESPONSE (ok/recipe/resolved/flags/lexicon_version/reference_vocabulary), no
# prompt/text/input field, and the prompt is unrecoverable from any artifact on this
# machine. Set ELEPHANT_PROMPT=... to add it once its exact wording is known.
PROMPTS = ["cathedral bell, opens up", "saw morphing into a bell"]
_ele = os.environ.get("ELEPHANT_PROMPT")
if _ele:
    PROMPTS = [_ele] + PROMPTS


def slug(p):
    return "".join(c if c.isalnum() else "_" for c in p)[:40]


def read_wav_stats(path):
    with wave.open(path, "rb") as w:
        n = w.getnframes()
        sr = w.getframerate()
        raw = w.readframes(n)
    sig = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    if sig.size == 0:
        return 0, sr, 0.0, 0.0
    peak = float(np.max(np.abs(sig)))
    rms = float(np.sqrt(np.mean(sig * sig)))
    return sig.size, sr, peak, rms


def key_sets():
    lex = dco_recipe.load_lexicon()
    return (
        {t["key"] for t in lex["techniques"]},
        {a["key"] for a in lex["adjectives"]},
        {m["key"] for m in lex["motions"]},
    )


# Descriptive motion labels the DETERMINISTIC movement passes append (NOT LLM keys,
# NOT catalogue keys): legitimate, so allowed alongside real motion keys.
_PASS_MOTION_LABELS = {"movement by default", "analog drift"}
_BANNED = ("not understood", "unresolved", "notunderstood")


def validate(resp, tkeys, akeys, mkeys):
    problems = []
    recipe = resp["recipe"]
    frames = recipe.get("frames")
    if frames != 256:
        problems.append(f"frames != 256 (got {frames!r})")

    resolved = resp.get("resolved") or {}
    tech = resolved.get("technique") or ""
    # composite technique strings join keys with '->' (and morph chains); split them.
    comps = [c.strip() for part in tech.replace("→", "->").split("->") for c in part.split(">")]
    comps = [c for c in comps if c]
    for c in comps:
        if c not in tkeys:
            problems.append(f"technique component {c!r} not a lexicon technique key")
    for a in resolved.get("adjectives") or []:
        if a not in akeys:
            problems.append(f"adjective {a!r} not a lexicon adjective key")
    for m in resolved.get("motion") or []:
        if m not in mkeys and m not in _PASS_MOTION_LABELS:
            problems.append(f"motion {m!r} not a lexicon motion key (nor a movement-pass label)")

    # NO 'not understood' / 'unresolved' phrasing in ANY flag field, any wording.
    for fl in resp.get("flags") or []:
        blob = " ".join(str(fl.get(k, "")) for k in ("word", "reason", "tier")).lower()
        for b in _BANNED:
            if b in blob:
                problems.append(f"flag carries banned phrasing {b!r}: {fl}")
    return problems, frames, comps


def main():
    if not os.path.isdir(MODEL_7B):
        print("7B MODEL MISSING:", MODEL_7B); return 2
    if not os.path.exists(BAKE):
        print("dco_bake MISSING:", BAKE); return 2
    tkeys, akeys, mkeys = key_sets()
    print(f"model: {MODEL_7B}\ndevice: {DEVICE}\noutdir: {OUTDIR}\n")

    lco_llm = lambda text, sysp, mx: run_instruct(text, MODEL_7B, DEVICE, sysp, max_new_tokens=mx)  # noqa: E731

    rows = []
    ok_all = True
    for prompt in PROMPTS:
        s = slug(prompt)
        resp = build_lco_response(prompt, lco_llm, frames=None)
        resp_path = os.path.join(OUTDIR, s + ".json")
        recipe_path = os.path.join(OUTDIR, s + ".recipe.json")
        wav_path = os.path.join(OUTDIR, s + ".wav")
        with open(resp_path, "w") as f:
            json.dump(resp, f, indent=1)
        with open(recipe_path, "w") as f:
            json.dump(resp["recipe"], f)

        run = subprocess.run([BAKE, recipe_path, wav_path], capture_output=True, text=True)
        n, sr, peak, rms = (0, 0, 0.0, 0.0)
        if os.path.exists(wav_path):
            n, sr, peak, rms = read_wav_stats(wav_path)

        problems, frames, comps = validate(resp, tkeys, akeys, mkeys)
        resolved = resp.get("resolved") or {}
        nkf = len(resp["recipe"].get("keyframes") or [])
        finite = np.isfinite(peak) and np.isfinite(rms)
        nonsilent = peak > 1e-4
        bake_ok = run.returncode == 0 and n > 0
        row_ok = (not problems) and finite and nonsilent and bake_ok
        ok_all = ok_all and row_ok

        print("=" * 90)
        print(f"PROMPT: {prompt!r}")
        print(f"  resolved.technique : {resolved.get('technique')!r}  (components: {comps})")
        print(f"  resolved.adjectives: {resolved.get('adjectives')}")
        print(f"  resolved.motion    : {resolved.get('motion')}")
        print(f"  keyframes={nkf}  kinds={[k.get('kind') for k in resp['recipe'].get('keyframes') or []]}"
              f"  frames={frames}  motion_rate_hz={resp['recipe'].get('motion_rate_hz')}")
        print(f"  flags: {[(f.get('word'), f.get('tier')) for f in resp.get('flags') or []]}")
        print(f"  dco_bake rc={run.returncode}  wav_samples={n}  sr={sr}  peak={peak:.5f}  rms={rms:.5f}"
              f"  finite={finite}  nonsilent={nonsilent}")
        if run.returncode != 0:
            print(f"  bake stderr: {run.stderr.strip()[:200]}")
        print(f"  VALIDATION: {'OK' if not problems else 'PROBLEMS: ' + '; '.join(problems)}")
        print(f"  -> {'PASS' if row_ok else 'FAIL'}")
        print(f"  files: {resp_path}\n         {wav_path}")
        rows.append((prompt, comps, resolved.get('adjectives'), resolved.get('motion'),
                     nkf, frames, wav_path, peak, rms, row_ok))

    print("\n" + "=" * 90)
    print("E2E RESULT:", "ALL PASS" if ok_all else "SOME FAILED")
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
