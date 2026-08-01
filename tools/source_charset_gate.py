#!/usr/bin/env python3
r"""Build gate: no C++ string literal may carry a character above ASCII unless
the code says, at that literal, how the bytes are to be read.

WHY THIS IS A GATE AND NOT A CONVENTION
=======================================
`juce::String` has no way to know what encoding a `const char*` is in, so it
assumes the only one that needs no decision -- one byte, one character:

    JUCE/modules/juce_core/text/juce_CharPointer_ASCII.h:108
        juce_wchar getAndAdvance() noexcept { return (juce_wchar) (uint8) *data++; }

    JUCE/modules/juce_core/text/juce_String.cpp:307
        String::String (const char* const t)
            : text (StringHolderUtils::createFromCharPointer (CharPointer_ASCII (t)))

That is Latin-1. So a source file saved as UTF-8 -- which every editor does by
default -- hands `String` the three bytes E2 80 94 for one em dash, and gets
three characters back: "a" with a circumflex, plus two invisible C1 controls.
The em dash is gone and an orphaned "a-circumflex" stands where it was. Same for
every curly quote, apostrophe, ellipsis and arrow.

JUCE's only defence is the `jassert` two lines below that constructor. This
project builds Release (CLAUDE.md: "Always build_clean/ with Release config"),
where `jassert` compiles to nothing. So the corruption is invisible at build
time, invisible at test time, and visible only to the person using the
instrument -- which is exactly how it reached shipped menus repeatedly.

The rule this enforces is therefore not "avoid non-ASCII". It is: WHEREVER a
literal is not plain ASCII, the reading of its bytes must be stated in the code
itself, next to the literal, in a form the compiler carries:

    juce::String::fromUTF8 ("Backend not found \xe2\x80\x94 reinstall ")
    juce::String (juce::CharPointer_UTF8 ("\xE2\x88\x92"))

Both are unambiguous and both survive any editor, compiler and platform. A raw
"--" pasted into a literal is not, and never can be.

WHAT FAILS
==========
1. A raw byte above 0x7F inside a string literal. Always -- the wrapper does not
   redeem it, because the bytes then depend on how the file was saved.
2. An escape that encodes a character above 0x7F (\xNN, \NNN, \uNNNN, \UNNNNNNNN)
   in a literal that is NOT the argument of one of the decoders in _DECODERS.
   Without that, the escape merely moves the same Latin-1 read one line along.

Comments are exempt: they never become a `juce::String`. The prose in this
codebase is full of em dashes and stays that way.

ESCAPE HATCH
============
A line ending in `// charset-ok: <reason>` is skipped, for the case where the
bytes are deliberately not text (a wire fixture, a byte-level comparison). It
requires a reason, and `grep -rn "charset-ok" src/` lists every one.

USAGE
=====
    python3 tools/source_charset_gate.py [ROOT ...]           # check; default src/
    python3 tools/source_charset_gate.py --fix [ROOT ...]     # check and repair

Exit 0 clean, 1 with a report. Wired into CMake as a pre-build target, so a build
cannot be produced that carries a violation.

--fix does the repair rather than asking for it, because the repair is
mechanical: the raw character becomes its UTF-8 escape, and the literal is
wrapped in juce::String::fromUTF8(...). Measured against this repo's own history:
of the 60 violations that stood in src/ before this rule existed, 47 are repaired
without a question. It changes no string -- every literal's decoded text is
identical before and after, checked across all 4022 literal runs in src/ -- and
all 47 translation units still compile.

The 13 it does not touch are the ones it must not: four `static_assert` messages
(a wrap does not compile), a `DBG` and an `fprintf` (not display text), and seven
`const char*` catalog entries decoded elsewhere. It only ever makes an edit that
leaves the literal CORRECT; it never makes one that merely makes it tidier. What
it declines it names, with the two answers available: wrap it, or say
`// charset-ok: <reason>`.
"""

import os
import re
import sys

_EXTS = (".cpp", ".h", ".hpp", ".mm", ".c", ".cc")

# Constructors that state the encoding of the bytes they are handed. A literal
# with a high escape is only legal as an argument of one of these.
_DECODERS = ("fromUTF8", "CharPointer_UTF8", "CharPointer_UTF16",
             "CharPointer_UTF32", "createStringFromData")

