# Closed semantic loop — the machine's hearing as a generative force

Each iteration: SA3 generates, CLAP re-describes the output against a vocabulary, those words drive the next prompts (audio also carries via init_audio, init_noise=0.5). Generation params are taken VERBATIM from the preset (no hard-coded duration/seed):

- preset: **Creamy-Dreamy SA3**, model `stable-audio-3-small-music`
- duration **11.0s**, 8 steps, CFG 1.0, magnitude 1.0, noise_sigma 0.0, injection `linear`
- **seed 2128708858 held FIXED across iterations** (preset randomSeed=off) so only the words + init_audio drive drift, not seed noise

**Seed collision (identity, fixed every iteration): A=`creamy cream, birds chirping` × B=`dreamy dream` at α=+0.005.** Except where noted, the machine's previous hearing is APPENDED to each pole (`pole, qualities`); the seed identity leads, the re-hearing modulates. Iter 1 is the pure preset render (the drift anchor). WAVs under each experiment dir are for listening.

## run1_both

*Collision-anchored affirmative loop: qualities appended to BOTH poles, α=ALPHA0. Does the collision survive its own re-hearing?*

vocab size: 112

CLAP cosine to iter-1 anchor (the preset render): +1.000 → +0.782 (drift +0.218).

| iter | qualities appended | machine hears (top-k) | cos→anchor |
|---:|---|---|---:|
| 1 | — (pure preset render) | dreamy (0.48), mellow (0.44), crystalline (0.44) | +1.000 |
| 2 | dreamy, mellow, crystalline | dreamy (0.44), crystalline (0.39), mellow (0.39) | +0.931 |
| 3 | dreamy, crystalline, mellow | dreamy (0.50), mellow (0.49), delicate (0.39) | +0.929 |
| 4 | dreamy, mellow, delicate | mellow (0.46), dreamy (0.45), glittering (0.40) | +0.900 |
| 5 | mellow, dreamy, glittering | dreamy (0.53), mellow (0.49), delicate (0.42) | +0.877 |
| 6 | dreamy, mellow, delicate | dreamy (0.51), mellow (0.46), crystalline (0.37) | +0.877 |
| 7 | dreamy, mellow, crystalline | mellow (0.47), dreamy (0.46), delicate (0.39) | +0.922 |
| 8 | mellow, dreamy, delicate | mellow (0.48), dreamy (0.43), delicate (0.40) | +0.914 |
| 9 | mellow, dreamy, delicate | mellow (0.39), dreamy (0.38), delicate (0.30) | +0.830 |
| 10 | mellow, dreamy, delicate | mellow (0.45), dreamy (0.36), gloomy (0.31) | +0.782 |

## run2_onlyB_null

*Only B = pole-B + qualities, α=0 → the blend cancels to **null**, and the A pole is dropped. The bare init_audio signal loop. The control: the gap to run1 is what the words do.*

vocab size: 112

CLAP cosine to iter-1 anchor (the preset render): +1.000 → +0.898 (drift +0.102).

| iter | qualities appended | machine hears (top-k) | cos→anchor |
|---:|---|---|---:|
| 1 | — (pure preset render) | dreamy (0.50), mellow (0.43), crystalline (0.40) | +1.000 |
| 2 | dreamy, mellow, crystalline (→null) | dreamy (0.54), glassy (0.42), crystalline (0.42) | +0.921 |
| 3 | dreamy, glassy, crystalline (→null) | dreamy (0.52), crystalline (0.43), glassy (0.42) | +0.914 |
| 4 | dreamy, crystalline, glassy (→null) | dreamy (0.52), crystalline (0.47), glassy (0.46) | +0.898 |
| 5 | dreamy, crystalline, glassy (→null) | dreamy (0.50), crystalline (0.48), glassy (0.46) | +0.914 |
| 6 | dreamy, crystalline, glassy (→null) | dreamy (0.49), glassy (0.46), crystalline (0.46) | +0.866 |
| 7 | dreamy, glassy, crystalline (→null) | dreamy (0.51), crystalline (0.48), delicate (0.47) | +0.882 |
| 8 | dreamy, crystalline, delicat (→null) | dreamy (0.46), crystalline (0.45), delicate (0.44) | +0.868 |
| 9 | dreamy, crystalline, delicat (→null) | dreamy (0.48), crystalline (0.47), delicate (0.42) | +0.886 |
| 10 | dreamy, crystalline, delicat (→null) | crystalline (0.50), dreamy (0.50), glassy (0.44) | +0.898 |

