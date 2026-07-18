#!/usr/bin/env python3
"""DISTINCTNESS + MICRO-MOVEMENT gate — the two things the behavioural gate is blind to.

tools/csound_keys_gate.py certifies BEHAVIOUR (bounded, stands, decays when asked,
is-noise-when-declared). It has no notion of QUALITY, so two failures slipped past
138 green idiom cases and a 31-prompt frozen corpus (audit 2026-07-18):

  1. ALIASES — 41 technique keys emitted only 28 distinct DSPs. `sync` was a plain
     sawtooth with no sync, `supersaw` a single saw with no detune, `metallic_fm`
     byte-identical to `fm_bell`. Ten of them were even listed as "live-opcode
     idioms" in the idiom suite's ledger, so it reported 0 GAPS.
  2. DEAD MICRO-MOVEMENT — in the CSOUND-CODE paradigm movement comes out of the
     code wherever the PROMPT implies it (BJ 2026-07-18). "movement by default"
     was a WAVETABLE-era rule (a static frame must be scanned); here the rule is:
       * MICRO-movement is implied by ADJECTIVES — dirty / overdriven / analog
         name a sound that lives and breathes, not a frozen waveshape.
       * MACRO-movement is implied by the CHAIN — "a > b", motion keys.
     A static `tanh()` satisfies "dirty" structurally while being audibly dead.

This gate asserts both, mechanically:

  A) DISTINCTNESS: keys that name different DSP must EMIT differently (exact-alias
     check, free) and, for declared-distinct pairs, must MEASURE differently
     (spectral distance over Welch PSDs of the real renders).
  B) MICRO-MOVEMENT: an adjective that names a living quality must raise a
     liveliness score over the same source WITHOUT it. Liveliness = temporal
     variability of the spectrum (std over time of per-frame Welch PSDs) plus
     drive/amplitude variability -- measured with scipy.signal, not a hand-rolled
     FFT peak-picker.

Usage:  .venv/bin/python tools/csound_liveliness_gate.py
"""
import os
import sys
import subprocess
import tempfile

import numpy as np
from scipy import signal

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "backend"))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import csound_orch as co  # noqa
from csound_keys_gate import CHECK, _load, ensure_check  # noqa

NOTE_HZ = 220.0
NOTE_DUR = 4.0
TOTAL = 4.4

# Keys that name genuinely different DSP. Same-group keys MAY legitimately share an
# emission (a hand-maintained ledger, NOT derived from the code -- deriving it would
# auto-bless whatever the code happens to do, which is how the aliases hid).
# Anything listed DISTINCT must differ; the audit found all of these identical.
DISTINCT_TECH = [
    "saw", "supersaw", "sync", "brass", "strings", "bass_saw",
    "square", "pulse", "clarinet", "chiptune",
    "fm", "fm_ep", "fm_bell", "metallic_fm",
    "sine", "sub_sine", "theremin",
    "cymbal", "glass", "struck_bar", "organ", "flute", "harpsichord",
]

# Adjectives whose OWN lexicon `why` names a moving quality -> they must measurably
# out-live the dry source. Grounded in the lexicon text, NOT invented here:
#   dirty  :: "a grimy per-frame SCATTER — deterministic amplitude+phase jitter …,
#             NOT a static upper-mid tilt"
#   analog :: "slow coherent drift … amplitude wobble + a tiny analog micro-detune"
# Adjectives the lexicon defines as a STATIC spectral shape are deliberately NOT
# asserted here — `distorted` is "nonlinear soft-clip (tanh waveshaper)", i.e. a
# frozen waveshaper is the correct reading; buzzy/harsh/growling/aggressive are
# tilts. Asserting movement on those would invent a requirement the spec never made.
# adjective -> (signature the lexicon names, minimum gain over the dry source)
#   "flux"  = the spectrum keeps re-scattering (harmonic jitter)
#   "cents" = the fundamental wanders (micro-detune), in cents peak-to-peak
MICRO_MOVE_ADJ = {
    "dirty":  ("flux", 1.0),    # "grimy per-frame scatter … NOT a static tilt"
    "analog": ("cents", 3.0),   # "amplitude wobble + a tiny analog micro-detune"
}
MICRO_BASE = "saw"


