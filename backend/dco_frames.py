#!/usr/bin/env python3
"""dco_frames — the fork-INDEPENDENT 256-frame wavetable engine.

The LLM understands the prompt and picks lexicon KEYS; dco_recipe's closed forms
turn those keys into an EXACT spectrum (verified against a brute-force DFT of the
C++ DcoBaker to ~1e-15, dco_recipe.py:1091-1093). This module takes the authored
recipe and COMPUTES up to 256 explicit single-cycle wavetable frames by Fourier
math: each frame is one 2048-sample single cycle synthesized from its OWN
per-frame spectrum, and MOVEMENT is that per-frame spectrum EVOLVING across the
frames. There is NO faking movement by handing 3-5 sparse "station" keyframes to
an interpolator (the rejected design) — every frame here is an explicit spectrum.

Movement mechanisms living here: harmonic dark<->bright ramp / integer-ratio FM
index sweep / character+texture on any harmonic carrier. An inharmonic base
(bell/metal/glass, non-integer partial ratios) is rendered as OPTION A — the
dense-harmonic 256-frame APPROXIMATION (Vital's actual method): each non-integer
partial at ratio r is mapped onto the integer harmonic grid as a dense local
CLUSTER whose amplitude-weighted centroid is pinned at r, evolved per frame
(_inharmonic_approx_spectra). This is the harmonic-grid approximation, NOT true
off-grid partials: a looped single cycle can only hold integer harmonics of the
played f0, so a bell partial at 2.76*f0 is not exactly representable. Option B (the
real-time additive/"stations" engine, unchanged) renders the true non-integer
partials; A vs B is the ear-gate the user chooses between. (A non-integer FM
*ratio* on the wavetable-FM path is a separate, narrow case that a single cycle
still cannot hold, and it stays fork-pending: bake_frames raises NotImplementedError
there, internal and never user-facing.)

Bake contract (mirrors src/dsp/DcoBaker.cpp, verified above): a frame is
irfft(2048) of a half-spectrum whose bin k (integer harmonic) carries
(N/2)*a*exp(i(phase - pi/2)) — the (2/N) irfft scaling then yields exactly
a*sin(k*theta + phase). Each frame subtracts its own mean (removeDC), and ONE
global peak normalization runs across ALL frames (peak -> 0.95; 0.83 when any
frame was waveshaped), never a per-frame renorm (so a tremolo's per-frame gain
survives into the wire, exactly as the C++ setExactFrames path does).

Reuses dco_recipe's exact spectra (never hand-rolls a new one):
  _station_partials / _saw_sc … _ring_sc / fm_spectrum / _opposite_endpoint.
Deterministic throughout: golden-angle and index-derived offsets only, no RNG
(double-run byte-identical), matching dco_recipe's character-pass convention.
"""
import math

import numpy as np

import dco_recipe

N_SAMPLES = 2048          # single-cycle length (DcoBaker kCycleLen)
MAX_FRAMES = 256          # DcoBaker frame ceiling
_GOLDEN = 2.399963229728653   # radians (golden angle) — decorrelates successive
                              # partial indices so a per-index perturbation never
                              # reads as one global tremolo (mirrors dco_recipe._GOLDEN)


# ─── movement/texture shaping helpers (all loop-seamless over F) ──────────────

def _brightness(f, F):
    """Loop-seamless brightness envelope b(f) = 0.5*(1 - cos(2*pi*f/F)): 0 at
    f=0, 1 at f=F/2, back to ~0 at f=F-1 — one 'breath' per table scan, and
    frame F-1 -> frame 0 wraps click-free (both ~0). Used both for the
    movement-by-default dark<->bright ramp and, at integer multiples, for the
    periodic textures below."""
    return 0.5 * (1.0 - math.cos(2.0 * math.pi * f / F))


