#!/usr/bin/env python3
"""Does a built bundle carry a WORKING Csound of its own?

The whole point of bundling Csound is the machine that does NOT have Csound
installed, and on the machine that does, a broken bundle is indistinguishable
from a good one: dyld silently falls through to /opt/homebrew, the library loads,
the orchestra compiles, the sound plays. A green run here would then certify
nothing — which is exactly the trap this file exists to avoid.

So the runtime half runs under `sandbox-exec` with every Homebrew Csound path
denied, and it FIRST proves that the denial actually bites. Only then does it
load the bundle's own CsoundLib64, and only then does it compile and PLAY a real
LRO orchestra in the host's own scaffold (backend/lco_write.wrap) and require
non-silent samples.

    tools/verify_csound_bundle.py build_clean/T5ynth_artefacts/Release/Standalone/akroasys.app

What it does not do: launch the Standalone. That test is BJ's fourth requirement
and needs a person or a driven GUI; this file is the part that can run on every
build, and it covers everything between the bundle's bytes and audible samples.
"""
import ctypes
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "backend"))

# Everything a Homebrew Csound could be reached through. The Cellar is the one
# that matters most: the library's baked-in opcode path points straight into it.
HOMEBREW_CSOUND = (
    "/opt/homebrew/Cellar/csound",
    "/opt/homebrew/opt/csound",
    "/opt/homebrew/bin/csound",
    "/opt/homebrew/lib/libcsnd6.6.0.dylib",
    "/usr/local/opt/csound",
    "/Library/Frameworks/CsoundLib64.framework",
)

# A quarter second at 44.1 kHz, like the authoring gate's own performance check.
PERF_SECONDS = 0.25
SILENCE_FLOOR = 1e-4      # a peak below this is silence, not a quiet sound


def fail(msg):
    print(f"FAIL  {msg}")
    sys.exit(1)


def ok(msg):
    print(f"ok    {msg}")


# ── static half: does the bundle name anything outside itself ────────────────

def mach_o_files(bundle):
    out = subprocess.run(["find", str(bundle), "-type", "f", "-perm", "-u+x"],
                         capture_output=True, text=True).stdout.split()
    for path in out:
        kind = subprocess.run(["file", "-b", path], capture_output=True,
                              text=True).stdout
        if "Mach-O" in kind:
            yield path


def foreign_references(path):
    load = subprocess.run(["otool", "-L", path], capture_output=True, text=True).stdout
    rpaths = subprocess.run(["otool", "-l", path], capture_output=True, text=True).stdout
    refs = [l.split()[0] for l in load.splitlines()[1:] if l.startswith("\t")]
    want_path = False
    for line in rpaths.splitlines():
        if "cmd LC_RPATH" in line:
            want_path = True
        elif want_path and line.strip().startswith("path "):
            refs.append(line.split()[1])
            want_path = False
    return [r for r in refs
            if r.startswith(("/opt/homebrew", "/usr/local")) or "/Cellar/" in r]


def check_bundle_contents(bundle):
    lib = bundle / "Contents" / "libs" / "CsoundLib64"
    opcodes = bundle / "Contents" / "libs" / "Opcodes64"
    licences = bundle / "Contents" / "Resources" / "licenses" / "csound"

    if not lib.is_file():
        fail(f"no bundled Csound library at {lib}")
    if not opcodes.is_dir() or not any(opcodes.glob("*.dylib")):
        fail(f"no bundled plugin opcodes at {opcodes}")
    if not (opcodes / "libscansyn.dylib").is_file():
        fail("libscansyn.dylib missing — scanu/scanu2/scans would be gone")
    for name in ("NOTICE.txt", "LGPL-2.1.txt"):
        if not (licences / name).is_file():
            fail(f"LGPL 2.1 requires {name} in the bundle ({licences})")
    ok(f"{lib.name} + {len(list(opcodes.glob('*.dylib')))} plugin opcode modules "
       f"+ licence texts present")

    offenders = []
    for m in mach_o_files(bundle):
        for ref in foreign_references(m):
            offenders.append(f"{Path(m).relative_to(bundle)}: {ref}")
    if offenders:
        fail("bundle reaches outside itself:\n      " + "\n      ".join(offenders))
    ok("no Mach-O in the bundle names /opt/homebrew, /usr/local or a Cellar path")
    return lib, opcodes


