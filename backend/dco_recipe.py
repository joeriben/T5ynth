#!/usr/bin/env python3
"""DCO recipe author — the S0->S4 pipeline of docs/DCO_LLM_GUARDRAILS.md.

Pure Python, stdlib only (json/math/re/copy/pathlib). Deliberately importable
WITHOUT torch/transformers so S1/S3/S4 unit-test cheaply and fast, in-process,
with no model load. S2 (the one LLM call) is injected by the caller as the
``llm_route`` callable -- this module never imports transformers itself.

Design principle (docs/DCO_LLM_GUARDRAILS.md S1): the LLM never authors DSP
data. It only routes an unmapped word to the nearest curated lexicon key, or
NONE. Every number in the returned recipe comes from a lexicon entry, an
explicitly typed user value, or a template default, composed here by
deterministic code.

Pipeline:
  S0 normalize      -> _normalize
  S1 keyword scan   -> _scan (techniques/adjectives/motions/typed-values/residue)
  S2 LLM route      -> the caller-supplied ``llm_route`` (residue only)
  S3 compose        -> _compose (template + adjective deltas + typed values +
                        motion intent; clamp)
  S4 validate/repair -> validate_recipe (structural clamp; fallback on the
                        rare unrepairable case)

Recipe schema (field names match the C++ consumer, src/dsp/DcoRecipeJson.h):
  {
    "keyframes": [ { "kind": "additive|saw|square|pulse|triangle|fm2|cheby|ring",
                     "partials": [{"h": int, "a": float, "phase": float}, ...],
                     "width": float, "ratio": int, "index": float,
                     "order": int, "drive": float, "mix": float }, ... ],  # <= 32
    "motion":    [ {"to": int, "dur_frac": float, "curve": "lin|fast|slow"},
                   ... ],                                                  # <= 16
    "loop": bool,
    "frames": int,               # baked wavetable frames, [8, 256]
    "motion_rate_hz": float,     # ABSOLUTE motion tempo: full motion loops per
                                 # second, [0.02, 8.0]. Read by the C++ DCO
                                 # motion driver; decouples the audible morph
                                 # tempo from the baked strip length
                                 # (frames*2048/sr, musically arbitrary).
  }

Determinism: no randomness anywhere. Same text + same lexicon + same
llm_route output -> byte-identical recipe (docs/DCO_LLM_GUARDRAILS.md S4).
"""
import copy
import json
import math
import re
from pathlib import Path

# ─── lexicon loading (cached) ──────────────────────────────────────────────

_DEFAULT_LEXICON_PATH = Path(__file__).resolve().parent / "dco_lexicon.json"
_LEXICON_CACHE = {}   # {str(path): lexicon dict}
_INDEX_CACHE = {}     # {id(lexicon dict): scan index}


def load_lexicon(path=None):
    """Load + cache backend/dco_lexicon.json (or an explicit path). Returns
    the raw lexicon dict (lexicon_version, techniques, adjectives, motions,
    degrees, stopwords)."""
    p = Path(path) if path else _DEFAULT_LEXICON_PATH
    key = str(p)
    cached = _LEXICON_CACHE.get(key)
    if cached is not None:
        return cached
    with open(p, "r", encoding="utf-8") as f:
        lexicon = json.load(f)
    _LEXICON_CACHE[key] = lexicon
    return lexicon


# ─── S0: normalization ──────────────────────────────────────────────────────
# Lowercase, strip punctuation to word boundaries, collapse whitespace.
# Keeps '%' (typed pulse-width values), '.' ONLY between digits (decimal
# values like "2.5"; a sentence-ending period is stripped like any other
# punctuation), and German umlauts/ß so the lexicon's native DE surface forms
# still match. '-' is treated as punctuation (a word boundary), same as the
# spec's "strip punctuation to word boundaries": "Moog-Bass" must scan as
# the two independently-meaningful tokens "moog","bass", not survive as one
# opaque "moog-bass" token. The lexicon carries the corresponding
# space-separated surface forms ("8 bit", "2 op fm", ...) for the handful of
# genuinely-compound technique names this affects.
#
# Arrow notation ("->", "-->", "=>", "<->", "→", "↔", ...) is rewritten to
# the word " into " BEFORE punctuation stripping runs: '-', '=', '<', '>'
# and the unicode arrow glyphs are not in _KEEP_CHARS_RE's keep-set and
# would otherwise be silently destroyed (turned into plain spaces), leaving
# "saw -> square" scanner-indistinguishable from "saw square". " into " is
# the canonical morph-connector surface form (dco_lexicon.json's
# "connectors"); the arrow spelling itself is never registered as a surface
# form and never user-visible -- _tokenize would drop a bare arrow token
# anyway (no alnum chars). A LETTER-FLANKED bare ">" ("square > sine",
# "square>sine") IS a morph arrow -- it is the idiom the DCO/LCO prompt editor
# invites and the maintainer types; the last alternative below rewrites it too.
# A NUMERIC ">" (a real comparison, "5 > 3", "> 3 harmonics") is left untouched:
# the letter lookbehind/lookahead never fires on a digit, so the original
# false-positive concern (greater-than has non-arrow uses) is sidestepped rather
# than blanket-excluded.

_ARROW_RE = re.compile(r"(?:<\s*[-=]+\s*>)|(?:[-=]+\s*>)|[→⇒➔⟶↔]"
                       r"|(?<=[a-zäöüß])\s*>\s*(?=[a-zäöüß])")
_KEEP_CHARS_RE = re.compile(r"[^a-z0-9\s%.äöüß]")
_STRAY_DOT_RE = re.compile(r"(?<!\d)\.|\.(?!\d)")
_WS_RE = re.compile(r"\s+")


def _normalize(text):
    text = (text or "").lower()
    text = _ARROW_RE.sub(" into ", text)
    text = _KEEP_CHARS_RE.sub(" ", text)
    text = _STRAY_DOT_RE.sub(" ", text)
    text = _WS_RE.sub(" ", text).strip()
    return text


def _tokenize(norm_text):
    return [t for t in norm_text.split(" ") if t and any(c.isalnum() for c in t)]


# ─── S1: typed-value regexes (docs/DCO_LLM_GUARDRAILS.md S3.4) ─────────────
# Run on the normalized text BEFORE the lexicon word-scan; matched spans are
# blanked out so their tokens ("ratio", "3", "index", "2.5", ...) don't also
# show up as unmapped residue. NOTE (spec deviation, documented): the spec
# says the percent pattern applies "near width words"; a percentage has no
# other referent anywhere in this schema, so it is captured unconditionally
# -- equivalent in practice, and avoids inventing an undefined proximity
# window.

_RE_RATIO = re.compile(r"\bratio\s*(\d+(?:\.\d+)?)")
_RE_INDEX = re.compile(r"\bindex\s*(\d+(?:\.\d+)?)")
_RE_ORDER = re.compile(r"\border\s*(\d+)")
_RE_HARMONICS = re.compile(r"(\d+)\s*(?:harmonics|obertöne)\b")
_RE_PERCENT = re.compile(r"(\d+(?:\.\d+)?)\s*%")


def _extract_typed_values(norm_text):
    """Returns (values dict, remaining_text with matched spans blanked)."""
    values = {}
    spans = []

    for m in _RE_RATIO.finditer(norm_text):
        values["ratio"] = int(round(float(m.group(1))))
        spans.append(m.span())
    for m in _RE_INDEX.finditer(norm_text):
        values["index"] = float(m.group(1))
        spans.append(m.span())
    for m in _RE_ORDER.finditer(norm_text):
        values["order"] = int(m.group(1))
        spans.append(m.span())
    for m in _RE_HARMONICS.finditer(norm_text):
        values["ceiling"] = int(m.group(1))
        spans.append(m.span())
    for m in _RE_PERCENT.finditer(norm_text):
        values["width"] = round(float(m.group(1)) / 100.0, 4)
        spans.append(m.span())

    chars = list(norm_text)
    for (s, e) in spans:
        for i in range(s, e):
            chars[i] = " "
    return values, "".join(chars)


# ─── S1: compositional harmonic instructions (additive addressing) ─────────
# Typed harmonic-level commands the adjective lexicon cannot express: "only odd
# overtones", "attenuate every 3rd", "boost harmonic 5", "remove the 3rd
# harmonic". Parsed on the normalized text BEFORE both the typed-value pass and
# the word-scan (so "every 3 harmonics" is claimed as a comb, not mis-read as a
# 3-partial ceiling); matched spans are blanked, so their tokens ("attenuate",
# "every", "3rd", ...) never leak to residue/S2. Each match yields a concrete
# additive op ({"op","args"}) that _compose applies AFTER the adjective pass
# (explicit user instruction beats worded adjective), through the SAME
# _ensure_additive + inapplicable-flag path as the spectral ops. This is pure
# deterministic parsing of the user's own words/numbers -- no LLM, exactly like
# the typed values above; fully inside the "router not author" guardrail.
#
# CONVENTION: "overtone" is treated as a synonym for "harmonic" in addressing
# (dominant colloquial synth usage) -- the Nth harmonic and the Nth overtone
# both mean partial h=N here, NOT the formal overtone=h(N+1). Documented in
# docs/DCO_LLM_GUARDRAILS.md.

_ORDINAL_WORDS = {"second": 2, "third": 3, "fourth": 4, "fifth": 5, "sixth": 6,
                  "seventh": 7, "eighth": 8, "ninth": 9, "tenth": 10, "other": 2}

# Verb taxonomy shared by the comb (every-Nth) and single-harmonic patterns.
# remove = zero it; reduce = proportional dip; boost = emphasize.
_REMOVE_VERBS = {"remove", "kill", "mute", "suppress", "notch"}
_REDUCE_VERBS = {"attenuate", "reduce", "damp", "dampen", "soften", "weaken",
                 "lower", "cut", "drop"}
_BOOST_VERBS = {"boost", "emphasize", "emphasise", "strengthen", "raise",
                "lift", "accent", "accentuate"}
_ALL_COMP_VERBS = _REMOVE_VERBS | _REDUCE_VERBS | _BOOST_VERBS
# longest-first so "accentuate" cannot be shadowed by "accent" in the alternation.
# Secondary alphabetical key makes the alternation SOURCE fully deterministic
# (the input is a set, whose same-length iteration order is hash-seed-dependent);
# matching behaviour is order-invariant here, but a stable source keeps the
# compiled pattern byte-reproducible across processes.
_VERB_ALT = "|".join(sorted(_ALL_COMP_VERBS, key=lambda w: (-len(w), w)))

_HNOUN = r"(?:harmonics|harmonic|overtones|overtone|partials|partial|obertöne|oberton)"
_ORD_ALT = r"\d+(?:st|nd|rd|th)?|" + "|".join(_ORDINAL_WORDS.keys())

# "only|just|nur (odd|even) <hnoun>"
_RE_ONLY_PARITY = re.compile(r"\b(?:only|just|nur)\s+(?P<parity>odd|even|ungerade|gerade)\s+" + _HNOUN)
# "<verb> every|each <ordinal> [<hnoun>]"  (noun optional: "attenuate every 3rd")
_RE_COMB = re.compile(r"\b(?P<verb>" + _VERB_ALT + r")\s+(?:every|each)\s+(?P<ord>" +
                      _ORD_ALT + r")\s*" + _HNOUN + r"?")
# "<verb> [the] <hnoun> N"  OR  "<verb> [the] Nth <hnoun>"
_RE_HN = re.compile(r"\b(?P<verb>" + _VERB_ALT + r")\s+(?:the\s+)?(?:" +
                    _HNOUN + r"\s+(?P<hnum1>\d+)|(?P<hnum2>\d+)(?:st|nd|rd|th)\s+" + _HNOUN + r")")

_COMB_REMOVE_FACTOR = 0.0
_COMB_REDUCE_FACTOR = 0.35
_COMB_BOOST_FACTOR = 1.7
_HN_BOOST_AMOUNT = 0.5      # additive (can create an absent partial)
_HN_REDUCE_FACTOR = 0.3     # multiplicative (only reduces existing)
_HN_REMOVE_FACTOR = 0.0


def _parse_ordinal(tok):
    """'3rd'/'3'/'third'/'other' -> int, or None."""
    tok = tok.strip().lower()
    if tok in _ORDINAL_WORDS:
        return _ORDINAL_WORDS[tok]
    m = re.match(r"(\d+)", tok)
    return int(m.group(1)) if m else None


def _comb_factor(verb):
    if verb in _REMOVE_VERBS:
        return _COMB_REMOVE_FACTOR
    if verb in _BOOST_VERBS:
        return _COMB_BOOST_FACTOR
    return _COMB_REDUCE_FACTOR


def _extract_composition_ops(norm_text):
    """Returns (ops list in prompt order, remaining_text with spans blanked).
    Each op is {"word": <matched phrase, for the flag machinery>, "op": ...,
    "args": ...}. Deterministic: regex order + prompt position only."""
    hits = []   # (start_pos, op_dict)
    spans = []

    for m in _RE_ONLY_PARITY.finditer(norm_text):
        odd = m.group("parity") in ("odd", "ungerade")
        # even_odd(balance): -1 -> evens x0, odds x2 (== only-odd after normalize);
        # +1 -> odds x0 (incl. the odd fundamental h1), evens x2 (== only-even).
        hits.append((m.start(), {"word": m.group(0).strip(), "op": "even_odd",
                                 "args": {"balance": -1.0 if odd else 1.0}}))
        spans.append(m.span())

    for m in _RE_COMB.finditer(norm_text):
        spans.append(m.span())   # a recognized harmonic phrase: always blanked (no residue leak)
        n = _parse_ordinal(m.group("ord"))
        if n is None or n < 2:
            continue   # "every 1st" / unparseable -> blanked, but no op
        hits.append((m.start(), {"word": m.group(0).strip(), "op": "comb",
                                 "args": {"n": n, "factor": _comb_factor(m.group("verb"))}}))

    for m in _RE_HN.finditer(norm_text):
        spans.append(m.span())   # recognized harmonic phrase: always blanked (no residue leak)
        h = int(m.group("hnum1") or m.group("hnum2"))
        if h < 1:
            continue   # "harmonic 0" addresses nothing (h1 is the fundamental) -> blanked, no op
        verb = m.group("verb")
        if verb in _BOOST_VERBS:
            op = {"op": "boost", "args": {"h": h, "amount": _HN_BOOST_AMOUNT}}
        else:
            factor = _HN_REMOVE_FACTOR if verb in _REMOVE_VERBS else _HN_REDUCE_FACTOR
            op = {"op": "scale_h", "args": {"h": h, "factor": factor}}
        op["word"] = m.group(0).strip()
        hits.append((m.start(), op))

    hits.sort(key=lambda t: t[0])
    ordered = [op for _, op in hits]

    chars = list(norm_text)
    for (s, e) in spans:
        for i in range(s, e):
            chars[i] = " "
    return ordered, "".join(chars)


# ─── S1: relative FM instructions (comparative index / ratio nudges) ───────
# The comparative twin of the typed "index 2.5" / "ratio 3" overrides: a
# direction word bound to an FM noun nudges the fm2 keyframe. Amount words
# ("more/deeper/less/gentler") + an index noun (modulation/sidebands/fm depth)
# move fm_index; ratio words ("higher/lower/wider/closer") + "ratio" move
# fm_ratio. Applied through the SAME _apply_delta_op fm_index/fm_ratio path the
# "metallic" adjective uses, so a non-FM template gets the identical honest
# "no FM operator" flag -- never a silent coercion. Bare "fm" is deliberately
# NOT an index noun (it is itself a technique surface form); the multi-word
# "fm depth / fm amount / fm index" forms cover the fm-worded case without
# shadowing the technique.
_FM_INDEX_UP   = {"more", "deeper", "stronger", "harder", "denser", "mehr"}
_FM_INDEX_DOWN = {"less", "gentler", "softer", "cleaner", "weaker", "weniger"}
_FM_RATIO_UP   = {"higher", "wider", "bigger"}
_FM_RATIO_DOWN = {"lower", "closer", "narrower", "smaller"}
_FM_INDEX_DELTA = 1.5    # one clear step, == the "metallic" adjective's index bump
_FM_RATIO_DELTA = 1      # one integer ratio step (fm_ratio is an int in [1,8])

