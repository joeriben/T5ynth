#!/usr/bin/env python3
"""LCO recipe author, LLM-map variant — THE shipping entry (promoted from
proof-of-concept 2026-07-14, BJ's standing order): this synth is an
LLM-based language-understanding synth. The ONE language-UNDERSTANDING LLM
call below reads the whole prompt and emits curated dco_lexicon.json KEYS;
those keys TRIGGER dco_recipe's deterministic composer/validator, which
builds the recipe exactly. Without the model there is no oscillator
(pipe_inference refuses mode=="dco" before this module is reached) — there
is NO deterministic prompt path. dco_recipe.author_recipe's exact-phrase
word-scan remains only as an internal construction driver for the unit
suite, never as a prompt entry.

It swaps S1 (keyword scan -> technique/adjective/motion hits) for the LLM
semantic router, while S0 (_normalize), the typed-value/composition/FM regex
pre-passes, S3 (_compose) and S4 (validate_recipe) are dco_recipe's OWN
code — imported and called unmodified, never reimplemented. This carries forward
docs/DCO_LLM_GUARDRAILS.md's central guardrail unchanged: the LLM never
authors DSP data, it only routes. Here it routes WORDS -> LEXICON KEYS (the
base technique(s), adjectives, and the motion), the same kind of decision S2
already makes for residue words, just for the technique/adjective/motion
categories a plain word-scan used to own. Closed-enum validation against the
lexicon's own key sets is the injection guard: any key text the LLM returns
that is not a real, current lexicon key is DROPPED and flagged, never
composed into the recipe — no LLM output ever reaches the recipe except
through that whitelist.

llm_map_to_recipe() returns a dict IDENTICAL in shape to
dco_recipe.author_recipe()'s return: {ok, recipe, resolved, flags,
lexicon_version, reference_vocabulary}.

Pure Python; no direct transformers/torch import here (mirrors dco_recipe.py
/ lco_author.py's own caller-injects-the-model convention: ``llm`` is a
plain ``(text, system_prompt, max_new_tokens) -> str`` callable supplied by
the caller), so this module imports and unit-tests cheaply with a stub llm,
no model load required.
"""
import copy
import re

import dco_recipe
import lco_author


# ─── the ONE LLM call: system prompt + runtime catalogue ──────────────────
# VERBATIM system-prompt head (docs/DCO_LLM_GUARDRAILS.md's routing principle,
# applied here to base technique/adjective/motion selection instead of only
# S2 residue routing): the model chooses ONLY from the catalogue appended
# below, never invents a name or a number. The catalogue is generated from
# the lexicon's own "why" fields (see _build_catalogue) — the exact same
# strings a human reads in dco_lexicon.json — so the model's briefing can
# never drift out of sync with the real key set the closed-enum guard (step
# 5, _validate_keys) validates against below.
_SYSTEM_PROMPT_HEAD = (
    "You translate a sound description into choices from a fixed catalogue of "
    "synthesis tools. You never invent names or numbers — you only choose keys "
    "from the catalogue.\n"
    "Read the description and reply in EXACTLY this format and nothing else:\n"
    "TECHNIQUE: <key> [> <key> ...]\n"
    "ADJECTIVES: <key>, <key>, ...\n"
    "MOTION: <key>\n"
    "Rules: TECHNIQUE lists the base waveform/synthesis method(s); if the sound "
    "morphs from one into another, list them in order separated by \" > \"; "
    "otherwise one key. ADJECTIVES are timbral modifiers (or \"none\"). MOTION "
    "is how the timbre moves over time (or \"none\"). Match by MEANING even if "
    "the wording differs (a \"church bell\" is the bell technique; \"shimmering\" "
    "is a motion; \"harsh\"/\"aggressive\" are adjectives). Use ONLY keys from "
    "the catalogue below; if nothing in a category fits, write \"none\".\n"
    "CATALOGUE:\n"
)

# None = no cap: run_instruct then sizes the reply to the real model context
# (ctx - used - 16). A fixed number here truncates mid-sentence WITHOUT raising,
# so a clipped recipe reads as a model that answered badly rather than one that
# was cut off. This path is currently dormant (the live Csound author reaches
# only _build_catalogue/_validate_keys), and the cap is cleared now so reviving
# the path cannot revive the cap with it.
_MAX_NEW_TOKENS = None


