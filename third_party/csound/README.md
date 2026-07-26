# Csound, as the app ships it

The LRO **is** Csound: an LRO sound is an orchestra the author writes, compiled and
performed at runtime. So Csound has to be inside the app — not a thing the user
installs first, and not a thing a CI runner has to be talked into providing.

What is here is the exact payload that goes into a build, per platform:

| | |
|---|---|
| `include/csound/` | the C API headers the build compiles against, shared by every platform. Measured 2026-07-26: the Windows release's headers are byte-identical to the macOS ones apart from line endings — the only real difference is a typo fix in `plugin.h`, which is for *writing* plugin opcodes and which nothing here includes. |
| `macos-arm64/lib/` | drop-in copy of an app bundle's `Contents/libs`: `CsoundLib64`, its 9 support libraries, and `Opcodes64/` with the 24 dependency-free plugin opcode modules. Every load command is already `@loader_path/../libs/…`, so bundling is a copy and needs no `dylibbundler` and no Homebrew. |
| `windows-x64/lib/csound64.lib` | the import library to link against. |
| `windows-x64/bin/` | drop-in copy of what sits next to the module at runtime: `csound64.dll` and `plugins64/` with 16 dependency-free plugin opcode modules. |

Nothing here is modified Csound. On Windows the files are the Csound project's own
release binaries byte for byte; on macOS they are stock Homebrew builds with only
the recorded library paths rewritten (`install_name_tool`), which is what lets them
resolve inside a bundle.

Neither payload adds a system dependency. Measured: `csound64.dll` imports nothing
but KERNEL32, WS2_32 and the MSVC runtime (`VCRUNTIME140` + the UCRT), which our own
MSVC build already needs; the macOS libraries name nothing outside `/usr/lib` and
`/System`, which `tools/verify_csound_bundle.py` re-checks on every build.

## Provenance

| | |
|---|---|
| Csound | 6.18.1, LGPL-2.1-or-later, https://github.com/csound/csound |
| macOS payload | Homebrew `csound 6.18.1_14`, bottle for arm64 macOS, taken 2026-07-26 from `/opt/homebrew/Frameworks/CsoundLib64.framework` |
| Windows payload | `Csound-6.18.1-windows-x64-binaries.zip` from the 6.18.1 GitHub release, taken 2026-07-26 · SHA-256 `bd499ac6f476d98c6ae951a8fbfda365e594ef34bcf480c5a273281df940c220` |
| headers | the same zip's `include/`, plus the two generated headers (`float-version.h`, `version.h`) from the macOS side, which the zip only ships as CMake templates |
| support libraries | macOS: libsndfile 1.2.2, libFLAC 1.5.0, libogg 1.3.6, libvorbis(+enc) 1.3.7, libopus 1.6.1, libmpg123 1.33.6, libmp3lame 3.100, libintl 1.0 — separate dylibs. Windows: the same family is linked **inside** `csound64.dll` by the Csound project (libsndfile 1.1.0, libFLAC 1.3.4, libvorbis 1.3.7, …). Versions and licences in `THIRD_PARTY_LICENSES.txt` and in the `NOTICE.txt` that ships beside the library. |

### Which plugin opcode modules, and why not all of them

Both platforms ship the modules that need nothing beyond the system: no FLTK, no
JACK, no Python, no PortAudio, no liblo. The two sets differ because upstream
builds differ, not because anything was dropped by choice — Windows has no
`urandom`, `chua`, `control`, `linear_algebra`, `p5g`, `stkops`, `cmidi` or
`ableton_link_opcodes` module in its release, and `rtauhal` is replaced by
`rtwinmm`. Checked before vendoring: no orchestra in `backend/lco_library.json`
uses an opcode from any of them.

## Replacing it (LGPL 2.1 §6)

The point of shipping the shared library rather than linking it statically is that
you can put your own build in its place. The `NOTICE.txt` beside the library says
how, per platform.

No entitlement is involved on macOS, and none is needed. Measured 2026-07-26:
replacing any file under `Contents/libs` invalidates the bundle's seal
(`codesign --verify` reports "a sealed resource is missing or invalid"), so the
re-sign the NOTICE asks for is unavoidable — and that re-sign, ad-hoc and without
`--options runtime`, is itself what leaves library validation out of the picture.
`com.apple.security.cs.disable-library-validation` would only weaken the shipped
app's hardened runtime while buying the replacer nothing.

## Refreshing it

**macOS.** Install the Csound version you want, build a bundle with it the old way
(`CSOUND_FRAMEWORK_BINARY=… tools/bundle_csound_macos.sh <bundle>`), then copy that
bundle's `Contents/libs` over `macos-arm64/lib`.

**Windows.** Take `Csound-<version>-windows-x64-binaries.zip` from the Csound GitHub
release: `build/Release/csound64.lib` → `windows-x64/lib/`, `build/Release/csound64.dll`
→ `windows-x64/bin/`, and the dependency-free opcode DLLs → `windows-x64/bin/plugins64/`.
Check the dependencies before adding a module (`llvm-objdump -p <dll> | grep 'DLL Name'`
works on macOS too). A module may name the OS's own DLLs, `csound64.dll`, and the MSVC
runtime our build already requires (`VCRUNTIME140`, `VCRUNTIME140_1`, `MSVCP140`, the
`api-ms-win-crt-*` UCRT set). Anything else — `liblo`, `portaudio`, `portmidi` — brings
a file of its own and does not belong here.

**Both.** The header sets are meant to stay one tree — after a refresh, diff the new
platform headers against `include/csound/` and split them only if they have really
diverged. Then update the tables above, `THIRD_PARTY_LICENSES.txt` and
`resources/licenses/csound/NOTICE.txt`, and re-run `tools/verify_csound_bundle.py`
and `tools/verify_lro_in_standalone.py`.
