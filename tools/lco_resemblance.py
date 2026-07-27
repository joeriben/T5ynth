#!/usr/bin/env python3
"""Does a lexicon entry sound like the instrument it is named after?

Every meter this project had before this one measured a body against ITSELF --
loudness spread across an axis, clipping headroom, movement, partial levels.  All
of them are satisfied by a body that is internally consistent and wrong.  Measured
on 2026-07-25: `vibraphone` passed every one of them and was a decaying sine,
`rain` passed every one and sat 1.6 dB from plain noise.  A green suite over the
wrong question reads as proof, which makes it worse than no suite at all.

So ask the outside question.  Two independent audio-text models (CLAP) hear every
entry and every entry's own NAME, and the entry is ranked against the whole library
for that name.  The number that decides is not the raw similarity -- CLAP's absolute
scores mean little -- but the RANK: if the body that claims "vibraphone" is not the
one CLAP picks out of 69 candidates when asked for a vibraphone, the claim is not
carried by the sound.

The meter is itself an instrument and is calibrated before it is read:

  * CLAP's own sine-vs-noise gate (`pipe_inference._clap_assert_sane`) runs at load.
  * A control set of entries whose identity is not in doubt must rank first for
    their own names.  If the controls fail, the run REFUSES to report the rest --
    a ranking from a meter that cannot hear a cymbal is not evidence about a
    waterphone.
  * Two checkpoints, agreeing or not.  A disagreement is reported as a
    disagreement, never averaged into one confident number.

  lco_resemblance.py [--device mps|cpu] [--out DIR] [--keys k1,k2,...]
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
sys.path.insert(0, str(REPO / "backend"))

import lco_measure as M          # noqa: E402
import pipe_inference as pi      # noqa: E402

LEX = REPO / "backend" / "dco_lexicon.json"
CLAP_SR = 48_000
RENDER_FREQS = (110.0, 220.0, 440.0)
RENDER_DUR = 5.0
RENDER_PREROLL = 1.0

# Two checkpoints.  The first is the one the backend ships and the one the repo's
# sanity gate was written against; the second is trained on more music and speech.
# Neither is authoritative alone.  `laion/larger_clap_music` is deliberately NOT
# here: the copy in this machine's cache fails the sanity gate outright (text-text
# 1.000, every similarity 0.006 -- a collapsed text tower), and a degenerate model
# would have voted on every ranking below without ever disagreeing with itself.
CHECKPOINTS = ("laion/clap-htsat-unfused", "laion/larger_clap_music_and_speech")

# Entries whose identity is not in question: a cymbal is a cymbal.  If CLAP cannot
# rank these near the top for their own names, it cannot be read about anything else.
CONTROLS = ("cymbal", "organ", "flute", "harpsichord", "thunder", "cricket")
CONTROL_RANK = 5

# The caption CLAP is asked.  Its training captions are sentences about a sound,
# not bare nouns, so the query is phrased as one.
def caption(name: str) -> str:
    return f"the sound of a {name}"


def entry_name(entry: dict) -> str:
    """The word a listener would use.  The first surface form is the instrument's
    own name in the library's own terms; the key is a programmer's spelling."""
    forms = entry.get("surface_forms") or []
    return (forms[0] if forms else entry["key"].replace("_", " ")).strip()


def to_clap(y: np.ndarray, sr: int) -> np.ndarray:
    """Mono float32 at CLAP's 48 kHz.  Linear resampling is enough: CLAP's own
    front end is a 64-band mel spectrogram, far coarser than the interpolation."""
    if y.ndim > 1:
        y = y.mean(axis=1)
    if sr != CLAP_SR:
        n = int(round(len(y) * CLAP_SR / sr))
        y = np.interp(np.linspace(0, len(y) - 1, n), np.arange(len(y)), y)
    peak = float(np.max(np.abs(y))) or 1.0
    return (y / peak * 0.7).astype("float32")


def render_all(entries, keys):
    """One render per entry per register, kept SEPARATE.  CLAP's audio tower reads
    a fixed ~10 s window, so three concatenated 5 s renders would silently drop the
    top register; each is embedded on its own and the embeddings averaged, so the
    body is judged over its whole range and none of it is thrown away."""
    out = {}
    for i, t in enumerate(entries, 1):
        if keys and t["key"] not in keys:
            continue
        parts = []
        for f in RENDER_FREQS:
            y, err = M.render(t["code"], dur=RENDER_DUR, freq=f, preroll=RENDER_PREROLL)
            if y is None:
                print(f"  [{i}] {t['key']} @ {f} Hz: {err}", file=sys.stderr)
                break
            parts.append(to_clap(y, M.SR))
        if len(parts) != len(RENDER_FREQS):
            print(f"  [{i}] {t['key']}: DID NOT RENDER -- skipped", file=sys.stderr)
            continue
        out[t["key"]] = parts
        print(f"  [{i}/{len(entries)}] {t['key']}", file=sys.stderr)
    return out


def rank_one(sims: np.ndarray, keys: list, key: str) -> tuple:
    """Where does `key` sit in the ranking induced by `sims` (higher = closer)?"""
    order = np.argsort(-sims)
    ranked = [keys[j] for j in order]
    return ranked.index(key) + 1, ranked, sims[keys.index(key)]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--device", default="mps")
    ap.add_argument("--out", default=str(REPO / "tools" / "lco_resemblance_out"))
    ap.add_argument("--keys", default="")
    args = ap.parse_args()
    only = {k.strip() for k in args.keys.split(",") if k.strip()}

    print("""\
