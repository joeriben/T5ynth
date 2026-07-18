#!/usr/bin/env python3
"""Csound orchestra builder — REAL synthesis idioms from lexicon keys.

This is the corrected Csound backend (2026-07-17): the language-understanding
7B routes a prompt to dco_lexicon.json KEYS (technique / adjectives / motion,
incl. a morph chain "a > b"); THIS module turns those keys into a live Csound
orchestra that uses Csound's OWN synthesis opcodes — `vco2`+`kpw` for PWM,
`foscili` for FM, `tanh` waveshaping for dirt/overdrive, a k-rate additive bank
for a genuine spectral MORPH — never a static partial-amplitude table. It is the
capability-complete VERTICAL SLICE (CLAUDE.md "Migration & Substrate Discipline":
prove the paradigm on one real idiom per class before widening the vocabulary).

The emitted CSD follows the EXACT Phase-1 channel/instrument contract that
src/dsp/CsoundEngine.cpp compiles and drives (verify against that file, not this
comment): sr=%SR% (host substitutes), ksmps=64, nchnls=16, 0dbfs=1; ONE numeric
`instr 1` with p4=voice(1..16); six named control channels per voice
(gate/freq/vel/pres/timb/trig %d) read via chnget; output `aout = asig * kgate *
kvel * kpresGain * headroom` to `outch ivoice, aout`; 16 always-on score
instances `i 1 0 360000 v`. The always-on instance never re-inits at note time,
so a per-note MORPH trajectory is (re)started off the trig-epoch channel via
`changed2`/`reinit` — this is a SPECTRAL trajectory at constant level, NOT an
amplitude envelope (those correctly belong to the synth ADSR and stay removed,
BJ 2026-07-17).

Pure Python, deterministic, no model import here — pipe_inference.py's "csound"
mode calls build_orchestra() with the keys the 7B picked (validated closed-enum
against the lexicon, dco_llm_map._validate_keys). Emits `sr = %SR%` verbatim; the
engine (and tools/csound_orch_check) substitute the real rate at compile time.
"""
from __future__ import annotations

KSMPS = 64
NCHNLS = 16          # == CsoundEngine::kMaxVoices
HEADROOM = 0.32      # bounds a single idiom's peak; the voice VCA/DCA shapes the rest
DEFAULT_MORPH_SEC = 1.4   # intrinsic morph duration when the prompt gives no speed cue


# ── representative spectra for additive / morph endpoints (ratio, amp) ────────
# Real partial sets (the inharmonic ones are genuinely non-integer — that is the
# point). These drive the additive bank and the morph; a single-partial "sine"
# is the degenerate additive case a morph collapses onto.
_SPECTRA = {
    # bright inharmonic "glass" sheen (no dedicated technique key; the 7B reaches
    # it via `additive`/`glassy`, and it is the canonical morph START of BJ's
    # example "glass ... morphing into a sine wave").
    "glass":    [(1.00, 1.00), (2.71, 0.62), (3.83, 0.44), (5.17, 0.30), (6.61, 0.20), (8.09, 0.13)],
    # generic additive base (light 2nd/3rd), what `additive` resolves to.
    "additive": [(1.00, 1.00), (2.00, 0.45), (3.00, 0.28), (4.00, 0.16), (5.00, 0.09)],
    # inharmonic bell partials (lexicon fm_bell: h 2.76/5.4/8.93).
    "bell":     [(1.00, 1.00), (2.76, 0.55), (5.40, 0.32), (8.93, 0.18)],
    # the reference tone: one partial. A morph endpoint collapses onto this.
    "sine":     [(1.00, 1.00)],
    # classic waveform harmonic series, as ADDITIVE morph endpoints (a morph is a
    # trajectory in one additive bank -- so a morph FROM a saw/square/etc. reads
    # its spectrum here; the STEADY path still uses the live vco2). saw = all
    # harmonics 1/n; square/triangle = odd only (1/n, 1/n^2); pulse = thin bright
    # pulse (all harmonics, slow rolloff); cheby/fm = a few strong harmonics.
    "saw":      [(1.00, 1.00), (2.00, 0.50), (3.00, 0.333), (4.00, 0.25), (5.00, 0.20), (6.00, 0.167)],
    "square":   [(1.00, 1.00), (3.00, 0.333), (5.00, 0.20), (7.00, 0.143), (9.00, 0.111)],
    "triangle": [(1.00, 1.00), (3.00, 0.1111), (5.00, 0.0400), (7.00, 0.0204),
                 (9.00, 0.0123), (11.00, 0.0083)],
    "pulse":    [(1.00, 1.00), (2.00, 0.80), (3.00, 0.60), (4.00, 0.45), (5.00, 0.33), (6.00, 0.24)],
    "cheby":    [(1.00, 1.00), (2.00, 0.50), (3.00, 0.35), (4.00, 0.22), (5.00, 0.12)],
    "fm":       [(1.00, 1.00), (2.00, 0.60), (3.00, 0.40), (4.00, 0.28), (5.00, 0.18), (6.00, 0.10)],
    # morph-to-zero endpoint: all-amplitude-0. A chain "<x> > silence" fades every
    # partial of x to nothing over the morph leg -> a clean transient / pseudo-env
    # (the ONE amplitude shaping BJ authorized, 2026-07-18). norm() guards sum==0.
    "zero":     [(1.00, 0.00)],
}

