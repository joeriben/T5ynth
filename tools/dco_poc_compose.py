#!/usr/bin/env python3
"""PoC for the WHOLE approach: a SMALL model, given a rich vocabulary as an
INTERPRETATION AID, takes up synthesis methods, PARAMETRIZES them, and COMBINES
them -- OPEN-LOOP (no self-hearing crutch). The three verbs the DCO was accused
of exploiting to ZERO ("aufgreifen, parametrisieren, kombinieren").

  interpretation aid = the METHOD palette below, each method with its PARAMETERS
                       and what each parameter DOES to the sound (the DSP knowledge
                       the small model needs, handed to it as language). This is a
                       hand-written miniature of the offline orientation hypotheses.
  model task         = read the target -> SELECT 2-4 methods, SET their parameters
                       to match the target, ORDER them as a morph. No raw spectra.
  measure (NOT hear) = bake through the REAL dco::Baker, then measure DSP richness
                       (distinct methods, partials, flux, inharmonicity) + read back
                       CLAP tags. Measurement only -- the model never hears/corrects.

The point is NOT word-match. It is: does a small model, guided ONLY by a parametric
vocabulary, produce compositions that genuinely (a) vary parameters per target
[parametrised, not template defaults], (b) combine >=2 distinct methods, and (c)
bake to rich, moving, plausible timbres. That is the basis; self-hearing and product
wiring come AFTER and only if this holds.

  BAKE=<dco_bake> .venv/bin/python tools/dco_poc_compose.py [model]
"""
import os
import re
import sys
import json
import subprocess
from pathlib import Path

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "backend"))
sys.path.insert(0, os.path.dirname(__file__))
import lco_testbench as TB              # analyze() + _find_bake()
import dco_recipe as DR                 # validate_recipe -> bakeable
import clap_probe as CP                 # CLAP tags, MEASUREMENT only

OUT = os.path.join(os.path.dirname(__file__), "dco_poc_out")
CLAP_MODEL = "laion/clap-htsat-unfused"

# ─── The interpretation aid: methods x parameters x what each parameter DOES ──
# This is the "rich vocabulary" the small model reads. The parametric directions
# (ratio non-integer -> metallic; index up -> shriller; order up -> harsher) are
# the DSP knowledge the model would otherwise have to discover by ear -- handed to
# it as language so it can parametrise OPEN-LOOP.
PALETTE_AID = """fm2  -- FM synthesis. Parameters:
   ratio 1..8  : modulator/carrier frequency ratio. INTEGER (1,2,3)=harmonic, mellow, tonal.
                 NON-INTEGER (1.4, 2.7, 3.5)=inharmonic, metallic, bell-like, clangorous.
   index 0..8  : FM depth. 0=pure sine. LOW(1-2)=soft, few overtones.
                 HIGH(5-8)=bright, buzzy, harsh, many sidebands. Raise index to make it SHRILLER.
cheby -- waveshaping / distortion. Parameters:
   order 2..12 : LOW(2-3)=gentle, warm. HIGH(8-12)=harsh, edgy, screaming, aggressive.
   drive 0.1..1: input gain. HIGH=more bite / dirt.
ring  -- ring modulation. clangorous, bell, metallic, robotic. Parameters:
   ratio 1..8  : NON-INTEGER=inharmonic clang. mix 0..1=wet amount.
pulse -- pulse / PWM wave. Parameters:
   width 0.02..0.98 : 0.5=hollow square. THIN(<0.15)=nasal, reedy, bright, buzzy. FAT(>0.4)=rounder.
additive -- hand-drawn harmonic spectrum. Parameter:
   preset : sine | soft | warm | bright | hollow | organ | saw | square | triangle
            (soft/warm=mellow strong lows; bright=strong highs; hollow/organ=reedy odd harmonics)
shape 0..1 (OPTIONAL on ANY method): post distortion. Raise for grit, edge, dirt, aggression."""

SYS = (
    "You are a synthesis programmer for a wavetable oscillator. Read the target sound and "
    "COMPOSE it from the METHOD palette: SELECT 2 to 4 methods the timbre MORPHS through "
    "(start -> end), SET each method's parameters to match the target's character, and "
    "COMBINE different methods for a richer result. Let the parameter meanings guide you -- "
    "e.g. a shrill metallic sound wants fm2 with a NON-integer ratio and HIGH index; a warm "
    "mellow sound wants low index / soft presets, not bright ones.\n\n"
    "Reply ONLY the composition, one method per line, as:  method param=value param=value\n"
    "No prose, no numbering. Example for 'a harsh metallic stab':\n"
    "fm2 ratio=3.5 index=7\ncheby order=10 drive=0.9\nadditive preset=bright\n\n"
    "PALETTE:\n" + PALETTE_AID
)

