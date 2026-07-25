"""External-API transport for the LRO author.

Same plain-string / on_delta(piece) contract as pipe_inference.py's
run_gguf_instruct: this module knows nothing about Csound, translation or the
repair loop (pipe_inference.py / lco_write.py own all of that) -- it only
speaks one provider's wire format and hands back text, so it can be dropped
behind the exact closure shape run_gguf_instruct already fills.

GUARDRAIL: never log the raw api_key, the Authorization/x-api-key header
value, or a full outgoing request body/headers dict. pipe_inference.py's
root logger (logging.basicConfig) writes unconditionally to
backend_stderr.log, a file users routinely paste into support requests --
log host + model + HTTP status + a truncated PROVIDER response body only.
"""

import json
import logging

import requests

log = logging.getLogger("author_api")

# Reaching the host at all is bounded; what the model then takes to answer is
# not (see below).
_CONNECT_TIMEOUT = 10
# No read timeout on a model call, ever. A free tier queues before it answers
# and a 120B can sit silent well past any number that looks generous here, so
# a read cap does not catch a stalled stream -- it kills a healthy slow one,
# and the user sees a timeout where the model was simply thinking. Reaching an
# unreachable host is the connect timeout's job, and it still applies.
_READ_TIMEOUT = None

# Reused across calls: the csound repair loop makes several sequential calls
# per authoring attempt, and a fresh TLS handshake per repair round would
# compound the already-worse latency of a network call.
_session = requests.Session()


class AuthorAPIError(RuntimeError):
    """One external-author-API failure, one actionable message. Distinct text
    per failure mode is deliberate -- a generic "external call failed" would
    be exactly the un-actionable failure this codebase's error messages
    elsewhere avoid (see pipe_inference.py's "... not installed (load it in
    Settings)" style)."""


def _coerce_turns(text):
    """Mirrors run_gguf_instruct's text-or-message-list contract exactly, so
    the API path stays a drop-in replacement for the local one. Empty in ->
    None out (the caller returns "" for that, same as the local path)."""
    if isinstance(text, (list, tuple)):
        turns = [dict(m) for m in text if (m.get("content") or "").strip()]
        return turns or None
    text = (text or "").strip()
    if not text:
        return None
    return [{"role": "user", "content": text}]


def _truncated_body(resp, limit=200):
    try:
        body = resp.text
    except Exception:
        return ""
    body = body.strip().replace("\n", " ")
    return body if len(body) <= limit else body[:limit] + "..."


def _post(url, headers, payload, host, stream=False):
    try:
        resp = _session.post(url, headers=headers, json=payload,
                              timeout=(_CONNECT_TIMEOUT, _READ_TIMEOUT),
                              stream=stream)
    except requests.exceptions.Timeout as exc:
        raise AuthorAPIError(f"{host}: request timed out") from exc
    except requests.exceptions.ConnectionError as exc:
        raise AuthorAPIError(f"{host}: could not connect") from exc
    except requests.exceptions.RequestException as exc:
        raise AuthorAPIError(f"{host}: request failed ({exc.__class__.__name__})") from exc

    if resp.status_code >= 400:
        # Read+raise inside a finally-close regardless of `stream`: a rejected
        # call never reaches the caller's own iter_lines()/resp.close() path,
        # so without this a streaming call rejected by status (expired key,
        # rate limit, ...) leaks its connection out of _session's pool -- and
        # the csound repair loop can make several of these in a row against
        # the same bad key.
        try:
            if resp.status_code in (401, 403):
                raise AuthorAPIError(f"{host}: invalid API key (HTTP {resp.status_code})")
            if resp.status_code == 429:
                raise AuthorAPIError(f"{host}: rate-limited by provider (HTTP 429)")
            if resp.status_code >= 500:
                raise AuthorAPIError(f"{host}: provider server error (HTTP {resp.status_code})")
            raise AuthorAPIError(
                f"{host}: request rejected (HTTP {resp.status_code}): {_truncated_body(resp)}")
        finally:
            resp.close()
    return resp



# ── What a call cost ───────────────────────────────────────────────────
#
# Carried over from the maintainer's own platform (sarah
# src/lib/server/ai/client.ts), which reads `usage` off every response and
# threads the token counts through the whole pipeline. The USD half of that
# code is NOT carried over, because it does not exist there: accumulated_cost_usd
# is a declared column that is never written with a computed value, and a price
# table per model would be stale by the next provider price change anyway.
#
# So: tokens always (every provider reports them, and tokens are what the user
# is billed on), and real money only where the provider itself states it --
# OpenRouter returns `usage.cost` in account credits when the request asks for
# it, which is a measured number rather than one this code guessed.
#
# Two booleans travel with the numbers, because the caller cannot tell either
# of them from the numbers alone and BOTH are about the user's money:
#
#   served   -- the provider actually answered (HTTP 2xx). False for a call
#               that never got there: no connection, an invalid key, a 429.
#               Nothing is billed for those, so counting them as calls made
#               would report spending that did not happen -- and, worse, would
#               push the "some calls are not priced here" caveat on forever
#               after one 429, in a session where every billed call IS priced.
#   complete -- the numbers are final. Anthropic sends the input count in
#               `message_start` and the output count only in `message_delta`,
#               so a stream that drops between the two leaves a usage block
#               that LOOKS whole (input + an initial output of 1) and is short
#               by the entire generation. That call was billed, so its floor is
#               kept -- but flagged, and the panel then says "at least".
_LAST_USAGE = {"served": False, "complete": False}