# `<decoder> (` immediately before the literal run, whitespace and the literal's
# own encoding prefix allowed -- without the prefix, the correct
# `fromUTF8(u8"...")` would be reported as a violation and block a build.
_DECODER_CALL = re.compile(r"(?:%s)\s*\(\s*(?:u8|u|U|L|R)?$" % "|".join(_DECODERS))

# Only ever honoured inside a real comment -- see _scan. Matched against the
# source line, but the marker's offset must land in a comment span, or
# `juce::String("// charset-ok: x \xe2\x80\x94")` would excuse itself.
_OPT_OUT = re.compile(r"//\s*charset-ok:\s*\S")

# Encoding prefixes a literal may carry. R is handled separately (raw strings
# have no escapes and no line splices).
_PREFIX = r"(?:u8|u|U|L)?"

# Escapes that can denote a character above ASCII.
_HEX_ESC = re.compile(r"\\x([0-9a-fA-F]+)")
_OCT_ESC = re.compile(r"\\([0-7]{1,3})")
_UNI_ESC = re.compile(r"\\u([0-9a-fA-F]{4})|\\U([0-9a-fA-F]{8})")


_RAW_OPEN = re.compile(r'(?:u8|u|U|L)?R"([^ ()\\\t\v\f\n]{0,16})\(')
_LIT_OPEN = re.compile(r'(?:u8|u|U|L)?"')
_HEXDIGIT = "0123456789abcdefABCDEF"


def _scan(src):
    """One pass over a translation unit, hand-written because every regex-shaped
    shortcut here has a false negative that hides a real violation.

    Returns (blanked, skeleton, literals, comments):
      blanked   -- `src` with comments and character literals turned to spaces,
                   every byte offset and newline preserved, so offsets still map
                   to real line numbers
      skeleton  -- blanked, and with the literals' CONTENTS spaced out too. Only
                   for counting brackets: `DBG("... (loader accepts " << x)` has
                   an opening parenthesis inside a string, and a bracket count
                   that believes it walks out of the wrong call -- which is how
                   a static_assert message would get wrapped in a juce::String
                   and stop compiling.
      literals  -- [(start, end, kind)] with kind 'raw' or 'esc'
      comments  -- [(start, end)] spans

    What it has to get right, each one a hole a simpler version had:
      * RAW STRINGS. `R"NOTICE(...)"` runs across newlines and contains no
        escapes at all. src/gui/SetupWizard.cpp already has three. A scanner
        that reads their opening quote as an ordinary one loses its place for
        the rest of the paragraph.
      * DIGIT SEPARATORS. `1'000` is not a character literal. Reading it as one
        blanks the source from there to the next apostrophe, taking any
        violation in between with it.
      * LINE SPLICES. A backslash at end of line continues a literal onto the
        next line; a scanner that ends the literal at the newline loses the
        rest.
      * PREFIXES. `u8"..."`, `L"..."` -- the quote is not the start of the
        token, and the decoder check reads what stands in front of the token."""
    out = list(src)
    literals, comments = [], []
    i, n = 0, len(src)

    def blank(a, b):
        for k in range(a, b):
            if out[k] != "\n":
                out[k] = " "

    while i < n:
        c = src[i]

        if c == "/" and src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            comments.append((i, j))
            blank(i, j)
            i = j
            continue

        if c == "/" and src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            comments.append((i, j))
            blank(i, j)
            i = j
            continue

        if c == "'":
            # A digit separator, not a literal: C++14 allows 1'000, and it only
            # ever stands between two digits of one numeric token.
            if (0 < i < n - 1 and src[i - 1] in _HEXDIGIT and src[i + 1] in _HEXDIGIT):
                i += 1
                continue
            j = i + 1
            while j < n and src[j] != "'":
                j += 2 if src[j] == "\\" else 1
            blank(i, min(j + 1, n))
            i = j + 1
            continue

        if c in "RuUL\"":
            m = _RAW_OPEN.match(src, i)
            if m:
                close = ')' + m.group(1) + '"'
                j = src.find(close, m.end())
                j = n if j < 0 else j + len(close)
                literals.append((i, j, "raw"))
                i = j
                continue
            m = _LIT_OPEN.match(src, i)
            if m:
                j = m.end()                     # first byte after the quote
                while j < n and src[j] != '"':
                    if src[j] == "\\":
                        j += 2                  # covers \" and the \<newline> splice
                    elif src[j] == "\n":
                        break                   # unterminated; do not run away
                    else:
                        j += 1
                literals.append((i, min(j + 1, n), "esc"))
                i = j + 1
                continue

        i += 1

    skel = list(out)
    for a, b, _kind in literals:
        for k in range(a, b):
            if skel[k] != "\n":
                skel[k] = " "
    return "".join(out), "".join(skel), literals, comments


