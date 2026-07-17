#!/usr/bin/env python3
"""Phase-3 Csound tool library — the semantic vocabulary re-implemented as
CURATED CSOUND CODE, mirroring the existing LCO/DCO lexicon's key names
(backend/dco_lexicon.json, backend/dco_llm_map.py) where the concept carries
over 1:1 (bell, fm_bell, saw->saw_stack, square->square_stack, bright, dark,
harsh, glassy, metallic, vibrato, ...). BJ's line (SPEC_phase3_csound_lexicon.md):
the LLM points at prefabricated Csound code (keys), never authors DSP.

Pure data + tiny helpers — no Csound text is assembled here (that is
backend/csound_assembler.py's job; this module is imported by it).

DEVIATION from the spec's literal "csound_body_template" (a bare string with
named slots): every FAMILY tool below carries its Csound content as a
PYTHON-COMPUTED NUMERIC TABLE (partials: ratio/amp) rather than a static
string. This is necessary, not just convenient: characters (bright/dark/warm/
glassy/metallic) are implemented as PYTHON-side spectral tilt/skew transforms
on this table (cheaper and more precise than any runtime DSP, and trivially
deterministic — no RNG), and the per-layer partial COUNT varies by family.
The "named slots" the spec describes are still exactly what a family gets
handed at assemble time: a namespace, a register-scaled frequency variable,
and its (already character-shaped) partial table; the assembler
(csound_assembler.py) is the thing that actually renders Csound text from
this data, one family at a time, for FAMILY entries. CHARACTER and MOTION
entries are lighter — mostly numeric coefficients the assembler folds into
its own generic per-layer pipeline (post-synthesis tilt/drive for
non-additive families, pre-synthesis table edits for additive ones) — see
csound_assembler.py's module docstring for the exact pipeline order.

STANDING-SOUND CONTRACT (2026-07-17, BJ: "Hüllkurven gehören nicht in den
Oszillator. Vollständig entfernen."): there is no envelope concept anywhere
in this lexicon. Every family is a STANDING tone, static for as long as the
gate holds — amplitude SHAPE is the synth's own ADSR/VCA outside the Csound
orchestra, never the oscillator's job. The lexicon used to carry a fourth
"envelope" bucket (strike/swell/sustain/decay_only) plus per-partial decay/
bed fields on the strike-family partial tables (bell/metal/cymbal/glass/
pluck) and fm_bell's own index decay; all of that one-shot amplitude
machinery is gone. What made bell read as bell, metal as metal, etc. was
never the decay shape — it is each family's own partial-ratio/amplitude
fingerprint (csound_lexicon._*_PARTIALS below), which is exactly what
survives, held static.

Every key name below is used VERBATIM by csound_assembler.py and
backend/test_csound_lexicon.py; backend/CSOUND_LEXICON.md is generated from
the same "description" strings via generate_markdown() below (run this
module directly, `python -m backend.csound_lexicon`, to regenerate it).

Determinism: everything here is a fixed Python literal or a pure function of
one — no randomness anywhere, ever (SPEC hard requirement).
"""
import math
from pathlib import Path

LEXICON_VERSION = 2

# ─── register (organ footage) parsing ──────────────────────────────────────
# "32'"=0.25, "16'"=0.5, "8'"=1, "4'"=2, "2'"=4 (relative to the played note);
# a plain numeric ratio ("1.5", "0.75") is also accepted verbatim (spec).

REGISTER_RATIOS = {
    "32'": 0.25,
    "16'": 0.5,
    "8'": 1.0,
    "4'": 2.0,
    "2'": 4.0,
}


