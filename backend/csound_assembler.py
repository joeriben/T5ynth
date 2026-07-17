#!/usr/bin/env python3
"""Phase-3 deterministic Csound orchestra assembler
(SPEC_phase3_csound_lexicon.md). Composes selected backend/csound_lexicon.py
TOOL keys into a full Phase-1-contract orchestra: `assemble(spec) ->
(orchestra_text, reading)`.

`assemble()` is a DROP-IN REPLACEMENT for src/dsp/CsoundEngine.cpp's
buildOrchestra() (same header shape, same instr-1 voice-bridge prologue, same
score) — see that function's own comments for the contract this mirrors
verbatim: ONE numeric `instr 1`, p4 = voice index 1..16, channels
gateN/freqN/velN/presN/timbN/trigN via sprintf on p4, portk declick on
gate+freq, retrigger = changed2 on the trig channel + reinit/rireturn, outch
ivoice, numeric-p1 score `i 1 0 360000 N` x16, ksmps=64, nchnls=16, 0dbfs=1.
The ONE difference: `sr = %SR%` is left as a literal marker (not baked to a
number) — the assembler runs offline in Python with no sample rate of its
own; whatever compiles this text (tools/csound_orch_check.cpp for this
phase's own testing; the C++ engine in a later phase) substitutes the real
number in, the same way buildOrchestra() itself bakes sr via snprintf.

── Pipeline (per layer, in this exact order — read before editing) ──────────
 1. Resolve tool/register/envelope/characters/motion keys. ANY unknown key
    raises ValueError immediately (LLM-first guardrail: an unmapped word is
    an honest error upstream, never a silent default — dco_recipe's own
    discipline, mirrored here).
 2. kL{ns}_freq0 = kfreq * registerRatio (the layer's register-scaled
    fundamental; every partial's frequency is relative to THIS, never to the
    raw contract kfreq directly).
 3. MOTION pre-emit hooks (vibrato, evolve): build a shared per-layer LFO and
    either (vibrato) a freq_mod_suffix every partial's frequency expression
    picks up, or (evolve) a per-partial amplitude-tilt term the additive
    emitter's own per-partial loop consults. These must run BEFORE step 5
    because they change what step 5 emits, not what it emits into.
 4. CHARACTER application (bright/dark/warm/glassy/metallic/harsh), in the
    layer's own list order. For an ADDITIVE family (ctx.partials is not
    None): tilt/skew MUTATE ctx.partials in place (cheap, deterministic,
    zero extra runtime opcodes). For fm_bell (ctx.partials is None): the
    character is instead QUEUED (ctx.pending_characters) and applied as a
    runtime post-filter after step 5 (there is no discrete partial table to
    mutate). "harsh" (and part of "warm") always accumulates
    ctx.drive_amount, applied once, after everything else, in step 7 —
    regardless of family kind.
 4b. Additive-only: re-cap the (possibly character-boosted) partial table's
    total amplitude back to its PRE-character sum — see the "Headroom"
    paragraph below; this is what keeps step 4's tilt/skew from silently
    invalidating step 8's budget math.
 5. FAMILY EMISSION. additive_decay (bell/metal/cymbal/glass/pluck): one
    transeg per partial INSIDE the shared STRIKE:...rireturn block (the
    envelope key only adjusts onset_scale/bed_multiplier —
    _DECAY_ENVELOPE_ADJUST — the family's OWN per-partial decay/bed shape,
    its defining character, is never replaced). additive_continuous
    (pad/organ_tone/saw_stack/square_stack/noise_wash/sub): partials have NO
    intrinsic envelope; a single shared per-layer transeg (built straight
    from the chosen envelope key's fields) is applied to the whole sum in
    step 6. fm_bell (custom): real 2-operator FM with its own always-on
    index-decay transeg (bright->plain sidebands) PLUS the same shared
    per-layer envelope applied to the carrier's amplitude.
 6. Generic per-layer envelope-gain (additive_continuous + fm_bell only —
    additive_decay families self-shape in step 5 and skip this).
 7. MOTION post-emit hooks (shimmer, breathe) + queued CHARACTER runtime
    fallback (fm_bell only) + harsh/warm drive — all operate directly on the
    fully-formed aL{ns}_raw signal, in that order.
 8. Level + headroom-budget scale -> aL{ns}_out.

GLOBAL motion (spec's top-level "motion", optional) is broadcast: appended to
EVERY layer's own motion list before step 3, each entry keeping its own
unique per-layer index so a global AND a layer-local motion of the same key
never collide (namespaced kL{ns}_m{idx}_... throughout, never by key name).

Headroom: additive family partial tables are authored to ~unit raw peak
(SPEC); _LAYER_BUDGET_NUM / n_layers keeps the WORST-CASE (every partial
phase-aligned at once, across all layers, at max pressure) sum safely under
1.0 — proven empirically by tools/csound_orch_check.cpp's `check` mode, which
prints the measured peak for exactly this reason. That invariant only holds
if characters never let a layer's raw sum exceed what it was authored at —
bright/glassy's positive spectral tilt has no ceiling of its own (an opus
verification pass on this file caught it clipping via ordinary combinations
like "bright glassy saw" well under the 4-layer cap), so step 4b re-caps an
additive layer's total partial amplitude back to its PRE-character sum
(never boosts it — a thinning character's quieter result is left alone),
and step 8 divides fm_bell's budget by an analytical estimate of its
runtime-fallback characters' worst-case expansion (_estimate_pending_runtime_gain,
since fm_bell has no partial table for step 4b to re-cap). Both re-derive
the SAME "~unit-peak regardless of character stack" guarantee this
paragraph claims — see either function's own docstring for the exact
reasoning.

Determinism: everything here is pure Python arithmetic over the caller's
spec dict and csound_lexicon.py's fixed tables — no randomness anywhere.
assemble() called twice with the same spec returns byte-identical text.
"""
import math
import sys
from pathlib import Path

