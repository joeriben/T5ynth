#!/usr/bin/env python3
"""Strip heuristic-derived garbage tags from bundled .t5p preset files.

The pre-2026-05-22 PresetTagSuggester mapped audio-feature analysis onto
nine tag strings that turned out to be useless as classifications:

    hot, dense, sparse, transient, sustained, quiet, one-shot, short, long

`hot` in particular got baked into roughly half of the bundled UCDCAE
presets because Stable Audio output is loud enough that PeakCap mode
dominates. This tool surgically removes those tags from the JSON header
of every .t5p file under `resources/presets/`. Anything that isn't on
the garbage list (prompt-keyword tags like `perc`, `metallic`, `bright`,
`ambient`, plus any user-curated additions) is preserved.

Audio payload is byte-copied — neither the FLAC blobs (v4+) nor the
interleaved float32 PCM (v3) are touched. Both the magic header and the
version field are preserved unchanged.

Run from repo root:

    python3 tools/strip_preset_garbage_tags.py            # dry-run + summary
    python3 tools/strip_preset_garbage_tags.py --write    # actually patch
"""

from __future__ import annotations

import argparse
import json
import struct
import sys
import tempfile
from pathlib import Path

GARBAGE_TAGS = frozenset({
    "hot",
    "dense",
    "sparse",
    "transient",
    "sustained",
    "quiet",
    "one-shot",
    "short",
    "long",
})

MAGIC = b"T5YN"


def parse_header(data: bytes) -> tuple[int, int, str, bytes] | None:
    """Return (version, json_len, json_str, audio_tail) or None on bad file."""
    if len(data) < 12 or data[:4] != MAGIC:
        return None
    version = struct.unpack("<I", data[4:8])[0]
    json_len = struct.unpack("<I", data[8:12])[0]
    if 12 + json_len > len(data):
        return None
    json_bytes = data[12 : 12 + json_len]
    try:
        json_str = json_bytes.decode("utf-8")
    except UnicodeDecodeError:
        return None
    audio_tail = data[12 + json_len :]
    return version, json_len, json_str, audio_tail


def serialise_header(version: int, json_str: str, audio_tail: bytes) -> bytes:
    json_bytes = json_str.encode("utf-8")
    return (
        MAGIC
        + struct.pack("<I", version)
        + struct.pack("<I", len(json_bytes))
        + json_bytes
        + audio_tail
    )


def filter_tags(tags: list) -> list:
    return [t for t in tags if isinstance(t, str) and t not in GARBAGE_TAGS]


def process_file(path: Path, write: bool) -> tuple[str, list, list] | None:
    """Return (status, old_tags, new_tags) for the report, or None on parse fail."""
    data = path.read_bytes()
    parsed = parse_header(data)
    if parsed is None:
        return ("parse-fail", [], [])
    version, _, json_str, audio_tail = parsed

    try:
        root = json.loads(json_str)
    except json.JSONDecodeError:
        return ("json-fail", [], [])

    if not isinstance(root, dict):
        return ("not-object", [], [])

    old_tags = root.get("tags")
    if not isinstance(old_tags, list):
        # No tags field at all — nothing to clean.
        return ("skip-no-tags", [], [])

    new_tags = filter_tags(old_tags)
    if new_tags == old_tags:
        return ("skip-clean", old_tags, new_tags)

    root["tags"] = new_tags

    # Compact serialisation matches JUCE's allOnOneLine=true output. The
    # load path parses JSON, so whitespace doesn't matter for correctness;
    # compact just keeps the JSON length predictable.
    new_json = json.dumps(root, ensure_ascii=False, separators=(",", ":"))
    new_data = serialise_header(version, new_json, audio_tail)

    if write:
        # Atomic write — tempfile in same directory so rename stays on one
        # filesystem, then os.replace for atomicity on POSIX.
        with tempfile.NamedTemporaryFile(
            mode="wb",
            delete=False,
            dir=str(path.parent),
            prefix=path.stem + ".",
            suffix=".tmp",
        ) as tmp:
            tmp.write(new_data)
            tmp_path = Path(tmp.name)
        tmp_path.replace(path)

    return ("patched", old_tags, new_tags)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument(
        "--write",
        action="store_true",
        help="Apply changes. Without this flag the tool only reports what would change.",
    )
    ap.add_argument(
        "--dir",
        default="resources/presets",
        help="Directory to scan (default: resources/presets, relative to repo root)",
    )
    args = ap.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    scan_dir = (repo_root / args.dir).resolve()
    if not scan_dir.is_dir():
        print(f"Not a directory: {scan_dir}", file=sys.stderr)
        return 1

    files = sorted(scan_dir.glob("*.t5p"))
    if not files:
        print(f"No .t5p files in {scan_dir}", file=sys.stderr)
        return 1

    patched = 0
    clean = 0
    no_tags = 0
    failed = 0

    print(f"Scanning {len(files)} preset(s) in {scan_dir}")
    print(f"Mode: {'WRITE' if args.write else 'dry-run'}\n")

    for path in files:
        result = process_file(path, args.write)
        if result is None:
            failed += 1
            print(f"  [FAIL] {path.name}: read error")
            continue
        status, old, new = result
        rel = path.name
        if status == "patched":
            patched += 1
            removed = [t for t in old if t not in new]
            print(f"  [patch] {rel}")
            print(f"          old: {old}")
            print(f"          new: {new}")
            print(f"          removed: {removed}")
        elif status == "skip-clean":
            clean += 1
        elif status == "skip-no-tags":
            no_tags += 1
        else:
            failed += 1
            print(f"  [FAIL] {rel}: {status}")

    print()
    print(f"Summary:")
    print(f"  {'patched' if args.write else 'would patch'}: {patched}")
    print(f"  already clean:                     {clean}")
    print(f"  no tags field:                     {no_tags}")
    print(f"  failed:                            {failed}")

    if not args.write and patched > 0:
        print()
        print("Re-run with --write to apply the changes.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
