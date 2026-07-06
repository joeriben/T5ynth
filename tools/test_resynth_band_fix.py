#!/usr/bin/env python3
"""Audition the Resynth band fix: OLD mapping (0.50-0.45*amount, caps at 0.50 →
prompt always drowned) vs NEW (0.90-0.85*amount, low amount → ~0.9 = prompt wins).

Pad seed + "samba" prompt, at the Resynth amounts a user actually dials. Proves
the typed prompt now breaks through at LOW Resynth without Re-Prompt. Metric =
CLAP cos(out,"samba") − cos(out,"ambient pad"): >0 = sounds like the prompt.

    .venv/bin/python tools/test_resynth_band_fix.py
"""
from __future__ import annotations
import subprocess, sys
from pathlib import Path
import numpy as np
import soundfile as sf
import torch, torchaudio

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_init_audio import PipeClient, encode_init_audio, write_wav   # noqa: E402
from clap_probe import CLAP_SR, embed_texts, embed_audios, assert_model_sane  # noqa: E402
from transformers import ClapModel, ClapProcessor                      # noqa: E402

REPO = Path(__file__).resolve().parents[1]
BACKEND = REPO / "backend" / "pipe_inference.py"
OUT = Path(__file__).resolve().parent / "resynth_band_fix_out"
PAD_WAV = Path("/Users/joerissen/Music/Samples/Yamaha D-85 Vibraphone_.wav")

MODEL = "stable-audio-3-small-music"
SEED, STEPS, CFG, DUR = 12345, 8, 1.0, 3.0
PROMPT, DECOY = "a samba group playing on the streets", "a slow ambient synth pad"
AMOUNTS = [0.05, 0.25, 0.50, 1.0]          # Min-ish → Full
old_map = lambda a: 0.50 - 0.45 * a
new_map = lambda a: 0.90 - 0.85 * a
DEVICE = "cpu"


def log(m): print(m, file=sys.stderr, flush=True)


def load_pad(d):
    wav, sr = sf.read(str(PAD_WAV), dtype="float32", always_2d=True)
    a = np.ascontiguousarray(wav.T, np.float32)
    need = int(d * sr) + 1
    if a.shape[1] < need:
        a = np.tile(a, (1, int(np.ceil(need / a.shape[1]))))[:, :need]
    return np.ascontiguousarray(a, np.float32), sr


def clap_emb(model, proc, au, sr):
    mono = au.mean(axis=0).astype("float32")
    if sr != CLAP_SR:
        mono = torchaudio.functional.resample(torch.from_numpy(mono), sr, CLAP_SR).numpy()
    return embed_audios(model, proc, [mono], DEVICE)[0]


def main():
    if not BACKEND.is_file() or not PAD_WAV.is_file():
        log("ERROR: backend or pad missing"); return 1
    OUT.mkdir(exist_ok=True)
    log("Loading CLAP ...")
    clap = ClapModel.from_pretrained("laion/clap-htsat-unfused").to(DEVICE).eval()
    cproc = ClapProcessor.from_pretrained("laion/clap-htsat-unfused")
    assert_model_sane(clap, cproc, DEVICE)
    t_p, t_d = embed_texts(clap, cproc, [PROMPT, DECOY], DEVICE)
    pad, pad_sr = load_pad(DUR)
    pad_b64 = encode_init_audio(pad)

    client = PipeClient([sys.executable, str(BACKEND)])
    try:
        if MODEL not in client.info.get("models", []):
            log(f"ERROR: {MODEL} not installed"); return 1

        def render(noise):
            r = client.request({"model": MODEL, "duration": DUR, "steps": STEPS,
                                "cfg_scale": CFG, "prompt_a": PROMPT, "prompt_b": "",
                                "alpha": -1.0, "seed": SEED,
                                "init_audio_b64": pad_b64, "init_audio_sr": pad_sr,
                                "init_audio_channels": pad.shape[0],
                                "init_noise_level": noise})
            return r["audio"], r["sample_rate"]

        def bt(au, sr):
            e = clap_emb(clap, cproc, au, sr)
            return float(e @ t_p) - float(e @ t_d)

        log(f"\nPad seed + prompt '{PROMPT}'. samba−pad: >0 = prompt wins.\n")
        log(f"  {'Resynth':>8} | {'OLD σ':>6} {'samba−pad':>9} | {'NEW σ':>6} {'samba−pad':>9}")
        for a in AMOUNTS:
            on, nn = old_map(a), new_map(a)
            ao, sro = render(on); an, srn = render(nn)
            bo, bn = bt(ao, sro), bt(an, srn)
            tag = "Full" if a >= 1.0 else ("Min~" if a <= 0.05 else f"{int(a*100)}%")
            write_wav(OUT / f"OLD_resynth{int(a*100):03d}_sigma{int(on*100):02d}.wav", ao, sro)
            write_wav(OUT / f"NEW_resynth{int(a*100):03d}_sigma{int(nn*100):02d}.wav", an, srn)
            log(f"  {tag:>8} | {on:>6.2f} {bo:>+9.3f} | {nn:>6.2f} {bn:>+9.3f}")

        log("\n=== READOUT ===")
        log("  At LOW Resynth (Min~/25%): NEW samba−pad should be clearly > OLD")
        log("  (the prompt now wins where the old band still drowned it).")
        log("  At Full: both ~equal (σ 0.05 either way → input preserved).")
        log(f"\nWAVs in {OUT}  (OLD_* vs NEW_* per Resynth setting)")
        try: subprocess.run(["open", str(OUT)], check=False)
        except Exception: pass
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