# ─── Additive presets: named spectra so the model never authors raw partials ──
def _saw():   return [{"h": n, "a": round(1.0 / n, 3), "phase": 0.0} for n in range(1, 9)]
def _square():return [{"h": n, "a": round(1.0 / n, 3), "phase": 0.0} for n in range(1, 10, 2)]
def _tri():   return [{"h": n, "a": round(1.0 / (n * n), 3), "phase": 0.0} for n in range(1, 10, 2)]
_PRESET = {
    "sine":  [{"h": 1, "a": 1.0, "phase": 0.0}],
    "soft":  [{"h": 1, "a": 1.0}, {"h": 2, "a": 0.5}, {"h": 3, "a": 0.3}, {"h": 4, "a": 0.15}, {"h": 5, "a": 0.08}],
    "warm":  [{"h": 1, "a": 1.0}, {"h": 2, "a": 0.6}, {"h": 3, "a": 0.3}, {"h": 4, "a": 0.12}],
    "bright":[{"h": 1, "a": 1.0}, {"h": 2, "a": 0.9}, {"h": 3, "a": 0.85}, {"h": 4, "a": 0.7}, {"h": 5, "a": 0.6}, {"h": 6, "a": 0.5}, {"h": 7, "a": 0.4}],
    "hollow":[{"h": 1, "a": 1.0}, {"h": 3, "a": 0.5}, {"h": 5, "a": 0.33}, {"h": 7, "a": 0.25}],
    "organ": [{"h": 1, "a": 1.0}, {"h": 3, "a": 0.5}, {"h": 5, "a": 0.33}, {"h": 8, "a": 0.2}],
    "saw": _saw(), "square": _square(), "triangle": _tri(),
}
_METHODS = ("fm2", "cheby", "ring", "pulse", "additive")
# defaults let us DETECT parametrisation (a value != default == the model actually set it)
_DEFAULT = {"ratio": 2.0, "index": 1.0, "order": 4, "drive": 0.7, "width": 0.5,
            "mix": 1.0, "shape": 0.0, "preset": "soft"}


def ollama(model):
    import urllib.request
    def call(system, user, max_new=200):
        body = json.dumps({
            "model": model,
            "messages": [{"role": "system", "content": system},
                         {"role": "user", "content": user}],
            "stream": False, "think": False,
            "options": {"temperature": 0, "num_predict": int(max_new)},
        }).encode()
        req = urllib.request.Request("http://localhost:11434/api/chat", body,
                                     {"Content-Type": "application/json"})
        return json.load(urllib.request.urlopen(req, timeout=300))["message"]["content"]
    return call


_NUM = re.compile(r"^-?(?:\d+\.?\d*|\.\d+)$")


def parse_composition(raw):
    """Model reply -> list of (method, params, set_keys). set_keys = the params the
    model EXPLICITLY set (to detect real parametrisation vs. template defaults)."""
    comp = []
    for line in raw.splitlines():
        toks = line.strip().split()
        if not toks:
            continue
        method = toks[0].lower().strip(":,-*")
        if method not in _METHODS:
            continue
        params, set_keys = {}, []
        for t in toks[1:]:
            if "=" not in t:
                continue
            k, v = t.split("=", 1)
            k = k.lower().strip()
            v = v.strip().strip(",")
            if k == "preset":
                if v.lower() in _PRESET:
                    params["preset"] = v.lower(); set_keys.append("preset")
            elif k in ("ratio", "index", "order", "drive", "width", "mix", "shape") and _NUM.match(v):
                params[k] = float(v); set_keys.append(k)
        comp.append((method, params, set_keys))
        if len(comp) >= 4:
            break
    return comp


def build_keyframe(method, params):
    if method == "fm2":
        return {"kind": "fm2", "ratio": params.get("ratio", _DEFAULT["ratio"]),
                "index": params.get("index", _DEFAULT["index"]), "shape": params.get("shape", 0.0)}
    if method == "cheby":
        return {"kind": "cheby", "order": int(params.get("order", _DEFAULT["order"])),
                "drive": params.get("drive", _DEFAULT["drive"]), "shape": params.get("shape", 0.0)}
    if method == "ring":
        return {"kind": "ring", "ratio": params.get("ratio", _DEFAULT["ratio"]),
                "mix": params.get("mix", _DEFAULT["mix"]), "shape": params.get("shape", 0.0)}
    if method == "pulse":
        return {"kind": "pulse", "width": params.get("width", _DEFAULT["width"]),
                "shape": params.get("shape", 0.0)}
    # additive
    kf = {"kind": "additive", "partials": [dict(p) for p in _PRESET[params.get("preset", "soft")]]}
    if params.get("shape"):
        kf["shape"] = params["shape"]
    return kf


