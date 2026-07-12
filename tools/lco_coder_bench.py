#!/usr/bin/env python3
"""LCO CODER head-to-head — does an LLM-authored-spectrum LCO work with a
CAPABLE model? Same real-baker gate as tools/lco_testbench.py, but the recipe
is authored by the PER-STATION CODER path (plan 2-3 timbre stations, emit one
Csound GEN spectrum each) driven by an ollama model of the caller's choosing.

The maintainer's question: the 3B coder failed — but is that the APPROACH or
the MODEL? This runs the identical prompts through bigger models (ollama:
mistral-nemo ~7B, gemma3:27b ~30B, mistral-medium ~80B) and bakes them through
the REAL dco::Baker -> WavetableOscillator, scoring plausibility with the same
gate. No lco_author.py edit — this file owns an IMPROVED spectrum prompt (the
3B/gemma GEN9-vs-GEN10 confusion was a PROMPT bug) and reuses lco_author's
parsers + lco_testbench's analysis so the comparison is apples-to-apples.

Run from repo root (ollama serving on :11434):
  BAKE=<dco_bake>  .venv/bin/python tools/lco_coder_bench.py gemma3:27b
"""
import os, sys, re, json, urllib.request, subprocess

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "backend"))
sys.path.insert(0, os.path.dirname(__file__))
import lco_author as LA               # parsers + keyframe/motion helpers (legacy path)
import lco_testbench as TB            # analyze() + gate() + _find_bake() + PROMPTS

# Station planning that PRESERVES literal waveform/instrument names (the 27B run
# paraphrased "square" -> "harsh digital pulse", losing the exactness the
# maintainer demands). Only descriptive prompts get invented mood phrases.
STATION_SYS_V2 = (
    "Break the described sound into the 2-3 TIMBRAL STATIONS it passes through, "
    "start to end. Reply ONLY the station names, one per line — no numbering, no "
    "other text.\n"
    "RULE: if the prompt names specific waveforms or instruments (sine, square, "
    "saw, sawtooth, triangle, pulse, violin, piano, bell, organ, clarinet, "
    "flute, ...), use those EXACT words as the stations, IN ORDER — never "
    "paraphrase them into moods. Invent a short descriptive phrase ONLY for a "
    "purely descriptive prompt that names no waveform/instrument.\n"
    "'square > sine' ->\nsquare\nsine\n"
    "'saw into triangle' ->\nsaw\ntriangle\n"
    "'a swell from muffled to piercing' ->\nmuffled dark\npiercing bright"
)

OUT_ROOT = os.path.join(os.path.dirname(__file__), "lco_coder_out")

# Station planning: reuse lco_author.STATION_SYS (it decomposes fine). The fix is
# the SPECTRUM prompt — make the routine choice explicit (GEN10 for HARMONIC,
# GEN09 ONLY for inharmonic) with COMPLETE examples carrying their routine number,
# so a model can't mislabel a square's odd-harmonic amplitudes as routine 9.
SPECTRUM_SYS_V2 = (
    "You write ONE Csound GEN function-table statement: a single-cycle waveform "
    "for the given timbre. Reply with ONLY the f1 line (plus an optional SHAPE "
    "line). No prose, no code fences, no reasoning.\n\n"
    "Choose the routine:\n"
    "* HARMONIC timbre (sine, saw, square, triangle, pulse, strings, reed, organ, "
    "warm, bright, hollow, ...) -> routine 10 (GEN10): a list of harmonic "
    "strengths, one per harmonic starting at the fundamental.\n"
    "    sine:     f1 0 2048 10 1\n"
    "    saw:      f1 0 2048 10 1 0.5 0.33 0.25 0.2 0.166 0.142 0.125\n"
    "    square:   f1 0 2048 10 1 0 0.333 0 0.2 0 0.142 0 0.111   (odd only; evens = 0)\n"
    "    triangle: f1 0 2048 10 1 0 0.111 0 0.04 0 0.02           (odd, steep rolloff)\n"
    "    warm/dark:f1 0 2048 10 1 0.6 0.3 0.15 0.08 0.04 0.02     (strong lows, rolled highs)\n"
    "    bright:   f1 0 2048 10 1 0.9 0.85 0.8 0.7 0.6 0.5 0.4    (strong highs)\n"
    "    hollow:   f1 0 2048 10 1 0 0.5 0 0.33 0 0.25             (odd-heavy, reedy)\n"
    "* INHARMONIC timbre ONLY (bell, metallic, glassy, crystalline, clangorous) "
    "-> routine 9 (GEN09): triples partial#,strength,phase-degrees; a NON-integer "
    "partial# is the inharmonic partial.\n"
    "    bell:     f1 0 2048 9 1 1 0 2.76 0.55 0 5.40 0.33 0 8.93 0.16 0\n"
    "    metallic: f1 0 2048 9 1 1 0 1.73 0.7 0 3.19 0.5 0 4.51 0.3 0\n\n"
    "Add 'SHAPE: f1=<0..1>' on a second line ONLY if dirty / distorted / "
    "aggressive / screaming. Use up to ~10 numbers. Match the timbre's brightness "
    "and harmonic makeup. Output the f1 line now."
)