def _params_line(t):
    """A parametrised technique's controls, as one compact line the routing
    model can actually use: `  params: name=anchor|anchor|anchor  name=...`,
    anchors ordered low value to high so the AXIS DIRECTION is visible.

    This line is not decoration. The csound system prompt tells the model to
    read a key's "params:" line for its parameter names and anchor words, and
    for the first two parametrised keys that line did not exist -- the lexicon
    carried the anchors and NOTHING emitted them, so the model was sent to a
    line that was never written and had to guess both the names and the values
    from prose. Anything it guessed that was not an exact anchor word or a bare
    0-1 number was silently dropped back to the default. Symptom, from a real
    session: "rhodes" and "piano" stopped reaching the electric piano at all.

    Only the anchor WORDS go in, never the numbers or the glosses: the words
    are what a small model produces reliably, the per-anchor gloss text stays
    in the lexicon for a human reading it, and the key's own "why" already says
    in one clause what each control does. Repeating any of that here would cost
    catalogue tokens for nothing."""
    params = t.get("params")
    if not params:
        return None
    out = []
    for name, spec in params.items():
        anchors = spec.get("anchors") or {}
        if not anchors:
            continue
        words = [w for w, _ in sorted(anchors.items(), key=lambda kv: kv[1]["value"])]
        out.append(f"{name}={'|'.join(words)}")
    return "  params: " + "  ".join(out) if out else None


def _build_catalogue(lexicon):
    """TECHNIQUES / ADJECTIVES / MOTIONS, one '<key>: <why>' line per entry —
    the lexicon's own "why" fields ARE the interpretation aid handed to the
    model (no second, hand-maintained gloss that could drift out of sync) —
    plus, for a parametrised technique, an indented "params:" line naming its
    controls and their anchor words (see _params_line)."""
    lines = ["TECHNIQUES:"]
    for t in lexicon["techniques"]:
        lines.append(f"{t['key']}: {t['why']}")
        pl = _params_line(t)
        if pl:
            lines.append(pl)
    lines.append("ADJECTIVES:")
    for a in lexicon["adjectives"]:
        lines.append(f"{a['key']}: {a['why']}")
    lines.append("MOTIONS:")
    for m in lexicon["motions"]:
        lines.append(f"{m['key']}: {m['why']}")
    return "\n".join(lines)


# ─── reply parsing + the closed-enum injection guard ───────────────────────

# Trailing "S" optional on every label: the required reply format is singular
# "TECHNIQUE:"/"MOTION:" (plural "ADJECTIVES:"), but the CATALOGUE appended to
# the same system prompt headers its own sections "TECHNIQUES:"/"ADJECTIVES:"/
# "MOTIONS:" (plural) -- a small model echoing the spelling it just read is a
# real, observed failure mode (verified empirically: a reply using the
# catalogue's plural headers verbatim was silently mis-parsed as an EMPTY
# technique/motion before this fix, collapsing to the "saw" default with no
# flag at all). Matching both spellings and normalizing via startswith (below)
# keeps the guard closed-enum on VALUES while being liberal on this label
# spelling, which carries no injection risk of its own.
_REPLY_LINE_RE = re.compile(r'^\s*(TECHNIQUES?|ADJECTIVES?|MOTIONS?)\s*:\s*(.*?)\s*$', re.IGNORECASE)


def _parse_reply(raw):
    """raw LLM text -> (technique_raw, adjectives_raw, motion_raw) strings,
    the verbatim value after each label's ':' (last occurrence wins if a
    label repeats — tolerant of a model that echoes the format block back
    before answering). A leading list-marker per line is stripped
    defensively; missing labels come back as ""."""
    technique_raw = adjectives_raw = motion_raw = ""
    for line in (raw or "").splitlines():
        stripped = line.strip().lstrip("-*• \t")
        m = _REPLY_LINE_RE.match(stripped)
        if not m:
            continue
        label, value = m.group(1).upper(), m.group(2).strip()
        if label.startswith("TECHNIQUE"):
            technique_raw = value
        elif label.startswith("ADJECTIVE"):
            adjectives_raw = value
        elif label.startswith("MOTION"):
            motion_raw = value
    return technique_raw, adjectives_raw, motion_raw