## e6_driftB

*ASYMMETRIC: pole A held pure, only pole B drifts under the machine's hearing. Collision = stable anchor vs ear-driven drift.*

vocab size: 112

CLAP cosine to iter-1 anchor (the preset render): +1.000 → +0.925 (drift +0.075).

| iter | qualities appended | machine hears (top-k) | cos→anchor |
|---:|---|---|---:|
| 1 | — (pure preset render) | dreamy (0.45), mellow (0.40), glittering (0.36) | +1.000 |
| 2 | dreamy, mellow, glittering | dreamy (0.49), mellow (0.44), crystalline (0.39) | +0.930 |
| 3 | dreamy, mellow, crystalline | dreamy (0.49), mellow (0.45), glittering (0.39) | +0.860 |
| 4 | dreamy, mellow, glittering | dreamy (0.51), mellow (0.46), glassy (0.46) | +0.865 |
| 5 | dreamy, mellow, glassy | dreamy (0.54), crystalline (0.46), mellow (0.45) | +0.874 |
| 6 | dreamy, crystalline, mellow | dreamy (0.51), mellow (0.46), crystalline (0.44) | +0.897 |
| 7 | dreamy, mellow, crystalline | dreamy (0.46), glassy (0.42), crystalline (0.42) | +0.910 |
| 8 | dreamy, glassy, crystalline | dreamy (0.51), mellow (0.46), crystalline (0.45) | +0.884 |
| 9 | dreamy, mellow, crystalline | dreamy (0.52), mellow (0.45), delicate (0.42) | +0.871 |
| 10 | dreamy, mellow, delicate | dreamy (0.45), mellow (0.43), crystalline (0.40) | +0.925 |

## e3_counter

*ADVERSARIAL. Read the machine's TOP-k, append the ANTONYM of each to BOTH poles (α=ALPHA0). 'qualities fed' = antonyms we steer WITH; 'machine hears' = what it reads anyway. Escape vs recapture gap.*

vocab size: 112

CLAP cosine to iter-1 anchor (the preset render): +1.000 → +0.717 (drift +0.283).

| iter | qualities appended | machine hears (top-k) | cos→anchor |
|---:|---|---|---:|
| 1 | — (pure preset render) | dreamy (0.46), mellow (0.38), crystalline (0.38) | +1.000 |
| 2 | percussive, screaming | dreamy (0.46), crystalline (0.40), glittering (0.38) | +0.940 |
| 3 | percussive, robotic | mellow (0.43), dreamy (0.42), crystalline (0.36) | +0.899 |
| 4 | screaming, percussive | dreamy (0.48), mellow (0.46), gloomy (0.42) | +0.895 |
| 5 | percussive, screaming | mellow (0.43), dreamy (0.41), crystalline (0.37) | +0.893 |
| 6 | screaming, percussive | mellow (0.50), dreamy (0.42), delicate (0.38) | +0.866 |
| 7 | screaming, percussive, punchy | mellow (0.49), dreamy (0.36), delicate (0.35) | +0.745 |
| 8 | screaming, percussive, punchy | mellow (0.46), gloomy (0.29), dreamy (0.29) | +0.693 |
| 9 | screaming, percussive | mellow (0.42), dreamy (0.28), velvety (0.27) | +0.701 |
| 10 | screaming, percussive, humming | mellow (0.35), dreamy (0.27), velvety (0.22) | +0.717 |

