#!/usr/bin/env python3
"""Simulate the closed SEMANTIC loop (mode 3 / option "B" of the UI proposal),
seeded from a REAL .t5p preset so the loop's iter-1 reproduces the actual sound.

Distinct from the homeostasis PoC (clap_homeostasis.py), which closed a *scalar
control* loop (CLAP cosine -> sigma). Here the loop is SEMANTIC: the machine
DESCRIBES its own output in words (CLAP ranks the audio against a vocabulary ->
top-k tags) and those words drive the next generation's prompts. The audio also
carries forward via init_audio (the existing resynth harness), so this is the
full cybernetic loop the plugin would run:

    audio_n  --CLAP-->  tags_n  -->  prompt_{n+1}  --SA3-->  audio_{n+1}
       \__________________ init_audio (signal carry) _________________/

GROUND TRUTH = THE PRESET'S OWN PARAMETERS
------------------------------------------
An earlier version hard-coded duration=3s / a made-up seed and got output that
had NOTHING to do with the plugin's actual render: the user's "Creamy-Dreamy SA3"
preset (11s, its own seed) renders a WARM CREAMY PAD, but at 3s the *same prompt*
renders a bright METALLIC blip (centroid 3002 vs 1104 Hz; CLAP "industrial metal"
vs "warm analog pad"). The duration alone flipped the character. So this tool now
LOADS the .t5p and takes ALL its generation params (duration, seed, alpha,
prompts, model, steps, cfg, magnitude, noise) verbatim — iter 1 reproduces the
preset, and the loop evolves a FAITHFUL identity instead of an artefact.

THE IDENTITY ANCHOR (collision)
-------------------------------
The user's presets are A/B *collisions* (e.g. "creamy cream, birds chirping" vs
"dreamy dream"), so the anchor is a PAIR. Both poles are held fixed every
iteration; the machine's hearing is APPENDED to the pole(s), never replaces them:

    A (prompt_a) = HEADER_A + ", " + <qualities>     identity pole A, fixed
    B (prompt_b) = HEADER_B + ", " + <qualities>     identity pole B, fixed
    alpha        = ALPHA0 (the preset's blend)        the collision is preserved

The seed identity leads each prompt; the re-hearing modulates ("Eigenschaften
hinterher"). Iter 1 is the pure collision at the preset's params (the drift
anchor). The seed is held FIXED across iterations (matching the plugin's
randomSeed=off behaviour) so the only drivers of drift are the changing words and
the init_audio carry — not seed noise.

NB on the engine: the loop REQUIRES init_audio, and only SA3 (diffusion_cond_
inpaint) supports it — which is also the preset's model.

Alpha / prompt convention (verified against backend _generate_native + the
PromptPanel single-prompt guard):
  linear blend  manipulated = (0.5-0.5a)*A + (0.5+0.5a)*B
  a=-1 -> pure A ;  a=0 -> 50/50 ;  a=+1 -> pure B
  an empty field is the reflection of the other through null:  echo = 2*null - other
  => only-B at a=0  ==  0.5*(2null-B) + 0.5*B  ==  NULL  (the null-control).

Experiments (each: N iters, every iteration saved as a WAV to audition):
  run1_both       collision-anchored affirmative loop: qualities appended to BOTH
                  poles, a=ALPHA0. Does the collision survive its own re-hearing?
  run2_onlyB_null only B = pole-B + qualities, a=0 -> the null mirror (NO A pole,
                  semantics cancel; the bare init_audio signal loop). The control.
  e6_driftB       ASYMMETRIC: pole A fixed pure, only pole B drifts under the
                  machine's hearing. Collision = stable anchor vs ear-driven drift.
  e3_counter      ADVERSARIAL: read the machine's TOP-k, append the ANTONYM of each
                  to BOTH poles, to steer toward the opposite of what it hears while
                  the collision is held. Escape vs recapture is the gap.
  e5_audioset     collision-anchored, but the SOURCE-FRAMED AudioSet ontology as the
                  menu (the bias is loudest) -> the menu is the politics.
  e4_two_ears     two CLAP ears that DISAGREE, each appended to a different pole.

Reuses the plugin-identical IPC path (test_init_audio) and the validated CLAP
ear (clap_probe, sine-vs-noise sanity gate). Read-only except under --out.

Usage:
    .venv/bin/python tools/clap_semantic_loop.py                 # default preset
    .venv/bin/python tools/clap_semantic_loop.py --preset "~/Library/T5ynth/presets/UCDCAE AI Lab/evolvement03 SA3.t5p"
    .venv/bin/python tools/clap_semantic_loop.py --experiments run1_both
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
# Default seed preset: a real SA3-authored A/B collision the user flagged. Loading
# the .t5p (not hard-coded params) is what keeps iter-1 faithful to the plugin.
DEFAULT_PRESET = str(Path.home() / "Library/T5ynth/presets/UCDCAE AI Lab/Creamy-Dreamy SA3.t5p")

# These are filled from the preset in main() (load_preset); the literals are only
# fallbacks if no preset is given. ALL must match what PromptPanel.buildInferenceRequest
# + PipeInference serialise, or the render diverges from the plugin's.
HEADER_A = "creamy cream, birds chirping"
HEADER_B = "dreamy dream"
ALPHA0 = 0.005229949951172
MODEL = "stable-audio-3-small-music"          # only init_audio-capable engine
DURATION_S = 11.0
STEPS = 8
CFG = 1.0
SEED = 2128708858                             # fixed across iters (preset randomSeed=off)
MAGNITUDE = 1.0
NOISE_SIGMA = 0.0
INJECTION_MODE = "linear"

UNFUSED = "laion/clap-htsat-unfused"          # canonical, separable (the control ear)
MUSIC = "laion/larger_clap_music_and_speech"  # music-tuned, faithful timbre (2nd ear)


def log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


def load_preset(path: Path) -> dict:
    """Read a .t5p (JSON header) and return the generation params verbatim, mapped
    to the backend's request keys (cfg->cfg_scale, noise->noise_sigma)."""
    raw = path.read_bytes()
    obj, _ = json.JSONDecoder().raw_decode(raw[raw.find(b"{"):].decode("utf-8", "replace"))
    s = obj.get("synth", {})
    return {
        "name": obj.get("name", path.stem),
        "header_a": s.get("promptA", ""),
        "header_b": s.get("promptB", ""),
        "alpha0": float(s.get("alpha", 0.0)),
        "model": s.get("model", MODEL),
        "duration": float(s.get("duration", DURATION_S)),
        "steps": int(s.get("steps", STEPS)),
        "cfg": float(s.get("cfg", CFG)),
        "seed": int(s.get("seed", SEED)),
        "magnitude": float(s.get("magnitude", 1.0)),
        "noise_sigma": float(s.get("noise", 0.0)),
        "injection_mode": s.get("injectionMode", "linear"),
        "random_seed": bool(s.get("randomSeed", False)),
    }


