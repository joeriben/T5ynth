#!/usr/bin/env bash
# Make a JUCE .app self-contained w.r.t. Csound by bundling CsoundLib64 and its
# whole non-system dependency tree into Contents/libs and rewriting every load
# command to @loader_path/../libs. End users then need NO system Csound.
#
# Why flatten the framework: Homebrew ships CsoundLib64 as a .framework whose
# binary links an absolute /opt/homebrew tree (libsndfile -> ogg/vorbis/FLAC/
# opus/mpg123/lame, plus gettext/libintl).
# The LCO orchestras use only CORE opcodes -- verified against the whole of
# backend/lco_library.json on 2026-07-23: atone, balance, butterhp, butterlp,
# changed2, foscili, gbuzz, limit, mode, oscili, poscil, rand, randi, reson,
# syncphasor, tablei, tone, tonek, vco2, vdelay, vdelay3. All are compiled into
# libcsound, so no framework-relative plugin/resource dir is needed
# and the framework can be flattened to a plain dylib. dylibbundler ignores
# .framework inputs, so we flatten first, then let it bundle the rest of the tree.
#
# Usage: bundle_csound_macos.sh <path-to.app> [<csound-framework-binary>]
set -euo pipefail

APP="${1:?usage: bundle_csound_macos.sh <path-to.app-or-plugin-bundle> [csound-fw-binary]}"
FW_BIN="${2:-/opt/homebrew/opt/csound/Frameworks/CsoundLib64.framework/Versions/6.0/CsoundLib64}"
# The executable name is NOT necessarily the bundle folder name (and differs for
# VST3/AU bundles) -- read CFBundleExecutable, fall back to the sole Mach-O.
EXEC_NAME="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$APP/Contents/Info.plist" 2>/dev/null || true)"
[ -n "$EXEC_NAME" ] || EXEC_NAME="$(ls "$APP/Contents/MacOS" 2>/dev/null | head -1)"
BIN="$APP/Contents/MacOS/$EXEC_NAME"
LIBS="$APP/Contents/libs"

[ -x "$BIN" ]     || { echo "no executable at $BIN"; exit 1; }
[ -f "$FW_BIN" ]  || { echo "no Csound framework binary at $FW_BIN"; exit 1; }

# The absolute load command the app currently records for Csound.
OLD_REF="$(otool -L "$BIN" | awk '/CsoundLib64/{print $1; exit}')"
[ -n "$OLD_REF" ] || { echo "app does not link CsoundLib64 — nothing to bundle"; exit 0; }

mkdir -p "$LIBS"

# 1) Flatten the framework binary to a plain dylib in Contents/libs.
cp -f "$FW_BIN" "$LIBS/CsoundLib64"
chmod +w "$LIBS/CsoundLib64"
install_name_tool -id "@loader_path/../libs/CsoundLib64" "$LIBS/CsoundLib64"

# 2) Point the app at the flattened dylib.
install_name_tool -change "$OLD_REF" "@loader_path/../libs/CsoundLib64" "$BIN"

# 3) Let dylibbundler pull CsoundLib64's own tree (libsndfile & co.) into libs
#    and rewrite every inter-lib reference to @loader_path/../libs.
dylibbundler -of -b -cd \
  -x "$LIBS/CsoundLib64" \
  -x "$BIN" \
  -d "$LIBS" \
  -p "@loader_path/../libs/" < /dev/null

# 4) Dedupe LC_RPATHs. Homebrew's CsoundLib64 carried two rpaths
#    (@loader_path/../lib and @loader_path/../Frameworks); dylibbundler rewrites
#    BOTH to @loader_path/../libs/, and dyld HARD-FAILS on a duplicate
#    LC_RPATH ("duplicate LC_RPATH"). Every dep was rewritten to an
#    @executable_path-absolute reference, so these rpaths are vestigial anyway —
#    collapse any value that appears more than once down to a single entry.
dedupe_rpaths() {
    local f="$1" rp
    while :; do
        rp=$(otool -l "$f" | awk '/cmd LC_RPATH/{g=1} g&&/^ *path /{print $2; g=0}' \
             | sort | uniq -d | head -1)
        [ -z "$rp" ] && break
        install_name_tool -delete_rpath "$rp" "$f"
    done
}
for f in "$BIN" "$LIBS"/*; do
    [ -f "$f" ] && dedupe_rpaths "$f"
done

# 5) Re-sign every bundled dylib, then re-seal the whole bundle (ad-hoc). The
#    bundle-level signature must be regenerated so its _CodeSignature covers the
#    newly-added libs/ (otherwise codesign --verify reports a broken seal).
for f in "$LIBS"/*; do
    [ -f "$f" ] && codesign --force --sign - "$f" 2>/dev/null || true
done
codesign --force --sign - "$BIN" 2>/dev/null || true
codesign --force --sign - "$APP" 2>/dev/null || true

echo "bundled Csound tree into $LIBS:"
ls -1 "$LIBS"