def _amp_phase_maps(partials):
    """[{h,a,phase}] -> (amp_by_h, phase_by_h) keyed on rounded h (6 dp, the same
    coincidence key dco_recipe uses). A partial absent from one endpoint reads as
    amplitude 0 there (union alignment, dco_recipe spec sec.4)."""
    a, ph = {}, {}
    for p in partials:
        h = round(float(p["h"]), 6)
        a[h] = float(p["a"])
        ph[h] = float(p.get("phase", 0.0))
    return a, ph


def _union_h(*partial_lists):
    """Sorted union of the h values across several partial lists (rounded 6 dp)."""
    seen = {}
    for pl in partial_lists:
        for p in pl:
            seen[round(float(p["h"]), 6)] = True
    return sorted(seen)


def _base_partials(base_kf):
    """One base keyframe -> its EXACT partials via dco_recipe's closed forms
    (_saw_sc/_square_sc/_triangle_sc/_pulse_sc/_cheby_sc/_ring_sc, or the literal
    additive partials). fm2 is handled separately (per-frame index sweep), never
    here."""
    return dco_recipe._station_partials(base_kf)


def _is_inharmonic(partials):
    return any(abs(float(p["h"]) - round(float(p["h"]))) > 1.0e-3 for p in partials)


# ─── per-frame spectrum sources (point 2 of the engine spec) ──────────────────

def _harmonic_movement_spectra(base_partials, movement, F):
    """The undirected harmonic movement: a dark<->bright ramp evaluated
    CONTINUOUSLY per frame (not as two baked corners). dco_recipe._opposite_endpoint
    supplies the ear-VALIDATED bright endpoint — an ABSOLUTE amplitude ramp over the
    h-ranked partials (plus, for a sparse set, a synthesized upward series extension
    that is silent at the dark end), the migration a multiplicative tilt was judged
    too weak to make (dco_recipe.py:970-975). Frame f blends base<->bright by
    b(f): at b=0 the clean base timbre, at b=1 the bloomed bright endpoint, so the
    saw stays a saw at home and genuinely migrates energy upward at the peak. A
    'static' movement returns the base spectrum unchanged on every frame (the base
    for a texture layer, or an explicit 'static' order)."""
    base_partials = [dict(p) for p in base_partials]
    if movement != "brightness":
        return [[dict(p) for p in base_partials] for _ in range(F)]

    bright = dco_recipe._opposite_endpoint(base_partials, True)
    base_a, base_ph = _amp_phase_maps(base_partials)
    bright_a, bright_ph = _amp_phase_maps(bright)
    hs = _union_h(base_partials, bright)
    specs = []
    for f in range(F):
        b = _brightness(f, F)
        row = []
        for h in hs:
            a = (1.0 - b) * base_a.get(h, 0.0) + b * bright_a.get(h, 0.0)
            if a <= 0.0:
                continue
            row.append({"h": h, "a": a, "phase": base_ph.get(h, bright_ph.get(h, 0.0))})
        specs.append(row)
    return specs


# ─── OPTION A: dense-harmonic approximation of an INHARMONIC base ──────────────
# The harmonic-grid approximation of a bell/metal/glass spectrum (Vital's actual
# method). HONEST: a looped single-cycle frame holds only integer harmonics of the
# played f0, so a true partial at ratio r (e.g. 2.76) is NOT exactly representable.
# Each non-integer partial is mapped onto the integer grid as a dense local CLUSTER
# (nearest-harmonic linear-interpolation pair carrying the bulk, plus a small
# centroid-neutral skirt) with the partial's PITCH (amplitude-weighted centroid)
# and LOUDNESS (energy) both preserved; the "slow beating that reads as inharmonic
# shimmer" is realized as per-frame PHASE evolution of the skirt bins (a single-f0
# grid cannot detune two bins a few Hz apart — the scan is the only time axis) and,
# where dense clusters overlap, as genuine inter-partial beating in the shared bins.
# This is NOT true off-grid partials (that is Option B, the real-time additive
# engine). Deterministic (golden-angle only, no RNG).
#
# MOVEMENT-BY-DEFAULT (fixed 2026-07-15): the per-frame bloom is the SAME
# ear-VALIDATED gesture Option B and the harmonic path (_harmonic_movement_spectra)
# use — dco_recipe._opposite_endpoint's ABSOLUTE amplitude ramp over the h-ranked
# partials PLUS a synthesized upward series EXTENSION silent at the dark end —
# evaluated continuously per frame by blending the base<->bright INHARMONIC spectrum
# BEFORE the grid map. The earlier weak multiplicative in-place tilt (_INHARM_BLOOM,
# removed) swept the centroid ~100x too little (a bell moved ~16 Hz, not hundreds);
# this is the migration dco_recipe.py:970-975 says a tilt is too weak to make.
_INHARM_SKIRT = 0.18          # skirt weight as a fraction of the main-pair weight (small)
_INHARM_PHASE_DRIFT = 1.0     # skirt phase turns across one F-frame scan (loop-seamless shimmer)
_INHARM_SHIMMER = 0.10        # per-frame skirt amplitude wobble depth (frames genuinely differ)


