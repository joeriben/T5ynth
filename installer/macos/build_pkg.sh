#!/bin/bash
set -euo pipefail

# ── T5ynth macOS .pkg Installer Builder ──────────────────────────────
# Usage: build_pkg.sh --app <path> --presets <dir>
#                     --version <ver> --output <pkg>

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ── Parse arguments ──────────────────────────────────────────────────
APP="" PRESETS="" VERSION="0.3.0" OUTPUT="T5ynth-macOS-Installer.pkg"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --app)     APP="$2";     shift 2 ;;
        --presets) PRESETS="$2";  shift 2 ;;
        --version) VERSION="$2"; shift 2 ;;
        --output)  OUTPUT="$2";  shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

# Strip leading 'v' from version tag (e.g. v1.1.0 -> 1.1.0)
VERSION="${VERSION#v}"

# pkgbuild/productbuild expect a dotted numeric version, and macOS receipts
# compare that version for reinstall / upgrade decisions. Keep the user-facing
# tag semantics, but translate prereleases into a monotonically increasing
# fourth numeric component:
#   0.3.0-alpha.2 -> 0.3.0.102
#   0.3.0-beta.1  -> 0.3.0.201
#   0.3.0-rc.3    -> 0.3.0.303
#   0.3.0         -> 0.3.0.400
CORE_VERSION="${VERSION%%-*}"
PACKAGE_VERSION=""

if [[ "$VERSION" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)$ ]]; then
    PACKAGE_VERSION="${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.${BASH_REMATCH[3]}.400"
elif [[ "$VERSION" =~ ^([0-9]+)\.([0-9]+)\.([0-9]+)-(alpha|beta|rc)\.([0-9]+)$ ]]; then
    case "${BASH_REMATCH[4]}" in
        alpha) PACKAGE_STAGE_BASE=100 ;;
        beta)  PACKAGE_STAGE_BASE=200 ;;
        rc)    PACKAGE_STAGE_BASE=300 ;;
    esac

    PACKAGE_STAGE_NUMBER=$((PACKAGE_STAGE_BASE + BASH_REMATCH[5]))
    PACKAGE_VERSION="${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.${BASH_REMATCH[3]}.${PACKAGE_STAGE_NUMBER}"
else
    echo "Error: unsupported version format '$VERSION'"
    echo "Expected X.Y.Z or X.Y.Z-alpha.N / -beta.N / -rc.N"
    exit 1
fi

for var in APP PRESETS; do
    if [[ -z "${!var}" ]]; then
        echo "Error: --$(echo $var | tr '[:upper:]' '[:lower:]') is required"
        exit 1
    fi
done

# ── Temp workspace ───────────────────────────────────────────────────
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "==> Building T5ynth installer ${VERSION} (pkg version ${PACKAGE_VERSION})"

# ── Stage: Standalone ────────────────────────────────────────────────
echo "  Staging Standalone..."
STAGE_APP="$WORK/stage-standalone"
mkdir -p "$STAGE_APP"
cp -R "$APP" "$STAGE_APP/"

# Prevent Installer from "following" an existing T5ynth.app with the same
# bundle identifier into a dev/build path. We always want the packaged app to
# land at /Applications on the selected volume.
COMPONENT_PLIST="$WORK/standalone-components.plist"
pkgbuild --analyze --root "$STAGE_APP" "$COMPONENT_PLIST" >/dev/null
/usr/libexec/PlistBuddy -c "Set :0:BundleIsRelocatable false" "$COMPONENT_PLIST"
/usr/libexec/PlistBuddy -c "Set :0:BundleIsVersionChecked false" "$COMPONENT_PLIST"

pkgbuild \
    --root "$STAGE_APP" \
    --component-plist "$COMPONENT_PLIST" \
    --identifier org.ai4artsed.t5ynth.standalone \
    --version "$PACKAGE_VERSION" \
    --install-location /Applications \
    --scripts "$SCRIPT_DIR/scripts-standalone" \
    "$WORK/standalone.pkg"

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
    --version "$PACKAGE_VERSION" \
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
sed "s/version=\"0\"/version=\"${PACKAGE_VERSION}\"/g" \
    "$SCRIPT_DIR/distribution.xml" > "$WORK/distribution.xml"

mkdir -p "$(dirname "$OUTPUT")"
productbuild \
    --distribution "$WORK/distribution.xml" \
    --package-path "$WORK" \
    --resources "$RESOURCES" \
    "$OUTPUT"

echo "==> Done: $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"