limits of this run -- read before quoting any number from it
  out of domain. CLAP is trained on captioned real-world recordings. Here it is
    asked to rank synthetic single notes against a closed set of synthesiser jargon.
    Its own controls failing is the expected result of that, not a fault to repair.
  a rank is not recognisability, even with the controls passing. There is no
    listener, no panel and no cross-language check -- which is where the question
    actually lives: Zacharakis, Pastiadis & Reiss on the semantic dimensions of
    timbre; Saitis & Weinzierl, "The Semantics of Timbre", in Siedenburg et al.
    (eds.), Timbre, Springer 2019. For descriptors rather than semantics, Peeters,
    Giordano, Susini, Misdariis & McAdams, The Timbre Toolbox, JASA 130(5), 2011 --
    whose own finding is that most such descriptors are redundant.
  the control set and the rank-5 bar were chosen here, by hand, against nothing.
""", file=sys.stderr)

    lex = json.loads(LEX.read_text())
    entries = lex["techniques"]
    print(f"lexicon_version {lex.get('lexicon_version')}, {len(entries)} entries",
          file=sys.stderr)

    print("rendering...", file=sys.stderr)
    audio = render_all(entries, only)
    keys = [t["key"] for t in entries if t["key"] in audio]
    names = {t["key"]: entry_name(t) for t in entries if t["key"] in audio}
    print(f"{len(keys)} entries rendered\n", file=sys.stderr)

    outdir = Path(args.out)
    outdir.mkdir(parents=True, exist_ok=True)

    import torch
    results = {}
    for ckpt in CHECKPOINTS:
        print(f"--- {ckpt} ---", file=sys.stderr)
        pi.CLAP_MODEL_ID = ckpt
        pi._clap_cache.clear()
        pi._clap_vocab_cache.clear()
        model, proc, _, _ = pi._get_clap(args.device)

        with torch.no_grad():
            flat, span = [], []
            for k in keys:
                span.append((len(flat), len(flat) + len(audio[k])))
                flat.extend(audio[k])
            per = pi._clap_embed_audios(model, proc, flat, args.device)
            aemb = torch.nn.functional.normalize(
                torch.stack([per[a:b].mean(0) for a, b in span]), dim=-1)
            temb = pi._clap_embed_texts(
                model, proc, [caption(names[k]) for k in keys], args.device)
        sim = (temb @ aemb.T).numpy()      # rows: captions, cols: bodies

        res = {}
        for i, k in enumerate(keys):
            rank, ranked, own = rank_one(sim[i], keys, k)
            res[k] = {
                "name": names[k],
                "rank": rank,
                "of": len(keys),
                "own_similarity": float(own),
                "top": ranked[:3],
                "best_similarity": float(sim[i].max()),
            }
        results[ckpt] = res

        bad = [c for c in CONTROLS if c in res and res[c]["rank"] > CONTROL_RANK]
        print("  controls: " + ", ".join(
            f"{c} #{res[c]['rank']}" for c in CONTROLS if c in res), file=sys.stderr)
        if bad:
            print(f"  !! {ckpt} put {bad} outside the top {CONTROL_RANK} for their "
                  f"own names", file=sys.stderr)
        # The control gate is only meaningful over the whole library: on a subset
        # the candidate pool is not the library, so a rank in it is not a rank.
        results[ckpt + "::controls_ok"] = (not bad) or bool(only)

    (outdir / "resemblance.json").write_text(
        json.dumps({"lexicon_version": lex.get("lexicon_version"),
                    "results": results}, indent=1) + "\n")

    ok = all(results.get(c + "::controls_ok") for c in CHECKPOINTS)
    if not ok:
        print("\nREFUSING to rank the library: at least one checkpoint failed its "
              "controls, and a ranking from a meter that cannot hear a cymbal is "
              "not evidence about anything else.  Raw numbers are in "
              f"{outdir/'resemblance.json'} for inspection only.")
        return 1

    a, b = CHECKPOINTS
    rows = sorted(keys, key=lambda k: -(results[a][k]["rank"] + results[b][k]["rank"]))
    print(f"\n{'entry':>16}  {'asked for':<24} {'unfused':>8} {'music':>8}   "
          f"what each model picks instead")
    for k in rows:
        ra, rb = results[a][k], results[b][k]
        alt = ra["top"][0] if ra["rank"] > 1 else (rb["top"][0] if rb["rank"] > 1 else "")
        print(f"{k:>16}  {caption(names[k]):<24} "
              f"{ra['rank']:>4}/{ra['of']} {rb['rank']:>4}/{rb['of']}   {alt}")
    print(f"\nwritten to {outdir/'resemblance.json'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