# technique key -> which _SPECTRA entry represents it as a MORPH endpoint (a
# morph interpolates spectra, so every endpoint needs a spectral reading). Every
# lexicon technique the 7B can emit maps here; anything unlisted defaults to
# "additive" in _emit_morph, so a morph NEVER silently collapses to a steady tone.
_MORPH_SPECTRUM = {
    "sine": "sine", "sub_sine": "sine", "theremin": "sine",
    "additive": "additive", "organ": "additive", "flute": "additive",
    "glass": "glass",
    "fm_bell": "bell", "metallic_fm": "bell", "struck_bar": "bell",
    "fm": "fm", "fm_ep": "fm",
    "saw": "saw", "supersaw": "saw", "brass": "saw", "strings": "saw",
    "bass_saw": "saw", "sync": "saw",
    "square": "square", "clarinet": "square", "chiptune": "square", "pulse": "square",
    "triangle": "triangle",
    "pwm": "pulse",
    "cheby": "cheby",
    "silence": "zero", "zero": "zero",   # morph-to-zero transient terminal
}
_DEFAULT_MORPH_SPECTRUM = "additive"   # a morph endpoint with no reading falls here


# Csound-LOCAL technique keys — real idioms the assembler already knows (they have
# a _SPECTRA / _MORPH_SPECTRUM reading) but that are NOT in the shared
# dco_lexicon.json (which lco_author.py also consumes; we do NOT edit it from the
# csound path). build_csound_response augments the csound canon + catalogue with
# these so the 7B can route to them for THIS path only. "glass" is BJ's canonical
# morph START ("a glass sound morphing into a sine wave"): without it the 7B
# demotes the word to the adjective "glassy" and the whole morph collapses to a
# bare sine (frozen-corpus finding, 2026-07-18).
_CS_TECH_EXTRA = {
    "glass": {
        "why": "bright inharmonic struck-glass sheen — additive, non-integer partials",
        "surface_forms": ["glass", "glassy", "glass pad", "glassy pad"],
    },
}

# Validation-ONLY chain terminals: accepted by the closed-enum guard if the 7B
# emits them, but deliberately NOT listed as pickable techniques in the catalogue
# (listing "silence" as a technique made the 7B treat it as a standalone
# oscillator — "OSC1: silence" — or append it to sustained drones/pads; corpus
# regression 2026-07-18). The 7B learns silence ONLY from the explicit SILENCE
# RULE + worked example in _CS_SYSTEM_PROMPT_HEAD, where its scope is constrained.
_CS_TERMINALS = {
    "silence": ["silence", "zero", "nothing", "silent"],
}

# pwm names a MOVING DUTY on a pulse wave, not a wave in its own right: when the
# 7B lists it in a morph chain beside a pulse-family wave ("pwm > square"), the
# intent is a pwm'd square (one moving-duty tone), not a near-static pulse->pulse
# morph. build_csound_response collapses such a chain to the single pwm idiom.
_PULSE_FAMILY = {"square", "pulse", "chiptune", "clarinet"}


