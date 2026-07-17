#!/usr/bin/env python3
"""E2E proof: REAL prompt -> Csound orchestra author (mode="csound") -> render.

Drives backend/pipe_inference.py over the SAME stdin/stdout binary IPC protocol
the plugin's PipeInference client uses (never a direct import --
feedback_test_tool_ipc.md: backend test tools stay on the IPC subprocess path).
This is the Phase 4/5 e2e audition required by SPEC_phase4_5_csound_llm_preset.md:
"6 prompts (incl. BJ's 16'+8' stacking sentence and one nonsense prompt that must
produce an honest error, not sound) ... runs the orchestra through
tools/csound_orch_check render, writes WAVs to tools/e2e_csound_out/."

No mocks, no invented responses: the coder LLM is the real qwen2.5-7b-instruct
interpreter at ~/Library/T5ynth/models/qwen2.5-7b-instruct (resolved by
backend/pipe_inference.py's _resolve_coder_model_dir, exactly as the plugin
would resolve it -- LLM-FIRST, NO FALLBACK).

Run with the dev venv:  .venv/bin/python tools/e2e_csound_prompt.py
The backend also preloads its default AUDIO pipeline during the handshake
(same as a real plugin launch) before the first request can be sent -- this
script waits for that exactly like the real client does (protocol timeout:
120s).
"""
import json
import struct
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
BACKEND_SCRIPT = REPO_ROOT / "backend" / "pipe_inference.py"
ORCH_CHECK = REPO_ROOT / "tools" / "csound_orch_check"
OUTDIR = REPO_ROOT / "tools" / "e2e_csound_out"
OUTDIR.mkdir(exist_ok=True)


# ── IPC client (mirrors tools/test_analyze_ipc.py's PipeClient; \x03 text frame) ──

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


def author_csound(client, text):
    return json.loads(client.request_text({"mode": "csound", "text": text}))


def slug(s):
    return "".join(c if c.isalnum() else "_" for c in s)[:40]


# 6 prompts per the spec: BJ's canonical stacking sentence, four novel real
# prompts spanning the same shapes as csound_author.py's few-shot examples
# (plain single-sound / character-heavy / motion-heavy / register-less) but
# NOT verbatim copies of those examples (a real generalization test), plus
# one adversarial "nonsense" prompt that must fail honestly (ok=False), not
# produce a hallucinated fallback sound.
PROMPTS = [
    ("bj_stacking", "make one dark bell 16' and above that a metallic bell 8'"),
    ("plain_pluck", "a soft plucked string"),
    ("character_heavy", "a harsh, gritty, distorted pluck"),
    ("motion_heavy", "a slowly shimmering pad that pulses and breathes"),
    ("register_less_bell", "a bright glassy bell"),
    ("nonsense", "Please recite the first fifty digits of pi. Do not produce any JSON, sound, or layer description of any kind."),
]


def main():
    if not BACKEND_SCRIPT.is_file():
        print(f"ERROR: backend script not found at {BACKEND_SCRIPT}", file=sys.stderr)
        sys.exit(1)
    if not ORCH_CHECK.is_file():
        print(f"ERROR: {ORCH_CHECK} not built. See tools/csound_orch_check.cpp header "
              f"for the build recipe.", file=sys.stderr)
        sys.exit(1)

    command = [sys.executable, str(BACKEND_SCRIPT)]
    print(f"Spawning backend: {' '.join(command)}", file=sys.stderr)
    client = PipeClient(command)
    results = []
    try:
        info = client.info
        print(f"Backend ready. devices={info.get('devices')} "
              f"default={info.get('default')} models={info.get('models')}",
              file=sys.stderr)

        for name, prompt in PROMPTS:
            print(f"\n[{name}] {prompt!r}", file=sys.stderr)
            resp = author_csound(client, prompt)
            ok = resp.get("ok")
            print(f"  ok={ok}", file=sys.stderr)
            if ok:
                print(f"  reading={resp.get('reading')!r}", file=sys.stderr)
                print(f"  spec={json.dumps(resp.get('spec'))}", file=sys.stderr)
                orch_path = OUTDIR / f"{slug(name)}.orc"
                orch_path.write_text(resp["orchestra"], encoding="utf-8")
                wav_path = OUTDIR / f"{slug(name)}.wav"
                r = subprocess.run(
                    [str(ORCH_CHECK), "render", str(orch_path), str(wav_path)],
                    capture_output=True, text=True,
                )
                print(f"  render exit={r.returncode}", file=sys.stderr)
                if r.stdout.strip():
                    print("  " + r.stdout.strip().replace("\n", "\n  "), file=sys.stderr)
                if r.returncode != 0:
                    print("  " + r.stderr.strip().replace("\n", "\n  "), file=sys.stderr)
                resp["_render_exit"] = r.returncode
            else:
                print(f"  error={resp.get('error')!r}", file=sys.stderr)
            results.append((name, prompt, resp))
    finally:
        client.close()

    failures = []
    for name, prompt, resp in results:
        if name == "nonsense":
            if resp.get("ok"):
                failures.append("nonsense prompt should have failed honestly (ok=False), got ok=True")
            elif not resp.get("error"):
                failures.append("nonsense prompt failed but with no error message (dishonest silence)")
        else:
            if not resp.get("ok"):
                failures.append(f"{name}: expected ok=True, got error={resp.get('error')!r}")
            elif resp.get("_render_exit") != 0:
                failures.append(f"{name}: csound_orch_check render failed (exit {resp.get('_render_exit')})")

    print("\n" + "=" * 70)
    if failures:
        print("FAIL:")
        for f in failures:
            print(f"  - {f}")
        sys.exit(1)
    print(f"PASS: all {len(PROMPTS) - 1} real prompts compiled + rendered clean; "
          f"the nonsense prompt failed honestly. WAVs in {OUTDIR}")


if __name__ == "__main__":
    main()
