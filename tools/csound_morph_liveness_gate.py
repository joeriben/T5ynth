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

The gate grew a second job -- has a key's own control STOPPED? -- because every
serious defect in this subsystem has been at the ends of the register, and FOUR
successive rounds of fixing that check reproduced the same fault one level up:
a magnitude bar across pitches, then a magnitude bar across metrics, each time
with the comment above it arguing that magnitude bars cannot work. The way out
was to stop deciding from the current measurement at all. See the comment block
in main() and BASELINE_PATH.

Falsified, not assumed correct: seventeen regressions injected into a sandbox
copy of csound_orch.py, each required to produce a non-zero exit. 13 caught at
48 kHz, 14 at 44.1 k. The suite is deliberately half AMPLITUDE injections,
because the previous one was nine-tenths spectral and therefore shared, by
construction, the blind spot of the code it certified -- it declared the gate
sound while a plain revert of its sibling commit passed. That revert, the
doublet ratio collapsed, the doublet silenced outright, and a nulled sub_sine
divider are all now caught, and none of them was visible before.

KNOWN LIMIT, stated so it is not mistaken for coverage: the four still missed
are all REDUNDANT-CONTROL cases -- one of several parallel controls disabled
while the others keep running, so the key really is still moving. Nulling one
organ rank's wander leaves two; locking the harpsichord's 4' choir leaves the
fixed 0.0031 detune beating; freezing pulse's comparator leaves its per-voice
drift; nulling the strings' bow pressure leaves three players' independent
intonation. Catching these needs a per-CONTROL probe (null one line, require the
output to change), not a per-key one. That is a different tool, and worth
building; what must not happen is reading 13/17 as 17/17.

