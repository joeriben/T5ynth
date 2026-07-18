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
    # ring modulation of a carrier by a 2x modulator = 1/2[cos(f) - cos(3f)]: a
    # sparse two-partial product, energy only at f and 3f. Its OWN morph reading
    # (the steady _emit_steady renders exactly this product); do NOT reuse fm's
    # dense harmonic series (that misrepresents what ring_mod actually plays).
    "ring_mod": [(1.00, 1.00), (3.00, 1.00)],
    # --- M3 consolidation: real spectra for former gap techniques (lexicon "why"
    #     partial sets honoured verbatim where given). ---
    # tuned struck metal bar (music box/glocken/celesta/kalimba): ideal free-free
    # bar partials h 2.76/5.4/8.93/13.34, brighter & THINNER than the big bell
    # (weaker fundamental, more high energy).
    "struck_bar": [(1.00, 0.85), (2.76, 0.70), (5.40, 0.50), (8.93, 0.32), (13.34, 0.18)],
    # dense bright inharmonic plate wash (cymbal/crash/ride/hi-hat): weak
    # fundamental, energy piled into dense OFF-GRID upper partials (an additive
    # sketch of a 2-D plate's modal spectrum, per the lexicon; true noise = later).
    "cymbal":   [(1.00, 0.22), (2.19, 0.38), (3.41, 0.55), (4.83, 0.72), (6.37, 0.88),
                 (8.09, 1.00), (10.24, 0.86), (12.71, 0.64), (15.83, 0.42)],
    # classic drawbar harmonic set {1,2,3,4,6,8} at typical registration levels.
    "organ":    [(1.00, 1.00), (2.00, 0.70), (3.00, 0.50), (4.00, 0.40), (6.00, 0.30), (8.00, 0.25)],
    # near-pure tone with a faint 2nd/3rd (breath noise not modelled here).
    "flute":    [(1.00, 1.00), (2.00, 0.12), (3.00, 0.05)],
    # bright, plucky additive spectrum (many odd+even partials, slow rolloff).
    "harpsichord": [(1.00, 1.00), (2.00, 0.72), (3.00, 0.56), (4.00, 0.44), (5.00, 0.34),
                    (6.00, 0.26), (7.00, 0.20), (8.00, 0.15)],
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
    "additive": "additive",
    "organ": "organ", "flute": "flute", "harpsichord": "harpsichord",
    "glass": "glass",
    "fm_bell": "bell", "metallic_fm": "bell",
    "struck_bar": "struck_bar", "cymbal": "cymbal",
    "fm": "fm", "fm_ep": "fm", "ring_mod": "ring_mod",
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
    # --- M4a: noise / texture (Geräusche). Substrate-native NOISE beds (rand/
    #     pinkish + real filters), NOT additive fakes. Pitch-independent textures;
    #     movement comes from the motion layer. Rendered by _emit_noise. ---
    # Substrate-native NOISE beds (rand/pinkish + real filters). The "why" text is
    # deliberately CONCISE: a small 7B routes these best on short, plain glosses --
    # verbose "continuous/sustained/NOISE (not a tone)" edits DESTABILISED routing
    # (heavy rain -> pulse, ocean -> cymbal; empirically re-tested 2026-07-18). The
    # rain/surf "> silence" over-append and thunder miss are handled at the SILENCE
    # RULE / example layer, not by loading these lines.
    "noise": {
        "why": "full-spectrum white noise (rand) — a flat hiss/wash",
        "surface_forms": ["white noise", "noise", "white-noise", "rauschen", "weißes rauschen"],
    },
    "pink_noise": {
        "why": "pink noise (pinkish, 1/f tilt) — softer, warmer broadband hiss",
        "surface_forms": ["pink noise", "pink-noise", "rosa rauschen"],
    },
    "wind": {
        "why": "wind: wide band-passed noise, a breathy airy howl (no pitch)",
        "surface_forms": ["wind", "howling wind", "breeze", "gust", "windig", "wind noise"],
    },
    "rain": {
        "why": "rain: bright high-passed noise, a fine hiss/patter",
        "surface_forms": ["rain", "rainfall", "drizzle", "regen", "raindrops"],
    },
    "surf": {
        "why": "surf/ocean: broad low-mid noise wash, a rolling sea",
        "surface_forms": ["surf", "ocean", "sea", "waves", "meer", "wellen"],
    },
    "thunder": {
        # mirror the working "wind:"/"rain:" pattern EXACTLY — a SINGLE keyword +
        # colon (no "thunder/rumble:" slash-compound, which the 7B copied whole as
        # the key; and no gloss-first, which lost the pick to cymbal). 2026-07-18.
        "why": "thunder: low broadband-noise rumble, a deep distant storm",
        "surface_forms": ["thunder", "rolling thunder", "distant thunder", "thunderclap",
                          "storm", "rumble", "rumbling", "donner", "grollen"],
    },
    "hiss": {
        "why": "hiss/static: bright high-passed noise, tape/radio static",
        "surface_forms": ["hiss", "static", "tape hiss", "radio static", "zischen"],
    },
    "crackle": {
        "why": "crackle/fire: randomly gated noise, a crackling campfire",
        "surface_forms": ["crackle", "crackling", "fire", "campfire", "knistern", "feuer"],
    },
}

