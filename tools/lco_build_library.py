#!/usr/bin/env python3
"""Generate the LCO's curated library from the lexicon + the parked emitters.

The library is what BJ ordered (docs/LCO_CONCEPT.md §1, dictated 2026-07-19):

    "Wir bauen eine Klangwelt-Code-Bibliothek mit zig Instrumenten. Wir
     informieren ueber Parameter jeweils dieses Csound-Codes innerhalb seines
     Spektrums ... und Parametrisierungshinweise wie 'square ist sharp wenn
     Wert x = y, ist hollow wenn x = y'."

So an entry carries REAL Csound code, its parameters with their measured
ranges, and named anchors with a perceptual gloss. None of that is invented
here: the code is MACHINE-HARVESTED by running the parked implementation's own
emitters (tag `parked/keys-path-csound-20260721`), whose constants are measured
and ear-approved, and the parameters and anchors come from
`backend/dco_lexicon.json` verbatim. This script writes
`backend/lco_library.json`, which `backend/lco_write.py` renders into the
author's prompt.

Harvesting rather than hand-writing is the point. A hand-written library is a
new, smaller vocabulary wearing the old one's name -- the exact destruction
CLAUDE.md's migration discipline forbids. Running the old emitters means the
library inherits every idiom the old path could actually produce.

Usage:
    .venv/bin/python tools/lco_build_library.py [--check]

--check regenerates in memory and diffs against the committed JSON (exit 1 on
drift), so the library cannot silently fall out of step with the lexicon.
"""
import argparse
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
LEXICON = REPO / "backend" / "dco_lexicon.json"
OUT = REPO / "backend" / "lco_library.json"
PARKED_TAG = "parked/keys-path-csound-20260721"

# Lines the emitters add around every oscillator: the per-layer register, the
# mix law and the final assignment. They belong to the host scaffold, not to the
# instrument, and the author writes his own.
_SCAFFOLD = re.compile(
    r"^\s*(kfr\d+\s+limit|kvsum\s*=|kmix\s*=|asig\s*=\s*kvol|"
    r"aout\b|outch\b)", re.IGNORECASE)

_BODY = re.compile(r"knote\s+= knote \+ 1/kr\n(.*?)\n  aout", re.DOTALL)


def load_parked_emitter():
    """Import the parked csound_orch.py from the tag, without touching the tree."""
    src = subprocess.run(["git", "show", f"{PARKED_TAG}:backend/csound_orch.py"],
                         cwd=REPO, capture_output=True, text=True, check=True).stdout
    tmp = Path(tempfile.mkdtemp(prefix="lco_parked_"))
    (tmp / "parked_csound_orch.py").write_text(src)
    sys.path.insert(0, str(tmp))
    sys.path.insert(0, str(REPO / "backend"))
    import parked_csound_orch  # noqa: E402
    return parked_csound_orch


# The emitters name each layer's live pitch `kfr1`/`kfr2`/`kfr3`, defined by the
# scaffold line this script strips. Rewrite them to the expression the author is
# told to use, so every harvested example is self-contained and speaks the same
# variable names as the prompt's contract.
_LAYER_FREQ = re.compile(r"\bkfr([123])\b")

# The multi-osc emitter names each oscillator's OUTPUT `aosc<tag>` (tag 0 for the
# single-osc harvest), because the mix scaffold — which this script strips —
# reads `asig = kvol*aosc0`. A harvested EXAMPLE therefore ended in `aosc0`, not
# `asig`, contradicting the one rule the author must follow ("write your final
# audio into `asig`"). The author copied an idiom ending in `aosc0`, then wrote
# `... = asig * ...` on the next line — `asig` used before defined — and failed
# to compile. It bit compound prompts hardest, where a second layer is stacked
# after the copied idiom (observed: "a bright shimmering fm bell with a long
# metallic ring", three straight failures). `aosc0` occurs exactly once per body,
# as the final output, so renaming it to `asig` makes every example model the
# exact contract the author is held to. Only the output is touched: the internal
# tagged names (abl0, kbtp0) keep their tag — stripping it would collide names
# like `abs0` onto the built-in `abs`.
_OSC_OUT = re.compile(r"\baosc0\b")


