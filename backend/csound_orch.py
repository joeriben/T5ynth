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

import math
import re as _re

KSMPS = 64
NCHNLS = 16          # == CsoundEngine::kMaxVoices
HEADROOM = 0.32      # bounds a single idiom's peak; the voice VCA/DCA shapes the rest
DEFAULT_MORPH_SEC = 1.4   # intrinsic morph duration when the prompt gives no speed cue

# Every bare `rand xamp` in Csound uses a 16-bit generator whose sequence REPEATS
# after 2**16 = 65536 samples -- 1.365 s at 48k. Fed into a resonator bank or read
# as broadband noise, the output then replays IDENTICALLY at that period; BJ heard
# it on `glass` as "ca. 1 Sekunde als Schwingungsintervall, dann komplette
# Wiederholung" (2026-07-20). Measured: the stock modal glass has a razor
# autocorrelation spike of +0.83 at exactly 65536 samples. `, 0.5, 1` selects the
# 31-bit generator (period 2**31 ~= 12.4 h), fixed seed 0.5 so the patch stays
# deterministic; the amplitude distribution is UNCHANGED (uniform +-xamp), so every
# downstream level normalisation that assumed white `rand` input stays valid -- the
# reason this is a suffix on `rand`, not a swap to `noise`/`gauss`. Append to any
# FULL-BAND `rand` exciter/source (NOT to `randi`/`randh` at a low kcps: those emit
# few numbers per second, so 65536 of them span minutes, no audible loop).
_RAND31 = ", 0.5, 1"     # 31-bit + fixed seed -> aperiodic within any real note


# ── mode frequencies for the struck/plate resonator bank (ratio, amp) ─────────
# NOT an additive table, and the name no longer invites it to become one. These
# ratios become `mode` resonator centre frequencies driven by a continuous `rand`
# exciter -- a filter bank excited by noise, which is how a struck object is
# modelled, not a sum of sine oscillators.
#
# This dict used to hold 21 entries under the header "representative spectra for
# additive / morph endpoints" and fed the deleted additive morph. Eighteen of
# them had no consumer left the moment that morph went; keeping partial tables
# named after `saw`, `square` and `sine` lying around is how the additive path
# grew back the first time. Only the three the resonator bank actually reads
# survive, and _MODAL_PARAMS lists exactly those same three (_MODAL_TECH also
# carries the parametrised drum_head, which computes its own spectrum).
_MODAL_SPECTRA = {
    # bright inharmonic "glass" sheen.
    "glass":    [(1.00, 1.00), (2.71, 0.62), (3.83, 0.44), (5.17, 0.30), (6.61, 0.20), (8.09, 0.13)],
    # tuned struck metal bar (music box/glocken/celesta/kalimba): ideal free-free
    # bar partials h 2.76/5.4/8.93/13.34, brighter & THINNER than a big bell
    # (weaker fundamental, more high energy).
    "struck_bar": [(1.00, 0.85), (2.76, 0.70), (5.40, 0.50), (8.93, 0.32), (13.34, 0.18)],
    # dense bright inharmonic plate wash (cymbal/crash/ride/hi-hat): weak
    # fundamental, energy piled into dense OFF-GRID upper partials.
    "cymbal":   [(1.00, 0.22), (2.19, 0.38), (3.41, 0.55), (4.83, 0.72), (6.37, 0.88),
                 (8.09, 1.00), (10.24, 0.86), (12.71, 0.64), (15.83, 0.42)],
}

# The tonal technique keys, as a CATALOGUE. This was a dict mapping each key to
# the partial set that represented it inside the additive morph; with that morph
# deleted the values had no reader, but the KEYS are the catalogue every gate
# enumerates ("a key made live in future cannot be forgotten"), so the mapping is
# replaced by the set rather than removed with it.
_TONAL_KEYS = frozenset({
    "sine", "sub_sine", "theremin", "additive",
    "organ", "flute", "harpsichord", "glass",
    "fm_bell", "metallic_fm", "struck_bar", "cymbal", "drum_head",
    "fm", "fm_ep", "ring_mod",
    "saw", "supersaw", "brass", "strings", "bass_saw", "sync",
    "square", "clarinet", "chiptune", "pulse", "triangle", "pwm", "cheby",
    "analog_osc",       # the first PARAMETRISED key (wave/drive/fat/age); see
                        # _emit_analog_osc and dco_lexicon.json's analog_osc.params
    # the STRUCK instruments -- the first keys that bring their own decay, under
    # BJ's `wave` convention (§4). See _emit_struck.
    "rhodes", "wurlitzer", "vibraphone",
    "silence", "zero",                      # morph-to-zero transient terminals
})


# Csound-LOCAL technique keys — real idioms the assembler already knows (they have
# a spectral reading) but that are NOT in the shared
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
    # --- THE INSTRUMENT READING (BJ, 2026-07-20). These three are the first keys
    #     in the library that bring their OWN amplitude decay, and they exist
    #     because BJ released them: see the `wave` convention in §4 of
    #     docs/LCO_CONCEPT.md. A struck instrument that fades is not fighting the
    #     player's envelope here -- it IS the instrument, and the spectrum-source
    #     reading of the same sound stays reachable by writing `wave` after it.
    #     All three re-strike on the trig epoch, so a new note is a new strike.
    #     Measured 2026-07-20 at --format=float, kamp 0.3, 0dbfs=1 (see
    #     tools/csound_model_probe.py and §6): dead in tune (+-0 cents) over the
    #     whole 55..1760 Hz sweep, unlike almost everything else in that list.
    "rhodes": {
        "why": "a real Rhodes tine piano that RINGS AND FADES on its own "
               "(~0.6 s half-life) -- bell-like metallic attack over a woody "
               "body; the note dies whether or not the key is held. Write "
               "\"epiano wave\" instead for the standing-tone version.",
        # "piano" and "elektrisches piano" are here because fm_ep held them before
        # the convention and nothing else claims them -- an acoustic piano key
        # does not exist, so the electric one is where they have always landed.
        # Verified by diffing the whole canon before/after: no word that routed
        # somewhere now routes nowhere.
        "surface_forms": ["rhodes", "fender rhodes", "electric piano", "epiano",
                          "e-piano", "tine piano", "e piano", "elektrisches klavier",
                          "piano", "elektrisches piano"],
    },
    "wurlitzer": {
        "why": "a real Wurlitzer reed piano that RINGS AND FADES on its own "
               "(~0.7 s half-life) -- reedier and barkier than a Rhodes, its "
               "brightness sitting on the odd harmonics rather than off the comb.",
        "surface_forms": ["wurlitzer", "wurli", "reed piano", "wurlitzer piano"],
    },
    "vibraphone": {
        "why": "a real struck metal bar that RINGS AND FADES on its own "
               "(~0.3 s half-life) -- soft mallet on aluminium, sweet and round "
               "rather than clangorous.",
        "surface_forms": ["vibraphone", "vibes", "vibraharp", "metallophone",
                          "vibraphon", "metallophon"],
    },
}

# The other half of BJ's `wave` convention, applied to the CSOUND PATH ONLY.
#
# BJ, 2026-07-19: „taiko drum wave" = ohne env, „taiko drum" = das echte Csound
# Instrument -- and, hearing instrument 2 the next day, „das wäre schon ein
# kandidat für ‚epiano wave'". So the BARE words name the real instrument, and
# the spectrum-source reading keeps them only with `wave` appended.
#
# Which means the keys that already own those bare words have to give them up.
# `fm_ep` answers to "epiano"/"electric piano"/"piano"/"rhodes" today; left
# alone, the new `rhodes` key would be unreachable and the convention would be
# decoration. So its surface forms are REWRITTEN here, in the shallow per-request
# copy of the lexicon that build_csound_response already builds -- NOT in
# backend/dco_lexicon.json, which lco_author.py also reads and which has no
# `fmrhode` to disambiguate against. The shared lexicon stays untouched.
#
# NOTHING IS DELETED. fm_ep keeps its key, its parameters, its emitter and its
# ear-approval; only the words that reach it change, and every one of them stays
# reachable with `wave`. Confirmed by BJ 2026-07-20 as a deliberate behaviour
# change to an approved instrument, not a side effect.
# THE WAVE FORMS ARE DERIVED, NEVER HAND-WRITTEN. A hand-written replacement list
# was the first attempt and it silently destroyed capability: of `fm_ep`'s 14
# surface forms it re-listed 7, so "piano" and "elektrisches piano" routed
# NOWHERE, and -- worse, because the fallback matches a sub-phrase and raises no
# flag -- "dx piano" rendered a BELL (`dx` belongs to fm_bell) and "fm piano"
# collapsed onto the bare `fm` key. Deriving `f + " wave"` from the key's OWN
# current list makes that class of loss impossible and keeps working when the
# shared lexicon changes underneath.
#
# Two different operations, and the difference is the whole rule:
#
#   _CS_WAVE_HANDOVER -- the key GIVES UP its bare words to a real instrument
#       that exists, and answers only to "<form> wave" plus whatever bare forms
#       are listed here as deliberately kept.
#   _CS_WAVE_ALSO -- the key KEEPS every bare word AND additionally answers to
#       "<form> wave". For keys that are the `wave` reading but have no
#       instrument counterpart built yet.
#
# A KEY ONLY GIVES UP A WORD TO A KEY THAT EXISTS. `drum_head` is in the second
# table, not the first, even though BJ's convention was first stated about a drum
# ("taiko drum wave"): no struck drum key is built, so stripping "taiko"/"tom"/
# "timpani" would not hand them to an instrument reading -- it would make them
# unroutable and delete a working sound. It gains the `wave` forms so BJ's own
# originating example routes, and loses nothing. Move it to the handover table on
# the day a struck drum lands, and not before.
_CS_WAVE_HANDOVER = {
    # `fm_ep` hands the plain electric-piano words to `rhodes`/`wurlitzer` and
    # KEEPS the ones that already name the SYNTHETIC version by construction --
    # "fm piano" and "dx piano" are asking for FM, which is exactly what fm_ep
    # is. Those would be actively wrong on a physical-model Rhodes.
    "fm_ep": ["fm piano", "fm-piano", "fm e-piano", "dx piano",
              "fm electric piano", "dx electric piano"],
}
_CS_WAVE_ALSO = {"drum_head"}


def _wave_forms(entry):
    """A lexicon technique entry -> its surface_forms under the `wave` convention.

    Derived from the entry's own list so nothing can be dropped by hand. Returns
    the list unchanged for a key the convention does not touch."""
    key = entry.get("key")
    forms = list(entry.get("surface_forms", []))
    # Both spellings of every hyphen/space pair, because _canon matches the
    # literal string and does not normalise: without this, "e piano" survives as
    # "e-piano wave" only, and "e piano wave" routes nowhere.
    variants = []
    for f in forms:
        variants.append(f)
        if "-" in f:
            variants.append(f.replace("-", " "))
        elif " " in f:
            variants.append(f.replace(" ", "-"))
    waves = [f"{v} wave" for v in variants]
    if key in _CS_WAVE_ALSO:
        return forms + waves
    if key in _CS_WAVE_HANDOVER:
        return waves + _CS_WAVE_HANDOVER[key]
    return forms


# The catalogue the model actually reads prints ONLY "<key>: <why>" --
# dco_llm_map._build_catalogue never emits surface_forms. So a convention that
# lives purely in surface_forms is invisible to the layer that has to apply it:
# the forms decide whether the model's ANSWER is accepted, the `why` decides what
# it PICKS. fm_ep's shared `why` still opens "electric piano / rhodes /
# wurlitzer / dx piano", three of which now belong to other keys. Rewritten here,
# on the same per-request copy, for the same reason.
_CS_WHY_OVERRIDE = {
    "fm_ep": ("the STANDING-TONE electric piano, asked for as \"epiano wave\" or "
              "\"rhodes wave\" -- a struck tine body with a metallic attack that "
              "settles into a tone and then HOLDS for as long as the note is "
              "held. Also the right key for \"fm piano\" and \"dx piano\", which "
              "name the synthetic version outright. For an electric piano that "
              "rings and fades by itself, use `rhodes` or `wurlitzer` instead."),
    "drum_head": ("a struck drum head that KEEPS SOUNDING while the note is held "
                  "-- tom, timpani, taiko, frame drum. Also answers to \"drum "
                  "wave\", \"taiko drum wave\" and so on."),
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

# Output gain per vowel, MEASURED, not derived. The three vowels differ by 9 dB
# through the same resonator bank even though their formant amplitudes sum to
# nearly the same number (1.92 / 1.85 / 1.72): what actually sets the level is
# where F1 sits relative to the glottal tilt and how narrow the bands are. 'ee'
# has F1 at 270 Hz, barely attenuated by the source pole and only 50 Hz wide, so
# it comes out loudest by far. One shared constant left 'ee' 14 dB above a sine
# and 'ah' 5 dB above. Each vowel is scaled to land ~2 dB under a sine.
_VOWEL_GAIN = {"voice": 10.7, "voice_ee": 4.35, "voice_oo": 5.19}

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
# for these physical objects. The ratio/amp sets stay in _MODAL_SPECTRA (single source,
# lexicon-honoured); _MODAL_PARAMS adds what a resonator needs beyond a partial
# list: excitation level, master gain (empirically calibrated, see
# tools/csound_keys_gate.py peaks), and a Q range (first->last mode).
# The exciter is CONTINUOUS, so a held note stands (platform fundamental); the
# stochastic drive gives the metal live micro-shimmer a static bank cannot have.
# drum_head is here for the CPU budget only -- it has no _MODAL_SPECTRA /
# _MODAL_PARAMS entry, because its spectrum comes from its four controls.
# _emit_steady dispatches it before this set is ever tested.
_MODAL_TECH = {"struck_bar", "cymbal", "glass", "drum_head"}
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
              "flute",
              # The ten keys that stopped being tables and became real idioms had
              # to be added here too, and were not. Standalone they measure
              # 13.9-52.7% spectral-centroid travel; inside `a > b` all six of
              # these emitted nothing but `oscili` -- the additive bank this
              # project forbids -- because being absent from this set is exactly
              # what routes a chain into the partial-bank path. What each one
              # lost is its whole reason for existing:
              #   organ       three gbuzz ranks whose independent wander IS the
              #               difference between a section and one pipe
              #   additive    two detuned gbuzz twins; the BEATING is the key
              #   harpsichord two choirs at 8' and 4', likewise
              #   clarinet    a foscili bore opened by breath pressure
              #   cheby       a driven waveshaper; the drive travel is the timbre
              #   ring_mod    a genuine PRODUCT plus a drifting carrier leak,
              #               which no bank of partials can express at all
              # tools/csound_morph_liveness_gate.py measures this rather than
              # trusting the list, so the next key made live cannot be forgotten.
              "organ", "additive", "harpsichord", "clarinet", "cheby",
              "ring_mod",
              # The struck-metal FM keys, once their index started falling as the
              # bell rings and their doublets started beating. A static partial
              # bank cannot carry either. tools/csound_morph_liveness_gate.py
              # caught all three the moment they became live -- which is exactly
              # what it is for, and what the hand-maintained list failed at.
              "fm", "fm_bell", "metallic_fm"}

# NOTE: a set of sub-fundamental spectra used to live here, whose ONLY job was
# to steer such chains around the additive morph. With that morph deleted every
# chain renders its real idiom, so the exemption has nothing left to except.

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
# SUSTAIN-INTENT guard, the mirror of the decay guard for the struck instruments.
#
# The words the stillness set above deliberately excludes -- pad, drone, held --
# because they name DURATION rather than stillness. Duration is exactly what
# matters here: `rhodes`, `wurlitzer` and `vibraphone` ring and die on their own,
# so "a warm electric piano PAD" routed to `rhodes` is a pad that is silent after
# 1.5 seconds (measured: a held note's RMS is 0.0000 from 3 s on). The prompt asked
# for a sound that stands; the convention says a bare instrument name means the one
# that fades; the two collide, and the prompt wins, because the deterministic layer
# owns the envelope -- the same principle the decay guard already applies in the
# opposite direction.
#
# Narrow on purpose. A wrong match here silently denies the user the real
# instrument they asked for, which is the more expensive error.
_SUSTAIN_CUE_WORDS = {
    "pad", "pads", "drone", "drones", "bed", "beds", "wash", "sustained",
    "sustain", "sustaining", "held", "holding", "continuous", "endless",
    "ambient", "atmosphere", "atmospheric", "soundscape",
    # German, for the same reason the stillness set carries it: an English-only
    # cue set makes a German prompt unable to reach its own guard.
    "fläche", "flächen", "flaeche", "teppich", "dauerton", "gehalten",
    "liegeton", "liegetöne", "schwebend",
}
# Where a struck key goes when the prompt asked for a standing sound. Each target
# is the SAME sound family with a continuous excitation, i.e. exactly the `wave`
# reading of the convention -- not a downgrade to a generic tone.
_STRUCK_SUSTAIN_COUNTERPART = {
    "rhodes": "fm_ep",        # the standing-tone electric piano this key came from
    "wurlitzer": "fm_ep",
    "vibraphone": "struck_bar",   # a continuously excited metal-bar modal bank
}


def _prompt_wants_sustain(text):
    """True iff the prompt itself names a sound that must STAND -- pad, drone,
    held. Only then is a self-decaying instrument overridden."""
    low = (text or "").lower()
    return any(w in _SUSTAIN_CUE_WORDS for w in _re.findall(r"[a-zäöüß]+", low))


