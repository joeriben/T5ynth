#!/usr/bin/env python3
"""Reproduce the actual loop dynamics: "alte samples hängen über mehrere
Generierungen". The user feeds external audio (a pad) + a STATIC prompt and runs
the Resynth auto-regen loop; the output stays a "leicht verändertes Pad" and the
prompt never takes over. This reproduces both loop seeding strategies over N
rounds, STATIC prompt (typed once, never changed — the real scenario), and
measures whether the output WALKS toward the prompt or STICKS to the seed.

  mode "selffeed": round_n init_audio = output_{n-1}  (the committed default loop)
  mode "extcap"  : round_n init_audio = the SAME external pad every round
                   (the uncommitted ext-capture experiment)

Per round: conv = timbre_corr(out_n, out_{n-1})  (→1.0 = hung / no change)
           samba = CLAP(out_n, prompt) − CLAP(out_n, "a slow ambient synth pad")
                   (>0 = output sounds like the PROMPT; <0 = like a pad)
Fixed seed, α pinned to the lone prompt's pole, across the reachable init_noise band.

    .venv/bin/python tools/test_resynth_loop_hang.py [model]
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

REPO = Path(__file__).resolve().parents[1]
BACKEND = REPO / "backend" / "pipe_inference.py"
OUT = Path(__file__).resolve().parent / "loop_hang_out"
PAD_WAV = Path("/Users/joerissen/Music/Samples/Yamaha D-85 Vibraphone_.wav")  # pad-analog

MODEL = sys.argv[1] if len(sys.argv) > 1 else "stable-audio-3-small-music"
SEED, STEPS, CFG, DUR = 12345, 8, 1.0, 3.0
PROMPT = "a samba group playing on the streets"
DECOY = "a slow ambient synth pad"          # the "pad" pole for the breakthrough metric
ALPHA = -1.0                                 # lone prompt → pinned to its pole (what the plugin does)
NOISES = [0.05, 0.35, 0.50]                  # reachable Resynth band: Full → mid → Min
ROUNDS = 6
DEVICE = "cpu"


def log(m): print(m, file=sys.stderr, flush=True)


def load_pad(gen_dur):
    wav, sr = sf.read(str(PAD_WAV), dtype="float32", always_2d=True)
    a = np.ascontiguousarray(wav.T, np.float32)
    need = int(gen_dur * sr) + 1
    if a.shape[1] < need:
        a = np.tile(a, (1, int(np.ceil(need / a.shape[1]))))[:, :need]
    return np.ascontiguousarray(a, np.float32), sr


def clap_audio_emb(model, proc, audio_cs, sr):
    mono = audio_cs.mean(axis=0).astype("float32")
    if sr != CLAP_SR:
        mono = torchaudio.functional.resample(torch.from_numpy(mono), sr, CLAP_SR).numpy()
    return embed_audios(model, proc, [mono], DEVICE)[0]


def main():
    if not BACKEND.is_file() or not PAD_WAV.is_file():
        log("ERROR: backend or pad wav missing"); return 1
    OUT.mkdir(exist_ok=True)

    log("Loading CLAP ...")
    clap = ClapModel.from_pretrained("laion/clap-htsat-unfused").to(DEVICE).eval()
    cproc = ClapProcessor.from_pretrained("laion/clap-htsat-unfused")
    assert_model_sane(clap, cproc, DEVICE)
    t_prompt, t_decoy = embed_texts(clap, cproc, [PROMPT, DECOY], DEVICE)

    pad, pad_sr = load_pad(DUR)
    pad_b64 = encode_init_audio(pad)
    write_wav(OUT / "pad_seed.wav", pad, pad_sr)

    client = PipeClient([sys.executable, str(BACKEND)])
    try:
        models = client.info.get("models", [])
        model = MODEL if MODEL in models else None
        if model is None:
            log(f"ERROR: {MODEL} not installed. Have: {models}"); return 1
        log(f"Model: {model}\n")

        def render(init_b64, init_sr, init_ch, noise, seed=SEED):
            req = {"model": model, "duration": DUR, "steps": STEPS, "cfg_scale": CFG,
                   "prompt_a": PROMPT, "prompt_b": "", "alpha": ALPHA, "seed": seed}
            if init_b64 is not None:
                req.update({"init_audio_b64": init_b64, "init_audio_sr": init_sr,
                            "init_audio_channels": init_ch, "init_noise_level": noise})
            r = client.request(req)
            return r["audio"], r["sample_rate"]

        def breakthrough(au, sr):
            e = clap_audio_emb(clap, cproc, au, sr)
            return float(e @ t_prompt) - float(e @ t_decoy)

        desc = {"extcap": "same external pad every round, FIXED model seed (the default)",
                "extcap_fresh": "same external pad, FRESH model seed each round (the proposed fix)",
                "selffeed": "feeds last output back, fixed model seed"}
        for mode in ("extcap", "extcap_fresh", "selffeed"):
            log(f"================ mode: {mode} ({desc[mode]}) ================")
            for noise in NOISES:
                log(f"  init_noise {noise:.2f}  (Resynth { 'Full' if noise<=0.05 else 'Min' if noise>=0.50 else 'mid' }):")
                log(f"    {'round':>5} | {'conv(n,n-1)':>11} | {'samba−pad':>9}")
                prev = None
                seed_b64, seed_sr, seed_ch = pad_b64, pad_sr, pad.shape[0]  # round 0 always seeds the pad
                for n in range(ROUNDS):
                    model_seed = (SEED + n) if mode == "extcap_fresh" else SEED
                    au, sr = render(seed_b64, seed_sr, seed_ch, noise, seed=model_seed)
                    conv = timbre_corr(prev[0], au, sr) if prev is not None else float("nan")
                    bt = breakthrough(au, sr)
                    log(f"    {n:>5} | {conv:>11.4f} | {bt:>+9.3f}")
                    if n == 0:
                        write_wav(OUT / f"{mode}_n{int(noise*100):02d}_round{n}.wav", au, sr)
                    if n == ROUNDS - 1:
                        write_wav(OUT / f"{mode}_n{int(noise*100):02d}_round{n}.wav", au, sr)
                    prev = (au, sr)
                    if mode == "selffeed":
                        # feed THIS output back as next round's seed
                        seed_b64 = encode_init_audio(np.ascontiguousarray(au, np.float32))
                        seed_sr, seed_ch = int(sr), au.shape[0]
                    # extcap: leave seed_* as the static pad
                log("")

        log("=== READOUT ===")
        log("  extcap + static prompt: conv≈1.0 every round AND samba−pad flat ⇒ identical output,")
        log("    the loop CANNOT walk to the prompt (static seed re-injected each round, no accumulation).")
        log("  selffeed: does samba−pad RISE over rounds (prompt accumulates) or stick (attractor)?")
        log(f"\nWAVs in {OUT}")
        try: subprocess.run(["open", str(OUT)], check=False)
        except Exception: pass
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
