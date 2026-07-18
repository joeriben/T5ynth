#!/usr/bin/env python3
"""Mechanical gate for the DECAY-INTENT guard (backend/csound_orch.py).

The morph-to-zero pseudo-env (a chain ending in `silence`) is an ENVELOPE-class
decision, and the deterministic layer -- not the 7B -- owns the envelope: a held
sound STANDS unless the prompt itself asks it to fade/decay/stop. The small-7B is
intermittently non-deterministic (MPS) and sometimes appends "> silence" to a
STANDING prompt, which made "a shimmering evolving pad" decay to nothing. The guard
strips such an unmotivated trailing silence.

That guard is invisible to the two big suites when the 7B happens to route cleanly
(the failure is intermittent by nature), so this test pins it DETERMINISTICALLY with
a FAKE llm that always emits silence-terminated chains: the behaviour then depends
only on the guard, not on the model's mood. Fast -- no 7B, no csound render.

Covers:
  1. `_prompt_wants_decay` over the frozen corpus prompts (transient -> True,
     standing -> False). A cue-set edit that breaks either class fails here.
  2. End-to-end through `build_csound_response` with a canned reply: a no-cue prompt
     must lose the trailing silence (and keep the rest of the morph); a cue-bearing
     prompt must KEEP it (the transient class must still be able to decay).

Usage:  .venv/bin/python tools/csound_decay_guard_test.py
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "backend"))
import csound_orch as co  # noqa

# --- 1. cue classification over the FROZEN corpus prompts ------------------------
# These are the real prompts in tools/csound_keys_corpus.py. Kept in sync by hand:
# they are the behaviour contract, not a derived list.
TRANSIENT = [
    "a plucked string that quickly fades to nothing",
    "a short percussive blip that decays to silence",
    "a vocal stab that fades out",
    "distant rolling thunder",
]
STANDING = [
    "a pwm square wave", "a glass sound morphing into a sine wave",
    "a square wave morphing into a sine wave", "a pure sine wave",
    "a bright sawtooth lead", "a warm mellow organ", "a deep sub bass",
    "a bright metallic bell", "a hollow wooden clarinet", "a harsh distorted lead",
    "a dirty aggressive bass", "a dirty overdriven bass that slowly opens up",
    "a shimmering evolving pad", "an evolving analog drone", "a wobbling acid bass",
    "a metallic bell morphing into a soft flute",
    "a fat detuned saw stacked with a deep sub",
    "a bright bell layered over a warm analog pad",
    "white noise", "howling wind", "heavy rain", "ocean waves on the shore",
    "a crackling campfire", "a sung ahh voice", "a human voice",
    "a bright eee vowel voice", "a voice morphing from ah to ee",
]


def _fake_llm(reply):
    """An llm(text, system_prompt, max_new) stub that always returns `reply`, so the
    outcome is decided purely by the guard -- never by the model."""
    return lambda text, system_prompt, max_new: reply


# A two-oscillator reply whose BOTH chains end in silence (the exact shape the 7B
# produced for "a shimmering evolving pad" when it wrongly made the pad decay).
CANNED = ("OSC1: strings > fm > glass > silence\n"
          "VOL1: 1.0\n"
          "OSC2: additive > glass > silence\n"
          "VOL2: 0.7\n"
          "ADJECTIVES: bright\n"
          "MOTION: evolve\n")


def main():
    fails = []

    for p in TRANSIENT:
        if not co._prompt_wants_decay(p):
            fails.append(f"cue: transient prompt not recognised as decaying: {p!r}")
    for p in STANDING:
        if co._prompt_wants_decay(p):
            fails.append(f"cue: standing prompt wrongly read as decaying: {p!r}")
    print(f"cue classification: {len(TRANSIENT)} transient + {len(STANDING)} standing "
          f"-> {len([f for f in fails if f.startswith('cue:')])} misclassified")

    # --- 2. end-to-end strip / keep through the real assembler -------------------
    # standing prompt: the canned silence MUST be stripped from every oscillator,
    # while the rest of each morph chain survives (the pad still evolves).
    r = co.build_csound_response("a shimmering evolving pad", _fake_llm(CANNED))
    if not r.get("ok"):
        fails.append(f"strip: standing prompt returned not-ok: {r.get('error')}")
    else:
        chains = [o["chain"] for o in r.get("oscillators", [])]
        if not chains:
            fails.append("strip: standing prompt produced no oscillators")
        for ch in chains:
            if ch and ch[-1] in ("silence", "zero"):
                fails.append(f"strip: standing prompt kept a trailing silence: {ch}")
        if chains and chains[0] != ["strings", "fm", "glass"]:
            fails.append(f"strip: morph body not preserved, got {chains[0]}")
        if "silence" in (r.get("reading") or ""):
            fails.append(f"strip: reading still advertises silence: {r.get('reading')!r}")
        print(f"standing prompt -> chains={chains} reading={r.get('reading')!r}")

    # transient prompt: the SAME canned reply must KEEP its trailing silence, so the
    # authorised morph-to-zero pseudo-env still works for genuinely short sounds.
    r2 = co.build_csound_response("a plucked string that quickly fades to nothing",
                                  _fake_llm(CANNED))
    if not r2.get("ok"):
        fails.append(f"keep: transient prompt returned not-ok: {r2.get('error')}")
    else:
        chains2 = [o["chain"] for o in r2.get("oscillators", [])]
        if not any(ch and ch[-1] in ("silence", "zero") for ch in chains2):
            fails.append(f"keep: transient prompt LOST its morph-to-zero: {chains2}")
        print(f"transient prompt -> chains={chains2} reading={r2.get('reading')!r}")

    print("\n" + "=" * 70)
    if fails:
        print(f"DECAY GUARD: {len(fails)} FAILURE(S)")
        for f in fails:
            print("  - " + f)
        return 1
    print("DECAY GUARD: all checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
