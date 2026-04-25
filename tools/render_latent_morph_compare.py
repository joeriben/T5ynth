#!/usr/bin/env python3
"""Render embedding-vs-latent morph comparison clips through pipe_inference.

This is a listening-study helper for Stable Audio Open 1.0.
It renders:
  - endpoint A / endpoint B
  - embedding-space morphs between A and B
  - latent-space morphs between cached A/B latents
  - one control re-decode of cached A

Outputs:
  - WAV files
  - manifest.json
  - listening_sheet.csv
  - README.txt
"""

from __future__ import annotations

import argparse
import csv
import json
import struct
import subprocess
import sys
import time
import wave
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable

import numpy as np


@dataclass(frozen=True)
class PromptPair:
    slug: str
    prompt_a: str
    prompt_b: str


PAIR_LIBRARY = [
    PromptPair("piano_glass", "soft felt piano motif", "granular glass shimmer"),
    PromptPair("bass_choir", "warm analog bass pulse", "cathedral choir swell"),
    PromptPair("rain_strings", "rain on metal roof", "distant bowed string drone"),
    PromptPair("wood_turbine", "dry wooden percussion pattern", "industrial turbine hum"),
    PromptPair("forest_glitch", "birds in a wet forest", "glitch noise burst with sub bass"),
    PromptPair("guitar_drum_machine", "acoustic guitar harmonics", "distorted drum machine pulse"),
]

DEFAULT_EMBEDDING_ALPHAS = [-0.5, 0.0, 0.5]
DEFAULT_LATENT_ALPHAS = [0.25, 0.5, 0.75]
DEFAULT_SEEDS = [101, 202]


class PipeProtocolError(RuntimeError):
    pass


class PipeClient:
    def __init__(self, command: list[str]):
        self.command = command
        self.process = subprocess.Popen(
            command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=None,
            text=False,
        )
        assert self.process.stdin is not None
        assert self.process.stdout is not None
        self.stdin = self.process.stdin
        self.stdout = self.process.stdout
        self.info = self._read_ready()

    def close(self) -> None:
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

    def _read_exact(self, num_bytes: int) -> bytes:
        chunks = bytearray()
        while len(chunks) < num_bytes:
            chunk = self.stdout.read(num_bytes - len(chunks))
            if not chunk:
                raise PipeProtocolError(
                    f"Backend closed pipe while reading {num_bytes} bytes "
                    f"(got {len(chunks)})"
                )
            chunks.extend(chunk)
        return bytes(chunks)

    def _read_ready(self) -> dict:
        ready = self._read_exact(1)
        if ready == b"\x00":
            msg_len = struct.unpack("<I", self._read_exact(4))[0]
            msg = self._read_exact(msg_len).decode("utf-8", errors="replace")
            raise PipeProtocolError(f"Backend startup failed: {msg}")
        if ready != b"\x02":
            raise PipeProtocolError(f"Unexpected ready byte: {ready!r}")

        info_len = struct.unpack("<H", self._read_exact(2))[0]
        info = json.loads(self._read_exact(info_len).decode("utf-8"))
        return info

    def request(self, payload: dict) -> dict:
        data = (json.dumps(payload, separators=(",", ":")) + "\n").encode("utf-8")
        self.stdin.write(data)
        self.stdin.flush()

        status = self._read_exact(1)
        if status == b"\x00":
            msg_len = struct.unpack("<I", self._read_exact(4))[0]
            msg = self._read_exact(msg_len).decode("utf-8", errors="replace")
            raise PipeProtocolError(msg)
        if status != b"\x01":
            raise PipeProtocolError(f"Unexpected response byte: {status!r}")

        flag, samples, channels, sr, seed, elapsed_ms = struct.unpack(
            "<iiiiif", self._read_exact(24)
        )
        if flag != 1:
            raise PipeProtocolError(f"Unexpected audio flag: {flag}")

        pcm = np.frombuffer(self._read_exact(samples * channels * 4), dtype="<f4").copy()
        audio = pcm.reshape(channels, samples)

        num_dims = struct.unpack("<H", self._read_exact(2))[0]
        if num_dims:
            self._read_exact(num_dims * 3 * 4)

        return {
            "audio": audio,
            "sample_rate": sr,
            "seed": seed,
            "elapsed_ms": elapsed_ms,
        }


