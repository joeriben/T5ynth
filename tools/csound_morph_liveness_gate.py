"""Does a key that MOVES on its own still move when a morph chain uses it?

The morph path has two routes. Tonal stages are interpolated as static partial
banks (`_emit_morph`); stages that cannot survive that -- noise, modal, voice,
and anything in `_LIVE_TECH` -- are rendered for real and amplitude-crossfaded.
Choosing the first route for a key whose identity is TIME VARIATION silently
replaces a live Csound idiom with a bank of `oscili`, which is both the additive
bank this project forbids and the "everything sounds the same" collapse that
`_LIVE_TECH` was introduced to stop.

`_LIVE_TECH` is hand-maintained, and this gate exists because that failed exactly
as hand-maintained lists do. A session replaced ten table-driven keys with real
idioms -- organ became three gbuzz ranks with independent wander, clarinet a
foscili bore opened by breath pressure, additive and harpsichord detuned twins
whose BEATING is the whole point, cheby a driven waveshaper, ring_mod a genuine
product with a drifting carrier leak -- and did not add one of them to the list.
Standalone they measured 14-27% spectral-centroid travel. Inside `a > b` all six
emitted nothing but `oscili`: every idiom gone, silently, with the suite green.

So the gate does not consult the list. It MEASURES which keys move, and then
requires that a morph preserve the idiom of every key that does:

  1. Render each technique alone and measure spectral-centroid travel.
  2. Any key above LIVE_TRAVEL is live, whatever any set says.
  3. Require every live key to take the CROSSFADE route.

Step 3 evaluates the assembler's own routing predicate rather than sniffing the
emitted orchestra for opcode signatures. Signature-sniffing was tried first and
produced a false negative immediately: `ring_mod`'s identity is a PRODUCT of two
signals, not an opcode, so a version of this gate that looked for characteristic
opcodes passed it while the morph was in fact flattening it to `oscili`. What is
actually being asked is "which of the two routes does this chain take", and that
question has an exact answer in the code.

A key that is genuinely static (saw, square, triangle, pulse, sine) is allowed to
be flattened -- reduced to a static spectrum a saw is still a saw. That is the
real distinction, and measuring it is the only way to keep it honest as keys are
rewritten.

Run: .venv/bin/python tools/csound_morph_liveness_gate.py
Exits non-zero on any live key whose idiom a morph discards.
"""
import importlib.util
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from scipy.io import wavfile

ROOT = Path(__file__).resolve().parent.parent
CS = "/opt/homebrew/bin/csound"
SR = 48000
DUR = 9.0
HZ = 220.0

# Below this, a key is static enough that a partial bank represents it. `sine`
# measures 0.0% and the reworked acoustic keys 14-68%, so the boundary is wide.
LIVE_TRAVEL = 5.0

# Keys that measure live but are deliberately still flattened, each with the
# reason and the number. NOT a way to quiet the gate: an entry here is a claim
# that a static partial bank genuinely represents the key, and the gate prints
# every one of them on each run so the claim stays visible.
FLATTEN_ANYWAY = {
    "square": (
        "9.8% travel, from the comparator threshold drift (kdty 0.012 at "
        "0.057 Hz) that gives the analogue oscillators their life. Unlike the "
        "keys above it loses a SUBTLETY, not its idiom -- reduced to a static "
        "spectrum a square is still a square -- while forcing it onto the "
        "crossfade path would cost genuine spectral interpolation on the "
        "commonest morph there is, `saw > square`. OPEN QUESTION for BJ: "
        "analogue life inside morph chains, or spectral interpolation between "
        "the basic waveforms? Fixing it properly means the morph path carrying "
        "k-rate modulation into the interpolated bank, which is the ftmorf work."
    ),
}

# Opcodes that constitute an oscillator idiom. `oscili` is deliberately absent:
# it is what the additive bank is BUILT from, so its presence is never evidence
# that an idiom survived.
IDIOM_OPCODES = ("foscili", "foscil", "gbuzz", "buzz", "vco2", "tablei", "mode",
                 "dust2", "syncphasor", "streson", "reson", "tone", "atone",
                 "butterlp", "pinkish", "wgclar", "fof")