# Techniques rendered by the substrate NOISE path (_emit_noise), not the additive/
# opcode steady path. They are aperiodic textures -- the behavioral gate's noise
# check (pitchedness < 0.4) certifies each is really NOISE, not a pitched tone.
_NOISE_TECH = {"noise", "pink_noise", "wind", "rain", "surf", "thunder",
               "hiss", "crackle"}

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
    "silence. A continuous texture (wind, rain, sea, fire, noise) does not fade on "
    "its own; append silence ONLY when the prompt itself says the sound fades, "
    "decays, or stops.\n"
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

def _emit_noise(technique, tag="0"):
    """Substrate-native NOISE textures (M4a Geräusche) -> `aosc<tag>` via Csound's
    real noise generators (rand / pinkish) + real filters -- NOT an additive fake
    (an additive partial bank cannot be noise; that is the toy-vocabulary trap).
    Pitch-independent beds (a texture plays the same at every key; movement is the
    motion layer's job). Every branch is bounded (the tail limiter is the final
    safety) and verified aperiodic by the gate's noise check (pitchedness < 0.4).
    Filter cutoffs are tuned to stay broadband: a too-narrow low-pass makes noise
    quasi-periodic (a 1-pole `tone` at 400 rumbles; a 2-pole at 300 reads pitched)."""
    ov = f"aosc{tag}"
    nz = f"anz{tag}"
    L = [f"  {nz}    rand 1.0                   ; broadband noise source"]
    if technique == "noise":
        L.append(f"  {ov}    = {nz} * 0.6                ; white noise (flat hiss)")
    elif technique == "pink_noise":
        L.append(f"  {ov}    pinkish {nz}               ; pink noise (1/f tilt)")
    elif technique == "wind":
        L.append(f"  {ov}    reson {nz}, 600, 400, 2     ; wind: wide band-passed noise")
        L.append(f"  {ov}    = {ov} * 0.5")
    elif technique == "rain":
        L.append(f"  a{tag}rn  atone {nz}, 1500          ; rain: bright high-passed hiss")
        L.append(f"  {ov}    = a{tag}rn * 0.5")
    elif technique == "surf":
        L.append(f"  {ov}    tone {nz}, 900              ; surf/ocean: broad low-mid wash")
        L.append(f"  {ov}    = {ov} * 0.7")
    elif technique == "thunder":
        L.append(f"  {ov}    tone {nz}, 400              ; thunder/rumble: low broadband")
        L.append(f"  {ov}    = {ov} * 0.9")
    elif technique == "hiss":
        L.append(f"  {ov}    atone {nz}, 6000            ; hiss/static: bright high noise")
        L.append(f"  {ov}    = {ov} * 0.7")
    elif technique == "crackle":
        L.append(f"  k{tag}cr  randh 1, 30                ; 30 Hz random gate")
        L.append(f"  {ov}    = {nz} * (k{tag}cr > 0.6 ? 1 : 0.15) * 0.6 ; crackle/fire")
    else:
        L.append(f"  {ov}    = {nz} * 0.6")
    return "\n".join(L)