def parse_register(value):
    """"16'" -> 0.5, "1.5" / 1.5 -> 1.5. Raises ValueError (never a silent
    default — LLM-first guardrail, mirrored from dco_recipe's own validation
    discipline) on anything else -- including a non-finite ratio (nan/inf):
    Python's own float() happily parses the strings "nan"/"inf"/"infinity"
    (case-insensitive), so the <=0 check alone would let those through (both
    comparisons against 0 are False for nan, and inf > 0 is True) --
    defense-in-depth alongside csound_author.py's own parse-time rejection,
    since this module is also called directly from tests/tools with
    arbitrary specs."""
    if isinstance(value, (int, float)):
        ratio = float(value)
        if not math.isfinite(ratio):
            raise ValueError(f"register ratio must be finite, got {value!r}")
        if ratio <= 0:
            raise ValueError(f"register ratio must be positive, got {value!r}")
        return ratio
    s = str(value).strip()
    if s in REGISTER_RATIOS:
        return REGISTER_RATIOS[s]
    try:
        ratio = float(s)
    except ValueError:
        raise ValueError(
            f"unknown register {value!r} — expected one of "
            f"{sorted(REGISTER_RATIOS)} or a plain numeric ratio"
        )
    if not math.isfinite(ratio):
        raise ValueError(f"register ratio must be finite, got {value!r}")
    if ratio <= 0:
        raise ValueError(f"register ratio must be positive, got {value!r}")
    return ratio


# ─── partial-table generators (pure functions, deterministic) ──────────────

def _saw_partials(n, scale):
    """Truncated Fourier sawtooth series (1/h), matching dco_recipe._saw_series'
    own convention for continuity with the existing lexicon's additive
    vocabulary — but SCALED so the raw sum reads near unit peak (the
    assembler's own headroom budget divides down from there)."""
    return [{"ratio": float(h), "amp": round(scale / h, 6)} for h in range(1, n + 1)]


def _square_partials(n_odd, scale):
    """Truncated odd-harmonic Fourier square series (1/h, h odd only)."""
    return [{"ratio": float(h), "amp": round(scale / h, 6)}
            for h in range(1, 2 * n_odd, 2)]


# Golden-angle deterministic placement (2.399963229728653 rad, ~137.5 degrees):
# the SAME decorrelation technique dco_lexicon's own "shimmer" motion already
# uses ("golden-angle decorrelated so the top of the spectrum glitters" —
# dco_lexicon.json's shimmer "why"). Reused here for noise_wash's dense,
# irregularly-spaced partial cluster: a deterministic stand-in for filtered
# noise (Csound's `noise`/`rand` opcodes seed from system time by default —
# nondeterministic across renders unless explicitly seeded — which the spec's
# "no RNG anywhere" hard rule rules out; this reads as a noise wash to the ear
# while staying bit-exact-reproducible, no seeding required).
_GOLDEN_ANGLE = 2.399963229728653


def _golden_cluster(n, ratio_lo, ratio_hi, scale):
    out = []
    for k in range(n):
        frac = (k * _GOLDEN_ANGLE / (2.0 * math.pi)) % 1.0
        ratio = ratio_lo + frac * (ratio_hi - ratio_lo)
        amp = (scale / n) * (0.7 + 0.6 * frac)
        out.append({"ratio": round(ratio, 6), "amp": round(amp, 6)})
    return out


# ─── FAMILY partial tables (Phase-0/Phase-1 derived; BJ ear-approved anchor
# material for bell — tools/csound_poc_out/csound_strike_pad.csd — adapted
# with fewer partials for the 4-layer CPU budget) ──────────────────────────
# Every family's partial table carries ONLY ratio + amp: a STANDING additive
# tone, static for as long as the gate holds (see the module docstring's
# "STANDING-SOUND CONTRACT" paragraph). No family carries an intrinsic decay/
# bed pair anymore — bell/metal/cymbal/glass/pluck used to ramp from `amp`
# down to a `bed` fraction over a per-partial `decay` time; they now simply
# HOLD `amp`, forever, as their own sustained inharmonic (bell/metal/cymbal/
# glass) or harmonic (pluck) partial bed. The numeric `amp` values themselves
# are unchanged from before this migration — only the time-shape is gone.

_BELL_PARTIALS = [
    {"ratio": 0.56,  "amp": 0.28},
    {"ratio": 1.00,  "amp": 0.30},
    {"ratio": 1.19,  "amp": 0.22},
    {"ratio": 1.71,  "amp": 0.20},
    {"ratio": 2.00,  "amp": 0.16},
    {"ratio": 2.007, "amp": 0.16},
    {"ratio": 2.74,  "amp": 0.14},
    {"ratio": 3.76,  "amp": 0.11},
]

