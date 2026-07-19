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

import re as _re

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
    # metallic_fm's OWN morph reading. The steady path was differentiated from
    # fm_bell (c:m 1:2.41 at index 6.0 against 1:1.41 at 3.2) but _MORPH_SPECTRUM
    # still pointed both at "bell", so inside a morph the two stayed byte-identical
    # -- the last surviving alias. These ratios and weights are the actual sideband
    # set of that FM pair, computed from the Bessel functions J_n(6.0) that govern
    # it (scipy.special.jv), not sketched by hand: energy at index 6 spreads far
    # out from the carrier, which is exactly what makes it a clang rather than a
    # bell. Truncated at 13x to stay near the other spectra's partial counts.
    "metal_fm": [(1.00, 0.42), (1.41, 0.76), (3.41, 0.76), (3.82, 0.67),
                 (5.82, 0.67), (6.23, 0.32), (8.23, 0.32), (8.64, 0.99),
                 (10.64, 0.99), (11.05, 1.00), (13.05, 1.00)],
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
    # a TRUE 30% rectangular wave, A_n = |sin(n*pi*d)|/n normalised (d=0.30) -- the
    # lexicon's stated default width. The non-monotonic ripple (a weak 3rd, a
    # strong 5th) is exactly what makes a narrow pulse sound nasal/hollow; the
    # smooth 1.0/0.8/0.6/0.45 rolloff that stood here was invented, not a pulse.
    "pulse":    [(1.00, 1.00), (2.00, 0.588), (3.00, 0.127), (4.00, 0.182),
                 (5.00, 0.247), (6.00, 0.121), (7.00, 0.055), (8.00, 0.147)],
    # cylindrical reed: odd-dominant with a faint even trace (a real bore is not
    # ideal) and the clarinet's rolloff above the 7th -- NOT a bare square.
    "clarinet": [(1.00, 1.00), (2.00, 0.08), (3.00, 0.55), (4.00, 0.06),
                 (5.00, 0.35), (6.00, 0.04), (7.00, 0.20), (9.00, 0.10)],
    # dark saw: the lexicon's rolled-off low end (a steeper tilt than 1/n).
    "bass_saw": [(1.00, 1.00), (2.00, 0.45), (3.00, 0.25), (4.00, 0.14),
                 (5.00, 0.08), (6.00, 0.04)],
    # sub oscillator: fundamental plus real weight one octave BELOW it.
    "sub_sine": [(0.50, 0.36), (1.00, 1.00)],
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
    "sine": "sine", "sub_sine": "sub_sine", "theremin": "sine",
    "additive": "additive",
    "organ": "organ", "flute": "flute", "harpsichord": "harpsichord",
    "glass": "glass",
    "fm_bell": "bell", "metallic_fm": "metal_fm",
    "struck_bar": "struck_bar", "cymbal": "cymbal",
    "fm": "fm", "fm_ep": "fm", "ring_mod": "ring_mod",
    "saw": "saw", "supersaw": "saw", "brass": "saw", "strings": "saw",
    "bass_saw": "bass_saw", "sync": "saw",
    "square": "square", "clarinet": "clarinet", "chiptune": "square", "pulse": "pulse",
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
    # --- M4b: VOICE / formant synthesis. A sawtooth glottal source shaped by a
    #     bank of resonators at vowel formant frequencies (Csound's native formant
    #     idiom, `reson`) -- NOT an additive vowel sketch. Sustained, pitched; a
    #     voice>voice chain morphs the FORMANTS (a vowel sweep = a "talking" sound).
    "voice": {
        "why": "a sung human voice, an open 'ahh' vowel (formant reson bank), warm vocal/choir",
        "surface_forms": ["voice", "vocal", "vocals", "choir", "vowel", "ahh", "aah",
                          "singing", "sung", "soprano", "voix", "stimme", "chor", "gesang", "human voice"],
    },
    "voice_ee": {
        "why": "a bright 'eee' vowel voice (formant reson bank), thin nasal vocal",
        "surface_forms": ["eee vowel", "ee vowel", "bright voice", "nasal voice", "iii"],
    },
    "voice_oo": {
        "why": "a dark rounded 'ooh' vowel voice (formant reson bank), hollow vocal",
        "surface_forms": ["ooh vowel", "oo vowel", "dark voice", "hollow voice", "uuu"],
    },
}

# M4b VOICE/formant techniques -> _emit_voice / _emit_voice_morph. Each is a
# sawtooth glottal source through a 3-resonator bank at classic sung-vowel formant
# frequencies (F1/F2/F3, Hz), (bandwidth Hz, linear amp). A voice is PITCHED (the
# fundamental is the played note); the vowel is the formant envelope on top. A
# voice>voice morph glides the formants -> a vowel sweep (the native formant-motion
# feature). Values are standard sung-vowel formants (bass/tenor register).
_VOWEL_FORMANTS = {
    # (F_Hz, bandwidth_Hz, linear amp). Upper formants kept relatively strong for
    # articulate, recognisable vowels -- and so a vowel sweep MOVES the spectral
    # centroid enough to register (a weak F2/F3 lets the strong F1 pin the centroid
    # and a big F2 glide reads as near-static).
    "voice":    [(600, 60, 1.00), (1040, 80, 0.60), (2250, 120, 0.32)],   # 'ah'
    "voice_ee": [(270, 50, 1.00), (2290, 100, 0.55), (3010, 130, 0.30)],  # 'ee'
    "voice_oo": [(300, 55, 1.00), (870, 80, 0.50), (2240, 120, 0.22)],    # 'oo'
}
_VOICE_TECH = set(_VOWEL_FORMANTS)

# Techniques rendered by the substrate NOISE path (_emit_noise), not the additive/
# opcode steady path. They are aperiodic textures -- the behavioral gate's noise
# check (pitchedness < 0.4) certifies each is really NOISE, not a pitched tone.
_NOISE_TECH = {"noise", "pink_noise", "wind", "rain", "surf", "thunder",
               "hiss", "crackle"}

# Techniques rendered as a MODAL RESONATOR BANK (Csound's native `mode` filter),
# continuously noise-excited: real struck-metal/glass physics (resonators with
# bandwidth that RING and shimmer under a driven excitation) instead of a static
# additive sine sum — BJ's ear-finding 2026-07-18: the additive cymbal/glass was a
# dead sine cluster ("pfeifender hoher Oszillator statt echter metallischer
# Sounds"), exactly the toy-vocabulary pattern the substrate discipline forbids
# for these physical objects. The ratio/amp sets stay in _SPECTRA (single source,
# lexicon-honoured); _MODAL_PARAMS adds what a resonator needs beyond a partial
# list: excitation level, master gain (empirically calibrated, see
# tools/csound_keys_gate.py peaks), and a Q range (first->last mode).
# The exciter is CONTINUOUS, so a held note stands (platform fundamental); the
# stochastic drive gives the metal live micro-shimmer a static bank cannot have.
_MODAL_TECH = {"struck_bar", "cymbal", "glass"}
_MODAL_PARAMS = {
    #             exc,   master, Q first, Q last
    "struck_bar": (0.06, 0.150,  900, 1400),   # tuned bar: clear, ringing modes
    "cymbal":     (0.09, 0.150,  260, 460),    # plate wash: dense, broader bands
    "glass":      (0.05, 0.150, 1400, 2000),   # glassy: few, very pure, high-Q
}

# Techniques whose IDENTITY is a live opcode structure -- a hard-synced slave, a
# detuned stack, a stepping duty, a breathing filter, a wavering partial, a
# softening FM index. None of them can exist in the additive partial bank that
# _emit_morph interpolates: reduced to a static spectrum, `supersaw` IS `saw` and
# `chiptune` IS `square` (the exact aliasing BJ heard as "everything sounds the
# same"). So a morph touching any of them takes the CROSSFADE path, where every
# stage renders its own real idiom -- the same reason noise and modal stages do.
_LIVE_TECH = {"pwm", "sync", "supersaw", "chiptune", "brass", "strings",
              "theremin", "fm_ep",
              # flute's identity is tone + BREATH. Noise cannot live in a partial
              # bank at all, so as a morph stage it fell back to the additive
              # spectrum this diff itself measured at 0.033 from a bare sine.
              "flute"}

# Spectra with a partial BELOW the fundamental. _emit_morph aligns stages by
# partial INDEX, which silently assumes every spectrum starts at ratio 1.0: give
# it one that starts at 0.5 and index 0 interpolates 1.00 -> 0.50, i.e. the
# dominant partial GLISSANDOS an octave. Measured on `sine > sub_sine`: 220 ->
# 210 -> 196 -> 182 -> 170 -> 162 Hz, a 663-cent bend; `sub_sine > saw` bent
# +884 cents the other way. Pitch belongs to the synth's glide, never to the
# oscillator, so any such spectrum is barred from the additive morph and takes
# the crossfade path, which renders each stage's real idiom and never aligns
# partials at all. Derived from _SPECTRA rather than hand-listed, so a future
# sub-fundamental spectrum cannot reintroduce the bend by being forgotten here.
_SUBFUND_SPECTRA = {n for n, sp in _SPECTRA.items() if any(r < 1.0 for r, _ in sp)}

# Validation-ONLY chain terminals: accepted by the closed-enum guard if the 7B
# emits them, but deliberately NOT listed as pickable techniques in the catalogue
# (listing "silence" as a technique made the 7B treat it as a standalone
# oscillator — "OSC1: silence" — or append it to sustained drones/pads; corpus
# regression 2026-07-18). The 7B learns silence ONLY from the explicit SILENCE
# RULE + worked example in _CS_SYSTEM_PROMPT_HEAD, where its scope is constrained.
_CS_TERMINALS = {
    "silence": ["silence", "zero", "nothing", "silent"],
}

# DECAY-INTENT guard (the morph-to-zero pseudo-env is an ENVELOPE-class decision, and
# the deterministic layer -- not the 7B -- owns the envelope: a sound STANDS while a
# note is held unless the prompt itself asks it to fade/decay/stop). The small-7B is
# intermittently non-deterministic (MPS) and sometimes appends "> silence" to a
# STANDING prompt ("a shimmering evolving pad" -> both chains end in silence -> the
# pad wrongly decays), a failure the soft SILENCE RULE cannot reliably prevent. So
# after routing, if the prompt carries NO decay/transient cue, a trailing terminal
# silence is STRIPPED (never added -- the guard only removes unmotivated decay). This
# turns an intermittent hallucination into deterministic-correct behaviour and keeps
# BJ's authorised morph-to-zero firing ONLY on genuinely transient prompts.
# The cue set is deliberately GENEROUS (a spurious silence that happens to sit beside
# a real cue is harmless; wrongly stripping a real transient is not) -- every corpus
# transient prompt carries one (fades / decays to silence / plucked / percussive blip
# / vocal stab / rolling thunder). Word-boundary match for single words, substring
# for the multi-word phrases.
_DECAY_CUE_WORDS = {
    "fade", "fades", "fading", "faded", "decay", "decays", "decaying", "decayed",
    "die", "dies", "dying", "fizzle", "fizzles", "taper", "tapers", "tapering",
    "dwindle", "dwindles", "wane", "wanes", "ebb", "ebbs", "vanish", "vanishes",
    "vanishing", "disappear", "disappears", "gone",
    # inherently short / transient sound types (a note-length event, not a bed)
    "pluck", "plucked", "plucking", "pizz", "pizzicato", "stab", "stabs",
    "blip", "blips", "click", "clicks", "snap", "snaps", "staccato",
    "percussive", "percussion", "transient", "burst", "bursts", "zap", "zaps",
    "strike", "struck", "knock", "knocks", "thump", "thud", "tap", "taps",
    "clap", "thunder", "thunderclap", "crack", "plink", "plonk",
    # DELIBERATELY NOT CUES (adversarial review 2026-07-18): short / brief / quick /
    # quickly / momentary / sudden describe ATTACK SPEED or duration-of-change, and
    # pop / hit are a genre and an idiom -- not decay. They made STANDING prompts
    # read as decaying ("an evolving pad with a quick attack", "a lush synth-pop
    # pad"), so the unmotivated silence survived and a held pad fell to zero: the
    # exact Contract-2 failure this guard exists to prevent. They also earn nothing:
    # every frozen-corpus transient that contains one is already True via a phrase
    # AND another word ("...quickly fades to nothing" -> "to nothing" + fades +
    # plucked), so dropping them costs zero coverage. Do not re-add a duration or
    # speed word here; a decay cue must name the sound ENDING, not how fast it moves.
}
_DECAY_CUE_PHRASES = (
    "to nothing", "to silence", "to zero", "into nothing", "into silence",
    "trail off", "trails off", "trailing off", "dies away", "die away",
    "fades out", "fade out", "fades away", "fade away", "cut off", "cuts off",
    "peter out", "peters out",
)


def _prompt_wants_decay(text):
    """True iff the prompt itself expresses that the SOUND fades / decays / stops (or
    is an inherently transient event). Governs whether a routed trailing `silence`
    (morph-to-zero pseudo-env) is honoured or stripped as an unmotivated 7B artefact."""
    low = (text or "").lower()
    if any(ph in low for ph in _DECAY_CUE_PHRASES):
        return True
    # split on apostrophes too: no cue word contains one, and keeping them inside a
    # token made a possessive miss its own cue ("thunder's" != "thunder").
    words = _re.findall(r"[a-z]+", low)
    return any(w in _DECAY_CUE_WORDS for w in words)

