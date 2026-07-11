#!/usr/bin/env python3
"""
GEN-Wavetable PoC — Stufe 2, korrigiert: das LLM emittiert eine SPRACHE, die es
schon kann, statt ein bespoke Schema, das wir ihm beibringen muessen.

Die Kritik, die diesen Prototyp ausloest: eine eigene "deklarative Synth-Bau-
Sprache" (DcoRecipe) zu erfinden ist Unfug, wenn es sie seit Jahrzehnten gibt.
Also: Emit-Ziel = Csound GEN-Function-Table-Statements (Music-N-Linie, ~1960 ->
Csound) -- die kanonische deklarative Wavetable-Vokabel. Ein Coder/Instruct-LLM
hat sie im Training gesehen; kein Few-Shot-Zwang fuer eine Kunstsprache.

Was wir NICHT von Csound uebernehmen: den Renderer (kein libcsound-Dep). Wir
implementieren das kleine GEN-Subset selbst -- GEN10/GEN09 sind je ein paar
Zeilen. Wir adoptieren die SPRACHE, nicht die Engine.

Was UNSER Teil ist (die eigentliche Innovation): der Morph ueber die Frames.
GEN macht STATISCHE Einzelzyklen. Das LLM ordnet GEN-Keyframes in eine MORPH-
Trajektorie -> lebender Table. Reused vocabulary, novel authoring.

Verglichen wird gegen tools/bridge_poc_qwen.py (freies Bridge-Selektieren, das
scheiterte): dieselben 8 Prompts, dieselbe Frage -- bildet das Modell Woerter,
die KEINE Vokabel sind ("luminous", "aggressive", "muddy"), ueber die Bedeutung
auf die richtige Konstruktion ab?

Transport: run_instruct DIREKT importiert (kein IPC) -- gleiches Muster wie die
anderen tools/-Proben; der Shipping-Backend hat diesen Mode noch nicht.
"""
import os
import sys
import re
from pathlib import Path
import numpy as np

sys.path.insert(0, "/Users/joerissen/ai/t5ynth/tools")
sys.path.insert(0, "/Users/joerissen/ai/t5ynth/backend")
import bridge_poc as BP          # scan_render, write_wav, frame_liveness, FRAME_SIZE, NUM_FRAMES
import pipe_inference as P       # run_instruct, _resolve_translation_model_dir

N = BP.FRAME_SIZE                # 2048
NUM_FRAMES = BP.NUM_FRAMES       # 256
OUT_DIR = Path("/Users/joerissen/ai/t5ynth/tools/csound_gen_poc_out")

_X = 2.0 * np.pi * np.arange(N) / N   # one cycle of phase, [0, 2*pi)


# ─── Das GEN-Subset (unser leichter Renderer der Csound-Sprache) ───────────

def gen10(args):
    """GEN10: sum of harmonically-related SINES. arg k (1-based) = strength of
    harmonic k. saw = 1, .5, .33, ... ; square = 1, 0, .33, 0, ... (odd only)."""
    cyc = np.zeros(N)
    for k, s in enumerate(args, start=1):
        if s:
            cyc += float(s) * np.sin(k * _X)
    return cyc


def gen09(args):
    """GEN09: sum of sines by TRIPLES (partial#, strength, phase_degrees).
    partial# may be non-integer -> inharmonic (glassy/bell/metallic)."""
    cyc = np.zeros(N)
    for i in range(0, len(args) - 2, 3):
        pn, st, ph = float(args[i]), float(args[i + 1]), float(args[i + 2])
        if st:
            cyc += st * np.sin(pn * _X + np.deg2rad(ph))
    return cyc


def apply_shape(cyc, s):
    """Our 'dirty/distorted' post-shaper -- the SAME asymmetric tanh DcoBaker
    ships (adds even+odd harmonics = analog grit, not a hollowing symmetric
    tanh). s in [0,1], 0 = clean."""
    s = float(np.clip(s, 0.0, 1.0))
    if s <= 0.0:
        return cyc
    drive = 1.0 + s * 4.0
    bias = 0.2 * s
    y = np.tanh(drive * (cyc + bias))
    return y - y.mean()


_GEN = {10: gen10, 9: gen09}


def render_keyframe(gennum, args, shape=0.0):
    fn = _GEN.get(int(gennum))
    if fn is None:
        return None
    cyc = fn(args)
    cyc = cyc - cyc.mean()            # DC out (Baker contract)
    cyc = apply_shape(cyc, shape)
    return cyc


# ─── Unser Teil: der Morph ueber die Frames (die Innovation) ───────────────

def _curve(a, curve):
    a = np.clip(a, 0.0, 1.0)
    if curve == "fast":
        return a ** 0.4
    if curve == "slow":
        return a ** 2.5
    return a                          # lin


