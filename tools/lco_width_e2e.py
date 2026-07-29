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

The signature is odd/even harmonic energy. A pulse's amplitudes go as |sin(pi*n*w)|/n,
so odd-over-even is a direct read of the duty -- huge at 0.5, collapsing as the width
moves off it -- while the centroid barely moves under a duty sweep and would read a
real PWM as inert (see `lco_measure.odd_even_db`). Splitting the note into thirds and
requiring the reading to TRAVEL is therefore a test of the thing itself, not of colour.

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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--prompt", action="append")
    ap.add_argument("--freq", type=float, default=FREQ)
    a = ap.parse_args()

    if not GGUF.exists():
        raise SystemExit(f"author model not installed: {GGUF}")

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
        thirds = [round(M.odd_even_db(y[int(t0 * M.SR):int(t1 * M.SR)], a.freq), 1)
                  for t0, t1 in ((0.3, 2.1), (2.1, 3.9), (3.9, 5.7))]
        cent = [round(M.centroid(y[int(t0 * M.SR):int(t1 * M.SR)]), 1)
                for t0, t1 in ((0.3, 2.1), (2.1, 3.9), (3.9, 5.7))]
        span = max(thirds) - min(thirds)
        moves = span >= 3.0
        bad += not moves
        print(f"  odd/even over the note {thirds}  span {span:.1f} dB   "
              f"{'MOVES' if moves else 'STANDS'}")
        print(f"  centroid              {cent}")
        print(f"  vco2 imodes used: "
              f"{sorted({l.split(',')[2].strip() for l in body.splitlines() if 'vco2' in l and l.count(',') >= 2})}")
        for line in body.splitlines():
            if "vco2" in line or "kpw" in line.lower():
                print(f"    | {line.rstrip()}")
    print(f"\n{'FAIL' if bad else 'ok'}: {bad} of the prompts did not move")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