_BACKEND_DIR = str(Path(__file__).resolve().parent)
if _BACKEND_DIR not in sys.path:
    sys.path.insert(0, _BACKEND_DIR)

import csound_lexicon as lex  # noqa: E402  (sys.path fixed up above)

MAX_LAYERS = 4
_LAYER_BUDGET_NUM = 0.35  # final per-layer scale = _LAYER_BUDGET_NUM / n_layers

# additive_decay envelope-key -> (onset_scale, bed_multiplier). See the
# module docstring, step 5: additive_decay families keep their OWN authored
# per-partial bed; the chosen envelope only suppresses the transient
# ('swell') or rescales how much of that natural bed remains audible.
_DECAY_ENVELOPE_ADJUST = {
    "strike":     (1.00, 1.0),
    "swell":      (0.12, 1.0),
    "sustain":    (1.00, 2.6),
    "decay_only": (1.00, 0.0),
}

_CONTRACT_CHANNELS = ("gate", "freq", "vel", "pres", "timb", "trig")


class _LayerCtx:
    """Per-layer code-generation scratchpad. `ns` namespaces every k/a-rate
    variable this layer emits (kL{ns}_..., aL{ns}_...) so two layers can use
    the SAME tool key without ever colliding (namespaced by LAYER INDEX, not
    by tool name — the assembler's own guarantee, SPEC's sub-osc-stacking
    requirement)."""

    def __init__(self, index):
        self.index = index
        self.ns = f"L{index}"
        self.reinit_lines = []   # inside the shared STRIKE:...rireturn block
        self.body_lines = []     # main k/a-rate flow
        self.freq_mod_suffix = ""      # e.g. " * cent(kL1_m0_vibC)"
        self.evolve_terms = []         # [(lfo_var, depth_scale), ...]
        self.partials = None           # mutable list[dict] for additive kinds, else None
        self.kind = None               # "additive_decay" | "additive_continuous" | "custom"
        self.pending_characters = []   # fm_bell-only: [(char_key, amount)] runtime fallback
        self.drive_amount = 0.0        # accumulated waveshaper drive (harsh + a touch of warm)
        self.raw_var = None            # set once family emission completes: "aL{ns}_raw"
        self.out_var = None            # set at finalize: "aL{ns}_out"
        self.character_reading_bits = []  # human-readable character words, in list order
        self.motion_reading_bits = []     # human-readable motion words, in list order


