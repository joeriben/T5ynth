# CLAP + LLM closed loop — the ear hears, the interpreter steers

Each iteration: SA3 generates, **CLAP** ranks the output into top-k timbre words, and the **instruct LLM** (Qwen2.5-1.5B, the prompt translator reused via the `interpret` IPC mode) transforms those words — per the stance — into the next prompt(s). The audio also carries forward via init_audio (init_noise=0.9). Generation params are loaded VERBATIM from the preset.

- preset: **Creamy-Dreamy SA3**, model `stable-audio-3-small-music`
- duration **11.0s**, 8 steps, CFG 1.0, **seed 2128708858 fixed across iters**
- Prompt A: `creamy cream, birds chirping`  ·  Prompt B: `dreamy dream`  ·  collision α=+0.800
- dual-coupling stances: A = **transcribe** × B = **transcribe**  ·  develop target (if used): `a deep slow underwater bell`

The **coupling** (one axis: how far the machine displaces the human input):
- **alpha** — A is the fixed human anchor; only B is rewritten (replace). α sets B's authority over the blend.
- **concat** — BOTH poles: the human original stays as the first impulse and ONLY the latest interpretation is appended (original + last, never the whole chain) — short prompt; the original damps the drift without erasing it.
- **voll** — BOTH poles REPLACED by two interpreter stances — the human input is broken; α blends two machine self-readings (machine-internal A/B collision).

The cosine-to-anchor is a **drift diagnostic**, not the verdict — listen to the WAVs under each dir. The interesting columns are what the **interpreter wrote** — the thing a raw CLAP tag-append cannot do.

## voll

*coupling **voll** — BOTH poles REPLACED by two interpreter stances — the human input is broken; α blends two machine self-readings (machine-internal A/B collision).*

CLAP cosine to iter-1 anchor: +1.000 → +0.574 (drift +0.426).

| iter | Prompt A (rendered) | Prompt B (rendered) | hears (top-k) | next A | next B |
|---:|---|---|---|---|---|
| 1 | creamy cream, birds chirping | dreamy dream | dreamy, crystalline, delicate, mellow, gloomy | Dreamy, crystalline, delicat | Dreamy, crystalline, delicat |
| 2 | Dreamy, crystalline, delicate, m | Dreamy, crystalline, delicate, m | mellow, cozy, saturated, gloomy, smooth | Mellow, cozy, saturated, glo | Mellow, cozy, saturated, glo |
| 3 | Mellow, cozy, saturated, gloomy, | Mellow, cozy, saturated, gloomy, | mellow, pristine, saturated, gloomy, velvety | Mellow, pristine, saturated, | Mellow, pristine, saturated, |
| 4 | Mellow, pristine, saturated, glo | Mellow, pristine, saturated, glo | mellow, pristine, cozy, dreamy, velvety | Mellow, pristine, cozy, drea | Mellow, pristine, cozy, drea |
| 5 | Mellow, pristine, cozy, dreamy,  | Mellow, pristine, cozy, dreamy,  | mellow, dreamy, cozy, pristine, delicate | Mellow, dreamy, cozy, pristi | Mellow, dreamy, cozy, pristi |
| 6 | Mellow, dreamy, cozy, pristine,  | Mellow, dreamy, cozy, pristine,  | mellow, pristine, cozy, dreamy, cold | Mellow, pristine, cozy, drea | Mellow, pristine, cozy, drea |
| 7 | Mellow, pristine, cozy, dreamy,  | Mellow, pristine, cozy, dreamy,  | mellow, dreamy, gloomy, glittering, brilliant | Mellow, dreamy, gloomy, glit | Mellow, dreamy, gloomy, glit |
| 8 | Mellow, dreamy, gloomy, glitteri | Mellow, dreamy, gloomy, glitteri | mellow, cozy, pristine, cold, saturated | — | — |

## alpha_opposite

*coupling **alpha** — A is the fixed human anchor; only B is rewritten (replace). α sets B's authority over the blend.*

CLAP cosine to iter-1 anchor: +1.000 → +0.563 (drift +0.437).

| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |
|---:|---|---|---|
| 1 | dreamy dream | dreamy, crystalline, delicate, mellow, sparkly | gritty, chaotic, harsh, loud, static |
| 2 | gritty, chaotic, harsh, loud, static | abrasive, distorted, gaseous, murky, tinny | serene, clear, crystalline, bright, warm |
| 3 | serene, clear, crystalline, bright, warm | mellow, dreamy, brilliant, gloomy, delicate | Gloomy, stormy, dark, eerie, vibrant |
| 4 | Gloomy, stormy, dark, eerie, vibrant | mellow, dreamy, gloomy, brilliant, smooth | Bright, bustling, energetic, noisy, distorted |
| 5 | Bright, bustling, energetic, noisy, dist | mellow, cozy, tinny, cold, icy | Whispering, desolate, booming, steamy, humid |
| 6 | Whispering, desolate, booming, steamy, h | mellow, resonant, dreamy, gaseous, delicate | Hot, dry, barren, cracked, scorching |
| 7 | Hot, dry, barren, cracked, scorching | raspy, coarse, abrasive, morphing, woody | Misty, lush, verdant, serene, dewy |
| 8 | Misty, lush, verdant, serene, dewy | resonant, mellow, dreamy, saturated, velvety | — |

