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

    Only KEYS are checked, because a key is the word the index prints and the
    author writes back. A shared non-key form merely opens both entries, which
    costs nothing.

    A MULTI-WORD form narrows the same way when it is nothing but a quality: `flute`
    claimed "breathy tone", `breathy` is an adjective's own key and "tone" names no
    instrument, so "a steady breathy tone" opened 1 entry of 50 — the first version of
    this guard compared whole forms only and passed it. The test is what survives
    removing the adjective and motion keys: "hollow reed" leaves "reed", which is a
    family, and "bright voice" leaves "voice", which is another entry's key, so both
    are fair. "breathy tone" leaves a word for sound-in-general and is not.

    Underscores are normalised to spaces as well as case, so `washed_out` and
    `washed out` cannot pass by spelling.
    """
    generic = {"tone", "sound", "timbre", "texture", "note", "wave", "waveform",
               "sonority", "character", "quality", "colour", "color"}

    def norm(s):
        return " ".join(str(s).lower().replace("_", " ").split())

    keys = {norm(e["key"]): (sec, e["key"])
            for sec in ("adjectives", "motions") for e in lex[sec]}
    bad = []
    for e in lex["techniques"]:
        for f in [e["key"]] + list(e.get("surface_forms") or []):
            n = norm(f)
            hit = keys.get(n)
            if hit:
                bad.append((e["key"], f, hit[0], hit[1], None))
                continue
            words = n.split()
            if len(words) < 2:
                continue
            quality = [w for w in words if w in keys]
            rest = [w for w in words if w not in keys]
            if quality and rest and all(w in generic for w in rest):
                bad.append((e["key"], f, keys[quality[0]][0], keys[quality[0]][1],
                            " ".join(rest)))
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="regenerate and diff against the committed library")
    args = ap.parse_args()

    lex = json.loads(LEXICON.read_text())
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
    n_code = sum(1 for i in lib["instruments"] if i["code"] and not i["code"].startswith(";"))
    print(f"{OUT.relative_to(REPO)}: {n_code}/{len(lib['instruments'])} instruments with "
          f"code, {len(lib['adjectives'])} adjectives, {len(lib['motions'])} motions")
    empty = [i["key"] for i in lib["instruments"]
             if not i["code"] or i["code"].startswith(";")]
    if empty:
        print("WITHOUT code: " + ", ".join(empty))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