def _clamp(v, lo, hi):
    # Defense-in-depth against untrusted LLM specs (this module is also
    # called directly from tests/tools with arbitrary specs, not only via
    # csound_author.py's own parse-time rejection): NaN fails BOTH
    # comparisons below, so it would silently pass straight through
    # unclamped and reach the orchestra text as a literal `nan` token.
    # Raise instead -- ValueError is this module's documented invalid-spec
    # signal (build_csound_response's assemble() try/except already treats
    # it as a retry-able failure, same as any other unknown-key/shape error).
    if not math.isfinite(v):
        raise ValueError(f"expected a finite number, got {v!r}")
    return lo if v < lo else (hi if v > hi else v)


def _validate_key(key, bucket, what):
    if key not in lex.TOOLS or lex.TOOLS[key]["family"] != bucket:
        raise ValueError(f"unknown {what} key: {key!r} (not a {bucket} tool in the Csound lexicon)")
    return key


# ─── step 3: motion pre-emit hooks ──────────────────────────────────────────

def _apply_motion_pre(ctx, idx, key, amount):
    tool = lex.TOOLS[key]
    kind = tool["kind"]
    if kind == "vibrato":
        rate = tool["rate_hz_base"] + tool["rate_hz_scale"] * amount
        depth = tool["depth_cents_scale"] * amount
        var = f"kL{ctx.ns}_m{idx}_vibC"
        ctx.body_lines.append(f"  {var} oscili {depth:.4f}, {rate:.4f}")
        ctx.freq_mod_suffix += f" * cent({var})"
        ctx.motion_reading_bits.append("vibrato")
        return True
    if kind == "evolve":
        var = f"kL{ctx.ns}_m{idx}_evoLfo"
        depth = tool["depth_scale"] * amount
        ctx.body_lines.append(f"  {var} oscili 1, {tool['rate_hz']:.4f}")
        ctx.evolve_terms.append((var, depth))
        ctx.motion_reading_bits.append("evolve")
        return True
    return False  # shimmer/breathe are post-emit — handled in _apply_motion_post


def _apply_motion_post(ctx, idx, key, amount):
    tool = lex.TOOLS[key]
    kind = tool["kind"]
    if kind == "shimmer":
        ratio = tool["detune_ratio"]
        amp = tool["level_scale"] * amount
        a1 = f"aL{ctx.ns}_m{idx}_shimA"
        a2 = f"aL{ctx.ns}_m{idx}_shimB"
        ctx.body_lines.append(f"  {a1} oscili {amp:.4f}, kL{ctx.ns}_freq0")
        ctx.body_lines.append(f"  {a2} oscili {amp:.4f}, kL{ctx.ns}_freq0*{ratio:.4f}")
        ctx.body_lines.append(f"  {ctx.raw_var} = {ctx.raw_var} + ({a1}+{a2})*kgate")
        ctx.motion_reading_bits.append("shimmer")
        return True
    if kind == "breathe":
        rate = tool["rate_hz"]
        depth = tool["depth_scale"] * amount
        lfo = f"kL{ctx.ns}_m{idx}_brLfo"
        gain = f"kL{ctx.ns}_m{idx}_brGain"
        ctx.body_lines.append(f"  {lfo} oscili 1, {rate:.4f}")
        ctx.body_lines.append(f"  {gain} = 1 + {depth:.4f}*{lfo}")
        ctx.body_lines.append(f"  {ctx.raw_var} = {ctx.raw_var} * {gain}")
        ctx.motion_reading_bits.append("breathe")
        return True
    return False  # vibrato/evolve already handled pre-emit


# ─── step 4: character application ─────────────────────────────────────────

def _apply_tilt_to_partials(partials, db_per_oct, amount, decay_tilt=0.0):
    if not partials:
        return
    for p in partials:
        ratio = max(p["ratio"], 1e-6)
        octaves = math.log2(ratio)
        gain = 10.0 ** ((db_per_oct * amount * octaves) / 20.0)
        p["amp"] = p["amp"] * gain
        if decay_tilt and "decay" in p and octaves > 0:
            # glassy/metallic: upper partials decay FASTER (shorter decay time)
            shrink = _clamp(1.0 - decay_tilt * amount * min(octaves / 3.0, 1.0), 0.15, 1.0)
            p["decay"] = p["decay"] * shrink


