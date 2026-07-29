#!/usr/bin/env python3
"""Does the AUTHOR still write a moving pulse width after the `analog_osc` rewrite?

`analog_osc` no longer carries a hardwired duty swing: the width is a position and a
movement source is something the author WRITES. That is a capability moving from one
layer to another, so it has to be proven at the layer that now owns it, on the prompt
path the player actually drives -- not in a harness that sets the parameter itself.

So this runs `build_csound_response` against the real shipped author model (the 4-bit
GGUF through llama.cpp, the same closure `pipe_inference.py`'s mode=="csound" builds),
renders what comes back, and asks an OBJECTIVE question of the audio: does the duty
travel over the note?

IT REPORTS, IT DOES NOT JUDGE, and the first version's judgement is why. It split the
note in thirds and called a span below 3 dB "STANDS", which was wrong three separate
ways -- an adversarial review measured every one:

  * it failed the lexicon's OWN reference PWM (the `width=moving` block, span 2.8 dB),
    so the one answer it wants the author to write could never pass;
  * a third is 1.8 s, so anything above about 1 Hz averages out inside one -- the same
    body at 1/2/4/8 Hz all read ~1.1 dB, and the `rate` axis it replaced went to 8;
  * it was reading `age`, not the LFO. With the modulation depth set to ZERO the span
    came out LARGER (5.1 dB) than at full depth, because `kjit` drifts slowly through
    the neighbourhood of 0.5, where odd/even is steepest.

And odd/even is the wrong signature for half of what gets asked: a triangle morphing
into a sawtooth moves the CENTROID and barely touches odd/even at all.

So this prints the numbers and the code the author wrote, and nothing else. The
project's established movement reading is `centroid_travel_hz` / `centroid_motion_hz`
from `tools/lco_author_offline.py --measure`, which is also what a change to
`_SYSTEM_HEAD` is measured with over `tools/lco_morph_corpus.txt`, before AND after.
The verdict on a sound is BJ's ear.

    .venv/bin/python tools/lco_width_e2e.py
    .venv/bin/python tools/lco_width_e2e.py --prompt "a saturated pwm square wave"
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "backend"))
sys.path.insert(0, str(REPO / "tools"))

import lco_measure as M  # noqa: E402

GGUF = Path.home() / ("Library/T5ynth/models/coder/gemma-4-12b-it-qat-q4_0/"
                      "gemma-4-12b-it-qat-q4_0.gguf")
FREQ = 220.0
DUR = 6.0
PROMPTS = [
    "a saturated pwm square wave",
    "a slow pulse width modulation",
    "a triangle morphing into a sawtooth",
]


def oe(y, t0, t1, f):
    """Odd-over-even harmonic energy in ONE window. `lco_measure.odd_even_db` takes no
    window, so the bounds have to go to `partials` as t0/t1 -- slicing the array and
    calling it left its default 0.5..3.5 s in place, silently discarding the front of
    every window and returning nothing at all for a window shorter than 0.5 s."""
    lin = [10 ** (v / 20.0) for v in M.partials(y, f, n=16, t0=t0, t1=t1)]
    return round(float(20 * np.log10(max(sum(lin[0::2]), 1e-12)
                                     / max(sum(lin[1::2]), 1e-12))), 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--prompt", action="append")
    ap.add_argument("--freq", type=float, default=FREQ)
    ap.add_argument("--library",
                    help="a lco_library.json other than backend/'s -- for running the "
                         "SAME prompts against an older library, which is the only way "
                         "to tell an author weakness from a regression I caused. "
                         "Decoding is greedy, so the comparison is exact.")
    a = ap.parse_args()

    if not GGUF.exists():
        raise SystemExit(f"author model not installed: {GGUF}")

    import lco_write
    if a.library:
        lco_write.LIBRARY_PATH = Path(a.library).resolve()
        lco_write._library_cache = None
        print(f"library: {lco_write.LIBRARY_PATH}")
    from lco_write import build_csound_response
    from pipe_inference import run_gguf_instruct

    def llm(text, system_prompt, max_new_tokens, on_delta=None):
        return run_gguf_instruct(text, str(GGUF), system_prompt,
                                 max_new_tokens=max_new_tokens, on_delta=on_delta)

    bad = 0
    for prompt in (a.prompt or PROMPTS):
        print(f"\n=== {prompt!r}")
        resp = build_csound_response(prompt, llm)
        if not resp.get("ok"):
            print(f"  AUTHORING FAILED: {resp.get('error')}")
            bad += 1
            continue
        # `orchestra` is already wrapped in the host scaffold; `params_text` is the
        # bare body, which is what lco_measure.render wraps in its own.
        body = resp["params_text"]
        print(f"  READING: {resp.get('reading', '')}")
        y, err = M.render(body, dur=DUR, freq=a.freq, preroll=0.5)
        if err:
            print(f"  RENDER FAILED: {err}")
            bad += 1
            continue
        y = np.asarray(y)
        m = M.measure(y, a.freq)
        track = [oe(y, t, t + 0.15, a.freq) for t in np.arange(0.3, DUR - 0.4, 0.1)]
        print(f"  duty (odd/even, 0.15 s windows): span {max(track) - min(track):>5.1f} dB"
              f"   min {min(track):>6.1f}  max {max(track):>6.1f}"
              f"   -- confounded by `age`, read it with the code")
        print(f"  colour: centroid {m['centroid']}  travel {m.get('centroid_travel_hz')} Hz"
              f"  motion {m.get('centroid_motion_hz')} Hz  coherence {m.get('motion_coherence')}")
        print(f"  level:  rms {m['rms_db']} dB  travel {m.get('loudness_travel_db')} dB")
        print("  ---- the body, verbatim ----")
        for line in body.splitlines():
            print(f"    | {line.rstrip()}")
    print(f"\n{len(a.prompt or PROMPTS) - bad} of {len(a.prompt or PROMPTS)} authored; "
          "no verdict is offered on the sound (see the module docstring)")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