def _canon_fallback(label, canon):
    """Second chance for a label the catalogue does not carry VERBATIM.

    The exact lookup drops the whole entry on a miss, and for an OSCILLATOR that
    means the layer disappears from the patch. BJ hit this with
    `saw wave 8" + sine bass 16" + pwm pulse 4"`: the model answered `bass_sine`
    for the middle layer, no catalogue entry carries that label, and the sine bass
    was silently gone from a three-layer prompt. (The same prompt written with a
    foot mark instead of an inch mark routed all three, which is how arbitrary the
    failure looked from outside.)

    This is the case _validate_keys' own docstring describes -- "a small model
    tends to emit the natural word it read rather than the internal key" -- one
    step further: the model also COMPOUNDS and REORDERS those words. So normalize
    the separators, then try the whole label, then its longest contiguous
    sub-phrases, all against the SAME canon map. Nothing outside the catalogue can
    be produced, and an ambiguous label (tokens reaching two different keys) is
    dropped exactly as before rather than guessed at.
    """
    norm = re.sub(r"[^a-z0-9]+", " ", label).strip()
    if not norm:
        return None
    for direct in (norm, norm.replace(" ", ""), norm.replace(" ", "_")):
        if direct in canon:
            return canon[direct]
    toks = norm.split()
    # Every catalogue hit anywhere in the label, with where it sits and how long
    # it is. Collecting them ALL first matters: a pure longest-phrase-wins loop
    # accepted "square wave" out of `pwm square wave` and returned `square`
    # SILENTLY, throwing away the one word that carries the capability -- the
    # moving pulse width -- and leaving no flag behind (BJ, 2026-07-18, reading
    # "saw + sine 2' + square" for a prompt ending in "pwm square wave"). That is
    # precisely the loss CLAUDE.md's substrate discipline is written about.
    hits = []                                  # (pos, size, key, is_exact_key)
    for size in range(len(toks), 0, -1):
        for i in range(len(toks) - size + 1):
            phrase = " ".join(toks[i:i + size])
            if phrase in canon:
                hits.append((i, size, canon[phrase], canon[phrase] == phrase))
    if not hits:
        return None
    if len({h[2] for h in hits}) == 1:
        return hits[0][2]

    # A phrase that IS a catalogue key outranks one that is merely a surface form
    # of another key. `bass_sine` reaches both `sine` (a key) and `bass_saw` (via
    # the surface form "bass"), and dropping it as ambiguous is how BJ's sine-bass
    # layer disappeared -- but the model plainly named a sine.
    exact = [h for h in hits if h[3]]
    if len({h[2] for h in exact}) == 1:
        return exact[0][2]

    if exact:
        # Several REAL keys in one label is not an ambiguity to give up on: the
        # model named a wave together with what is done to it. English puts the
        # modifier first, and the modifier is the capability -- a pwm'd square IS
        # the `pwm` idiom, so keeping `square` would discard the movement and keep
        # only the carrier. `pwm square wave` -> pwm, `pwm pulse` -> pwm (which
        # previously lost the whole layer). Longest phrase breaks a tie at the
        # same position.
        return min(exact, key=lambda h: (h[0], -h[1]))[2]
    return None


def _validate_keys(raw_candidates, canon):
    """The closed-enum injection guard, WITH surface-form canonicalization: each
    candidate is looked up in ``canon`` (a {token: canonical-key} map for one
    category mapping every lexicon KEY to itself AND every registered SURFACE
    FORM to its key). A hit yields the canonical key; a miss is DROPPED and
    flagged — no LLM output influences the recipe except through this whitelist.
    Canonicalization matters because a small model tends to emit the natural
    word it read ("bell", "church bell") rather than the internal key
    ("fm_bell"); mapping that word through the lexicon's OWN surface forms turns
    it into the key instead of discarding it. The LLM still does the semantic
    SELECTION (which concepts apply); this only normalizes its chosen LABEL — it
    is NOT a raw-prompt word-scan. "none"/empty is an honest "nothing fits",
    silently skipped. No de-duplication here (one hit per occurrence, same as
    dco_recipe._scan); dedup, where it happens, is downstream _compose's job."""
    kept = []
    flags = []
    for raw in raw_candidates:
        c = raw.strip().lower()
        if not c or c == "none":
            continue
        key = canon.get(c)
        if key is None:
            key = _canon_fallback(c, canon)
        if key is not None:
            kept.append(key)
        else:
            # The model chose a label the catalogue does not carry; the plan
            # proceeds without it. That is an ADAPTATION of the plan, never a
            # user-facing "not understood" (the interpreter read the whole
            # prompt; one of ITS labels missed the catalogue). Tier "adapted"
            # keeps it out of the UI's unresolved/"Not understood" rendering
            # by design (LLM-first standing order, 2026-07-14).
            flags.append({"word": c, "reason": "interpreted, but no catalogue entry carries this label — the rest of the plan stands", "tier": "adapted"})
    return kept, flags


