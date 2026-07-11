#!/usr/bin/env python3
"""
GEN-Wavetable PoC — Stufe 3: per-station authoring, deterministic morph.

Vorgeschichte: das kleine LLM (Qwen) EMITTIERT zuverlaessig ein einzelnes
Ziel-Timbre als Csound GEN-Statement (Semantik ist da -- Zentroid trackt die
Bedeutung), aber es KOMPONIERT keine zuverlaessige spektrale Bewegung zwischen
mehreren Keyframes: in einem Schuss wiederholt es die Partial-Liste und variiert
nur die SHAPE -> tote Tables. Drei Prompt-Varianten, dasselbe Scheitern.

Der Fix (User): "das Modell pro Station fahren und dann deterministisch damit
weiterarbeiten." Also:
  1. Das Modell zerlegt den Prompt in 2-3 TIMBRAL STATIONS (Wortphrasen).
  2. Das Modell emittiert PRO STATION ein einzelnes GEN-Spektrum -- die enge
     Frage, die es kann.
  3. Der Code morpht zwischen den Stationen -> lebender Table, GARANTIERT
     (identische Keyframes sind strukturell unmoeglich: jede Station ist ein
     eigener Aufruf mit eigener Beschreibung).

Reused vocabulary (Csound GEN), novel authoring (per-station + unser Morph).
Modell = Timbre, Code = Morph -- "der Morph bleibt unsere Innovation."

Transport: run_instruct DIREKT importiert (kein IPC) -- gleiches Muster wie die
anderen tools/-Proben. LCO_MODEL_DIR waehlt das Modell (Coder), sonst Translator.
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
    """cycles = station single-cycles in order. Linear-blend across NUM_FRAMES,
    curve-shaped local position. One station -> static table (appropriate for a
    sound that does not evolve). Distinct stations -> guaranteed motion."""
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


def _centroid(cycle):
    """Spectral centroid (harmonic-bin weighted) — objective brightness proxy."""
    mag = np.abs(np.fft.rfft(cycle))
    bins = np.arange(len(mag))
    tot = mag.sum()
    return float((bins * mag).sum() / tot) if tot > 1e-9 else 0.0


# ─── Parser: eine Station -> ein Spektrum ──────────────────────────────────

# f<N> <time> <size> <gennum> <args...>
_F = re.compile(r"^\s*f\s*(\d+)\s+[-\d.]+\s+\d+\s+(\d+)\s+(.*)$", re.I)


def _floats(s):
    # Accept leading-dot decimals (.5, .33) as well as 0.5 / 4.2 / -45 / 2048.
    # A digit-first-only pattern silently turns ".5" into "5" -> corrupt strengths.
    return [float(t) for t in re.findall(r"-?(?:\d+\.?\d*|\.\d+)", s)]


def _parse_one_spectrum(raw):
    """One station's model reply -> ONE single-cycle spectrum. Takes the FIRST
    valid GEN f-statement + an optional SHAPE. Returns (gennum, args, shape) or
    None. args capped so a runaway list can't build a pathological cycle."""
    gennum = args = None
    for line in raw.splitlines():
        m = _F.match(line)
        if m and int(m.group(2)) in _GEN:
            gennum, args = int(m.group(2)), _floats(m.group(3))[:24]
            break
    if gennum is None or not args:
        return None
    shape = 0.0
    ms = re.search(r"SHAPE:?\s*(?:f\s*\d+\s*=\s*)?(-?(?:\d+\.?\d*|\.\d+))", raw, re.I)
    if ms:
        shape = float(ms.group(1))
    return gennum, args, shape


# ─── Die zwei engen Modell-Aufgaben (je ein Aufruf) ────────────────────────

STATION_SYS = (
    "You are a sound designer. Break the described sound into 2 or 3 TIMBRAL "
    "STATIONS -- the key points the timbre passes through from start to end. "
    "Each station is a SHORT phrase (2-5 words) naming that timbre. If the sound "
    "barely evolves, give 2 near-identical stations. Reply ONLY the phrases, one "
    "per line, ordered start to end -- no numbering, no other text.\n\n"
    "Example -- 'a swell from muffled to piercing':\n"
    "muffled and dark\npiercing and bright"
)