def _apply_skew_to_partials(partials, skew_scale, amount, decay_tilt=0.0):
    if not partials:
        return
    for i, p in enumerate(partials):
        sign = 1.0 if (i % 2 == 0) else -1.0
        p["ratio"] = p["ratio"] * (1.0 + amount * skew_scale * (i + 1) * sign)
        p["amp"] = p["amp"] * (1.0 + 0.10 * amount)  # a touch brighter/clangier too
        if decay_tilt and "decay" in p:
            p["decay"] = p["decay"] * _clamp(1.0 - decay_tilt * amount * 0.5, 0.2, 1.0)


def _apply_character(ctx, idx, key, amount):
    amount = _clamp(float(amount), 0.0, 1.0)
    tool = lex.TOOLS[key]
    kind = tool["kind"]
    ctx.character_reading_bits.append(key)

    if kind == "drive":  # harsh
        ctx.drive_amount += tool["drive_scale"] * amount
        return
    if kind == "tilt_drive":  # warm: tilt + a touch of drive
        ctx.drive_amount += tool.get("drive_scale", 0.0) * amount * 0.25
        kind = "tilt"  # fall through to the tilt path below with warm's own db_per_oct

    if ctx.partials is not None:
        if kind == "tilt":
            _apply_tilt_to_partials(ctx.partials, tool["db_per_oct"], amount,
                                     decay_tilt=tool.get("decay_tilt", 0.0))
        elif kind == "skew":  # metallic
            _apply_skew_to_partials(ctx.partials, tool["skew_scale"], amount,
                                     decay_tilt=tool.get("decay_tilt", 0.0))
        return

    # No discrete partial table (fm_bell): queue a runtime post-filter,
    # applied once in step 7 (after the raw signal exists).
    ctx.pending_characters.append((key, amount))


def _apply_pending_character_runtime(ctx, idx, key, amount):
    """fm_bell-only runtime fallback for bright/dark/warm/glassy/metallic:
    there is no discrete partial table to tilt/skew, so these operate on the
    already-synthesized aL{ns}_raw signal instead (a real one-pole tone-filter
    tilt, or — for metallic — a slow deterministic AM 'clang' flavour)."""
    tool = lex.TOOLS[key]
    kind = tool["kind"] if tool["kind"] != "tilt_drive" else "tilt"
    var = f"aL{ctx.ns}_c{idx}"
    if kind == "tilt":
        cutoff = tool["runtime_cutoff_hz"]
        mix = tool["runtime_mix_scale"] * amount
        lp = f"{var}_lp"
        ctx.body_lines.append(f"  {lp} tone {ctx.raw_var}, {cutoff:.2f}")
        if tool.get("db_per_oct", 0.0) >= 0:
            # bright/glassy: add back a scaled highpass-ish complement
            ctx.body_lines.append(f"  {ctx.raw_var} = {ctx.raw_var} + {mix:.4f}*({ctx.raw_var}-{lp})")
        else:
            # dark/warm: blend toward the lowpassed copy
            ctx.body_lines.append(f"  {ctx.raw_var} = {ctx.raw_var}*(1-{mix:.4f}) + {lp}*{mix:.4f}")
    elif kind == "skew":  # metallic fallback: slow deterministic AM clang
        lfo = f"{var}_am"
        ctx.body_lines.append(f"  {lfo} oscili 1, 37")
        ctx.body_lines.append(f"  {ctx.raw_var} = {ctx.raw_var} * (1 + {0.15*amount:.4f}*{lfo})")


