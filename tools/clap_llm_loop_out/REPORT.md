# CLAP + LLM closed loop — the ear hears, the interpreter steers

Each iteration: SA3 generates, **CLAP** ranks the output into top-k timbre words, and the **instruct LLM** (Qwen2.5-1.5B, the prompt translator reused via the `interpret` IPC mode) transforms those words — per the mode's stance — into the next Prompt B. The audio also carries forward via init_audio (init_noise=0.5). Generation params are loaded VERBATIM from the preset.

- preset: **Creamy-Dreamy SA3**, model `stable-audio-3-small-music`
- duration **11.0s**, 8 steps, CFG 1.0, **seed 2128708858 fixed across iters**
- anchor Prompt A (fixed): `creamy cream, birds chirping`  ·  collision α=+0.005
- develop target (if used): `a deep slow underwater bell`

The cosine-to-anchor is a **drift diagnostic**, not the verdict — listen to the WAVs under each mode dir. The interesting column is **`LLM wrote next B`**: that is the interpreter at work, the thing a raw CLAP tag-append cannot do.

## variation

*Prompt-B variation machine (A = fixed human anchor). The LLM re-writes B each round into a fresh variation in the same family, guided by what the ear hears.*

CLAP cosine to iter-1 anchor: +1.000 → +0.898 (drift +0.102).

| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |
|---:|---|---|---|
| 1 | dreamy dream | dreamy, mellow, crystalline, delicate, cold | moonlit moon |
| 2 | moonlit moon | dreamy, mellow, delicate, crystalline, glittering | silver shimmer |
| 3 | silver shimmer | dreamy, mellow, glittering, gloomy, delicate | moonlit mist |
| 4 | moonlit mist | dreamy, crystalline, glassy, sparkly, mellow | shimmering dewdrops |
| 5 | shimmering dewdrops | crystalline, dreamy, mellow, glassy, delicate | twinkling moonbeams |
| 6 | twinkling moonbeams | dreamy, mellow, crystalline, delicate, glassy | moonlit dewdrops |
| 7 | moonlit dewdrops | dreamy, crystalline, glassy, mellow, delicate | starlit mist |
| 8 | starlit mist | dreamy, crystalline, mellow, glassy, delicate | — |

## abduction

*The machine ABDUCTS: from the bare heard timbres it leaps to an unexpected real-world scene that could make such a sound — proposing an aesthetic frame the human never wrote.*

CLAP cosine to iter-1 anchor: +1.000 → +0.930 (drift +0.070).

| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |
|---:|---|---|---|
| 1 | dreamy dream | dreamy, mellow, crystalline, delicate, glittering | A child's bedroom at sunset, moonlit crystals on |
| 2 | A child's bedroom at sunset, moonlit cry | dreamy, mellow, glittering, crystalline, bell-like | A child's bedroom at sunset, filled with fairy l |
| 3 | A child's bedroom at sunset, filled with | mellow, dreamy, glittering, delicate, gloomy | A haunted library at dusk with ancient books glo |
| 4 | A haunted library at dusk with ancient b | dreamy, mellow, crystalline, delicate, glittering | A child's bedroom at sunset, moonlit crystals on |
| 5 | A child's bedroom at sunset, moonlit cry | dreamy, mellow, delicate, crystalline, glassy | A whispering waterfall at dawn's misty edge. |
| 6 | A whispering waterfall at dawn's misty e | dreamy, mellow, crystalline, delicate, glittering | A child's bedroom at sunset, moonlit crystals on |
| 7 | A child's bedroom at sunset, moonlit cry | dreamy, crystalline, mellow, delicate, glassy | A whispering waterfall at dawn through misty pin |
| 8 | A whispering waterfall at dawn through m | mellow, dreamy, crystalline, delicate, glittering | — |

## develop

*Development chain toward a target (`a deep slow underwater bell`): each round the LLM nudges the sound one step from where it is now toward the target.*

CLAP cosine to iter-1 anchor: +1.000 → +0.662 (drift +0.338).

| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |
|---:|---|---|---|
| 1 | dreamy dream | dreamy, mellow, crystalline, delicate, cold | Echoes of distant whispers, soft and soothing |
| 2 | Echoes of distant whispers, soft and soo | dreamy, mellow, delicate, crystalline, cold | Echoes of distant whispers, soft and soothing |
| 3 | Echoes of distant whispers, soft and soo | dreamy, mellow, delicate, crystalline, cold | Echoes of distant whispers, soft and soothing |
| 4 | Echoes of distant whispers, soft and soo | mellow, dreamy, delicate, glittering, feathery | Soft, resonant hum undercurrent |
| 5 | Soft, resonant hum undercurrent | mellow, dreamy, delicate, crystalline, gloomy | Glimmering, ethereal, shimmering, serene |
| 6 | Glimmering, ethereal, shimmering, serene | mellow, dreamy, delicate, velvety, glittering | Echoes of distant whispers resonate. |
| 7 | Echoes of distant whispers resonate. | mellow, dreamy, delicate, pristine, resonant | Subtle tremolo adds depth |
| 8 | Subtle tremolo adds depth | mellow, dreamy, resonant, delicate, velvety | — |
