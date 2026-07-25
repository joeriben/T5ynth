#!/usr/bin/env python3
"""Hold a lexicon entry next to a real recording of the instrument it names.

BJ, 2026-07-26, having listened to `vibraphone` and `waterphone`:

    "entweder vergleichst du das mit einem realen Wav - oder erzeuge eines mit
     SA3 - oder Du lässt es."

That is now the acceptance criterion, and this is the instrument for it.  A
reference is either a real recording dropped into the entry's folder, or one
generated here by the synth's own SA3 engine.  Both are treated identically from
that point on.

The number that decides is a RANK, not a similarity.  Absolute audio-text or
audio-audio similarities from a pretrained model are not interpretable and it is
easy to talk oneself into any of them.  So every one of the library's bodies is
compared against the same reference, and the question is only: is the body that
CLAIMS the name the one the reference picks?  If `struck_bar` sits closer to a
real vibraphone than `vibraphone` does, the claim is not carried by the sound,
and no amount of internal consistency repairs that.

Two independent distances are reported side by side and never averaged:

  * CLAP audio-embedding cosine -- what a pretrained listener hears.  It is
    sensitive to the whole character of a recording, INCLUDING the room and the
    performance, which a raw oscillator has none of.  Read it as a ranking.
  * a pitch-robust spectral-envelope distance -- the cepstrally smoothed
    long-term spectrum on a log-frequency grid, level-normalised.  It ignores
    the room and the performance and compares resonance structure only.

Where the two disagree, the disagreement is the finding.

  lco_reference.py generate --keys vibraphone,waterphone [--n 4] [--model NAME]
  lco_reference.py compare  --keys vibraphone,waterphone
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

import lco_measure as M              # noqa: E402
import lco_resemblance as R          # noqa: E402
import pipe_inference as pi          # noqa: E402

LEX = REPO / "backend" / "dco_lexicon.json"
OUT = REPO / "tools" / "lco_reference_out"

# A reference has to be the INSTRUMENT, not a track that features it.  SA3 will
# happily write a whole arrangement around a word; the prompt pins it to one
# instrument, close, dry, unaccompanied -- which is also the only thing a bare
# oscillator could ever be compared against.
#
# It says "unaccompanied", NOT "no drums, no music".  The backend decides between
# "TrackType: Instrument" and "TrackType: Music" with a string classifier
# (`_looks_like_music`) that matches the WORDS drums / music / percussion and
# understands no negation, so a prompt asking for no music is routed to Music --
# measured, the first reference generated here came back as a Music track for
# exactly that reason.  `assert_instrument` below re-checks the resolved prefix
# per key rather than trusting this wording to stay safe for every instrument name.
PROMPT = ("solo {name}, one instrument alone, single sustained note, close "
          "microphone, dry studio recording, unaccompanied, no reverb")

REF_SECONDS = 6.0
REF_STEPS = 50
REF_CFG = 6.0
SEEDS = (11, 22, 33, 44, 55, 66, 77, 88)


# ---------------------------------------------------------------- references

def generate(keys, n, model_name, device):
    """Generate n SA3 references per key.  Existing files are kept: a reference
    is evidence and regenerating it under a new seed would quietly move the
    goalposts of every comparison already made against it."""
    lex = json.loads(LEX.read_text())
    by = {t["key"]: t for t in lex["techniques"]}
    models = pi.find_models()
    if not models:
        print("no models found", file=sys.stderr)
        return 1
    pi._available_models.update(models)
    if model_name not in models:
        print(f"{model_name} is not installed; have: {sorted(models)}",
              file=sys.stderr)
        return 1
    pipe = pi.get_pipeline(model_name, device)
    import soundfile as sf

    for key in keys:
        if key not in by:
            print(f"{key}: not in the lexicon", file=sys.stderr)
            continue
        name = R.entry_name(by[key])
        prefix = pi._native_modality_prefix(pipe, "Instrument",
                                            PROMPT.format(name=name), 1)
        if "Instrument" not in prefix:
            print(f"{key}: the prompt for {name!r} resolves to {prefix.strip()!r}, "
                  f"not an isolated instrument -- a reference generated from it "
                  f"would be an arrangement. SKIPPED.", file=sys.stderr)
            continue
        d = OUT / key
        d.mkdir(parents=True, exist_ok=True)
        (d / "PROMPT.txt").write_text(
            PROMPT.format(name=name) + f"\n\nmodel: {model_name}\n"
            f"steps: {REF_STEPS}  cfg: {REF_CFG}  seconds: {REF_SECONDS}\n")
        for seed in SEEDS[:n]:
            path = d / f"sa3_seed{seed}.wav"
            if path.exists():
                print(f"  {key} seed {seed}: already there", file=sys.stderr)
                continue
            req = {
                "prompt_a": PROMPT.format(name=name),
                # alpha = -1 is PURE A, and it is not optional here.  The backend
                # mirrors an empty B through the model's null encoding
                # (`_echo_through_null`: B = 2*null - A), so the linear blend at
                # the alpha DEFAULT of 0.0 is exactly `null` -- an unconditional
                # generation with the prompt cancelled out.  The plugin pins alpha
                # for a single prompt in the GUI layer (`PromptPanel.cpp:3076`),
                # not in the backend, so every tool that calls the backend direct
                # has to pin it itself.  Measured before this line existed: flute,
                # cymbal, organ and rain references at the same seed came back with
                # the same RMS to three decimals and the same spectral centroid to
                # within 3 Hz -- four different instruments, one sound.
                "alpha": -1.0,
                "duration": REF_SECONDS,
                "steps": REF_STEPS,
                "cfg_scale": REF_CFG,
                "seed": seed,
                "track_type": "Instrument",
                "modality_epoch": 1,
            }
            audio, sr, used, elapsed, _ = pi._generate_native(pipe, req)
            if audio.ndim == 1:
                audio = np.stack([audio, audio])
            sf.write(str(path), np.asarray(audio).T, sr)
            print(f"  {key} seed {used}: {path.name}  ({elapsed:.0f}s)",
                  file=sys.stderr)
    return 0


def load_refs(key):
    """Every wav in the key's folder is a reference, whoever made it -- an SA3
    generation and a real recording the user dropped in are read the same way."""
    d = OUT / key
    if not d.is_dir():
        return []
    import soundfile as sf
    out = []
    for p in sorted(d.glob("*.wav")):
        y, sr = sf.read(str(p), dtype="float32", always_2d=True)
        out.append((p.name, R.to_clap(y.mean(axis=1), sr)))
    return out


# ------------------------------------------------------- spectral envelope

def envelope(y, sr=R.CLAP_SR, nbands=48, lifter=32):
    """Cepstrally smoothed log spectrum on a log-frequency grid, 40 Hz - 16 kHz,
    normalised to its own mean.  Liftering removes the harmonic comb, so two
    recordings of the same body at different pitches still land on each other;
    the mean-normalisation removes level, so this is colour only."""
    w = int(0.10 * sr)
    k = max(1, len(y) // w)
    f = y[:k * w].reshape(k, w) * np.hanning(w)
    p = (np.abs(np.fft.rfft(f, axis=1)) ** 2).mean(0)
    logp = np.log(np.maximum(p, 1e-12))
    ceps = np.fft.irfft(logp)
    ceps[lifter:-lifter] = 0.0
    sm = np.fft.rfft(ceps).real
    freqs = np.fft.rfftfreq(w, 1.0 / sr)
    grid = np.logspace(np.log10(40.0), np.log10(16000.0), nbands)
    env = np.interp(grid, freqs[1:], sm[1:len(freqs)])
    return (env - env.mean()) * (20.0 / np.log(10))     # dB, mean-removed


def env_distance(a, b):
    return float(np.sqrt(((a - b) ** 2).mean()))


# ------------------------------------------------------------------ compare

def calibrate_references(refs, device):
    """Do the references themselves carry the instrument they name?

    Asked BEFORE anything is compared against them, because a reference set that
    does not separate by instrument cannot tell you anything about a body -- and
    a set exactly like that was generated here on the first attempt (the prompt
    was being cancelled to the null embedding; see `alpha` in `generate`).  Every
    clip's nearest OTHER clip must be one of its own instrument, and the mean
    within-instrument similarity must beat the between-instrument one.  Under two
    instruments there is nothing to separate and the check is skipped.
    """
    import itertools
    import torch
    clips, owner = [], []
    for k, lst in refs.items():
        for _, y in lst:
            clips.append(y)
            owner.append(k)
    if len(set(owner)) < 2:
        return True, "only one instrument -- nothing to separate"
    model, proc, _, _ = pi._get_clap(device)
    with torch.no_grad():
        emb = pi._clap_embed_audios(model, proc, clips, device).numpy()
    env = np.stack([envelope(y) for y in clips])
    owner = np.array(owner)

    lines, ok = [], True
    for label, mat, closer_is_high in (
            ("CLAP cosine", emb @ emb.T, True),
            ("spectrum", np.stack([[env_distance(env[i], env[j])
                                    for j in range(len(clips))]
                                   for i in range(len(clips))]), False)):
        w = [mat[i, j] for i, j in itertools.combinations(range(len(clips)), 2)
             if owner[i] == owner[j]]
        b = [mat[i, j] for i, j in itertools.combinations(range(len(clips)), 2)
             if owner[i] != owner[j]]
        hit = 0
        for i in range(len(clips)):
            v = mat[i].copy()
            v[i] = -1e9 if closer_is_high else 1e9
            j = int(np.argmax(v) if closer_is_high else np.argmin(v))
            hit += owner[j] == owner[i]
        good = ((np.mean(w) > np.mean(b)) if closer_is_high
                else (np.mean(w) < np.mean(b))) and hit > len(clips) // 2
        ok &= bool(good)
        lines.append(f"    {label:<12} within {np.mean(w):7.3f}  between "
                     f"{np.mean(b):7.3f}  nearest neighbour is the same "
                     f"instrument {hit}/{len(clips)}"
                     f"{'' if good else '   <-- FAILS'}")
    return ok, "\n".join(lines)


def compare(keys, device):
    lex = json.loads(LEX.read_text())
    entries = lex["techniques"]

    refs = {k: load_refs(k) for k in keys}
    missing = [k for k in keys if not refs[k]]
    if missing:
        print(f"no reference audio for {missing} -- run `generate` first, or drop "
              f"a real recording into {OUT}/<key>/", file=sys.stderr)
        keys = [k for k in keys if refs[k]]
    if not keys:
        return 1

    print("do the references separate by instrument?")
    ok, lines = calibrate_references({k: refs[k] for k in keys}, device)
    print(lines)
    if not ok:
        print("\nREFUSING to compare. These references do not carry the instrument "
              "they name, so a body's distance to them is not evidence about the "
              "body. Fix the references -- or use real recordings -- before "
              "reading anything below.")
        return 1

    print("\nrendering the library...", file=sys.stderr)
    audio = R.render_all(entries, set())
    lib = [t["key"] for t in entries if t["key"] in audio]

    # Every register is kept as its OWN vector and aggregated only after the
    # distance is taken.  Averaging the three registers' embeddings first is what
    # the first version did, and it does not measure what it looks like it
    # measures: an entry whose registers differ pulls its mean toward the middle
    # of the space, where it is close to everything -- so the ranking came out
    # IDENTICAL for a flute, a cymbal, an organ and rain (the same five entries
    # "closest" to all four).  A mean of distances has no such bias.
    import torch
    model, proc, _, _ = pi._get_clap(device)
    with torch.no_grad():
        flat, span = [], []
        for k in lib:
            span.append((len(flat), len(flat) + len(audio[k])))
            flat.extend(audio[k])
        per = pi._clap_embed_audios(model, proc, flat, device).numpy()
    lib_emb = [per[a:b] for a, b in span]                    # per entry: R x D
    lib_env = [np.stack([envelope(x) for x in audio[k]]) for k in lib]

    report = {}
    for key in keys:
        names = [n for n, _ in refs[key]]
        with torch.no_grad():
            remb = pi._clap_embed_audios(
                model, proc, [y for _, y in refs[key]], device).numpy()
        renv = np.stack([envelope(y) for _, y in refs[key]])

        clap = np.array([float((e @ remb.T).mean()) for e in lib_emb])   # higher = closer
        env = np.array([float(np.mean([[env_distance(a, r) for r in renv]
                                       for a in e]))
                        for e in lib_env])                              # lower = closer

        c_order = [lib[i] for i in np.argsort(-clap)]
        e_order = [lib[i] for i in np.argsort(env)]
        report[key] = {
            "references": names,
            "clap_rank": c_order.index(key) + 1 if key in c_order else None,
            "env_rank": e_order.index(key) + 1 if key in e_order else None,
            "of": len(lib),
            "clap_top": c_order[:5],
            "env_top": e_order[:5],
            "clap_own": float(clap[lib.index(key)]),
            "env_own": float(env[lib.index(key)]),
            "env_best": float(env.min()),
        }
        r = report[key]
        print(f"\n=== {key} vs {len(names)} references ===")
        print(f"  CLAP      rank {r['clap_rank']}/{r['of']}   "
              f"closest: {', '.join(r['clap_top'])}")
        print(f"  spectrum  rank {r['env_rank']}/{r['of']}   "
              f"closest: {', '.join(r['env_top'])}")
        print(f"  own spectral distance {r['env_own']:.2f} dB, "
              f"best in library {r['env_best']:.2f} dB")

    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / "comparison.json").write_text(json.dumps(
        {"lexicon_version": lex.get("lexicon_version"), "report": report},
        indent=1) + "\n")
    print(f"\nwritten to {OUT/'comparison.json'}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=("generate", "compare"))
    ap.add_argument("--keys", required=True)
    ap.add_argument("--n", type=int, default=4)
    ap.add_argument("--model", default="stable-audio-3-medium")
    ap.add_argument("--device", default="mps")
    a = ap.parse_args()
    keys = [k.strip() for k in a.keys.split(",") if k.strip()]
    if a.cmd == "generate":
        return generate(keys, a.n, a.model, a.device)
    return compare(keys, a.device)


if __name__ == "__main__":
    sys.exit(main())
