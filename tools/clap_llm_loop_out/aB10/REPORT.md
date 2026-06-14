# CLAP + LLM closed loop — the ear hears, the interpreter steers

Each iteration: SA3 generates, **CLAP** ranks the output into top-k timbre words, and the **instruct LLM** (Qwen2.5-1.5B, the prompt translator reused via the `interpret` IPC mode) transforms those words — per the mode's stance — into the next Prompt B. The audio also carries forward via init_audio (init_noise=0.5). Generation params are loaded VERBATIM from the preset.

- preset: **Creamy-Dreamy SA3**, model `stable-audio-3-small-music`
- duration **11.0s**, 8 steps, CFG 1.0, **seed 2128708858 fixed across iters**
- anchor Prompt A (fixed): `creamy cream, birds chirping`  ·  collision α=+1.000
- develop target (if used): `a deep slow underwater bell`

The cosine-to-anchor is a **drift diagnostic**, not the verdict — listen to the WAVs under each mode dir. The interesting column is **`LLM wrote next B`**: that is the interpreter at work, the thing a raw CLAP tag-append cannot do.

## variation

*Prompt-B variation machine (A = fixed human anchor). The LLM re-writes B each round into a fresh variation in the same family, guided by what the ear hears.*

CLAP cosine to iter-1 anchor: +1.000 → +0.872 (drift +0.128).

| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |
|---:|---|---|---|
| 1 | dreamy dream | dreamy, crystalline, mellow, delicate, sparkly | shimmering stars |
| 2 | shimmering stars | dreamy, mellow, crystalline, sparkly, delicate | twinkling lights |
| 3 | twinkling lights | dreamy, glassy, bell-like, crystalline, mellow | moonlit paths |
| 4 | moonlit paths | dreamy, mellow, glittering, bell-like, gloomy | whispering shadows |
| 5 | whispering shadows | dreamy, mellow, glittering, glassy, gloomy | moonlit mist |
| 6 | moonlit mist | dreamy, mellow, glassy, crystalline, glittering | whispering shadows |
| 7 | whispering shadows | dreamy, mellow, glittering, crystalline, glassy | shimmering mist |
| 8 | shimmering mist | dreamy, mellow, gloomy, glittering, crystalline | — |

## abduction

*The machine ABDUCTS: from the bare heard timbres it leaps to an unexpected real-world scene that could make such a sound — proposing an aesthetic frame the human never wrote.*

CLAP cosine to iter-1 anchor: +1.000 → +0.669 (drift +0.331).

| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |
|---:|---|---|---|
| 1 | dreamy dream | dreamy, crystalline, sparkly, mellow, delicate | A child's breath on frost-covered windowpane at  |
| 2 | A child's breath on frost-covered window | dreamy, crystalline, mellow, glassy, shadowy | A haunted library at midnight during a full moon |
| 3 | A haunted library at midnight during a f | mellow, dreamy, glittering, gloomy, delicate | A vintage disco ball reflecting moonlight throug |
| 4 | A vintage disco ball reflecting moonligh | dreamy, mellow, glassy, delicate, glittering | A child's bedroom at sunset |
| 5 | A child's bedroom at sunset | dreamy, mellow, glittering, sparkly, crystalline | A child's bedroom at sunset, filled with fairy l |
| 6 | A child's bedroom at sunset, filled with | mellow, dreamy, glittering, gloomy, delicate | A vintage disco ball reflecting moonlight throug |
| 7 | A vintage disco ball reflecting moonligh | mellow, dreamy, cold, delicate, crystalline | A winter's night in a cozy, frost-covered cabin |
| 8 | A winter's night in a cozy, frost-covere | mellow, dreamy, delicate, gloomy, glittering | — |

## develop

*Development chain toward a target (`a deep slow underwater bell`): each round the LLM nudges the sound one step from where it is now toward the target.*

CLAP cosine to iter-1 anchor: +1.000 → +0.643 (drift +0.357).

| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |
|---:|---|---|---|
| 1 | dreamy dream | dreamy, mellow, delicate, crystalline, gloomy | Deep growl resonates, echoing through the water. |
| 2 | Deep growl resonates, echoing through th | dreamy, glassy, crystalline, sparkly, delicate | Soft, ethereal hum resonates. |
| 3 | Soft, ethereal hum resonates. | dreamy, sparkly, crystalline, glittering, shadowy | Echoes faintly of distant waves |
| 4 | Echoes faintly of distant waves | mellow, dreamy, glittering, delicate, ethereal | Glimmering, shimmering, luminous, radiant |
| 5 | Glimmering, shimmering, luminous, radian | mellow, dreamy, delicate, gloomy, crystalline | Echoes of distant whispers, soft and soothing |
| 6 | Echoes of distant whispers, soft and soo | mellow, dreamy, delicate, crystalline, gloomy | Glimmering, ethereal, shimmering, serene |
| 7 | Glimmering, ethereal, shimmering, serene | mellow, saturated, dreamy, delicate, resonant | Subtle tremolo adds depth |
| 8 | Subtle tremolo adds depth | mellow, saturated, velvety, resonant, delicate | — |
