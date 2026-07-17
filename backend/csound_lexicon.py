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
PYTHON-COMPUTED NUMERIC TABLE (partials: ratio/amp/decay/bed) rather than a
static string. This is necessary, not just convenient: characters (bright/
dark/warm/glassy/metallic) are implemented as PYTHON-side spectral tilt/skew
transforms on this table (cheaper and more precise than any runtime DSP, and
trivially deterministic — no RNG), and the per-layer partial COUNT varies by
family. The "named slots" the spec describes are still exactly what a family
gets handed at assemble time: a namespace, a register-scaled frequency
variable, and its (already character-shaped) partial table; the assembler
(csound_assembler.py) is the thing that actually renders Csound text from
this data, one family at a time, for FAMILY entries. CHARACTER, MOTION and
ENVELOPE entries are lighter — mostly numeric coefficients the assembler
folds into its own generic per-layer pipeline (post-synthesis tilt/drive for
non-additive families, pre-synthesis table edits for additive ones) — see
csound_assembler.py's module docstring for the exact pipeline order.

Every key name below is used VERBATIM by csound_assembler.py and
backend/test_csound_lexicon.py; backend/CSOUND_LEXICON.md is generated from
the same "description" strings via generate_markdown() below (run this
module directly, `python -m backend.csound_lexicon`, to regenerate it).

Determinism: everything here is a fixed Python literal or a pure function of
one — no randomness anywhere, ever (SPEC hard requirement).
"""
import math
from pathlib import Path

LEXICON_VERSION = 1

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
    discipline) on anything else."""
    if isinstance(value, (int, float)):
        ratio = float(value)
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
# additive_decay fields: ratio, amp, decay (seconds to the bed floor),
#   bed (fraction of amp the partial settles to and HOLDS at, forever, once
#   the strike decay completes — Csound's transeg holds its last breakpoint
#   value indefinitely, so no separate "sustain stage" opcode is needed).
# additive_continuous fields: ratio, amp only — no intrinsic decay; the
#   assembler's shared per-layer envelope (the "envelope" tool selection)
#   supplies 100% of the macro time-shape for these.

_BELL_PARTIALS = [
    {"ratio": 0.56,  "amp": 0.28, "decay": 8.0, "bed": 0.35},
    {"ratio": 1.00,  "amp": 0.30, "decay": 7.0, "bed": 0.35},
    {"ratio": 1.19,  "amp": 0.22, "decay": 5.5, "bed": 0.35},
    {"ratio": 1.71,  "amp": 0.20, "decay": 5.0, "bed": 0.35},
    {"ratio": 2.00,  "amp": 0.16, "decay": 4.0, "bed": 0.35},
    {"ratio": 2.007, "amp": 0.16, "decay": 4.0, "bed": 0.35},
    {"ratio": 2.74,  "amp": 0.14, "decay": 2.8, "bed": 0.35},
    {"ratio": 3.76,  "amp": 0.11, "decay": 2.0, "bed": 0.35},
]

_METAL_PARTIALS = [
    {"ratio": 1.00, "amp": 0.30, "decay": 3.0, "bed": 0.18},
    {"ratio": 1.73, "amp": 0.24, "decay": 2.6, "bed": 0.18},
    {"ratio": 2.42, "amp": 0.20, "decay": 2.2, "bed": 0.18},
    {"ratio": 3.19, "amp": 0.17, "decay": 1.8, "bed": 0.18},
    {"ratio": 4.51, "amp": 0.14, "decay": 1.4, "bed": 0.18},
    {"ratio": 5.88, "amp": 0.11, "decay": 1.1, "bed": 0.18},
]

_CYMBAL_PARTIALS = [
    {"ratio": 3.00,  "amp": 0.17, "decay": 1.4,  "bed": 0.05},
    {"ratio": 3.71,  "amp": 0.15, "decay": 1.1,  "bed": 0.05},
    {"ratio": 4.83,  "amp": 0.14, "decay": 0.9,  "bed": 0.05},
    {"ratio": 6.15,  "amp": 0.12, "decay": 0.7,  "bed": 0.05},
    {"ratio": 8.02,  "amp": 0.10, "decay": 0.55, "bed": 0.05},
    {"ratio": 10.40, "amp": 0.09, "decay": 0.45, "bed": 0.05},
    {"ratio": 12.50, "amp": 0.07, "decay": 0.4,  "bed": 0.05},
]