def _cluster_bins(r, a):
    """One inharmonic partial (ratio r, amplitude a) -> [[k, weight, kind], ...] on
    the integer grid. The nearest-harmonic linear-interpolation pair (floor(r),
    ceil(r)) carries the bulk with the amplitude-weighted centroid EXACTLY at r
    (so the partial's perceived frequency survives); a small skirt one bin outside
    each side (r-1, r+1) is added centroid-NEUTRAL (weights balanced so their first
    moments about r cancel), the shimmer material. An already-integer r (e.g. h=1)
    is a single exact bin, no skirt. Finally the whole cluster is L2 (energy)
    normalized so sum(w^2)==a^2 — a uniform scale that preserves BOTH the centroid
    and the linear-interp weight ratios while matching the time-domain RMS of one
    partial of amplitude a (distinct bins => RMS^2 = sum(w^2)/2). So pitch AND
    loudness both survive the grid map; only 'one line -> a few bins' is lost — the
    irreducible grid artifact this whole path exists to measure."""
    kf = int(math.floor(r))
    kc = int(math.ceil(r))
    out = []   # [k, weight, kind]
    if kf == kc:
        out.append([kf, 1.0, "main"])                    # r already integer -> exact
    else:
        frac = r - kf
        out.append([kf, 1.0 - frac, "main"])
        out.append([kc, frac, "main"])
        dL = r - (kf - 1)
        dR = (kc + 1) - r
        if (kf - 1) >= 1 and dL > 0.0 and dR > 0.0:
            # balanced skirt: wL*dL == wR*dR => net moment about r is zero => the
            # centroid stays exactly r. Both sides added together or not at all (a
            # one-sided skirt would pull the centroid off r).
            wL = _INHARM_SKIRT * dR / (dL + dR)
            wR = _INHARM_SKIRT * dL / (dL + dR)
            out.append([kf - 1, wL, "skirt"])
            out.append([kc + 1, wR, "skirt"])
    ss = math.sqrt(sum(w * w for _k, w, _t in out)) or 1.0
    s = abs(float(a)) / ss
    for e in out:
        e[1] *= s
    return out


