#!/usr/bin/env python3
"""End-to-end acceptance corpus for the keys-path: PROMPT -> 7B -> assembler -> gate.

The idiom suite (csound_keys_idioms.py) feeds KEYS and certifies the assembler's
DSP; it cannot certify that a natural-language PROMPT actually REACHES the right
keys (the adversarial review flagged a key-fed suite as self-certifying). THIS
corpus closes that: a FROZEN list of natural-language prompts (BJ's own cases +
the capability surface), each run through the REAL source backend over IPC (the
exact wire the plugin uses: mode=="csound" -> 7B understanding -> build_orchestra),
then through the behavioral gate. It is deliberately hand-written NL, NOT derived
from the vocabulary, so it cannot inherit the assembler's blind spot.

Per prompt it reports the routed keys + reading + the behavioral verdict, with an
expected behavior class (sustain/move). A prompt that the 7B fails to route
(ok=false) or that gates wrong is a real finding -- either 7B routing or the
assembler. Run at every milestone.

Usage:  .venv/bin/python tools/csound_keys_corpus.py
"""
import os
import sys
import json

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "backend"))
from test_dco_author import PipeClient, BACKEND_SCRIPT  # noqa
from csound_keys_gate import gate  # noqa

# FROZEN corpus: (prompt, expected {sustain, move}). Hand-written natural
# language spanning the capability classes; NOT regenerable from the vocabulary.
CORPUS = [
    # --- BJ's canonical acceptance cases ---
    ("a pwm square wave",                               {"sustain": "stand", "move": True}),
    ("a glass sound morphing into a sine wave",         {"sustain": "stand", "move": True}),
    ("a square wave morphing into a sine wave",         {"sustain": "stand", "move": True}),
    # --- static tones (must stand, movement NOT required) ---
    ("a pure sine wave",                                {"sustain": "stand", "move": False}),
    ("a bright sawtooth lead",                          {"sustain": "stand", "move": False}),
    ("a warm mellow organ",                             {"sustain": "stand", "move": False}),
    ("a deep sub bass",                                 {"sustain": "stand", "move": False}),
    ("a bright metallic bell",                          {"sustain": "stand", "move": False}),
    ("a hollow wooden clarinet",                        {"sustain": "stand", "move": False}),
    # --- dirt / waveshaping (must stand, bounded) ---
    ("a harsh distorted lead",                          {"sustain": "stand", "move": False}),
    ("a dirty aggressive bass",                         {"sustain": "stand", "move": False}),
    # --- movement (must move: motion or morph or pwm) ---
    ("a dirty overdriven bass that slowly opens up",    {"sustain": "stand", "move": True}),
    ("a shimmering evolving pad",                       {"sustain": "stand", "move": True}),
    ("an evolving analog drone",                        {"sustain": "stand", "move": True}),
    ("a wobbling acid bass",                            {"sustain": "stand", "move": True}),
    ("a metallic bell morphing into a soft flute",      {"sustain": "stand", "move": True}),
    # --- M2: multi-oscillator layering (should route OSC1 + OSC2/3) ---
    ("a fat detuned saw stacked with a deep sub",       {"sustain": "stand", "move": False}),
    ("a bright bell layered over a warm analog pad",    {"sustain": "stand", "move": False}),
    # --- M2: morph-to-zero transient (should end a chain with 'silence') ---
    ("a plucked string that quickly fades to nothing",  {"sustain": "transient", "move": True}),
    ("a short percussive blip that decays to silence",  {"sustain": "transient", "move": True}),
    # --- M4a: noise / Geräusche. The prompt must ROUTE to a noise technique AND
    #     the render must be genuinely aperiodic (noise:True asserts pitchedness).
    #     A routing miss (to a tonal key) fails the noise check -- real parity.
    #     The five STEADY environmental beds enforce `stand` (a held note keeps the
    #     texture going): they caught the 7B over-appending "> silence" (fixed at
    #     the SILENCE-RULE layer, 2026-07-18). Thunder is deliberately `transient`:
    #     "distant rolling thunder" is a decaying EVENT (a roll that swells and
    #     dies), NOT a steady bed -- the 7B faithfully routes `thunder > silence`,
    #     and the transient expectation is STRICTER, asserting it really decays AND
    #     is real aperiodic noise (BJ's authorized morph-to-zero pseudo-env). ---
    ("white noise",                                     {"sustain": "stand", "move": False, "noise": True}),
    ("howling wind",                                    {"sustain": "stand", "move": False, "noise": True}),
    ("heavy rain",                                      {"sustain": "stand", "move": False, "noise": True}),
    ("distant rolling thunder",                         {"sustain": "transient", "move": False, "noise": True}),
    ("ocean waves on the shore",                        {"sustain": "stand", "move": False, "noise": True}),
    ("a crackling campfire",                            {"sustain": "stand", "move": False, "noise": True}),
]


def main():
    client = PipeClient([sys.executable, str(BACKEND_SCRIPT)])
    print("backend ready.")
    npass = nfail = 0
    fails = []
    try:
        for prompt, expect in CORPUS:
            raw = client.request_text({"mode": "csound", "text": prompt})
            try:
                resp = json.loads(raw)
            except Exception:
                resp = {"ok": False, "error": "non-JSON: " + raw[:80]}
            if not resp.get("ok"):
                npass_this = False
                print(f"[FAIL] {prompt!r}\n    - ok=False: {resp.get('error')}")
                nfail += 1
                fails.append((prompt, resp.get("error")))
                continue
            keys = resp.get("technique"), resp.get("adjectives"), resp.get("motion")
            r = gate(resp.get("orchestra", ""), expect, label=prompt[:28])
            m = r["measurements"]
            verdict = "PASS" if r["passed"] else "FAIL"
            print(f"[{verdict}] {prompt!r}")
            print(f"    routed: tech={resp.get('technique')} adj={resp.get('adjectives')} "
                  f"motion={resp.get('motion')}  reading={resp.get('reading')!r}")
            print(f"    gate: peak={m.get('worst_peak')} bench={m.get('bench_us')}us "
                  f"stand(late/max)={m.get('rms_late')}/{m.get('rms_max')} "
                  f"move(cent/amp)={m.get('centroid_travel')}x/{m.get('amp_mod')}")
            if r["passed"]:
                npass += 1
            else:
                nfail += 1
                fails.append((prompt, "; ".join(r["failures"])))
                for f in r["failures"]:
                    print("    - " + f)
    finally:
        client.close()

    print("\n" + "=" * 70)
    print(f"CORPUS: {npass} passed, {nfail} failed  (of {len(CORPUS)} prompts)")
    if fails:
        print("\nFINDINGS:")
        for p, why in fails:
            print(f"  {p!r}: {why}")
    return nfail


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