SPECTRUM_SYS = (
    "You are a sound designer writing ONE Csound GEN function-table statement -- "
    "a single-cycle waveform matching a timbre.\n"
    "  f1 0 2048 10 <s1> <s2> ...   GEN10: harmonic partial strengths "
    "(saw = 1 0.5 0.33 0.25; square = 1 0 0.33 0 0.2; warm/dark = strong lows, "
    "rolled highs; bright = strong highs; hollow = odd-heavy).\n"
    "  f1 0 2048 9 <p1> <str1> <ph1> ...   GEN09: partials by (partial#, strength, "
    "phase-degrees); a NON-integer partial# is inharmonic -> glassy / metallic / "
    "bell / crystalline.\n"
    "Add a second line 'SHAPE: f1=<0..1>' ONLY if the timbre is dirty / distorted "
    "/ aggressive / screaming.\n"
    "Use at most 8 numbers. Reply ONLY the f1 line (and optional SHAPE line), "
    "nothing else."
)


def _plan_stations(prompt, mdir, dev):
    """Model -> ordered list of 2-3 short timbre phrases (planning, its strength).
    Falls back to the prompt itself as a single station if nothing usable comes."""
    raw = P.run_instruct(prompt, mdir, dev, STATION_SYS, max_new_tokens=64)
    stations = []
    for line in raw.splitlines():
        # Strip only a real leading list-marker ("-", "*", "1.", "1)"), NOT a
        # digit run inside a legit phrase ("80s brass" must not become "s brass").
        t = re.sub(r"^\s*(?:[-*]|\d+[.)])?\s*", "", line).strip()
        if t and any(c.isalpha() for c in t):
            stations.append(t)
    return stations[:3] if stations else [prompt]


def _emit_spectrum(phrase, mdir, dev):
    """Model -> one GEN spectrum for one station phrase (the narrow task it can do)."""
    raw = P.run_instruct(phrase, mdir, dev, SPECTRUM_SYS, max_new_tokens=96)
    return _parse_one_spectrum(raw)


# Dieselben 8 wie zuvor -- Woerter, die KEINE Vokabel sind.
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


def main():
    # Swap the combiner model without touching run_instruct: LCO_MODEL_DIR points
    # at any HF causal-LM dir (the coder), else fall back to the translator Qwen.
    mdir = os.environ.get("LCO_MODEL_DIR") or P._resolve_translation_model_dir({})
    dev = "mps"
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    print(f"model: {mdir}\n")

    results = []  # (prompt, table) for the discrimination summary
    for i, prompt in enumerate(PROMPTS, 1):
        stations = _plan_stations(prompt, mdir, dev)
        cycles, tags = [], []
        for s in stations:
            parsed = _emit_spectrum(s, mdir, dev)
            if parsed is None:
                tags.append(f"{s!r}:??")
                continue
            cycles.append(render_keyframe(*parsed))
            tags.append(f"{s!r}:G{parsed[0]}{'+sh' if parsed[2] else ''}")
        table = morph_table(cycles)
        name = f"gen_{i:02d}_" + re.sub(r"[^a-z]+", "_", prompt.lower())[:24].strip("_")
        print(f"[{i}] {prompt}")
        print(f"    stations: {'  ->  '.join(tags) if tags else '(none)'}")
        if table is None:
            print("    (no valid spectra)\n")
            continue
        audio = BP.scan_render(table, scan="sweep")
        BP.write_wav(OUT_DIR / f"{name}.wav", audio)
        results.append((prompt, table))
        print(f"    liveness={BP.frame_liveness(table):.4f}\n")
    print(f"WAVs -> {OUT_DIR}  (gen_*.wav)")

    # ── Discrimination: does MEANING map to a DISTINCT, LIVING construction? ──
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
