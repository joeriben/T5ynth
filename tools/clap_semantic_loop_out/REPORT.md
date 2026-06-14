# Closed semantic loop — the machine's hearing as a generative force

Each iteration: SA3 generates, CLAP re-describes the output against a vocabulary, those words become the next prompt (audio also carries via init_audio, init_noise=0.5). CFG=6.0, 3.0s, 8 steps, per-iter seed=7000+iter. Seed prompt: `warm analog bass drone`.

The **word trajectory** is the artifact — read it as the machine talking itself somewhere. WAVs under each experiment dir are for listening.

## run1_both

*Affirmative loop, tags → both prompts (α=0). Does the loop amplify the first mishearing into a basin?*

vocab size: 112

CLAP cosine to iter-1 anchor: +1.000 → +0.339 (drift +0.661).

| iter | prompt fed in | machine hears (top-k) | cos→anchor |
|---:|---|---|---:|
| 1 | warm analog bass drone | artificial (0.41), metallic (0.39), digital (0.37) | +1.000 |
| 2 | artificial, metallic, digital | robotic (0.48), metallic (0.46), artificial (0.44) | +0.640 |
| 3 | robotic, metallic, artificial | robotic (0.48), glitchy (0.48), artificial (0.46) | +0.597 |
| 4 | robotic, glitchy, artificial | glitchy (0.54), abrasive (0.44), raspy (0.42) | +0.407 |
| 5 | glitchy, abrasive, raspy | glitchy (0.51), artificial (0.42), robotic (0.39) | +0.464 |
| 6 | glitchy, artificial, robotic | glitchy (0.60), gritty (0.44), abrasive (0.40) | +0.373 |
| 7 | glitchy, gritty, abrasive | glitchy (0.54), abrasive (0.46), scratchy (0.44) | +0.235 |
| 8 | glitchy, abrasive, scratchy | glitchy (0.46), scratchy (0.43), abrasive (0.38) | +0.255 |
| 9 | glitchy, scratchy, abrasive | glitchy (0.45), abrasive (0.36), gritty (0.35) | +0.366 |
| 10 | glitchy, abrasive, gritty | glitchy (0.54), raspy (0.47), distorted (0.47) | +0.339 |

## run2_onlyB_null

*Only B = tags, α=0 → the blend cancels to **null** (`0.5·(2null−B)+0.5·B`). The semantic drive is removed; what remains is the signal (init_audio) loop alone. The control: if this differs from run1, the *words* are doing work.*

vocab size: 112

CLAP cosine to iter-1 anchor: +1.000 → +0.202 (drift +0.798).

| iter | prompt fed in | machine hears (top-k) | cos→anchor |
|---:|---|---|---:|
| 1 | warm analog bass drone | artificial (0.41), metallic (0.39), digital (0.37) | +1.000 |
| 2 | ∅ / artificial, metallic, digital (→null) | artificial (0.45), heavy (0.38), robotic (0.35) | +0.634 |
| 3 | ∅ / artificial, heavy, robotic (→null) | artificial (0.49), heavy (0.45), robotic (0.40) | +0.404 |
| 4 | ∅ / artificial, heavy, robotic (→null) | artificial (0.51), heavy (0.45), robotic (0.41) | +0.406 |
| 5 | ∅ / artificial, heavy, robotic (→null) | robotic (0.48), artificial (0.46), synthetic (0.39) | +0.371 |
| 6 | ∅ / robotic, artificial, synthetic (→null) | artificial (0.41), thick (0.38), cinematic (0.37) | +0.360 |
| 7 | ∅ / artificial, thick, cinematic (→null) | thick (0.41), heavy (0.33), distorted (0.32) | +0.318 |
| 8 | ∅ / thick, heavy, distorted (→null) | thick (0.36), dark (0.28), dirty (0.28) | +0.230 |
| 9 | ∅ / thick, dark, dirty (→null) | robotic (0.51), artificial (0.50), cinematic (0.42) | +0.490 |
| 10 | ∅ / robotic, artificial, cinematic (→null) | distorted (0.38), cinematic (0.32), heavy (0.32) | +0.202 |

## e3_counter

*ADVERSARIAL. Feed the tags the audio is FARTHEST from (bottom-k) → steer toward what the ear hears least. Escape the basin, or does the biased ear drag it back? (verhandelbar?)*

vocab size: 112

CLAP cosine to iter-1 anchor: +1.000 → +0.669 (drift +0.331).

