#!/usr/bin/env python3
"""render_dual_ab.py -- OFFLINE proof that a dual "A+B" oscillator preserves the
FULL sound of a REAL LLM-authored LCO recipe, with NO partial lost AND the
CORRECT A:B balance (joint amplitude-normalization + per-engine ENERGY/RMS
compensating gain -- see step 6 below; fixes a 2026-07-15 bass-heavy bug. First
attempt matched PEAKS, which under-represents the higher-crest engine's ENERGY
(B is many summed inharmonic partials, crest ~3-4; A is often a lone sinusoid,
crest ~1.41) -- and spectral centroid is ENERGY-weighted, so B stayed too quiet.
The shipped fix matches per-oscillator ENERGY instead: crest-immune, and exact
up to the incoherent-phase energy model. History in this docstring.

THE CONCEPT: one recipe -> split every partial by
    harmonic := abs(h - round(h)) <= 1e-3
Harmonic partials define oscillator A (baked to a single-cycle wavetable via
backend/dco_frames.py bake_frames -- the SAME engine tools/render_bell_ab_clean.py
plays as its "Option A"). Non-integer partials define oscillator B (the shipped
real-time additive "stations" engine, tools/audition_additive_sets -- the SAME
engine render_bell_ab_clean.py plays as its "Option B"). Both play at once and
are mixed; this tool measures whether anything is lost in that split+remix.

RECIPE PROVENANCE (NO synthetic/hand-written recipes -- ever): every recipe is
authored by driving the EXACT surface tools/e2e_lco_7b_proof.py drives -- the
shipping IPC LLM path: run_instruct() against the frozen Qwen2.5-7B-Instruct
model dir, feeding build_lco_response() (backend/lco_author.py). This synth is
LLM-first with NO deterministic fallback (lco_author.py's own docstring:
"without the model there is no oscillator"); if the model/interpreter is
unavailable this tool STOPS and reports clearly -- it never fabricates a recipe.

PER-SOUND PIPELINE:
  1. author the REAL recipe (above).
  2. gather the FULL partial set: every keyframe/station -> its exact additive
     partials (dco_recipe._station_partials, the same closed forms DcoBaker
     mirrors) -> union-aligned across all keyframes (dco_recipe._union_align,
     the SAME alignment the shipping engine's own _stationize uses) so a
     partial that only blooms at a LATER station is still counted, at its PEAK
     amplitude across the chain. Handles a lone classic-kind keyframe (fm2/
     cheby/ring/saw/...), a harmonic analog-life drift chain, and an inharmonic
     stationized bloom chain uniformly (union-align on 1 station is a no-op,
     so there is no special-casing between "single keyframe" and "multi-
     keyframe chain" -- one code path handles both, per the task brief).
  3. partition every partial into harmonic (A) / non-integer (B) by the EXACT
     threshold above (the same 1e-3 the shipping engine itself uses to route
     inharmonic content to the sets engine -- not an arbitrary new number).
     Asserted: count(A)+count(B)==count(all), A union B reconstructs the
     gathered list exactly (no drop, no dupe) -- true by construction (a
     strict filter over one list), re-verified here defensively.
  4. render A: bake_frames on a STATIC single-cycle recipe carrying ONLY the
     harmonic partials (render_bell_ab_clean.py's A path, reused: same
     bake_frames + wavetable_render). Empty -> bake_frames's own empty-
     spectrum handling -> silence natively, no special-casing needed.
  5. render B: the shipped additive_sets auditioner playing a SINGLE static
     station (K=1 -> engine motion off, per audition_additive_sets.cpp's own
     docstring) carrying ONLY the non-integer partials (render_bell_ab_clean.py's
     B path, reused). Empty -> special-cased in Python (the C++ tool's own
     "silent -> FAIL" exit code would be a false alarm for a legitimately-empty
     B-set), a DUR*SR zero array is used directly, the binary is never called.
  6. BALANCE FIX (2026-07-15) -- ENERGY/RMS, not peak. The bug: A and B were
     independently PEAK-normalized (bake_frames ALWAYS re-peaks A to 0.95;
     additive_sets peak-limits B to ~0.95 too), summed as-is. That is doubly
     wrong: (i) it ignores each subset's true share of the sound, and (ii) even
     the corrected PEAK-matched version stays skewed because centroid is
     ENERGY-weighted and the two engines have very different crest factors
     (measured: A crest ~1.41 for a lone sinusoid; B crest ~3.9 for 14 summed
     inharmonic partials), so peak-matching leaves the high-crest B ~10x too
     quiet in RMS. FIX (crest-immune, exact up to the incoherent-phase model):
       (1) JOINT-normalize the gathered union amplitudes ONCE -- as you would a
           single sound -- so every partial keeps its proportion relative to
           the loudest partial in the whole recipe (_peak_normalize_partials,
           target=1.0/max(a); h/phase untouched). This is UNCHANGED from the
           first attempt and its absolute scale is irrelevant (only ratios feed
           the energies below, and A & B scale identically).
       (2) TARGET ENERGIES from the joint-normalized amplitudes: E_A = sum of
           a_i^2 over the HARMONIC partials, E_B = sum of a_i^2 over the
           INHARMONIC partials -- summed over the partials the engines actually
           RENDER at this f0/sr (0 < h*f0 < sr/2). Above-Nyquist partials are
           rendered by NEITHER engine NOR the unsplit reference (measured: ~26%
           of raw E_B for metallic_plate/glassy_chime sits above Nyquist at C4);
           counting them would boost the rendered partials to carry phantom
           energy -> B over-boosted -> centroid OVER-shoots the reference. The
           rendered-subset restriction is what makes the split reconstruct the
           reference EXACTLY. (For A here every harmonic partial is below
           Nyquist, so E_A is unaffected; the restriction only bites E_B.)
       (3) MEASURE the actual RMS of each raw render: rmsA = rms(A_raw from
           bake_frames), rmsB = rms(B_raw from additive_sets). Measured, never
           assumed -- because each engine applies its OWN internal
           normalization (bake_frames -> fixed 0.95 peak; additive_sets ->
           peak-limits its built wavetable), so neither raw RMS equals the
           analytic sqrt(E/2) an un-normalized incoherent-phase sum would give
           (verified: rmsB/sqrt(E_B/2) ranges 0.09..0.86 across these sounds,
           NOT a constant -> a C++ port MUST measure rmsB, cannot hardcode
           gainB=sqrt(2); likewise rmsA=0.95/crest_A with crest_A varying by
           harmonic count -> C++ must measure rmsA on the baked strip).
       (4) COMPENSATING GAIN per engine (_energy_gain): gainA = sqrt(E_A)/rmsA,
           gainB = sqrt(E_B)/rmsB (0.0 if that subset is empty or its render is
           silent). Applying it makes gainX*X carry EXACTLY sqrt(E_X) RMS, so
           A:B ENERGY = E_A:E_B = the natural balance, independent of either
           engine's crest factor or internal normalization. Only the RATIO
           gainA:gainB matters (the final -18 dBFS RMS normalize is a per-file
           scalar), so this sets the balance and nothing else.
  7. mix equal-power (out = cos(m*pi/2)*(gainA*A) + sin(m*pi/2)*(gainB*B)) at
     m=0 (Aonly), m=0.5 (mix50), m=1 (Bonly); "full" = gainA*A + gainB*B
     summed DIRECTLY (unweighted) -- the reference "nothing dropped, correctly
     balanced" sound. m=0/m=1 use the EXACT single-engine (gain-compensated)
     signal, not the cos/sin formula (avoids the ~1e-17 float leak fixed
     earlier).
  8. level-match EVERY one of the 4 outputs to -18 dBFS RMS independently (a
     pure constant-gain scalar, render_bell_ab_clean.py's own helpers, reused
     verbatim) and write 16-bit WAVs. Both engines render at the same F0/DUR/SR
     and neither carries added movement (both static) -- "movement realizations
     match" trivially (zero vs zero).
  9. fidelity gate ("no Jota lost"): FFT the level-matched, 16-bit-written FULL
     mix; for every ORIGINAL gathered partial frequency h*f0, take the peak
     magnitude (dB) in a +/-2-bin neighborhood and compare it against the
     spectrum's OWN noise floor (median magnitude across all bins -- robust,
     since real partials are a small minority of the bins). The worst
     (smallest) partial-to-floor margin across the whole sound is reported;
     positive means every original partial clears the floor.
  Bonus, non-required cross-check: an independent single-engine "unsplit"
  reference (ALL gathered partials, harmonic+inharmonic together, through the
  SAME additive_sets engine as one K=1 station) is compared against the "full"
  two-engine mix via log-mel cosine distance (render_bell_ab.py's own distance
  helpers, reused) -- an objective, non-audible proxy for "does the split+remix
  diverge from a natural unsplit rendering of the identical spectrum". This is
  NOT a substitute for an ear check on the disjointedness question -- numbers
  only, the ear is the instance for that.

    .venv/bin/python tools/render_dual_ab.py [OUTDIR]
Default OUTDIR: ~/Downloads/lco_dual_AB_<today>/
"""
import datetime
import json
import math
import os
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "backend"))
sys.path.insert(0, str(REPO / "tools"))