def _parse_and_validate(raw, technique_index, adjective_index, motion_index):
    technique_raw, adjectives_raw, motion_raw = _parse_reply(raw)

    technique_candidates = technique_raw.split(">") if technique_raw else []
    adjective_candidates = adjectives_raw.split(",") if adjectives_raw else []
    motion_candidates = [motion_raw] if motion_raw else []

    technique_keys, tflags = _validate_keys(technique_candidates, technique_index)
    adjective_keys, aflags = _validate_keys(adjective_candidates, adjective_index)
    motion_keys, mflags = _validate_keys(motion_candidates, motion_index)
    motion_key = motion_keys[0] if motion_keys else None

    return technique_keys, adjective_keys, motion_key, (tflags + aflags + mflags)


# ─── scan assembly: reproduce the EXACT shape dco_recipe._scan returns ────
# _compose (and, for a morph chain, _technique_sequence inside it) reads this
# dict exactly as it would a real word-scan's output — so the LLM's picks are
# threaded in as hits at synthetic, strictly-ordered token positions ("pos"),
# not real token indices (there is no tokenized text on this path: the LLM
# read the whole prompt at once, not token-by-token). Positions only need to
# be MUTUALLY ORDERED correctly, since every consumer (_resolve_technique,
# _technique_sequence, the adjective priority sort in _compose step 2, the
# intent/speed hit selection in step 4) only ever compares pos values against
# OTHER pos values within the same hit list, never against an absolute scale.
#
# Morph chain (>=2 validated technique keys): built as ONE technique_hit per
# occurrence — {"key", "priority", "pos"} — at pos = 0, 2, 4, ... (even), with
# ONE connector_hit — {"key": "morph_into", "pos"} — at pos = 1, 3, 5, ...
# (odd) in EVERY gap between consecutive occurrences. This reproduces the
# INPUT SHAPE a real "a into b into c" scan would produce (repeats included,
# nothing pre-deduped by this module) and hands it to dco_recipe._compose,
# which internally calls dco_recipe._technique_sequence — THAT function's own
# dedup-by-first-occurrence + "a connector strictly between the first and
# last surviving hit" gate is what actually decides the chain, unmodified.
# Nothing about chain detection/capping/non-chainable-bail is reimplemented
# here.
def _assemble_scan(technique_keys, adjective_keys, motion_key, values, composition_ops, fm_ops,
                   technique_index, adjective_index, motion_index):
    technique_hits = []
    connector_hits = []
    if len(technique_keys) >= 2:
        for i, key in enumerate(technique_keys):
            technique_hits.append({"key": key, "priority": technique_index[key]["priority"], "pos": 2 * i})
        for i in range(len(technique_keys) - 1):
            connector_hits.append({"key": "morph_into", "pos": 2 * i + 1})
    elif len(technique_keys) == 1:
        key = technique_keys[0]
        technique_hits.append({"key": key, "priority": technique_index[key]["priority"], "pos": 0})
    # else: no technique key survived validation — technique_hits stays empty,
    # so dco_recipe._resolve_technique falls through to its own
    # adjective-driven / "saw" default cascade, exactly as a real scan that
    # found no technique surface form at all would.

    adjective_hits = [
        {"key": key, "priority": adjective_index[key]["priority"], "pos": i, "degree": 1.0}
        for i, key in enumerate(adjective_keys)
    ]

    motion_hits = []
    if motion_key is not None:
        entry = motion_index[motion_key]
        motion_hits.append({"key": motion_key, "priority": entry["priority"], "pos": 0,
                            "motion_category": entry["category"]})

    return {
        "values": values,
        "composition_ops": composition_ops,
        "fm_ops": fm_ops,
        "technique_hits": technique_hits,
        "adjective_hits": adjective_hits,
        "motion_hits": motion_hits,
        "connector_hits": connector_hits,
        "residue": [],   # the LLM read the whole prompt at once; there is no
                         # leftover-token concept on this path (unlike a real
                         # word-scan's per-token residue)
    }