# longest-first: "modulationsindex" MUST precede "modulation", or leftmost-first
# alternation (no trailing \b) matches only the "modulation" prefix and leaks the
# "sindex" tail as residue (same longest-first discipline as _VERB_ALT above).
_FM_INDEX_NOUN = r"(?:modulationsindex|modulation|sidebands?|fm\s+depth|fm\s+amount|fm\s+index)"
_FM_RATIO_NOUN = r"(?:ratio|verhältnis)"
# longest-first alternation, alphabetical secondary key -> byte-reproducible
# compiled pattern across processes (same rationale as _VERB_ALT above).
_FM_INDEX_DIR_ALT = "|".join(sorted(_FM_INDEX_UP | _FM_INDEX_DOWN, key=lambda w: (-len(w), w)))
_FM_RATIO_DIR_ALT = "|".join(sorted(_FM_RATIO_UP | _FM_RATIO_DOWN, key=lambda w: (-len(w), w)))
# "<dir> <index-noun>" and "<dir> [fm] <ratio-noun>" -- the noun must immediately
# follow the direction (one whitespace run), so "pulse width modulation" and a
# bare "ratio 3" are never mis-claimed (no leading direction word / a number, not
# a direction, follows/precedes).
_RE_FM_INDEX = re.compile(r"\b(?P<dir>" + _FM_INDEX_DIR_ALT + r")\s+" + _FM_INDEX_NOUN)
_RE_FM_RATIO = re.compile(r"\b(?P<dir>" + _FM_RATIO_DIR_ALT + r")\s+(?:fm\s+)?" + _FM_RATIO_NOUN)


def _extract_fm_ops(norm_text):
    """Returns (ops in prompt order, remaining_text with spans blanked). Each op
    is {"word": <matched phrase>, "op": "fm_index"|"fm_ratio", "args":{"delta":±}}.
    Deterministic: regex order + prompt position only (mirrors
    _extract_composition_ops)."""
    hits = []   # (start_pos, op_dict)
    spans = []

    for m in _RE_FM_INDEX.finditer(norm_text):
        spans.append(m.span())
        delta = _FM_INDEX_DELTA if m.group("dir") in _FM_INDEX_UP else -_FM_INDEX_DELTA
        hits.append((m.start(), {"word": m.group(0).strip(), "op": "fm_index",
                                 "args": {"delta": delta}}))
    for m in _RE_FM_RATIO.finditer(norm_text):
        spans.append(m.span())
        delta = _FM_RATIO_DELTA if m.group("dir") in _FM_RATIO_UP else -_FM_RATIO_DELTA
        hits.append((m.start(), {"word": m.group(0).strip(), "op": "fm_ratio",
                                 "args": {"delta": delta}}))

    hits.sort(key=lambda t: t[0])
    ordered = [op for _, op in hits]

    chars = list(norm_text)
    for (s, e) in spans:
        for i in range(s, e):
            chars[i] = " "
    return ordered, "".join(chars)


# ─── S1: lexicon scan index (longest-match-first) ──────────────────────────

def _build_index(lexicon):
    cache_key = id(lexicon)
    cached = _INDEX_CACHE.get(cache_key)
    if cached is not None:
        return cached

    lookup = {}   # {tuple(words): {"category":..., "key":..., "priority":..., ...}}
    max_len = 1

    def register(words_str, payload):
        nonlocal max_len
        phrase = tuple(words_str.split(" "))
        if not phrase or not phrase[0]:
            return
        if phrase in lookup:
            raise ValueError(f"dco_lexicon.json: duplicate surface form {words_str!r} "
                              f"(already registered as {lookup[phrase]!r})")
        lookup[phrase] = payload
        max_len = max(max_len, len(phrase))

    for t in lexicon["techniques"]:
        for sf in t["surface_forms"]:
            register(sf, {"category": "technique", "key": t["key"], "priority": t["priority"]})
    for a in lexicon["adjectives"]:
        for sf in a["surface_forms"]:
            register(sf, {"category": "adjective", "key": a["key"], "priority": a["priority"]})
    for mo in lexicon["motions"]:
        for sf in mo["surface_forms"]:
            register(sf, {"category": "motion", "key": mo["key"], "priority": mo["priority"],
                          "motion_category": mo["category"]})
    # lexicon_version 3+; lexicon.get(...) so an older lexicon dict (no
    # "connectors" key) still loads -- a morph chain simply never gates.
    for c in lexicon.get("connectors", []):
        for sf in c["surface_forms"]:
            register(sf, {"category": "connector", "key": c["key"]})
    for word, mult in lexicon["degrees"].items():
        register(word, {"category": "degree", "multiplier": float(mult)})
    # Stopwords go through the SAME lookup dict (not a separate set) so a
    # multi-word stopword phrase is matched by the identical longest-match-
    # first scan as every other category -- a single-word-only check here
    # would silently fail to recognize a 2+-word stopword phrase (its
    # trailing word(s) would leak through as unmapped residue).
    for sw in lexicon["stopwords"]:
        register(sw, {"category": "stopword"})

    index = {"lookup": lookup, "max_len": max_len}
    _INDEX_CACHE[cache_key] = index
    return index


def _scan(norm_text, lexicon):
    """S1. Returns a dict: values, technique_hits, adjective_hits, motion_hits,
    connector_hits, residue (list of {"word","degree","pos"})."""
    # Pre-extract the three regex-driven instruction families in a fixed order,
    # each blanking its spans so the next pass and the lexicon scan never re-read
    # them. FM ops FIRST (a comparative "<dir> ratio" must be claimed before a
    # typed "ratio N" could grab the bare "ratio"); composition BEFORE typed
    # values ("every 3 harmonics" must be a comb before _RE_HARMONICS mis-reads
    # the "3 harmonics" inside it as a 3-partial ceiling). The families use
    # disjoint nouns/verbs, so the order resolves only these two shadowing cases
    # and never introduces a new one.
    fm_ops, remaining = _extract_fm_ops(norm_text)
    composition_ops, remaining = _extract_composition_ops(remaining)
    values, remaining = _extract_typed_values(remaining)
    tokens = _tokenize(remaining)
    index = _build_index(lexicon)
    lookup = index["lookup"]
    max_len = index["max_len"]

    technique_hits = []
    adjective_hits = []
    motion_hits = []
    connector_hits = []
    residue = []

    pending_degree = None
    n = len(tokens)
    i = 0
    while i < n:
        matched = None
        upper = min(max_len, n - i)
        for L in range(upper, 0, -1):
            phrase = tuple(tokens[i:i + L])
            hit = lookup.get(phrase)
            if hit is not None:
                matched = (L, hit)
                break

        if matched is None:
            word = tokens[i]
            residue.append({"word": word,
                             "degree": pending_degree if pending_degree is not None else 1.0,
                             "pos": i})
            pending_degree = None
            i += 1
            continue

        L, hit = matched
        cat = hit["category"]
        if cat == "stopword":
            pending_degree = None
            i += L
            continue
        elif cat == "degree":
            pending_degree = hit["multiplier"]
            i += L
            continue
        elif cat == "technique":
            technique_hits.append({"key": hit["key"], "priority": hit["priority"], "pos": i})
        elif cat == "adjective":
            adjective_hits.append({"key": hit["key"], "priority": hit["priority"], "pos": i,
                                    "degree": pending_degree if pending_degree is not None else 1.0})
        elif cat == "motion":
            motion_hits.append({"key": hit["key"], "priority": hit["priority"], "pos": i,
                                 "motion_category": hit["motion_category"]})
        elif cat == "connector":
            connector_hits.append({"key": hit["key"], "pos": i})
        pending_degree = None
        i += L

    return {"values": values, "composition_ops": composition_ops, "fm_ops": fm_ops,
            "technique_hits": technique_hits, "adjective_hits": adjective_hits,
            "motion_hits": motion_hits, "connector_hits": connector_hits,
            "residue": residue}


# ─── S2 wiring helpers (the LLM call itself lives in the caller) ──────────

_S2_RESIDUE_CAP = 12


def _run_s2(residue, adjective_index, llm_route):
    """Dedupe residue words (preserving first-occurrence order), cap at
    _S2_RESIDUE_CAP unique words for the LLM call, route the rest, and fold
    results back into an adjective-hit list plus honesty flags. Any word
    NOT resolved to a real adjective key (llm_route absent, LLM said NONE,
    call raised, or truncated past the cap) is flagged, never invented."""
    flags = []
    extra_adjective_hits = []

    seen = set()
    unique_words = []
    for r in residue:
        if r["word"] not in seen:
            seen.add(r["word"])
            unique_words.append(r["word"])

    to_route = unique_words[:_S2_RESIDUE_CAP]
    truncated = unique_words[_S2_RESIDUE_CAP:]

    s2_map = {}
    if llm_route is not None and to_route:
        allowed_keys = sorted(adjective_index.keys())
        try:
            result = llm_route(list(to_route), list(allowed_keys))
            if isinstance(result, dict):
                s2_map = result
        except Exception:
            s2_map = {}  # deterministic degradation -- never propagate an LLM failure

    resolved_words = set()
    for r in residue:
        w = r["word"]
        if w not in to_route:
            continue
        key = s2_map.get(w)
        if key in adjective_index:
            extra_adjective_hits.append({"key": key, "priority": adjective_index[key]["priority"],
                                          "pos": r["pos"], "degree": r["degree"]})
            resolved_words.add(w)

    for w in to_route:
        if w not in resolved_words:
            flags.append({"word": w, "reason": "no mapping — ignored"})
    for w in truncated:
        flags.append({"word": w, "reason": "unprocessed — exceeds the 12-word S2 budget"})

    return extra_adjective_hits, flags


# ─── S3: closed-form partial series (saw/square/triangle/pulse -> additive) ─
# Standard Fourier MAGNITUDE series, phase=0.0 uniformly. Phase alternation
# (e.g. sawtooth's sign flip) is NOT encoded: every op this module applies
# (tilt/even_odd/ceiling/boost/cut) only ever touches amplitude, never phase,
# so phase is inert to every delta this composer computes. Reconstructing the
# exact original phase relationship would require assuming the C++ baker's
# basis-function convention, which is out of this module's scope (src/ is not
# touched here) -- amplitude-only is the conservative, documented choice.

_CONVERT_MAX_H = 64


def _saw_series(n):
    return [{"h": h, "a": round(1.0 / h, 6), "phase": 0.0} for h in range(1, n + 1)]


def _square_series(n):
    return [{"h": h, "a": round(1.0 / h, 6), "phase": 0.0} for h in range(1, n + 1, 2)]


def _triangle_series(n):
    return [{"h": h, "a": round(1.0 / (h * h), 6), "phase": 0.0} for h in range(1, n + 1, 2)]


def _pulse_series(width, n):
    width = max(0.02, min(0.98, width))
    raw = [abs(math.sin(math.pi * h * width)) / h for h in range(1, n + 1)]
    peak = max(raw) if raw and max(raw) > 0 else 1.0
    return [{"h": h, "a": round(raw[h - 1] / peak, 6), "phase": 0.0} for h in range(1, n + 1)]


def _ensure_additive(kf):
    """Convert a closed-form keyframe (saw/square/pulse/triangle) to kind
    'additive' in place, truncated at 64 partials, per
    docs/DCO_LLM_GUARDRAILS.md S3 step 2. Returns the (now-additive) kf on
    success, or None if kf's kind has no defined conversion (fm2/cheby/ring)
    -- the caller treats that as an inapplicable-op case for THIS keyframe."""
    kind = kf.get("kind")
    if kind == "additive":
        return kf
    if kind == "saw":
        partials = _saw_series(_CONVERT_MAX_H)
    elif kind == "square":
        partials = _square_series(_CONVERT_MAX_H)
    elif kind == "triangle":
        partials = _triangle_series(_CONVERT_MAX_H)
    elif kind == "pulse":
        partials = _pulse_series(kf.get("width", 0.5), _CONVERT_MAX_H)
    else:
        return None  # fm2 / cheby / ring: no closed-form-series conversion defined

    for stale in ("width", "ratio", "index", "order", "drive", "mix"):
        kf.pop(stale, None)
    kf["kind"] = "additive"
    kf["partials"] = partials
    return kf


# ─── S3: the closed op set (docs/DCO_LLM_GUARDRAILS.md S3.2) ──────────────

def _op_tilt(kf, db_per_oct, from_h):
    db_per_oct = max(-6.0, min(6.0, float(db_per_oct)))
    from_h = max(1, int(from_h))
    for p in kf["partials"]:
        h = p["h"]
        if h >= from_h:
            octaves = math.log2(h / from_h) if h != from_h else 0.0
            gain = 10.0 ** ((db_per_oct * octaves) / 20.0)
            p["a"] = p["a"] * gain


def _op_even_odd(kf, balance):
    """Even/odd harmonic balance. balance>0 favours evens, <0 favours odds; ±1
    fully isolates one parity. Returns True if applied, False if this balance
    would zero EVERY partial — "only even" (mult_odd=0) on an all-odd spectrum
    (sine/square/triangle/clarinet has no even harmonics to keep). In that
    unsatisfiable case the spectrum is left UNCHANGED (never silent — the same
    invariant _op_ceiling guards; a silent keyframe is also a divide-by-zero for
    the baker's peak-normalize) and the caller flags it."""
    balance = max(-1.0, min(1.0, float(balance)))
    mult_even = max(0.0, min(2.0, 1.0 + balance))
    mult_odd = max(0.0, min(2.0, 1.0 - balance))
    would_survive = any((mult_even if p["h"] % 2 == 0 else mult_odd) > 0.0 and p["a"] > 0.0
                        for p in kf["partials"])
    if not would_survive:
        return False
    for p in kf["partials"]:
        p["a"] = p["a"] * (mult_even if p["h"] % 2 == 0 else mult_odd)
    return True


def _op_ceiling(kf, h_max):
    h_max = int(h_max)
    kf["partials"] = [p for p in kf["partials"] if p["h"] <= h_max]
    if not kf["partials"]:
        kf["partials"] = [{"h": 1, "a": 1.0, "phase": 0.0}]  # never leave a keyframe silent


def _op_boost(kf, h, amount):
    h = int(h)
    amount = float(amount)
    for p in kf["partials"]:
        if p["h"] == h:
            p["a"] = p["a"] + amount
            return
    if amount > 0:
        kf["partials"].append({"h": h, "a": amount, "phase": 0.0})


def _op_cut(kf, h, amount):
    h = int(h)
    amount = float(amount)
    for p in kf["partials"]:
        if p["h"] == h:
            p["a"] = p["a"] + amount  # amount is negative by convention
            return
    # nothing to cut -- no partial created (cut only reduces existing presence)


def _op_comb(kf, n, factor):
    """Periodic harmonic addressing ("attenuate every 3rd harmonic"): multiply
    every n-th harmonic (h % n == 0) by factor. factor<1 attenuates, ==0 removes,
    >1 emphasizes. n>=2 (n==1 would scale every partial = a global gain, a no-op
    after the baker's normalize). MULTIPLICATIVE, unlike boost/cut's additive
    amount, because a comb acts across the whole series and a proportional notch
    reads as the same gesture at every partial level. The fundamental (h1) is
    never touched for n>=2 (1 % n != 0), so a comb never removes the pitch."""
    n = max(2, int(n))
    factor = max(0.0, min(4.0, float(factor)))
    for p in kf["partials"]:
        if p["h"] % n == 0:
            p["a"] = p["a"] * factor


def _op_scale_h(kf, h, factor):
    """Multiply ONE named harmonic's amplitude by factor (attenuate/remove a
    specific harmonic). Unlike _op_boost this never CREATES a partial: you can
    only reduce presence that already exists -- "attenuate harmonic 30" on a
    spectrum that never had an h30 is correctly a silent no-op. (A single-
    harmonic BOOST stays on _op_boost, which DOES create, so "boost harmonic 4"
    on a square -- which has no even harmonics -- can still add one.)"""
    h = int(h)
    factor = max(0.0, min(4.0, float(factor)))
    for p in kf["partials"]:
        if p["h"] == h:
            p["a"] = p["a"] * factor
            return


def _apply_fm_index(recipe, delta):
    applied = False
    for kf in recipe["keyframes"]:
        if kf.get("kind") == "fm2":
            kf["index"] = kf.get("index", 1.0) + float(delta)
            applied = True
    return applied


def _apply_fm_ratio(recipe, delta):
    applied = False
    for kf in recipe["keyframes"]:
        if kf.get("kind") == "fm2":
            kf["ratio"] = kf.get("ratio", 2) + delta
            applied = True
    return applied


def _apply_width(recipe, delta):
    applied = False
    for kf in recipe["keyframes"]:
        if kf.get("kind") == "pulse":
            kf["width"] = kf.get("width", 0.5) + float(delta)
            applied = True
    return applied


