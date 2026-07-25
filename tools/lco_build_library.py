#!/usr/bin/env python3
"""Assemble the LCO's curated library from the lexicon.

The library is what BJ ordered (docs/LCO_CONCEPT.md §1, dictated 2026-07-19):

    "Wir bauen eine Klangwelt-Code-Bibliothek mit zig Instrumenten. Wir
     informieren ueber Parameter jeweils dieses Csound-Codes innerhalb seines
     Spektrums ... und Parametrisierungshinweise wie 'square ist sharp wenn
     Wert x = y, ist hollow wenn x = y'."

So an entry carries REAL Csound code, its parameters with their measured ranges,
and named anchors with a perceptual gloss. All of it lives in
`backend/dco_lexicon.json`; this script only shapes it into the form
`backend/lco_write.py` renders into the author's prompt.

The 29 inherited instruments, the 51 adjectives and the motions did NOT start
life hand-written -- their Csound was machine-harvested by running the parked
implementation's own emitters (tag `parked/keys-path-csound-20260721`), whose
constants are measured and ear-approved, so the library inherited every idiom
the old path could actually produce rather than a smaller vocabulary wearing its
name. That harvest ran once and its result is now IN the lexicon verbatim. It is
not repeated: rebuilding a library by reviving a file that was deleted from the
tree is no way to maintain a curated one, and a new instrument has nowhere to
live in an emitter that never had it. Curating an entry now means editing the
lexicon -- which is already where an instrument's `why`, parameters, anchors and
glosses live. The harvest itself is in the git history if it is ever needed
again.

Usage:
    .venv/bin/python tools/lco_build_library.py [--check]

--check regenerates in memory and diffs against the committed JSON (exit 1 on
drift), so the library cannot silently fall out of step with the lexicon.
"""
import argparse
import json
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
LEXICON = REPO / "backend" / "dco_lexicon.json"
OUT = REPO / "backend" / "lco_library.json"


def assemble(lex):
    instruments = []
    for entry in lex["techniques"]:
        item = {"key": entry["key"], "why": entry.get("why", ""),
                "code": entry.get("code", "")}

        params = entry.get("params")
        if params:
            item["params"] = {}
            for pname, pspec in params.items():
                item["params"][pname] = {
                    "range": pspec.get("range"),
                    "default": pspec.get("default"),
                    "note": pspec.get("note", ""),
                    "anchors": {a: {"value": v["value"], "gloss": v.get("gloss", "")}
                                for a, v in (pspec.get("anchors") or {}).items()},
                }
            # The parameter->sound mapping made concrete: the SAME instrument at
            # each anchor of its character axis, so the model can see which
            # numbers move with the word. First param = character axis. Only kept
            # when it actually MOVES across the anchors -- a single distinct body
            # means the parameter never reached the code, which is a bug in the
            # entry, not a one-anchor instrument.
            variants = entry.get("anchor_code")
            if variants and len(set(variants.values())) > 1:
                item["anchor_code"] = variants
        instruments.append(item)

    adjectives = [{"key": e["key"], "why": e.get("why", ""), "code": e.get("code", "")}
                  for e in lex["adjectives"]]

    motions = [{"key": e["key"], "why": e.get("why", ""),
                "kind": e.get("kind"),
                # WHICH quantity the word moves -- colour, pitch or loudness. The
                # platform lets the colour travel over a note and not the
                # loudness, so the author has to be able to see which of the two a
                # word asks for instead of inferring it from the code.
                "moves": e.get("moves"),
                # A word can need a different idiom on a different substrate.
                # `shimmer` post-processed re-weights partials that already exist;
                # on a mellow FM tone there are none above the highpass, so the
                # same word has to reach for the index instead. Measured: 16 Hz of
                # centroid travel the first way, 199 Hz the second.
                "alt": e.get("alt"),
                "rate_hz": e.get("motion_rate_hz"),
                "code": e.get("code", "")}
               for e in lex["motions"]]

    return {
        "generated_from": {"lexicon_version": lex.get("lexicon_version")},
        "instruments": instruments,
        "adjectives": adjectives,
        "motions": motions,
    }