def with_quals(pole: str, quals: str | None) -> str:
    """Lead with the fixed identity pole, append the machine's hearing after it."""
    if not pole:
        return quals or ""
    if not quals:
        return pole
    return f"{pole}, {quals}"


def clap_audio_emb(ear, proc, audio_cs: np.ndarray, sr: int, device: str) -> torch.Tensor:
    """Normalized CLAP audio embedding of an [channels, samples] array."""
    mono = audio_cs.mean(axis=0).astype("float32")
    if sr != cp.CLAP_SR:
        mono = torchaudio.functional.resample(torch.from_numpy(mono), sr, cp.CLAP_SR).numpy()
    return cp.embed_audios(ear, proc, [mono], device)[0]


def analyze(ear, proc, audio, sr, device, vocab_emb, vocab_labels, topk, which="top"):
    """Rank audio against the vocabulary; return (labels[(str,score)], audio_emb)."""
    emb = clap_audio_emb(ear, proc, audio, sr, device)
    sims = emb @ vocab_emb.T                                  # [N]
    order = torch.argsort(sims, descending=True)
    idx = order[:topk] if which == "top" else order.flip(0)[:topk]
    labels = [(vocab_labels[int(i)], float(sims[int(i)])) for i in idx]
    return labels, emb


def generate(client, base, prompt_a, prompt_b, alpha, prev_audio, sr, init_noise):
    """One SA3 generation at the preset's fixed seed. prev_audio (None on iter 1)
    feeds back as init_audio."""
    req = {**base, "prompt_a": prompt_a, "prompt_b": prompt_b,
           "alpha": float(alpha), "seed": SEED}
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
    """Collision-anchored semantic loop. policy(quals_str)->(prompt_a, prompt_b, alpha).
    Iter 1 = pure preset collision (HEADER_A/HEADER_B/ALPHA0, no qualities) = drift anchor.
    """
    exp_dir = out_dir / name
    exp_dir.mkdir(parents=True, exist_ok=True)
    prompt_a, prompt_b, alpha = HEADER_A, HEADER_B, ALPHA0   # iter1 = pure collision
    prev_audio, sr, anchor_emb, quals = None, None, None, None
    rows = []
    for it in range(1, iters + 1):
        audio, sr = generate(client, base, prompt_a, prompt_b, alpha, prev_audio, sr, init_noise)
        labels, emb = analyze(ear, proc, audio, sr, device,
                              vocab_emb, vocab_labels, topk, which)
        if anchor_emb is None:
            anchor_emb = emb
        cos_anchor = float(emb @ anchor_emb)
        write_wav(exp_dir / f"iter{it:02d}.wav", audio, sr)
        rows.append({
            "iter": it, "prompt_a": prompt_a, "prompt_b": prompt_b,
            "alpha": round(float(alpha), 4), "quals": quals,
            "init_noise": (None if prev_audio is None else init_noise),
            "heard": labels, "cos_to_anchor": round(cos_anchor, 4),
        })
        log(f"  [{name}] it{it:02d} A='{prompt_a[:26]}' B='{prompt_b[:26]}' "
            f"α={alpha:+.2f} -> {', '.join(f'{l}({s:.2f})' for l, s in labels)}"
            f"  cos0={cos_anchor:+.3f}")
        quals = ", ".join(l.replace(",", "") for l, _ in labels)
        prompt_a, prompt_b, alpha = policy(quals)
        prev_audio = audio
    return rows