# ─── top level ──────────────────────────────────────────────────────────────

def llm_map_to_recipe(text, llm, frames=None):
    """LLM-routed twin of dco_recipe.author_recipe: an S1 lexicon-key mapping
    by MEANING (one LLM call) replaces the exact-phrase word-scan; S0/S3/S4
    and the composer/validator are dco_recipe's own, called unmodified.
    Returns a dict IDENTICAL in shape to author_recipe's: {ok, recipe,
    resolved, flags, lexicon_version, reference_vocabulary}. Never raises —
    any failure (LLM call, parsing, composition) falls back to the SAME plain
    technique-template recipe author_recipe itself falls back to on an
    unrepairable composition failure.

    ``llm`` is a caller-injected ``(text, system_prompt, max_new_tokens) ->
    str`` callable (this module never imports transformers/torch itself),
    matching dco_recipe.py's own S2 ``llm_route`` and lco_author.py's
    ``llm`` conventions.
    """
    lexicon = dco_recipe.load_lexicon()
    norm = dco_recipe._normalize(text)

    technique_index = {t["key"]: t for t in lexicon["techniques"]}
    adjective_index = {a["key"]: a for a in lexicon["adjectives"]}
    motion_index = {m["key"]: m for m in lexicon["motions"]}

    # Canonicalization maps for the guard: every KEY -> itself, plus every
    # registered SURFACE FORM -> its key (the lexicon's OWN surface_forms, the
    # same strings the deterministic scanner matches — reused here only to
    # normalize the LLM's chosen LABEL, e.g. "bell" -> "fm_bell", never to scan
    # the raw prompt). Later entries never collide: the lexicon forbids
    # duplicate surface forms across keys (dco_recipe._build_index raises).
    def _canon(entries):
        m = {}
        for e in entries:
            m[e["key"]] = e["key"]
            for sf in e.get("surface_forms", []):
                m[str(sf).strip().lower()] = e["key"]
        return m
    technique_canon = _canon(lexicon["techniques"])
    adjective_canon = _canon(lexicon["adjectives"])
    motion_canon = _canon(lexicon["motions"])

    # Explicit-number pre-pass: deterministic regex parsing of NUMERIC SYNTAX
    # ("ratio 3", "index 2.5", "boost harmonic 5", "more modulation", ...),
    # not word MEANING — so it stays outside the LLM call entirely, run
    # exactly as dco_recipe._scan itself runs it: the same three functions,
    # the same order (fm_ops -> composition_ops -> typed values), each
    # threading the PREVIOUS call's blanked remainder into the next so none
    # of the three can double-claim the other's tokens (e.g. "every 3
    # harmonics" must be claimed by composition_ops before the typed-value
    # pass could misread it as a 3-partial ceiling — see dco_recipe._scan's
    # own comment). Pure regex/lookup — cannot raise — so it is safe to run
    # before the try block below, mirroring author_recipe computing its own
    # S1 scan before ITS try.
    fm_ops, _remaining = dco_recipe._extract_fm_ops(norm)
    composition_ops, _remaining = dco_recipe._extract_composition_ops(_remaining)
    values, _remaining = dco_recipe._extract_typed_values(_remaining)

    scan = None       # becomes the assembled LLM-driven scan; reused by the
                      # except-branch fallback below if it was already built
                      # when the failure happened — mirrors author_recipe's
                      # own fallback, which reuses ITS S1 scan for the
                      # identical _resolve_technique call.
    key_flags = []    # becomes the closed-enum injection-guard flags, if
                      # parsing got that far before a later failure; preserved
                      # in the except-branch below exactly as author_recipe's
                      # own fallback preserves its pre-computed s2_flags
                      # (dco_recipe.py's `flags = s2_flags + [...]`).
    try:
        system_prompt = _SYSTEM_PROMPT_HEAD + _build_catalogue(lexicon)
        raw = llm(text, system_prompt, _MAX_NEW_TOKENS)

        technique_keys, adjective_keys, motion_key, key_flags = _parse_and_validate(
            raw, technique_canon, adjective_canon, motion_canon)

        scan = _assemble_scan(technique_keys, adjective_keys, motion_key, values,
                              composition_ops, fm_ops, technique_index, adjective_index, motion_index)

        # REUSE, not reimplementation: the exact same composer/validator
        # dco_recipe.author_recipe itself calls. {} (empty) stands in for the
        # S2 extra-adjective-hits list _compose otherwise receives — this
        # module has no separate S2 residue-routing stage (the one LLM call
        # above already covers what S1+S2 together used to do), and
        # list({}) == list([]) == [] so _compose's
        # `list(scan["adjective_hits"]) + list(s2_extra_adjective_hits)` is
        # unaffected either way.
        recipe, resolved, flags_c = dco_recipe._compose(scan, {}, lexicon, frames)
        recipe, _ = dco_recipe.validate_recipe(recipe)

        # Tier-stamp _compose's own flags exactly as author_recipe's final
        # return does (dco_recipe._flag_tier, unmodified), then merge in this
        # module's own closed-enum injection-guard flags (already tier-
        # stamped "adapted" at construction in _validate_keys: a dropped
        # model label is an adaptation of the plan, never a user-facing
        # "not understood" — LLM-first standing order, 2026-07-14).
        # dco_recipe._dedupe_flags (unmodified) does the same order-
        # preserving (word, reason) de-duplication author_recipe itself
        # applies before returning.
        flags_c_tiered = [dict(f, tier=dco_recipe._flag_tier(f.get("reason", ""))) for f in flags_c]
        all_flags = dco_recipe._dedupe_flags(flags_c_tiered + key_flags)

        resp = {
            "ok": True,
            "recipe": recipe,
            "resolved": resolved,
            "flags": all_flags,
            "lexicon_version": lexicon.get("lexicon_version"),
            "reference_vocabulary": dco_recipe.reference_vocabulary(lexicon),
        }
        # Movement by default (a static sound must be explicitly ordered): the
        # SAME analog-life pass build_lco_response gates behind an adjective/
        # analog cue fires here unconditionally, for whatever harmonic single-
        # keyframe recipe this LLM-map path just composed. It early-outs on an
        # already-moving recipe (dur_frac > 0 from a real motion trajectory,
        # including one dco_recipe._apply_motion_intent just realized via
        # endpoint synthesis), so a Change-1 motion is never double-drifted.
        resp = lco_author._apply_analog_life(resp, text, always=True)
        return resp
    except Exception as e:
        # SAME plain-template fallback author_recipe uses on an unrepairable
        # composition failure: resolve a bare technique template (no
        # adjectives/composition/fm/motion applied) via
        # dco_recipe._resolve_technique, reusing whatever technique signal
        # this LLM path had already validated before the failure (or none,
        # if it failed before getting that far — _resolve_technique's own
        # defaulting cascade then applies, identically to author_recipe's
        # fallback when ITS S1 scan also found nothing).
        fallback_scan = scan if scan is not None else {"technique_hits": []}
        fallback_flags = []
        resolved_technique = dco_recipe._resolve_technique(fallback_scan, set(), technique_index, fallback_flags)
        template = technique_index[resolved_technique]["template"]
        recipe = copy.deepcopy(template)
        if frames is not None:
            recipe["frames"] = frames
        recipe, _ = dco_recipe.validate_recipe(recipe)
        resolved = {"technique": resolved_technique, "adjectives": [], "composition": [],
                    "fm": [], "motion": [], "values": {}}
        reason = f"recipe composition failed ({e}) — fell back to the plain technique template"
        fallback_flag = {"word": resolved_technique, "reason": reason, "tier": dco_recipe._flag_tier(reason)}
        # dco_recipe._dedupe_flags again here too (unmodified, same as the try
        # branch above): key_flags can itself carry an internal duplicate (the
        # LLM repeating the same hallucinated word twice in one category), and
        # author_recipe's own except branch flows through the SAME shared
        # `_dedupe_flags(flags)` tail its try branch does, so this mirrors that
        # guarantee rather than dropping it on the fallback path specifically.
        flags = dco_recipe._dedupe_flags(key_flags + [fallback_flag])
        return {
            "ok": True,
            "recipe": recipe,
            "resolved": resolved,
            "flags": flags,
            "lexicon_version": lexicon.get("lexicon_version"),
            "reference_vocabulary": dco_recipe.reference_vocabulary(lexicon),
        }
