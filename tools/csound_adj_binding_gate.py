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
o, r = orch(["saw"], ["hollow"])
check("unparametrised key: saw+hollow keeps post-mix FORMANT",
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
check("averaging: hollow(0.55)+nasal(1.0) -> wave 0.775",
      abs(v - 0.775) < 1e-9, f"got {v}")
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

# ── summary ──────────────────────────────────────────────────────────────────
print()
if FAILS:
    print(f"FAILED ({len(FAILS)}):")
    for f in FAILS:
        print("  ", f)
    sys.exit(1)
print("ALL PASS")