def run_two_ears(client, base, earU, procU, vembU, earM, procM, vembM, vocab_labels,
                 device, iters, init_noise, out_dir, name, topk):
    """Two ears in conflict, each appended to a different pole. cos tracks unfused ear."""
    exp_dir = out_dir / name
    exp_dir.mkdir(parents=True, exist_ok=True)
    prompt_a, prompt_b, alpha = HEADER_A, HEADER_B, ALPHA0   # iter1 = pure collision
    prev_audio, sr, anchor_emb = None, None, None
    rows = []
    for it in range(1, iters + 1):
        audio, sr = generate(client, base, prompt_a, prompt_b, alpha, prev_audio, sr, init_noise)
        heardU, embU = analyze(earU, procU, audio, sr, device, vembU, vocab_labels, topk)
        heardM, embM = analyze(earM, procM, audio, sr, device, vembM, vocab_labels, topk)
        if anchor_emb is None:
            anchor_emb = embU
        cos_anchor = float(embU @ anchor_emb)
        write_wav(exp_dir / f"iter{it:02d}.wav", audio, sr)
        rows.append({
            "iter": it, "prompt_a": prompt_a, "prompt_b": prompt_b,
            "alpha": round(float(alpha), 4),
            "init_noise": (None if prev_audio is None else init_noise),
            "heard_unfused": heardU, "heard_music": heardM,
            "cos_to_anchor": round(cos_anchor, 4),
        })
        log(f"  [{name}] it{it:02d} α={alpha:+.2f}  "
            f"unfused={heardU[0][0]}({heardU[0][1]:.2f})  "
            f"music={heardM[0][0]}({heardM[0][1]:.2f})  cos0={cos_anchor:+.3f}")
        uTag = heardU[0][0].replace(",", "")
        mTag = heardM[0][0].replace(",", "")
        prompt_a, prompt_b, alpha = with_quals(HEADER_A, uTag), with_quals(HEADER_B, mTag), ALPHA0
        prev_audio = audio
    return rows


