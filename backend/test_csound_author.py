#!/usr/bin/env python3
"""Phase-4 Csound author test suite (SPEC_phase4_5_csound_llm_preset.md).

Runnable plain:  python -m backend.test_csound_author
Also pytest-collectible (every test_* function is self-contained, plain
asserts, no fixtures) — but these are MOCK-LLM tests: no model load, no
subprocess, no network, safe for CI. ``llm`` is always a small stand-in
callable (mirrors dco_llm_map.py's own test convention: csound_author.py
never imports transformers/torch, so a fake ``(text, system_prompt,
max_new_tokens) -> str`` is a complete substitute for the real model).

Covers the spec's checklist for backend/csound_author.py:
  1. A clean JSON reply on the FIRST attempt -> ok=True, orchestra/reading
     set, exactly ONE llm call.
  2. A reply wrapped in a ```json fence, and one wrapped in prose -> both
     still parse (JSON robustness).
  3. A first reply that fails to parse, second reply valid -> ok=True,
     exactly TWO llm calls (the one retry), and the retry's user turn carries
     the first failure's error text.
  4. A first reply naming an unknown/invalid key, second reply valid -> same
     recovery via the assembler's ValueError path.
  4b. A first reply with a wrong-typed field (valid JSON, e.g. "layers": 3)
     that makes the assembler raise TypeError instead of ValueError, second
     reply valid -> same recovery (regression: build_csound_response's own
     "never raises" contract was broken for this exception type).
  5. BOTH attempts invalid (parse failure) -> ok=False, error set, EXACTLY
     two llm calls, never a third — no fallback, no default spec.
  6. BOTH attempts invalid (unknown key) -> same honest ok=False.
  6b. BOTH attempts wrong-typed (TypeError path) -> same honest ok=False, not
     an uncaught exception.
  7. BJ's canonical stacking example ("make one dark bell 16' and above that
     a metallic bell 8'") assembles two register-differentiated bell layers.
  8. Untrusted-input hardening: a reply containing a bare NaN/Infinity token
     or an overflowing literal (1e400) is rejected AT PARSE (json.loads'
     parse_constant/parse_float hooks) and folds into the same retry/honest-
     error contract as 3/5 above -- never a literal nan/inf reaching the
     orchestra text or an ok:True response. 8b. every ok:True response this
     suite produces must survive json.dumps(..., allow_nan=False) (the wire
     contract pipe_inference.py actually sends).

A real-model smoke (optional, gated behind T5YNTH_CSOUND_AUTHOR_SMOKE=1) is
NOT included here — the project's IPC-only rule for inference (feedback_
test_tool_ipc) puts a real-backend smoke in tools/e2e_csound_prompt.py
instead, which drives the actual PyInstaller/subprocess path exactly like the
plugin does, never a direct import of the model.
"""
import json
import sys
from pathlib import Path

_BACKEND_DIR = Path(__file__).resolve().parent
if str(_BACKEND_DIR) not in sys.path:
    sys.path.insert(0, str(_BACKEND_DIR))

import csound_author  # noqa: E402

_VALID_BELL_STACK_REPLY = json.dumps({
    "layers": [
        {"tool": "bell", "register": "16'", "characters": [{"key": "dark", "amount": 0.6}]},
        {"tool": "metal", "register": "8'", "characters": [{"key": "metallic", "amount": 0.5}]},
    ]
})

_VALID_SIMPLE_REPLY = json.dumps({"layers": [{"tool": "saw_stack"}]})


class MockLlm:
    """Scripted (text, system_prompt, max_new_tokens) -> str stand-in. Each
    call pops the next reply off `replies` (the last is reused if exhausted,
    matching a caller who over-counts) and records the ``text`` it was
    called with, so tests can assert the retry folded the error in."""

    def __init__(self, replies):
        self.replies = list(replies)
        self.calls = []  # list of (text, system_prompt, max_new_tokens)

    def __call__(self, text, system_prompt, max_new_tokens):
        self.calls.append((text, system_prompt, max_new_tokens))
        idx = min(len(self.calls) - 1, len(self.replies) - 1)
        return self.replies[idx]

    @property
    def call_count(self):
        return len(self.calls)


def test_first_try_success():
    llm = MockLlm([_VALID_SIMPLE_REPLY])
    resp = csound_author.build_csound_response("a bright saw", llm)
    assert resp["ok"] is True, resp
    assert resp["error"] is None
    assert isinstance(resp["orchestra"], str) and "instr 1" in resp["orchestra"]
    assert isinstance(resp["reading"], str) and resp["reading"]
    assert llm.call_count == 1, f"expected exactly one LLM call, got {llm.call_count}"


