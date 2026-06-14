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

Three COUPLING topologies (--couple) set how far the machine displaces the human
input — one axis, from "anchor held" to "anchor broken":

  alpha   A is the fixed human anchor; only B is rewritten (replace). α sets B's
          authority over the blend. (the status quo)
  concat  BOTH poles: the human original is KEPT and each round's interpretation is
          APPENDED (machine layers onto the human input — the anchor dilutes but is
          never erased). "concat, abgeschwächt".
  voll    BOTH poles REPLACED by two interpreter stances (--stance-a × --stance-b):
          the human input is broken; α now blends two machine self-readings — a
          machine-internal A/B collision, echoing the preset A/B collisions.

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


# t5gemma (SA3 text encoder) budgets ~256 tokens PER pole. 140 words ≈ ~200 tokens
# leaves margin for the "TrackType: Music, ..." modality prefix — generous enough
# that an 8-iter run never trims; the guard exists for long runs.
CONCAT_MAX_WORDS = 140

COUPLE_NOTES = {
    "alpha": "A is the fixed human anchor; only B is rewritten (replace). α sets B's "
             "authority over the blend.",
    "concat": "BOTH poles: the human original is KEPT and each round's interpretation "
              "is APPENDED — the machine layers onto the human input, the anchor "
              "dilutes but is never erased.",
    "voll": "BOTH poles REPLACED by two interpreter stances — the human input is "
            "broken; α blends two machine self-readings (machine-internal A/B collision).",
}


def _concat(glieder, max_words: int = CONCAT_MAX_WORDS) -> str:
    """Join prompt parts comma-separated for the concat coupling. ``glieder[0]`` is
    the human original (the anchor — NEVER dropped); the rest are machine
    interpretations appended over iterations. If the whole would exceed the
    t5gemma word budget, drop the OLDEST machine glieder first so the original
    always survives (no silent tail-truncation by the tokenizer)."""
    if not glieder:
        return ""
    original, machine = glieder[0], list(glieder[1:])
    while machine and len((original + " " + " ".join(machine)).split()) > max_words:
        machine.pop(0)
    return ", ".join([original, *machine]) if machine else original


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
                 coupling, build_a, build_b, header_a, header_b, alpha0, seed,
                 init_noise, llm_device, iters, topk, max_new, out_dir, name):
    """Collision-anchored loop where the LLM (not a raw tag-append) writes the next
    prompt(s). iter 1 = the pure preset render. ``coupling`` decides how far the
    machine displaces the human input:
      alpha  — A fixed, only B rewritten (replace); ``build_a`` is None.
      voll   — both poles replaced by ``build_a`` / ``build_b``.
      concat — both poles: original kept, each interpretation appended (diluted).
    ``*_glieder[0]`` is always the human original; the anti-stasis ``recent`` for a
    stance is that pole's last 3 glieder (identical to the old b_history sequence in
    the alpha path, so the verified abduction fix is preserved byte-for-byte)."""
    exp_dir = out_dir / name
    exp_dir.mkdir(parents=True, exist_ok=True)
    dual = build_a is not None
    prompt_a, prompt_b, alpha = header_a, header_b, alpha0   # iter1 = pure collision
    prev_audio, sr, anchor_emb = None, None, None
    a_glieder = [header_a]   # glied[0] = human original (kept in concat); rest = machine
    b_glieder = [header_b]
    rows = []
    for it in range(1, iters + 1):
        audio, sr = generate(client, base, prompt_a, prompt_b, alpha, seed,
                             prev_audio, sr, init_noise)
        labels, emb = analyze(ear, proc, audio, sr, device, vocab_emb, vocab_labels, topk)
        if anchor_emb is None:
            anchor_emb = emb
        cos_anchor = float(emb @ anchor_emb)
        write_wav(exp_dir / f"iter{it:02d}.wav", audio, sr)
        tags = ", ".join(l for l, _ in labels)

        next_a = next_b = None
        if it < iters:  # no need to interpret after the last render
            sysp_b, usr_b = build_b(tags, prompt_b, b_glieder[-3:])
            next_b = _clean_prompt(interpret(client, sysp_b, usr_b, max_new, llm_device)) \
                or b_glieder[-1]
            if dual:
                sysp_a, usr_a = build_a(tags, prompt_a, a_glieder[-3:])
                next_a = _clean_prompt(interpret(client, sysp_a, usr_a, max_new, llm_device)) \
                    or a_glieder[-1]

        rows.append({
            "iter": it, "coupling": coupling,
            "prompt_a": prompt_a, "prompt_b": prompt_b,
            "alpha": round(float(alpha), 4),
            "init_noise": (None if it == 1 else init_noise),
            "heard": labels, "llm_next_a": next_a, "llm_next_b": next_b,
            "cos_to_anchor": round(cos_anchor, 4),
        })
        msg = (f"  [{name}] it{it:02d} " + (f"A='{prompt_a[:22]}' " if dual else "")
               + f"B='{prompt_b[:26]}' -> "
               + ", ".join(f"{l}({s:.2f})" for l, s in labels) + f"  cos0={cos_anchor:+.3f}")
        if next_b:
            msg += f"  ⇒B'='{next_b[:26]}'"
        if dual and next_a:
            msg += f" A'='{next_a[:22]}'"
        log(msg)

        # apply the interpretation(s) for the NEXT iteration, per coupling
        if next_b is not None:
            b_glieder.append(next_b)
            prompt_b = _concat(b_glieder) if coupling == "concat" else next_b
        if dual and next_a is not None:
            a_glieder.append(next_a)
            prompt_a = _concat(a_glieder) if coupling == "concat" else next_a
        prev_audio = audio
    return rows