## e5_audioset

*Collision-anchored, but the SOURCE-FRAMED AudioSet menu. The menu is the politics — compare to run1's timbre menu.*

vocab size: 632

CLAP cosine to iter-1 anchor (the preset render): +1.000 → +0.889 (drift +0.111).

| iter | qualities appended | machine hears (top-k) | cos→anchor |
|---:|---|---|---:|
| 1 | — (pure preset render) | Electronic organ (0.48), Lullaby (0.47), Mellotron (0.45) | +1.000 |
| 2 | Electronic organ, Lullaby, Mellotr | Electronic organ (0.42), Lullaby (0.39), Mellotron (0.38) | +0.941 |
| 3 | Electronic organ, Lullaby, Mellotr | Mellotron (0.45), Lullaby (0.45), Musical instrument (0.44) | +0.933 |
| 4 | Mellotron, Lullaby, Musical instru | Electronic organ (0.51), Musical instrument (0.50), Mellotron (0.47) | +0.881 |
| 5 | Electronic organ, Musical instrume | Lullaby (0.52), Musical instrument (0.49), Electronic organ (0.47) | +0.940 |
| 6 | Lullaby, Musical instrument, Elect | Musical instrument (0.50), Electronic organ (0.47), Organ (0.44) | +0.907 |
| 7 | Musical instrument, Electronic org | Electronic organ (0.50), Organ (0.48), Musical instrument (0.46) | +0.900 |
| 8 | Electronic organ, Organ, Musical i | Electronic organ (0.52), Organ (0.52), Musical instrument (0.47) | +0.864 |
| 9 | Electronic organ, Organ, Musical i | Electronic organ (0.50), Organ (0.47), Musical instrument (0.45) | +0.889 |
| 10 | Electronic organ, Organ, Musical i | Electronic organ (0.50), Musical instrument (0.48), Organ (0.48) | +0.889 |

## e4_two_ears

*Two ears that disagree, each appended to a different pole. Two western ears split the collision between them.*

vocab size: 112

CLAP cosine to iter-1 anchor (the preset render): +1.000 → +0.864 (drift +0.136).

| iter | A / B (pole + each ear's tag) | unfused hears | music hears | cos→anchor |
|---:|---|---|---|---:|
| 1 | creamy cream, birds chirping / dreamy dream | dreamy, mellow, crystalline | brassy, brilliant, crystalline | +1.000 |
| 2 | creamy cream, birds chirping / dreamy dream, brassy | dreamy, crystalline, glassy | warm, brassy, brilliant | +0.938 |
| 3 | creamy cream, birds chirping / dreamy dream, warm | dreamy, mellow, crystalline | warm, brassy, evolving | +0.901 |
| 4 | creamy cream, birds chirping / dreamy dream, warm | dreamy, glassy, mellow | delicate, warm, hollow | +0.920 |
| 5 | creamy cream, birds chirping / dreamy dream, delicate | dreamy, mellow, crystalline | brassy, delicate, warm | +0.907 |
| 6 | creamy cream, birds chirping / dreamy dream, brassy | dreamy, crystalline, mellow | brassy, delicate, warm | +0.838 |
| 7 | creamy cream, birds chirping / dreamy dream, brassy | dreamy, mellow, crystalline | delicate, brassy, warm | +0.916 |
| 8 | creamy cream, birds chirping / dreamy dream, delicate | dreamy, mellow, crystalline | brassy, delicate, earthy | +0.891 |
| 9 | creamy cream, birds chirping / dreamy dream, brassy | crystalline, dreamy, mellow | brassy, delicate, warm | +0.946 |
| 10 | creamy cream, birds chirping / dreamy dream, brassy | dreamy, mellow, gloomy | brassy, brilliant, crystalline | +0.864 |
