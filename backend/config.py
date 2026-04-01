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
    if sys.platform == "darwin":
        app_support = Path.home() / "Library" / "T5ynth" / "models"
    elif sys.platform == "win32":
        app_support = Path(os.environ.get("APPDATA", "")) / "T5ynth" / "models"
    else:  # Linux
        app_support = Path.home() / ".local" / "share" / "T5ynth" / "models"

    legacy = Path.home() / "t5ynth" / "models"

    # Prefer app support if the model exists there
    if (app_support / "stable-audio-open-1.0" / "model_index.json").is_file():
        return app_support
    if (legacy / "stable-audio-open-1.0" / "model_index.json").is_file():
        return legacy

    # Default to app support (will be created by the GUI download)
    return app_support

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