_METAL_PARTIALS = [
    {"ratio": 1.00, "amp": 0.30},
    {"ratio": 1.73, "amp": 0.24},
    {"ratio": 2.42, "amp": 0.20},
    {"ratio": 3.19, "amp": 0.17},
    {"ratio": 4.51, "amp": 0.14},
    {"ratio": 5.88, "amp": 0.11},
]

_CYMBAL_PARTIALS = [
    {"ratio": 3.00,  "amp": 0.17},
    {"ratio": 3.71,  "amp": 0.15},
    {"ratio": 4.83,  "amp": 0.14},
    {"ratio": 6.15,  "amp": 0.12},
    {"ratio": 8.02,  "amp": 0.10},
    {"ratio": 10.40, "amp": 0.09},
    {"ratio": 12.50, "amp": 0.07},
]

_GLASS_PARTIALS = [
    {"ratio": 2.00,  "amp": 0.30},
    {"ratio": 4.73,  "amp": 0.24},
    {"ratio": 7.19,  "amp": 0.18},
    {"ratio": 9.87,  "amp": 0.13},
    {"ratio": 13.10, "amp": 0.09},
]

# pluck: karplus-ish plucked-string SPECTRUM, approximated as a HARMONIC
# additive partial bed (ratios 1..6) with the classic karplus-strong tilt —
# upper partials sit quieter than the fundamental — held STANDING like every
# other family (no time-varying decay: that was the removed one-shot
# envelope). Reuses the exact same additive emission path as bell/metal/
# cymbal/glass (one fewer bespoke "custom" family to hand-write, and it gets
# characters/motion for free).
_PLUCK_PARTIALS = [
    {"ratio": 1.0, "amp": 0.34},
    {"ratio": 2.0, "amp": 0.25},
    {"ratio": 3.0, "amp": 0.18},
    {"ratio": 4.0, "amp": 0.13},
    {"ratio": 5.0, "amp": 0.09},
    {"ratio": 6.0, "amp": 0.06},
]

_PAD_PARTIALS = [
    {"ratio": 1.00,  "amp": 0.20},
    {"ratio": 1.19,  "amp": 0.18},
    {"ratio": 1.71,  "amp": 0.16},
    {"ratio": 2.007, "amp": 0.14},
    {"ratio": 2.74,  "amp": 0.12},
    {"ratio": 3.76,  "amp": 0.10},
    {"ratio": 4.07,  "amp": 0.09},
    {"ratio": 5.43,  "amp": 0.07},
]

_ORGAN_PARTIALS = [
    {"ratio": 1.0, "amp": 0.30},
    {"ratio": 2.0, "amp": 0.22},
    {"ratio": 3.0, "amp": 0.16},
    {"ratio": 4.0, "amp": 0.12},
    {"ratio": 6.0, "amp": 0.08},
    {"ratio": 8.0, "amp": 0.05},
]

_SAW_STACK_PARTIALS = _saw_partials(16, 0.30)
_SQUARE_STACK_PARTIALS = _square_partials(8, 0.30)  # h = 1,3,...,15
_NOISE_WASH_PARTIALS = _golden_cluster(14, 2.2, 11.0, 0.34)
_SUB_PARTIALS = [{"ratio": 1.0, "amp": 0.38}]


# ─── the TOOLS table ────────────────────────────────────────────────────────
# Each entry: {key, family, description, params_schema, ...family-specific...}.
# "family" here is the SPEC's own field name for the top-level bucket
# discriminator ("family" | "character" | "motion") — an overloaded term
# (bucket "family" vs. the specific instrument families like "bell"), kept
# because SPEC_phase3_csound_lexicon.md names it that way verbatim; see the
# module docstring above.

