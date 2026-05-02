#!/bin/bash
set -euo pipefail

# ── T5ynth macOS .pkg Installer Builder ──────────────────────────────
# Usage: build_pkg.sh --app <path> --presets <dir>
#                     --version <ver> --output <pkg>
#                     [--vst3 <T5ynth.vst3>] [--au <T5ynth.component>]
#                     [--sign-app-identity <identity>]
#                     [--sign-pkg-identity <identity>]
#                     [--notary-keychain-profile <profile>]
#                     [--notary-apple-id <apple-id> --notary-password <password> --notary-team-id <team-id>]
#                     [--notary-api-key-path <p8> --notary-api-key-id <id> --notary-api-issuer <issuer>]
#
# VST3 and AU components are optional — when omitted, the installer is
# Standalone-only (legacy behaviour). When provided, they install to
# /Library/Audio/Plug-Ins/{VST3,Components} and piggy-back on the
# Standalone's bundled Python backend (see MainPanel::startBackend).

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ── Parse arguments ──────────────────────────────────────────────────
APP="" PRESETS="" VERSION="0.3.0" OUTPUT="T5ynth-macOS-Installer.pkg"
VST3="" AU=""
APP_SIGN_IDENTITY="${MACOS_APP_SIGN_IDENTITY:-}"
PKG_SIGN_IDENTITY="${MACOS_PKG_SIGN_IDENTITY:-${MACOS_INSTALLER_SIGN_IDENTITY:-}}"
NOTARY_KEYCHAIN_PROFILE="${MACOS_NOTARY_KEYCHAIN_PROFILE:-}"
NOTARY_APPLE_ID="${MACOS_NOTARY_APPLE_ID:-}"
NOTARY_PASSWORD="${MACOS_NOTARY_PASSWORD:-}"
NOTARY_TEAM_ID="${MACOS_NOTARY_TEAM_ID:-}"
NOTARY_API_KEY_PATH="${MACOS_NOTARY_API_KEY_PATH:-}"
NOTARY_API_KEY_ID="${MACOS_NOTARY_API_KEY_ID:-}"
NOTARY_API_ISSUER="${MACOS_NOTARY_API_ISSUER:-}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --app)     APP="$2";     shift 2 ;;
        --presets) PRESETS="$2";  shift 2 ;;
        --vst3)    VST3="$2";    shift 2 ;;
        --au)      AU="$2";      shift 2 ;;
        --version) VERSION="$2"; shift 2 ;;
        --output)  OUTPUT="$2";  shift 2 ;;
        --sign-app-identity) APP_SIGN_IDENTITY="$2"; shift 2 ;;
        --sign-pkg-identity) PKG_SIGN_IDENTITY="$2"; shift 2 ;;
        --notary-keychain-profile) NOTARY_KEYCHAIN_PROFILE="$2"; shift 2 ;;
        --notary-apple-id) NOTARY_APPLE_ID="$2"; shift 2 ;;
        --notary-password) NOTARY_PASSWORD="$2"; shift 2 ;;
        --notary-team-id) NOTARY_TEAM_ID="$2"; shift 2 ;;
        --notary-api-key-path) NOTARY_API_KEY_PATH="$2"; shift 2 ;;
        --notary-api-key-id) NOTARY_API_KEY_ID="$2"; shift 2 ;;
        --notary-api-issuer) NOTARY_API_ISSUER="$2"; shift 2 ;;
        *) echo "Unknown option: $1"; exit 1 ;;
    esac
done

die() {
    echo "Error: $*" >&2
    exit 1
}

NOTARY_ARGS=()