def _inharmonic_approx_spectra(base_partials, movement, F):
    """Inharmonic base -> F per-frame spectra on the integer grid (Option A). Each
    target partial becomes a dense cluster (_cluster_bins); across the F frames the
    spectrum EVOLVES by real per-frame computation (movement-by-default). The
    movement is the SAME ear-VALIDATED gesture Option B and the harmonic path
    (_harmonic_movement_spectra) use: dco_recipe._opposite_endpoint supplies the
    bright endpoint — an ABSOLUTE amplitude ramp over the h-ranked partials PLUS,
    for a sparse set, a synthesized upward series EXTENSION that is silent at the
    dark end — and frame f blends the base<->bright INHARMONIC spectrum by b(f)
    BEFORE the grid map, so the bell/metal/cymbal genuinely migrates energy upward
    (and grows new high partials) with the same DEPTH as B, NOT the ~16 Hz twitch a
    multiplicative in-place tilt gave (dco_recipe.py:970-975). Each blended frame's
    inharmonic partials are then clustered onto the integer grid; ON TOP of the
    bloom, each skirt bin's phase rotates one loop-seamless turn per scan (the
    inharmonic shimmer a single-f0 grid cannot otherwise produce) with a small
    golden-angle amplitude wobble so consecutive frames genuinely differ. An
    explicit 'static' movement freezes at the base spectrum (b=0 every frame — no
    bloom, no drift) -> a still dense-harmonic approximation. Multiple partials whose
    clusters land on the same integer bin are summed as complex phasors by
    bake_frames, so overlapping (dense cymbal) clusters beat honestly. Deterministic
    (golden-angle only, no RNG)."""
    moving = (movement == "brightness")
    base_a, base_ph = _amp_phase_maps(base_partials)
    if moving:
        # the ear-validated bright endpoint (absolute ramp + high-partial series
        # extension) — the SAME closed form the harmonic path and Option B use.
        bright = dco_recipe._opposite_endpoint(base_partials, True)
        bright_a, bright_ph = _amp_phase_maps(bright)
        ratios = _union_h(base_partials, bright)
    else:
        bright_a, bright_ph = {}, {}
        ratios = _union_h(base_partials)

    specs = []
    for f in range(F):
        b = _brightness(f, F) if moving else 0.0
        drift = _INHARM_PHASE_DRIFT * 2.0 * math.pi * f / F if moving else 0.0
        row = []
        for r_idx, r in enumerate(ratios):
            # per-frame INHARMONIC amplitude: the base<->bright migration by b(f),
            # the deep ear-validated gesture (a fading low + a blooming/extending
            # high) — NOT a weak in-place re-weighting. THEN map onto the grid.
            a = (1.0 - b) * base_a.get(r, 0.0) + b * bright_a.get(r, 0.0)
            if a <= 0.0:
                continue
            ph = base_ph.get(r, bright_ph.get(r, 0.0))
            for k, w, kind in _cluster_bins(r, a):
                if k < 1:
                    continue
                amp = w
                phase = ph
                if moving and kind == "skirt":
                    phase = ph + drift + _GOLDEN * r_idx
                    amp *= (1.0 + _INHARM_SHIMMER * math.sin(drift + _GOLDEN * (r_idx + k)))
                if amp <= 0.0:
                    continue
                row.append({"h": float(k), "a": amp, "phase": phase})
        specs.append(row)
    return specs


def _fm_index_sweep_spectra(ratio, index_sweep, F):
    """The star mechanism: integer-ratio 2-op FM whose modulation index is swept
    across the frames, index(f) = I0 + (I1-I0)*b(f). An INTEGER ratio is exactly
    periodic, so every frame is a clean single cycle; the Bessel sidebands
    genuinely BLOOM frame to frame (dco_recipe.fm_spectrum recomputes the exact
    J_k(index) expansion each frame) — REAL computed movement, not a two-corner
    interpolation. A non-integer ratio would be inharmonic (fork-pending) and is
    rejected by the caller before we get here."""
    r = int(round(ratio))
    I0, I1 = float(index_sweep[0]), float(index_sweep[1])
    specs = []
    for f in range(F):
        idx = I0 + (I1 - I0) * _brightness(f, F)
        specs.append([dict(p) for p in dco_recipe.fm_spectrum(r, idx)])
    return specs


# ─── textures as PER-FRAME variation (point 4 — reclassified from motion) ─────
# vibrate / wobble / flutter used to be MOTION intents routed to scan trajectories
# (dco_recipe._MOTION_REWRITE/_motion_periodic); that was the wrong bucket — they
# are per-frame TEXTURE, a periodic timbral variation with an INTEGER number of
# cycles over F (loop-seamless). tremolo and shimmer join them here. Each is
# audible precisely because the global normalization is once-across-all-frames,
# never per-frame (the C++ setExactFrames wire path does no per-frame renorm).