# ── csound-specific multi-oscillator LLM schema ──────────────────────────────
# Up to THREE oscillators, each its own morph chain + volume; the whole sound has
# adjectives + motion. This is a csound-OWNED schema (BJ 2026-07-18: "bis 3
# oszillatoren, morph-ketten pro osc, vol pro osc"); it does NOT touch the shared
# dco_llm_map._SYSTEM_PROMPT_HEAD (lco_author.py depends on that single-technique
# format). The KEY LISTS still come from dco_llm_map._build_catalogue(lex_cs).
_CS_MAX_NEW_TOKENS = 160   # 3 osc + vols + adjectives + motion (~8 short lines)
_CS_SYSTEM_PROMPT_HEAD = (
    "You translate a sound description into a small synthesizer patch of up to "
    "THREE oscillators, choosing ONLY keys from the fixed catalogue below. You "
    "never invent names or numbers.\n"
    "Reply in EXACTLY this format and nothing else (omit OSC2/OSC3 lines if not "
    "needed):\n"
    "OSC1: <key> [> <key> ...]\n"
    "VOL1: <number 0.0-1.0>\n"
    "OSC2: <key> [> <key> ...]\n"
    "VOL2: <number 0.0-1.0>\n"
    "OSC3: <key> [> <key> ...]\n"
    "VOL3: <number 0.0-1.0>\n"
    "ADJECTIVES: <key>, <key>, ...\n"
    "MOTION: <key>\n"
    "Rules: use ONE oscillator for a simple sound; add OSC2/OSC3 only to LAYER or "
    "detune (e.g. a fat saw stacked with a sub, or a bright bell over a warm pad). "
    "Each OSC line is that layer's waveform/method; if it MORPHS from one sound "
    "into another, list them in order separated by \" > \" (e.g. glass > sine). "
    "VOLn is that layer's loudness (main layer 1.0, supporting layers less). "
    "ADJECTIVES are timbral modifiers for the whole sound (or \"none\"). MOTION is "
    "how the whole sound moves over time (or \"none\"). Match by MEANING even if "
    "the wording differs. Use ONLY keys from the catalogue; if nothing in a "
    "category fits, write \"none\".\n"
    "SILENCE RULE: only for a sound that literally FADES OUT — a pluck, a stab, a "
    "percussive hit, or a sound described as decaying / fading to nothing. Then "
    "append \" > silence\" to that oscillator's chain AFTER at least one real "
    "waveform (e.g. \"pulse > silence\", \"strings > silence\"). NEVER write "
    "\"silence\" by itself, and NEVER append silence to a SUSTAINED sound — a "
    "drone, pad, lead, organ or held note keeps ringing and must not end in "
    "silence.\n"
    "Example — a plucked, percussive tone that fades to nothing:\n"
    "OSC1: pulse > silence\n"
    "VOL1: 1.0\n"
    "ADJECTIVES: bright\n"
    "MOTION: none\n"
    "CATALOGUE:\n"
)

import re as _re
_CS_OSC_RE = _re.compile(r'(?i)^OSC\s*(\d+)\s*:\s*(.*)$')
_CS_TECH_RE = _re.compile(r'(?i)^TECHNIQUE\s*:\s*(.*)$')   # legacy single-osc reply
_CS_VOL_RE = _re.compile(r'(?i)^VOL\s*(\d+)\s*:\s*(.*)$')
_CS_ADJ_RE = _re.compile(r'(?i)^ADJECTIVES?\s*:\s*(.*)$')
_CS_MOT_RE = _re.compile(r'(?i)^MOTION\s*:\s*(.*)$')        # singular only (not the MOTIONS catalogue header)


def _parse_csound_reply(raw):
    """Parse the multi-osc reply -> (oscs, adjectives_raw, motion_raw) where oscs
    is an ORDERED list of (chain_raw_string, vol_float). Tolerant: a legacy
    'TECHNIQUE:' line is read as OSC1; a missing VOLn defaults to 1.0; last
    occurrence of a label wins (a model that echoes the format before answering).
    Empty / 'none' oscillator lines are dropped."""
    osc_chains, osc_vols = {}, {}
    adjectives_raw, motion_raw = "", ""
    for line in (raw or "").splitlines():
        s = line.strip().lstrip("-*• \t")
        m = _CS_OSC_RE.match(s)
        if m:
            osc_chains[int(m.group(1))] = m.group(2).strip()
            continue
        m = _CS_TECH_RE.match(s)
        if m:
            osc_chains[1] = m.group(1).strip()   # legacy single-technique reply == OSC1
            continue
        m = _CS_VOL_RE.match(s)
        if m:
            try:
                osc_vols[int(m.group(1))] = float(m.group(2).strip().split()[0])
            except (ValueError, IndexError):
                pass
            continue
        m = _CS_ADJ_RE.match(s)
        if m:
            adjectives_raw = m.group(1).strip()
            continue
        m = _CS_MOT_RE.match(s)
        if m:
            motion_raw = m.group(1).strip()
            continue
    oscs = []
    for idx in sorted(osc_chains):
        chain = osc_chains[idx]
        if not chain or chain.lower() == "none":
            continue
        oscs.append((chain, osc_vols.get(idx, 1.0)))
    return oscs, adjectives_raw, motion_raw


def _strip_nonterminal_silence(chain):
    """silence/zero is authorized ONLY as a chain's TERMINAL stage — a morph-to-
    zero fade-OUT (the one sanctioned amplitude shaping, BJ 2026-07-18). A leading
    or middle silence would make _emit_morph ramp partial amplitudes UP from zero
    = an unauthorized fade-IN ATTACK envelope, which the synth (JUCE ADSR) owns.
    So drop any silence/zero token that is not the last element (adversarial
    review finding, 2026-07-18: "silence > saw" emitted a 1.4 s attack swell)."""
    n = len(chain)
    return [k for i, k in enumerate(chain)
            if k not in ("silence", "zero") or i == n - 1]


def _validate_osc_chain(chain_raw, tcanon, dco_llm_map):
    """One OSC line's raw chain string -> a validated list of technique keys.
    ">" = morph chain (keep every valid stage, ordered); a bare comma list is one
    compound technique -> first valid key. Enforces terminal-only silence and the
    pwm/pulse-family collapse."""
    if ">" in chain_raw:
        keys, flags = dco_llm_map._validate_keys(chain_raw.split(">"), tcanon)
    else:
        valid, flags = dco_llm_map._validate_keys(chain_raw.split(","), tcanon)
        keys = valid[:1]
    keys = _strip_nonterminal_silence(keys)
    if "pwm" in keys and all(k == "pwm" or k in _PULSE_FAMILY for k in keys):
        keys = ["pwm"]
    return keys, flags