def morph_table(cycles, curve="lin"):
    """cycles = keyframe single-cycles in MORPH order. Linear-blend across
    NUM_FRAMES, curve-shaped local position. One keyframe -> static (dead) table:
    allowed, and frame_liveness will expose it as the anti-goal it is."""
    cycles = [c for c in cycles if c is not None]
    if not cycles:
        return None
    if len(cycles) == 1:
        return np.tile(cycles[0], (NUM_FRAMES, 1))
    segs = len(cycles) - 1
    table = np.zeros((NUM_FRAMES, N))
    for i in range(NUM_FRAMES):
        p = i / (NUM_FRAMES - 1)
        fpos = p * segs
        s = min(int(np.floor(fpos)), segs - 1)
        a = _curve(fpos - s, curve)
        table[i] = (1.0 - a) * cycles[s] + a * cycles[s + 1]
    peak = np.max(np.abs(table))
    if peak > 1e-9:
        table *= 0.9 / peak
    return table


# ─── Parser: Qwens Csound-Ausgabe -> Keyframes + Morph ─────────────────────

# f<N> <time> <size> <gennum> <args...>
_F = re.compile(r"^\s*f\s*(\d+)\s+[-\d.]+\s+\d+\s+(\d+)\s+(.*)$", re.I)


def _floats(s):
    # Accept leading-dot decimals (.5, .33) as well as 0.5 / 4.2 / -45 / 2048.
    # A digit-first-only pattern silently turns ".5" into "5" -> corrupt strengths,
    # and the SYS vocab line itself teaches the bare-dot form.
    return [float(t) for t in re.findall(r"-?(?:\d+\.?\d*|\.\d+)", s)]


def parse(raw):
    """-> (ordered_construction, meta_str). ordered_construction is a list of
    (gennum, args, shape). Only GEN10/GEN09 survive; junk is dropped, never
    invented. MORPH gives the order; SHAPE gives per-table drive."""
    keyframes = {}      # table# -> (gennum, args)
    shapes = {}         # table# -> shape amount
    morph = []          # ordered table#s
    for line in raw.splitlines():
        m = _F.match(line)
        if m:
            tbl, gennum, rest = int(m.group(1)), int(m.group(2)), m.group(3)
            if gennum in _GEN:
                keyframes[tbl] = (gennum, _floats(rest))
            continue
        ms = re.match(r"\s*SHAPE:?\s*(.+)", line, re.I)
        if ms:
            for tbl, val in re.findall(r"f\s*(\d+)\s*=\s*(-?(?:\d+\.?\d*|\.\d+))", ms.group(1), re.I):
                shapes[int(tbl)] = float(val)
            continue
        mm = re.match(r"\s*MORPH:?\s*(.+)", line, re.I)
        if mm:
            morph = [int(t) for t in re.findall(r"f\s*(\d+)", mm.group(1), re.I)]

    if not morph:
        morph = sorted(keyframes)                     # fallback: numeric order
    morph = [t for t in morph if t in keyframes]      # drop dangling refs
    construction = [(keyframes[t][0], keyframes[t][1], shapes.get(t, 0.0)) for t in morph]

    meta = " -> ".join(f"f{t}(G{keyframes[t][0]}{'+sh' if shapes.get(t) else ''})" for t in morph)
    return construction, meta


# ─── Der Autor-Prompt: das LLM schreibt Csound (few-shot, kein Kunst-Schema) ─

SYS = (
    "You are a sound designer who builds wavetables by writing Csound GEN "
    "function-table statements. Each f-statement is ONE single-cycle waveform "
    "(a keyframe). You then order keyframes into a MORPH so the wavetable "
    "EVOLVES across its frames -- a LIVING table, never one static cycle.\n\n"
    "Vocabulary (use ONLY this):\n"
    "  f<N> 0 2048 10 <s1> <s2> <s3> ...   GEN10: harmonic partials. s_k = strength of harmonic k.\n"
    "     saw = 1 0.5 0.33 0.25 0.2 ; square = 1 0 0.33 0 0.2 0 (odd only);\n"
    "     warm/dark = strong lows, rolled highs ; bright = boosted highs ; hollow = odd-heavy.\n"
    "  f<N> 0 2048 9 <p1> <str1> <ph1>  <p2> <str2> <ph2> ...   GEN09: partials by\n"
    "     (partial#, strength, phase-degrees). partial# MAY be non-integer -> inharmonic:\n"
    "     use for glassy / bell / metallic / crystalline shimmer.\n"
    "  SHAPE: f<N>=<0..1>   optional per-keyframe distortion (dirty/gritty/aggressive/screaming). 0=clean.\n"
    "  MORPH: f<A> f<B> f<C>   the ordered keyframes the table morphs through. USE AT LEAST TWO.\n\n"
    "Translate the prompt's MEANING into this vocabulary -- match by meaning, NOT by exact word: "
    "'luminous/crystalline' -> inharmonic highs (GEN09); 'aggressive/screaming' -> SHAPE; "
    "'opens up/evolves' -> morph dark->bright; 'mellow/soft' -> few low partials.\n\n"
    "Build the wavetable as a MOTION from a START timbre to an END timbre -- the "
    "keyframes are stops on that journey, and each MUST differ from the one "
    "before it (that difference IS the sound's movement). Read the journey from "
    "the prompt: 'pad that opens up' = dark start -> bright end; 'aggressive stab' "
    "= clean start -> distorted end (rising SHAPE); 'soft mellow' = few partials, "
    "little change. Use 2 or 3 keyframes. Keep each keyframe's list SHORT -- at "
    "most 8 numbers -- never a long shrinking tail.\n\n"
    "Reply in EXACTLY this shape, no other text:\n"
    "KEYFRAMES:\n"
    "<one f-statement per keyframe -- GEN10 or GEN09 as the sound needs>\n"
    "SHAPE: f<N>=<0..1>   (only for dirty keyframes; omit otherwise)\n"
    "MORPH: <ordered f names>"
)


