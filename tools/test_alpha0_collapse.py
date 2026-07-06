#!/usr/bin/env python3
"""Head-on test of the user's challenge.

My earlier Phase-5 edit-sensitivity at α=0 was near-zero (editA_Δ≈0.01), which
IMPLIES the absurd consequence: any A/B prompt pair at α=0 would sound ~identical.
α=0 is EQUILIBRIUM (0.5·A + 0.5·B), not deletion — so distinct pairs should give
distinct output. This tests that consequence directly, and disentangles two
candidate explanations if it DOES collapse:

  (1) text-only  (no init_audio)            — isolates the α=0 blend itself
  (2) with init_audio seed @ init_noise 0.35 — the user's actual scenario (seed)

K contrasting prompt pairs, all at α=0, same fixed seed. Metric = CLAP cosine
(semantic), NOT the coarse avg-spectrum timbre_corr that produced the suspect
number. Output: a pair-vs-pair CLAP similarity matrix per condition (high
off-diagonal ⇒ collapse), each output's cosine to its OWN A/B prompt texts, and
all WAVs on disk for the ear (the real arbiter).

    .venv/bin/python tools/test_alpha0_collapse.py [model]
"""

from __future__ import annotations
import subprocess, sys
from pathlib import Path
import numpy as np
import soundfile as sf
import torch, torchaudio

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_init_audio import PipeClient, encode_init_audio, write_wav   # noqa: E402
from test_resynth_loop import timbre_corr                              # noqa: E402
from clap_probe import CLAP_SR, embed_texts, embed_audios, assert_model_sane  # noqa: E402
from transformers import ClapModel, ClapProcessor                      # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[1]
BACKEND = REPO_ROOT / "backend" / "pipe_inference.py"
OUT = Path(__file__).resolve().parent / "alpha0_collapse_out"
SEED_WAV = Path("/Users/joerissen/Music/Samples/Yamaha D-85 Vibraphone_.wav")

MODEL = sys.argv[1] if len(sys.argv) > 1 else "stable-audio-3-small-music"
SEED = 12345
STEPS, CFG, DUR = 8, 1.0, 3.0
INIT_NOISE = 0.35
DEVICE = "cpu"

PAIRS = [
    ("a samba group playing on the streets", "a solo cello melody"),
    ("a distorted techno kick drum loop",    "a gentle harp arpeggio"),
    ("a violent thunderstorm with heavy rain", "birdsong in a quiet forest"),
    ("a triumphant brass fanfare",           "a deep dark ambient drone"),
]
LABELS = ["samba|cello", "techno|harp", "storm|birds", "brass|drone"]


def log(m): print(m, file=sys.stderr, flush=True)


def load_seed(gen_dur):
    wav, sr = sf.read(str(SEED_WAV), dtype="float32", always_2d=True)
    a = np.ascontiguousarray(wav.T, dtype=np.float32)
    need = int(gen_dur * sr) + 1
    if a.shape[1] < need:
        a = np.tile(a, (1, int(np.ceil(need / a.shape[1]))))[:, :need]
    return np.ascontiguousarray(a, np.float32), sr


def clap_audio_emb(model, proc, audio_cs, sr):
    mono = audio_cs.mean(axis=0).astype("float32")
    if sr != CLAP_SR:
        mono = torchaudio.functional.resample(torch.from_numpy(mono), sr, CLAP_SR).numpy()
    return embed_audios(model, proc, [mono], DEVICE)[0]


def matrix(embs):
    n = len(embs)
    return [[float(embs[i] @ embs[j]) for j in range(n)] for i in range(n)]


def print_matrix(M, labels):
    log("        " + " ".join(f"{l:>11}" for l in labels))
    for i, row in enumerate(M):
        log(f"  {labels[i]:>11} " + " ".join(f"{v:>11.3f}" for v in row))
    off = [M[i][j] for i in range(len(M)) for j in range(len(M)) if i != j]
    log(f"  off-diagonal CLAP cos: mean {np.mean(off):.3f}  min {np.min(off):.3f}  max {np.max(off):.3f}")
    return float(np.mean(off))


def main():
    if not BACKEND.is_file() or not SEED_WAV.is_file():
        log("ERROR: backend or seed wav missing"); return 1
    OUT.mkdir(exist_ok=True)

    log("Loading CLAP ...")
    clap = ClapModel.from_pretrained("laion/clap-htsat-unfused").to(DEVICE).eval()
    cproc = ClapProcessor.from_pretrained("laion/clap-htsat-unfused")
    assert_model_sane(clap, cproc, DEVICE)

    seed_audio, seed_sr = load_seed(DUR)
    seed_b64 = encode_init_audio(seed_audio)

    client = PipeClient([sys.executable, str(BACKEND)])
    try:
        models = client.info.get("models", [])
        model = MODEL if MODEL in models else None
        if model is None:
            log(f"ERROR: {MODEL} not installed. Have: {models}"); return 1
        log(f"Model: {model}\n")

        def render(a, b, with_seed):
            req = {"model": model, "duration": DUR, "steps": STEPS, "cfg_scale": CFG,
                   "prompt_a": a, "prompt_b": b, "alpha": 0.0, "seed": SEED}
            if with_seed:
                req.update({"init_audio_b64": seed_b64, "init_audio_sr": seed_sr,
                            "init_audio_channels": seed_audio.shape[0],
                            "init_noise_level": INIT_NOISE})
            r = client.request(req)
            return r["audio"], r["sample_rate"]

        results = {}
        for cond, with_seed in [("textonly", False), ("withseed", True)]:
            log(f"=== α=0, condition: {cond} ({'init_audio seed @ n0.35' if with_seed else 'no init_audio'}) ===")
            embs, auds = [], []
            for i, (a, b) in enumerate(PAIRS):
                au, sr = render(a, b, with_seed)
                write_wav(OUT / f"a0_{cond}_{i}_{LABELS[i].replace('|','-')}.wav", au, sr)
                embs.append(clap_audio_emb(clap, cproc, au, sr))
                auds.append((au, sr))
            log("  pair-vs-pair CLAP cosine (1.0 = identical; collapse ⇒ high off-diagonal):")
            offmean = print_matrix(matrix(embs), LABELS)
            # does each output resemble ITS OWN prompts?
            log("  each output's CLAP cos to its own A / B prompt text:")
            for i, (a, b) in enumerate(PAIRS):
                ta, tb = embed_texts(clap, cproc, [a, b], DEVICE)
                log(f"    {LABELS[i]:>11}:  cosA {float(embs[i]@ta):+.3f}   cosB {float(embs[i]@tb):+.3f}")
            results[cond] = (offmean, embs, auds)
            log("")

        # cross-check: are the SAME-pair outputs different between conditions?
        log("=== seed effect: cos(textonly_i, withseed_i) per pair ===")
        for i in range(len(PAIRS)):
            e_t = results["textonly"][1][i]; e_s = results["withseed"][1][i]
            log(f"    {LABELS[i]:>11}: {float(e_t@e_s):+.3f}")

        log("\n=== READOUT ===")
        log(f"  text-only off-diagonal mean : {results['textonly'][0]:.3f}")
        log(f"  with-seed off-diagonal mean : {results['withseed'][0]:.3f}")
        log("  (low/moderate ⇒ distinct pairs ⇒ α=0 is NOT prompt-insensitive ⇒ my Phase-5 read was a metric artifact.")
        log("   high (~0.9+) ⇒ collapse; compare the two conditions to see if it's the seed or the α=0 blend.)")
        log(f"\nWAVs in {OUT}")
        try: subprocess.run(["open", str(OUT)], check=False)
        except Exception: pass
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