def _emit_steady(technique, tag="0"):
    """A single (non-morph) technique -> `aosc<tag>`. Uses Csound's native
    opcodes; every temporary is suffixed with `tag` (per-osc uniqueness)."""
    ov = f"aosc{tag}"
    L = []
    if technique in _NOISE_TECH:
        return _emit_noise(technique, tag)
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
    elif technique == "ring_mod":
        # ring modulation: carrier * modulator at a 2:1 ratio -> sum/difference
        # sidebands (the closed-form ring/AM spectrum). The substrate doing RM
        # natively (a genuine product, not a partial table).
        L.append(f"  acar{tag}    oscili 0.8, kfreq")
        L.append(f"  amod{tag}    oscili 1.0, kfreq * 2.0")
        L.append(f"  {ov}    = acar{tag} * amod{tag}          ; ring modulation (2:1 sidebands)")
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

    # align all stages to a common partial count (union by index). A stage that
    # lacks partial i is padded with an amp-0 partial that holds the ratio of the
    # NEAREST stage ALONG THE CHAIN that actually has partial i -- so every fade leg
    # (a partial audible at one end, silent at the other) is frequency-FLAT: the
    # partial fades in/out at its own frequency and never glisses. Only real->real
    # legs (both ends audible) interpolate ratios, which is the intended spectral
    # morph. A single GLOBAL reference is wrong for >=2 legs: in saw>square>sine the
    # shortest stage (sine) would take saw's grid, so square's harmonics glissed
    # 3->2 etc. as they faded in leg 2 (adversarial review 2026-07-18). Per-leg
    # "nearest real stage" holds the fade-neighbor's ratio and fixes 3+ stage chains
    # (2-stage is unchanged: the nearest real stage IS the counterpart).
    n = max(len(s) for s in stages)
    m = len(stages)
    aligned = []
    for j, s in enumerate(stages):
        row = list(s)
        while len(row) < n:
            i = len(row)
            nearest = min((k for k in range(m) if i < len(stages[k])),
                          key=lambda k: (abs(k - j), k))
            row.append((stages[nearest][i][0], 0.0))
        aligned.append(row)

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


def _emit_noise_morph(chain, imorphtime, tag="0"):
    """A crossfade morph for chains that CONTAIN a NOISE texture -> `aosc<tag>`.
    Noise is aperiodic and cannot be an additive partial bank, so the tonal
    additive-partial morph (_emit_morph) would silently degrade the noise leg to a
    PITCHED additive tone -- a migration-discipline capability loss the frozen
    corpus caught (2026-07-18: 'rain > silence' rendered an additive tone that
    faded, pitchedness 0.99, not rain). Here each stage renders to its OWN audio
    var (noise stages via _emit_noise, tonal stages via _emit_steady, silence -> no
    signal) and a per-stage EQUAL-POWER 'tent' gain crossfades between adjacent
    stages, restarted per note off the trig epoch. Crossfading noise is click-free
    (incoherent sources do not beat), so an amplitude crossfade -- not the additive
    morph -- is the correct morph whenever noise is involved. The gains are the
    SQRT of the linear tents (equal-POWER): incoherent noise sums in power, so
    linear-amplitude gains would sag ~-3 dB at each crossover midpoint (g0=g1=0.5 ->
    power 0.5); sqrt gains keep g0^2+g1^2 = 1 across every leg, holding loudness
    constant like _emit_morph's per-stage norm() budget (adversarial review
    2026-07-18). The whole thing stays bounded (the tail limiter is the final
    safety)."""
    n = len(chain)
    if n < 2:
        return None
    leg = imorphtime / (n - 1)
    lbl = f"Lnzmorph{tag}"
    L = [f"  ; --- osc {tag}: noise crossfade morph (real noise per stage, trig-epoch reinit) ---"]
    stage_vars = []
    for j, k in enumerate(chain):
        if k in ("silence", "zero"):
            stage_vars.append(None)   # terminal fade: neighbours' tents carry it to 0
            continue
        sub = f"{tag}m{j}"
        L.append(_emit_noise(k, sub) if k in _NOISE_TECH else _emit_steady(k, sub))
        stage_vars.append(f"aosc{sub}")
    L.append("  if changed2(ktrig) == 1 then")
    L.append(f"    reinit {lbl}")
    L.append("  endif")
    L.append(f"{lbl}:")
    terms = []
    for j, var in enumerate(stage_vars):
        if var is None:
            continue
        # linear tent: 1 at breakpoint j, 0 at every other breakpoint (adjacent
        # stages cross). n values, n-1 legs of equal duration. In any leg exactly
        # two adjacent tents are non-zero and sum to 1, so sqrt(tent) gives an
        # equal-POWER crossfade (sum of squares = 1) -- flat loudness for incoherent
        # noise. sqrt is safe: linseg stays in [0,1].
        seg = ", ".join(f"{(1.0 if b == j else 0.0):.4f}, {leg:.4f}" for b in range(n - 1))
        seg += f", {(1.0 if (n - 1) == j else 0.0):.4f}"
        lj, gj = f"k{tag}L{j}", f"k{tag}g{j}"
        L.append(f"  {lj}   linseg {seg}")
        L.append(f"  {gj}   = sqrt({lj})")
        terms.append(f"{var} * {gj}")
    L.append("  rireturn")
    L.append(f"  aosc{tag}    = " + (" + ".join(terms) if terms else "0"))
    return "\n".join(L)