def _estimate_pending_runtime_gain(ctx):
    """Custom families only (fm_bell — no discrete partial table to renormalize
    per step 4b): an ANALYTICAL worst-case bound on how much the QUEUED
    runtime-fallback character ops (_apply_pending_character_runtime) can
    expand the signal, computed from the exact same constants that function
    uses. Found by review (opus verification pass) alongside the additive
    over-tilt bug: bright/glassy's fallback is `raw += mix*(raw-lp)` — an
    EXPANSIVE update (mix up to 0.95) with no ceiling — while dark/warm's
    fallback is a convex blend (`raw*(1-mix) + lp*mix`, weights sum to 1)
    and therefore non-expansive/safe, and metallic's AM fallback is bounded
    to +-15%. Only the expansive terms accumulate here; the result divides
    the final budget (step 8) so this family gets the SAME "never exceed the
    authored headroom" guarantee step 4b restores for additive families."""
    gain = 1.0
    for key, amount in ctx.pending_characters:
        tool = lex.TOOLS[key]
        kind = tool["kind"]
        if kind == "drive":
            continue  # harsh never reaches pending_characters (see _apply_character)
        if kind == "tilt_drive":
            kind = "tilt"
        if kind == "tilt" and tool.get("db_per_oct", 0.0) >= 0:  # bright/glassy
            gain *= 1.0 + tool["runtime_mix_scale"] * amount
        elif kind == "skew":  # metallic
            gain *= 1.0 + 0.15 * amount
        # kind == "tilt" with db_per_oct < 0 (dark/warm): convex blend, safe.
    return gain


# ─── step 5: additive emission (additive_decay / additive_continuous) ──────

def _emit_additive(ctx, envelope_key):
    partials = ctx.partials
    freq_mod = ctx.freq_mod_suffix
    ns = ctx.ns
    part_vars = []

    for p, partial in enumerate(partials):
        ratio = partial["ratio"]
        amp = partial["amp"]
        amp_terms = ""
        for lfo_var, depth in ctx.evolve_terms:
            tilt_const = _clamp(0.16 * math.log2(max(ratio, 1e-6)), -0.4, 0.4) * depth
            if abs(tilt_const) > 1e-9:
                amp_terms += f" * (1 + {lfo_var}*{tilt_const:.4f})"

        freq_var = f"kL{ns}_p{p}freq"
        ctx.body_lines.append(f"  {freq_var} = kL{ns}_freq0 * {ratio:.6f}{freq_mod}")

        if ctx.kind == "additive_decay":
            onset_scale, bed_mult = _DECAY_ENVELOPE_ADJUST[envelope_key]
            onset = onset_scale
            hold = _clamp(partial["bed"] * bed_mult, 0.0, 1.0)
            decay = max(partial["decay"], 0.05)
            env_var = f"kL{ns}_p{p}env"
            ctx.reinit_lines.append(
                f"    {env_var} transeg 0, 0.003, 0, {onset:.4f}, {decay:.4f}, -6, {hold:.6f}")
            a_var = f"aL{ns}_p{p}"
            ctx.body_lines.append(f"  {a_var} oscili {amp:.6f}*{env_var}{amp_terms}, {freq_var}")
        else:  # additive_continuous
            a_var = f"aL{ns}_p{p}"
            ctx.body_lines.append(f"  {a_var} oscili {amp:.6f}{amp_terms}, {freq_var}")
        part_vars.append(a_var)

    sum_expr = "+".join(part_vars)
    if ctx.kind == "additive_decay":
        ctx.raw_var = f"aL{ns}_raw"
        ctx.body_lines.append(f"  {ctx.raw_var} = {sum_expr}")
    else:
        ctx.raw_var = f"aL{ns}_sum"
        ctx.body_lines.append(f"  {ctx.raw_var} = {sum_expr}")


# ─── step 5 (custom): fm_bell ───────────────────────────────────────────────

def _emit_fm_bell(ctx):
    ns = ctx.ns
    freq_mod = ctx.freq_mod_suffix
    mod_ratio = 2.0
    car_amp = 0.34

    # FM index: always decays bright -> plain, independent of the chosen
    # envelope (SPEC: "the FM index has its own fixed decay").
    idx_var = f"kL{ns}_idx"
    ctx.reinit_lines.append(f"    {idx_var} transeg 4.0, 0.004, 0, 4.0, 2.4, -6, 0.35")

    mod_var = f"kL{ns}_mod"
    ctx.body_lines.append(
        f"  {mod_var} oscili {idx_var}*kL{ns}_freq0*{mod_ratio:.4f}, "
        f"kL{ns}_freq0*{mod_ratio:.4f}{freq_mod}")

    ctx.raw_var = f"aL{ns}_sum"  # reuses the additive_continuous generic-envelope path
    ctx.body_lines.append(
        f"  {ctx.raw_var} oscili {car_amp:.4f}, kL{ns}_freq0{freq_mod} + {mod_var}")