def _runs(code, literals):
    """Adjacent literals are one argument to the compiler ("a" "b" == "ab"), so
    they are one unit here too -- otherwise `fromUTF8("akr\\xc3\\xb3" "asys")`
    would report its second half as unwrapped. Whitespace and comments may stand
    between them, and comments are already blanked in `code`."""
    i = 0
    while i < len(literals):
        j = i
        while (j + 1 < len(literals)
               and code[literals[j][1]:literals[j + 1][0]].strip() == ""):
            j += 1
        start, end = literals[i][0], literals[j][1]
        kinds = {literals[k][2] for k in range(i, j + 1)}
        yield start, end, kinds
        i = j + 1


def _high_escapes(text):
    """Escaped code points above 0x7F in a literal run, as written."""
    found = []
    for m in _HEX_ESC.finditer(text):
        # C++ \x consumes every following hex digit; only the low byte can
        # exceed ASCII in the narrow literals this codebase writes.
        if int(m.group(1), 16) > 0x7F:
            found.append(m.group(0))
    for m in _OCT_ESC.finditer(text):
        if int(m.group(1), 8) > 0x7F:
            found.append(m.group(0))
    for m in _UNI_ESC.finditer(text):
        if int(m.group(1) or m.group(2), 16) > 0x7F:
            found.append(m.group(0))
    return found


def _opt_out_lines(src, comments):
    """Line numbers carrying a `charset-ok` marker THAT IS A COMMENT. Matching
    the raw line instead would let `juce::String("// charset-ok: x \\xe2\\x80\\x94")`
    excuse itself -- an opt-out reachable from inside the thing it excuses is
    not an opt-out."""
    out = set()
    for a, b in comments:
        for m in _OPT_OUT.finditer(src, a, b):
            out.add(src.count("\n", 0, m.start()) + 1)
    return out


# Callers whose argument must stay a plain literal: wrapping one of these in a
# juce::String would not compile (static_assert wants a literal) or would change
# what the call does (a byte-level printf, a debug macro).
_NEVER_WRAP = {"static_assert", "fprintf", "printf", "sprintf", "snprintf",
               "fputs", "puts", "fwrite", "strlen", "strcmp", "strncmp",
               "DBG", "jassert", "jassertfalse", "error", "pragma"}

_IDENT_BEFORE_PAREN = re.compile(r"([A-Za-z_][A-Za-z_0-9]*)\s*\($")

# Operators after which a literal is being used as a VALUE in an expression, so
# putting a juce::String there is the same expression with the encoding stated.
# `,` and `{` are deliberately absent: those are also how a `const char*` table's
# entries are written, and wrapping one of those does not compile.
_VALUE_BEFORE = ("=", "<<", "+", "?", ":", "return", "==", "!=", "&&", "||")


