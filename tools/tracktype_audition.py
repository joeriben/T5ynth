#!/usr/bin/env python3
"""A/B audition: TrackType: Music vs TrackType: Instrument on stable-audio-3-medium.

FAITHFUL to the plugin's request path. Two lessons baked in:

1. Single-prompt promptability guard lives in C++ (PromptPanel::buildInferenceRequest,
   ~line 2188), NOT the backend. The linear blend is (0.5-0.5α)·A + (0.5+0.5α)·B, so a
   lone prompt at α=0 (the backend default!) renders the null/unconditional output —
   on SA3 a prompt-invariant buzz. The plugin pins α to ±1 when exactly one field is
   filled. So here we drive the sound through prompt_b with α=+1.0 (pure B), mirroring
   the plugin's B-only behaviour. (Earlier harness sent prompt_a only at α=0 → collapse,
   output was identical across every prompt — that was a TEST bug, not the model.)

2. The backend ALREADY prepends the modality prefix from track_type. So we send plain
   prompts + track_type="music"/"instrument" and let _native_modality_prefix add exactly
   one "TrackType: ..." — never type the prefix into the prompt (that would double it).

Everything except track_type is held identical, so any audible difference is the prefix.
A sanity gate first proves the request path actually conditions (different prompts must
diverge); only then is the Music-vs-Instrument delta meaningful.

Run with the project venv:
  .venv/bin/python tools/tracktype_audition.py
"""
from __future__ import annotations
import json, struct, subprocess, sys, time, wave
from pathlib import Path
import numpy as np

REPO = Path(__file__).resolve().parents[1]
BACKEND = REPO / "backend" / "pipe_inference.py"
OUT = Path(__file__).resolve().parent / "tracktype_audition_out"

MODEL = "stable-audio-3-medium"   # full matrix: Music, Instrument AND SFX
# SA3 is ARC-distilled: the correct regime is steps=8, cfg=1.0 — the plugin's per-model
# default (PromptPanel::defaultParamsFor matches "stable-audio-3" → 8/1.0, medium
# included; model_config training.demo demo_steps=8 / demo_cfg_scales=[1]; every SA3
# preset stores 8/1.0). Higher CFG overdrives it → harsh/broken audio.
DURATION, STEPS, CFG = 3.0, 8, 1.0
SEEDS = [1, 2]

# VERBATIM prompts from the UCDCAE AI Lab preset bank (synth.promptA/B). These are
# real, usable T5ynth prompts — they specify PITCH (c3/c4/g3) and often BPM/genre,
# unlike a generic "a synth pad". Single pitched-instrument voices = exactly where
# TrackType: Instrument ("isolated single instrument") should bite vs TrackType: Music.
PROMPTS = [
    ("saw_c3",      "steady saw wave, c3"),                                    # Clean Saw / Transistor / Vintage
    ("analog_c4",   "instrument: analog synthesizer, classic saw wave. c4"),  # HipHop 92 BPM
    ("trombone_c3", "a trombone, c3"),                                         # Wild Ducks
    ("bells_c3",    "silver bells, c3, 140bpm"),                               # Silver House
    ("funkbass_c3", "FUNKY res BASS LINE, c3"),                               # Funky D&B
]

VARIANTS = [("A_music", "music"), ("B_instrument", "instrument")]


class PipeClient:
    def __init__(self, command):
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=None, text=False)
        self.stdin, self.stdout = self.process.stdin, self.process.stdout
        self.info = self._read_ready()

    def _read_exact(self, n):
        buf = bytearray()
        while len(buf) < n:
            c = self.stdout.read(n - len(buf))
            if not c:
                raise RuntimeError(f"backend closed pipe ({len(buf)}/{n})")
            buf.extend(c)
        return bytes(buf)

    def _read_ready(self):
        h = self._read_exact(1)
        if h == b"\x00":
            n = struct.unpack("<I", self._read_exact(4))[0]
            raise RuntimeError(self._read_exact(n).decode("utf-8", "replace"))
        if h != b"\x02":
            raise RuntimeError(f"bad ready byte {h!r}")
        n = struct.unpack("<H", self._read_exact(2))[0]
        return json.loads(self._read_exact(n).decode("utf-8"))

    def request(self, payload):
        self.stdin.write((json.dumps(payload, separators=(",", ":")) + "\n").encode())
        self.stdin.flush()
        h = self._read_exact(1)
        if h == b"\x00":
            n = struct.unpack("<I", self._read_exact(4))[0]
            raise RuntimeError(self._read_exact(n).decode("utf-8", "replace"))
        if h != b"\x01":
            raise RuntimeError(f"bad response byte {h!r}")
        flag, samples, channels, sr, seed, elapsed_ms = struct.unpack("<iiiiif", self._read_exact(24))
        pcm = np.frombuffer(self._read_exact(samples * channels * 4), dtype="<f4").copy()
        audio = pcm.reshape(channels, samples)
        ndims = struct.unpack("<H", self._read_exact(2))[0]
        if ndims:
            self._read_exact(ndims * 3 * 4)
        return {"audio": audio, "sample_rate": sr, "seed": seed, "elapsed_ms": elapsed_ms}

    def close(self):
        try:
            if self.stdin and not self.stdin.closed:
                self.stdin.close()
        finally:
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.process.kill()


