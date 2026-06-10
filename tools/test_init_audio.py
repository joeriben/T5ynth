#!/usr/bin/env python3
"""Listening + correlation test for the init_audio (i2i / resynthesis) path.

Proves the new backend plumbing end-to-end over the real stdin/stdout binary
IPC the plugin uses (backend/pipe_inference.py), not by importing internals:

  1. Render a SOURCE clip from a text prompt (no init_audio).
  2. Re-feed that exact waveform as ``init_audio`` while conditioning on a
     DIFFERENT target prompt, sweeping ``init_noise_level`` from low (stay close
     to the source) to 1.0 (full re-generation from the target text).
  3. Render the pure-text target (no init_audio) as the noise=∞ reference.

Quantitative check: Pearson correlation of each i2i output against the source
should fall monotonically as init_noise_level rises — if init_audio were being
ignored, every output would be the text-only clip and correlation-to-source
would be flat. Listen too: low noise_level keeps the source's gesture, high
noise_level reverts to the target prompt.

Transport (matches backend _decode_init_audio_request): the source audio goes
back as base64'd float32 little-endian PCM, planar (channel-major), with
init_audio_sr / init_audio_channels describing it.

Output: WAVs + manifest.json under tools/init_audio_test_output/.

Run:
  python3 tools/test_init_audio.py
"""

from __future__ import annotations

import base64
import json
import struct
import subprocess
import sys
import time
import wave
from pathlib import Path

import numpy as np

# ── Test configuration ───────────────────────────────────────────────
SOURCE_PROMPT = "warm analog bass drone"
TARGET_PROMPT = "metallic glass shimmer bell texture"
SOURCE_SEED = 101
TARGET_SEED = 202
NOISE_LEVELS = [0.2, 0.4, 0.6, 0.8, 1.0]
STEPS = 8
CFG_SCALE = 4.0
DURATION_S = 3.0
# Default SAO (generate_diffusion_cond). Pass a model id as argv[1] to test
# another engine — e.g. "stable-audio-3-small-music" exercises SA3's
# generate_diffusion_cond_inpaint path (init_audio with no inpaint mask), which
# is the path the SA3-gated Resynth UI actually uses.
PREFERRED_MODEL = sys.argv[1] if len(sys.argv) > 1 else "stable-audio-open-small"

REPO_ROOT = Path(__file__).resolve().parents[1]
BACKEND_SCRIPT = REPO_ROOT / "backend" / "pipe_inference.py"
OUT_DIR = Path(__file__).resolve().parent / "init_audio_test_output"


# ── IPC client (mirrors plugin's PipeInference) ──────────────────────

class PipeProtocolError(RuntimeError):
    pass


