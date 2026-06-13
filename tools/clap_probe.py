#!/usr/bin/env python3
"""CLAP feasibility probe for the semantic resynth loop.

Background
----------
T5ynth's existing resynth loop is a *signal* loop: Audio -> init_audio (SA3) ->
Audio. The proposed *semantic* loop instead re-describes the output in words and
lets those words steer the next generation:

    Audio -> CLAP audio embedding -> nearest tags from a vocabulary
          -> prompt -> T5 encoder -> Audio

CLAP cannot caption (no language decoder); it *ranks* audio against a candidate
text vocabulary in a shared embedding space. So the whole idea hinges on two
empirical questions this probe answers, before any C++/wiring is touched:

  Q1  Does CLAP produce *sensible* top-K tags for real T5ynth synth textures?
  Q2  Which *vocabulary* gives sensible AND separable rankings?
        (a) musiccaps  - mined from MusicCaps `aspect_list` (real musical tags)
        (b) audioset   - the 632-class AudioSet ontology (sound-event labels)
        (c) naive      - a generated adjective list with deliberate synonym
                         clusters (the "ask an LLM for sound words" strawman)

It also checks a bonus question the loop depends on:

  Q3  Does CLAP *hear* the resynth drift? (audio-embedding cosine of each
      resynth variant to the anchor should fall as sigma/iter rise.)

Resolution is bounded by label *separability* in CLAP space, not list length, so
the probe reports redundancy (mean pairwise text-embedding cosine) and
discriminability (top1-top2 / top1-top5 margins) per vocabulary.

Uses CLAP via HF transformers (no `laion_clap` pip package needed). Default is
`laion/clap-htsat-unfused` (the canonical, well-tested checkpoint). NOTE: the
music-tuned `laion/larger_clap_music` HF port is BROKEN — its text pooler
collapses (all texts map to one direction, cross-modal alignment absent), so it
silently produces garbage rankings; `assert_model_sane()` below catches that
class of bug. Its sibling `laion/larger_clap_music_and_speech`, however, is
INTACT (passes the gate) — a working music-tuned option via HF, no native
`laion_clap` loader needed. It trades better musical-timbre fidelity for worse
vocabulary separability (more anisotropic). Pure read-only: writes only under --out.

Usage
-----
    .venv/bin/python tools/clap_probe.py
    .venv/bin/python tools/clap_probe.py --audio a.wav b.wav --template "This is a {} sound."
"""

from __future__ import annotations

import argparse
import ast
import collections
import csv
import json
import sys
import urllib.request
from pathlib import Path

import numpy as np
import soundfile as sf
import torch
import torchaudio
from transformers import ClapModel, ClapProcessor


ROOT = Path(__file__).resolve().parents[1]
CLAP_SR = 48_000
ONTOLOGY_URL = "https://raw.githubusercontent.com/audioset/ontology/master/ontology.json"

# Real T5ynth outputs from the resynth-loop experiment make the ideal probe set:
# a clean anchor plus drift variants across the sigma/iter sweep.
DEFAULT_AUDIO_CANDIDATES = [
    "tools/resynth_loop_out/original.wav",
    "tools/resynth_loop_out/anchor_family.wav",
    "tools/resynth_loop_out/sigma0.050_iter20.wav",
    "tools/resynth_loop_out/sigma0.200_iter10.wav",
    "tools/resynth_loop_out/sigma0.300_iter15.wav",
    "tools/resynth_loop_out/sigma0.500_iter10.wav",
    "resources/test_sample.wav",
]
ANCHOR_BASENAME = "original.wav"