import dco_frames as df                                                      # noqa: E402
import dco_recipe                                                             # noqa: E402
import render_bell_ab as rba                                                  # noqa: E402
from render_bell_ab_clean import _normalize_rms, _rms, TARGET_DBFS, TARGET_RMS  # noqa: E402
from render_dco_frames import wavetable_render, write_wav                     # noqa: E402
from lco_author import build_lco_response                                     # noqa: E402
from pipe_inference import run_instruct                                       # noqa: E402

MODEL_7B = os.path.expanduser("~/Library/T5ynth/models/qwen2.5-7b-instruct")
DEVICE = os.environ.get("T5YNTH_DEVICE", "mps")
ADDITIVE_SETS_BIN = rba.ADDITIVE_SETS_BIN

F0 = rba.F0             # 261.63 Hz (C4) -- same knobs as render_bell_ab(_clean).py
DUR = rba.DUR           # 3.0 s
SR = rba.SR             # 44100

HARMONIC_TOL = 1.0e-3   # THE concept's own threshold -- identical to dco_frames._is_inharmonic /
                        # dco_recipe._router_contract_ok's own inharmonic-routing boundary.

SCRATCH = Path(os.environ.get(
    "T5YNTH_SCRATCH",
    "/private/tmp/claude-502/-Users-joerissen-ai4artsed-webserver/"
    "bff45a5c-d3ff-4b08-9cf8-4c0545e36a7b/scratchpad")) / "render_dual_ab"

# Test prompts -- verbatim, agreed with the caller. (slug, prompt).
PROMPTS = [
    ("sine_transient_glass", "A sine base plus sharp transient evolving into gentle cool glass"),
    ("bright_bell",          "a bright struck bell"),
    ("metallic_plate",       "a metallic shimmering plate"),
    ("glassy_chime",         "a glassy inharmonic chime"),
]


# ─── S0: author the REAL recipe via the shipping 7B IPC path (NO fallback) ────

def _author_real_recipe(prompt):
    """Drive EXACTLY the surface tools/e2e_lco_7b_proof.py drives: run_instruct
    against the frozen 7B model dir feeds build_lco_response. Returns the resp
    dict, or None -- NEVER a fabricated recipe -- if the model/interpreter is
    unavailable or the call fails."""
    if not os.path.isdir(MODEL_7B):
        print(f"    STOP: 7B model missing at {MODEL_7B} -- this synth is LLM-first "
              f"with NO deterministic fallback; cannot author a REAL recipe without it.")
        return None
    lco_llm = lambda text, sysp, mx: run_instruct(text, MODEL_7B, DEVICE, sysp, max_new_tokens=mx)  # noqa: E731
    try:
        resp = build_lco_response(prompt, lco_llm, frames=None)
    except Exception as e:
        print(f"    STOP: build_lco_response raised for {prompt!r}: {e!r}")
        return None
    if not resp or not (resp.get("recipe") or {}).get("keyframes"):
        print(f"    STOP: no usable recipe authored for {prompt!r}: {resp!r}")
        return None
    return resp


