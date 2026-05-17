#!/usr/bin/env python3
"""Generate manifest.json for the T5ynth-Presets repository.

Scans every .t5p under presets/, extracts the stored preset name from each
file's JSON header, computes the SHA256, and writes a manifest.json the
T5ynth plugin can fetch and diff against the user's local copy.

.t5p binary layout (version 3):
  [4B]  magic  "T5YN"
  [4B]  version (uint32 LE)
  [4B]  json length (uint32 LE)
  [NB]  UTF-8 JSON
  [...] audio PCM (ignored here)

Run from the repo root:
  python scripts/generate_manifest.py
"""
from __future__ import annotations

import datetime as _dt
import hashlib
import json
import struct
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PRESETS_DIR = REPO_ROOT / "presets"
MANIFEST_PATH = REPO_ROOT / "manifest.json"

BASE_URL = (
    "https://raw.githubusercontent.com/joeriben/T5ynth-Presets/main/presets/"
)
SCHEMA_VERSION = 1
MAGIC = b"T5YN"


def read_preset_name(path: Path) -> str | None:
    """Return the stored "name" field, or None if the file is malformed."""
    try:
        with path.open("rb") as f:
            header = f.read(12)
            if len(header) < 12 or header[:4] != MAGIC:
                return None
            _version, json_len = struct.unpack("<II", header[4:12])
            if json_len <= 0 or json_len > 8 * 1024 * 1024:
                return None
            body = f.read(json_len)
            if len(body) != json_len:
                return None
        meta = json.loads(body.decode("utf-8"))
        name = (meta.get("name") or "").strip()
        return name or None
    except Exception:
        return None


def sha256_of(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 16), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    if not PRESETS_DIR.is_dir():
        print(f"error: {PRESETS_DIR} does not exist", file=sys.stderr)
        return 1

    presets = []
    for path in sorted(PRESETS_DIR.glob("*.t5p")):
        rel = path.name
        name = read_preset_name(path) or path.stem
        presets.append({
            "name": name,
            "path": rel,
            "size": path.stat().st_size,
            "sha256": sha256_of(path),
        })

    manifest = {
        "schema_version": SCHEMA_VERSION,
        "generated_at": _dt.datetime.now(_dt.timezone.utc).strftime(
            "%Y-%m-%dT%H:%M:%SZ"
        ),
        "base_url": BASE_URL,
        "presets": presets,
    }

    MANIFEST_PATH.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print(f"wrote {MANIFEST_PATH.relative_to(REPO_ROOT)} ({len(presets)} presets)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
