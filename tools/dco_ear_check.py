#!/usr/bin/env python3
"""Does the synth's OWN ear (CLAP) hear the DCO bakes meaningfully? The audition
loop's whole premise is that bake -> CLAP tags track what the recipe actually is,
so a small model can adjust toward a target by listening. This CLAP-analyzes the
hand-authored palette WAVs (tools/dco_palette_out) with the SAME CLAP the backend
uses (laion/clap-htsat-unfused, reusing tools/clap_probe.py), and prints the top
tags the ear returns for each bake.

If the metallic/ring bakes read 'metallic/clangy', the cheby bakes read
'harsh/gritty', and the dead additive square reads dull/thin, the fitness signal
is real and the loop is buildable.

  .venv/bin/python tools/dco_ear_check.py
"""
import os
import sys
import glob
from pathlib import Path

sys.path.insert(0, os.path.dirname(__file__))
import clap_probe as CP                       # the CLAP ear (embed/rank/vocab/sanity)
from transformers import ClapModel, ClapProcessor

PAL = os.path.join(os.path.dirname(__file__), "dco_palette_out")
MODEL = "laion/clap-htsat-unfused"
DEV = "cpu"


def main():
    wavs = sorted(glob.glob(os.path.join(PAL, "*.wav")))
    if not wavs:
        print("No palette WAVs; run tools/dco_palette_demo.py first.", file=sys.stderr)
        return 2
    print(f"loading CLAP {MODEL} on {DEV} (first run downloads weights) ...")
    model = ClapModel.from_pretrained(MODEL).to(DEV).eval()
    processor = ClapProcessor.from_pretrained(MODEL)
    CP.assert_model_sane(model, processor, DEV)

    vocab = list(dict.fromkeys(CP.NAIVE_VOCAB))
    text_emb = CP.embed_texts(model, processor, vocab, DEV)
    audios = [CP.load_audio_48k_mono(Path(w)) for w in wavs]
    audio_emb = CP.embed_audios(model, processor, audios, DEV)
    vals, idx = CP.rank(audio_emb, text_emb)

    print(f"\n{'bake':<30} top-6 tags the synth HEARS")
    print("-" * 92)
    for ai, w in enumerate(wavs):
        tags = ", ".join(f"{vocab[idx[ai, j].item()]} ({vals[ai, j].item():.2f})"
                          for j in range(6))
        print(f"{os.path.basename(w):<30} {tags}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
