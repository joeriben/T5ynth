"""Does a key that MOVES on its own still move when a morph chain uses it?

There is now ONE tonal morph route: every stage renders its own idiom and the
stages are equal-power crossfaded. The second route -- interpolating two static
partial banks -- is deleted, and this file is part of why it survived as long as
it did, so the history is kept here rather than tidied away.

That route replaced a live Csound idiom with a bank of `oscili`: both the
additive bank this project forbids and the "everything sounds the same" collapse
`_LIVE_TECH` was introduced to stop. This gate was written to catch it, and did
catch it for the keys it looked at. It did not look at the basic waveforms,
because of a sentence that used to stand in this docstring:

    "A key that is genuinely static (saw, square, triangle, pulse, sine) is
     allowed to be flattened -- reduced to a static spectrum a saw is still a
     saw. That is the real distinction."

It is not a saw. Measured on `saw > sine` at 220 Hz: the saw alone is `vco2`
with 59 partials above -40 dB; through the additive morph it was 6 partials,
no `vco2`, with the harmonics already ramped down (H2 at 0.131 where a saw
wants 0.496) -- an ordinary sine within a second of the note. BJ's report was
"saw > sine does not even work". Six keys sat behind that sentence -- saw,
square, triangle, pulse, bass_saw, sine -- and the sentence was doing real work:
it gated a `continue` that skipped the morph check entirely for all of them.

So the lesson is not "that claim was wrong". It is that a claim about how
something SOUNDS was allowed to decide whether anything would be measured. The
morph check now runs for every key, live or static, and it RENDERS the morph
instead of comparing opcode names in the emitted text.

`_LIVE_TECH` is hand-maintained, and this gate exists because that failed exactly
as hand-maintained lists do. A session replaced ten table-driven keys with real
idioms -- organ became three gbuzz ranks with independent wander, clarinet a
foscili bore opened by breath pressure, additive and harpsichord detuned twins
whose BEATING is the whole point, cheby a driven waveshaper, ring_mod a genuine
product with a drifting carrier leak -- and did not add one of them to the list.
Standalone they measured 14-27% spectral-centroid travel. Inside `a > b` all six
emitted nothing but `oscili`: every idiom gone, silently, with the suite green.

So the gate does not consult the list. It MEASURES:

  1. Render every catalogue key alone and measure how it moves.
  2. Render `key > sine` and require the key's idiom to survive it -- for EVERY
     key, not only the ones step 1 called live. Restricting this to live keys is
     the exact hole described above.
  3. Compare every reading against a recorded reference, per cell.

Step 1 takes its key list from the catalogue, not from a regex over the source.
The first version scraped `technique == "..."` and so tested 30 of 39 keys,
missing the three dispatched as `technique in (...)` -- which were the only
untested keys routing to the partial-bank path. A gate that claims nothing has
to be remembered must not itself depend on being remembered.

Step 3 used to evaluate a hand-transcribed COPY of the assembler's own routing
condition. Asking a duplicate of the code under test whether the code under test
did its job is not a check: the mirror agreed with the original and both were
wrong about the audio. It now builds the morph, renders it, and asks the signal
three things -- the standalone idiom opcodes are still present, it makes sound,
and its spectrum actually travels from one endpoint to the other.

(Signature-sniffing alone was tried even earlier and produced a false negative:
`ring_mod`'s identity is a PRODUCT of two signals, not an opcode. That is why
the opcode check is one of three questions rather than the whole test, and why
the other two are asked of rendered audio.)


THE SECOND JOB, AND SIX ROUNDS OF THE SAME MISTAKE
--------------------------------------------------
The gate also asks: has a key's own control STOPPED? Every serious defect in this
subsystem has been at the ends of the register, and SIX successive rounds of
fixing this check reproduced one single fault, each time one abstraction level up
or down from the last, each time under a comment arguing that this exact fault
was impossible:

  1. The freeze scan ran AFTER the classification that could `continue` past it.
  2. It was ARMED on `max(travels) >= LIVE_TRAVEL` -- a magnitude bar across
     PITCHES, so a key frozen at every pitch never armed the scan at all.
  3. A second metric was added and combined as `max(travel, swing)` -- a
     magnitude bar across METRICS, under which a fall in swing could not fail
     the gate by any path.
  4. `rms_swing` internally took `max(slow_band, pitched_band)` -- the same
     across BANDS. Measured on the key this was all about: reverting the balance
     fix collapses fm_bell's slow band to 14% at 1200 Hz and 10% at 4000 Hz
     while the max reports 104% and 99% of baseline.
  5. A reference was finally recorded -- and then `BASELINE_FLOOR = 2.0` decided
     WHETHER to consult it, which is a magnitude bar on the reference. 53 of 390
     cells fell through it back onto rule 3's max(). `pulse` with its comparator
     drift frozen reads 11-15% of baseline and passed.
  6. Every check was a LOWER bound, so a key documented as deliberately still
     could gain motion it is not supposed to have and nothing looked.

The move is always the same: combine two independent readings, or gate a check
behind a magnitude. It survives because a regression lowers the very statistic
that decides whether to look at the regression.

So there is now exactly one rule, and it is applied per CELL -- a cell being one
(key, pitch, metric) triple, with three independent metrics per pitch that are
never combined for this purpose:

    reference says the cell MOVED  ->  require cur >= BASELINE_FRACTION * ref
    reference says the cell was STILL ->  require cur <= STILL_CEILING

Every cell gets exactly one of these. Nothing arms them, nothing skips them,
nothing is combined, and stillness is asserted as strictly as motion. A cell
with no reference is a FAILURE, not a pass, because "not baselined" being
survivable is what let regenerate-over-a-regression become permanent.

Disjunctions still appear in two places and are correct in both, because the
question there is EXISTENTIAL ("does this key move at all", "is it live enough
that flattening destroys something") rather than CONSERVATIONAL ("does this
particular control still work"). Conservation is only ever judged per cell,
against the reference. That distinction is the whole repair; if a future edit
blurs it, this file is back to round 3.

Falsified, not assumed correct: regressions injected into a sandbox copy of
csound_orch.py, each required to produce a non-zero exit. The suite is
deliberately balanced across AMPLITUDE, SPECTRAL, RATE and STILLNESS axes,
because two earlier suites shared by construction the blind spot of the code
they certified -- one was nine-tenths spectral and declared the gate sound while
a plain revert of its sibling commit passed; the next was entirely lower-bound
and could not see an invented tremolo.

Run: .venv/bin/python tools/csound_morph_liveness_gate.py [--sr 44100]
Record the reference: ... --update-baseline (both rates), and read the diff.
--update-baseline REFUSES to write over a structural failure, and refuses to
write over a fallen reading unless --accept-regressions is also given, so
accepting a loss is an explicit act that appears in the shell history.
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
#
# Changing this tuple invalidates the reference BY CELL, not wholesale: pitches
# are stored by name, so an added pitch is reported as an unbaselined cell and a
# removed one as a stale reference. The previous schema stored bare positional
# arrays, under which appending a pitch was silently never checked.
SWEEP_HZ = (40.0, 55.0, 220.0, 1200.0, 4000.0)

# The three independent readings taken at each pitch. They are stored and
# compared SEPARATELY, always. Combining any two of them is the fault this file
# has now made four times; see the docstring.
METRICS = ("travel", "swing_slow", "swing_pitched")

# Below this a key is static enough that a partial bank represents it. `sine`
# measures 0.0% travel and the reworked acoustic keys 14-68%, so the boundary is
# wide.
LIVE_TRAVEL = 5.0

# Below this a key is not merely quieter in its motion, it is FROZEN --
# indistinguishable from a key that has no motion at all. This is the absolute
# existential floor ("is ANYTHING moving here"), and it is the only check a key
# with no reference yet can get. It is not the conservation check; that is
# per cell against the reference and cannot be reached past.
#
# 0.6, not 1.5. The margin has to hold at EVERY sample rate, and the reading it
# was calibrated against does not: supersaw at 4 kHz measures 2.46% at 48 kHz but
# 1.27% at 44.1 kHz, because fewer harmonics fit below a lower Nyquist. At 1.5
# this gate failed a healthy key the moment the orchestra ran at 44.1 k -- a
# threshold that follows the sample rate, which is the exact fault this project
# rejected wgclar for.
DEAD_TRAVEL = 0.6

# How much of the distance between its OWN two endpoints a morph must actually
# cover. Measured, not chosen: across the 37 keys whose standalone spectrum
# differs audibly from a sine, the worst healthy reading is voice_ee at 68.2%
# and the median is 98.0%. A morph frozen at either endpoint reads ~0%, so the
# populations are nowhere near each other and 0.45 sits with a 1.5x margin under
# the worst healthy key.
TRAVEL_FRACTION = 0.45

# Window for the pitch-scaled swing band, in periods of the played note. 4 sits
# between the two constraints: above ~2 it rejects the carrier, below ~6 it still
# resolves any amplitude motion that scales WITH pitch. (The FM-metal doublet
# beat used to live here at |mod2-mod|*kfreq; since 0bea3fd1 it is a fixed-Hz
# warble, 1.1-7.6 Hz, and moved to the swing_slow band -- see swing_pitched
# below.)
SWING_PERIODS = 4.0

# THE REFERENCE. Absolute motion cannot be judged without knowing what the key
# measured when it was right, and nothing armed on a CURRENT measurement can
# substitute, because a regression lowers the very statistic used to decide
# whether to check the reading.
#
# Per sample rate, because the readings genuinely differ (glass measures 123.6%
# at 55 Hz on 48 kHz and 35.6% on 44.1 k). Regenerate with --update-baseline; the
# diff is then a visible part of the commit, which is the whole discipline: a
# baseline regenerated silently proves nothing, and one regenerated from broken
# code certifies the breakage.
BASELINE_PATH = ROOT / "tools" / "csound_liveness_baseline.json"

# Fail when a moving cell falls below this fraction of its recorded value. The
# mildest regression this has to catch is a plain revert of the balance fix, at
# 0.31 on fm_bell at 40 Hz, so the bar sits at 0.45 to keep clear of it.
BASELINE_FRACTION = 0.45

# Record the baseline as the MINIMUM of this many runs. 38 of 39 keys reproduce
# bit-identically, but pink_noise does not: over 14 runs its cells range 0.435 to
# 0.738 of each other, so taking the low end absorbs that variance in the number
# being compared against instead of paying for it in the threshold for all 39
# keys. Enumerating every (baseline-pair, later-run) combination from 30 renders
# gives 0 of 12180 false failures, with the worst recordable bar at 60.81 against
# an observed minimum of 80.70.
BASELINE_RUNS = 2

# The live/still boundary FOR A REFERENCE CELL, measured rather than chosen --
# choosing one is what BASELINE_FLOOR did, and it handed 14% of all cells back to
# the disjunction it was meant to replace. `sine` is the designated standing tone
# with no k-rate control at all, so what it reads IS the estimator's floor:
# travel 0.0000, swing_slow 0.0020, swing_pitched 0.2418 (worst over both rates
# and all five pitches; the 0.2418 is at 4 kHz / 44.1 k, where a 4-period window
# holds few enough cycles for the residual to show).
#
# 0.5 is just over twice that worst reading. Above it a cell is asserted to MOVE
# and gets the lower bound; below it the cell is asserted to be STILL and gets
# the upper bound. There is no third case and nothing is skipped -- which is the
# entire difference from BASELINE_FLOOR, whose purpose was to skip.
LIVE_CELL = 0.5

# How far a cell the reference calls STILL may read before it counts as motion
# that was invented rather than measured. Every check in this gate used to be a
# lower bound, so `hiss` -- documented in csound_orch.py as "deliberately left
# STATIONARY ... giving this one gusts or swells would be inventing a
# phenomenon" -- could be given a 3.1 Hz tremolo and nothing looked.
#
# Sized from both sides rather than picked: the healthy still cells top out well
# below it, and an invented tremolo lands far above. Stated as a real number so
# the margin is checkable; see tools/csound_liveness_baseline.json for the
# distribution it has to clear.
STILL_CEILING = 3.0

# LOAD-BEARING ORDERING, asserted rather than trusted to prose. A still cell
# gets only an upper bound, so nothing fails if its reading falls -- and the
# justification for that ("a still cell has no motion to lose") is only true
# because a pitch at which EVERY metric is still is caught by the existential
# floor instead. That holds precisely while DEAD_TRAVEL > LIVE_CELL: if all
# three metrics sit below LIVE_CELL, their max is below DEAD_TRAVEL too and the
# key is failed as DEAD at that pitch.
#
# Invert the ordering and a pitch could have all three metrics still, no cell
# with a lower bound, and no existential failure -- a silent hole of exactly
# the kind this file has now produced four times by reasoning instead of
# checking. So it is checked.
assert DEAD_TRAVEL > LIVE_CELL, (
    f"DEAD_TRAVEL ({DEAD_TRAVEL}) must exceed LIVE_CELL ({LIVE_CELL}), or a "
    f"pitch whose every metric is still gets no check at all")

# The one key with NO k-rate control at all. Its cells are all still, so the
# per-cell rule above already holds it to that; this entry carries the REASON,
# and the gate fails if a key listed here is ever measured moving, so the claim
# cannot rot into dead text.
NO_CONTROL = {
    "sine": (
        "the designated standing tone and the movement escape hatch, so it is "
        "mathematically pure by design -- _emit_vco_drift excludes it BY NAME. "
        "Renders bit-identical with the drift nulled. Reads 0.0000% travel and "
        "0.0020% slow swing everywhere; the pitch-scaled band reads up to "
        "0.2418% at 4 kHz on 44.1 kHz, which is the estimator's own floor and "
        "is what LIVE_CELL is calibrated against."
    ),
}

# The `square` exemption that used to live here is GONE, and with it the open
# question it carried. It said a square could keep its comparator drift or keep
# spectral interpolation on `saw > square`, but not both -- and asked BJ to
# choose. The choice was false: the interpolation on offer was a six-partial
# bank, so the trade was analogue life against nothing. With the additive morph
# deleted both endpoints render their real idiom and the question dissolves.
# An exemption is a claim; this one's premise was never measured.

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
    """Peak-to-peak spread of the short-term rms over window `w`, as % of mean."""
    w = max(int(w), 16)
    if len(x) < 3 * w:
        return 0.0
    r = np.array([np.sqrt(np.mean(x[i:i + w] ** 2))
                  for i in range(0, len(x) - w, w)])
    r = r[r > 1e-9]
    if len(r) < 3:
        return 0.0
    return float((r.max() - r.min()) / max(r.mean(), 1e-9) * 100.0)


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


def measure(sr, x, hz):
    """The three readings at one pitch. Kept SEPARATE, all the way to disk.

    They see genuinely different phenomena, which is why none of them may stand
    in for another:

      * `travel` -- spectral. `square`'s comparator drift is purely spectral and
        reads 10.2% travel against 0.22% swing at 55 Hz.
      * `swing_slow` -- amplitude motion below ~5 Hz. A bell's ring crescendo
        lives here, and so does sub_sine's 0.067 Hz divider drift. Since
        0bea3fd1 the FM-metal doublet beat lives here too: a fixed-Hz warble
        (fm_bell 2.6, metallic_fm 7.6, fm 3.6 Hz) at EVERY pitch, where the old
        two-ratio law put it at |mod2-mod|*kfreq and let it climb into the
        roughness band. Centroid travel is nearly blind to all of this:
        sub_sine reads 0.72% live against 0.53% with its drift LFO nulled, 0.19
        pp apart, while on swing the same pair reads 29.5% against 3.8%.
      * `swing_pitched` -- amplitude motion at a rate proportional to the note,
        measured over 4 periods. This band still exists to catch a
        pitch-scaling beat should any key emit one; the FM-metal doublet used
        to be that case (|mod2-mod|*kfreq, 1 Hz at the clamp floor rising to
        200-320 Hz at 4 kHz, which a fixed 100 ms window is a ~5 Hz low-pass
        against and could not see) but no longer is -- its fixed-Hz replacement
        is caught by swing_slow above, which is why metallic_fm's re-baselined
        swing_slow@4000 Hz cell now reads MOVES.

    These last two were briefly combined as `max(slow, pitched)` under the
    argument that they are one phenomenon (amplitude motion at any rate). They
    are not: a level follower cancels motion BELOW its cutoff, so reverting the
    balance fix kills the slow band alone -- 14% and 10% of baseline at 1200 and
    4000 Hz -- while the max reads 104% and 99% and reports nothing.

    One honest limitation: on a noise bed the short window measures the source's
    own statistical variance rather than any envelope motion, so `wind` reads
    365% at 4 kHz. Reproducible to 0.00 pp run-to-run, so it compares correctly
    against its own baseline, but the absolute number means nothing.
    """
    xx = x[int(sr * 0.4):]
    return {"travel": centroid_travel(sr, x),
            "swing_slow": _swing_at(xx, sr, sr * 0.1),
            "swing_pitched": _swing_at(xx, sr, sr * SWING_PERIODS / max(hz, 1.0))}


def idioms(orc):
    body = orc.split("instr 1")[1] if "instr 1" in orc else orc
    return {op for op in IDIOM_OPCODES if f" {op} " in body}


def morph_problems(M, k, orc, want, hz=220.0):
    """Does `k > sine` still SOUND like k turning into a sine?

    This replaces a routing predicate that was a hand-transcribed COPY of the
    assembler's own condition. Asking a duplicate of the code under test whether
    the code under test did its job is not a check, and it failed exactly that
    way: `saw > sine` emitted six `oscili` where the saw alone is a 59-partial
    `vco2`, and this file called it correct at every sample rate. The mirror
    agreed with the original, and both were wrong about the audio.

    So nothing here consults a set, a table, or a mirrored condition. The morph
    is BUILT and RENDERED, and three things are asked of the actual signal:

      1. the idiom opcodes present when k plays alone are still present;
      2. it makes sound at all;
      3. it actually travels -- its spectrum early in the note resembles k, late
         in the note resembles sine, and the two differ. A crossfade that never
         moves and a morph frozen at one endpoint are both failures, and neither
         is visible to an opcode grep.

    Check 3 only fires when k and sine are audibly different to begin with
    (a key that already IS a sine has nothing to travel), so the threshold is
    relative to the standalone distance rather than absolute.
    """
    out = []
    morc, _ = M.build_orchestra(oscs=[{"chain": [k, "sine"], "vol": 1.0}])
    lost = want - idioms(morc)
    if lost:
        out.append("morph drops " + ", ".join(sorted(lost)))
    sr, xm = render(morc, hz)
    if sr is None:
        return out + ["morph fails to render"], morc
    if float(np.abs(xm).max()) < 1e-4:
        return out + ["morph renders silent"], morc
    _, xk = render(orc, hz)
    ssrc, xs = render(M.build_orchestra(oscs=[{"chain": ["sine"], "vol": 1.0}])[0], hz)
    ck, cs = _mean_centroid(sr, xk), _mean_centroid(ssrc, xs)
    span = abs(ck - cs) / max(ck, cs, 1.0)
    if span > 0.15:                      # the two endpoints are audibly apart
        # The early window is taken from the MORPH time, not from the render
        # length. Sized off the render it spanned 0.54-1.8 s of a 1.4 s morph --
        # i.e. it averaged the whole transition and then complained the
        # transition had not happened, failing five healthy keys. A measurement
        # window has to be derived from the thing being measured.
        mt = getattr(M, "DEFAULT_MORPH_SEC", 1.4)
        early = _mean_centroid(sr, xm[int(sr * mt * 0.04):int(sr * mt * 0.16)])
        late = _mean_centroid(sr, xm[int(len(xm) * 0.80):])
        travelled = abs(early - late) / max(abs(ck - cs), 1.0)
        if travelled < TRAVEL_FRACTION:
            out.append(f"morph does not travel: centroid {early:.0f}->{late:.0f} Hz, "
                       f"only {travelled * 100:.0f}% of the {ck:.0f}->{cs:.0f} Hz "
                       f"distance between its own endpoints")
    return out, morc


def _mean_centroid(sr, x):
    """Mean spectral centroid in Hz (not a spread -- that is centroid_travel)."""
    if len(x) < 2048:
        return 0.0
    w = min(int(sr * 0.20), len(x))
    cs = []
    for i in range(0, len(x) - w + 1, max(w // 2, 1)):
        seg = x[i:i + w] * np.hanning(w)
        mag = np.abs(np.fft.rfft(seg))
        tot = mag.sum()
        if tot > 1e-9:
            cs.append(float((mag * np.fft.rfftfreq(w, 1.0 / sr)).sum() / tot))
    return float(np.mean(cs)) if cs else 0.0


def cell_problems(ref, cur):
    """THE conservation check. One rule per cell, nothing armed, nothing skipped.

    `ref` and `cur` are {pitch_name: {metric: value}}. A cell the reference calls
    moving must still move; a cell it calls still must still be still. There is
    no branch that declines to check, which is the property four previous
    versions of this function lacked in four different places.

    A missing reference cell is a PROBLEM, not a pass. "Not baselined" being
    survivable is what made regenerate-over-a-regression permanent: the previous
    version dropped keys that failed structurally from the recorded set, wrote
    the remainder wholesale, and thereafter reported those keys as merely
    unbaselined -- documented, in the same file, as not a failure.
    """
    out = []
    swept = {f"{h:.0f}" for h in SWEEP_HZ}
    # An older reference format is a DISCARD of reference information, so it
    # reports as a problem and needs the same explicit flag as accepting a fall.
    # Letting a shape change quietly regenerate would mean renaming a metric
    # silently voids every check that metric carried.
    if not isinstance(ref, dict) or not all(
            isinstance(v, dict) for v in ref.values()):
        return ["reference is in an older format (positional arrays, before the "
                "metrics were stored by name) -- nothing can be compared"]
    for hz in SWEEP_HZ:
        p = f"{hz:.0f}"
        rp = ref.get(p)
        if rp is None:
            out.append(f"no reference at {p}Hz")
            continue
        for m in METRICS:
            if m not in rp:
                out.append(f"no reference for {m} at {p}Hz")
                continue
            was, now = float(rp[m]), cur[p][m]
            if was >= LIVE_CELL:
                if now < BASELINE_FRACTION * was:
                    out.append(f"{m} at {p}Hz {now:.2f} vs {was:.2f} "
                               f"({now / was * 100:.0f}%)")
            elif now > STILL_CEILING:
                out.append(f"{m} at {p}Hz MOVES {now:.2f}, reference had it "
                           f"still at {was:.2f}")
    for p in sorted(set(ref) - swept):
        out.append(f"reference has {p}Hz, which the sweep no longer covers")
    return out


def technique_keys(M):
    """Every routable key, from the catalogue -- NOT scraped out of the source.

    This was `re.findall(r'technique == "..."')` over csound_orch.py, which found
    30 of 39 keys and silently skipped the rest, because `fm`, `fm_bell` and
    `metallic_fm` are dispatched as `technique in (...)`. Those three are exactly
    the untested keys that route to the partial-bank path, so the gate's whole
    claim -- that a key made live in future cannot be forgotten -- was false for
    precisely the keys it most needed to cover.
    """
    keys = set(M._TONAL_KEYS) | set(M._CS_TECH_EXTRA)
    # Validation-only chain terminals: `x > silence` is a transient, and a
    # terminal rendered alone is silence by definition, which the non-silence
    # check below would rightly reject.
    return sorted(keys - {"silence", "zero"})


def main():
    update = "--update-baseline" in sys.argv
    accept = "--accept-regressions" in sys.argv
    M = _load()
    techs = technique_keys(M)
    stale = set(NO_CONTROL) - set(techs)
    all_base = (json.loads(BASELINE_PATH.read_text())
                if BASELINE_PATH.exists() else {})
    base = all_base.get(str(SR), {})
    measured = {}
    # STRUCTURAL failures can never be written over. RELATIVE ones -- a reading
    # that fell, or a still cell that started moving -- are a deliberate act and
    # need --accept-regressions, which puts the decision in the shell history
    # rather than in a silently regenerated file.
    structural, relative = [], []
    if update:
        print(f"--update-baseline: RE-RECORDING {SR} Hz. Read the diff before "
              f"committing it -- a baseline taken from broken code certifies "
              f"the breakage.\n")
    elif not base:
        # A missing rate used to print an advisory and exit 0, which meant
        # deleting one key from this file disabled every conservation check at
        # that rate while the gate still printed OK.
        print(f"no baseline for {SR} Hz in {BASELINE_PATH.name} -- run with "
              f"--update-baseline to record one\n")
        structural.append((f"<{SR} Hz>", "no reference recorded at this rate",
                           set()))
    head = " ".join(f"{h:.0f}Hz".rjust(17) for h in SWEEP_HZ)
    print(f"{'key':14s} {head}  {'live?':6}  idiom in `key > sine`")
    print(f"{'':14s} " + " ".join("trav/slow/pitch".rjust(17) for _ in SWEEP_HZ))
    # An exemption naming a key that no longer exists is dead text that reads as
    # cover. Neither set was ever checked against the catalogue, so renaming a
    # key would have silently voided its entry -- the same forgotten-list failure
    # this gate exists to eliminate, reintroduced in the gate's own tables.
    for k in sorted(stale):
        print(f"{k:14s}  <== declared in NO_CONTROL but is not a "
              f"catalogue key")
        structural.append((k, "exemption names a key that does not exist", set()))
    for k in techs:
        orc, _ = M.build_orchestra(oscs=[{"chain": [k], "vol": 1.0}])
        cur, failed, silent = {}, False, False
        for hz in SWEEP_HZ:
            # Only --update-baseline repeats: the reference is the LOW end of
            # what a healthy key measures, so pink_noise's variance is absorbed
            # there rather than paid for by every key in the threshold.
            runs = []
            for _ in range(BASELINE_RUNS if update else 1):
                sr, x = render(orc, hz)
                if sr is None:
                    failed = True
                    break
                if float(np.abs(x).max()) < 1e-4:
                    silent = True
                    runs.append({m: 0.0 for m in METRICS})
                    continue
                runs.append(measure(sr, x, hz))
            if failed:
                break
            cur[f"{hz:.0f}"] = {m: min(r[m] for r in runs) for m in METRICS}
        if failed:
            print(f"{k:14s} {'RENDER FAILED':>17}")
            structural.append((k, "render failed", set()))
            continue
        row = " ".join(f"{cur[f'{h:.0f}']['travel']:5.1f}/"
                       f"{cur[f'{h:.0f}']['swing_slow']:4.1f}/"
                       f"{cur[f'{h:.0f}']['swing_pitched']:5.1f}" for h in SWEEP_HZ)
        # A silent orchestra measures 0.0% on everything and would sail through
        # as "static". That is not hypothetical: an init error in a vco2 line
        # deletes all 16 score instances, which IS the silence, and this gate
        # would have called the result a well-behaved static key.
        if silent:
            print(f"{k:14s} {row}  <== renders no audio at all")
            structural.append((k, "renders silent", set()))
            continue
        # EXISTENTIAL, absolute: is anything moving at all here? A disjunction
        # across the metrics is correct for THIS question -- a beat and a
        # spectral sweep are both motion -- and it is never the only check on a
        # baselined key, because cell_problems() runs unconditionally below. On
        # an UNBASELINED key it is the only check there can be, which is exactly
        # why an unbaselined key is now a failure rather than a note.
        alive = {p: max(v[m] for m in METRICS) for p, v in cur.items()}
        dead = [f"{h:.0f}Hz" for h in SWEEP_HZ
                if alive[f"{h:.0f}"] < DEAD_TRAVEL]
        if k in NO_CONTROL:
            moving = [f"{h:.0f}Hz" for h in SWEEP_HZ
                      if alive[f"{h:.0f}"] >= DEAD_TRAVEL]
            if moving:
                print(f"{k:14s} {row}  {'?':6}  MOVES at " + ", ".join(moving)
                      + " -- NO_CONTROL entry is stale")
                structural.append((k, "declared NO_CONTROL but moves at "
                                   + ", ".join(moving), set()))
                continue
        elif dead:
            print(f"{k:14s} {row}  {'DEAD':6}  nothing moving at "
                  + ", ".join(dead))
            structural.append((k, "no motion on any metric at "
                               + ", ".join(dead), set()))
            continue
        measured[k] = {p: {m: round(v[m], 4) for m in METRICS}
                       for p, v in cur.items()}
        # CONSERVATIONAL, per cell, against the reference. Unconditional -- and
        # it does NOT `continue`. A fallen reading and a flattened idiom are
        # independent defects that can hold at once, and reporting only the
        # first means the second surfaces a run later, if at all. Skipping a
        # remaining check after one failure is the shape of the very first
        # version of this bug (the freeze scan sat after a branch that could
        # `continue` past it), so every check below runs for every key that
        # produced measurements.
        label = "LIVE"
        if k in base:
            fell = cell_problems(base[k], cur)
            if fell:
                label = "FELL"
                note = "; ".join(fell[:3]) + (f" (+{len(fell) - 3} more)"
                                              if len(fell) > 3 else "")
                print(f"{k:14s} {row}  {'FELL':6}  {note}")
                relative.append((k, "; ".join(fell), set()))
        elif not update:
            label = "?"
            print(f"{k:14s} {row}  {'?':6}  NOT BASELINED")
            structural.append((k, "no reference for this key -- record one with "
                               "--update-baseline", set()))
        # Liveness is judged on the MEDIAN across the sweep, not the best pitch.
        # Best-pitch classification promotes keys on a single outlier reading:
        # `saw` and `pulse` are genuinely static (1.5% in isolation) yet measure
        # 14.9% and 9.8% at 4 kHz, because vco2 switches bandlimited tables as the
        # analogue drift moves pitch across a table boundary and the harmonic
        # count changes by one. That is a discontinuity in the measurement, not
        # movement, and treating it as movement would force the two commonest
        # waveforms onto the crossfade path for no musical reason.
        #
        # This classification is now REPORTING ONLY. It used to decide whether
        # the morph was inspected at all, which is how the basic waveforms went
        # unexamined for the whole life of this file.
        med = {m: float(np.median([cur[f"{h:.0f}"][m] for h in SWEEP_HZ]))
               for m in METRICS}
        kind = "static" if max(med.values()) < LIVE_TRAVEL else "LIVE"
        # THE MORPH CHECK NOW RUNS FOR EVERY KEY, live or static. It used to run
        # only for live ones, because a static key was "allowed to be flattened"
        # on the written grounds that "reduced to a static spectrum a saw is
        # still a saw". That sentence was false and it was load-bearing: saw,
        # square, triangle, pulse, bass_saw and sine -- the basic waveforms --
        # were exactly the keys the additive morph still swallowed, and this
        # branch is the reason the gate never once looked at them. A `continue`
        # justified by a claim about how something sounds is not a shortcut, it
        # is the claim going unchecked forever.
        want = idioms(orc)
        probs, _morc = morph_problems(M, k, orc, want)
        status = "OK" if not probs else "; ".join(probs)
        if label == "LIVE":
            print(f"{k:14s} {row}  {kind:6}  {status}")
        elif probs:
            # The key already printed a FELL / NOT BASELINED line above; say the
            # second defect out loud rather than letting it wait for the run
            # after the first one is fixed.
            print(f"{'':14s} {'':{len(row)}}  {'':6}  ...and {status}")
        if probs:
            structural.append((k, "; ".join(probs), set()))
    print()
    for label, entries in (("declared to have no control", NO_CONTROL),):
        for k, why in sorted(entries.items()):
            print(f"{label} -- {k}:")
            for ln in __import__("textwrap").wrap(why, 72):
                print(f"    {ln}")
            print()
    if update:
        # --update-baseline used to `return 0` here, BEFORE the failure check --
        # so it printed `FLATTENED to a partial bank` and exited 0, and
        # regenerating over a regression recorded the broken numbers as the new
        # truth. Both doors are now shut, and the second one needs an explicit
        # flag rather than a promise to read the diff.
        if structural:
            print(f"REFUSING to record: {len(structural)} structural failure(s)")
            for k, why, lost in structural:
                print(f"  {k}: {why}"
                      + (f" ({', '.join(sorted(lost))})" if lost else ""))
            return 1
        missing = sorted(set(techs) - set(measured))
        if missing:
            # Belt and braces on the above: a wholesale write of a partial dict
            # is what silently DELETED the reference of any key that was dead at
            # record time, after which it was only ever "not baselined".
            print(f"REFUSING to record: {len(missing)} key(s) unmeasured -- "
                  + ", ".join(missing))
            return 1
        if relative and not accept:
            print(f"REFUSING to record: {len(relative)} reading(s) below the "
                  f"existing reference. This is how a regression becomes the "
                  f"new truth. Re-run with --accept-regressions if the loss is "
                  f"intended, and say why in the commit body.")
            for k, why, _ in relative:
                print(f"  {k}: {why}")
            return 1
        if relative:
            print(f"--accept-regressions: recording {len(relative)} fallen "
                  f"reading(s) as the new reference.")
            for k, why, _ in relative:
                print(f"  {k}: {why}")
        all_base[str(SR)] = measured
        BASELINE_PATH.write_text(
            json.dumps(all_base, indent=1, sort_keys=True) + "\n")
        print(f"recorded {len(measured)} keys x {len(SWEEP_HZ)} pitches x "
              f"{len(METRICS)} metrics at {SR} Hz -> {BASELINE_PATH.name}")
        return 0
    failures = structural + relative
    if failures:
        print(f"FAIL: {len(failures)} problem(s)")
        for k, why, lost in failures:
            print(f"  {k}: {why}"
                  + (f" ({', '.join(sorted(lost))})" if lost else ""))
        print("\nA live key must take the crossfade path -- add it to _LIVE_TECH")
        print("in backend/csound_orch.py, or explain in code why a static")
        print("partial bank genuinely represents it. A reading below its")
        print("reference means a control has weakened; a still cell that has")
        print("started moving means one was invented.")
        return 1
    print("OK: every key that moves keeps its idiom, every control still runs,")
    print("and every cell the reference calls still is still still.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