def run_adversarial(client, base, ear, proc, device, vocab_emb, vocab_labels, iters,
                    init_noise, out_dir, name, topk):
    """Adversarial, collision-anchored: read TOP-k, append the ANTONYM of each to BOTH
    poles (the vocab term with the most-negative text-text cosine). 'qualities' = the
    antonyms we steer WITH; 'heard' = what it reads anyway -> escape vs recapture gap."""
    exp_dir = out_dir / name
    exp_dir.mkdir(parents=True, exist_ok=True)
    tt = vocab_emb @ vocab_emb.T                      # [N, N]
    antonym_of = torch.argmin(tt, dim=1)             # [N] -> most-opposite index
    prompt_a, prompt_b, alpha = HEADER_A, HEADER_B, ALPHA0   # iter1 = pure collision
    prev_audio, sr, anchor_emb, quals = None, None, None, None
    rows = []
    for it in range(1, iters + 1):
        audio, sr = generate(client, base, prompt_a, prompt_b, alpha, prev_audio, sr, init_noise)
        emb = clap_audio_emb(ear, proc, audio, sr, device)
        sims = emb @ vocab_emb.T
        top_idx = torch.argsort(sims, descending=True)[:topk]
        heard = [(vocab_labels[int(i)], float(sims[int(i)])) for i in top_idx]
        anti_idx = list(dict.fromkeys(int(antonym_of[int(i)]) for i in top_idx))
        counter = [vocab_labels[j] for j in anti_idx]
        if anchor_emb is None:
            anchor_emb = emb
        cos_anchor = float(emb @ anchor_emb)
        write_wav(exp_dir / f"iter{it:02d}.wav", audio, sr)
        rows.append({
            "iter": it, "prompt_a": prompt_a, "prompt_b": prompt_b,
            "alpha": round(float(alpha), 4), "quals": quals,
            "init_noise": (None if prev_audio is None else init_noise),
            "heard": heard, "counter_fed_next": counter,
            "cos_to_anchor": round(cos_anchor, 4),
        })
        log(f"  [{name}] it{it:02d} quals='{(quals or '')[:24]}' HEARS "
            f"{', '.join(f'{l}({s:.2f})' for l, s in heard)} => counter={','.join(counter)}"
            f"  cos0={cos_anchor:+.3f}")
        quals = ", ".join(c.replace(",", "") for c in counter)
        prompt_a, prompt_b, alpha = with_quals(HEADER_A, quals), with_quals(HEADER_B, quals), ALPHA0
        prev_audio = audio
    return rows


# ── prompt policies (quals_str -> (prompt_a, prompt_b, alpha)) ──────────────
def policy_both(q):  return (with_quals(HEADER_A, q), with_quals(HEADER_B, q), ALPHA0)
def policy_onlyB(q): return ("", with_quals(HEADER_B, q), 0.0)
def policy_driftB(q): return (HEADER_A, with_quals(HEADER_B, q), ALPHA0)


