#!/usr/bin/env python3
"""Medium-only step x CFG sweep over the real IPC backend, with a glitch proxy.
Loads medium once (lazy on first request), then sweeps (steps, cfg) and reports a
spectral glitch metric per setting so we can tell 'needs more steps / CFG=1' apart
from 'fundamentally broken' without asking the user to listen each time.

  glitch proxy: flat (spectral flatness ~1=noise), zcr, dyn (frame-RMS CV; music>0.4)
"""
from __future__ import annotations
import json, struct, subprocess, sys, time, wave
from pathlib import Path
import numpy as np

REPO = Path(__file__).resolve().parents[1]
BACKEND = REPO / "backend" / "pipe_inference.py"
OUT = Path(__file__).resolve().parent / "medium_sweep_out"
MODEL = "stable-audio-3-medium"
PROMPT = "warm cinematic strings swelling, lush hall reverb"
DURATION = 3.0
SEED = 1
GRID = [(8, 1.0), (8, 4.0), (50, 1.0), (50, 4.0), (150, 1.0), (150, 7.0), (250, 1.0)]


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


def glitch_proxy(audio):
    x = audio.mean(0).astype(np.float32)
    f = np.abs(np.fft.rfft(x * np.hanning(len(x)))) + 1e-9
    p = f ** 2
    flat = float(np.exp(np.mean(np.log(p))) / np.mean(p))
    zcr = float(np.mean(np.abs(np.diff(np.sign(x)))) / 2)
    fr = np.array([np.sqrt(np.mean(x[i:i+2048] ** 2)) for i in range(0, len(x) - 2048, 2048)])
    dyn = float(fr.std() / (fr.mean() + 1e-9))
    return flat, zcr, dyn, float(np.sqrt(np.mean(x ** 2))), float(np.abs(x).max())


def write_wav(path, audio, sr):
    a = np.clip(audio, -1, 1).astype(np.float32)
    pcm = (a.T * 32767.0).round().astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(a.shape[0]); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(pcm.tobytes())


def main():
    OUT.mkdir(exist_ok=True)
    client = PipeClient([sys.executable, str(BACKEND)])
    print(f"backend ready device={client.info.get('default')} models={client.info.get('models')}")
    print(f"\n{'steps':>5} {'cfg':>5} {'backend_s':>9} {'flat':>7} {'zcr':>6} {'dyn':>6} {'rms':>6} {'verdict'}")
    try:
        for steps, cfg in GRID:
            t0 = time.time()
            r = client.request({"model": MODEL, "duration": DURATION, "steps": steps,
                                "cfg_scale": cfg, "prompt_a": PROMPT, "seed": SEED})
            flat, zcr, dyn, rms, peak = glitch_proxy(r["audio"])
            verdict = "MUSIC" if (flat < 0.01 and dyn > 0.4) else ("glitch" if flat > 0.05 else "?mixed")
            write_wav(OUT / f"med_s{steps}_cfg{cfg:g}.wav", r["audio"], r["sample_rate"])
            print(f"{steps:5d} {cfg:5.1f} {r['elapsed_ms']/1000:9.1f} {flat:7.3f} {zcr:6.3f} "
                  f"{dyn:6.2f} {rms:6.3f} {verdict}")
    finally:
        client.close()
    print(f"\nWAVs in {OUT}")


if __name__ == "__main__":
    main()
