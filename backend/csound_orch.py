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
}
_DEFAULT_MORPH_SPECTRUM = "additive"   # a morph endpoint with no reading falls here


def _reading(technique_keys, adjective_keys, motion_key):
    """Short human-readable interpretation for the UI card (mirrors the old
    'reading' string). e.g. 'glass > sine morph, glassy' or 'pwm, distorted'."""
    if len(technique_keys) >= 2:
        head = " > ".join(technique_keys) + " morph"
    elif technique_keys:
        head = technique_keys[0]
    else:
        head = "sine"
    bits = [head]
    if adjective_keys:
        bits.append(", ".join(adjective_keys))
    if motion_key and motion_key != "static":
        bits.append("~" + motion_key)
    return " · ".join(bits)


# ── per-technique synthesis: each returns Csound lines producing `asig` from
#    `kfreq` (the limited freq channel). Standing tones (hold while gate open). ──

def _emit_steady(technique, adjective_keys):
    """A single (non-morph) technique -> `asig`. Uses Csound's native opcodes."""
    L = []
    if technique == "pwm":
        # classic PWM: band-limited pulse whose DUTY moves (square 50% -> thin
        # 8% -> back), a genuinely moving spectrum. kpw is the pulse width.
        L.append("  klfo     oscili 0.5, 0.25            ; -0.5..0.5, 4 s period")
        L.append("  kpw      = 0.29 + 0.21 * (klfo + 0.5) ; duty 0.08..0.50..0.08")
        L.append("  asig     vco2 0.6, kfreq, 2, kpw     ; imode 2 = pulse, kpw = width")
    elif technique in ("square", "clarinet", "chiptune", "pulse"):
        L.append("  asig     vco2 0.6, kfreq, 2, 0.5     ; square (50%% pulse)")
    elif technique == "triangle":
        # vco2 imode 4 renders SILENT on this Csound build (the triangle band-
        # limited table is not pre-generated). Use the additive triangle (odd
        # harmonics ~1/n^2) -- band-limited by construction, guaranteed to sound.
        L.append(_emit_additive(_SPECTRA["triangle"], gain=0.6))
    elif technique in ("saw", "supersaw", "brass", "strings", "bass_saw", "sync"):
        L.append("  asig     vco2 0.6, kfreq, 0          ; band-limited sawtooth")
    elif technique in ("fm_bell", "fm", "fm_ep", "metallic_fm", "sync"):
        # FM via foscili: an inharmonic-ish carrier:modulator ratio gives the
        # bell/metal sideband spectrum natively (no partial table).
        car, mod, ndx = ("1", "1.41", "3.2") if technique in ("fm_bell", "metallic_fm") else ("1", "2", "1.8")
        L.append(f"  asig     foscili 0.5, kfreq, {car}, {mod}, {ndx}, giSine ; FM (foscili)")
    elif technique == "cheby":
        # Chebyshev/tanh waveshaping of a sine = polynomial harmonics (real
        # waveshaper, the substrate doing dirt natively).
        L.append("  adrv     oscili 0.9, kfreq")
        L.append("  asig     = tanh(adrv * 3.0) * 0.5    ; waveshaper harmonics")
    else:
        # sine / additive / theremin / sub_sine / flute / organ and anything not
        # given a bespoke idiom yet: render its spectrum additively (real oscils
        # at true ratios), defaulting to a pure sine.
        spec = _SPECTRA.get(_MORPH_SPECTRUM.get(technique, "sine"), _SPECTRA["sine"])
        L.append(_emit_additive(spec, gain=0.6))
    return "\n".join(L)


def _emit_additive(spectrum, gain=0.6):
    """A static additive bank: one `oscili` per partial at kfreq*ratio. LEGITIMATE
    here (a genuinely inharmonic tone summed at its true partial frequencies is
    what Csound additive synthesis is for) — the regression's sin was using this
    for EVERYTHING incl. pwm/FM/dirt, not additive per se."""
    lines = []
    total = sum(a for _, a in spectrum) or 1.0
    scale = gain / total
    terms = []
    for i, (ratio, amp) in enumerate(spectrum):
        lines.append(f"  ap{i:<2d}     oscili {amp*scale:.4f}, kfreq * {ratio:.4f}")
        terms.append(f"ap{i}")
    lines.append("  asig     = " + " + ".join(terms))
    return "\n".join(lines)


