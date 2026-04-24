#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import math
import json
import subprocess
from dataclasses import dataclass, asdict
from pathlib import Path

import numpy as np
import soundfile as sf


K_NORMALIZE_CEILING_DB = -1.0
K_SUSTAINED_TARGET_DB = -18.0
K_TRANSIENT_PERCENTILE_TARGET_DB = -10.0
K_NEAR_SILENT_PEAK_DB = -36.0
K_NEAR_SILENT_ACTIVE_DB = -50.0
K_HOT_HEADROOM_DB = 2.0
K_SHORT_TRANSIENT_SECONDS = 0.75
K_ACTIVE_BLOCK_THRESHOLD_DB = -40.0
K_TRANSIENT_ACTIVE_RATIO = 0.35
K_TRANSIENT_CREST_DB = 18.0
K_TRANSIENT_PEAK_GAP_DB = 8.0


@dataclass
class NormalizeAnalysis:
    duration_seconds: float = 0.0
    peak: float = 0.0
    percentile_peak: float = 0.0
    rms: float = 0.0
    active_rms: float = 0.0
    crest_db: float = 0.0
    peak_to_percentile_db: float = 0.0
    active_ratio: float = 0.0


@dataclass
class RenderResult:
    source: str
    output: str
    sample_rate: int
    channels: int
    mode: str
    gain_db: float
    peak_db_before: float
    peak_db_after: float
    rms_db_before: float
    rms_db_after: float
    active_rms_db_before: float
    active_rms_db_after: float
    crest_db: float
    peak_gap_db: float
    active_ratio: float
    duration_seconds: float


def db_to_gain(db: float) -> float:
    return float(10.0 ** (db / 20.0))


def gain_to_db(gain: float) -> float:
    return float(20.0 * math.log10(max(gain, 1.0e-9)))


def analyze_region(audio: np.ndarray, sample_rate: float) -> NormalizeAnalysis:
    if audio.ndim == 1:
        audio = audio[:, None]

    frames, channels = audio.shape
    if frames == 0 or channels == 0 or sample_rate <= 0.0:
        return NormalizeAnalysis()

    frame_peaks = np.max(np.abs(audio), axis=1)
    peak = float(np.max(frame_peaks))
    rms = float(np.sqrt(np.mean(audio ** 2)))

    block_frames = max(1, int(round(sample_rate * 0.050)))
    active_threshold = db_to_gain(K_ACTIVE_BLOCK_THRESHOLD_DB)
    active_blocks = []
    for start in range(0, frames, block_frames):
        block = audio[start:start + block_frames]
        if len(block) == 0:
            continue
        block_rms = float(np.sqrt(np.mean(block ** 2)))
        if block_rms > active_threshold:
            active_blocks.append(block)

    if active_blocks:
        active_audio = np.concatenate(active_blocks, axis=0)
        active_rms = float(np.sqrt(np.mean(active_audio ** 2)))
    else:
        active_rms = rms

    active_ratio = (
        float(len(active_blocks)) / float(max(1, math.ceil(frames / block_frames)))
    )

    percentile_peak = float(np.percentile(frame_peaks, 99.9))
    crest_db = gain_to_db(peak / max(rms, 1.0e-9))
    peak_gap_db = gain_to_db(peak / max(percentile_peak, 1.0e-9))

    return NormalizeAnalysis(
        duration_seconds=float(frames / sample_rate),
        peak=peak,
        percentile_peak=percentile_peak,
        rms=rms,
        active_rms=active_rms,
        crest_db=crest_db,
        peak_to_percentile_db=peak_gap_db,
        active_ratio=active_ratio,
    )


def choose_mode(analysis: NormalizeAnalysis) -> str:
    if analysis.peak <= 0.0:
        return "Bypass"

    peak_db = gain_to_db(analysis.peak)
    active_db = gain_to_db(max(analysis.active_rms, analysis.rms))
    headroom_db = -peak_db

    if peak_db <= K_NEAR_SILENT_PEAK_DB and active_db <= K_NEAR_SILENT_ACTIVE_DB:
        return "Bypass"

    if headroom_db < K_HOT_HEADROOM_DB:
        return "PeakCap"

    if (
        analysis.duration_seconds < K_SHORT_TRANSIENT_SECONDS
        or analysis.active_ratio < K_TRANSIENT_ACTIVE_RATIO
        or analysis.crest_db > K_TRANSIENT_CREST_DB
        or analysis.peak_to_percentile_db > K_TRANSIENT_PEAK_GAP_DB
    ):
        return "Transient"

    return "Sustained"