TOOLS = {
    # ── FAMILIES (12): spectral/timbral source, all STANDING while gated ──
    "bell": {
        "key": "bell", "family": "family", "kind": "additive_continuous",
        "description": "inharmonic bell — 8 stretched partials, held as a standing sustained bed",
        "params_schema": {"level": "0..1.5 float, default 0.8", "register": "organ footage or ratio, default \"8'\""},
        "partials": _BELL_PARTIALS,
    },
    "metal": {
        "key": "metal", "family": "family", "kind": "additive_continuous",
        "description": "clangorous inharmonic metal bar — denser partial set than bell, standing",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio"},
        "partials": _METAL_PARTIALS,
    },
    "cymbal": {
        "key": "cymbal", "family": "family", "kind": "additive_continuous",
        "description": "dense bright inharmonic plate wash — no strong fundamental, standing",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio"},
        "partials": _CYMBAL_PARTIALS,
    },
    "glass": {
        "key": "glass", "family": "family", "kind": "additive_continuous",
        "description": "sparse high inharmonic partials, thin and bright — a standing glass spectrum",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio"},
        "partials": _GLASS_PARTIALS,
    },
    "pluck": {
        "key": "pluck", "family": "family", "kind": "additive_continuous",
        "description": "karplus-ish plucked-string spectrum — harmonic partials, upper ones quieter than the fundamental, held standing",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio"},
        "partials": _PLUCK_PARTIALS,
    },
    "fm_bell": {
        "key": "fm_bell", "family": "family", "kind": "custom",
        "description": "true 2-operator FM bell — ratio-2 modulator, fixed standing brightness index",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio"},
    },
    "pad": {
        "key": "pad", "family": "family", "kind": "additive_continuous",
        "description": "slow-crossing partial bed — 8 partials, gentle taper, standing",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio"},
        "partials": _PAD_PARTIALS,
    },
    "organ_tone": {
        "key": "organ_tone", "family": "family", "kind": "additive_continuous",
        "description": "clean drawbar-ish partials {1,2,3,4,6,8} at typical registration levels",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio"},
        "partials": _ORGAN_PARTIALS,
    },
    "saw_stack": {
        "key": "saw_stack", "family": "family", "kind": "additive_continuous",
        "description": "truncated-Fourier sawtooth (16 harmonics, 1/h) — the canonical bright analogue waveform",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio"},
        "partials": _SAW_STACK_PARTIALS,
    },
    "square_stack": {
        "key": "square_stack", "family": "family", "kind": "additive_continuous",
        "description": "truncated-Fourier square (8 odd harmonics, 1/h) — hollow, clarinet-adjacent",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio"},
        "partials": _SQUARE_STACK_PARTIALS,
    },
    "noise_wash": {
        "key": "noise_wash", "family": "family", "kind": "additive_continuous",
        "description": "dense golden-angle-spaced partial cluster reading as a filtered noise bed — deterministic, standing",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio (shifts the cluster's center)"},
        "partials": _NOISE_WASH_PARTIALS,
    },
    "sub": {
        "key": "sub", "family": "family", "kind": "additive_continuous",
        "description": "pure low sine reinforcement — a clean single partial, typically paired with a low register",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio, typically \"16'\"/\"32'\""},
        "partials": _SUB_PARTIALS,
    },

    # ── CHARACTERS (6): per-layer modifier, intensity 0..1 ──
    "bright": {
        "key": "bright", "family": "character", "kind": "tilt",
        "description": "spectral tilt toward the upper partials",
        "params_schema": {"amount": "0..1 float intensity"},
        "db_per_oct": 3.2, "runtime_cutoff_hz": 1400.0, "runtime_mix_scale": 0.85,
    },
    "dark": {
        "key": "dark", "family": "character", "kind": "tilt",
        "description": "spectral tilt away from the upper partials",
        "params_schema": {"amount": "0..1 float intensity"},
        "db_per_oct": -3.6, "runtime_cutoff_hz": 2600.0, "runtime_mix_scale": 0.75,
    },
    "warm": {
        "key": "warm", "family": "character", "kind": "tilt_drive",
        "description": "gentle low emphasis plus a touch of soft drive",
        "params_schema": {"amount": "0..1 float intensity"},
        "db_per_oct": -2.0, "runtime_cutoff_hz": 3200.0, "runtime_mix_scale": 0.55, "drive_scale": 0.8,
    },
    "harsh": {
        "key": "harsh", "family": "character", "kind": "drive",
        "description": "waveshaping drive — real intermodulation harmonics, the extreme upper-energy pole",
        "params_schema": {"amount": "0..1 float intensity"},
        "drive_scale": 6.0,
    },
    "glassy": {
        "key": "glassy", "family": "character", "kind": "tilt",
        "description": "strong high-partial emphasis — a bright, glassy spectral tilt",
        "params_schema": {"amount": "0..1 float intensity"},
        "db_per_oct": 4.6, "runtime_cutoff_hz": 900.0, "runtime_mix_scale": 0.95,
    },
    "metallic": {
        "key": "metallic", "family": "character", "kind": "skew",
        "description": "inharmonic ratio skew toward the bell/cymbal partial sets — a deterministic per-partial detune",
        "params_schema": {"amount": "0..1 float intensity"},
        "skew_scale": 0.026,
    },

    # ── MOTION (4): per-layer (or global, broadcast to every layer) ──
    "vibrato": {
        "key": "vibrato", "family": "motion", "kind": "vibrato",
        "description": "shared per-layer pitch LFO (rate/depth scale with amount) applied via cent() to every partial",
        "params_schema": {"amount": "0..1 float"},
        "rate_hz_base": 4.5, "rate_hz_scale": 2.5, "depth_cents_scale": 45.0,
    },
    "evolve": {
        "key": "evolve", "family": "motion", "kind": "evolve",
        "description": "slow spectral crossfade within the layer — a low LFO tilts partial balance dark<->bright and back",
        "params_schema": {"amount": "0..1 float"},
        "rate_hz": 0.09, "depth_scale": 0.6,
    },
    "shimmer": {
        "key": "shimmer", "family": "motion", "kind": "shimmer",
        "description": "detuned pair riding the fundamental — audible slow beating",
        "params_schema": {"amount": "0..1 float"},
        "detune_ratio": 1.007, "level_scale": 0.12,
    },
    "breathe": {
        "key": "breathe", "family": "motion", "kind": "breathe",
        "description": "slow amplitude wave over the whole layer",
        "params_schema": {"amount": "0..1 float"},
        "rate_hz": 0.2, "depth_scale": 0.22,
    },
}


