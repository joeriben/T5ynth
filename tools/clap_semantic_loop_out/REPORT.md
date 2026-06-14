# Closed semantic loop — the machine's hearing as a generative force

Each iteration: SA3 generates, CLAP re-describes the output against a vocabulary, those words become the next prompt (audio also carries via init_audio, init_noise=0.5). CFG=1.0, 3.0s, 8 steps, per-iter seed=7000+iter. Seed prompt: `warm analog bass drone`.

The **word trajectory** is the artifact — read it as the machine talking itself somewhere. WAVs under each experiment dir are for listening.

## run1_both

*Affirmative loop, tags → both prompts (α=0). Does the loop amplify the first mishearing into a basin?*

vocab size: 112

CLAP cosine to iter-1 anchor: +1.000 → +0.618 (drift +0.382).

| iter | prompt fed in | machine hears (top-k) | cos→anchor |
|---:|---|---|---:|
| 1 | warm analog bass drone | brassy (0.34), dark (0.21), heavy (0.20) | +1.000 |
| 2 | brassy, dark, heavy | brassy (0.37), artificial (0.28), huge (0.27) | +0.787 |
| 3 | brassy, artificial, huge | brassy (0.36), artificial (0.35), brilliant (0.30) | +0.543 |
| 4 | brassy, artificial, brilliant | brassy (0.36), artificial (0.34), brilliant (0.31) | +0.512 |
| 5 | brassy, artificial, brilliant | brassy (0.33), artificial (0.32), brilliant (0.26) | +0.645 |
| 6 | brassy, artificial, brilliant | brassy (0.37), artificial (0.33), brilliant (0.29) | +0.634 |
| 7 | brassy, artificial, brilliant | brassy (0.37), artificial (0.30), huge (0.28) | +0.625 |
| 8 | brassy, artificial, huge | brassy (0.39), artificial (0.39), brilliant (0.34) | +0.544 |
| 9 | brassy, artificial, brilliant | brassy (0.33), artificial (0.32), brilliant (0.27) | +0.607 |
| 10 | brassy, artificial, brilliant | brassy (0.38), artificial (0.33), steely (0.30) | +0.618 |

## run2_onlyB_null

*Only B = tags, α=0 → the blend cancels to **null** (`0.5·(2null−B)+0.5·B`). The semantic drive is removed; what remains is the signal (init_audio) loop alone. The control: if this differs from run1, the *words* are doing work.*

vocab size: 112

CLAP cosine to iter-1 anchor: +1.000 → +0.727 (drift +0.273).

| iter | prompt fed in | machine hears (top-k) | cos→anchor |
|---:|---|---|---:|
| 1 | warm analog bass drone | brassy (0.34), dark (0.21), heavy (0.20) | +1.000 |
| 2 | ∅ / brassy, dark, heavy (→null) | distorted (0.18), brassy (0.17), heavy (0.15) | +0.874 |
| 3 | ∅ / distorted, brassy, heavy (→null) | distorted (0.18), brassy (0.18), heavy (0.17) | +0.856 |
| 4 | ∅ / distorted, brassy, heavy (→null) | cinematic (0.22), heavy (0.22), digital (0.20) | +0.776 |
| 5 | ∅ / cinematic, heavy, digital (→null) | cinematic (0.20), heavy (0.18), thick (0.17) | +0.786 |
| 6 | ∅ / cinematic, heavy, thick (→null) | cinematic (0.18), heavy (0.17), thick (0.16) | +0.745 |
| 7 | ∅ / cinematic, heavy, thick (→null) | cinematic (0.19), digital (0.18), heavy (0.17) | +0.645 |
| 8 | ∅ / cinematic, digital, heavy (→null) | digital (0.21), robotic (0.19), heavy (0.18) | +0.667 |
| 9 | ∅ / digital, robotic, heavy (→null) | robotic (0.20), cinematic (0.19), digital (0.19) | +0.734 |
| 10 | ∅ / robotic, cinematic, digital (→null) | cinematic (0.19), robotic (0.18), digital (0.17) | +0.727 |

## e3_counter

*ADVERSARIAL. Feed the tags the audio is FARTHEST from (bottom-k) → steer toward what the ear hears least. Escape the basin, or does the biased ear drag it back? (verhandelbar?)*

vocab size: 112

CLAP cosine to iter-1 anchor: +1.000 → +0.326 (drift +0.674).