# ─── gather the full partial set (handles lone keyframe + multi-station chain) ─

def _gather_full_partials(recipe, flags=None):
    """recipe['keyframes'] -> one canonical partial list for the WHOLE sound.
    Every keyframe/station -> its exact partials (dco_recipe._station_partials,
    kind-dispatched: additive/saw/square/triangle/pulse/cheby/ring/fm2) ->
    union-aligned (dco_recipe._union_align) so every keyframe shares the same
    h-grid; amplitude at each aligned index is the PEAK reached anywhere in the
    chain, so a partial that is silent at one station (e.g. an extension
    partial that only blooms later) is still counted. Works uniformly for a
    single classic-kind keyframe, a harmonic analog-life drift chain, and an
    inharmonic stationized bloom chain (union-align on 1 station is a no-op)."""
    kfs = recipe.get("keyframes") or []
    if not kfs:
        return []
    corners = [dco_recipe._station_partials(kf) for kf in kfs]
    aligned = dco_recipe._union_align(corners, flags)
    if not aligned or not aligned[0]:
        return []
    n = len(aligned[0])
    full = []
    for i in range(n):
        h = aligned[0][i]["h"]
        phase = aligned[0][i]["phase"]
        amp = max(st[i]["a"] for st in aligned)     # peak reached anywhere in the chain
        full.append({"h": h, "a": amp, "phase": phase})
    return full


def _is_harmonic(h):
    return abs(float(h) - round(float(h))) <= HARMONIC_TOL


def _partition(full_partials):
    """full_partials -> (a_set, b_set, complete). complete asserts BOTH
    count(A)+count(B)==count(all) AND A union B reconstructs the gathered list
    exactly (no drop, no dupe) -- true by construction of a strict filter, but
    re-verified here defensively rather than only assumed."""
    a_set = [p for p in full_partials if _is_harmonic(p["h"])]
    b_set = [p for p in full_partials if not _is_harmonic(p["h"])]
    count_ok = (len(a_set) + len(b_set)) == len(full_partials)

    def _key(p):
        return (round(float(p["h"]), 6), round(float(p["a"]), 9), round(float(p["phase"]), 9))
    recon_ok = sorted(map(_key, a_set + b_set)) == sorted(map(_key, full_partials))
    return a_set, b_set, (count_ok and recon_ok)


# ─── BALANCE FIX: joint amplitude-normalize + per-engine ENERGY/RMS gain ──────
# See module docstring step 6 for the full narrative. Three small, pure
# functions a parallel C++ build must replicate EXACTLY for parity:
#   _peak_normalize_partials -- step 1, joint amplitude-normalize the union.
#   _subset_energy           -- step 2, target energy over the RENDERED partials.
#   _energy_gain             -- step 4, gain = sqrt(target_energy)/measured_rms.
# Steps 1-2 are on the ABSTRACT partial list (no rendering); step 3 (rmsA/rmsB)
# is a MEASURED quantity of each engine's real output (see _process_prompt).

def _peak_normalize_partials(partials, target=1.0):
    """STEP 1 (joint amplitude-normalize, UNCHANGED from the peak-based
    attempt). partials -> (normalized_partials, joint_peak_raw). ONE normalize
    over the WHOLE list (harmonic+inharmonic together, "as you would a single
    sound"): every partial's `a` is scaled by the SAME constant `target /
    max(a)`, so the loudest partial in the union hits `target` and every other
    partial keeps its amplitude PROPORTION. `h`/`phase` pass through unchanged.
    The absolute scale is irrelevant to the final balance (only the a_i RATIOS
    feed the energies, and A & B scale identically), but a fixed target=1.0
    keeps the amplitudes interpretable. joint_peak_raw is the PRE-normalize max
    `a` (0.0 for an empty/all-silent list -> returned unscaled)."""
    if not partials:
        return [], 0.0
    joint_peak_raw = max(float(p["a"]) for p in partials)
    if joint_peak_raw <= 0.0:
        return [dict(p) for p in partials], 0.0
    g = target / joint_peak_raw
    normalized = [{"h": float(p["h"]), "a": float(p["a"]) * g, "phase": float(p.get("phase", 0.0))}
                 for p in partials]
    return normalized, joint_peak_raw


def _subset_energy(partials, f0, sr):
    """STEP 2 (target energy). partials -> (E_rendered, E_all, n_above_nyq).
    E = sum of a_i^2 (a_i already joint-normalized) over the partials the
    engines actually RENDER at this f0/sr: 0 < h*f0 < sr/2. Above-Nyquist
    partials are rendered by NEITHER bake_frames (dropped above the wavetable
    Nyquist) NOR additive_sets (above the audio Nyquist) NOR the unsplit
    reference (same additive engine), so summing them into the energy TARGET
    would make the compensating gain boost the rendered partials to carry
    phantom energy -> that oscillator over-boosted -> centroid over-shoots the
    reference. E_rendered is therefore THE quantity for the gain; E_all and
    n_above_nyq are reported alongside so the above-Nyquist share is auditable
    (for these sounds A has none, B has ~26% at C4). The `0 < h*f0` guard also
    drops any DC/non-positive partial (none expected, defensive)."""
    E_all = sum(float(p["a"]) ** 2 for p in partials)
    rendered = [p for p in partials if 0.0 < float(p["h"]) * f0 < sr / 2.0]
    E_rendered = sum(float(p["a"]) ** 2 for p in rendered)
    return E_rendered, E_all, (len(partials) - len(rendered))