def _apply_texture(spec, f, F, texture):
    """One frame's spectrum -> the same spectrum with the texture's periodic
    per-frame variation applied. n_cycles is an integer so the modulation closes
    the loop. Deterministic (golden-angle / cosine only)."""
    name = texture["name"]
    n = int(texture.get("n_cycles", 4))
    depth = float(texture.get("depth", 0.5))
    theta = 2.0 * math.pi * n * f / F        # integer cycles over F -> loop-seamless
    n_p = len(spec)

    if name == "tremolo":
        # periodic per-frame GAIN modulation (a scalar on every partial). Audible
        # because setExactFrames does no per-frame renorm — the amplitude wobble
        # survives into the wire. gain=1 at f=0, dips by `depth`.
        g = 1.0 - depth * 0.5 * (1.0 - math.cos(theta))
        return [{"h": p["h"], "a": p["a"] * g, "phase": p["phase"]} for p in spec]

    if name in ("vibrate", "flutter"):
        # A true PITCH vibrato is not single-cycle-representable (one 2048-sample
        # cycle is pitch-agnostic — pitch is the playback rate, not the table), so
        # 'vibrate'/'flutter' are realized as a periodic TIMBRAL shimmer: the
        # spectral centroid wobbles bright<->dark n times per loop. Honest, and no
        # "not understood" — a shiver you can hear, on the axis a wavetable owns.
        beta = 0.5 * (1.0 - math.cos(theta))                 # 0..1 periodic
        out = []
        for i, p in enumerate(spec):
            t = i / (n_p - 1) if n_p > 1 else 0.0
            tilt = 1.0 + depth * beta * (2.0 * t - 1.0)      # lows down, highs up at the peak
            a = p["a"] * tilt
            out.append({"h": p["h"], "a": a if a > 0.0 else 0.0, "phase": p["phase"]})
        return out

    if name == "wobble":
        # periodic spectral-TILT sweep: the low<->high balance rocks back and forth
        # (cos spans -1..+1, so it tilts both ways around the base, unlike the
        # one-sided vibrate shimmer).
        s = math.cos(theta)
        out = []
        for i, p in enumerate(spec):
            t = i / (n_p - 1) if n_p > 1 else 0.0
            tilt = 1.0 + depth * s * (2.0 * t - 1.0)
            a = p["a"] * tilt
            out.append({"h": p["h"], "a": a if a > 0.0 else 0.0, "phase": p["phase"]})
        return out

    if name == "shimmer":
        # periodic HIGH-partial sparkle, golden-angle decorrelated per partial so
        # the top of the spectrum glitters instead of pulsing in lockstep.
        out = []
        for i, p in enumerate(spec):
            t = i / (n_p - 1) if n_p > 1 else 0.0
            hi = t * t                                        # weight toward the top
            spk = 1.0 + depth * hi * math.sin(theta + _GOLDEN * i)
            a = p["a"] * spk
            out.append({"h": p["h"], "a": a if a > 0.0 else 0.0, "phase": p["phase"]})
        return out

    return spec   # unknown texture name -> untouched (defensive; never reached)


# ─── character passes as PER-FRAME variation (point 3 — ported from dco_recipe) ─
# dco_recipe's _pass_dirty / _pass_analog / _waveshape_harmonic run over the
# discrete station chain; here the SAME deterministic transforms run over the F
# frames (station index s_idx -> frame index f). Constants and golden-angle math
# are carried verbatim so a charactered frame table reads like the shipping
# station chain, one bucket finer.

