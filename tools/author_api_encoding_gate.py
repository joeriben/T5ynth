#!/usr/bin/env python3
r"""Gate: what the author writes is what the panel shows, byte for byte.

WHY THIS EXISTS
===============
The LRO author's reasoning reached the THOUGHT panel with every curly
apostrophe, en dash and ellipsis replaced by a lone "a-circumflex" and two
invisible control characters -- reported six times, root cause not found on the
first five attempts because every reproduction was run against the LOCAL GGUF,
which never touches this transport.

The cause is in `requests`, and it is provider-dependent, which is why it looked
intermittent:

    requests.utils.get_encoding_from_headers({'content-type': 'text/event-stream'})
        -> 'ISO-8859-1'          # HTTP/1.1's default for any text/* with no charset
    requests.utils.get_encoding_from_headers({'content-type': 'text/event-stream; charset=utf-8'})
        -> 'utf-8'

`iter_lines(decode_unicode=True)` honours that, so against a provider that omits
the charset parameter -- which the SSE spec permits, since it mandates UTF-8
unconditionally -- the model's UTF-8 bytes were read one-byte-one-character.

A code-shaped check ("is resp.encoding assigned?") would not hold: the next way
to reintroduce this is not the next line of author_api.py, it is a requests
upgrade, a new provider, a fourth transport. So this gate asserts the BEHAVIOUR,
against a real socket, with the exact header the corrupting providers send.

WHAT IT CHECKS
==============
Both transports (`call_openai_compatible`, `call_anthropic`), streaming and
non-streaming, and the provider-error body, over a real HTTP server whose
Content-Type deliberately carries NO charset:

  1. Streamed text round-trips byte for byte -- including characters split
     across TCP writes, which is where an incremental decoder is the only
     correct answer.
  2. Every delta handed to on_delta concatenates to that same text, so the LIVE
     panel and the final trace cannot disagree.
  3. Non-streamed text round-trips.
  4. A provider's error message round-trips into AuthorAPIError.
  5. A frame carrying U+2028 survives whole. `str.splitlines()` treats U+2028,
     U+2029, U+0085, \v and \f as line breaks, so decoding before splitting cuts
     such a frame in half and the malformed JSON is dropped in silence.

The corpus is deliberately wider than the bug: General Punctuation is what was
observed, but a fix scoped to what was observed is how this became a six-time
report. CJK, an emoji outside the BMP, and combining marks are in there because
a transport that is right about them cannot be wrong about a dash.

USAGE
=====
    python3 tools/author_api_encoding_gate.py
Exit 0 clean, 1 on the first failure with the exact bytes on both sides.
"""

import json
import os
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "backend"))

import author_api  # noqa: E402


# The characters an LLM actually reaches for, plus enough beyond them that a
# repair scoped to one Unicode block could not pass this.
CORPUS = (
    "I’m imagining a damp fluorescent tube — 6–8 times per second, "
    "“grabbed” and slid… "
    "ratio 3:2×, ±3 cents, 440 Hz, café naïve straße, "
    "中文テスト русский "
    "\U0001f50a é"
)

# Split so that multi-byte characters land across TCP write boundaries: every
# piece here is emitted as its own SSE frame, and the frames are written in two
# socket writes with the split falling mid-character.
PIECES = [CORPUS[i:i + 7] for i in range(0, len(CORPUS), 7)]

FAILURES = []


def fail(what, expected, got):
    FAILURES.append(what)
    print(f"FAIL  {what}")
    print(f"      expected {expected!r}")
    print(f"      got      {got!r}")


def check(what, expected, got):
    if expected == got:
        print(f"ok    {what}")
    else:
        fail(what, expected, got)


class _Server:
    """A real HTTP server whose Content-Type carries no charset -- the shape
    that corrupts. Constructed per case so each case gets a clean socket."""

    def __init__(self, handler_cls):
        self.httpd = HTTPServer(("127.0.0.1", 0), handler_cls)
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)

    def __enter__(self):
        self.thread.start()
        return f"http://127.0.0.1:{self.httpd.server_port}"

    def __exit__(self, *exc):
        self.httpd.shutdown()
        self.httpd.server_close()