def _energy_gain(target_energy, measured_rms):
    """STEP 4 (compensating gain). gain = sqrt(target_energy) / measured_rms.
    target_energy = E_A or E_B (sum a_i^2 over the engine's RENDERED partial
    subset within the joint-normalized spectrum); measured_rms = the RMS of
    that engine's ACTUAL raw render (measured, not assumed -- each engine
    applies its own internal normalization, so raw RMS != analytic sqrt(E/2)).
    Applying the returned gain makes gain*render carry EXACTLY sqrt(target_energy)
    RMS (energy == target_energy: (gain*rms)^2 == target_energy), so A:B ENERGY
    == E_A:E_B regardless of either engine's crest factor or internal
    normalization -- crest-immune, unlike peak-matching. Empty subset / silent
    render (target_energy<=0 or measured_rms<=0) -> gain 0.0 (no 0/0 NaN)."""
    if target_energy <= 0.0 or measured_rms <= 0.0:
        return 0.0
    return math.sqrt(float(target_energy)) / float(measured_rms)


# ─── Render A: bake_frames, STATIC, harmonic partials only ────────────────────

def _render_A_partials(partials):
    """Option A path (render_bell_ab_clean.py._render_A, reused): bake_frames a
    STATIC single-cycle wavetable carrying EXACTLY `partials` -- nothing
    invented, nothing dropped -- then scan it at F0. Empty -> bake_frames's own
    empty-spectrum handling returns all-zero frames -> silence natively.
    Returns (audio, bake_peak): bake_peak is the peak |sample| of bake_frames'
    OWN returned frame array -- its internal re-peak target (0.95 unshaped /
    0.83 waveshaped) for ANY non-empty spectrum, REGARDLESS of the input
    partials' true amplitude (0.0 for an empty spectrum, which bakes to
    all-zero frames). bake_peak is a REPORTED diagnostic only (it documents
    that bake_frames re-normalizes, which is WHY the energy gain must measure
    the render's RMS rather than assume it); the balance gain itself uses the
    measured RMS of `audio` (see _energy_gain in _process_prompt), not
    bake_peak."""
    frame_engine = {"base": {"kind": "additive", "partials": [dict(p) for p in partials]},
                    "movement": "static", "character": [], "character_params": {},
                    "texture": None}
    recipe = {"frame_engine": frame_engine, "frames": df.MAX_FRAMES}
    frames = df.bake_frames(recipe)
    bake_peak = float(np.max(np.abs(frames))) if frames.size else 0.0
    audio = wavetable_render(frames, f0=F0, dur=DUR, sr=SR, scan_seconds=DUR)
    return audio.astype(np.float64), bake_peak


# ─── Render B: additive_sets, K=1 STATIC station ───────────────────────────────

def _read_wav(path):
    with wave.open(str(path), "rb") as w:
        sr = w.getframerate()
        raw = w.readframes(w.getnframes())
    return np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0, sr


def _render_additive(partials, jpath, wpath):
    """Shared B-engine driver (used for the real B-set AND the bonus unsplit
    reference): a K=1 static additive_sets station (render_bell_ab_clean.py's
    B path, reused) carrying exactly `partials`. Returns (signal, note) --
    signal is None only on a genuine tool failure (never fabricated); an EMPTY
    partials list is handled in Python (silence), never sent to the binary
    (whose own "silent -> FAIL" exit code would be a false alarm for a
    legitimately-empty station)."""
    n_samples = int(round(DUR * SR))
    if not partials:
        return np.zeros(n_samples, dtype=np.float64), "empty partial set -> silence (no engine call)"
    sets_json = {"sets": [[{"h": float(p["h"]), "a": float(p["a"]),
                            "phase": float(p.get("phase", 0.0))} for p in partials]],
                "motion_rate_hz": 0.5}
    with open(jpath, "w") as f:
        json.dump(sets_json, f)
    run = subprocess.run([str(ADDITIVE_SETS_BIN), str(jpath), str(wpath), str(DUR), str(F0)],
                         capture_output=True, text=True)
    tool_line = (run.stdout.strip().splitlines() or [""])[-1]
    if run.returncode != 0 or not Path(wpath).is_file():
        return None, (f"additive_sets FAILED (rc={run.returncode}): "
                      f"{run.stderr.strip() or run.stdout.strip()}")
    sig, _sr = _read_wav(wpath)
    if sig.size < n_samples:
        sig = np.pad(sig, (0, n_samples - sig.size))
    elif sig.size > n_samples:
        sig = sig[:n_samples]
    return sig, tool_line


# ─── fidelity gate: "no Jota lost" ─────────────────────────────────────────────

def _fidelity_gate(sig, full_partials, f0, sr, n_fft=32768):
    """Level-matched FULL mix -> (worst_gap_db, floor_db, per-partial rows).
    worst_gap_db = the SMALLEST (peak-in-neighborhood - noise-floor) margin
    across every gathered partial's h*f0; positive means every one clears the
    floor. floor_db = median magnitude across ALL bins (robust: real partials
    are a small minority of an n_fft/2-bin spectrum)."""
    if sig.size == 0 or not full_partials:
        return float("nan"), float("nan"), []
    start = min(int(0.1 * sr), max(0, sig.size - n_fft))
    n_fft = min(n_fft, 1 << int(np.floor(np.log2(max(64, sig.size - start)))))
    seg = sig[start:start + n_fft] * np.hanning(n_fft)
    mag_db = 20.0 * np.log10(np.abs(np.fft.rfft(seg)) + 1e-12)
    bin_hz = sr / n_fft
    floor_db = float(np.median(mag_db))

    rows = []
    for p in full_partials:
        hz = float(p["h"]) * f0
        if hz <= 0.0 or hz >= sr / 2.0:
            rows.append(dict(h=float(p["h"]), hz=hz, gap_db=float("nan"), note="above Nyquist -- excluded"))
            continue
        k = int(round(hz / bin_hz))
        lo, hi = max(0, k - 2), min(mag_db.size, k + 3)
        peak_db = float(np.max(mag_db[lo:hi])) if hi > lo else float("nan")
        rows.append(dict(h=float(p["h"]), hz=hz, gap_db=peak_db - floor_db, note=""))

    finite = [r["gap_db"] for r in rows if math.isfinite(r["gap_db"])]
    worst = min(finite) if finite else float("nan")
    return worst, floor_db, rows


# ─── per-prompt pipeline ────────────────────────────────────────────────────────