def _apply_motion_rate(recipe, scale):
    """"slowly/langsam scales segment curves/rate" (S3.5). Two effects, both
    deterministic (pure float ops), and both live INSIDE this function so
    that every call site -- the adjective-op dispatch in _apply_delta_op and
    the speed-word loop in _compose step 4 -- scales each exactly once:

    1. ABSOLUTE tempo: multiplies motion_rate_hz (full motion loops per
       second; scale > 1 = faster). This is the field the C++ DCO motion
       driver reads.
    2. SHAPE: reallocates the dur_frac SHARE of the interior (non-start,
       non-closing) segment(s) relative to the fixed closing segment; final
       normalization to sum=1.0 happens once, later, in _clamp_and_repair --
       so this must NOT renormalize itself (a uniform scale-then-renormalize
       is a no-op)."""
    scale = float(scale)
    if scale > 1e-6:
        recipe["motion_rate_hz"] = recipe.get("motion_rate_hz", 0.25) * scale
    motion = recipe.get("motion") or []
    if len(motion) < 3:
        return
    inv = (1.0 / scale) if scale > 1e-6 else 1.0
    for seg in motion[1:-1]:
        seg["dur_frac"] = seg["dur_frac"] * inv


def _apply_motion_depth(recipe, scale):
    """Widens/narrows the spectral distance the motion morphs across, per
    kind's natural "depth" field. Keyframe 0 (the anchor/start/close point)
    is left untouched; closed-form (unconverted) kinds have no depth field
    and are silently skipped -- not flagged, since motion_rate/motion_depth
    are not kind-scoped in the spec's op vocabulary the way fm_index/width
    are (no "only if..." clause), they simply act where a field exists."""
    scale = float(scale)
    for idx, kf in enumerate(recipe["keyframes"]):
        if idx == 0:
            continue
        kind = kf.get("kind")
        if kind == "fm2":
            kf["index"] = kf.get("index", 1.0) * scale
        elif kind == "pulse":
            w = kf.get("width", 0.5)
            kf["width"] = 0.5 + (w - 0.5) * scale
        elif kind == "cheby":
            kf["drive"] = kf.get("drive", 1.0) * scale
        elif kind == "ring":
            kf["mix"] = kf.get("mix", 1.0) * scale
        elif kind == "additive":
            for p in kf["partials"]:
                if p["h"] != 1:
                    p["a"] = p["a"] * scale


def _apply_drive(recipe, amount):
    """Waveshape (soft-clip) amount added to every keyframe's post-render
    'shape' field, clamped [0,1]. Kind-agnostic: the C++ baker applies a tanh
    soft-clip to the rendered cycle of ANY kind, so unlike fm_index/width this
    never has an inapplicable-keyframe case (always applies, never flags)."""
    amount = float(amount)
    for kf in recipe["keyframes"]:
        kf["shape"] = max(0.0, min(1.0, kf.get("shape", 0.0) + amount))


def _apply_inharm(recipe, amount, adjective_word, flags):
    """Real inharmonicity. The engine now synthesizes non-integer partials
    directly (real-time additive), so an additive keyframe is STRETCHED off the
    harmonic grid (a stiff-bar/bell model, higher partials progressively sharp) —
    the true percept, not the old integer-grid approximation. FM keyframes, which
    still bake to a single cycle, keep the older integer-sideband densification and
    are flagged as an approximation. A cheby/ring-only recipe is flagged ignored.
    Deterministic."""
    a = max(0.0, min(1.0, float(amount)))
    if a <= 0.0:
        return
    fm_touched = False
    add_touched = False
    for kf in recipe["keyframes"]:
        if kf.get("kind") == "fm2":
            kf["ratio"] = min(8, int(kf.get("ratio", 2)) + int(round(a * 3)))
            kf["index"] = min(8.0, float(kf.get("index", 1.0)) + a * 2.0)
            fm_touched = True
    # additive path (convert closed-form kinds first, like the spectral ops).
    # STRETCH the partials off the integer grid into REAL inharmonicity: a stiff
    # bar/plate/bell pushes higher partials progressively sharp,
    #   h' = h * sqrt(1 + B*(h^2 - 1)),  B = amount * 0.02
    # so the fundamental (h=1) is unmoved and each higher partial detunes sharper.
    # The engine now synthesizes non-integer partials directly (real-time additive),
    # so this is the true percept — NOT the old even-attenuate/odd-boost trick that
    # kept every partial on an integer h because a single cycle couldn't hold others.
    B = a * 0.02
    for kf in recipe["keyframes"]:
        conv = _ensure_additive(kf)
        if conv is None:
            continue
        add_touched = True
        for p in conv["partials"]:
            h = float(p["h"])
            p["h"] = round(h * math.sqrt(1.0 + B * (h * h - 1.0)), 4)
    # Honesty flag, per path that fired. The additive path is now REAL inharmonicity
    # (no longer an approximation); the FM path still only densifies integer-ratio
    # sidebands (a wavetable-FM keyframe can't hold non-integer partials, so it stays
    # an approximation). A mixed recipe rewrites both, so name both truthfully.
    # Neither-applicable (cheby/ring-only) is flagged as ignored, like the other ops.
    reasons = []
    if add_touched:
        reasons.append("partials stretched off the harmonic grid into true inharmonicity")
    if fm_touched:
        reasons.append("FM sidebands densified (integer-ratio approximation)")
    if reasons:
        flags.append({"word": adjective_word, "reason": "; ".join(reasons)})
    else:
        flags.append({"word": adjective_word,
                      "reason": "no FM or additive keyframe here for inharmonicity — ignored"})


_SPECTRAL_OPS = {"tilt", "even_odd", "ceiling", "boost", "cut", "comb", "scale_h"}


def _apply_delta_op(recipe, adjective_word, op_name, args, flags):
    if op_name in _SPECTRAL_OPS:
        any_additive = False
        even_odd_applied = False   # any keyframe where the parity op actually took
        for kf in recipe["keyframes"]:
            conv = _ensure_additive(kf)
            if conv is None:
                continue
            any_additive = True
            if op_name == "tilt":
                _op_tilt(conv, args["db_per_oct"], args["from_h"])
            elif op_name == "even_odd":
                if _op_even_odd(conv, args["balance"]):
                    even_odd_applied = True
            elif op_name == "ceiling":
                _op_ceiling(conv, args["h_max"])
            elif op_name == "boost":
                _op_boost(conv, args["h"], args["amount"])
            elif op_name == "cut":
                _op_cut(conv, args["h"], args["amount"])
            elif op_name == "comb":
                _op_comb(conv, args["n"], args["factor"])
            elif op_name == "scale_h":
                _op_scale_h(conv, args["h"], args["factor"])
        if not any_additive:
            flags.append({"word": adjective_word,
                          "reason": "no additive-convertible keyframe in this recipe — ignored"})
        elif op_name == "even_odd" and not even_odd_applied:
            # e.g. "only even" on an all-odd base — nothing to isolate; the op
            # left the spectrum unchanged (audible) rather than silencing it.
            flags.append({"word": adjective_word,
                          "reason": "no harmonics of that parity to isolate — left unchanged"})
    elif op_name == "fm_index":
        if not _apply_fm_index(recipe, args["delta"]):
            flags.append({"word": adjective_word, "reason": "no FM operator in this recipe — ignored"})
    elif op_name == "fm_ratio":
        if not _apply_fm_ratio(recipe, args["delta"]):
            flags.append({"word": adjective_word, "reason": "no FM operator in this recipe — ignored"})
    elif op_name == "width":
        if not _apply_width(recipe, args["delta"]):
            flags.append({"word": adjective_word, "reason": "no pulse width in this recipe — ignored"})
    elif op_name == "motion_rate":
        _apply_motion_rate(recipe, args["scale"])
    elif op_name == "motion_depth":
        _apply_motion_depth(recipe, args["scale"])
    elif op_name == "inharm":
        _apply_inharm(recipe, args["amount"], adjective_word, flags)
    elif op_name == "drive":
        _apply_drive(recipe, args["amount"])
    # unknown op names cannot occur -- the lexicon is validated at load/index time


# ─── S3: motion intent rewrites (docs/DCO_LLM_GUARDRAILS.md S3.3 / step 4) ──
# Pure timing/trajectory rewrites over the ALREADY-composed keyframes list
# (never touches keyframe spectral content). K = number of keyframes.

def _seg(to, dur_frac, curve):
    return {"to": to, "dur_frac": dur_frac, "curve": curve}


def _motion_static(K):
    return [_seg(0, 0.0, "lin")]


def _motion_cycle(K):
    if K <= 1:
        return _motion_static(K)
    out = [_seg(0, 0.0, "lin")]
    share = 1.0 / K
    for idx in range(1, K):
        out.append(_seg(idx, share, "lin"))
    out.append(_seg(0, share, "lin"))
    return out


def _motion_forward(K):
    # One-way scan 0 -> 1 -> ... -> (K-1): a DIRECTIONAL morph that ENDS on its
    # destination. No baked return-to-start -- "A into B" arrives at B and stays.
    # Loop-seamlessness, IF the user engages a loop, is the playback's job
    # (pingpong on the visible loop control), NOT a repeated frame appended here.
    if K <= 1:
        return _motion_static(K)
    out = [_seg(0, 0.0, "lin")]
    share = 1.0 / (K - 1)
    for idx in range(1, K):
        out.append(_seg(idx, share, "lin"))
    return out


def _motion_open_up(K):
    return [_seg(0, 0.0, "lin"), _seg(K - 1, 0.75, "slow"), _seg(0, 0.25, "fast")]


def _motion_close(K):
    # There-and-back: starts bright (K-1), settles dark (0), returns to K-1
    # so a looping wavetable wraps frame[N-1]->frame[0] on the SAME spectrum
    # (a close that ended on 0 would jump K-1->0 at the wrap = periodic click).
    return [_seg(K - 1, 0.0, "lin"), _seg(0, 0.5, "slow"), _seg(K - 1, 0.5, "fast")]


def _motion_sweep(K):
    return [_seg(0, 0.0, "lin"), _seg(K - 1, 0.5, "slow"), _seg(0, 0.5, "slow")]


def _motion_breathe(K):
    return [_seg(0, 0.0, "lin"), _seg(K - 1, 0.6, "slow"), _seg(0, 0.4, "slow")]


def _motion_periodic(K, n_cycles, curve):
    # Retained helper: this built the there-and-back scan trajectory for the old
    # vibrate/wobble/flutter motion intents. Those are now per-frame TEXTURE
    # (dco_frames), so this has no live caller — kept as the generic periodic
    # trajectory builder a future genuine motion intent could reuse.
    out = [_seg(0, 0.0, "lin")]
    n_segs = 2 * n_cycles
    share = 1.0 / n_segs
    toggle = K - 1
    cur = 0
    for _ in range(n_segs):
        cur = toggle if cur == 0 else 0
        out.append(_seg(cur, share, curve))
    return out


def _motion_evolve(K):
    if K <= 1:
        return _motion_static(K)
    out = [_seg(0, 0.0, "lin")]
    per = 0.8 / (K - 1)
    for idx in range(1, K):
        out.append(_seg(idx, per, "slow"))
    out.append(_seg(0, 0.2, "slow"))
    return out


def _motion_settle(K):
    return [_seg(0, 0.0, "fast"), _seg(K - 1, 0.15, "fast"),
            _seg(K - 1, 0.80, "lin"), _seg(0, 0.05, "fast")]


# vibrate / wobble / flutter USED to live here as MOTION intents (scan-trajectory
# rewrites via _motion_periodic, gated by _MOTION_NEEDS_K2). That was the wrong
# bucket: they are per-frame TEXTURE (a periodic timbral variation), now carried
# as lexicon category "texture" and realized frame-by-frame in dco_frames
# (_apply_texture), alongside the new tremolo/shimmer. They are deliberately absent
# from both sets below so _compose never routes them to a motion trajectory.
_MOTION_NEEDS_K2 = {"open_up", "close", "sweep", "pingpong", "breathe",
                    "evolve", "settle"}

_MOTION_REWRITE = {
    "open_up": _motion_open_up,
    "close": _motion_close,
    "sweep": _motion_sweep,
    "pingpong": lambda K: _motion_cycle(K) if K > 2 else _motion_sweep(K),
    "breathe": _motion_breathe,
    "evolve": _motion_evolve,
    "static": _motion_static,
    "cycle": _motion_cycle,
    "settle": _motion_settle,
}

_MOTION_SPEED_SCALE = {"slow": 0.5, "fast": 1.7, "snap": 3.0}

# A named motion intent ("open up", "close", ...) on a SINGLE charactered
# keyframe has, historically, nothing to move between -- _apply_motion_intent
# used to just refuse it (flag + leave the flat static loop). The missing
# second endpoint is HONESTLY synthesizable instead: the OPPOSITE of the base
# spectrum along the dark<->bright axis (LCO wave-interpolation spec sec.7).
# Ear-gate 2026-07-14: a multiplicative amplitude tilt (the first cut of this
# synthesis) moved a 4-partial bell's centroid by only ~110 Hz and was judged
# barely audible, while the ear-PASSED hand-built bloom migrated energy into
# HIGH partials that are silent at the dark end (772->2901 Hz). The opposite
# endpoint is therefore an ABSOLUTE amplitude ramp plus, for sparse sets, a
# synthesized upward extension of the partial series -- not a tilt.
_ENDPOINT_RAMP_FLOOR = 0.25   # endpoint amplitude at the weakest rank (ear-tunable)
_ENDPOINT_EXTEND_MAX = 4      # continuation partials appended for a sparse brighter endpoint
_ENDPOINT_EXTEND_BELOW = 16   # only sets sparser than this get synthesized extension partials
_ENDPOINT_EXTEND_REACH = 2.8  # extension stops at this multiple of the source's top h


def _opposite_endpoint(partials, brighter):
    """Return a NEW partials list: the opposite endpoint of the dark<->bright
    movement gesture. Amplitudes are an ABSOLUTE linear ramp over the h-ranked
    partials -- floor _ENDPOINT_RAMP_FLOOR at the weakest rank up to 1.0
    (top rank eased to 0.8 for `brighter`, mirroring the ear-passed bloom's
    tip rolloff) -- so energy genuinely migrates instead of being re-weighted
    in place. For `brighter` on a SPARSE set (< _ENDPOINT_EXTEND_BELOW
    partials), up to _ENDPOINT_EXTEND_MAX continuation partials are appended
    above the top h, extrapolating the series' gap pattern (growth ratio
    clamped [1.0, 1.6], reach capped at _ENDPOINT_EXTEND_REACH * top h); the
    source station keeps them at amplitude 0 via union alignment. A HARMONIC
    source stays harmonic: extension h snaps to the integer grid, so a baked
    2-keyframe chain never turns inharmonic and re-routes. `darker` is the
    inverse ramp on the EXISTING partials only (darkening fades what is
    there; nothing to extend). Existing phases preserved, new partials get
    phase 0. Requires len(partials) >= 2 (callers guarantee). Builds entirely
    new partial dicts (never mutates/aliases the input)."""
    base = sorted((dict(p) for p in partials), key=lambda p: float(p.get("h", 1.0)))
    hs = [float(p.get("h", 1.0)) for p in base]
    harmonic = all(abs(h - round(h)) <= 1e-3 for h in hs)
    out = [{"h": p.get("h", 1.0), "a": 0.0, "phase": p.get("phase", 0.0)} for p in base]

    if brighter and len(base) < _ENDPOINT_EXTEND_BELOW:
        gaps = [hs[i + 1] - hs[i] for i in range(len(hs) - 1)]
        if len(gaps) >= 2 and gaps[-2] > 1e-9:
            growth = gaps[-1] / gaps[-2]
            growth = 1.0 if growth < 1.0 else (1.6 if growth > 1.6 else growth)
        else:
            growth = 1.25
        gap = gaps[-1] if gaps and gaps[-1] > 1e-9 else 1.0
        top = hs[-1]
        prev = hs[-1]
        for _ in range(_ENDPOINT_EXTEND_MAX):
            gap *= growth
            nxt = prev + gap
            if nxt > _ENDPOINT_EXTEND_REACH * top:
                break
            if harmonic:
                nxt = float(round(nxt))
                if nxt <= prev:
                    nxt = prev + 1.0
            out.append({"h": round(nxt, 6), "a": 0.0, "phase": 0.0})
            prev = nxt

    n = len(out)
    for i, p in enumerate(out):
        t = i / (n - 1) if n > 1 else 1.0
        a = _ENDPOINT_RAMP_FLOOR + (1.0 - _ENDPOINT_RAMP_FLOOR) * (t if brighter else 1.0 - t)
        p["a"] = 0.0 if a < 0.0 else (1.0 if a > 1.0 else a)
    if brighter:
        out[-1]["a"] = round(out[-1]["a"] * 0.8, 6)
    return out