def body_of(orchestra):
    """The oscillator lines only — scaffold and mix law removed, output renamed
    to `asig` so the example ends exactly as the author is told to end."""
    m = _BODY.search(orchestra)
    if not m:
        return ""
    kept = [ln.rstrip() for ln in m.group(1).splitlines()
            if ln.strip() and not _SCAFFOLD.match(ln)]
    # de-indent by the common leading whitespace
    if kept:
        pad = min(len(l) - len(l.lstrip()) for l in kept)
        kept = [l[pad:] for l in kept]
    body = _LAYER_FREQ.sub(r"(kfreq * koct\1)", "\n".join(kept))
    return _OSC_OUT.sub("asig", body)


def emit(P, **kw):
    try:
        orc, _reading = P.build_orchestra(**kw)
        return body_of(orc)
    except Exception as exc:                                  # noqa: BLE001
        return f"; (not harvestable: {exc})"


# What a harvested delta may assume rather than define: the host scaffold's own
# variables and ftables (backend/lco_write.py `wrap`). Everything else a delta
# reads has to be defined inside it.
_HOST_VARS = {
    "kfreq", "koct1", "koct2", "koct3", "kvol1", "kvol2", "kvol3",
    "knote", "kvel", "kpres", "ktimb", "ktrig", "kgate", "kgateraw", "kfreqraw",
    "ivoice", "asig", "aout", "giSine", "giCos", "giCheb", "giImp",
}
# A Csound statement names its result variable(s) first: `kx opcode a, b`,
# `kx = expr`, `ax, ay opcode ...`.
_WRITES = re.compile(r"^\s*([A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)*)\s+")
_IDENT = re.compile(r"\b([a-zA-Z_]\w*)\b")
# Control flow does not define anything, and its first token is not a variable.
# Without this `if changed2(ktrig) == 1 then` would "define" `if`.
_CONTROL = re.compile(r"^\s*(if|elseif|else|endif|until|od|do|"
                      r"[ki]?goto|loop_[lg][et]|reinit|rireturn)\b")


def _writes(line):
    """Result variables, in the order they are written. Order matters: the last
    audio result decides the example's output line, and a set would iterate
    differently between runs, making the generated library non-reproducible."""
    src = line.split(";")[0]
    if _CONTROL.match(src):
        return []
    m = _WRITES.match(src)
    return [t.strip() for t in m.group(1).split(",")] if m else []


def _reads(line):
    """Identifiers a line uses, taken from everything AFTER the result variables.

    Not `all identifiers minus the results`: an accumulator (`ktm0 = ktm0 + 1/kr`)
    both writes and reads the same name, and subtracting the write would hide
    the read and leave `ktm0` undefined in a harvested delta -- the very failure
    this closure exists to prevent. Opcode names come along harmlessly: a name
    is only acted on when some OTHER line is found to define it, and no line
    defines `vco2`."""
    src = line.split(";")[0]
    if _CONTROL.match(src):
        return set(_IDENT.findall(src))
    m = _WRITES.match(src)
    return set(_IDENT.findall(src[m.end():] if m else src))


# The lexicon carries the PUBLIC parameter names -- the words the author reads.
# The parked emitter knows only its own internal ones. When a parameter is renamed
# for clarity (2026-07-23: bite->index, fade->ring, shimmer->detune, reed->
# hollowness, spot->strikepos), the harvest must translate back, or the emitter
# drops the unknown name and every anchor harvests the DEFAULT -- identical across
# the row, which is precisely the silent failure this file already had once.
# The map is PER INSTRUMENT: `ring` is fm_ep's own emitter name, but the FM
# family's `ring` is the emitter's `fade`.
_EMITTER_PARAM = {
    "fm":          {"index": "bite", "ring": "fade", "detune": "shimmer"},
    "fm_bell":     {"index": "bite", "ring": "fade", "detune": "shimmer"},
    "metallic_fm": {"index": "bite", "ring": "fade", "detune": "shimmer"},
    "fm_ep":       {"hollowness": "reed"},
    "drum_head":   {"strikepos": "spot"},
}