def build_report(out_dir, all_rows, vocab_sizes, init_noise, preset_name):
    md = [
        "# Closed semantic loop — the machine's hearing as a generative force",
        "",
        "Each iteration: SA3 generates, CLAP re-describes the output against a "
        "vocabulary, those words drive the next prompts (audio also carries via "
        f"init_audio, init_noise={init_noise}). Generation params are taken VERBATIM "
        f"from the preset (no hard-coded duration/seed):",
        "",
        f"- preset: **{preset_name}**, model `{MODEL}`",
        f"- duration **{DURATION_S}s**, {STEPS} steps, CFG {CFG}, magnitude {MAGNITUDE}, "
        f"noise_sigma {NOISE_SIGMA}, injection `{INJECTION_MODE}`",
        f"- **seed {SEED} held FIXED across iterations** (preset randomSeed=off) so only "
        "the words + init_audio drive drift, not seed noise",
        "",
        f"**Seed collision (identity, fixed every iteration): A=`{HEADER_A}` × "
        f"B=`{HEADER_B}` at α={ALPHA0:+.3f}.** Except where noted, the machine's previous "
        "hearing is APPENDED to each pole (`pole, qualities`); the seed identity leads, "
        "the re-hearing modulates. Iter 1 is the pure preset render (the drift anchor). "
        "WAVs under each experiment dir are for listening.",
        "",
    ]
    notes = {
        "run1_both": "Collision-anchored affirmative loop: qualities appended to BOTH "
                     "poles, α=ALPHA0. Does the collision survive its own re-hearing?",
        "run2_onlyB_null": "Only B = pole-B + qualities, α=0 → the blend cancels to "
                           "**null**, and the A pole is dropped. The bare init_audio "
                           "signal loop. The control: the gap to run1 is what the words do.",
        "e6_driftB": "ASYMMETRIC: pole A held pure, only pole B drifts under the "
                     "machine's hearing. Collision = stable anchor vs ear-driven drift.",
        "e3_counter": "ADVERSARIAL. Read the machine's TOP-k, append the ANTONYM of each "
                      "to BOTH poles (α=ALPHA0). 'qualities fed' = antonyms we steer WITH; "
                      "'machine hears' = what it reads anyway. Escape vs recapture gap.",
        "e5_audioset": "Collision-anchored, but the SOURCE-FRAMED AudioSet menu. The "
                       "menu is the politics — compare to run1's timbre menu.",
        "e4_two_ears": "Two ears that disagree, each appended to a different pole. Two "
                       "western ears split the collision between them.",
    }
    for name, rows in all_rows.items():
        if not rows:
            continue
        md += [f"## {name}", "", f"*{notes.get(name, '')}*", "",
               f"vocab size: {vocab_sizes.get(name, '?')}", ""]
        c0, cl = rows[0]["cos_to_anchor"], rows[-1]["cos_to_anchor"]
        md.append(f"CLAP cosine to iter-1 anchor (the preset render): {c0:+.3f} → {cl:+.3f} "
                  f"(drift {1.0 - cl:+.3f}).")
        md.append("")
        if "heard_unfused" in rows[0]:
            md += ["| iter | A / B (pole + each ear's tag) | unfused hears | music hears | cos→anchor |",
                   "|---:|---|---|---|---:|"]
            for r in rows:
                hu = ", ".join(f"{l}" for l, _ in r["heard_unfused"])
                hm = ", ".join(f"{l}" for l, _ in r["heard_music"])
                pa = (r["prompt_a"] or "∅")[:28]
                pb = (r["prompt_b"] or "∅")[:28]
                md.append(f"| {r['iter']} | {pa} / {pb} | {hu} | {hm} | {r['cos_to_anchor']:+.3f} |")
        else:
            md += ["| iter | qualities appended | machine hears (top-k) | cos→anchor |",
                   "|---:|---|---|---:|"]
            for r in rows:
                heard = ", ".join(f"{l} ({s:.2f})" for l, s in r["heard"])
                if r["iter"] == 1:
                    q = "— (pure preset render)"
                elif not r["prompt_a"]:
                    q = f"{(r.get('quals') or '∅')[:28]} (→null)"
                else:
                    q = (r.get("quals") or "∅")[:34]
                md.append(f"| {r['iter']} | {q} | {heard} | {r['cos_to_anchor']:+.3f} |")
        md.append("")
    (out_dir / "REPORT.md").write_text("\n".join(md), encoding="utf-8")