| iter | prompt fed in | machine hears (top-k) | cos→anchor |
|---:|---|---|---:|
| 1 | warm analog bass drone | clangy (-0.18), swirling (-0.16), piercing (-0.16) | +1.000 |
| 2 | clangy, swirling, piercing | soft (-0.17), smooth (-0.11), acoustic (-0.08) | +0.477 |
| 3 | soft, smooth, acoustic | pulsing (-0.17), screaming (-0.16), nasal (-0.15) | +0.503 |
| 4 | pulsing, screaming, nasal | droning (-0.25), soft (-0.25), pulsing (-0.24) | +0.400 |
| 5 | droning, soft, pulsing | nasal (-0.20), atmospheric (-0.19), soft (-0.16) | +0.359 |
| 6 | nasal, atmospheric, soft | swirling (-0.12), nasal (-0.10), scratchy (-0.09) | +0.574 |
| 7 | swirling, nasal, scratchy | soft (-0.15), humming (-0.12), nasal (-0.10) | +0.364 |
| 8 | soft, humming, nasal | soft (-0.21), throbbing (-0.21), droning (-0.21) | +0.305 |
| 9 | soft, throbbing, droning | soft (-0.18), atmospheric (-0.16), pulsing (-0.14) | +0.434 |
| 10 | soft, atmospheric, pulsing | soft (-0.18), atmospheric (-0.17), pulsing (-0.14) | +0.326 |

## e5_audioset

*Same affirmative loop, but the SOURCE-FRAMED AudioSet menu (events: 'Donkey', 'Children shouting'). The menu is the politics — compare the trajectory to run1's timbre menu.*

vocab size: 632

CLAP cosine to iter-1 anchor: +1.000 → +0.293 (drift +0.707).

| iter | prompt fed in | machine hears (top-k) | cos→anchor |
|---:|---|---|---:|
| 1 | warm analog bass drone | Air horn, truck horn (0.46), Dub (0.44), Reggae (0.43) | +1.000 |
| 2 | Air horn truck horn, Dub, Reggae | Air horn, truck horn (0.48), Train horn (0.44), Dub (0.39) | +0.711 |
| 3 | Air horn truck horn, Train horn, D | Train whistle (0.60), Train horn (0.58), Train (0.47) | +0.476 |
| 4 | Train whistle, Train horn, Train | Train whistle (0.56), Train horn (0.56), Rail transport (0.44) | +0.449 |
| 5 | Train whistle, Train horn, Rail tr | Train horn (0.55), Train whistle (0.52), Steam whistle (0.47) | +0.238 |
| 6 | Train horn, Train whistle, Steam w | Train horn (0.46), Tire squeal (0.43), Wail, moan (0.41) | +0.254 |
| 7 | Train horn, Tire squeal, Wail moan | Train horn (0.52), Rail transport (0.46), Train whistle (0.44) | +0.247 |
| 8 | Train horn, Rail transport, Train  | Wail, moan (0.46), Tire squeal (0.45), Train horn (0.44) | +0.241 |
| 9 | Wail moan, Tire squeal, Train horn | Train horn (0.50), Tire squeal (0.47), Train whistle (0.44) | +0.259 |
| 10 | Train horn, Tire squeal, Train whi | Train horn (0.51), Train whistle (0.44), Rail transport (0.43) | +0.293 |

## e4_two_ears

*Two ears that disagree (unfused→A, music→B, α=0). The synth lives in the contradiction between two western ears.*

vocab size: 112

CLAP cosine to iter-1 anchor: +1.000 → +0.526 (drift +0.474).

| iter | prompt (A / B) | unfused hears | music hears | cos→anchor |
|---:|---|---|---|---:|
| 1 | warm analog bass drone / ∅ | brassy, dark, heavy | brassy, cinematic, tinny | +1.000 |
| 2 | brassy / brassy | brassy, abrasive, artificial | brassy, cinematic, tinny | +0.811 |
| 3 | brassy / brassy | brassy, abrasive, huge | brassy, cinematic, sterile | +0.712 |
| 4 | brassy / brassy | clear, brassy, brilliant | woody, cinematic, sterile | +0.505 |
| 5 | clear / woody | brassy, clear, mellow | sterile, clean, brilliant | +0.659 |
| 6 | brassy / sterile | brassy, round, brilliant | brilliant, sterile, woody | +0.559 |
| 7 | brassy / brilliant | brassy, artificial, brilliant | brassy, brilliant, raspy | +0.578 |
| 8 | brassy / brassy | brassy, artificial, brilliant | brassy, raspy, brilliant | +0.584 |
| 9 | brassy / brassy | brassy, artificial, brilliant | brassy, shimmering, sterile | +0.528 |
| 10 | brassy / brassy | brassy, artificial, brilliant | brassy, sterile, brilliant | +0.526 |
