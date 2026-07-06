#!/usr/bin/env python3
"""Phase-0 viability probe for stable-audio-3-medium over the REAL IPC backend.

Measures, for medium and (for ratio) small-music, via the same stdin/stdout
binary protocol the plugin uses:
  - cold latency  : first request wall-clock (includes lazy model load)
  - warm latency  : steady-state backend elapsed_ms (model already resident)
  - resident RAM  : subprocess RSS sampled right after the model is loaded
  - audio sanity  : sample-rate + peak amplitude (non-silent)

Run (after the medium checkpoint has fully downloaded):
  python3 tools/test_medium_probe.py
"""
from __future__ import annotations
import json, struct, subprocess, sys, time, wave
from pathlib import Path
import numpy as np

REPO = Path(__file__).resolve().parents[1]
BACKEND = REPO / "backend" / "pipe_inference.py"
OUT = Path(__file__).resolve().parent / "medium_probe_out"
MED_CKPT = Path.home() / "Library/T5ynth/models/stable-audio-3-medium/model.safetensors"

DURATION, STEPS, CFG = 3.0, 8, 4.0
PROMPT = "warm cinematic strings swelling, lush hall reverb"
MODELS = ["stable-audio-3-medium", "stable-audio-3-small-music"]
SEEDS = [1, 2, 3]


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


def rss_mb(pid):
    out = subprocess.run(["ps", "-o", "rss=", "-p", str(pid)], capture_output=True, text=True).stdout.strip()
    return int(out) / 1024.0 if out else float("nan")


def write_wav(path, audio, sr):
    a = np.clip(audio, -1, 1).astype(np.float32)
    pcm = (a.T * 32767.0).round().astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(a.shape[0]); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(pcm.tobytes())


def probe(model):
    client = PipeClient([sys.executable, str(BACKEND)])
    try:
        models = client.info.get("models", [])
        print(f"\n[{model}] backend ready. device={client.info.get('default')} discovered={models}")
        if model not in models:
            print(f"[{model}] SKIP — not discovered (missing model_config.json?)")
            return None
        base = {"model": model, "duration": DURATION, "steps": STEPS, "cfg_scale": CFG}
        rows, rss, sr, peak = [], None, None, None
        for i, seed in enumerate(SEEDS):
            t0 = time.time()
            r = client.request({**base, "prompt_a": PROMPT, "seed": seed})
            wall = time.time() - t0
            sr, peak = r["sample_rate"], float(np.abs(r["audio"]).max())
            rows.append((wall, r["elapsed_ms"] / 1000.0))
            if i == 0:
                rss = rss_mb(client.process.pid)
                write_wav(OUT / f"{model}.wav", r["audio"], sr)
            print(f"  run{i} seed{seed}: wall={wall:5.1f}s  backend={r['elapsed_ms']/1000:5.1f}s  "
                  f"peak={peak:.3f}  sr={sr}")
        warm = min(b for _, b in rows[1:]) if len(rows) > 1 else rows[0][1]
        return {"model": model, "cold_wall": rows[0][0], "cold_backend": rows[0][1],
                "warm_backend": warm, "rss_mb": rss, "sr": sr, "peak": peak}
    finally:
        client.close()


def main():
    OUT.mkdir(exist_ok=True)
    if MED_CKPT.exists():
        gb = MED_CKPT.stat().st_size / 1e9
        if gb < 9.0:
            print(f"medium checkpoint only {gb:.2f} GB (< 9.0) — download not finished; aborting.")
            sys.exit(1)
    results = [r for r in (probe(m) for m in MODELS) if r]
    print("\n================ SUMMARY ================")
    print(f"{'model':32} {'cold_wall':>9} {'warm':>7} {'RSS':>9} {'peak':>6}")
    for r in results:
        print(f"{r['model']:32} {r['cold_wall']:8.1f}s {r['warm_backend']:6.1f}s "
              f"{r['rss_mb']/1024:7.2f}GB {r['peak']:6.3f}")
    if len(results) == 2:
        med, sml = results[0], results[1]
        print(f"\nmedium/small warm-latency ratio: {med['warm_backend']/sml['warm_backend']:.2f}x")
        print(f"medium/small RSS ratio:          {med['rss_mb']/sml['rss_mb']:.2f}x")
    print(f"\nWAVs in {OUT}")


if __name__ == "__main__":
    main()