# ─── step 6: generic per-layer envelope-gain (additive_continuous + fm_bell) ─

def _apply_generic_envelope(ctx, envelope_key):
    env = lex.TOOLS[envelope_key]
    ns = ctx.ns
    env_var = f"kL{ns}_envRaw"
    if env["decay_sec"] > 0:
        ctx.reinit_lines.append(
            f"    {env_var} transeg 0, {env['attack_sec']:.4f}, {env['attack_curve']}, "
            f"{env['onset_level']:.4f}, {env['decay_sec']:.4f}, -6, {env['hold_level']:.4f}")
    else:
        ctx.reinit_lines.append(
            f"    {env_var} transeg 0, {env['attack_sec']:.4f}, {env['attack_curve']}, "
            f"{env['onset_level']:.4f}")
    new_raw = f"aL{ns}_raw"
    ctx.body_lines.append(f"  {new_raw} = {ctx.raw_var} * {env_var}")
    ctx.raw_var = new_raw


# ─── step 7 (tail): harsh/warm drive ────────────────────────────────────────

def _apply_drive(ctx):
    if ctx.drive_amount <= 1e-6:
        return
    drive = ctx.drive_amount
    norm = 1.0 / math.tanh(1.0 + drive)
    drv_var = f"aL{ctx.ns}_drv"
    ctx.body_lines.append(f"  {drv_var} = {ctx.raw_var} * (1 + {drive:.4f})")
    ctx.body_lines.append(f"  {ctx.raw_var} = tanh({drv_var}) * {norm:.6f}")


# ─── layer builder ──────────────────────────────────────────────────────────

