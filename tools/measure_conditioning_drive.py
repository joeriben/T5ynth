#!/usr/bin/env python3
"""Measure DRIVE and DIRECTION of any A/B conditioning construction.

The T5 oscillator exists to reach conditioning that no text can produce, so
"the model has never seen this" is never an argument here. What IS an argument
is measurable, and this tool measures it:

  DRIVE     = ||proj(cond)|| over the real tokens, relative to the pole prompts.
              CFG's unconditional branch is literally zero context
              (dit.py: null_embed = torch.zeros_like(cross_attn_cond), and
              to_cond_embed is bias-free), so this norm IS the distance from
              "unconditioned". Less drive = closer to the model's prior = closer
              to the most-trained, most-average region it has. That direction is
              the regression, not unfamiliarity.

  DIRECTION = cos to each pole, on the same projected tensor. This is where the
              instrument does its work; a construction may point anywhere.

A change that lowers drive without changing direction is a loss. A change that
alters direction while holding drive is the instrument working.

Everything runs on CPU in well under a minute per engine — there is no excuse
for arguing this from intuition.

  python3 tools/measure_conditioning_drive.py --engine sao \
      --a "deep resonant bell" --b "dense rain forest at night"

  python3 tools/measure_conditioning_drive.py --engine both --ops linear,nlerp,product
"""
import argparse
import os
import sys

import torch
import torch.nn.functional as F
from safetensors.torch import load_file

MODELS = os.path.expanduser("~/Library/T5ynth/models")
ENGINES = {
    # name: (checkpoint, conditioner dir, max_length, padding mode)
    "sao": (f"{MODELS}/stable-audio-open-small/model.safetensors",
            f"{MODELS}/t5-base", 64, "zero"),
    "sa3": (f"{MODELS}/stable-audio-3-small-music/model.safetensors",
            f"{MODELS}/stable-audio-3-small-music/t5gemma-b-b-ul2", 256, "learned"),
}


class Conditioning:
    """The model's own text conditioner + the checkpoint's real to_cond_embed.

    Mirrors stable_audio_tools' T5Conditioner / T5GemmaConditioner (tokenizer,
    max_length, padding mode) and the DiT's context projection
    Linear(bias=False) -> SiLU -> Linear(bias=False). fp32 throughout: this
    measures geometry, not throughput.
    """

    def __init__(self, engine):
        ckpt, cond_dir, self.max_length, self.padding_mode = ENGINES[engine]
        for p in (ckpt, cond_dir):
            if not os.path.exists(p):
                sys.exit(f"missing: {p}")
        sd = load_file(ckpt)
        self.W0 = sd["model.model.to_cond_embed.0.weight"].float()
        self.W2 = sd["model.model.to_cond_embed.2.weight"].float()
        self.pad = (sd["conditioner.conditioners.prompt.padding_embedding"].float()
                    if self.padding_mode == "learned" else None)
        del sd

        from transformers import AutoTokenizer
        self.tok = AutoTokenizer.from_pretrained(cond_dir)
        if engine == "sao":
            from transformers import T5EncoderModel
            self.enc = T5EncoderModel.from_pretrained(cond_dir).eval().float()
        else:
            from transformers import AutoConfig, T5GemmaEncoderModel
            cfg = AutoConfig.from_pretrained(cond_dir)
            cfg.is_encoder_decoder = False
            self.enc = T5GemmaEncoderModel.from_pretrained(cond_dir, config=cfg).eval().float()

    def proj(self, x):
        """What the cross-attention actually consumes (V is NOT normalised;
        qk_norm only makes the attention PATTERN magnitude-robust)."""
        return F.linear(F.silu(F.linear(x, self.W0)), self.W2)

    @torch.no_grad()
    def encode(self, text):
        e = self.tok([text], truncation=True, max_length=self.max_length,
                     padding="max_length", return_tensors="pt")
        m = e["attention_mask"].bool()
        h = self.enc(input_ids=e["input_ids"], attention_mask=m)["last_hidden_state"].float()
        if self.padding_mode == "learned":
            return torch.where(m.unsqueeze(-1), h, self.pad.expand_as(h)), m
        return h * m.unsqueeze(-1).float(), m