def render(orc, wav, hz=NOTE_HZ):
    with tempfile.NamedTemporaryFile("w", suffix=".orc", delete=False) as f:
        f.write(orc)
        orp = f.name
    try:
        r = subprocess.run(
            [CHECK, "probe", orp, wav, str(hz), "0.85", "0.0", "0.5",
             str(NOTE_DUR), str(TOTAL)],
            capture_output=True, text=True, timeout=90)
        return r.returncode == 0
    finally:
        os.unlink(orp)


def _frames(x, sr, win=0.12, hop=0.06):
    """Per-frame Welch PSDs (scipy), normalized per frame -> spectral SHAPE over time."""
    w, h = int(win * sr), int(hop * sr)
    out = []
    for i in range(0, max(1, len(x) - w), h):
        seg = x[i:i + w]
        if len(seg) < w:
            break
        f, p = signal.welch(seg, sr, nperseg=min(1024, len(seg)))
        s = p.sum()
        out.append(p / s if s > 0 else p)
    return np.array(out) if out else np.zeros((1, 1))


def psd(x, sr):
    """One normalized Welch PSD of the steady part -> a spectral fingerprint."""
    seg = x[int(0.5 * sr):int(3.5 * sr)]
    f, p = signal.welch(seg, sr, nperseg=2048)
    s = p.sum()
    return f, (p / s if s > 0 else p)


def flatness(x, sr):
    """Spectral flatness (geometric/arithmetic mean of the magnitude spectrum).

    1.0 is white noise, a pitched oscillator sits near 0. This is the ONLY measure
    here that can see added noise: noise passes every other check in this file
    trivially -- it is maximally 'lively', it stands, it is bounded, and it never
    drifts in pitch. That is exactly how a `rand` layer sat inside `airy`,
    `breathy`, `shimmering`, `icy`, `washed_out` and the `flute` idiom itself
    until BJ heard it ("ein starkes pinkes rauschen ... 'airy' hat ZERO mit
    statischem Rauschen zu tun") -- five adjectives and a technique, none of which
    any gate could fail.
    """
    seg = x[int(0.4 * sr):int(1.4 * sr)]
    if len(seg) < 1024:
        return 0.0
    seg = seg * np.hanning(len(seg))
    mag = np.abs(np.fft.rfft(seg))
    frq = np.fft.rfftfreq(len(seg), 1.0 / sr)
    m = mag[(frq > 50) & (frq < 16000)] + 1e-12
    return float(np.exp(np.log(m).mean()) / m.mean())


def liveliness(x, sr):
    """How much the sound MOVES on its own, INDEPENDENT of how rich it is.

    Spectral FLUX between consecutive frames of the per-frame NORMALIZED PSD: each
    frame sums to 1, so the frame-to-frame L1 difference is a distance between
    distributions, bounded [0,2], and a spectrally rich but frozen sound scores as
    low as a plain one. (The first attempt used per-bin temporal STD, which merely
    counted active bins: it rated a dead sawtooth 4.07 and genuinely-moving pwm
    1.80 — measuring richness, not movement. Validated now against known cases:
    pwm/shimmer/modal must out-score static sine and static saw.)
    Plus amplitude-envelope modulation depth, so tremolo-type movement counts too.
    """
    fr = _frames(x, sr)
    if fr.shape[0] < 3:
        return 0.0
    flux = float(np.mean(np.abs(np.diff(fr, axis=0)).sum(axis=1)))
    env = np.abs(signal.hilbert(x[int(0.5 * sr):int(3.5 * sr)]))
    env = signal.decimate(env, 100, ftype="fir") if len(env) > 1000 else env
    amp_var = float(np.std(env) / max(np.mean(env), 1e-9))
    return flux * 100.0 + amp_var * 10.0


