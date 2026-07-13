#!/usr/bin/env python3
"""LCO author — routes to the deterministic dco_recipe author.

build_lco_response() (mode=="dco" in pipe_inference.py) delegates recipe
authoring to dco_recipe.author_recipe(), per docs/DCO_LLM_GUARDRAILS.md: "the
LLM never authors DSP data. It only routes." The injected coder ``llm`` is
used ONLY for S2 residue-routing — mapping prompt words the curated lexicon
doesn't already cover to the nearest allowed lexicon key (or NONE), through a
closed-choice enum check that is itself the sandbox. It no longer plans
timbre stations or authors Csound GEN spectra.

The per-station Csound-GEN coder path that previously authored spectra
directly is retained unchanged below a banner comment as dead code (a
possible Phase-2 residue-fallback candidate); it is not on the shipping
response path. It was replaced because the coder emitted unparseable/wrong
GEN lines for basic waveforms and byte-identical spectra for distinct
timbres.
"""
import math
import re


def build_lco_response(text, llm, frames=None):
    """The response dict pipe_inference sends to C++ for mode=="dco". Delegates
    authoring to dco_recipe.author_recipe (the deterministic, regression-tested
    author) per docs/DCO_LLM_GUARDRAILS.md: the LLM never authors DSP data, it
    only routes. ``llm`` (the injected coder) is used ONLY as the S2
    residue-router via the ``llm_route`` adapter below. Returns
    author_recipe's dict verbatim: {ok, recipe, resolved, flags,
    lexicon_version, reference_vocabulary}."""
    from dco_recipe import author_recipe

    def llm_route(residue_words, allowed_keys):
        """S2 residue-routing (docs/DCO_LLM_GUARDRAILS.md S2): the coder maps each
        unmapped word to the nearest curated lexicon key, or NONE. Closed-choice —
        the enum check is the sandbox; it never authors DSP numbers."""
        system_prompt = (
            "You map sound-descriptor words to a fixed vocabulary. Reply with one "
            "line per input word, exactly \"word -> key\". key must be one of the "
            "allowed keys. If no key fits, use NONE. No other text."
        )
        user_prompt = ("Words: " + ", ".join(residue_words) + "\n"
                       "Allowed keys: " + ", ".join(allowed_keys))
        max_new = min(96, 8 * len(residue_words))
        try:
            raw = llm(user_prompt, system_prompt, max_new)
        except Exception:
            return {w: None for w in residue_words}   # deterministic degradation

        allowed_set = set(allowed_keys)
        lower_to_word = {w.lower(): w for w in residue_words}
        line_re = re.compile(r"^(\S.*?)\s*->\s*([A-Z_a-z]+)$")
        routed = {w: None for w in residue_words}
        for line in raw.splitlines():
            m = line_re.match(line.strip())
            if not m:
                continue
            word = lower_to_word.get(m.group(1).strip().lower())
            if word is None:
                continue
            key = m.group(2).strip().lower()   # lexicon keys are lowercase; LLM may echo "BRIGHT"
            routed[word] = key if key in allowed_set else None   # enum IS the sandbox
        return routed

    resp = author_recipe(text, llm_route=llm_route, frames=frames)
    return _apply_analog_life(resp, text)


# ─── Analog life: a charactered-but-static table drifts alive ─────────────────
# The maintainer's bridge_poc rule (tools/bridge_poc.py): a bare geometric base
# (saw/square/tri) with NO character and NO motion stays static; anything with
# CHARACTER lives via a tiny deterministic per-partial drift across the frames —
# "die kleine Frequenz-/Timbre-Abweichung zwischen den Waves" that turns 256
# identical copies into a living table. dco_recipe composes the spectral
# character statically; this pass adds the MOVEMENT, so dco_recipe (and its test
# suite) stay byte-identical. Deterministic (math.sin of frame+partial index),
# stdlib-only, and re-validated through dco_recipe so the recipe stays bakeable.

_ANALOG_CUES = ("analog", "analogue", "vintage", "living", "alive", "drift", "drifting")
_GRIT_CUES = ("gritty", "grit", "dirty", "rough", "harsh", "raw", "crunchy")
_GOLDEN = 2.399963229728653   # radians — decorrelates successive partials