def _process_prompt(slug, prompt, outdir):
    print(f"\n{'=' * 100}\nPROMPT: {prompt!r}  (slug={slug})")
    saved_path = outdir / f"{slug}.recipe.json"
    if saved_path.is_file():
        # REUSE a previously-authored recipe verbatim -- the 7B model is NEVER
        # loaded on this path (deterministic, fast; used to re-verify a
        # rendering-side fix like the BALANCE fix without re-authoring).
        # Provenance is still real (the file was itself written by a prior
        # REAL 7B authoring pass, never hand-written) -- see the module
        # docstring's RECIPE PROVENANCE paragraph. Handles both the bare
        # {"keyframes": [...]} format this tool originally saved and a richer
        # {"recipe":..., "resolved":..., "flags":...} format (below), so old
        # saved files keep working.
        with open(saved_path) as f:
            saved = json.load(f)
        if "keyframes" in saved:
            recipe, resolved, flags = saved, {}, []
        else:
            recipe = saved.get("recipe") or {}
            resolved = saved.get("resolved") or {}
            flags = saved.get("flags") or []
        if not (recipe.get("keyframes")):
            return dict(slug=slug, prompt=prompt, ok=False,
                        reason=f"saved recipe at {saved_path} has no keyframes")
        print(f"  REUSING saved recipe: {saved_path.name}  (7B NOT invoked)  "
              f"keyframes={len(recipe.get('keyframes') or [])}")
        if resolved:
            print(f"  resolved (saved): technique={resolved.get('technique')!r} "
                  f"adjectives={resolved.get('adjectives')} motion={resolved.get('motion')}")
        if flags:
            print(f"  authoring flags (saved): {[(fl.get('word'), fl.get('reason')) for fl in flags]}")
    else:
        resp = _author_real_recipe(prompt)
        if resp is None:
            return dict(slug=slug, prompt=prompt, ok=False, reason="recipe authoring unavailable/failed")
        recipe = resp["recipe"]
        resolved = resp.get("resolved") or {}
        flags = resp.get("flags") or []
        with open(saved_path, "w") as f:
            json.dump({"recipe": recipe, "resolved": resolved, "flags": flags}, f, indent=1)
        print(f"  resolved: technique={resolved.get('technique')!r} adjectives={resolved.get('adjectives')} "
              f"motion={resolved.get('motion')}  keyframes={len(recipe.get('keyframes') or [])}")
        if flags:
            print(f"  authoring flags: {[(fl.get('word'), fl.get('reason')) for fl in flags]}")

    align_flags = []
    full_partials = _gather_full_partials(recipe, align_flags)
    if align_flags:
        print(f"  union-align flags: {align_flags}")

    # BALANCE FIX step 1 (joint amplitude-normalize the WHOLE union ONCE), THEN
    # partition -- so a_set/b_set carry amplitudes that are each other's true
    # proportion within the one joint spectrum. full_partials itself
    # (pre-normalize) is left untouched and still feeds the fidelity gate
    # (frequency-only, amp-blind) and the bonus unsplit reference.
    full_partials_norm, joint_peak_raw = _peak_normalize_partials(full_partials, target=1.0)
    a_set, b_set, partition_ok = _partition(full_partials_norm)
    n_all, n_a, n_b = len(full_partials), len(a_set), len(b_set)
    starved = n_a <= 1
    print(f"  partials: total={n_all}  harmonic->A={n_a}  inharmonic->B={n_b}  "
          f"partition_complete={'Y' if partition_ok else 'N'}{'  [A STARVED]' if starved else ''}")

    # STEP 2: target energies over the RENDERED (below-Nyquist) partials.
    E_A, E_A_all, nA_above = _subset_energy(a_set, F0, SR)
    E_B, E_B_all, nB_above = _subset_energy(b_set, F0, SR)

    A_raw, bake_peak = _render_A_partials(a_set)
    B_raw, b_note = _render_additive(b_set, SCRATCH / f"{slug}_B.json", SCRATCH / f"{slug}_B.wav")
    print(f"  A engine (bake_frames, static): {n_a} partial(s){' -- SILENT' if n_a == 0 else ''}")
    print(f"  B engine (additive_sets, K=1 static): {b_note}")
    if B_raw is None:
        return dict(slug=slug, prompt=prompt, ok=False, reason=f"B render failed: {b_note}",
                    n_all=n_all, n_a=n_a, n_b=n_b, partition_ok=partition_ok, starved=starved)

    n = min(A_raw.size, B_raw.size)
    A_raw, B_raw = A_raw[:n], B_raw[:n]

    # STEP 3: MEASURE each raw render's RMS (+ peak/crest for the parity report).
    rmsA, rmsB = _rms(A_raw), _rms(B_raw)
    peakA = float(np.max(np.abs(A_raw))) if A_raw.size else 0.0
    peakB = float(np.max(np.abs(B_raw))) if B_raw.size else 0.0
    crestA = (peakA / rmsA) if rmsA > 0 else float("nan")
    crestB = (peakB / rmsB) if rmsB > 0 else float("nan")

    # STEP 4: energy/RMS compensating gains -> A:B energy == E_A:E_B.
    gainA = _energy_gain(E_A, rmsA)
    gainB = _energy_gain(E_B, rmsB)

    # C++-parity diagnostics (see report). (a) Does raw rmsB == analytic
    # sqrt(E_B/2)? NO -- additive_sets internally applies gain 0.95/sum|a_i|
    # (WavetableOscillator.cpp:856, "0.95 / max_over_stations(sum|a_i|)"; K=1),
    # so rmsB == (0.95/sumAbs_B)*sqrt(E_B/2). Hence gainB is NOT the bare
    # constant sqrt(2); but the sum|a_i| gain is KNOWN, so gainB has an exact
    # CLOSED FORM: gainB == sqrt(2)*sumAbs_B/0.95 (the audible-energy term
    # cancels). Verified against the measured gainB below (matches to ~4 sig
    # figs). => a C++ port can compute gainB from sum|a_i| ALONE, with no B
    # render and no RMS measurement. The Python tool still USES the measured
    # rmsB (robust to any engine change), and reports the analytic value for
    # parity. (b) rmsA == 0.95/crest_A (bake_frames re-peaks to 0.95); crest_A
    # varies with harmonic count (1.42 lone sinusoid .. 3.46 for 64 partials)
    # and has no closed form => C++ MUST measure rmsA on the baked strip.
    ADDITIVE_PEAK = 0.95   # additive_sets internal peak target (WavetableOscillator.cpp:856)
    analyticA = math.sqrt(E_A / 2.0) if E_A > 0 else 0.0   # incoherent-phase, un-normalized
    analyticB = math.sqrt(E_B / 2.0) if E_B > 0 else 0.0
    parityA = (rmsA / analyticA) if analyticA > 0 else float("nan")
    parityB = (rmsB / analyticB) if analyticB > 0 else float("nan")
    sumAbs_B = sum(abs(float(p["a"])) for p in b_set)      # over ALL inharmonic partials sent to the engine
    gainB_closed = (math.sqrt(2.0) * sumAbs_B / ADDITIVE_PEAK) if b_set else 0.0

    print(f"  joint amplitude-normalize (target=1.0): joint_peak_raw={joint_peak_raw:.6f}")
    print(f"  ENERGY (rendered/below-Nyquist): E_A={E_A:.6f} (of {E_A_all:.6f}, {nA_above} above-Nyq)  "
          f"E_B={E_B:.6f} (of {E_B_all:.6f}, {nB_above} above-Nyq)")
    print(f"  MEASURED raw: rmsA={rmsA:.6f} peakA={peakA:.4f} crestA={crestA:.3f}  "
          f"rmsB={rmsB:.6f} peakB={peakB:.4f} crestB={crestB:.3f}")
    print(f"  BALANCE gains (energy/RMS): gainA={gainA:.6f} (= sqrt(E_A) {math.sqrt(E_A):.6f} / rmsA)  "
          f"gainB={gainB:.6f} (= sqrt(E_B) {math.sqrt(E_B):.6f} / rmsB)")
    print(f"  C++ parity: rmsB/sqrt(E_B/2)={parityB:.4f} (NOT sqrt2-constant)  "
          f"gainB_closed=sqrt2*sumAbs_B({sumAbs_B:.4f})/0.95={gainB_closed:.6f} (== measured gainB)  "
          f"rmsA/sqrt(E_A/2)={parityA:.4f}; rmsA=0.95/crestA -> A must be measured")

    A_g = gainA * A_raw    # gain-compensated A: RMS now == sqrt(E_A)
    B_g = gainB * B_raw    # gain-compensated B: RMS now == sqrt(E_B)

    # m=0/m=1 use the EXACT single-engine (gain-compensated) signal rather
    # than the general cos(m*pi/2)/sin(m*pi/2) formula: math.cos(math.pi/2) is
    # ~6.12e-17, not exactly 0.0 (pi itself is a finite-precision
    # approximation), so when one side is legitimately silent (e.g. B empty),
    # the formula would leak a ~1e-17-scaled copy of the OTHER side into what
    # should be pure silence -- and _normalize_rms would then amplify that
    # leakage by ~1e16x to reach -18 dBFS, corrupting "Bonly" into a quiet
    # copy of A dressed up at full level. cos(0.0)/sin(0.0) are exact, so this
    # only bit the m=1 endpoint, asymmetrically -- caught empirically on
    # bright_bell (B-set empty) below.
    raw_mixes = {
        "Aonly": A_g.copy(),
        "mix50": math.cos(math.pi / 4) * A_g + math.sin(math.pi / 4) * B_g,
        "Bonly": B_g.copy(),
        "full":  A_g + B_g,
    }

    levels = {}
    full_sig_written = None
    for name in ("Aonly", "mix50", "Bonly", "full"):
        sig_n, gain = _normalize_rms(raw_mixes[name], TARGET_RMS)
        path = outdir / f"{slug}_{name}.wav"
        write_wav(path, sig_n, sr=SR)
        sig_w, _sr = _read_wav(path)          # measure the ACTUAL written (16-bit) signal
        achieved = _rms(sig_w)
        levels[name] = dict(rms=achieved,
                            dbfs=(20.0 * np.log10(achieved) if achieved > 0 else float("-inf")),
                            pre_norm_peak=(float(np.max(np.abs(sig_n))) if sig_n.size else 0.0),
                            gain=gain, path=str(path))
        if name == "full":
            full_sig_written = sig_w

    worst_gap_db, floor_db, gate_rows = _fidelity_gate(full_sig_written, full_partials, F0, SR)
    no_jota_lost = math.isfinite(worst_gap_db) and worst_gap_db > 0.0
    print(f"  levels (target {TARGET_DBFS:.0f} dBFS): " +
          "  ".join(f"{k}={v['dbfs']:+.2f}dB" for k, v in levels.items()))
    print(f"  fidelity gate: noise_floor={floor_db:.1f}dB  worst_partial_gap={worst_gap_db:+.1f}dB  "
          f"no_jota_lost={'Y' if no_jota_lost else 'N'}")

    # bonus (non-required) cross-check: an independent single-engine "unsplit"
    # rendering of the SAME full partial set, compared against the two-engine
    # "full" mix. Objective spectral-shape proxy only -- not an ear check.
    ref_raw, ref_note = _render_additive(full_partials, SCRATCH / f"{slug}_ref.json", SCRATCH / f"{slug}_ref.wav")
    ref_block = None
    if ref_raw is not None:
        m = min(ref_raw.size, full_sig_written.size)
        ref_n, _g = _normalize_rms(ref_raw[:m], TARGET_RMS)
        ref_path = SCRATCH / f"{slug}_ref_normalized.wav"
        write_wav(ref_path, ref_n, sr=SR)
        ref_sig, _sr = _read_wav(ref_path)
        magF, nF = rba._avg_mag(full_sig_written[:m])
        magR, nR = rba._avg_mag(ref_sig[:m])
        cenF, cenR = rba._centroid_hz(magF, SR, nF), rba._centroid_hz(magR, SR, nR)
        lm = rba._cos_dist(rba._logmel(magF, SR, nF), rba._logmel(magR, SR, nR))
        ref_block = dict(cen_full=cenF, cen_ref=cenR, logmel_cos=lm, note=ref_note)
        print(f"  bonus xcheck: split-full cen={cenF:.0f}Hz vs unsplit-ref cen={cenR:.0f}Hz  "
              f"logmel_cos_dist={lm:.4f} (0=identical shape)")
    else:
        print(f"  bonus xcheck: SKIPPED ({ref_note})")

    return dict(slug=slug, prompt=prompt, ok=True, n_all=n_all, n_a=n_a, n_b=n_b,
               partition_ok=partition_ok, starved=starved, levels=levels,
               worst_gap_db=worst_gap_db, floor_db=floor_db, no_jota_lost=no_jota_lost,
               gate_rows=gate_rows, resolved=resolved, flags=flags, ref_block=ref_block,
               joint_peak_raw=joint_peak_raw,
               E_A=E_A, E_A_all=E_A_all, nA_above=nA_above,
               E_B=E_B, E_B_all=E_B_all, nB_above=nB_above,
               rmsA=rmsA, rmsB=rmsB, peakA=peakA, peakB=peakB, crestA=crestA, crestB=crestB,
               bake_peak=bake_peak, parityA=parityA, parityB=parityB,
               sumAbs_B=sumAbs_B, gainB_closed=gainB_closed,
               gainA=gainA, gainB=gainB)