def test_json_wrapped_in_code_fence():
    fenced = f"```json\n{_VALID_SIMPLE_REPLY}\n```"
    llm = MockLlm([fenced])
    resp = csound_author.build_csound_response("a bright saw", llm)
    assert resp["ok"] is True, resp
    assert llm.call_count == 1


def test_json_wrapped_in_prose():
    prose = f"Here is the spec you asked for:\n{_VALID_SIMPLE_REPLY}\nHope that helps!"
    llm = MockLlm([prose])
    resp = csound_author.build_csound_response("a bright saw", llm)
    assert resp["ok"] is True, resp
    assert llm.call_count == 1


def test_retry_recovers_from_parse_failure():
    llm = MockLlm(["not json at all, sorry", _VALID_SIMPLE_REPLY])
    resp = csound_author.build_csound_response("a bright saw", llm)
    assert resp["ok"] is True, resp
    assert llm.call_count == 2, f"expected exactly one retry (2 calls), got {llm.call_count}"
    # The retry's user turn must fold the ORIGINAL prompt back in plus the failure text.
    retry_text = llm.calls[1][0]
    assert "a bright saw" in retry_text
    assert "could not be parsed" in retry_text.lower() or "json" in retry_text.lower()


def test_retry_recovers_from_unknown_key():
    bad = json.dumps({"layers": [{"tool": "not_a_real_tool_key"}]})
    llm = MockLlm([bad, _VALID_SIMPLE_REPLY])
    resp = csound_author.build_csound_response("a bright saw", llm)
    assert resp["ok"] is True, resp
    assert llm.call_count == 2
    retry_text = llm.calls[1][0]
    assert "a bright saw" in retry_text
    assert "invalid" in retry_text.lower() or "unknown" in retry_text.lower()


def test_exhausted_retries_parse_failure_is_honest_error():
    llm = MockLlm(["garbage one", "garbage two, still not json"])
    resp = csound_author.build_csound_response("xk7 vroom zzzzz plaid teapot", llm)
    assert resp["ok"] is False, resp
    assert resp["orchestra"] is None
    assert resp["reading"] is None
    assert resp["error"], "an honest failure must carry a non-empty error message"
    assert llm.call_count == 2, f"must retry EXACTLY once, never more: got {llm.call_count} calls"


def test_exhausted_retries_unknown_key_is_honest_error():
    bad = json.dumps({"layers": [{"tool": "definitely_not_in_the_lexicon"}]})
    llm = MockLlm([bad, bad])
    resp = csound_author.build_csound_response("a fictional instrument", llm)
    assert resp["ok"] is False, resp
    assert resp["error"]
    assert llm.call_count == 2


def test_retry_recovers_from_wrong_typed_field():
    # Regression: csound_assembler.assemble() raises TypeError (not
    # ValueError) for a spec that is valid JSON but has a wrong-typed field --
    # e.g. "layers" as an int instead of a list hits `len(layer_specs)` before
    # any type check. build_csound_response's own docstring promises "never
    # raises"; before the fix this TypeError propagated uncaught, skipping the
    # retry entirely (empirically confirmed: only 1 llm call, an unhandled
    # exception instead of an honest ok=False).
    bad = json.dumps({"layers": 3})
    llm = MockLlm([bad, _VALID_SIMPLE_REPLY])
    resp = csound_author.build_csound_response("a bright saw", llm)
    assert resp["ok"] is True, resp
    assert llm.call_count == 2


def test_exhausted_retries_wrong_typed_field_is_honest_error():
    bad = json.dumps({"layers": [{"tool": "bell", "characters": [{"key": "bright", "amount": None}]}]})
    llm = MockLlm([bad, bad])
    resp = csound_author.build_csound_response("a fictional instrument", llm)
    assert resp["ok"] is False, resp
    assert resp["orchestra"] is None
    assert resp["reading"] is None
    assert resp["error"], "an honest failure must carry a non-empty error message"
    assert llm.call_count == 2, f"must retry EXACTLY once, never more: got {llm.call_count} calls"


def test_never_more_than_two_calls_even_if_scripted_longer():
    # Three scripted replies, but the author must stop after 2 (0-indexed: the
    # request + one retry) regardless of how many replies are available.
    llm = MockLlm(["bad1", "bad2", _VALID_SIMPLE_REPLY])
    resp = csound_author.build_csound_response("whatever", llm)
    assert resp["ok"] is False, "a THIRD attempt would be an unauthorized extra retry"
    assert llm.call_count == 2