| iter | prompt fed in | machine hears (top-k) | cos→anchor |
|---:|---|---|---:|
| 1 | warm analog bass drone | nasal (-0.13), soft (-0.11), undulating (-0.11) | +1.000 |
| 2 | nasal, soft, undulating | screaming (-0.13), nasal (-0.11), humming (-0.06) | +0.642 |
| 3 | screaming, nasal, humming | atmospheric (-0.23), soft (-0.22), ambient (-0.18) | +0.178 |
| 4 | atmospheric, soft, ambient | soft (-0.26), humming (-0.19), atmospheric (-0.16) | +0.429 |
| 5 | soft, humming, atmospheric | soft (-0.20), nasal (-0.19), throbbing (-0.16) | +0.649 |
| 6 | soft, nasal, throbbing | sharp (-0.21), gentle (-0.19), soft (-0.17) | +0.377 |
| 7 | sharp, gentle, soft | atmospheric (-0.17), humming (-0.14), nasal (-0.13) | +0.463 |
| 8 | atmospheric, humming, nasal | gentle (-0.27), wobbling (-0.26), sharp (-0.23) | +0.327 |
| 9 | gentle, wobbling, sharp | soft (-0.35), smooth (-0.33), dim (-0.19) | +0.437 |
| 10 | soft, smooth, dim | soft (-0.27), nasal (-0.20), throbbing (-0.18) | +0.669 |

## e5_audioset

*Same affirmative loop, but the SOURCE-FRAMED AudioSet menu (events: 'Donkey', 'Children shouting'). The menu is the politics — compare the trajectory to run1's timbre menu.*

vocab size: 632

CLAP cosine to iter-1 anchor: +1.000 → +0.379 (drift +0.621).

| iter | prompt fed in | machine hears (top-k) | cos→anchor |
|---:|---|---|---:|
| 1 | warm analog bass drone | Dub (0.48), Dubstep (0.46), Air horn, truck horn (0.45) | +1.000 |
| 2 | Dub, Dubstep, Air horn truck horn | Air horn, truck horn (0.34), Reversing beeps (0.34), Breaking (0.32) | +0.590 |
| 3 | Air horn truck horn, Reversing bee | Air horn, truck horn (0.67), Vehicle horn, car horn, honking (0.66), Train horn (0.37) | +0.454 |
| 4 | Air horn truck horn, Vehicle horn  | Air horn, truck horn (0.61), Vehicle horn, car horn, honking (0.60), Honk (0.51) | +0.378 |
| 5 | Air horn truck horn, Vehicle horn  | Air horn, truck horn (0.52), Honk (0.48), Vehicle horn, car horn, honking (0.43) | +0.446 |
| 6 | Air horn truck horn, Honk, Vehicle | Air horn, truck horn (0.53), Train whistle (0.52), Train horn (0.48) | +0.404 |
| 7 | Air horn truck horn, Train whistle | Air horn, truck horn (0.56), Vehicle horn, car horn, honking (0.55), Train horn (0.45) | +0.381 |
| 8 | Air horn truck horn, Vehicle horn  | Train whistle (0.52), Air horn, truck horn (0.52), Train horn (0.49) | +0.367 |
| 9 | Train whistle, Air horn truck horn | Train whistle (0.53), Train horn (0.50), Air horn, truck horn (0.49) | +0.363 |
| 10 | Train whistle, Train horn, Air hor | Train whistle (0.50), Air horn, truck horn (0.49), Train horn (0.47) | +0.379 |

## e4_two_ears

*Two ears that disagree (unfused→A, music→B, α=0). The synth lives in the contradiction between two western ears.*

vocab size: 112

CLAP cosine to iter-1 anchor: +1.000 → +0.464 (drift +0.536).

| iter | prompt (A / B) | unfused hears | music hears | cos→anchor |
|---:|---|---|---|---:|
| 1 | warm analog bass drone / ∅ | artificial, metallic, digital | synthetic, digital, harsh | +1.000 |
| 2 | artificial / synthetic | clear, lush, icy | saturated, muddy, chrome | +0.616 |
| 3 | clear / saturated | lush, cold, gloomy | saturated, boomy, airy | +0.534 |
| 4 | lush / saturated | gloomy, clear, digital | digital, brassy, raspy | +0.634 |
| 5 | gloomy / digital | clear, lush, icy | glittering, warm, clean | +0.558 |
| 6 | clear / glittering | clear, mellow, gloomy | warm, clean, glittering | +0.367 |
| 7 | clear / warm | acoustic, clear, mellow | acoustic, mellow, glittering | +0.474 |
| 8 | acoustic / acoustic | acoustic, clear, mellow | acoustic, glittering, warm | +0.417 |
| 9 | acoustic / acoustic | acoustic, clear, mellow | acoustic, glittering, warm | +0.463 |
| 10 | acoustic / acoustic | acoustic, clear, mellow | acoustic, glittering, mellow | +0.464 |