def _apply_motion_intent(recipe, intent_key, flags):
    """Returns True iff the loop was actually rewritten (the caller only
    applies the intent's own motion_rate_hz override in that case — a bailed
    rewrite must leave the template's rate untouched)."""
    K = len(recipe["keyframes"])
    if intent_key in _MOTION_NEEDS_K2 and K < 2:
        # Endpoint synthesis: a single charactered keyframe has nothing to move
        # BETWEEN yet, but a named motion is an honest instruction we CAN realize
        # for ANY additive spectrum -- synthesize the missing second endpoint
        # (the base's dark<->bright OPPOSITE, _opposite_endpoint) rather than
        # refusing outright. The ramp ranks partials by h, so it works on an
        # INHARMONIC bank (bell/bar/cymbal) exactly as on a harmonic one: the old
        # integer-h refusal only protected the baked harmonic grid, and an
        # inharmonic chain now routes to the real-time additive-sets engine (LCO
        # wave-interpolation spec sec.7). Only the single-partial floor stays -- a
        # pure sine has no spectral opposite (lco_author's analog-life /
        # amplitude-breathe handles that degenerate case).
        kf0 = recipe["keyframes"][0]
        partials = kf0.get("partials") or []
        if kf0.get("kind") == "additive" and len(partials) >= 2:
            brighter = intent_key not in {"close", "settle"}
            new_kf = {"kind": "additive",
                      "partials": _opposite_endpoint(partials, brighter)}
            if "shape" in kf0:
                new_kf["shape"] = kf0["shape"]
            recipe["keyframes"].append(new_kf)
            K = len(recipe["keyframes"])   # now 2 -- fall through to the normal rewrite below
        else:
            flags.append({"word": intent_key,
                          "reason": "only one keyframe in this recipe — motion has nothing to move between"})
            return False
    fn = _MOTION_REWRITE.get(intent_key)
    if fn is None:
        return False
    recipe["motion"] = fn(K)
    recipe["loop"] = True   # an explicit motion intent is cyclic -> it repeats
    return True


# --- S3.5: station pipeline (LCO wave-interpolation spec, Slice 2a) ----------
# When a composed chain carries inharmonic content (any additive keyframe with a
# NON-integer h), the shipped engine cannot BAKE it: a looped single cycle
# projects the bell back onto the harmonic grid -> a sawtooth. Instead the WHOLE
# chain routes to the real-time additive bank as K index-aligned "stations" the
# engine blends by scan position (the PromptPanel.cpp router contract). This
# post-compose step GUARANTEES that contract: every keyframe additive, every
# station the SAME length, index i == the same (h, phase) partial in every
# station, loop-closed. Harmonic-only chains never enter here -> their baked-path
# composition stays byte-identical to before this step existed.
#
# The station SPECTRA are computed EXACTLY, mirroring src/dsp/DcoBaker.cpp's
# closed forms (the baked path is the reference): saw/square/triangle/pulse
# Fourier series, cheby polynomial->harmonic expansion, ring sum/difference
# tones, and fm2 via the 2-op Bessel expansion (fm_spectrum). Relative amplitudes
# and per-partial SIGN (carried in phase, so a stays >= 0 for the [0,1] wire
# clamp) are preserved; absolute scale is irrelevant (the engine renormalizes,
# spec sec.3). Verified against a brute-force DFT of the same time-domain formulas
# DcoBaker renders (worst relative-magnitude diff ~1e-15; FM matches within the
# spec's deliberate Carson-band/2e-3 sideband truncation).

_STATION_MAX_H = 64          # generous harmonic extent per converted classic wave
_UNION_BUDGET = 64           # MAX_ADDITIVE_PARTIALS: union capped, weakest dropped
_STATION_MAX = 32            # backend chain budget, now == the wire cap kMaxKeyframes
                             # (DcoRecipeJson.h, raised 8->32 in Slice 2b-i). Slice 2b
                             # character passes land the dense sub-station chains this
                             # headroom was raised for; motion-trajectory sampling
                             # (_sample_stations) practically tops out near 16 (its
                             # interior-curve budget is floored at the old 8, so a
                             # curved path samples byte-identically -- only extra
                             # motion BREAKPOINTS, e.g. flutter's 9th, now survive).
_SAMPLE_INTERIOR_FLOOR = 8   # interior curved-segment sample budget in _sample_stations,
                             # pinned at the pre-Slice-2b cap so every already-fitting
                             # curved chain (open_up/close/settle bells) stays byte-
                             # identical; the 8->32 raise only rescues truncated
                             # BREAKPOINTS (the known flutter loss), never densifies
                             # a path that already fit.
_SUBSTATION_CAP = 32         # hard cap on sub-stations a character pass may insert
                             # (== kMaxKeyframes wire cap; MAX_ADDITIVE_SETS 64 stays
                             # the engine ceiling). Sets-path only; see _densify_stations.
_COINCIDE = 1.0e-6           # h-merge tolerance (union alignment / FM fold)
_STATION_PEAK = 0.98         # global amplitude ceiling (survive the [0,1] wire clamp)


def _bessel_j(n, x):
    """J_n(x), Bessel function of the first kind, via a numerically stable
    ascending power series. Deterministic, stdlib-only, no new dependency. The
    recipe clamps the FM index to [0, 8] and the Carson band caps |k| at
    ceil(index)+2 (<= 10) -- a bounded domain where this series converges quickly
    and exactly (Miller's downward recurrence is the textbook alternative for
    large order/argument, unneeded here)."""
    n = abs(int(n))
    if x == 0.0:
        return 1.0 if n == 0 else 0.0
    half = 0.5 * x
    term = 1.0
    for k in range(1, n + 1):
        term *= half / k                 # term_0 = (x/2)^n / n!
    total = term
    q = half * half
    k = 1
    while k <= 200:
        term *= -q / (k * (k + n))
        total += term
        if abs(term) < 1.0e-18 * (abs(total) + 1.0e-30):
            break
        k += 1
    return total


def fm_spectrum(ratio, index, thresh=2.0e-3):
    """2-op FM (carrier 1, modulator ``ratio``, ``index``) -> additive partial
    set: the EXACT Bessel expansion of DcoBaker::renderFm2's
    sin(x + I*sin(r*x)) = sum_k J_k(I) * sin((1 + k*r) * x). Components at
    h = |1 + k*ratio| with amplitude J_k(index); a negative folded frequency
    negates the amplitude (sin(-u) = -sin(u)); components at a coincident |h|
    (within 1e-6) sum. Truncated at |J_k| < 2e-3; the Carson band
    |k| <= ceil(index)+2 is the STARTING extent, grown until the edge term
    actually falls below the threshold -- a hard Carson cut would keep a
    band-edge k while dropping its above-threshold negative-frequency fold
    partner, emitting the top retained sideband with the wrong amplitude.
    A NON-integer ratio yields a non-integer (inharmonic) set. Returned as
    [{"h","a","phase"}] with the sign carried in phase (0 or pi); h<=0 (DC)
    dropped. This is a TOOL for stations -- the wire ``fm2`` keyframe kind
    (integer ratio, baked path) is untouched."""
    ratio = float(ratio)
    index = float(index)
    kmax = int(math.ceil(index)) + 2
    while abs(_bessel_j(kmax + 1, index)) >= thresh:
        kmax += 1
    jn = [_bessel_j(k, index) for k in range(kmax + 1)]
    merged = []   # [ [h, signed_amp], ... ]
    for k in range(-kmax, kmax + 1):
        jk = jn[abs(k)] * (((-1.0) ** k) if k < 0 else 1.0)   # J_{-k} = (-1)^k J_k
        if abs(jk) < thresh:
            continue
        f = 1.0 + k * ratio
        amp = jk
        h = f
        if f < 0.0:
            amp = -amp
            h = -f
        if h <= _COINCIDE:
            continue   # DC (or below) -> dropped (removeDC in the baker)
        slot = None
        for entry in merged:
            if abs(entry[0] - h) < _COINCIDE:
                slot = entry
                break
        if slot is None:
            merged.append([h, amp])
        else:
            slot[1] += amp
    out = []
    for h, amp in merged:
        if abs(amp) < 1.0e-9:
            continue
        out.append({"h": round(h, 6), "a": abs(amp),
                    "phase": 0.0 if amp >= 0.0 else math.pi})
    out.sort(key=lambda p: p["h"])
    return out


def _sc_to_partials(sc):
    """{h: (sine_coeff, cosine_coeff)} -> [{"h","a","phase"}], combining the
    quadrature pair at each h into one magnitude+phase partial:
    S*sin(h x) + C*cos(h x) = R*sin(h x + phi), R = hypot(S, C),
    phi = atan2(C, S). Drops ~zero partials and h<=0; sorts by h ascending."""
    out = []
    for h, (s, c) in sc.items():
        if h <= _COINCIDE:
            continue
        r = math.hypot(s, c)
        if r < 1.0e-9:
            continue
        phi = math.atan2(c, s) % (2.0 * math.pi)
        out.append({"h": round(float(h), 6), "a": r, "phase": phi})
    out.sort(key=lambda p: p["h"])
    return out


def _saw_sc(n):
    # DcoBaker::renderSaw: (2/(pi h)) * (-1)^(h+1) sin(h x). Relative sine coeff.
    return {h: (((-1.0) ** (h + 1)) / h, 0.0) for h in range(1, n + 1)}


def _square_sc(n):
    # DcoBaker::renderSquare: (4/(pi h)) sin(h x), odd h. Relative sine coeff.
    return {h: (1.0 / h, 0.0) for h in range(1, n + 1, 2)}


def _triangle_sc(n):
    # DcoBaker::renderTriangle: (8/pi^2)((-1)^((h-1)/2)/h^2) sin(h x), odd h.
    out, sign = {}, 1.0
    for h in range(1, n + 1, 2):
        out[h] = (sign / (h * h), 0.0)
        sign = -sign
    return out


def _pulse_sc(width, n):
    # DcoBaker::renderPulse: (4/(pi h)) sin(pi h w) cos(h x). COSINE basis.
    width = max(0.02, min(0.98, float(width)))
    return {h: (0.0, (1.0 / h) * math.sin(math.pi * h * width)) for h in range(1, n + 1)}


def _cheby_poly_coeffs(order):
    """Chebyshev T_order(z) = sum c[m] z^m, integer coefficients via the standard
    recurrence T_0=1, T_1=z, T_k = 2 z T_{k-1} - T_{k-2}."""
    t0, t1 = [1], [0, 1]
    if order <= 0:
        return t0
    if order == 1:
        return t1
    prev2, prev1 = t0, t1
    for _ in range(2, order + 1):
        cur = [0] * (len(prev1) + 1)
        for i, v in enumerate(prev1):
            cur[i + 1] += 2 * v
        for i, v in enumerate(prev2):
            cur[i] -= v
        prev2, prev1 = prev1, cur
    return prev1


def _sinpow_sc(m):
    """sin^m(x) expanded into harmonics: odd m -> sines, even m -> cosines
    (plus a DC term at h=0). Exact finite Fourier series."""
    out = {}
    if m % 2 == 1:
        p = (m - 1) // 2
        for j in range(p + 1):
            h = m - 2 * j
            coeff = ((-1.0) ** (p - j)) * math.comb(m, j) / (4.0 ** p)
            s, c = out.get(h, (0.0, 0.0))
            out[h] = (s + coeff, c)
    else:
        p = m // 2
        out[0] = (0.0, math.comb(m, p) / (2.0 ** m))
        for j in range(p):
            h = m - 2 * j
            coeff = 2.0 * ((-1.0) ** (p - j)) * math.comb(m, j) / (2.0 ** m)
            s, c = out.get(h, (0.0, 0.0))
            out[h] = (s, c + coeff)
    return out


def _cheby_sc(order, drive):
    # DcoBaker::renderCheby: T_order(drive*sin(x)); expand z=drive*sin(x), z^m via
    # _sinpow_sc, sum over the polynomial. DC (h=0, from even powers) dropped
    # (removeDC in the baker).
    coeffs = _cheby_poly_coeffs(int(order))
    drive = float(drive)
    acc = {}
    for m, cm in enumerate(coeffs):
        if cm == 0 or m == 0:
            continue
        scale = cm * (drive ** m)
        for h, (s, c) in _sinpow_sc(m).items():
            if h == 0:
                continue
            a = acc.get(h, (0.0, 0.0))
            acc[h] = (a[0] + scale * s, a[1] + scale * c)
    return acc


def _ring_sc(ratio, mix):
    # DcoBaker::renderRing: (1-m) sin(x) + m sin(x) sin(r x);
    # sin(x) sin(r x) = 0.5[cos((r-1)x) - cos((r+1)x)]. r-1==0 -> DC, dropped.
    r = int(round(float(ratio)))
    mix = float(mix)
    acc = {}

    def add(h, s, c):
        a = acc.get(h, (0.0, 0.0))
        acc[h] = (a[0] + s, a[1] + c)

    add(1, 1.0 - mix, 0.0)
    if r - 1 >= 1:
        add(r - 1, 0.0, 0.5 * mix)
    add(r + 1, 0.0, -0.5 * mix)
    return acc


def _station_partials(kf):
    """One keyframe -> its EXACT additive partial set (list of {h,a,phase}),
    mirroring DcoBaker's closed form for that kind. An already-additive keyframe
    returns a defensive copy of its partials; fm2 goes through the Bessel tool."""
    kind = kf.get("kind")
    if kind == "additive":
        return [{"h": float(p.get("h", 1.0)), "a": float(p.get("a", 0.0)),
                 "phase": float(p.get("phase", 0.0))} for p in (kf.get("partials") or [])]
    if kind == "saw":
        return _sc_to_partials(_saw_sc(_STATION_MAX_H))
    if kind == "square":
        return _sc_to_partials(_square_sc(_STATION_MAX_H))
    if kind == "triangle":
        return _sc_to_partials(_triangle_sc(_STATION_MAX_H))
    if kind == "pulse":
        return _sc_to_partials(_pulse_sc(kf.get("width", 0.5), _STATION_MAX_H))
    if kind == "cheby":
        return _sc_to_partials(_cheby_sc(kf.get("order", 3), kf.get("drive", 0.7)))
    if kind == "ring":
        return _sc_to_partials(_ring_sc(kf.get("ratio", 2), kf.get("mix", 1.0)))
    if kind == "fm2":
        return fm_spectrum(kf.get("ratio", 2), kf.get("index", 1.0))
    return [{"h": 1.0, "a": 1.0, "phase": 0.0}]   # unreachable for a valid kind


def _union_align(stations, flags=None):
    """Union-alignment (LCO spec sec.4, the ear-decided mode -- the ONLY one emitted):
    the union of all h across ``stations`` (merged within 1e-6, sorted ascending);
    a station lacking a partial gets a=0 there; the phase at each index is the
    FIRST defining station's phase (h is then constant per index -> a pure spectral
    blend, nothing glides). Every station ends the SAME length. Budget: union > 64
    -> drop the indices with the smallest peak |a| across stations, with an honest
    'reduced' flag. Returns the aligned station list."""
    union = []   # [ [h, phase], ... ] first-seen order
    for st in stations:
        for p in st:
            h = float(p["h"])
            if not any(abs(e[0] - h) < _COINCIDE for e in union):
                union.append([h, float(p.get("phase", 0.0))])
    union.sort(key=lambda e: e[0])

    def amp_at(st, h):
        for p in st:
            if abs(float(p["h"]) - h) < _COINCIDE:
                return float(p.get("a", 0.0))
        return 0.0

    if len(union) > _UNION_BUDGET:
        peaks = sorted((max(amp_at(st, h) for st in stations), idx)
                       for idx, (h, _ph) in enumerate(union))
        drop = {idx for _peak, idx in peaks[:len(union) - _UNION_BUDGET]}
        union = [e for idx, e in enumerate(union) if idx not in drop]
        if flags is not None:
            flags.append({"word": "partials",
                          "reason": f"reduced to {_UNION_BUDGET} partials for the moving inharmonic chain"})

    return [[{"h": round(h, 6), "a": amp_at(st, h), "phase": ph} for h, ph in union]
            for st in stations]


