# CLAP semantic-loop probe

- model: `laion/clap-htsat-unfused`
- template: `{}`
- top-k: 5

## Q2 — vocabulary separability

Redundancy = mean pairwise cosine of the vocabulary's own text embeddings (high ⇒ collinear/redundant labels). Margins = how much the top tag wins by, averaged over clips (high ⇒ decisive ranking). Resolution is bounded by separability, not label count.

| vocab | #labels | redundancy | %pairs>0.9 | mean top1 | margin 1→2 | margin 1→5 |
|---|---:|---:|---:|---:|---:|---:|
| musiccaps | 200 | 0.247 | 0.00 | 0.465 | 0.086 | 0.148 |
| audioset | 632 | 0.156 | 0.00 | 0.517 | 0.063 | 0.119 |
| naive | 112 | 0.291 | 0.00 | 0.470 | 0.059 | 0.186 |

## Q1 — top tags per clip

### `original.wav`

| rank | musiccaps | audioset | naive |
|---:|---|---|---|
| 1 | aggressive (0.448) | Children shouting (0.574) | screaming (0.518) |
| 2 | crowd cheering (0.413) | Booing (0.497) | aggressive (0.448) |
| 3 | reverb (0.331) | Screaming (0.494) | rough (0.343) |
| 4 | youthful (0.305) | Yell (0.448) | shadowy (0.306) |
| 5 | claps (0.295) | Skidding (0.424) | gaseous (0.295) |

### `anchor_family.wav`

| rank | musiccaps | audioset | naive |
|---:|---|---|---|
| 1 | dj (0.339) | Hi-hat (0.400) | chrome (0.300) |
| 2 | electronic drums (0.330) | Beatboxing (0.292) | percussive (0.286) |
| 3 | drums (0.312) | Rapping (0.278) | artificial (0.282) |
| 4 | tambourine (0.291) | Cumbia (0.272) | reedy (0.281) |
| 5 | jazz (0.274) | Cymbal (0.251) | breathy (0.263) |

### `sigma0.050_iter20.wav`

| rank | musiccaps | audioset | naive |
|---:|---|---|---|
| 1 | engaging (0.440) | Speech synthesizer (0.528) | digital (0.528) |
| 2 | addictive (0.413) | Reversing beeps (0.446) | robotic (0.425) |
| 3 | passionate (0.409) | Synthetic singing (0.439) | delicate (0.390) |
| 4 | positive (0.408) | Electro (0.346) | glitchy (0.384) |
| 5 | r&b (0.391) | Ringtone (0.339) | transparent (0.381) |

### `sigma0.200_iter10.wav`

| rank | musiccaps | audioset | naive |
|---:|---|---|---|
| 1 | aggressive (0.574) | Yell (0.565) | aggressive (0.574) |
| 2 | fun (0.347) | Children shouting (0.503) | screaming (0.482) |
| 3 | youthful (0.327) | Shout (0.457) | gaseous (0.345) |
| 4 | mono (0.321) | Donkey, ass (0.442) | rough (0.335) |
| 5 | vocalisation (0.274) | Screaming (0.438) | abrasive (0.283) |

### `sigma0.300_iter15.wav`

| rank | musiccaps | audioset | naive |
|---:|---|---|---|
| 1 | aggressive (0.533) | Yell (0.583) | aggressive (0.533) |
| 2 | youthful (0.370) | Booing (0.507) | screaming (0.432) |
| 3 | male vocal (0.312) | Loudspeaker (0.487) | gaseous (0.349) |
| 4 | vocalisation (0.307) | Chant (0.484) | round (0.288) |
| 5 | amateur recording (0.301) | Shout (0.473) | abrasive (0.277) |

### `sigma0.500_iter10.wav`

| rank | musiccaps | audioset | naive |
|---:|---|---|---|
| 1 | aggressive (0.502) | Children shouting (0.531) | aggressive (0.502) |
| 2 | crowd cheering (0.371) | Booing (0.524) | screaming (0.497) |
| 3 | youthful (0.369) | Shout (0.502) | rough (0.317) |
| 4 | vocal harmony (0.327) | Yell (0.501) | gaseous (0.307) |
| 5 | female singer (0.321) | Screaming (0.482) | abrasive (0.261) |

### `test_sample.wav`

| rank | musiccaps | audioset | naive |
|---:|---|---|---|
| 1 | e-bass (0.419) | Air horn, truck horn (0.441) | punchy (0.336) |
| 2 | heavy metal (0.412) | Dub (0.414) | cinematic (0.311) |
| 3 | punchy kick (0.408) | Bass drum (0.396) | heavy (0.279) |
| 4 | bass guitar (0.385) | Bass guitar (0.380) | fat (0.239) |
| 5 | upright bass (0.365) | Heavy metal (0.379) | distorted (0.227) |

## Q3 — does CLAP hear the resynth drift?

Cosine of each clip's *audio* embedding to the anchor (`original.wav`). If CLAP tracks the loop, this falls as the drift (sigma/iter) rises.

| clip | cos to anchor |
|---|---:|
| `original.wav` | 1.000 |
| `anchor_family.wav` | 0.227 |
| `sigma0.050_iter20.wav` | 0.111 |
| `sigma0.200_iter10.wav` | 0.744 |
| `sigma0.300_iter15.wav` | 0.674 |
| `sigma0.500_iter10.wav` | 0.904 |
| `test_sample.wav` | 0.249 |