def build(op, A, B, null, alpha, mask):
    """The constructions. `linear` is what backend/pipe_inference.py ships.

    Norms are taken over the REAL tokens (`mask`), never over the whole padded
    tensor — on SA3 that is 256 positions of which typically 3-8 carry content.
    """
    def rn(t):                              # real-token norm
        return t[0][mask].norm().clamp_min(1e-8)

    def rescale(t, ref):
        return t * (ref / rn(t))

    if op == "linear":                      # pipe_inference.py::_generate_native
        return (0.5 - 0.5 * alpha) * A + (0.5 + 0.5 * alpha) * B
    if op == "nlerp":                       # same direction, pole drive restored
        m = (0.5 - 0.5 * alpha) * A + (0.5 + 0.5 * alpha) * B
        pole = rn(A) if alpha < 0 else rn(B)
        t = min(1.0, abs(alpha))
        return rescale(m, (1.0 - t) * 0.5 * (rn(A) + rn(B)) + t * pole)
    if op == "delta":                       # A + alpha*(B - null)
        return A + alpha * (B - null)
    if op == "product":                     # elementwise, scaled to pole length
        return rescale(A * B, 0.5 * (rn(A) + rn(B)))
    if op == "echo":                        # _echo_through_null on the B side
        return (0.5 - 0.5 * alpha) * A + (0.5 + 0.5 * alpha) * (2.0 * null - A)
    raise SystemExit(f"unknown op: {op}")


def report(engine, pa, pb, ops, alphas):
    c = Conditioning(engine)
    A, ma = c.encode(pa)
    B, mb = c.encode(pb)
    null, _ = c.encode("")
    mask = (ma | mb)[0]

    def stats(t):
        p = c.proj(t)[0][mask]
        return p.norm().item(), p

    (nA, pA), (nB, pB) = stats(A), stats(B)
    pole = 0.5 * (nA + nB)
    print(f"\n=== {engine.upper()}  A={pa!r} ({int(ma.sum())} tok)  "
          f"B={pb!r} ({int(mb.sum())} tok)")
    print(f"    pole drive: ||proj(A)||={nA:.1f}  ||proj(B)||={nB:.1f}  "
          f"|| proj(\"\") ||={stats(null)[0]:.1f}   (CFG uncond = exactly 0)")
    print(f"{'op':>8} {'alpha':>6} | {'drive':>7} {'rel.pole':>9} | "
          f"{'cos to A':>9} {'cos to B':>9}")
    print("-" * 60)
    for op in ops:
        for alpha in (alphas if op not in ("product",) else [0.0]):
            n, p = stats(build(op, A, B, null, alpha, mask))
            print(f"{op:>8} {alpha:>6.2f} | {n:7.1f} {n / pole:9.3f} | "
                  f"{F.cosine_similarity(p.flatten(), pA.flatten(), dim=0):+9.3f} "
                  f"{F.cosine_similarity(p.flatten(), pB.flatten(), dim=0):+9.3f}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--engine", default="both", choices=["sao", "sa3", "both"])
    ap.add_argument("--a", default="deep resonant bell")
    ap.add_argument("--b", default="dense rain forest at night")
    ap.add_argument("--ops", default="linear,nlerp,delta,product",
                    help="linear,nlerp,delta,product,echo")
    ap.add_argument("--alphas", default="-1,-0.5,0,0.5,1,2")
    a = ap.parse_args()
    for eng in (["sao", "sa3"] if a.engine == "both" else [a.engine]):
        report(eng, a.a, a.b, a.ops.split(","),
               [float(x) for x in a.alphas.split(",")])