# ── runtime half, inside the sandbox ────────────────────────────────────────

def prove_homebrew_is_unreachable():
    """If the sandbox did not actually cut Csound off, every result after this is
    worthless — the bundle could be reaching straight back into Homebrew."""
    reachable = [p for p in HOMEBREW_CSOUND if os.path.exists(p) and _readable(p)]
    if reachable:
        fail("the sandbox did not deny Homebrew's Csound — still readable: "
             + ", ".join(reachable) + "\n      "
             "(this test proves nothing while that is true)")
    ok("Homebrew's Csound is unreachable in this process")


def _readable(path):
    try:
        if os.path.isdir(path):
            os.listdir(path)
        else:
            with open(path, "rb") as fh:
                fh.read(4)
        return True
    except OSError:
        return False


def load_bundled(lib_path, opcodes):
    try:
        lib = ctypes.CDLL(str(lib_path))
    except OSError as exc:
        fail(f"the bundled library will not load: {exc}")
    for name, restype, argtypes in (
        ("csoundCreate", ctypes.c_void_p, [ctypes.c_void_p]),
        ("csoundSetOpcodedir", None, [ctypes.c_char_p]),
        ("csoundSetOption", ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p]),
        ("csoundCompileCsdText", ctypes.c_int, [ctypes.c_void_p, ctypes.c_char_p]),
        ("csoundStart", ctypes.c_int, [ctypes.c_void_p]),
        ("csoundPerformKsmps", ctypes.c_int, [ctypes.c_void_p]),
        ("csoundSetControlChannel", None,
         [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_double]),
        ("csoundGetSpout", ctypes.POINTER(ctypes.c_double), [ctypes.c_void_p]),
        ("csoundGetKsmps", ctypes.c_uint32, [ctypes.c_void_p]),
        ("csoundGetNchnls", ctypes.c_uint32, [ctypes.c_void_p]),
        ("csoundGetSr", ctypes.c_double, [ctypes.c_void_p]),
        ("csoundNewOpcodeList", ctypes.c_int, [ctypes.c_void_p, ctypes.c_void_p]),
        ("csoundDestroy", None, [ctypes.c_void_p]),
    ):
        fn = getattr(lib, name)
        fn.restype = restype
        fn.argtypes = argtypes
    lib.csoundSetOpcodedir(str(opcodes).encode())
    ok(f"loaded {lib_path.name} with its own Opcodes64")
    return lib


def check_plugin_opcodes(lib):
    """The measured trap: a bundle without Opcodes64 still compiles core opcodes
    and silently loses 565 entries. `fractalnoise` is one of them, so it answers
    the question with a compile rather than with a count that could drift."""
    cs = lib.csoundCreate(None)
    try:
        for opt in (b"-n", b"-d", b"-m0"):
            lib.csoundSetOption(cs, opt)
        entries = ctypes.c_void_p()
        count = lib.csoundNewOpcodeList(cs, ctypes.byref(entries))
        csd = ("<CsoundSynthesizer>\n<CsInstruments>\nsr=44100\nksmps=64\nnchnls=1\n"
               "0dbfs=1\ninstr 1\nasig fractalnoise 0.2, 2\nout asig\nendin\n"
               "</CsInstruments>\n<CsScore>\ne 0\n</CsScore></CsoundSynthesizer>\n")
        if lib.csoundCompileCsdText(cs, csd.encode()) != 0:
            fail("the bundled Csound has no plugin opcodes (fractalnoise did not "
                 "compile) — scanu/scans, tvconv, ftgenonce, limit1 and GEN "
                 "padsynth are gone with it")
        ok(f"plugin opcodes register ({count} opcode entries; fractalnoise compiles)")
    finally:
        lib.csoundDestroy(cs)