def test_bj_canonical_stacking_prompt():
    llm = MockLlm([_VALID_BELL_STACK_REPLY])
    resp = csound_author.build_csound_response(
        "make one dark bell 16' and above that a metallic bell 8'", llm)
    assert resp["ok"] is True, resp
    spec = resp["spec"]
    assert len(spec["layers"]) == 2
    assert spec["layers"][0]["tool"] == "bell"
    assert spec["layers"][0]["register"] == "16'"
    assert spec["layers"][1]["register"] == "8'"
    # Two distinct registers -> the assembler namespaces them (L1/L2) so both
    # survive in one orchestra; the reading names both layers, one per line.
    assert resp["reading"].count("\n") >= 1
    assert "16'" in resp["reading"] and "8'" in resp["reading"]


def test_system_prompt_built_from_lexicon_not_hand_listed():
    import csound_lexicon as lex
    prompt = csound_author._build_system_prompt()
    for key in lex.FAMILY_KEYS + lex.CHARACTER_KEYS + lex.MOTION_KEYS:
        assert key in prompt, f"lexicon key {key!r} missing from the generated system prompt"


# ─── untrusted-input regression: non-finite numbers (NaN/Infinity/1e400) ────
# The 7B's JSON is untrusted. Python's json.loads accepts the bare NaN/
# Infinity/-Infinity tokens (and 1e400 overflows to float('inf')) by default.
# These must be rejected AT PARSE so they fold into the existing one-retry/
# honest-error contract, rather than reaching csound_assembler.assemble() as
# a literal `nan`/`inf` token in the orchestra text, or being echoed back in
# an ok:True response's `spec` field where json.dumps(..., allow_nan=True)
# would put the bare (invalid-JSON) token `Infinity` on the wire.

_NAN_CHARACTER_REPLY_RAW = (
    '{"layers": [{"tool": "bell", "characters": [{"key": "harsh", "amount": NaN}]}]}'
)


def test_retry_recovers_from_nan_character_amount():
    llm = MockLlm([_NAN_CHARACTER_REPLY_RAW, _VALID_SIMPLE_REPLY])
    resp = csound_author.build_csound_response("a harsh bell", llm)
    assert resp["ok"] is True, resp
    assert llm.call_count == 2, f"expected exactly one retry (2 calls), got {llm.call_count}"
    retry_text = llm.calls[1][0]
    assert "a harsh bell" in retry_text
    assert "non-finite" in retry_text.lower() or "finite" in retry_text.lower()


def test_exhausted_retries_nan_character_amount_is_honest_error():
    llm = MockLlm([_NAN_CHARACTER_REPLY_RAW, _NAN_CHARACTER_REPLY_RAW])
    resp = csound_author.build_csound_response("a harsh bell", llm)
    assert resp["ok"] is False, resp
    assert resp["orchestra"] is None
    assert resp["reading"] is None
    assert resp["error"], "an honest failure must carry a non-empty error message"
    assert "non-finite" in resp["error"].lower() or "finite" in resp["error"].lower()
    assert llm.call_count == 2, f"must retry EXACTLY once, never more: got {llm.call_count} calls"


def test_retry_recovers_from_infinity_level():
    bad = '{"layers": [{"tool": "saw_stack", "level": Infinity}]}'
    llm = MockLlm([bad, _VALID_SIMPLE_REPLY])
    resp = csound_author.build_csound_response("a loud saw", llm)
    # After the fix, Infinity is rejected at PARSE time (json.loads' own
    # parse_constant hook), so this recovers via the retry -- it never
    # reaches csound_assembler.assemble() at all.
    assert resp["ok"] is True, resp
    assert llm.call_count == 2
    retry_text = llm.calls[1][0]
    assert "non-finite" in retry_text.lower() or "finite" in retry_text.lower()


def test_retry_recovers_from_overflow_1e400():
    # 1e400 is lexically an ordinary JSON float (no NaN/Infinity token), but
    # Python's float() overflows it to float('inf') -- caught by the
    # parse_float hook (_finite_parse_float), not parse_constant.
    bad = '{"layers": [{"tool": "saw_stack", "level": 1e400}]}'
    llm = MockLlm([bad, _VALID_SIMPLE_REPLY])
    resp = csound_author.build_csound_response("a loud saw", llm)
    assert resp["ok"] is True, resp
    assert llm.call_count == 2
    retry_text = llm.calls[1][0]
    assert "non-finite" in retry_text.lower() or "finite" in retry_text.lower()


_BIGINT_LEVEL_REPLY_RAW = (
    '{"layers": [{"tool": "saw_stack", "level": ' + "1" + "0" * 400 + "}]}"
)