def _reading(oscs, adjective_keys, motion_key):
    """Short human-readable interpretation for the UI card. Multi-osc: each layer
    is 'a > b' (morph) or 'a'; layers joined with ' + '. e.g.
    'saw + sub_sine · warm ~evolve' or 'glass > sine · glassy'."""
    def osc_str(o):
        chain = o["chain"]
        if len(chain) >= 2:
            return " > ".join(chain)
        return chain[0] if chain else "sine"
    heads = [osc_str(o) for o in oscs] or ["sine"]
    head = " + ".join(heads)
    bits = [head]
    if adjective_keys:
        bits.append(", ".join(adjective_keys))
    if motion_key and motion_key != "static":
        bits.append("~" + motion_key)
    return " · ".join(bits)


# ── per-technique synthesis: each returns Csound lines producing the oscillator
#    signal `aosc<tag>` from `kfreq`. `tag` (the osc index "0"/"1"/"2") suffixes
#    EVERY generated symbol so up to three oscillators coexist in one `instr 1`
#    with no variable collision. Standing tones (hold while gate open). ──

def _emit_steady(technique, tag="0"):
    """A single (non-morph) technique -> `aosc<tag>`. Uses Csound's native
    opcodes; every temporary is suffixed with `tag` (per-osc uniqueness)."""
    ov = f"aosc{tag}"
    L = []
    if technique == "pwm":
        # classic PWM: band-limited pulse whose DUTY moves (square 50% -> thin
        # 8% -> back), a genuinely moving spectrum. kpw is the pulse width.
        L.append(f"  klfo{tag}    oscili 0.5, 0.25            ; -0.5..0.5, 4 s period")
        L.append(f"  kpw{tag}     = 0.29 + 0.21 * (klfo{tag} + 0.5) ; duty 0.08..0.50..0.08")
        L.append(f"  {ov}    vco2 0.6, kfreq, 2, kpw{tag}     ; imode 2 = pulse, kpw = width")
    elif technique in ("square", "clarinet", "chiptune", "pulse"):
        L.append(f"  {ov}    vco2 0.6, kfreq, 2, 0.5     ; square (50%% pulse)")
    elif technique == "triangle":
        # vco2 imode 4 renders SILENT on this Csound build (the triangle band-
        # limited table is not pre-generated). Use the additive triangle (odd
        # harmonics ~1/n^2) -- band-limited by construction, guaranteed to sound.
        L.append(_emit_additive(_SPECTRA["triangle"], gain=0.6, tag=tag))
    elif technique in ("saw", "supersaw", "brass", "strings", "bass_saw", "sync"):
        L.append(f"  {ov}    vco2 0.6, kfreq, 0          ; band-limited sawtooth")
    elif technique in ("fm_bell", "fm", "fm_ep", "metallic_fm", "sync"):
        # FM via foscili: an inharmonic-ish carrier:modulator ratio gives the
        # bell/metal sideband spectrum natively (no partial table).
        car, mod, ndx = ("1", "1.41", "3.2") if technique in ("fm_bell", "metallic_fm") else ("1", "2", "1.8")
        L.append(f"  {ov}    foscili 0.5, kfreq, {car}, {mod}, {ndx}, giSine ; FM (foscili)")
    elif technique == "cheby":
        # Chebyshev/tanh waveshaping of a sine = polynomial harmonics (real
        # waveshaper, the substrate doing dirt natively).
        L.append(f"  adrv{tag}    oscili 0.9, kfreq")
        L.append(f"  {ov}    = tanh(adrv{tag} * 3.0) * 0.5    ; waveshaper harmonics")
    else:
        # sine / additive / theremin / sub_sine / flute / organ / glass and
        # anything not given a bespoke idiom yet: render its spectrum additively
        # (real oscils at true ratios), defaulting to a pure sine.
        spec = _SPECTRA.get(_MORPH_SPECTRUM.get(technique, "sine"), _SPECTRA["sine"])
        L.append(_emit_additive(spec, gain=0.6, tag=tag))
    return "\n".join(L)


def _emit_additive(spectrum, gain=0.6, tag="0"):
    """A static additive bank -> `aosc<tag>`: one `oscili` per partial at
    kfreq*ratio (partials named `a<tag>p<i>`). LEGITIMATE here (a genuinely
    inharmonic tone summed at its true partial frequencies is what Csound additive
    synthesis is for) — the regression's sin was using this for EVERYTHING incl.
    pwm/FM/dirt, not additive per se."""
    lines = []
    total = sum(a for _, a in spectrum) or 1.0
    scale = gain / total
    terms = []
    for i, (ratio, amp) in enumerate(spectrum):
        lines.append(f"  a{tag}p{i:<2d}   oscili {amp*scale:.4f}, kfreq * {ratio:.4f}")
        terms.append(f"a{tag}p{i}")
    lines.append(f"  aosc{tag}    = " + " + ".join(terms))
    return "\n".join(lines)