def _frame_dirty(specs, params):
    """dirty/gritty: per-frame partial amp+phase jitter that varies per FRAME and
    partial (dco_recipe._pass_dirty with s_idx -> f). Golden angle decorrelates
    partials; the frame index makes neighbours differ."""
    amp_j = float(params.get("amp_jitter", 0.12))
    ph_j = float(params.get("phase_jitter", 0.14))
    out = []
    for f, sp in enumerate(specs):
        row = []
        for i, p in enumerate(sp):
            j_a = math.sin(_GOLDEN * (i + 1) + 1.2341 * f + 0.7)
            j_p = math.sin(_GOLDEN * (i + 1) * 0.5 + 2.3299 * f + 1.9)
            row.append({"h": p["h"], "a": float(p["a"]) * (1.0 + amp_j * j_a),
                        "phase": float(p["phase"]) + ph_j * j_p})
        out.append(row)
    return out


def _frame_analog(specs, params):
    """analog: slow COHERENT drift across the whole frame table (dco_recipe._pass_analog,
    harmonic branch — no h-detune, an integer grid must stay integer). Amplitude
    wobble + phase drift on a smooth 2*pi cycle across the frames (loop-closed:
    f=0 and f=F-1 coincide), golden-angle offset per partial index."""
    wob = float(params.get("amp_wobble", 0.08))
    ph_drift = float(params.get("phase_drift", 0.10))
    F = len(specs)
    denom = (F - 1) if F > 1 else 1
    out = []
    for f, sp in enumerate(specs):
        theta = 2.0 * math.pi * f / denom
        row = [{"h": p["h"], "a": float(p["a"]) * (1.0 + wob * math.sin(theta + _GOLDEN * i)),
                "phase": float(p["phase"]) + ph_drift * math.sin(theta + 1.7 * i + 0.5)}
               for i, p in enumerate(sp)]
        out.append(row)
    return out


def _waveshape_cycle(cyc, drive):
    """distorted/overdriven: per-frame asymmetric tanh waveshaping applied to the
    frame CYCLE after irfft (time domain). gain = 1 + 4*drive, bias = 0.2*drive —
    the exact soft-clip dco_recipe._waveshape_harmonic (and DcoBaker::applyShape)
    render. Peak-normalized first so `drive` means the same regardless of level,
    then DC removed. NOTE: dco_recipe realizes overdrive in the SPECTRAL domain
    (reconstruct -> tanh -> project back to the integer grid) so its sets/baked
    wire stays harmonic; here the frame is already a time-domain cycle, so the same
    nonlinearity is applied directly to it (the engine-spec instruction), which is
    equivalent up to the projection the shipping path needs and this path does
    not."""
    peak = float(np.max(np.abs(cyc))) or 1.0
    gain = 1.0 + drive * 4.0
    bias = 0.2 * drive
    y = np.tanh(gain * (cyc / peak + bias))
    return y - y.mean()


# ─── plan assembly + the bake ─────────────────────────────────────────────────

def _frame_spectra(plan, F):
    """recipe frame-plan -> (list of F per-frame spectra, waveshape_flag, drive).
    Order of layers: base movement (harmonic ramp | FM index sweep | static) ->
    texture (periodic per-frame) -> spectral character (dirty, analog). Overdrive
    is a TIME-domain pass (returned as a flag) applied after irfft in bake_frames.
    An inharmonic base (non-integer additive partials — bell/metal/glass) routes to
    the Option-A dense-harmonic approximation (_inharmonic_approx_spectra); only a
    non-integer FM *ratio* still raises NotImplementedError (fork-pending)."""
    base = plan["base"]
    kind = base.get("kind")
    movement = plan.get("movement", "brightness")
    character = list(plan.get("character") or [])
    cparams = plan.get("character_params") or {}
    texture = plan.get("texture")

    if kind == "fm2":
        ratio = float(base.get("ratio", 2))
        if abs(ratio - round(ratio)) > 1.0e-6:
            raise NotImplementedError(
                f"dco_frames: non-integer FM ratio {ratio} is inharmonic — "
                "fork-pending (inharmonic routing not built)")
        base_index = float(base.get("index", 1.5))
        # Index sweep endpoints: near-sine (I0) up to a bright bloom (I1), clamped
        # to fm_spectrum's [0, 8] domain. Derived from the recipe's own index so a
        # brighter base blooms further.
        sweep = plan.get("index_sweep")
        if not sweep:
            I1 = min(8.0, max(2.0, base_index * 4.0))
            sweep = [0.2, I1]
        specs = _fm_index_sweep_spectra(ratio, sweep, F)
    else:
        base_partials = _base_partials(base)
        if _is_inharmonic(base_partials):
            # OPTION A: dense-harmonic 256-frame approximation of the inharmonic
            # bell/metal/glass spectrum (harmonic-grid, NOT true off-grid partials —
            # see the module docstring + _inharmonic_approx_spectra).
            specs = _inharmonic_approx_spectra(base_partials, movement, F)
        else:
            specs = _harmonic_movement_spectra(base_partials, movement, F)

    if texture:
        specs = [_apply_texture(sp, f, F, texture) for f, sp in enumerate(specs)]

    # spectral character (dirty scatter, then the slow coherent analog layer)
    if "dirty" in character:
        specs = _frame_dirty(specs, cparams.get("dirty", {}))
    if "analog" in character:
        specs = _frame_analog(specs, cparams.get("analog", {}))

    waveshape = "overdriven" in character
    drive = float((cparams.get("overdriven") or {}).get("drive", 0.6))
    return specs, waveshape, drive


