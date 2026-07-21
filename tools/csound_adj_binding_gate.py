#!/usr/bin/env python3
"""Gate for the adjective -> per-instrument parameter binding (LCO v1 phase A).

What it locks down (backend/csound_orch.py `_ADJ_PARAM_BINDINGS` /
`_bind_adjectives` / the build_orchestra split):

  1. PARTITION -- for every (instrument, adjective) pair the orchestra changes
     exactly when the binding table predicts a nudge that differs from the
     parameter's default. A change without a prediction is a parity violation
     (the mechanism leaked into sounds it must not touch); a prediction without
     a change is dead wire (the exact failure mode of the params line that was
     never written, LCO_CONCEPT.md §8).
  2. CONSUMPTION -- a bound word's post-mix idiom is gone (never applied
     twice); an UNBOUND word's post-mix idiom is present (nothing silently
     eaten).
  3. PRECEDENCE -- an explicitly LLM-set parameter blocks the nudge AND the
     word falls back to post-mix (blocked != consumed).
  4. NO-OP GUARD -- a nudge landing on the default consumes nothing.
  5. READING -- a nudged parameter shows up as its anchor word, and the
     adjective stays listed (the word was understood; honesty contract).

Pure text-level checks on build_orchestra's output -- no rendering, no ear
claims. Run: .venv/bin/python tools/csound_adj_binding_gate.py
"""
import sys
import os

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "backend"))
import csound_orch as co  # noqa: E402

FAILS = []


def check(name, cond, detail=""):
    print(("PASS " if cond else "FAIL ") + name + (f"  [{detail}]" if detail and not cond else ""))
    if not cond:
        FAILS.append(name)


# Post-mix signature lines per adjective op family (from _emit_adjectives).
# Presence/absence of these is how consumption is asserted at text level.
_POSTMIX_SIGNATURE = {
    "hollow": "reson asig, 700",
    "nasal": "reson asig, 1300",
    "bright": "atone asig, 2200",
    "airy": "atone aprea, 3500",
    "resonant": "reson asig, 1100",
    "thin": "atone asig, 350",
}


def orch(chain, adjectives=(), params=None):
    oscs = [{"chain": list(chain), "vol": 1.0, "register": 1.0,
             "params": params or {}}]
    return co.build_orchestra(oscs=oscs, adjective_keys=list(adjectives))


# ── 1. partition: predicted-changed iff changed ──────────────────────────────
all_adjectives = sorted(co._ADJ_MAP.keys())
for key in sorted(co._ADJ_PARAM_BINDINGS):
    base, _ = orch([key])
    table = co._ADJ_PARAM_BINDINGS[key]
    defaults = co._PARAM_DEFAULTS[key]
    for a in all_adjectives:
        predicted = False
        for pname, mode, val in table.get(a, []):
            base_v = val if mode == "set" else defaults[pname] + val
            v = min(1.0, max(0.0, base_v))
            if abs(v - defaults[pname]) >= 1e-9:
                predicted = True
        o, _r = orch([key], [a])
        # strip the post-mix block difference: compare only the per-osc body
        # (everything before the "kvsum" mix line is oscillator territory).
        body = o.split("kvsum")[0]
        body_base = base.split("kvsum")[0]
        changed = body != body_base
        check(f"partition {key}+{a}: source-change iff predicted "
              f"({'predicted' if predicted else 'must-not-touch'})",
              changed == predicted)

# ── 2. consumption both ways ────────────────────────────────────────────────
o, r = orch(["analog_osc"], ["hollow"])
check("consumed: analog_osc+hollow drops post-mix FORMANT",
      _POSTMIX_SIGNATURE["hollow"] not in o)
o, r = orch(["analog_osc"], ["airy"])
check("unbound: analog_osc+airy keeps post-mix AIR",
      _POSTMIX_SIGNATURE["airy"] in o)
o, r = orch(["organ"], ["hollow"])
check("unparametrised key: organ+hollow keeps post-mix FORMANT",
      _POSTMIX_SIGNATURE["hollow"] in o)
