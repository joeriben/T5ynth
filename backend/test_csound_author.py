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
    for key in lex.FAMILY_KEYS + lex.CHARACTER_KEYS + lex.MOTION_KEYS + lex.ENVELOPE_KEYS:
        assert key in prompt, f"lexicon key {key!r} missing from the generated system prompt"


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
