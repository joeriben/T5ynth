"""
T5ynth Backend Configuration

Standalone config for the Stable Audio + Crossmodal Lab server.
All values overridable via environment variables.
"""

import os
from pathlib import Path

import torch

# --- Device Auto-Detection ---
if torch.cuda.is_available():
    _DEFAULT_DEVICE = "cuda"
    _DEFAULT_DTYPE = "float16"
elif hasattr(torch.backends, 'mps') and torch.backends.mps.is_available():
    _DEFAULT_DEVICE = "mps"
    _DEFAULT_DTYPE = "float32"
else:
    _DEFAULT_DEVICE = "cpu"
    _DEFAULT_DTYPE = "float32"

# --- Server ---
HOST = os.environ.get("T5YNTH_HOST", "127.0.0.1")  # Loopback only — no external access
PORT = int(os.environ.get("T5YNTH_PORT", "17850"))

# --- Model Storage ---
# Platform-agnostic: check app data directory first, then legacy ~/t5ynth/models
import sys

def _default_model_dir() -> Path:
    """Return the first existing model directory, or the platform-standard one."""
    candidates = []
    if sys.platform == "darwin":
        candidates.append(Path("/Library/Application Support/T5ynth/models"))  # system-wide (.pkg)
        candidates.append(Path.home() / "Library" / "Application Support" / "T5ynth" / "models")
        candidates.append(Path.home() / "Library" / "T5ynth" / "models")  # legacy
    elif sys.platform == "win32":
        candidates.append(Path(os.environ.get("PROGRAMDATA", "C:\\ProgramData")) / "T5ynth" / "models")
        candidates.append(Path(os.environ.get("APPDATA", "")) / "T5ynth" / "models")  # legacy
    else:  # Linux
        candidates.append(Path.home() / ".local" / "share" / "T5ynth" / "models")
    candidates.append(Path.home() / "t5ynth" / "models")  # legacy

    # Return first candidate that contains any model
    for d in candidates:
        if d.is_dir() and any(
            (child / "model_index.json").is_file() or (child / "model_config.json").is_file()
            for child in d.iterdir() if child.is_dir()
        ):
            return d

    # Default to system-wide on macOS, else first candidate
    return candidates[0]

MODEL_DIR = Path(os.environ.get("T5YNTH_MODEL_DIR", str(_default_model_dir())))

# --- Stable Audio ---
STABLE_AUDIO_ENABLED = os.environ.get("STABLE_AUDIO_ENABLED", "true").lower() == "true"
STABLE_AUDIO_MODEL_ID = os.environ.get("STABLE_AUDIO_MODEL_ID", "stabilityai/stable-audio-open-1.0")
STABLE_AUDIO_DEVICE = os.environ.get("STABLE_AUDIO_DEVICE", _DEFAULT_DEVICE)
STABLE_AUDIO_DTYPE = os.environ.get("STABLE_AUDIO_DTYPE", _DEFAULT_DTYPE)
STABLE_AUDIO_LAZY_LOAD = os.environ.get("STABLE_AUDIO_LAZY_LOAD", "true").lower() == "true"
STABLE_AUDIO_MAX_DURATION = 47.55  # seconds (model maximum)
STABLE_AUDIO_SAMPLE_RATE = 44100

# --- Crossmodal Lab ---
CROSS_AESTHETIC_ENABLED = os.environ.get("CROSS_AESTHETIC_ENABLED", "true").lower() == "true"

# --- Disabled backends (not needed for T5ynth MVP) ---
IMAGEBIND_ENABLED = os.environ.get("IMAGEBIND_ENABLED", "false").lower() == "true"
MMAUDIO_ENABLED = os.environ.get("MMAUDIO_ENABLED", "false").lower() == "true"