## alpha_entkitscher

*coupling **alpha** — A is the fixed human anchor; only B is rewritten (replace). α sets B's authority over the blend.*

CLAP cosine to iter-1 anchor: +1.000 → +0.352 (drift +0.648).

| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |
|---:|---|---|---|
| 1 | dreamy dream | dreamy, crystalline, delicate, sparkly, mellow | simple, stark, serene, quiet |
| 2 | simple, stark, serene, quiet | mellow, dreamy, gloomy, brilliant, clear | Simple, stark, serene, quiet |
| 3 | Simple, stark, serene, quiet | mellow, gloomy, dreamy, brilliant, delicate | Hard, bare, cold, silent |
| 4 | Hard, bare, cold, silent | mellow, cozy, gloomy, dreamy, brilliant | Hard, bare, cold, silent |
| 5 | Hard, bare, cold, silent | mellow, gloomy, cozy, dreamy, brilliant | Hard, bare, cold, silent |
| 6 | Hard, bare, cold, silent | mellow, cozy, gloomy, dreamy, smooth | Harsh, stark, unfeeling |
| 7 | Harsh, stark, unfeeling | resonant, crystalline, mellow, delicate, saturated | Unyielding, bare, indifferent |
| 8 | Unyielding, bare, indifferent | tinny, aggressive, resonant, gaseous, reedy | — |

## alpha_verniedlicher

*coupling **alpha** — A is the fixed human anchor; only B is rewritten (replace). α sets B's authority over the blend.*

CLAP cosine to iter-1 anchor: +1.000 → +0.748 (drift +0.252).

| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |
|---:|---|---|---|
| 1 | dreamy dream | dreamy, crystalline, delicate, mellow, gloomy | crystalline dreams, gentle glow |
| 2 | crystalline dreams, gentle glow | dreamy, mellow, resonant, crystalline, delicate | soft whispers, tender light |
| 3 | soft whispers, tender light | mellow, dreamy, gloomy, glassy, glittering | gentle sighs, soft beams |
| 4 | gentle sighs, soft beams | mellow, dreamy, gloomy, brilliant, glittering | soft whispers, gentle beams |
| 5 | soft whispers, gentle beams | dreamy, mellow, gloomy, delicate, glassy | Soft murmurs, tender beams |
| 6 | Soft murmurs, tender beams | mellow, dreamy, gloomy, glittering, glassy | Whispering sighs, gentle rays |
| 7 | Whispering sighs, gentle rays | dreamy, mellow, glassy, gloomy, crystalline | Soft murmurs, tender light |
| 8 | Soft murmurs, tender light | mellow, dreamy, gloomy, crystalline, glassy | — |

## alpha_planetarizer

*coupling **alpha** — A is the fixed human anchor; only B is rewritten (replace). α sets B's authority over the blend.*

CLAP cosine to iter-1 anchor: +1.000 → +0.593 (drift +0.407).

| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |
|---:|---|---|---|
| 1 | dreamy dream | dreamy, crystalline, delicate, sparkly, mellow | Crystal Dream |
| 2 | Crystal Dream | mellow, velvety, steely, saturated, ethereal | Mystic Serene Nexus |
| 3 | Mystic Serene Nexus | mellow, gloomy, dreamy, glittering, delicate | Mystic Echoes of the Forgotten Realm |
| 4 | Mystic Echoes of the Forgotten Realm | mellow, dreamy, gloomy, ethereal, glittering | Whispering Veil of the Enchanted Forest |
| 5 | Whispering Veil of the Enchanted Forest | mellow, gloomy, dreamy, brilliant, cold | Enigmatic Nightly Symphony / Melancholic Starlig |
| 6 | Enigmatic Nightly Symphony / Melancholic | mellow, dreamy, gloomy, delicate, brilliant | Whispering Shadows of the Ancient Grove |
| 7 | Whispering Shadows of the Ancient Grove | mellow, dreamy, crystalline, ethereal, glassy | Gossamer Echoes of the Celestial Labyrinth |
| 8 | Gossamer Echoes of the Celestial Labyrin | mellow, dreamy, crystalline, glassy, ethereal | — |