def measures(x, sr):
    """The three DISTINCT movement signatures, reported separately.

    A single scalar cannot gate them fairly: `dirty` scatters HARMONICS (flux) but
    holds pitch, `analog` micro-DETUNES (cents) but holds its spectrum. Summing
    them let one signature mask the other's absence — the lexicon names a specific
    quality per adjective, so the gate asserts that specific one."""
    fr = _frames(x, sr)
    flux = float(np.mean(np.abs(np.diff(fr, axis=0)).sum(axis=1))) * 100.0 if fr.shape[0] > 2 else 0.0
    env = np.abs(signal.hilbert(x[int(0.5 * sr):int(3.5 * sr)]))
    env = signal.decimate(env, 100, ftype="fir") if len(env) > 1000 else env
    amp = float(np.std(env) / max(np.mean(env), 1e-9)) * 10.0
    return {"flux": flux, "amp": amp, "cents": pitch_drift_cents(x, sr)}


def pitch_drift_cents(x, sr, f0=NOTE_HZ):
    """Peak-to-peak wander of the fundamental, in cents.

    A slow micro-DETUNE (what the lexicon names for `analog`) is invisible to the
    PSD-flux term: 0.6% of 220 Hz is a 1.3 Hz swing while a Welch bin is ~47 Hz
    wide. So track f0 itself with long, zero-padded frames and parabolic peak
    interpolation — this measures the named quality directly instead of hoping a
    coarse spectrogram notices it."""
    w, h = int(0.25 * sr), int(0.10 * sr)
    nfft = 1 << 16
    lo, hi = f0 * 0.75, f0 * 1.25
    est = []
    # STEADY part only: the note's attack/release edges give the peak-picker
    # low-energy frames whose estimate scatters, which read as ~13 cents of
    # phantom "drift" on a provably constant-pitch sawtooth.
    i0, i1 = int(1.0 * sr), min(len(x), int(3.2 * sr))
    for i in range(i0, max(i0 + 1, i1 - w), h):
        seg = x[i:i + w]
        if len(seg) < w:
            break
        mag = np.abs(np.fft.rfft(seg * np.hanning(w), n=nfft))
        freq = np.fft.rfftfreq(nfft, 1.0 / sr)
        band = np.where((freq >= lo) & (freq <= hi))[0]
        if len(band) < 3:
            continue
        k = band[int(np.argmax(mag[band]))]
        if k <= 0 or k >= len(mag) - 1 or mag[k] <= 0:
            continue
        a, b, c = mag[k - 1], mag[k], mag[k + 1]
        denom = (a - 2 * b + c)
        delta = 0.5 * (a - c) / denom if denom != 0 else 0.0
        est.append(float(freq[k] + delta * (freq[1] - freq[0])))
    if len(est) < 3:
        return 0.0
    est = np.array(est)
    # robust span (5th..95th pct), so one bad frame cannot manufacture drift
    hi_p, lo_p = np.percentile(est, 95), np.percentile(est, 5)
    return float(1200.0 * np.log2(hi_p / max(lo_p, 1e-9)))


def spectral_distance(p, q):
    """Symmetric L1 distance between two normalized PSDs (0 = identical)."""
    n = min(len(p), len(q))
    return float(np.abs(p[:n] - q[:n]).sum())