_STILL_CUE_PHRASES = (
    "no movement", "without movement", "no motion", "without motion",
    "does not move", "doesn't move", "do not move", "never moves",
    "perfectly still", "dead still", "stands still", "standing still",
    "holds steady", "keine bewegung", "ohne bewegung", "bewegt sich nicht",
)
# Idioms that already carry their own temporal change, so the default would be
# piling motion on motion. `pwm` is here on MEASUREMENT, not on theory: in the built
# Standalone its even/odd partial ratio travels 0.26 -> 0.76 and pulses at ~0.6 Hz.
#
# The noise family used to be justified as "stochastic by construction (rand /
# randh drive it)". That conflates STOCHASTIC with NON-STATIONARY, and they are
# not the same thing: white noise has a random waveform but its spectrum and its
# loudness do not change at all over time, which is exactly as static as a held
# sine. Measured on the beds as they were, RMS variation over 22 s: wind 0.016,
# rain 0.001, surf 0.005, thunder 0.007 -- i.e. frozen, while sitting in the set
# that tells the movement guard to stand down. A prompt asking for wind that
# builds got a stationary hiss and nothing downstream could see it.
#
# Those four (and crackle) now really do move, from their own physics -- gusts,
# drops, swell, roll -- so their membership here is now true: wind 0.274, rain
# 0.162, surf 0.580, thunder 0.286, crackle 0.204.
#
# STILL NOT TRUE for `noise`, `pink_noise` and `hiss`, deliberately: white noise
# and tape hiss ARE stationary processes, that is what the words mean, and giving
# them a swell would be inventing a phenomenon rather than modelling one. They
# stay in this set, so a prompt cannot currently make them evolve. Whether those
# three should instead leave the set and let the motion layer move them is a
# routing question with an audible answer, so it is BJ's call, not a silent edit.
# fm_ep belongs here since it became the parametrised electric piano: its
# identity IS a struck transient, a metallic attack decaying out of the tone
# over a measured 86-770ms, on top of the body index softening it always had.
# A patch built on it already travels, so the movement-by-default guard must
# not layer a second motion on top of a strike.
# The struck instruments belong here for the same reason and more strongly: their
# identity IS a strike decaying away, a trajectory the model owns end to end. A
# second motion layer on top would modulate a sound that is already leaving.
_STRUCK_TECH = {"rhodes", "wurlitzer", "vibraphone"}
_SELF_MOVING_TECH = {"pwm", "fm_ep"} | _NOISE_TECH | _STRUCK_TECH


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
    r"(?<![\d/])\b(32|16|8|4|2)\s*(?:'|’|\"|”|ft\b|foot\b|feet\b)(?![A-Za-z])",
    _re.IGNORECASE)
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
_FOOTAGE_DE_RE = _re.compile(r"(?<![\d/])\b(32|16|8|4|2)\s*(?:fuß|fuss)\b",
                             _re.IGNORECASE)


def _all_footages(text):
    """EVERY real footage in `text`, in order, as pitch multipliers. English feet
    first (the order they appear), then any German ones -- which is what makes
    `_footage_in` below return the first English footage and fall back to German
    only when there is none, exactly as it always has.

    One function rather than three open-coded scans, because the three had to
    agree and did not: the strip in `_parse_csound_reply` ran the case-SENSITIVE
    pattern against the original-case line while detection ran it against a
    lowercased copy, so "saw 16 FT > glass" detected a register and then left the
    token sitting in the chain text. Both patterns carry IGNORECASE now, so the
    scan that finds a footage and the scan that removes it cannot diverge."""
    low = (text or "").lower()
    out = []
    for m in _FOOTAGE_RE.finditer(low):
        tail = low[m.end():m.end() + 12].strip()
        if any(tail.startswith(w) for w in _NOT_FOOTAGE_NEXT):
            continue                     # `2" tape hiss` names tape, not a stop
        out.append(_FOOTAGE_MULT[m.group(1)])
    out += [_FOOTAGE_MULT[m.group(1)] for m in _FOOTAGE_DE_RE.finditer(low)]
    return out


def _footage_in(text):
    """The pitch multiplier named by the FIRST real footage in `text`, or None.
    Shared by the prompt-authority scan and the OSC-line strip so the two can
    never disagree about what counts as a footage."""
    feet = _all_footages(text)
    return feet[0] if feet else None


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


# ── analog_osc: the first PARAMETRISED technique key ─────────────────────────
# Every other technique is a fixed idiom; analog_osc is a real analogue VCO
# with four NAMED 0..1 parameters (wave/drive/fat/age) the routing LLM sets
# directly, in a reply like `analog_osc(wave=saw, drive=hot)`. The anchor
# tables below are the SINGLE source of truth for (a) resolving an anchor WORD
# to its number when parsing the LLM's reply, (b) the DSP in _emit_analog_osc,
# and (c) the nearest-anchor label in _reading -- mirrored BY HAND into
# dco_lexicon.json's analog_osc.params (which additionally carries the
# per-anchor GLOSS text the routing LLM reads; this file has no lexicon
# access, matching every other technique's Python-side constants, e.g.
# _MODAL_SPECTRA/_VOWEL_FORMANTS restate in Python what the lexicon's "why"
# describes in prose). Keep the two in sync by hand if either changes.
_AOSC_ANCHORS = {
    "wave":  {"triangle": 0.0, "saw": 0.45, "square": 0.55, "pulse": 1.0},
    "drive": {"clean": 0.0, "warm": 0.3, "hot": 0.6, "screaming": 1.0},
    "fat":   {"single": 0.0, "subtle": 0.15, "thick": 0.5, "wide": 1.0},
    "age":   {"new": 0.0, "worn": 0.35, "old": 0.8},
}
_AOSC_DEFAULTS = {"wave": 0.45, "drive": 0.0, "fat": 0.0, "age": 0.35}

# fm_ep: the SECOND parametrised key. Same contract as _AOSC_ANCHORS above --
# these tables resolve an anchor WORD to its number, drive _emit_fm_ep's DSP,
# and label the UI reading, and they are mirrored BY HAND into
# dco_lexicon.json's fm_ep.params (which carries the per-anchor gloss text).
_FMEP_ANCHORS = {
    "ting":   {"none": 0.0, "soft": 0.3, "classic": 0.55, "clangy": 1.0},
    "ring":   {"short": 0.0, "medium": 0.45, "long": 1.0},
    "reed":   {"full": 0.0, "hollow": 0.45, "tine": 0.75, "reed": 1.0},
    "strike": {"soft": 0.0, "normal": 0.64, "hard": 1.0},
}
# strike 0.64 -> body index 4.20 and ring 0.25 -> a short tine: these defaults
# reproduce the pre-parametrisation fm_ep's own starting index exactly, so a
# prompt that sets nothing gets the key it always got, plus the tine attack it
# never had. See _emit_fm_ep.
_FMEP_DEFAULTS = {"ting": 0.55, "ring": 0.25, "reed": 0.75, "strike": 0.64}

# Registry of every technique key that takes parameters, keyed by its
# CANONICAL key -> {param: {anchor: value}}. A future parametrised
# instrument just adds its own entry here (and its own _emit_xxx / dispatch
# line) -- _extract_osc_params below is generic over this registry.
# drum_head: the THIRD parametrised key. Same contract again; see _emit_drum_head.
_DRUM_ANCHORS = {
    "pitched": {"tom": 0.0, "mixed": 0.5, "timpani": 1.0},
    "spot":    {"centre": 0.0, "halfway": 0.5, "rim": 1.0},
    "tension": {"slack": 0.0, "normal": 0.5, "tight": 1.0},
    "damping": {"open": 0.0, "damped": 0.5, "muffled": 1.0},
}
_DRUM_DEFAULTS = {"pitched": 0.25, "spot": 0.35, "tension": 0.5, "damping": 0.35}

# Registry of every technique key that takes parameters, keyed by its
# CANONICAL key -> {param: {anchor: value}}. A future parametrised
# instrument just adds its own entry here (and its own _emit_xxx / dispatch
# line) -- _extract_osc_params below is generic over this registry.
_PARAM_SCHEMAS = {"analog_osc": _AOSC_ANCHORS, "fm_ep": _FMEP_ANCHORS,
                  "drum_head": _DRUM_ANCHORS}


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
# Appended to the system prompt ONLY on a correction pass (see
# build_csound_response's `correction`). Kept out of the normal head so the first
# authoring is byte-identical to what it has always been: the model must not read
# a rule about repairing a previous attempt when there is no previous attempt.
_CS_CORRECTION_RULE = (
    "\nTHIS IS A SECOND ATTEMPT. Two extra lines follow the request. YOU WROTE is "
    "the patch you produced last time. A LISTENER HEARD is how a listener "
    "described that patch and which quality of the request it missed.\n"
    "Start from YOU WROTE and repair it. The listener's word says what the patch "
    "CAME OUT AS; it is not a word to delete but a direction to move AWAY from. "
    "Keep the oscillators and the adjectives the listener did not name, and add "
    "or exchange keys that pull the opposite way. Deleting adjectives is not a "
    "repair -- it removes what was already right and usually makes the miss "
    "worse. Your answer must be a COMPLETE patch, not a diff.\n"
    "Example: request \"a soft wooden flute\", YOU WROTE \"flute 8' - woody, "
    "bright\", A LISTENER HEARD \"asked for soft, but the sound is described as "
    "harsh\" -> keep flute, keep woody, drop bright because it pulls the wrong "
    "way, add mellow.\n"
    "The original request still stands -- never drop it, and never answer the "
    "listener instead of the request. Reply in EXACTLY the same format as "
    "always: the OSC/VOL/ADJECTIVES/MOTION lines and nothing else.\n"
)

