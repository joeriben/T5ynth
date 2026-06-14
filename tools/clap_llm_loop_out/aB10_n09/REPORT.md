# CLAP + LLM closed loop — the ear hears, the interpreter steers

Each iteration: SA3 generates, **CLAP** ranks the output into top-k timbre words, and the **instruct LLM** (Qwen2.5-1.5B, the prompt translator reused via the `interpret` IPC mode) transforms those words — per the mode's stance — into the next Prompt B. The audio also carries forward via init_audio (init_noise=0.9). Generation params are loaded VERBATIM from the preset.

- preset: **Creamy-Dreamy SA3**, model `stable-audio-3-small-music`
- duration **11.0s**, 8 steps, CFG 1.0, **seed 2128708858 fixed across iters**
- anchor Prompt A (fixed): `creamy cream, birds chirping`  ·  collision α=+1.000
- develop target (if used): `a deep slow underwater bell`

The cosine-to-anchor is a **drift diagnostic**, not the verdict — listen to the WAVs under each mode dir. The interesting column is **`LLM wrote next B`**: that is the interpreter at work, the thing a raw CLAP tag-append cannot do.

## variation

*Prompt-B variation machine (A = fixed human anchor). The LLM re-writes B each round into a fresh variation in the same family, guided by what the ear hears.*

CLAP cosine to iter-1 anchor: +1.000 → +0.586 (drift +0.414).

| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |
|---:|---|---|---|
| 1 | dreamy dream | dreamy, crystalline, delicate, sparkly, mellow | shimmering stars |
| 2 | shimmering stars | mellow, dreamy, ethereal, crystalline, gloomy | moonlit mist |
| 3 | moonlit mist | mellow, gloomy, dreamy, delicate, glittering | whispering shadows |
| 4 | whispering shadows | dreamy, mellow, crystalline, glassy, resonant | moonlit whispers |
| 5 | moonlit whispers | mellow, dreamy, gloomy, delicate, cold | whispers through mist |
| 6 | whispers through mist | crystalline, dreamy, ethereal, shadowy, granular | moonlit paths under moonlight |
| 7 | moonlit paths under moonlight | mellow, dreamy, gloomy, clear, bell-like | whispering fog along ancient trails |
| 8 | whispering fog along ancient trails | dreamy, resonant, crystalline, mellow, gaseous | — |

## abduction

*The machine ABDUCTS: from the bare heard timbres it leaps to an unexpected real-world scene that could make such a sound — proposing an aesthetic frame the human never wrote.*

CLAP cosine to iter-1 anchor: +1.000 → +0.303 (drift +0.697).

| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |
|---:|---|---|---|
| 1 | dreamy dream | dreamy, crystalline, delicate, mellow, glassy | A whispering waterfall at dawn through misty pin |
| 2 | A whispering waterfall at dawn through m | crystalline, dreamy, glassy, resonant, delicate | A whispering waterfall at dawn through misty pin |
| 3 | A whispering waterfall at dawn through m | dreamy, crystalline, mellow, glassy, delicate | A whispering waterfall at dawn through misty pin |
| 4 | A whispering waterfall at dawn through m | crystalline, dreamy, mellow, glassy, delicate | A whispering waterfall at dawn through misty pin |
| 5 | A whispering waterfall at dawn through m | dreamy, crystalline, glassy, mellow, delicate | A whispering waterfall at dawn through misty pin |
| 6 | A whispering waterfall at dawn through m | crystalline, dreamy, mellow, glassy, delicate | A whispering waterfall at dawn through misty pin |
| 7 | A whispering waterfall at dawn through m | crystalline, glassy, dreamy, mellow, delicate | A vintage phonograph playing wax records in a co |
| 8 | A vintage phonograph playing wax records | cozy, woody, delicate, percussive, tinny | — |

## develop

*Development chain toward a target (`a deep slow underwater bell`): each round the LLM nudges the sound one step from where it is now toward the target.*

CLAP cosine to iter-1 anchor: +1.000 → +0.519 (drift +0.481).

| iter | Prompt B (this render) | machine hears (top-k) | LLM wrote next B |
|---:|---|---|---|
| 1 | dreamy dream | dreamy, delicate, mellow, crystalline, gloomy | Deep growl resonates, echoing through the water. |
| 2 | Deep growl resonates, echoing through th | shadowy, swirling, earthy, evolving, dark | Rumbling, distant, echoing, growing deeper |
| 3 | Rumbling, distant, echoing, growing deep | synthetic, ethereal, resonant, massive, granular | Echoes fade into distant rumble |
| 4 | Echoes fade into distant rumble | crystalline, ethereal, dreamy, glassy, shadowy | Echoes faintly through misty depths |
| 5 | Echoes faintly through misty depths | dreamy, mellow, crystalline, cold, glassy | Echoes of distant waves lapping gently |
| 6 | Echoes of distant waves lapping gently | swirling, evolving, ethereal, shadowy, crystalline | Echoes of distant, resonant waves |
| 7 | Echoes of distant, resonant waves | dreamy, resonant, evolving, ethereal, shadowy | Grows louder and more distant, fading into mist. |
| 8 | Grows louder and more distant, fading in | dreamy, crystalline, mellow, glassy, shadowy | — |
