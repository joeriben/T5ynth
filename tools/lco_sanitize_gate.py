"""Does `sanitize()` still take the ORCHESTRA out of the author's reply?

Since the author was asked to reason in plain language before writing the code
(`HOW TO ANSWER`), a reply is prose AND code, and `sanitize` has to tell them
apart. Getting that wrong is silent and expensive in both directions:

  - prose picked as the body -> the compiler is handed English, the attempt is
    burned, and a reply that contained a complete orchestra is reported as "the
    author could not write a compiling orchestra after N attempts",
  - code picked as the thinking -> the panel quotes Csound as the machine's
    reasoning, and the sketch it did NOT choose is what the user hears.

Neither shows up by ear and neither shows up in a green authoring run, because
the repair loop hides the first and the panel is only read afterwards. Hence a
gate, run on the shapes real replies actually take — including the malformed
ones, which are the whole reason this code is hard: the body is the LAST thing
written, so a generation that stops early loses its closing fence.

No model, no compiler, no IPC: this tests the parser, in-process, in a second.

Run:  .venv/bin/python tools/lco_sanitize_gate.py
"""
import os, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "backend"))
import lco_write as L  # noqa: E402

REASONING = "\n".join(
    f"The prompt asks for line {i} of reasoning about the timbre." for i in range(30))
BODY = "\n".join(["asig vco2 0.5, kfreq*koct1, 2"] * 10)

# (name, reply, what the body must start with, predicate on the thinking)
CASES = [
    ("well-formed",
     "It is a bowed string: noisy continuous excitation, woody body.\n\n"
     "```csound\nkstr limit kfreq * koct1, 20, 12000\naexc dust 1.0, 3900\n"
     "asig mode aexc, kstr, 240\nREADING: bowed string\n```\n",
     "kstr limit", lambda t: t.startswith("It is a bowed string")),

    # The body is written last, so a stopped generation loses the closing fence.
    # With reasoning longer than the code — which HOW TO ANSWER invites — a
    # line-counting split inverts here and compiles the prose.
    ("unclosed final fence, reasoning longer than the body",
     REASONING + "\n\n```csound\n" + BODY,
     "asig vco2", lambda t: t.startswith("The prompt asks")),

    # A sketch quoted mid-thought, then the real body with no closer: taking the
    # last COMPLETE fence ships the sketch and the user hears the wrong sound.
    ("sketch fence, then the real body unclosed",
     "First thought.\n```csound\nasig vco2 0.4, kfreq\n```\nOn reflection, better:\n"
     "```csound\nasig vco2 0.5, kfreq*koct1, 2\nasig tone asig, 900\n",
     "asig vco2 0.5", lambda t: "On reflection" in t),

    # A lone closer above the real block: pairing binds it to the real block's
    # OPENER and captures the whitespace between them -> an empty body.
    ("stray closer above the real block",
     "Reasoning here.\n```\n\n```csound\nasig vco2 0.5, kfreq\nREADING: x\n```\n",
     "asig vco2", lambda t: t.startswith("Reasoning here")),

    ("no fence at all",
     "asig poscil 0.4, kfreq\nREADING: plain\n",
     "asig poscil", lambda t: t == ""),

    ("nothing but a fence",
     "```csound\nasig poscil 0.4, kfreq\nREADING: bare\n```",
     "asig poscil", lambda t: t == ""),

    ("CRLF line endings",
     "Thinking.\r\n```csound\r\nasig poscil 0.4, kfreq\r\nREADING: crlf\r\n```\r\n",
     "asig poscil", lambda t: t == "Thinking."),

    # The thinking is a QUOTATION. A block the author quoted mid-sentence is part
    # of that sentence; deleting it leaves the sentence with its object missing
    # and still presents it as verbatim.
    ("a block quoted inside the reasoning survives",
     "The library saw idiom is:\n```\nasig vco2 0.5, kfreq\n```\n"
     "but a struck bar is closer.\n```csound\nares mode aexc, kfreq, 240\n"
     "READING: bar\n```",
     "ares mode",
     lambda t: "asig vco2 0.5, kfreq" in t and "but a struck bar is closer." in t),

    # Chat-template control tokens leak into the reply on the shipped GGUF. They
    # are the tokeniser's, not the author's, and must not be quoted as thinking.
    ("chat-template control tokens are not the author's words",
     "<|channel>thought\n<channel|>Reasoning.\n```csound\nasig poscil 0.4, kfreq\n```",
     "asig poscil", lambda t: t == "Reasoning."),
]

failures = []
for name, reply, body_starts, thinking_ok in CASES:
    body, reading, thinking = L.sanitize(reply)
    first = body.splitlines()[0] if body.strip() else "<EMPTY BODY>"
    if not first.startswith(body_starts):
        failures.append(f"{name}: body starts {first!r}, expected {body_starts!r}")
    elif not thinking_ok(thinking):
        failures.append(f"{name}: thinking is {thinking!r}")
    else:
        print(f"  ok   {name}", flush=True)

# The two must never be the same text: if they are, the split put one segment on
# both sides and the panel would caption the orchestra as its own reasoning.
for name, reply, _, _ in CASES:
    body, _, thinking = L.sanitize(reply)
    if thinking.strip() and thinking.strip() == body.strip():
        failures.append(f"{name}: thinking IS the body")

print()
if failures:
    print("FAIL")
    for f in failures:
        print("  " + f)
    sys.exit(1)
print(f"PASS — {len(CASES)} reply shapes, the orchestra came out of every one")