def _build_segments(motion, num_kf):
    """Mirror DcoBaker::bake's segment construction: (from_kf, to_kf, cum_start,
    cum_end, curve) over cumulative trajectory fraction [0,1]. Returns [] when
    there is no real motion (the caller treats that as a single static station)."""
    def clamp_kf(i):
        return max(0, min(num_kf - 1, int(i)))

    if not motion or len(motion) <= 1:
        return []
    start = clamp_kf(motion[0].get("to", 0))
    raw = [max(0.0, float(seg.get("dur_frac", 0.0))) for seg in motion[1:]]
    total = sum(raw)
    if total <= 0.0:
        return []
    segs = []
    cum = 0.0
    frm = start
    for i, seg in enumerate(motion[1:]):
        dur = raw[i] / total
        to = clamp_kf(seg.get("to", 0))
        cum_end = 1.0 if (i + 1 == len(raw)) else (cum + dur)
        segs.append((frm, to, cum, cum_end, seg.get("curve", "lin")))
        cum = cum_end
        frm = to
    return segs


def _curve_shape(a, curve):
    # Mirror DcoBaker::shapeCurve (Lin / Fast=a^0.4 / Slow=a^2.5).
    a = 0.0 if a < 0.0 else (1.0 if a > 1.0 else a)
    if curve == "fast":
        return a ** 0.4
    if curve == "slow":
        return a ** 2.5
    return a


def _content_at(p, segs, corners):
    """Blend the union-aligned corner spectra at trajectory position p in [0,1],
    exactly as DcoBaker::bake samples one frame: find the active segment, shape
    the local fraction by its curve, lerp per-index amplitude between from/to.
    ``corners`` are aligned (same length, same h/phase per index)."""
    active = segs[-1]
    for seg in segs:
        if p <= seg[3] + 1e-12:
            active = seg
            break
    frm, to, cs, ce, curve = active
    span = ce - cs
    a = _curve_shape((p - cs) / span if span > 1e-9 else 1.0, curve)
    cf, ct = corners[frm], corners[to]
    return [{"h": cf[i]["h"], "a": (1.0 - a) * cf[i]["a"] + a * ct[i]["a"],
             "phase": cf[i]["phase"]} for i in range(len(cf))]