build_notary_args() {
    NOTARY_ARGS=()

    if [[ -n "$NOTARY_KEYCHAIN_PROFILE" ]]; then
        NOTARY_ARGS=(--keychain-profile "$NOTARY_KEYCHAIN_PROFILE")
        return 0
    fi

    if [[ -n "$NOTARY_APPLE_ID" || -n "$NOTARY_PASSWORD" || -n "$NOTARY_TEAM_ID" ]]; then
        [[ -n "$NOTARY_APPLE_ID" ]] || die "notary Apple ID auth requires --notary-apple-id"
        [[ -n "$NOTARY_PASSWORD" ]] || die "notary Apple ID auth requires --notary-password"
        [[ -n "$NOTARY_TEAM_ID" ]] || die "notary Apple ID auth requires --notary-team-id"
        NOTARY_ARGS=(--apple-id "$NOTARY_APPLE_ID" --password "$NOTARY_PASSWORD" --team-id "$NOTARY_TEAM_ID")
        return 0
    fi

    if [[ -n "$NOTARY_API_KEY_PATH" || -n "$NOTARY_API_KEY_ID" || -n "$NOTARY_API_ISSUER" ]]; then
        [[ -n "$NOTARY_API_KEY_PATH" ]] || die "notary API auth requires --notary-api-key-path"
        [[ -f "$NOTARY_API_KEY_PATH" ]] || die "notary API key file not found: $NOTARY_API_KEY_PATH"
        [[ -n "$NOTARY_API_KEY_ID" ]] || die "notary API auth requires --notary-api-key-id"
        [[ -n "$NOTARY_API_ISSUER" ]] || die "notary API auth requires --notary-api-issuer"
        NOTARY_ARGS=(--key "$NOTARY_API_KEY_PATH" --key-id "$NOTARY_API_KEY_ID" --issuer "$NOTARY_API_ISSUER")
        return 0
    fi

    return 1
}

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

[[ -d "$APP" ]] || die "app bundle not found: $APP"
[[ -d "$PRESETS" ]] || die "presets directory not found: $PRESETS"
if [[ -n "$VST3" ]]; then
    [[ -d "$VST3" ]] || die "VST3 plugin not found: $VST3"
    [[ "$(basename "$VST3")" == *.vst3 ]] || die "VST3 path must be a .vst3 bundle: $VST3"
fi
if [[ -n "$AU" ]]; then
    [[ -d "$AU" ]] || die "AU plugin not found: $AU"
    [[ "$(basename "$AU")" == *.component ]] || die "AU path must be a .component bundle: $AU"
fi

# ── Temp workspace ───────────────────────────────────────────────────
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "==> Building T5ynth installer ${VERSION} (pkg version ${PACKAGE_VERSION})"

# ── Stage: Standalone ────────────────────────────────────────────────
echo "  Staging Standalone..."
STAGE_APP="$WORK/stage-standalone"
mkdir -p "$STAGE_APP"
cp -R "$APP" "$STAGE_APP/"
STAGED_APP="$STAGE_APP/$(basename "$APP")"

if [[ -n "$APP_SIGN_IDENTITY" ]]; then
    echo "  Signing app bundle with Developer ID..."
    codesign \
        --force \
        --deep \
        --timestamp \
        --options runtime \
        --sign "$APP_SIGN_IDENTITY" \
        "$STAGED_APP"
    codesign --verify --deep --strict "$STAGED_APP"
fi

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
mkdir -p "$STAGE_SUPPORT/docs"

