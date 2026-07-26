# Csound in the app

The LRO **is** Csound, so Csound ships inside the app. Nobody installs anything.

## The four rules this had to satisfy

1. **Csound is never required to build.** Delete `third_party/csound/` and everything
   builds exactly as before: `T5YNTH_CSOUND_FOUND` goes false, `CsoundEngine.cpp` is
   not compiled, and the always-defined `T5YNTH_HAS_CSOUND` lets each call site
   branch. CI runners and dev machines without Csound are unaffected.
2. **Dynamic, never static.** LGPL 2.1: the shipped library is replaceable, and
   `resources/licenses/csound/NOTICE.txt` travels with it and says how.
3. **Re-signed after copying**, for Standalone, VST3 and AU.
4. **The acceptance test is the one that can fail:** make the machine's own Csound
   unreachable, then produce an LRO sound in the *built* Standalone.

## What ships, and where it comes from

`third_party/csound/` holds the exact bytes — `include/csound/` shared by both
platforms, `macos-arm64/lib/` a drop-in `Contents/libs`, `windows-x64/{lib,bin}/`
the import library and the runtime files. Provenance, the plugin-module selection
and the refresh recipe are in that directory's README.

| | macOS | Windows |
|---|---|---|
| library | `Contents/libs/CsoundLib64` | `csound64.dll` beside the module |
| plugin opcodes | `Contents/libs/Opcodes64/` (24) | `plugins64/` (16) |
| how it is found | load command `@loader_path/../libs/…`, recorded at link time | delay-load, then `LoadLibraryA` by absolute path at first use |
| bundling step | `tools/bundle_csound_macos.sh` (POST_BUILD) | CMake `copy_directory` (POST_BUILD) |

## The two things that are not obvious

**Windows resolves imports against the host process, not against the module.** A
VST3 is a DLL the DAW loads by full path, and that path is not added to the import
search. A normal import of `csound64.dll` would therefore be looked for beside the
DAW's executable — so the standalone would work and the plugin would not load in
most hosts. Hence `/DELAYLOAD:csound64.dll`: the decision moves to first use, where
`CsoundEngine.cpp` loads the DLL from its own module's directory by absolute path
(`GetModuleHandleEx` → `GetModuleFileName` → `LoadLibraryA`). The delay-load helper
then finds a module of that base name already loaded and uses it.

**The opcode directory is baked in at build time.** A copied library keeps reaching
into the tree it was built in, which exists on the build machine and nowhere else —
measured: without an override the bundle silently drops from 2267 to 1917 opcodes,
losing `scanu`/`scans`, `fractalnoise`, GEN `padsynth` and more. The author WRITES
Csound, so a missing opcode is a capability it cannot reach. `csoundLibraryReady()`
therefore points Csound at the plugin directory *we shipped*, and only if it is
really there — a plain system install has no such directory, so its own resolution
is left alone.

Which directory that is differs by platform, and the reason is worth knowing.
dyld keys images by resolved path, so each macOS bundle's `CsoundLib64` is its own
image with its own copy of Csound's process-global opcode-dir setting; anchoring on
the loaded library is exactly right there. The Windows loader keys modules by **base
name**: if another Csound-based plugin (Cabbage, CsoundVST) is already loaded in the
same DAW, `LoadLibrary` on our own `csound64.dll` hands back *their* image — there is
no second image to be had. Anchoring on the loaded library would then point at their
installation and ours would never be used. So on Windows the anchor is our own
module's directory. The shared-image case remains: both plugins then share one
opcode-dir global, and we set it to our own stock 6.18.1 modules rather than lose
them. Shipping the DLL under a unique base name plus a delay-load hook would remove
the sharing entirely; that is an open decision, not something this change does.

## Failsafe

`csoundLibraryReady()` is the single gate. On Windows it loads the DLL and returns
false if it cannot; both `prepare()` and `renderBareOscillator()` return
failure/empty before touching a Csound symbol. No library means a silent LRO — the
same outcome as a build without Csound — never a crash.

It loads from our own module's directory by absolute path and from nowhere else.
There is deliberately no fall-back to a bare `LoadLibrary("csound64.dll")`: the
default search order includes the current directory, which would let a file of that
name in whatever folder the host was pointed at into the process. We always ship the
library ourselves, so its absence means a broken installation, not a machine to go
looking on.

Every Windows path call is wide (`GetModuleFileNameW`, `LoadLibraryW`), with a
growing buffer rather than `MAX_PATH`. The Standalone is unzipped wherever the user
likes, and the ANSI variants return a path with `?` substituted for anything outside
the machine's code page — a Cyrillic or CJK user name would leave the LRO silent for
good. `csoundSetOpcodedir` only takes `const char*`, so the opcode path is converted
to the code page and falls back to the 8.3 short name where it cannot be; if neither
works, that is logged rather than silently costing 350 opcodes.

**First Windows build: check the link log for `LNK4199`.** The vendored `csound.h`
declares the API `__declspec(dllexport)` for consumers too (an upstream quirk), so
MSVC will emit a spurious export table. If it instead decides the symbols are locally
defined it drops the delay-load directive with that warning — which the PE check
below then catches, loudly.

## What proves it

- `tools/verify_csound_bundle.py <bundle-or-dir>` — runs on every CI build, all
  bundles. macOS: under `sandbox-exec` with every Homebrew Csound path denied,
  proving the denial bites first. Windows: loads by absolute path and reads back
  `GetModuleFileNameW` to prove it is the shipped file, and reads the module's PE
  to prove `csound64.dll` is delay-imported and not normally imported. Both then
  compile and PLAY a real library orchestra and require non-silent samples.
- `tools/verify_lro_in_standalone.py` — requirement 4, macOS: launches the built
  Standalone with a throwaway settings home and a preset that selects the LRO,
  Homebrew Csound denied, records the audio and checks the pitch. `--prove-it-can-fail`
  shows the harness can fail.