def _emit_morph(technique_keys, imorphtime):
    """A genuine spectral MORPH between two idioms: ONE additive bank whose
    per-partial amplitudes AND frequency ratios travel A->B over `imorphtime`,
    (re)started per note off the trig epoch. This is NOT two instances crossfaded
    (that beats/cancels and plays both at once) — it is the SAME oscillators with
    moving parameters, so the spectrum genuinely transforms. Multi-stage chains
    (a > b > c) split the time into equal legs via one linseg across stages."""
    # resolve each technique key to a spectrum reading; drop any endpoint we have
    # no spectral reading for (a morph needs both endpoints as spectra).
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
    # across the morph (a spectral morph must not read as a volume change).
    def norm(stage):
        tot = sum(a for _, a in stage) or 1.0
        return [(r, a / tot) for r, a in stage]
    aligned = [norm(s) for s in aligned]

    nlegs = len(stages) - 1
    leg = imorphtime / nlegs

    L = []
    L.append("  ; --- per-note spectral morph (trig-epoch reinit; spectral, NOT amp env) ---")
    L.append("  if changed2(ktrig) == 1 then")
    L.append("    reinit Lmorph")
    L.append("  endif")
    L.append("Lmorph:")
    terms = []
    for i in range(n):
        # ONE linseg per partial through every stage's value, equal legs. linseg
        # natively does the piecewise-linear breakpoint walk and restarts on
        # reinit, so partial i's amplitude AND ratio travel A->B(->C...) per note.
        amps = ", ".join(f"{aligned[j][i][1]:.4f}, {leg:.4f}" for j in range(nlegs))
        amps += f", {aligned[nlegs][i][1]:.4f}"
        rats = ", ".join(f"{aligned[j][i][0]:.4f}, {leg:.4f}" for j in range(nlegs))
        rats += f", {aligned[nlegs][i][0]:.4f}"
        L.append(f"  ka{i:<2d}     linseg {amps}")
        L.append(f"  kr{i:<2d}     linseg {rats}")
        L.append(f"  ap{i:<2d}     oscili ka{i} * 0.6, kfreq * kr{i}")
        terms.append(f"ap{i}")
    L.append("  rireturn")
    L.append("  asig     = " + " + ".join(terms))
    return "\n".join(L)


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
    """A little k-rate spectral motion so 'moving' prompts move (movement by
    default). Slice: sweep/evolve/open/close -> a slow cutoff LFO on a lowpass."""
    if not motion_key or motion_key == "static":
        return ""
    if motion_key in ("sweep", "evolve", "open_up", "close", "breathe", "wobble", "cycle"):
        return ("  kcut     oscili 2200, 0.15\n"
                "  asig     tone asig, 2600 + kcut       ; slow spectral sweep")
    if motion_key in ("shimmer", "vibrate", "flutter", "tremolo"):
        return ("  ksh      oscili 0.18, 5.5\n"
                "  asig     = asig * (1 + ksh)           ; fast shimmer")
    return ""


def build_orchestra(technique_keys, adjective_keys, motion_key, morph_sec=None):
    """keys -> (orchestra_text, reading). technique_keys is a list (>=2 => morph
    chain). Deterministic. The returned text carries `sr = %SR%` for the engine
    to substitute at its real rate."""
    technique_keys = [k for k in (technique_keys or []) if k]
    adjective_keys = [k for k in (adjective_keys or []) if k]
    imorphtime = float(morph_sec) if morph_sec else DEFAULT_MORPH_SEC

    # synthesis body -> asig
    body = None
    if len(technique_keys) >= 2:
        body = _emit_morph(technique_keys, imorphtime)
    if body is None:
        tk = technique_keys[0] if technique_keys else "sine"
        body = _emit_steady(tk, adjective_keys)

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

    parts = [head, body]
    if adj:
        parts.append(adj)
    if mot:
        parts.append(mot)
    orchestra = "\n".join(p for p in parts if p) + tail + score
    return orchestra, _reading(technique_keys, adjective_keys, motion_key)


def build_csound_response(text, llm):
    """Prompt -> live Csound orchestra, the REAL pipeline entry (pipe_inference
    mode=="csound" calls this). Reuses the restored language-understanding layer
    UNCHANGED: one 7B instruct call routes the whole prompt to closed-enum
    lexicon keys (technique incl. a "a > b" morph chain, adjectives, motion) via
    dco_llm_map's own catalogue + parser + injection guard; build_orchestra then
    renders those keys as real Csound idioms. LLM-first, no fallback: if nothing
    maps, return an honest ok=false rather than a junk tone.

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

        tcanon = _canon(lexicon["techniques"])
        acanon = _canon(lexicon["adjectives"])
        mcanon = _canon(lexicon["motions"])

        system_prompt = dco_llm_map._SYSTEM_PROMPT_HEAD + dco_llm_map._build_catalogue(lexicon)
        raw = llm(text, system_prompt, dco_llm_map._MAX_NEW_TOKENS)

        # Parse like dco_llm_map._parse_and_validate, but tolerate the 7B listing
        # a COMPOUND technique with commas: "pwm square wave" -> "TECHNIQUE: pwm,
        # square" (a pwm'd square = ONE sound). The shared parser splits TECHNIQUE
        # only on ">" (the morph operator), so it takes "pwm, square" as a single
        # bogus key, drops BOTH and fails the whole prompt. Here ">" alone is a
        # morph chain (ordered, keep every valid stage); a comma list is one
        # compound technique -> take the first valid key. Adjective/motion parsing
        # is identical to the shared parser (reused verbatim below).
        technique_raw, adjectives_raw, motion_raw = dco_llm_map._parse_reply(raw)
        if ">" in technique_raw:
            technique_keys, tflags = dco_llm_map._validate_keys(technique_raw.split(">"), tcanon)
        else:
            _valid, tflags = dco_llm_map._validate_keys(technique_raw.split(","), tcanon)
            technique_keys = _valid[:1]
        adjective_keys, aflags = dco_llm_map._validate_keys(
            adjectives_raw.split(",") if adjectives_raw else [], acanon)
        _motion_keys, mflags = dco_llm_map._validate_keys(
            [motion_raw] if motion_raw else [], mcanon)
        motion_key = _motion_keys[0] if _motion_keys else None
        flags = tflags + aflags + mflags

        if not technique_keys and not adjective_keys and not motion_key:
            return {"ok": False,
                    "error": "no synthesis idiom matched the prompt"}

        orchestra, reading = build_orchestra(technique_keys, adjective_keys, motion_key)
        return {
            "ok": True,
            "orchestra": orchestra,
            "reading": reading,
            "technique": technique_keys,
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
