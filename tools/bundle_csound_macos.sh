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
# Which plugins: exactly those with NO non-system dependency of their own. That is
# computed per file, not from a hand-written list that would drift — a plugin
# needing fltk, jack, fluidsynth, hdf5, portaudio, liblo, libpng, Python, libfaust
# or libwiiuse is skipped and named on every build. (libfaust would be pointless
# anyway: faustcompile returns -1 here and an import("stdfaust.lib") segfaults the
# host — see CLAUDE.md.)
#
# The rule is deliberately stricter than "its dependencies happen to be in
# Contents/libs". Exactly one module would qualify under the looser rule —
# libmp3out.dylib, which needs libmp3lame that libsndfile brings in anyway — and
# it is the mp3 FILE opcodes. An orchestra body reads and writes no files (the
# host owns that), so bundling it would buy nothing and cost a load-command
# rewrite in a directory where nothing else needs one.
#
# TWO SOURCES for the same payload:
#
#   CSOUND_VENDORED_LIBS  a ready Contents/libs tree (third_party/csound/…/lib).
#                         Every load command already resolves inside the bundle,
#                         so bundling is a copy plus signatures — no Homebrew, no
#                         dylibbundler, nothing to install. This is what a build
#                         uses by default, and what CI uses.
#   CSOUND_FRAMEWORK_BINARY  a system framework to flatten and pull apart, which
#                         is how the vendored tree was made in the first place.
#
# STK's rawwave data comes along too, and is the one part of the payload that is
# not a library: Csound's STK module (libstkops) reads getenv("RAWWAVE_PATH") when
# it loads and, finding nothing, refuses to register — measured 2026-08-06, 2267
# opcode entries without it against 2294 with, the difference being every STK
# opcode there is. CsoundEngine.cpp and lco_write.py set the variable to a
# `rawwaves` directory beside the library, the same sibling rule as Opcodes64.
# The directory has to be COMPLETE, which is why the count is checked rather than
# the mere existence: a rawwave file STK asks for and does not find is neither a
# compile error nor silence — stk::StkError escapes the module as an uncaught C++
# exception and abort()s the process, which inside a plugin is the host DAW.
#
# Usage: bundle_csound_macos.sh <bundle> [<bundle> ...]
#   CSOUND_VENDORED_LIBS      a prepared Contents/libs to copy in (takes priority)
#   CSOUND_FRAMEWORK_BINARY   override the framework binary to copy
#   CSOUND_OPCODE_DIR         override the plugin opcode directory to copy
#   CSOUND_RAWWAVES           override the STK rawwave directory to copy
#   CSOUND_LICENSE_DIR        override the licence texts copied into the bundle
set -euo pipefail

VENDORED_LIBS="${CSOUND_VENDORED_LIBS:-}"
# The framework ROOT's symlinks (CsoundLib64 -> Versions/Current/CsoundLib64,
# Resources -> Versions/Current/Resources), never Versions/6.0 directly: a
# version-pinned path is a landmine on the next Csound. `cp` follows them.
FW_BIN="${CSOUND_FRAMEWORK_BINARY:-/opt/homebrew/opt/csound/Frameworks/CsoundLib64.framework/CsoundLib64}"
OPCODE_SRC="${CSOUND_OPCODE_DIR:-$(dirname "$FW_BIN")/Resources/Opcodes64}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LICENSE_SRC="${CSOUND_LICENSE_DIR:-$REPO_ROOT/resources/licenses/csound}"

# The vendored payload already carries rawwaves/, so this only has to find a
# source for the OTHER route — a build against a system framework. The checkout's
# copy first, an installed STK second. Neither present is not an error: deleting
# third_party/csound must stay a valid thing to do (CMakeLists' oldest rule), and
# a bundle without the data is a bundle without the STK opcodes, which is what
# every build before this one was.
#
# STK 5.0.1's own file count, and an ABSOLUTE floor: the source is copied into
# the destination, so comparing the two would let a three-file CSOUND_RAWWAVES
# certify itself, and on a machine with no source at all there would be nothing
# to compare against. tools/verify_csound_bundle.py holds the same number.
RAWWAVES_MIN=41
RAWWAVES_SRC="${CSOUND_RAWWAVES:-}"
if [ -z "$RAWWAVES_SRC" ]; then
    for cand in "$REPO_ROOT/third_party/csound/macos-arm64/lib/rawwaves" \
                /opt/homebrew/share/stk/rawwaves /usr/local/share/stk/rawwaves; do
        if [ -d "$cand" ]; then RAWWAVES_SRC="$cand"; break; fi
    done