def build_report(out_dir, all_rows, init_noise, preset_name, model, dur, steps,
                 cfg, seed, header_a, header_b, alpha0, target, stance_a, stance_b):
    md = [
        "# CLAP + LLM closed loop — the ear hears, the interpreter steers",
        "",
        "Each iteration: SA3 generates, **CLAP** ranks the output into top-k timbre "
        "words, and the **instruct LLM** (Qwen2.5-1.5B, the prompt translator reused via "
        "the `interpret` IPC mode) transforms those words — per the stance — into the "
        "next prompt(s). The audio also carries forward via init_audio "
        f"(init_noise={init_noise}). Generation params are loaded VERBATIM from the preset.",
        "",
        f"- preset: **{preset_name}**, model `{model}`",
        f"- duration **{dur}s**, {steps} steps, CFG {cfg}, **seed {seed} fixed across iters**",
        f"- Prompt A: `{header_a}`  ·  Prompt B: `{header_b}`  ·  collision α={alpha0:+.3f}",
        f"- dual-coupling stances: A = **{stance_a}** × B = **{stance_b}**  ·  "
        f"develop target (if used): `{target}`",
        "",
        "The **coupling** (one axis: how far the machine displaces the human input):",
        f"- **alpha** — {COUPLE_NOTES['alpha']}",
        f"- **concat** — {COUPLE_NOTES['concat']}",
        f"- **voll** — {COUPLE_NOTES['voll']}",
        "",
        "The cosine-to-anchor is a **drift diagnostic**, not the verdict — listen to the "
        "WAVs under each dir. The interesting columns are what the **interpreter wrote** "
        "— the thing a raw CLAP tag-append cannot do.",
        "",
    ]
    for name, rows in all_rows.items():
        if not rows:
            continue
        coupling = rows[0].get("coupling", "alpha")
        dual = coupling in ("concat", "voll")
        c0, cl = rows[0]["cos_to_anchor"], rows[-1]["cos_to_anchor"]
        md += [f"## {name}", "",
               f"*coupling **{coupling}** — {COUPLE_NOTES.get(coupling, '')}*", "",
               f"CLAP cosine to iter-1 anchor: {c0:+.3f} → {cl:+.3f} (drift {1.0 - cl:+.3f}).",
               ""]
        if dual:
            md += ["| iter | Prompt A (rendered) | Prompt B (rendered) | hears (top-k) "
                   "| next A | next B |", "|---:|---|---|---|---|---|"]
            for r in rows:
                heard = ", ".join(l for l, _ in r["heard"])
                md.append(f"| {r['iter']} | {(r['prompt_a'] or '∅')[:32]} "
                          f"| {(r['prompt_b'] or '∅')[:32]} | {heard} "
                          f"| {(r['llm_next_a'] or '—')[:28]} | {(r['llm_next_b'] or '—')[:28]} |")
        else:
            md += ["| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |",
                   "|---:|---|---|---|"]
            for r in rows:
                heard = ", ".join(l for l, _ in r["heard"])
                md.append(f"| {r['iter']} | {(r['prompt_b'] or '∅')[:40]} | {heard} "
                          f"| {(r['llm_next_b'] or '—')[:48]} |")
        md.append("")
    (out_dir / "REPORT.md").write_text("\n".join(md), encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preset", default=DEFAULT_PRESET,
                    help="path to a .t5p; ALL generation params are taken from it")
    ap.add_argument("--modes", nargs="*", default=["variation", "abduction"],
                    choices=list(MODES.keys()),
                    help="B-pole stances run under the alpha coupling (one dir each). "
                         "develop is retired (it needs a non-arbitrary, prompt-derived "
                         "trajectory) — still callable explicitly, not in the default.")
    ap.add_argument("--couple", nargs="*", default=["alpha"],
                    choices=["alpha", "concat", "voll"],
                    help="coupling topology — how far the machine displaces the human "
                         "input. alpha: A fixed, only B rewritten (status quo, uses "
                         "--modes for the B stance[s]). concat: both poles kept + each "
                         "interpretation appended (machine layers on, anchor survives). "
                         "voll: both poles replaced by --stance-a × --stance-b (human "
                         "input broken, machine-internal A/B collision). Pass several to "
                         "compare side by side.")
    ap.add_argument("--stance-a", default="variation", choices=list(MODES.keys()),
                    help="interpreter stance for the A pole in dual couplings (concat/voll)")
    ap.add_argument("--stance-b", default="abduction", choices=list(MODES.keys()),
                    help="interpreter stance for the B pole in dual couplings (concat/voll)")
    ap.add_argument("--target", default="a deep slow underwater bell",
                    help="target character for the (retired) develop stance")
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

        # build the run list: alpha expands over --modes (legacy, dir = stance name);
        # concat/voll are single dual runs (dir = coupling name).
        specs = []
        multi = len(args.couple) > 1
        for couple in args.couple:
            if couple == "alpha":
                for m in args.modes:
                    nm = f"alpha_{m}" if multi else m
                    specs.append((nm, "alpha", None,
                                  MODES[m](header_a, header_b, args.target), None, m))
            else:
                specs.append((couple, couple,
                              MODES[args.stance_a](header_a, header_b, args.target),
                              MODES[args.stance_b](header_a, header_b, args.target),
                              args.stance_a, args.stance_b))

        for nm, coupling, build_a, build_b, sa, sb in specs:
            log(f"\n== {nm}  (coupling={coupling}, A={sa or 'fixed'} × B={sb}) ==")
            all_rows[nm] = run_llm_loop(
                client, base, ear, proc, args.device, vocab_emb, timbre_vocab,
                coupling=coupling, build_a=build_a, build_b=build_b,
                header_a=header_a, header_b=header_b, alpha0=alpha0, seed=seed,
                init_noise=args.init_noise, llm_device=args.llm_device,
                iters=args.iters, topk=args.topk, max_new=args.max_new,
                out_dir=out_dir, name=nm)
            (out_dir / nm / "manifest.json").write_text(json.dumps({
                "name": nm, "coupling": coupling, "stance_a": sa, "stance_b": sb,
                "preset": preset_name, "model": model,
                "duration_s": dur, "steps": steps, "cfg": cfg, "seed": seed,
                "magnitude": magnitude, "noise_sigma": noise_sigma,
                "injection_mode": injection, "init_noise": args.init_noise,
                "topk": args.topk, "max_new_tokens": args.max_new,
                "header_a": header_a, "header_b": header_b, "alpha0": alpha0,
                "target": args.target, "llm": "qwen2.5-1.5b-instruct",
                "iters": all_rows[nm],
            }, indent=2), encoding="utf-8")

        build_report(out_dir, all_rows, args.init_noise, preset_name, model, dur,
                     steps, cfg, seed, header_a, header_b, alpha0, args.target,
                     args.stance_a, args.stance_b)
        log(f"\nDone in {time.time()-t0:.0f}s. WAVs + REPORT.md under {out_dir}")
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    sys.exit(main())