# Dieselben 8 wie bridge_poc_qwen.py -- Woerter, die KEINE Vokabel sind.
PROMPTS = [
    "warm analog pad that slowly opens up",
    "luminous crystalline bell",
    "cold aggressive metallic stab",
    "brittle glass shimmer",
    "muddy dark drone",
    "hollow woody clarinet",
    "screaming bright lead",
    "soft mellow flute",
]


def _centroid(cycle):
    """Spectral centroid (harmonic-bin weighted) — objective brightness proxy."""
    mag = np.abs(np.fft.rfft(cycle))
    bins = np.arange(len(mag))
    tot = mag.sum()
    return float((bins * mag).sum() / tot) if tot > 1e-9 else 0.0


def main():
    # Swap the combiner model without touching run_instruct: LCO_MODEL_DIR points
    # at any HF causal-LM dir (the coder), else fall back to the translator Qwen.
    mdir = os.environ.get("LCO_MODEL_DIR") or P._resolve_translation_model_dir({})
    dev = "mps"
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    print(f"translator: {mdir}\n")
    print(f"{'prompt':38} {'live':7}  construction")
    print("-" * 100)
    results = []  # (prompt, table) for the discrimination summary
    for i, prompt in enumerate(PROMPTS, 1):
        raw = P.run_instruct(prompt, mdir, dev, SYS, max_new_tokens=220)
        construction, meta = parse(raw)
        cycles = [render_keyframe(g, a, sh) for (g, a, sh) in construction]
        table = morph_table(cycles)
        name = f"gen_{i:02d}_" + re.sub(r"[^a-z]+", "_", prompt.lower())[:24].strip("_")
        if table is None:
            print(f"{prompt:38} {'--':>7}  (no valid GEN emitted)")
            print(f"    RAW: {raw.strip()!r}")
            continue
        audio = BP.scan_render(table, scan="sweep")
        BP.write_wav(OUT_DIR / f"{name}.wav", audio)
        live = BP.frame_liveness(table)
        results.append((prompt, table))
        print(f"{prompt:38} {live:7.4f}  {meta or '(none)'}")
        print(f"    RAW: {raw.strip()!r}")
    print(f"\nWAVs -> {OUT_DIR}  (gen_*.wav)")

    # ── The real metric: does MEANING map to a DISTINCT construction? ──
    # frame_liveness measures per-frame wiggle; it CANNOT see two prompts that
    # produced the SAME table. Centroid sweep + cross-prompt RMS can.
    print("\ndiscrimination — centroid sweep + cross-prompt distinctness")
    print(f"{'prompt':26} {'cen[0]':>7} {'cen[N]':>7} {'sweepΔ':>7}")
    for prompt, t in results:
        c0, c1 = _centroid(t[0]), _centroid(t[-1])
        print(f"{prompt[:26]:26} {c0:7.2f} {c1:7.2f} {abs(c1 - c0):7.2f}")
    parent = list(range(len(results)))

    def _find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    pairs = []
    for a in range(len(results)):
        for b in range(a + 1, len(results)):
            d = float(np.sqrt(((results[a][1] - results[b][1]) ** 2).mean()))
            if d < 0.02:
                pairs.append((results[a][0], results[b][0]))
                parent[_find(a)] = _find(b)
    clusters = len({_find(i) for i in range(len(results))}) if results else 0
    print("\nnear-identical (RMS<0.02): "
          + ("; ".join(f"{x[:12]}=={y[:12]}" for x, y in pairs) if pairs else "none"))
    print(f"distinct constructions: {clusters} / {len(results)}")


if __name__ == "__main__":
    main()