def _drift_frames(base_partials, n, amt, pdev):
    """n keyframes = base_partials + a small deterministic per-partial deviation,
    sampled on a CLOSED sine cycle (frame n-1 -> frame 0 continues the sine, so a
    looping table wraps click-free). Partial index i is offset by the golden angle
    so partials wander out of lockstep — analog drift, not a global tremolo."""
    frames = []
    for j in range(n):
        theta = 2.0 * math.pi * j / n
        parts = []
        for i, p in enumerate(base_partials):
            # h/a read defensively (like phase, and like the inharmonic guard in
            # _apply_analog_life): base_partials is always post-validate_recipe here
            # so both keys are present, but a live instrument must never crash a
            # drift pass on a malformed partial — default to a silent fundamental.
            a = p.get("a", 0.0) * (1.0 + amt * math.sin(theta + _GOLDEN * i))
            ph = p.get("phase", 0.0) + pdev * math.sin(theta + 1.7 * i + 0.5)
            a = 0.0 if a < 0.0 else (1.0 if a > 1.0 else a)
            parts.append({"h": p.get("h", 1.0), "a": a, "phase": ph})
        frames.append(parts)
    return frames


def _apply_analog_life(resp, text):
    """Expand a charactered-but-static single-cycle recipe into a living drift
    table (bridge_poc rule). No-op for bare geometry, for an already-moving or
    morphing recipe, and for a non-additive-convertible keyframe (fm2/cheby/ring)."""
    import dco_recipe

    recipe = resp.get("recipe") or {}
    kfs = recipe.get("keyframes") or []
    resolved = resp.get("resolved") or {}
    adjectives = resolved.get("adjectives") or []
    motion = recipe.get("motion") or []

    # already alive? (a real morph chain, or a lexicon motion trajectory)
    has_motion = len(motion) > 1 and any(seg.get("dur_frac", 0.0) > 0.0 for seg in motion[1:])
    if len(kfs) != 1 or has_motion:
        return resp

    low = (text or "").lower()
    analog = any(c in low for c in _ANALOG_CUES)
    # "Alles andere lebt": any spectral character OR an explicit analog cue.
    # Bare geometry (no adjective, no cue) stays static — the documented exception.
    if not adjectives and not analog:
        return resp

    kf = kfs[0]
    if kf.get("kind") != "additive":
        conv = dco_recipe._ensure_additive(dict(kf))   # copy — never mutate resp's kf
        if conv is None:                               # fm2/cheby/ring: no closed-form series
            return resp
        base_partials = conv.get("partials") or []
        shape = conv.get("shape", kf.get("shape", 0.0))
    else:
        base_partials = kf.get("partials") or []
        shape = kf.get("shape", 0.0)
    if not base_partials:
        return resp

    # An INHARMONIC partial set (non-integer h — bell/metal/glass) must stay a SINGLE
    # additive keyframe. The drift below fans the base into several keyframes, which
    # the C++ Baker renders as a wavetable morph — and each inharmonic frame is then
    # projected back onto the harmonic grid (the bell drifts into a sawtooth again).
    # The engine plays a single inharmonic bank directly, so keep it static until an
    # animated-additive path can drift partials in the spectral domain. Harmonic
    # character (integer h) still drifts alive exactly as before.
    if any(abs(float(p.get("h", 1)) - round(float(p.get("h", 1)))) > 1e-3 for p in base_partials):
        return resp

    grit = analog or "dirty" in adjectives or any(c in low for c in _GRIT_CUES)
    # SMALL deviations ("kleine Abweichungen") — the drift must read as a living
    # shimmer, clearly gentler than a full waveform morph (flux ~0.15). Tuned on
    # the real-baker gate (tools/lco_testbench.py) so grit ~0.09, gentle ~0.045.
    amt = 0.10 if grit else 0.05
    pdev = 0.12 if grit else 0.06

    n = 3
    new_kfs = []
    for parts in _drift_frames(base_partials, n, amt, pdev):
        nk = {"kind": "additive", "partials": parts}
        if shape and shape > 0.0:
            nk["shape"] = shape
        new_kfs.append(nk)
    recipe["keyframes"] = new_kfs
    recipe["motion"] = ([{"to": 0, "dur_frac": 0.0, "curve": "lin"}]
                        + [{"to": i, "dur_frac": 1.0 / n, "curve": "lin"} for i in range(1, n)]
                        + [{"to": 0, "dur_frac": 1.0 / n, "curve": "lin"}])
    recipe["loop"] = True
    recipe["motion_rate_hz"] = 0.15   # a slow analog wander (not a fast wobble)

    recipe, _ = dco_recipe.validate_recipe(recipe)   # loop closure + range clamps -> bakeable
    resp["recipe"] = recipe

    mot = list(resolved.get("motion") or [])
    if "analog drift" not in mot:
        mot.append("analog drift")
    resolved["motion"] = mot
    resp["resolved"] = resolved
    # the analog/living cue is now REALIZED, not dropped — clear its "no mapping" flag
    resp["flags"] = [f for f in resp.get("flags", [])
                     if str(f.get("word", "")).lower() not in _ANALOG_CUES]
    return resp