def _emit_morph(technique_keys, imorphtime, tag="0"):
    """A genuine spectral MORPH between idioms -> `aosc<tag>`: ONE additive bank
    whose per-partial amplitudes AND frequency ratios travel A->B over
    `imorphtime`, (re)started per note off the trig epoch. This is NOT two
    instances crossfaded (that beats/cancels and plays both at once) — it is the
    SAME oscillators with moving parameters, so the spectrum genuinely transforms.
    Multi-stage chains (a > b > c) split the time into equal legs via one linseg
    across stages. A `silence`/`zero` terminal fades every partial to 0 (a clean
    transient / pseudo-env). Every symbol is suffixed with `tag` and the reinit
    label is `Lmorph<tag>`, so oscillators never collide."""
    # resolve each technique key to a spectrum reading; unknown endpoint -> the
    # additive default (a morph NEVER silently collapses to a steady tone).
    stages = []
    for k in technique_keys:
        sk = _MORPH_SPECTRUM.get(k, _DEFAULT_MORPH_SPECTRUM)
        stages.append(_SPECTRA[sk])
    if len(stages) < 2:
        return None  # <2 endpoints given; caller falls back to steady (never
        #              a silent collapse -- every key yields a stage above)

    # align all stages to a common partial count (union by index); pad a shorter
    # stage with amp-0 partials that hold the previous ratio, so a fading partial
    # never sweeps its frequency audibly.
    n = max(len(s) for s in stages)
    aligned = []
    for s in stages:
        padded = list(s)
        while len(padded) < n:
            padded.append((padded[-1][0], 0.0))
        aligned.append(padded)

    # normalize each stage's gain to a common budget so loudness stays ~constant
    # across the morph (a spectral morph must not read as a volume change). A
    # zero/silence stage sums to 0 -> guarded to 1.0 -> amps stay 0 (fade-out).
    def norm(stage):
        tot = sum(a for _, a in stage) or 1.0
        return [(r, a / tot) for r, a in stage]
    aligned = [norm(s) for s in aligned]

    nlegs = len(stages) - 1
    leg = imorphtime / nlegs

    lbl = f"Lmorph{tag}"
    L = []
    L.append(f"  ; --- osc {tag}: per-note spectral morph (trig-epoch reinit; spectral, NOT amp env) ---")
    L.append("  if changed2(ktrig) == 1 then")
    L.append(f"    reinit {lbl}")
    L.append("  endif")
    L.append(f"{lbl}:")
    terms = []
    for i in range(n):
        # ONE linseg per partial through every stage's value, equal legs. linseg
        # natively does the piecewise-linear breakpoint walk and restarts on
        # reinit, so partial i's amplitude AND ratio travel A->B(->C...) per note.
        amps = ", ".join(f"{aligned[j][i][1]:.4f}, {leg:.4f}" for j in range(nlegs))
        amps += f", {aligned[nlegs][i][1]:.4f}"
        rats = ", ".join(f"{aligned[j][i][0]:.4f}, {leg:.4f}" for j in range(nlegs))
        rats += f", {aligned[nlegs][i][0]:.4f}"
        L.append(f"  k{tag}a{i:<2d}   linseg {amps}")
        L.append(f"  k{tag}r{i:<2d}   linseg {rats}")
        L.append(f"  a{tag}p{i:<2d}   oscili k{tag}a{i} * 0.6, kfreq * k{tag}r{i}")
        terms.append(f"a{tag}p{i}")
    L.append("  rireturn")
    L.append(f"  aosc{tag}    = " + " + ".join(terms))
    return "\n".join(L)


def _emit_oscillator(oi, chain, imorphtime):
    """One oscillator (index `oi`, 0..2) from its technique chain -> (body_lines,
    out_var). >=2 stages -> a spectral morph; otherwise a steady technique. The
    out_var is `aosc<oi>`, mixed by build_orchestra."""
    tag = str(oi)
    body = None
    if len(chain) >= 2:
        body = _emit_morph(chain, imorphtime, tag)
    if body is None:
        body = _emit_steady(chain[0] if chain else "sine", tag)
    return body, f"aosc{tag}"


