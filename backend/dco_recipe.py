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
                     "order": int, "drive": float, "mix": float }, ... ],  # <= 8
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


_MOTION_NEEDS_K2 = {"open_up", "close", "sweep", "wobble", "pingpong", "breathe",
                    "evolve", "flutter", "vibrate", "settle"}

_MOTION_REWRITE = {
    "open_up": _motion_open_up,
    "close": _motion_close,
    "sweep": _motion_sweep,
    "wobble": lambda K: _motion_periodic(K, 2, "fast"),
    "pingpong": lambda K: _motion_cycle(K) if K > 2 else _motion_sweep(K),
    "breathe": _motion_breathe,
    "evolve": _motion_evolve,
    "static": _motion_static,
    "cycle": _motion_cycle,
    "flutter": lambda K: _motion_periodic(K, 4, "fast"),
    "vibrate": lambda K: _motion_periodic(K, 6, "fast"),
    "settle": _motion_settle,
}

_MOTION_SPEED_SCALE = {"slow": 0.5, "fast": 1.7, "snap": 3.0}


def _apply_motion_intent(recipe, intent_key, flags):
    """Returns True iff the loop was actually rewritten (the caller only
    applies the intent's own motion_rate_hz override in that case — a bailed
    rewrite must leave the template's rate untouched)."""
    K = len(recipe["keyframes"])
    if intent_key in _MOTION_NEEDS_K2 and K < 2:
        flags.append({"word": intent_key,
                      "reason": "only one keyframe in this recipe — motion has nothing to move between"})
        return False
    fn = _MOTION_REWRITE.get(intent_key)
    if fn is None:
        return False
    recipe["motion"] = fn(K)
    return True


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
            "motion": _motion_cycle(len(keys)),
            "loop": True,
            "frames": 128,
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
            # afterwards, so "slow wobble" = wobble's rate x 0.5.
            m_entry = motion_index.get(winner["key"], {})
            if "motion_rate_hz" in m_entry:
                recipe["motion_rate_hz"] = m_entry["motion_rate_hz"]
        applied_motion_keys.append(winner["key"])

    for h in sorted(speed_hits, key=lambda h: h["pos"]):
        scale = _MOTION_SPEED_SCALE[h["key"]]
        _apply_motion_rate(recipe, scale)
        applied_motion_keys.append(h["key"])

    # step 5: clamp everything, force loop-closure on the start keyframe
    recipe, repairs = _clamp_and_repair(recipe)

    resolved = {
        "technique": resolved_technique,
        "adjectives": applied_adjective_keys,
        "composition": applied_composition,
        "fm": applied_fm,
        "motion": applied_motion_keys,
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
    if len(keyframes) > 8:
        keyframes = keyframes[:8]
        repairs.append("more than 8 keyframes — truncated to 8")

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
            for p in partials[:64] if len(partials) > 64 else partials:
                if not isinstance(p, dict):
                    continue
                # h is a FLOAT ratio, not an integer harmonic index: a non-integer h
                # is an inharmonic partial (bell/metal/glass), which the engine now
                # synthesizes directly (real-time additive) instead of projecting onto
                # the harmonic grid. Rounding it here (the old _clamp_int) was the
                # backend half of the "inharmonicity unrepresentable" assumption that
                # made bells bake to sawtooths. Harmonic recipes carry integer-valued
                # h (1.0, 2.0, ...) that the spectral ops still match exactly. Lower
                # bound stays >0 (the engine drops h<=0); allow sub-fundamental hums.
                h, h_bad = _clamp_float(p.get("h", 1.0), 0.03125, 1024.0, 1.0)
                a, a_bad = _clamp_float(p.get("a", 0.0), 0.0, 1.0, 0.0)
                try:
                    phase = float(p.get("phase", 0.0))
                except (TypeError, ValueError):
                    phase = 0.0
                if h_bad or a_bad:
                    repairs.append(f"partial h={p.get('h')} a={p.get('a')} clamped")
                clean_partials.append({"h": h, "a": a, "phase": phase})
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

    frames, f_bad = _clamp_int(recipe.get("frames", 128), 8, 256, 128)
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