def narrowing_forms(lex):
    """Instrument surface forms that are an adjective's or a motion's own key.

    Not a style complaint. `lco_write.open_entries` opens the WHOLE library only
    when the author's reply named no instrument, so a word that describes a
    QUALITY but is also registered as an instrument's form silently cuts the
    author's view from every instrument to that one. Found the hard way: the
    recovered `hiss` carried the form `static`, which is the motion `static`'s own
    key, so "a static, dirty analog pad" opened 2 instruments instead of 45 —
    the word for "does not move" selecting a white-noise bed. `glass` carried
    `glassy` the same way.

    Every SURFACE FORM is checked, not only the keys. The first version of this
    guard compared keys alone, on the reasoning that "a shared non-key form merely
    opens both entries, which costs nothing" — and that reasoning is false, which is
    the whole point of `open_entries`: it opens everything only when NO instrument
    was named, so any instrument hit at all collapses 57 entries to 1. Checking keys
    only left the guard evadable by every one of the ~500 non-key forms. Measured
    against the router itself, all of these passed the old guard and all of them
    opened exactly 1 entry of 57: `hiss` as "steady" or "stationary" (both forms of
    the motion `static` — the very motion whose KEY produced the original bug),
    `noise` as "gritty" (a form of `dirty`), `wind` as "moving" (a form of `evolve`),
    `triangle` as "soft".

    A MULTI-WORD form narrows the same way when it is nothing but a quality: `flute`
    claimed "breathy tone", `breathy` is an adjective's own key and "tone" names no
    instrument, so "a steady breathy tone" opened 1 entry of 50 — the first version of
    this guard compared whole forms only and passed it. The test is what survives
    removing the quality words: "hollow reed" leaves "reed", which is a family, and
    "bright voice" leaves "voice", which is another entry's key, so both are fair.
    "breathy tone" and "breathy whisper" leave a word for sound-in-general and are not.

    That test had THREE structural holes, all found against the router and all worth
    naming because a closed filler list would never have closed either. A form of
    NOTHING but quality words leaves the remainder EMPTY — the strongest possible
    answer — and an `and rest` guard short-circuited it to silence, so `flute` claiming
    "soft breathy" and `wind` claiming "slowly evolving" each opened 1 entry of 57 in
    quiet. The filler list held `tone` but not `tones`, so "breathy tones", "steady
    textures", "soft layers" and "gritty sounds" all walked through the exact rule that
    caught their singulars. And the scan itself was word by word while the quality index
    is keyed by PHRASE, so a MULTI-WORD quality plus a filler was invisible: "washed out"
    fired and "washed out tone" was silent, opening the same 1 entry of 57, and all
    thirteen multi-word qualities in the lexicon behaved that way. An empty remainder now
    reads as the empty phrase it is, the inflection is stripped before the list is
    consulted (including -y -> -ies, or `quality` and `sonority` are listed while
    "qualities" and "sonorities" are not), the quality phrases are matched longest-first,
    and adverbs are a set of their own: an adverb modifies the quality beside it and can
    never head the phrase, which is a grammatical fact rather than one more word to
    remember. Articles are theirs too — "bit" was in the adverb set, where it made the
    guard fire on `chiptune` claiming "gritty bit", a form an 8-bit entry has earned.

    DIRECTION decides the near misses, and it is structural rather than semantic. An
    instrument reaching for a quality word is the hazard; a quality listing the
    instrument's own word is not. The adjective `glassy` carries the form `glass`,
    which is the instrument `glass`'s own key — and an author writing the bare "glass"
    means the instrument, so narrowing is the right answer. Verified on the router: "a
    glassy pad" and "glassy shimmering texture" both open 57 of 57, because it matches
    whole words. Hence a quality's KEY is compared against the instrument's key as
    well, a quality's non-key FORM only against the instrument's non-key forms, and a
    multi-word form containing the instrument's own key is left alone ("glass pad").

    Underscores AND hyphens are normalised to spaces as well as case, so `washed_out`,
    `washed-out` and `washed out` cannot pass by spelling — the hyphen was a live hole.

    `--selftest` asserts all of this, in both directions, on forty-four cases.
    """
    def norm(s):
        return " ".join(str(s).lower().replace("_", " ").replace("-", " ").split())

    # Every form the router can match, keyed to the entry it belongs to. `washed_out`
    # and `washed out` normalise together, so neither spelling nor case is a hiding
    # place; a form listed under an adjective AND an instrument is exactly the case
    # this looks for, so the mapping is built from the quality side only.
    keys = {norm(f): (sec, e["key"])
            for sec in ("adjectives", "motions") for e in lex[sec]
            for f in [e["key"]] + list(e.get("surface_forms") or [])}
    # A quality's own KEY is checked against the instrument's key too; a quality's
    # non-key FORM only against the instrument's non-key forms. The direction is the
    # whole distinction. `hiss` claiming the form `static` is an instrument reaching
    # for a quality word, and the author writing "static" then means "does not move".
    # The adjective `glassy` listing the form `glass` is the reverse — the instrument
    # `glass` owns that word as its key, and an author writing the bare "glass" means
    # the instrument. Verified on the router: "a glassy pad" and "glassy shimmering
    # texture" both open 57 of 57, because it matches whole words, so the collision
    # costs nothing until someone writes the bare key, where narrowing is correct.
    qkeys = {norm(e["key"]): (sec, e["key"])
             for sec in ("adjectives", "motions") for e in lex[sec]}
    # Words for sound-in-general. A closed list against an open problem, and it stays
    # one deliberately: the alternative — asking whether the remainder names an
    # instrument anywhere in the vocabulary — was tried and is worse in both
    # directions. It fired on five legitimate shipped forms ("radio static", "glass
    # pad", "duty sweep", "tearing sweep", "analoger oszillator"), whose second word
    # names the instrument perfectly well in context and merely happens to be unique to
    # it, and it stopped catching "breathy tone", because some other entry's form
    # contains "tone" and that was enough to vouch for it. "whisper" is here because
    # `flute` claiming "breathy whisper" evaded the twelve-word version for no reason
    # but that nobody had thought of the word. When the next one turns up, add it.
    generic = {"tone", "sound", "timbre", "texture", "note", "wave", "waveform",
               "sonority", "character", "quality", "colour", "color", "whisper",
               "murmur", "hush", "sonic", "audio", "signal", "patch", "preset",
               "layer"}
    # Degree and manner adverbs. A separate set from the nouns above because the
    # reason they are here is grammatical rather than lexical: an adverb modifies the
    # quality word beside it and can never be the head of the phrase, so "slowly
    # evolving" names a quality exactly as "evolving" does. `wind` claiming that form
    # evaded a noun-only list, and would have gone on evading it however many nouns
    # were added. Any of these that is ALSO a quality in the lexicon is matched as one
    # before it ever reaches here.
    adverbs = {"slowly", "quickly", "gently", "softly", "barely", "slightly",
               "faintly", "heavily", "lightly", "deeply", "richly", "subtly",
               "steadily", "smoothly", "roughly", "very", "quite", "rather",
               "somewhat", "mostly", "nearly", "almost", "just"}
    # Articles, which are not adverbs and were in that set by mistake — along with
    # "bit", which is a NOUN and made the guard fire on `chiptune` claiming "gritty
    # bit", a form an 8-bit entry has every right to. A word that is neither a quality
    # nor a word for sound-in-general must not be treated as absent.
    articles = {"a", "an", "the"}

    def filler(w):
        """A word for sound-in-general, an adverb or an article — singular or plural.

        The plural is not a nicety: with `tone` listed and `tones` not, "breathy tone"
        was caught and "breathy tones" walked straight through, and the same held for
        every other noun in the list. Stripping the inflection is one rule instead of
        twenty more entries that would each have to be thought of — and it has to
        cover -y -> -ies, or `quality` and `sonority` are on the list while "qualities"
        and "sonorities" are not, which is two of the list's own twenty-one words.
        """
        return (w in generic or w in adverbs or w in articles
                or (w.endswith("s") and w[:-1] in generic)
                or (w.endswith("es") and w[:-2] in generic)
                or (w.endswith("ies") and w[:-3] + "y" in generic))

    def qualities_in(words):
        """The quality phrases in `words`, longest first, and the words left over.

        Word by word is not enough and was the third structural hole in this branch:
        `keys` is keyed by PHRASE, so a multi-word quality plus one filler evaded the
        test completely. `flute` claiming "washed out" fired, and "washed out tone"
        and "washed out texture" were silent while opening the same 1 entry of 57 —
        because neither "washed" nor "out" is a key on its own. All thirteen multi-word
        qualities in the lexicon behaved that way (`washed out`, `fades away`,
        `builds up`, `open up`, `opens up`, `close down`, `back and forth`, `ping
        pong`, `entwickelt sich`, `beruhigt sich`, `schließt sich`, `öffnet sich`,
        `hin und her`) against any filler noun.
        """
        found, rest, i = [], [], 0
        longest = max((len(k.split()) for k in keys), default=1)
        while i < len(words):
            for n in range(min(longest, len(words) - i), 0, -1):
                if " ".join(words[i:i + n]) in keys:
                    found.append(" ".join(words[i:i + n]))
                    i += n
                    break
            else:
                rest.append(words[i])
                i += 1
        return found, rest

    bad = []
    for e in lex["techniques"]:
        for i, f in enumerate([e["key"]] + list(e.get("surface_forms") or [])):
            n = norm(f)
            hit = qkeys.get(n) if (i == 0 or n == norm(e["key"])) else keys.get(n)
            if hit:
                bad.append((e["key"], f, hit[0], hit[1], None))
                continue
            words = n.split()
            if len(words) < 2:
                continue
            quality, rest = qualities_in(words)
            # The same direction rule as above: if the instrument's OWN key is one of
            # the words, the phrase belongs to the instrument whatever else is in it.
            # `glass`'s form "glass pad" is the instrument's key plus a word for
            # sound-in-general, and "a glass pad" means the instrument.
            if norm(e["key"]) in words:
                continue
            # No `rest` requirement, and that used to be the biggest hole in this
            # branch: a form made of NOTHING but quality words leaves the remainder
            # empty, which is the strongest possible answer to "what is left when the
            # qualities are removed" — and `and rest` short-circuited it to silence.
            # `flute` claiming "soft breathy" opened 1 entry of 57 with the guard
            # saying nothing. An empty remainder now reads as the empty phrase it is.
            if quality and all(filler(w) for w in rest):
                bad.append((e["key"], f, keys[quality[0]][0], keys[quality[0]][1],
                            " ".join(rest) or "nothing at all"))
    return bad


