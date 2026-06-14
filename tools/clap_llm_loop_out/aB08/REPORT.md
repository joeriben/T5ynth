# CLAP + LLM closed loop — the ear hears, the interpreter steers

Each iteration: SA3 generates, **CLAP** ranks the output into top-k timbre words, and the **instruct LLM** (Qwen2.5-1.5B, the prompt translator reused via the `interpret` IPC mode) transforms those words — per the mode's stance — into the next Prompt B. The audio also carries forward via init_audio (init_noise=0.5). Generation params are loaded VERBATIM from the preset.

- preset: **Creamy-Dreamy SA3**, model `stable-audio-3-small-music`
- duration **11.0s**, 8 steps, CFG 1.0, **seed 2128708858 fixed across iters**
- anchor Prompt A (fixed): `creamy cream, birds chirping`  ·  collision α=+0.800
- develop target (if used): `a deep slow underwater bell`

The cosine-to-anchor is a **drift diagnostic**, not the verdict — listen to the WAVs under each mode dir. The interesting column is **`LLM wrote next B`**: that is the interpreter at work, the thing a raw CLAP tag-append cannot do.

## variation

*Prompt-B variation machine (A = fixed human anchor). The LLM re-writes B each round into a fresh variation in the same family, guided by what the ear hears.*

CLAP cosine to iter-1 anchor: +1.000 → +0.897 (drift +0.103).

| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |
|---:|---|---|---|
| 1 | dreamy dream | dreamy, crystalline, mellow, delicate, sparkly | shimmering stars |
| 2 | shimmering stars | dreamy, crystalline, glassy, mellow, sparkly | twinkling lights |
| 3 | twinkling lights | dreamy, glittering, mellow, glassy, bell-like | moonlit paths |
| 4 | moonlit paths | mellow, dreamy, glassy, gloomy, crystalline | whispering shadows |
| 5 | whispering shadows | dreamy, crystalline, glittering, mellow, glassy | moonlit mist |
| 6 | moonlit mist | dreamy, mellow, glittering, glassy, gloomy | whispering shadows |
| 7 | whispering shadows | dreamy, crystalline, glittering, glassy, mellow | moonlit mist |
| 8 | moonlit mist | dreamy, glassy, crystalline, mellow, gloomy | — |

## abduction

*The machine ABDUCTS: from the bare heard timbres it leaps to an unexpected real-world scene that could make such a sound — proposing an aesthetic frame the human never wrote.*

CLAP cosine to iter-1 anchor: +1.000 → +0.648 (drift +0.352).

| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |
|---:|---|---|---|
| 1 | dreamy dream | dreamy, crystalline, mellow, delicate, gloomy | A haunted lighthouse at dusk |
| 2 | A haunted lighthouse at dusk | dreamy, crystalline, glassy, sparkly, gloomy | A haunted antique store at midnight |
| 3 | A haunted antique store at midnight | mellow, dreamy, crystalline, glittering, glassy | A distant waterfall at night cascading through c |
| 4 | A distant waterfall at night cascading t | dreamy, crystalline, mellow, glassy, delicate | A whispering waterfall at dawn through misty pin |
| 5 | A whispering waterfall at dawn through m | mellow, dreamy, delicate, velvety, crystalline | A whispering waterfall at dawn's misty edge. |
| 6 | A whispering waterfall at dawn's misty e | mellow, dreamy, crystalline, delicate, glassy | A whispering waterfall at dawn through misty pin |
| 7 | A whispering waterfall at dawn through m | mellow, dreamy, delicate, cold, crystalline | A winter's night in a remote mountain cabin |
| 8 | A winter's night in a remote mountain ca | mellow, dreamy, delicate, cold, resonant | — |

## develop

*Development chain toward a target (`a deep slow underwater bell`): each round the LLM nudges the sound one step from where it is now toward the target.*

CLAP cosine to iter-1 anchor: +1.000 → +0.800 (drift +0.200).

| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |
|---:|---|---|---|
| 1 | dreamy dream | dreamy, crystalline, delicate, mellow, gloomy | Echoes of distant whispers, faintly melodic |
| 2 | Echoes of distant whispers, faintly melo | dreamy, crystalline, glassy, mellow, glittering | Echoes faintly, distant, mysterious |
| 3 | Echoes faintly, distant, mysterious | dreamy, glassy, crystalline, mellow, delicate | Softly resonant, ethereal, flowing, serene |
| 4 | Softly resonant, ethereal, flowing, sere | dreamy, crystalline, mellow, glassy, delicate | Echoes of distant waves lapping gently |
| 5 | Echoes of distant waves lapping gently | dreamy, crystalline, mellow, delicate, glittering | Echoes of distant waves lapping gently |
| 6 | Echoes of distant waves lapping gently | mellow, dreamy, delicate, glittering, ethereal | Glimmering whispers echo softly. |
| 7 | Glimmering whispers echo softly. | mellow, dreamy, delicate, crystalline, glassy | Softly resonant, ethereal, flowing, serene |
| 8 | Softly resonant, ethereal, flowing, sere | mellow, dreamy, delicate, velvety, crystalline | — |
