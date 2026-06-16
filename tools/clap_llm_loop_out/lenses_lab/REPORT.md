# CLAP + LLM closed loop — the ear hears, the interpreter steers

Each iteration: SA3 generates, **CLAP** ranks the output into top-k timbre words, and the **instruct LLM** (Qwen2.5-1.5B, the prompt translator reused via the `interpret` IPC mode) transforms those words — per the stance — into the next prompt(s). The audio also carries forward via init_audio (init_noise=0.9). Generation params are loaded VERBATIM from the preset.

- preset: **Creamy-Dreamy SA3**, model `stable-audio-3-small-music`
- duration **11.0s**, 8 steps, CFG 1.0, **seed 2128708858 fixed across iters**
- Prompt A: `creamy cream, birds chirping`  ·  Prompt B: `dreamy dream`  ·  collision α=+0.800
- dual-coupling stances: A = **variation** × B = **abduction**  ·  develop target (if used): `a deep slow underwater bell`

The **coupling** (one axis: how far the machine displaces the human input):
- **alpha** — A is the fixed human anchor; only B is rewritten (replace). α sets B's authority over the blend.
- **concat** — BOTH poles: the human original stays as the first impulse and ONLY the latest interpretation is appended (original + last, never the whole chain) — short prompt; the original damps the drift without erasing it.
- **voll** — BOTH poles REPLACED by two interpreter stances — the human input is broken; α blends two machine self-readings (machine-internal A/B collision).

The cosine-to-anchor is a **drift diagnostic**, not the verdict — listen to the WAVs under each dir. The interesting columns are what the **interpreter wrote** — the thing a raw CLAP tag-append cannot do.

## planetarizer

*coupling **alpha** — A is the fixed human anchor; only B is rewritten (replace). α sets B's authority over the blend.*

CLAP cosine to iter-1 anchor: +1.000 → +0.588 (drift +0.412).

| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |
|---:|---|---|---|
| 1 | dreamy dream | dreamy, crystalline, sparkly, delicate, glassy | dreamy dreamscape, shimmering sea, ethereal mist |
| 2 | dreamy dreamscape, shimmering sea, ether | dreamy, shadowy, mellow, ethereal, glassy | dreamy dream, shimmering sea, ethereal mist, del |
| 3 | dreamy dream, shimmering sea, ethereal m | mellow, dreamy, ethereal, delicate, crystalline | heard: whispering wind, dancing flames, crystal  |
| 4 | heard: whispering wind, dancing flames,  | crystalline, resonant, dreamy, ethereal, glassy | Whispering wind, dancing flames, crystal chandel |
| 5 | Whispering wind, dancing flames, crystal | resonant, crystalline, dreamy, ethereal, gaseous | Whispering wind, dancing flames, crystal chandel |
| 6 | Whispering wind, dancing flames, crystal | resonant, dreamy, shadowy, gaseous, eerie | Whispering wind, dancing flames, crystal chandel |
| 7 | Whispering wind, dancing flames, crystal | resonant, ethereal, crystalline, dreamy, gaseous | Echoes of distant drums, shimmering waterfalls,  |
| 8 | Echoes of distant drums, shimmering wate | mellow, ethereal, gloomy, glittering, dreamy | — |
