"""Diagnostic: measure crest factor of generated audio across embedding manipulations.

Purpose: decide whether distortion spikes on choir prompts originate from the
Stable-Audio generator (impulsive VAE output) or from the SamplePlayer's
normalize step (RMS-boost followed by a hard-clip).

Generates four runs with identical seeds/steps and prints RMS, Peak, Crest.
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

# Allow `import pipe_inference` when run from repo root or tools/.
BACKEND_DIR = Path(__file__).resolve().parent.parent / "backend"
sys.path.insert(0, str(BACKEND_DIR))

import pipe_inference as pi  # noqa: E402


def measure(audio_np: np.ndarray) -> tuple[float, float, float]:
    rms = float(np.sqrt(np.mean(audio_np.astype(np.float64) ** 2)))
    peak = float(np.max(np.abs(audio_np)))
    crest = peak / rms if rms > 1e-9 else float("inf")
    return rms, peak, crest


def db(x: float) -> float:
    return 20.0 * np.log10(max(x, 1e-9))


def main() -> int:
    models = pi.find_models()
    if not models:
        print("No models found. Check ~/.config/share/T5ynth/models/", file=sys.stderr)
        return 1
    pi._available_models.update(models)

    candidates = pi.startup_model_candidates(models)
    devices = pi.available_devices()
    model_name, device, failures = pi.choose_startup_model(models, devices)
    print(f"Loaded model: {model_name} on {device}")
    if failures:
        print(f"Startup failures (ignored): {failures}")

    pipe = pi.get_pipeline(model_name, device)

    runs = [
        ("chor neutral",       "a cathedral choir singing in unison",        1.0, 0.0),
        ("chor mag=2.0",       "a cathedral choir singing in unison",        2.0, 0.0),
        ("chor noise=0.3",     "a cathedral choir singing in unison",        1.0, 0.3),
        ("pluck reference",    "a piano pluck, single note",                 1.0, 0.0),
    ]

    SEED = 12345
    DURATION = 3.0
    STEPS = 8

    results = []
    for label, prompt, magnitude, noise_sigma in runs:
        req = {
            "prompt_a": prompt,
            "magnitude": magnitude,
            "noise_sigma": noise_sigma,
            "duration": DURATION,
            "steps": STEPS,
            "seed": SEED,
        }
        audio_np, sr, seed, elapsed, _emb = pi._generate_native(pipe, req)
        rms, peak, crest = measure(audio_np)
        results.append((label, prompt, magnitude, noise_sigma, rms, peak, crest, elapsed))
        print(
            f"  {label:<20s} rms={rms:.4f} ({db(rms):+6.1f} dB)  "
            f"peak={peak:.4f} ({db(peak):+6.1f} dB)  crest={crest:.2f}  "
            f"t={elapsed:.1f}s"
        )

    print()
    print("Crest factor interpretation:")
    print("  < 3.5  normal content, generator not impulsive")
    print("  3.5-5  moderate — likely normalize + peaky signal interaction")
    print("  > 5    generator produces impulsive spikes — embedding path suspect")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