def main():
    ensure_check()
    tmp = tempfile.mkdtemp(prefix="liveliness_")
    fails = []

    # ---- A) exact-alias check (free, catches byte-identical emissions) ---------
    print("A) DISTINCTNESS — emission aliases among keys declared distinct")
    emis = {}
    for t in DISTINCT_TECH:
        try:
            emis[t] = co._emit_steady(t, "0")
        except Exception as e:
            fails.append(f"emit failed for {t}: {e}")
    groups = {}
    for t, body in emis.items():
        groups.setdefault(body, []).append(t)
    aliases = [ks for ks in groups.values() if len(ks) > 1]
    for ks in aliases:
        fails.append(f"ALIAS: {', '.join(ks)} emit byte-identical DSP but name different things")
        print(f"   FAIL alias: {', '.join(ks)}")
    print(f"   {len(DISTINCT_TECH)} keys -> {len(groups)} distinct emissions "
          f"({len(aliases)} alias group(s))")

    # ---- B) measured distinctness of the renders ------------------------------
    print("\nB) DISTINCTNESS — measured spectral distance (renders)")
    prints = {}
    for t in sorted(emis):
        orc, _ = co.build_orchestra([t], [], None)
        wav = os.path.join(tmp, f"t_{t}.wav")
        if not render(orc, wav):
            fails.append(f"render failed: {t}")
            continue
        x, sr = _load(wav)
        _, p = psd(x, sr)
        prints[t] = p
    keys = sorted(prints)
    near = []
    for i, a in enumerate(keys):
        for b in keys[i + 1:]:
            d = spectral_distance(prints[a], prints[b])
            if d < 0.05:
                near.append((a, b, d))
    for a, b, d in near:
        fails.append(f"NEAR-IDENTICAL: {a} vs {b} spectral distance {d:.4f} (<0.05)")
        print(f"   FAIL {a:12} vs {b:12} distance {d:.4f}")
    print(f"   {len(keys)} rendered, {len(near)} near-identical pair(s)")

    # ---- C) micro-movement from adjectives ------------------------------------
    print("\nC) MICRO-MOVEMENT — adjectives that name a living quality")
    orc, _ = co.build_orchestra([MICRO_BASE], [], None)
    wav = os.path.join(tmp, "dry.wav")
    base = {"flux": 0.0, "amp": 0.0, "cents": 0.0}
    if render(orc, wav):
        x, sr = _load(wav)
        base = measures(x, sr)
    print(f"   dry {MICRO_BASE}: flux {base['flux']:.2f}  amp {base['amp']:.2f}  "
          f"drift {base['cents']:.1f} cents")
    for a, (sig, need) in MICRO_MOVE_ADJ.items():
        orc, _ = co.build_orchestra([MICRO_BASE], [a], None)
        wav = os.path.join(tmp, f"a_{a}.wav")
        if not render(orc, wav):
            fails.append(f"render failed: {MICRO_BASE}+{a}")
            continue
        x, sr = _load(wav)
        m = measures(x, sr)
        gain = m[sig] - base[sig]
        ok = gain >= need
        print(f"   {'ok  ' if ok else 'FAIL'} {a:8} {sig:6} {m[sig]:7.2f} "
              f"(dry {base[sig]:6.2f}, +{gain:6.2f}, need +{need})   "
              f"[flux {m['flux']:.1f} amp {m['amp']:.1f} drift {m['cents']:.1f}c]")
        if not ok:
            fails.append(f"DEAD: adjective '{a}' names a moving quality ({sig}) but "
                         f"adds none: {m[sig]:.2f} vs dry {base[sig]:.2f} "
                         f"(need +{need})")

    # ---------------------------------------------------------------- D) noise
    # The oscillator may NEVER add static noise. The synth owns a noise module,
    # env-controllable and switchable; an oscillator that bakes hiss in takes that
    # away from the player, exactly as owning the envelope or the glide would.
    # Only keys that ARE noise may measure as noise.
    print("\nD) NO ADDED NOISE — the synth owns the noise module, not the oscillator")
    # Flatness alone cannot separate "legitimately dense" from "noise bolted on":
    # a cymbal SHOULD measure dense. So the assertion is split, and each half is
    # something this measure can actually decide.
    #
    # D1 -- ADJECTIVES, asserted as a DELTA over the same dry source. An adjective
    # is a modifier; whatever the source was, a timbral word must not turn it into
    # noise. This is exactly the class BJ reported: airy took a dry supersaw from
    # 0.007 to 0.469. A delta cannot be fooled by a dense base.
    # The limit sits in a 6x gap, not near either edge. Legitimate MODULATION
    # smears the spectrum a little -- `analog` wanders a fractional delay (+0.050)
    # and `dirty` jitters a waveshaper's drive (+0.043); noise as a modulation
    # SOURCE is fine, it is noise as an audible LAYER that is banned. The actual
    # violations measured +0.33 to +0.49 (airy, breathy, shimmering, icy,
    # washed_out). 0.10 is 2x above the honest modulation and 3x below the
    # cheapest violation.
    ADJ_DELTA = 0.10
    dryw = os.path.join(tmp, "noise_dry_saw.wav")
    orc, _ = co.build_orchestra(["saw"], [], None)
    render(orc, dryw)
    xd, srd = _load(dryw)
    dry_fl = flatness(xd, srd)
    worst = []
    for a in sorted(co._ADJ_MAP):
        orc, _ = co.build_orchestra(["saw"], [a], None)
        wav = os.path.join(tmp, f"noise_adj_{a}.wav")
        if not render(orc, wav):
            continue
        x, sr = _load(wav)
        d = flatness(x, sr) - dry_fl
        worst.append((d, a))
        if d > ADJ_DELTA:
            fails.append(f"NOISE: adjective '{a}' raises spectral flatness by "
                         f"{d:.3f} over the dry source (limit {ADJ_DELTA}) -- it is "
                         f"adding noise the prompt never asked for")
    worst.sort(reverse=True)
    print(f"   D1 {len(worst)} adjectives vs dry saw {dry_fl:.3f}, max delta "
          f"{ADJ_DELTA}. worst: " + ", ".join(f"{a} +{d:.3f}" for d, a in worst[:4]))

    # D2 -- TECHNIQUES that must be TONAL, as a hand-maintained ledger. Deriving
    # this from the code would auto-bless whatever the code happens to do, which is
    # how the aliases hid (see the DISTINCT ledger above). A key is listed here
    # because the INSTRUMENT it names is a pitched, non-noisy sound.
    TONAL = {"sine", "saw", "square", "pulse", "triangle", "pwm", "additive",
             "organ", "clarinet", "flute", "brass", "strings", "bass_saw",
             "supersaw", "harpsichord", "theremin", "sub_sine",
             "fm", "fm_bell", "fm_ep", "metallic_fm", "cheby", "ring_mod"}
    TONAL_LIMIT = 0.10
    dense = []
    for t in sorted(TONAL):
        orc, _ = co.build_orchestra([t], [], None)
        wav = os.path.join(tmp, f"noise_tech_{t}.wav")
        if not render(orc, wav):
            continue
        x, sr = _load(wav)
        fl = flatness(x, sr)
        dense.append((fl, t))
        if fl > TONAL_LIMIT:
            fails.append(f"NOISE: '{t}' names a pitched instrument but measures "
                         f"spectral flatness {fl:.3f} (limit {TONAL_LIMIT})")
    dense.sort(reverse=True)
    print(f"   D2 {len(dense)} tonal techniques, limit {TONAL_LIMIT}. noisiest: " +
          ", ".join(f"{t} {f:.3f}" for f, t in dense[:4]))

    # Reported, deliberately NOT asserted: the keys whose density is either the
    # point (the modal metals ring high-Q resonators, and a cymbal that measured
    # tonal would be wrong) or a defect this gate found and cannot yet fix.
    # `sync` measures ~0.50 at EVERY pitch against a band-limited saw's 0.002 --
    # its raw syncphasor is not band-limited, so that is aliasing, not timbre.
    # `chiptune` sits at 0.25-0.35. Both are pre-existing and tracked separately;
    # they are printed rather than silently exempted so nobody reads their absence
    # as a pass.
    print("   -- reported, not asserted (tracked): ", end="")
    for t in ("sync", "chiptune", "cymbal", "glass", "struck_bar"):
        orc, _ = co.build_orchestra([t], [], None)
        wav = os.path.join(tmp, f"noise_watch_{t}.wav")
        if render(orc, wav):
            x, sr = _load(wav)
            print(f"{t} {flatness(x, sr):.3f}  ", end="")
    print()

    print("\n" + "=" * 72)
    if fails:
        print(f"LIVELINESS GATE: {len(fails)} FAILURE(S)")
        for f in fails:
            print("  - " + f)
        return 1
    print("LIVELINESS GATE: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
