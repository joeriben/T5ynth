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
            a = p["a"] * (1.0 + amt * math.sin(theta + _GOLDEN * i))
            ph = p.get("phase", 0.0) + pdev * math.sin(theta + 1.7 * i + 0.5)
            a = 0.0 if a < 0.0 else (1.0 if a > 1.0 else a)
            parts.append({"h": p["h"], "a": a, "phase": ph})
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


# ─── Phase-1 legacy per-station coder path (retained for a possible Phase-2 residue fallback; NOT on the shipping path) ───

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
