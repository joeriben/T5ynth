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

  1. Render every catalogue key alone and measure spectral-centroid travel.
  2. Any key above LIVE_TRAVEL is live, whatever any set says.
  3. Require every live key to take the CROSSFADE route.

Step 1 takes its key list from the catalogue, not from a regex over the source.
The first version scraped `technique == "..."` and so tested 30 of 39 keys,
missing the three dispatched as `technique in (...)` -- which were the only
untested keys routing to the partial-bank path. A gate that claims nothing has
to be remembered must not itself depend on being remembered.

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

# Liveness is measured ACROSS the register, not at one comfortable pitch.
# Every serious defect in this subsystem has been at the ends: a bow control
# inert above 2666 Hz, a bell index frozen above 2030 Hz, a reed that aliased
# above sr/13. All three passed a 220 Hz check, because a key can move
# perfectly at 220 Hz and be a standing tone two octaves up. A gate that samples
# one pitch shares the blind spot of the code it is checking.
#
# The top of the sweep is 4 kHz rather than the 12 kHz clamp because above that
# the limit is physical, not a defect: an FM bell or a clarinet bore at 8 kHz has
# no sideband room left below Nyquist, so it MUST fall still. 4 kHz is the
# highest pitch at which every live key can still be asked to move.
SWEEP_HZ = (55.0, 220.0, 1200.0, 4000.0)

# Below this, a key is static enough that a partial bank represents it. `sine`
# measures 0.0% and the reworked acoustic keys 14-68%, so the boundary is wide.
LIVE_TRAVEL = 5.0

# Below this a key is not merely quieter in its motion, it is FROZEN --
# indistinguishable from a key that has no motion at all (`sine` reads 0.0% and
# `triangle`, which is genuinely static, 1.1-2.4%, so this is the metric's own
# floor). The distinction matters: a mechanism can legitimately have less to act
# on at some pitches -- a supersaw at 4 kHz has six harmonics below Nyquist, so
# its detune moves the centroid 2.5% instead of 43.7% -- and that is physics, not
# a defect. A control that has STOPPED is a defect, and it looks different:
# metallic_fm's frozen index measured 0.07-0.21% while running 116% two octaves
# down. Flagging the first kind would make this gate cry wolf, which costs
# exactly as much as missing the second kind.
#
# 0.6, not 1.5. The margin has to hold at EVERY sample rate, and the reading it
# was calibrated against does not: supersaw at 4 kHz measures 2.46% at 48 kHz but
# 1.27% at 44.1 kHz, because fewer harmonics fit below a lower Nyquist. At 1.5
# this gate failed a healthy key the moment the orchestra ran at 44.1 k -- a
# threshold that follows the sample rate, which is the exact fault this project
# rejected wgclar for. Frozen controls read 0.00-0.21%, so 0.6 keeps a factor of
# ~2 below the worst legitimate reading and ~3 above the worst broken one.
DEAD_TRAVEL = 0.6

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


def _playable(orc, dur, hz):
    """Trim the 100-hour score to one held note on voice 1 and open its gate.

    BOTH the `i 1` instances AND the `e 360000` marker have to go: leaving the
    end marker renders 100 hours regardless of the notes, which has already
    filled this machine's disk once.
    """
    orc = orc.replace("%SR%", str(SR)).replace("-n -d", "-d")
    ctrl = ('\ninstr 900\n  chnset 1,"gate1"\n  chnset %s,"freq1"\n'
            '  chnset 0.8,"vel1"\n  chnset 1,"pres1"\n  chnset 0,"timb1"\n'
            '  chnset 1,"trig1"\nendin\n' % hz)
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


