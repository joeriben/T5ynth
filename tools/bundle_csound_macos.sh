#!/usr/bin/env bash
# Make a JUCE bundle self-contained w.r.t. Csound: copy CsoundLib64, its plugin
# opcodes and its whole non-system dependency tree into Contents/libs, rewrite
# every load command to @loader_path/../libs, and re-sign. End users then need
# NO system Csound. Works on a .app, a .vst3 and a .component alike — all three
# link CsoundLib64, and all three need the copy.
#
# Why flatten the framework: Homebrew ships CsoundLib64 as a .framework whose
# binary links an absolute /opt/homebrew tree (libsndfile -> ogg/vorbis/FLAC/
# opus/mpg123/lame, plus gettext/libintl). dylibbundler ignores .framework
# inputs, so we flatten first and let it bundle the rest of the tree. Flattening
# does not statically link anything: the result is still the shared library,
# replaceable by the user (resources/licenses/csound/NOTICE.txt says how).
#
# Why the plugin opcodes come along, MEASURED 2026-07-26 and easy to get wrong:
# CsoundLib64 finds Resources/Opcodes64 through an absolute path baked in at
# build time (`strings` shows /opt/homebrew/Cellar/csound/<version>/…/Opcodes64).
# On this machine a flattened copy therefore still reaches into the Cellar and
# everything works; on a machine WITHOUT Homebrew Csound the same copy is down to
# the 1917 built-in opcode entries — no scanu/scanu2/scans, no fractalnoise, no
# tvconv, MixerSend, ftgenonce, limit1 or GEN padsynth. The LRO's author WRITES
# Csound, so every one of those is something it may legitimately reach for. The
# 24 modules bundled here restore 350 entries (2267 registered); the 215 further
# entries Homebrew's full set would add (2482) are in the modules we cannot
# bundle — fltk widgets, jack, Python, websockets and the like, none of them
# synthesis. They land in Contents/libs/Opcodes64, and the host points Csound at
# that directory at runtime (CsoundEngine.cpp, lco_write.py).
#
# Which plugins: exactly those whose non-system dependencies are already covered
# by what we bundle. That is computed per file, not from a hand-written list that
# would drift — a plugin needing fltk, jack, fluidsynth, hdf5, portaudio, liblo,
# libpng, Python, libfaust or libwiiuse is skipped and named. (libfaust would be
# pointless anyway: faustcompile returns -1 here and an import("stdfaust.lib")
# segfaults the host — see CLAUDE.md.)
#
# Usage: bundle_csound_macos.sh <bundle> [<bundle> ...]
#   CSOUND_FRAMEWORK_BINARY   override the framework binary to copy
#   CSOUND_OPCODE_DIR         override the plugin opcode directory to copy
#   CSOUND_LICENSE_DIR        override the licence texts copied into the bundle
set -euo pipefail

# The framework ROOT's symlinks (CsoundLib64 -> Versions/Current/CsoundLib64,
# Resources -> Versions/Current/Resources), never Versions/6.0 directly: a
# version-pinned path is a landmine on the next Csound. `cp` follows them.
FW_BIN="${CSOUND_FRAMEWORK_BINARY:-/opt/homebrew/opt/csound/Frameworks/CsoundLib64.framework/CsoundLib64}"
OPCODE_SRC="${CSOUND_OPCODE_DIR:-$(dirname "$FW_BIN")/Resources/Opcodes64}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LICENSE_SRC="${CSOUND_LICENSE_DIR:-$REPO_ROOT/resources/licenses/csound}"

[ $# -ge 1 ] || { echo "usage: bundle_csound_macos.sh <bundle> [<bundle> ...]"; exit 1; }

die() { echo "bundle_csound_macos: $*" >&2; exit 1; }

command -v dylibbundler >/dev/null \
    || die "dylibbundler not found (brew install dylibbundler)"
[ -f "$FW_BIN" ]     || die "no Csound framework binary at $FW_BIN"
[ -d "$OPCODE_SRC" ] || die "no Csound plugin opcode dir at $OPCODE_SRC"
[ -f "$LICENSE_SRC/NOTICE.txt" ] \
    || die "no Csound licence texts at $LICENSE_SRC (LGPL 2.1 requires them in the bundle)"

# Every non-system, non-self dependency this Mach-O records, one per line.
#
# WHITELIST, not blacklist: a reference is acceptable only if it is bundle-relative
# (@loader_path/@executable_path/@rpath) or the OS's own (/usr/lib, /System). A
# blacklist of /opt/homebrew|/usr/local|Cellar looks equivalent and is not — CMake
# records an rpath from wherever Csound was FOUND (T5YNTH_CSOUND_PREFIX may name
# any prefix), and a leftover reference to that prefix would pass a blacklist and
# exist on no other machine.
foreign_deps() {
    otool -L "$1" | tail -n +2 | awk '{print $1}' \
        | grep -vE '^(@|/usr/lib/|/System/)' \
        | grep -v "/$(basename "$1")\$" || true
}

# Every Mach-O in a bundle, NUL-separated. Not `find -perm -u+x`: dylibbundler
# writes its output at mode 0644, so a permission filter hides 9 of the 10 bundled
# dylibs from the very check that exists to catch a missed rewrite (measured — a
# planted 0644 dylib naming /opt/homebrew passed step 6 unnoticed).
mach_o_files() {
    find "$1" -type f -print0 | while IFS= read -r -d '' f; do
        case "$(file -b "$f" 2>/dev/null)" in
            *Mach-O*) printf '%s\0' "$f" ;;
        esac
    done
}