def _build_layer(index, layer_spec, global_motion, n_layers):
    if not isinstance(layer_spec, dict):
        raise ValueError(f"layer {index}: expected an object, got {layer_spec!r}")

    tool_key = layer_spec.get("tool")
    if tool_key is None:
        raise ValueError(f"layer {index}: missing required 'tool' key")
    _validate_key(tool_key, "family", "tool")
    family = lex.TOOLS[tool_key]

    register_str = layer_spec.get("register", "8'")
    register_ratio = lex.parse_register(register_str)

    level = _clamp(float(layer_spec.get("level", 0.8)), 0.0, 1.5)

    envelope_key = layer_spec.get("envelope", "strike")
    _validate_key(envelope_key, "envelope", "envelope")

    characters = layer_spec.get("characters") or []
    for c in characters:
        if "key" not in c:
            raise ValueError(f"layer {index}: character entry missing 'key': {c!r}")
        _validate_key(c["key"], "character", "character")

    layer_motion = layer_spec.get("motion") or []
    combined_motion = list(layer_motion) + list(global_motion)
    for m in combined_motion:
        if "key" not in m:
            raise ValueError(f"layer {index}: motion entry missing 'key': {m!r}")
        _validate_key(m["key"], "motion", "motion")

    ctx = _LayerCtx(index)
    ctx.kind = family["kind"]
    ns = ctx.ns

    ctx.body_lines.append(f"  kL{ns}_freq0 = kfreq * {register_ratio:.6f}")

    original_amp_sum = None
    if ctx.kind != "custom":
        ctx.partials = [dict(p) for p in family["partials"]]  # private deep-ish copy
        # Headroom invariant (SPEC: "blocks emit ~unit-peak"; the assembler's
        # budget math at step 8 is only sound if that stays true regardless
        # of which characters got stacked on top). Captured BEFORE any
        # character tilt/skew runs, in step 4 below.
        original_amp_sum = sum(p["amp"] for p in ctx.partials)

    # step 3: pre-emit motion hooks
    for m_idx, m in enumerate(combined_motion):
        _apply_motion_pre(ctx, m_idx, m["key"], _clamp(float(m.get("amount", 0.5)), 0.0, 1.0))

    # step 4: characters
    for c_idx, c in enumerate(characters):
        _apply_character(ctx, c_idx, c["key"], c.get("amount", 0.5))

    # step 4b: re-cap the additive partial table's total amp to its ORIGINAL
    # (pre-character) sum — never boost it. Found by review (opus
    # verification pass): bright/glassy's positive spectral tilt has no
    # ceiling of its own (_apply_tilt_to_partials), so stacking two boosting
    # characters (e.g. "bright glassy saw") — or even "glassy" alone at a
    # high level — could push the layer's raw sum well past what step 8's
    # budget constant assumes, clipping at the final outch. A dark/thinning
    # character is left alone (its quieter result is the intended effect,
    # not something to re-inflate) — only a NET increase gets scaled back
    # down, restoring the "~unit-peak" invariant the budget math depends on
    # for EVERY character combination, not just the ones in the sweep.
    if ctx.partials is not None and original_amp_sum is not None:
        new_amp_sum = sum(p["amp"] for p in ctx.partials)
        if new_amp_sum > original_amp_sum and new_amp_sum > 1e-9:
            scale = original_amp_sum / new_amp_sum
            for p in ctx.partials:
                p["amp"] = p["amp"] * scale

    # step 5: family emission
    if tool_key == "fm_bell":
        _emit_fm_bell(ctx)
    else:
        _emit_additive(ctx, envelope_key)

    # step 6: generic envelope (skipped for additive_decay — self-shaped)
    if ctx.kind != "additive_decay":
        _apply_generic_envelope(ctx, envelope_key)

    # step 7: post-emit motion hooks, queued character fallback, drive
    for m_idx, m in enumerate(combined_motion):
        _apply_motion_post(ctx, m_idx, m["key"], _clamp(float(m.get("amount", 0.5)), 0.0, 1.0))
    for c_idx, (key, amount) in enumerate(ctx.pending_characters):
        _apply_pending_character_runtime(ctx, c_idx, key, amount)
    _apply_drive(ctx)

    # step 8: level + headroom budget. custom families (fm_bell) have no
    # partial table for step 4b's re-cap, so their queued runtime-fallback
    # characters' worst-case expansion is compensated for here analytically
    # instead (never boosted — only ever divided down, same rule as 4b).
    runtime_gain = _estimate_pending_runtime_gain(ctx) if ctx.partials is None else 1.0
    budget = (_LAYER_BUDGET_NUM / n_layers) * level / max(1.0, runtime_gain)
    out_var = f"aL{ns}_out"
    ctx.body_lines.append(f"  {out_var} = {ctx.raw_var} * {budget:.6f}")
    ctx.out_var = out_var

    char_words = " ".join(ctx.character_reading_bits)
    words = " ".join(w for w in [char_words, tool_key] if w)
    reading = f"{words} {register_str} \u00b7 {envelope_key}"
    if ctx.motion_reading_bits:
        reading += " +" + "+".join(ctx.motion_reading_bits)
    ctx.reading = reading
    return ctx


# ─── top-level orchestra emission ──────────────────────────────────────────