def render(orc, hz, dur=DUR):
    d = Path(tempfile.mkdtemp())
    try:
        (d / "t.csd").write_text(_playable(orc, dur, hz))
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

    Exact for the `key > sine` chains this gate builds, on all 39 keys. It is NOT
    exact in general: a pure vowel pair such as `voice > voice_ee` routes to
    _emit_voice_morph rather than the crossfade, so the `_VOICE_TECH` term here
    is what makes it right against `sine` and wrong for vowel-only chains. Do not
    reuse this for arbitrary chains without re-deriving it.
    """
    return (key in getattr(M, "_NOISE_TECH", set())
            or key in getattr(M, "_MODAL_TECH", set())
            or key in getattr(M, "_LIVE_TECH", set())
            or key in getattr(M, "_VOICE_TECH", set())
            or M._MORPH_SPECTRUM.get(key) in M._SUBFUND_SPECTRA)


def technique_keys(M):
    """Every routable key, from the catalogue -- NOT scraped out of the source.

    This was `re.findall(r'technique == "..."')` over csound_orch.py, which found
    30 of 39 keys and silently skipped the rest, because `fm`, `fm_bell` and
    `metallic_fm` are dispatched as `technique in (...)`. Those three are exactly
    the untested keys that route to the partial-bank path, so the gate's whole
    claim -- that a key made live in future cannot be forgotten -- was false for
    precisely the keys it most needed to cover. They measure 0.0% travel today,
    so nothing is being flattened yet; give one of them a k-rate index wander,
    which is the same edit that made the other ten keys live, and the old gate
    stayed green. That is the original failure reproduced one level up.
    """
    keys = set(M._MORPH_SPECTRUM) | set(M._CS_TECH_EXTRA)
    # Validation-only chain terminals: `x > silence` is a transient, and a
    # terminal rendered alone is silence by definition, which the non-silence
    # check below would rightly reject.
    return sorted(keys - {"silence", "zero"})


def main():
    M = _load()
    techs = technique_keys(M)
    print(f"{'key':14s} " + " ".join(f"{h:.0f}Hz".rjust(7) for h in SWEEP_HZ)
          + f"  {'live?':6}  idiom in `key > sine`")
    failures = []
    for k in techs:
        orc, _ = M.build_orchestra(oscs=[{"chain": [k], "vol": 1.0}])
        travels, silent = [], False
        for hz in SWEEP_HZ:
            sr, x = render(orc, hz)
            if sr is None:
                travels = None
                break
            if float(np.abs(x).max()) < 1e-4:
                silent = True
                travels.append(0.0)
                continue
            travels.append(centroid_travel(sr, x))
        if travels is None:
            print(f"{k:14s} {'RENDER FAILED':>8}")
            failures.append((k, "render failed", set()))
            continue
        row = " ".join(f"{t:6.1f}%" for t in travels)
        # A silent orchestra measures 0.0% travel and would sail through as
        # "static". That is not hypothetical: an init error in a vco2 line
        # deletes all 16 score instances, which IS the silence, and this gate
        # would have called the result a well-behaved static key.
        if silent:
            print(f"{k:14s} {row}  <== renders no audio at all")
            failures.append((k, "renders silent", set()))
            continue
        # THE FREEZE SCAN RUNS FIRST, before any classification can `continue`
        # past it. It used to sit after the median branch, and that ordering
        # reproduced, inside this gate, the exact fault the gate exists to catch:
        # a key frozen at 3 of 4 sweep pitches has a median near zero, so it was
        # relabelled "static" -- which is also the label that makes a key
        # eligible for the flatten path -- and its freeze was never reported. The
        # sensitivity was inverted: the WORSE the regression, the more likely the
        # gate called it healthy. Injecting the historic clip so that three keys
        # became standing tones from 167 Hz upward made this gate print OK and
        # exit 0.
        #
        # The commit's own meta-verification passed only because the historic
        # freeze started at 2030 Hz and left 2 of 4 pitches alive -- i.e. it
        # tested the single case the design happened to catch. A gate must be
        # falsified where it is WEAKEST, not where the last bug happened to be.
        # The discriminator is the COLLAPSE, not the magnitude. A magnitude bar
        # cannot work: `saw`'s vco2 table-switch artifact reads 14.9%, HIGHER
        # than a genuinely broken bell's best surviving pitch (6.7%). What
        # separates them is that a healthy static key never reads near zero
        # anywhere -- saw's floor is 1.7%, pulse's 2.6%, triangle's 1.1% -- while
        # a stopped control reads 0.00-0.21%. So: moves somewhere, near-zero
        # somewhere else.
        if max(travels) >= LIVE_TRAVEL:
            dead = [f"{h:.0f}Hz" for h, t in zip(SWEEP_HZ, travels)
                    if t < DEAD_TRAVEL]
            if dead and k not in FLATTEN_ANYWAY:
                print(f"{k:14s} {row}  {'LIVE':6}  FROZEN at " + ", ".join(dead))
                failures.append((k, "moves elsewhere but its motion has stopped "
                                 "at " + ", ".join(dead), set()))
                continue
        # Liveness is judged on the MEDIAN across the sweep, not the best pitch.
        # Best-pitch classification promotes keys on a single outlier reading:
        # `saw` and `pulse` are genuinely static (1.5% in isolation) yet measure
        # 14.9% and 9.8% at 4 kHz, because vco2 switches bandlimited tables as the
        # analogue drift moves pitch across a table boundary and the harmonic
        # count changes by one. That is a discontinuity in the measurement, not
        # movement, and treating it as movement would force the two commonest
        # waveforms onto the crossfade path for no musical reason.
        #
        # The freeze check below still runs per pitch, so using the median here
        # does not weaken it: a key whose control has stopped somewhere is caught
        # by DEAD_TRAVEL regardless of what the median says.
        if float(np.median(travels)) < LIVE_TRAVEL:
            # Evaluate the routing even for static keys: this line used to say
            # "(may be flattened)" unconditionally, which was the opposite of the
            # truth for the noise beds and sub_sine, all of which take the
            # crossfade path regardless of how still they are.
            where = "flattened" if not takes_crossfade(M, k) else "crossfaded anyway"
            print(f"{k:14s} {row}  {'static':6}  ({where})")
            continue
        travel = max(travels)
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
        print(f"{k:14s} {row}  {'LIVE':6}  {status}")
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
