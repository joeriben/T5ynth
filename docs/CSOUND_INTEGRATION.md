# Csound in the app

The LRO **is** Csound, so Csound ships inside the app. Nobody installs anything.

## The four rules this had to satisfy

1. **Csound is never required to build.** Delete `third_party/csound/` and everything
   builds exactly as before: `T5YNTH_CSOUND_FOUND` goes false, `CsoundEngine.cpp` is
   not compiled, and the always-defined `T5YNTH_HAS_CSOUND` lets each call site
   branch. CI runners and dev machines without Csound are unaffected.
2. **Dynamic, never static.** LGPL 2.1: the shipped library is replaceable, and
   `resources/licenses/csound/NOTICE.txt` travels with it and says how.
3. **Re-signed after copying**, for Standalone, VST3, AU and CLAP.
4. **The acceptance test is the one that can fail:** make the machine's own Csound
   unreachable, then produce an LRO sound in the *built* Standalone.

## What ships, and where it comes from

`third_party/csound/` holds the exact bytes for the two platforms that need them —
`include/csound/` shared, `macos-arm64/lib/` a drop-in `Contents/libs`,
`windows-x64/{lib,bin}/` the import library and the runtime files. Provenance, the
plugin-module selection and the refresh recipe are in that directory's README.

**Linux does not vendor, and that is not an inconsistency.** The app itself is
compiled on the machine that packages it, so a Csound from that machine carries
exactly the glibc floor akróasys already carries, while a checked-in `.so` would be
the one binary in the tree nobody could regenerate. Ubuntu 24.04 ships the same
6.18.1 from the distribution's own archive: a first-party, licence-audited source,
which is what makes it a different question from `brew install` on a user's Mac.

| | macOS | Windows | Linux |
|---|---|---|---|
| library | `Contents/libs/CsoundLib64` | `csound64.dll` beside the module | `libs/libcsound64.so.6.0`, or the distro's for the `.deb` |
| plugin opcodes | `Contents/libs/Opcodes64/` (24) | `plugins64/` (16) | `libs/Opcodes64/`, or Csound's own for the `.deb` |
| how it is found | load command `@loader_path/../libs/…`, recorded at link time | delay-load, then `LoadLibraryW` by absolute path at first use | SONAME + `$ORIGIN/libs` rpath |
| where it comes from | vendored | vendored | `apt install libcsound64-dev` on the runner |
| bundling step | `tools/bundle_csound_macos.sh` | CMake `copy_directory` | `tools/bundle_csound_linux.sh` (patchelf) |

One binary serves both Linux distributions. It links Csound by SONAME, so the
tarball resolves it through the `$ORIGIN/libs` rpath, and the `.deb` — which ships
the bare executable and declares `libcsound64-6.0` — resolves it in `/usr/lib`,
because an rpath that finds nothing simply falls through. The opcode directory
follows the same rule everywhere: a sibling `Opcodes64` next to the library actually
loaded. The bundled copy has one; `/usr/lib` does not, so there Csound's own baked-in
plugin path is left alone, which is the right answer for a distro install.

**What Linux costs: `scansyn`.** Debian and Ubuntu ship no scanned synthesis at all —
not in `libcsound64-6.0`, and not in the separate `csound-plugins` package either.
So `scanu`, `scanu2` and `scans` are reachable on macOS and Windows and not on Linux.
Nothing in `backend/lco_library.json` uses them, and the verifier probes each module
exactly where it exists, so a platform that has it can never lose it quietly.

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

## The backend needs the same Csound, and for a long time it did not have it

The engine PLAYS the orchestra; the Python backend GATES it before it ever gets
there — `backend/lco_write.py` compiles what the author wrote, plays a fraction of
a second of it, and turns each of its knobs to see whether the sound moves. Those
gates have to hold the same compiler the engine holds, or they judge a different
instrument than the one that sounds. Being Python, the backend reaches it through
`ctypes` on the very library the app ships.

Compiling is enough for the syntax gate and it happens in-process. **Playing is
not**: it means `csoundStart`, and an authored endless loop or abort inside the
backend would take a multi-GB model down with it. So the perform check and the
knob gate run in a child process — and until 2026-08-04 the only child they could
use was an installed `csound` CLI. **No bundle contains one.** On every machine
without Homebrew Csound both checks therefore returned "unchecked, fine": a body
that compiled and made no sound went to the engine as a finished instrument, and
nothing anywhere had listened. The engine played it perfectly. That is the
failure this section exists to prevent repeating — a gate whose absence looks
exactly like a pass.

The app is now its own missing binary. `pipe_inference.py` dispatches
`--lro-render` before it imports torch; `lco_write.render_child_main` loads the
shipped library and performs the CSD, forwarding argv straight to
`csoundSetOption`. Process boundary kept, nothing installed, one compiler.

Where the backend looks for the library it was given (`_bundled_csound_libs`):

- **macOS** — an ancestor directory named `Contents`, then `Contents/libs/CsoundLib64`,
  with `Opcodes64` beside it. This is the layout `tools/bundle_csound_macos.sh` writes.

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
  bundles, all three platforms. macOS: under `sandbox-exec` with every Homebrew
  Csound path denied, proving the denial bites first. Windows: loads by absolute
  path and reads back `GetModuleFileNameW` to prove it is the shipped file, and
  reads the module's PE to prove `csound64.dll` is delay-imported and not normally
  imported. Linux: reads `/proc/self/maps` back for the same statement — which
  matters most there, because the build machine is exactly the machine that has
  Csound in `/usr/lib` — plus `ldd` over every bundled ELF. All three then compile
  and PLAY a real library orchestra and require non-silent samples.
- The backend's own reach, macOS, measured 2026-08-04 under the same denial: with
  `/opt/homebrew`, `/usr/local` and `/Library/Frameworks/CsoundLib64.framework`
  unreadable and only the bundle's `CsoundLib64` named, a body that plays passes
  the perform check, `vco2 kamp, kcps, 1` is caught with Csound's own diagnostic, a
  knob that changes no sample is withheld and a working one survives. The shipped
  binary's `--lro-render` exits 0 on the first and 1 on the second. This has no
  tool of its own yet; `tools/verify_csound_bundle.py` proves the ENGINE's reach,
  not the gates'.
- `tools/verify_lro_in_standalone.py` — requirement 4, macOS: launches the built
  Standalone with a throwaway settings home and a preset that selects the LRO,
  Homebrew Csound denied, records the audio and checks the pitch. `--prove-it-can-fail`
  shows the harness can fail.
