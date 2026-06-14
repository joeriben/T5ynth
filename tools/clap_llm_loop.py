#!/usr/bin/env python3
"""CLAP + LLM closed loop — "CLAP is the ear, the LLM is the interpreter".

Sibling of clap_semantic_loop.py. There, the machine's hearing was fed back as a
RAW TAG-APPEND (pole + ", " + top-k CLAP words) — which only realises the
CLAP-alone modes (affirmative self-variation / homeostasis). The genuinely
generative deconstructive modes — *variation*, *abduction*, *development chain*,
reflexive *contextualisation* — need an INTERPRETER between the ear and the next
prompt. T5ynth already ships one: the prompt-translation LLM is Qwen2.5-1.5B-
Instruct (a general instruction follower, not a narrow MT model), reached over
the same IPC the plugin uses via the new ``interpret`` mode (custom system
prompt). So the loop becomes:

    audio_n --CLAP--> tags_n --LLM(system_prompt=mode)--> prompt_{n+1} --SA3--> audio_{n+1}
       \________________________ init_audio (signal carry) ________________________/

The point is NOT bias detection (that is only ONE of several deconstructive
dimensions). The cybernetic moment is STEERING: what you feed back, and HOW the
interpreter transforms it, decides where the sound goes. Each --mode is a
different interpreter stance:

  variation   Prompt-B variation machine (the UI feature the user endorsed): A is
              the fixed human anchor, B is re-written each round into a fresh
              variation in the same family, guided by what the ear hears.
  abduction   the machine ABDUCTS — leaps from the heard timbres to an unexpected
              real-world scene/source that could make such a sound (Peirce). It
              proposes aesthetic frames the human did not write.
  develop     a development chain toward a --target: each round nudges the sound
              one step from where it is now toward a target character.
  critique    reflexive contextualisation (the ONE bias-adjacent mode, done as
              "expose, don't correct"): re-describe the sound from a different
              listening tradition than the machine's. Available, not run by default.

Ground truth = the preset's own params (load_preset), exactly as in
clap_semantic_loop: iter-1 reproduces the real plugin render, the seed is held
fixed, init_audio carries the signal. Only SA3 supports init_audio.

Read-only except under --out. WAVs are for AUDITION — the cosine-to-anchor column
is a drift diagnostic, not the verdict; the verdict is by ear.

Usage:
    .venv/bin/python tools/clap_llm_loop.py
    .venv/bin/python tools/clap_llm_loop.py --modes variation abduction
    .venv/bin/python tools/clap_llm_loop.py --modes develop --target "a deep underwater bell"
    .venv/bin/python tools/clap_llm_loop.py --preset "~/Library/T5ynth/presets/UCDCAE AI Lab/evolvement03 SA3.t5p"
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_init_audio import PipeClient, encode_init_audio, write_wav  # noqa: E402
import clap_probe as cp  # noqa: E402
# Pure helpers (no module-global dependency) reused from the tag-append sibling.
from clap_semantic_loop import load_preset, analyze  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[1]
BACKEND_SCRIPT = REPO_ROOT / "backend" / "pipe_inference.py"
DEFAULT_PRESET = str(Path.home() / "Library/T5ynth/presets/UCDCAE AI Lab/Creamy-Dreamy SA3.t5p")

UNFUSED = "laion/clap-htsat-unfused"  # the separable control ear (clap_probe canonical)


def log(msg: str) -> None:
    print(msg, file=sys.stderr, flush=True)


# ── the interpreter stances (mode -> system prompt + per-turn user text) ─────
#
# Each factory captures the run's fixed context (anchor A, target) and returns a
# build(tags, prev_b) -> (system_prompt, user_text). The system prompt is the
# stance; the user turn carries the changing state (what was heard, current B).

def _mode_variation(header_a, header_b, target):
    sysp = (
        "You are the Prompt-B variation engine of a text-to-audio synthesizer. "
        f'The fixed identity (Prompt A) is: "{header_a}". '
        "Each turn you receive the current Prompt B and the timbres a machine ear hears "
        "in the latest rendered sound. Write ONE new short Prompt B (3 to 8 words) that "
        "VARIES the current one: keep its spirit and the family of the sound, but shift "
        "the imagery in a fresh musical direction suggested by what is heard. "
        "Reply with ONLY the new prompt - no quotes, no label, no explanation."
    )

    def build(tags, prev_b, recent):
        return sysp, f'Current Prompt B: "{prev_b}"\nHeard now: {tags}'

    return build


def _mode_abduction(header_a, header_b, target):
    sysp = (
        "You are the interpreter of a text-to-audio synthesizer. You are given the bare "
        "timbre words a machine ear heard in a sound, plus the scenes already tried. Make "
        "an abductive leap: name a concrete, unexpected real-world scene or source that "
        "could plausibly PRODUCE such a sound, phrased as ONE short generation prompt "
        "(3 to 8 words). Each turn, leap to a scene CLEARLY DIFFERENT from the ones already "
        "tried - never repeat or lightly reword them. Be surprising but physically "
        "plausible. Reply with ONLY the prompt - no quotes, no label, no explanation."
    )

    def build(tags, prev_b, recent):
        tried = ("\nAlready tried (do not reuse): " + " / ".join(recent)) if recent else ""
        return sysp, f"Heard: {tags}{tried}"

    return build


def _mode_develop(header_a, header_b, target):
    sysp = (
        f'You are steering a sound, step by step, toward a target character: "{target}". '
        "Each turn you receive the timbres currently heard in the sound. Write ONE short "
        "prompt (3 to 8 words) for the NEXT step that nudges the sound from where it is "
        "now a single step toward the target - a step, not a jump. "
        "Reply with ONLY the prompt - no quotes, no label, no explanation."
    )

    def build(tags, prev_b, recent):
        return sysp, f"Heard now: {tags}\nNext step:"

    return build


def _mode_critique(header_a, header_b, target):
    sysp = (
        "You are a reflexive interpreter for a text-to-audio synthesizer. You are given "
        "the words a machine ear (trained mostly on Western audio datasets) used to "
        "describe a sound. In ONE short prompt (3 to 10 words), re-describe the SAME "
        "sound from a different listening tradition or framing than the machine's - not "
        "correcting it, but offering another ear. "
        "Reply with ONLY the prompt - no quotes, no label, no explanation."
    )

    def build(tags, prev_b, recent):
        return sysp, f"Machine heard: {tags}"

    return build


MODES = {
    "variation": _mode_variation,
    "abduction": _mode_abduction,
    "develop": _mode_develop,
    "critique": _mode_critique,
}


def _clean_prompt(s: str, max_chars: int = 140) -> str:
    """Small instruct models leak quotes/labels/extra lines; keep the first real
    line as a bare phrase."""
    s = (s or "").strip()
    first = ""
    for line in s.splitlines():
        line = line.strip()
        if line:
            first = line
            break
    if not first:
        return ""
    s = first.strip().strip("`").strip()
    low = s.lower()
    for lab in ("prompt b:", "new prompt b:", "new prompt:", "prompt:", "next:",
                "output:", "answer:"):
        if low.startswith(lab):
            s = s[len(lab):].strip()
            break
    # strip wrapping quotes (straight or curly, incl. mismatched open/close pairs)
    s = s.strip().strip("\"'“”‘’").strip()
    if len(s) > max_chars:
        s = s[:max_chars].rsplit(" ", 1)[0].strip()
    return s


def generate(client, base, prompt_a, prompt_b, alpha, seed, prev_audio, sr, init_noise):
    """One SA3 generation at a fixed seed; prev_audio (None on iter 1) feeds back
    as init_audio. Mirrors clap_semantic_loop.generate but with an explicit seed."""
    req = {**base, "prompt_a": prompt_a, "prompt_b": prompt_b,
           "alpha": float(alpha), "seed": int(seed)}
    if prev_audio is not None:
        req.update({
            "init_audio_b64": encode_init_audio(prev_audio),
            "init_audio_sr": sr,
            "init_audio_channels": prev_audio.shape[0],
            "init_noise_level": init_noise,
        })
    res = client.request(req)
    return res["audio"], res["sample_rate"]


def interpret(client, system_prompt, user_text, max_new, device):
    """Ask the instruct LLM (Qwen) for the next prompt over the IPC interpret mode."""
    return client.request_text({
        "mode": "interpret",
        "system_prompt": system_prompt,
        "prompt_a": user_text,
        "max_new_tokens": int(max_new),
        "device": device,
    })


def run_llm_loop(client, base, ear, proc, device, vocab_emb, vocab_labels, *,
                 mode_build, header_a, header_b, alpha0, seed, init_noise,
                 llm_device, iters, topk, max_new, out_dir, name):
    """Collision-anchored loop where the LLM (not a raw tag-append) writes the next
    Prompt B. A is the fixed human anchor; iter 1 = the pure preset render."""
    exp_dir = out_dir / name
    exp_dir.mkdir(parents=True, exist_ok=True)
    prompt_a, prompt_b, alpha = header_a, header_b, alpha0   # iter1 = pure collision
    prev_audio, sr, anchor_emb = None, None, None
    b_history = []   # Prompt B used so far → anti-stasis memory (abduction "don't reuse")
    rows = []
    for it in range(1, iters + 1):
        audio, sr = generate(client, base, prompt_a, prompt_b, alpha, seed,
                             prev_audio, sr, init_noise)
        b_history.append(prompt_b)
        labels, emb = analyze(ear, proc, audio, sr, device, vocab_emb, vocab_labels, topk)
        if anchor_emb is None:
            anchor_emb = emb
        cos_anchor = float(emb @ anchor_emb)
        write_wav(exp_dir / f"iter{it:02d}.wav", audio, sr)
        tags = ", ".join(l for l, _ in labels)

        next_b = None
        if it < iters:  # no need to interpret after the last render
            recent = b_history[-3:]   # scenes used so far (incl. current) → anti-stasis
            sysp, usr = mode_build(tags, prompt_b, recent)
            raw = interpret(client, sysp, usr, max_new, llm_device)
            next_b = _clean_prompt(raw) or prompt_b  # fall back to current B if blank

        rows.append({
            "iter": it, "prompt_a": prompt_a, "prompt_b": prompt_b,
            "alpha": round(float(alpha), 4),
            "init_noise": (None if it == 1 else init_noise),
            "heard": labels, "llm_next_b": next_b,
            "cos_to_anchor": round(cos_anchor, 4),
        })
        log(f"  [{name}] it{it:02d} B='{prompt_b[:34]}' "
            f"-> hears {', '.join(f'{l}({s:.2f})' for l, s in labels)}  "
            f"cos0={cos_anchor:+.3f}" + (f"  ⇒ B'='{next_b[:40]}'" if next_b else ""))

        if next_b is not None:
            prompt_b = next_b      # A stays the anchor; alpha stays the collision blend
        prev_audio = audio
    return rows


def build_report(out_dir, all_rows, init_noise, preset_name, model, dur, steps,
                 cfg, seed, header_a, alpha0, target):
    md = [
        "# CLAP + LLM closed loop — the ear hears, the interpreter steers",
        "",
        "Each iteration: SA3 generates, **CLAP** ranks the output into top-k timbre "
        "words, and the **instruct LLM** (Qwen2.5-1.5B, the prompt translator reused via "
        "the `interpret` IPC mode) transforms those words — per the mode's stance — into "
        "the next Prompt B. The audio also carries forward via init_audio "
        f"(init_noise={init_noise}). Generation params are loaded VERBATIM from the preset.",
        "",
        f"- preset: **{preset_name}**, model `{model}`",
        f"- duration **{dur}s**, {steps} steps, CFG {cfg}, **seed {seed} fixed across iters**",
        f"- anchor Prompt A (fixed): `{header_a}`  ·  collision α={alpha0:+.3f}",
        f"- develop target (if used): `{target}`",
        "",
        "The cosine-to-anchor is a **drift diagnostic**, not the verdict — listen to the "
        "WAVs under each mode dir. The interesting column is **`LLM wrote next B`**: that "
        "is the interpreter at work, the thing a raw CLAP tag-append cannot do.",
        "",
    ]
    notes = {
        "variation": "Prompt-B variation machine (A = fixed human anchor). The LLM "
                     "re-writes B each round into a fresh variation in the same family, "
                     "guided by what the ear hears.",
        "abduction": "The machine ABDUCTS: from the bare heard timbres it leaps to an "
                     "unexpected real-world scene that could make such a sound — proposing "
                     "an aesthetic frame the human never wrote.",
        "develop": f"Development chain toward a target (`{target}`): each round the LLM "
                   "nudges the sound one step from where it is now toward the target.",
        "critique": "Reflexive contextualisation (the one bias-adjacent stance, as "
                    "'expose, don't correct'): re-describe the sound from a different "
                    "listening tradition than the machine's.",
    }
    for name, rows in all_rows.items():
        if not rows:
            continue
        c0, cl = rows[0]["cos_to_anchor"], rows[-1]["cos_to_anchor"]
        md += [f"## {name}", "", f"*{notes.get(name, '')}*", "",
               f"CLAP cosine to iter-1 anchor: {c0:+.3f} → {cl:+.3f} (drift {1.0 - cl:+.3f}).",
               "",
               "| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |",
               "|---:|---|---|---|"]
        for r in rows:
            heard = ", ".join(f"{l}" for l, _ in r["heard"])
            pb = (r["prompt_b"] or "∅")[:40]
            nb = (r["llm_next_b"] or "—")[:48]
            md.append(f"| {r['iter']} | {pb} | {heard} | {nb} |")
        md.append("")
    (out_dir / "REPORT.md").write_text("\n".join(md), encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preset", default=DEFAULT_PRESET,
                    help="path to a .t5p; ALL generation params are taken from it")
    ap.add_argument("--modes", nargs="*", default=["variation", "abduction", "develop"],
                    choices=list(MODES.keys()))
    ap.add_argument("--target", default="a deep slow underwater bell",
                    help="target character for --modes develop")
    ap.add_argument("--iters", type=int, default=8)
    ap.add_argument("--topk", type=int, default=5, help="CLAP tags handed to the LLM")
    ap.add_argument("--max-new", type=int, default=64, help="LLM max new tokens per prompt")
    ap.add_argument("--init-noise", type=float, default=0.9,
                    help="init_audio noise level (0=full carry, 1=text-only). Default 0.9: "
                         "measured (tools/diag_promptbite.py) that at 0.5 the carry DOMINATES "
                         "and suppresses the prompt — every iteration/mode collapses onto the "
                         "shared iter-1, so the interpreter is inaudible. 0.9 lets the words "
                         "drive (signal-continuity vs promptability is the tradeoff this dials).")
    ap.add_argument("--alpha", type=float, default=None,
                    help="override the preset blend toward the interpreter pole B: "
                         "+1=pure B, +0.8≈0.9·B+0.1·A, 0=50/50, -1=pure A. The preset's "
                         "near-0 alpha lets the fixed A anchor dominate; push toward B to "
                         "hear what the LLM actually does.")
    ap.add_argument("--device", default="cpu", choices=["cpu", "mps", "cuda"],
                    help="device for CLAP (generation uses the backend default)")
    ap.add_argument("--llm-device", default="cpu", choices=["cpu", "mps", "cuda"],
                    help="device for the interpret LLM; cpu keeps it off the SA3 (mps) "
                         "memory pool so neither evicts the other")
    ap.add_argument("--out", default="tools/clap_llm_loop_out")
    args = ap.parse_args()

    # ── preset params verbatim (iter-1 must reproduce the real plugin render) ──
    preset_path = Path(args.preset).expanduser() if args.preset else None
    if not (preset_path and preset_path.is_file()):
        log(f"ERROR: preset not found at {preset_path}")
        return 1
    p = load_preset(preset_path)
    header_a, header_b, alpha0 = p["header_a"], p["header_b"], p["alpha0"]
    model, dur, steps, cfg, seed = p["model"], p["duration"], p["steps"], p["cfg"], p["seed"]
    magnitude, noise_sigma, injection = p["magnitude"], p["noise_sigma"], p["injection_mode"]
    preset_name = p["name"]
    if args.alpha is not None:
        alpha0 = args.alpha   # push the blend toward B so the interpreter pole drives the sound
        log(f"alpha override: blend α={alpha0:+.3f} (preset was {p['alpha0']:+.3f})")
    if "stable-audio-3" not in model:
        log(f"ERROR: preset model '{model}' is not SA3 — the loop needs init_audio (SA3 only).")
        return 1
    if p["random_seed"]:
        log("NOTE: preset randomSeed=on; pinning the stored seed for reproducibility.")

    if not BACKEND_SCRIPT.is_file():
        log(f"ERROR: backend not found at {BACKEND_SCRIPT}")
        return 1
    out_dir = REPO_ROOT / args.out
    out_dir.mkdir(parents=True, exist_ok=True)

    # ── CLAP ear (single, separable control ear) ──
    from transformers import ClapModel, ClapProcessor
    timbre_vocab = list(dict.fromkeys(cp.NAIVE_VOCAB))
    log(f"Loading CLAP {UNFUSED} on {args.device} ...")
    ear = ClapModel.from_pretrained(UNFUSED).to(args.device).eval()
    proc = ClapProcessor.from_pretrained(UNFUSED)
    cp.assert_model_sane(ear, proc, args.device)
    vocab_emb = cp.embed_texts(ear, proc, timbre_vocab, args.device)

    t0 = time.time()
    client = PipeClient([sys.executable, str(BACKEND_SCRIPT)])
    all_rows: dict[str, list] = {}
    try:
        models = client.info.get("models", [])
        if model not in models:
            log(f"ERROR: {model} not available; backend has {models}")
            return 1
        log(f"Backend ready. preset='{preset_name}' model={model} dur={dur}s seed={seed} "
            f"α0={alpha0:+.3f}  anchor A='{header_a}'  B0='{header_b}'")
        # Mirror PipeInference serialisation (SA3 clears semantic_axes/dim_offsets).
        base = {"model": model, "duration": dur, "steps": steps, "cfg_scale": cfg,
                "magnitude": magnitude, "noise_sigma": noise_sigma, "start_pos": 0.0,
                "injection_mode": injection}

        for name in args.modes:
            log(f"\n== {name} ==")
            mode_build = MODES[name](header_a, header_b, args.target)
            all_rows[name] = run_llm_loop(
                client, base, ear, proc, args.device, vocab_emb, timbre_vocab,
                mode_build=mode_build, header_a=header_a, header_b=header_b,
                alpha0=alpha0, seed=seed, init_noise=args.init_noise,
                llm_device=args.llm_device, iters=args.iters, topk=args.topk,
                max_new=args.max_new, out_dir=out_dir, name=name)
            (out_dir / name / "manifest.json").write_text(json.dumps({
                "name": name, "preset": preset_name, "model": model,
                "duration_s": dur, "steps": steps, "cfg": cfg, "seed": seed,
                "magnitude": magnitude, "noise_sigma": noise_sigma,
                "injection_mode": injection, "init_noise": args.init_noise,
                "topk": args.topk, "max_new_tokens": args.max_new,
                "header_a": header_a, "header_b": header_b, "alpha0": alpha0,
                "target": args.target, "llm": "qwen2.5-1.5b-instruct",
                "iters": all_rows[name],
            }, indent=2), encoding="utf-8")

        build_report(out_dir, all_rows, args.init_noise, preset_name, model, dur,
                     steps, cfg, seed, header_a, alpha0, args.target)
        log(f"\nDone in {time.time()-t0:.0f}s. WAVs + REPORT.md under {out_dir}")
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    sys.exit(main())