def keys_in_family(bucket):
    """All TOOL keys whose 'family' bucket == bucket ('family'|'character'|
    'motion'), in TOOLS' own (insertion-stable) order."""
    return [k for k, v in TOOLS.items() if v["family"] == bucket]


FAMILY_KEYS = keys_in_family("family")
CHARACTER_KEYS = keys_in_family("character")
MOTION_KEYS = keys_in_family("motion")


def generate_markdown():
    """backend/CSOUND_LEXICON.md content — every key, one line each (the
    Phase-4 prompt vocabulary table). Deterministic (TOOLS' own order)."""
    lines = [
        "# Csound Lexicon — Phase 3 tool vocabulary",
        "",
        "Every key the deterministic assembler (`backend/csound_assembler.py`) "
        "accepts, one line each. The LLM (Phase 4, out of scope here) only ever "
        "points at these keys — it never authors Csound or DSP numbers.",
        "",
        "Every family is a STANDING tone: static for as long as the gate holds. "
        "There is no envelope concept in this orchestra — amplitude shape "
        "belongs to the synth's own ADSR, outside the orchestra.",
        "",
        "## Families (`\"tool\"`, 12)",
        "",
    ]
    for k in FAMILY_KEYS:
        lines.append(f"- **{k}** — {TOOLS[k]['description']}")
    lines += ["", "## Characters (`\"characters\": [{\"key\", \"amount\"}]`, 6)", ""]
    for k in CHARACTER_KEYS:
        lines.append(f"- **{k}** — {TOOLS[k]['description']}")
    lines += ["", "## Motion (`\"motion\"`, per-layer or global, 4)", ""]
    for k in MOTION_KEYS:
        lines.append(f"- **{k}** — {TOOLS[k]['description']}")
    lines += [
        "",
        "## Register (organ footage, per layer)",
        "",
        "- **32'** = ratio 0.25",
        "- **16'** = ratio 0.5",
        "- **8'**  = ratio 1.0 (the played note)",
        "- **4'**  = ratio 2.0",
        "- **2'**  = ratio 4.0",
        "- a plain numeric ratio (e.g. `1.5`) is also accepted verbatim.",
        "",
        "## Layer cap",
        "",
        "At most 4 layers per composition (CPU budget; see "
        "`tools/csound_orch_check.cpp`'s `bench` mode).",
        "",
    ]
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    out_path = Path(__file__).resolve().parent / "CSOUND_LEXICON.md"
    out_path.write_text(generate_markdown(), encoding="utf-8")
    print(f"wrote {out_path}")