class PipeClient:
    def __init__(self, command: list[str]):
        self.process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=None,
            text=False,
        )
        self.stdin = self.process.stdin
        self.stdout = self.process.stdout
        self.info = self._read_ready()

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
                    self.process.wait(timeout=5)

    def _read_exact(self, n: int) -> bytes:
        buf = bytearray()
        while len(buf) < n:
            chunk = self.stdout.read(n - len(buf))
            if not chunk:
                raise PipeProtocolError(
                    f"Backend closed pipe while reading {n} bytes (got {len(buf)})"
                )
            buf.extend(chunk)
        return bytes(buf)

    def _read_ready(self) -> dict:
        head = self._read_exact(1)
        if head == b"\x00":
            n = struct.unpack("<I", self._read_exact(4))[0]
            raise PipeProtocolError(self._read_exact(n).decode("utf-8", "replace"))
        if head != b"\x02":
            raise PipeProtocolError(f"Unexpected ready byte: {head!r}")
        n = struct.unpack("<H", self._read_exact(2))[0]
        return json.loads(self._read_exact(n).decode("utf-8"))

    def request(self, payload: dict) -> dict:
        data = (json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8")
        self.stdin.write(data)
        self.stdin.flush()

        head = self._read_exact(1)
        if head == b"\x00":
            n = struct.unpack("<I", self._read_exact(4))[0]
            raise PipeProtocolError(self._read_exact(n).decode("utf-8", "replace"))
        if head != b"\x01":
            raise PipeProtocolError(f"Unexpected response byte: {head!r}")

        flag, samples, channels, sr, seed, elapsed_ms = struct.unpack(
            "<iiiiif", self._read_exact(24)
        )
        if flag != 1:
            raise PipeProtocolError(f"Unexpected audio flag: {flag}")

        pcm = np.frombuffer(self._read_exact(samples * channels * 4), dtype="<f4").copy()
        audio = pcm.reshape(channels, samples)

        # Drain optional embedding stats trailer
        num_dims = struct.unpack("<H", self._read_exact(2))[0]
        if num_dims:
            self._read_exact(num_dims * 3 * 4)

        return {"audio": audio, "sample_rate": sr, "seed": seed, "elapsed_ms": elapsed_ms}


# ── Helpers ──────────────────────────────────────────────────────────

def write_wav(path: Path, audio: np.ndarray, sr: int):
    audio = np.clip(audio, -1.0, 1.0).astype(np.float32)
    pcm = (audio.T * 32767.0).round().astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(audio.shape[0])
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(pcm.tobytes())


def encode_init_audio(audio: np.ndarray) -> str:
    """Planar (channel-major) float32 LE → base64, matching the backend decode."""
    return base64.b64encode(np.ascontiguousarray(audio, dtype="<f4").tobytes()).decode("ascii")


def corr_to(reference: np.ndarray, other: np.ndarray) -> float:
    """Pearson correlation of two clips, mono-mixed and trimmed to common length."""
    a = reference.mean(axis=0)
    b = other.mean(axis=0)
    n = min(a.shape[-1], b.shape[-1])
    a, b = a[:n], b[:n]
    if n < 2 or np.std(a) < 1e-9 or np.std(b) < 1e-9:
        return float("nan")
    return float(np.corrcoef(a, b)[0, 1])


# ── Main ─────────────────────────────────────────────────────────────

def main():
    if not BACKEND_SCRIPT.is_file():
        print(f"ERROR: backend script not found at {BACKEND_SCRIPT}", file=sys.stderr)
        sys.exit(1)

    OUT_DIR.mkdir(exist_ok=True)
    command = [sys.executable, str(BACKEND_SCRIPT)]
    print(f"Spawning backend: {' '.join(command)}", file=sys.stderr)
    client = PipeClient(command)
    try:
        info = client.info
        models = info.get("models", [])
        print(f"Backend ready. Devices: {info.get('devices')}, default: "
              f"{info.get('default')}, models: {models}", file=sys.stderr)
        model = PREFERRED_MODEL if PREFERRED_MODEL in models else (models[0] if models else None)
        if not model:
            print("ERROR: backend discovered no models", file=sys.stderr)
            sys.exit(1)
        print(f"Using model: {model}", file=sys.stderr)

        base = {
            "model": model,
            "duration": DURATION_S,
            "steps": STEPS,
            "cfg_scale": CFG_SCALE,
        }

        # 1. SOURCE clip (text only).
        src = client.request({**base, "prompt_a": SOURCE_PROMPT, "seed": SOURCE_SEED})
        sr = src["sample_rate"]
        write_wav(OUT_DIR / "source.wav", src["audio"], sr)
        print(f"source: {src['audio'].shape} @ {sr}Hz  "
              f"backend={src['elapsed_ms']/1000:.1f}s", file=sys.stderr)
        init_b64 = encode_init_audio(src["audio"])
        init_ch = src["audio"].shape[0]

        # 2. Pure-text target reference (no init_audio) — the noise=∞ end of the sweep.
        tgt = client.request({**base, "prompt_a": TARGET_PROMPT, "seed": TARGET_SEED})
        write_wav(OUT_DIR / "target_textonly.wav", tgt["audio"], sr)
        corr_tgt_src = corr_to(src["audio"], tgt["audio"])
        print(f"target (text only): corr-to-source={corr_tgt_src:+.3f}", file=sys.stderr)

        # 3. i2i sweep: target prompt + source as init_audio, rising noise level.
        manifest = {
            "model": model,
            "sample_rate": sr,
            "source_prompt": SOURCE_PROMPT,
            "target_prompt": TARGET_PROMPT,
            "source_seed": SOURCE_SEED,
            "target_seed": TARGET_SEED,
            "steps": STEPS,
            "cfg_scale": CFG_SCALE,
            "duration_s": DURATION_S,
            "target_textonly_corr_to_source": round(corr_tgt_src, 4),
            "sweep": [],
        }
        corrs = []
        for lvl in NOISE_LEVELS:
            res = client.request({
                **base,
                "prompt_a": TARGET_PROMPT,
                "seed": TARGET_SEED,
                "init_audio_b64": init_b64,
                "init_audio_sr": sr,
                "init_audio_channels": init_ch,
                "init_noise_level": lvl,
            })
            c = corr_to(src["audio"], res["audio"])
            corrs.append(c)
            name = f"i2i_noise{lvl:.2f}".replace(".", "") + ".wav"
            write_wav(OUT_DIR / name, res["audio"], sr)
            print(f"i2i noise={lvl:.2f}: corr-to-source={c:+.3f}  "
                  f"backend={res['elapsed_ms']/1000:.1f}s", file=sys.stderr)
            manifest["sweep"].append({
                "init_noise_level": lvl,
                "wav": name,
                "corr_to_source": round(c, 4),
            })

        # Mono init_audio leg: exercises the documented default (channels=1).
        # prepare_audio's PadCrop unpacks "n, s = signal.shape", so a 1-D [frames]
        # tensor would crash here downstream of the decode try/except — this leg
        # is the integration guard for that. Downmix the source to one channel.
        mono_src = src["audio"].mean(axis=0, keepdims=True)  # [1, frames]
        mono_res = client.request({
            **base,
            "prompt_a": TARGET_PROMPT,
            "seed": TARGET_SEED,
            "init_audio_b64": encode_init_audio(mono_src),
            "init_audio_sr": sr,
            "init_audio_channels": 1,
            "init_noise_level": 0.2,
        })
        mono_corr = corr_to(src["audio"], mono_res["audio"])
        write_wav(OUT_DIR / "i2i_mono_noise020.wav", mono_res["audio"], sr)
        print(f"i2i MONO noise=0.20: corr-to-source={mono_corr:+.3f}  "
              f"backend={mono_res['elapsed_ms']/1000:.1f}s", file=sys.stderr)
        manifest["mono_leg"] = {
            "init_noise_level": 0.2,
            "wav": "i2i_mono_noise020.wav",
            "corr_to_source": round(mono_corr, 4),
        }

        with open(OUT_DIR / "manifest.json", "w") as f:
            json.dump(manifest, f, indent=2)

        # Verdict: correlation-to-source should trend DOWN as noise rises, and
        # the lowest-noise i2i must hug the source far more than the text-only
        # target does. Both fail if init_audio is ignored.
        valid = [(l, c) for l, c in zip(NOISE_LEVELS, corrs) if not np.isnan(c)]
        print("\n── verdict ──", file=sys.stderr)
        ok = True
        if len(valid) >= 2:
            low_corr = valid[0][1]
            high_corr = valid[-1][1]
            trend_ok = low_corr > high_corr
            hug_ok = low_corr > corr_tgt_src if not np.isnan(corr_tgt_src) else low_corr > 0.1
            print(f"corr@{valid[0][0]:.2f}={low_corr:+.3f} > "
                  f"corr@{valid[-1][0]:.2f}={high_corr:+.3f} ? {'YES' if trend_ok else 'NO'}",
                  file=sys.stderr)
            print(f"low-noise i2i hugs source more than text-only target "
                  f"({low_corr:+.3f} > {corr_tgt_src:+.3f}) ? {'YES' if hug_ok else 'NO'}",
                  file=sys.stderr)
            mono_ok = (not np.isnan(mono_corr)) and mono_corr > 0.3
            print(f"mono (default channels=1) i2i works end-to-end "
                  f"(corr={mono_corr:+.3f} > 0.3) ? {'YES' if mono_ok else 'NO'}",
                  file=sys.stderr)
            ok = trend_ok and hug_ok and mono_ok
        else:
            print("not enough valid correlations to judge", file=sys.stderr)
            ok = False
        print(f"\nDone. {len(NOISE_LEVELS)+2} clips in {OUT_DIR}", file=sys.stderr)
        print(f"INIT_AUDIO WIRED: {'PASS' if ok else 'CHECK MANUALLY'}", file=sys.stderr)
        sys.exit(0 if ok else 2)
    finally:
        client.close()


if __name__ == "__main__":
    main()
