#!/usr/bin/env python3
"""CLAP follow-up analyses, building on tools/clap_probe.py's findings.

Two analyses the probe pointed to:

A. CURATED VOCABULARY + separability pruning.
   The probe showed the ideal loop vocabulary is a *timbre/affect* register
   (naive-style) *pruned for separability* (audioset-style spread) — neither raw
   vocab was ideal (audioset = decisive but source-framed; naive = right register
   but redundant; musiccaps = affective filler). Here we hand-curate a timbre
   vocabulary, then greedily prune it to a maximally-separated subset (k-center /
   max-min on CLAP text embeddings), report redundancy before/after, and re-rank
   the clips with the pruned set.

B. CONTROLLED DRIFT SWEEP.
   The probe's drift table mixed clips and looked non-monotonic. The real
   resynth grid (tools/resynth_loop_out/) is a clean sigma x iter sweep, ALL
   resynths of the same original ("warm analog bass drone"). We measure CLAP
   audio-embedding cosine(iter_N, original) across the grid. Per
   RESYNTH_CALIBRATION_FINDINGS.md the spectral ground truth (timbre_corr/finA)
   says LOW sigma = strong evolution (drifts far), HIGH sigma = wash-out (stays
   near). If CLAP is a faithful ear, its iter-20 cosine should RISE with sigma
   (matching finA's 0.37 -> 0.94 trend) and FALL across iterations at low sigma.

Uses the canonical laion/clap-htsat-unfused (default in clap_probe) with the same
sine-vs-noise sanity gate. Read-only; writes only under --out.

Usage:
    .venv/bin/python tools/clap_followup.py
    .venv/bin/python tools/clap_followup.py --prune-k 48
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

import torch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import clap_probe as cp  # noqa: E402  (embed_texts/embed_audios/rank/redundancy/...)


# Hand-curated timbre/affect/sound-design register (the "right" register), grouped
# by perceptual dimension with deliberate breadth; max-min pruning removes the
# near-synonyms automatically. Deduped at load.
CURATED_TIMBRE = [
    # spectral brightness
    "bright", "dark", "brilliant", "dull", "glassy", "muffled", "airy",
    "piercing", "mellow", "shrill", "crisp", "murky",
    # weight / body
    "thin", "fat", "heavy", "light", "hollow", "full", "boomy", "deep",
    "shallow", "massive", "dense", "sparse",
    # grain / texture
    "smooth", "rough", "gritty", "grainy", "fuzzy", "clean", "noisy",
    "scratchy", "silky", "coarse", "velvety", "sandy",
    # material
    "metallic", "wooden", "watery", "plastic", "ceramic", "crystalline",
    "rubbery", "stony", "liquid", "icy", "molten", "papery",
    # motion / time
    "pulsing", "throbbing", "evolving", "static", "stuttering", "gliding",
    "wobbling", "fluttering", "churning", "drifting", "steady", "jittery",
    # affect / mood
    "warm", "cold", "tense", "calm", "eerie", "playful", "menacing",
    "melancholic", "serene", "anxious", "tender", "brooding", "euphoric",
    "sinister",
    # space
    "dry", "wet", "cavernous", "intimate", "distant", "roomy", "boxy", "vast",
    "airless", "reverberant", "echoing", "smeared",
    # articulation
    "percussive", "sustained", "plucked", "bowed", "breathy", "staccato",
    "legato", "punchy", "snappy", "droning", "ringing", "clicky",
    # distortion / character
    "distorted", "saturated", "pristine", "lo-fi", "hi-fi", "crunchy",
    "clipped", "overdriven", "fizzy", "raw", "polished", "degraded",
    # harmonic / pitch
    "harmonic", "inharmonic", "dissonant", "consonant", "tonal", "atonal",
    "microtonal", "detuned", "resonant", "buzzy", "beating", "warbling",
    # energy
    "aggressive", "gentle", "violent", "soothing", "frantic", "relaxed",
    "explosive", "subtle", "harsh", "biting", "caressing", "restless",
    # organic / synthetic
    "organic", "synthetic", "mechanical", "robotic", "natural", "artificial",
    "alien", "vocal", "digital", "analog", "granular", "glitchy",
]

# A few representative clips for the curated re-rank (varied textures).
CURATE_CLIPS = [
    "tools/resynth_loop_out/original.wav",
    "tools/resynth_loop_out/anchor_family.wav",
    "tools/resynth_loop_out/sigma0.050_iter20.wav",
    "tools/resynth_loop_out/sigma0.300_iter15.wav",
    "resources/test_sample.wav",
]


def maxmin_prune(text_emb: torch.Tensor, k: int) -> list[int]:
    """Greedy k-center on cosine: pick k labels that are maximally spread apart."""
    n = text_emb.shape[0]
    k = min(k, n)
    sim = text_emb @ text_emb.T  # [n, n] cosine (rows L2-normalized)
    off = sim.clone()
    off.fill_diagonal_(2.0)  # exclude self (cosine <= 1, so 2 is never the min)
    flat_idx = off.view(-1).argmin().item()  # most-distant pair seeds the set
    i, j = flat_idx // n, flat_idx % n
    selected = [i, j]
    chosen = torch.zeros(n, dtype=torch.bool)
    chosen[i] = chosen[j] = True
    while len(selected) < k:
        sims_to_sel = sim[:, selected].max(dim=1).values  # closeness to the set
        sims_to_sel[chosen] = 2.0  # never re-pick
        nxt = int(sims_to_sel.argmin().item())  # farthest from everything chosen
        selected.append(nxt)
        chosen[nxt] = True
    return selected


def topk_per_clip(audio_emb, text_emb, labels, k):
    vals, idx = cp.rank(audio_emb, text_emb)
    out = []
    for ai in range(audio_emb.shape[0]):
        out.append([(labels[idx[ai, j].item()], round(vals[ai, j].item(), 3)) for j in range(k)])
    return out


def analysis_curate(model, processor, device, args):
    labels = list(dict.fromkeys(CURATED_TIMBRE))
    print(f"[A] curated timbre vocab: {len(labels)} terms -> prune to {args.prune_k}")
    text_emb = cp.embed_texts(model, processor, labels, device)
    red_full, hi_full = cp.redundancy(text_emb)

    keep = maxmin_prune(text_emb, args.prune_k)
    pruned_labels = [labels[i] for i in keep]
    pruned_emb = text_emb[keep]
    red_pruned, hi_pruned = cp.redundancy(pruned_emb)
    dropped = [l for i, l in enumerate(labels) if i not in set(keep)]

    clip_paths = [cp.ROOT / c for c in CURATE_CLIPS if (cp.ROOT / c).exists()]
    audios = [cp.load_audio_48k_mono(p) for p in clip_paths]
    audio_emb = cp.embed_audios(model, processor, audios, device)
    topk = topk_per_clip(audio_emb, pruned_emb, pruned_labels, args.topk)

    return {
        "n_full": len(labels),
        "n_pruned": len(pruned_labels),
        "redundancy_full": red_full,
        "redundancy_pruned": red_pruned,
        "frac_gt_0.9_full": hi_full,
        "frac_gt_0.9_pruned": hi_pruned,
        "pruned_vocab": pruned_labels,
        "dropped_sample": dropped[:30],
        "clips": [str(p.relative_to(cp.ROOT)) for p in clip_paths],
        "topk": topk,
    }


def analysis_drift(model, processor, device):
    base = cp.ROOT / "tools/resynth_loop_out"
    orig = base / "original.wav"
    files = sorted(base.glob("sigma*_iter*.wav"))
    grid = {}  # sigma -> {iter -> path}
    pat = re.compile(r"sigma([0-9.]+)_iter([0-9]+)\.wav")
    for f in files:
        m = pat.match(f.name)
        if m:
            grid.setdefault(m.group(1), {})[int(m.group(2))] = f

    paths = [orig] + [grid[s][i] for s in sorted(grid) for i in sorted(grid[s])]
    audios = [cp.load_audio_48k_mono(p) for p in paths]
    emb = cp.embed_audios(model, processor, audios, device)
    anchor = emb[0]
    cos = {str(paths[k].name): float((emb[k] @ anchor).item()) for k in range(len(paths))}

    sigmas = sorted(grid)
    iters = sorted({i for s in grid for i in grid[s]})
    table = {s: {i: cos[grid[s][i].name] for i in sorted(grid[s])} for s in sigmas}
    iter_max = max(iters)
    final_by_sigma = {s: table[s].get(iter_max) for s in sigmas}
    return {"sigmas": sigmas, "iters": iters, "table": table,
            "final_by_sigma": final_by_sigma, "iter_max": iter_max}


def render(args, A, B):
    md = ["# CLAP follow-up: curated vocabulary + controlled drift sweep", "",
          f"- model: `{args.model}`", ""]
    md += ["## A — curated timbre vocabulary, pruned for separability", "",
           f"Hand-curated timbre/affect register ({A['n_full']} terms) greedily "
           f"pruned (max-min / k-center on CLAP text embeddings) to "
           f"{A['n_pruned']} maximally-separated terms.", "",
           "| vocab | #labels | redundancy | %pairs>0.9 |",
           "|---|---:|---:|---:|",
           f"| curated (full) | {A['n_full']} | {A['redundancy_full']:.3f} | {A['frac_gt_0.9_full']:.2f} |",
           f"| curated (pruned) | {A['n_pruned']} | {A['redundancy_pruned']:.3f} | {A['frac_gt_0.9_pruned']:.2f} |",
           "",
           "**Pruned vocabulary:** " + ", ".join(A["pruned_vocab"]), "",
           "**Sample of dropped near-synonyms:** " + ", ".join(A["dropped_sample"]), "",
           "### Re-ranked clips (pruned vocab)", ""]
    for ci, clip in enumerate(A["clips"]):
        md += [f"**`{Path(clip).name}`** — " +
               ", ".join(f"{lbl} ({sc})" for lbl, sc in A["topk"][ci]), ""]

    md += ["## B — controlled drift sweep (cosine of each resynth to `original.wav`)", "",
           "All clips are resynths of the same source. Per "
           "`RESYNTH_CALIBRATION_FINDINGS.md`, LOW sigma = strong evolution "
           "(drifts far), HIGH sigma = wash-out (stays near). A faithful CLAP "
           "ear should show cosine FALLING across iterations at low sigma and "
           "the iter-{} value RISING with sigma.".format(B["iter_max"]), "",
           "| sigma | " + " | ".join(f"iter{i:02d}" for i in B["iters"]) + " |",
           "|---|" + "|".join(["---:"] * len(B["iters"])) + "|"]
    for s in B["sigmas"]:
        row = [s] + [f"{B['table'][s].get(i, float('nan')):.3f}" for i in B["iters"]]
        md.append("| " + " | ".join(row) + " |")
    md += ["",
           f"**Converged distance (iter{B['iter_max']:02d} cosine to original) vs sigma** "
           "— should rise monotonically (more wash-out = nearer original):", "",
           "| sigma | " + " | ".join(B["sigmas"]) + " |",
           "|---|" + "|".join(["---:"] * len(B["sigmas"])) + "|",
           "| cos | " + " | ".join(f"{B['final_by_sigma'][s]:.3f}" for s in B["sigmas"]) + " |",
           ""]
    return "\n".join(md)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", default="laion/clap-htsat-unfused")
    ap.add_argument("--device", default="cpu", choices=["cpu", "mps", "cuda"])
    ap.add_argument("--prune-k", type=int, default=64)
    ap.add_argument("--topk", type=int, default=6)
    ap.add_argument("--out", default="tools/clap_followup_out")
    args = ap.parse_args()

    out_dir = cp.ROOT / args.out
    out_dir.mkdir(parents=True, exist_ok=True)

    from transformers import ClapModel, ClapProcessor
    print(f"[followup] loading {args.model} on {args.device} ...")
    model = ClapModel.from_pretrained(args.model).to(args.device).eval()
    processor = ClapProcessor.from_pretrained(args.model)
    cp.assert_model_sane(model, processor, args.device)

    A = analysis_curate(model, processor, args.device, args)
    B = analysis_drift(model, processor, args.device)

    report = render(args, A, B)
    (out_dir / "REPORT.md").write_text(report, encoding="utf-8")
    (out_dir / "results.json").write_text(json.dumps({"curate": A, "drift": B}, indent=2), encoding="utf-8")

    print("\n[A] curated redundancy: full {:.3f} ({} terms) -> pruned {:.3f} ({} terms)".format(
        A["redundancy_full"], A["n_full"], A["redundancy_pruned"], A["n_pruned"]))
    print("[B] iter{:02d} cosine-to-original by sigma:".format(B["iter_max"]))
    for s in B["sigmas"]:
        print(f"      sigma {s}:  {B['final_by_sigma'][s]:.3f}")
    print(f"\n[followup] wrote {out_dir.relative_to(cp.ROOT)}/REPORT.md and results.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