def _emit_adjectives(adjective_keys):
    """Post-process `asig` in place for a few slice adjectives. Real DSP: dirt is
    a tanh waveshaper (new harmonics), bright/dark a one-pole tilt. Unknown
    adjectives are silently no-ops (they were validated as real lexicon keys;
    the slice just has no idiom for them yet)."""
    L = []
    s = set(adjective_keys)
    if s & {"distorted", "dirty", "aggressive", "growling", "harsh"}:
        L.append("  asig     = tanh(asig * 2.6) * 0.7    ; overdrive/dirt (real harmonics)")
    if "bright" in s or "sharp" in s or "piercing" in s:
        L.append("  asig     tone asig, 7000             ; bright tilt")
    elif "dark" in s or "muddy" in s or "dull" in s or "warm" in s:
        L.append("  asig     tone asig, 900              ; dark tilt")
    return "\n".join(L)


def _emit_motion(motion_key):
    """A k-rate motion so 'moving' prompts actually move (movement by default).

    The spectral family (sweep/evolve/open_up/close/breathe/wobble/cycle) drives
    an LFO-swept waveshaper: the drive continuously grows and retracts upper
    harmonics, so the spectrum opens and closes over time on ANY source — a pure
    sine or sub bass included. The earlier idiom was a lowpass cutoff sweep, which
    is INAUDIBLE on a spectrally-poor source (nothing above the cutoff to remove);
    the frozen NL corpus caught 'evolving analog drone', 'wobbling acid bass' and
    'a bass that slowly opens up' all rendering static because the 7B routed them
    onto sub_sine/sine. A waveshaper that MAKES harmonics move is source-agnostic.

    The shimmer family (shimmer/vibrate/flutter/tremolo) stays a fast amplitude
    tremolo (already source-agnostic)."""
    if not motion_key or motion_key == "static":
        return ""
    if motion_key in ("sweep", "evolve", "open_up", "close", "breathe", "wobble", "cycle"):
        # faster, shallower for the periodic wobble/cycle; slow & deep for the
        # directional-feel evolve/open family (still periodic = keeps living).
        # `balance` re-scales the waveshaped signal to the pre-shape RMS every
        # k-frame, so the harmonic content travels (genuine SPECTRAL motion) but
        # loudness holds steady -- this is NOT the shimmer family's amplitude
        # tremolo. Without it, tanh(x*kdrv) ~= x*kdrv at small signal, so the
        # drive LFO would pump overall level ~2.4x (an evolving PAD must not throb
        # in volume; that belongs to shimmer/tremolo).
        rate = {"wobble": 2.2, "cycle": 1.1}.get(motion_key, 0.16)
        return (f"  kmot     oscili 0.5, {rate}             ; -0.5..0.5 motion LFO\n"
                "  kdrv     = 1.0 + 3.4 * (kmot + 0.5)   ; waveshaper drive 1.0..4.4\n"
                "  awsh     = tanh(asig * kdrv)          ; harmonics grow & retract\n"
                "  asig     balance awsh, asig           ; steady loudness (spectral, not tremolo)")
    if motion_key in ("shimmer", "vibrate", "flutter", "tremolo"):
        return ("  ksh      oscili 0.18, 5.5\n"
                "  asig     = asig * (1 + ksh)           ; fast shimmer")
    return ""


def _normalize_oscs(technique_keys, oscs):
    """Resolve the two call conventions to a clean list of up to 3 oscillator
    specs [{chain:[keys], vol:float}]. Back-compat: a bare technique_keys list is
    one oscillator at vol 1.0 (byte-for-byte the pre-M2 single-osc emission).
    An oscillator whose chain is ONLY silence produces nothing and is dropped; if
    every osc drops, fall back to a single sine so the orchestra is never empty."""
    if oscs is None:
        oscs = [{"chain": list(technique_keys or []), "vol": 1.0}]
    out = []
    for o in oscs:
        chain = [k for k in (o.get("chain") or []) if k]
        # silence only as a terminal fade-out (never a leading/middle fade-in).
        chain = _strip_nonterminal_silence(chain)
        if not chain:
            chain = ["sine"]
        if all(k in ("silence", "zero") for k in chain):
            continue  # an all-silent oscillator adds nothing
        try:
            vol = float(o.get("vol", 1.0))
        except (TypeError, ValueError):
            vol = 1.0
        out.append({"chain": chain, "vol": max(0.0, min(1.0, vol))})
        if len(out) == 3:
            break
    # positive-weight floor: a muted supporting layer (vol~0) is dropped when some
    # other layer is audible; if EVERY layer is muted (a VOL 0.0 / negative the 7B
    # emitted -- an in-range but degenerate volume), the mix would be dead silent
    # and shipped as ok=True (adversarial review, fundamental-3 violation). Play
    # the timbres the model chose at unity instead of emitting a silent instrument.
    audible = [o for o in out if o["vol"] > 1e-4]
    if audible:
        out = audible
    elif out:
        for o in out:
            o["vol"] = 1.0
    if not out:
        out = [{"chain": ["sine"], "vol": 1.0}]
    return out