def _emit_orchestra(layers):
    lines = []
    lines.append("<CsoundSynthesizer>")
    lines.append("<CsOptions>")
    lines.append("-n -d")
    lines.append("</CsOptions>")
    lines.append("<CsInstruments>")
    lines.append("sr = %SR%")
    lines.append("ksmps = 64")
    lines.append("nchnls = 16")
    lines.append("0dbfs = 1")
    lines.append("")
    lines.append("; Phase-3 assembled orchestra (backend/csound_assembler.py).")
    lines.append("; Voice-bridge prologue is a byte-for-byte mirror of the Phase-1")
    lines.append("; built-in orchestra (src/dsp/CsoundEngine.cpp's buildOrchestra()):")
    lines.append("; gate = voice ACTIVE (incl. release), never closed on note-off;")
    lines.append("; retrigger is the changed2(trig)+reinit/rireturn epoch idiom, never")
    lines.append("; a gate edge (a stolen/reused voice never sees gate fall).")
    lines.append("instr 1")
    lines.append("  ivoice   = p4")
    lines.append('  Sgate    sprintf "gate%d", ivoice')
    lines.append('  Sfreq    sprintf "freq%d", ivoice')
    lines.append('  Svel     sprintf "vel%d", ivoice')
    lines.append('  Spres    sprintf "pres%d", ivoice')
    lines.append('  Stimb    sprintf "timb%d", ivoice')
    lines.append('  Strig    sprintf "trig%d", ivoice')
    lines.append("")
    lines.append("  kgateraw chnget Sgate")
    lines.append("  kfreqraw chnget Sfreq")
    lines.append("  kvel     chnget Svel")
    lines.append("  kpres    chnget Spres")
    lines.append("  ktimb    chnget Stimb")
    lines.append("  ktrigch  chnget Strig")
    lines.append("")
    lines.append("  kgate    portk kgateraw, 0.008")
    lines.append("  kfreq0   portk kfreqraw, 0.008")
    lines.append("  kfreq    limit kfreq0, 20, 12000")
    lines.append("")
    lines.append("  ktrig    changed2 ktrigch")
    lines.append("  if ktrig == 1 then")
    lines.append("    reinit STRIKE")
    lines.append("  endif")
    lines.append("  STRIKE:")
    for layer in layers:
        lines.extend(layer.reinit_lines)
    lines.append("  rireturn")
    lines.append("")

    for layer in layers:
        lines.append(f"  ; ---- layer {layer.index}: {layer.reading} ----")
        lines.extend(layer.body_lines)
        lines.append("")

    lines.append("  ; ---- MPE epilogue (contract, not per-block): kpres -> gentle level")
    lines.append("  ; lift, ktimb (+ a touch of kpres) -> brightness lowpass cutoff. Applies")
    lines.append("  ; identically regardless of what any individual layer/block does with")
    lines.append("  ; pres/timb itself -- every composition gets a living MPE response.")
    lines.append("  kmpeCut   = 700 + 6000*ktimb + 1200*kpres")
    lines.append("  kmpeCut   limit kmpeCut, 100, 18000")
    lines.append("  kmpeMix   = 0.30 + 0.70*ktimb")
    lines.append("  kpresGain = 1.0 + 0.412*kpres")
    lines.append("")
    sum_expr = "+".join(layer.out_var for layer in layers)
    lines.append(f"  asum      = {sum_expr}")
    lines.append("  aMpeLp    tone asum, kmpeCut")
    lines.append("  aMpeOut   = aMpeLp*(1-kmpeMix) + asum*kmpeMix")
    lines.append("  aout      = aMpeOut * kgate * kvel * kpresGain")
    lines.append("  outch     ivoice, aout")
    lines.append("endin")
    lines.append("</CsInstruments>")
    lines.append("<CsScore>")
    for v in range(1, 17):
        lines.append(f"i 1 0 360000 {v}")
    lines.append("e 360000")
    lines.append("</CsScore>")
    lines.append("</CsoundSynthesizer>")
    lines.append("")
    return "\n".join(lines)


def assemble(spec):
    """spec -> (orchestra_text: str, reading: str). Raises ValueError on any
    unknown key (never a silent default). See the module docstring for the
    full per-layer pipeline."""
    if not isinstance(spec, dict):
        raise ValueError(f"spec must be an object, got {spec!r}")

    layer_specs = spec.get("layers")
    if not layer_specs:
        raise ValueError("spec must contain at least one layer")
    if len(layer_specs) > MAX_LAYERS:
        raise ValueError(f"at most {MAX_LAYERS} layers allowed, got {len(layer_specs)}")

    global_motion = spec.get("motion") or []
    for m in global_motion:
        if "key" not in m:
            raise ValueError(f"global motion entry missing 'key': {m!r}")
        _validate_key(m["key"], "motion", "motion")

    n = len(layer_specs)
    layers = [_build_layer(i + 1, ls, global_motion, n) for i, ls in enumerate(layer_specs)]

    orchestra_text = _emit_orchestra(layers)
    reading = "\n".join(layer.reading for layer in layers)
    return orchestra_text, reading