rpaths_of() {
    otool -l "$1" | awk '/cmd LC_RPATH/{g=1} g&&/^ *path /{print $2; g=0}'
}

# Delete the LC_RPATHs matching a pattern. Two reasons, both concrete:
#
#   * Homebrew's CsoundLib64 carries @loader_path/../lib and
#     @loader_path/../Frameworks. Neither is used — every dependency it records is
#     an absolute path, not an @rpath one — but dylibbundler rewrites BOTH to
#     @loader_path/../libs/, and dyld HARD-FAILS on a duplicate LC_RPATH.
#   * The bundle's own binary carries -rpath /opt/homebrew/…/Frameworks from the
#     CMake link line. Harmless once the load command is rewritten, but it leaves
#     the bundle naming a machine-specific path, which is exactly what step 6
#     is there to rule out.
strip_rpaths() {
    local f="$1" pattern="$2" rp
    while :; do
        rp="$(rpaths_of "$f" | grep -E "$pattern" | head -1)" || true
        [ -z "$rp" ] && break
        install_name_tool -delete_rpath "$rp" "$f" 2>/dev/null || break
    done
}

bundle_one() {
    local APP="$1"
    [ -d "$APP" ] || die "not a bundle: $APP"

    # The executable name is NOT necessarily the bundle folder name (and differs
    # for VST3/AU bundles) -- read CFBundleExecutable, fall back to the sole Mach-O.
    local EXEC_NAME BIN LIBS OPCODES OLD_REF
    EXEC_NAME="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' \
                 "$APP/Contents/Info.plist" 2>/dev/null || true)"
    [ -n "$EXEC_NAME" ] || EXEC_NAME="$(ls "$APP/Contents/MacOS" 2>/dev/null | head -1)"
    BIN="$APP/Contents/MacOS/$EXEC_NAME"
    LIBS="$APP/Contents/libs"
    OPCODES="$LIBS/Opcodes64"

    [ -x "$BIN" ] || die "no executable at $BIN"

    # The load command this bundle currently records for Csound. Absent means the
    # bundle was built without Csound (T5YNTH_CSOUND_FOUND false) — nothing to do,
    # and NOT an error: a build without Csound must stay a valid build.
    OLD_REF="$(otool -L "$BIN" | awk '/CsoundLib64/{print $1; exit}')"
    if [ -z "$OLD_REF" ]; then
        echo "  $(basename "$APP"): does not link CsoundLib64 — nothing to bundle"
        return 0
    fi

    mkdir -p "$LIBS"

    # 1) Flatten the framework binary to a plain dylib in Contents/libs.
    cp -f "$FW_BIN" "$LIBS/CsoundLib64"
    chmod +w "$LIBS/CsoundLib64"
    install_name_tool -id "@loader_path/../libs/CsoundLib64" "$LIBS/CsoundLib64" 2>/dev/null
    strip_rpaths "$LIBS/CsoundLib64" '.'

    # 2) Point the bundle's own binary at the flattened dylib. Idempotent: on a
    #    rebuild the linker has restored the absolute reference, and when it has
    #    not, OLD_REF is already the @loader_path form and this is a no-op.
    install_name_tool -change "$OLD_REF" "@loader_path/../libs/CsoundLib64" "$BIN" 2>/dev/null
    strip_rpaths "$BIN" '^/opt/homebrew|^/usr/local|/Cellar/'

    # 3) Plugin opcodes: copy the ones whose dependencies we can satisfy, name the
    #    rest. A skipped plugin is a capability the LRO's author cannot reach, so
    #    this list is printed on every build rather than decided silently.
    rm -rf "$OPCODES"
    mkdir -p "$OPCODES"
    local skipped=() f deps
    for f in "$OPCODE_SRC"/*.dylib; do
        [ -f "$f" ] || continue
        deps="$(foreign_deps "$f" | sed 's|.*/||' | sort -u | tr '\n' ' ')"
        # libstdutil is Csound's UTILITY module (srconv, dnoise …), not opcodes,
        # and it is the only dep-carrying file we would otherwise have to rewrite.
        if [ -n "${deps// /}" ] || [ "$(basename "$f")" = "libstdutil.dylib" ]; then
            skipped+=("$(basename "$f") [${deps:-utilities, not opcodes}]")
            continue
        fi
        cp -f "$f" "$OPCODES/"
        chmod +w "$OPCODES/$(basename "$f")"
        # Its recorded install name is the absolute path it was built at. Nothing
        # links against a dlopen'd plugin, so this is hygiene rather than a fix —
        # but it is also what lets step 6 stay strict, with no exception for the
        # one load command that is a self-reference.
        install_name_tool -id "@loader_path/$(basename "$f")" \
            "$OPCODES/$(basename "$f")" 2>/dev/null
    done
    # libscansyn carries scanu/scanu2/scans — named in CLAUDE.md as available on
    # this build, and the canary that step 3 actually ran.
    [ -f "$OPCODES/libscansyn.dylib" ] \
        || die "libscansyn.dylib did not reach $OPCODES (scanu/scans would be gone)"

    # 4) Let dylibbundler pull CsoundLib64's own tree (libsndfile & co.) into libs
    #    and rewrite every inter-lib reference to @loader_path/../libs. That prefix
    #    is right for both locations: for the executable in Contents/MacOS and for a
    #    dylib in Contents/libs, ../libs is Contents/libs. The copied plugins are
    #    dependency-free by construction (step 3), so they are not passed in.
    dylibbundler -of -b -cd \
      -x "$LIBS/CsoundLib64" \
      -x "$BIN" \
      -d "$LIBS" \
      -p "@loader_path/../libs/" < /dev/null

    for f in "$LIBS"/*; do
        if [ -f "$f" ]; then strip_rpaths "$f" '.'; fi
    done

    # 5) LGPL 2.1: the licence text and the notice that says which libraries are
    #    bundled and how to replace them ship INSIDE the bundle, not only in the repo.
    mkdir -p "$APP/Contents/Resources/licenses/csound"
    cp -f "$LICENSE_SRC"/*.txt "$APP/Contents/Resources/licenses/csound/"

    # 6) Re-sign every copied file INDIVIDUALLY, then the executable, then re-seal
    #    the bundle. Three measured reasons this is not boilerplate:
    #      * install_name_tool invalidates a dylib's signature, and dyld does not
    #        merely refuse such a library — it KILLS the process (rc 137).
    #      * `codesign --deep` does NOT sign loose dylibs inside a bundle: an
    #        unsigned Contents/libs/libogg.dylib stays unsigned through
    #        `codesign --force --deep --sign - <app>`. Every file must be named.
    #      * a failure here must be FATAL. With `|| true` the script printed its
    #        success line and exited 0 on a bundle that would SIGKILL on launch.
    while IFS= read -r -d '' f; do
        codesign --force --sign - "$f" >/dev/null 2>&1 \
            || die "could not sign $f"
    done < <(mach_o_files "$LIBS")
    codesign --force --sign - "$BIN" >/dev/null 2>&1 || die "could not sign $BIN"
    codesign --force --deep --sign - "$APP" >/dev/null 2>&1 \
        || die "could not seal $(basename "$APP")"

    # 7) THE CHECKS THAT CAN FAIL, after everything else has succeeded.
    #
    #    `codesign --verify --deep --strict` is NOT one of them: measured, it
    #    returns 0 on a bundle containing an unsigned dylib, because Contents/libs
    #    is sealed as RESOURCES — the bytes are checked, the nested signatures are
    #    not. So each Mach-O is verified by name, and each is required to name
    #    nothing outside the bundle but the OS's own libraries. The second check is
    #    the difference between a bundle that works everywhere and one that happens
    #    to work here, where the path it still names exists.
    #    Scope: the bundle's own binary and everything WE put in Contents/libs.
    #    Deliberately not the whole bundle — a release .app also carries the
    #    PyInstaller backend, whose hundreds of Mach-Os are another concern's
    #    responsibility, and a gate that drags them in would either be noisy or
    #    would have to learn exceptions until it stops meaning anything.
    local offenders="" refs
    while IFS= read -r -d '' m; do
        codesign --verify "$m" >/dev/null 2>&1 \
            || die "unsigned or broken signature after bundling: ${m#"$APP"/}"
        refs="$({ otool -L "$m" 2>/dev/null | tail -n +2 | awk '{print $1}'
                  rpaths_of "$m" 2>/dev/null; } \
                | grep -vE '^(@|/usr/lib/|/System/)' || true)"
        [ -z "$refs" ] || offenders="$offenders
${m#"$APP"/}: $(echo "$refs" | tr '\n' ' ')"
    done < <(printf '%s\0' "$BIN"; mach_o_files "$LIBS")

    [ -z "$offenders" ] || die "bundle still reaches outside itself:$offenders"

    echo "  $(basename "$APP"): $(ls -1 "$LIBS" | grep -v Opcodes64 | wc -l | tr -d ' ') libs, \
$(ls -1 "$OPCODES" | wc -l | tr -d ' ') plugin opcode modules"
    if [ ${#skipped[@]} -gt 0 ]; then
        printf '    not bundled (foreign dependency): %s\n' "${skipped[@]}"
    fi
}

echo "bundling Csound ($FW_BIN)"
for app in "$@"; do
    bundle_one "$app"
done