# multi-osc: one binder in the patch consumes for the whole mix
o, r = co.build_orchestra(oscs=[
    {"chain": ["drum_head"], "vol": 1.0, "register": 1.0, "params": {}},
    {"chain": ["saw"], "vol": 1.0, "register": 1.0, "params": {}},
], adjective_keys=["bright"])
check("multi-osc: drum_head binds bright, mix loses post-mix BRIGHT",
      _POSTMIX_SIGNATURE["bright"] not in o)

# ── 3. precedence: explicit param blocks nudge, word falls to post-mix ───────
o, r = orch(["analog_osc"], ["hollow"],
            params={"analog_osc": {"wave": 0.0}})
check("precedence: explicit wave blocks hollow, post-mix FORMANT survives",
      _POSTMIX_SIGNATURE["hollow"] in o)

# ── 4. no-op guard (synthetic: a set exactly on the default) ─────────────────
oscs = [{"chain": ["analog_osc"], "vol": 1.0, "register": 1.0, "params": {}}]
saved = co._ADJ_PARAM_BINDINGS["analog_osc"].get("__test__")
co._ADJ_PARAM_BINDINGS["analog_osc"]["__test__"] = [
    ("wave", "set", co._PARAM_DEFAULTS["analog_osc"]["wave"])]
try:
    consumed = co._bind_adjectives(oscs, ["__test__"])
    check("no-op guard: default-landing nudge consumes nothing",
          not consumed and not oscs[0]["params"])
finally:
    if saved is None:
        del co._ADJ_PARAM_BINDINGS["analog_osc"]["__test__"]
    else:
        co._ADJ_PARAM_BINDINGS["analog_osc"]["__test__"] = saved

# ── 5. averaging + reading honesty ───────────────────────────────────────────
oscs = [{"chain": ["analog_osc"], "vol": 1.0, "register": 1.0, "params": {}}]
co._bind_adjectives(oscs, ["hollow", "nasal"])
v = oscs[0]["params"]["analog_osc"]["wave"]
check("nudge sum: default 0.45 + hollow(+.10) + nasal(+.45) -> wave 1.0 (clamped)",
      abs(v - 1.0) < 1e-9, f"got {v}")
_o, r = orch(["analog_osc"], ["hollow"])
check("reading: nudged param shows anchor word", "(square)" in r)
check("reading: the adjective stays listed", "hollow" in r)
_o, r = orch(["fm_ep"], ["bright"])
check("reading: fm_ep+bright shows (hard)", "(hard)" in r)
_o, r = orch(["drum_head"], ["bright"])
check("reading: drum_head+bright shows (tight)", "(tight)" in r)

# ── 6. purity + idempotency (build_orchestra must not mutate its input) ──────
# a caller's params dict survives the call untouched...
caller_params = {}
oscs_in = [{"chain": ["analog_osc"], "vol": 1.0, "register": 1.0,
            "params": caller_params}]
o1, r1 = co.build_orchestra(oscs=oscs_in, adjective_keys=["hollow"])
check("purity: caller params dict not mutated",
      caller_params == {} and oscs_in[0]["params"] == {})
# ...and a SECOND build on the SAME list is byte-identical (idempotent).
o2, r2 = co.build_orchestra(oscs=oscs_in, adjective_keys=["hollow"])
check("idempotency: second build on same oscs is identical", o1 == o2 and r1 == r2)
# rendered_out reflects the nudge (metadata matches orchestra).
rendered = []
co.build_orchestra(oscs=oscs_in, adjective_keys=["hollow"], rendered_out=rendered)
check("rendered_out: nudge present in echoed oscillators",
      rendered and rendered[0]["params"].get("analog_osc", {}).get("wave") == 0.55)
check("rendered_out: still did not leak into caller", oscs_in[0]["params"] == {})

# ── 7. waveform words delegate to the single analogue VCO ────────────────────
# square/saw/pulse/triangle are now _emit_analog_osc at a wave anchor, not bespoke
# oscillators: the old idioms are gone, and an adjective bends them at the source.
for wk, anchor in (("square", "square"), ("saw", "saw"),
                   ("pulse", "pulse"), ("triangle", "triangle")):
    o, _r = orch([wk])
    check(f"{wk}: delegates to analog_osc (aosc body, no bespoke branch)",
          "aosc0" in o and "wave seg" in o)
