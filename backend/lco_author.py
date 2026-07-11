#!/usr/bin/env python3
"""LCO author — per-station Csound-GEN wavetable recipe.

Replaces the lexicon/S2 author (dco_recipe.py). A small coder LLM does the two
narrow things it reliably does — (1) decompose a prompt into 2-3 TIMBRAL
STATIONS, (2) emit ONE Csound GEN spectrum per station — and this module's
deterministic code turns each spectrum into an Additive keyframe and the station
order into a Motion trajectory. The SHIPPING DcoBaker then bakes the exact same
time-domain morph the PoC validated (tools/csound_gen_poc.py): the baker's
per-frame blend (1-a)*from + a*to IS our morph (DcoBaker.cpp), so no DSP lives
here — only the GEN->keyframe projection.

Per-station INDEPENDENCE is the liveness guarantee: each station is a separate
model call with its own phrase, so identical keyframes are structurally
impossible unless the prompt genuinely names one static timbre.

GEN -> partials is DIRECT and exact (no rendering, no FFT): DcoBaker::renderAdditive
sums a*sin(h*x + phase) with a FLOAT harmonic number h, so
  GEN10 arg k        -> partial (h=k,        a=strength, phase=0)
  GEN09 triple p,s,d -> partial (h=p,        a=s,        phase=radians(d))
and GEN09's non-integer p is an INHARMONIC partial the baker renders natively
(glassy/metallic/bell). This reproduces the PoC single cycle verbatim.

Torch-free / numpy-free by contract (stdlib only): the ONE model dependency is
the injected ``llm`` callable ``(text, system_prompt, max_new_tokens) -> str``,
supplied by pipe_inference.py, so the whole GEN->recipe path unit-tests
in-process with no model load.

Determinism: stdlib + a greedy ``llm`` -> byte-identical recipe for identical
text. The translator already assumes MPS greedy determinism; the coder is
empirically 3x byte-identical (tools/csound_gen_poc.py restore check).
"""
import math
import re

# Baked wavetable frames per recipe (DcoBaker clamps to [8, 256]); matches the PoC.
NUM_FRAMES = 256

# Caps mirrored from src/dsp/DcoRecipeJson.h (the C++ parser hard-caps these; we
# stay strictly under so every emitted partial/keyframe/segment survives).
_MAX_KEYFRAMES = 8          # DcoRecipeJson kMaxKeyframes
_MAX_PARTIALS = 120         # < kMaxPartials (128); a GEN statement emits <= 8 anyway
_MOTION_RATE_DEFAULT = 0.25  # Hz — the dco template default (backend clamp [0.02, 8.0])


def _clip01(v):
    return 0.0 if v < 0.0 else (1.0 if v > 1.0 else v)


# ─── GEN spectrum -> one Additive keyframe (direct symbolic mapping) ──────────

def _empty_keyframe():
    """A full Additive keyframe carrying the SAME defaults DcoRecipeJson applies,
    so the unused (non-additive) fields are explicit and byte-stable."""
    return {"kind": "additive", "partials": [], "width": 0.5, "ratio": 2.0,
            "index": 1.0, "order": 2, "drive": 1.0, "mix": 1.0, "shape": 0.0}


def _spectrum_to_keyframe(parsed):
    """(gennum, args, shape) -> one Additive keyframe, or None if it is empty.
    GEN10 arg k -> partial (h=k, a=strength, phase=0). GEN09 triple
    (partial#, strength, phase_deg) -> partial (h=partial#, a=strength,
    phase=radians) — a non-integer partial# is an inharmonic partial the baker
    renders natively. ``shape`` rides the keyframe (DcoBaker::applyShape mirrors
    the PoC apply_shape), NEVER pre-applied here (that would double-shape)."""
    gennum, args, shape = parsed
    parts = []
    if gennum == 10:
        for k, s in enumerate(args, start=1):
            if s:
                parts.append({"h": float(k), "a": float(s), "phase": 0.0})
    elif gennum == 9:
        for i in range(0, len(args) - 2, 3):
            pn, st, ph = float(args[i]), float(args[i + 1]), float(args[i + 2])
            if st:
                parts.append({"h": pn, "a": st, "phase": math.radians(ph)})
    else:
        return None
    if not parts:
        return None
    kf = _empty_keyframe()
    kf["partials"] = parts[:_MAX_PARTIALS]
    kf["shape"] = _clip01(float(shape))
    return kf


# ─── Parser: one station's model reply -> one GEN spectrum ────────────────────
# Ported verbatim from tools/csound_gen_poc.py (the validated PoC).