def build_orchestra(technique_keys=None, adjective_keys=None, motion_key=None,
                    morph_sec=None, oscs=None):
    """keys -> (orchestra_text, reading). Two conventions:
      * single osc: technique_keys is a list (>=2 => morph chain), vol implied 1.0
      * up to 3 osc: oscs=[{chain:[keys], vol:float}, ...] (each its own morph
        chain + volume; a chain may end in `silence` for a morph-to-zero transient)
    Deterministic. The returned text carries `sr = %SR%` for the engine to
    substitute at its real rate."""
    adjective_keys = [k for k in (adjective_keys or []) if k]
    imorphtime = float(morph_sec) if morph_sec else DEFAULT_MORPH_SEC
    oscs = _normalize_oscs(technique_keys, oscs)

    # emit each oscillator (tagged, collision-free) and build the weighted mix.
    # The mix gain is 1/max(1, sum_vol): per-osc vol is a RELATIVE weight, so
    # overall loudness stays ~constant as layers are added (a "sensible overall
    # mix" -- adding a layer enriches the timbre without a loudness jump), and a
    # single osc at vol 1.0 is unchanged (gain 1.0). HEADROOM still bounds it.
    bodies = []
    mix_terms = []
    sum_vol = sum(o["vol"] for o in oscs) or 1.0
    mgain = 1.0 / max(1.0, sum_vol)
    for oi, o in enumerate(oscs):
        body, outv = _emit_oscillator(oi, o["chain"], imorphtime)
        bodies.append(body)
        w = o["vol"] * mgain
        if abs(w - 1.0) < 1e-6:
            mix_terms.append(outv)
        else:
            mix_terms.append(f"{w:.4f} * {outv}")
    body = "\n".join(bodies)
    # collapse the per-osc outputs into `asig` (what adjectives/motion/tail read).
    # Single unit-weight osc -> `asig = aosc0` (one a-rate copy, sound identical
    # to pre-M2); multi-osc -> the weighted sum.
    mix = "  asig     = " + " + ".join(mix_terms)

    adj = _emit_adjectives(adjective_keys)
    mot = _emit_motion(motion_key)

    head = (
        "<CsoundSynthesizer>\n<CsOptions>\n-n -d\n</CsOptions>\n<CsInstruments>\n"
        "sr = %SR%\n"
        f"ksmps = {KSMPS}\n"
        f"nchnls = {NCHNLS}\n"
        "0dbfs = 1\n"
        "giSine ftgen 1, 0, 65536, 10, 1\n\n"
        "instr 1\n"
        "  ivoice   = p4\n"
        "  Sgate    sprintf \"gate%d\", ivoice\n"
        "  Sfreq    sprintf \"freq%d\", ivoice\n"
        "  Svel     sprintf \"vel%d\", ivoice\n"
        "  Spres    sprintf \"pres%d\", ivoice\n"
        "  Stimb    sprintf \"timb%d\", ivoice\n"
        "  Strig    sprintf \"trig%d\", ivoice\n"
        "  kgateraw chnget Sgate\n"
        "  kfreqraw chnget Sfreq\n"
        "  kvel     chnget Svel\n"
        "  kpres    chnget Spres\n"
        "  ktimb    chnget Stimb\n"
        "  ktrig    chnget Strig\n"
        "  kgate    portk kgateraw, 0.001\n"
        "  kfreq    limit kfreqraw, 20, 12000\n"
        "  kpresGain = 1.0 + 0.15 * kpres\n\n"
    )
    tail = (
        "\n"
        f"  aout     = asig * kgate * kvel * kpresGain * {HEADROOM}\n"
        "  outch    ivoice, aout\n"
        "endin\n"
        "</CsInstruments>\n<CsScore>\n"
    )
    score = "".join(f"i 1 0 360000 {v}\n" for v in range(1, NCHNLS + 1))
    score += "e 360000\n</CsScore>\n</CsoundSynthesizer>\n"

    parts = [head, body, mix]
    if adj:
        parts.append(adj)
    if mot:
        parts.append(mot)
    orchestra = "\n".join(p for p in parts if p) + tail + score
    return orchestra, _reading(oscs, adjective_keys, motion_key)