_CS_SYSTEM_PROMPT_HEAD = (
    "You translate a sound description into a small synthesizer patch of up to "
    "THREE oscillators, choosing ONLY keys from the fixed catalogue below. You "
    "never invent names or numbers.\n"
    "Reply in EXACTLY this format and nothing else (omit OSC2/OSC3 lines if not "
    "needed):\n"
    "OSC1: <key> [> <key> ...] [<32|16|8|4|2>']\n"
    "VOL1: <number 0.0-1.0>\n"
    "OSC2: <key> [> <key> ...] [<32|16|8|4|2>']\n"
    "VOL2: <number 0.0-1.0>\n"
    "OSC3: <key> [> <key> ...] [<32|16|8|4|2>']\n"
    "VOL3: <number 0.0-1.0>\n"
    "ADJECTIVES: <key>, <key>, ...\n"
    "MOTION: <key>\n"
    "Rules: use ONE oscillator for a simple sound; add OSC2/OSC3 only to LAYER or "
    "detune (e.g. a fat saw stacked with a sub, or a bright bell over a warm pad). "
    "Each OSC line is that layer's waveform/method; if it MORPHS from one sound "
    "into another, list them in order separated by \" > \" (e.g. glass > sine). "
    "VOLn is that layer's loudness (main layer 1.0, supporting layers less). "
    "A layer may carry ONE organ register in feet, written at the end of its OSC "
    "line -- 8' is the played pitch, and every halving of the number is an octave "
    "UP: 32' two octaves lower, 16' one lower, 8' the played pitch, 4' one higher, "
    "2' two higher (e.g. \"OSC2: sine 16'\"). Those five are the only legal "
    "values, and the register applies to the WHOLE layer including every stage of "
    "its morph. Add one ONLY if the prompt asks for a register; otherwise write no "
    "feet at all. If the prompt puts feet on several stages of one chain, keep the "
    "FIRST and write it once at the end of the line. "
    "ADJECTIVES are timbral modifiers for the whole sound (or \"none\"). This "
    "INCLUDES words for the kind of circuit or the age of the gear -- analog, "
    "vintage, old -- which describe how the tone behaves and are catalogue "
    "adjectives like any other: if the prompt says analog, ADJECTIVES must "
    "contain analog. MOTION is "
    "how the whole sound moves over time (or \"none\"). Match by MEANING even if "
    "the wording differs. Use ONLY keys from the catalogue; if nothing in a "
    "category fits, write \"none\". Every descriptive word in the prompt should "
    "reach one of these lines if the catalogue has a key for it.\n"
    "NOTATION: the prompt's own punctuation is binding. \"a > b\" is ONE "
    "oscillator morphing a into b — put both on the SAME OSC line separated by "
    "\" > \", never on two lines. \"a + b\" is two oscillators on two lines. "
    "COUNT THE \" > \" MARKS: \"saw > sine > square\" has two marks, so it is the "
    "single line \"OSC1: saw > sine > square\", NOT three oscillators — and feet "
    "attached to a stage do not change that count, so \"saw 16' > glass 2'\" is "
    "also ONE oscillator, NOT two layers.\n"
    "ONLY \" > \" MAKES A MORPH. Two describing words side by side are ONE sound "
    "and take ONE key: \"pwm saw\" is a single pulse-width-modulated waveform, so "
    "write \"pwm\", NEVER \"pwm > saw\". Apart from the silence rule below, never "
    "insert a \" > \" the prompt did not write.\n"
    "THE WORD \"wave\" IS NOT A DESCRIBING WORD — it picks between two DIFFERENT "
    "catalogue keys for the same instrument. Written on its own after an "
    "instrument (\"epiano wave\", \"rhodes wave\") it asks for the STANDING-TONE "
    "version, which holds as long as the note is held. Left off (\"epiano\", "
    "\"rhodes\") it asks for the REAL INSTRUMENT, which rings and fades by itself "
    "like a struck string or bar. They are two SEPARATE catalogue keys, so read "
    "both entries and pick the one whose description matches: \"epiano\" is the "
    "key `rhodes`, \"epiano wave\" is the key `fm_ep`. This does NOT apply to "
    "\"saw wave\", \"square wave\", \"sine wave\" and the like, where \"wave\" is "
    "simply part of the waveform's ordinary name.\n"
    "A KEY THAT ALREADY FADES BY ITSELF NEEDS NO SILENCE. `rhodes`, `wurlitzer` "
    "and `vibraphone` ring and die on their own, so never append \" > silence\" to "
    "them — that would cut the sound off twice.\n"
    "PARAMS: a few catalogue keys take named parameters right after the key, "
    "in parentheses: key(name=value, name=value). See that key's own "
    "\"params:\" line in the catalogue for its parameter names and anchor "
    "words. A value is an anchor WORD (preferred) or a bare number 0-1 to sit "
    "between two anchors. Omit a parameter you have no cue for — it keeps its "
    "own default.\n"
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
    "Example — two layers at named registers, and EVERY descriptive word carried "
    "(both adjectives listed; no loudness was asked for, so both layers stay 1.0):\n"
    "OSC1: saw 16'\n"
    "VOL1: 1.0\n"
    "OSC2: sine 4'\n"
    "VOL2: 1.0\n"
    "ADJECTIVES: analog, warm\n"
    "MOTION: none\n"
    "Example — the prompt \"gentle saw > flute 4' > epiano wave\": two \" > \" "
    "marks, so ONE oscillator with three stages on ONE line, its one register "
    "moved to the end where it belongs, and \"epiano wave\" resolving to the "
    "standing-tone key rather than the struck instrument (bare \"epiano\" here "
    "would be `rhodes`):\n"
    "OSC1: saw > flute > fm_ep 4'\n"
    "VOL1: 1.0\n"
    "ADJECTIVES: gentle\n"
    "MOTION: none\n"
    "Example — a named parametrised instrument, driven hard:\n"
    "OSC1: analog_osc(wave=saw, drive=hot, fat=thick, age=worn)\n"
    "VOL1: 1.0\n"
    "ADJECTIVES: none\n"
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
    osc_chains, osc_vols, osc_octs, osc_lost_regs = {}, {}, {}, {}
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
                #
                # That drop USED to be described here as "visible by absence,
                # because the reading card names the register that won". It is
                # not: adversarial review rendered the case and found flags empty
                # and the card reading "saw > sine 16'" -- byte-identical to what
                # a faithful single-register layer produces. Nothing whatsoever
                # distinguished "you asked for one register" from "you asked for
                # two and we threw one away", and which one survives is decided by
                # STAGE ORDER, not intent ("glass 2' > saw 16'" puts the whole
                # chain at 2'). So the loss is reported now, and the count is
                # taken BEFORE stripping because after it there is nothing to see.
                _feet = _all_footages(chain)
                if len(set(_feet)) > 1:
                    osc_lost_regs[idx] = [f"{_FOOTAGE_LABEL[f]}'" for f in _feet]
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
    oscs, lost_regs = [], []
    for idx in sorted(osc_chains):
        chain = osc_chains[idx]
        if not chain or chain.lower() == "none":
            continue
        oscs.append((chain, osc_vols.get(idx, 1.0), osc_octs.get(idx)))
        # collected per SURVIVING layer, in the same order as `oscs`, so a caller
        # can name the layer without re-deriving which OSCn lines were dropped
        lost_regs.append(osc_lost_regs.get(idx))
    return oscs, adjectives_raw, motion_raw, lost_regs


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


# Parenthesized parameter syntax on an OSC chain stage: KEY(name=value,
# name=value). Anchor words are what a small model produces reliably; a bare
# number 0..1 lets it interpolate between anchors. Extraction runs BEFORE
# _validate_keys ever sees the text, so the existing surface-form/fallback
# machinery keeps matching plain key text exactly as it always has -- a chain
# with no parens (every existing key, always) is untouched byte-for-byte.
#
# The key pattern MUST admit spaces and hyphens, because most keys are reached
# by a MULTI-WORD surface form and the model writes the words it was given.
# With a `\w+`-only pattern `analog oscillator(drive=hot)` captured just
# "oscillator", the schema lookup missed, and every parameter was silently
# dropped while the key itself still validated -- a setting that vanishes with
# no flag anywhere. Measured on the shipped analog_osc: `analog oscillator`,
# `analog osc` and `analog synth` all lost their parameters, and `fm_ep`
# escaped only by luck, because all fourteen of its multi-word forms end in
# "piano" and "piano" is itself a registered form. `>` and `,` stay out of the
# class so a chain or a compound list can never be swallowed into one key.
_OSC_PARAM_RE = _re.compile(r'([a-zA-Z][a-zA-Z0-9_ \-]*?)\s*\(([^)]*)\)')


def _parse_param_value(raw, anchors):
    """One raw param VALUE ("saw", "0.62", " Hot ") -> a float 0..1, or None if
    it names neither a real anchor nor an in-range number. FORGIVING BY
    DESIGN: the caller drops a None and keeps the parameter's own default --
    never drops the key, never fails the whole reply."""
    s = (raw or "").strip().lower()
    if not s:
        return None
    if s in anchors:
        return anchors[s]
    try:
        v = float(s)
    except ValueError:
        return None
    return v if 0.0 <= v <= 1.0 else None


def _extract_osc_params(chain_raw, tcanon):
    """chain_raw ("analog_osc(wave=saw, drive=hot) > sine") -> (stripped_chain,
    {canonical_key: {param: value}}). The raw key text in front of each "(...)"
    is canonicalized through the SAME tcanon map _validate_keys itself uses (so
    a surface form like "minimoog(drive=hot)" still lands its params on
    "analog_osc"), then looked up in _PARAM_SCHEMAS; a key with no params
    schema (i.e. every technique but analog_osc today) has its parenthetical
    silently dropped -- the bare key text survives and validates normally. An
    unknown parameter NAME or an unresolvable VALUE is dropped individually
    (via _parse_param_value's None), never the whole parenthetical.

    Every "(...)" is stripped here regardless of outcome, so _validate_keys
    downstream always sees plain key text -- this is a PRE-PASS, not a
    replacement for any existing validation."""
    found = {}

    def _sub(m):
        key_raw = m.group(1).strip().lower()
        canon_key = tcanon.get(key_raw, key_raw)
        schema = _PARAM_SCHEMAS.get(canon_key)
        if schema:
            parsed = {}
            for piece in m.group(2).split(","):
                if "=" not in piece:
                    continue
                name, val = piece.split("=", 1)
                name = name.strip().lower()
                anchors = schema.get(name)
                if anchors is None:
                    continue                       # unknown param name -> dropped
                v = _parse_param_value(val, anchors)
                if v is not None:
                    parsed[name] = v               # else: dropped, default stands
            if parsed:
                found[canon_key] = parsed
        return key_raw

    stripped = _OSC_PARAM_RE.sub(_sub, chain_raw)
    return stripped, found


def _validate_osc_chain(chain_raw, tcanon, dco_llm_map):
    """One OSC line's raw chain string -> (validated technique keys, flags,
    params). ">" = morph chain (keep every valid stage, ordered); a bare comma
    list is one compound technique -> first valid key. Enforces terminal-only
    silence and the pwm/pulse-family collapse. `params` is
    {canonical_key: {name: value}}, filtered to the keys that actually
    survived validation/collapse -- a stage the guard drops takes its
    parameters with it, never the reverse."""
    chain_raw, stage_params = _extract_osc_params(chain_raw, tcanon)
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
    params = {k: stage_params[k] for k in keys if k in stage_params}
    return keys, flags, params


def _param_anchor_label(technique_key, name, value):
    """Nearest anchor WORD for a resolved param value (reading/label only --
    the DSP always uses the plain float). e.g. 0.62 on analog_osc's "drive"
    -> "hot" (closest to 0.6). Generic over _PARAM_SCHEMAS, not
    analog_osc-specific, so a future second parametrised key needs no change
    here."""
    anchors = _PARAM_SCHEMAS.get(technique_key, {}).get(name, {})
    value = float(value)
    if not anchors:
        return f"{value:g}"
    word, _ = min(anchors.items(), key=lambda kv: abs(kv[1] - value))
    return word


def _param_is_applied(technique_key, name, value):
    """True iff the emitters' own resolvers would KEEP this setting rather than
    silently fall back to the parameter's default -- a KNOWN name for this key
    carrying a number within 0..1.

    `_resolve_analog_osc_params` and `_resolve_fm_ep_params` both re-validate
    defensively, because a direct caller of build_orchestra can hand them
    anything, and they drop an unknown name (`if k not in out: continue`) and
    an out-of-range value alike. The READING has to apply exactly the same two
    tests or the two disagree, and every way they disagree is a lie in the UI:
    an out-of-range value would be labelled with its nearest anchor while the
    oscillator quietly used the default, an unknown name would be printed as a
    bare number for a control that does not exist, and a non-numeric value used
    to crash the card outright. The reading is what the user is told the
    machine understood -- it must show what was applied and nothing else."""
    if name not in _PARAM_SCHEMAS.get(technique_key, {}):
        return False
    try:
        return 0.0 <= float(value) <= 1.0
    except (TypeError, ValueError):
        return False


def _reading(oscs, adjective_keys, motion_key):
    """Short human-readable interpretation for the UI card. Multi-osc: each layer
    is 'a > b' (morph) or 'a'; layers joined with ' + '. e.g.
    'saw + sub_sine · warm ~evolve' or 'glass > sine · glassy'. A parametrised
    key (analog_osc, fm_ep) that the LLM actually SET parameters on shows them
    compactly as anchor words, e.g. 'analog_osc (saw, hot, thick)' -- a bare,
    all-default key shows no parameter suffix at all."""
    def osc_str(o):
        chain = o["chain"]
        s = " > ".join(chain) if len(chain) >= 2 else (chain[0] if chain else "sine")
        # name the register only when it is NOT the played pitch: "8'" on every
        # layer would be noise in the card, a "16'" is the thing worth reading.
        reg = o.get("register", 1.0)
        if reg != 1.0:
            s += " " + _FOOTAGE_LABEL.get(reg, "8") + "'"
        params = o.get("params") or {}
        for key in chain:
            pset = params.get(key)
            # isinstance, not truthiness: `_param_is_applied` hardens the
            # VALUES but nothing upstream guarantees the SHAPE, and a direct
            # caller of build_orchestra can pass a list or a string here. The
            # resolvers have the same guard for the same reason.
            if isinstance(pset, dict) and pset:
                # dict insertion order == the order the LLM wrote them, which
                # is already a sensible display order (no re-sort needed).
                # Only values the emitter will actually APPLY are shown -- see
                # _param_is_applied for why the two must not diverge.
                labels = [_param_anchor_label(key, n, v) for n, v in pset.items()
                          if _param_is_applied(key, n, v)]
                if labels:
                    s += " (" + ", ".join(labels) + ")"
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
    L = [f"  {nz}    rand 1.0{_RAND31}         ; broadband noise source (31-bit: no 1.365 s loop)"]
    if technique == "noise":
        L.append(f"  {ov}    = {nz} * 0.6                ; white noise (flat hiss)")
    elif technique == "pink_noise":
        L.append(f"  {ov}    pinkish {nz}               ; pink noise (1/f tilt)")
    elif technique == "wind":
        # A gust is one physical variable -- air speed -- and it moves TWO things
        # together: faster air makes the turbulence both louder and higher in
        # frequency. (Here that coupling is right, unlike the string section where
        # independence was the point: one gust really does drive both.) Two
        # incommensurate rates so the gusting never falls into a pattern.
        L.append(f"  kgsa{tag}   poscil 0.5, 0.071            ; gust, slow")
        L.append(f"  kgsb{tag}   poscil 0.3, 0.113            ; second, incommensurate")
        L.append(f"  kgst{tag}   = 0.5 + kgsa{tag} + kgsb{tag}  ; gust strength")
        L.append(f"  awd{tag}    reson {nz}, 380 + 520 * kgst{tag}, 400, 2 ; higher when it blows")
        L.append(f"  {ov}    = awd{tag} * (0.30 + 0.42 * kgst{tag}) ; and louder")
    elif technique == "rain":
        # Rain is not a hiss, it is thousands of DROPS: discrete impacts, each one
        # a click that rings whatever it lands on. `dust2` is a Poisson stream of
        # impulses, which is exactly that process, and it measured what the shape
        # predicts -- crest 12.05 against filtered noise's ~4, i.e. actual
        # transients rather than a wash. The high-passed layer underneath is the
        # many distant drops that no longer arrive separately.
        L.append(f"  adrp{tag}   dust2 0.5, 1400              ; the patter: ~1400 impacts/s")
        L.append(f"  arzn{tag}   reson adrp{tag}, 2400, 1800, 2 ; each drop rings the surface")
        L.append(f"  awsh{tag}   atone {nz}, 2000             ; distant drops, merged to a wash")
        L.append(f"  kint{tag}   poscil 0.22, 0.043           ; the rain comes and goes")
        L.append(f"  {ov}    = (arzn{tag} * 0.94 + awsh{tag} * 0.51) * (1 + kint{tag})")
    elif technique == "surf":
        # Waves. The body of the water is low and always there; the BREAK is
        # bright and happens only at the crest, which is why the swell is squared
        # in the break term -- a wave does not break gently on its way up.
        L.append(f"  kswa{tag}   poscil 0.5, 0.055            ; swell, ~18 s")
        L.append(f"  kswb{tag}   poscil 0.22, 0.083           ; second, incommensurate")
        L.append(f"  kswl{tag}   = 0.5 + kswa{tag} + kswb{tag}  ; 0 .. 1.2")
        L.append(f"  asfl{tag}   tone {nz}, 700               ; the body of the wave")
        L.append(f"  asfb{tag}   atone {nz}, 1800             ; the break, bright")
        L.append(f"  {ov}    = asfl{tag} * (0.35 + 0.35 * kswl{tag}) + "
                 f"asfb{tag} * 0.30 * kswl{tag} * kswl{tag}")
    elif technique == "thunder":
        # Thunder ROLLS: the shock reaches the ear over a long, dispersive path,
        # so its loudness swells and fades irregularly for many seconds. A steady
        # low-passed hiss is a rumble that never rolls. Three incommensurate rates
        # give a roll with no period anyone can hear.
        L.append(f"  krla{tag}   poscil 0.5, 0.037            ; the roll")
        L.append(f"  krlb{tag}   poscil 0.28, 0.061")
        L.append(f"  krlc{tag}   poscil 0.16, 0.091")
        L.append(f"  athl{tag}   tone {nz}, 400               ; low broadband body")
        L.append(f"  {ov}    = athl{tag} * (0.80 + 0.61 * "
                 f"(krla{tag} + krlb{tag} + krlc{tag}))")
    elif technique == "hiss":
        # Deliberately left STATIONARY. Tape hiss and radio static really are
        # stationary processes -- that is what the words mean -- so giving this one
        # gusts or swells would be inventing a phenomenon rather than modelling it.
        L.append(f"  {ov}    atone {nz}, 6000            ; hiss/static: bright high noise")
        L.append(f"  {ov}    = {ov} * 0.7")
    elif technique == "crackle":
        # A fire crackles because resin cells burst: discrete pops, each ringing
        # the wood. The old version gated continuous noise with a 30 Hz random
        # hold, which is a noise being switched, not a series of events -- the
        # attacks were the gate's, not the fire's. `dust2` makes the events.
        L.append(f"  acrk{tag}   dust2 0.8, 22                ; ~22 pops a second")
        L.append(f"  apop{tag}   reson acrk{tag}, 1600, 1200, 2 ; each pop rings the wood")
        L.append(f"  kbed{tag}   poscil 0.5, 0.13             ; the fire breathes")
        L.append(f"  absd{tag}   atone {nz}, 3000             ; fine sizzle between pops")
        L.append(f"  {ov}    = apop{tag} * 1.90 + absd{tag} * (0.34 + 0.20 * kbed{tag})")
    else:
        L.append(f"  {ov}    = {nz} * 0.6")
    return "\n".join(L)


def _emit_voice(technique, tag="0"):
    """VOICE/formant synthesis (M4b) -> `aosc<tag>`: a sawtooth glottal source
    (rich harmonics for the formants to shape) through a bank of `reson` filters at
    the vowel's formant frequencies -- Csound's native formant idiom, NOT an
    additive vowel sketch. Pitched (the fundamental is the played note); the vowel
    is the formant envelope. reson iscl=1 is the PEAK-normalised mode -- iscl=2 is
    an RMS normalisation that assumes white-noise input (measured gain 4.4x at
    1200/900 and 6.8x at 460/380 for a harmonic source), and its scaling constant
    derives from exp(-2*pi*bw/sr), so it is not even constant across sample rates.
    Use 2 only for the genuinely noise-excited beds. The weighted
    sum is scaled and the tail limiter is the final bound."""
    ov = f"aosc{tag}"
    src = f"asrc{tag}"
    formants = _VOWEL_FORMANTS.get(technique, _VOWEL_FORMANTS["voice"])
    # JITTER and SHIMMER. These are the two quantities voice science actually
    # measures to tell a living voice from a synthetic one: jitter is the
    # cycle-to-cycle variation in period (a healthy voice runs a few tenths of a
    # percent), shimmer the same in amplitude (a few percent). A vocal fold is a
    # wet, asymmetric piece of tissue driven by a turbulent air stream -- it
    # cannot repeat a cycle exactly, and a source that does is heard as a machine
    # immediately, however good the formants are. That is what this key sounded
    # like: one perfectly periodic saw through three filters that never moved.
    #
    # Three and two incommensurate rates rather than a random generator. Jitter IS
    # aperiodic, and `randi` would model that more directly, but it draws a fresh
    # path every run -- the same patch would then sound different each time and no
    # measurement of it could be repeated. Rates with no common factor never
    # realign, which is aperiodic enough to hear and still deterministic.
    L = [f"  kjta{tag}   poscil 0.0022, 7.3           ; jitter: period, 3 rates",
         f"  kjtb{tag}   poscil 0.0014, 11.7",
         f"  kjtc{tag}   poscil 0.0009, 19.1",
         f"  ksha{tag}   poscil 0.030, 5.1            ; shimmer: amplitude, 2 rates",
         f"  kshb{tag}   poscil 0.018, 8.9",
         f"  {src}    vco2 0.5, kfreq * (1 + kjta{tag} + kjtb{tag} + kjtc{tag}), 0"]
    # The GLOTTAL SLOPE. A sawtooth falls at -6 dB/octave; the glottal flow
    # derivative a real larynx produces falls at about -12. That single octave of
    # difference is most of why saw-through-formants sounds buzzy and electronic
    # rather than voiced -- the upper harmonics are far too strong, so the
    # formants sit on a bed of buzz. One fixed pole supplies the missing tilt.
    # Fixed, not swept: this is the source's shape, not a filter effect.
    L.append(f"  aglt{tag}   tone {src}, 200              ; glottal slope, -6 -> -12 dB/oct")
    terms = []
    for i, (f, bw, amp) in enumerate(formants):
        # The tract is never still either -- tongue and jaw drift continuously,
        # so each formant wanders slowly on its own rate. Small (about 1%): the
        # VOWEL must stay this vowel, this is life inside it, not articulation.
        rate = (0.23, 0.31, 0.19)[i % 3]
        L.append(f"  kfw{tag}x{i}  poscil 0.011, {rate}            ; tract wander, formant {i+1}")
        L.append(f"  a{tag}f{i}   reson aglt{tag}, {f} * (1 + kfw{tag}x{i}), {bw}, 1 "
                 f"; formant {i+1} @ {f} Hz")
        terms.append(f"a{tag}f{i} * {amp:.3f}")
    gain = _VOWEL_GAIN.get(technique, 0.5)
    L.append(f"  {ov}    = ({' + '.join(terms)}) * {gain} * (1 + ksha{tag} + kshb{tag})")
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
    # The same larynx as the steady vowels -- jitter on the source, the glottal
    # -12 dB/oct tilt, shimmer on the output. Without this a morph would travel
    # between two living vowels through a dead one, which is worse than either.
    L = [f"  ; --- osc {tag}: vowel-sweep formant morph (trig-epoch reinit) ---",
         f"  kjta{tag}   poscil 0.0022, 7.3           ; jitter: period, 3 rates",
         f"  kjtb{tag}   poscil 0.0014, 11.7",
         f"  kjtc{tag}   poscil 0.0009, 19.1",
         f"  ksha{tag}   poscil 0.030, 5.1            ; shimmer: amplitude, 2 rates",
         f"  kshb{tag}   poscil 0.018, 8.9",
         f"  {src}0   vco2 0.5, kfreq * (1 + kjta{tag} + kjtb{tag} + kjtc{tag}), 0",
         f"  {src}    tone {src}0, 200             ; glottal slope, -6 -> -12 dB/oct"]
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
        L.append(f"  a{tag}f{i}   reson {src}, k{tag}cf{i}, {bw}, 1")
        terms.append(f"a{tag}f{i} * k{tag}am{i}")
    # The per-vowel output gain travels too. It has to: the vowels differ by 9 dB
    # through this bank (see _VOWEL_GAIN), so a fixed scale would make the morph
    # swell or collapse in loudness as it passes through 'ee'.
    gains = [_VOWEL_GAIN.get(k, 0.5) for k in chain if k not in ("silence", "zero")]
    gseg = ", ".join(f"{gains[j]}, {leg:.4f}" for j in range(nstage - 1)) + f", {gains[-1]}"
    L.append(f"  k{tag}gn   linseg {gseg}")
    L.append("  rireturn")
    L.append(f"  aosc{tag}    = ({' + '.join(terms)}) * k{tag}gn "
             f"* (1 + ksha{tag} + kshb{tag})")
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
    # fm_ep was here until it became the parametrised electric piano: three
    # foscili pairs plus a balance stage measure 16.08us MARGINAL, above three
    # keys this file already classes costly. See _emit_fm_ep's docstring for
    # the bench table and for why a whole-orchestra median hid it.
    "fm", "fm_bell", "metallic_fm", "cheby", "ring_mod", "sub_sine",
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
    spec = _thin(_MODAL_SPECTRA[technique], nmodes)
    n = len(spec)
    L = [f"  aexq{tag}   rand {exc}{_RAND31}          ; continuous exciter -> the metal is driven, a held note stands (31-bit: no 1.365 s loop)"]
    terms = []
    for i, (r, a) in enumerate(spec):
        q = int(q0 + (q1 - q0) * (i / (n - 1) if n > 1 else 0.0))
        L.append(f"  k{tag}qf{i}   limit kfreq * {r:.4f}, 20, 15000")
        L.append(f"  k{tag}qg{i}   = (kfreq * {r:.4f} < 15000 ? {a:.3f} : 0)")
        L.append(f"  a{tag}q{i}   mode aexq{tag}, k{tag}qf{i}, {q}      ; mode {i + 1} @ x{r:.2f}, Q {q}")
        terms.append(f"a{tag}q{i} * k{tag}qg{i}")
    L.append(f"  aosc{tag}    = ({' + '.join(terms)}) * {master:.3f}")
    return "\n".join(L)


def _emit_vco_drift(tag, depth=0.0007):
    """Per-VOICE analogue oscillator instability -> `kvdr<tag>`, a factor near 1.

    `depth` (default 0.0007, ~1.21 cents -- UNCHANGED, every existing caller
    passes none and gets exactly this) parametrizes the poscil amplitude for a
    caller that needs to SCALE it, e.g. analog_osc's `age` (0..0.007 = 0..12
    cents, see _emit_analog_osc). depth->cents is exact and measured:
    0.0007->1.21, 0.003->5.19, 0.007->12.08, 0.015->25.78.

    BJ's complaint was that the movement in this instrument was "alles langsame
    Filter sweeps, nicht wirklich glockenmovements oder analoge
    mikrofluktuationen". This is the second of those two.

    A real analogue polysynth has a separate oscillator board PER VOICE. Each has
    its own exponential converter at its own temperature, so voice 1 and voice 2
    drift differently and never quite agree -- which is why a chord on an
    analogue synth is wide and a chord on a digital one can be sterile. The
    important word is PER VOICE: a single global LFO applied to every voice would
    move them all in lockstep and produce none of that, the same mistake as a
    filter on the sum, one layer down.

    `ivoice` (= p4) is in scope in `instr 1`, so each voice can be given its own
    phase and its own rate. Golden-ratio phase offsets spread 16 voices about as
    evenly as possible, and the small per-voice rate difference stops them ever
    re-aligning. Deterministic -- no random generator, so the same patch drifts
    the same way twice and a measurement of it can be repeated.

    Measured in isolation: three voices at the SAME nominal pitch produce a
    wandering beat (envelope std/mean 0.24 over 6 s) instead of standing still.

    Depth is deliberately about a cent. That is below anything anyone would call
    movement in a single held note -- the liveness probe's own floor is 2 dB of
    partial travel and this produces essentially none -- so it does not tread on
    the "static" escape hatch. It becomes audible exactly where it should: when
    two oscillators or two voices sound together and beat.

    Two keys are deliberately EXCLUDED. `sine` is the designated standing tone and
    the movement escape hatch, so it stays mathematically pure. `chiptune` is not
    analogue at all: a chip's oscillator is a divider off a crystal, and being
    rock-steady in pitch is part of what makes it sound like a chip. Giving it an
    analogue drift would be the same category error as filtering a saw for brass.
    """
    return [f"  ivph{tag}   = frac(ivoice * 0.6180339887)   ; per-voice phase",
            f"  kvdr{tag}   poscil {depth:g}, 0.043 + 0.0037 * ivoice, giSine, ivph{tag}"]


# ── analog_osc: constants + params -> Csound (the first PARAMETRISED key) ───
# wave-axis kpw endpoints (MEASURED, this Csound 6.18 build): vco2 imode 4's
# kpw=0.5 is a pure symmetric triangle, kpw=0.02 converges EXACTLY onto imode
# 0's own sawtooth spectrum; imode 2's kpw IS the literal ON-duty fraction,
# 0.5 a square, 0.10 a narrow pulse.
_AOSC_KPW_TRIANGLE = 0.5
_AOSC_KPW_SAW = 0.02
_AOSC_KPW_SQUARE = 0.5
_AOSC_KPW_PULSE = 0.10
_AOSC_WAVE_SEG_A_MAX = 0.45   # wave <= this: pure imode-4 segment
_AOSC_WAVE_SEG_B_MIN = 0.55   # wave >= this: pure imode-2 segment
                              # between: linear (coherent, phase-locked) blend,
                              # segment A parked at its saw endpoint (kpw
                              # 0.02) and segment B parked at its square
                              # endpoint (kpw 0.5) -- wave 0.45/0.55 ARE
                              # exactly the saw/square anchors.
# imode-2 segment level compensation vs imode-4, MEASURED at kamp 0.6 (see
# commit report): the two are flat-RMS across their own whole range and
# differ by a fixed ratio, reproducing the 0.2887/0.4991=0.578 ratio measured
# at kamp 0.5 (a ratio is scale-invariant; re-verified at 0.6, not assumed).
_AOSC_SEG2_COMP = 0.578
_AOSC_KAMP = 0.6              # matches saw/square/pulse/triangle's own kamp
_AOSC_FAT_MAX_CENTS = 40.0    # total spread at fat=1 (symmetric: +-20 cents)
# 3-copy unison sum level compensation, MEASURED (see commit report: peak/RMS
# of the raw 3-copy sum vs a single copy across the wave axis).
_AOSC_FAT_COMP = {1: 1.0, 3: 0.578}
_AOSC_AGE_PITCH_DEPTH = 0.007   # -> _emit_vco_drift's depth arg (0..12 cents)
_AOSC_AGE_AMP_DEPTH = 0.03      # amplitude wobble, 0.7 Hz (mirrors the
                                # existing "analog" adjective's own DRIFT op)
_AOSC_AGE_SHAPE_DEPTH = 0.012   # duty/kpw wobble (mirrors square/pulse's own
                                # existing "kdty poscil 0.012, 0.057" idiom)


def _resolve_analog_osc_params(raw):
    """A possibly-partial/None {name: float 0..1} (whatever _validate_osc_chain
    survived) -> the complete 4-key dict, filling any missing/invalid entry
    with its documented default (dco_lexicon.json analog_osc.params[name]
    .default; mirrored in _AOSC_DEFAULTS). Re-validates defensively so a
    direct/standalone caller (a test harness invoking _emit_steady with no
    params at all) still gets a safe, complete, in-range dict."""
    raw = raw if isinstance(raw, dict) else {}
    out = dict(_AOSC_DEFAULTS)
    for k, v in raw.items():
        if k not in out:
            continue
        try:
            fv = float(v)
        except (TypeError, ValueError):
            continue
        if 0.0 <= fv <= 1.0:
            out[k] = fv
    return out


def _analog_osc_wave_segments(wave):
    """wave (0..1) -> [(imode, kpw_base, seg_gain, blend_weight), ...], the 1
    or 2 active vco2 segments at this wave value. blend_weight sums to 1.0
    across the returned list."""
    if wave <= _AOSC_WAVE_SEG_A_MAX:
        kpw = (_AOSC_KPW_TRIANGLE + (_AOSC_KPW_SAW - _AOSC_KPW_TRIANGLE)
               * (wave / _AOSC_WAVE_SEG_A_MAX))
        return [(4, kpw, 1.0, 1.0)]
    if wave >= _AOSC_WAVE_SEG_B_MIN:
        span = 1.0 - _AOSC_WAVE_SEG_B_MIN
        kpw = (_AOSC_KPW_SQUARE + (_AOSC_KPW_PULSE - _AOSC_KPW_SQUARE)
               * ((wave - _AOSC_WAVE_SEG_B_MIN) / span))
        return [(2, kpw, _AOSC_SEG2_COMP, 1.0)]
    blend = (wave - _AOSC_WAVE_SEG_A_MAX) / (_AOSC_WAVE_SEG_B_MIN - _AOSC_WAVE_SEG_A_MAX)
    return [(4, _AOSC_KPW_SAW, 1.0, 1.0 - blend),
            (2, _AOSC_KPW_SQUARE, _AOSC_SEG2_COMP, blend)]


def _emit_analog_osc(tag, params):
    """The parametrised analogue VCO -> `aosc<tag>`. wave/drive/fat/age are
    named 0..1 controls (dco_lexicon.json analog_osc.params), not a fixed
    idiom -- the lexicon's FIRST parametrised technique key.

    WAVE: two vco2 calls sharing iphs=0 explicitly (MEASURED: at the same
    kphs and frequency the two imodes' fundamentals sit -93.6/-90.0 degrees
    apart and a 50/50 sum is BIT-STABLE over time -- a genuine coherent
    waveform blend, not two oscillators beating, so a LINEAR crossfade is the
    correct blend, never equal-power). Segment A (imode 4) sweeps kpw 0.5
    (triangle) -> 0.02 (sawtooth) over wave 0..0.45; segment B (imode 2, the
    literal ON-duty fraction) sweeps 0.5 (square) -> 0.10 (pulse) over wave
    0.55..1.0, level-compensated (0.578, MEASURED). Outside 0.45..0.55 only
    the active segment renders; inside it, both render (each parked at its
    nearer endpoint) and blend linearly by wave.

    DRIVE: the Minimoog idiom -- no separate drive knob, the mixer is simply
    pushed past unity into a soft limiter. Maps 0..1 to a tanh pre-gain of
    1..20 (a compile-time constant, since drive is fixed per note), output-
    compensated so the peak returns to the oscillator's own nominal level at
    every setting -- the parameter changes CHARACTER, not loudness. drive=0
    skips the stage ENTIRELY (audibly clean, not merely low-THD).

    FAT: 0 -> one voice, no unison stage at all. Above 0 -> 3 copies at a
    symmetric detune (0..40 cents total spread), summed and level-compensated
    (MEASURED). Kept at 3 copies -- thickness saturates quickly with count and
    this instrument is deliberately small.

    AGE: one control moving three measured analogue instabilities together --
    PITCH (reuses _emit_vco_drift, scaled 0..0.007 depth = 0..12 cents),
    AMPLITUDE (a 0.7 Hz wobble, 0..3%, mirrors the "analog" adjective's own
    DRIFT op), and SHAPE (the existing square/pulse "kdty" duty-wobble idiom,
    scaled 0..0.012 on kpw). All three always render (age can legitimately be
    0, i.e. a harmless always-zero wobble) -- age never changes the CODE
    SHAPE, only these depths, unlike wave/drive/fat above."""
    ov = f"aosc{tag}"
    p = _resolve_analog_osc_params(params)
    wave, drive, fat, age = p["wave"], p["drive"], p["fat"], p["age"]

    segs = _analog_osc_wave_segments(wave)
    n_copies = 3 if fat > 0.0 else 1

    L = []
    L += _emit_vco_drift(tag, depth=age * _AOSC_AGE_PITCH_DEPTH)
    L.append(f"  kdty{tag}   poscil {age * _AOSC_AGE_SHAPE_DEPTH:g}, 0.057     "
             f"; age: shape/duty wobble on kpw")

    terms = []
    for si, (imode, kpw0, seg_gain, blend_w) in enumerate(segs):
        if blend_w <= 0.0:
            continue
        kpwv = f"kpw{tag}s{si}"
        L.append(f"  {kpwv}   = {kpw0:g} + kdty{tag}")
        for u in range(n_copies):
            if n_copies == 1:
                cents = 0.0
            else:
                cents = (u - (n_copies - 1) / 2.0) * (_AOSC_FAT_MAX_CENTS / (n_copies - 1))
            var = f"aao{tag}s{si}u{u}"
            freq_expr = "kfreq" if cents == 0.0 else f"kfreq * {2.0 ** (cents / 1200.0):.6f}"
            note = f"wave seg{si} imode{imode} copy {u}" + (f" ({cents:+.1f}c)" if cents else "")
            L.append(f"  {var}  vco2 {_AOSC_KAMP}, {freq_expr} * (1 + kvdr{tag}), "
                     f"{imode}, {kpwv}, 0   ; {note}")
            gain = seg_gain * blend_w * _AOSC_FAT_COMP[n_copies]
            terms.append(f"{var} * {gain:.6f}")
    L.append(f"  acln{tag}   = " + " + ".join(terms) + "    ; clean oscillator (pre-drive)")

    if drive > 0.0:
        pregain = 1.0 + 19.0 * drive
        comp = _AOSC_KAMP / math.tanh(_AOSC_KAMP * pregain)
        L.append(f"  adrv{tag}   = tanh(acln{tag} * {pregain:.4f}) * {comp:.4f}  "
                 f"; drive {drive:g}: pregain {pregain:.2f}x into tanh, output-compensated")
    else:
        L.append(f"  adrv{tag}   = acln{tag}                     ; drive 0: clean, no saturator stage")

    L.append(f"  kagw{tag}   oscili {age * _AOSC_AGE_AMP_DEPTH:g}, 0.7        "
             f"; age: amplitude wobble")
    L.append(f"  {ov}    = adrv{tag} * (1 + kagw{tag})")
    return "\n".join(L)


# ── fm_ep: constants + params -> Csound (the parametrised electric piano) ───
# Two BODY operator pairs crossfaded, plus a TINE pair, through `balance`.
# Every ratio is car=1, mod=R. DC appears iff car/mod is a positive INTEGER, so
# any R>1 is DC-safe unconditionally -- which is why the old 1:1 body and its
# empirical degree-4 DC-correction polynomial are both gone. The trap they
# existed to patch cannot occur here, and that trap was worse than the old
# comment knew: measured, the 1:1 DC is HISTORY-dependent, not a function of
# the current index at all (the same index 1.30 gives -11.2% held statically,
# +35.5% after a step, +21.8% down the shipped ramp). A fitted correction was
# therefore only ever valid for the exact ramp it was fitted against, which is
# precisely what a parametrised strike control would have invalidated.
_FMEP_BODY_MOD_EVEN = 3   # car=1, mod=3: a body carrying even AND odd harmonics
_FMEP_BODY_MOD_ODD = 2    # car=1, mod=2: sidebands at f*(1 +- 2n) = f,3f,5f -- odd ONLY
                          # (measured odd/even +131..135 dB, evens at the numerical floor)
_FMEP_TINE_MOD = 14.2     # the classic DX-EP region. 14.2 and NOT 14.0: an exact
                          # integer ratio is perfectly harmonic and reads as a bright
                          # buzz, while 14.2 places its partials BETWEEN harmonics and
                          # reads as metal.
_FMEP_AMP = 0.30          # per-pair source amplitude. Only the RATIO between the three
                          # matters -- `balance` sets the output level.
_FMEP_REF_AMP = 0.5       # balance reference: preserves the pre-parametrisation fm_ep's
                          # own output level (it ran `foscili 0.5`).
# ihp=10, and deliberately NOT fm_bell's ihp=1. MEASURED across 40 corners of the
# parameter space: at ihp=1 the follower's ~0.16 s time constant cannot track an
# 86 ms tine decay, so the onset overshoots the held level and loudness travels up
# to 1.24 dB at 1760 Hz; ihp=10 brings that to 0.46 dB and IMPROVES 220 Hz as well.
# fm_bell's 1 exists to protect its doublet beat; this construction has no doublet
# by design, so there is no amplitude feature here to preserve. Do not "correct"
# this back to match the neighbouring key.
_FMEP_BALANCE_IHP = 10
# The tine index decays to ZERO, and this is not a free choice. At ratio 14.2
# the tine's partials sit at f*|1 +- 14.2n| -- 13.2x and 15.2x the fundamental
# -- which is exactly what makes the ATTACK read as struck metal and exactly
# what must not survive into the held tone. An earlier 0.30 floor was invented
# here on the reasoning that "a trace of metal keeps the note's material"; BJ's
# ear found it immediately ("stark dissonant" on the plain `electric piano`
# prompt), because a permanent 13-15x inharmonic partial is a dissonance, not a
# material. At index 0 `foscili` emits its carrier alone -- a clean sine at the
# fundamental -- so the pair leaves the spectrum without leaving a hole.
_FMEP_TINE_FLOOR = 0.0
_FMEP_TINE_I0 = (1.5, 2.5)      # ting 0..1 -> starting tine index
_FMEP_RING_SEC = (0.086, 0.770)  # ring 0..1 -> tine half-life. Both ends MEASURED off
                                 # the reference opcodes: fmrhode's bright attack has a
                                 # half-life of ~86 ms, fmwurlie's ~770 ms.
_FMEP_BODY_IDX = (1.0, 6.0)     # strike 0..1 -> body STARTING index
_FMEP_BODY_FLOOR = 0.31   # the body index settles to start*0.31, which reproduces the
                          # pre-parametrisation 1.30/4.20 ratio exactly.
_FMEP_BODY_SOFTEN_SEC = 1.6     # unchanged from the pre-parametrisation key
_FMEP_REED_KNEE = 0.75    # see _fm_ep_reed_to_mix


def _resolve_fm_ep_params(raw):
    """A possibly-partial/None {name: float 0..1} -> the complete 4-key dict,
    filling any missing or invalid entry with its documented default
    (dco_lexicon.json fm_ep.params[name].default; mirrored in _FMEP_DEFAULTS).
    Re-validates defensively so a standalone caller still gets a safe dict."""
    raw = raw if isinstance(raw, dict) else {}
    out = dict(_FMEP_DEFAULTS)
    for k, v in raw.items():
        if k not in out:
            continue
        try:
            fv = float(v)
        except (TypeError, ValueError):
            continue
        if 0.0 <= fv <= 1.0:
            out[k] = fv
    return out


def _fm_ep_reed_to_mix(reed):
    """reed (0..1) -> kW, the odd-only body's share of the body mix.

    MEASURED odd/even balance against kW: 0.0 -> -1.5 dB, 0.4 -> +0.3,
    0.6 -> +4.8, 0.8 -> +12.7, 0.90 -> +19.8, 1.0 -> +66.6 dB -- a cubic over
    most of the range and then a cliff at the very top, where the even body is
    gone entirely and the only evens left are `balance`'s own 2*f0 follower
    ripple. A LINEAR control would spend nine tenths of its travel below a
    Rhodes and then lurch, so the warp is piecewise: a cube root up to the
    classic-EP point (kW 0.90, which lands on fmrhode's own measured late
    odd/even of +19.9 dB), then a short linear run on to the pure reed.

    A modulator-RATIO sweep was tried for this axis first and refuted twice
    over. Harmonicity is a step function of the ratio's RATIONALITY, not a
    continuous function of the ratio (mod 1.5 measures 0.411, mod 1.6 measures
    0.003) -- that is number theory and finer stepping cannot fix it. And
    approaching mod 2.0 beats at exactly |2-mod|*f0, pitch-proportional,
    reaching amp_mod 0.118 against the project's own 0.08 movement floor: at
    220 Hz that is a 22 Hz beat, the same roughness band the fm_bell history
    already rejected once. Exactly 2.0 is the only beat-free point (amp_mod
    0.026) and stopping just short of it buys nothing -- 1.98 measures the same
    odd character AND the beating."""
    if reed <= _FMEP_REED_KNEE:
        return 0.9 * (reed / _FMEP_REED_KNEE) ** (1.0 / 3.0)
    return 0.9 + 0.4 * (reed - _FMEP_REED_KNEE)


def _emit_fm_ep(tag, params):
    """The parametrised FM electric piano -> `aosc<tag>`. ting/ring/reed/strike
    are named 0..1 controls (dco_lexicon.json fm_ep.params).

    What this replaced and why. The pre-parametrisation fm_ep was a single 1:1
    `foscili` with a softening index. Measured against Csound's own `fmrhode`
    and `fmwurlie`, it had the woody body and NO metallic attack whatsoever:
    0.01% of its energy above the 8th harmonic, where the two references have
    11.7% and 72.8%. It could not be tuned into one either -- a 1:1 ratio is
    harmonic by construction and cannot place a partial off the comb.

    The body's index STILL softens over the note and then holds, exactly as
    before; `strike` sets where that softening starts, and the default lands on
    the old key's own 4.20 -> 1.30. This is a timbre motion, not an amplitude
    envelope: the tone never dies.

    TING adds what was missing -- a high-ratio pair whose index decays, so its
    upper partials fade while the tone's LEVEL holds. That is the whole point
    of the `balance` stage: colour travels, loudness does not.

    THE TING IS AN ONSET FEATURE, and in a morph chain that does not reach
    fm_ep until later it is simply not heard. That is a property, not a bug,
    and the measurement is worth keeping so nobody "fixes" it twice. `knote`
    counts from the note, so an 86ms tine has decayed to under half a percent
    by the time a 1.4s morph leg opens: `sine > fm_ep` peaks at 1131Hz against
    3584Hz for fm_ep alone. Making it audible there would mean re-striking when
    the stage arrives -- inventing a note-onset that the player never played,
    which is the same class of error as an oscillator owning its own transport.
    A hammer that was struck a second ago cannot be heard now.

    KNOWN LIMIT, and it is structural rather than a tuning miss. Above
    f0 = sr/(2*14.2) -- about 1553 Hz at 44.1 kHz -- the tine's modulator
    exceeds Nyquist and folds. The `limit` below keeps it legal, but a cap is
    not a fix: at 1760 Hz the effective ratio becomes 11.28, odd/even collapses
    from 29.2 to 1.4 dB and above-8th energy from 44.7% to 16.3%. The top
    octave genuinely will not sound like the bottom. It does NOT read as
    aliasing and no aliasing check will catch it -- measured energy below the
    fundamental stays at 0.00% -- it simply gets quietly duller.

    `balance` is also not spectrally transparent: its gain follower ripples at
    2*f0 and injects even-harmonic content into an odd-only signal (a pure 1:2
    body measures +135.5 dB odd/even raw but +66.6 dB through the stage). Here
    that is welcome, because it lands near fmwurlie's own measured +75.9, but
    it is a real side effect and not a coincidence to be relied on blindly.

    NOT `_CHEAP_TECH` any more, and the first measurement of that said the
    opposite. Comparing whole-orchestra medians (20.7 us here against `fm`'s
    19.1) hides the answer, because the orchestra's fixed overhead dominates
    both. The number that matters is the MARGINAL cost per oscillator,
    (bench(3 osc) - bench(1 osc)) / 2: this key went 6.29 -> 16.08 us, which is
    more than `clarinet` (9.42), `harpsichord` (9.79) and `additive` (12.35),
    all three of which the file classes costly. Left in the allowlist it would
    contribute ZERO to `_modal_budget`'s costly count, so `cymbal + fm_ep +
    fm_ep` would claim the most generous thinning tier while being nearly the
    most expensive patch reachable -- measured 105.2 -> 115.5 us against the
    133 us gate from this change alone. That is precisely the under-count the
    allowlist exists to prevent."""
    ov = f"aosc{tag}"
    p = _resolve_fm_ep_params(params)
    ting, ring, reed, strike = p["ting"], p["ring"], p["reed"], p["strike"]

    kw = _fm_ep_reed_to_mix(reed)
    tine_amt = ting ** 0.65          # linearises above-8th energy against the control
    tine_i0 = _FMEP_TINE_I0[0] + (_FMEP_TINE_I0[1] - _FMEP_TINE_I0[0]) * ting
    half = _FMEP_RING_SEC[0] * (_FMEP_RING_SEC[1] / _FMEP_RING_SEC[0]) ** ring
    idx0 = _FMEP_BODY_IDX[0] + (_FMEP_BODY_IDX[1] - _FMEP_BODY_IDX[0]) * strike
    idxf = idx0 * _FMEP_BODY_FLOOR

    L = []
    # `knote` is the shared per-VOICE elapsed-time counter from the orchestra
    # preamble, deliberately not a private one here: a counter emitted in this
    # body lands inside _emit_crossfade_morph's `if <tent gain> > 0` block and
    # counts audible rather than elapsed seconds, which silently swallows an
    # 86ms tine on every morph chain. See the preamble for the measurement.
    L.append(f"  kbdx{tag}   = {idxf:g} + {idx0 - idxf:g} * (1 - min(knote / "
             f"{_FMEP_BODY_SOFTEN_SEC:g}, 1)) ; strike -> mellow, then holds")
    # exp(), NOT expseg/expon: tools/csound_keys_gate.py's _FORBIDDEN_ENV bars
    # those opcodes anywhere in the orchestra (the synth owns amplitude). This is
    # an INDEX decay, not an amplitude one, and exp() is a function, not an opcode.
    L.append(f"  ktng{tag}   = {_FMEP_TINE_FLOOR:g} + {tine_i0 - _FMEP_TINE_FLOOR:g} "
             f"* exp(-knote * {0.69315 / half:.4f}) ; the metal fades out of the tone")
    L.append(f"  ktr{tag}    limit {_FMEP_TINE_MOD:g}, 1, sr * 0.45 / kfreq "
             f"; tine ratio, capped below Nyquist (see docstring: a cap, not a fix)")
    L.append(f"  aevn{tag}   foscili {_FMEP_AMP * (1 - kw):.4f}, kfreq, 1, "
             f"{_FMEP_BODY_MOD_EVEN}, kbdx{tag}, giSine ; body, even + odd harmonics")
    L.append(f"  aodd{tag}   foscili {_FMEP_AMP * kw:.4f}, kfreq, 1, "
             f"{_FMEP_BODY_MOD_ODD}, kbdx{tag}, giSine ; body, odd harmonics only")
    L.append(f"  atin{tag}   foscili {_FMEP_AMP * tine_amt:.4f}, kfreq, 1, "
             f"ktr{tag}, ktng{tag}, giSine ; the inharmonic ting")
    L.append(f"  asum{tag}   = aevn{tag} + aodd{tag} + atin{tag}")
    L.append(f"  aref{tag}   poscil {_FMEP_REF_AMP:g}, kfreq")
    L.append(f"  {ov}    balance asum{tag}, aref{tag}, {_FMEP_BALANCE_IHP} "
             f"; colour travels, loudness holds")
    return "\n".join(L)


# ── drum_head: constants + params -> Csound (the parametrised membrane) ─────
# A stretched circular membrane rings at the zeros of a Bessel function, which
# are NOT whole-number multiples of the fundamental -- that is the whole reason
# a drum reads as a drum and not as a low note, and it is the substance of this
# instrument rather than a detail of it.
_DRUM_IDEAL = (1.000, 1.594, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501)
# Loading the head with the air in a kettle drags those spacings towards whole
# numbers until a definite pitch appears. These are the principal timpani modes
# and they are very nearly 2:3:4:5:6:7 -- which is exactly why a kettledrum has
# a note and a tom does not.
_DRUM_TIMPANI = (1.000, 1.500, 1.990, 2.440, 2.890, 3.330, 3.770, 4.210)
# The modes that breathe evenly across the whole skin rather than rippling
# around it (the axially symmetric ones). A stroke in the dead centre can only
# wake these; a stroke at the rim barely touches them. Indices into the tuples
# above.
_DRUM_AXISYMMETRIC = frozenset({0, 3})
_DRUM_EXC = 0.07          # continuous exciter, in the range the three existing
                          # modal keys use (0.05-0.09)
_DRUM_MASTER = 3.5        # NOT comparable to the other modal banks' master
                          # gains: those scale a peak-normalised spectrum, this
                          # scales one levelled on emitted power (_drum_bank_rms),
                          # which is a different unit. Set by measurement, on RMS
                          # rather than peak because RMS is what the ear compares
                          # when switching keys: at 110Hz the three neighbouring
                          # modal keys sit at rms 0.084-0.090 (cymbal/glass/
                          # struck_bar), and this lands drum_head at 0.085.
                          # Headroom checked at the same time, since levelling on
                          # RMS says nothing about peaks. Worst true peak over 16
                          # corners x 6 pitches is 0.757, at the brightest,
                          # longest-ringing corner at 20Hz. Measure PRE-clip if
                          # you revisit this: the output of `clip 0, 0.95, 0.85`
                          # asymptotes at ~0.879 no matter how hard it is driven,
                          # so a post-clip reading of 0.879 is the limiter's
                          # ceiling being reported as though it were margin --
                          # at that same corner the pre-clip signal is 1.000 at
                          # vel=0.8 and 1.601 at vel=1.0 with three layers
                          # stacked. Inside the 50-200Hz band this instrument is
                          # for, nothing clips even at vel=1.0 (110Hz: 0.452 one
                          # layer, 0.767 three). The margin is thin only at the
                          # infrasonic extreme, and cymbal behaves identically
                          # (0.762/0.879), so this is the family's shared
                          # ceiling rather than something drum_head introduced.
# Q is bounded by RING-UP time, not by taste. A `mode` resonator reaches steady
# state in roughly Q/(pi*f) seconds, and a drum lives at 50-200Hz where that
# gets long fast: Q=220 at 110Hz is a 0.64s time constant, so the note is still
# growing seconds after it starts. Measured at that value, damping=open made
# loudness travel +2.61dB over the note -- an audible swell, and a straight
# breach of the rule that colour may travel and loudness may not. Q=60 would
# give 0.17s there; 28 gives 0.08s, which is what it took to hold the travel
# inside 1dB at the LOW pitches this instrument actually lives at.
_DRUM_Q_OPEN = 28.0       # first mode, damping=0: rings, and still settles
_DRUM_Q_MUFFLED = 5.0     # first mode, damping=1: broad enough to read as a thud
_DRUM_Q_TILT = 0.35       # the top mode's Q as a fraction of the first mode's --
                          # higher modes always die back faster on a real skin


# ── The struck instruments: keys that own their own decay ────────────────────
#
# The first three keys in this library where the sound FADES BY ITSELF. §4's
# invariant says the oscillator is a spectrum source and the synth owns the
# amplitude envelope; BJ narrowed that on 2026-07-20 from a rule into a CHOICE
# THE PROMPT MAKES (the `wave` convention). These are the other half of it.
#
# Each re-strikes on the trigger epoch via the same changed2/reinit idiom the
# vowel-sweep and morph emitters already use -- so a new note is a new strike
# and a repeated note does not keep ringing the first one.
#
# LEVELS ARE MEASURED, NOT CHOSEN. Rendered at --format=float (so nothing clips
# and the numbers are the models', not the meter's) with kamp swept x2, then the
# peak checked at seven pitches across 55..3520 Hz:
#
#   fmrhode   kamp LINEAR (x2.00, x1.99); peak FLAT 0.60 at every pitch measured
#   fmwurlie  kamp LINEAR (x1.98, x2.03); peak FLAT 0.60-0.61 at every pitch
#   vibes     kamp NOT linear (x2.14, x2.07); peak runs 3.52 at 40 Hz down to
#             0.58 at 440 and back to 0.80 at 3520 -- a 15 dB tilt across the
#             register, which is a VOLUME control called "pitch" and exactly the
#             defect §7 failure mode 8 is about. Compensated below.
_RHODES_AMP = 0.7821      # -> peak 0.60, flat across the register (measured)
_WURLI_AMP = 0.9839       # -> peak 0.60, flat across the register (measured)
_VIBES_AMP = 0.1863       # -> peak 0.58 AT 440 Hz ONLY; see _VIBES_TILT
# Below ~440 Hz vibes gets louder as the note gets lower, on a clean power law:
# measured +15.4/+13.0/+10.5/+8.5/+6.5/+4.7/+2.0 dB at 40/55/80/110/160/220/320
# against its own 440 Hz level. An exponent of 0.73 on (f/440) tracks that within
# 0.4 dB. Above 440 the model RISES about 2.6 dB to 3520 (0.185 -> 0.249) -- an
# earlier version of this comment claimed "flat within ~1 dB" while quoting
# figures that were 2.8 dB apart, i.e. it contradicted itself. The compensation is
# still capped at 1.0 up there, because 2.6 dB of gentle brightening across three
# octaves is inside what every other key in this file is allowed, and because a
# polynomial spanning both regimes fits 5-7 dB worse than this two-regime form.
_VIBES_TILT = 0.73
# ...and above ~4 kHz vibes collapses: measured peak 0.249 at 3520 Hz, 0.124 at
# 4000, 0.062 at 5000. A structural ceiling of the model, like fmvoice's formant
# floor -- not tunable, and reachable from an ordinary note at the 2' register.
# UNHANDLED, deliberately: clamping the key's register or crossfading to a modal
# bank above the ceiling are both audible decisions, and BJ has not heard the
# instrument yet. Tracked as task #34 rather than decided here.


def _emit_struck(technique, tag, strike_gate=None):
    """One of the struck instruments -> `aosc<tag>`, re-struck on the trig epoch.

    The opcode call sits INSIDE the reinit block on purpose: these models put
    their whole envelope in the init pass, so re-running init is what makes a new
    note a new strike. Outside it, the first note of a session would ring and
    every note after it would be silent.

    `strike_gate` is the crossfade-morph tent gain for THIS stage, and it exists
    because of a defect found in review: a struck key in a LATER morph stage
    struck at note-on, decayed to nothing during the earlier stages, and its leg
    then opened onto silence. Measured before the fix -- `saw > rhodes`, note held
    to 8 s, went permanently silent at 2.0 s and the rhodes was never heard at
    all. So the stage also re-strikes when its own gain RISES off zero, which is
    the moment the morph actually arrives at it. `trigger` mode 0 is the
    rising-edge crossing; it first runs on the k-cycle the enclosing `if gain > 0`
    branch goes true, with its internal previous value still at its init 0, so
    that first crossing is caught rather than missed."""
    ov = f"aosc{tag}"
    lbl = f"strike{tag}"
    L = [f"  ; --- osc {tag}: {technique} -- struck instrument, decays on its own ---"]
    if strike_gate:
        L.append(f"  ktrg{tag}   trigger {strike_gate}, 0.0001, 0"
                 f"   ; this morph stage's leg opening = a fresh strike")
        L.append(f"  if changed2(ktrig) == 1 || ktrg{tag} == 1 then")
    else:
        L.append("  if changed2(ktrig) == 1 then")
    L.append(f"    reinit {lbl}")
    L.append("  endif")
    L.append(f"{lbl}:")
    if technique == "rhodes":
        # kc1/kc2 are the two FM index scalars; kvdepth/kvrate the built-in
        # vibrato, kept shallow (0.01 at 6 Hz) because vibrato belongs to the
        # synth's LFO, not to the oscillator. Tables 1,1,1,1,1 = the sine table.
        L.append(f"  {ov}    fmrhode {_RHODES_AMP:.4f}, kfreq, 1, 1, 0.01, 6, 1, 1, 1, 1, 1")
    elif technique == "wurlitzer":
        L.append(f"  {ov}    fmwurlie {_WURLI_AMP:.4f}, kfreq, 1, 1, 0.01, 6, 1, 1, 1, 1, 1")
    else:  # vibraphone
        # ihrd 0.9 = hard mallet, ipos 0.5 = struck at the centre, table 4 = the
        # strike impulse (a TABLE number -- passing a scalar here deletes the note
        # at init and renders silence; that mistake is what rejected this opcode
        # in the first place, see failure mode 9 in docs/LCO_CONCEPT.md).
        L.append(f"  kvtl{tag}   limit kfreq / 440, 0.001, 1   ; 1.0 at and above 440 Hz")
        L.append(f"  kvamp{tag}  = {_VIBES_AMP:.4f} * kvtl{tag} ^ {_VIBES_TILT}"
                 f"   ; undo the model's 15 dB register tilt")
        L.append(f"  {ov}    vibes kvamp{tag}, kfreq, 0.9, 0.5, 4, 6, 0.01, 1, 0.5")
    L.append("  rireturn")
    return "\n".join(L)


def _resolve_drum_head_params(raw):
    """A possibly-partial/None {name: float 0..1} -> the complete 4-key dict,
    filling any missing or invalid entry with its documented default
    (dco_lexicon.json drum_head.params[name].default; mirrored in
    _DRUM_DEFAULTS). Guards the SHAPE as well as the values, because a direct
    caller of build_orchestra can hand this anything."""
    raw = raw if isinstance(raw, dict) else {}
    out = dict(_DRUM_DEFAULTS)
    for k, v in raw.items():
        if k not in out:
            continue
        try:
            fv = float(v)
        except (TypeError, ValueError):
            continue
        if 0.0 <= fv <= 1.0:
            out[k] = fv
    return out


def _drum_head_spectrum(p):
    """The four controls -> [(ratio, amplitude), ...], the membrane's partials.

    `pitched` interpolates the RATIOS from the ideal membrane to the tuned
    kettledrum, so the drum acquires a definite pitch without the played pitch
    moving at all -- pitch belongs to the synth, and this control only decides
    whether the sound HAS one.

    `spot` and `tension` shape the AMPLITUDES, and they are deliberately two
    different things rather than two names for brightness. `spot` decides WHICH
    modes are woken: a centre stroke can only excite the axially symmetric ones
    and rolls off steeply above them, a rim stroke spreads energy up the bank
    and barely touches them. `tension` then tilts whatever is there, geometric
    in the mode index, so a slack head keeps only its lowest partials and a
    tight one carries its upper ones.

    The amplitudes come back UNNORMALISED. What the bank is worth in loudness
    depends on the resonator Q as much as on these weights, and Q is not known
    here -- see `_emit_drum_head`, which levels the bank once it has both."""
    n = len(_DRUM_IDEAL)
    pitched, spot = p["pitched"], p["spot"]
    tilt = 0.35 + 1.30 * p["tension"]
    spec = []
    for i in range(n):
        ratio = _DRUM_IDEAL[i] + (_DRUM_TIMPANI[i] - _DRUM_IDEAL[i]) * pitched
        centre_w = 1.0 / (1.0 + i)                      # steep: only the low modes
        rim_w = 0.35 + 0.65 * (i / (n - 1))             # spread up the bank
        amp = centre_w + (rim_w - centre_w) * spot
        if i == 0:
            # The fundamental has to GET OUT OF THE WAY on the tom side, or the
            # instrument's whole substance is inaudible. Measured with a flat
            # weight, autocorrelation pitchedness sat at 0.840 for an ideal
            # membrane and 0.844 for a tuned kettledrum -- i.e. `pitched` moved
            # nothing, because a dominant partial at ratio 1.0 reads as a pitch
            # whatever the partials above it are doing. This is also the
            # physical truth: a tom's lowest mode is heavily air-damped, which
            # is exactly why it thumps instead of singing.
            amp *= 0.40 + 0.60 * pitched
        if i in _DRUM_AXISYMMETRIC:
            w = 1.40 + (0.50 - 1.40) * spot             # 1.40 at centre, 0.50 at rim
            # ...but only while those indices ARE the axially symmetric modes.
            # The two ratio tuples are aligned by list POSITION, not by physical
            # mode: _DRUM_IDEAL is the Bessel set, whose m=0 modes are indices 0
            # and 3, while _DRUM_TIMPANI lists (1,1)(2,1)(3,1)... -- not one of
            # which is axially symmetric. Holding the boost across the
            # interpolation would apply a centre-strike emphasis to two modes a
            # centre strike cannot excite at all, so it fades out with `pitched`.
            amp *= 1.0 + (w - 1.0) * (1.0 - pitched)
        amp *= tilt ** (i / (n - 1))
        spec.append((ratio, amp))
    return spec


def _drum_bank_rms(spec, qs):
    """The RMS a noise-driven `mode` bank will actually emit, in arbitrary units
    -- for LEVEL-MATCHING one control setting against another, nothing else.

    Why this is an integral and not a sum. The obvious normalisation is
    sum(a^2): treat the modes as independent and add their powers. They are not
    independent -- every resonator in the bank is fed the SAME noise, so where
    two bands overlap they add in amplitude, not in power. Measured on the real
    opcode, two modes at Q=5 a fifth apart put out 0.70dB MORE than the sum of
    their powers, and at a 1.075 spacing 2.74dB more. This is not a correction
    factor, it is the difference between two spacings of the drum's own ratios,
    and no sum can see it. So integrate |sum_i H_i|^2 over frequency and let the
    cross terms fall out on their own.

    H_i is a `mode` resonator, whose peak gain runs as a*Q/f -- fixed not by
    reading the opcode but by rendering it: single-mode power over a grid of
    frequency and Q fits P ~ Q^1.020 / f^1.015, i.e. Q/f, worst residual 0.63dB.
    The model reproduces the measured two-mode overlap to within 0.04dB at every
    spacing and Q tried, and predicts the rendered level of the whole bank to
    within 0.21dB across the 16 corners -- against 4.62dB worst for sum(a^2).

    MEASURE THIS UNCLIPPED. Two modes at unit amplitude peak near 2.9 against
    0dbfs=1, so a 16-bit render hard-clips them and every number above comes out
    wrong in a plausible-looking way: the fit degrades to Q^0.956/f^0.951 with a
    1.4dB residual that invites being blamed on the opcode, and the Q=28 overlap
    reads -0.6dB, i.e. an apparent CANCELLATION that does not exist (it is
    +0.01dB). Render to float, or scale the test signal down.

    Frequency is in units of the played pitch, so kfreq cancels -- it is common
    to every mode and this is only ever used as a ratio. The grid is
    logarithmic because the resonances are narrow: at Q=28 a peak's half-width
    is 1.8% of its centre, and a linear grid coarse enough to be cheap would
    step straight over it."""
    if not spec:
        return 1.0
    lo = 0.02
    hi = max(r for r, _ in spec) * 6.0
    n = 6000
    step = math.log(hi / lo) / n
    total = 0.0
    terms = [(r, a / r, q) for (r, a), q in zip(spec, qs)]
    for k in range(n + 1):
        x = lo * math.exp(k * step)
        sre = sim = 0.0
        for r, g0, q in terms:
            u = x / r
            dre = 1.0 - u * u
            dim = u / q
            g = g0 / (dre * dre + dim * dim)
            sre += g * dre
            sim -= g * dim
        total += (sre * sre + sim * sim) * x * step   # dx = x*step on a log grid
    return math.sqrt(total) or 1.0


def _emit_drum_head(tag, params, nmodes=None):
    """The parametrised drum head -> `aosc<tag>`: a bank of `mode` resonators at
    a membrane's inharmonic ratios, driven by continuous low-level noise -- the
    same idiom the cymbal/glass/struck_bar keys already use, and the reason a
    held note STANDS here. The skin is a resonating object being excited, so
    there is no self-decay to fake and no amplitude envelope to smuggle in: the
    synth still owns the envelope, exactly as it does everywhere else.

    A real drum is of course a STRUCK, dying thing, and the honest way to say
    what this is instead is a bowed or rubbed head -- a membrane held in
    excitation. That is a deliberate reading of the platform rule that a
    self-decay is acceptable only where there is no other way: here there is
    another way, so it is taken, and the strike itself belongs to the player's
    envelope rather than to the oscillator.

    `damping` sets the resonator Q: open leaves narrow clear resonances that
    sing, muffled broadens them until the bank reads as a coloured thud. Q
    always falls across the bank, because higher modes die back faster on a real
    skin. Because Q is also what decides how much power the bank emits, moving
    it means re-levelling the bank -- see the normalisation below; without that
    step `muffled` is a 4dB fader wearing a colour control's name.

    Per-mode k-rate gating mutes any resonator the played pitch would push past
    the safe band -- a `mode` filter near Nyquist is unstable, so this mutes
    rather than clamps, and no pinned high whine can survive.

    MEASURING THIS BANK: use RMS windows of about a second, never a tenth. A
    narrow resonator fed with noise emits narrowband noise, whose envelope
    fluctuates on a timescale of Q/f -- so two short windows sample one slowly
    varying random envelope at two arbitrary phases and report the difference
    as drift. Measured over the same corners: 9.02 dB of apparent loudness
    travel with 0.1s windows, 1.07 dB with 1.0s windows. The first number is an
    artefact of the metric and nearly bought a fix for a defect that was not
    there; the second is the real ring-up and is within tolerance. Re-measured
    after the normalisation went to _drum_bank_rms: worst first-second-to-last-
    second travel is 1.57 dB, at maximum Q with only the low modes awake
    (pitched/centre/slack/open), and it is a 20Hz figure -- that same corner at
    55Hz is -0.15 dB, and the worst over all 16 corners at 55Hz is 0.43 dB.

    AND A REAL ENSEMBLE IS HARDER THAN IT LOOKS. Averaging over renders is the
    right instinct, but `rand` carries its OWN iseed (default 0.5) and ignores a
    global `seed` statement, so an orchestra re-rendered under a dozen different
    seeds is bit-identical every time -- max abs diff 0.0. Every ensemble figure
    quoted while this instrument was built was one realisation wearing an
    ensemble's name; the conclusions survived re-measurement with distinct
    iseeds, but they were not entitled to. Vary the iseed, and check that two
    renders actually differ before trusting the average of many."""
    p = _resolve_drum_head_params(params)
    spec = _thin(_drum_head_spectrum(p), nmodes)
    n = len(spec)
    q0 = _DRUM_Q_OPEN + (_DRUM_Q_MUFFLED - _DRUM_Q_OPEN) * p["damping"]
    q1 = q0 * _DRUM_Q_TILT
    qs = [max(2.0, q0 + (q1 - q0) * (i / (n - 1) if n > 1 else 0.0))
          for i in range(n)]
    # Level the bank on the power it will ACTUALLY emit, so that four COLOUR
    # controls do not double as faders. Normalising on sum(a^2) left `damping`
    # acting as a 4dB volume control and `spot` swinging 3-5dB across its
    # travel, because it ignored both the Q/f weighting and the overlap between
    # bands -- see _drum_bank_rms. Done here rather than in _drum_head_spectrum
    # because Q is only known here, and AFTER _thin so a bank thinned to fit the
    # CPU budget keeps its loudness instead of quietly losing its dropped modes.
    # Measured after: worst full-travel swing of any control is 0.24dB at 110Hz
    # and above, 0.46dB at 55Hz, 1.92dB at 20Hz -- the bottom figure is `mode`
    # departing from an ideal 2-pole down there, and is left rather than fitted
    # away, since a drum head at 20Hz is a rumble and the instrument lives at
    # 50-200Hz. All four colour axes still travel: definiteness 0.26->0.53,
    # `spot` centroid 346->491Hz, `tension` 378->455Hz, `damping` in-band
    # fraction 0.302->0.196.
    norm = _drum_bank_rms(spec, qs)
    L = [f"  aexq{tag}   rand {_DRUM_EXC}{_RAND31}          ; continuous exciter -- "
         f"the skin is driven, a held note stands (31-bit: no 1.365 s loop)"]
    terms = []
    for i, ((r, a), q) in enumerate(zip(spec, qs)):
        a /= norm
        L.append(f"  k{tag}qf{i}   limit kfreq * {r:.4f}, 20, 15000")
        L.append(f"  k{tag}qg{i}   = (kfreq * {r:.4f} < 15000 ? {a:.3f} : 0)")
        L.append(f"  a{tag}q{i}   mode aexq{tag}, k{tag}qf{i}, {q:.1f}      "
                 f"; mode {i + 1} @ x{r:.2f}, Q {q:.0f}")
        terms.append(f"a{tag}q{i} * k{tag}qg{i}")
    L.append(f"  aosc{tag}    = ({' + '.join(terms)}) * {_DRUM_MASTER:.3f}")
    return "\n".join(L)


def _emit_steady(technique, tag="0", nmodes=None, params=None, strike_gate=None):
    """A single (non-morph) technique -> `aosc<tag>`. Uses Csound's native
    opcodes; every temporary is suffixed with `tag` (per-osc uniqueness).
    `params` ({canonical_key: {name: value}}) is only ever consulted for a
    technique in _PARAM_SCHEMAS (analog_osc today); every other branch below
    ignores it, unchanged."""
    ov = f"aosc{tag}"
    L = []
    if technique in _NOISE_TECH:
        return _emit_noise(technique, tag)
    if technique in _VOICE_TECH:
        return _emit_voice(technique, tag)
    if technique == "drum_head":
        # BEFORE the _MODAL_TECH branch on purpose: drum_head is in that set so
        # _modal_budget counts its resonator bank against the CPU budget, but
        # its spectrum is computed from parameters rather than looked up in
        # _MODAL_SPECTRA, so it must never reach _emit_modal.
        return _emit_drum_head(tag, (params or {}).get("drum_head"), nmodes)
    if technique in _STRUCK_TECH:
        return _emit_struck(technique, tag, strike_gate)
    if technique in _MODAL_TECH:
        return _emit_modal(technique, tag, nmodes)
    if technique == "analog_osc":
        return _emit_analog_osc(tag, (params or {}).get("analog_osc"))
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
        L += _emit_vco_drift(tag)
        L.append(f"  apw{tag}     vco2 0.6, kfreq * (1 + kvdr{tag}), 2, kpw{tag} ; imode 2 = pulse, kpw = width")
        L.append(f"  {ov}    = apw{tag} - 0.6 * (2 * kpw{tag} - 1) ; {_DC_NOTE}")
    elif technique == "square":
        L += _emit_vco_drift(tag)
        L.append(f"  kdty{tag}   poscil 0.012, 0.057          ; comparator threshold drift")
        L.append(f"  apw{tag}    vco2 0.6, kfreq * (1 + kvdr{tag}), 2, 0.5 + kdty{tag} ; square")
        L.append(f"  {ov}    = apw{tag} - 1.2 * kdty{tag}    ; the duty drift carries DC with it")
    elif technique == "pulse":
        # the lexicon's OWN spec: "narrow rectangular wave (default 30% width)".
        # It emitted 0.5 -- i.e. a square, a byte-identical alias of `square`.
        L += _emit_vco_drift(tag)
        L.append(f"  kdty{tag}   poscil 0.012, 0.057          ; comparator threshold drift")
        L.append(f"  apl{tag}    vco2 0.6, kfreq * (1 + kvdr{tag}), 2, 0.30 + kdty{tag} ; narrow pulse (30%% width)")
        L.append(f"  {ov}    = apl{tag} + 0.2400         ; {_DC_NOTE}")
    elif technique == "clarinet":
        # A clarinet is a cylindrical pipe closed at the reed end. That boundary
        # condition is what produces the odd-harmonic spectrum -- the 2nd, 4th and
        # 6th are suppressed -- and the reed is a nonlinear valve whose harmonic
        # output GROWS with blowing pressure. Both facts are modelled here rather
        # than imitated with a filtered square.
        #
        # WGCLAR WAS TRIED AND REJECTED ON MEASUREMENT. Do not put it back without
        # reading this. Its pitch tracking is genuinely excellent (+-1 cent from
        # 110 to 1200 Hz, sustain 1.056), which is why it was adopted -- but the
        # probe that cleared it only tested 110/220/440/880 Hz, and the synth
        # clamps the played pitch to 20..12000 Hz. Across the range the instrument
        # actually allows, three separate faults:
        #
        #   * kstiff IS INERT. Rendered output is BIT-IDENTICAL (max difference
        #     exactly 0.000e+00) for kstiff at 0.05 / 0.10 / 0.26 / 0.35 / 0.90 /
        #     1.50, and for a k-rate LFO of +-0.80 on it. Every other argument does
        #     change the output, so this is the opcode, not the harness. The
        #     embouchure motion written against it was dead code, and the key
        #     measured 4.1% centroid travel against a 1.2% measurement floor -- a
        #     standing tone, in an instrument whose fundamental is that sounds move.
        #   * IT DIES ABOVE sr/10.4 and emits almost pure DC instead of a note
        #     (4613 Hz at 48 kHz, 4238 Hz at 44.1 kHz). Because the threshold
        #     follows the sample rate, the same patch and note is audible at 96 kHz
        #     and a silent DC step at 44.1 kHz. Reachable from ordinary notes: at
        #     the 4' register, anything from C7 up.
        #   * BELOW 25 Hz IT IS CATASTROPHICALLY MISTUNED AT FULL LEVEL -- 20 Hz
        #     renders 100.3 Hz (+2792 cents), 24.5 Hz renders 1291 Hz (+6864
        #     cents) -- because iminfreq cannot be supplied here (kfreq arrives
        #     through a chnget on a string channel, which does not resolve at
        #     i-time) so the delay line is sized for the 50 Hz fallback. It also
        #     logs "No base frequency for clarinet" 16 times, once per voice, on
        #     every compile.
        #
        # That is the same class of failure that disqualified wgflute (+1945
        # cents), just at the ends of the range instead of the middle.
        #
        # So: FM at a 1:2 ratio, which produces exactly the odd series a
        # cylindrical bore does, with the index standing in for the reed's
        # nonlinearity -- harder blowing, more harmonics, which is the real
        # behaviour and is what makes a clarinet expressive. 1:2 also cannot put a
        # sideband on DC (no whole-number kcar/kmod), the fault that had to be
        # corrected in fm_ep and theremin.
        #
        # foscili is NOT bandlimited, so the index has to be tapered where the
        # sidebands run out of room, or the bore aliases instead of playing. The
        # 3rd harmonic crosses Nyquist at kfreq = sr/6, and above that the folded
        # partials land on a lower odd series: at 9 kHz on a 48 kHz orchestra the
        # set 3000/9000/15000/21000 reads as a clarinet an octave and a fifth
        # BELOW the note asked for, and at 12 kHz on 44.1 kHz the loudest partial
        # is 8100 Hz with the asked pitch 9.2 dB down. That threshold follows the
        # sample rate, which is precisely the fault wgclar is rejected for above;
        # an earlier version of this comment claimed the branch "works at every
        # pitch the synth can ask for", and that was false.
        #
        # kndmax is the largest index whose highest significant sideband still
        # fits: sidebands reach about kfreq*(1 + 2*(index+1)), so solving against
        # 0.45*sr gives the expression below. Below sr/6 it exceeds the musical
        # index and nothing changes; above it the reed closes down and the tone
        # degrades to a sine, which is the correct limit -- a bore that small has
        # no harmonics to give. It does mean the breath motion fades out up
        # there, which is honest: at 12 kHz there is no room for it to move in.
        L.append(f"  kbrc{tag}   poscil 0.5, 0.19             ; breath pressure, slow")
        L.append(f"  kndmx{tag}  limit (sr * 0.45 / kfreq - 1) / 2 - 1, 0, 40 ; sideband room")
        # Scaled into the room rather than clipped to it, for the reason spelled
        # out at the bell: `min(breath, cap)` kills the breath outright once the
        # cap drops under its floor. It did -- the cap first binds at 0.0769*sr
        # (3692 Hz at 48 kHz, i.e. sr/13.0), NOT at the sr/6 = 8000 Hz an earlier
        # version of this comment claimed, because the reed index only spans
        # 0.875..1.425 and the cap was sized against a wider range. Breath travel
        # was fully dead from 4547 Hz. Scaling keeps it alive at every pitch that
        # has room, shallower as the room shrinks.
        L.append(f"  kntp{tag}   min 1.425, kndmx{tag}        ; brightest the band allows")
        L.append(f"  kndc{tag}   = kntp{tag} * (0.807 + 0.386 * kbrc{tag}) ; the reed opens the series")
        L.append(f"  {ov}    foscili 0.55, kfreq, 1, 2, kndc{tag}, giSine ; cylindrical bore")
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
        L += _emit_vco_drift(tag)
        L.append(f"  {ov}    vco2 0.6, kfreq * (1 + kvdr{tag}), 12 ; band-limited triangle")
    elif technique == "saw":
        L += _emit_vco_drift(tag)
        L.append(f"  {ov}    vco2 0.6, kfreq * (1 + kvdr{tag}), 0  ; band-limited sawtooth")
    elif technique == "supersaw":
        # 7 detuned saws (the JP-8000 idiom). The lexicon says the wavetable path
        # had to fake this because "the detune itself is not a cycle property" --
        # on a live substrate the detune is simply real, and the beating between
        # the copies is movement that arises from the structure itself.
        L += _emit_vco_drift(tag)
        ratios = (1.0, 1.0035, 0.9965, 1.0071, 0.9929, 1.0110, 0.9890)
        for i, r in enumerate(ratios):
            L.append(f"  asu{tag}x{i}  vco2 0.6, kfreq * {r:.4f} * (1 + kvdr{tag}), 0")
        stack = " + ".join(f"asu{tag}x{i}" for i in range(len(ratios)))
        L.append(f"  {ov}    = ({stack}) * 0.17   ; 7-saw detuned stack")
    elif technique == "sync":
        # REAL hard sync. The lexicon admits the wavetable stand-in ("bright,
        # dense FM stands in for hard sync's sideband-rich character"); Csound
        # has the actual thing: a slave saw phase-RESET by the master period.
        # The slave ratio SWEEPS, because a static sync ratio is just a fixed
        # formant -- the sweep is what makes sync recognisable, exactly as the
        # moving duty is what makes pwm recognisable.
        L += _emit_vco_drift(tag)
        L.append(f"  kswp{tag}   oscili 1.1, 0.16            ; slave-ratio sweep")
        L.append(f"  krat{tag}   = 2.6 + kswp{tag}            ; 1.5 .. 3.7 x master")
        L.append(f"  azro{tag}   = 0")
        L.append(f"  amst{tag}, asyn{tag}  syncphasor kfreq * (1 + kvdr{tag}), azro{tag}")
        L.append(f"  aslv{tag}, adum{tag}  syncphasor kfreq * krat{tag}, asyn{tag}")
        L.append(f"  {ov}    = (aslv{tag} * 2 - 1) * 0.45  ; hard-synced saw")
    elif technique == "brass":
        # The lexicon asks for "opens from a dark 6-harmonic set to a brighter
        # 12-harmonic set", and this used to do it with a resonant lowpass on a
        # saw. That is backwards about the instrument. A trumpet does not get
        # bright because something UNCOVERS harmonics that were always there: at
        # high playing pressure the wave travelling down the bore STEEPENS towards
        # a shock, and the upper harmonics are MADE by that nonlinearity. It is why
        # brass is quiet-and-round but loud-and-blazing, and why the change is so
        # much more dramatic than any filter.
        #
        # `gbuzz` generates the series rather than filtering one: kmul is the ratio
        # between successive harmonics, so breath pressure driving kmul from 0.30
        # to 0.72 genuinely adds upper partials. No filter anywhere in this branch.
        #
        # The BELL FORMANT is fixed in Hz and does NOT track the played pitch --
        # that is the point of it. A trumpet's bell radiates a broad peak around
        # 1.2 kHz wherever the note sits, so the timbre changes character across
        # the register: the same instrument is dark low and piercing high. A
        # pitch-tracking resonance would have made every note sound identical,
        # which is the single most synthetic thing about filtered-saw brass.
        L.append(f"  kbp{tag}    poscil 0.5, 0.22             ; breath pressure, slow")
        L.append(f"  kbm{tag}    = 0.51 + 0.21 * kbp{tag}       ; pressure steepens the wave")
        L.append(f"  klw{tag}    poscil 0.0006, 0.083         ; lip instability, own rate")
        L.append(f"  abz{tag}    gbuzz 0.5, kfreq * (1 + klw{tag}), 12, 1, kbm{tag}, giCos ; lip + bore")
        L.append(f"  abl{tag}    reson abz{tag}, 1200, 900, 1   ; bell formant, FIXED in Hz")
        L.append(f"  {ov}    = abz{tag} * 0.95 + abl{tag} * 0.40")
    elif technique == "strings":
        # A string section is PLAYERS. The old version had the ensemble half right
        # -- three detuned saws -- but then put ONE slow filter over the sum, so
        # every player brightened and darkened in perfect unison. Real desks do not
        # breathe together; that is the whole difference between a section and a
        # chorus effect on one violin.
        #
        # So each player gets two processes of their own, at rates that share no
        # common factor with anyone else's: an INTONATION drift (nobody holds a
        # pitch exactly) and a BOW PRESSURE that opens and closes their own
        # harmonic series (nobody bows at a constant weight). Six independent
        # variables where there used to be one, and no partial is driven by
        # anything another partial is driven by.
        #
        # A saw whose CORNER is rounded, not a cosine stack -- and unlike brass,
        # that is the physics rather than a convenience. Brass gets brighter when
        # blown harder because the bore steepens the wave nonlinearly: those
        # harmonics are genuinely MADE, which is what gbuzz's kmul does. A bowed
        # string instead has its full Helmholtz sawtooth already, and bow force
        # changes how sharply the slip-stick corner is ROUNDED (finite bow width,
        # string bending stiffness) -- a first-order lowpass, which is `tone`.
        #
        # Measured, the cosine stack was also lying about the ensemble: three
        # detuned gbuzz players peaked at EXACTLY 3x one player's amplitude
        # (0.4500 from 3 x 0.15) at 110/220/440 Hz, because every harmonic sits at
        # cosine phase and a cosine stack is a pulse train -- so all three pulses
        # land together however far apart they are tuned. Crest 4.85 against a
        # rounded saw's 3.14, i.e. 3.3 dB of headroom spent on a spike that the
        # comment above claimed could not exist.
        # How many harmonics FIT below Nyquist, and a corner centre that never
        # asks for more than that. A fixed 7th-harmonic centre stops meaning
        # anything once 5.25*kfreq clears Nyquist, and the clamp then pins the
        # whole excursion -- a dead control, which is what this had. Capping the
        # centre instead keeps the sweep inside the representable band at every
        # pitch, so bow pressure still moves the balance of the few harmonics
        # that remain up there. Below ~2 kHz the cap never binds and the centre
        # is the authored 7th harmonic exactly.
        L.append(f"  krm{tag}    = sr * 0.47 / kfreq        ; harmonics that fit")
        L.append(f"  kctr{tag}   limit krm{tag} * 0.62, 1.6, 7 ; corner centre, capped")
        players = ((1.0000, 0.061, 0.089), (1.0042, 0.073, 0.107),
                   (0.9958, 0.047, 0.127))
        for i, (det, rd, rb) in enumerate(players):
            L.append(f"  ksd{tag}x{i}  poscil 0.0009, {rd}         ; player {i+1} intonation")
            L.append(f"  ksb{tag}x{i}  poscil 0.35, {rb}           ; player {i+1} bow pressure")
            L.append(f"  asr{tag}x{i}  vco2 0.30, kfreq * {det:.4f} * (1 + ksd{tag}x{i}), 0")
            # The clamp has to sit at the edges of what is REPRESENTABLE, not at
            # a round number, or it silently eats the control. With a 200..14000
            # clamp the multiplier's own range (7 +- 1.75 = 5.25..8.75) pinned
            # BOTH ends -- inert below 22.86 Hz and above 2666.67 Hz, which is
            # E7 at 8', ordinary playing range, and 25.6% of the 20-12000 clamp
            # in total. Bit-identical output with the LFO nulled, exactly
            # 0.000e+00: the same dead-control defect this file removed wgclar
            # for, reintroduced in the same commit that removed it.
            L.append(f"  kbc{tag}x{i}  limit kfreq * kctr{tag} * (1 + 0.7 * ksb{tag}x{i}), "
                     f"50, sr * 0.47 ; bow sharpens the corner")
            L.append(f"  ase{tag}x{i}  tone asr{tag}x{i}, kbc{tag}x{i}")
        L.append(f"  aens{tag}   = ase{tag}x0 + ase{tag}x1 + ase{tag}x2 ; the desk")
        # The instrument's BOX, fixed in Hz like the brass bell: a violin body's
        # main resonances sit near 460 Hz whatever note is stopped, which is why a
        # violin has a register character at all.
        L.append(f"  abd{tag}    reson aens{tag}, 460, 380, 1 ; body resonance, FIXED")
        L.append(f"  {ov}    = aens{tag} * 0.70 + abd{tag} * 0.24")
    elif technique == "bass_saw":
        # the lexicon's own spec: "harmonic count ceilinged near 24 for a
        # rolled-off low end" -- a literal 24th-harmonic ceiling the code never had.
        # The ceiling alone measured only 0.042 from a plain saw (a saw's energy
        # above its 24th harmonic is tiny), so the key stayed an alias in the ear.
        # What makes it a BASS saw is the weight below: a sub octave under the
        # rolled-off saw. Both halves of the name now exist in the code.
        L += _emit_vco_drift(tag)
        L.append(f"  abs{tag}    vco2 0.6, kfreq * (1 + kvdr{tag}), 0 ; saw source")
        L.append(f"  kcut{tag}   limit kfreq * 24, 100, 15000 ; 24-harmonic ceiling")
        L.append(f"  adk{tag}    butterlp abs{tag}, kcut{tag}")
        L.append(f"  asb{tag}    oscili 0.30, kfreq * 0.5    ; sub-octave weight")
        L.append(f"  {ov}    = adk{tag} * 0.72 + asb{tag}")
    elif technique == "fm_ep":
        return _emit_fm_ep(tag, (params or {}).get("fm_ep"))
    elif technique in ("fm_bell", "fm", "metallic_fm"):
        # FM via foscili: an inharmonic-ish carrier:modulator ratio gives the
        # bell/metal sideband spectrum natively (no partial table). metallic_fm
        # is a HARSHER CLANG than fm_bell — a wider inharmonic ratio and a much
        # deeper index spread the sidebands into dissonant metal (they were
        # byte-identical before, a degenerate duplicate; BJ ear-finding
        # 2026-07-18).
        # A STRUCK BELL IS NEVER STILL, and these three were: measured 0.0-0.2%
        # spectral-centroid travel, literal standing tones, which is the "high
        # whistling oscillator instead of real metallic sounds" finding against
        # this family. The modal keys (cymbal/glass/struck_bar) were given real
        # life; these kept a constant index and were left behind.
        #
        # Two things make struck metal sound like metal, and both are spectrum,
        # not amplitude -- the synth still owns the envelope:
        #
        #   * The partials decay at DIFFERENT rates. High partials die first, so
        #     the tone DARKENS as it rings. In FM that is the index falling, and
        #     it is the same k-rate timbre shaper fm_ep already uses (softens,
        #     then HOLDS -- the tone never dies on its own).
        #   * Casting asymmetry splits each mode into a DOUBLET a few cents
        #     apart, and the pair beats. That warble is what separates a bell
        #     from a sine; a perfectly symmetric bell would not sound like one.
        #     The two halves also ring with slightly different brightness, so the
        #     twin gets its own slightly lower index.
        #
        # Per-note counter on the trig epoch rather than a reinit label, for the
        # reason spelled out in fm_ep: this can be emitted inside the crossfade
        # path's `if <gain> > 0` blocks, where a label and its reinit would sit
        # inside a conditional.
        car, mod, dtw, i0, i1, tau, a1, a2, aref = {
            "fm_bell":     ("1", "1.41", 1.1, 6.0, 1.45, 2.5, 0.40, 0.26, 0.46),
            "metallic_fm": ("1", "2.41", 1.3, 9.0, 3.00, 3.0, 0.37, 0.24, 0.44),
        }.get(technique, ("1", "2", 1.2, 4.0, 1.80, 2.0, 0.40, 0.26, 0.46))
        L.append(f"  ktm{tag}    init 0")
        L.append(f"  if changed2(ktrig) == 1 then")
        L.append(f"    ktm{tag}   = 0")
        L.append(f"  endif")
        L.append(f"  ktm{tag}    = ktm{tag} + 1/kr           ; seconds since this strike")
        # foscili is not bandlimited and these indices are large, so the same cap
        # the clarinet needs applies here: sidebands reach about
        # kfreq*(car + mod*(index+1)), and past Nyquist they fold back onto a
        # wrong pitch instead of ringing. Below the cap nothing changes.
        L.append(f"  kbmx{tag}   limit (sr * 0.45 / kfreq - 1) / {mod} - 1, 0, 40 ; sideband room")
        # SCALE the ring into the room, never CLIP it to the room. `min(ramp,
        # cap)` looks equivalent and is not: as soon as the cap falls below the
        # ramp's floor it flattens the whole thing, and the key becomes the exact
        # standing tone this branch exists to remove. metallic_fm froze from
        # 2030 Hz -- A6, ordinary playing range -- and measured 0.07-0.21%
        # travel, WORSE than the 0.0-0.2% it replaced, with the threshold
        # following the sample rate like every other bug in this file's history.
        # Scaling keeps the same proportional darkening at every pitch that has
        # any room at all, and only genuinely runs out when the room does.
        L.append(f"  kbtp{tag}   min {i0}, kbmx{tag}          ; brightest the band allows")
        L.append(f"  kbnx{tag}   = kbtp{tag} * ({i1 / i0:.4f} + {1 - i1 / i0:.4f} * "
                 f"(1 - min(ktm{tag} / {tau}, 1))) ; darkens as it rings")
        L.append(f"  abl{tag}    foscili {a1}, kfreq, {car}, {mod}, kbnx{tag}, giSine ; the bell")
        # The twin is the SAME ratio pair started a fixed few Hz above the
        # fundamental. Three doublet schemes have been tried; the first two each
        # failed in a documented, OPPOSITE way:
        #
        #   * PROPORTIONAL carrier detune (kfreq * (1+eps)): the pairs' nulls
        #     coincide, so the doublet does not warble, it TREMOLOS -- measured
        #     17-19 dB of rms swing, an amplitude envelope generated inside the
        #     oscillator, which the oscillator must not own.
        #   * TWO MODULATOR RATIOS (1.41 vs 1.46): kills the tremolo, but pair n
        #     then beats at n*|mod2-mod|*kfreq -- PROPORTIONAL TO PITCH. Measured
        #     on the emitted orchestras: 11 Hz at 220 -> 44 Hz at 880 (fm_bell),
        #     35 -> 70 (metallic_fm), 15 -> 62 (fm). From mid-keyboard up that is
        #     the 20-70 Hz psychoacoustic ROUGHNESS band -- the BJ ear-finding of
        #     2026-07-19: "alle glocken, metal, chimes sind ein ALBTRAUM mit
        #     nichts als hochfrequentem BEATING".
        #
        # A real bell's casting asymmetry splits each mode by a few Hz -- an
        # OFFSET, not a factor. So: same ratio, carrier {dtw} Hz high. Sideband n
        # of the twin then sits dtw*(1 + n*mod) Hz above its partner: fixed in Hz
        # at EVERY pitch (warble, never roughness), and DIFFERENT for every pair,
        # so the nulls never line up and no coherent tremolo can form.
        L.append(f"  abt{tag}    foscili {a2}, kfreq + {dtw}, {car}, {mod}, kbnx{tag} * 0.94, giSine ; its doublet")
        L.append(f"  abs{tag}    = abl{tag} + abt{tag}       ; the pair beats")
        # A falling FM index moves energy BETWEEN the carrier and its sidebands
        # (J0 rises as the index drops), and the sidebands that sit above Nyquist
        # are simply gone, so the audible level drifts over the ring. That is an
        # amplitude shape owned by the oscillator, the category this branch is
        # not allowed to touch, and it needs removing however it points.
        #
        # `balance` against a steady reference is this project's existing answer
        # to exactly that (spectral movement without loudness pumping, as used by
        # the motion waveshaper). It holds the level while the spectrum travels,
        # and as a side effect the level stops drifting with pitch.
        #
        # MEASUREMENT METHOD, stated because without it these numbers are not
        # reproducible and were twice wrong: rms of the first whole beat period
        # after the strike against rms of the last. The slowest beat component
        # is the carrier pair at dtw Hz, so the window is rounded UP to whole
        # multiples of 1/dtw (>=250 ms). The rounding is the point: a window cut
        # anywhere else samples beat PHASE and reports it as level, which is how
        # two earlier sets of committed figures came out unreproducible (~0.6 dB
        # of apparent level was window choice). Epoch caveat: measured from
        # score start; the shipped `instr 1` is always on, so a note struck
        # later meets a settled follower (the Onset paragraph below).
        #
        # Strike to rest with balance, at 20 / 55 / 220 Hz -- fixed-Hz doublet,
        # so unlike the old pitch-proportional law these no longer change with
        # pitch:
        #   fm_bell      -0.25 / -0.28 / -0.26
        #   metallic_fm  -0.03 / +0.14 / +0.20
        #   fm           -0.20 / -0.22 / -0.23
        # All residuals <=0.28 dB. They are larger than the <=0.13 dB the
        # two-ratio scheme measured because the same-position sideband pairs
        # interfere at full depth, and the first window necessarily starts at an
        # arbitrary phase of the 2.7-7.6 Hz pair beats riding inside it.
        #
        # ihp 1, NOT the default 10, and the difference is the whole doublet.
        # `balance` tracks rms through a low-pass at ihp, so it cancels every
        # amplitude motion SLOWER than that -- and the beat IS an amplitude
        # motion, now at dtw*(1 + n*mod) Hz: 1.1-7.6 Hz for these three keys, at
        # EVERY pitch. The default follower would therefore sit on top of the
        # whole doublet everywhere (under the old pitch-proportional law it only
        # did below ~200 Hz): measured at 220 Hz against the same code with the
        # balance line removed, ihp 1 retains 80/93/96% of the swing
        # (fm_bell/metallic_fm/fm) where ihp 10 keeps 40/73/42%. A fix for an
        # amplitude artifact must not delete the amplitude FEATURE tuned two
        # paragraphs above -- the same shape of loss as the regression the
        # migration section of CLAUDE.md is about.
        #
        # The two are separable because they live on different timescales: the
        # darkening runs over tau (2-3 s, i.e. 0.05-0.08 Hz), the slowest beat
        # at dtw (1.1 / 1.3 / 1.2 Hz for fm_bell / metallic_fm / fm) -- and both
        # are fixed in Hz, so the separation no longer depends on pitch.
        # fm_bell is the MARGINAL key: dtw = 1.1 sits closest above the ihp 1
        # cutoff, which is why its retention (80%) is the worst of the three.
        # Sample-rate independent, since both the beat rate and ihp are
        # specified in Hz -- verified at 44.1 k.
        #
        # It also stops the follower boosting the beat nulls. Bounds under the
        # fixed-Hz doublet are re-verified by the keys gate's loudest-params
        # sweep (vel=pres=1): peaks 0.32 / 0.28 / 0.29 for fm_bell /
        # metallic_fm / fm against the 0.8075 knee. Two figures on purpose --
        # the third decimal of a peak sweep is method (window, chain), not
        # signal; an earlier pair of independent runs of the three-layer
        # variant read 0.396 and 0.412 and taught exactly that.
        #
        # Onset: the oscillator runs continuously inside the always-on `instr 1`,
        # so on the STEADY path the follower is long settled before any gate
        # opens. That is not true on the crossfade path, where this body is
        # emitted inside `if <gain> > 0` and a stage's gain is exactly zero for
        # its whole first leg -- the follower does not run, then restarts with a
        # ~0.16 s time constant instead of ~0.016 s. Measured on that path, the
        # worst extra dip against ihp 10 is 0.77 dB over ~150 ms, under a tent
        # that is itself still below unity. At a retrigger, fm_bell dips -4.08 dB
        # against -3.37 at 20 Hz and -2.67 against -2.21 at 55 Hz, and `fm` is
        # the worst at -6.91 against -5.03; fm_bell at 20 Hz is still 0.56 dB
        # down at 200-400 ms. Smooth, bounded, and under the synth's own attack.
        L.append(f"  abr{tag}    poscil {aref}, kfreq        ; steady level reference")
        L.append(f"  {ov}    balance abs{tag}, abr{tag}, 1 ; loudness held, beat kept")
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


def _emit_crossfade_morph(chain, imorphtime, tag="0", nmodes=None, params=None):
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
        # strike_gate=gj: a struck stage re-strikes when ITS leg opens, not only
        # at note-on -- see _emit_struck. Harmless for every other key (ignored).
        body = _emit_noise(k, sub) if k in _NOISE_TECH else _emit_steady(k, sub, nmodes, params, gj)
        L.append("\n".join("  " + ln for ln in body.splitlines()))
        L.append("  endif")
        terms.append(f"{var} * {gj}")
    L.append(f"  aosc{tag}    = " + (" + ".join(terms) if terms else "0"))
    return "\n".join(L)


def _emit_oscillator(oi, chain, imorphtime, nmodes=None, params=None):
    """One oscillator (index `oi`, 0..2) from its technique chain -> (body_lines,
    out_var). >=2 stages -> a morph, and there are now exactly TWO paths:
      - a PURE vowel sweep (>=2 voice stages, no silence) -> _emit_voice_morph, the
        native formant glide ('ah'->'ee'), which is a real filter sweep;
      - everything else -> _emit_crossfade_morph, which renders each stage with
        its OWN idiom and equal-power crossfades between them.
    A single stage -> a steady technique. The out_var is `aosc<oi>`.

    There used to be a third path, `_emit_morph`, which interpolated two static
    partial banks. It is deleted, and the routing condition that steered keys
    around it is deleted with it -- that condition was the bug, not the fix.

    WHAT IT COST, because this is the whole reason the code now looks like this:
    a chain that failed all five terms of the old condition was silently
    implemented as a bank of `oscili`. Six keys still fell through -- saw,
    square, triangle, pulse, bass_saw, sine, i.e. the basic waveforms, the
    most-played sounds in the instrument. Measured on `saw > sine` at 220 Hz:
    the saw alone is `vco2` with 59 partials above -40 dB; through the morph it
    was 6 partials and no `vco2` at all, with the harmonic amplitudes already
    ramped down (H2 at 0.131 where a saw wants 0.496), so within a second of
    the note it was an ordinary sine. BJ's report was "saw > sine does not even
    work", and that is what it was.

    The history matters more than the diff. Additive banks were ordered removed
    COMPLETELY. The single-key path was converted (a63dd944); when it emerged
    that the morph turned those same keys back into `oscili` banks, the response
    (5e7a8d33) added routing so the noticed keys went around the additive path
    and left the path standing. The remainder became a backlog note ("morph path
    loses its spectra -- needs ftmorf") and then the liveness gate encoded it as
    CORRECT ("a saw reduced to a static spectrum is still a saw"), so the
    unexecuted half of the order was certified green. An order routed around is
    an order not carried out; the only safe form is deleting the mechanism, so
    that no future condition can reach it.

    The audible consequence, stated plainly: a tonal morph is now a crossfade,
    so mid-morph BOTH endpoints are briefly heard rather than one spectrum
    turning into the other. That is a real difference. The additive path never
    delivered the interpolation it promised -- it delivered six partials."""
    tag = str(oi)
    body = None
    if len(chain) >= 2:
        real = [k for k in chain if k not in ("silence", "zero")]
        has_silence = any(k in ("silence", "zero") for k in chain)
        if real and all(k in _VOICE_TECH for k in real) and len(real) >= 2 and not has_silence:
            body = _emit_voice_morph(chain, imorphtime, tag)   # pure vowel sweep
        else:
            body = _emit_crossfade_morph(chain, imorphtime, tag, nmodes, params)
    if body is None:
        body = _emit_steady(chain[0] if chain else "sine", tag, nmodes, params)
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


# Techniques whose spectrum is SPARSE enough to have no partial PAIRS worth
# speaking of: at most a fundamental plus one or two weak partials. These are
# exactly the sources for which the waveshaper below is both necessary AND
# harmless (see _emit_motion's docstring for the measurement that splits the two
# families).
_SPARSE_TECH = {"sine", "sub_sine", "theremin", "flute", "triangle"}


# The motion words this post-mix layer answers for. Named once because two call
# sites must agree exactly: the emitter that renders them and the response
# builder that flags the ones it cannot render. They drifting apart is how a word
# gets silently swallowed.
_POSTMIX_SPECTRAL_MOTION = frozenset({
    "sweep", "evolve", "open_up", "close", "breathe", "wobble",
    "cycle", "slow", "fast", "snap", "pingpong", "settle",
})


def _sparse_patch(oscs):
    """True iff EVERY sounding stage of the patch is sparse -- the only case in
    which the post-mix waveshaper may run. One dense stage anywhere disqualifies
    the whole patch, because the operator sits on the summed mix and would
    intermodulate that stage's partial pairs. An empty patch is not sparse: with
    no stage to reason about, the safe answer is the one that adds nothing."""
    stages = [k for o in (oscs or []) for k in (o.get("chain") or [])
              if k not in ("silence", "zero")]
    return bool(stages) and all(k in _SPARSE_TECH for k in stages)


def _emit_motion(motion_key, oscs=None):
    """A k-rate motion so 'moving' prompts actually move (movement by default).

    NO FILTER LIVES HERE. BJ, 2026-07-19, on hearing what `analog saw 16" + sine
    4"` had become: "ein Filtersweep ist keine analoge Welle. Ein Filtersweep hat
    null zu suchen, es sei denn, der ist beauftragt oder es gibt interne Gruende
    dafuer, denn dieser Synthesizer hat einen Filter." A pitch-tracked resonant
    lowpass used to sit on the finished mix here, sweeping x1.8..x26 the
    fundamental at 0.16 Hz; on that patch it periodically shut the 4' sine away
    entirely, and it read to the player as the word "analog" having been
    translated into a filter sweep. It is deleted, not made quieter.

    It is also the wrong LAYER, which is why no replacement filter belongs here.
    A filter imposes ONE shared frequency-dependent envelope on a summed mix. A
    dense or inharmonic sound does not evolve that way in the world: its partials
    change their relative weights at DIFFERENT rates, which cannot be expressed
    post-mix at all -- only inside the emitter that still knows which partial is
    which. So for those sources the motion belongs to the key idiom (the FM
    index darkening as it rings, the modal bank's stochastic exciter, the organ
    ranks wandering against each other), never to a post-mix operator.

    What remains here is the one operator that is honest post-mix:

      * A WAVESHAPER (tanh at an LFO-swept drive) MAKES harmonics, so it moves
        even a bare sine -- which is why it exists: the frozen NL corpus caught
        'evolving analog drone', 'wobbling acid bass' and 'a bass that slowly
        opens up' all rendering static, because the 7B had routed them onto
        sine/sub_sine and there was nothing above a cutoff to remove.
        It is confined to _SPARSE_TECH patches, and that confinement is load
        bearing: on a DENSE source it intermodulates every partial PAIR, and on
        an INHARMONIC one those products land off every grid. Measured in the
        built Standalone on `a slowly evolving bell` (fm_bell, c:m 1:1.41, index
        3.2) against the same patch with no motion -- identical source, motion
        the only difference:
            crest factor      1.57 constant   ->  1.13 .. 1.35   (waveform squared off)
            energy above 3kHz 0.036 .. 0.129  ->  0.086 .. 0.244
            partials          the FM grid     ->  + 1800, 2167, 3367, 3733, 4103 Hz
        2167 Hz is 2*1213 - 260, a textbook cubic intermodulation product; the
        3367/3733/4103 trio is the FM grid regenerated far above where the dry
        bell has any energy at all. This is BJ's "die Bells und Metals sind im
        Synth extrem verzerrt" (2026-07-19), and the reason it survived every
        check for days is the `balance` on the next line: loudness is held
        identical while the waveform is destroyed, so no peak/RMS gate sees it.

    A spectral-motion word on a NON-sparse patch therefore emits nothing from
    this layer. That is a real gap, not a silent one: build_csound_response
    flags the word so the player is told the movement did not render, instead of
    being handed a filter and told it is analogue.

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
    if motion_key in _POSTMIX_SPECTRAL_MOTION:
        # faster, shallower for the periodic wobble/cycle; slow & deep for the
        # directional-feel evolve/open family (still periodic = keeps living).
        rate = _SPECTRAL_RATE.get(motion_key, 0.16)
        if not _sparse_patch(oscs):
            # Dense / inharmonic: nothing honest to do post-mix (see docstring).
            # The word is flagged by build_csound_response, never dropped mute.
            return ""
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
    if motion_key in ("shimmer", "vibrate", "flutter", "tremolo"):
        return ("  ksh      oscili 0.18, 5.5\n"
                "  asig     = asig * (1 + ksh)           ; fast shimmer")
    return ""


def _normalize_oscs(technique_keys, oscs):
    """Resolve the two call conventions to a clean list of up to 3 oscillator
    specs [{chain:[keys], vol:float, register:float, params:{key:{name:val}}}].
    Back-compat: a bare technique_keys list is one oscillator at vol 1.0 and 8'
    (byte-for-byte the pre-M2 single-osc emission), with an empty params dict
    (byte-for-byte the pre-analog_osc emission for every technique but it). An
    oscillator whose chain is ONLY silence produces nothing and is dropped; if
    every osc drops, fall back to a single sine so the orchestra is never
    empty."""
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
        # params: {canonical_key: {name: value}} for a parametrised technique
        # in this chain (analog_osc today). Absent/malformed -> {} -- every
        # existing caller (technique_keys convention, or an oscs dict with no
        # "params" key at all) gets this, and _emit_analog_osc's own defaults
        # then apply, exactly as if nothing had been set.
        params = o.get("params")
        if not isinstance(params, dict):
            params = {}
        out.append({"chain": chain, "vol": max(0.0, min(1.0, vol)), "register": reg,
                    "params": params})
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
        out = [{"chain": ["sine"], "vol": 1.0, "register": 1.0, "params": {}}]
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
        body, outv = _emit_oscillator(oi, o["chain"], imorphtime, nmodes, o.get("params"))
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
        # STRIKE IMPULSE for the struck instruments (`vibes`, _emit_struck). Perry
        # Cook's models take their `imp` argument as a TABLE NUMBER, not a scalar:
        # hand them a number that is not a table and Csound raises "No table for
        # Marimba strike" and DELETES THE NOTE -- which renders as silence, reads
        # exactly like a model that decays on its own, and is what got this whole
        # family wrongly rejected (failure mode 9, docs/LCO_CONCEPT.md). A short
        # decaying harmonic burst is the mallet contact.
        "giImp  ftgen 4, 0, 256, 10, 1, 0.5, 0.3, 0.2, 0.1\n"
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
        "  kpresGain = 1.0 + 0.15 * kpres\n"
        # Seconds since THIS note, for any stage that shapes timbre over the
        # note. It lives here, in the per-voice preamble, because it is a
        # property of the NOTE and not of an oscillator stage: an emitter that
        # keeps its own counter has it placed inside _emit_crossfade_morph's
        # `if <tent gain> > 0` block, where it counts AUDIBLE seconds instead
        # of elapsed ones. A slow ramp survives that (the old fm_ep's 1.6s
        # softening against a 1.4s morph leg still reads as a softening), but a
        # struck transient does not: measured, fm_ep's 86ms tine peaked at
        # 3692Hz on its own and only 1470Hz as `sine > fm_ep`, i.e. the attack
        # never reached the output at full gain at all.
        # init 0 rather than a settled value: a note whose gate and trig are
        # already high before the first k-cycle gives changed2 no edge to fire
        # on, so a counter starting anywhere else would sit frozen forever.
        "  knote    init 0\n"
        "  if changed2(ktrig) == 1 then\n"
        "    knote  = 0\n"
        "  endif\n"
        "  knote    = knote + 1/kr\n\n"
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


def build_csound_response(text, llm, correction="", previous=""):
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
        # The `wave` convention (§4, BJ 2026-07-20): a key whose bare words now
        # name a REAL INSTRUMENT keeps them only with `wave` appended. Rewritten
        # on the per-request copy, entry by entry, so backend/dco_lexicon.json --
        # which lco_author.py also reads, and which has no instrument reading to
        # disambiguate against -- is never touched. Only surface_forms change; the
        # key, its `why` and its emitter are untouched, so nothing becomes
        # unreachable, it just answers to `... wave` instead.
        lex_cs["techniques"] = [
            (dict(e, surface_forms=_wave_forms(e),
                  why=_CS_WHY_OVERRIDE.get(e["key"], e.get("why", "")))
             if (e.get("key") in _CS_WAVE_HANDOVER or e.get("key") in _CS_WAVE_ALSO
                 or e.get("key") in _CS_WHY_OVERRIDE) else e)
            for e in lexicon["techniques"]
        ] + [
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
        # The correction rides in the USER turn, and `text` stays the untouched
        # prompt: every intent guard below (_prompt_wants_decay, _prompt_names_
        # register, _prompt_wants_sustain, _prompt_wants_still) reads the PROMPT to
        # decide what the user asked for. Feeding them the augmented turn would let
        # a listener's word ("the sound is described as bright") count as if the
        # user had written it -- the guards would start honouring the machine's
        # complaint as intent.
        user_turn = text
        if correction:
            system_prompt += _CS_CORRECTION_RULE
            # `previous` is the last pass's own reading. Without it the brief's
            # "keep what the listener did not name" is unfollowable -- each call is
            # fresh, so the model has never seen the patch it is being asked to
            # repair, and it guesses by deleting. Measured: without this line the
            # 7B answered "church organ" + "described as bright" by dropping the
            # rich/deep it had just chosen, which moves the sound the wrong way.
            user_turn = text
            if previous:
                user_turn += "\n\nYOU WROTE: " + str(previous).strip()
            user_turn += "\nA LISTENER HEARD: " + str(correction).strip()
        raw = llm(user_turn, system_prompt, _CS_MAX_NEW_TOKENS)

        osc_specs, adjectives_raw, motion_raw, lost_regs = _parse_csound_reply(raw)

        # DECAY-INTENT guard: the deterministic layer owns the (pseudo-)envelope, so a
        # routed morph-to-zero is honoured ONLY when the prompt actually asks the sound
        # to fade/decay/stop. Otherwise a trailing terminal-silence is an unmotivated
        # 7B artefact (intermittent, MPS non-determinism) and is stripped below, so a
        # held STANDING sound (pad/drone/bed) never decays on its own.
        wants_decay = _prompt_wants_decay(text)

        # SUSTAIN-INTENT guard, the mirror of the line above for the struck
        # instruments (rhodes/wurlitzer/vibraphone), which bring their own decay.
        # See _prompt_wants_sustain and _STRUCK_SUSTAIN_COUNTERPART.
        wants_sustain = _prompt_wants_sustain(text)

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
            keys, kflags, stage_params = _validate_osc_chain(chain_raw, tcanon, dco_llm_map)
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
            # SUSTAIN-INTENT guard: a struck instrument rings and dies, so a prompt
            # that asked for a sound which STANDS (pad, drone, held) gets the
            # continuously-excited member of the same family instead. The prompt
            # wins over the convention's bare-name rule for the same reason the
            # decay guard above wins over the model: the deterministic layer owns
            # the envelope. Without this, "a warm electric piano pad" is silent
            # after 1.5 s and nothing anywhere says why.
            if wants_sustain:
                for _ki, _k in enumerate(keys):
                    _sub = _STRUCK_SUSTAIN_COUNTERPART.get(_k)
                    if _sub is None:
                        continue
                    keys[_ki] = _sub
                    flags.append({
                        "word": f"OSC{oi}: {_k}",
                        "reason": f"{_k} rings and fades by itself, but the prompt "
                                  f"asks for a sound that holds — using {_sub}, the "
                                  f"standing-tone version of the same instrument. "
                                  f"Write \"{_k}\" without a pad/drone word to get "
                                  f"the real one.",
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
                # A chain that named SEVERAL registers keeps only the first: the
                # register is one player knob per layer, so the rest have nothing
                # to attach to. Say so, rather than letting the card read exactly
                # as it would for a layer that asked for one register all along.
                _lost = lost_regs[oi - 1] if oi - 1 < len(lost_regs) else None
                if _lost and names_register:
                    flags.append({
                        "word": f"OSC{oi}: {' > '.join(_lost)}",
                        "reason": "a layer has ONE register, so the whole chain "
                                  f"plays at {_lost[0]}; a morph that transposes "
                                  "between its stages does not exist yet",
                        "tier": "adapted",
                    })
                if reg and reg != 1.0 and not names_register:
                    flags.append({
                        "word": f"OSC{oi}: {_FOOTAGE_LABEL.get(reg, '?')}'",
                        "reason": "the prompt names no register, so the layer plays "
                                  "at the pitch you hold and the octave stays yours",
                        "tier": "adapted",
                    })
                oscs.append({"chain": keys, "vol": v, "register": r, "params": stage_params})

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
        #
        # Gated on _sparse_patch since 2026-07-19: the dense/inharmonic branch of
        # _emit_motion was a post-mix resonant filter sweep, and BJ ordered it out
        # ("ein Filtersweep hat null zu suchen ... dieser Synthesizer hat einen
        # Filter"). Without it this default had nothing to render on a dense patch,
        # so firing anyway would have printed "so it breathes" over a sound that
        # does not -- the flag would have become the lie. It now fires only where
        # it renders.
        if (not motion_key or motion_key == "static") \
                and _sparse_patch(oscs) \
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
        # An ASKED-FOR spectral motion that this layer cannot render is reported,
        # never dropped mute: the player hears no movement and must be told why,
        # rather than being handed a filter and told it is the sound's own life.
        elif motion_key in _POSTMIX_SPECTRAL_MOTION and not _sparse_patch(oscs):
            flags.append({
                "word": motion_key,
                "reason": "this source's partials would have to move at different "
                          "rates, which cannot be done to a finished mix -- a filter "
                          "sweep over the whole sound is not that, and is the synth's "
                          "own filter to apply if you want one",
                "tier": "dropped",
            })
            motion_key = None

        orchestra, reading = build_orchestra(oscs=oscs, adjective_keys=adjective_keys,
                                             motion_key=motion_key)
        # echo the oscillators as ACTUALLY rendered (post terminal-silence strip,
        # vol clamp, muted-drop / unity-promotion) so the metadata never lies about
        # what the orchestra plays.
        rendered = _normalize_oscs(None, oscs)

        # The panel's parametrisation surface (BJ, 2026-07-19): NOT the raw Csound
        # source -- the catalogue entry (why + knob anchor words) each ACTUALLY
        # rendered technique key resolved to, one block per distinct key in
        # first-use order. Reuses dco_llm_map's own "<key>: <why>" / "params:"
        # line builders (the exact text the 7B was shown) so this can never drift
        # into a second, hand-maintained gloss.
        by_key = {t["key"]: t for t in lex_cs["techniques"]}
        seen_keys = set()
        param_blocks = []
        for osc in rendered:
            for key in osc["chain"]:
                if key in seen_keys or key not in by_key:
                    continue
                seen_keys.add(key)
                t = by_key[key]
                block = f"{t['key']}: {t['why']}"
                pl = dco_llm_map._params_line(t)
                if pl:
                    block += "\n" + pl
                param_blocks.append(block)
        params_text = "\n\n".join(param_blocks)

        return {
            "ok": True,
            "orchestra": orchestra,
            "reading": reading,
            "params_text": params_text,
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