def _handler(content_type, body_writer, status=200):
    class H(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.0"

        def log_message(self, *a):
            pass

        def do_POST(self):
            self.rfile.read(int(self.headers.get("content-length", 0)))
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.end_headers()
            body_writer(self.wfile)
            self.wfile.flush()
    return H


def _sse(frames):
    """Serialise SSE frames and hand them back as a writer that splits the byte
    stream mid-character, so an incremental decoder is genuinely required."""
    blob = b"".join(b"data: " + json.dumps(f, ensure_ascii=False).encode("utf-8") + b"\n\n"
                    for f in frames)
    blob += b"data: [DONE]\n\n"

    def write(out):
        # Cut at a byte that is a UTF-8 continuation byte where possible, so the
        # split lands inside a character rather than between two.
        cut = next((i for i in range(len(blob) // 3, len(blob)) if blob[i] & 0xC0 == 0x80),
                   len(blob) // 2)
        out.write(blob[:cut])
        out.flush()
        out.write(blob[cut:])
    return write


def case_openai_streamed():
    frames = [{"choices": [{"delta": {"content": p}}]} for p in PIECES]
    deltas = []
    with _Server(_handler("text/event-stream", _sse(frames))) as base:
        got = author_api.call_openai_compatible(base + "/v1", "k", "m", "hi", "sys",
                                                on_delta=deltas.append)
    check("openai-compatible, streamed, charset-less text/event-stream", CORPUS, got)
    check("openai-compatible, live deltas match the final text", CORPUS, "".join(deltas))


def case_anthropic_streamed():
    frames = [{"type": "content_block_delta", "delta": {"text": p}} for p in PIECES]
    deltas = []
    with _Server(_handler("text/event-stream", _sse(frames))) as base:
        author_api._ANTHROPIC_URL = base + "/v1/messages"
        got = author_api.call_anthropic("k", "m", "hi", "sys", on_delta=deltas.append)
    check("anthropic, streamed, charset-less text/event-stream", CORPUS, got)
    check("anthropic, live deltas match the final text", CORPUS, "".join(deltas))


def case_line_separator_frame():
    """U+2028 inside the prose must not cut the frame in half."""
    # Written as escapes on purpose: these characters are invisible in a
    # source file, and a test whose subject cannot be seen is not a test.
    text = "before\u2028after\u2029and\u0085more\x0bstill\x0chere"
    frames = [{"choices": [{"delta": {"content": text}}]}]
    with _Server(_handler("text/event-stream", _sse(frames))) as base:
        got = author_api.call_openai_compatible(base + "/v1", "k", "m", "hi", "sys",
                                                on_delta=lambda p: None)
    check("a frame carrying U+2028 survives whole", text, got)


def case_openai_not_streamed():
    body = {"choices": [{"message": {"content": CORPUS}}]}

    def write(out):
        out.write(json.dumps(body, ensure_ascii=False).encode("utf-8"))
    with _Server(_handler("application/json", write)) as base:
        got = author_api.call_openai_compatible(base + "/v1", "k", "m", "hi", "sys")
    check("openai-compatible, not streamed", CORPUS, got)


def case_anthropic_not_streamed():
    body = {"content": [{"type": "text", "text": CORPUS}]}

    def write(out):
        out.write(json.dumps(body, ensure_ascii=False).encode("utf-8"))
    with _Server(_handler("application/json", write)) as base:
        author_api._ANTHROPIC_URL = base + "/v1/messages"
        got = author_api.call_anthropic("k", "m", "hi", "sys")
    check("anthropic, not streamed", CORPUS, got)


def case_error_body():
    """A provider's own error text reaches the user through _truncated_body."""
    msg = "quota exhausted — top up at the provider’s console"

    def write(out):
        out.write(msg.encode("utf-8"))
    with _Server(_handler("text/plain", write, status=400)) as base:
        try:
            author_api.call_openai_compatible(base + "/v1", "k", "m", "hi", "sys")
            fail("provider error body round-trips", msg, "<no error raised>")
            return
        except author_api.AuthorAPIError as exc:
            got = str(exc)
    if msg in got:
        print("ok    provider error body round-trips")
    else:
        fail("provider error body round-trips", msg, got)


def main():
    real_anthropic_url = author_api._ANTHROPIC_URL
    try:
        case_openai_streamed()
        case_anthropic_streamed()
        case_line_separator_frame()
        case_openai_not_streamed()
        case_anthropic_not_streamed()
        case_error_body()
    finally:
        author_api._ANTHROPIC_URL = real_anthropic_url

    print()
    if FAILURES:
        print(f"author api encoding gate: {len(FAILURES)} failure(s).")
        print("The author's words are not surviving the wire. Check that "
              "author_api._post pins")
        print("resp.encoding and that both transports read their SSE through "
              "_sse_lines -- requests")
        print("decodes a charset-less text/* as ISO-8859-1, which is exactly "
              "this corruption.")
        return 1
    print("author api encoding gate: clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