# ─── Per-station Csound-GEN coder path (author_recipe) ───────────────────────
# The LLM plans 2-3 timbre stations, then emits ONE Csound GEN spectrum per
# station; the morph between them is a Motion trajectory the shipping Baker walks.
# The prompts below are the ones proven on the REAL baker (tools/lco_coder_bench.py):
# with the literal-station rule + explicit GEN09/GEN10 routine choice, gemma3:27b
# authors exact odd-only squares / 1/n saws / inharmonic bells (8/9 gate), and an
# 80B model hits 9/9. The 3B collapse was substantially THIS prompt bug (GEN9/GEN10
# confusion + waveform paraphrase), not only model size -- so model capability is
# now the remaining gate, and the coder model is user-installed + availability-gated.
# build_lco_response() does not yet route to this path (it delegates to the
# deterministic dco_recipe author); wiring it in is the boundary change under review.

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
# System prompts are the versions proven on the real baker (tools/lco_coder_bench.py),
# not the original PoC prompts (which confused GEN09/GEN10 and paraphrased waveforms).

# Station planning that PRESERVES literal waveform/instrument names -- the earlier
# prompt let the model paraphrase "square" -> "harsh digital pulse", losing the
# exactness the DCO demands. Only a purely descriptive prompt gets an invented
# mood phrase. (Proven on the real baker: tools/lco_coder_bench.py, 8/9 gemma3:27b,
# 9/9 mistral-medium.)
STATION_SYS = (
    "Break the described sound into the 2-3 TIMBRAL STATIONS it passes through, "
    "start to end. Reply ONLY the station names, one per line -- no numbering, no "
    "other text.\n"
    "RULE: if the prompt names specific waveforms or instruments (sine, square, "
    "saw, sawtooth, triangle, pulse, violin, piano, bell, organ, clarinet, "
    "flute, ...), use those EXACT words as the stations, IN ORDER -- never "
    "paraphrase them into moods. Invent a short descriptive phrase ONLY for a "
    "purely descriptive prompt that names no waveform/instrument.\n"
    "'square > sine' ->\nsquare\nsine\n"
    "'saw into triangle' ->\nsaw\ntriangle\n"
    "'a swell from muffled to piercing' ->\nmuffled dark\npiercing bright"
)

# The routine choice is made EXPLICIT (GEN10 for harmonic, GEN09 ONLY for
# inharmonic) with COMPLETE examples that carry their routine number, so the model
# can't mislabel a square's odd-harmonic amplitudes as GEN09. This was the core 3B
# failure -- a PROMPT bug, not only a model-size limit.
SPECTRUM_SYS = (
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


def _plan_stations(prompt, llm):
    """Model -> ordered list of 2-3 short timbre phrases (planning, its strength).
    Falls back to the prompt itself as a single station if nothing usable comes."""
    raw = llm(prompt, STATION_SYS, 80)
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
    raw = llm(phrase, SPECTRUM_SYS, 160)
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