# f<N> <time> <size> <gennum> <args...>
_F = re.compile(r"^\s*f\s*(\d+)\s+[-\d.]+\s+\d+\s+(\d+)\s+(.*)$", re.I)
_GEN_NUMS = (9, 10)


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
        if m and int(m.group(2)) in _GEN_NUMS:
            gennum, args = int(m.group(2)), _floats(m.group(3))[:24]
            break
    if gennum is None or not args:
        return None
    shape = 0.0
    ms = re.search(r"SHAPE:?\s*(?:f\s*\d+\s*=\s*)?(-?(?:\d+\.?\d*|\.\d+))", raw, re.I)
    if ms:
        shape = float(ms.group(1))
    return gennum, args, shape


# ─── The two narrow model tasks (one call each) ───────────────────────────────
# System prompts ported verbatim from the validated PoC.

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


def _plan_stations(prompt, llm):
    """Model -> ordered list of 2-3 short timbre phrases (planning, its strength).
    Falls back to the prompt itself as a single station if nothing usable comes."""
    raw = llm(prompt, STATION_SYS, 64)
    stations = []
    for line in raw.splitlines():
        # Strip only a real leading list-marker ("-", "*", "1.", "1)"), NOT a
        # digit run inside a legit phrase ("80s brass" must not become "s brass").
        t = re.sub(r"^\s*(?:[-*]|\d+[.)])?\s*", "", line).strip()
        if t and any(c.isalpha() for c in t):
            stations.append(t)
    return stations[:3] if stations else [prompt]


def _emit_spectrum(phrase, llm):
    """Model -> one GEN spectrum for one station phrase (the narrow task it can do)."""
    raw = llm(phrase, SPECTRUM_SYS, 96)
    return _parse_one_spectrum(raw)


# ─── The morph as a Motion trajectory the shipping Baker walks ────────────────

def _build_motion(n):
    """Station keyframes 0..n-1 walked as equal one-pass segments. motion[0] is
    the start point (its dur_frac is ignored by DcoBaker::bake); each subsequent
    entry is a segment to that keyframe. One station -> a single static point
    (the baker collapses it to a static table — right for a non-evolving timbre)."""
    if n <= 1:
        return [{"to": 0, "dur_frac": 1.0, "curve": "lin"}]
    motion = [{"to": 0, "dur_frac": 0.0, "curve": "lin"}]
    for i in range(1, n):
        motion.append({"to": i, "dur_frac": 1.0, "curve": "lin"})
    return motion


def author_recipe(text, llm, frames=None):
    """prompt -> (recipe dict, kept station phrases). The recipe is the exact
    schema DcoRecipeJson.h parses; the shipping DcoBaker bakes it."""
    stations = _plan_stations(text, llm)
    keyframes, kept = [], []
    for phrase in stations:
        parsed = _emit_spectrum(phrase, llm)
        kf = _spectrum_to_keyframe(parsed) if parsed else None
        if kf is not None:
            keyframes.append(kf)
            kept.append(phrase)
        if len(keyframes) >= _MAX_KEYFRAMES:
            break

    if not keyframes:
        # Nothing parsed (the model went off-format for every station). One plain
        # sine keeps the baker from ever seeing an empty recipe; the response
        # still reports the (unrendered) stations so the UI shows the reading.
        kf = _empty_keyframe()
        kf["partials"] = [{"h": 1.0, "a": 1.0, "phase": 0.0}]
        keyframes = [kf]
        kept = list(stations)

    recipe = {
        "keyframes": keyframes,
        "motion": _build_motion(len(keyframes)),
        "loop": False,                      # one-pass sweep through the stations
        "frames": int(frames) if frames else NUM_FRAMES,
        "motion_rate_hz": _MOTION_RATE_DEFAULT,
    }
    return recipe, kept


def build_lco_response(text, llm, frames=None):
    """The response dict pipe_inference sends to C++. ``recipe`` is the hard DSP
    contract (DcoRecipeJson.h). The remaining fields are the MINIMAL Phase-1
    projection onto what PromptPanel reads today (machineReading + the two-tier
    flag panel + Re-Prompt) so the C++ side keeps working unchanged: the station
    decomposition surfaces via ``resolved.adjectives``; ``flags`` is empty
    (there is no lexicon 'not-understood/adapted' notion in this author) rather
    than fabricating tiers. The honest station-as-reading disclosure is Phase 3."""
    recipe, stations = author_recipe(text, llm, frames)
    multi = len(recipe["keyframes"]) > 1
    return {
        "ok": True,
        "recipe": recipe,
        "resolved": {
            "technique": "additive",
            "adjectives": list(stations),
            "composition": [],
            "fm": [],
            "motion": ["morph"] if multi else [],
            "values": {},
        },
        "flags": [],
        "reference_vocabulary": [],
    }