# MOVEMENT-BY-DEFAULT guard (BJ's platform fundamental, 2026-07-13: "wir überlassen
# nur die Wahl dass KEIN movement stattfindet" -- the platform assumes movement and
# only the ORDER to stand still is delegated). It was a requirement and never a
# mechanism: `_emit_motion(None)` emits nothing, so every patch the model gave no
# MOTION rendered mathematically dead-still. Measured in the built Standalone on
# 2026-07-18: a held C4 of `saw + sine 2' + square` travelled 1.07x in spectral
# centroid over three seconds -- a flat line, and BJ's "stereotyp und dull".
# The 7B will not close this by instruction: told in the system prompt that sounds
# move, it still returned MOTION: none for "a warm mellow organ" AND for "a breathy
# wooden flute" (both measured in the synth), so the instruction was reverted rather
# than left inert in a prompt whose line-rhythm is known to be fragile. This is the
# deterministic layer's call, exactly like the decay guard above: the model proposes,
# the platform holds the fundamental, and the prompt can always override it.
# Mirrors _prompt_wants_decay in shape; the cue set is deliberately NARROW here (the
# decay guard is generous because a wrong strip is cheap -- here a wrong match makes a
# sound stand still, which is the very defect this guard exists to prevent).
_STILL_CUE_WORDS = {
    "static", "motionless", "unmoving", "unchanging", "unchanged", "unwavering",
    "immobile", "frozen", "steady", "constant",
    # German -- the register scan taught this lesson: an English-only cue set makes
    # a German prompt unable to reach its own guard.
    "statisch", "unbewegt", "bewegungslos", "gleichbleibend", "konstant", "starr",
    # DELIBERATELY NOT CUES: sustained / held / long / drone / pad name DURATION or
    # sound TYPE, not stillness -- a held drone is exactly the case that must move.
    # Bare "still" is out too: in English it is mostly an adverb ("still bright") and
    # in German it means quiet, so it earns its place only inside a phrase below.
}
_STILL_CUE_PHRASES = (
    "no movement", "without movement", "no motion", "without motion",
    "does not move", "doesn't move", "do not move", "never moves",
    "perfectly still", "dead still", "stands still", "standing still",
    "holds steady", "keine bewegung", "ohne bewegung", "bewegt sich nicht",
)
# Idioms that already carry their own temporal change, so the default would be
# piling motion on motion. `pwm` is here on MEASUREMENT, not on theory: in the built
# Standalone its even/odd partial ratio travels 0.26 -> 0.76 and pulses at ~0.6 Hz.
# The noise family is stochastic by construction (rand / randh drive it).
_SELF_MOVING_TECH = {"pwm"} | _NOISE_TECH


def _prompt_wants_still(text):
    """True iff the prompt itself orders the sound to STAND STILL. Only then does the
    movement-by-default guard stand down; everything else moves."""
    low = (text or "").lower()
    if any(ph in low for ph in _STILL_CUE_PHRASES):
        return True
    words = _re.findall(r"[a-z]+", low)
    return any(w in _STILL_CUE_WORDS for w in words)


def _patch_already_moves(oscs):
    """True iff the patch travels on its own: a morph chain (two or more stages walk
    from one spectrum to the next), or an idiom whose identity is movement."""
    for o in (oscs or []):
        chain = list(o.get("chain") or [])
        if len(chain) >= 2:
            return True
        if any(k in _SELF_MOVING_TECH for k in chain):
            return True
    return False


# ── register / footage (organ stop feet) ─────────────────────────────────────
# 8' is the played pitch, every halving of the number is an octave UP (organ
# convention): 16'=x0.5, 8'=x1, 4'=x2. The oscillator's register is a SYNTH-side
# control (a knob the player owns, like glide/level), not part of the spectrum
# the LLM authors -- so the authored value is only ever a STARTING value.
_FOOTAGE_MULT = {"32": 0.25, "16": 0.5, "8": 1.0, "4": 2.0, "2": 4.0}
_FOOTAGE_LABEL = {v: k for k, v in _FOOTAGE_MULT.items()}
_DEFAULT_FOOTAGE = "8"

# A footage written as a number plus a feet mark: 16', 16", 16ft, 16 feet. The
# typographic quotes are here because a prompt typed in a text editor gets them
# by autocorrect ("saw wave 8"" is what BJ's own screenshot showed).
#
# Two guards, both from real synth-prompt language rather than caution:
#   (?<![\d/])  -- a TAPE WIDTH is not an organ stop. `1/4" tape saturation` and
#                  `1/2" reel` would otherwise read as 4' and 2', i.e. two whole
#                  octaves off, and worse, they would open the authority gate.
#   (?![A-Za-z]) -- an OPENING quote is followed by a letter: `the 8 "voices" of
#                  a choir`, `4 'clicks' then silence`. A real footage is followed
#                  by a space, a punctuation mark or the end of the prompt.
_FOOTAGE_RE = _re.compile(
    r"(?<![\d/])\b(32|16|8|4|2)\s*(?:'|’|\"|”|ft\b|foot\b|feet\b)(?![A-Za-z])")
# Bare inches that survive the guards above but name a THING, not a register.
# `2" tape hiss` has no slash and no opening quote, so only the following word
# tells them apart.
_NOT_FOOTAGE_NEXT = (
    "tape", "reel", "speaker", "driver", "woofer", "cone", "monitor", "screen",
    "vinyl", "record", "disk", "disc", "floppy", "nail", "pipe",
)
# ...or named in words. These phrases MOVE a layer's register; a bare
# "bass"/"high" does NOT belong here (it describes the timbre the LLM routes, not
# a transposition the player asked for). German too: the instrument is prompted
# in German as often as in English, and the feature was specified in German.
_REGISTER_CUE_PHRASES = (
    "octave below", "octave down", "octave under", "octave lower",
    "octave above", "octave up", "octave higher",
    "sub octave", "sub-octave", "suboctave",
    "oktave tiefer", "oktave höher", "oktave hoeher", "oktave darunter",
    "oktave darüber", "oktave darueber", "oktave unter", "oktave über",
    "suboktave", "sub-oktave", "fußlage", "fusslage",
)
# German footage: "eine 4 Fuß Orgel", "16 Fuss".
_FOOTAGE_DE_RE = _re.compile(r"(?<![\d/])\b(32|16|8|4|2)\s*(?:fuß|fuss)\b")


def _footage_in(text):
    """The pitch multiplier named by the FIRST real footage in `text`, or None.
    Shared by the prompt-authority scan and the OSC-line strip so the two can
    never disagree about what counts as a footage."""
    low = (text or "").lower()
    for m in _FOOTAGE_RE.finditer(low):
        tail = low[m.end():m.end() + 12].strip()
        if any(tail.startswith(w) for w in _NOT_FOOTAGE_NEXT):
            continue                     # `2" tape hiss` names tape, not a stop
        return _FOOTAGE_MULT[m.group(1)]
    m = _FOOTAGE_DE_RE.search(low)
    return _FOOTAGE_MULT[m.group(1)] if m else None


def _prompt_names_register(text):
    """True iff the PROMPT ITSELF names a register/footage. This is the authority
    gate for overwriting a hand-set octave (BJ 2026-07-18: hand-set values stay
    put, "nur wenn der Prompt selbst eine Fußlage nennt" does a new prompt reset
    them). Deterministic on purpose -- the 7B writes a register whenever it feels
    like one, so its emission cannot distinguish "the prompt asked for 16'" from
    "the model's habit"; only the prompt text can. Same shape as
    _prompt_wants_decay: the model proposes, this authorizes."""
    low = (text or "").lower()
    if _footage_in(low) is not None:
        return True
    return any(ph in low for ph in _REGISTER_CUE_PHRASES)


# pwm names a MOVING DUTY on a pulse wave, not a wave in its own right: when the
# 7B lists it in a morph chain beside a plain pulse wave ("pwm > square"), the
# intent is a pwm'd square (one moving-duty tone), not a near-static pulse->pulse
# morph -- "a pwm square wave" is the canonical prompt that produces it.
# build_csound_response collapses such a chain to the single pwm idiom.
#
# `chiptune` and `clarinet` were in this set and had to come OUT. The set was
# written when all four pulse-family keys emitted the same 50% square, so
# collapsing lost nothing. They are distinct idioms now -- chiptune steps its
# duty, clarinet is a reed rolloff -- so the collapse would silently DELETE the
# second half of "pwm > chiptune" (verified: it returned ['pwm']). A rule that
# was merely redundant became a capability loss the moment the aliases were
# fixed; only genuinely static pulse waves belong here.
_PULSE_FAMILY = {"square", "pulse"}


