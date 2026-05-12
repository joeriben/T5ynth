#!/usr/bin/env python3
"""Prepare the T5 Base text encoder assets required by backend bundling."""

import shutil
from pathlib import Path

from huggingface_hub import snapshot_download


ALLOW_PATTERNS = [
    "config.json",
    "model.safetensors",
    "tokenizer.json",
    "spiece.model",
]


REPO_ROOT = Path(__file__).resolve().parents[1]
TARGET_DIR = REPO_ROOT / "backend" / "hf_assets" / "t5-base"


def main() -> int:
    snapshot = Path(snapshot_download("t5-base", allow_patterns=ALLOW_PATTERNS))
    TARGET_DIR.mkdir(parents=True, exist_ok=True)

    for name in ALLOW_PATTERNS:
        src = snapshot / name
        if not src.is_file():
            raise FileNotFoundError(f"Missing required t5-base asset in cache: {src}")
        shutil.copy2(src, TARGET_DIR / name, follow_symlinks=True)

    print(f"t5-base assets ready: {TARGET_DIR}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