def _wrappable(skel, start):
    """Whether --fix may put `juce::String::fromUTF8(...)` around the run at
    `start`. Positive recognition only: anything this cannot read as an
    expression is left for a person, because a wrong wrap does not mojibake --
    it stops the build, or worse, silently changes what a call means.

    Walks back over balanced brackets in the SKELETON (literal contents and
    comments blanked), so a bracket inside a string cannot move the count.

      * innermost open bracket is `(`  -- an argument list. Wrappable unless the
        callee is one of _NEVER_WRAP.
      * innermost open bracket is `{` or none -- a statement. Wrappable only
        after one of _VALUE_BEFORE, which is what separates
        `errorMessage = "..."` from a `const char*` table's `, "...",`.
      * a statement that declares a char pointer is never wrappable:
        `const char* x = "..."` reads as a value assignment and is not one."""
    depth = 0
    i = start - 1
    while i >= 0:
        c = skel[i]
        if c in ")]}":
            depth += 1
        elif c in "([{":
            if depth == 0:
                break
            depth -= 1
        i -= 1

    if i >= 0 and skel[i] == "(":
        m = _IDENT_BEFORE_PAREN.search(skel[max(0, i - 96):i + 1])
        return (m.group(1) if m else "") not in _NEVER_WRAP

    # Statement context: from the last statement boundary up to the literal.
    # NOT `:` -- that is a ternary's second arm as often as it is a label, and
    # cutting there loses `return a ? "x" : "y"`'s whole statement.
    head = skel[:start]
    cut = max(head.rfind(";"), head.rfind("{"), head.rfind("}"))
    stmt = head[cut + 1:] if cut >= 0 else head
    if re.search(r"\bchar\b", stmt):
        return False
    stripped = stmt.rstrip()
    return any(stripped.endswith(op) for op in _VALUE_BEFORE)


def _escape_high(run):
    r"""The same string with every character above ASCII written as its UTF-8
    bytes. Byte-for-byte identical to the compiler, and no longer dependent on
    how the file was saved or on MSVC's source-charset default.

    The `" "` it inserts is not cosmetic: C++'s \x escape consumes EVERY hex
    digit that follows it, so `"\xe2\x80\x94abc"` is one huge character and a
    compile error, not a dash followed by "abc". Ending the literal and starting
    the next one is the standard way to stop it, and adjacent literals are one
    string to the compiler."""
    out = []
    for i, ch in enumerate(run):
        if ord(ch) <= 0x7F:
            out.append(ch)
            continue
        out.append("".join(f"\\x{b:02x}" for b in ch.encode("utf-8")))
        if i + 1 < len(run) and run[i + 1] in _HEXDIGIT:
            out.append('" "')
    return "".join(out)


def scan_file(path):
    with open(path, "rb") as fh:
        raw = fh.read()
    src = raw.decode("utf-8", errors="surrogateescape")
    code, skel, literals, comments = _scan(src)
    lines = src.splitlines()
    excused = _opt_out_lines(src, comments)
    problems = []

    for start, end, kinds in _runs(code, literals):
        # From the BLANKED code, never from `src`: a run can have a comment
        # standing between two of its literals, and reading the raw source here
        # pulls that comment's own text into the literal. SetupWizard.cpp:466 is
        # a comment that SHOWS what the mojibake looks like, in a table whose
        # literals sit on either side of it -- it reported itself.
        run = code[start:end]
        line_no = src.count("\n", 0, start) + 1
        end_line = src.count("\n", 0, end) + 1
        line = lines[line_no - 1] if line_no <= len(lines) else ""
        # Anywhere in the run, not only on its first line: a literal run can be a
        # twenty-line paragraph, and the marker belongs beside the character it
        # excuses rather than at the top of the paragraph containing it. The
        # cost of that reach: a marker excuses EVERY non-ASCII literal on the
        # lines its run spans, so keep one statement per marked line.
        if excused & set(range(line_no, end_line + 1)):
            continue

        decoded = bool(_DECODER_CALL.search(code[:start]))
        # A raw string cannot carry an escape, so it can never be healed either.
        wrappable = "raw" not in kinds and _wrappable(skel, start)

        rawhigh = sorted({ch for ch in run if ord(ch) > 0x7F})
        if rawhigh:
            shown = " ".join(f"{ch!r} (U+{ord(ch):04X})" for ch in rawhigh)
            problems.append({"line": line_no, "text": line.strip(),
                             "why": "raw non-ASCII in a string literal: " + shown,
                             "start": start, "end": end,
                             "escape": "raw" not in kinds, "decoded": decoded,
                             "wrap": wrappable and not decoded})
            continue

        # A raw string has no escapes to read: `R"(\xe2)"` is a backslash, an x
        # and a 2. Only the raw-byte rule above applies to one.
        if kinds == {"raw"}:
            continue

        esc = _high_escapes(run)
        if esc and not decoded:
            problems.append({"line": line_no, "text": line.strip(),
                             "why": "high escape (%s) in a literal that states no "
                                    "encoding" % ", ".join(sorted(set(esc))[:4]),
                             "start": start, "end": end,
                             "escape": False, "decoded": decoded,
                             "wrap": wrappable})
    return problems