# ── csound-specific multi-oscillator LLM schema ──────────────────────────────
# Up to THREE oscillators, each its own morph chain + volume; the whole sound has
# adjectives + motion. This is a csound-OWNED schema (BJ 2026-07-18: "bis 3
# oszillatoren, morph-ketten pro osc, vol pro osc"); it does NOT touch the shared
# dco_llm_map._SYSTEM_PROMPT_HEAD (lco_author.py depends on that single-technique
# format). The KEY LISTS still come from dco_llm_map._build_catalogue(lex_cs).
# No output cap: the reply is a fixed short format and greedy decoding stops at
# EOS by itself, whereas a cap silently CUTS the last line (a 3-oscillator patch
# with registers is already ~11 lines, so 160 tokens was within reach of losing
# the MOTION line entirely -- a truncated reply parses as a valid smaller patch,
# which is the worst kind of failure: silent and plausible).
_CS_MAX_NEW_TOKENS = None
_CS_SYSTEM_PROMPT_HEAD = (
    "You translate a sound description into a small synthesizer patch of up to "
    "THREE oscillators, choosing ONLY keys from the fixed catalogue below. You "
    "never invent names or numbers.\n"
    "Reply in EXACTLY this format and nothing else (omit OSC2/OSC3 lines if not "
    "needed):\n"
    "OSC1: <key> [> <key> ...] [<16|8|4>']\n"
    "VOL1: <number 0.0-1.0>\n"
    "OSC2: <key> [> <key> ...] [<16|8|4>']\n"
    "VOL2: <number 0.0-1.0>\n"
    "OSC3: <key> [> <key> ...] [<16|8|4>']\n"
    "VOL3: <number 0.0-1.0>\n"
    "ADJECTIVES: <key>, <key>, ...\n"
    "MOTION: <key>\n"
    "Rules: use ONE oscillator for a simple sound; add OSC2/OSC3 only to LAYER or "
    "detune (e.g. a fat saw stacked with a sub, or a bright bell over a warm pad). "
    "Each OSC line is that layer's waveform/method; if it MORPHS from one sound "
    "into another, list them in order separated by \" > \" (e.g. glass > sine). "
    "VOLn is that layer's loudness (main layer 1.0, supporting layers less). "
    "A layer may end with an organ register in feet -- 8' is the played pitch, "
    "16' one octave lower, 4' one octave higher (e.g. \"OSC2: sine 16'\"). Add "
    "one ONLY if the prompt asks for a register; otherwise write no feet at all. "
    "ADJECTIVES are timbral modifiers for the whole sound (or \"none\"). MOTION is "
    "how the whole sound moves over time (or \"none\"). Match by MEANING even if "
    "the wording differs. Use ONLY keys from the catalogue; if nothing in a "
    "category fits, write \"none\".\n"
    "NOTATION: the prompt's own punctuation is binding. \"a > b\" is ONE "
    "oscillator morphing a into b — put both on the SAME OSC line separated by "
    "\" > \", never on two lines. \"a + b\" is two oscillators on two lines. A "
    "chain may have three or more stages: a prompt written \"saw > sine > "
    "square\" is the single line \"OSC1: saw > sine > square\", NOT three "
    "oscillators.\n"
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

_CS_OSC_RE = _re.compile(r'(?i)^OSC\s*(\d+)\s*:\s*(.*)$')
_CS_TECH_RE = _re.compile(r'(?i)^TECHNIQUE\s*:\s*(.*)$')   # legacy single-osc reply
_CS_VOL_RE = _re.compile(r'(?i)^VOL\s*(\d+)\s*:\s*(.*)$')
_CS_ADJ_RE = _re.compile(r'(?i)^ADJECTIVES?\s*:\s*(.*)$')
_CS_MOT_RE = _re.compile(r'(?i)^MOTION\s*:\s*(.*)$')        # singular only (not the MOTIONS catalogue header)


def _parse_csound_reply(raw):
    """Parse the multi-osc reply -> (oscs, adjectives_raw, motion_raw) where oscs
    is an ORDERED list of (chain_raw_string, vol_float, register_mult_or_None).
    Tolerant: a legacy 'TECHNIQUE:' line is read as OSC1; a missing VOLn defaults
    to 1.0; a missing/unreadable OCTn is None (= the model named no register, so
    the layer plays at 8'); last occurrence of a label wins (a model that echoes
    the format before answering). Empty / 'none' oscillator lines are dropped."""
    osc_chains, osc_vols, osc_octs = {}, {}, {}
    adjectives_raw, motion_raw = "", ""
    for line in (raw or "").splitlines():
        s = line.strip().lstrip("-*• \t")
        m = _CS_OSC_RE.match(s)
        if m:
            idx, chain = int(m.group(1)), m.group(2).strip()
            # The register rides ON the oscillator line ("sine 16'"), the way it
            # is written in a prompt, rather than on a line of its own: a third
            # line per oscillator broke the reply's rhythm badly enough that the
            # 7B stopped after it and never emitted ADJECTIVES/MOTION at all
            # (measured on three corpus prompts). Strip it out here so the chain
            # validator only ever sees keys.
            mult = _footage_in(chain)
            if mult is not None:
                osc_octs[idx] = mult
                # strip EVERY footage token, not just the one that supplied the
                # value: a register belongs to the LAYER, so in "saw 16' > sine
                # 4'" the trailing 4' has nothing to attach to and is dropped.
                # Not silent -- the reading card names the register that won
                # ("saw > sine 16'"), so the discarded one is visible by absence.
                stripped = _FOOTAGE_DE_RE.sub(" ", _FOOTAGE_RE.sub(" ", chain))
                chain = " ".join(stripped.split())
            osc_chains[idx] = chain
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
        oscs.append((chain, osc_vols.get(idx, 1.0), osc_octs.get(idx)))
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
    # Collapse ADJACENT duplicates: morphing a spectrum into itself is a no-op by
    # construction, but it still emits a full second stage and reads back as the
    # nonsense "saw > saw" (observed verbatim in the 7B corpus run for "a fat
    # detuned saw stacked with a deep sub"). Only adjacent pairs collapse --
    # "a > b > a" is a real round trip and must survive.
    keys = [k for i, k in enumerate(keys) if i == 0 or k != keys[i - 1]]
    if "pwm" in keys and all(k == "pwm" or k in _PULSE_FAMILY for k in keys):
        keys = ["pwm"]
    return keys, flags


def _reading(oscs, adjective_keys, motion_key):
    """Short human-readable interpretation for the UI card. Multi-osc: each layer
    is 'a > b' (morph) or 'a'; layers joined with ' + '. e.g.
    'saw + sub_sine · warm ~evolve' or 'glass > sine · glassy'."""
    def osc_str(o):
        chain = o["chain"]
        s = " > ".join(chain) if len(chain) >= 2 else (chain[0] if chain else "sine")
        # name the register only when it is NOT the played pitch: "8'" on every
        # layer would be noise in the card, a "16'" is the thing worth reading.
        reg = o.get("register", 1.0)
        if reg != 1.0:
            s += " " + _FOOTAGE_LABEL.get(reg, "8") + "'"
        return s
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


def _emit_voice(technique, tag="0"):
    """VOICE/formant synthesis (M4b) -> `aosc<tag>`: a sawtooth glottal source
    (rich harmonics for the formants to shape) through a bank of `reson` filters at
    the vowel's formant frequencies -- Csound's native formant idiom, NOT an
    additive vowel sketch. Pitched (the fundamental is the played note); the vowel
    is the formant envelope. reson iscl=2 is peak-normalised per band; the weighted
    sum is scaled and the tail limiter is the final bound."""
    ov = f"aosc{tag}"
    src = f"asrc{tag}"
    formants = _VOWEL_FORMANTS.get(technique, _VOWEL_FORMANTS["voice"])
    L = [f"  {src}    vco2 0.5, kfreq, 0           ; glottal saw source ({technique})"]
    terms = []
    for i, (f, bw, amp) in enumerate(formants):
        L.append(f"  a{tag}f{i}   reson {src}, {f}, {bw}, 2      ; formant {i+1} @ {f} Hz")
        terms.append(f"a{tag}f{i} * {amp:.3f}")
    L.append(f"  {ov}    = ({' + '.join(terms)}) * 0.5")
    return "\n".join(L)


def _emit_voice_morph(chain, imorphtime, tag="0"):
    """A VOWEL-SWEEP morph (M4b) -> `aosc<tag>`: one glottal saw source through a
    3-resonator bank whose formant CENTRE FREQUENCIES and amplitudes travel A->B
    across the vowels over `imorphtime` (restarted per note off the trig epoch).
    reson takes k-rate cf/bw, so the formants genuinely glide -- a 'talking' vowel
    morph (voice > voice_ee = 'ah'->'ee'), the native formant-motion feature. Every
    vowel has exactly 3 formants, so stages align without padding."""
    stages = [_VOWEL_FORMANTS.get(k, _VOWEL_FORMANTS["voice"]) for k in chain
              if k not in ("silence", "zero")]
    if len(stages) < 2:
        return None
    nf = 3
    nstage = len(stages)
    leg = imorphtime / (nstage - 1)
    src = f"asrc{tag}"
    lbl = f"Lvoxmorph{tag}"
    L = [f"  ; --- osc {tag}: vowel-sweep formant morph (trig-epoch reinit) ---",
         f"  {src}    vco2 0.5, kfreq, 0           ; glottal saw source"]
    L.append("  if changed2(ktrig) == 1 then")
    L.append(f"    reinit {lbl}")
    L.append("  endif")
    L.append(f"{lbl}:")
    terms = []
    for i in range(nf):
        cf = ", ".join(f"{stages[j][i][0]}, {leg:.4f}" for j in range(nstage - 1)) + f", {stages[nstage-1][i][0]}"
        am = ", ".join(f"{stages[j][i][2]:.3f}, {leg:.4f}" for j in range(nstage - 1)) + f", {stages[nstage-1][i][2]:.3f}"
        bw = stages[0][i][1]   # bandwidth held at the first vowel's value (stable band)
        L.append(f"  k{tag}cf{i}  linseg {cf}")
        L.append(f"  k{tag}am{i}  linseg {am}")
        L.append(f"  a{tag}f{i}   reson {src}, k{tag}cf{i}, {bw}, 2")
        terms.append(f"a{tag}f{i} * k{tag}am{i}")
    L.append("  rireturn")
    L.append(f"  aosc{tag}    = ({' + '.join(terms)}) * 0.5")
    return "\n".join(L)


# An asymmetric rectangular wave carries DC: at duty d and amplitude A its mean is
# A*(2d-1). Measured at 220 Hz, `pulse` (30% duty) sat at a constant -0.085 -- 34%
# of its own peak, pure wasted headroom -- and `chiptune` was worse: its duty STEPS
# between 12.5/25/50%, so the offset stepped with it, 0.000 -> -0.159, a
# 63%-of-peak DC jump about once a second. That is an audible thump on every step,
# and nothing in the suite could see it (peak 0.251, note stands, movement present
# -- all green).
#
# Corrected at the SOURCE, not with a DC-blocking filter. `dcblock2` was tried and
# rejected on measurements: at its default order it costs -12.1 dB at 20 Hz (it
# deletes the sub-bass this instrument exists to make), and at order 512, which
# does keep the bass, it delays by 1022 samples = 21.3 ms -- which left the first
# ~20 ms after every retrigger silent (RMS 0.0001/0.0011/0.0043/0.0086 against
# 0.1399 undelayed), i.e. it would have hollowed out the front of every percussive
# sound. Subtracting the known mean costs one multiply-add, has no latency, no
# filter and no bass loss. The law was verified, not assumed: predicted DC ratio
# between d=0.30 and d=0.125 is 0.533, measured 0.535.
_DC_NOTE = "remove the duty's DC offset, A*(2d-1)"

# Stages measured to cost clearly under the ~5us/stage line, as marginal cost
# inside the whole 16-voice orchestra under the heavy adjective stack. This is an
# ALLOWLIST on purpose: `_modal_budget` counts everything NOT listed here against
# the block budget, so a key added later is treated as expensive until somebody
# measures it. Under-counting is exactly how the old budget let a mixed patch
# through, so the default has to fail expensive. Borderline members are therefore
# absent rather than included -- `additive` benched 4.9us and 6.6us on two runs and
# is counted costly; `clarinet` 5.1, `triangle` 6.3, `organ` 6.8, `bass_saw` 6.7,
# `supersaw` 8.0, `strings` 8.8, `flute` 9.6, the voices 8.2-8.4 and `harpsichord`
# 10.5 (the most expensive single stage there is) are all well over the line.
_CHEAP_TECH = frozenset({
    "sine", "saw", "square", "pulse", "pwm", "silence",
    "fm", "fm_bell", "fm_ep", "metallic_fm", "cheby", "ring_mod", "sub_sine",
    "sync", "brass", "chiptune", "theremin",
    "noise", "pink_noise", "wind", "rain", "surf", "thunder", "hiss", "crackle",
})

# (simultaneous modal banks, simultaneous costly stages) -> modes per bank.
# Every cell measured; see _modal_budget's docstring for the bench table and for
# why the target is 112us rather than the 133us gate.
_MODAL_BUDGET = {
    (1, 0): 9, (1, 1): 9, (1, 2): 9, (1, 3): 9, (1, 4): 9, (1, 5): 6,
    (2, 0): 9, (2, 1): 9, (2, 2): 6, (2, 3): 6, (2, 4): 3,
    (3, 0): 6, (3, 1): 6, (3, 2): 4, (3, 3): 4,
    (4, 0): 4, (4, 1): 4, (4, 2): 3,
    (5, 0): 4, (5, 1): 3,
    (6, 0): 2,
}


def _modal_budget(oscs):
    """Modes per modal bank, sized by how many banks can sound AT THE SAME TIME.

    A `mode` filter is cheap alone and ruinous in bulk. The count that matters is
    not how many modal stages the orchestra contains but how many are audible at
    once: a crossfade morph only ever has its two adjacent stages open, so an
    oscillator contributes at most 2 banks no matter how long its chain is (and
    min(2, ...) deliberately over-counts a chain like cymbal>sine>glass, where
    the two modal stages are never adjacent -- erring toward cheaper).

    The thresholds are MEASURED mid-morph AND under a full adjective stack, because
    both of those were blind spots that cost a recalibration:

      - Benching the settled tail understates the cost badly: by then the morph has
        landed on a single stage per oscillator. The same 3x cymbal>glass>struck_bar
        orchestra benches 79us settled and 138us mid-morph.
      - Benching a bare oscillator stack understates it again. The first version of
        this table was calibrated with adj=[] motion=None -- the same "measure the
        easy case" blind spot these thresholds exist to prevent -- and adding
        ordinary timbral words pushed the 6-bank row from 116.9 to 149.8us, i.e.
        over a gate it had just passed. Any prompt with five adjectives and a
        motion word reaches that, so it is not a corner case.

      - And the one that cost a SECOND recalibration, found by adversarial review
        the same day the first landed: sizing the banks by the MODAL count alone is
        wrong, because the modal layer does not have the block to itself. Two modal
        banks next to two supersaw>strings>flute layers -- 2 modal, 4 costly, all
        catalogue keys, all reachable from one prompt -- took the most generous tier
        and benched 138.8us against a 133us gate. Every row that calibrated the old
        table was HOMOGENEOUS (3x the same chain), so a mixed patch was never
        measured: a table that fixed a "measured only the easy case" blind spot
        reproduced it one dimension over. The budget now costs the WHOLE patch.

    Measured mid-morph with 5 adjectives + wobble, cells built from the most
    expensive members of each class (cymbal for modal, supersaw+flute for costly),
    us against the 133us gate. `--` = not reached, the cell fits at a larger size:

        modal\\costly     0        1        2        3        4        5
             1         61.0*    72.4*    80.5*    93.8*   104.0*   116.5/105.7
             2         94.0*   103.8*   116.3/96.6  124.5/106.4  139.0!/104.0
             3        125.7/95.5  136.5/108.9  149.1!/103.4  162.0!/111.4
             4        156.1!/95.0  172.1!/108.0  182.8!/104.4
             5        190.2!/108.5  209.9!/104.6
             6        223.3!/92.3
        (* fits at 9 modes; a/b = benched at 9 modes / at the size finally picked)

    The picked sizes are _MODAL_BUDGET below. They are chosen against a 112us
    target rather than the 133us gate, so the table keeps ~16% in hand for a
    slower or busier machine -- the old 6-bank row sat at 96% of the gate, which
    is a pass on paper and a dropout on a loaded laptop. Several cells came out
    MORE generous than the old tiers (5 banks 2 -> 4 modes, 6 banks 2 -> 3 before
    the margin cut it back), because counting only modal stages was too strict
    where the rest of the patch was cheap and far too loose where it was not.

    Thinning SUBSAMPLES each spectrum evenly (first and last partial always kept)
    rather than truncating it, so the metal keeps its spectral SPAN -- the top
    partials are exactly where a cymbal's brightness lives, and lopping them off
    would dull the sound instead of merely simplifying it."""
    def sim(o, pred):
        return min(2, sum(1 for k in o["chain"] if pred(k)))
    modal = sum(sim(o, lambda k: k in _MODAL_TECH) for o in oscs)
    if not modal:
        return 9                      # nothing to thin
    costly = sum(sim(o, lambda k: k not in _MODAL_TECH and k not in _CHEAP_TECH)
                 for o in oscs)
    # Counting the two classes separately can report up to 4 stages for a single
    # oscillator (cymbal>glass>supersaw>flute) where only 2 are ever open. That
    # over-count is deliberate -- it errs expensive -- but the grid was measured
    # only over reachable patches (3 oscillators x 2 open stages = 6), so clamp
    # back into it rather than falling off the table.
    modal = min(modal, 6)
    costly = min(costly, 6 - modal)
    return _MODAL_BUDGET.get((modal, costly), 2)


def _thin(spec, nmodes):
    """Evenly subsample a partial list to `nmodes`, always keeping both ends."""
    if not nmodes or nmodes >= len(spec) or len(spec) < 2:
        return spec
    if nmodes < 2:                 # nmodes-1 below would divide by zero
        return spec[:1]
    idx = [round(i * (len(spec) - 1) / (nmodes - 1)) for i in range(nmodes)]
    return [spec[i] for i in sorted(set(idx))]


def _emit_modal(technique, tag="0", nmodes=None):
    """Struck metal / glass via Csound's native MODAL idiom -> `aosc<tag>`: a bank
    of `mode` resonators (real bandwidth + ring time) at the technique's
    inharmonic ratios, driven by continuous low-level noise. The metal is a
    resonating OBJECT being excited — it rings, beats and shimmers — not a static
    additive sine sum (the dead cluster BJ's ear caught 2026-07-18). Because the
    excitation is continuous, a held note STANDS (the synth still owns the
    envelope); the stochastic drive is the sound's intrinsic micro-life.
    Per-mode k-rate gating mutes any resonator the played pitch would push past
    the safe band (a `mode` filter near Nyquist is unstable — muting, not
    clamping, so no pinned 15 kHz whine)."""
    exc, master, q0, q1 = _MODAL_PARAMS[technique]
    spec = _thin(_SPECTRA[technique], nmodes)
    n = len(spec)
    L = [f"  aexq{tag}   rand {exc}                    ; continuous exciter -> the metal is driven, a held note stands"]
    terms = []
    for i, (r, a) in enumerate(spec):
        q = int(q0 + (q1 - q0) * (i / (n - 1) if n > 1 else 0.0))
        L.append(f"  k{tag}qf{i}   limit kfreq * {r:.4f}, 20, 15000")
        L.append(f"  k{tag}qg{i}   = (kfreq * {r:.4f} < 15000 ? {a:.3f} : 0)")
        L.append(f"  a{tag}q{i}   mode aexq{tag}, k{tag}qf{i}, {q}      ; mode {i + 1} @ x{r:.2f}, Q {q}")
        terms.append(f"a{tag}q{i} * k{tag}qg{i}")
    L.append(f"  aosc{tag}    = ({' + '.join(terms)}) * {master:.3f}")
    return "\n".join(L)


def _emit_steady(technique, tag="0", nmodes=None):
    """A single (non-morph) technique -> `aosc<tag>`. Uses Csound's native
    opcodes; every temporary is suffixed with `tag` (per-osc uniqueness)."""
    ov = f"aosc{tag}"
    L = []
    if technique in _NOISE_TECH:
        return _emit_noise(technique, tag)
    if technique in _VOICE_TECH:
        return _emit_voice(technique, tag)
    if technique in _MODAL_TECH:
        return _emit_modal(technique, tag, nmodes)
    if technique == "pwm":
        # classic PWM: band-limited pulse whose DUTY moves (square 50% -> thin
        # 8% -> back), a genuinely moving spectrum. kpw is the pulse width.
        # Duty sweeps 80% .. 20% (BJ, 2026-07-18: "pwm square sollte einfach
        # defaulten, 80/20 oder so etwas"). Duty is the ON fraction of the cycle on
        # the standard PWM scale, so 80/20 names the two ends of the sweep, and the
        # square (50%) sits at its centre -- which is what a key called "pwm SQUARE"
        # should be built around.
        #
        # This deliberately DEPARTS from the lexicon `why`, which still reads
        # "square (50%) narrowing to a thin pulse (8%) and back" -- a wavetable-era
        # figure. Recorded here rather than left silent, because a comment that
        # describes something the code does not do is exactly the defect class this
        # whole pass exists to remove; the why text needs the same edit, which is
        # a measured change (it steers the LLM's routing) tracked separately.
        #
        # The arithmetic was ALSO wrong before: `0.29 + 0.21*(klfo+0.5)` mapped
        # [-0.5,0.5] onto [0,1], so the excursion was never negative and the duty
        # crept over 0.29..0.50 -- a quarter of even the old intent. Wrong since
        # febda214, with the comment above it claiming 8% the whole time.
        L.append(f"  klfo{tag}    oscili 0.5, 0.25            ; -0.5..0.5, 4 s period")
        L.append(f"  kpw{tag}     = 0.5 + 0.6 * klfo{tag}     ; duty 0.20..0.80, square at centre")
        L.append(f"  apw{tag}     vco2 0.6, kfreq, 2, kpw{tag}     ; imode 2 = pulse, kpw = width")
        L.append(f"  {ov}    = apw{tag} - 0.6 * (2 * kpw{tag} - 1) ; {_DC_NOTE}")
    elif technique == "square":
        L.append(f"  {ov}    vco2 0.6, kfreq, 2, 0.5     ; square (50%% pulse)")
    elif technique == "pulse":
        # the lexicon's OWN spec: "narrow rectangular wave (default 30% width)".
        # It emitted 0.5 -- i.e. a square, a byte-identical alias of `square`.
        L.append(f"  apl{tag}    vco2 0.6, kfreq, 2, 0.30    ; narrow pulse (30%% width)")
        L.append(f"  {ov}    = apl{tag} + 0.2400         ; {_DC_NOTE}")
    elif technique == "clarinet":
        # cylindrical reed: the odd-only square family IS the textbook
        # approximation (lexicon), but a real clarinet's odd harmonics roll off
        # above ~the 9th -- that rolloff is what separates it from a raw square.
        L.append(f"  acl{tag}    vco2 0.6, kfreq, 2, 0.5     ; odd-only source")
        L.append(f"  kcl{tag}    limit kfreq * 9, 100, 15000 ; reed rolloff ~9th harmonic")
        L.append(f"  {ov}    butterlp acl{tag}, kcl{tag}")
    elif technique == "chiptune":
        # a real chip lead is a pulse whose DUTY STEPS between the chip's three
        # settings (12.5 / 25 / 50%) -- the stepping IS the chiptune signature,
        # and it is movement arising from the idiom, not a bolted-on LFO. tonek
        # rounds the step over ~5 ms so the duty jump does not click.
        L.append(f"  kst{tag}    oscili 1.49, 0.9            ; slow 3-zone selector")
        L.append(f"  kdt{tag}    = (kst{tag} > 0.5 ? 0.125 : (kst{tag} > -0.5 ? 0.25 : 0.5))")
        L.append(f"  kdw{tag}    tonek kdt{tag}, 30          ; de-click the duty step")
        L.append(f"  ach{tag}    vco2 0.6, kfreq, 2, kdw{tag}     ; stepped chip duty")
        L.append(f"  {ov}    = ach{tag} - 0.6 * (2 * kdw{tag} - 1) ; {_DC_NOTE}")
    elif technique == "organ":
        # A drawbar organ is RANKS, and its life is that the ranks are never in
        # lockstep -- separate tonewheels/pipes, each drifting on its own. The
        # six-row (ratio, amp) table this replaces could not express either half
        # of that: every partial sat at a fixed level on an exact integer, so the
        # sound was frozen by construction ("ein voellig farbloser, langweiliger,
        # statischer Orgelsound", BJ 2026-07-19).
        #
        # `gbuzz` is the substrate's own living harmonic stack: harmonic COUNT and
        # amplitude ROLLOFF are k-rate, so the registration itself breathes -- the
        # spectrum changes without anything sweeping across it. kmul is the ratio
        # between successive harmonics, so 0.72 gives 1 / .72 / .52 / .37 / .27 /
        # .19 / .14 / .10, close to the registration this replaces but continuous
        # over ALL harmonics: the old table skipped the 5th and 7th, which a real
        # drawbar set does not.
        #
        # The QUINT drawbars are what make an organ hollow rather than merely
        # bright, and they are physically separate pipes -- so they are separate
        # oscillators here, drifting against the body on their own incommensurate
        # LFO. aq2 sits at 3x, where the body already has its own 3rd harmonic:
        # the two beat. At 0.05% detune that is ~0.4 Hz at middle C, the slow roll
        # a real rank pair has. This is decorrelated micro-life inside the
        # generator, not one LFO moving everything together.
        # The life is in the DETUNE BETWEEN RANKS, not in any LFO. A first attempt
        # here put one slow LFO on gbuzz's kmul and measured 1.02 dB of travel in
        # the built Standalone -- under the noise floor, i.e. still static, and PC1
        # 0.675: one hidden variable again, the same mistake as a bus filter, just
        # moved inside the generator. One gbuzz is ONE rank and cannot have
        # independent partials by construction.
        #
        # So: real ranks, permanently a few cents apart, exactly as a pipe organ is
        # built. Rank 4' sits +4 cents from where rank 8's own 2nd harmonic falls,
        # so those two beat at 520*(2^(4/1200)-1) ~ 1.2 Hz; the quint's 2nd harmonic
        # lands on the body's 3rd a few cents off and beats at its own, DIFFERENT
        # rate. Every partial therefore beats against a different neighbour at a
        # different speed -- decorrelated by construction, which no single LFO can
        # be. The slow wander on top only keeps the beat rates from being perfectly
        # fixed; it is the seasoning, not the mechanism.
        L.append(f"  kw8{tag}    poscil 0.00035, 0.061        ; rank 8' tuning wander")
        L.append(f"  kw4{tag}    poscil 0.00045, 0.083        ; rank 4', incommensurate")
        L.append(f"  kwq{tag}    poscil 0.00030, 0.107        ; quint rank, incommensurate")
        # `gbuzz` normalises to PEAK, and three stacks of in-phase cosines are a
        # pulse train (crest 5.0), so the amplitudes here are not what the key
        # SOUNDS like -- read as levels they left the organ 11.7 dB under a sine.
        # Fewer harmonics per rank brings the crest down; the ranks together still
        # cover the same series, because they overlap at the octave and the quint.
        L.append(f"  a8{tag}     gbuzz 0.46, kfreq * (1 + kw8{tag}), 6, 1, 0.72, giCos ; 8' principal")
        L.append(f"  a4{tag}     gbuzz 0.24, kfreq * 2.0023 * (1 + kw4{tag}), 4, 1, 0.66, giCos ; 4' octave, +4 cents")
        L.append(f"  aq{tag}     gbuzz 0.16, kfreq * 1.4987 * (1 + kwq{tag}), 3, 1, 0.60, giCos ; 5 1/3' quint, -1.5 cents")
        L.append(f"  {ov}    = a8{tag} + a4{tag} + aq{tag}")
    elif technique == "triangle":
        # The real band-limited triangle, replacing a six-row odd-harmonic stand-in.
        # Nothing had to be pre-generated for it: vco2 builds its table sets on
        # demand (see the header note -- the older "renders silent" story was a
        # misdiagnosis of an unrelated argument error). Measured against the
        # stand-in: h3 -19.1 dB, h5 -28.0, h7 -33.8, no even harmonics, level in
        # line with the saw family -- and at 12 kHz it produces ZERO content below
        # the played pitch, where the stand-in aliased at 0.120 relative.
        L.append(f"  {ov}    vco2 0.6, kfreq, 12         ; band-limited triangle")
    elif technique == "saw":
        L.append(f"  {ov}    vco2 0.6, kfreq, 0          ; band-limited sawtooth")
    elif technique == "supersaw":
        # 7 detuned saws (the JP-8000 idiom). The lexicon says the wavetable path
        # had to fake this because "the detune itself is not a cycle property" --
        # on a live substrate the detune is simply real, and the beating between
        # the copies is movement that arises from the structure itself.
        ratios = (1.0, 1.0035, 0.9965, 1.0071, 0.9929, 1.0110, 0.9890)
        for i, r in enumerate(ratios):
            L.append(f"  asu{tag}x{i}  vco2 0.6, kfreq * {r:.4f}, 0")
        stack = " + ".join(f"asu{tag}x{i}" for i in range(len(ratios)))
        L.append(f"  {ov}    = ({stack}) * 0.17   ; 7-saw detuned stack")
    elif technique == "sync":
        # REAL hard sync. The lexicon admits the wavetable stand-in ("bright,
        # dense FM stands in for hard sync's sideband-rich character"); Csound
        # has the actual thing: a slave saw phase-RESET by the master period.
        # The slave ratio SWEEPS, because a static sync ratio is just a fixed
        # formant -- the sweep is what makes sync recognisable, exactly as the
        # moving duty is what makes pwm recognisable.
        L.append(f"  kswp{tag}   oscili 1.1, 0.16            ; slave-ratio sweep")
        L.append(f"  krat{tag}   = 2.6 + kswp{tag}            ; 1.5 .. 3.7 x master")
        L.append(f"  azro{tag}   = 0")
        L.append(f"  amst{tag}, asyn{tag}  syncphasor kfreq, azro{tag}")
        L.append(f"  aslv{tag}, adum{tag}  syncphasor kfreq * krat{tag}, asyn{tag}")
        L.append(f"  {ov}    = (aslv{tag} * 2 - 1) * 0.45  ; hard-synced saw")
    elif technique == "brass":
        # the lexicon's own spec taken literally: "opens from a dark 6-harmonic
        # set to a brighter 12-harmonic set". On a live substrate that is a
        # resonant lowpass BREATHING between 6*f0 and 12*f0 -- brass under breath
        # pressure -- rather than two frozen keyframes.
        L.append(f"  abr{tag}    vco2 0.6, kfreq, 0          ; saw source")
        L.append(f"  kbr{tag}    oscili 0.5, 0.22            ; slow breath cycle")
        L.append(f"  kcut{tag}   limit kfreq * (9 + 6 * kbr{tag}), 100, 15000 ; 6..12 harmonics")
        # rezzy, not moogladder: measured 11.7us vs 116us for the SAME orchestra
        # (and more centroid travel, 263Hz vs 130Hz). moogladder's cost is real --
        # three brass oscillators, which the 3-osc architecture allows, benched
        # 341us against the 133us block gate (257% of budget, a hard FAIL).
        L.append(f"  arz{tag}    rezzy abr{tag}, kcut{tag}, 8   ; brass formant push")
        L.append(f"  {ov}    = arz{tag} * 1.35           ; level-match the saw family")
    elif technique == "strings":
        # ensemble = several players slightly apart: 3 gently detuned saws, plus
        # the lexicon's "slow, gentle harmonic-count breathing motion" as a
        # bright, slow filter breath (brighter, slower and far gentler than brass).
        for i, r in enumerate((1.0, 1.0042, 0.9958)):
            L.append(f"  ase{tag}x{i}  vco2 0.6, kfreq * {r:.4f}, 0")
        L.append(f"  aens{tag}   = (ase{tag}x0 + ase{tag}x1 + ase{tag}x2) * 0.30 ; ensemble")
        L.append(f"  kbr{tag}    oscili 0.5, 0.13            ; gentle, slow breathing")
        L.append(f"  kcut{tag}   limit kfreq * (14 + 4 * kbr{tag}), 100, 15000")
        L.append(f"  {ov}    butterlp aens{tag}, kcut{tag}")
    elif technique == "bass_saw":
        # the lexicon's own spec: "harmonic count ceilinged near 24 for a
        # rolled-off low end" -- a literal 24th-harmonic ceiling the code never had.
        # The ceiling alone measured only 0.042 from a plain saw (a saw's energy
        # above its 24th harmonic is tiny), so the key stayed an alias in the ear.
        # What makes it a BASS saw is the weight below: a sub octave under the
        # rolled-off saw. Both halves of the name now exist in the code.
        L.append(f"  abs{tag}    vco2 0.6, kfreq, 0          ; saw source")
        L.append(f"  kcut{tag}   limit kfreq * 24, 100, 15000 ; 24-harmonic ceiling")
        L.append(f"  adk{tag}    butterlp abs{tag}, kcut{tag}")
        L.append(f"  asb{tag}    oscili 0.30, kfreq * 0.5    ; sub-octave weight")
        L.append(f"  {ov}    = adk{tag} * 0.72 + asb{tag}")
    elif technique == "fm_ep":
        # the lexicon's "1:1 FM with a softening index over the cycle, the classic
        # tine-EP sideband shape". On a live substrate the index actually SOFTENS
        # and then HOLDS: a tine that mellows into a standing tone. This is a
        # TIMBRE motion, not an amplitude envelope -- the tone never dies (and
        # linseg is the same k-rate shaper the morph path already uses; the
        # forbidden opcodes are the amplitude envelopes).
        # The index has to soften PER NOTE. A bare `linseg` here fired exactly
        # once per SESSION: instr 1 is always-on (i 1 0 360000), so the ramp
        # completed 1.6 s after Csound started and every note the player struck
        # afterwards got a frozen 1.30 -- the capability never reached the
        # instrument. Measured: struck at t=0 the centroid ran 686->421 Hz, struck
        # at t=6 it sat at 421 Hz from the first window. No gate could see it,
        # because the probe always triggers at t=0.
        # A per-note counter reset on the trig epoch, NOT a reinit label: this code
        # is emitted inside the crossfade path's `if <gain> > 0` blocks, where a
        # label and its reinit would sit inside a conditional.
        # init 0, not a large "already settled" value: a note whose gate and trig
        # are already high before the first k-cycle -- which is how the probe and
        # a held-at-load patch both behave -- gives changed2 no edge to fire on,
        # so the counter would never start and the index would sit frozen at 1.30.
        # Starting at 0 ramps that case correctly and still restarts on every
        # later trigger edge.
        L.append(f"  ktm{tag}    init 0")
        L.append(f"  if changed2(ktrig) == 1 then")
        L.append(f"    ktm{tag}   = 0")
        L.append(f"  endif")
        L.append(f"  ktm{tag}    = ktm{tag} + 1/kr           ; seconds since this note")
        L.append(f"  kndx{tag}   = 1.30 + 2.90 * (1 - min(ktm{tag} / 1.6, 1)) ; tine -> mellow, holds")
        L.append(f"  {ov}    foscili 0.5, kfreq, 1, 1, kndx{tag}, giSine ; 1:1 tine EP")
    elif technique in ("fm_bell", "fm", "metallic_fm"):
        # FM via foscili: an inharmonic-ish carrier:modulator ratio gives the
        # bell/metal sideband spectrum natively (no partial table). metallic_fm
        # is a HARSHER CLANG than fm_bell — a wider inharmonic ratio and a much
        # deeper index spread the sidebands into dissonant metal (they were
        # byte-identical before, a degenerate duplicate; BJ ear-finding
        # 2026-07-18).
        car, mod, ndx = {"fm_bell": ("1", "1.41", "3.2"),
                         "metallic_fm": ("1", "2.41", "6.0")}.get(technique, ("1", "2", "1.8"))
        L.append(f"  {ov}    foscili 0.5, kfreq, {car}, {mod}, {ndx}, giSine ; FM (foscili)")
    elif technique == "cheby":
        # A CHEBYSHEV shaper, which is what the key is named after: a sine read
        # through a GEN13 transfer function produces exactly the harmonics that
        # table encodes. The old code was `tanh(sine * 3)` -- a soft clipper whose
        # harmonics were whatever tanh happened to give, not a chosen spectrum.
        # The drive is what makes a waveshaper live: at low drive the sine only
        # touches the middle of the curve and comes out nearly pure, at full drive
        # it sweeps the whole curve and every harmonic appears. Two incommensurate
        # wanders so the harmonics do not all rise and fall as one.
        # `tablei ..., ixmode 1, ixoff 0.5` reads the table over an input range of
        # -0.5 .. +0.5, so the DRIVE MUST PEAK BELOW 0.5. At 0.62 + 0.22 + 0.09 it
        # peaked at 0.910 and 34% of all samples sat pinned at a table end (the
        # ends are not symmetric, so that is a hard clipper, not a shaper -- the
        # exact "harmonics were whatever the clipper gave" charge this branch was
        # written to answer). 0.30 + 0.12 + 0.05 = 0.47 stays inside the table and
        # still sweeps 26%..94% of the curve, which is the whole point: near the
        # middle the sine comes out almost pure, at the top every harmonic is there.
        L.append(f"  kdrv{tag}   poscil 0.12, 0.074           ; drive wander")
        L.append(f"  kdrb{tag}   poscil 0.05, 0.119           ; second, incommensurate")
        L.append(f"  adrv{tag}   poscil 0.30 + kdrv{tag} + kdrb{tag}, kfreq")
        L.append(f"  ash{tag}    tablei adrv{tag}, giCheb, 1, 0.5 ; Chebyshev transfer curve")
        L.append(f"  {ov}    = ash{tag} * 0.55           ; level-match the family")
    elif technique == "ring_mod":
        # A genuine product of two oscillators -- the sidebands are real, not
        # tabulated. `poscil` rather than `oscili`: same cost, no table
        # interpolation error, which matters when the output is a product (both
        # factors' error multiplies).
        #
        # A diode ring is never balanced: the diodes are not matched, so some
        # carrier LEAKS THROUGH unmodulated, and the leak drifts with temperature.
        # That feedthrough is the characteristic sound of real analogue ring
        # modulators -- the played note stays faintly present under the metallic
        # sidebands instead of vanishing -- and because it drifts, the balance
        # between note and sidebands keeps shifting.
        #
        # The modulator's own oscillator drifts too, and the size of that drift is
        # what makes it audible or not: at +-0.0009 it moved the sidebands by 0.2 Hz
        # and measured a spectral travel of 1.01x, i.e. nothing. +-0.004 is about 7
        # cents on the modulator, which walks the lower sideband audibly and is
        # honest for a free-running analogue oscillator.
        L.append(f"  krmw{tag}   poscil 0.004, 0.091          ; modulator drift, ~7 cents")
        L.append(f"  krlk{tag}   poscil 0.03, 0.037           ; diode imbalance, drifting")
        L.append(f"  acar{tag}   poscil 0.8, kfreq")
        L.append(f"  amod{tag}   poscil 1.0, kfreq * (2.0 + krmw{tag})")
        L.append(f"  {ov}    = acar{tag} * (amod{tag} * 0.88 + 0.10 + krlk{tag}) ; ring mod + carrier leak")
    elif technique == "sub_sine":
        # A sub oscillator: the fundamental PLUS real weight one octave below it.
        # (`sine` and `sub_sine` were byte-identical -- the sub is the whole point
        # of the key. The played pitch still dominates, so this adds weight
        # without transposing the note: pitch stays the synth's business.)
        #
        # An analogue sub is a DIVIDER, so it is a pulse and rich, never a clean
        # sine -- and that matters here for more than accuracy: a pure sine at f/2
        # shares no frequency with the fundamental and cannot beat with it, which
        # is why the two-oscili version sat perfectly still. `gbuzz` gives the
        # divider its harmonics, so the sub's 2nd lands ON the fundamental and the
        # small divider drift makes the pair beat slowly -- the weight breathes
        # instead of being added as a constant.
        #
        # Exactly TWO harmonics. At three, the sub's 3rd sits at 1.5x the played
        # note and measured -26 dB: an audible hollow fifth in a key whose whole
        # job is to add weight under the note. (A square-wave divider has odd
        # harmonics only and no component at f at all, so it could not beat either;
        # two harmonics is the pulse divider that actually does what the key says.)
        L.append(f"  ksbw{tag}   poscil 0.0006, 0.067         ; divider drift")
        # 0.28, not 0.20: gbuzz normalises to peak, so two cosines at kamp 0.20 put
        # only 0.129 on the 110 Hz partial -- 2.3 dB less weight than the plain
        # oscili it replaces, in the one key whose entire job is weight.
        L.append(f"  asb{tag}    gbuzz 0.28, kfreq * 0.5 * (1 + ksbw{tag}), 2, 1, 0.55, giCos ; sub octave (divider)")
        L.append(f"  afn{tag}    poscil 0.50, kfreq          ; fundamental")
        L.append(f"  {ov}    = afn{tag} + asb{tag}")
    elif technique == "flute":
        # Csound's real blown-pipe waveguide, not three sines with a noise band
        # bolted on. The old idiom mixed `rand` through a band-pass at 0.55 to
        # stand in for breath, and it measured as noise: flatness 0.391 for the
        # bare key with no adjective at all (a supersaw is 0.007). Adding static
        # noise is forbidden -- the SYNTH owns a noise module, env-controllable,
        # and an oscillator that bakes hiss in takes that choice away.
        #
        # Csound's own wgflute was tried FIRST and rejected on measurement: it does
        # not track the requested pitch. Asked for 110/165/220/330/440/660 Hz with
        # the noise gain at zero it produced 338/178/233/342/457/678 -- +1945, +135,
        # +102, +64, +64, +47 cents, a pitch-dependent error, not an offset that
        # could be compensated -- and the jet ratio shifts it further (jet 0.16 ->
        # +508 cents, 0.48 -> +1831 at the same note). Pitch belongs to the synth,
        # so a model that invents its own register is unusable here however good it
        # sounds. wgclar by contrast lands dead in tune at every pitch, so this is
        # about wgflute specifically, not about waveguides.
        #
        # So: the flute's harmonics come from `gbuzz`, whose ROLLOFF is k-rate, and
        # the thing that moves them is the thing that really moves in a flute --
        # BREATH PRESSURE. At low kmul the stack collapses towards a sine, at high
        # kmul the upper partials come up: that IS what harder blowing does, and it
        # is one continuous k-rate parameter, so no partial has to be listed and
        # nothing clicks. Depth and rate are what the movement gate measures, not
        # what reads well in a comment: the earlier three-oscili version at 0.19 Hz
        # moved the centroid 1.05x/4.7Hz -- static. Pressure has to swing wide and
        # cycle in about 3 s to register.
        #
        # One breath is honestly ONE variable, so the harmonics would all move
        # together -- true of a flute, but it leaves the tone glassy. A real air
        # column is never quite stable at the octave, so a separate octave partial
        # drifts on its own rate and beats against the body's own 2nd harmonic:
        # one genuinely decorrelated partial, which is what the instrument has.
        L.append(f"  kbr{tag}    poscil 0.5, 0.31             ; breath-pressure cycle, ~3 s")
        L.append(f"  kfm{tag}    = 0.24 + 0.16 * kbr{tag}      ; pressure opens the harmonics")
        L.append(f"  abd{tag}    gbuzz 0.34, kfreq, 4, 1, kfm{tag}, giCos ; blown harmonic stack")
        L.append(f"  kwo{tag}    poscil 0.0009, 0.077         ; air-column wander, own rate")
        L.append(f"  aoc{tag}    poscil 0.05, kfreq * 2 * (1 + kwo{tag})  ; octave, beats with the body's 2nd")
        L.append(f"  {ov}    = abd{tag} + aoc{tag}")
    elif technique == "theremin":
        # The heterodyne tone: a theremin makes its note by MIXING two RF
        # oscillators and keeping the difference, and the mixer's nonlinearity is
        # what gives the tone its small, unstable upper-partial content. `foscili`
        # at a 1:1 ratio is that same nonlinear mixing at audio rate -- an index
        # near zero is a pure sine, and raising it brings the harmonic series up
        # continuously. So the waver is produced by the mechanism the instrument
        # actually uses, instead of a hand-set 2nd-harmonic level.
        #
        # Its famous vibrato is PITCH, which belongs to the synth's glide, not to
        # the oscillator, so the oscillator carries the timbral waver and nothing
        # else. The waver has to be DEEP enough to be a timbre, not a rounding
        # error: the earlier fixed-partial version at 0.10 +- 0.07 measured centroid
        # travel 1.07 / std 6.6 Hz, i.e. indistinguishable from a static tone.
        # A 1:1 ratio is the obvious way to write "mixing" and it is wrong here: in
        # `foscili` the sideband at kcps*(kcar - n*kmod) lands on DC whenever
        # kcar/kmod is a whole number, so 1:1 puts -J1(index) straight on 0 Hz.
        # Measured: -0.032 DC, 18% of peak, and because the index wavers the offset
        # WANDERS with it at 0.9 Hz. 1:2 has no integer n with kcar = n*kmod, so no
        # sideband can reach DC -- and the surviving partials are the odd series,
        # which is what a symmetric nonlinearity produces anyway.
        L.append(f"  kwv{tag}    poscil 0.5, 0.9              ; slow timbre waver")
        L.append(f"  kndx{tag}   = 0.42 + 0.34 * kwv{tag}       ; heterodyne mix depth")
        L.append(f"  {ov}    foscili 0.55, kfreq, 1, 2, kndx{tag}, giSine ; heterodyne nonlinearity")
    elif technique == "sine":
        # One oscillator. `poscil` rather than `oscili`: same cost, no table
        # interpolation error. This is also the movement escape hatch -- when the
        # prompt asks for a standing tone this is what it gets, so it must be pure.
        L.append(f"  {ov}    poscil 0.6, kfreq            ; pure tone")
    elif technique == "additive":
        # `gbuzz` IS Csound's additive oscillator: it sums cosine harmonics, and
        # both the harmonic COUNT and the amplitude ROLLOFF are k-rate. So the
        # generic "a stack of harmonics" key is the substrate's own harmonic stack,
        # not a typed-out list of levels that can never change.
        #
        # Two stacks a few cents apart rather than one, for the reason the organ
        # taught: one gbuzz is ONE rank, and every partial inside it moves as the
        # single hidden variable moves. Detuned twins beat instead -- and harmonic n
        # beats at n times the detuning, so every partial breathes at its OWN rate
        # with nothing driving them in common. The life is structural.
        #
        # LEVELS: `gbuzz` normalises to PEAK, not RMS, and a stack of in-phase
        # cosines is a pulse train -- crest 3.2 here against a sine's 1.41. Read as
        # if kamp were an RMS level it lands ~12 dB under its neighbours, which
        # reads to a player as a broken key rather than a quiet one. Fewer harmonics
        # (7, not 10) lowers the crest, and the twin sits a third of the way down so
        # the two peaks do not coincide.
        L.append(f"  kad{tag}    poscil 0.0004, 0.053         ; slow drift, upper stack")
        L.append(f"  aa1{tag}    gbuzz 0.52, kfreq, 7, 1, 0.68, giCos          ; harmonic stack")
        L.append(f"  aa2{tag}    gbuzz 0.34, kfreq * (1.0018 + kad{tag}), 7, 1, 0.64, giCos ; twin, +3 cents")
        L.append(f"  {ov}    = aa1{tag} + aa2{tag}")
    elif technique == "harpsichord":
        # A harpsichord is plucked by a quill and has TWO CHOIRS of strings (8' and
        # 4'), which is the same rank structure as the organ and the same source of
        # life: the choirs are never in perfect tune, so the tone rolls slowly.
        #
        # `pluck` and the waveguide pluck opcodes were not used, although they are
        # the obvious "real" idiom: they decay on their own. The oscillator here is
        # a SPECTRUM source and the synth owns the envelope, so a generator that
        # insists on its own decay would fight the instrument's amp envelope and
        # take the choice away from the player. `gbuzz` gives the quill's bright,
        # nasal spectrum -- a shallow rolloff so the upper harmonics stay strong.
        #
        # klh stays 1 on BOTH choirs. Setting the 4' choir to klh=2 was meant to
        # thin it, but that choir already runs at 2.0031*f, so its lowest partial
        # landed at 4.006*f: a 2' rank, two octaves up, not the octave choir the
        # comment claimed. Same level caveat as `additive` -- gbuzz normalises to
        # peak, and 14 in-phase cosines are a pulse train (crest 4.06), so the key
        # measured 15 dB under its neighbours until the amplitudes were raised and
        # the harmonic count brought down.
        L.append(f"  khw8{tag}   poscil 0.00040, 0.071        ; 8' choir tuning drift")
        L.append(f"  khw4{tag}   poscil 0.00055, 0.094        ; 4' choir, incommensurate")
        L.append(f"  ah8{tag}    gbuzz 0.56, kfreq * (1 + khw8{tag}), 9, 1, 0.80, giCos ; 8' choir, bright")
        L.append(f"  ah4{tag}    gbuzz 0.24, kfreq * 2.0031 * (1 + khw4{tag}), 6, 1, 0.72, giCos ; 4' choir")
        L.append(f"  {ov}    = ah8{tag} + ah4{tag}")
    else:
        # Nothing routable lands here any more -- every key in the catalogue has a
        # real idiom above. A key with no idiom is the case BJ decided the LLM
        # should write code for; until that exists it gets a plain tone rather than
        # a table, so an unrouted key is audible as exactly what it is.
        L.append(f"  {ov}    poscil 0.6, kfreq            ; unrouted key: bare tone")
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


def _emit_crossfade_morph(chain, imorphtime, tag="0", nmodes=None):
    """A generic amplitude-crossfade morph -> `aosc<tag>`, used whenever a chain
    stage cannot live in the tonal additive-partial bank of _emit_morph: a NOISE
    texture (aperiodic, no partials), a VOICE/formant stage (a filtered source,
    not an oscillator bank), a MODAL metal/glass stage (a driven resonator bank),
    or a mix. The additive morph would silently degrade
    such a stage to a PITCHED additive tone -- a migration-discipline capability
    loss the frozen corpus caught (2026-07-18: 'rain > silence' rendered an
    additive tone that faded, pitchedness 0.99, not rain). Here each stage renders
    to its OWN audio
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

    # The GAINS come first, because each stage's DSP is then rendered INSIDE
    # `if <its gain> > 0`. In a crossfade only the two adjacent stages around the
    # playhead are audible; every other stage's tent gain is exactly 0, so its
    # output is multiplied away -- yet it used to compute a full k-cycle of audio
    # regardless. That waste is what broke the block budget: 3 oscillators each
    # morphing cymbal>glass>struck_bar rendered 9 modal banks at once and benched
    # 229us against the 133us gate (172%), while at most 2 banks were ever heard.
    # Gating on the tent makes cost track what is AUDIBLE, not what was written.
    # Verified empirically before adopting (scratchpad/condprobe.csd): an opcode
    # whose branch is false at i-time still initialises and sounds correctly once
    # the branch goes true at k-time -- silent while false, full level after.
    # `init 0` keeps each stage var defined for the sum on the k-cycles it is
    # skipped; the tent rises from exactly 0, so a resuming stage fades in from
    # nothing and cannot click.
    stage_vars, gains = [], []
    for j, k in enumerate(chain):
        if k in ("silence", "zero"):
            stage_vars.append(None)   # terminal fade: neighbours' tents carry it to 0
            gains.append(None)
            continue
        stage_vars.append(f"aosc{tag}m{j}")
        gains.append(f"k{tag}g{j}")
    L.append("  if changed2(ktrig) == 1 then")
    L.append(f"    reinit {lbl}")
    L.append("  endif")
    L.append(f"{lbl}:")
    for j, gj in enumerate(gains):
        if gj is None:
            continue
        # linear tent: 1 at breakpoint j, 0 at every other breakpoint (adjacent
        # stages cross). n values, n-1 legs of equal duration. In any leg exactly
        # two adjacent tents are non-zero and sum to 1, so sqrt(tent) gives an
        # equal-POWER crossfade (sum of squares = 1) -- flat loudness for incoherent
        # noise. sqrt is safe: linseg stays in [0,1].
        seg = ", ".join(f"{(1.0 if b == j else 0.0):.4f}, {leg:.4f}" for b in range(n - 1))
        seg += f", {(1.0 if (n - 1) == j else 0.0):.4f}"
        L.append(f"  k{tag}L{j}   linseg {seg}")
        L.append(f"  {gj}   = sqrt(k{tag}L{j})")
    L.append("  rireturn")

    terms = []
    for j, (var, gj) in enumerate(zip(stage_vars, gains)):
        if var is None:
            continue
        k = chain[j]
        sub = f"{tag}m{j}"
        L.append(f"  {var}  init 0")
        L.append(f"  if {gj} > 0 then")
        body = _emit_noise(k, sub) if k in _NOISE_TECH else _emit_steady(k, sub, nmodes)
        L.append("\n".join("  " + ln for ln in body.splitlines()))
        L.append("  endif")
        terms.append(f"{var} * {gj}")
    L.append(f"  aosc{tag}    = " + (" + ".join(terms) if terms else "0"))
    return "\n".join(L)


def _emit_oscillator(oi, chain, imorphtime, nmodes=None):
    """One oscillator (index `oi`, 0..2) from its technique chain -> (body_lines,
    out_var). >=2 stages -> a morph, choosing the path by stage kind:
      - a PURE vowel sweep (>=2 voice stages, no silence) -> _emit_voice_morph, the
        native formant glide ('ah'->'ee');
      - any NOISE or VOICE stage otherwise (incl. voice>silence, voice+tonal) ->
        _emit_crossfade_morph, which renders each stage on its own and amplitude-
        crossfades (noise/voice cannot live in the additive partial bank);
      - purely tonal -> _emit_morph, the additive-partial spectral morph.
    A single stage -> a steady technique. The out_var is `aosc<oi>`."""
    tag = str(oi)
    body = None
    if len(chain) >= 2:
        real = [k for k in chain if k not in ("silence", "zero")]
        has_silence = any(k in ("silence", "zero") for k in chain)
        if any(k in _NOISE_TECH or k in _MODAL_TECH or k in _LIVE_TECH
               or _MORPH_SPECTRUM.get(k) in _SUBFUND_SPECTRA for k in chain):
            # noise, modal AND live-opcode stages cannot live in the additive
            # partial bank: render each stage real (noise / mode bank / its own
            # live idiom) and crossfade.
            body = _emit_crossfade_morph(chain, imorphtime, tag, nmodes)
        elif real and all(k in _VOICE_TECH for k in real) and len(real) >= 2 and not has_silence:
            body = _emit_voice_morph(chain, imorphtime, tag)   # pure vowel sweep
        elif any(k in _VOICE_TECH for k in real):
            body = _emit_crossfade_morph(chain, imorphtime, tag, nmodes)  # voice+silence / voice+tonal
        else:
            body = _emit_morph(chain, imorphtime, tag)
    if body is None:
        body = _emit_steady(chain[0] if chain else "sine", tag, nmodes)
    return body, f"aosc{tag}"


# M3 adjective consolidation: every lexicon adjective maps to one or more of a
# small set of BOUNDED, in-place DSP operations on the mixed `asig`, chosen to
# honour the lexicon's own "why". Operations (emit order below):
#   BODY  g      -> add a low-passed copy (weight below): `asig + tone(asig,220)*g`
#   THIN  fc     -> high-pass out the body (thinner): `atone asig, fc`
#   FORMANT (fc,g) -> a peak-normalised reson bump (nasal/reedy/boxy/resonant)
#   CLANG (r,w)  -> ring modulation at an irrational ratio of the played note =
#                   REAL inharmonic partials at f*(n +- r) (metallic/clangorous/
#                   glassy/brittle, whose lexicon deltas all carry an `inharm` op)
#   DARK  fc     -> one-pole low-pass tilt (dark/warm/mellow/...)
#   BRIGHT s     -> add a high-passed copy (more upper energy): `asig + atone(asig,2200)*s`
#   DIRT (k,g)   -> tanh waveshaper = REAL new harmonics (dirty/distorted/buzzy/...)
#   AIR   g      -> exciter: waveshape the top band so it breeds partials above
#                   itself (airy/breathy/shimmering/icy) -- spectral, never noise
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
    # The four adjectives whose lexicon delta carries an `inharm` op get REAL
    # inharmonicity from a ring modulator (see CLANG below), not just the reson
    # band emphasis they all used to share -- a filter reweights the harmonic
    # grid, it cannot move a partial off it, and all four measured inharm 0.000.
    # Ratios differ per adjective so they stay four distinct colours: 1.41 (sqrt2)
    # is the densest clang per unit wet, 2.37 is what densifies an FM base's
    # sidebands (13 -> 30 partials on fm_bell), 5.43 throws a sparse high sheen.
    # Each of the four deltas is TWO claims -- a `tilt` and an `inharm` -- and the
    # reson band emphasis they all shared served neither: measured across saw,
    # organ, sine and fm_bell it added 0.000 inharmonicity everywhere, cost ~2x
    # peak (saw 0.190 -> 0.450), pulled the saw centroid DOWN 500 Hz and lowered
    # the partial count. So `tilt` is now a modest BRIGHT and `inharm` is CLANG,
    # and the op that measured as doing neither is gone.
    "metallic":   [("BRIGHT", 0.35), ("CLANG", (2.37, 0.275))],
    "smooth":     [("DARK", 2600)],
    "shimmering": [("BRIGHT", 0.35), ("AIR", 0.12)],
    "airy":       [("THIN", 300), ("AIR", 0.20)],
    "harsh":      [("DIRT", (3.2, 0.70)), ("BRIGHT", 0.50)],
    "woody":      [("DARK", 1900), ("FORMANT", (600, 0.30))],
    "deep":       [("BODY", 0.60), ("DARK", 900)],
    "glassy":     [("BRIGHT", 0.50), ("CLANG", (5.43, 0.275))],
    "brittle":    [("THIN", 400), ("BRIGHT", 0.55), ("CLANG", (5.43, 0.22))],
    "clangorous": [("BRIGHT", 0.30), ("CLANG", (1.41, 0.44))],
    "growling":   [("DIRT", (2.6, 0.70)), ("DARK", 1600)],
    "punchy":     [("BRIGHT", 0.40), ("BODY", 0.25)],
    "mellow":     [("DARK", 2000)],
    "sharp":      [("BRIGHT", 0.85)],
    "round":      [("DARK", 2200)],
    "cold":       [("BRIGHT", 0.60)],
    # lexicon `why`: "a grimy per-frame SCATTER — deterministic amplitude+phase
    # jitter …, NOT a static upper-mid tilt". So dirty is NOT the frozen `distorted`
    # waveshaper it used to share: it gets its own GRIME op whose drive JITTERS, so
    # the harmonics keep re-scattering. (In the wavetable paradigm the scatter was
    # per-station; the csound-code equivalent is continuous k-rate jitter.)
    "dirty":      [("GRIME", (2.6, 0.70))],
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
            elif name in ("BODY", "BRIGHT", "AIR", "DRIFT"):
                acc[name] = max(acc[name], val)
            elif name == "DIRT":
                acc[name] = val if val[0] > acc[name][0] else acc[name]   # stronger drive
            elif name == "GRIME":
                acc[name] = val if val[0] > acc[name][0] else acc[name]   # grimier scatter
            elif name == "CLANG":
                acc[name] = val if val[1] > acc[name][1] else acc[name]   # wetter clang wins
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
        # The cutoff must TRACK the note. Fixed at 300-400 Hz it sat above the
        # fundamental of every note below ~G4 and high-passed the fundamental away:
        # `an organ, brittle` at A3 came out an octave high (+1200 cents, strongest
        # partial 440 not 220), because a one-pole at 400 Hz leaves the 2nd harmonic
        # 1.5x more of its level than the 1st and the organ's fundamental has no
        # margin to spare. Pitch belongs to the synth, so "thin" has to mean "less
        # body RELATIVE to this note", not "delete everything under 400 Hz". `limit`
        # gives min(nominal, kfreq*0.75): unchanged for notes above the nominal,
        # scaled down below it, so the fundamental keeps ~80% of its level at any
        # pitch. (Pre-existing since the adjective consolidation; found while
        # measuring the four inharm adjectives, all of which pass through THIN or
        # sit next to something that does.)
        L.append(f"  kthn     limit kfreq * 0.75, 20, {int(acc['THIN'])}       ; thin, but never above the fundamental")
        L.append(f"  asig     atone asig, kthn            ; thin (remove low body)")
    if "FORMANT" in acc:
        fc, g = acc["FORMANT"]
        L.append(f"  afrm     reson asig, {int(fc)}, {int(fc * 0.4)}, 2")
        L.append(f"  asig     = asig + afrm * {g:.2f}       ; formant / mid bump")
    if "CLANG" in acc:
        # Ring modulation = the substrate's own inharmonicity. Sidebands land at
        # f*(n +- r); for an irrational r they sit OFF the harmonic grid, on any
        # source spectrum -- which is exactly what the `metallic` why asks for
        # ("on any spectrum"). Two measured bounds decide the numbers:
        #   * PITCH. The dry path carries the fundamental (ring mod alone has no
        #     component at the carrier), and because the blend is CONVEX it scales
        #     every dry component by the same (1-w): the played fundamental keeps
        #     exactly 1-w of its level and no dry partial can overtake another.
        #     Measured across 55/110/220 Hz: 0.725 at w=.275, 0.560 at w=.44, i.e.
        #     1-w to three decimals, pitch-invariant. The ceiling is the point where
        #     the ADDED sidebands outweigh what is left of the fundamental: at wet
        #     0.70 the strongest partial jumps (+1522 cents on saw r=1.41, +2577 on
        #     saw r=5.43, -1547 on organ r=1.41), which would hand pitch to the
        #     oscillator, and pitch is the synth's. Wet 0.55 was clean, so the map
        #     is 0.55 * the lexicon's own inharm amount -- clangorous .8 -> .44,
        #     metallic/glassy .5 -> .275, brittle .4 -> .22 -- all under the cliff.
        #     Ring mod also puts DIFFERENCE tones below the fundamental (0.7% of
        #     total energy at w=.275, 8.8% at w=.44, the same at every pitch). That
        #     is what a ring modulator is, and it stays bounded; only at the very
        #     bottom of the range (A1) does any of it fall under 30 Hz.
        #   * LEVEL is free: |asig * amod| <= |asig| for a unit modulator and the
        #     blend is convex, so peak cannot rise (measured 0.190 dry -> 0.190 at
        #     every setting). The reson emphasis these adjectives used to rely on
        #     alone pushed the same saw to 0.450.
        r, w = acc["CLANG"]
        L.append(f"  armc     oscili 1, kfreq * {r:.4f}       ; inharmonic modulator")
        L.append(f"  arng     = asig * armc")
        L.append(f"  asig     = asig * {1.0 - w:.3f} + arng * {w:.3f}  ; clang (real inharmonic partials)")
    if "AIR" in acc:
        # The exciter's source has to be taken BEFORE the dark tilt. `washed_out`
        # is DARK 2800 + AIR, and a low-pass at 2800 removes the very band the
        # exciter feeds on: sourced after it, the adjective measured flatness
        # 0.0074 and peak 0.181 against a dry 0.0073/0.186 -- a silent no-op. A
        # dark body with an airy top is a perfectly ordinary thing to ask for, so
        # the sheen is bred from the untilted signal and added on top of the tilt.
        L.append("  aprea    = asig                       ; exciter source, pre-tilt")
    if "DARK" in acc:
        L.append(f"  asig     tone asig, {int(acc['DARK'])}            ; dark tilt (low-pass)")
    if "BRIGHT" in acc:
        L.append("  abrt     atone asig, 2200")
        L.append(f"  asig     = asig + abrt * {acc['BRIGHT']:.2f}       ; bright tilt (add highs)")
    if "DIRT" in acc:
        k, g = acc["DIRT"]
        L.append(f"  asig     = tanh(asig * {k:.2f}) * {g:.2f}    ; dirt / overdrive (real harmonics)")
    if "GRIME" in acc:
        # `dirty` per lexicon = a grimy SCATTER, not a static tilt: the waveshaper
        # drive JITTERS (fast interpolated random), so the generated harmonics keep
        # re-scattering instead of freezing into one fixed distortion. A static tanh
        # measurably made the sound DEADER than dry (liveliness 3.16 vs 4.07) —
        # saturation flattens variation, so "dirty" removed life instead of adding
        # it (tools/csound_liveliness_gate.py, 2026-07-18).
        k, g = acc["GRIME"]
        # iseed MUST lie in [0,1]. It was 2, and above 1 Csound reseeds from the
        # system clock: the same patch rendered differently on every run (max|diff|
        # 0.065 on a 0.188 peak) -- the exact fault that was fixed for the analog
        # micro-detune a few lines below, left standing here.
        L.append(f"  kgrm     randi 0.45, 11, 0.5          ; grime scatter (jittering drive)")
        L.append(f"  asig     = tanh(asig * ({k:.2f} * (1 + kgrm))) * {g:.2f}  ; dirty = moving grime")
    if "AIR" in acc:
        # "airy" is a SPECTRAL property -- a frequency distribution, the way an
        # enhancer/exciter works -- and has nothing to do with noise. This op used
        # to be `rand` through a high-pass, added to the signal, which measured as
        # what it was: a dry supersaw has spectral flatness 0.007, and `airy` drove
        # it to 0.469, more than halfway to white noise. BJ heard exactly that
        # ("ein starkes pinkes rauschen ... 'airy' hat ZERO mit statischem Rauschen
        # zu tun"). It is also a boundary violation: the SYNTH owns a noise module,
        # env-controllable and switchable, and an oscillator that bakes noise in
        # takes that away from the player -- the same mistake as owning the
        # envelope or the glide.
        #
        # The air is now GENERATED FROM THE SIGNAL: take the band above 3.5 kHz,
        # waveshape it so it breeds new partials above itself, and blend that back.
        # That is what an exciter does -- signal-derived, deterministic, tonal, and
        # it moves when the note moves instead of sitting there as a static hiss.
        L.append("  aexc     atone aprea, 3500             ; the band that carries air")
        L.append("  aexc     = tanh(aexc * 3.0) * 0.5      ; breed partials ABOVE it")
        L.append(f"  asig     = asig + aexc * {acc['AIR']:.2f}       ; air / sheen (no noise)")
    if "DRIFT" in acc:
        # lexicon `why` for analog: "slow coherent drift — amplitude wobble + a tiny
        # analog micro-DETUNE". Only the wobble existed; the detune was missing, so
        # `analog` measured no livelier than dry. The detune is done the standard
        # way for a mixed bus: a slowly wandering fractional delay (vdelay3) REPLACES
        # the signal (replacing, not mixing, so it micro-detunes instead of combing).
        L.append(f"  adrf     oscili {acc['DRIFT']:.3f}, 0.7")
        L.append("  asig     = asig * (1 + adrf)         ; slow analog drift (amplitude wobble)")
        # micro-detune size: the pitch offset is -d(delay)/dt, so for a delay
        # A*sin(2*pi*f*t) the peak offset is 2*pi*f*A. Two incommensurate slow LFOs
        # (2.5 ms @ 0.13 Hz -> 3.5 cents, 1.5 ms @ 0.31 Hz -> 5.1 cents) sum to a
        # never-repeating wander of ~8 cents peak: audible as analog instability,
        # still "a TINY micro-detune" per the lexicon. DETERMINISTIC on purpose --
        # `randi` drew a fresh path per run, so the same patch detuned by 6.2 cents
        # once and 2.0 the next time (measured), which is both an unstable sound and
        # an unfalsifiable test. MEASURED, not assumed (tools/csound_liveliness_
        # gate.py tracks f0): +-0.55 ms @ 0.31 Hz gave 0.07% and +-2.5 ms @ 0.6 Hz
        # only 2.2 cents — both below audibility.
        L.append("  kdrfa    oscili 2.5, 0.13            ; slow tuning wander (~3.5 cents)")
        L.append("  kdrfb    oscili 1.5, 0.31            ; second, incommensurate (~5 cents)")
        # kdrf*, not kdt*: kdt<n> is chiptune's duty on oscillator n, so the two
        # collided whenever chiptune landed on oscillator 1 or 2. It was masked
        # purely by emission order (oscillator bodies precede adjectives), i.e. a
        # bug waiting for that order to change.
        L.append("  asig     vdelay3 asig, 10 + kdrfa + kdrfb, 40 ; analog micro-detune")
    return "\n".join(L)


# Techniques whose spectrum is SPARSE enough that a filter sweep has nothing to
# work on: at most a fundamental plus one or two weak partials. These are exactly
# the sources for which the waveshaper below is both necessary AND harmless (see
# _emit_motion's docstring for the measurement that splits the two families).
_SPARSE_TECH = {"sine", "sub_sine", "theremin", "flute", "triangle"}


def _emit_motion(motion_key, oscs=None):
    """A k-rate motion so 'moving' prompts actually move (movement by default).

    The spectral family (sweep/evolve/open_up/close/breathe/wobble/cycle) makes the
    spectrum open and close over the note. HOW it does that depends on what the
    patch's sources actually contain, because the two available operators fail on
    exactly opposite material:

      * A WAVESHAPER (tanh at an LFO-swept drive) MAKES harmonics, so it moves even
        a bare sine -- which is why it was introduced: the frozen NL corpus caught
        'evolving analog drone', 'wobbling acid bass' and 'a bass that slowly opens
        up' all rendering static, because the 7B had routed them onto sine/sub_sine
        and the older cutoff sweep had nothing above the cutoff to remove.
        But on a DENSE source it intermodulates every partial PAIR, and on an
        INHARMONIC one those products land off every grid: audible distortion.
        Measured in the built Standalone on `a slowly evolving bell` (fm_bell,
        c:m 1:1.41, index 3.2) against the same patch with no motion -- identical
        source, motion the only difference:
            crest factor      1.57 constant   ->  1.13 .. 1.35   (waveform squared off)
            energy above 3kHz 0.036 .. 0.129  ->  0.086 .. 0.244
            partials          the FM grid     ->  + 1800, 2167, 3367, 3733, 4103 Hz
        2167 Hz is 2*1213 - 260, a textbook cubic intermodulation product; the 3367/
        3733/4103 trio is the FM grid regenerated far above where the dry bell has
        any energy at all. This is BJ's "die Bells und Metals sind im Synth extrem
        verzerrt" (2026-07-19), and the reason it survived every check for days is
        the `balance` on the next line: loudness is held identical while the
        waveform is destroyed, so no peak/RMS gate can see it.

      * A FILTER SWEEP adds NO partial whatsoever -- it only re-weights what is
        already there -- so it cannot intermodulate anything. It is silent only on
        a source with nothing to re-weight.

    So the operator is chosen by the material, and the two conditions are
    complementary: every source falls cleanly into one side. A patch whose every
    stage is in _SPARSE_TECH gets the waveshaper (safe there precisely BECAUSE the
    spectrum is sparse: tanh on a near-sine breeds a clean odd-harmonic series,
    which is the intended "harmonics grow and retract"); everything else gets a
    pitch-tracked resonant lowpass sweep.

    The shimmer family (shimmer/vibrate/flutter/tremolo) stays a fast amplitude
    tremolo (already source-agnostic). The speed/intent motions (slow/fast/snap/
    pingpong/settle) are the same spectral motion at a mapped rate -- the lexicon
    frames them as how-fast the timbre travels, so they are just rate variants
    (slow 0.08 .. snap 0.9 Hz)."""
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
        rate = _SPECTRAL_RATE.get(motion_key, 0.16)
        stages = [k for o in (oscs or []) for k in o.get("chain", [])
                  if k not in ("silence", "zero")]
        sparse = bool(stages) and all(k in _SPARSE_TECH for k in stages)
        if sparse:
            # Nothing to re-weight: MAKE harmonics. Safe here because a sparse
            # near-sine spectrum has no partial pairs to intermodulate -- tanh
            # breeds an ordinary odd-harmonic series on top of the fundamental.
            # `balance` holds loudness steady: without it tanh(x*kdrv) ~= x*kdrv
            # at small signal, so the drive LFO would pump level ~2.4x (an
            # evolving PAD must not throb; that belongs to shimmer/tremolo).
            return (f"  kmot     oscili 0.5, {rate}             ; -0.5..0.5 motion LFO\n"
                    "  kdrv     = 1.0 + 3.4 * (kmot + 0.5)   ; waveshaper drive 1.0..4.4\n"
                    "  awsh     = tanh(asig * kdrv)          ; sparse source: breed harmonics\n"
                    "  asig     balance awsh, asig           ; steady loudness (spectral, not tremolo)")
        # Dense / inharmonic source: re-weight what is already there. The cutoff
        # TRACKS PITCH (a fixed-Hz sweep would sit wide open on a top note and
        # shut on a bottom one), travelling x1.8 .. x26 the fundamental -- about
        # 3.9 octaves, from "fundamental plus one partial" to "everything through".
        # Exponential in the LFO so the sweep is even in pitch, not in Hz.
        # `rezzy` rather than `moogladder`: measured 11.7us against 116us for the
        # same orchestra elsewhere in this module, and this stage is post-mix so
        # the whole patch pays it once. Modest resonance -- enough that passing
        # partials are emphasised (what makes a sweep read AS a sweep), short of
        # the self-whistle that would be a new artefact on an inharmonic bank.
        return (f"  kmot     oscili 0.5, {rate}             ; -0.5..0.5 motion LFO\n"
                "  apre     = asig                       ; loudness reference\n"
                "  kcut     limit kfreq * exp(1.923 + 2.670 * kmot), 150, 15000 ; x1.8..x26 f0\n"
                "  afil     rezzy asig, kcut, 3          ; dense source: re-weight, add nothing\n"
                "  asig     balance afil, apre           ; steady loudness (spectral, not tremolo)")
    if motion_key in ("shimmer", "vibrate", "flutter", "tremolo"):
        return ("  ksh      oscili 0.18, 5.5\n"
                "  asig     = asig * (1 + ksh)           ; fast shimmer")
    return ""


def _normalize_oscs(technique_keys, oscs):
    """Resolve the two call conventions to a clean list of up to 3 oscillator
    specs [{chain:[keys], vol:float, register:float}]. Back-compat: a bare
    technique_keys list is one oscillator at vol 1.0 and 8' (byte-for-byte the
    pre-M2 single-osc emission). An oscillator whose chain is ONLY silence
    produces nothing and is dropped; if every osc drops, fall back to a single
    sine so the orchestra is never empty."""
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
        # register: the pitch multiplier of the layer's organ footage. Only the
        # values the footage table defines are legal -- anything else (a model
        # writing "12" or a hand-edited preset) falls back to 8' rather than
        # transposing the layer to an arbitrary interval.
        try:
            reg = float(o.get("register", 1.0))
        except (TypeError, ValueError):
            reg = 1.0
        if reg not in _FOOTAGE_MULT.values():
            reg = 1.0
        out.append({"chain": chain, "vol": max(0.0, min(1.0, vol)), "register": reg})
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
        out = [{"chain": ["sine"], "vol": 1.0, "register": 1.0}]
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
    nmodes = _modal_budget(oscs)   # thin modal banks to fit the block budget

    # emit each oscillator (tagged, collision-free) and build the weighted mix.
    # The mix gain is 1/max(1, sum_vol): per-osc vol is a RELATIVE weight, so
    # overall loudness stays ~constant as layers are added (a "sensible overall
    # mix" -- adding a layer enriches the timbre without a loudness jump), and a
    # single osc at vol 1.0 is unchanged (gain 1.0). HEADROOM still bounds it.
    bodies = []
    mix_terms = []
    # ONE gain law for every layer. Independent layers sum INCOHERENTLY, so their
    # combined level grows with sqrt(N), and dividing by sqrt(N) is what actually
    # keeps "adding a layer enriches the timbre without a loudness jump" true. The
    # old 1/N divided by the coherent bound instead, which only held for layers
    # that were literally the same signal -- and made every genuinely different
    # 3-layer patch 4.8 dB quieter than a single one. There is deliberately no
    # per-case correction here: a mix gain that needs to know whether its inputs
    # happen to correlate is a special case waiting to be wrong.
    #
    # The law is evaluated at K-RATE from the live `oscNvol` channels, not baked
    # from the authored numbers: mix and register are PLAYER controls (BJ
    # 2026-07-18, the 3-oscillator decision), and a knob that recompiled the
    # orchestra on every move would swap-and-crossfade the whole instrument
    # instead of turning. The authored values are only the STARTING values, and
    # they are seeded into the same channels in the header below, so an orchestra
    # rendered offline (csound_orch_check, the gate) or by a host that never
    # writes the channels sounds exactly as authored.

    # Two oscillators carrying the SAME chain are perfectly coherent, and any
    # normalization then cancels the doubling: (x + x)/2 = x, bit for bit.
    # `cymbal>glass + 2x supersaw>strings>flute` measured peak 0.2696 / rms 0.0501
    # for the pair and 0.2696 / 0.0501 for one of them alone -- the second layer
    # was not merely inaudible, it was arithmetically absent. Duplicates are an
    # ARTEFACT that should not normally reach here at all (the router should not
    # emit them unless the prompt really orders two of the same saw), but when one
    # does, the layers are spread a few cents apart: that is what a doubled layer
    # means musically, the beating between them IS the thickness. The spread is
    # SYMMETRIC about the played note (two at -3.5/+3.5 cents, three at -7/0/+7)
    # so the perceived pitch does not move -- pitch is the synth's. Chains that
    # already differ are decorrelated by their own content and get nothing.
    _DUP_CENTS = 7.0
    # The group key includes the REGISTER: two layers carrying the same chain an
    # octave apart are not coherent at all -- that is the classic 8'+16' organ
    # registration, and (x + x)/2 = x does not apply to it. Grouping on the chain
    # alone detuned the 8' layer 3.5 cents flat of the played note and made the
    # octave impure by 7 cents (adversarial review; reachable straight from a
    # model reply of "OSC1: pwm / OSC2: pwm 16'").
    _groups = {}
    for _oi, _o in enumerate(oscs):
        _groups.setdefault((tuple(_o["chain"]), _o["register"]), []).append(_oi)
    detune = {}
    for _members in _groups.values():
        if len(_members) < 2:
            continue
        _span = (len(_members) - 1) / 2.0
        for _slot, _oi in enumerate(_members):
            detune[_oi] = (_slot - _span) * _DUP_CENTS

    for oi, o in enumerate(oscs):
        body, outv = _emit_oscillator(oi, o["chain"], imorphtime, nmodes)
        n = oi + 1                      # 1-based: the channel names name UI slots
        cents = detune.get(oi)
        # ONE per-layer frequency: the played pitch times the layer's live
        # register, times the duplicate-detune factor when there is one. Every
        # `kfreq` inside the body (including an idiom's own sub-octave weights)
        # is rewritten to it, so a layer transposes as a whole. The same 20..12000
        # limit the shared kfreq carries is re-applied AFTER the multiply --
        # otherwise a 16' layer on a bottom-octave note would run below 20 Hz.
        fv = f"kfr{n}"
        factor = f" * {2 ** (cents / 1200.0):.6f}" if cents else ""
        note = (f"   ; layer {n} register x detune {cents:+.1f} cents" if cents
                else f"   ; layer {n} at its live register")
        body = _re.sub(r"\bkfreq\b", fv, body)
        body = (f"  {fv}    limit kfreq * koct{n}{factor}, 20, 12000{note}\n" + body)
        bodies.append(body)
        mix_terms.append(f"kvol{n} * kmix * {outv}")
    body = "\n".join(bodies)
    # collapse the per-osc outputs into `asig` (what adjectives/motion/tail read),
    # k-rate weighted by the live channels. A single layer at vol 1.0 gives
    # kvol1 = kmix = 1, i.e. the same signal the baked single-osc form produced.
    mix = ("  kvsum    = " + " + ".join(f"kvol{i + 1}" for i in range(len(oscs))) + "\n"
           "  kmix     = 1 / sqrt(kvsum < 1 ? 1 : kvsum)"
           "        ; incoherent layers sum ~sqrt(N)\n"
           "  asig     = " + " + ".join(mix_terms))

    adj = _emit_adjectives(adjective_keys)
    mot = _emit_motion(motion_key, oscs)

    # The authored mix/register values are SEEDED into their channels once, at
    # orchestra init, before any instrument runs. That makes the authored patch
    # the state of the instrument the moment it is compiled: an offline render
    # and a host that never writes the channels both play exactly what the prompt
    # asked for, while the plugin simply overwrites them from its parameters on
    # the very next block. No sentinel value, no "unset" encoding -- and a mix of
    # 0.0 stays what it reads as, silence, instead of doubling as "not set".
    seed = "".join(
        f"chnset {o['vol']:.4f}, \"osc{i + 1}vol\"\n"
        f"chnset {o['register']:.4f}, \"osc{i + 1}oct\"\n"
        for i, o in enumerate(oscs))
    # ...and read back at k-rate inside the instrument.
    reads = "".join(
        f"  kvol{i + 1}    chnget \"osc{i + 1}vol\"\n"
        f"  koct{i + 1}    chnget \"osc{i + 1}oct\"\n"
        for i in range(len(oscs)))

    head = (
        "<CsoundSynthesizer>\n<CsOptions>\n-n -d\n</CsOptions>\n<CsInstruments>\n"
        "sr = %SR%\n"
        f"ksmps = {KSMPS}\n"
        f"nchnls = {NCHNLS}\n"
        "0dbfs = 1\n"
        "giSine ftgen 1, 0, 65536, 10, 1\n"
        # gbuzz reads a COSINE table (its harmonics are cosines). GEN11 with one
        # partial is exactly that; a sine table here would phase-shift every
        # harmonic against the fundamental.
        "giCos  ftgen 2, 0, 65536, 11, 1\n"
        # GEN13: a Chebyshev TRANSFER function. Driven through `tablei` by a sine,
        # it produces exactly the listed harmonic weights -- Csound's own
        # waveshaping idiom, where `cheby` belongs. (The key used to be a bare
        # tanh on a sine, which is a soft clipper, not a Chebyshev shaper: the
        # harmonics it made were whatever tanh happened to produce.)
        # The coefficients after `13, xint, xamp` start at h0 = DC, so the
        # FUNDAMENTAL is the second of them. Getting that off by one put a 1 on DC
        # and a 0 on the fundamental: the key rendered an octave high (measured
        # +1195..+1201 cents at 110/262/523 Hz) out of even harmonics only, and
        # carried a DC offset besides.
        #
        # ODD harmonics only, and that is not a taste decision. h0 = 0 zeroes the
        # DC term only when the drive is exactly full scale; the EVEN terms put DC
        # on every other drive level (measured: this curve with h2/h4 read -0.189
        # at input 0, and the emitted signal carried +0.070 DC = 16% of peak). This
        # branch modulates its drive on purpose, so with even terms the offset does
        # not merely exist, it WANDERS at the LFO rate -- a sub-audio excursion no
        # trim can remove. An odd-only transfer is antisymmetric, f(0) = 0, so it is
        # DC-free at EVERY drive: the shaper can be swept freely. That is also what
        # a symmetric analogue nonlinearity does; the even-harmonic (single-ended)
        # kind carries DC in real circuits too, which is why they need a coupling
        # capacitor. Fixed at the source, per the DC note below -- not filtered off.
        #
        # ALTERNATING SIGNS, which costs nothing and is not cosmetic: T_n(1) = 1 for
        # every n, so with all-positive coefficients every harmonic reaches its
        # maximum together at the input peak and the output is a spike -- measured
        # crest 4.04, i.e. 10 dB of RMS given away to a transient nobody hears as
        # loudness. Alternating them leaves each harmonic's MAGNITUDE untouched
        # (a sign is a phase) while the peak sums to 0.71 instead of 2.01.
        "giCheb ftgen 3, 0, 8193, 13, 1, 1, 0, 1, 0, -0.5, 0, 0.28, 0, -0.15, 0, 0.08\n"
        # NO vco2init here, deliberately. It was added on the theory that the
        # triangle table set is missing on this build and that this was why
        # `triangle` rendered silent through vco2. Both halves were wrong, and the
        # check that showed it was removing the line: the render is BIT-IDENTICAL
        # without it (vco2 generates its sets on demand), and `vco2init 16` does not
        # mean "every waveform" -- it allocates one set, 130 tables, against 650 for
        # all five. The real cause of the historical silence was `vco2 0.6, kfreq, 4`
        # failing with "insufficient required arguments" (imode 4 needs kpw); in
        # this always-on `instr 1` an init error deletes all 16 score instances,
        # which IS the silence. Left as a comment because the wrong explanation is
        # what costs the next reader an evening.\n
        + seed + "\n"
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
        + reads +
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

        # DECAY-INTENT guard: the deterministic layer owns the (pseudo-)envelope, so a
        # routed morph-to-zero is honoured ONLY when the prompt actually asks the sound
        # to fade/decay/stop. Otherwise a trailing terminal-silence is an unmotivated
        # 7B artefact (intermittent, MPS non-determinism) and is stripped below, so a
        # held STANDING sound (pad/drone/bed) never decays on its own.
        wants_decay = _prompt_wants_decay(text)

        # REGISTER-INTENT guard, the same shape one level up: the octave is a
        # player control, so a prompt may only move it when the prompt ITSELF
        # names a register. Without that cue every layer is authored at 8' (the
        # played pitch) no matter what the model wrote, and the plugin then keeps
        # whatever the player had dialled in -- BJ 2026-07-18, asked what happens
        # to hand-set values on a new prompt: "nur wenn der Prompt selbst eine
        # Fußlage nennt". This flag travels to the UI as `register_authored`; it
        # is what tells the plugin whether it may overwrite the octave knobs.
        names_register = _prompt_names_register(text)

        # validate each oscillator's chain independently (morph-chain / compound /
        # pwm-collapse handled per osc), keep its volume; drop an osc that yields
        # no valid key. Cap at 3 (the schema promises up to three).
        oscs, flags = [], []
        for oi, (chain_raw, vol, reg) in enumerate(osc_specs[:3], start=1):
            keys, kflags = _validate_osc_chain(chain_raw, tcanon, dco_llm_map)
            flags += kflags
            # strip an UNMOTIVATED trailing silence (see the decay-intent guard above):
            # keep the rest of the morph so the sound still evolves, it just no longer
            # fades to zero. `_validate_osc_chain` has already collapsed any internal
            # silence, so at most one terminal can be here and >=1 real stage remains.
            # The flag mirrors the {word, reason, tier} shape every other producer in
            # the pipeline emits (dco_llm_map._validate_keys) -- a bare string here
            # would break consumers that do f.get("word") (lco_author.py).
            if not wants_decay:
                while len(keys) >= 2 and keys[-1] in ("silence", "zero"):
                    keys = keys[:-1]
                    flags.append({
                        "word": f"OSC{oi}: {keys[-1]} > silence",
                        "reason": "the prompt does not say the sound fades, so it "
                                  "holds instead of decaying to nothing",
                        "tier": "adapted",
                    })
            # keep only oscillators with real content; an all-silence chain (the
            # 7B emitting "silence" with no source) produces nothing -> drop it so
            # the response's oscillator list matches what build_orchestra renders.
            # Clamp vol to [0,1] here too so the reported metadata equals the mix.
            if keys and not all(k in ("silence", "zero") for k in keys):
                try:
                    v = max(0.0, min(1.0, float(vol)))
                except (TypeError, ValueError):
                    v = 1.0
                r = reg if (names_register and reg) else 1.0
                if reg and reg != 1.0 and not names_register:
                    flags.append({
                        "word": f"OSC{oi}: {_FOOTAGE_LABEL.get(reg, '?')}'",
                        "reason": "the prompt names no register, so the layer plays "
                                  "at the pitch you hold and the octave stays yours",
                        "tier": "adapted",
                    })
                oscs.append({"chain": keys, "vol": v, "register": r})

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

        # MOVEMENT BY DEFAULT -- placed AFTER the honest-failure frame above, so a
        # prompt that matched nothing still fails honestly instead of being rescued
        # into a moving sine. `evolve` is the undirected fallback BJ named: spectrum
        # into its opposite and back (_emit_motion's slow waveshaper sweep at 0.16 Hz,
        # loudness held by `balance`), not a tremolo and not a pitch wobble.
        if (not motion_key or motion_key == "static") \
                and not _prompt_wants_still(text) \
                and not _patch_already_moves(oscs):
            motion_key = "evolve"
            flags.append({
                "word": "motion",
                "reason": "the prompt asks for no particular movement and the sound "
                          "would otherwise stand perfectly still, so it breathes -- "
                          "ask for a static or steady tone to hold it still",
                "tier": "adapted",
            })

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
            # Whether the plugin may overwrite hand-set octave controls with the
            # registers above. Naming a register in WORDS ("an octave below") is
            # not enough on its own: the phrase authorizes but carries no value,
            # so if the model then wrote no footage every layer would be 8' and
            # this flag would order the plugin to RESET the player's octaves to
            # 8' -- the exact opposite of what the prompt asked for. It takes a
            # numeric footage in the prompt, or a register that actually moved.
            "register_authored": bool(
                names_register and (_footage_in(text) is not None
                                    or any(o["register"] != 1.0 for o in rendered))),
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