fi

[ $# -ge 1 ] || { echo "usage: bundle_csound_macos.sh <bundle> [<bundle> ...]"; exit 1; }

die() { echo "bundle_csound_macos: $*" >&2; exit 1; }

if [ -n "$VENDORED_LIBS" ]; then
    [ -f "$VENDORED_LIBS/CsoundLib64" ] \
        || die "no CsoundLib64 in the vendored payload at $VENDORED_LIBS"
    [ -d "$VENDORED_LIBS/Opcodes64" ] \
        || die "no Opcodes64 in the vendored payload at $VENDORED_LIBS"
else
    command -v dylibbundler >/dev/null \
        || die "dylibbundler not found (brew install dylibbundler)"
    [ -f "$FW_BIN" ]     || die "no Csound framework binary at $FW_BIN"
    [ -d "$OPCODE_SRC" ] || die "no Csound plugin opcode dir at $OPCODE_SRC"
fi
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
#
# /System/Volumes is excluded from the /System/ exemption: on APFS the whole data
# volume — /opt/homebrew included — is also reachable as
# /System/Volumes/Data/opt/homebrew/…, and that spelling would otherwise sail
# through as "the OS's own".
foreign_deps() {
    otool -L "$1" | tail -n +2 | awk '{print $1}' \
        | grep -vE '^(@|/usr/lib/|/System/)' \
        | grep -v "/$(basename "$1")\$" || true
    otool -L "$1" | tail -n +2 | awk '{print $1}' \
        | grep -E '^/System/Volumes/' || true
}

# Every Mach-O in a bundle, NUL-separated. Not `find -perm -u+x`: dylibbundler
# writes its output at mode 0644, so a permission filter hides 9 of the 10 bundled
# dylibs from the very check that exists to catch a missed rewrite (measured — a
# planted 0644 dylib naming /opt/homebrew went unsigned and unchecked).
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
#     the bundle naming a machine-specific path, which is exactly what step 8
#     is there to rule out.
strip_rpaths() {
    local f="$1" pattern="$2" rp
    while :; do
        rp="$(rpaths_of "$f" | grep -E "$pattern" | head -1)" || true
        [ -z "$rp" ] && break
        install_name_tool -delete_rpath "$rp" "$f" 2>/dev/null || break
    done
}

# How many STK data files a directory holds, 0 for one that is not there. Always
# succeeds: `set -o pipefail` turns a glob that matches nothing, or a `find` on a
# missing directory, into a failed assignment and — with `set -e` — an abrupt exit
# in place of the message that was meant to explain what is wrong.
count_raw() {
    local n=0
    [ -d "$1" ] && n="$(find "$1" -maxdepth 1 -type f -name '*.raw' | wc -l)"
    echo "$((n))"
}

# install_rawwaves <Contents/libs> <the libstkops the route would ship> <source>
#
# Put STK's data in a Contents/libs. Called by BOTH routes and BEFORE the plugin
# modules are chosen, because which of the two ships is not an independent
# decision: libstkops without complete data either declines to register or kills
# the host, so the data has to be in place before anything asks whether the module
# may come along.
#
# THE DATA FOLLOWS THE MODULE. Installing it whenever a source happened to be
# findable made the script manufacture the very split the gate in finish_bundle
# rejects: point CSOUND_OPCODE_DIR at a Csound built without STK — which is any
# Csound whose build did not find STK's headers, and which the Csound project's
# own Windows release is — and the checkout's data was copied in beside an opcode
# set that has no reader for it, whereupon the build died on the script's own
# output. Worse, the remedy the message names did not work: the next run re-created
# the directory. Reading the module source instead settles both halves from the
# same fact, before either is written.
#
# Complete or not at all, for the same reason: a source holding 1-40 files would
# install data that no later step is allowed to accompany with the module.
#
# Re-copied on every build rather than only when the directory is missing:
# Contents/libs survives a rebuild on the framework route, so a directory left
# short by an interrupted copy would otherwise stay short for good.
#
# The count also guards the copy itself. `cp -f dir/*.raw` on a directory with
# none of them gets the unexpanded glob, exits 1, and `set -e` ends the run one
# line after `mkdir -p` created the destination — leaving exactly the empty
# directory the checks exist to prevent.
install_rawwaves() {
    local dest="$1/rawwaves" module="$2" src="$3"
    [ -f "$module" ] || return 0
    if [ "$(count_raw "$src")" -ge "$RAWWAVES_MIN" ]; then
        mkdir -p "$dest"
        cp -f "$src"/*.raw "$dest/"
        chmod -R u+w "$dest"
    fi
}

# Leave one of each. dylibbundler rewrites EVERY absolute rpath to the same
# @loader_path/../libs/, so a binary that had two of them now has one load command
# twice — and that is not cosmetic: dyld aborts the process at launch.
dedupe_rpaths() {
    local f="$1" rp
    while :; do
        rp="$(rpaths_of "$f" | sort | uniq -d | head -1)" || true
        [ -z "$rp" ] && break
        install_name_tool -delete_rpath "$rp" "$f" 2>/dev/null || break
    done
}

finish_bundle() {
    # Everything from here is common to both sources of the payload: the
    # licences the LGPL requires in the bundle, the signatures, and the checks
    # that can fail. APP/BIN/LIBS/OPCODES and SKIPPED come from the caller.
    local APP="$1" BIN="$2" LIBS="$3" OPCODES="$4" SKIPPED="$5"
    # 5) STK: the module and its data ship together, or neither ships. Enforced
    #    HERE, on WHAT IS IN THE BUNDLE, because this is the one point both routes
    #    pass through and the bundle's own state is the only thing that decides
    #    whether it works. Deciding it from the SOURCE let both splits through,
    #    each exiting 0 and each rejected afterwards by
    #    tools/verify_csound_bundle.py: a stale but complete rawwaves with no
    #    module (the framework route had no source, so it skipped the module), and
    #    a vendored payload carrying the module with no data at all (the vendored
    #    route hands over a whole Contents/libs and never consults a source).
    #
    #    A `die` in both directions rather than a repair: install_rawwaves and
    #    step 3 already hold every input needed to get this right, so arriving
    #    here split means one of them was handed something broken — a payload
    #    assembled by hand, or a Contents/libs left half-written. The message says
    #    which half is missing.
    local RAWWAVES="$LIBS/rawwaves" have
    have="$(count_raw "$RAWWAVES")"
    if [ -f "$OPCODES/libstkops.dylib" ]; then
        [ "$have" -ge "$RAWWAVES_MIN" ] || die "libstkops.dylib is in $OPCODES but \
$RAWWAVES holds $have of $RAWWAVES_MIN STK data files. That module reads \
RAWWAVE_PATH as it loads: with no data it declines to register and all 27 STK \
opcodes are silently gone, and with PART of the data the first STK note abort()s \
the host process. Supply the data (CSOUND_RAWWAVES=…), or an opcode source \
without the module."
    elif [ "$have" -gt 0 ]; then
        die "$RAWWAVES holds $have STK data files but there is no \
libstkops.dylib in $OPCODES, so nothing in this bundle can read them — a \
capability this bundle had and has lost. Reachable by rebuilding over a bundle \
that DID carry the module, against a Csound built without STK. Remove that \
directory (nothing re-creates it once the module is gone), or supply an opcode \
source that carries the module."
    fi

    # 6) LGPL 2.1: the licence text and the notice that says which libraries are
    #    bundled and how to replace them ship INSIDE the bundle, not only in the repo.
    mkdir -p "$APP/Contents/Resources/licenses/csound"
    cp -f "$LICENSE_SRC"/*.txt "$APP/Contents/Resources/licenses/csound/"

    # 7) Re-sign every copied file INDIVIDUALLY, then the executable, then re-seal
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

    # 8) THE CHECKS THAT CAN FAIL, after everything else has succeeded.
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
    #
    #    A symlink under Contents/libs is a hole in all of it: `find -type f` does
    #    not report one and `os.walk` does not descend one, so a Contents/libs that
    #    LOOKS complete can consist of links to files that exist only here — signed
    #    by nobody, checked by nothing, absent on the user's machine.
    local links
    links="$(find "$LIBS" -type l 2>/dev/null || true)"
    [ -z "$links" ] || die "symlink(s) under Contents/libs, which escape every \
check below and point at files the user's machine will not have: \
$(echo "$links" | sed "s|^$APP/||" | tr '\n' ' ')"

    local offenders="" refs dupes
    while IFS= read -r -d '' m; do
        codesign --verify "$m" >/dev/null 2>&1 \
            || die "unsigned or broken signature after bundling: ${m#"$APP"/}"
        refs="$({ otool -L "$m" 2>/dev/null | tail -n +2 | awk '{print $1}'
                  rpaths_of "$m" 2>/dev/null; } \
                | grep -vE '^(@|/usr/lib/|/System/)' || true)"
        refs="$refs
$({ otool -L "$m" 2>/dev/null | tail -n +2 | awk '{print $1}'
    rpaths_of "$m" 2>/dev/null; } | grep -E '^/System/Volumes/' || true)"
        refs="$(echo "$refs" | grep -v '^$' || true)"
        [ -z "$refs" ] || offenders="$offenders
${m#"$APP"/}: $(echo "$refs" | tr '\n' ' ')"
        # dyld aborts the process on a duplicate LC_RPATH, before main(), with no
        # part of this bundle at fault that any other check can see.
        dupes="$(rpaths_of "$m" 2>/dev/null | sort | uniq -d || true)"
        [ -z "$dupes" ] || die "duplicate LC_RPATH in ${m#"$APP"/} (dyld kills the \
process at launch): $(echo "$dupes" | tr '\n' ' ')"
    done < <(printf '%s\0' "$BIN"; mach_o_files "$LIBS")

    [ -z "$offenders" ] || die "bundle still reaches outside itself:$offenders"

    echo "  $(basename "$APP"): \
$(ls -1 "$LIBS" | grep -vE '^(Opcodes64|rawwaves)$' | wc -l | tr -d ' ') libs, \
$(ls -1 "$OPCODES" | wc -l | tr -d ' ') plugin opcode modules, \
$(count_raw "$RAWWAVES") STK rawwaves"
    [ -z "$SKIPPED" ] || echo "$SKIPPED" | grep -v '^$' \
        | sed 's/^/    not bundled: /'
}

bundle_one() {
    local APP="$1"
    [ -d "$APP" ] || die "not a bundle: $APP"
    # Absolute from here on. dylibbundler resolves @loader_path against the file it
    # is fixing and then compares the result with the destination it was given as a
    # STRING: hand it a relative bundle path and it tries to copy CsoundLib64 onto
    # itself, cp refuses, and the run dies mid-bundle. CMake always passes an
    # absolute path, so this only ever bit a hand-typed one.
    APP="$(cd "$APP" && pwd -P)"

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

    # 0) The vendored payload: a Contents/libs that is already correct. Copy it,
    #    strip whatever rpath the link line left in our own binary, and go
    #    straight to the licences, the signatures and the checks — steps 1 to 4
    #    exist only to MAKE such a tree out of a system framework.
    if [ -n "$VENDORED_LIBS" ]; then
        rm -rf "$LIBS"
        mkdir -p "$LIBS"
        cp -R "$VENDORED_LIBS"/. "$LIBS/"
        chmod -R u+w "$LIBS"
        # The payload normally brings rawwaves/ with it, and its own copy stands:
        # only an EXPLICIT CSOUND_RAWWAVES refreshes it, never the checkout that
        # the framework route falls back on. That is what lets the override
        # complete a hand-prepared payload carrying the STK module and no data,
        # without overwriting one that brought data of its own.
        install_rawwaves "$LIBS" "$LIBS/Opcodes64/libstkops.dylib" \
                         "${CSOUND_RAWWAVES:-}"
        install_name_tool -change "$OLD_REF" "@loader_path/../libs/CsoundLib64" \
            "$BIN" 2>/dev/null || true
        strip_rpaths "$BIN" '^[^@]'
        dedupe_rpaths "$BIN"
        finish_bundle "$APP" "$BIN" "$LIBS" "$OPCODES" ""
        return 0
    fi

    # 1) Flatten the framework binary to a plain dylib in Contents/libs.
    cp -f "$FW_BIN" "$LIBS/CsoundLib64"
    chmod +w "$LIBS/CsoundLib64"
    install_name_tool -id "@loader_path/../libs/CsoundLib64" "$LIBS/CsoundLib64" 2>/dev/null
    strip_rpaths "$LIBS/CsoundLib64" '.'

    # 2) Point the bundle's own binary at the flattened dylib. Idempotent: on a
    #    rebuild the linker has restored the absolute reference, and when it has
    #    not, OLD_REF is already the @loader_path form and this is a no-op.
    install_name_tool -change "$OLD_REF" "@loader_path/../libs/CsoundLib64" "$BIN" 2>/dev/null
    # Every rpath that is not bundle-relative goes, for the same reason foreign_deps
    # is a whitelist: CMake records an rpath from wherever Csound was FOUND, and
    # T5YNTH_CSOUND_PREFIX may name any prefix. A blacklist of /opt/homebrew and
    # /usr/local left a CI-workspace rpath standing, dylibbundler then rewrote it —
    # and the one below it — to the SAME @loader_path/../libs/, and dyld hard-fails
    # on a duplicate LC_RPATH. Both gates were green on that bundle; the app died
    # at launch with "duplicate LC_RPATH".
    strip_rpaths "$BIN" '^[^@]'

    # 3) Plugin opcodes: copy the ones whose dependencies we can satisfy, name the
    #    rest. A skipped plugin is a capability the LRO's author cannot reach, so
    #    this list is printed on every build rather than decided silently.
    #
    #    STK's data goes in FIRST, because one of those decisions depends on it:
    #    libstkops may only come along if the data it reads is there and complete.
    #    And the data goes in only if this opcode source HAS that module — a
    #    Csound built without STK has none, and data with no reader is the other
    #    half of the same split.
    install_rawwaves "$LIBS" "$OPCODE_SRC/libstkops.dylib" "$RAWWAVES_SRC"
    rm -rf "$OPCODES"
    mkdir -p "$OPCODES"
    local skipped="" f deps
    for f in "$OPCODE_SRC"/*.dylib; do
        [ -f "$f" ] || continue
        deps="$(foreign_deps "$f" | sed 's|.*/||' | sort -u | tr '\n' ' ')"
        # libstdutil is Csound's UTILITY module (srconv, dnoise …), not opcodes,
        # and it is the only dep-carrying file we would otherwise have to rewrite.
        if [ -n "${deps// /}" ] || [ "$(basename "$f")" = "libstdutil.dylib" ]; then
            skipped="$skipped$(basename "$f") [${deps:-utilities, not opcodes}]
"
            continue
        fi
        # libstkops without complete STK data is a module that either declines to
        # register or kills the host on the first note, so it comes along only
        # when the data is already in Contents/libs — the DESTINATION, which
        # install_rawwaves has just written and which a previous build may also
        # have left there. Asking the SOURCE instead dropped the module on a
        # rebuild that already had all 41 files sitting in the bundle.
        if [ "$(basename "$f")" = "libstkops.dylib" ] \
           && [ "$(count_raw "$LIBS/rawwaves")" -lt "$RAWWAVES_MIN" ]; then
            skipped="$skipped$(basename "$f") [no STK rawwave data to go with it]
"
            continue
        fi
        cp -f "$f" "$OPCODES/"
        chmod +w "$OPCODES/$(basename "$f")"
        # Its recorded install name is the absolute path it was built at. Nothing
        # links against a dlopen'd plugin, so this is hygiene rather than a fix —
        # but it is also what lets step 8 stay strict, with no exception for the
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
    dedupe_rpaths "$BIN"

    finish_bundle "$APP" "$BIN" "$LIBS" "$OPCODES" "$skipped"
}

if [ -n "$VENDORED_LIBS" ]; then
    echo "bundling Csound (vendored payload $VENDORED_LIBS)"
else
    echo "bundling Csound ($FW_BIN)"
fi
for app in "$@"; do
    bundle_one "$app"
done