def last_usage():
    """The most recent call: {"served", "complete", "input", "output", "cost"}.
    `input`/`output`/`cost` are absent when the provider reported none; `cost`
    only ever appears where the provider stated one. Read it straight after the
    call -- a module-level slot, deliberately, so the text-in/text-out contract
    that makes this a drop-in for run_gguf_instruct stays exactly as it is."""
    return dict(_LAST_USAGE)


def _begin_call():
    """Reset the slot. Called before ANY early return in a transport: the
    caller reads it in a `finally`, so a call that returns without touching the
    network would otherwise hand it the previous call's numbers to bill twice."""
    _LAST_USAGE.clear()
    _LAST_USAGE["served"] = False
    _LAST_USAGE["complete"] = False


def _record_usage(usage, complete=True):
    """Normalise one provider's usage block. OpenAI-compatible names first,
    Anthropic's native ones as the fallback -- the two disagree on every key."""
    if not isinstance(usage, dict):
        return
    prompt = usage.get("prompt_tokens", usage.get("input_tokens"))
    completion = usage.get("completion_tokens", usage.get("output_tokens"))
    if prompt is not None:
        _LAST_USAGE["input"] = int(prompt)
    if completion is not None:
        _LAST_USAGE["output"] = int(completion)
    reported = usage.get("cost")
    if reported is not None:
        try:
            _LAST_USAGE["cost"] = float(reported)
        except (TypeError, ValueError):
            pass
    _LAST_USAGE["complete"] = complete


def _is_openrouter(base_url):
    return "openrouter.ai" in (base_url or "")


def call_openai_compatible(base_url, api_key, model, text, system_prompt,
                            max_new_tokens=None, on_delta=None):
    """One instruct call against any OpenAI-compatible /chat/completions
    endpoint (OpenAI, Mistral, OpenRouter, a remote llama.cpp `server`, ...).

    No local-style "-1 means unlimited" sentinel exists on this wire: when
    max_new_tokens is unset, `max_tokens` is OMITTED from the payload rather
    than sent as -1, because most hosted providers reject a negative value
    with a 400."""
    _begin_call()
    turns = _coerce_turns(text)
    if turns is None:
        return ""
    url = base_url.rstrip("/") + "/chat/completions"
    # A local server (Ollama) has no account and no key; its OpenAI-compatible
    # shim still wants the header to exist. Same placeholder the maintainer's
    # other platform uses (sarah client.ts: `apiKey || 'ollama'`).
    headers = {"Authorization": f"Bearer {api_key or 'local'}",
               "Content-Type": "application/json"}
    payload = {
        "model": model,
        "messages": [{"role": "system", "content": system_prompt}] + turns,
        "temperature": 0.0,
    }
    if max_new_tokens:
        payload["max_tokens"] = int(max_new_tokens)
    # OpenRouter states what the call actually cost, in account credits, but
    # only when asked. Every other endpoint ignores the field.
    if _is_openrouter(base_url):
        payload["usage"] = {"include": True}

    if on_delta is None:
        resp = _post(url, headers, payload, host=base_url)
        _LAST_USAGE["served"] = True     # 2xx in hand: this one was billed
        try:
            body = resp.json()
            _record_usage(body.get("usage"))
            return body["choices"][0]["message"]["content"] or ""
        except (KeyError, IndexError, TypeError, ValueError) as exc:
            raise AuthorAPIError(f"{base_url}: malformed response ({exc})") from exc

    payload["stream"] = True
    # Without this the streamed response carries no usage block at all and the
    # run would report zero tokens for a generation that plainly cost some.
    payload["stream_options"] = {"include_usage": True}
    parts = []
    resp = _post(url, headers, payload, host=base_url, stream=True)
    _LAST_USAGE["served"] = True
    try:
        for line in resp.iter_lines(decode_unicode=True):
            if not line or not line.startswith("data:"):
                continue
            data = line[len("data:"):].strip()
            if data == "[DONE]":
                break
            try:
                chunk = json.loads(data)
            except ValueError:
                continue
            # The usage block arrives in its own final frame, which carries an
            # empty `choices` list -- so it must be read before the content
            # extraction below skips the frame.
            if chunk.get("usage"):
                _record_usage(chunk["usage"])
            try:
                piece = (chunk["choices"][0].get("delta") or {}).get("content") or ""
            except (KeyError, IndexError, TypeError):
                continue
            if not piece:
                continue
            parts.append(piece)
            if on_delta is not None:
                try:
                    on_delta(piece)
                except Exception as exc:
                    log.warning(f"streaming callback failed, generation continues: {exc}")
                    on_delta = None
    except requests.exceptions.RequestException as exc:
        raise AuthorAPIError(
            f"{base_url}: connection dropped mid-stream ({exc.__class__.__name__})") from exc
    finally:
        resp.close()
    return "".join(parts)