def choose_gain(analysis: NormalizeAnalysis, mode: str) -> float:
    if analysis.peak <= 0.0:
        return 1.0

    ceiling_gain = db_to_gain(K_NORMALIZE_CEILING_DB) / analysis.peak

    if mode == "Bypass":
        return 1.0
    if mode == "PeakCap":
        return min(1.0, ceiling_gain)
    if mode == "Transient":
        if analysis.percentile_peak <= 0.0:
            return min(1.0, ceiling_gain)
        transient_gain = db_to_gain(K_TRANSIENT_PERCENTILE_TARGET_DB) / analysis.percentile_peak
        return min(ceiling_gain, max(1.0, transient_gain))
    if mode == "Sustained":
        reference = max(analysis.active_rms, analysis.rms)
        if reference <= 0.0:
            return min(1.0, ceiling_gain)
        sustained_gain = db_to_gain(K_SUSTAINED_TARGET_DB) / reference
        return min(ceiling_gain, sustained_gain)
    raise ValueError(f"Unknown mode: {mode}")


def decode_audio(source_path: Path) -> tuple[np.ndarray, int]:
    probe = subprocess.run(
        [
            "ffprobe",
            "-v",
            "error",
            "-show_entries",
            "stream=sample_rate,channels",
            "-of",
            "json",
            str(source_path),
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    meta = json.loads(probe.stdout)
    sample_rate = int(meta["streams"][0]["sample_rate"])
    channels = int(meta["streams"][0]["channels"])

    decode = subprocess.run(
        [
            "ffmpeg",
            "-v",
            "error",
            "-i",
            str(source_path),
            "-f",
            "f32le",
            "-acodec",
            "pcm_f32le",
            "-",
        ],
        check=True,
        capture_output=True,
    )
    audio = np.frombuffer(decode.stdout, dtype=np.float32)
    if audio.size == 0:
        return np.zeros((0, channels), dtype=np.float32), sample_rate
    return audio.reshape(-1, channels), sample_rate


def normalize_file(source_path: Path, output_dir: Path) -> RenderResult:
    audio, sample_rate = decode_audio(source_path)

    before = analyze_region(audio, sample_rate)
    mode = choose_mode(before)
    gain = choose_gain(before, mode)
    rendered = audio * gain
    after = analyze_region(rendered, sample_rate)

    output_path = output_dir / f"{source_path.stem}__normalized.wav"
    sf.write(str(output_path), rendered, sample_rate, subtype="PCM_24")

    return RenderResult(
        source=str(source_path),
        output=str(output_path),
        sample_rate=int(sample_rate),
        channels=int(audio.shape[1]),
        mode=mode,
        gain_db=gain_to_db(gain),
        peak_db_before=gain_to_db(before.peak),
        peak_db_after=gain_to_db(after.peak),
        rms_db_before=gain_to_db(before.rms),
        rms_db_after=gain_to_db(after.rms),
        active_rms_db_before=gain_to_db(before.active_rms),
        active_rms_db_after=gain_to_db(after.active_rms),
        crest_db=before.crest_db,
        peak_gap_db=before.peak_to_percentile_db,
        active_ratio=before.active_ratio,
        duration_seconds=before.duration_seconds,
    )


def write_reports(results: list[RenderResult], output_dir: Path) -> None:
    csv_path = output_dir / "report.csv"
    md_path = output_dir / "report.md"

    with csv_path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(asdict(results[0]).keys()))
        writer.writeheader()
        for row in results:
            writer.writerow(asdict(row))

    lines = [
        "# Batch Normalize Report",
        "",
        "| file | mode | gain dB | peak before | peak after | active RMS before | active RMS after | crest dB | active ratio |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for row in results:
        lines.append(
            "| {file} | {mode} | {gain:.2f} | {peak_b:.2f} | {peak_a:.2f} | {active_b:.2f} | {active_a:.2f} | {crest:.2f} | {ratio:.3f} |".format(
                file=Path(row.source).name,
                mode=row.mode,
                gain=row.gain_db,
                peak_b=row.peak_db_before,
                peak_a=row.peak_db_after,
                active_b=row.active_rms_db_before,
                active_a=row.active_rms_db_after,
                crest=row.crest_db,
                ratio=row.active_ratio,
            )
        )
    md_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Apply the sampler normalization heuristic to WAV files.")
    parser.add_argument("inputs", nargs="+", help="Input WAV files.")
    parser.add_argument(
        "--output-dir",
        default="/tmp/t5ynth_normalize_batch",
        help="Directory for rendered WAVs and reports.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    results = []
    for item in args.inputs:
        source_path = Path(item).expanduser()
        if not source_path.exists():
            raise FileNotFoundError(source_path)
        results.append(normalize_file(source_path, output_dir))

    if not results:
        return 0

    write_reports(results, output_dir)

    for row in results:
        print(
            f"{Path(row.source).name}: mode={row.mode} gain_db={row.gain_db:.2f} "
            f"peak_before={row.peak_db_before:.2f} peak_after={row.peak_db_after:.2f} "
            f"output={row.output}"
        )
    print(f"report_csv={output_dir / 'report.csv'}")
    print(f"report_md={output_dir / 'report.md'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