def parse_csv_floats(text: str) -> list[float]:
    return [float(part.strip()) for part in text.split(",") if part.strip()]


def parse_csv_ints(text: str) -> list[int]:
    return [int(part.strip()) for part in text.split(",") if part.strip()]


def resolve_pairs(selected_slugs: Iterable[str] | None) -> list[PromptPair]:
    if not selected_slugs:
        return list(PAIR_LIBRARY)

    by_slug = {pair.slug: pair for pair in PAIR_LIBRARY}
    resolved = []
    missing = []
    for slug in selected_slugs:
        pair = by_slug.get(slug)
        if pair is None:
            missing.append(slug)
        else:
            resolved.append(pair)
    if missing:
        raise SystemExit(f"Unknown pair slug(s): {', '.join(missing)}")
    return resolved


def default_output_dir(repo_root: Path) -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return repo_root / "dist" / "latent_morph_compare" / stamp


def resolve_backend_command(repo_root: Path, backend_arg: str | None) -> list[str]:
    if backend_arg:
        backend_path = Path(backend_arg).expanduser()
        if backend_path.suffix == ".py":
            python = repo_root / ".venv" / "bin" / "python"
            return [str(python if python.exists() else Path(sys.executable)), str(backend_path)]
        return [str(backend_path)]

    python = repo_root / ".venv" / "bin" / "python"
    backend_script = repo_root / "backend" / "pipe_inference.py"
    return [str(python if python.exists() else Path(sys.executable)), str(backend_script)]


def alpha_token(value: float) -> str:
    text = f"{value:+.2f}"
    return text.replace("+", "p").replace("-", "m")


def write_wav(path: Path, audio: np.ndarray, sample_rate: int) -> None:
    audio = np.asarray(audio, dtype=np.float32)
    if audio.ndim != 2:
        raise ValueError(f"Expected [channels, samples], got shape {audio.shape}")

    clipped = np.clip(audio, -1.0, 1.0)
    pcm = (clipped.T * 32767.0).round().astype("<i2")
    path.parent.mkdir(parents=True, exist_ok=True)

    with wave.open(str(path), "wb") as wav_file:
        wav_file.setnchannels(audio.shape[0])
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(pcm.tobytes())


def render_clip(
    client: PipeClient,
    request: dict,
    output_path: Path,
    records: list[dict],
    *,
    pair: PromptPair,
    seed: int,
    family: str,
    mix_value: float | None,
    description: str,
) -> None:
    started = time.time()
    result = client.request(request)
    write_wav(output_path, result["audio"], result["sample_rate"])
    wall_ms = (time.time() - started) * 1000.0

    print(
        f"[{pair.slug} seed={seed}] {description:<28} "
        f"-> {output_path.name} "
        f"(backend {result['elapsed_ms'] / 1000.0:.1f}s, wall {wall_ms / 1000.0:.1f}s)"
    )

    records.append(
        {
            "pair_slug": pair.slug,
            "seed": seed,
            "family": family,
            "mix_value": mix_value,
            "file": str(output_path),
            "prompt_a": pair.prompt_a,
            "prompt_b": pair.prompt_b,
            "backend_elapsed_ms": result["elapsed_ms"],
            "response_seed": result["seed"],
            "description": description,
        }
    )


def write_readme(
    path: Path,
    *,
    model: str,
    device: str,
    duration: float,
    steps: int,
    cfg_scale: float,
    embedding_alphas: list[float],
    latent_alphas: list[float],
    seeds: list[int],
) -> None:
    text = "\n".join(
        [
            "Latent Morph Listening Study",
            "",
            f"Model: {model}",
            f"Device: {device}",
            f"Duration: {duration:.2f}s",
            f"Steps: {steps}",
            f"CFG: {cfg_scale}",
            f"Seeds: {', '.join(str(seed) for seed in seeds)}",
            f"Embedding alphas: {', '.join(str(v) for v in embedding_alphas)}",
            f"Latent lerp alphas: {', '.join(str(v) for v in latent_alphas)}",
            "",
            "File groups per pair/seed:",
            "  00_endpoint_a.wav             Prompt A rendered with cache_as",
            "  01_endpoint_b.wav             Prompt B rendered with cache_as",
            "  02_control_decode_a.wav       Cached latent A decoded again",
            "  10_embedding_alpha_*.wav      Prompt-space interpolation",
            "  20_latent_alpha_*.wav         Latent-space interpolation",
            "",
            "Suggested listening questions:",
            "  1. Is latent morph audibly distinct from embedding morph?",
            "  2. Does it create a useful intermediate sonic space?",
            "  3. Does it remain recognizably anchored between A and B?",
            "  4. Would you keep it as an artistic mode rather than just a speed hack?",
            "",
        ]
    )
    path.write_text(text, encoding="utf-8")