def _load():
    spec = importlib.util.spec_from_file_location(
        "csound_orch", ROOT / "backend" / "csound_orch.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _playable(orc, dur):
    """Trim the 100-hour score to one held note on voice 1 and open its gate.

    BOTH the `i 1` instances AND the `e 360000` marker have to go: leaving the
    end marker renders 100 hours regardless of the notes, which has already
    filled this machine's disk once.
    """
    orc = orc.replace("%SR%", str(SR)).replace("-n -d", "-d")
    ctrl = ('\ninstr 900\n  chnset 1,"gate1"\n  chnset %s,"freq1"\n'
            '  chnset 0.8,"vel1"\n  chnset 1,"pres1"\n  chnset 0,"timb1"\n'
            '  chnset 1,"trig1"\nendin\n' % HZ)
    orc = orc.replace("</CsInstruments>", ctrl + "</CsInstruments>")
    keep = []
    for ln in orc.splitlines():
        m = re.match(r"^i 1 0 360000 (\d+)\s*$", ln)
        if m:
            if m.group(1) == "1":
                keep.append(f"i 1 0 {dur} 1")
            continue
        keep.append("e" if ln.strip() == "e 360000" else ln)
    return "\n".join(keep).replace("<CsScore>", f"<CsScore>\ni 900 0 {dur} 0")


def render(orc, dur=DUR):
    d = Path(tempfile.mkdtemp())
    try:
        (d / "t.csd").write_text(_playable(orc, dur))
        subprocess.run([CS, "-o", str(d / "t.wav"), "-W", "--nodisplays",
                        str(d / "t.csd")], capture_output=True, text=True,
                       timeout=180)
        if not (d / "t.wav").exists():
            return None, None
        sr, x = wavfile.read(d / "t.wav")
        if x.ndim > 1:
            x = x[:, 0]
        return sr, x.astype(np.float64) / 32768.0
    finally:
        shutil.rmtree(d, ignore_errors=True)


def centroid_travel(sr, x):
    """Peak-to-peak spread of the spectral centroid, as a % of its mean."""
    x = x[int(sr * 0.4):]
    w = int(sr * 0.20)
    cs = []
    for i in range(0, len(x) - w, w):
        seg = x[i:i + w] * np.hanning(w)
        mag = np.abs(np.fft.rfft(seg))
        tot = mag.sum()
        if tot > 1e-9:
            cs.append(float((mag * np.fft.rfftfreq(w, 1.0 / sr)).sum() / tot))
    if len(cs) < 3:
        return 0.0
    cs = np.array(cs)
    return float((cs.max() - cs.min()) / max(cs.mean(), 1e-9) * 100.0)


def idioms(orc):
    body = orc.split("instr 1")[1] if "instr 1" in orc else orc
    return {op for op in IDIOM_OPCODES if f" {op} " in body}


def takes_crossfade(M, key):
    """The assembler's OWN routing predicate for a chain containing `key`.

    Mirrors the condition in _emit_osc_body. Kept as a mirror rather than a call
    because the routing there is entangled with chain assembly; if that condition
    moves, this gate must be updated with it -- which is the point, since the
    condition IS what the gate is testing.
    """
    return (key in getattr(M, "_NOISE_TECH", set())
            or key in getattr(M, "_MODAL_TECH", set())
            or key in getattr(M, "_LIVE_TECH", set())
            or key in getattr(M, "_VOICE_TECH", set())
            or M._MORPH_SPECTRUM.get(key) in M._SUBFUND_SPECTRA)


def main():
    M = _load()
    src = (ROOT / "backend" / "csound_orch.py").read_text()
    techs = sorted(set(re.findall(r'technique == "([a-z_0-9]+)"', src)))
    print(f"{'key':14s} {'travel':>8}  {'live?':6}  idiom in `key > sine`")
    failures = []
    for k in techs:
        orc, _ = M.build_orchestra(oscs=[{"chain": [k], "vol": 1.0}])
        sr, x = render(orc)
        if sr is None:
            print(f"{k:14s} {'RENDER FAILED':>8}")
            failures.append((k, "render failed", set()))
            continue
        travel = centroid_travel(sr, x)
        if travel < LIVE_TRAVEL:
            print(f"{k:14s} {travel:7.1f}%  {'static':6}  (may be flattened)")
            continue
        crossfaded = takes_crossfade(M, k)
        want = idioms(orc)
        morc, _ = M.build_orchestra(oscs=[{"chain": [k, "sine"], "vol": 1.0}])
        lost = want - idioms(morc)
        if crossfaded:
            status = "OK"
        elif k in FLATTEN_ANYWAY:
            status = "flattened, documented exception"
        else:
            status = "FLATTENED to a partial bank"
            if lost:
                status += " (loses " + ", ".join(sorted(lost)) + ")"
        print(f"{k:14s} {travel:7.1f}%  {'LIVE':6}  {status}")
        if not crossfaded and k not in FLATTEN_ANYWAY:
            failures.append((k, "takes the additive path", lost))
    print()
    for k, why in sorted(FLATTEN_ANYWAY.items()):
        print(f"documented exception -- {k}:")
        for ln in __import__("textwrap").wrap(why, 72):
            print(f"    {ln}")
        print()
    if failures:
        print(f"FAIL: {len(failures)} live key(s) flattened by the morph path")
        for k, why, lost in failures:
            print(f"  {k}: {why}"
                  + (f" ({', '.join(sorted(lost))})" if lost else ""))
        print("\nA live key must take the crossfade path -- add it to _LIVE_TECH")
        print("in backend/csound_orch.py, or explain in code why a static")
        print("partial bank genuinely represents it.")
        return 1
    print("OK: every key that moves keeps its idiom inside a morph chain")
    return 0


if __name__ == "__main__":
    sys.exit(main())