def main() -> int:
    global HEADER_A, HEADER_B, ALPHA0, MODEL, DURATION_S, STEPS, CFG, SEED
    global MAGNITUDE, NOISE_SIGMA, INJECTION_MODE
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preset", default=DEFAULT_PRESET,
                    help="path to a .t5p preset; ALL generation params are taken from it")
    ap.add_argument("--iters", type=int, default=10)
    ap.add_argument("--topk", type=int, default=3)
    ap.add_argument("--init-noise", type=float, default=0.5,
                    help="init_audio noise level (0=full carry, 1=text-only); 0.5 = both matter")
    ap.add_argument("--device", default="cpu", choices=["cpu", "mps", "cuda"])
    ap.add_argument("--experiments", nargs="*", default=[
        "run1_both", "run2_onlyB_null", "e6_driftB", "e3_counter", "e5_audioset", "e4_two_ears"])
    ap.add_argument("--out", default="tools/clap_semantic_loop_out")
    args = ap.parse_args()

    preset_name = "(literal fallbacks)"
    preset_path = Path(args.preset).expanduser() if args.preset else None
    if preset_path and preset_path.is_file():
        p = load_preset(preset_path)
        HEADER_A, HEADER_B, ALPHA0 = p["header_a"], p["header_b"], p["alpha0"]
        MODEL, DURATION_S, STEPS, CFG, SEED = p["model"], p["duration"], p["steps"], p["cfg"], p["seed"]
        MAGNITUDE, NOISE_SIGMA, INJECTION_MODE = p["magnitude"], p["noise_sigma"], p["injection_mode"]
        preset_name = p["name"]
        if p["random_seed"]:
            log("NOTE: preset has randomSeed=on; this loop pins the stored seed for reproducibility.")
        if "stable-audio-3" not in MODEL:
            log(f"WARN: preset model '{MODEL}' is not SA3 — the loop needs init_audio (SA3 only). "
                "iter-1 will render but init_audio carry will be ignored by the backend.")
    elif args.preset:
        log(f"WARN: preset not found at {preset_path}; using literal fallback params.")

    if not BACKEND_SCRIPT.is_file():
        log(f"ERROR: backend not found at {BACKEND_SCRIPT}")
        return 1
    out_dir = REPO_ROOT / args.out
    out_dir.mkdir(parents=True, exist_ok=True)

    from transformers import ClapModel, ClapProcessor

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
        log(f"Backend ready. preset='{preset_name}' model={MODEL} dur={DURATION_S}s "
            f"seed={SEED} α0={ALPHA0:+.3f}  collision='{HEADER_A}' × '{HEADER_B}'")
        # Mirror PipeInference's serialisation exactly (SA3 clears semantic_axes/dim_offsets).
        base = {"model": MODEL, "duration": DURATION_S, "steps": STEPS, "cfg_scale": CFG,
                "magnitude": MAGNITUDE, "noise_sigma": NOISE_SIGMA, "start_pos": 0.0,
                "injection_mode": INJECTION_MODE}

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
            elif name == "e6_driftB":
                all_rows[name] = run_loop(
                    client, base, earU, procU, args.device, vembU_timbre, timbre_vocab,
                    args.iters, policy_driftB, "top", args.init_noise, out_dir, name, args.topk)
                vocab_sizes[name] = len(timbre_vocab)
            elif name == "e3_counter":
                all_rows[name] = run_adversarial(
                    client, base, earU, procU, args.device, vembU_timbre, timbre_vocab,
                    args.iters, args.init_noise, out_dir, name, args.topk)
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
                "preset": preset_name, "cfg": CFG, "init_noise": args.init_noise,
                "model": MODEL, "steps": STEPS, "duration_s": DURATION_S, "seed": SEED,
                "magnitude": MAGNITUDE, "noise_sigma": NOISE_SIGMA, "injection_mode": INJECTION_MODE,
                "topk": args.topk, "header_a": HEADER_A, "header_b": HEADER_B,
                "alpha0": ALPHA0, "iters": all_rows[name],
            }, indent=2), encoding="utf-8")

        build_report(out_dir, all_rows, vocab_sizes, args.init_noise, preset_name)
        log(f"\nDone in {time.time()-t0:.0f}s. WAVs + REPORT.md under {out_dir}")
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    sys.exit(main())