check("square: old comparator-drift branch is gone",
      "comparator threshold drift" not in orch(["square"])[0])
# "thin square" BENDS the square narrower (source changes) AND consumes thin.
sq, _ = orch(["square"])
tsq, tr = orch(["square"], ["thin"])
check("thin square: bends the source (wave nudged from the square anchor)",
      sq.split("kvsum")[0] != tsq.split("kvsum")[0])
check("thin square: thin is consumed (post-mix THIN gone)",
      _POSTMIX_SIGNATURE["thin"] not in tsq)
check("thin square: reading still lists thin (understood at the source)",
      "thin" in tr)
# a cross-waveform morph stays TWO distinct stages (keys not merged, no collapse).
o, mr = orch(["triangle", "saw"])
check("triangle>saw: stays a two-stage morph (distinct keys survive)",
      mr.count(">") == 1 and o.count("clean oscillator (pre-drive)") >= 2)

# ── 8. pwm rate is a real parameter driven by slow/fast ──────────────────────
import re as _re


def _pwm_lfo_hz(orch):
    m = _re.search(r"klfo0\s+oscili 0\.5, ([0-9.]+)", orch)
    return float(m.group(1)) if m else None


def _pwm_hz(motion=None, params=None):
    """The REAL composition: the speed word binds (build_csound_response's step),
    THEN build_orchestra renders the resulting rate. build_orchestra itself no
    longer binds -- that would be too late on the live path (see section 9)."""
    oscs = [{"chain": ["pwm"], "vol": 1.0, "register": 1.0, "params": params or {}}]
    mk = co._bind_speed_to_pwm(oscs, motion) if motion else motion
    o, _ = co.build_orchestra(oscs=oscs)
    return _pwm_lfo_hz(o), mk


hz, _ = _pwm_hz()
check("pwm default rate -> 0.5 Hz (1 s per half-cycle)", hz == 0.5)
hz, mk = _pwm_hz("slow")
check("slow pwm -> 0.25 Hz + consumed to static", hz == 0.25 and mk == "static")
hz, mk = _pwm_hz("fast")
check("fast pwm -> 1.0 Hz + consumed to static", hz == 1.0 and mk == "static")
# an explicit pwm(rate=...) wins the VALUE over the speed word.
hz, _ = _pwm_hz("slow", {"pwm": {"rate": 1.0}})
check("explicit pwm rate beats the speed motion", hz == 1.0)
# a patch with NO pwm keeps its global motion untouched.
check("slow+flute leaves the global motion intact",
      co._bind_speed_to_pwm([{"chain": ["flute"], "params": {}}], "slow") == "slow")

# ── 9. LIVE PATH: slow/fast bind BEFORE the spectral-motion drop gate ─────────
# Guards the adversarial find: build_csound_response's post-mix motion gate
# consumed slow/fast on the (always non-sparse) pwm patch and flagged the movement
# as un-renderable BEFORE the pwm binding ran -- so the sweep never moved and the
# user was misinformed. Exercise the REAL entry point with a canned reply.
def _fake_llm(reply):
    return lambda user, system, maxtok: reply


for word, want in (("slowly", 0.25), ("quickly", 1.0)):
    resp = co.build_csound_response(f"a {word} pwm",
                                    _fake_llm(f"OSC1: pwm\nMOTION: {word}"))
    got = resp.get("ok") and _pwm_lfo_hz(resp.get("orchestra", "")) == want
    dropped = any(f.get("word") in ("slow", "fast") and f.get("tier") == "dropped"
                  for f in resp.get("flags", []))
    check(f"live path: '{word} pwm' sweeps at {want} Hz", got)
    check(f"live path: '{word} pwm' does NOT falsely drop the motion", not dropped)

# ── summary ──────────────────────────────────────────────────────────────────
print()
if FAILS:
    print(f"FAILED ({len(FAILS)}):")
    for f in FAILS:
        print("  ", f)
    sys.exit(1)
print("ALL PASS")