def harvest(P, lex):
    instruments = []
    for entry in lex["techniques"]:
        key = entry["key"]
        item = {"key": key, "why": entry.get("why", ""),
                "code": emit(P, technique_keys=[key])}

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
            # The parameter->sound mapping made concrete: the SAME instrument
            # emitted at each anchor of its character axis, so the model can see
            # which numbers move with the word. First param = character axis.
            first = next(iter(params))
            emit_name = _EMITTER_PARAM.get(key, {}).get(first, first)
            variants = {}
            for aname, aspec in (params[first].get("anchors") or {}).items():
                # The emitter reads params as {technique_key: {name: value}}
                # (_normalize_oscs -> _emit_oscillator). Keying by the bare param
                # name lands nowhere: the value is silently dropped and every
                # anchor harvests the DEFAULT emission, identical across the row.
                code = emit(P, oscs=[{"chain": [key], "vol": 1.0,
                                      "params": {key: {emit_name: aspec["value"]}}}])
                if code and not code.startswith(";"):
                    variants[f"{first}={aspec['value']}  ({aname}: {aspec.get('gloss','')})"] = code
            # Only keep the anchor_code if it actually MOVES across anchors -- a
            # single distinct body means the parameter never reached the emitter,
            # which is a harvest bug, not a one-anchor instrument.
            if len(set(variants.values())) > 1:
                item["anchor_code"] = variants
        instruments.append(item)

    # Adjectives and motions: what the WORD does, in code. Harvested as the
    # difference a word makes to the same plain carrier, so each entry shows the
    # lines that word is responsible for and nothing else.
    #
    # "and nothing else" has to stop short of the lines that DEFINE what the
    # difference uses. A pure line-set difference dropped them -- they are in the
    # carrier too -- and left nine adjectives quoting variables nothing declares
    # (`warm` was a single line reading `adrv0 = tanh(acln0 * 6.7)` with no
    # `acln0` anywhere). The author is shown these as code to imitate, so a
    # broken example teaches the exact failure the repair loop then has to
    # catch: "Variable used before defined". The delta is closed over its own
    # free variables instead: any carrier line that defines one is pulled back
    # in, in the carrier's order, until nothing is left dangling.
    def delta_against(carrier, **kw):
        base_lines = set(emit(P, technique_keys=[carrier]).splitlines())
        lines = emit(P, technique_keys=[carrier], **kw).splitlines()
        keep = {i for i, l in enumerate(lines) if l not in base_lines}
        for _ in range(len(lines)):
            written = {v for i in keep for v in _writes(lines[i])}
            # Backwards: pull in what the kept lines READ but nothing defines.
            need = set().union(*(_reads(lines[i]) for i in keep)) if keep else set()
            need -= written | _HOST_VARS
            # Forwards: pull in what CONSUMES the kept lines. A word's difference
            # is often only in the constants of a control signal, so the line that
            # applies it is byte-identical in both versions and drops out of a
            # pure difference -- which left `old` as three modulators wired to
            # nothing, rendering bit-identical to the bare carrier. A delta that
            # never reaches the audio is not an example of anything.
            grew = False
            for i, l in enumerate(lines):
                if i in keep:
                    continue
                if _writes(l) and set(_writes(l)) & need:
                    keep.add(i); grew = True
                elif _reads(l) & written:
                    keep.add(i); grew = True
            if not grew:
                break
        kept = [lines[i] for i in sorted(keep)]
        # End where the author must end. A delta that builds audio but leaves it
        # in the emitter's own tagged name (`adrv0`, `acln0`) models the contract
        # being broken -- the same trap that made the harvested instruments end
        # in `aosc0` (4cb92441). Deltas that only build control signals have no
        # audio to hand over and correctly end where they end.
        audio = [w for l in kept for w in _writes(l) if w.startswith("a")]
        if audio and audio[-1] != "asig":
            kept.append(f"asig    = {audio[-1]}")
        return "\n".join(kept)

    adjectives = []
    for entry in lex["adjectives"]:
        adjectives.append({"key": entry["key"], "why": entry.get("why", ""),
                           "code": delta_against("saw", adjective_keys=[entry["key"]])})

    # Motions are harvested against a SPARSE carrier (sine). The parked motion
    # layer deliberately emitted nothing on dense or inharmonic sources -- a post-
    # mix operator cannot move partials at different rates, so that motion had to
    # live inside the instrument. Under the write-path the author writes it into
    # the instrument, which is where it belongs; the entries that harvest empty
    # still carry their meaning and their rate, which is what the author needs.
    # Motions are AUTHORED in the lexicon where the parked emitter had nothing to
    # harvest that matched the word. The old harvest emitted ONE identical LFO for
    # five different words (open_up, close, sweep, breathe, evolve) and a second
    # identical one for four more -- so "open_up" and its stated opposite "close"
    # came out byte-for-byte the same. That is not a capability being preserved,
    # it is a distinction that never existed; authoring it ADDS what the emitter
    # could not express. Entries without authored code still harvest.
    motions = []
    for entry in lex["motions"]:
        code = entry.get("code")
        if code is None:
            code = delta_against("sine", motion_key=entry["key"])
        motions.append({"key": entry["key"], "why": entry.get("why", ""),
                        "kind": entry.get("kind"),
                        # WHICH quantity the word moves -- colour, pitch or loudness.
                        # The platform lets the colour travel over a note and not the
                        # loudness, so the author has to be able to see which of the two
                        # a word asks for instead of inferring it from the code.
                        "moves": entry.get("moves"),
                        # A word can need a different idiom on a different substrate.
                        # `shimmer` post-processed re-weights partials that already
                        # exist; on a mellow FM tone there are none above the
                        # highpass, so the same word has to reach for the index
                        # instead. Measured: 16 Hz of centroid travel the first way,
                        # 199 Hz the second.
                        "alt": entry.get("alt"),
                        "rate_hz": entry.get("motion_rate_hz"),
                        "code": code})

    return {
        "generated_from": {"lexicon_version": lex.get("lexicon_version"),
                           "parked_tag": PARKED_TAG},
        "instruments": instruments,
        "adjectives": adjectives,
        "motions": motions,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true",
                    help="regenerate and diff against the committed library")
    args = ap.parse_args()

    lex = json.loads(LEXICON.read_text())
    P = load_parked_emitter()
    lib = harvest(P, lex)
    text = json.dumps(lib, indent=1, ensure_ascii=False) + "\n"

    if args.check:
        if not OUT.exists():
            print(f"{OUT} missing", file=sys.stderr)
            return 1
        if OUT.read_text() != text:
            print(f"{OUT} is out of step with {LEXICON.name} / the parked emitters.\n"
                  f"Regenerate: .venv/bin/python tools/lco_build_library.py",
                  file=sys.stderr)
            return 1
        print("library is in step")
        return 0

    OUT.write_text(text)
    n_code = sum(1 for i in lib["instruments"] if i["code"] and not i["code"].startswith(";"))
    print(f"{OUT.relative_to(REPO)}: {n_code}/{len(lib['instruments'])} instruments with "
          f"harvested code, {len(lib['adjectives'])} adjectives, {len(lib['motions'])} motions")
    empty = [i["key"] for i in lib["instruments"]
             if not i["code"] or i["code"].startswith(";")]
    if empty:
        print("NOT harvestable: " + ", ".join(empty))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