def ollama_llm(model, warm=True):
    def call(text, system_prompt, max_new_tokens=160):
        body = json.dumps({
            "model": model,
            "messages": [{"role": "system", "content": system_prompt},
                         {"role": "user", "content": text}],
            "stream": False,
            "think": False,                       # ignored by non-reasoning models
            "options": {"temperature": 0, "num_predict": int(max_new_tokens)},
        }).encode()
        req = urllib.request.Request("http://localhost:11434/api/chat", body,
                                     {"Content-Type": "application/json"})
        return json.load(urllib.request.urlopen(req, timeout=300))["message"]["content"]
    if warm:
        try:
            call("sine", SPECTRUM_SYS_V2, 32)     # pay the cold-load once
        except Exception as e:
            print(f"warm-up failed: {e}", file=sys.stderr)
    return call


def author_via_coder(prompt, llm, frames=None):
    """The legacy per-station coder path, with the improved station + spectrum
    prompts. Returns (recipe, kept_stations, per_station_debug)."""
    raw_st = llm(prompt, STATION_SYS_V2, 80)
    stations = []
    for line in raw_st.splitlines():
        t = re.sub(r"^\s*(?:[-*]|\d+[.)])?\s*", "", line).strip()
        if t and any(c.isalpha() for c in t):
            stations.append(t)
    stations = stations[:3] if stations else [prompt]
    keyframes, kept, dbg = [], [], []
    for s in stations:
        raw = llm(s, SPECTRUM_SYS_V2, 160)
        parsed = LA._parse_one_spectrum(raw)
        kf = LA._spectrum_to_keyframe(parsed) if parsed else None
        genline = next((l.strip() for l in raw.splitlines() if l.strip().lower().startswith("f")), "(no f-line)")
        dbg.append((s, kf is not None, genline[:74]))
        if kf is not None:
            keyframes.append(kf); kept.append(s)
        if len(keyframes) >= LA._MAX_KEYFRAMES:
            break
    if not keyframes:
        kf = LA._empty_keyframe(); kf["partials"] = [{"h": 1.0, "a": 1.0, "phase": 0.0}]
        keyframes = [kf]; kept = list(stations)
    recipe = {"keyframes": keyframes, "motion": LA._build_motion(len(keyframes)),
              "loop": False, "frames": int(frames) if frames else LA.NUM_FRAMES,
              "motion_rate_hz": LA._MOTION_RATE_DEFAULT}
    return recipe, kept, dbg


def main():
    model = sys.argv[1] if len(sys.argv) > 1 else "gemma3:27b"
    bake = TB._find_bake()
    if not bake:
        print("NO dco_bake. Set BAKE=<path>.", file=sys.stderr); return 2
    outdir = os.path.join(OUT_ROOT, model.replace(":", "_").replace("/", "_"))
    os.makedirs(outdir, exist_ok=True)
    print(f"model: {model}   bake: {bake}\nout: {outdir}\n")

    llm = ollama_llm(model)
    npass = 0
    for i, prompt in enumerate(TB.PROMPTS, 1):
        slug = f"{i:02d}_{TB._slug(prompt)}"
        recipe, kept, dbg = author_via_coder(prompt, llm)
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
        print(f"{'PASS' if ok else '*** FAIL ***'}  [{slug}]  {prompt!r}   ({nkf} keyframe(s))")
        for (s, okk, gl) in dbg:
            print(f"      station {s!r}: {'PARSED' if okk else 'DROPPED'}  {gl}")
        print(f"    trajectory: centroid spread={an['morph_range']:.2f}  flux={an['flux']:.3f}  "
              f"({'ALIVE' if an['flux'] > 0.012 else 'static'})")
        for n in notes:
            print(f"      - {n}")
        npass += 1 if ok else 0
    print(f"\n==== {model}: {npass}/{len(TB.PROMPTS)} PASS the plausibility gate ====")
    print(f"WAVs: {outdir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