# (c) The naive strawman: grouped by perceptual axis with heavy near-synonym
# clusters on purpose, so the redundancy metric has something to expose.
NAIVE_VOCAB = [
    # brightness
    "bright", "brilliant", "sparkly", "shimmering", "glittering", "glassy",
    "crystalline", "shiny",
    # darkness
    "dark", "murky", "muddy", "gloomy", "dull", "shadowy", "dim",
    # warmth
    "warm", "cozy", "round", "mellow", "smooth", "velvety", "lush",
    # cold
    "cold", "icy", "sterile", "clinical", "brittle",
    # metallic
    "metallic", "tinny", "clangy", "brassy", "steely", "bell-like", "chrome",
    # harsh
    "harsh", "abrasive", "gritty", "rough", "coarse", "raspy", "scratchy",
    "sharp",
    # soft
    "soft", "gentle", "airy", "breathy", "feathery", "delicate",
    # aggressive
    "aggressive", "punchy", "biting", "cutting", "piercing", "screaming",
    # thick
    "thick", "fat", "heavy", "dense", "massive", "huge", "boomy",
    # thin
    "thin", "weak", "hollow", "reedy", "nasal",
    # organic
    "organic", "woody", "earthy", "natural", "acoustic",
    # digital
    "digital", "synthetic", "artificial", "plasticky", "robotic",
    # noisy
    "noisy", "fuzzy", "distorted", "dirty", "saturated", "crunchy",
    # clean
    "clean", "pure", "pristine", "clear", "transparent",
    # moody
    "dreamy", "ethereal", "haunting", "eerie", "mysterious", "ambient",
    "atmospheric", "cinematic",
    # motion
    "pulsing", "throbbing", "wobbling", "undulating", "evolving", "swirling",
    "morphing",
    # texture extras
    "granular", "glitchy", "watery", "liquid", "gaseous", "percussive",
    "resonant", "droning", "buzzing", "humming",
]