# Both directions, because this guard has now been wrong in both. Each case adds one
# form to the real lexicon and asserts whether `narrowing_forms` fires. The `True` rows
# are evasions that were live at some point: the eight that only KEY-matching let
# through, the hyphen, and the multi-word remainder no closed list had thought of. The
# `False` rows are the instrument's own vocabulary, which must stay untouched — a guard
# that fires on "wooden flute" or "thumb piano" gets switched off, and then it guards
# nothing.
_NARROWING_CASES = [
    ("hiss", "static", True), ("hiss", "steady", True), ("hiss", "stationary", True),
    ("noise", "gritty", True), ("wind", "moving", True), ("triangle", "soft", True),
    ("hiss", "washed-out", True), ("hiss", "washed_out", True),
    ("glass", "glassy", True), ("flute", "breathy tone", True),
    ("flute", "breathy whisper", True), ("flute", "soft texture", True),
    # Nothing but quality words, so the remainder is empty. `and rest` used to
    # short-circuit exactly this — the strongest case — into silence.
    ("flute", "soft breathy", True), ("hiss", "steady stationary", True),
    # An adverb in front of a quality. A noun-only filler list could never catch it.
    ("wind", "slowly evolving", True), ("triangle", "barely moving", True),
    # The plural. Each of these was caught in the singular and walked through in the
    # plural, which is four of the six evasions found in one inflection.
    ("flute", "breathy tones", True), ("hiss", "steady textures", True),
    ("triangle", "soft layers", True), ("noise", "gritty sounds", True),
    ("flute", "flute", False), ("flute", "wooden flute", False),
    ("flute", "hollow reed", False), ("flute", "bright voice", False),
    ("glass", "glass", False), ("glass", "glass pad", False),
    ("mbira", "thumb piano", False), ("hiss", "radio static", False),
    # …and the new rules must not reach past sound-in-general. A plural that names a
    # thing is still a thing, and an adverb beside a real noun still leaves the noun.
    ("mbira", "thumb pianos", False), ("flute", "breathy pipes", False),
    ("flute", "softly blown reed", False), ("glass", "glass layers", False),
    # A MULTI-WORD quality plus a filler. The word-by-word scan could not see any of
    # these, because `washed` and `out` are not keys on their own — only "washed out"
    # is. All thirteen multi-word qualities in the lexicon evaded it the same way.
    ("flute", "washed out", True), ("flute", "washed out tone", True),
    ("flute", "washed out texture", True), ("wind", "fades away slowly", True),
    ("hiss", "back and forth", True), ("wind", "builds up", True),
    # The -y -> -ies plural, two of the filler list's own words.
    ("flute", "breathy qualities", True), ("hiss", "steady sonorities", True),
    # And "bit" is a noun, not an adverb: an 8-bit entry may claim this.
    ("chiptune", "gritty bit", False), ("chiptune", "gritty bits", False),
    # A multi-word quality is still only a quality when what remains names something.
    ("flute", "washed out reed", False), ("glass", "washed out glass", False),
]


