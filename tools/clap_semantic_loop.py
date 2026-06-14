#!/usr/bin/env python3
"""Simulate the closed SEMANTIC loop (mode 3 / option "B" of the UI proposal).

Distinct from the homeostasis PoC (clap_homeostasis.py), which closed a *scalar
control* loop (CLAP cosine -> sigma). Here the loop is SEMANTIC: the machine
DESCRIBES its own output in words (CLAP ranks the audio against a vocabulary ->
top-k tags) and those words become the PROMPT for the next generation. The audio
also carries forward via init_audio (the existing resynth harness), so this is
the full cybernetic loop the plugin would run:

    audio_n  --CLAP-->  tags_n  -->  prompt_{n+1}  --SA3-->  audio_{n+1}
       \__________________ init_audio (signal carry) _________________/

The point is NOT control; it is to make the machine's enculturated hearing
audible as a *generative force*. If a warm drone is heard as "violent", and
"violent" steers the next round, does the loop talk itself into the stereotype?
That is the bias as a self-fulfilling prophecy, made literal and listenable.

Alpha / prompt convention (verified against backend _generate_native + the
PromptPanel single-prompt guard):
  linear blend  manipulated = (0.5-0.5a)*A + (0.5+0.5a)*B
  a=-1 -> pure A ;  a=0 -> 50/50 ;  a=+1 -> pure B
  an empty field is the reflection of the other through null:  echo = 2*null - other
  => only-B at a=0  ==  0.5*(2null-B) + 0.5*B  ==  NULL  (the "null point" the
     plugin's guard normally prevents, but deliberately allows when a is
     explicitly set). This is the null-control: the words cancel exactly.

Experiments (each: 10 iters, every iteration saved as a WAV to audition):
  run1_both       affirmative loop, tags -> BOTH prompts (a=b=tags), a=0
  run2_onlyB_null only B = tags, a=0  -> the null mirror (semantic drive cancels;
                  signal loop alone) = the control that isolates whether words matter
  e3_counter      ADVERSARIAL: feed the tags the audio is FARTHEST from (bottom-k)
                  -> "become what the machine hears least"; can you escape the basin?
  e5_audioset     same as run1 but the SOURCE-FRAMED AudioSet ontology as the menu
                  (the bias is loudest: "Donkey", "Children shouting") -> the menu
                  is the politics; watch the trajectory diverge from run1's timbre menu
  e4_two_ears     two CLAP ears that DISAGREE (unfused top -> A, music top -> B, a=0)
                  -> the synth lives in the contradiction between two western ears

Reuses the plugin-identical IPC path (test_init_audio) and the validated CLAP
ear (clap_probe, sine-vs-noise sanity gate). Read-only except under --out.
WAVs are the deliverable; manifest.json + REPORT.md carry the word trajectories.

Usage:
    .venv/bin/python tools/clap_semantic_loop.py
    .venv/bin/python tools/clap_semantic_loop.py --experiments run1_both run2_onlyB_null
    .venv/bin/python tools/clap_semantic_loop.py --iters 10 --device mps
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

import numpy as np
import torch
import torchaudio

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_init_audio import PipeClient, encode_init_audio, write_wav  # noqa: E402
import clap_probe as cp  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[1]
BACKEND_SCRIPT = REPO_ROOT / "backend" / "pipe_inference.py"

SEED_PROMPT = "warm analog bass drone"
MODEL = "stable-audio-3-small-music"          # only init_audio-capable engine
STEPS = 8
DURATION_S = 3.0
SEED_BASE = 7000                              # per-iter seed = SEED_BASE + iter
UNFUSED = "laion/clap-htsat-unfused"          # canonical, separable (the control ear)
MUSIC = "laion/larger_clap_music_and_speech"  # music-tuned, faithful timbre (2nd ear)


def log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


def clap_audio_emb(ear, proc, audio_cs: np.ndarray, sr: int, device: str) -> torch.Tensor:
    """Normalized CLAP audio embedding of an [channels, samples] array."""
    mono = audio_cs.mean(axis=0).astype("float32")
    if sr != cp.CLAP_SR:
        mono = torchaudio.functional.resample(torch.from_numpy(mono), sr, cp.CLAP_SR).numpy()
    return cp.embed_audios(ear, proc, [mono], device)[0]


def analyze(ear, proc, audio, sr, device, vocab_emb, vocab_labels, topk, which="top"):
    """Rank audio against the vocabulary; return (labels[(str,score)], audio_emb).

    which='top' = nearest tags (affirmative); 'bottom' = farthest tags (the
    adversarial counter-steer: drive toward what the ear hears LEAST).
    """
    emb = clap_audio_emb(ear, proc, audio, sr, device)
    sims = emb @ vocab_emb.T                                  # [N]
    order = torch.argsort(sims, descending=True)
    idx = order[:topk] if which == "top" else order.flip(0)[:topk]
    labels = [(vocab_labels[int(i)], float(sims[int(i)])) for i in idx]
    return labels, emb


def generate(client, base, prompt_a, prompt_b, alpha, seed, prev_audio, sr, init_noise):
    """One SA3 generation. prev_audio (or None on iter 1) feeds back as init_audio."""
    req = {**base, "prompt_a": prompt_a, "prompt_b": prompt_b,
           "alpha": float(alpha), "seed": seed}
    if prev_audio is not None:
        req.update({
            "init_audio_b64": encode_init_audio(prev_audio),
            "init_audio_sr": sr,
            "init_audio_channels": prev_audio.shape[0],
            "init_noise_level": init_noise,
        })
    res = client.request(req)
    return res["audio"], res["sample_rate"]


def run_loop(client, base, ear, proc, device, vocab_emb, vocab_labels, iters,
             policy, which, init_noise, out_dir, name, topk):
    """Single-ear semantic loop. policy(tags_str)->(prompt_a, prompt_b, alpha).

    Iter 1 is always the clean seed prompt (pure A) so every experiment shares an
    anchor; iters 2.. are driven by the analysis of the previous output.
    """
    exp_dir = out_dir / name
    exp_dir.mkdir(parents=True, exist_ok=True)
    prompt_a, prompt_b, alpha = SEED_PROMPT, "", -1.0        # iter1 = pure seed
    prev_audio, sr, anchor_emb = None, None, None
    rows = []
    for it in range(1, iters + 1):
        audio, sr = generate(client, base, prompt_a, prompt_b, alpha,
                             SEED_BASE + it, prev_audio, sr, init_noise)
        labels, emb = analyze(ear, proc, audio, sr, device,
                              vocab_emb, vocab_labels, topk, which)
        if anchor_emb is None:
            anchor_emb = emb
        cos_anchor = float(emb @ anchor_emb)
        write_wav(exp_dir / f"iter{it:02d}.wav", audio, sr)
        rows.append({
            "iter": it, "prompt_a": prompt_a, "prompt_b": prompt_b,
            "alpha": round(float(alpha), 3),
            "init_noise": (None if prev_audio is None else init_noise),
            "heard": labels, "cos_to_anchor": round(cos_anchor, 4),
        })
        log(f"  [{name}] it{it:02d} a='{prompt_a[:30]}' b='{prompt_b[:20]}' "
            f"α={alpha:+.1f} -> {', '.join(f'{l}({s:.2f})' for l, s in labels)}"
            f"  cos0={cos_anchor:+.3f}")
        # Strip any commas INSIDE a label (AudioSet has "Air horn, truck horn")
        # so the comma-joined prompt still reads as exactly topk terms.
        prompt_a, prompt_b, alpha = policy(", ".join(l.replace(",", "") for l, _ in labels))
        prev_audio = audio
    return rows


def run_two_ears(client, base, earU, procU, vembU, earM, procM, vembM, vocab_labels,
                 device, iters, init_noise, out_dir, name, topk):
    """Two ears in conflict: unfused top tag -> A, music top tag -> B, alpha=0.

    cos_to_anchor tracks the unfused (control) ear; both ears' readings are logged.
    """
    exp_dir = out_dir / name
    exp_dir.mkdir(parents=True, exist_ok=True)
    prompt_a, prompt_b, alpha = SEED_PROMPT, "", -1.0        # iter1 = pure seed
    prev_audio, sr, anchor_emb = None, None, None
    rows = []
    for it in range(1, iters + 1):
        audio, sr = generate(client, base, prompt_a, prompt_b, alpha,
                             SEED_BASE + it, prev_audio, sr, init_noise)
        heardU, embU = analyze(earU, procU, audio, sr, device, vembU, vocab_labels, topk)
        heardM, embM = analyze(earM, procM, audio, sr, device, vembM, vocab_labels, topk)
        if anchor_emb is None:
            anchor_emb = embU
        cos_anchor = float(embU @ anchor_emb)
        write_wav(exp_dir / f"iter{it:02d}.wav", audio, sr)
        rows.append({
            "iter": it, "prompt_a": prompt_a, "prompt_b": prompt_b,
            "alpha": round(float(alpha), 3),
            "init_noise": (None if prev_audio is None else init_noise),
            "heard_unfused": heardU, "heard_music": heardM,
            "cos_to_anchor": round(cos_anchor, 4),
        })
        log(f"  [{name}] it{it:02d} α={alpha:+.1f}  "
            f"unfused={heardU[0][0]}({heardU[0][1]:.2f})  "
            f"music={heardM[0][0]}({heardM[0][1]:.2f})  cos0={cos_anchor:+.3f}")
        # The contradiction becomes the next A/B pair, blended 50/50.
        prompt_a, prompt_b, alpha = heardU[0][0], heardM[0][0], 0.0
        prev_audio = audio
    return rows


# ── prompt policies (tags_str -> (prompt_a, prompt_b, alpha)) ──────────────
def policy_both(s):    return (s, s, 0.0)     # tags into BOTH prompts, equal blend
def policy_onlyB(s):   return ("", s, 0.0)    # only B -> null mirror (the control)


def build_report(out_dir, all_rows, vocab_sizes, init_noise, cfg):
    md = [
        "# Closed semantic loop — the machine's hearing as a generative force",
        "",
        "Each iteration: SA3 generates, CLAP re-describes the output against a "
        "vocabulary, those words become the next prompt (audio also carries via "
        f"init_audio, init_noise={init_noise}). CFG={cfg}, {DURATION_S}s, {STEPS} steps, "
        f"per-iter seed={SEED_BASE}+iter. Seed prompt: `{SEED_PROMPT}`.",
        "",
        "The **word trajectory** is the artifact — read it as the machine talking "
        "itself somewhere. WAVs under each experiment dir are for listening.",
        "",
    ]
    notes = {
        "run1_both": "Affirmative loop, tags → both prompts (α=0). Does the loop "
                     "amplify the first mishearing into a basin?",
        "run2_onlyB_null": "Only B = tags, α=0 → the blend cancels to **null** "
                           "(`0.5·(2null−B)+0.5·B`). The semantic drive is removed; "
                           "what remains is the signal (init_audio) loop alone. The "
                           "control: if this differs from run1, the *words* are doing work.",
        "e3_counter": "ADVERSARIAL. Feed the tags the audio is FARTHEST from "
                      "(bottom-k) → steer toward what the ear hears least. Escape the "
                      "basin, or does the biased ear drag it back? (verhandelbar?)",
        "e5_audioset": "Same affirmative loop, but the SOURCE-FRAMED AudioSet menu "
                       "(events: 'Donkey', 'Children shouting'). The menu is the "
                       "politics — compare the trajectory to run1's timbre menu.",
        "e4_two_ears": "Two ears that disagree (unfused→A, music→B, α=0). The synth "
                       "lives in the contradiction between two western ears.",
    }
    for name, rows in all_rows.items():
        if not rows:
            continue
        md += [f"## {name}", "", f"*{notes.get(name, '')}*", "",
               f"vocab size: {vocab_sizes.get(name, '?')}", ""]
        c0, cl = rows[0]["cos_to_anchor"], rows[-1]["cos_to_anchor"]
        md.append(f"CLAP cosine to iter-1 anchor: {c0:+.3f} → {cl:+.3f} "
                  f"(drift {1.0 - cl:+.3f}).")
        md.append("")
        if "heard_unfused" in rows[0]:
            md += ["| iter | prompt (A / B) | unfused hears | music hears | cos→anchor |",
                   "|---:|---|---|---|---:|"]
            for r in rows:
                hu = ", ".join(f"{l}" for l, _ in r["heard_unfused"])
                hm = ", ".join(f"{l}" for l, _ in r["heard_music"])
                pa = (r["prompt_a"] or "∅")[:22]
                pb = (r["prompt_b"] or "∅")[:18]
                md.append(f"| {r['iter']} | {pa} / {pb} | {hu} | {hm} | {r['cos_to_anchor']:+.3f} |")
        else:
            md += ["| iter | prompt fed in | machine hears (top-k) | cos→anchor |",
                   "|---:|---|---|---:|"]
            for r in rows:
                heard = ", ".join(f"{l} ({s:.2f})" for l, s in r["heard"])
                if r["prompt_a"] and r["prompt_b"]:
                    p = r["prompt_a"][:34]
                elif r["prompt_b"]:
                    p = f"∅ / {r['prompt_b'][:30]} (→null)"
                else:
                    p = r["prompt_a"][:34] or "∅"
                md.append(f"| {r['iter']} | {p} | {heard} | {r['cos_to_anchor']:+.3f} |")
        md.append("")
    (out_dir / "REPORT.md").write_text("\n".join(md), encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--iters", type=int, default=10)
    ap.add_argument("--topk", type=int, default=3)
    ap.add_argument("--cfg", type=float, default=6.0,
                    help="CFG: high enough that the evolving PROMPT actually steers")
    ap.add_argument("--init-noise", type=float, default=0.5,
                    help="init_audio noise level (0=full carry, 1=text-only); 0.5 = both matter")
    ap.add_argument("--device", default="cpu", choices=["cpu", "mps", "cuda"])
    ap.add_argument("--experiments", nargs="*", default=[
        "run1_both", "run2_onlyB_null", "e3_counter", "e5_audioset", "e4_two_ears"])
    ap.add_argument("--out", default="tools/clap_semantic_loop_out")
    args = ap.parse_args()

    if not BACKEND_SCRIPT.is_file():
        log(f"ERROR: backend not found at {BACKEND_SCRIPT}")
        return 1
    out_dir = REPO_ROOT / args.out
    out_dir.mkdir(parents=True, exist_ok=True)

    from transformers import ClapModel, ClapProcessor

    # The timbre/affect menu (clap_probe's naive register) is the default loop
    # vocabulary; AudioSet (source-framed) is loaded only if e5 is requested.
    timbre_vocab = list(dict.fromkeys(cp.NAIVE_VOCAB))
    audioset_vocab = None
    if "e5_audioset" in args.experiments:
        try:
            log("Fetching AudioSet ontology ...")
            audioset_vocab = cp.load_audioset_vocab()
        except Exception as e:
            log(f"WARN: AudioSet unavailable ({type(e).__name__}: {e}); skipping e5_audioset")
            args.experiments = [e for e in args.experiments if e != "e5_audioset"]

    need_music = "e4_two_ears" in args.experiments

    log(f"Loading CLAP {UNFUSED} on {args.device} ...")
    earU = ClapModel.from_pretrained(UNFUSED).to(args.device).eval()
    procU = ClapProcessor.from_pretrained(UNFUSED)
    cp.assert_model_sane(earU, procU, args.device)
    vembU_timbre = cp.embed_texts(earU, procU, timbre_vocab, args.device)
    vembU_audioset = (cp.embed_texts(earU, procU, audioset_vocab, args.device)
                      if audioset_vocab else None)

    earM = procM = vembM_timbre = None
    if need_music:
        log(f"Loading CLAP {MUSIC} on {args.device} ...")
        earM = ClapModel.from_pretrained(MUSIC).to(args.device).eval()
        procM = ClapProcessor.from_pretrained(MUSIC)
        cp.assert_model_sane(earM, procM, args.device)
        vembM_timbre = cp.embed_texts(earM, procM, timbre_vocab, args.device)

    t0 = time.time()
    client = PipeClient([sys.executable, str(BACKEND_SCRIPT)])
    all_rows: dict[str, list] = {}
    vocab_sizes: dict[str, int] = {}
    try:
        models = client.info.get("models", [])
        if MODEL not in models:
            log(f"ERROR: {MODEL} not available; backend has {models}")
            return 1
        log(f"Backend ready. Using {MODEL}.  experiments={args.experiments}")
        base = {"model": MODEL, "duration": DURATION_S, "steps": STEPS, "cfg_scale": args.cfg}

        for name in args.experiments:
            log(f"\n== {name} ==")
            if name == "run1_both":
                all_rows[name] = run_loop(
                    client, base, earU, procU, args.device, vembU_timbre, timbre_vocab,
                    args.iters, policy_both, "top", args.init_noise, out_dir, name, args.topk)
                vocab_sizes[name] = len(timbre_vocab)
            elif name == "run2_onlyB_null":
                all_rows[name] = run_loop(
                    client, base, earU, procU, args.device, vembU_timbre, timbre_vocab,
                    args.iters, policy_onlyB, "top", args.init_noise, out_dir, name, args.topk)
                vocab_sizes[name] = len(timbre_vocab)
            elif name == "e3_counter":
                all_rows[name] = run_loop(
                    client, base, earU, procU, args.device, vembU_timbre, timbre_vocab,
                    args.iters, policy_both, "bottom", args.init_noise, out_dir, name, args.topk)
                vocab_sizes[name] = len(timbre_vocab)
            elif name == "e5_audioset":
                all_rows[name] = run_loop(
                    client, base, earU, procU, args.device, vembU_audioset, audioset_vocab,
                    args.iters, policy_both, "top", args.init_noise, out_dir, name, args.topk)
                vocab_sizes[name] = len(audioset_vocab)
            elif name == "e4_two_ears":
                all_rows[name] = run_two_ears(
                    client, base, earU, procU, vembU_timbre, earM, procM, vembM_timbre,
                    timbre_vocab, args.device, args.iters, args.init_noise, out_dir,
                    name, args.topk)
                vocab_sizes[name] = len(timbre_vocab)
            else:
                log(f"WARN: unknown experiment '{name}', skipping")
                continue
            (out_dir / name / "manifest.json").write_text(json.dumps({
                "name": name, "vocab_size": vocab_sizes.get(name),
                "cfg": args.cfg, "init_noise": args.init_noise, "model": MODEL,
                "steps": STEPS, "duration_s": DURATION_S, "seed_base": SEED_BASE,
                "topk": args.topk, "seed_prompt": SEED_PROMPT, "iters": all_rows[name],
            }, indent=2), encoding="utf-8")

        build_report(out_dir, all_rows, vocab_sizes, args.init_noise, args.cfg)
        log(f"\nDone in {time.time()-t0:.0f}s. WAVs + REPORT.md under {out_dir}")
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    sys.exit(main())
