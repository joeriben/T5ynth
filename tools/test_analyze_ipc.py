#!/usr/bin/env python3
"""IPC smoke-test for the backend ``analyze`` op (the CLAP ear of the semantic loop).

Spawns backend/pipe_inference.py over the SAME stdin/stdout binary protocol the
plugin's PipeInference uses (never a direct import — the op must work across the
real IPC boundary), sends a pure sine and white noise as init_audio, and checks
that CLAP + the DSP spectral_words tell them apart:

  - a 440 Hz sine  → spectral "...tonal", CLAP tags in the tonal/clean register
  - white noise    → spectral "...noisy", CLAP tags in the noisy/harsh register

Also checks the error path (analyze with no audio → \\x00 error frame).

Run with the dev venv:  .venv/bin/python tools/test_analyze_ipc.py
CLAP (laion/clap-htsat-unfused, ~0.6 GB) loads on first use; it is already in
the HF cache here, so this is a few seconds, not a download.
"""
import json
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np

REPO_ROOT = Path(__file__).resolve().parents[1]
BACKEND_SCRIPT = REPO_ROOT / "backend" / "pipe_inference.py"
SR = 44_100          # native rate → exercises the backend's resample-to-48k branch
DUR_S = 3.0
TOPK = 6


# ── IPC client (mirrors the plugin's PipeInference; \x03 text frame) ──────────

class PipeProtocolError(RuntimeError):
    pass


class PipeClient:
    def __init__(self, command):
        self.process = subprocess.Popen(
            command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=None, text=False,
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

    def _read_exact(self, n):
        buf = bytearray()
        while len(buf) < n:
            chunk = self.stdout.read(n - len(buf))
            if not chunk:
                raise PipeProtocolError(
                    f"Backend closed pipe while reading {n} bytes (got {len(buf)})"
                )
            buf.extend(chunk)
        return bytes(buf)

    def _read_ready(self):
        head = self._read_exact(1)
        if head == b"\x00":
            n = struct.unpack("<I", self._read_exact(4))[0]
            raise PipeProtocolError(self._read_exact(n).decode("utf-8", "replace"))
        if head != b"\x02":
            raise PipeProtocolError(f"Unexpected ready byte: {head!r}")
        n = struct.unpack("<H", self._read_exact(2))[0]
        return json.loads(self._read_exact(n).decode("utf-8"))

    def request_text(self, payload):
        """Send a request, read a TEXT frame (\\x03 + uint32 len + UTF-8)."""
        data = (json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8")
        self.stdin.write(data)
        self.stdin.flush()
        head = self._read_exact(1)
        if head == b"\x00":
            n = struct.unpack("<I", self._read_exact(4))[0]
            raise PipeProtocolError(self._read_exact(n).decode("utf-8", "replace"))
        if head != b"\x03":
            raise PipeProtocolError(f"Unexpected text response byte: {head!r}")
        n = struct.unpack("<I", self._read_exact(4))[0]
        return self._read_exact(n).decode("utf-8", "replace")


def encode_init_audio(audio):
    """Planar (channel-major) float32 LE → base64, matching the backend decode."""
    import base64
    return base64.b64encode(np.ascontiguousarray(audio, dtype="<f4").tobytes()).decode("ascii")


def analyze(client, audio_1xN, topk=TOPK):
    payload = {
        "mode": "analyze",
        "init_audio_b64": encode_init_audio(audio_1xN),
        "init_audio_sr": SR,
        "init_audio_channels": int(audio_1xN.shape[0]),
        "topk": topk,
    }
    return json.loads(client.request_text(payload))


def main():
    if not BACKEND_SCRIPT.is_file():
        print(f"ERROR: backend script not found at {BACKEND_SCRIPT}", file=sys.stderr)
        sys.exit(1)

    n = int(SR * DUR_S)
    t = np.arange(n) / SR
    sine = (0.5 * np.sin(2 * np.pi * 440.0 * t)).astype(np.float32).reshape(1, n)
    rng = np.random.default_rng(0)
    noise = (0.1 * rng.standard_normal(n)).astype(np.float32).reshape(1, n)

    command = [sys.executable, str(BACKEND_SCRIPT)]
    print(f"Spawning backend: {' '.join(command)}", file=sys.stderr)
    client = PipeClient(command)
    failures = []
    try:
        info = client.info
        print(f"Backend ready. devices={info.get('devices')} "
              f"default={info.get('default')} models={info.get('models')}",
              file=sys.stderr)

        print("\n[analyze] 440 Hz sine ...", file=sys.stderr)
        s = analyze(client, sine)
        print(f"  sine  -> tags=[{s['tags']}]  spectral=[{s['spectral']}]  ({s.get('model')})")

        print("[analyze] white noise ...", file=sys.stderr)
        w = analyze(client, noise)
        print(f"  noise -> tags=[{w['tags']}]  spectral=[{w['spectral']}]  ({w.get('model')})")

        # ── sanity checks (robust DSP/CLAP facts) ────────────────────────────
        if "tonal" not in s["spectral"]:
            failures.append(f"sine spectral should be 'tonal', got '{s['spectral']}'")
        if "noisy" not in w["spectral"]:
            failures.append(f"noise spectral should be 'noisy', got '{w['spectral']}'")
        if s["spectral"] == w["spectral"]:
            failures.append("sine and noise produced identical spectral words")
        if s["tags"] == w["tags"]:
            failures.append("sine and noise produced identical CLAP tags (ear not discriminating)")
        if not s["tags"] or not w["tags"]:
            failures.append("empty CLAP tags")

        # ── error path: analyze with no audio → \x00 error frame ─────────────
        print("[analyze] no-audio error path ...", file=sys.stderr)
        try:
            client.request_text({"mode": "analyze"})
            failures.append("analyze with no init_audio should have errored, but returned a text frame")
        except PipeProtocolError as e:
            print(f"  correctly errored: {e}")
    finally:
        client.close()

    if failures:
        print("\nFAIL:", file=sys.stderr)
        for f in failures:
            print(f"  - {f}", file=sys.stderr)
        sys.exit(1)
    print("\nPASS: analyze op distinguishes sine (tonal) from noise (noisy), "
          "CLAP tags differ, error path works.")


if __name__ == "__main__":
    main()