def write_wav(path, audio, sr):
    a = np.clip(audio, -1, 1).astype(np.float32)
    pcm = (a.T * 32767.0).round().astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(a.shape[0]); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(pcm.tobytes())


def xcorr(x, y):
    xm, ym = x.mean(0), y.mean(0)
    return float(np.dot(xm, ym) / (np.linalg.norm(xm) * np.linalg.norm(ym) + 1e-12))


def main():
    OUT.mkdir(exist_ok=True)
    for w in OUT.glob("*.wav"):   # clear this harness's own prior output (not user data)
        w.unlink()
    client = PipeClient([sys.executable, str(BACKEND)])
    audios = {}   # (slug, seed, tt) -> audio
    try:
        models = client.info.get("models", [])
        print(f"backend ready. device={client.info.get('default')} discovered={models}")
        if MODEL not in models:
            print(f"SKIP — {MODEL} not discovered (missing model_config.json?)")
            sys.exit(1)
        # PLUGIN-FAITHFUL single-prompt request: sound in prompt_b, α=+1 (pure B).
        base = {"model": MODEL, "duration": DURATION, "steps": STEPS, "cfg_scale": CFG,
                "prompt_a": "", "alpha": 1.0, "magnitude": 1.0, "injection_mode": "linear"}
        for slug, prompt in PROMPTS:
            for seed in SEEDS:
                for tag, tt in VARIANTS:
                    r = client.request({**base, "prompt_b": prompt, "seed": seed, "track_type": tt})
                    audio = r["audio"]
                    audios[(slug, seed, tt)] = audio
                    rms = float(np.sqrt(np.mean(audio ** 2)))
                    peak = float(np.abs(audio).max())
                    name = f"{slug}__seed{seed}__{tag}.wav"
                    write_wav(OUT / name, audio, r["sample_rate"])
                    print(f"  {slug:11} seed{seed} {tt:10} rms={rms:.4f} peak={peak:.3f} -> {name}")
    finally:
        client.close()

    # ── Sanity gate: does the request path condition AT ALL? ──
    # Different prompts (same seed+tt) must now DIVERGE. If xcorr≈1.0 the path is still
    # collapsed and the Music/Instrument comparison below is meaningless.
    print("\n=== SANITY GATE: different prompts must diverge (xcorr << 1.0) ===")
    slugs = [s for s, _ in PROMPTS]
    worst = 0.0
    for seed in SEEDS:
        for tt in ("music", "instrument"):
            for i in range(len(slugs)):
                for j in range(i + 1, len(slugs)):
                    cc = xcorr(audios[(slugs[i], seed, tt)], audios[(slugs[j], seed, tt)])
                    worst = max(worst, abs(cc))
    print(f"  worst |xcorr| across distinct-prompt pairs = {worst:.4f}  "
          f"({'COLLAPSED — invalid' if worst > 0.95 else 'OK — path conditions'})")

    # ── The actual question: Music vs Instrument, same prompt+seed ──
    print("\n=== MUSIC vs INSTRUMENT (same prompt+seed): how much does the prefix change? ===")
    print(f"{'prompt':11} {'seed':>4} {'xcorr':>7} {'rms(Δ)':>8}  interpretation")
    for slug in slugs:
        for seed in SEEDS:
            m = audios[(slug, seed, "music")]
            i = audios[(slug, seed, "instrument")]
            cc = xcorr(m, i)
            rmsd = float(np.sqrt(np.mean((m - i) ** 2)))
            tag = "identical" if cc > 0.999 else ("subtle" if cc > 0.8 else "STRONG change")
            print(f"{slug:11} {seed:>4} {cc:7.4f} {rmsd:8.4f}  {tag}")

    print(f"\n{len(audios)} WAVs in {OUT}")
    print("Listen A (music) vs B (instrument) per prompt+seed — names sort adjacently.")


if __name__ == "__main__":
    main()