def write_listening_sheet(path: Path, records: list[dict]) -> None:
    fieldnames = [
        "pair_slug",
        "seed",
        "family",
        "mix_value",
        "file",
        "prompt_a",
        "prompt_b",
        "description",
        "backend_elapsed_ms",
        "response_seed",
        "eigenstaendigkeit",
        "kontinuitaet",
        "prompttreue",
        "brauchbarkeit",
        "direkturteil",
        "notizen",
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for record in records:
            row = dict(record)
            row.update(
                {
                    "eigenstaendigkeit": "",
                    "kontinuitaet": "",
                    "prompttreue": "",
                    "brauchbarkeit": "",
                    "direkturteil": "",
                    "notizen": "",
                }
            )
            writer.writerow(row)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Render Stable Audio 1.0 embedding-vs-latent morph comparison clips."
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        help="Output directory. Default: dist/latent_morph_compare/<timestamp>",
    )
    parser.add_argument(
        "--backend",
        help="Optional backend executable or pipe_inference.py path. Default uses .venv + backend/pipe_inference.py",
    )
    parser.add_argument(
        "--model",
        default="stable-audio-open-1.0",
        help="Model id to request from the backend. Default: stable-audio-open-1.0",
    )
    parser.add_argument(
        "--device",
        default="",
        help="Explicit backend device. Default: backend reported default",
    )
    parser.add_argument(
        "--pair",
        action="append",
        dest="pairs",
        help="Pair slug to render. Repeat to limit the run. Default: all pairs",
    )
    parser.add_argument(
        "--list-pairs",
        action="store_true",
        help="Print available pair slugs and exit",
    )
    parser.add_argument(
        "--seeds",
        default="101,202",
        help="Comma-separated seeds. Default: 101,202",
    )
    parser.add_argument(
        "--embedding-alphas",
        default="-0.5,0.0,0.5",
        help="Comma-separated embedding alphas. Default: -0.5,0.0,0.5",
    )
    parser.add_argument(
        "--latent-alphas",
        default="0.25,0.5,0.75",
        help="Comma-separated latent lerp alphas. Default: 0.25,0.5,0.75",
    )
    parser.add_argument(
        "--duration",
        type=float,
        default=4.0,
        help="Clip duration in seconds. Default: 4.0",
    )
    parser.add_argument(
        "--steps",
        type=int,
        default=20,
        help="Inference steps. Default: 20",
    )
    parser.add_argument(
        "--cfg",
        type=float,
        default=7.0,
        help="CFG scale. Default: 7.0",
    )
    parser.add_argument(
        "--skip-control-decode",
        action="store_true",
        help="Do not render the cached-latent re-decode control clip",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print the run plan and exit without starting the backend",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.list_pairs:
        for pair in PAIR_LIBRARY:
            print(f"{pair.slug}: {pair.prompt_a!r}  <->  {pair.prompt_b!r}")
        return 0

    repo_root = Path(__file__).resolve().parents[1]
    output_dir = args.output_dir.expanduser() if args.output_dir else default_output_dir(repo_root)
    seeds = parse_csv_ints(args.seeds)
    embedding_alphas = parse_csv_floats(args.embedding_alphas)
    latent_alphas = parse_csv_floats(args.latent_alphas)
    pairs = resolve_pairs(args.pairs)
    backend_command = resolve_backend_command(repo_root, args.backend)

    if args.dry_run:
        print("Dry run:")
        print(f"  output_dir: {output_dir}")
        print(f"  backend:    {' '.join(backend_command)}")
        print(f"  model:      {args.model}")
        print(f"  device:     {args.device or '<backend default>'}")
        print(f"  seeds:      {seeds}")
        print(f"  emb:        {embedding_alphas}")
        print(f"  latent:     {latent_alphas}")
        print("  pairs:")
        for pair in pairs:
            print(f"    - {pair.slug}: {pair.prompt_a!r} <-> {pair.prompt_b!r}")
        return 0

    output_dir.mkdir(parents=True, exist_ok=True)

    client = PipeClient(backend_command)
    try:
        available_models = set(client.info.get("models", []))
        if args.model not in available_models:
            raise SystemExit(
                f"Model {args.model!r} not reported by backend. "
                f"Have: {', '.join(sorted(available_models))}"
            )

        device = args.device or client.info.get("default", "cpu")
        records: list[dict] = []

        for pair in pairs:
            for seed in seeds:
                pair_dir = output_dir / pair.slug / f"seed_{seed}"
                pair_dir.mkdir(parents=True, exist_ok=True)

                cache_a = f"{pair.slug}_seed{seed}_a"
                cache_b = f"{pair.slug}_seed{seed}_b"

                base_request = {
                    "model": args.model,
                    "device": device,
                    "duration": args.duration,
                    "steps": args.steps,
                    "cfg_scale": args.cfg,
                    "seed": seed,
                }

                render_clip(
                    client,
                    {
                        **base_request,
                        "prompt_a": pair.prompt_a,
                        "cache_as": cache_a,
                    },
                    pair_dir / "00_endpoint_a.wav",
                    records,
                    pair=pair,
                    seed=seed,
                    family="endpoint",
                    mix_value=None,
                    description="endpoint A",
                )

                render_clip(
                    client,
                    {
                        **base_request,
                        "prompt_a": pair.prompt_b,
                        "cache_as": cache_b,
                    },
                    pair_dir / "01_endpoint_b.wav",
                    records,
                    pair=pair,
                    seed=seed,
                    family="endpoint",
                    mix_value=None,
                    description="endpoint B",
                )

                if not args.skip_control_decode:
                    render_clip(
                        client,
                        {
                            "model": args.model,
                            "device": device,
                            "mode": "decode_cached",
                            "latent_name": cache_a,
                            "duration": args.duration,
                        },
                        pair_dir / "02_control_decode_a.wav",
                        records,
                        pair=pair,
                        seed=seed,
                        family="control_decode",
                        mix_value=None,
                        description="control decode A",
                    )

                for alpha in embedding_alphas:
                    render_clip(
                        client,
                        {
                            **base_request,
                            "prompt_a": pair.prompt_a,
                            "prompt_b": pair.prompt_b,
                            "alpha": alpha,
                        },
                        pair_dir / f"10_embedding_alpha_{alpha_token(alpha)}.wav",
                        records,
                        pair=pair,
                        seed=seed,
                        family="embedding",
                        mix_value=alpha,
                        description=f"embedding alpha {alpha:+.2f}",
                    )

                for alpha in latent_alphas:
                    render_clip(
                        client,
                        {
                            "model": args.model,
                            "device": device,
                            "mode": "interpolate",
                            "latent_a": cache_a,
                            "latent_b": cache_b,
                            "lerp_alpha": alpha,
                            "duration": args.duration,
                        },
                        pair_dir / f"20_latent_alpha_{alpha_token(alpha)}.wav",
                        records,
                        pair=pair,
                        seed=seed,
                        family="latent",
                        mix_value=alpha,
                        description=f"latent alpha {alpha:.2f}",
                    )

        manifest = {
            "created_at": datetime.now(timezone.utc).isoformat(),
            "backend_command": backend_command,
            "backend_info": client.info,
            "model": args.model,
            "device": device,
            "duration": args.duration,
            "steps": args.steps,
            "cfg_scale": args.cfg,
            "pairs": [pair.__dict__ for pair in pairs],
            "seeds": seeds,
            "embedding_alphas": embedding_alphas,
            "latent_alphas": latent_alphas,
            "records": records,
        }
        (output_dir / "manifest.json").write_text(
            json.dumps(manifest, indent=2),
            encoding="utf-8",
        )
        write_readme(
            output_dir / "README.txt",
            model=args.model,
            device=device,
            duration=args.duration,
            steps=args.steps,
            cfg_scale=args.cfg,
            embedding_alphas=embedding_alphas,
            latent_alphas=latent_alphas,
            seeds=seeds,
        )
        write_listening_sheet(output_dir / "listening_sheet.csv", records)

        print(f"\nWrote comparison set to {output_dir}")
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    raise SystemExit(main())