def _emit_oscillator(oi, chain, imorphtime):
    """One oscillator (index `oi`, 0..2) from its technique chain -> (body_lines,
    out_var). >=2 stages -> a morph (a NOISE-crossfade morph if any stage is a
    noise texture, else the tonal additive-partial morph); otherwise a steady
    technique. The out_var is `aosc<oi>`, mixed by build_orchestra."""
    tag = str(oi)
    body = None
    if len(chain) >= 2:
        if any(k in _NOISE_TECH for k in chain):
            body = _emit_noise_morph(chain, imorphtime, tag)
        else:
            body = _emit_morph(chain, imorphtime, tag)
    if body is None:
        body = _emit_steady(chain[0] if chain else "sine", tag)
    return body, f"aosc{tag}"


# M3 adjective consolidation: every lexicon adjective maps to one or more of a
# small set of BOUNDED, in-place DSP operations on the mixed `asig`, chosen to
# honour the lexicon's own "why". Operations (emit order below):
#   BODY  g      -> add a low-passed copy (weight below): `asig + tone(asig,220)*g`
#   THIN  fc     -> high-pass out the body (thinner): `atone asig, fc`
#   FORMANT (fc,g) -> a peak-normalised reson bump (nasal/reedy/boxy/resonant)
#   METAL g      -> a high inharmonic-band reson emphasis (metallic/clangorous)
#   DARK  fc     -> one-pole low-pass tilt (dark/warm/mellow/...)
#   BRIGHT s     -> add a high-passed copy (more upper energy): `asig + atone(asig,2200)*s`
#   DIRT (k,g)   -> tanh waveshaper = REAL new harmonics (dirty/distorted/buzzy/...)
#   AIR   g      -> add high-passed noise (airy/breathy breath air)
#   DRIFT d      -> a slow amplitude micro-wobble (analog/old life)
# Every op is bounded and the tail HEADROOM (0.32) leaves ample margin even when
# several stack; the behavioral gate's swept probe verifies no combination clips.
_ADJ_MAP = {
    "bright":     [("BRIGHT", 0.70)],
    "dark":       [("DARK", 1200)],
    "warm":       [("DARK", 1800), ("BODY", 0.30)],
    "hollow":     [("FORMANT", (700, 0.35)), ("DARK", 3000)],
    "nasal":      [("FORMANT", (1300, 0.50))],
    "fat":        [("BODY", 0.50)],
    "thin":       [("THIN", 350)],
    "buzzy":      [("DIRT", (2.0, 0.75)), ("BRIGHT", 0.40)],
    "metallic":   [("METAL", 0.50)],
    "smooth":     [("DARK", 2600)],
    "shimmering": [("BRIGHT", 0.35), ("AIR", 0.12)],
    "airy":       [("THIN", 300), ("AIR", 0.20)],
    "harsh":      [("DIRT", (3.2, 0.70)), ("BRIGHT", 0.50)],
    "woody":      [("DARK", 1900), ("FORMANT", (600, 0.30))],
    "deep":       [("BODY", 0.60), ("DARK", 900)],
    "glassy":     [("BRIGHT", 0.50), ("METAL", 0.25)],
    "brittle":    [("THIN", 400), ("BRIGHT", 0.55), ("METAL", 0.20)],
    "clangorous": [("METAL", 0.70)],
    "growling":   [("DIRT", (2.6, 0.70)), ("DARK", 1600)],
    "punchy":     [("BRIGHT", 0.40), ("BODY", 0.25)],
    "mellow":     [("DARK", 2000)],
    "sharp":      [("BRIGHT", 0.85)],
    "round":      [("DARK", 2200)],
    "cold":       [("BRIGHT", 0.60)],
    "dirty":      [("DIRT", (2.6, 0.70))],
    "clean":      [("DARK", 6000)],
    "aggressive": [("DIRT", (2.8, 0.70)), ("BRIGHT", 0.50)],
    "gentle":     [("DARK", 1700)],
    "brassy":     [("FORMANT", (1600, 0.45)), ("BRIGHT", 0.30)],
    "breathy":    [("THIN", 400), ("AIR", 0.22)],
    "crisp":      [("BRIGHT", 0.55)],
    "muddy":      [("DARK", 700), ("BODY", 0.35)],
    "resonant":   [("FORMANT", (1100, 0.55))],
    "full":       [("BODY", 0.45), ("BRIGHT", 0.20)],
    "thick":      [("BODY", 0.55)],
    "raspy":      [("DIRT", (2.2, 0.72))],
    "piercing":   [("BRIGHT", 0.90)],
    "velvety":    [("DARK", 2100), ("BODY", 0.25)],
    "icy":        [("BRIGHT", 0.65), ("AIR", 0.15)],
    "boxy":       [("FORMANT", (500, 0.45)), ("DARK", 2000)],
    "reedy":      [("FORMANT", (1200, 0.40)), ("DIRT", (1.8, 0.80))],
    "dull":       [("DARK", 650)],
    "vibrant":    [("BRIGHT", 0.50), ("BODY", 0.20)],
    "flat":       [("DARK", 1300)],
    "rich":       [("BRIGHT", 0.30), ("DIRT", (1.6, 0.85))],
    "sparse":     [("DARK", 1500)],
    "edgy":       [("DIRT", (2.0, 0.75))],
    "distorted":  [("DIRT", (3.4, 0.68))],
    # 'analog' = the lexicon's authorized amplitude wobble + softened highs
    # (dco_lexicon 'why': "slow coherent drift ... amplitude wobble"). 'old' is,
    # per its own 'why', "progressive high-partial erosion + light smear" with a
    # tape wow that is a slow PITCH wobble "flagged as not carried on the baked
    # path" -- so old is realized purely SPECTRALLY (darker erosion), NOT with an
    # amplitude wobble (that belongs to analog, and pitch wow is out of scope here).
    "analog":     [("DRIFT", 0.03), ("DARK", 3000)],
    "old":        [("DARK", 2200)],
    "washed_out": [("DARK", 2800), ("AIR", 0.12)],
}