def play_real_orchestra(lib):
    """Compile and PLAY the host's own scaffold around a real library body, with
    the voice channels set to a played note — the same thing the engine does."""
    import json
    import lco_write

    library = json.loads((REPO / "backend" / "lco_library.json").read_text())
    # The library's own instruments, as the author reads them: a list of
    # {key, why, code}. Prefer a physical model over a plain waveform — a `mode`
    # bank exercises far more of the library than a single vco2 line would.
    entries = {e["key"]: e for e in library["instruments"] if e.get("code")}
    body = name = None
    for key in ("struck_bar", "bell", "string", "additive", "saw"):
        if key in entries:
            body, name = entries[key]["code"], key
            break
    if body is None and entries:
        name, entry = next(iter(entries.items()))
        body = entry["code"]
    if body is None:
        fail("no Csound body found in backend/lco_library.json to play")

    csd = lco_write.wrap(body).replace("%SR%", "44100")
    cs = lib.csoundCreate(None)
    try:
        for opt in (b"-n", b"-d", b"-m0"):
            lib.csoundSetOption(cs, opt)
        if lib.csoundCompileCsdText(cs, csd.encode()) != 0:
            fail(f"the library idiom '{name}' did not compile in the bundled Csound")
        if lib.csoundStart(cs) != 0:
            fail(f"csoundStart failed for '{name}'")
        for chan, value in (("freq", 220.0), ("gate", 1.0), ("vel", 0.8),
                            ("trig", 1.0)):
            lib.csoundSetControlChannel(cs, f"{chan}1".encode(), value)

        ksmps = lib.csoundGetKsmps(cs)
        nchnls = lib.csoundGetNchnls(cs)
        sr = lib.csoundGetSr(cs)
        spout = lib.csoundGetSpout(cs)
        peak = 0.0
        energy = 0.0
        frames = 0
        while frames < int(PERF_SECONDS * sr):
            if lib.csoundPerformKsmps(cs) != 0:
                break
            for i in range(ksmps):                    # channel 1 == voice 1
                s = spout[i * nchnls]
                peak = max(peak, abs(s))
                energy += s * s
            frames += ksmps
        if frames == 0:
            fail(f"'{name}' rendered nothing at all")
        rms = (energy / frames) ** 0.5
        if peak < SILENCE_FLOOR:
            fail(f"'{name}' played SILENCE (peak {peak:.2e}) — the bundled Csound "
                 "compiles but does not sound")
        ok(f"played '{name}' for {frames / sr:.2f} s: peak {peak:.4f}, "
           f"rms {rms:.4f} — audible")
    finally:
        lib.csoundDestroy(cs)


# ── driver ──────────────────────────────────────────────────────────────────

SANDBOX_PROFILE = """(version 1)
(allow default)
{denies}
"""


def run_child_under_sandbox(bundle):
    denies = "\n".join(
        f'(deny file-read* (subpath "{p}"))' if os.path.isdir(p)
        else f'(deny file-read* (literal "{p}"))'
        for p in HOMEBREW_CSOUND)
    profile = Path(os.environ.get("TMPDIR", "/tmp")) / "deny_homebrew_csound.sb"
    profile.write_text(SANDBOX_PROFILE.format(denies=denies))
    print(f"      denying Homebrew Csound via {profile}")
    return subprocess.run(["/usr/bin/sandbox-exec", "-f", str(profile),
                           sys.executable, str(Path(__file__).resolve()),
                           str(bundle), "--child"]).returncode


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        print(__doc__)
        return 2
    bundle = Path(args[0]).resolve()
    if not bundle.is_dir():
        fail(f"not a bundle: {bundle}")

    child = "--child" in sys.argv
    if not child:
        print(f"verifying {bundle}")
        check_bundle_contents(bundle)
        rc = run_child_under_sandbox(bundle)
        if rc == 0:
            print("PASS  the bundle plays the LRO without any Csound on the system")
        return rc

    prove_homebrew_is_unreachable()
    lib = load_bundled(bundle / "Contents" / "libs" / "CsoundLib64",
                       bundle / "Contents" / "libs" / "Opcodes64")
    check_plugin_opcodes(lib)
    play_real_orchestra(lib)
    return 0


if __name__ == "__main__":
    sys.exit(main())