def _derive_plan(recipe):
    """Fallback plan for a bare recipe with no explicit ``frame_engine``: base =
    keyframes[0], movement = FM index sweep for an fm2 base else the dark<->bright
    default, texture carried if the recipe self-describes one. No character (the
    adjective->pass mapping lives in the author response, not the bare recipe)."""
    kfs = recipe.get("keyframes") or []
    base = dict(kfs[0]) if kfs else {"kind": "saw"}
    movement = "fm_index" if base.get("kind") == "fm2" else "brightness"
    return {"base": base, "movement": movement, "character": [],
            "character_params": {}, "texture": recipe.get("texture")}


def bake_frames(recipe):
    """recipe -> np.ndarray (F, 2048) float32, F <= 256. Each row is one explicit
    single-cycle wavetable frame synthesized by irfft of its OWN per-frame
    half-spectrum; movement is that spectrum evolving across the rows. Reads
    recipe['frame_engine'] when present (see plan_from_response) else derives a
    plan from the recipe's keyframes. An inharmonic base (non-integer additive
    partials — bell/metal/glass) is rendered as the Option-A dense-harmonic
    approximation; only a non-integer FM *ratio* still raises NotImplementedError
    (internal, not user-facing — that narrow wavetable-FM case is fork-pending)."""
    plan = recipe.get("frame_engine") or _derive_plan(recipe)
    F = int(recipe.get("frames", MAX_FRAMES) or MAX_FRAMES)
    F = max(1, min(MAX_FRAMES, F))
    specs, waveshape, drive = _frame_spectra(plan, F)

    N = N_SAMPLES
    nyq = N // 2
    frames = np.zeros((F, N), dtype=np.float64)
    for f in range(F):
        half = np.zeros(nyq + 1, dtype=np.complex128)
        for p in specs[f]:
            k = int(round(float(p["h"])))
            if k < 1 or k > nyq:
                continue   # DC and above-Nyquist partials dropped (removeDC / alias-safe)
            half[k] += (N / 2.0) * float(p["a"]) * np.exp(1j * (float(p["phase"]) - math.pi / 2.0))
        cyc = np.fft.irfft(half, N)
        cyc = cyc - cyc.mean()                 # removeDC (DcoBaker contract)
        if waveshape:
            cyc = _waveshape_cycle(cyc, drive)
        frames[f] = cyc

    # ONE global peak normalization across ALL frames (never per-frame — a
    # per-frame renorm would erase tremolo). 0.95, or 0.83 when any frame was
    # waveshaped, mirroring DcoBaker::bake.
    peak = float(np.max(np.abs(frames)))
    if peak > 0.0:
        target = 0.83 if waveshape else 0.95
        frames *= target / peak
    return frames.astype(np.float32)


