#!/usr/bin/env python3
"""Diagnostic: does the PROMPT actually move SA3's output, or does the init_audio
carry lock it? Motivated by the audition verdict that clap_llm_loop's aB10/aB08
renders all sound nearly identical despite wildly different Prompt B.

Two clean tests at the preset's own params (Creamy-Dreamy SA3), alpha=+1.0 (pure
B), seed fixed, three deliberately different prompts:

  P1 dreamy dream                               (the loop's B0)
  P2 A winter's night in a cozy frost-covered cabin   (an actual abduction output)
  P3 heavy distorted industrial machine noise, harsh metallic clanging  (far field)

  T1  NO init_audio  -> do the three prompts produce DIFFERENT audio at all?
  T2  init_audio = P1 render, init_noise=0.5 (the loop's default)
  T3  init_audio = P1 render, init_noise=0.9

Verdict:
  - T1 different  => prompt+blend WORK (the bite is real).
  - T1 identical  => prompt/blend path is broken (deeper bug).
  - T2 hugs P1 but T3 separates => init_noise=0.5 carry LOCKS the loop (the cause).

Measures raw audio (NOT CLAP, per the user's distrust of the cosine numbers):
Pearson waveform corr + global spectral centroid + RMS. WAVs saved for audition.
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_init_audio import PipeClient, encode_init_audio, write_wav, corr_to  # noqa: E402
from clap_semantic_loop import load_preset  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[1]
BACKEND_SCRIPT = REPO_ROOT / "backend" / "pipe_inference.py"
PRESET = Path.home() / "Library/T5ynth/presets/UCDCAE AI Lab/Creamy-Dreamy SA3.t5p"
OUT = REPO_ROOT / "tools" / "diag_promptbite_out"

P = {
    "P1_dreamy": "dreamy dream",
    "P2_cabin": "A winter's night in a cozy frost-covered cabin",
    "P3_industrial": "heavy distorted industrial machine noise, harsh metallic clanging",
}


def log(m): print(m, file=sys.stderr, flush=True)


def centroid(audio, sr):
    mono = audio.mean(axis=0).astype("float64")
    spec = np.abs(np.fft.rfft(mono))
    freqs = np.fft.rfftfreq(mono.shape[-1], 1.0 / sr)
    s = spec.sum()
    return float((freqs * spec).sum() / s) if s > 0 else 0.0


def rms(audio):
    return float(np.sqrt(np.mean(audio.astype("float64") ** 2)))


def main():
    p = load_preset(PRESET)
    OUT.mkdir(parents=True, exist_ok=True)
    base = {"model": p["model"], "duration": p["duration"], "steps": p["steps"],
            "cfg_scale": p["cfg"], "magnitude": p["magnitude"],
            "noise_sigma": p["noise_sigma"], "start_pos": 0.0,
            "injection_mode": p["injection_mode"]}
    seed, ha = p["seed"], p["header_a"]
    log(f"preset={p['name']} model={p['model']} dur={p['duration']}s steps={p['steps']} "
        f"cfg={p['cfg']} seed={seed}  (alpha forced +1.0 = pure B)")

    client = PipeClient([sys.executable, str(BACKEND_SCRIPT)])
    try:
        if p["model"] not in client.info.get("models", []):
            log(f"ERROR: {p['model']} not available")
            return 1

        def gen(prompt_b, init=None, init_noise=None):
            req = {**base, "prompt_a": ha, "prompt_b": prompt_b, "alpha": 1.0, "seed": seed}
            if init is not None:
                a, sr0 = init
                req.update({"init_audio_b64": encode_init_audio(a), "init_audio_sr": sr0,
                            "init_audio_channels": a.shape[0], "init_noise_level": init_noise})
            r = client.request(req)
            return r["audio"], r["sample_rate"]

        # ── T1: pure text, no init_audio ──
        log("\n== T1: NO init_audio (does the prompt move the audio?) ==")
        t1 = {}
        for k, prompt in P.items():
            a, sr = gen(prompt)
            t1[k] = a
            write_wav(OUT / f"T1_{k}.wav", a, sr)
            log(f"  {k:16s} centroid={centroid(a, sr):7.1f}Hz  rms={rms(a):.3f}")
        keys = list(P.keys())
        log("  pairwise waveform corr (lower = more different):")
        for i in range(len(keys)):
            for j in range(i + 1, len(keys)):
                log(f"    {keys[i]:16s} vs {keys[j]:16s}: {corr_to(t1[keys[i]], t1[keys[j]]):+.3f}")

        # ── T2 / T3: init_audio = P1 render, two noise levels ──
        init_p1 = (t1["P1_dreamy"], sr)
        for lvl, tag in ((0.5, "T2"), (0.9, "T3")):
            log(f"\n== {tag}: init_audio=P1, init_noise={lvl} "
                f"(does prompt escape the carry?) ==")
            base_corr = corr_to(t1["P1_dreamy"], t1["P1_dreamy"])  # 1.0 sanity
            for k in ("P2_cabin", "P3_industrial"):
                a, sr2 = gen(P[k], init=init_p1, init_noise=lvl)
                write_wav(OUT / f"{tag}_{k}.wav", a, sr2)
                c_to_p1 = corr_to(t1["P1_dreamy"], a)
                c_to_t1 = corr_to(t1[k], a)  # how close to the pure-text render of the same prompt
                log(f"  {k:16s} centroid={centroid(a, sr2):7.1f}Hz  "
                    f"corr→P1render={c_to_p1:+.3f}  corr→its-own-text-render={c_to_t1:+.3f}")

        log("\nVerdict guide: T1 corr LOW => prompt bites. "
            "T2 corr→P1render HIGH & T3 lower => init_noise=0.5 locks the loop.")
        log(f"WAVs under {OUT}")
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    sys.exit(main())
