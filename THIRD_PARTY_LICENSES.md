# Third-Party Licenses

T5ynth incorporates or depends on the following third-party components.

---

## Stable Audio Open 1.0

**Model:** `stabilityai/stable-audio-open-1.0`
**License:** Stability AI Community License Agreement
**URL:** https://huggingface.co/stabilityai/stable-audio-open-1.0

T5ynth uses the Stable Audio Open 1.0 diffusion model for audio generation.
The model is not included in this repository and must be downloaded separately
by the user from HuggingFace (requires acceptance of the model license).

**Powered by Stability AI**

Per the Stability AI Community License Agreement:
- The model is licensed for research, non-commercial, and commercial use
  (with revenue threshold; see license for details)
- Commercial use above USD $1,000,000 annual revenue requires a separate
  enterprise license from Stability AI
- Users must comply with Stability AI's Acceptable Use Policy
- The model may not be used to create competing foundational AI models
- Full license: https://stability.ai/community-license-agreement

Training data attribution: https://info.stability.ai/attributions

---

## Stable Audio Open Small

**Model:** `stabilityai/stable-audio-open-small`
**License:** Stability AI Community License Agreement
**URL:** https://huggingface.co/stabilityai/stable-audio-open-small

T5ynth uses the Stable Audio Open Small diffusion model as an alternative,
lighter-weight audio generation engine. The model weights are redistributed
via GitHub Releases with the required license and attribution.

**Powered by Stability AI**

The same license terms as Stable Audio Open 1.0 apply (see above):
- Licensed for research, non-commercial, and commercial use
  (with revenue threshold; see license for details)
- Commercial use above USD $1,000,000 annual revenue requires a separate
  enterprise license from Stability AI
- Users must comply with Stability AI's Acceptable Use Policy
- The model may not be used to create competing foundational AI models
- Full license: https://stability.ai/community-license-agreement

Training data attribution: https://info.stability.ai/attributions

---

## JUCE Framework

**Version:** 8.0.6
**License:** GNU AGPLv3 / Commercial JUCE License (dual-licensed)
**URL:** https://juce.com
**Full license:** See `JUCE/LICENSE.md`

T5ynth uses JUCE under the AGPLv3. The JUCE source code is vendored in the
`JUCE/` directory. JUCE itself includes additional third-party components
documented in `JUCE/LICENSE.md` (AudioUnitSDK, Oboe, FLAC, Ogg Vorbis,
VST3 SDK, LV2, and others).

---

## Reverb Impulse Responses — EMT 140 Plate

**Author:** Greg Hopkins
**License:** Creative Commons Attribution (CC BY)
**Source:** https://oramics.github.io/sampled/IR/EMT140-Plate/

**Files:**
- `resources/ir/emt_140_plate_bright.wav`
- `resources/ir/emt_140_plate_medium.wav`
- `resources/ir/emt_140_plate_dark.wav`

EMT 140 plate reverb impulse responses by Greg Hopkins, distributed
under Creative Commons Attribution license.

---

## Python Dependencies

The following Python packages are used by the inference backend
(`backend/requirements.txt`). They are not included in this repository
and are installed separately by the user.

| Package       | License      | Purpose                              |
|---------------|-------------|--------------------------------------|
| diffusers     | Apache 2.0  | Stable Audio pipeline                |
| transformers  | Apache 2.0  | T5 text encoder                     |
| torch         | BSD-3       | Deep learning framework              |
| torchsde      | Apache 2.0  | BrownianTree noise (SDE sampler)     |
| soundfile     | BSD-3       | Audio I/O                            |
| scipy         | BSD-3       | Signal processing                    |
| sentencepiece | Apache 2.0  | T5 tokenizer (vendored in C++ build) |

---

## Signalsmith Stretch

**Version:** 1.1.0
**Author:** Geraint Luff / Signalsmith Audio Ltd.
**License:** MIT
**URL:** https://github.com/Signalsmith-Audio/signalsmith-stretch

Pitch-preserving sample transposition (STFT-based pitch shifting).
Fetched at build time via CMake FetchContent. Header-only library,
compiled into the T5ynth binary.

---

## nlohmann/json

**Version:** 3.11.3
**Author:** Niels Lohmann
**License:** MIT
**URL:** https://github.com/nlohmann/json

JSON parsing for configuration and preset files.
Fetched at build time via CMake FetchContent.

---

## SentencePiece

**License:** Apache 2.0
**URL:** https://github.com/google/sentencepiece

Fetched at build time via CMake FetchContent for the native C++ T5 tokenizer.