Run: .venv/bin/python tools/csound_morph_liveness_gate.py [--sr 44100]
Record the reference: ... --update-baseline (both rates), and read the diff.
Exits non-zero on any live key whose idiom a morph discards, on any key whose
control has stopped, and on any reading that has fallen below its reference.
"""
import importlib.util
import json
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
# --sr, because the baseline is per rate and both have to be recorded and
# checked. Every threshold in this file has been wrong at one rate at least once.
SR = int(sys.argv[sys.argv.index("--sr") + 1]) if "--sr" in sys.argv else 48000
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
#
# 40 Hz was added after the BOTTOM turned out to be a blind spot too. A level
# follower only cancels beats slower than its cutoff, so weakening one is a
# bass-register regression by construction: reverting the balance fix leaves
# fm_bell's beat at 0.90 of baseline at 220 Hz, 0.36 at 55, and 0.31 at 40. With
# the sweep starting at 55 the gate could not fail a plain revert of its own
# sibling commit. Both ends now, and E1 is an ordinary note.
SWEEP_HZ = (40.0, 55.0, 220.0, 1200.0, 4000.0)

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
#
# The floor it sits under is REAL MOTION, not the estimator's noise -- which had
# to be checked, because a threshold calibrated against measurement noise would
# be measuring the FFT window rather than the synth, and would move the moment
# either changed. Nulling `kvdr`, the per-voice analogue drift, collapses
# triangle from 2.38/1.09/1.06/1.44% to 0.69/0.29/0.17/0.06 and saw from
# 1.99/1.66/5.69/14.92 to 0.26/0.01/0.03/0.06. So every key here has a live
# control at every pitch and the lowest healthy reading, triangle's 1.05% at
# 44.1 k, is that control being seen. (_emit_vco_drift's own docstring says the
# drift "produces essentially none" of what a liveness probe measures. That is
# wrong by a factor of 3.5-24x, and this gate depends on it being wrong.)
DEAD_TRAVEL = 0.6

# Window for the pitch-scaled swing band, in periods of the played note. 4 sits
# between the two constraints: above ~2 it rejects the carrier, below ~6 it still
# resolves a doublet beat (which arrives every 1/|mod2-mod| = 12-20 periods).
SWING_PERIODS = 4.0

# THE REFERENCE. Every check above this line is absolute -- it asks whether a
# number is near zero -- and absolute motion cannot be judged without knowing
# what the key measured when it was right. That sentence was written into this
# docstring two revisions ago and the reference was deferred as "a different
# test"; the deferral is exactly the hole that let the next two regressions
# through, so it is built here.
#
# Why nothing armed on a CURRENT measurement can replace it: a regression that
# lowers a reading also lowers the statistic used to decide whether to check the
# reading. Arming on the median is defeated by a regression that takes the
# median down with it (reverting the balance fix drops fm_bell's swing median
# under the bar); arming on the max is defeated by any key legitimately quiet at
# one pitch. Self-defeat is inherent, and the way out is a number that does not
# move when the code breaks.
#
# Per sample rate, because the readings genuinely differ (glass measures 123.6%
# at 55 Hz on 48 kHz and 35.6% on 44.1 k). Regenerate with --update-baseline; the
# diff is then a visible part of the commit, which is the whole discipline: a
# baseline regenerated silently proves nothing, and one regenerated from broken
# code certifies the breakage. Read the diff before committing it.
BASELINE_PATH = ROOT / "tools" / "csound_liveness_baseline.json"

# Fail when a reading falls below this fraction of its recorded value. The
# mildest regression this has to catch is a plain revert of the balance fix, at
# 0.31 on fm_bell at 40 Hz, so the bar sits at 0.45 to keep clear of it.
BASELINE_FRACTION = 0.45

# Record the baseline as the MINIMUM of this many runs. 38 of 39 keys reproduce
# bit-identically, but pink_noise does not -- it varies to a min/max of 0.65,
# which alone would force the fraction below 0.35 and let the balance revert
# through. Taking the low end as the reference absorbs the variance where it
# belongs, in the number being compared against, instead of paying for it in the
# threshold for all 39 keys.
BASELINE_RUNS = 2

# Below this a baseline reading is too small to divide by -- the key was already
# near the floor there, and the absolute check is what covers it.
BASELINE_FLOOR = 2.0

# The one key with NO k-rate control at all, so the only one exempt from the
# "something must be moving" check. It is an ASSERTION, not a waiver: the gate
# fails if a key listed here is ever measured MOVING, because then the entry is
# stale and the key has silently lost its freeze cover. That is the difference
# between this list and `_LIVE_TECH`, whose forgotten entries fail silently and
# are the reason this gate exists.
NO_CONTROL = {
    "sine": (
        "the designated standing tone and the movement escape hatch, so it is "
        "mathematically pure by design -- _emit_vco_drift excludes it BY NAME. "
        "Renders bit-identical with the drift nulled, and reads 0.00% on both "
        "metrics at every pitch and both sample rates."
    ),
}

# Keys that measure live but are deliberately still flattened, each with the
# reason and the number. NOT a way to quiet the gate: an entry here is a claim
# that a static partial bank genuinely represents the key, and the gate prints
# every one of them on each run so the claim stays visible.
#
# ROUTING ONLY. An entry says "a bank represents this key inside a morph"; it
# says nothing about whether the key's own control is allowed to stop, and it
# used to suppress the freeze failure as well. That let a real regression hide
# behind an unrelated waiver: freezing square's comparator drift above 150 Hz
# left the gate green. The freeze checks now ignore this set entirely.
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


def _swing_at(x, sr, w):
    w = max(int(w), 16)
    if len(x) < 3 * w:
        return 0.0
    r = np.array([np.sqrt(np.mean(x[i:i + w] ** 2))
                  for i in range(0, len(x) - w, w)])
    r = r[r > 1e-9]
    if len(r) < 3:
        return 0.0
    return float((r.max() - r.min()) / max(r.mean(), 1e-9) * 100.0)


def rms_swing(sr, x, hz):
    """Peak-to-peak spread of the short-term rms, as a % of its mean.

    The SECOND opinion, and it is not optional. Centroid travel is nearly blind
    to keys whose life is AMPLITUDE rather than spectrum: `sub_sine`'s divider
    beat -- the entire point of the key, the reason it uses gbuzz instead of two
    oscili -- reads 0.72% live against 0.53% with its drift LFO nulled. Those are
    0.19 pp apart, so on that metric alone a healthy sub_sine and a dead one are
    the same reading. On this metric the same pair reads 29.5% against 3.8%.

    The blindness runs both ways, which is why both metrics are kept and neither
    is trusted alone: `square`'s comparator drift is purely spectral and reads
    10.2% travel against 0.22% swing at 55 Hz.

    TWO BANDS, because one window cannot see the phenomenon. A fixed 100 ms
    window is a ~5 Hz low-pass on the envelope, while the doublet beat runs at
    |mod2 - mod| * kfreq -- 1 Hz at the bottom of the clamp but 200-320 Hz at
    4 kHz. So the metric silently retired at the top of the register for every
    key whose life is a pitch-proportional beat, which is the exact end where
    every serious defect in this subsystem has lived: fm_bell's beat at 4 kHz
    read 2.16% through the fixed window and reads 52.66% through this one. It
    was never dying, it was never being measured. A window of N periods rejects
    the carrier's own waveform (so N must be at least ~2) while resolving a beat
    down to f/2N.

    Slow motion still has to count -- sub_sine's divider drifts at 0.067 Hz --
    so both bands are measured and the larger taken. That is a disjunction
    WITHIN one phenomenon (amplitude motion at any rate), which is sound, and
    not one BETWEEN two phenomena, which is the mistake this file is repairing.

    One honest limitation: on a noise bed the short window measures the source's
    own statistical variance rather than any envelope motion, so `wind` reads
    365% at 4 kHz. Reproducible to 0.00 pp run-to-run, so it compares correctly
    against its own baseline, but the absolute number means nothing.
    """
    x = x[int(sr * 0.4):]
    return max(_swing_at(x, sr, sr * 0.1),
               _swing_at(x, sr, sr * SWING_PERIODS / max(hz, 1.0)))


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


def regressions(ref, travels, swings):
    """Which readings have fallen materially below what this key recorded.

    Per metric, never combined -- combining them is what let a stopped doublet
    hide behind a live index. A missing entry returns nothing, so a NEW key is
    not failed for having no history; it is reported as unbaselined instead,
    which is a visible prompt to record it rather than a silent pass.
    """
    if not ref:
        return []
    out = []
    for i, hz in enumerate(SWEEP_HZ):
        if i >= len(ref):
            break
        for j, (name, cur) in enumerate((("travel", travels[i]),
                                         ("swing", swings[i]))):
            was = ref[i][j]
            if was < BASELINE_FLOOR:
                continue
            if cur < BASELINE_FRACTION * was:
                out.append(f"{name} at {hz:.0f}Hz {cur:.2f} vs {was:.2f} "
                           f"({cur / was * 100:.0f}%)")
    return out


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
    update = "--update-baseline" in sys.argv
    M = _load()
    techs = technique_keys(M)
    stale = (set(NO_CONTROL) | set(FLATTEN_ANYWAY)) - set(techs)
    all_base = (json.loads(BASELINE_PATH.read_text())
                if BASELINE_PATH.exists() else {})
    base = all_base.get(str(SR), {})
    measured = {}
    if update:
        print(f"--update-baseline: RE-RECORDING {SR} Hz. Read the diff before "
              f"committing it -- a baseline taken from broken code certifies "
              f"the breakage.\n")
    elif not base:
        print(f"no baseline for {SR} Hz in {BASELINE_PATH.name} -- run with "
              f"--update-baseline to record one\n")
    print(f"{'key':14s} " + " ".join(f"{h:.0f}Hz".rjust(13) for h in SWEEP_HZ)
          + f"  {'live?':6}  idiom in `key > sine`")
    print(f"{'':14s} " + " ".join("travel/swing".rjust(13) for _ in SWEEP_HZ))
    failures = []
    # An exemption naming a key that no longer exists is dead text that reads as
    # cover. Neither set was ever checked against the catalogue, so renaming a
    # key would have silently voided its entry -- the same forgotten-list failure
    # this gate exists to eliminate, reintroduced in the gate's own tables.
    for k in sorted(stale):
        print(f"{k:14s}  <== declared in NO_CONTROL/FLATTEN_ANYWAY but is not a "
              f"catalogue key")
        failures.append((k, "exemption names a key that does not exist", set()))
    for k in techs:
        orc, _ = M.build_orchestra(oscs=[{"chain": [k], "vol": 1.0}])
        travels, swings, silent = [], [], False
        for hz in SWEEP_HZ:
            # Only --update-baseline repeats: the reference is the LOW end of
            # what a healthy key measures, so the variance is absorbed there
            # rather than paid for by every key in the threshold.
            t, s = [], []
            for _ in range(BASELINE_RUNS if update else 1):
                sr, x = render(orc, hz)
                if sr is None:
                    travels = None
                    break
                if float(np.abs(x).max()) < 1e-4:
                    silent = True
                    t.append(0.0)
                    s.append(0.0)
                    continue
                t.append(centroid_travel(sr, x))
                s.append(rms_swing(sr, x, hz))
            if travels is None:
                break
            travels.append(min(t))
            swings.append(min(s))
        if travels is None:
            print(f"{k:14s} {'RENDER FAILED':>8}")
            failures.append((k, "render failed", set()))
            continue
        row = " ".join(f"{t:6.1f}/{s:5.1f}" for t, s in zip(travels, swings))
        # What is moving AT ALL at each pitch, on either metric.
        alive = [max(t, s) for t, s in zip(travels, swings)]
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
        #
        # Ordering was not enough. The scan was still ARMED on `max(travels) >=
        # LIVE_TRAVEL`, which is a magnitude bar -- the discriminator the same
        # commit argued cannot work -- so a key whose control had stopped at ALL
        # FOUR pitches had max < 5.0, never armed the scan, fell through to the
        # median branch and was labelled `static`. The boundary moved from 3-of-4
        # to 4-of-4 and the inverted sensitivity survived: a plain `git revert` of
        # the commit that made the bells ring gives fm_bell 0.3/0.1/0.0/0.0% and
        # this gate printed OK, exit 0, at both sample rates. Arming on a
        # magnitude ALSO reintroduced the sample-rate dependence removed from
        # DEAD_TRAVEL two paragraphs up, because for saw and pulse the max IS the
        # vco2 artifact, and that reads 14.92% at 48 kHz against 2.06% at 44.1 k.
        #
        # Adding a second metric and combining it with max() moved the SAME fault
        # up one more level: from a magnitude bar across PITCHES to a magnitude
        # bar across METRICS. Under `max(travel, swing)` a key with two controls
        # only ever has to keep the LARGER-READING one alive, so the other may
        # stop dead at every pitch. Structurally, a fall in swing could not fail
        # this gate at all -- it could only feed a max already dominated by
        # travel, or reclassify a key to `static`, which is the laxer outcome.
        # Measured: a plain `git revert` of the sibling commit that restored the
        # bells' doublet beat -- swing 0.9% and 0.2% at 1200 and 4000 Hz, under
        # the dead floor -- left this gate green, as did silencing the doublet
        # outright. The gate printed the very number that proved the regression
        # and had no path by which that number could fail.
        #
        # So the disjunction is gone. Each metric is now checked on its own
        # against the baseline, and `alive` is used ONLY for the absolute
        # "renders something that moves" floor, where a disjunction is correct
        # because a beat and a spectral sweep are both motion.
        #
        #   A. UNCONDITIONAL, absolute. At every pitch something must be moving
        #      on either metric. Nothing arms it, so "dead everywhere" is the
        #      case it is strongest on. `sine` is the only key with no control
        #      at all, and it is declared.
        #   B. UNCONDITIONAL, relative. Every reading, on BOTH metrics, against
        #      what that key measured when it was right. Nothing arms this
        #      either -- which is the point, since every armed variant has been
        #      defeated by a regression that drags its own arming statistic down.
        dead = [f"{h:.0f}Hz" for h, a in zip(SWEEP_HZ, alive) if a < DEAD_TRAVEL]
        if k in NO_CONTROL:
            # The assertion, checked in the other direction: this key claims to
            # have no control, so it must not be moving.
            moving = [f"{h:.0f}Hz" for h, a in zip(SWEEP_HZ, alive)
                      if a >= DEAD_TRAVEL]
            if moving:
                print(f"{k:14s} {row}  {'?':6}  MOVES at " + ", ".join(moving)
                      + " -- NO_CONTROL entry is stale")
                failures.append((k, "declared NO_CONTROL but moves at "
                                 + ", ".join(moving), set()))
                continue
        elif dead:
            print(f"{k:14s} {row}  {'DEAD':6}  nothing moving at "
                  + ", ".join(dead))
            failures.append((k, "no motion on either metric at "
                             + ", ".join(dead), set()))
            continue
        measured[k] = [[round(t, 3), round(s, 3)]
                       for t, s in zip(travels, swings)]
        fell = regressions(base.get(k), travels, swings)
        if fell and not update:
            print(f"{k:14s} {row}  {'FELL':6}  " + "; ".join(fell))
            failures.append((k, "below baseline: " + "; ".join(fell), set()))
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
        # Using the median here does not weaken the freeze checks, because both
        # of them have already run ABOVE and neither can be reached past. (This
        # comment used to say "the freeze check below still runs per pitch ...
        # regardless of what the median says", while the check sat after this
        # branch's `continue` and did not run at all. A comment asserting the
        # property the code lacks is how the first version of this hole survived
        # review; it is repeated here as the thing to check for, not as advice.)
        #
        # Liveness is EITHER metric, for the same reason the freeze check is: a
        # key whose identity is a beat -- sub_sine's divider against the
        # fundamental, at 32% swing and 3.9% travel -- would lose exactly that
        # beat if a morph flattened it to a partial bank, and on travel alone
        # this gate would have called it static and allowed it. Nothing changes
        # verdict today (sub_sine, rain and pink_noise are promoted to LIVE and
        # all three already crossfade), which is the point: the hole is closed
        # while it is still theoretical.
        if (float(np.median(travels)) < LIVE_TRAVEL
                and float(np.median(swings)) < LIVE_TRAVEL):
            # Evaluate the routing even for static keys: this line used to say
            # "(may be flattened)" unconditionally, which was the opposite of the
            # truth for the noise beds and sub_sine, all of which take the
            # crossfade path regardless of how still they are.
            where = "flattened" if not takes_crossfade(M, k) else "crossfaded anyway"
            print(f"{k:14s} {row}  {'static':6}  ({where})")
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
        print(f"{k:14s} {row}  {'LIVE':6}  {status}")
        if not crossfaded and k not in FLATTEN_ANYWAY:
            failures.append((k, "takes the additive path", lost))
    print()
    for label, entries in (("documented exception", FLATTEN_ANYWAY),
                           ("declared to have no control", NO_CONTROL)):
        for k, why in sorted(entries.items()):
            print(f"{label} -- {k}:")
            for ln in __import__("textwrap").wrap(why, 72):
                print(f"    {ln}")
            print()
    if update:
        all_base[str(SR)] = measured
        BASELINE_PATH.write_text(
            json.dumps(all_base, indent=1, sort_keys=True) + "\n")
        print(f"recorded {len(measured)} keys at {SR} Hz -> "
              f"{BASELINE_PATH.name}")
        return 0
    fresh = sorted(set(measured) - set(base))
    if fresh:
        # Not a failure -- a new key legitimately has no history. Printed loudly
        # so it is recorded deliberately rather than sliding in unprotected,
        # which is how every hand-maintained list in this subsystem has rotted.
        print("NOT BASELINED (run --update-baseline to record): "
              + ", ".join(fresh) + "\n")
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
