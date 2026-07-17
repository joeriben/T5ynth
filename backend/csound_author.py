#!/usr/bin/env python3
"""Csound orchestra author (Phase 4, SPEC_phase4_5_csound_llm_preset.md).

build_csound_response() (mode=="csound" in pipe_inference.py) hands the WHOLE
prompt to ONE constrained instruct-LLM call: the model plans 1-4 LAYERS, each
pointing at fixed keys from backend/csound_lexicon.py (a "tool"/family, an
optional "register", "characters" and "motion") plus an optional
GLOBAL "motion" list. It never writes Csound text or a DSP number itself --
docs/DCO_LLM_GUARDRAILS.md's central guardrail, mirrored here for the Csound
paradigm: the LLM only ROUTES to prefabricated, curated Csound code
(backend/csound_assembler.py), exactly like dco_llm_map.py routes to
dco_recipe's lexicon keys. csound_assembler.assemble(spec) is the closed-enum
injection guard AND the deterministic renderer in one: any key the model
names that is not a real, current lexicon key raises ValueError immediately
-- no LLM output ever reaches a Csound string except through that gate.

Flow: prompt -> LLM -> strict-JSON parse -> csound_assembler.assemble(spec).
On a parse failure OR an assemble() ValueError: ONE retry, with the failure
folded into the retry's user turn (the model has no other memory of the
conversation -- this module never imports transformers/torch, ``llm`` is a
caller-injected (text, system_prompt, max_new_tokens) -> str callable, the
same convention as dco_llm_map.py/lco_author.py's own ``llm`` parameter).
Two failures in a row -> an HONEST {ok: False, error} -- LLM-FIRST, NO
FALLBACK: never keyword-match the prompt, never default-spec, never guess a
"reasonable" orchestra. The determinism guarantee is one-directional: the
SAME prompt may author a DIFFERENT spec on a different LLM run (temperature/
sampling is the model's own default, mirroring run_instruct's greedy-but-not-
forced discipline elsewhere), but spec -> orchestra is always deterministic
(csound_assembler's own guarantee, Phase 3).
"""
import json
import math
import re

import csound_assembler
import csound_lexicon as lex

_MAX_NEW_TOKENS = 700


# ─── system prompt: vocabulary generated FROM the lexicon data ────────────
# Never hand-list a key here -- csound_lexicon.TOOLS (keyed by keys_in_family)
# is the single source of truth; CSOUND_LEXICON.md is documentation generated
# from the SAME "description" strings (csound_lexicon.generate_markdown), so
# this prompt and that doc can never drift apart from each other, nor from
# the closed-enum guard csound_assembler.assemble() itself enforces.

def _vocab_block(title, keys):
    lines = [title + ":"]
    for k in keys:
        lines.append(f"  {k} -- {lex.TOOLS[k]['description']}")
    return "\n".join(lines)


def _build_catalogue():
    return "\n".join([
        _vocab_block("TOOLS (the layer's sound source; \"tool\" field)", lex.FAMILY_KEYS),
        _vocab_block("CHARACTERS (per-layer timbral modifiers; \"characters\": [{\"key\",\"amount\"}])",
                     lex.CHARACTER_KEYS),
        _vocab_block("MOTION (per-layer or global movement; \"motion\": [{\"key\",\"amount\"}])",
                     lex.MOTION_KEYS),
        "REGISTER (organ footage, \"register\" field, default \"8'\"):\n"
        "  32' = 0.25x, 16' = 0.5x, 8' = 1x (the played note), 4' = 2x, 2' = 4x\n"
        "  -- a plain numeric ratio (e.g. 1.5) is also accepted verbatim.",
    ])


_FEW_SHOT = [
    (
        "make one dark bell 16' and above that a metallic bell 8'",
        {
            "layers": [
                {"tool": "bell", "register": "16'", "characters": [{"key": "dark", "amount": 0.6}]},
                {"tool": "bell", "register": "8'", "characters": [{"key": "metallic", "amount": 0.6}]},
            ]
        },
    ),
    (
        "a warm saw pad",
        {
            "layers": [
                {"tool": "saw_stack",
                 "characters": [{"key": "warm", "amount": 0.5}]},
            ]
        },
    ),
    (
        "a harsh glassy metallic pluck",
        {
            "layers": [
                {"tool": "pluck",
                 "characters": [
                     {"key": "harsh", "amount": 0.5},
                     {"key": "glassy", "amount": 0.6},
                     {"key": "metallic", "amount": 0.5},
                 ]},
            ]
        },
    ),
    (
        "a shimmering evolving pad that breathes slowly",
        {
            "layers": [
                {"tool": "pad",
                 "motion": [
                     {"key": "shimmer", "amount": 0.5},
                     {"key": "evolve", "amount": 0.5},
                     {"key": "breathe", "amount": 0.4},
                 ]},
            ]
        },
    ),
    (
        "a bright square",
        {
            "layers": [
                {"tool": "square_stack", "characters": [{"key": "bright", "amount": 0.5}]},
            ]
        },
    ),
    (
        "a sustained organ tone",
        {
            "layers": [
                {"tool": "organ_tone"},
            ]
        },
    ),
]


