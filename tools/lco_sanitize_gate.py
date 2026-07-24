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

# The reasoning has to be the reasoning the author actually writes, and this is
# the whole difficulty of the corpus: HOW TO ANSWER asks three questions, and the
# answers come back as FRAGMENTS, which have no full stop to mark them as prose
# and routinely begin with a word starting a/k/i/f/S — the Csound type sigils.
# Generic filler ("The prompt asks for line 3 of reasoning.") scores zero and
# would let every defect this file exists to catch pass unnoticed, so the corpus
# asserts its own teeth below: it must outscore the body it is paired with.
REASONING = """What IS this sound: a cello, bowed, not plucked
a body of wood, air inside it, a long string across a bridge
and the bow is what keeps it going, so the excitation is continuous
it is not a struck thing, no impulse, nothing decays on its own
first the string, which is a comb filter on whatever excites it
k rate is enough for the bow pressure, it does not need audio rate
in the low register the body resonance sits under 300 Hz
as the bow speed rises the higher partials come up with it
kind of a nasal edge when the pressure is high
So: continuous noisy excitation into a string model, then a body filter
after that a gentle low shelf so it does not get thin
always moving, because a held bow is never perfectly steady"""
BODY = "\n".join([
    "aexc  dust 1.0, 3900",
    "kstr  limit kfreq * koct1, 20, 12000",
    "asig  mode aexc, kstr, 240",
    "asig  butlp asig, 2400",
])

# A library idiom quoted verbatim runs 8-18 lines; the body the author adapts
# from it runs 3-6. Whichever segment merely COUNTS higher is therefore the
# quotation, systematically — so a scoring picker ships the library's own
# example as the sound and captions the real orchestra as reasoning.
LIB_IDIOM = """aexq0   rand 0.06, 0.5, 1
k0qf0   limit (kfreq * koct1) * 1.0000, 20, 15000
k0qg0   = ((kfreq * koct1) * 1.0000 < 15000 ? 0.850 : 0)
a0q0   mode aexq0, k0qf0, 900
k0qf1   limit (kfreq * koct1) * 2.7600, 20, 15000
k0qg1   = ((kfreq * koct1) * 2.7600 < 15000 ? 0.700 : 0)
a0q1   mode aexq0, k0qf1, 1025
k0qf2   limit (kfreq * koct1) * 5.4000, 20, 15000
k0qg2   = ((kfreq * koct1) * 5.4000 < 15000 ? 0.500 : 0)
a0q2   mode aexq0, k0qf2, 1150
asig    = (a0q0 * k0qg0 + a0q1 * k0qg1 + a0q2 * k0qg2) * 0.150"""

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
     "aexc  dust", lambda t: t.startswith("What IS this sound")),

    # ... and the same reasoning under a PROPERLY CLOSED fence. Trivial to get
    # right — the fence says exactly where the body is — but a picker that
    # decides by content alone gets it wrong here too, so the damage is no
    # longer confined to malformed replies. This case is the canary.
    ("well-formed fence, reasoning longer than the body",
     REASONING + "\n\n```csound\n" + BODY + "\nREADING: bowed cello\n```\n",
     "aexc  dust", lambda t: t.startswith("What IS this sound")),

    # The author is oriented by the library, quotes the idiom it is working
    # from, and then writes something shorter of its own. The quotation always
    # comes first and is always the longer of the two.
    ("a library idiom quoted, then a shorter body of the author's own",
     "The struck_bar idiom is the closest starting point:\n\n```csound\n"
     + LIB_IDIOM + "\n```\n\nBut a cello is bowed, so the excitation is "
     "continuous and there is one string, not five modes:\n\n```csound\n"
     + BODY + "\nREADING: bowed cello\n```\n",
     "aexc  dust", lambda t: "k0qf0   limit" in t and "struck_bar idiom" in t),

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

# The corpus must be able to FAIL. Both halves of that: reasoning that a content
# score mistakes for code, and a quotation longer than the body. A corpus whose
# prose looks nothing like code passes every case above no matter how the picker
# is written, and reads as proof while proving nothing.
#
# Measured with the LINE MATCHER alone, deliberately not with _codeishness: the
# English-fragment guard inside _codeishness is itself under test here, so using
# it as the yardstick would let a change to that guard quietly declaw the corpus
# instead of failing it.
def _looks_like_code(chunk):
    return sum(1 for ln in chunk.splitlines()
               if ln.strip() and not L._SENTENCE_END.search(ln)
               and L._CODE_LINE.match(ln))


_prose, _body, _lib = (_looks_like_code(REASONING), _looks_like_code(BODY),
                       _looks_like_code(LIB_IDIOM))
if _prose <= _body:
    failures.append(f"corpus has no teeth: {_prose} lines of the reasoning read as "
                    f"code against {_body} in the body — a picker that counts code "
                    "lines would pass by luck")
if _lib <= _body:
    failures.append(f"corpus has no teeth: quoted idiom scores {_lib}, body {_body} — "
                    "the quotation must be the LONGER of the two")

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
