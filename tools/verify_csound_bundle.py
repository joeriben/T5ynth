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

# What the child may not read. Not a list of Csound paths — the WHOLE of both
# Homebrew prefixes, plus the system-wide framework location. A named-paths list
# only denies the ways I thought of; the bundle's dependency tree is libsndfile,
# ogg, vorbis, FLAC, opus, mpg123, lame and gettext as well, and any one of them
# resolving back to Homebrew would make a green run mean nothing. This is
# affordable because the child is /usr/bin/python3, which links nothing outside
# /usr/lib and /System (measured 2026-07-26).
DENIED_PREFIXES = (
    "/opt/homebrew",
    "/usr/local",
    "/Library/Frameworks/CsoundLib64.framework",
)

# The paths that must be gone for the test to mean anything, checked by name so
# the failure says which one survived. The Cellar matters most: the library's
# baked-in opcode path points straight into it.
HOMEBREW_CSOUND = (
    "/opt/homebrew/Cellar/csound",
    "/opt/homebrew/opt/csound",
    "/opt/homebrew/bin/csound",
    "/opt/homebrew/lib/libcsnd6.6.0.dylib",
    "/opt/homebrew/Frameworks/CsoundLib64.framework",
    "/opt/homebrew/lib/libsndfile.1.dylib",
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

def mach_o_files(*roots):
    """Every Mach-O under the given paths. Not `find -perm -u+x`: dylibbundler
    writes its copies 0644, and that filter hid 9 of the 10 bundled libraries from
    this check. os.walk rather than parsing find's output, because a path with a
    space in it silently split into two nonexistent ones."""
    for root in roots:
        root = Path(root)
        paths = [root] if root.is_file() else \
                [Path(d) / f for d, _, fs in os.walk(root) for f in fs]
        for path in paths:
            if path.is_symlink() or not path.is_file():
                continue
            kind = subprocess.run(["file", "-b", str(path)],
                                  capture_output=True, text=True).stdout
            if "Mach-O" in kind:
                yield path


def foreign_references(path):
    """Everything the file names that is not inside the bundle (@loader_path,
    @rpath, @executable_path) and not the OS's own (/usr/lib, /System). A
    whitelist, deliberately: the old blacklist of /opt/homebrew, /usr/local and
    /Cellar/ passes anything a differently-configured build machine records —
    /Users/…, /sw, a Nix store path — which is the same defect wearing a
    different prefix.

    /System/Volumes is carved back out of the /System exemption: on APFS the data
    volume is also reachable as /System/Volumes/Data/opt/homebrew/…, so a whole
    Homebrew tree can be spelled as if it were the OS's own."""
    load = subprocess.run(["otool", "-L", str(path)],
                          capture_output=True, text=True).stdout
    refs = [_first_field(l) for l in load.splitlines()[1:] if l.startswith("\t")]
    refs += rpaths_of(path)
    return [r for r in refs
            if not r.startswith(("@", "/usr/lib/", "/System/"))
            or r.startswith("/System/Volumes/")]


def _first_field(line):
    """otool prints `\\t<path> (compatibility version …)`. Splitting on whitespace
    truncates a path that contains a space, which then gets reported under a name
    that does not exist."""
    return line.strip().rsplit(" (", 1)[0].strip()


def rpaths_of(path):
    out = subprocess.run(["otool", "-l", str(path)],
                         capture_output=True, text=True).stdout
    found, want_path = [], False
    for line in out.splitlines():
        if "cmd LC_RPATH" in line:
            want_path = True
        elif want_path and line.strip().startswith("path "):
            found.append(line.strip()[5:].rsplit(" (offset ", 1)[0].strip())
            want_path = False
    return found


def check_bundle_contents(bundle):
    lib = bundle / "Contents" / "libs" / "CsoundLib64"
    opcodes = bundle / "Contents" / "libs" / "Opcodes64"
    licences = bundle / "Contents" / "Resources" / "licenses" / "csound"

    if not lib.is_file() or lib.is_symlink():
        fail(f"no bundled Csound library at {lib}"
             + (" (it is a symlink)" if lib.is_symlink() else ""))
    if not opcodes.is_dir() or not any(opcodes.glob("*.dylib")):
        fail(f"no bundled plugin opcodes at {opcodes}")
    # A symlink anywhere under Contents/libs is invisible to every check below:
    # os.walk does not descend a symlinked directory and this file skips symlinked
    # files, so a Contents/libs that LOOKS complete can be links to files that
    # exist on this machine only. Measured: with Opcodes64 a symlink, the sweep
    # dropped from 35 Mach-O files to 11 and still said PASS.
    links = sorted({p for p in [opcodes] + [Path(d) / n
                                            for d, ds, fs in os.walk(bundle / "Contents" / "libs")
                                            for n in ds + fs]
                    if p.is_symlink()})
    if links:
        fail("symlink(s) under Contents/libs, which every check below walks past: "
             + ", ".join(str(p.relative_to(bundle)) for p in links))
    if not (opcodes / "libscansyn.dylib").is_file():
        fail("libscansyn.dylib missing — scanu/scanu2/scans would be gone")
    for name in ("NOTICE.txt", "LGPL-2.1.txt"):
        if not (licences / name).is_file():
            fail(f"LGPL 2.1 requires {name} in the bundle ({licences})")
    ok(f"{lib.name} + {len(list(opcodes.glob('*.dylib')))} plugin opcode modules "
       f"+ licence texts present")

    # Scope: what the bundling step is responsible for — Contents/libs and the
    # bundle's own binary. Not the whole bundle: a release .app also carries the
    # PyInstaller backend, hundreds of Mach-Os that are another concern's
    # business, and a check that pulls them in would grow exceptions until it
    # stops meaning anything.
    checked = 0
    problems = []
    for m in mach_o_files(bundle / "Contents" / "libs", bundle / "Contents" / "MacOS"):
        checked += 1
        rel = m.relative_to(bundle)
        # `codesign --verify --deep --strict` on the bundle is NOT enough, and
        # this is measured: it returns 0 with an unsigned dylib in Contents/libs,
        # because that directory is sealed as resources — bytes checked, nested
        # signatures not. An install_name_tool'd library whose signature was not
        # renewed does not merely fail to load, it KILLS the process.
        #
        # The bundle's own executable is the exception: codesign --verify on it
        # verifies the whole bundle seal, so anything added to Contents/Resources
        # after signing — which is exactly what CI does with the PyInstaller
        # backend (.github/workflows/build.yml) — reports here as "the executable
        # has a broken signature". Run this AFTER the bundle is re-sealed; the
        # message says so rather than sending someone after the wrong file.
        if subprocess.run(["codesign", "--verify", str(m)],
                          capture_output=True).returncode != 0:
            problems.append(
                f"{rel}: unsigned or broken signature"
                + (" — note that verifying the bundle's executable verifies the "
                   "whole bundle seal, so this also fires when something was "
                   "copied into the bundle after signing; re-sign, then re-run"
                   if m.parent.name == "MacOS" else ""))
        for ref in foreign_references(m):
            problems.append(f"{rel}: {ref}")
        # dyld aborts at launch on a duplicate LC_RPATH — before main(), with a
        # crash report that names no file this sweep would otherwise flag.
        seen = rpaths_of(m)
        for dup in {r for r in seen if seen.count(r) > 1}:
            problems.append(f"{rel}: LC_RPATH {dup} appears {seen.count(dup)} "
                            "times — dyld kills the process at launch")
    if problems:
        fail("bundle is not self-contained:\n      " + "\n      ".join(problems))
    ok(f"{checked} Mach-O files: each signed, no duplicate rpath, none naming "
       f"anything outside the bundle but /usr/lib and /System")
    return lib, opcodes


# ── runtime half, inside the sandbox ────────────────────────────────────────

def prove_homebrew_is_unreachable():
    """If the sandbox did not actually cut Csound off, every result after this is
    worthless — the bundle could be reaching straight back into Homebrew."""
    reachable = [p for p in HOMEBREW_CSOUND + DENIED_PREFIXES
                 if os.path.exists(p) and _readable(p)]
    if reachable:
        fail("the sandbox did not deny Homebrew — still readable: "
             + ", ".join(reachable) + "\n      "
             "(this test proves nothing while that is true)")
    ok("neither Homebrew prefix is readable in this process")


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


# One opcode per bundled module that matters, written the way an orchestra would
# write it. A module that did not load takes its opcodes with it, and Csound says
# so only when something tries to compile them — which is why this is a compile
# per module rather than a count. The presence of a FILE named libscansyn.dylib
# proves nothing: with a different Mach-O under that name the file check passed
# while `scanu` was gone.
MODULE_PROBES = (
    ("libfractalnoise", "asig fractalnoise 0.2, 2\nout asig"),
    ("libscansyn",      "asig scans 0.5, 220, 1, 1\nout asig"),
    ("libmixer",        "asig oscili 0.2, 220\nMixerSend asig, 1, 1, 0.5\nout asig"),
    ("libdoppler",      "asig oscili 0.2, 220\nadop doppler asig, 1, 2\nout adop"),
    ("liburandom",      "ax urandom\nout ax * 0.1"),
    ("libpadsynth",     'gitab ftgen 0, 0, 262144, "padsynth", 261.6, 10, 1, 1, 1\n'
                        "asig oscili 0.2, 220, gitab\nout asig"),
    ("libpvsops",       "asig oscili 0.2, 220\nfsig pvsanal asig, 1024, 256, 1024, 1\n"
                        "ftr pvstrace fsig, 20\naout pvsynth ftr\nout aout"),
    ("libtrigenvsegs",  "ktrig metro 2\n"
                        "kenv trigexpseg ktrig, 0.001, 0.1, 1, 0.2, 0.001\n"
                        "asig oscili 0.2 * kenv, 220\nout asig"),
)

# 1917 entries register with no plugin modules at all, 2267 with the 24 this
# bundle carries. A floor rather than the exact figure, so a Csound update does
# not fail a build over a few new entries — but far enough above 1917 that a
# bundle which lost its modules cannot pass. Measured: with 22 of the 24 modules
# deleted, the old check still said PASS at 1929 entries.
MIN_OPCODE_ENTRIES = 2200


def check_plugin_opcodes(lib):
    """The measured trap: a bundle without Opcodes64 still compiles core opcodes
    and silently drops to the 1917 built-in entries, 350 fewer than with the
    modules we ship — no scanu/scans, no fractalnoise, no GEN padsynth."""
    cs = lib.csoundCreate(None)
    try:
        for opt in (b"-n", b"-d", b"-m0"):
            lib.csoundSetOption(cs, opt)
        entries = ctypes.c_void_p()
        count = lib.csoundNewOpcodeList(cs, ctypes.byref(entries))
    finally:
        lib.csoundDestroy(cs)
    if count < MIN_OPCODE_ENTRIES:
        fail(f"only {count} opcode entries register — 2267 with the bundled plugin "
             "modules, 1917 with none of them, so modules are missing or did not "
             "load")

    missing = []
    for module, body in MODULE_PROBES:
        cs = lib.csoundCreate(None)
        try:
            for opt in (b"-n", b"-d", b"-m0"):
                lib.csoundSetOption(cs, opt)
            csd = ("<CsoundSynthesizer>\n<CsInstruments>\nsr=44100\nksmps=64\n"
                   f"nchnls=1\n0dbfs=1\ninstr 1\n{body}\nendin\n</CsInstruments>\n"
                   "<CsScore>\ne 0\n</CsScore></CsoundSynthesizer>\n")
            if lib.csoundCompileCsdText(cs, csd.encode()) != 0:
                missing.append(module)
        finally:
            lib.csoundDestroy(cs)
    if missing:
        fail("plugin modules that did not deliver their opcodes: "
             + ", ".join(missing)
             + "\n      (the author WRITES Csound — an opcode that is not there "
               "is a capability it cannot reach)")
    ok(f"{count} opcode entries, and an opcode out of each of "
       f"{len(MODULE_PROBES)} bundled modules compiles")


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


SYSTEM_PYTHON = "/usr/bin/python3"


def run_child_under_sandbox(bundle):
    denies = "\n".join(f'(deny file-read* (subpath "{p}"))' for p in DENIED_PREFIXES)
    profile = Path(os.environ.get("TMPDIR", "/tmp")) / "deny_homebrew_csound.sb"
    profile.write_text(SANDBOX_PROFILE.format(denies=denies))
    # The child is the SYSTEM python, not ours: this interpreter comes from the
    # .venv and links a Homebrew libpython, so it could not even start inside a
    # sandbox that denies the whole prefix — and a sandbox narrow enough for it to
    # start is a sandbox the bundle's dependency tree can slip through. Measured:
    # /usr/bin/python3 names nothing outside /usr/lib and /System, and the parts
    # of backend/lco_write.py this test uses are standard library only.
    if not os.path.exists(SYSTEM_PYTHON):
        fail(f"{SYSTEM_PYTHON} is missing — the check needs an interpreter that "
             "does not itself depend on Homebrew")
    print(f"      denying {', '.join(DENIED_PREFIXES)} via {profile}")
    return subprocess.run(["/usr/bin/sandbox-exec", "-f", str(profile),
                           SYSTEM_PYTHON, str(Path(__file__).resolve()),
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