def _emit_adjectives(adjective_keys):
    """Post-process the mixed `asig` in place, mapping each adjective key to the
    bounded DSP operations in _ADJ_MAP. Operations dedupe (the strongest of each
    kind wins) and emit in a fixed timbral order. An adjective with no idiom is a
    silent no-op (still a valid closed-enum key)."""
    acc = {}
    for a in adjective_keys:
        for name, val in _ADJ_MAP.get(a, []):
            if name not in acc:
                acc[name] = val
            elif name == "DARK":
                acc[name] = min(acc[name], val)          # lower cutoff = darker
            elif name == "THIN":
                acc[name] = max(acc[name], val)          # higher hp cutoff = thinner
            elif name in ("BODY", "METAL", "BRIGHT", "AIR", "DRIFT"):
                acc[name] = max(acc[name], val)
            elif name == "DIRT":
                acc[name] = val if val[0] > acc[name][0] else acc[name]   # stronger drive
            elif name == "FORMANT":
                acc[name] = val if val[1] > acc[name][1] else acc[name]   # stronger bump
    if not acc:
        return ""
    # These ops shape TIMBRE only; they do NOT bound level here. A multi-adjective
    # additive pile-up (boxy+bright+fat+metallic) or an AIR-noise crest stacked on
    # resonant bumps CAN drive the signal well past unity -- that is bounded ONCE,
    # globally, by the soft peak-limiter on `aout` in build_orchestra's tail (which
    # sees the whole mix + adjectives + motion and caps the true output peak). An
    # earlier RMS-match bound HERE was removed: it matched RMS not PEAK (so an
    # uncorrelated AIR-noise crest still clipped the final output at bass pitches,
    # adversarial review 2026-07-18) and it divided the slow DRIFT wobble back out
    # whenever a hot additive adjective raised the baseline RMS. Peak-limit the
    # output, don't RMS-normalize the timbre.
    L = []
    if "BODY" in acc:
        L.append("  albd     tone asig, 220")
        L.append(f"  asig     = asig + albd * {acc['BODY']:.2f}       ; body / low weight")
    if "THIN" in acc:
        L.append(f"  asig     atone asig, {int(acc['THIN'])}            ; thin (remove low body)")
    if "FORMANT" in acc:
        fc, g = acc["FORMANT"]
        L.append(f"  afrm     reson asig, {int(fc)}, {int(fc * 0.4)}, 2")
        L.append(f"  asig     = asig + afrm * {g:.2f}       ; formant / mid bump")
    if "METAL" in acc:
        L.append("  amtl     reson asig, 3600, 1500, 2")
        L.append(f"  asig     = asig + amtl * {acc['METAL']:.2f}       ; metallic high emphasis")
    if "DARK" in acc:
        L.append(f"  asig     tone asig, {int(acc['DARK'])}            ; dark tilt (low-pass)")
    if "BRIGHT" in acc:
        L.append("  abrt     atone asig, 2200")
        L.append(f"  asig     = asig + abrt * {acc['BRIGHT']:.2f}       ; bright tilt (add highs)")
    if "DIRT" in acc:
        k, g = acc["DIRT"]
        L.append(f"  asig     = tanh(asig * {k:.2f}) * {g:.2f}    ; dirt / overdrive (real harmonics)")
    if "AIR" in acc:
        L.append("  anzr     rand 0.35")
        L.append("  aair     atone anzr, 5000")
        L.append(f"  asig     = asig + aair * {acc['AIR']:.2f}       ; breath / air noise")
    if "DRIFT" in acc:
        L.append(f"  adrf     oscili {acc['DRIFT']:.3f}, 0.7")
        L.append("  asig     = asig * (1 + adrf)         ; slow analog drift")
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
    tremolo (already source-agnostic). The speed/intent motions (slow/fast/snap/
    pingpong/settle) are the SAME spectral waveshaper at a mapped rate — the
    lexicon frames them as how-fast the timbre travels, so they are just rate
    variants (slow 0.08 .. snap 0.9 Hz)."""
    if not motion_key or motion_key == "static":
        return ""
    _SPECTRAL_RATE = {
        "wobble": 2.2, "cycle": 1.1, "snap": 0.9, "fast": 0.4,
        "pingpong": 0.25, "settle": 0.12, "slow": 0.08,
    }
    if motion_key in ("sweep", "evolve", "open_up", "close", "breathe", "wobble",
                      "cycle", "slow", "fast", "snap", "pingpong", "settle"):
        # faster, shallower for the periodic wobble/cycle; slow & deep for the
        # directional-feel evolve/open family (still periodic = keeps living).
        # `balance` re-scales the waveshaped signal to the pre-shape RMS every
        # k-frame, so the harmonic content travels (genuine SPECTRAL motion) but
        # loudness holds steady -- this is NOT the shimmer family's amplitude
        # tremolo. Without it, tanh(x*kdrv) ~= x*kdrv at small signal, so the
        # drive LFO would pump overall level ~2.4x (an evolving PAD must not throb
        # in volume; that belongs to shimmer/tremolo).
        rate = _SPECTRAL_RATE.get(motion_key, 0.16)
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
        "  aout     clip aout, 0, 0.95, 0.85    ; final soft peak safety: transparent\n"
        "                                       ; below ~0.8, asymptotes ~0.88 above,\n"
        "                                       ; bounds ANY op stack / crest / host gain\n"
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