def _sample_stations(corners, motion, max_stations=_STATION_MAX):
    """Sample the motion trajectory into <= max_stations aligned sub-stations
    (LCO spec sec.6.2), mirroring how DcoBaker bakes it into 256 frames -- only into
    the Sets medium. Station positions are the segment breakpoints (the corner
    visits: a plain andback -> [A,B,A]) PLUS interior samples inside CURVED
    (fast/slow), non-hold segments, so a shaped path (open_up's slow rise,
    settle's quick attack) is not flattened to a straight uniform blend. More of
    the <=8 slots are spent only when curves actually shape the path."""
    segs = _build_segments(motion, len(corners))
    if not segs:
        return [corners[0]]
    positions = [0.0] + [seg[3] for seg in segs]     # breakpoints (corner visits)
    # Interior curved-segment samples are budgeted against _SAMPLE_INTERIOR_FLOOR
    # (the pre-Slice-2b cap of 8), NOT max_stations: a curved path that already
    # fit under 8 samples byte-identically after the 8->32 raise. The raise ONLY
    # changes the final truncation below, so a motion with MORE breakpoints than
    # the old cap (flutter: 9 breakpoints > 8 -> the 9th used to be dropped) now
    # keeps them all. slots therefore never grows with max_stations.
    slots = min(_SAMPLE_INTERIOR_FLOOR, max_stations) - len(positions)
    if slots > 0:
        curved = [seg for seg in segs
                  if seg[4] != "lin" and seg[0] != seg[1] and (seg[3] - seg[2]) > 1e-6]
        if curved:
            per = max(1, slots // len(curved))
            extra = []
            for seg in curved:
                cs, ce = seg[2], seg[3]
                for j in range(1, per + 1):
                    extra.append(cs + (ce - cs) * j / (per + 1.0))
            positions.extend(extra[:slots])
    positions = sorted(set(round(p, 9) for p in positions))
    return [_content_at(p, segs, corners) for p in positions][:max_stations]


def _router_contract_ok(recipe):
    """Mirror the shipped PromptPanel.cpp additive-sets route test: every keyframe
    Additive, every station the SAME non-zero partial count, and SOME partial
    non-integer h. Returns (ok, reason). Used as a debug-level self-check in
    _stationize and asserted directly in tests."""
    kfs = recipe.get("keyframes") or []
    if not kfs:
        return False, "no keyframes"
    if not all(kf.get("kind") == "additive" for kf in kfs):
        return False, "not every keyframe is additive"
    counts = [len(kf.get("partials") or []) for kf in kfs]
    if counts[0] == 0:
        return False, "first station has no partials"
    if any(c != counts[0] for c in counts):
        return False, "station partial counts differ: " + str(counts)
    inh = any(abs(float(p["h"]) - round(float(p["h"]))) > 1.0e-3
              for kf in kfs for p in kf["partials"])
    if not inh:
        return False, "no non-integer partial (would bake, not route to sets)"
    return True, "ok"


def _stationize(recipe, flags):
    """Post-compose station pipeline (LCO wave-interpolation spec sec.3-6, Slice 2a).
    Runs ONLY when the composed chain carries inharmonic content AND has >= 2
    keyframes to align. A single already-additive inharmonic station already
    satisfies the router contract (K==1) and is left byte-identical; a harmonic
    chain is left byte-identical for the baked path. Otherwise: convert every
    keyframe to an exact additive station (mirroring DcoBaker), union-align them,
    sample the motion trajectory into <= 8 loop-closed sub-stations, and emit a
    recipe that satisfies the shipped additive-sets router contract."""
    kfs = recipe.get("keyframes") or []
    inharmonic = any(kf.get("kind") == "additive"
                     and any(abs(float(p.get("h", 1.0)) - round(float(p.get("h", 1.0)))) > 1.0e-3
                             for p in (kf.get("partials") or []))
                     for kf in kfs)
    if not inharmonic or len(kfs) < 2:
        return recipe   # harmonic chain, or a lone inharmonic station -> untouched

    # 1. every keyframe -> an exact additive partial set (mirrors DcoBaker).
    corners = [_station_partials(kf) for kf in kfs]
    # 2. union-align the corners (shared h+phase per index, equal length, budget).
    corners = _union_align(corners, flags)
    # 3. ONE global amplitude ceiling so the exact spectra survive the [0,1] wire
    #    clamp (relative + cross-station dynamics intact; the engine renormalizes
    #    anyway -- spec sec.3 forbids per-position renorm).
    peak = max((p["a"] for st in corners for p in st), default=0.0)
    if peak > _STATION_PEAK:
        s = _STATION_PEAK / peak
        for st in corners:
            for p in st:
                p["a"] *= s
    # 4. sample the trajectory into <= 8 sub-stations. The sampled station
    #    SEQUENCE already encodes the path shape: a directional morph -> [A..B]
    #    (ends on B); an explicit cyclic intent's there-and-back trajectory ->
    #    [A..B..A] (returns on its own). NO content is duplicated to force a wrap
    #    -- loop-seamlessness, when the user engages a loop, is the playback's job
    #    (pingpong), not a station repeated 1:1.
    stations = _sample_stations(corners, recipe.get("motion") or [])
    # 5. the stations ARE the keyframes/timeline now; scan them in order and KEEP
    #    the chain's own loop flag (directional morph False, cyclic intent True).
    recipe["keyframes"] = [{"kind": "additive", "partials": st} for st in stations]
    # A directional chain (loop False) scans forward and STOPS on its destination.
    # A cyclic intent (loop True) closes back on index 0, exactly as before -- its
    # there-and-back trajectory already made the last station equal the first, so
    # dropping the 1:1 duplication above leaves cyclic recipes byte-identical.
    recipe["motion"] = (_motion_cycle(len(stations)) if recipe.get("loop")
                        else _motion_forward(len(stations)))
    recipe, _ = _clamp_and_repair(recipe)
    # 6. debug-level self-check of the shipped router contract.
    ok, why = _router_contract_ok(recipe)
    assert ok, "stationize violated the additive-sets router contract: " + why
    return recipe


# --- S3.6: character/texture passes (LCO wave-interpolation spec sec.6.3/6.4) ---
# The five texture adjectives (dirty / analog / old / washed-out / overdriven, +
# their lexicon synonyms) are CONSTRUCTION PASSES over the keyframe chain, not
# spectral delta-ops on a single station (spec sec.6). Each pass is a named,
# deterministic transform over the union-aligned additive station chain that
# movement-by-default / the morph composer already produced. Spectral adjectives
# (bright/dark/hollow/thin/fat/...) STAY delta-ops in _compose. The passes are
# invoked ONCE, as the final authoring layer, from lco_author.build_lco_response
# AFTER the stations exist (movement-by-default's _stationize / _apply_analog_life)
# -- this is the spec's "post-stationize" position. Placing the FRAMEWORK here (in
# the torch-free DSP-domain module, reusing _station_partials / _union_align /
# _clamp_and_repair / _router_contract_ok / _flag_tier) and the INVOCATION in
# lco_author (where the multi-station chain is guaranteed to exist) reads cleaner
# than duplicating the alignment machinery in the orchestrator.
#
# STRICT no-op when the prompt carries no texture adjective, so every non-texture
# recipe is byte-identical (the hard non-regression gate, F.4). Determinism:
# golden-angle / index-derived offsets only -- NO random module (double-run
# byte-identical).
#
# Two media (spec sec.6.2/6.4):
#   * BAKE path (a harmonic chain, all-integer h): the 256-frame baker
#     interpolation IS the fluctuation medium, so passes perturb the EXISTING
#     keyframes IN PLACE; loop closure stays the motion's job (motion[0].to ==
#     motion[-1].to, already set upstream) so the keyframes need not be equal.
#   * SETS path (an inharmonic chain, some non-integer h): the discrete stations
#     ARE the medium, so a TEMPORAL pass first inserts perturbed midpoint
#     SUB-STATIONS (spec sec.6.4) up to _SUBSTATION_CAP; loop closure is
#     keyframe-level (station[-1] == station[0], re-asserted). The sets engine
#     (PluginProcessor.cpp loadDcoAdditive) blends ALL K stations by scan and never
#     reads recipe.motion, so the sub-station motion is emitted only to keep the
#     recipe valid + closed.
#
# A pass NEVER flips a chain's router classification (F.6): a harmonic chain stays
# all-integer-h (bakes), an inharmonic chain keeps its non-integer partials (routes
# to sets). Every pass preserves the wire caps (<= _SUBSTATION_CAP keyframes, <= 64
# aligned partials via _union_align's budget) and re-asserts the router contract
# for sets chains.

# Fixed order when several combine: NONLINEARITY FIRST (overdriven CREATES the new
# partials the amplitude passes then shape), BLUR LAST (washed-out smears whatever
# the others built; running it earlier would just be re-sharpened by dirty/analog).
# dirty (per-station scatter) precedes analog (coherent drift) precedes old
# (erosion+wow): scatter, then the slow coherent layer over it, then age it.
_PASS_ORDER = ("overdriven", "dirty", "analog", "old", "washed_out")
_TEMPORAL_PASSES = ("dirty", "analog", "old")   # essence is fluctuation-over-time
_GOLDEN = 2.399963229728653   # radians (golden angle) -- decorrelates successive
                              # partial indices so a per-index perturbation never
                              # reads as a global tremolo (mirrors lco_author._GOLDEN)
_DENSIFY_TARGET = 16          # sub-station count a temporal sets pass aims for
                              # (<= _SUBSTATION_CAP; spec sec.6.4 "dense 16-32")


def _texture_passes(resolved_adjectives, lexicon):
    """Ordered [(pass_name, params), ...] for the texture adjectives present, in
    the fixed _PASS_ORDER. Reads each adjective entry's additive 'pass' field (the
    LLM only ever picked the KEY; every number below is curated in the lexicon,
    never authored -- docs/DCO_LLM_GUARDRAILS.md). Two synonym keys mapping to the
    same pass name collapse to one pass (last definition wins, deterministic)."""
    by_key = {a["key"]: a for a in lexicon["adjectives"]}
    found = {}
    for key in resolved_adjectives:
        entry = by_key.get(key)
        pdef = entry.get("pass") if isinstance(entry, dict) else None
        if isinstance(pdef, dict) and pdef.get("name") in _PASS_ORDER:
            found[pdef["name"]] = pdef
    return [(name, found[name]) for name in _PASS_ORDER if name in found]


def _waveshape_harmonic(partials, drive, n=512, h_max=_STATION_MAX_H):
    """Overdrive on a HARMONIC station: reconstruct one cycle from the partials,
    push it through a tanh soft-clip (the same nonlinearity DcoBaker::applyShape
    renders, mapped 1 + drive*4 with a 0.2*drive asymmetry bias), and project the
    result back onto the INTEGER harmonic grid 1..h_max. This adds the real odd
    (and, from the bias, some even) harmonics a drive stage creates -- computed
    into the SPECTRUM so it is carried on both media (the sets path never runs the
    baker's shape). The cycle is peak-normalized first so `drive` means the same
    regardless of the incoming level (the engine renormalizes anyway). Deterministic,
    stdlib-only. Output h is always integer -> the chain stays harmonic (no reroute)."""
    two_pi = 2.0 * math.pi
    cyc = [0.0] * n
    for p in partials:
        h = float(p["h"]); a = float(p["a"]); ph = float(p.get("phase", 0.0))
        for k in range(n):
            cyc[k] += a * math.sin(h * two_pi * k / n + ph)
    peak = max((abs(v) for v in cyc), default=0.0) or 1.0
    gain = 1.0 + drive * 4.0
    bias = 0.2 * drive
    y = [math.tanh(gain * (v / peak + bias)) for v in cyc]
    mean = sum(y) / n
    y = [v - mean for v in y]
    out = []
    for h in range(1, h_max + 1):
        s = 0.0; c = 0.0
        for k in range(n):
            ang = h * two_pi * k / n
            s += y[k] * math.sin(ang); c += y[k] * math.cos(ang)
        s *= 2.0 / n; c *= 2.0 / n
        r = math.hypot(s, c)
        if r > 1.0e-4:
            out.append({"h": float(h), "a": r, "phase": math.atan2(c, s) % two_pi})
    return out or [{"h": 1.0, "a": 1.0, "phase": 0.0}]


def _intermod_partials(partials, drive):
    """Overdrive on an INHARMONIC station: a soft nonlinearity on a non-harmonic
    set produces INTERMODULATION partials at f_i +- f_j (and the 2nd self-harmonic
    2*f_i) that a single-cycle bake could never hold -- the sets engine synthesizes
    them directly. Amplitudes ~ drive * a_i * a_j (2nd-order term), phases combined
    (sum/difference of the parents). Existing partials are preserved (vector-summed
    so a coincident intermod reinforces or cancels honestly). Only the strongest
    partials seed the intermod (weak parents contribute negligibly), which also
    bounds a 64-partial union station to O(1) pairs. Non-integer parents keep the
    set inharmonic -> no reroute. Deterministic."""
    g2 = drive * 0.5
    strong = sorted(partials, key=lambda p: -float(p["a"]))[:12]
    acc = {}   # h_key -> [h, sin_sum, cos_sum]

    def add(h, amp, phase):
        if h <= _COINCIDE or amp == 0.0:
            return
        key = round(h, 6)
        s = amp * math.sin(phase); c = amp * math.cos(phase)
        e = acc.get(key)
        if e is None:
            acc[key] = [h, s, c]
        else:
            e[1] += s; e[2] += c

    for p in partials:
        add(float(p["h"]), float(p["a"]), float(p.get("phase", 0.0)))
    for ai in range(len(strong)):
        pi = strong[ai]; hi = float(pi["h"]); ampi = float(pi["a"]); fi = float(pi.get("phase", 0.0))
        add(2.0 * hi, g2 * ampi * ampi * 0.5, 2.0 * fi)
        for bj in range(ai + 1, len(strong)):
            pj = strong[bj]; hj = float(pj["h"]); ampj = float(pj["a"]); fj = float(pj.get("phase", 0.0))
            amp = g2 * ampi * ampj
            add(hi + hj, amp, fi + fj)
            add(abs(hi - hj), amp, fi - fj)
    out = []
    for h, s, c in acc.values():
        r = math.hypot(s, c)
        if r < 1.0e-6:
            continue
        out.append({"h": round(h, 6), "a": r, "phase": math.atan2(c, s) % (2.0 * math.pi)})
    out.sort(key=lambda p: p["h"])
    return out


def _pass_overdriven(stations, pdef, inharmonic, flags):
    """overdriven/distorted: waveshaping-equivalent -> real new harmonics per
    station (spec sec.6.3). Harmonic chain -> tanh-drive harmonics; inharmonic ->
    intermod partials. Re-union-aligns afterwards (the count changed) so every
    station stays the same length; _union_align applies the <=64 budget with the
    honest 'reduced to 64 partials' flag if the intermod overflows."""
    drive = float(pdef.get("drive", 0.6))
    if inharmonic:
        built = [_intermod_partials(st, drive) for st in stations]
    else:
        built = [_waveshape_harmonic(st, drive) for st in stations]
    return _union_align(built, flags)


def _pass_dirty(stations, pdef):
    """dirty/gritty: small deterministic amplitude+phase jitter that VARIES PER
    STATION AND PARTIAL (spec sec.6.3, the frame rule "every station slightly
    different"). Magnitudes from the lexicon. golden angle decorrelates partials;
    a distinct per-station phase makes neighbours differ. Loop closure is
    re-asserted by the caller (sets) or carried by motion (bake)."""
    amp_j = float(pdef.get("amp_jitter", 0.12))
    ph_j = float(pdef.get("phase_jitter", 0.14))
    for s_idx, st in enumerate(stations):
        for i, p in enumerate(st):
            j_a = math.sin(_GOLDEN * (i + 1) + 1.2341 * s_idx + 0.7)
            j_p = math.sin(_GOLDEN * (i + 1) * 0.5 + 2.3299 * s_idx + 1.9)
            p["a"] = float(p["a"]) * (1.0 + amp_j * j_a)
            p["phase"] = float(p.get("phase", 0.0)) + ph_j * j_p
    return stations


def _pass_analog(stations, pdef, inharmonic):
    """analog: slow COHERENT drift across the whole chain (spec sec.6.3;
    generalizes _apply_analog_life._drift_frames). Amplitude wobble + phase drift
    on a smooth 2*pi cycle across the stations (loop-closed: s=0 and s=K-1 coincide),
    golden-angle offset per partial index so nothing repeats. Plus an analog h
    micro-detune -- SETS path only, applied IDENTICALLY to every station so h stays
    CONSTANT per index (union alignment / no gliding, spec sec.4), the fundamental
    untouched. A harmonic chain gets NO h detune (any shift flips integer->non-integer
    and would reroute the engine path -- forbidden)."""
    wob = float(pdef.get("amp_wobble", 0.08))
    ph_drift = float(pdef.get("phase_drift", 0.10))
    h_detune = float(pdef.get("h_detune", 0.0025))
    K = len(stations)
    denom = (K - 1) if K > 1 else 1
    for s_idx, st in enumerate(stations):
        theta = 2.0 * math.pi * s_idx / denom
        for i, p in enumerate(st):
            p["a"] = float(p["a"]) * (1.0 + wob * math.sin(theta + _GOLDEN * i))
            p["phase"] = float(p.get("phase", 0.0)) + ph_drift * math.sin(theta + 1.7 * i + 0.5)
    if inharmonic and h_detune > 0.0 and stations:
        n = len(stations[0])
        offs = [(1.0 + h_detune * math.sin(_GOLDEN * i + 0.4)) if i > 0 else 1.0 for i in range(n)]
        for st in stations:
            for i, p in enumerate(st):
                p["h"] = round(float(p["h"]) * offs[i], 6)
    return stations


def _pass_old(stations, pdef, inharmonic, static_mode, flags):
    """old: HF erosion (progressive high-partial attenuation, quadratic in h-rank,
    varying slightly across stations) + light smear + wow. wow = slow coherent pitch
    wobble: SETS path -> a tiny COMMON h-scale per station oscillating across the
    sub-stations (a real, tape-tiny micro-glide, NOT the ear-rejected morph glide;
    stays inharmonic). BAKED path -> a non-integer pitch wobble is not expressible
    on the integer-grid baked wire without flipping the route, so the erosion+smear
    apply and the wow is FLAGGED honestly as not carried here."""
    erode = float(pdef.get("hf_erode", 0.5))
    smear = float(pdef.get("smear", 0.12))
    wow = float(pdef.get("wow", 0.004))
    K = len(stations)
    denom = (K - 1) if K > 1 else 1
    for s_idx, st in enumerate(stations):
        n = len(st)
        var = 1.0 + 0.1 * math.sin(2.0 * math.pi * s_idx / denom)
        eroded = []
        for i, p in enumerate(st):
            frac = i / (n - 1) if n > 1 else 0.0
            g = 1.0 - erode * var * frac * frac
            eroded.append(float(p["a"]) * (g if g > 0.0 else 0.0))
        for i, p in enumerate(st):
            lo = eroded[i - 1] if i > 0 else eroded[i]
            hi = eroded[i + 1] if i < n - 1 else eroded[i]
            p["a"] = (1.0 - 2.0 * smear) * eroded[i] + smear * lo + smear * hi
    if inharmonic and not static_mode and wow > 0.0:
        for s_idx, st in enumerate(stations):
            k = 1.0 + wow * math.sin(2.0 * math.pi * s_idx / denom)
            for p in st:
                p["h"] = round(float(p["h"]) * k, 6)
    elif not inharmonic:
        flags.append({"word": "old",
                      "reason": "wow (pitch wobble) not carried on the baked harmonic path -- HF erosion and smear applied"})
    return stations


def _pass_washed_out(stations, pdef):
    """washed-out: spectral blur -- a small fixed [beta, 1-2beta, beta] kernel over
    each station's h-ordered amplitude vector, so energy leaks between h-neighbours
    and partial edges lose definition (spec sec.6.3). Single-station spectral (NOT
    temporal), so it applies even under an explicit 'static' order."""
    beta = float(pdef.get("blur", 0.18))
    for st in stations:
        n = len(st)
        amps = [float(p["a"]) for p in st]
        for i, p in enumerate(st):
            lo = amps[i - 1] if i > 0 else amps[i]
            hi = amps[i + 1] if i < n - 1 else amps[i]
            p["a"] = (1.0 - 2.0 * beta) * amps[i] + beta * lo + beta * hi
    return stations


def _densify_stations(stations, target=_DENSIFY_TARGET, cap=_SUBSTATION_CAP):
    """Insert union-aligned midpoint SUB-STATIONS between adjacent stations until
    the chain reaches `target` (spec sec.6.4 sets-path medium), never exceeding
    `cap`. Midpoints are per-index amplitude means of the (already index-aligned)
    neighbours, keeping h/phase from the left station -> alignment preserved and, as
    every insertion sits strictly between distinct neighbours, loop closure survives
    (the last station is still a copy of the first). Deterministic."""
    out = [[dict(p) for p in st] for st in stations]
    while len(out) < target and (2 * len(out) - 1) <= cap:
        dense = [out[0]]
        for k in range(1, len(out)):
            a, b = out[k - 1], out[k]
            mid = [{"h": a[i]["h"], "a": 0.5 * (float(a[i]["a"]) + float(b[i]["a"])),
                    "phase": a[i]["phase"]} for i in range(len(a))]
            dense.append(mid)
            dense.append(b)
        out = dense
    return out


def _sets_motion(K):
    """motion[] for a stationized/densified SETS chain. The additive-sets engine
    blends all K stations by scan and NEVER reads recipe.motion (PluginProcessor.cpp
    loadDcoAdditive receives only the station list + rate), so this exists solely to
    keep the recipe structurally valid and loop-closed. A full per-station cycle when
    it fits the wire's <=16 motion segments (kMaxSegments, DcoRecipeJson.h); otherwise
    a compact 3-segment there-and-back (still loop-closed)."""
    if K + 1 <= 16:
        return _motion_cycle(K)
    return [_seg(0, 0.0, "lin"), _seg(K - 1, 0.5, "lin"), _seg(0, 0.5, "lin")]


def apply_character_passes(resp, text=None):
    """Final authoring layer (spec sec.6.3/6.4): run the texture/character passes
    over the station chain build_lco_response already produced. ``resp`` is that
    working dict {ok, recipe, resolved, flags, lexicon_version, reference_vocabulary};
    returned mutated. A STRICT no-op (byte-identical recipe) when no texture adjective
    is present -- the hard non-regression gate. See the section banner above for the
    two-media / classification-invariance / cap contract."""
    lexicon = load_lexicon()
    recipe = resp.get("recipe") or {}
    resolved = resp.get("resolved") or {}
    passes = _texture_passes(resolved.get("adjectives") or [], lexicon)
    if not passes:
        return resp   # non-texture recipe -> untouched, byte-identical

    kfs = recipe.get("keyframes") or []
    if not kfs:
        return resp
    added_flags = []

    # STATIC rule (deliverable E): an explicit 'static' order collapses the recipe to
    # ONE station upstream; texture temporality yields to it. Non-temporal components
    # (overdrive harmonics, washed-out blur) still apply to the single station; a pass
    # whose essence is fluctuation-over-time (dirty/analog/old) is SUPPRESSED with an
    # honest adapted-tier flag.
    static_mode = len(kfs) <= 1

    # 1. every keyframe -> exact additive partials, union-aligned to a shared index
    #    grid; classify ONCE and freeze (a pass must never flip this -> F.6).
    stations = [_station_partials(kf) for kf in kfs]
    if not any(stations):
        return resp
    stations = _union_align(stations, added_flags)
    inharmonic = any(abs(float(p["h"]) - round(float(p["h"]))) > 1.0e-3
                     for st in stations for p in st)

    # 2. densify (SETS + a temporal pass only): the sub-station medium (spec sec.6.4).
    temporal_present = any(name in _TEMPORAL_PASSES for name, _ in passes)
    if inharmonic and not static_mode and temporal_present:
        stations = _densify_stations(stations)

    # 3. run the passes in the fixed order.
    ran = []
    for name, pdef in passes:
        if static_mode and name in _TEMPORAL_PASSES:
            added_flags.append({"word": name,
                                "reason": f"static suppresses the fluctuation of {name}"})
            continue
        if name == "overdriven":
            stations = _pass_overdriven(stations, pdef, inharmonic, added_flags)
        elif name == "dirty":
            stations = _pass_dirty(stations, pdef)
        elif name == "analog":
            stations = _pass_analog(stations, pdef, inharmonic)
        elif name == "old":
            stations = _pass_old(stations, pdef, inharmonic, static_mode, added_flags)
        elif name == "washed_out":
            stations = _pass_washed_out(stations, pdef)
        ran.append(name)

    if not ran:
        # every requested pass was a suppressed temporal one (e.g. "static dirty
        # bell"): the recipe is unchanged -> keep it byte-identical, only surface
        # the suppression flag(s).
        for f in added_flags:
            f.setdefault("tier", _flag_tier(f.get("reason", "")))
        resp["flags"] = list(resp.get("flags") or []) + added_flags
        return resp

    # 4. amplitude sanitation + one global peak ceiling (mirrors _stationize step 3;
    #    the engine renormalizes, so this only guarantees the [0,1] wire clamp).
    for st in stations:
        for p in st:
            a = p["a"]
            p["a"] = 0.0 if a < 0.0 else (1.0 if a > 1.0 else a)
    peak = max((p["a"] for st in stations for p in st), default=0.0)
    if peak > _STATION_PEAK:
        sc = _STATION_PEAK / peak
        for st in stations:
            for p in st:
                p["a"] *= sc

    # 5. loop closure. SETS -> keyframe-level (station[-1] == station[0]); BAKE keeps
    #    the upstream motion's closure (motion[0].to == motion[-1].to).
    if inharmonic and len(stations) >= 2:
        stations[-1] = [dict(p) for p in stations[0]]

    # 6. rebuild keyframes; regenerate motion for a re-counted SETS chain; leave the
    #    bake chain's existing (loop-closing) motion untouched. A pre-existing 'shape'
    #    is preserved only on the bake path with an unchanged 1:1 keyframe map AND when
    #    overdrive did not run (overdrive bakes its harmonics into the partials, so the
    #    engine's render-time shape must not double it).
    old_shapes = [kf.get("shape") for kf in kfs]
    drop_shape = "overdriven" in ran
    new_kfs = []
    for i, st in enumerate(stations):
        nk = {"kind": "additive", "partials": st}
        if (not inharmonic and not drop_shape and i < len(old_shapes)
                and old_shapes[i] and len(stations) == len(kfs)):
            nk["shape"] = old_shapes[i]
        new_kfs.append(nk)
    recipe["keyframes"] = new_kfs
    if inharmonic:
        recipe["motion"] = _sets_motion(len(new_kfs))
        recipe["loop"] = True
    recipe, _ = _clamp_and_repair(recipe)
    if inharmonic:
        ok, why = _router_contract_ok(recipe)
        assert ok, "character passes violated the additive-sets router contract: " + why

    resp["recipe"] = recipe
    for f in added_flags:
        f.setdefault("tier", _flag_tier(f.get("reason", "")))
    resp["flags"] = list(resp.get("flags") or []) + added_flags
    resolved = dict(resolved)
    resolved["passes"] = ran
    resp["resolved"] = resolved
    return resp


# ─── S3: technique-template lookup + default inference ───────────────────

def _technique_sequence(scan, technique_index):
    """Gate for connector-driven morph-chain composition ("saw morphing into
    a square"): returns the participant technique hits in PROMPT order (one
    per distinct key, first occurrence kept), or None if this prompt is not
    a chain. A chain requires (a) at least one connector word anywhere in
    the prompt, (b) at least two DISTINCT technique keys ("saw into saw"
    chains nothing), and (c) at least one connector strictly BETWEEN the
    first and last (deduped) technique mention -- a connector outside that
    span ("into a warm saw square") does not establish an ordering between
    the two waveforms, so it does not gate a chain. Chainability itself
    (single-keyframe template) is NOT checked here -- that is a _compose
    concern, so a bailed chain can still explain itself via technique_index.
    technique_index is accepted but unused by the gate; kept in the
    signature for symmetry with _resolve_technique."""
    connector_hits = scan["connector_hits"]
    hits = scan["technique_hits"]
    if not connector_hits or len(hits) < 2:
        return None

    seen = set()
    deduped = []
    for h in sorted(hits, key=lambda h: h["pos"]):
        if h["key"] in seen:
            continue
        seen.add(h["key"])
        deduped.append(h)
    if len(deduped) < 2:
        return None

    first_pos = deduped[0]["pos"]
    last_pos = deduped[-1]["pos"]
    if not any(first_pos < c["pos"] < last_pos for c in connector_hits):
        return None

    return deduped


def _resolve_technique(scan, adjective_keys_present, technique_index, flags):
    hits = scan["technique_hits"]
    if hits:
        winner = max(hits, key=lambda h: (h["priority"], -h["pos"]))
        seen = {winner["key"]}
        for h in sorted(hits, key=lambda h: h["pos"]):
            if h["key"] in seen:
                continue
            seen.add(h["key"])
            flags.append({"word": h["key"],
                          "reason": f"also mentioned: {h['key']} — using {winner['key']}"})
        return winner["key"]

    if "bright" in adjective_keys_present:
        default = "saw"
        trigger = "bright"
    elif "hollow" in adjective_keys_present:
        default = "square"
        trigger = "hollow"
    else:
        default = "saw"
        trigger = None
    reason = (f"no technique named — inferred '{default}' from '{trigger}'" if trigger
              else f"no technique named — defaulted to '{default}'")
    flags.append({"word": default, "reason": reason})
    return default


# ─── S3: composer ──────────────────────────────────────────────────────────

def _compose(scan, s2_extra_adjective_hits, lexicon, frames_override):
    flags = []
    technique_index = {t["key"]: t for t in lexicon["techniques"]}
    adjective_index = {a["key"]: a for a in lexicon["adjectives"]}
    motion_index = {m["key"]: m for m in lexicon["motions"]}

    all_adjective_hits = list(scan["adjective_hits"]) + list(s2_extra_adjective_hits)
    adjective_keys_present = {h["key"] for h in all_adjective_hits}

    # step 1: template. A connector word ("morphing into", "wird zu", an
    # arrow rewritten by S0's _ARROW_RE, ...) spanning >=2 distinct
    # technique mentions composes a multi-keyframe morph chain instead of
    # the usual single-winner resolution -- see _technique_sequence. A
    # chain bails (falls through to the normal path below) if any
    # participant's template is itself multi-keyframe: chaining chains is
    # not supported, and it is more honest to say so than to silently
    # flatten it.
    sequence = _technique_sequence(scan, technique_index)
    if sequence is not None:
        # cap at 4 participants FIRST -- an honest drop, not a silent
        # truncation. Order matters: the multi-part bail below must only
        # judge the participants that actually survive the cap, or a
        # would-be-dropped 5th waveform could discard the whole chain.
        if len(sequence) > 4:
            for dropped in sequence[4:]:
                flags.append({"word": dropped["key"],
                              "reason": "morph chain capped at 4 waveforms — dropped"})
            sequence = sequence[:4]
        non_chainable = next((h["key"] for h in sequence
                               if len(technique_index[h["key"]]["template"]["keyframes"]) != 1), None)
        if non_chainable is not None:
            flags.append({"word": non_chainable,
                          "reason": "morph chain includes a multi-part recipe — using the usual resolution"})
            sequence = None

    if sequence is not None:

        keys = [h["key"] for h in sequence]
        recipe = {
            "keyframes": [copy.deepcopy(technique_index[k]["template"]["keyframes"][0]) for k in keys],
            # Directional by default: a bare "A into B" morph moves forward and
            # ENDS on B (no return-to-start). An explicit cyclic motion word
            # (breathe/cycle/...) overrides both below in _apply_motion_intent.
            "motion": _motion_forward(len(keys)),
            "loop": False,
            # Documented bake-path standard: 256 frames (LCO_WAVE_INTERPOLATION_SPEC.md
            # sec.6; DcoBaker full resolution). A morph chain composes its own recipe
            # rather than copying one template, so the frame count is set explicitly here.
            "frames": 256,
            "motion_rate_hz": sum(technique_index[k]["template"].get("motion_rate_hz", 0.25)
                                   for k in keys) / len(keys),
        }
        for k in keys:
            for mflag in technique_index[k].get("mandatory_flags", []):
                flags.append({"word": k, "reason": mflag})
        if frames_override is not None:
            recipe["frames"] = frames_override
        resolved_technique = "->".join(keys)
    else:
        # honest about the degenerate case a chain-shaped prompt collapses
        # to: a connector was said, but there was never a second waveform
        # to morph into (NOT the same as the multi-part bail above, which
        # gets its own flag).
        if scan["connector_hits"] and len({h["key"] for h in scan["technique_hits"]}) < 2:
            flags.append({"word": "morph",
                          "reason": "only one waveform named — nothing to morph into"})
        resolved_technique = _resolve_technique(scan, adjective_keys_present, technique_index, flags)
        template = technique_index[resolved_technique]["template"]
        recipe = copy.deepcopy(template)
        for mflag in technique_index[resolved_technique].get("mandatory_flags", []):
            flags.append({"word": resolved_technique, "reason": mflag})
        if frames_override is not None:
            recipe["frames"] = frames_override

    # step 1b: typed base-waveform values (width/ratio/index/order) are
    # applied to the template keyframes BEFORE the adjective pass, so that a
    # spectral adjective converting a closed-form keyframe to additive bakes
    # the USER's value into the converted spectrum instead of the template
    # default (step 3 re-asserts on the non-converted path and records
    # resolved.values). Values are pre-clamped to their S4 ranges here
    # because a conversion bakes them into partials, where S4's own clamp
    # can no longer reach them; resolved.values still reports the raw typed
    # value (safe repairs happen silently).
    values = scan["values"]
    pre_applied = {}
    if "width" in values:
        w, _ = _clamp_float(values["width"], 0.02, 0.98, 0.5)
        pre_applied["width"] = _apply_width_override(recipe, w)
    if "ratio" in values:
        v, _ = _clamp_int(values["ratio"], 1, 8, 2)
        pre_applied["ratio"] = _apply_ratio_override(recipe, v)
    if "index" in values:
        v, _ = _clamp_float(values["index"], 0.0, 8.0, 1.0)
        pre_applied["index"] = _apply_index_override(recipe, v)
    if "order" in values:
        v, _ = _clamp_int(values["order"], 2, 12, 2)
        pre_applied["order"] = _apply_order_override(recipe, v)

    # step 1c: relative FM instructions ("deeper modulation", "higher ratio")
    # nudge the fm2 keyframe BEFORE the adjective pass, so a later spectral
    # adjective that converts fm2->additive bakes the nudged index/ratio in
    # (mirrors step 1b's typed-value ordering). Same _apply_delta_op fm path the
    # "metallic" adjective uses; a non-FM template gets the honest "no FM
    # operator" flag. Prompt order; no degree scaling (comparatives carry none).
    # applied_fm records every parsed op verbatim -- even one that flags as
    # inapplicable -- exactly as step 2b's applied_composition does.
    applied_fm = []
    for fop in scan.get("fm_ops", []):
        _apply_delta_op(recipe, fop["word"], fop["op"], fop["args"], flags)
        applied_fm.append(fop["word"])

    # step 2: adjective deltas, priority order then prompt order
    ordered = sorted(all_adjective_hits, key=lambda h: (-h["priority"], h["pos"]))
    applied_adjective_keys = []
    seen_keys = set()
    for hit in ordered:
        key = hit["key"]
        if key not in seen_keys:
            seen_keys.add(key)
            applied_adjective_keys.append(key)
        entry = adjective_index.get(key)
        if entry is None:
            continue
        degree = hit.get("degree", 1.0)
        for step in entry["delta"]:
            op_name = step["op"]
            raw_args = step["args"]
            scaled_args = _scale_op_args(op_name, raw_args, degree)
            _apply_delta_op(recipe, key, op_name, scaled_args, flags)

    # step 2b: explicit compositional harmonic instructions ("only odd
    # overtones", "attenuate every 3rd", "boost harmonic 5"). Typed by the
    # user, so they land AFTER the adjective deltas (explicit beats worded) and
    # in prompt order, through the same _ensure_additive + inapplicable-flag
    # path. args are final (no degree scaling — these carry no degree word).
    applied_composition = []
    for cop in scan.get("composition_ops", []):
        _apply_delta_op(recipe, cop["word"], cop["op"], cop["args"], flags)
        applied_composition.append(cop["word"])

    # step 3: explicit typed values override (typed beats worded beats
    # default). Base-waveform values were already applied in step 1b; this
    # pass (a) re-asserts them on keyframes that survived the adjective pass
    # un-converted and (b) records resolved.values. A value counts as applied
    # if EITHER pass landed it — step 1b's application survives inside a
    # converted additive spectrum even though the keyframe kind is gone.
    resolved_values = {}
    if "width" in values:
        if _apply_width_override(recipe, values["width"]) or pre_applied.get("width"):
            resolved_values["width"] = values["width"]
        else:
            flags.append({"word": f"{int(round(values['width']*100))}%",
                          "reason": "no pulse width in this recipe — ignored"})
    if "ratio" in values:
        if _apply_ratio_override(recipe, values["ratio"]) or pre_applied.get("ratio"):
            resolved_values["ratio"] = values["ratio"]
        else:
            flags.append({"word": f"ratio {values['ratio']}",
                          "reason": "no FM operator in this recipe — ignored"})
    if "index" in values:
        if _apply_index_override(recipe, values["index"]) or pre_applied.get("index"):
            resolved_values["index"] = values["index"]
        else:
            flags.append({"word": f"index {values['index']}",
                          "reason": "no FM operator in this recipe — ignored"})
    if "order" in values:
        if _apply_order_override(recipe, values["order"]) or pre_applied.get("order"):
            resolved_values["order"] = values["order"]
        else:
            flags.append({"word": f"order {values['order']}",
                          "reason": "no waveshaper in this recipe — ignored"})
    if "ceiling" in values:
        applied = False
        for kf in recipe["keyframes"]:
            conv = _ensure_additive(kf)
            if conv is not None:
                _op_ceiling(conv, values["ceiling"])
                applied = True
        if applied:
            resolved_values["ceiling"] = values["ceiling"]
        else:
            flags.append({"word": f"{values['ceiling']} harmonics",
                          "reason": "no additive-convertible keyframe in this recipe — ignored"})

    # step 4: motion intent (winner-takes-all among "intent" hits; "speed"
    # hits are cumulative modifiers applied after, like an adjective delta)
    motion_hits = scan["motion_hits"]
    intent_hits = [h for h in motion_hits if h["motion_category"] == "intent"]
    speed_hits = [h for h in motion_hits if h["motion_category"] == "speed"]
    applied_motion_keys = []

    if intent_hits:
        winner = max(intent_hits, key=lambda h: (h["priority"], -h["pos"]))
        seen = {winner["key"]}
        for h in sorted(intent_hits, key=lambda h: h["pos"]):
            if h["key"] in seen:
                continue
            seen.add(h["key"])
            flags.append({"word": h["key"],
                          "reason": f"also mentioned: {h['key']} — using {winner['key']}"})
        if _apply_motion_intent(recipe, winner["key"], flags):
            # The gesture brings its own natural tempo when its lexicon
            # entry carries one: the rewrite replaced the loop, so its
            # motion_rate_hz replaces the template's. Entries without the
            # field keep the template rate. Speed words (below) scale it
            # afterwards, so "slow sweep" = sweep's rate x 0.5.
            m_entry = motion_index.get(winner["key"], {})
            if "motion_rate_hz" in m_entry:
                recipe["motion_rate_hz"] = m_entry["motion_rate_hz"]
        applied_motion_keys.append(winner["key"])

    for h in sorted(speed_hits, key=lambda h: h["pos"]):
        scale = _MOTION_SPEED_SCALE[h["key"]]
        _apply_motion_rate(recipe, scale)
        applied_motion_keys.append(h["key"])

    # step 4b: texture hits (vibrate/wobble/flutter/tremolo/shimmer). NOT motion —
    # a per-frame TIMBRAL variation realized frame-by-frame in dco_frames
    # (_apply_texture), never a scan trajectory. Winner-takes-all like an intent
    # (also-mentioned textures flagged). Recorded ONLY on resolved.textures so
    # plan_from_response can pick it up; the recipe WIRE is left untouched, so a
    # texture prompt's baked/sets recipe stays byte-identical (the non-regression
    # gate holds — textures live in the new frame engine, not the shipping wire).
    texture_hits = [h for h in motion_hits if h["motion_category"] == "texture"]
    applied_texture_keys = []
    if texture_hits:
        twin = max(texture_hits, key=lambda h: (h["priority"], -h["pos"]))
        seen = {twin["key"]}
        for h in sorted(texture_hits, key=lambda h: h["pos"]):
            if h["key"] in seen:
                continue
            seen.add(h["key"])
            flags.append({"word": h["key"],
                          "reason": f"also mentioned: {h['key']} — using {twin['key']}"})
        applied_texture_keys.append(twin["key"])

    # step 5: clamp everything, force loop-closure on the start keyframe
    recipe, repairs = _clamp_and_repair(recipe)

    # step 6: station pipeline (LCO wave-interpolation, Slice 2a). A multi-keyframe
    # INHARMONIC chain cannot bake (a bell would come out a sawtooth); convert it
    # to union-aligned additive stations the shipped engine blends by scan and
    # emit the additive-sets router contract. A strict no-op for harmonic chains,
    # (baked path stays byte-identical) and for a lone inharmonic station (already
    # a valid K==1 sets recipe) -- see _stationize.
    recipe = _stationize(recipe, flags)

    resolved = {
        "technique": resolved_technique,
        "adjectives": applied_adjective_keys,
        "composition": applied_composition,
        "fm": applied_fm,
        "motion": applied_motion_keys,
        "textures": applied_texture_keys,
        "values": resolved_values,
    }
    return recipe, resolved, flags


def _scale_op_args(op_name, raw_args, degree):
    """Scale a delta program step's magnitude args by the degree multiplier
    (slightly=0.5, very=1.5, extremely=2.0, default 1.0). Only magnitude-ish
    args scale; harmonic INDICES (h, from_h) are addresses, not magnitudes,
    and never scale."""
    args = dict(raw_args)
    if op_name == "tilt":
        args["db_per_oct"] = args["db_per_oct"] * degree
    elif op_name == "even_odd":
        args["balance"] = max(-1.0, min(1.0, args["balance"] * degree))
    elif op_name == "ceiling":
        pass  # a harmonic-count target, not a magnitude -- degree-invariant
    elif op_name in ("boost", "cut"):
        args["amount"] = args["amount"] * degree
    elif op_name in ("fm_index", "fm_ratio", "width"):
        args["delta"] = args["delta"] * degree
    elif op_name in ("motion_rate", "motion_depth"):
        # scale is itself a multiplier already centered on 1.0; degree
        # pushes it further from 1.0 in the same direction
        args["scale"] = 1.0 + (args["scale"] - 1.0) * degree
    elif op_name == "drive":
        args["amount"] = args["amount"] * degree
    elif op_name == "inharm":
        args["amount"] = args["amount"] * degree
    elif op_name in ("comb", "scale_h"):
        # factor is a multiplier centered on 1.0 (like motion_rate/motion_depth);
        # degree pushes it further from unity in the same direction
        args["factor"] = 1.0 + (args["factor"] - 1.0) * degree
    return args


def _apply_width_override(recipe, width):
    applied = False
    for kf in recipe["keyframes"]:
        if kf.get("kind") == "pulse":
            kf["width"] = width
            applied = True
    return applied


def _apply_ratio_override(recipe, ratio):
    applied = False
    for kf in recipe["keyframes"]:
        if kf.get("kind") == "fm2":
            kf["ratio"] = ratio
            applied = True
    return applied


def _apply_index_override(recipe, index):
    applied = False
    for kf in recipe["keyframes"]:
        if kf.get("kind") == "fm2":
            kf["index"] = index
            applied = True
    return applied


def _apply_order_override(recipe, order):
    applied = False
    for kf in recipe["keyframes"]:
        if kf.get("kind") == "cheby":
            kf["order"] = order
            applied = True
    return applied


# ─── S4: validate / repair / fallback ──────────────────────────────────────

_ALLOWED_KINDS = {"saw", "square", "pulse", "triangle", "additive", "fm2", "cheby", "ring"}
_ALLOWED_CURVES = {"lin", "fast", "slow"}


def _clamp_float(v, lo, hi, default):
    try:
        v = float(v)
    except (TypeError, ValueError):
        return default, True
    c = max(lo, min(hi, v))
    return c, (c != v)


def _clamp_int(v, lo, hi, default):
    try:
        v = int(round(float(v)))
    except (TypeError, ValueError):
        return default, True
    c = max(lo, min(hi, v))
    return c, (c != v)


def _clamp_and_repair(recipe):
    """Shared clamp/repair pass, used both as S3's own final clamp step and
    as (the bulk of) S4's validate_recipe. Never raises; always returns a
    structurally bakeable recipe. Mutates a deep-copied recipe, never the
    caller's object."""
    recipe = copy.deepcopy(recipe) if recipe is not None else {}
    repairs = []

    if not isinstance(recipe, dict):
        recipe = {}
        repairs.append("recipe was not an object — rebuilt from scratch")

    keyframes = recipe.get("keyframes")
    if not isinstance(keyframes, list) or not keyframes:
        keyframes = [{"kind": "additive", "partials": [{"h": 1, "a": 1.0, "phase": 0.0}]}]
        repairs.append("empty/missing keyframes — injected a default sine keyframe")
    if len(keyframes) > _SUBSTATION_CAP:
        keyframes = keyframes[:_SUBSTATION_CAP]
        repairs.append(f"more than {_SUBSTATION_CAP} keyframes — truncated to {_SUBSTATION_CAP}")

    clean_keyframes = []
    for kf in keyframes:
        if not isinstance(kf, dict):
            kf = {}
        kind = kf.get("kind")
        if kind not in _ALLOWED_KINDS:
            kind = "additive"
            repairs.append(f"keyframe kind {kf.get('kind')!r} not in the allowed set — coerced to additive")
        ckf = {"kind": kind}

        if kind == "additive":
            partials = kf.get("partials")
            if not isinstance(partials, list) or not partials:
                partials = [{"h": 1, "a": 1.0, "phase": 0.0}]
                repairs.append("additive keyframe with no partials — injected h1")
            clean_partials = []
            dropped_bad_h = 0
            for p in partials[:64] if len(partials) > 64 else partials:
                if not isinstance(p, dict):
                    continue
                # h is a FLOAT ratio, not an integer harmonic index: a non-integer h
                # is an inharmonic partial (bell/metal/glass), which the engine now
                # synthesizes directly (real-time additive) instead of projecting onto
                # the harmonic grid. Rounding it here (the old _clamp_int) was the
                # backend half of the "inharmonicity unrepresentable" assumption that
                # made bells bake to sawtooths. Harmonic recipes carry integer-valued
                # h (1.0, 2.0, ...) that the spectral ops still match exactly.
                # h>0 VALIDITY (LCO spec, Slice 2a): a NUMERIC but non-finite or
                # non-positive h is DROPPED, not clamped up to 0.03125 -- the engine
                # drops h<=0 at synthesis (silent partial), so keeping it was a
                # silent hole; dropping it is the honest repair. A non-NUMERIC h
                # still falls through to _clamp_float's default (unchanged), so a
                # valid partial (and every harmonic template) is byte-identical.
                h_raw = p.get("h", 1.0)
                if (isinstance(h_raw, (int, float)) and not isinstance(h_raw, bool)
                        and (not math.isfinite(h_raw) or h_raw <= 0.0)):
                    dropped_bad_h += 1
                    continue
                h, h_bad = _clamp_float(h_raw, 0.03125, 1024.0, 1.0)
                a, a_bad = _clamp_float(p.get("a", 0.0), 0.0, 1.0, 0.0)
                try:
                    phase = float(p.get("phase", 0.0))
                except (TypeError, ValueError):
                    phase = 0.0
                if h_bad or a_bad:
                    repairs.append(f"partial h={p.get('h')} a={p.get('a')} clamped")
                clean_partials.append({"h": h, "a": a, "phase": phase})
            if dropped_bad_h:
                repairs.append(f"dropped {dropped_bad_h} partial(s) with non-finite or h<=0")
            if len(partials) > 64:
                repairs.append("more than 64 partials — truncated to 64")
            if not clean_partials:
                clean_partials = [{"h": 1, "a": 1.0, "phase": 0.0}]
            elif not any(p["a"] > 1e-6 for p in clean_partials):
                # Never emit a SILENT (or near-silent) additive keyframe: the
                # baker peak-normalizes, and at/below 1e-6 it skips normalize and
                # emits ~-120 dB frames (DcoBaker.cpp bake() uses the same 1e-6
                # floor). An op zeroed the spectrum (e.g. "kill harmonic 1" on a
                # sine, or reducing h1 twelve times) — restore the fundamental.
                # Mirrors the empty-partials guard above and _op_ceiling's invariant.
                clean_partials = [{"h": 1, "a": 1.0, "phase": 0.0}]
                repairs.append("all-zero additive spectrum — restored the fundamental")
            ckf["partials"] = clean_partials
        elif kind == "pulse":
            w, bad = _clamp_float(kf.get("width", 0.5), 0.02, 0.98, 0.5)
            if bad:
                repairs.append(f"pulse width {kf.get('width')} clamped to {w}")
            ckf["width"] = w
        elif kind == "fm2":
            r, r_bad = _clamp_int(kf.get("ratio", 2), 1, 8, 2)
            idx, i_bad = _clamp_float(kf.get("index", 1.0), 0.0, 8.0, 1.0)
            if r_bad:
                repairs.append(f"fm2 ratio {kf.get('ratio')} clamped to {r}")
            if i_bad:
                repairs.append(f"fm2 index {kf.get('index')} clamped to {idx}")
            ckf["ratio"] = r
            ckf["index"] = idx
        elif kind == "cheby":
            order, o_bad = _clamp_int(kf.get("order", 3), 2, 12, 3)
            drive, d_bad = _clamp_float(kf.get("drive", 0.7), 0.1, 1.0, 0.7)
            if o_bad:
                repairs.append(f"cheby order {kf.get('order')} clamped to {order}")
            if d_bad:
                repairs.append(f"cheby drive {kf.get('drive')} clamped to {drive}")
            ckf["order"] = order
            ckf["drive"] = drive
        elif kind == "ring":
            r, r_bad = _clamp_int(kf.get("ratio", 2), 1, 8, 2)
            mix, m_bad = _clamp_float(kf.get("mix", 1.0), 0.0, 1.0, 1.0)
            if r_bad:
                repairs.append(f"ring ratio {kf.get('ratio')} clamped to {r}")
            if m_bad:
                repairs.append(f"ring mix {kf.get('mix')} clamped to {mix}")
            ckf["ratio"] = r
            ckf["mix"] = mix
        # saw/square/triangle: no extra fields

        sh, _ = _clamp_float(kf.get("shape", 0.0), 0.0, 1.0, 0.0)
        if sh > 0.0:
            ckf["shape"] = sh
        clean_keyframes.append(ckf)
    keyframes = clean_keyframes
    recipe["keyframes"] = keyframes
    K = len(keyframes)

    motion = recipe.get("motion")
    if not isinstance(motion, list):
        motion = []
        repairs.append("missing motion — reset to empty")
    if len(motion) > 16:
        motion = motion[:16]
        repairs.append("more than 16 motion segments — truncated to 16")
    clean_motion = []
    for m in motion:
        if not isinstance(m, dict):
            continue
        to, to_bad = _clamp_int(m.get("to", 0), 0, max(0, K - 1), 0)
        try:
            dur = float(m.get("dur_frac", 0.0))
        except (TypeError, ValueError):
            dur = 0.0
        dur = max(0.0, dur)
        curve = m.get("curve", "lin")
        if curve not in _ALLOWED_CURVES:
            curve = "lin"
        if to_bad:
            repairs.append(f"motion 'to' {m.get('to')} out of range — clamped to {to}")
        clean_motion.append({"to": to, "dur_frac": dur, "curve": curve})
    motion = clean_motion

    if motion:
        motion[0]["dur_frac"] = 0.0
        if len(motion) > 1:
            total = sum(m["dur_frac"] for m in motion[1:])
            if total <= 1e-9:
                share = 1.0 / (len(motion) - 1)
                for m in motion[1:]:
                    m["dur_frac"] = share
                repairs.append("motion dur_frac summed to 0 — reset to an equal split")
            else:
                for m in motion[1:]:
                    m["dur_frac"] = m["dur_frac"] / total

    loop = bool(recipe.get("loop", True))
    # A looping recipe must END on the keyframe it STARTED on (motion[0].to,
    # whatever that is — a "close" trajectory legitimately starts on K-1),
    # so the wavetable's frame[N-1]->frame[0] wrap lands on identical
    # spectra. Forcing 0 here would be wrong for any non-0 start.
    if loop and len(motion) > 1 and motion[-1]["to"] != motion[0]["to"]:
        motion[-1]["to"] = motion[0]["to"]
        repairs.append("loop recipe did not close on its start keyframe — forced")
    recipe["motion"] = motion
    recipe["loop"] = loop

    # Missing/invalid frames -> 256, the documented bake-path standard (LCO spec sec.6),
    # not a half-resolution 128. Every lexicon template already carries frames=256, so
    # this default only fires on external/malformed input.
    frames, f_bad = _clamp_int(recipe.get("frames", 256), 8, 256, 256)
    if f_bad:
        repairs.append(f"frames {recipe.get('frames')} clamped to {frames}")
    recipe["frames"] = frames

    # Absolute motion tempo (full motion loops per second) for the C++ DCO
    # motion driver. Neutral default 0.25 when missing (external/malformed
    # input — every lexicon template carries the field since version 2).
    rate, r_bad = _clamp_float(recipe.get("motion_rate_hz", 0.25), 0.02, 8.0, 0.25)
    if r_bad:
        repairs.append(f"motion_rate_hz {recipe.get('motion_rate_hz')} clamped to {rate}")
    recipe["motion_rate_hz"] = rate

    return recipe, repairs


def validate_recipe(recipe):
    """S4. Structural validation of a composed recipe: counts, ranges,
    dur_frac normalization, loop-closure. Safe repairs happen silently
    (returned in ``repairs`` for callers/tests that want to inspect them);
    this function never raises and always returns a structurally bakeable
    recipe -- even for a badly malformed or empty input dict."""
    return _clamp_and_repair(recipe)


def _dedupe_flags(flags):
    """Order-preserving removal of exactly-identical (word, reason) flags.
    Per-op flagging in _apply_delta_op legitimately produces the same flag
    once per inapplicable op (e.g. an adjective with two FM ops on a non-FM
    recipe); the user needs to read it once."""
    seen = set()
    out = []
    for f in flags:
        key = (f.get("word"), f.get("reason"))
        if key in seen:
            continue
        seen.add(key)
        out.append(f)
    return out


# ─── flag display tiers ────────────────────────────────────────────────────
# Every honesty flag carries a "tier" the C++ panel groups by, so the ~1 word
# the system genuinely could not read stops drowning in the honest disclosures
# that are the vast majority of flags. Two tiers:
#   "unresolved" — the token never became sound-shaping. ACTIONABLE: a residue
#                  word S2 could not route (add a lexicon entry), the S2 word
#                  budget overflowed (shorten the prompt), or S4 composition
#                  failed outright (a real fault). This is the "Not understood"
#                  count the user reacts to.
#   "adapted"    — it DID become sound-shaping, and the flag honestly discloses
#                  how (approximated / inapplicable-here / defaulted / clamped).
# Closed allowlist of unresolved reason stems; the DEFAULT is "adapted", so a
# newly added honest-disclosure reason is correctly-tiered by construction and
# never silently promoted into the actionable count. "no mapping" is a distinct
# prefix from the many adapted "no FM operator ..." / "no pulse width ..." ones.
_UNRESOLVED_REASON_STEMS = ("no mapping", "unprocessed", "recipe composition failed")


def _flag_tier(reason):
    """Map a flag's reason string to its display tier (see above). Pure and
    deterministic -- same reason -> same tier -- so stamping it leaves the
    'same text -> byte-identical response' invariant intact."""
    return "unresolved" if reason.startswith(_UNRESOLVED_REASON_STEMS) else "adapted"


# ─── reference vocabulary (Re-Prompt grounding) ────────────────────────────
# The controlled vocabulary the S0->S1 scanner actually resolves, formatted as
# a compact palette for the Re-Prompt LLM. Every term here is a real surface
# form (or composition pattern) _scan / _extract_composition_ops recognizes --
# it is the SAME vocabulary used "für die Auswertung" (for parsing), handed to
# the generator so a re-prompt stops emitting words the instrument silently
# drops. One canonical (first-ASCII) surface form per key keeps it a clean
# concept palette instead of every inflected / German synonym the scanner also
# accepts; because each canonical form is ITSELF a registered surface form, the
# brief never promises a word the parser cannot honour. Derived live from the
# lexicon + the shared composition-verb taxonomy -- there is no second, hand-
# maintained list that could drift out of sync with what _scan matches.

def _canonical_surface_form(item):
    """First ASCII surface form of a lexicon entry (the clean English one),
    falling back to the first form when every surface form is non-ASCII, and to
    the entry's key when the list is empty. Total by design: a malformed lexicon
    entry (empty surface_forms) must never raise here, because reference_
    vocabulary runs OUTSIDE author_recipe's _compose try/except -- a raise would
    break author_recipe's 'any text input -> ok:True' guarantee for every bake."""
    forms = item.get("surface_forms") or []
    for sf in forms:
        if all(ord(c) < 128 for c in sf):
            return sf
    return forms[0] if forms else item.get("key", "")


def reference_vocabulary(lexicon=None):
    """A human-readable brief of the exact vocabulary author_recipe resolves,
    for grounding the Re-Prompt LLM (docs/DCO_REPROMPT_CONCEPT.md). Pure lexicon
    derivation -- no model, deterministic, cheap enough to rebuild per bake."""
    lex = lexicon if lexicon is not None else load_lexicon()

    waveforms = [_canonical_surface_form(t) for t in lex["techniques"]]
    qualities = [_canonical_surface_form(a) for a in lex["adjectives"]]
    motions   = [_canonical_surface_form(m) for m in lex["motions"]]
    # the morph connector ("saw into square") and the intensity degrees are
    # scanner categories too -- surface them so chained / graded prompts parse.
    morph = _canonical_surface_form(lex["connectors"][0]) if lex.get("connectors") else "into"
    # One representative per intensity tier (dedupe by multiplier, first-seen):
    # collapses the graded synonyms -- incl. the German ones -- to the English
    # leads (slightly / very / extremely), a cleaner steer than the full list.
    # Relies on the lexicon listing the English form first in each tier (its
    # current convention); a stray German lead would still parse, just read odd.
    _seen_mult, intensity = set(), []
    for _word, _mult in lex["degrees"].items():
        if _mult not in _seen_mult:
            _seen_mult.add(_mult)
            intensity.append(_word)

    # composition-op verbs (additive harmonic addressing): "<verb> the Nth
    # harmonic", "<verb> every Nth harmonic", "only odd|even harmonics".
    boost  = ", ".join(sorted(_BOOST_VERBS))
    reduce = ", ".join(sorted(_REDUCE_VERBS))
    remove = ", ".join(sorted(_REMOVE_VERBS))

    # relative FM direction words (only bite on an FM/bell/EP recipe).
    fm_amount  = ", ".join(sorted(_FM_INDEX_UP | _FM_INDEX_DOWN))
    fm_spacing = ", ".join(sorted(_FM_RATIO_UP | _FM_RATIO_DOWN))

    # Framed as ACOUSTIC / SPECTRAL qualities of the SOUND -- not "the synth's
    # controls". The lexicon is a glass box of how a sound is heard and shaped
    # (bright, hollow, harmonic structure, movement), the human-negotiable
    # counterpart of the neural embedding; naming it "the machine's vocabulary"
    # is a category error that pushes the re-prompt LLM into machine-speak.
    return "\n".join([
        "BASE WAVEFORM — the raw tone's harmonic makeup (pick one, or morph "
        "two with '" + morph + "'): " + ", ".join(waveforms),
        "SPECTRAL CHARACTER: " + ", ".join(qualities),
        "MOVEMENT over time: " + ", ".join(motions),
        "DEGREE: " + ", ".join(intensity),
        "HARMONIC STRUCTURE: '<verb> the Nth harmonic', '<verb> every Nth harmonic', "
        "'only odd/even harmonics' (N = 2..10)",
        "  strengthen: " + boost,
        "  soften: " + reduce,
        "  remove: " + remove,
        "FM RICHNESS (bell / electric piano / metal tones): "
        "'<dir> modulation' = sideband density, '<dir> ratio' = partial spacing",
        "  denser / sparser: " + fm_amount,
        "  wider / closer: " + fm_spacing,
    ])


# ─── top level: author_recipe ──────────────────────────────────────────────

def author_recipe(text, llm_route=None, frames=None):
    """The full S0->S4 pipeline. Returns the response dict of
    docs/DCO_LLM_GUARDRAILS.md S5:
      {"ok", "recipe", "resolved", "flags", "lexicon_version"}

    ``llm_route`` implements S2: a callable
      (residue_words: list[str], allowed_keys: list[str]) -> dict[str, str|None]
    or None to skip S2 entirely (every residue word is then flagged --
    deterministic degradation, never a crash, never a fabricated mapping).

    S4 guarantees a bakeable recipe for any text input; ok is always True
    here. ok:false is reserved for a genuine transport-level failure (an
    exception raised before S4 could even run), which this function lets
    propagate to the caller rather than swallow -- pipe_inference.py's own
    request handler already turns any exception into the standard \\x00
    error frame, so there is no need to catch it a second time here.
    """
    lexicon = load_lexicon()
    adjective_index = {a["key"]: a for a in lexicon["adjectives"]}
    norm_text = _normalize(text)
    scan = _scan(norm_text, lexicon)
    s2_extra_hits, s2_flags = _run_s2(scan["residue"], adjective_index, llm_route)

    try:
        recipe, resolved, flags = _compose(scan, s2_extra_hits, lexicon, frames)
        flags = flags + s2_flags
    except Exception as e:
        # Unrepairable composition failure: fall back to the plain template
        # of whatever technique S1 alone resolved (S1 itself cannot raise --
        # it is pure lookup/regex code -- so this fallback is always
        # available). Never an error tone, never a crash.
        technique_index = {t["key"]: t for t in lexicon["techniques"]}
        fallback_flags = []
        resolved_technique = _resolve_technique(scan, set(), technique_index, fallback_flags)
        template = technique_index[resolved_technique]["template"]
        recipe = copy.deepcopy(template)
        if frames is not None:
            recipe["frames"] = frames
        recipe, _ = _clamp_and_repair(recipe)
        resolved = {"technique": resolved_technique, "adjectives": [], "composition": [],
                    "fm": [], "motion": [], "values": {}}
        flags = s2_flags + [{"word": resolved_technique,
                             "reason": f"recipe composition failed ({e}) — fell back to the plain technique template"}]

    return {
        "ok": True,
        "recipe": recipe,
        "resolved": resolved,
        # Stamp the display tier on each surviving flag (a copy -- never mutate
        # the per-op dicts _compose built). Pure function of the reason, so the
        # response stays byte-identical for identical input.
        "flags": [dict(f, tier=_flag_tier(f.get("reason", "")))
                  for f in _dedupe_flags(flags)],
        "lexicon_version": lexicon["lexicon_version"],
        # Sibling key (NOT inside "recipe", so the baked recipe stays byte-
        # identical): the Re-Prompt LLM's allowed palette, so its rewrites use
        # words this same pipeline can resolve. Static per lexicon; the C++ side
        # caches the first non-empty one it sees. Reuses the lexicon already
        # loaded above -- no second file read.
        "reference_vocabulary": reference_vocabulary(lexicon),
    }