def main():
    outdir = Path(sys.argv[1]) if len(sys.argv) > 1 else (
        Path.home() / "Downloads" / f"lco_dual_AB_{datetime.date.today().isoformat()}")
    outdir.mkdir(parents=True, exist_ok=True)
    SCRATCH.mkdir(parents=True, exist_ok=True)

    if not ADDITIVE_SETS_BIN.is_file():
        print(f"STOP: additive_sets auditioner not found at {ADDITIVE_SETS_BIN} "
              f"(build it via tools/e2e_additive_sets_render.py first).")
        return 2
    if not os.path.isdir(MODEL_7B):
        print(f"STOP: 7B model missing at {MODEL_7B}. This synth is LLM-first with NO "
              f"deterministic fallback (backend/lco_author.py) -- cannot author a REAL "
              f"recipe without it. Nothing was fabricated.")
        return 2

    print(f"Dual A+B oscillator proof -> {outdir}")
    print(f"  model={MODEL_7B}  device={DEVICE}")
    print(f"  f0={F0} Hz (C4), dur={DUR}s, sr={SR}, harmonic tol={HARMONIC_TOL}")
    print(f"  A = bake_frames (static wavetable) of the harmonic partials")
    print(f"  B = additive_sets (K=1 static station) of the non-integer partials\n")

    rows = []
    for slug, prompt in PROMPTS:
        try:
            row = _process_prompt(slug, prompt, outdir)
        except Exception as e:
            print(f"  EXCEPTION for {prompt!r}: {e!r}")
            row = dict(slug=slug, prompt=prompt, ok=False, reason=repr(e))
        rows.append(row)

    print("\n" + "=" * 110)
    print("SUMMARY")
    print(f"{'slug':20}{'total':>7}{'harm(A)':>9}{'inharm(B)':>11}{'partition':>11}"
          f"{'no_jota_lost':>14}{'worst_gap_dB':>14}")
    print("-" * 110)
    for r in rows:
        if not r.get("ok"):
            print(f"{r['slug']:20}  FAILED -- {r.get('reason')}")
            continue
        print(f"{r['slug']:20}{r['n_all']:7}{r['n_a']:9}{r['n_b']:11}"
              f"{('Y' if r['partition_ok'] else 'N'):>11}{('Y' if r['no_jota_lost'] else 'N'):>14}"
              f"{r['worst_gap_db']:+14.1f}")
    print("=" * 110)

    manifest = outdir / "DUAL_AB_metrics.txt"
    with open(manifest, "w") as f:
        f.write("Dual A+B oscillator proof -- MEASUREMENT ONLY (no audibility claims; the ear is\n")
        f.write("the instance for the 'disjointed' question -- this file is the numeric evidence).\n")
        f.write("A = bake_frames static wavetable of harmonic partials (backend/dco_frames.py).\n")
        f.write("B = additive_sets K=1 static station of non-integer partials (shipped engine).\n")
        f.write(f"f0={F0} Hz (C4), dur={DUR}s, sr={SR}, harmonic tol={HARMONIC_TOL} "
                f"(== dco_frames._is_inharmonic's own boundary).\n")
        f.write(f"Recipes authored via the REAL 7B IPC path (model={MODEL_7B}, device={DEVICE}) or, if a "
                f"{{slug}}.recipe.json already existed in this OUTDIR, REUSED verbatim (7B not reloaded) --\n")
        f.write("per-prompt lines below say which. NO synthetic/hand-written recipes either way.\n")
        f.write("BALANCE FIX (2026-07-15) -- ENERGY/RMS, not peak. A and B are the harmonic/inharmonic\n")
        f.write("SLICES of ONE jointly amplitude-normalized union spectrum (_peak_normalize_partials,\n")
        f.write("target=1.0/max(a) over harmonic+inharmonic together -- step 1, unchanged). The A:B BALANCE\n")
        f.write("is then set by ENERGY, not peak: target energies E_A=sum(a_i^2 over HARMONIC), E_B=sum(a_i^2\n")
        f.write("over INHARMONIC), summed ONLY over the partials the engines actually RENDER (0<h*f0<sr/2 --\n")
        f.write("above-Nyquist partials are rendered by neither engine nor the unsplit reference, so counting\n")
        f.write("them would over-boost that oscillator and over-shoot the reference; ~26% of raw E_B is above\n")
        f.write("Nyquist for metallic_plate/glassy_chime at C4). Each engine's raw render RMS is MEASURED\n")
        f.write("(rmsA, rmsB) and the compensating gain is _energy_gain = sqrt(E)/rms, so gain*render carries\n")
        f.write("exactly sqrt(E) RMS and A:B ENERGY == E_A:E_B (the natural balance). Energy-matching is\n")
        f.write("CREST-IMMUNE; the earlier PEAK-matched fix left B (crest ~3.9, many summed partials) ~10x too\n")
        f.write("quiet in RMS vs A (crest ~1.41, lone sinusoid) and, since centroid is energy-weighted, still\n")
        f.write("skewed (peak-based 'full' centroid closed the metallic_plate skew only 72%, glassy_chime 15%).\n")
        f.write("rmsA/rmsB are MEASURED (the Python tool measures for robustness) because each engine\n")
        f.write("self-normalizes: raw RMS != analytic sqrt(E/2) (parityB below ~0.09..0.86, NOT a constant),\n")
        f.write("so gainB is NOT the bare constant sqrt(2). C++ PARITY: additive_sets applies gain\n")
        f.write("0.95/sum|a_i| (WavetableOscillator.cpp:856), so rmsB==(0.95/sumAbs_B)*sqrt(E_B/2) and gainB\n")
        f.write("has an EXACT closed form gainB==sqrt(2)*sumAbs_B/0.95 (audible-energy term cancels; verified\n")
        f.write("to ~4 sig figs vs measured, per prompt below) -> a C++ port computes gainB from sum|a_i|\n")
        f.write("ALONE, no B render/measurement. For A: rmsA==0.95/crest_A with crest_A varying by harmonic\n")
        f.write("count (no closed form) -> C++ MUST measure rmsA on the baked strip.\n")
        f.write("The original (pre-fix) bug summed two INDEPENDENTLY peak-normalized renders: bass-heavy 'full'\n")
        f.write("centroid 2617Hz vs 5579Hz reference (metallic_plate), 3669 vs 7816Hz (glassy_chime).\n")
        f.write(f"Every output level-matched to {TARGET_DBFS:.0f} dBFS RMS (pure constant-gain scalar; only the\n")
        f.write("gainA:gainB RATIO sets the balance).\n\n")
        for r in rows:
            f.write("-" * 100 + "\n")
            f.write(f"[{r['slug']}] {r['prompt']!r}\n")
            if not r.get("ok"):
                f.write(f"  FAILED: {r.get('reason')}\n\n")
                continue
            f.write(f"  resolved: {r['resolved']}\n")
            if r.get("flags"):
                f.write(f"  authoring flags: {r['flags']}\n")
            f.write(f"  partials: total={r['n_all']} harmonic->A={r['n_a']} inharmonic->B={r['n_b']} "
                    f"partition_complete={'Y' if r['partition_ok'] else 'N'}"
                    f"{'  [A STARVED]' if r['starved'] else ''}\n")
            f.write(f"  joint amplitude-normalize (target=1.0): joint_peak_raw={r['joint_peak_raw']:.6f}\n")
            f.write(f"  target energy (rendered, below-Nyquist): "
                    f"E_A={r['E_A']:.6f} (of E_A_all={r['E_A_all']:.6f}, {r['nA_above']} above-Nyq)  "
                    f"E_B={r['E_B']:.6f} (of E_B_all={r['E_B_all']:.6f}, {r['nB_above']} above-Nyq)\n")
            f.write(f"  measured raw render: rmsA={r['rmsA']:.6f} peakA={r['peakA']:.4f} crestA={r['crestA']:.3f}  "
                    f"rmsB={r['rmsB']:.6f} peakB={r['peakB']:.4f} crestB={r['crestB']:.3f}\n")
            f.write(f"  BALANCE gains (energy/RMS): gainA={r['gainA']:.6f} (= sqrt(E_A) {math.sqrt(r['E_A']):.6f} / rmsA)  "
                    f"gainB={r['gainB']:.6f} (= sqrt(E_B) {math.sqrt(r['E_B']):.6f} / rmsB)\n")
            f.write(f"  C++ parity: rmsB/sqrt(E_B/2)={r['parityB']:.4f} (NOT sqrt2)  "
                    f"gainB_closed = sqrt2*sumAbs_B({r['sumAbs_B']:.4f})/0.95 = {r['gainB_closed']:.6f} "
                    f"(== measured gainB {r['gainB']:.6f})  |  rmsA/sqrt(E_A/2)={r['parityA']:.4f}, "
                    f"rmsA=0.95/crestA -> A measured\n")
            for name in ("Aonly", "mix50", "Bonly", "full"):
                lv = r["levels"][name]
                f.write(f"    {r['slug']}_{name}.wav  rms={lv['rms']:.6f} ({lv['dbfs']:+.2f} dBFS)  "
                        f"pre_norm_peak={lv['pre_norm_peak']:.4f}  gain={lv['gain']:.4f}\n")
            f.write(f"  fidelity gate: noise_floor={r['floor_db']:.1f}dB  "
                    f"worst_partial_gap={r['worst_gap_db']:+.1f}dB  "
                    f"no_jota_lost={'Y' if r['no_jota_lost'] else 'N'}\n")
            f.write("  per-partial gap (h, target Hz, gap dB above floor):\n")
            for gr in r["gate_rows"]:
                note = f"  ({gr['note']})" if gr.get("note") else ""
                f.write(f"    h={gr['h']:<10.4f} {gr['hz']:>9.1f} Hz  gap={gr['gap_db']:+7.1f} dB{note}\n")
            if r.get("ref_block"):
                rb = r["ref_block"]
                f.write(f"  bonus xcheck (split-full vs unsplit single-engine reference): "
                        f"cen_full={rb['cen_full']:.0f}Hz cen_ref={rb['cen_ref']:.0f}Hz "
                        f"logmel_cos_dist={rb['logmel_cos']:.4f}\n")
            f.write("\n")

    print(f"\nWrote {sum(1 for r in rows if r.get('ok'))}/{len(rows)} sound(s) "
          f"(4 WAVs + recipe JSON each) + DUAL_AB_metrics.txt to {outdir}")
    return 0 if all(r.get("ok") and r.get("partition_ok") and r.get("no_jota_lost") for r in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