# Copy factory presets
if [[ -d "$PRESETS" ]]; then
    cp "$PRESETS"/*.t5p "$STAGE_SUPPORT/presets/" 2>/dev/null || true
fi

# Copy license
if [[ -f "$SCRIPT_DIR/../../LICENSE.txt" ]]; then
    cp "$SCRIPT_DIR/../../LICENSE.txt" "$STAGE_SUPPORT/"
fi

# Copy install guides if they exist.
for guide in \
    "$SCRIPT_DIR/../../docs/releases/T5ynth-macOS-Installation-DE.pdf" \
    "$SCRIPT_DIR/../../docs/releases/T5ynth-macOS-Installation-EN.pdf"
do
    if [[ -f "$guide" ]]; then
        cp "$guide" "$STAGE_SUPPORT/docs/"
    fi
done

pkgbuild \
    --root "$STAGE_SUPPORT" \
    --identifier org.ai4artsed.t5ynth.support-data \
    --version "$PACKAGE_VERSION" \
    --install-location "/Library/Application Support/T5ynth" \
    --scripts "$SCRIPT_DIR/scripts" \
    "$WORK/support-data.pkg"

# ── Stage: VST3 plugin (optional) ────────────────────────────────────
if [[ -n "$VST3" ]]; then
    echo "  Staging VST3 plugin..."
    STAGE_VST3="$WORK/stage-vst3"
    mkdir -p "$STAGE_VST3"
    cp -R "$VST3" "$STAGE_VST3/"
    STAGED_VST3="$STAGE_VST3/$(basename "$VST3")"

    if [[ -n "$APP_SIGN_IDENTITY" ]]; then
        echo "  Signing VST3 plugin with Developer ID..."
        codesign \
            --force \
            --deep \
            --timestamp \
            --options runtime \
            --sign "$APP_SIGN_IDENTITY" \
            "$STAGED_VST3"
        codesign --verify --deep --strict "$STAGED_VST3"
    fi

    pkgbuild \
        --root "$STAGE_VST3" \
        --identifier org.ai4artsed.t5ynth.vst3 \
        --version "$PACKAGE_VERSION" \
        --install-location "/Library/Audio/Plug-Ins/VST3" \
        "$WORK/vst3.pkg"
fi

# ── Stage: Audio Unit plugin (optional) ──────────────────────────────
if [[ -n "$AU" ]]; then
    echo "  Staging AU plugin..."
    STAGE_AU="$WORK/stage-au"
    mkdir -p "$STAGE_AU"
    cp -R "$AU" "$STAGE_AU/"
    STAGED_AU="$STAGE_AU/$(basename "$AU")"

    if [[ -n "$APP_SIGN_IDENTITY" ]]; then
        echo "  Signing AU plugin with Developer ID..."
        codesign \
            --force \
            --deep \
            --timestamp \
            --options runtime \
            --sign "$APP_SIGN_IDENTITY" \
            "$STAGED_AU"
        codesign --verify --deep --strict "$STAGED_AU"
    fi

    pkgbuild \
        --root "$STAGE_AU" \
        --identifier org.ai4artsed.t5ynth.au \
        --version "$PACKAGE_VERSION" \
        --install-location "/Library/Audio/Plug-Ins/Components" \
        "$WORK/au.pkg"
fi

# ── Copy resources for the product installer ─────────────────────────
RESOURCES="$WORK/resources"
mkdir -p "$RESOURCES"
if [[ -f "$SCRIPT_DIR/../../LICENSE.txt" ]]; then
    cp "$SCRIPT_DIR/../../LICENSE.txt" "$RESOURCES/"
fi

# ── Build product archive ────────────────────────────────────────────
echo "  Building product installer..."

# Generate distribution.xml dynamically so the choices-outline + pkg-ref
# lists reflect exactly which components were staged. Standalone and
# support-data are always present; VST3 and AU are appended only when
# the corresponding bundles were passed in.
DIST_XML="$WORK/distribution.xml"
EXTRA_OUTLINE=""
EXTRA_CHOICES=""
EXTRA_PKG_REFS=""

if [[ -n "$VST3" ]]; then
    EXTRA_OUTLINE+="        <line choice=\"vst3\"/>"$'\n'
    EXTRA_CHOICES+="    <choice id=\"vst3\" title=\"VST3 Plugin\"
            description=\"T5ynth VST3 plugin (loads in any DAW that supports VST3). Requires the Standalone for the bundled inference backend.\"
            start_selected=\"true\">
        <pkg-ref id=\"org.ai4artsed.t5ynth.vst3\"/>
    </choice>

"
    EXTRA_PKG_REFS+="    <pkg-ref id=\"org.ai4artsed.t5ynth.vst3\" version=\"$PACKAGE_VERSION\" onConclusion=\"none\">vst3.pkg</pkg-ref>"$'\n'
fi

if [[ -n "$AU" ]]; then
    EXTRA_OUTLINE+="        <line choice=\"au\"/>"$'\n'
    EXTRA_CHOICES+="    <choice id=\"au\" title=\"Audio Unit Plugin\"
            description=\"T5ynth Audio Unit (AU) plugin (loads in Logic Pro, GarageBand, etc.). Requires the Standalone for the bundled inference backend.\"
            start_selected=\"true\">
        <pkg-ref id=\"org.ai4artsed.t5ynth.au\"/>
    </choice>

"
    EXTRA_PKG_REFS+="    <pkg-ref id=\"org.ai4artsed.t5ynth.au\" version=\"$PACKAGE_VERSION\" onConclusion=\"none\">au.pkg</pkg-ref>"$'\n'
fi

cat > "$DIST_XML" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>T5ynth</title>
    <license file="LICENSE.txt"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_anywhere="true" enable_currentUserHome="false" enable_localSystem="true"/>

    <choices-outline>
        <line choice="standalone"/>
        <line choice="support-data"/>
${EXTRA_OUTLINE}    </choices-outline>

    <choice id="standalone" title="T5ynth App"
            description="The T5ynth macOS app (required)."
            customLocation="/Applications"
            customLocationAllowAlternateVolumes="true"
            start_selected="true" enabled="false">
        <pkg-ref id="org.ai4artsed.t5ynth.standalone"/>
    </choice>

    <choice id="support-data" title="Factory Presets &amp; Support Data"
            description="Factory presets and model storage directory."
            customLocation="/Library/Application Support/T5ynth"
            customLocationAllowAlternateVolumes="true"
            start_selected="true" enabled="false">
        <pkg-ref id="org.ai4artsed.t5ynth.support-data"/>
    </choice>

${EXTRA_CHOICES}    <pkg-ref id="org.ai4artsed.t5ynth.standalone" version="$PACKAGE_VERSION" onConclusion="none">standalone.pkg</pkg-ref>
    <pkg-ref id="org.ai4artsed.t5ynth.support-data" version="$PACKAGE_VERSION" onConclusion="none">support-data.pkg</pkg-ref>
${EXTRA_PKG_REFS}</installer-gui-script>
EOF

mkdir -p "$(dirname "$OUTPUT")"
UNSIGNED_PRODUCT="$WORK/T5ynth-macOS-Installer-unsigned.pkg"
productbuild \
    --distribution "$WORK/distribution.xml" \
    --package-path "$WORK" \
    --resources "$RESOURCES" \
    "$UNSIGNED_PRODUCT"

FINAL_PRODUCT="$UNSIGNED_PRODUCT"

if [[ -n "$PKG_SIGN_IDENTITY" ]]; then
    echo "  Signing product installer with Developer ID Installer..."
    SIGNED_PRODUCT="$WORK/T5ynth-macOS-Installer-signed.pkg"
    productsign \
        --sign "$PKG_SIGN_IDENTITY" \
        "$UNSIGNED_PRODUCT" \
        "$SIGNED_PRODUCT"
    pkgutil --check-signature "$SIGNED_PRODUCT" >/dev/null
    FINAL_PRODUCT="$SIGNED_PRODUCT"
fi

if build_notary_args; then
    [[ -n "$PKG_SIGN_IDENTITY" ]] || die "notarization requires a signed installer (--sign-pkg-identity)"
    echo "  Submitting installer to Apple notary service..."
    xcrun notarytool submit "$FINAL_PRODUCT" "${NOTARY_ARGS[@]}" --wait
    echo "  Stapling notarization ticket..."
    xcrun stapler staple "$FINAL_PRODUCT"
    xcrun stapler validate "$FINAL_PRODUCT"
fi

rm -f "$OUTPUT"
cp "$FINAL_PRODUCT" "$OUTPUT"

echo "==> Done: $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"