def build_csound_response(text, llm):
    """Prompt -> live Csound orchestra, the REAL pipeline entry (pipe_inference
    mode=="csound" calls this). One 7B instruct call routes the whole prompt to
    closed-enum keys under the csound-OWNED multi-oscillator schema (up to 3
    oscillators, each its own "a > b" morph chain + volume, a chain may end in
    `silence` for a morph-to-zero transient; plus whole-sound adjectives + motion).
    The KEY LISTS are the shared lexicon catalogue (dco_llm_map._build_catalogue)
    plus the csound-local technique extension; validation reuses dco_llm_map's
    closed-enum injection guard. build_orchestra then renders the oscillators as
    real Csound idioms. LLM-first, no fallback: if nothing maps, return an honest
    ok=false rather than a junk tone.

    ``llm`` is the caller-injected ``(text, system_prompt, max_new_tokens) ->
    str`` callable (same convention as dco_llm_map / dco_recipe). Returns the
    {ok, orchestra, reading, error} shape the C++ PipeInference parses."""
    import dco_recipe
    import dco_llm_map

    try:
        lexicon = dco_recipe.load_lexicon()

        def _canon(entries):
            m = {}
            for e in entries:
                m[e["key"]] = e["key"]
                for sf in e.get("surface_forms", []):
                    m[str(sf).strip().lower()] = e["key"]
            return m

        # csound-local technique extension (glass, ...): fold into a shallow copy
        # of the lexicon so it lands IN the TECHNIQUES section of the catalogue
        # (the 7B reads it as a technique) and IN the technique canon (validation
        # accepts it). The shared lexicon object is never mutated.
        lex_cs = dict(lexicon)
        lex_cs["techniques"] = list(lexicon["techniques"]) + [
            {"key": k, "why": m["why"], "surface_forms": m.get("surface_forms", [])}
            for k, m in _CS_TECH_EXTRA.items()
        ]
        tcanon = _canon(lex_cs["techniques"])
        # validation-only chain terminals (silence): accepted if the 7B emits them
        # per the SILENCE RULE, but never listed as a pickable catalogue technique.
        for key, forms in _CS_TERMINALS.items():
            tcanon[key] = key
            for sf in forms:
                tcanon[str(sf).strip().lower()] = key
        acanon = _canon(lexicon["adjectives"])
        mcanon = _canon(lexicon["motions"])

        # csound-OWNED multi-oscillator schema (up to 3 osc, each its own morph
        # chain + volume); the key LISTS still come from the shared catalogue.
        system_prompt = _CS_SYSTEM_PROMPT_HEAD + dco_llm_map._build_catalogue(lex_cs)
        raw = llm(text, system_prompt, _CS_MAX_NEW_TOKENS)

        osc_specs, adjectives_raw, motion_raw = _parse_csound_reply(raw)

        # validate each oscillator's chain independently (morph-chain / compound /
        # pwm-collapse handled per osc), keep its volume; drop an osc that yields
        # no valid key. Cap at 3 (the schema promises up to three).
        oscs, flags = [], []
        for chain_raw, vol in osc_specs[:3]:
            keys, kflags = _validate_osc_chain(chain_raw, tcanon, dco_llm_map)
            flags += kflags
            # keep only oscillators with real content; an all-silence chain (the
            # 7B emitting "silence" with no source) produces nothing -> drop it so
            # the response's oscillator list matches what build_orchestra renders.
            # Clamp vol to [0,1] here too so the reported metadata equals the mix.
            if keys and not all(k in ("silence", "zero") for k in keys):
                try:
                    v = max(0.0, min(1.0, float(vol)))
                except (TypeError, ValueError):
                    v = 1.0
                oscs.append({"chain": keys, "vol": v})

        adjective_keys, aflags = dco_llm_map._validate_keys(
            adjectives_raw.split(",") if adjectives_raw else [], acanon)
        _motion_keys, mflags = dco_llm_map._validate_keys(
            [motion_raw] if motion_raw else [], mcanon)
        motion_key = _motion_keys[0] if _motion_keys else None
        flags += aflags + mflags

        # LLM-first, no fallback: nothing mapped at all -> honest failure frame.
        if not oscs and not adjective_keys and not motion_key:
            return {"ok": False,
                    "error": "no synthesis idiom matched the prompt"}

        orchestra, reading = build_orchestra(oscs=oscs, adjective_keys=adjective_keys,
                                             motion_key=motion_key)
        # echo the oscillators as ACTUALLY rendered (post terminal-silence strip,
        # vol clamp, muted-drop / unity-promotion) so the metadata never lies about
        # what the orchestra plays.
        rendered = _normalize_oscs(None, oscs)
        return {
            "ok": True,
            "orchestra": orchestra,
            "reading": reading,
            "oscillators": rendered,
            # legacy fields (the first oscillator) so existing UI / tests that read
            # technique/adjectives/motion keep working.
            "technique": rendered[0]["chain"] if rendered else [],
            "adjectives": adjective_keys,
            "motion": motion_key,
            "flags": flags,
        }
    except Exception as e:  # honest failure frame — never a silent junk tone
        return {"ok": False, "error": f"Csound authoring failed: {e}"}


if __name__ == "__main__":
    # quick manual smoke: print a few orchestras (for eyeballing / piping to the
    # csound_orch_check compile gate — NOT a demo-WAV proof).
    import sys
    cases = {
        "pwm": (["pwm"], [], None),
        "pwm_dirty": (["pwm"], ["distorted"], None),
        "fm_bell": (["fm_bell"], [], None),
        "glass_to_sine": (["glass", "sine"], ["glassy"], None),
        "saw_bright_sweep": (["saw"], ["bright"], "sweep"),
    }
    which = sys.argv[1] if len(sys.argv) > 1 else "glass_to_sine"
    orc, reading = build_orchestra(*cases[which])
    sys.stderr.write(f"; reading: {reading}\n")
    sys.stdout.write(orc)
