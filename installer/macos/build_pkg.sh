#!/bin/bash
set -euo pipefail

# ── T5ynth macOS .pkg Installer Builder ──────────────────────────────
# Usage: build_pkg.sh --app <path> --vst3 <path> --au <path>
#                     --presets <dir> --version <ver> --output <pkg>

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ── Parse arguments ──────────────────────────────────────────────────
APP="" VST3="" AU="" PRESETS="" VERSION="1.0.0" OUTPUT="T5ynth-macOS.pkg"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --app)     APP="$2";     shift 2 ;;
        --vst3)    VST3="$2";    shift 2 ;;
        --au)      AU="$2";      shift 2 ;;
        --presets) PRESETS="$2";  shift 2 ;;
        --version) VERSION="$2"; shift 2 ;;
        --output)  OUTPUT="$2";  shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# Strip leading 'v' from version tag (e.g. v1.1.0 -> 1.1.0)
VERSION="${VERSION#v}"

for var in APP VST3 AU PRESETS; do
    if [[ -z "${!var}" ]]; then
        echo "Error: --$(echo $var | tr '[:upper:]' '[:lower:]') is required"
        exit 1
    fi
done

# ── Temp workspace ───────────────────────────────────────────────────
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "==> Building T5ynth installer v${VERSION}"

# ── Stage: Standalone ────────────────────────────────────────────────
echo "  Staging Standalone..."
STAGE_APP="$WORK/stage-standalone"
mkdir -p "$STAGE_APP"
cp -R "$APP" "$STAGE_APP/"

pkgbuild \
    --root "$STAGE_APP" \
    --identifier org.ai4artsed.t5ynth.standalone \
    --version "$VERSION" \
    --install-location /Applications \
    --scripts "$SCRIPT_DIR/scripts-standalone" \
    "$WORK/standalone.pkg"

# ── Stage: VST3 ──────────────────────────────────────────────────────
echo "  Staging VST3..."
STAGE_VST3="$WORK/stage-vst3"
mkdir -p "$STAGE_VST3"
cp -R "$VST3" "$STAGE_VST3/"

pkgbuild \
    --root "$STAGE_VST3" \
    --identifier org.ai4artsed.t5ynth.vst3 \
    --version "$VERSION" \
    --install-location "/Library/Audio/Plug-Ins/VST3" \
    "$WORK/vst3.pkg"

# ── Stage: AU ────────────────────────────────────────────────────────
echo "  Staging AU..."
STAGE_AU="$WORK/stage-au"
mkdir -p "$STAGE_AU"
cp -R "$AU" "$STAGE_AU/"

pkgbuild \
    --root "$STAGE_AU" \
    --identifier org.ai4artsed.t5ynth.au \
    --version "$VERSION" \
    --install-location "/Library/Audio/Plug-Ins/Components" \
    "$WORK/au.pkg"

# ── Stage: Support data (factory presets + empty models dir) ─────────
echo "  Staging support data..."
STAGE_SUPPORT="$WORK/stage-support"
mkdir -p "$STAGE_SUPPORT/presets"
mkdir -p "$STAGE_SUPPORT/models"

# Copy factory presets
if [[ -d "$PRESETS" ]]; then
    cp "$PRESETS"/*.t5p "$STAGE_SUPPORT/presets/" 2>/dev/null || true
fi

# Copy license
if [[ -f "$SCRIPT_DIR/../../LICENSE.txt" ]]; then
    cp "$SCRIPT_DIR/../../LICENSE.txt" "$STAGE_SUPPORT/"
fi

pkgbuild \
    --root "$STAGE_SUPPORT" \
    --identifier org.ai4artsed.t5ynth.support-data \
    --version "$VERSION" \
    --install-location "/Library/Application Support/T5ynth" \
    --scripts "$SCRIPT_DIR/scripts" \
    "$WORK/support-data.pkg"

# ── Copy resources for the product installer ─────────────────────────
RESOURCES="$WORK/resources"
mkdir -p "$RESOURCES"
cp "$SCRIPT_DIR/resources/welcome.html" "$RESOURCES/"
if [[ -f "$SCRIPT_DIR/../../LICENSE.txt" ]]; then
    cp "$SCRIPT_DIR/../../LICENSE.txt" "$RESOURCES/"
fi

# ── Build product archive ────────────────────────────────────────────
echo "  Building product installer..."

# Inject version into distribution.xml pkg-ref elements
sed "s/version=\"0\"/version=\"${VERSION}\"/g" \
    "$SCRIPT_DIR/distribution.xml" > "$WORK/distribution.xml"

mkdir -p "$(dirname "$OUTPUT")"
productbuild \
    --distribution "$WORK/distribution.xml" \
    --package-path "$WORK" \
    --resources "$RESOURCES" \
    "$OUTPUT"

echo "==> Done: $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"
