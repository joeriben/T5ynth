# Csound, as the app ships it

The LRO **is** Csound: an LRO sound is an orchestra the author writes, compiled and
performed at runtime. So Csound has to be inside the app — not a thing the user
installs first, and not a thing a CI runner has to be talked into providing.

What is here is the exact payload that goes into a build, per platform:

| | |
|---|---|
| `macos-arm64/lib/` | drop-in copy of an app bundle's `Contents/libs`: `CsoundLib64`, its 9 support libraries, and `Opcodes64/` with the 24 dependency-free plugin opcode modules. Every load command is already `@loader_path/../libs/…`, so bundling is a copy and needs no `dylibbundler` and no Homebrew. |
| `macos-arm64/include/csound/` | the C API headers the build compiles against. |

Nothing here is modified Csound. The binaries are stock builds; only the recorded
library paths were rewritten (`install_name_tool`), which is what lets them resolve
inside a bundle.

## Provenance

| | |
|---|---|
| Csound | 6.18.1, LGPL-2.1-or-later, https://github.com/csound/csound |
| built by | Homebrew, `csound 6.18.1_14`, bottle for arm64 macOS |
| taken on | 2026-07-26, from `/opt/homebrew/Frameworks/CsoundLib64.framework` |
| support libraries | libsndfile 1.2.2, libFLAC 1.5.0, libogg 1.3.6, libvorbis(+enc) 1.3.7, libopus 1.6.1, libmpg123 1.33.6, libmp3lame 3.100, libintl (gettext) 1.0 — versions and licences in `THIRD_PARTY_LICENSES.txt` and in every bundle's `Contents/Resources/licenses/csound/NOTICE.txt` |

## Replacing it (LGPL 2.1 §6)

The point of shipping the shared library rather than linking it statically is that
you can put your own build in its place. `Contents/Resources/licenses/csound/NOTICE.txt`
inside every bundle says how, and the signed release carries
`com.apple.security.cs.disable-library-validation` so macOS lets your build load.

## Refreshing it

Install the Csound version you want, build a bundle with it the old way
(`CSOUND_FRAMEWORK_BINARY=… tools/bundle_csound_macos.sh <bundle>`), then copy that
bundle's `Contents/libs` over `macos-arm64/lib` and the framework's `Headers` over
`macos-arm64/include/csound`. Update the table above, `THIRD_PARTY_LICENSES.txt`
and `resources/licenses/csound/NOTICE.txt`, and re-run
`tools/verify_csound_bundle.py` and `tools/verify_lro_in_standalone.py`.