# ─── bridge: author response -> a recipe carrying an explicit frame plan ──────

def plan_from_response(resp):
    """Full author response (build_lco_response: {recipe, resolved, ...}) -> a COPY
    of the recipe carrying an explicit ``frame_engine`` plan the new baker reads.
    Does NOT mutate the shipping recipe (the C++ wire + its byte-identical test
    gate stay untouched): the plan is metadata bolted onto a copy.

    Mapping: base spectrum from the resolved TECHNIQUE's lexicon template (the
    clean closed-form base, before movement-by-default fanned it into drift
    keyframes); character passes from resolved['passes']; texture from
    resolved['textures'] (the reclassified vibrate/wobble/flutter/tremolo/shimmer);
    movement = FM index sweep for an fm2 base, else static under a texture or an
    explicit 'static' order, else the dark<->bright default."""
    recipe = resp.get("recipe") or {}
    resolved = resp.get("resolved") or {}
    lex = dco_recipe.load_lexicon()
    tindex = {t["key"]: t for t in lex["techniques"]}

    tech = resolved.get("technique")
    if tech in tindex:
        base = dict(tindex[tech]["template"]["keyframes"][0])
    elif recipe.get("keyframes"):
        base = dict(recipe["keyframes"][0])
    else:
        base = {"kind": "saw"}

    character = list(resolved.get("passes") or [])
    cparams = {}
    for a in lex["adjectives"]:
        pd = a.get("pass")
        if isinstance(pd, dict) and pd.get("name") in character:
            cparams[pd["name"]] = {k: v for k, v in pd.items() if k != "name"}

    texture = None
    textures = resolved.get("textures") or []
    if textures:
        tname = textures[0]
        tentry = next((m for m in lex["motions"] if m.get("key") == tname), {})
        td = tentry.get("texture") or {}
        texture = {"name": tname,
                   "n_cycles": int(td.get("n_cycles", 4)),
                   "depth": float(td.get("depth", 0.5))}

    kind = base.get("kind")
    if kind == "fm2":
        movement = "fm_index"
    elif texture is not None:
        movement = "static"           # the texture IS the per-frame variation
    elif "static" in (resolved.get("motion") or []):
        movement = "static"
    else:
        movement = "brightness"

    out = dict(recipe)
    out["frame_engine"] = {"base": base, "movement": movement, "character": character,
                           "character_params": cparams, "texture": texture}
    out["frames"] = recipe.get("frames") or MAX_FRAMES
    return out


# ─── analysis: prove real per-frame variation (not a 2-corner interpolation) ──

def frame_metrics(frames):
    """Per-run frame-difference metric. Returns a dict with the per-frame spectral
    centroid track (min/max/mean over frames) and the consecutive-frame spectral
    delta (max/mean of ||mag_{f+1} - mag_f||_1). A 2-corner interpolation would
    show a smooth, tiny, monotone delta; genuine per-frame computation shows real
    structure — this is the regression proof the engine spec asks for."""
    F, N = frames.shape
    mags = np.abs(np.fft.rfft(frames, axis=1))          # (F, N/2+1)
    k = np.arange(mags.shape[1])
    denom = mags.sum(axis=1)
    centroid = np.where(denom > 0, (mags * k).sum(axis=1) / np.maximum(denom, 1e-12), 0.0)
    if F > 1:
        d = np.abs(np.diff(mags, axis=0)).sum(axis=1)   # L1 spectral delta per adjacent pair
        max_delta = float(d.max())
        mean_delta = float(d.mean())
    else:
        max_delta = mean_delta = 0.0
    return {
        "frames": int(F),
        "centroid_bin_min": float(centroid.min()),
        "centroid_bin_max": float(centroid.max()),
        "centroid_bin_mean": float(centroid.mean()),
        "consecutive_delta_max": max_delta,
        "consecutive_delta_mean": mean_delta,
    }