_ANTHROPIC_URL = "https://api.anthropic.com/v1/messages"
_ANTHROPIC_VERSION = "2023-06-01"
_ANTHROPIC_HOST = "api.anthropic.com"
# Anthropic's max_tokens is REQUIRED and has no "unbounded" sentinel, unlike
# llama.cpp's max_tokens=-1 that the local path relies on to let a csound
# orchestra run to its own natural end (see run_gguf_instruct and
# lco_write.py's own "no token cap" contract). A LOW default here would
# silently truncate an orchestra mid-line -- csound would then report a
# syntax error with no visible cause, and the repair loop would re-run and
# re-truncate at the same boundary every attempt. So this is a genuine
# current-generation ceiling, not a convenience number: a model too old/small
# to support it fails loudly with an actionable "request rejected" error
# (_post already surfaces the provider's own message), which is the correct
# trade against a cap that fails silently.
_ANTHROPIC_DEFAULT_MAX_TOKENS = 64000


def call_anthropic(api_key, model, text, system_prompt, max_new_tokens=None,
                    on_delta=None):
    """One instruct call against Anthropic's native Messages API. Anthropic
    never takes a `system` role inside `messages` -- system_prompt goes in
    its own top-level field; the turns themselves are the same user/assistant
    shape run_gguf_instruct already produces."""
    _begin_call()
    turns = _coerce_turns(text)
    if turns is None:
        return ""
    headers = {
        "x-api-key": api_key,
        "anthropic-version": _ANTHROPIC_VERSION,
        "content-type": "application/json",
    }
    payload = {
        "model": model,
        "system": system_prompt,
        "messages": turns,
        "max_tokens": int(max_new_tokens) if max_new_tokens else _ANTHROPIC_DEFAULT_MAX_TOKENS,
        "temperature": 0.0,
    }

    if on_delta is None:
        resp = _post(_ANTHROPIC_URL, headers, payload, host=_ANTHROPIC_HOST)
        _LAST_USAGE["served"] = True
        try:
            body = resp.json()
            _record_usage(body.get("usage"))
            blocks = body["content"]
            return "".join(b.get("text", "") for b in blocks if b.get("type") == "text")
        except (KeyError, IndexError, TypeError, ValueError) as exc:
            raise AuthorAPIError(f"{_ANTHROPIC_HOST}: malformed response ({exc})") from exc

    payload["stream"] = True
    parts = []
    resp = _post(_ANTHROPIC_URL, headers, payload, host=_ANTHROPIC_HOST, stream=True)
    _LAST_USAGE["served"] = True
    try:
        for line in resp.iter_lines(decode_unicode=True):
            if not line or not line.startswith("data:"):
                continue
            data = line[len("data:"):].strip()
            if not data:
                continue
            try:
                event = json.loads(data)
            except ValueError:
                continue
            # Anthropic splits usage across two events: message_start carries
            # the input count, message_delta the output count as it finishes.
            # Merged rather than overwritten, or the second wipes the first.
            if event.get("type") == "message_start":
                # NOT complete: the output count here is Anthropic's initial 1,
                # and the real one only arrives with message_delta below.
                _record_usage((event.get("message") or {}).get("usage"),
                              complete=False)
            elif event.get("type") == "message_delta" and event.get("usage"):
                out = event["usage"].get("output_tokens")
                if out is not None:
                    _LAST_USAGE["output"] = int(out)
                    _LAST_USAGE["complete"] = True
            if event.get("type") != "content_block_delta":
                continue
            piece = (event.get("delta") or {}).get("text") or ""
            if not piece:
                continue
            parts.append(piece)
            if on_delta is not None:
                try:
                    on_delta(piece)
                except Exception as exc:
                    log.warning(f"streaming callback failed, generation continues: {exc}")
                    on_delta = None
    except requests.exceptions.RequestException as exc:
        raise AuthorAPIError(
            f"{_ANTHROPIC_HOST}: connection dropped mid-stream ({exc.__class__.__name__})") from exc
    finally:
        resp.close()
    return "".join(parts)