# --------------------------------------------------------------------------- #
# Vocabularies
# --------------------------------------------------------------------------- #
def load_musiccaps_vocab(top_n: int) -> list[str]:
    """Mine the `aspect_list` column of MusicCaps for the top-N musical tags."""
    from huggingface_hub import hf_hub_download

    csv_path = hf_hub_download(
        "google/MusicCaps", "musiccaps-public.csv", repo_type="dataset"
    )
    counter: collections.Counter[str] = collections.Counter()
    with open(csv_path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        if "aspect_list" not in (reader.fieldnames or []):
            raise RuntimeError(
                f"MusicCaps CSV missing 'aspect_list'; cols={reader.fieldnames}"
            )
        for row in reader:
            raw = (row.get("aspect_list") or "").strip()
            if not raw:
                continue
            aspects: list[str]
            try:
                parsed = ast.literal_eval(raw)
                aspects = list(parsed) if isinstance(parsed, (list, tuple)) else [str(parsed)]
            except (ValueError, SyntaxError):
                aspects = [a for a in raw.split(",")]
            for a in aspects:
                tag = str(a).strip().strip("'\"").lower()
                if tag:
                    counter[tag] += 1
    return [tag for tag, _ in counter.most_common(top_n)]


def load_audioset_vocab() -> list[str]:
    """The AudioSet ontology display names (632 sound-event labels)."""
    with urllib.request.urlopen(ONTOLOGY_URL, timeout=30) as r:
        data = json.load(r)
    # Dedupe while preserving order.
    seen: set[str] = set()
    names: list[str] = []
    for d in data:
        name = str(d.get("name", "")).strip()
        if name and name.lower() not in seen:
            seen.add(name.lower())
            names.append(name)
    return names


# --------------------------------------------------------------------------- #
# Audio + CLAP
# --------------------------------------------------------------------------- #
def load_audio_48k_mono(path: Path) -> np.ndarray:
    wav, sr = sf.read(str(path), dtype="float32", always_2d=True)  # (frames, ch)
    mono = wav.mean(axis=1)
    t = torch.from_numpy(mono)
    if sr != CLAP_SR:
        t = torchaudio.functional.resample(t, sr, CLAP_SR)
    return t.numpy()


@torch.no_grad()
def embed_texts(model, processor, texts: list[str], device: str, batch: int = 64) -> torch.Tensor:
    chunks = []
    for i in range(0, len(texts), batch):
        inputs = processor(text=texts[i : i + batch], return_tensors="pt", padding=True)
        inputs = {k: v.to(device) for k, v in inputs.items()}
        chunks.append(model.get_text_features(**inputs).cpu())
    emb = torch.cat(chunks, 0)
    return torch.nn.functional.normalize(emb, dim=-1)


@torch.no_grad()
def embed_audios(model, processor, audios: list[np.ndarray], device: str) -> torch.Tensor:
    inputs = processor(audio=audios, sampling_rate=CLAP_SR, return_tensors="pt", padding=True)
    inputs = {k: v.to(device) for k, v in inputs.items()}
    emb = model.get_audio_features(**inputs).cpu()
    return torch.nn.functional.normalize(emb, dim=-1)


@torch.no_grad()
def assert_model_sane(model, processor, device: str) -> None:
    """Fail LOUD if the model's towers are degenerate or cross-modally misaligned.

    Catches the laion/larger_clap_music HF-port bug (collapsed text pooler →
    silent garbage). A working CLAP must rank a sine-tone caption higher on sine
    audio than on noise, and vice versa, and must not map all texts to one point.
    """
    sr = CLAP_SR
    t = np.arange(sr * 3) / sr
    sine = (0.5 * np.sin(2 * np.pi * 440 * t)).astype("float32")
    rng = np.random.default_rng(0)
    noise = (0.1 * rng.standard_normal(sr * 3)).astype("float32")
    ae = embed_audios(model, processor, [sine, noise], device)
    te = embed_texts(
        model, processor,
        ["a steady pure sine wave tone", "harsh white noise static hiss"],
        device,
    )
    sim = te @ ae.T  # rows: [sine-text, noise-text]; cols: [SINE, NOISE]
    tt = (te @ te.T)[0, 1].item()
    sine_ok = (sim[0, 0] - sim[0, 1]).item() > 0.1
    noise_ok = (sim[1, 1] - sim[1, 0]).item() > 0.1
    if not (sine_ok and noise_ok and tt < 0.95):
        raise SystemExit(
            "[probe] ABORT: model failed the sine-vs-noise sanity check — its "
            "text/audio towers are misaligned or degenerate.\n"
            f"  sine-text · [sine, noise] audio = [{sim[0,0]:.3f}, {sim[0,1]:.3f}]\n"
            f"  noise-text · [sine, noise] audio = [{sim[1,0]:.3f}, {sim[1,1]:.3f}]\n"
            f"  text-text cosine = {tt:.3f} (degenerate if ~1.0)\n"
            "  This is the laion/larger_clap_music HF-port bug class; the default "
            "laion/clap-htsat-unfused passes."
        )
    print(
        f"[probe] model sanity OK (sine {sim[0,0]:.2f}/{sim[0,1]:.2f}, "
        f"noise {sim[1,0]:.2f}/{sim[1,1]:.2f}, text-text {tt:.2f})"
    )


# --------------------------------------------------------------------------- #
# Metrics
# --------------------------------------------------------------------------- #
def redundancy(text_emb: torch.Tensor) -> tuple[float, float]:
    """Mean pairwise cosine and fraction of pairs > 0.9 (collinearity)."""
    n = text_emb.shape[0]
    s = text_emb @ text_emb.T
    iu = torch.triu_indices(n, n, offset=1)
    vals = s[iu[0], iu[1]]
    return vals.mean().item(), (vals > 0.9).float().mean().item()


def rank(audio_emb: torch.Tensor, text_emb: torch.Tensor):
    """Return (sorted_scores [A,N], sorted_idx [A,N]) by descending cosine."""
    sims = audio_emb @ text_emb.T
    vals, idx = sims.sort(dim=-1, descending=True)
    return vals, idx


# --------------------------------------------------------------------------- #
# Main
# --------------------------------------------------------------------------- #
def resolve_audio(args) -> list[Path]:
    if args.audio:
        paths = [Path(a) if Path(a).is_absolute() else ROOT / a for a in args.audio]
    else:
        paths = [ROOT / c for c in DEFAULT_AUDIO_CANDIDATES]
    paths = [p for p in paths if p.exists()]
    if len(paths) < 2:
        pool = sorted((ROOT / "tools/resynth_loop_out").glob("*.wav"))
        paths = pool[: args.limit_audio or 6]
    if not paths:
        raise SystemExit("No audio files found. Pass --audio <wav> ...")
    if args.limit_audio:
        paths = paths[: args.limit_audio]
    return paths


def fmt_cell(label: str, score: float) -> str:
    return f"{label} ({score:.3f})"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--audio", nargs="*", help="WAV paths (default: resynth_loop_out sweep)")
    ap.add_argument("--limit-audio", type=int, default=0, help="cap number of clips")
    # laion/larger_clap_music is music-tuned but its HF text tower is broken
    # (see module docstring + assert_model_sane); clap-htsat-unfused is canonical.
    ap.add_argument("--model", default="laion/clap-htsat-unfused")
    ap.add_argument("--device", default="cpu", choices=["cpu", "mps", "cuda"])
    ap.add_argument("--template", default="{}", help="label template, e.g. 'This is a {} sound.'")
    ap.add_argument("--topk", type=int, default=5)
    ap.add_argument("--musiccaps-top", type=int, default=200)
    ap.add_argument("--no-musiccaps", action="store_true")
    ap.add_argument("--out", default="tools/clap_probe_out")
    args = ap.parse_args()

    out_dir = ROOT / args.out
    out_dir.mkdir(parents=True, exist_ok=True)

    audio_paths = resolve_audio(args)
    print(f"[probe] audio clips ({len(audio_paths)}):")
    for p in audio_paths:
        print(f"          {p.relative_to(ROOT)}")

    # --- vocabularies -----------------------------------------------------
    vocabs: dict[str, list[str]] = {}
    if not args.no_musiccaps:
        try:
            print(f"[probe] mining MusicCaps aspect_list (top {args.musiccaps_top}) ...")
            vocabs["musiccaps"] = load_musiccaps_vocab(args.musiccaps_top)
        except Exception as e:  # network / format issues must not kill the probe
            print(f"[probe] WARN musiccaps unavailable: {type(e).__name__}: {e}")
    try:
        print("[probe] fetching AudioSet ontology ...")
        vocabs["audioset"] = load_audioset_vocab()
    except Exception as e:
        print(f"[probe] WARN audioset unavailable: {type(e).__name__}: {e}")
    vocabs["naive"] = list(dict.fromkeys(NAIVE_VOCAB))  # dedupe, keep order
    for name, v in vocabs.items():
        print(f"[probe]   {name}: {len(v)} labels")

    # --- model ------------------------------------------------------------
    print(f"[probe] loading {args.model} on {args.device} (first run downloads weights) ...")
    model = ClapModel.from_pretrained(args.model).to(args.device).eval()
    processor = ClapProcessor.from_pretrained(args.model)

    # Refuse to produce rankings from a broken model (see assert_model_sane).
    assert_model_sane(model, processor, args.device)

    # --- audio embeddings -------------------------------------------------
    print("[probe] embedding audio ...")
    audios = [load_audio_48k_mono(p) for p in audio_paths]
    audio_emb = embed_audios(model, processor, audios, args.device)

    # --- per-vocab: text embeddings, ranking, metrics ---------------------
    results: dict[str, dict] = {}
    for name, labels in vocabs.items():
        print(f"[probe] embedding '{name}' text ({len(labels)} labels) ...")
        texts = [args.template.format(lbl) for lbl in labels]
        text_emb = embed_texts(model, processor, texts, args.device)
        red_mean, red_hi = redundancy(text_emb)
        vals, idx = rank(audio_emb, text_emb)
        topk = []
        for ai in range(len(audio_paths)):
            topk.append(
                [(labels[idx[ai, j].item()], vals[ai, j].item()) for j in range(args.topk)]
            )
        margin12 = (vals[:, 0] - vals[:, 1]).mean().item()
        margin15 = (vals[:, 0] - vals[:, min(4, vals.shape[1] - 1)]).mean().item()
        results[name] = {
            "n_labels": len(labels),
            "redundancy_mean": red_mean,
            "redundancy_frac_gt_0.9": red_hi,
            "mean_top1": vals[:, 0].mean().item(),
            "mean_margin_1_2": margin12,
            "mean_margin_1_5": margin15,
            "topk": topk,
        }

    # --- Q3: does CLAP hear the drift? ------------------------------------
    drift = None
    anchor_i = next((i for i, p in enumerate(audio_paths) if p.name == ANCHOR_BASENAME), None)
    if anchor_i is not None:
        cos = (audio_emb @ audio_emb[anchor_i]).tolist()
        drift = [
            {"clip": audio_paths[i].name, "cos_to_anchor": cos[i]}
            for i in range(len(audio_paths))
        ]

    # --- write report -----------------------------------------------------
    report = render_report(args, audio_paths, vocabs, results, drift)
    (out_dir / "REPORT.md").write_text(report, encoding="utf-8")
    (out_dir / "scores.json").write_text(
        json.dumps(
            {
                "model": args.model,
                "template": args.template,
                "audio": [str(p.relative_to(ROOT)) for p in audio_paths],
                "results": results,
                "drift": drift,
            },
            indent=2,
        ),
        encoding="utf-8",
    )

    print("\n" + summary_table(results))
    if drift:
        print("\n" + drift_table(drift))
    print(f"\n[probe] wrote {out_dir.relative_to(ROOT)}/REPORT.md and scores.json")
    return 0


def summary_table(results: dict) -> str:
    lines = [
        "VOCABULARY SEPARABILITY  (redundancy low = good; margins high = decisive)",
        f"{'vocab':<12}{'#lbl':>6}{'redund':>9}{'>0.9':>8}{'top1':>8}{'m1-2':>8}{'m1-5':>8}",
    ]
    for name, r in results.items():
        lines.append(
            f"{name:<12}{r['n_labels']:>6}{r['redundancy_mean']:>9.3f}"
            f"{r['redundancy_frac_gt_0.9']:>8.2f}{r['mean_top1']:>8.3f}"
            f"{r['mean_margin_1_2']:>8.3f}{r['mean_margin_1_5']:>8.3f}"
        )
    return "\n".join(lines)


def drift_table(drift: list[dict]) -> str:
    lines = ["DRIFT (audio-embedding cosine to anchor; should fall with sigma/iter)"]
    for d in drift:
        lines.append(f"  {d['cos_to_anchor']:.3f}  {d['clip']}")
    return "\n".join(lines)


def render_report(args, audio_paths, vocabs, results, drift) -> str:
    md = [
        "# CLAP semantic-loop probe",
        "",
        f"- model: `{args.model}`",
        f"- template: `{args.template}`",
        f"- top-k: {args.topk}",
        "",
        "## Q2 — vocabulary separability",
        "",
        "Redundancy = mean pairwise cosine of the vocabulary's own text embeddings "
        "(high ⇒ collinear/redundant labels). Margins = how much the top tag wins by, "
        "averaged over clips (high ⇒ decisive ranking). Resolution is bounded by "
        "separability, not label count.",
        "",
        "| vocab | #labels | redundancy | %pairs>0.9 | mean top1 | margin 1→2 | margin 1→5 |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for name, r in results.items():
        md.append(
            f"| {name} | {r['n_labels']} | {r['redundancy_mean']:.3f} | "
            f"{r['redundancy_frac_gt_0.9']:.2f} | {r['mean_top1']:.3f} | "
            f"{r['mean_margin_1_2']:.3f} | {r['mean_margin_1_5']:.3f} |"
        )

    md += ["", "## Q1 — top tags per clip", ""]
    names = list(results.keys())
    for ai, p in enumerate(audio_paths):
        md += [f"### `{p.name}`", "", "| rank | " + " | ".join(names) + " |",
               "|---:|" + "|".join(["---"] * len(names)) + "|"]
        for j in range(args.topk):
            row = [str(j + 1)]
            for name in names:
                lbl, sc = results[name]["topk"][ai][j]
                row.append(fmt_cell(lbl, sc))
            md.append("| " + " | ".join(row) + " |")
        md.append("")

    if drift:
        md += [
            "## Q3 — does CLAP hear the resynth drift?",
            "",
            "Cosine of each clip's *audio* embedding to the anchor "
            f"(`{ANCHOR_BASENAME}`). If CLAP tracks the loop, this falls as the "
            "drift (sigma/iter) rises.",
            "",
            "| clip | cos to anchor |",
            "|---|---:|",
        ]
        for d in drift:
            md.append(f"| `{d['clip']}` | {d['cos_to_anchor']:.3f} |")
        md.append("")

    return "\n".join(md)


if __name__ == "__main__":
    raise SystemExit(main())