def _build_system_prompt():
    head = (
        "You design a Csound orchestra as 1-4 LAYERS, each pointing at a fixed "
        "catalogue of prefabricated synthesis tools. You NEVER invent a tool, "
        "character or motion name, and you NEVER write Csound code or "
        "a DSP number yourself -- you only choose KEYS from the catalogue below, "
        "plus register footage, level and per-key amounts. Every layer is a "
        "STANDING tone that holds its level for as long as the note is held -- "
        "there is no envelope/time-shape concept here; amplitude shape is the "
        "synth's own ADSR, applied outside this orchestra.\n\n"
        "Reply with STRICT JSON only -- no prose, no markdown code fences, no "
        "explanation. Schema:\n"
        "{\n"
        '  "layers": [\n'
        "    {\n"
        '      "tool": "<TOOL key>",\n'
        '      "register": "<organ footage or numeric ratio, default \\"8\'\\">",\n'
        '      "level": <0.0-1.5, default 0.8>,\n'
        '      "characters": [{"key": "<CHARACTER key>", "amount": <0.0-1.0>}, ...],\n'
        '      "motion": [{"key": "<MOTION key>", "amount": <0.0-1.0>}, ...]\n'
        "    }\n"
        "  ],\n"
        '  "motion": [{"key": "<MOTION key>", "amount": <0.0-1.0>}, ...]  '
        "// OPTIONAL, GLOBAL: broadcast to every layer\n"
        "}\n"
        "\"register\", \"level\", \"characters\" and \"motion\" are all "
        "optional per layer (omit rather than guess). At most 4 layers total. "
        "Stack multiple layers when the prompt names multiple simultaneous or "
        "registered sounds (e.g. two named registers, or \"and above that\").\n\n"
    )
    catalogue = _build_catalogue()
    examples = ["\n\nEXAMPLES:"]
    for prompt, spec in _FEW_SHOT:
        examples.append(f"\nPrompt: {prompt}\nJSON: {json.dumps(spec)}")
    return head + catalogue + "".join(examples)


# ─── strict-JSON parsing, tolerant of fences/prose around the object ──────

_FENCE_RE = re.compile(r"```(?:json)?\s*(.*?)\s*```", re.DOTALL | re.IGNORECASE)


def _strip_code_fences(raw):
    raw = (raw or "").strip()
    m = _FENCE_RE.search(raw)
    return m.group(1).strip() if m else raw


def _extract_json_object(raw):
    """First balanced {...} block in raw, or raw itself if none closes."""
    start = raw.find("{")
    if start < 0:
        return raw
    depth = 0
    for i in range(start, len(raw)):
        if raw[i] == "{":
            depth += 1
        elif raw[i] == "}":
            depth -= 1
            if depth == 0:
                return raw[start:i + 1]
    return raw[start:]


def _reject_non_finite_constant(token):
    # json.loads' `parse_constant` hook fires for the bare NaN/Infinity/
    # -Infinity tokens Python's decoder otherwise accepts as an extension
    # (json.loads defaults to letting these through as float('nan')/inf).
    # The 7B's JSON is untrusted -- reject AT PARSE so a non-finite number
    # never reaches assemble() or an ok:True response (see module docstring
    # and csound_assembler.py's own defense-in-depth _clamp/parse_register).
    raise ValueError(
        f"non-finite number token {token!r} in LLM reply -- use plain finite numbers"
    )


def _finite_parse_float(s):
    # json.loads' `parse_float` hook fires for every JSON float literal;
    # catches the overflow case (e.g. 1e400 -> float('inf')) the constant
    # hook above cannot see, since 1e400 is lexically an ordinary float.
    v = float(s)
    if not math.isfinite(v):
        raise ValueError(
            f"non-finite number {s!r} in LLM reply -- use plain finite numbers "
            "(no huge exponents)"
        )
    return v