def test_retry_recovers_from_overflow_bigint():
    # A plain INTEGER literal (no exponent, no NaN/Infinity token) is the
    # parse_int twin of 1e400: json.loads parses it as an arbitrary-precision
    # Python int (finite AS an int, so neither the parse_constant nor the
    # parse_float hook sees it), and the assembler's downstream float(level)
    # overflows to OverflowError -- which is NEITHER ValueError nor TypeError,
    # so before the parse_int hook it escaped build_csound_response's retry
    # guard as an UNCAUGHT crash (the module's "never raises" contract broke).
    # The parse_int hook (_finite_parse_int) rejects it AT PARSE like 1e400.
    llm = MockLlm([_BIGINT_LEVEL_REPLY_RAW, _VALID_SIMPLE_REPLY])
    resp = csound_author.build_csound_response("a loud saw", llm)
    assert resp["ok"] is True, resp
    assert llm.call_count == 2
    retry_text = llm.calls[1][0]
    assert "finite" in retry_text.lower() or "too large" in retry_text.lower()


def test_exhausted_retries_overflow_bigint_is_honest_error():
    # Same input, both attempts bad -> honest ok=False, EXACTLY two calls,
    # never an uncaught OverflowError (the regression this locks).
    llm = MockLlm([_BIGINT_LEVEL_REPLY_RAW, _BIGINT_LEVEL_REPLY_RAW])
    resp = csound_author.build_csound_response("a loud saw", llm)
    assert resp["ok"] is False, resp
    assert resp["orchestra"] is None
    assert resp["reading"] is None
    assert resp["error"], "an honest failure must carry a non-empty error message"
    assert llm.call_count == 2, f"must retry EXACTLY once, never more: got {llm.call_count} calls"


def test_all_ok_responses_are_wire_safe_json():
    # Locks the wire contract end-to-end (this is what pipe_inference.py's
    # send_text(json.dumps(response)) actually sends): every ok:True
    # response produced by this suite's mock-LLM scenarios must survive
    # json.dumps(..., allow_nan=False) -- the strict mode that raises
    # ValueError on any stray NaN/Infinity instead of emitting the
    # (invalid-JSON) bare token, which is what juce::JSON would choke on.
    scenarios = [
        ("a bright saw", [_VALID_SIMPLE_REPLY]),
        ("a bright saw", [f"```json\n{_VALID_SIMPLE_REPLY}\n```"]),
        ("a bright saw", ["not json at all, sorry", _VALID_SIMPLE_REPLY]),
        ("make one dark bell 16' and above that a metallic bell 8'",
         [_VALID_BELL_STACK_REPLY]),
        ("a harsh bell", [_NAN_CHARACTER_REPLY_RAW, _VALID_SIMPLE_REPLY]),
        ("a loud saw", ['{"layers": [{"tool": "saw_stack", "level": Infinity}]}',
                        _VALID_SIMPLE_REPLY]),
        ("a loud saw", ['{"layers": [{"tool": "saw_stack", "level": 1e400}]}',
                        _VALID_SIMPLE_REPLY]),
        ("a loud saw", [_BIGINT_LEVEL_REPLY_RAW, _VALID_SIMPLE_REPLY]),
    ]
    checked = 0
    for prompt, replies in scenarios:
        resp = csound_author.build_csound_response(prompt, MockLlm(replies))
        if resp["ok"]:
            json.dumps(resp, allow_nan=False)  # raises ValueError if not wire-safe
            checked += 1
    assert checked > 0, "expected at least one ok:True scenario to actually check"


ALL_TESTS = [
    test_first_try_success,
    test_json_wrapped_in_code_fence,
    test_json_wrapped_in_prose,
    test_retry_recovers_from_parse_failure,
    test_retry_recovers_from_unknown_key,
    test_retry_recovers_from_wrong_typed_field,
    test_exhausted_retries_parse_failure_is_honest_error,
    test_exhausted_retries_unknown_key_is_honest_error,
    test_exhausted_retries_wrong_typed_field_is_honest_error,
    test_never_more_than_two_calls_even_if_scripted_longer,
    test_bj_canonical_stacking_prompt,
    test_system_prompt_built_from_lexicon_not_hand_listed,
    test_retry_recovers_from_nan_character_amount,
    test_exhausted_retries_nan_character_amount_is_honest_error,
    test_retry_recovers_from_infinity_level,
    test_retry_recovers_from_overflow_1e400,
    test_retry_recovers_from_overflow_bigint,
    test_exhausted_retries_overflow_bigint_is_honest_error,
    test_all_ok_responses_are_wire_safe_json,
]


def run_all():
    failures = []
    for t in ALL_TESTS:
        try:
            t()
            print(f"  ok:   {t.__name__}")
        except AssertionError as e:
            failures.append(t.__name__)
            print(f"  FAIL: {t.__name__}: {e}")
        except Exception as e:  # noqa: BLE001 — a crash is still a reportable failure
            failures.append(t.__name__)
            print(f"  ERROR: {t.__name__}: {type(e).__name__}: {e}")
    print(f"\n{'ALL PASS' if not failures else f'{len(failures)} FAILURES'} "
          f"({len(ALL_TESTS) - len(failures)}/{len(ALL_TESTS)})")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(run_all())