def selftest(lex):
    fails = []
    for inst, form, should in _NARROWING_CASES:
        probe = json.loads(json.dumps(lex))
        e = [x for x in probe["techniques"] if x["key"] == inst]
        if not e:
            fails.append(f"{inst!r} is not in the lexicon — the case cannot run")
            continue
        e = e[0]
        if form != e["key"] and form not in (e.get("surface_forms") or []):
            e.setdefault("surface_forms", []).append(form)
        fires = any(k == inst and f == form for k, f, *_ in narrowing_forms(probe))
        if fires != should:
            fails.append(f"{inst}/{form!r}: expected {should}, guard said {fires}")
        print(f"  {'PASS' if fires == should else 'FAIL'}  {inst} claiming {form!r} "
              f"{'narrows' if should else 'is its own vocabulary'}")
    print(f"\n{len(_NARROWING_CASES) - len(fails)}/{len(_NARROWING_CASES)} cases pass")
    for f in fails:
        print("  " + f, file=sys.stderr)
    return 1 if fails else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="regenerate and diff against the committed library")
    ap.add_argument("--selftest", action="store_true",
                    help="assert the narrowing guard on known evasions, both directions")
    args = ap.parse_args()

    lex = json.loads(LEXICON.read_text())
    if args.selftest:
        return selftest(lex)
    bad = narrowing_forms(lex)
    if bad:
        for inst, form, sec, key, rest in bad:
            if rest is None:
                print(f"instrument {inst!r} claims the form {form!r}, which is the "
                      f"{sec[:-1]} {key!r}'s own key — a prompt using that word would "
                      f"open only {inst!r} instead of the whole library",
                      file=sys.stderr)
            else:
                print(f"instrument {inst!r} claims the form {form!r}, which is nothing "
                      f"but a quality: the {sec[:-1]} {key!r}'s own key plus "
                      f"{rest!r}, which names no instrument. A prompt using that "
                      f"phrase would open only {inst!r} instead of the whole library",
                      file=sys.stderr)
        return 1
    lib = assemble(lex)
    text = json.dumps(lib, indent=1, ensure_ascii=False) + "\n"

    if args.check:
        if not OUT.exists():
            print(f"{OUT} missing", file=sys.stderr)
            return 1
        if OUT.read_text() != text:
            print(f"{OUT} is out of step with {LEXICON.name}.\n"
                  f"Regenerate: .venv/bin/python tools/lco_build_library.py",
                  file=sys.stderr)
            return 1
        print("library is in step")
        return 0

    OUT.write_text(text)
    # "Has code" means it has a line that is not a comment. Testing the FIRST character
    # instead read four real bodies as placeholders the moment they grew a leading
    # comment: `rain` has 8 code lines and an `asig`, and was reported as having none.
    def has_code(c):
        return any(l.strip() and not l.lstrip().startswith(";") for l in (c or "").splitlines())

    n_code = sum(1 for i in lib["instruments"] if has_code(i["code"]))
    print(f"{OUT.relative_to(REPO)}: {n_code}/{len(lib['instruments'])} instruments with "
          f"code, {len(lib['adjectives'])} adjectives, {len(lib['motions'])} motions")
    empty = [i["key"] for i in lib["instruments"] if not has_code(i["code"])]
    if empty:
        print("WITHOUT code: " + ", ".join(empty))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