_GLASS_PARTIALS = [
    {"ratio": 2.00,  "amp": 0.30, "decay": 0.9,  "bed": 0.03},
    {"ratio": 4.73,  "amp": 0.24, "decay": 0.7,  "bed": 0.03},
    {"ratio": 7.19,  "amp": 0.18, "decay": 0.5,  "bed": 0.03},
    {"ratio": 9.87,  "amp": 0.13, "decay": 0.35, "bed": 0.03},
    {"ratio": 13.10, "amp": 0.09, "decay": 0.25, "bed": 0.03},
]

# pluck: karplus-ish plucked excitation, approximated as a HARMONIC additive
# decay (ratios 1..8) with the classic karplus-strong signature — high
# partials die much faster than the fundamental — and no sustain bed
# ("bell/pluck natural" per the spec's envelope-tool description). No
# resonant-filter/noise-burst opcode needed: this reuses the exact same
# additive_decay emission path as bell/metal/cymbal/glass (one fewer bespoke
# "custom" family to hand-write, and it gets characters/motion for free).
_PLUCK_PARTIALS = [
    {"ratio": 1.0, "amp": 0.34, "decay": 2.2,  "bed": 0.0},
    {"ratio": 2.0, "amp": 0.25, "decay": 1.1,  "bed": 0.0},
    {"ratio": 3.0, "amp": 0.18, "decay": 0.65, "bed": 0.0},
    {"ratio": 4.0, "amp": 0.13, "decay": 0.42, "bed": 0.0},
    {"ratio": 5.0, "amp": 0.09, "decay": 0.28, "bed": 0.0},
    {"ratio": 6.0, "amp": 0.06, "decay": 0.18, "bed": 0.0},
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
# discriminator ("family" | "character" | "motion" | "envelope") — an
# overloaded term (bucket "family" vs. the specific instrument families like
# "bell"), kept because SPEC_phase3_csound_lexicon.md names it that way
# verbatim; see the module docstring above.

TOOLS = {
    # ── FAMILIES (12): spectral/timbral source ──
    "bell": {
        "key": "bell", "family": "family", "kind": "additive_decay",
        "description": "inharmonic strike bell — 8 stretched partials, per-partial decay into a quiet living bed",
        "params_schema": {"level": "0..1.5 float, default 0.8", "register": "organ footage or ratio, default \"8'\"", "envelope": "envelope key, default 'strike'"},
        "partials": _BELL_PARTIALS,
    },
    "metal": {
        "key": "metal", "family": "family", "kind": "additive_decay",
        "description": "clangorous inharmonic metal bar — denser partial set than bell, faster decay",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio", "envelope": "envelope key"},
        "partials": _METAL_PARTIALS,
    },
    "cymbal": {
        "key": "cymbal", "family": "family", "kind": "additive_decay",
        "description": "dense bright inharmonic plate wash — no strong fundamental, short decays",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio", "envelope": "envelope key"},
        "partials": _CYMBAL_PARTIALS,
    },
    "glass": {
        "key": "glass", "family": "family", "kind": "additive_decay",
        "description": "sparse high inharmonic partials, thin and very fast decay — a struck-glass crack",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio", "envelope": "envelope key"},
        "partials": _GLASS_PARTIALS,
    },
    "pluck": {
        "key": "pluck", "family": "family", "kind": "additive_decay",
        "description": "karplus-ish plucked string — harmonic partials, high ones decaying much faster than the fundamental, no sustain bed",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio", "envelope": "envelope key"},
        "partials": _PLUCK_PARTIALS,
    },
    "fm_bell": {
        "key": "fm_bell", "family": "family", "kind": "custom",
        "description": "true 2-operator FM bell — ratio-2 modulator, index envelope decaying from bright to plain",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio", "envelope": "envelope key (shapes the carrier's amplitude; the FM index has its own fixed decay)"},
    },
    "pad": {
        "key": "pad", "family": "family", "kind": "additive_continuous",
        "description": "slow-crossing partial bed — 8 partials, gentle taper, continuous (no intrinsic decay)",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio", "envelope": "envelope key, default 'swell'"},
        "partials": _PAD_PARTIALS,
    },
    "organ_tone": {
        "key": "organ_tone", "family": "family", "kind": "additive_continuous",
        "description": "clean drawbar-ish partials {1,2,3,4,6,8} at typical registration levels",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio", "envelope": "envelope key, default 'sustain'"},
        "partials": _ORGAN_PARTIALS,
    },
    "saw_stack": {
        "key": "saw_stack", "family": "family", "kind": "additive_continuous",
        "description": "truncated-Fourier sawtooth (16 harmonics, 1/h) — the canonical bright analogue waveform",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio", "envelope": "envelope key"},
        "partials": _SAW_STACK_PARTIALS,
    },
    "square_stack": {
        "key": "square_stack", "family": "family", "kind": "additive_continuous",
        "description": "truncated-Fourier square (8 odd harmonics, 1/h) — hollow, clarinet-adjacent",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio", "envelope": "envelope key"},
        "partials": _SQUARE_STACK_PARTIALS,
    },
    "noise_wash": {
        "key": "noise_wash", "family": "family", "kind": "additive_continuous",
        "description": "dense golden-angle-spaced partial cluster reading as a filtered noise bed — deterministic, continuous",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio (shifts the cluster's center)", "envelope": "envelope key"},
        "partials": _NOISE_WASH_PARTIALS,
    },
    "sub": {
        "key": "sub", "family": "family", "kind": "additive_continuous",
        "description": "pure low sine reinforcement — a clean single partial, typically paired with a low register",
        "params_schema": {"level": "0..1.5 float", "register": "organ footage or ratio, typically \"16'\"/\"32'\"", "envelope": "envelope key"},
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
        "description": "strong high-partial emphasis plus a fast-decay tilt (shortens upper-partial decay times)",
        "params_schema": {"amount": "0..1 float intensity"},
        "db_per_oct": 4.6, "runtime_cutoff_hz": 900.0, "runtime_mix_scale": 0.95, "decay_tilt": 0.5,
    },
    "metallic": {
        "key": "metallic", "family": "character", "kind": "skew",
        "description": "inharmonic ratio skew toward the bell/cymbal partial sets — a deterministic per-partial detune",
        "params_schema": {"amount": "0..1 float intensity"},
        "skew_scale": 0.026, "decay_tilt": 0.3,
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

    # ── ENVELOPE (4): per-layer macro time-shape ──
    # Only the ATTACK/DECAY-TO-BED shape lives here — note-off/release is
    # handled entirely downstream by T5ynth's own ampEnv/VCA (Phase-1 D3:
    # "gate = voice ACTIVE... closing this gate on note-off would abort the
    # release tail, so it never does"); these envelopes simply hold their
    # final value forever once the onset completes (Csound transeg's own
    # behaviour: it holds its last breakpoint value indefinitely).
    #
    # Fields, consumed TWO ways by the assembler (documented there):
    #  - additive_continuous families + fm_bell's carrier: a literal per-layer
    #    transeg built straight from attack_sec/attack_curve/onset_level/
    #    decay_sec/hold_level (decay_sec==0 means "no decay segment, hold at
    #    onset_level").
    #  - additive_decay families (bell/metal/cymbal/glass/pluck), which already
    #    carry their OWN intrinsic per-partial bed (csound_lexicon._*_PARTIALS):
    #    only onset_scale (suppresses the transient for 'swell') and
    #    bed_multiplier (scales the family's OWN natural bed) apply — see
    #    csound_assembler._DECAY_ENVELOPE_ADJUST.
    "strike": {
        "key": "strike", "family": "envelope", "kind": "envelope",
        "description": "fast attack, exponential decay down to a sustained bed level",
        "params_schema": {},
        "attack_sec": 0.004, "attack_curve": 0, "onset_level": 1.0,
        "decay_sec": 2.2, "hold_level": 0.30,
    },
    "swell": {
        "key": "swell", "family": "envelope", "kind": "envelope",
        "description": "slow attack, no transient — fades up into the held level",
        "params_schema": {},
        "attack_sec": 1.3, "attack_curve": 4, "onset_level": 1.0,
        "decay_sec": 0.0, "hold_level": 1.0,
    },
    "sustain": {
        "key": "sustain", "family": "envelope", "kind": "envelope",
        "description": "near-instant onset, flat organ-like hold — no decay",
        "params_schema": {},
        "attack_sec": 0.012, "attack_curve": 0, "onset_level": 1.0,
        "decay_sec": 0.0, "hold_level": 1.0,
    },
    "decay_only": {
        "key": "decay_only", "family": "envelope", "kind": "envelope",
        "description": "fast attack, full decay to near-silence — no sustain bed (bell/pluck natural)",
        "params_schema": {},
        "attack_sec": 0.003, "attack_curve": 0, "onset_level": 1.0,
        "decay_sec": 2.0, "hold_level": 0.02,
    },
}


def keys_in_family(bucket):
    """All TOOL keys whose 'family' bucket == bucket ('family'|'character'|
    'motion'|'envelope'), in TOOLS' own (insertion-stable) order."""
    return [k for k, v in TOOLS.items() if v["family"] == bucket]


FAMILY_KEYS = keys_in_family("family")
CHARACTER_KEYS = keys_in_family("character")
MOTION_KEYS = keys_in_family("motion")
ENVELOPE_KEYS = keys_in_family("envelope")


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
    lines += ["", "## Envelope (`\"envelope\"`, 4)", ""]
    for k in ENVELOPE_KEYS:
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