def heal_file(path):
    """Rewrite `path` so its literals say what they mean. Returns (healed, left).

    Two edits, both mechanical, neither a judgement call:
      * a raw byte above ASCII becomes its UTF-8 escape -- byte-identical to the
        compiler, and no longer dependent on how the file was saved;
      * a literal that carries such bytes and is not already decoded is wrapped
        in juce::String::fromUTF8(...), which is the whole content of the rule.

    What it leaves alone is exactly what it cannot know: a literal handed to
    static_assert, printf or DBG is not display text, and a raw string cannot
    carry an escape at all. Those are reported for a person to answer with a
    wrap or with `// charset-ok: <reason>`."""
    with open(path, "rb") as fh:
        src = fh.read().decode("utf-8", errors="surrogateescape")
    problems = scan_file(path)
    healed, left = 0, []
    # Back to front, so an earlier edit cannot move a later one's offsets.
    for p in sorted(problems, key=lambda q: q["start"], reverse=True):
        # Only edits that make the literal CORRECT, never ones that merely make
        # it tidier: escaping a static_assert message leaves it just as wrong and
        # would report itself again next run under a different rule, which reads
        # as the healer having failed rather than having declined.
        if not (p["wrap"] or (p["decoded"] and p["escape"])):
            left.append(p)
            continue
        run = src[p["start"]:p["end"]]
        new = _escape_high(run) if p["escape"] else run
        if p["wrap"]:
            new = "juce::String::fromUTF8(" + new + ")"
        src = src[:p["start"]] + new + src[p["end"]:]
        healed += 1
    if healed:
        with open(path, "wb") as fh:
            fh.write(src.encode("utf-8", errors="surrogateescape"))
    return healed, left


def _sources(roots):
    """Every source file under `roots`. A root may be a FILE: `os.walk` yields
    nothing for one, so passing a single file used to print `clean` whatever was
    in it -- a false all-clear at exactly the moment someone is checking one
    file by hand."""
    skip = {"JuceLibraryCode", "build", "build_clean", "JUCE", ".git"}
    for root in roots:
        if os.path.isfile(root):
            yield root
            continue
        if not os.path.isdir(root):
            raise SystemExit(f"source charset gate: no such path: {root}")
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in skip]
            for name in sorted(filenames):
                if name.endswith(_EXTS):
                    yield os.path.join(dirpath, name)


def main(argv):
    args = argv[1:]
    fix = "--fix" in args
    roots = [a for a in args if not a.startswith("-")] or ["src"]

    if fix:
        healed, left = 0, []
        for path in _sources(roots):
            n, rest = heal_file(path)
            healed += n
            for p in rest:
                left.append((path, p))
            if n:
                print(f"{path}: healed {n} literal(s)")
        print()
        print(f"source charset gate --fix: {healed} literal(s) rewritten.")
        if left:
            print()
            print(f"{len(left)} left for a person -- these are not display text, "
                  "or cannot carry an escape:")
            for path, p in left:
                print(f"  {path}:{p['line']}: {p['why']}")
                print(f"      {p['text']}")
            print()
            print("Wrap it in juce::String::fromUTF8(...) if it becomes UI text, "
                  "or write the")
            print("character in ASCII / end the line with `// charset-ok: <reason>` "
                  "if it does not.")
            return 1
        print("Re-run without --fix to confirm, and read the diff before committing.")
        return 0

    total = 0
    for path in _sources(roots):
        for p in scan_file(path):
            total += 1
            print(f"{path}:{p['line']}: {p['why']}")
            print(f"    {p['text']}")

    if total:
        print()
        print(f"source charset gate: {total} violation(s).")
        print("Heal them:  python3 tools/source_charset_gate.py --fix "
              + " ".join(roots))
        print()
        print("A juce::String built from a raw const char* reads its bytes as "
              "Latin-1, so these")
        print("reach the user as mojibake in a Release build, silently. Write "
              "the character as")
        print('UTF-8 escapes inside juce::String::fromUTF8("...") -- or, where '
              "the bytes are")
        print("deliberately not text, end the line with `// charset-ok: <reason>`.")
        return 1

    print("source charset gate: clean.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
