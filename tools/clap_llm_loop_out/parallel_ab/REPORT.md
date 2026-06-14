# CLAP + LLM closed loop — the ear hears, the interpreter steers

Each iteration: SA3 generates, **CLAP** ranks the output into top-k timbre words, and the **instruct LLM** (Qwen2.5-1.5B, the prompt translator reused via the `interpret` IPC mode) transforms those words — per the stance — into the next prompt(s). The audio also carries forward via init_audio (init_noise=0.9). Generation params are loaded VERBATIM from the preset.

- preset: **Creamy-Dreamy SA3**, model `stable-audio-3-small-music`
- duration **11.0s**, 8 steps, CFG 1.0, **seed 2128708858 fixed across iters**
- Prompt A: `creamy cream, birds chirping`  ·  Prompt B: `dreamy dream`  ·  collision α=+0.005
- dual-coupling stances: A = **variation** × B = **variation**  ·  develop target (if used): `a deep slow underwater bell`

The **coupling** (one axis: how far the machine displaces the human input):
- **alpha** — A is the fixed human anchor; only B is rewritten (replace). α sets B's authority over the blend.
- **concat** — BOTH poles: the human original stays as the first impulse and ONLY the latest interpretation is appended (original + last, never the whole chain) — short prompt; the original damps the drift without erasing it.
- **voll** — BOTH poles REPLACED by two interpreter stances — the human input is broken; α blends two machine self-readings (machine-internal A/B collision).

The cosine-to-anchor is a **drift diagnostic**, not the verdict — listen to the WAVs under each dir. The interesting columns are what the **interpreter wrote** — the thing a raw CLAP tag-append cannot do.

## alpha_variation

*coupling **alpha** — A is the fixed human anchor; only B is rewritten (replace). α sets B's authority over the blend.*

CLAP cosine to iter-1 anchor: +1.000 → +0.722 (drift +0.278).

| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |
|---:|---|---|---|
| 1 | dreamy dream | dreamy, mellow, crystalline, delicate, glittering | glittering dreams |
| 2 | glittering dreams | gaseous, delicate, tinny, dreamy, velvety | shimmering hues |
| 3 | shimmering hues | gaseous, clinical, synthetic, saturated, granular | ethereal mist |
| 4 | ethereal mist | mellow, velvety, steely, dreamy, organic | moonlit forest |
| 5 | moonlit forest | mellow, resonant, dreamy, delicate, brilliant | starlit meadow |
| 6 | starlit meadow | mellow, gloomy, delicate, dreamy, brilliant | moonstruck forest |
| 7 | moonstruck forest | mellow, dreamy, gloomy, brilliant, glittering | shimmering mist |
| 8 | shimmering mist | mellow, delicate, granular, dreamy, gloomy | — |

## concat

*coupling **concat** — BOTH poles: the human original stays as the first impulse and ONLY the latest interpretation is appended (original + last, never the whole chain) — short prompt; the original damps the drift without erasing it.*

CLAP cosine to iter-1 anchor: +1.000 → +0.803 (drift +0.197).

| iter | Prompt A (rendered) | Prompt B (rendered) | hears (top-k) | next A | next B |
|---:|---|---|---|---|---|
| 1 | creamy cream, birds chirping | dreamy dream | dreamy, mellow, crystalline, cold, delicate | moonlit dewdrops, whispers r | moonlit melody |
| 2 | creamy cream, birds chirping, mo | dreamy dream, moonlit melody | mellow, dreamy, gloomy, delicate, brilliant | starlit shadows, secrets whi | starlight serenade |
| 3 | creamy cream, birds chirping, st | dreamy dream, starlight serenade | dreamy, crystalline, mellow, resonant, glassy | moonlit whispers, echoes fad | moonlit melody |
| 4 | creamy cream, birds chirping, mo | dreamy dream, moonlit melody | mellow, dreamy, gloomy, delicate, ethereal | whispers through mist, shado | whispering shadows |
| 5 | creamy cream, birds chirping, wh | dreamy dream, whispering shadows | dreamy, mellow, ethereal, granular, crystalline | moonlit paths, whispers soft | moonlit whispers |
| 6 | creamy cream, birds chirping, mo | dreamy dream, moonlit whispers | mellow, dreamy, gloomy, ethereal, round | whispers through misty woods | whispers through misty woods |
| 7 | creamy cream, birds chirping, wh | dreamy dream, whispers through m | mellow, dreamy, delicate, glittering, saturated | moonlit river flowing softly | moonlit river flowing softly |
| 8 | creamy cream, birds chirping, mo | dreamy dream, moonlit river flow | mellow, dreamy, gloomy, delicate, glittering | — | — |

## voll

*coupling **voll** — BOTH poles REPLACED by two interpreter stances — the human input is broken; α blends two machine self-readings (machine-internal A/B collision).*

CLAP cosine to iter-1 anchor: +1.000 → +0.701 (drift +0.299).

| iter | Prompt A (rendered) | Prompt B (rendered) | hears (top-k) | next A | next B |
|---:|---|---|---|---|---|
| 1 | creamy cream, birds chirping | dreamy dream | dreamy, mellow, crystalline, delicate, glittering | moonlit dewdrops, whispers r | glittering dreams |
| 2 | moonlit dewdrops, whispers rustl | glittering dreams | mellow, dreamy, gloomy, brilliant, delicate | starlit shadows, secrets whi | shimmering visions |
| 3 | starlit shadows, secrets whisper | shimmering visions | dreamy, mellow, ethereal, crystalline, sparkly | moonlit paths, whispers fadi | twinkling stars |
| 4 | moonlit paths, whispers fading | twinkling stars | mellow, gloomy, delicate, dreamy, glittering | shadows dancing, secrets whi | moonlit whispers |
| 5 | shadows dancing, secrets whisper | moonlit whispers | mellow, cozy, gloomy, pristine, cold | moonlit paths, whispers rust | whispering shadows |
| 6 | moonlit paths, whispers rustling | whispering shadows | mellow, delicate, gloomy, dreamy, saturated | whispers sighing, moonlight  | moonlit whispers |
| 7 | whispers sighing, moonlight danc | moonlit whispers | resonant, crystalline, gaseous, saturated, delicate | veils shimmering, ethereal b | etheric sighs |
| 8 | veils shimmering, ethereal breat | etheric sighs | mellow, dreamy, crystalline, ethereal, gloomy | — | — |