def build_recipe(comp, frames=256):
    kfs = [build_keyframe(m, p) for m, p, _ in comp] or [build_keyframe("additive", {})]
    motion = [{"to": 0, "dur_frac": 0.0, "curve": "lin"}]
    for i in range(1, len(kfs)):
        motion.append({"to": i, "dur_frac": 1.0, "curve": "lin"})
    recipe = {"keyframes": kfs, "motion": motion, "loop": False,
              "frames": frames, "motion_rate_hz": 0.25}
    recipe, _ = DR.validate_recipe(recipe)
    return recipe


def _fmt_params(method, params, set_keys):
    """Show what the model SET; mark defaults so parametrisation is visible."""
    if not set_keys:
        return "(no params -> all defaults)"
    return " ".join(f"{k}={params[k]}" for k in set_keys)


def main():
    model = sys.argv[1] if len(sys.argv) > 1 else "qwen3:8b"
    bake = TB._find_bake()
    if not bake:
        print("NO dco_bake. Set BAKE=<path>.", file=sys.stderr); return 2
    os.makedirs(OUT, exist_ok=True)
    call = ollama(model)

    from transformers import ClapModel, ClapProcessor
    prompts = [
        "a bright metallic bell",
        "a warm mellow electric piano",
        "a harsh aggressive buzzy synth lead",
        "a hollow wooden clarinet",
        "the sound of quiet regret",
        "1985 chrome highway at dusk",
        "a rusty gate creaking in the rain",
    ]
    print(f"model: {model}   methods: {list(_METHODS)}   prompts: {len(prompts)}\nloading CLAP ...")
    clap = ClapModel.from_pretrained(CLAP_MODEL).eval()
    proc = ClapProcessor.from_pretrained(CLAP_MODEL)
    CP.assert_model_sane(clap, proc, "cpu")
    vocab = list(dict.fromkeys(CP.NAIVE_VOCAB))
    vocab_emb = CP.embed_texts(clap, proc, vocab, "cpu")

    n_param, n_combine, n_plausible = 0, 0, 0
    for prompt in prompts:
        raw = call(SYS, f"Target sound: {prompt}")
        comp = parse_composition(raw)
        if not comp:
            print(f"\n### {prompt!r}\n  (model produced no valid methods) raw={raw[:120]!r}")
            continue
        slug = re.sub(r"[^a-z0-9]+", "_", prompt.lower()).strip("_")[:32]
        recipe = build_recipe(comp)
        wpath = os.path.join(OUT, slug + ".wav")
        json.dump(recipe, open(os.path.join(OUT, slug + ".json"), "w"))
        r = subprocess.run([bake, os.path.join(OUT, slug + ".json"), wpath],
                           capture_output=True, text=True)
        if r.returncode != 0 or not os.path.exists(wpath):
            print(f"\n### {prompt!r}\n  BAKE FAILED: {r.stderr.strip()[:140]}"); continue

        an = TB.analyze(wpath)
        flux = an["flux"]
        parts = max(m[1] for m in an["perframe"])
        methods = [m for m, _, _ in comp]
        distinct = len(set(methods))
        parametrised = sum(1 for _, _, sk in comp if any(k != "preset" for k in sk))
        a_emb = CP.embed_audios(clap, proc, [CP.load_audio_48k_mono(Path(wpath))], "cpu")
        _, tidx = CP.rank(a_emb, vocab_emb)
        tags = [vocab[tidx[0, j].item()] for j in range(5)]

        did_param = parametrised > 0
        did_combine = distinct >= 2
        alive = flux > 0.03
        n_param += did_param; n_combine += did_combine; n_plausible += (alive and parts >= 4)

        print(f"\n### {prompt!r}")
        for m, p, sk in comp:
            print(f"   {m:<9} {_fmt_params(m, p, sk)}")
        print(f"   select={distinct} distinct/{len(methods)}  parametrised={parametrised}/{len(comp)} stages"
              f"  {'MORPH' if did_combine else 'single-method'}")
        print(f"   bake: partials={parts}  flux={flux:.3f} {'MOVING' if alive else 'static'}  heard={tags}")

    n = len(prompts)
    print(f"\n{'='*70}\nSUMMARY ({model}, open-loop, no self-hearing):")
    print(f"  parametrised (set >=1 synth param): {n_param}/{n}")
    print(f"  combined (>=2 distinct methods)    : {n_combine}/{n}")
    print(f"  plausible bake (moving, >=4 parts) : {n_plausible}/{n}")
    print(f"WAVs: {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