def _finite_parse_int(s):
    # json.loads' `parse_int` hook fires for every JSON *integer* literal --
    # which the two hooks above never see (a bare integer is neither a
    # NaN/Infinity constant nor a float literal). An integer is always finite
    # AS an int, but the assembler and parse_register convert every numeric
    # field to float downstream (float(level)/float(amount)/float(register));
    # an integer too large to represent as a finite float raises OverflowError
    # THERE -- and OverflowError is neither ValueError nor TypeError, so
    # build_csound_response's retry guard would miss it and this module's
    # "never raises" contract would break on an uncaught crash. A plain
    # 400-digit integer is the parse_int twin of the 1e400 float _finite_parse_
    # float already rejects (same magnitude, just written without an exponent);
    # fold it into the identical finite-number failure. (`int(s)` itself raises
    # ValueError for an absurd digit count -- sys.int_max_str_digits -- which
    # the except-ValueError below already folds into the same retry.)
    v = int(s)
    try:
        float(v)
    except OverflowError:
        raise ValueError(
            f"integer {s!r} in LLM reply is too large -- use plain finite numbers "
            "(no huge integers)"
        )
    return v


def _parse_spec_json(raw):
    """Raw LLM text -> spec dict. Raises ValueError on anything unparseable
    (fences and leading/trailing prose are stripped first; a JSON array or
    scalar at the top level is rejected -- assemble() requires an object).
    Also rejects any non-finite number (NaN/Infinity/-Infinity literals, an
    overflowing float literal like 1e400, or a plain INTEGER literal too large
    to survive the assembler's downstream float() conversion, e.g. a 400-digit
    integer) AT PARSE TIME -- json.loads accepts all of these by default, but a
    nan/inf (or a float()-overflowing int) reaching csound_assembler.assemble()
    would either emit a literal `nan`/`inf` token into the orchestra text
    (silent, non-compilable failure) or -- if clamped away downstream --
    still get echoed in an ok:True response's `spec` field, which is not
    valid JSON to send over the wire (json.dumps(..., allow_nan=True) emits
    the bare token `Infinity`). Rejecting here folds the failure into the
    existing one-retry/honest-error contract instead."""
    candidate = _extract_json_object(_strip_code_fences(raw))
    try:
        parsed = json.loads(
            candidate,
            parse_constant=_reject_non_finite_constant,
            parse_float=_finite_parse_float,
            parse_int=_finite_parse_int,
        )
    except json.JSONDecodeError as e:
        raise ValueError(f"could not parse JSON ({e}); raw reply: {(raw or '')[:200]!r}")
    except ValueError as e:
        # raised by _reject_non_finite_constant / _finite_parse_float /
        # _finite_parse_int above (JSONDecodeError, a ValueError subclass, is
        # already handled by the more specific clause first)
        raise ValueError(f"{e}; raw reply: {(raw or '')[:200]!r}")
    if not isinstance(parsed, dict):
        raise ValueError(f"expected a JSON object, got {type(parsed).__name__}")
    return parsed


# ─── top level ─────────────────────────────────────────────────────────────

def build_csound_response(text, llm):
    """prompt -> {ok, orchestra, reading, spec, error}. ``llm`` is a caller-
    injected (text, system_prompt, max_new_tokens) -> str callable (this
    module never imports transformers/torch itself), matching dco_llm_map.py/
    lco_author.py's own ``llm`` convention. Never raises: a parse or
    assembler failure retries ONCE (the error folded into the retry's user
    turn) and then returns an honest ok=False -- no fallback, no default
    orchestra, ever."""
    system_prompt = _build_system_prompt()
    attempt_text = text
    last_error = "no attempt made"

    for attempt in range(2):  # the request + exactly ONE retry
        raw = llm(attempt_text, system_prompt, _MAX_NEW_TOKENS)
        try:
            spec = _parse_spec_json(raw)
        except ValueError as e:
            last_error = str(e)
            attempt_text = (
                f"{text}\n\n(Your previous reply could not be parsed as JSON: {last_error}. "
                "Reply again with STRICT JSON only matching the schema -- no prose, no code fences.)"
            )
            continue

        try:
            orchestra, reading = csound_assembler.assemble(spec)
        except (ValueError, TypeError) as e:
            # assemble()'s own docstring promises ValueError on any invalid
            # spec, but several of its internal checks (len(), `in`, float())
            # run BEFORE a type check on a wrong-typed-but-JSON-valid field
            # (e.g. {"layers": 3} or {"amount": null}) and raise TypeError
            # instead -- a real, empirically-confirmed 7B failure mode, not a
            # hypothetical. Catching both here is what keeps THIS module's own
            # "never raises" contract (module docstring) honest regardless of
            # which exception type the assembler happens to surface; the spec
            # is untrusted LLM output either way, so both are equally "the
            # model's previous reply used an invalid shape".
            last_error = str(e)
            attempt_text = (
                f"{text}\n\n(Your previous reply used an invalid key or shape: {last_error}. "
                "Choose ONLY keys from the TOOLS/CHARACTERS/MOTION catalogue above. "
                "Reply again with STRICT JSON only.)"
            )
            continue

        return {"ok": True, "orchestra": orchestra, "reading": reading, "spec": spec, "error": None}

    return {"ok": False, "orchestra": None, "reading": None, "spec": None, "error": last_error}
