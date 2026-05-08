#!/usr/bin/env python3
"""Listening-test renderer for prompt-injection mode candidates.

Renders the same prompt pair under three candidate mixing modes so a
listener can decide whether the modes produce audibly different,
musically interesting results before any UI work happens.

Modes (backend field "injection_mode"):
  linear     — current T5ynth crossfade: (0.5 - 0.5α)·A + (0.5 + 0.5α)·B
  delta      — A + α·(B - null), preserves A more strongly
  late_step  — first 60% of sampler steps see A only; remaining steps see
               the linear blend. Implemented in the backend as a
               DiTWrapper.forward swap. Native pipeline only
               (stable-audio-open-small).

Talks to backend/pipe_inference.py via the same stdin/stdout binary IPC
the plugin uses, so this exercises the real production code path.

Output: WAVs + manifest.json under tools/injection_test_output/.

Run:
  python3 tools/test_injection_modes.py
"""

from __future__ import annotations

import json
import struct
import subprocess
import sys
import time
import wave
from pathlib import Path

import numpy as np

# ── Listening-test configuration ─────────────────────────────────────
PROMPT_PAIRS = [
    ("warm analog bass drone", "metallic glass shimmer texture"),
    ("soft felt piano motif",  "granular noise burst"),
]
SEEDS = [101]
ALPHAS = [-0.6, 0.0, 0.6]
STEPS = 8
CFG_SCALE = 4.0
DURATION_S = 3.0
LATE_STEP_TRANSITION = 0.6
MODEL_NAME = "stable-audio-open-small"
MODES = ("linear", "delta", "late_step")

REPO_ROOT = Path(__file__).resolve().parents[1]
BACKEND_SCRIPT = REPO_ROOT / "backend" / "pipe_inference.py"
OUT_DIR = Path(__file__).resolve().parent / "injection_test_output"


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


# ── WAV writer ───────────────────────────────────────────────────────

def write_wav(path: Path, audio: np.ndarray, sr: int):
    audio = np.clip(audio, -1.0, 1.0).astype(np.float32)
    pcm = (audio.T * 32767.0).round().astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(audio.shape[0])
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(pcm.tobytes())


# ── Main ─────────────────────────────────────────────────────────────

def slug_for(pair_idx: int, mode: str, seed: int, alpha: float) -> str:
    sign = "p" if alpha >= 0 else "m"
    return f"pair{pair_idx}_{mode}_seed{seed}_a{sign}{abs(alpha):.2f}".replace(".", "")


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
        print(f"Backend ready. Devices: {info.get('devices')}, default: "
              f"{info.get('default')}, models: {info.get('models')}", file=sys.stderr)
        if MODEL_NAME not in info.get("models", []):
            print(f"ERROR: model '{MODEL_NAME}' not in backend's discovered models",
                  file=sys.stderr)
            sys.exit(1)

        manifest = []
        total = len(PROMPT_PAIRS) * len(MODES) * len(SEEDS) * len(ALPHAS)
        n = 0

        for pair_idx, (prompt_a, prompt_b) in enumerate(PROMPT_PAIRS):
            for mode in MODES:
                for seed in SEEDS:
                    for alpha in ALPHAS:
                        n += 1
                        slug = slug_for(pair_idx, mode, seed, alpha)
                        payload = {
                            "model": MODEL_NAME,
                            "prompt_a": prompt_a,
                            "prompt_b": prompt_b,
                            "alpha": alpha,
                            "duration": DURATION_S,
                            "steps": STEPS,
                            "cfg_scale": CFG_SCALE,
                            "seed": seed,
                            "injection_mode": mode,
                            "injection_transition_at": LATE_STEP_TRANSITION,
                        }
                        t0 = time.time()
                        result = client.request(payload)
                        wall = time.time() - t0
                        wav_path = OUT_DIR / f"{slug}.wav"
                        write_wav(wav_path, result["audio"], result["sample_rate"])
                        rms = float(np.sqrt(np.mean(result["audio"] ** 2)))
                        backend_s = result["elapsed_ms"] / 1000.0
                        print(f"[{n}/{total}] {slug}  backend={backend_s:.1f}s "
                              f"wall={wall:.1f}s  rms={rms:.4f}", file=sys.stderr)
                        manifest.append({
                            "pair_idx": pair_idx,
                            "mode": mode,
                            "seed": seed,
                            "alpha": alpha,
                            "wav": wav_path.name,
                            "backend_elapsed_s": round(backend_s, 2),
                            "wall_elapsed_s": round(wall, 2),
                            "rms": round(rms, 4),
                            "prompt_a": prompt_a,
                            "prompt_b": prompt_b,
                            "steps": STEPS,
                            "cfg_scale": CFG_SCALE,
                            "duration_s": DURATION_S,
                            "late_step_transition": (
                                LATE_STEP_TRANSITION if mode == "late_step" else None
                            ),
                        })

        with open(OUT_DIR / "manifest.json", "w") as f:
            json.dump(manifest, f, indent=2)
        print(f"\nDone. {len(manifest)} clips in {OUT_DIR}", file=sys.stderr)
        print("Listen in groups: same pair_idx + same alpha across modes.",
              file=sys.stderr)
    finally:
        client.close()


if __name__ == "__main__":
    main()
