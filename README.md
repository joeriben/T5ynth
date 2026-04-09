# T5ynth

> **For the full formatted user guide, open [`resources/T5ynth_Guide.html`](resources/T5ynth_Guide.html) in your browser.**

(note: this description has been written by the co-coding AI, Claude Opus 4.6)


**A text-to-sound synthesizer that navigates the T5 embedding space of a diffusion audio model.**

T5ynth is a JUCE-based synthesizer plugin (Standalone / VST3 / AU) that deliberately repurposes Stability AI's [Stable Audio Open](https://huggingface.co/stabilityai/stable-audio-open-1.0) model as an oscillator — not to generate finished music, but to produce raw sonic material for human–nonhuman artistic collaboration. Where text-to-audio models are designed to substitute creative labor (type a description, get a result), T5ynth inverts this relationship: the AI produces material, the human creates. This is not a consumer tool — it is an instrument that requires musicianship.

T5ynth is a personal side-project by Prof. Dr. Benjamin Jörissen, UNESCO Chair in Digital Culture and Arts in Education (UCDCAE), Friedrich-Alexander-Universität Erlangen-Nürnberg, and part of the [UCDCAE AI Lab Software Collection](https://github.com/joeriben/ucdcae-ai-lab).

T5ynth is inspired by two research projects:

- [AI for Arts Education (AI4ArtsEd)](https://kubi-meta.de/ai4artsed), conducted together with the University of Cologne and the German Research Institute for Artificial Intelligence (DFKI) Kaiserslautern
- [ComeArts Across](https://comearts.uni-due.de/comenets/artsacross/), a research project for the development of digital cultural teacher education

(both funded by the Federal Ministry for Education, Family, Senior Citizens, Women and Youth (BMBFSFJ)).

T5ynth is dedicated to my dear colleague at the DFKI, musician and AI researcher Dr. Stephan Baumann, without whom AI4ArtsEd would not have come into being.

---

## Context: Why This Exists

T5ynth emerged from [AI4ArtsEd](https://kubi-meta.de/ai4artsed), a research project investigating alternative, non-standard uses of AI for educational purposes. AI4ArtsEd deliberately subverts consumerist user-subject positions and approaches AI from a critical, empowerment-oriented perspective.

### The Problem with Generative Audio AI

Text-to-audio models like Stable Audio are designed for a specific purpose: generating finished audio content from text prompts. Their intended use case is the substitution of creative labor — type a description, get a result. This positions the user as a consumer of AI output rather than an active creative agent.

### Open Source as Strategy

Stable Audio Open deserves credit: Unlike its commercial siblings which rob artists of their works, it was trained on ~486,000 Creative Commons-licensed recordings from Freesound and the Free Music Archive — not on copyrighted music. Stability AI conducted copyright verification and removed flagged content before training. This is genuinely better practice than much of the industry.

### What T5ynth Does Differently

T5ynth takes this openly released model and implements a non-intended use: instead of generating finished audio, it treats the model's 768-dimensional T5 text embedding space as a navigable sonic terrain. The diffusion model becomes an oscillator — a sound source that a musician shapes, filters, modulates, and sequences like any other synthesizer component.

This inverts the appropriation relationship:

- **Intended use:** Human types prompt → AI produces finished content → Human consumes
- **T5ynth:** Human navigates embedding space → AI produces raw material → Human creates

The generated audio is not the output — it is the starting point. It requires human musicianship, sound design, and compositional decisions to become anything. T5ynth does not make music. It makes material for making music.

---

## Features

### The T5 Oscillator

The core of T5ynth is a new kind of oscillator that doesn't exist in any conventional synthesizer. Where traditional oscillators generate sound from mathematical waveforms (sine, saw, square) or from recorded samples, the T5 Oscillator generates sound from *meaning*.

The key operation is not prompting — it is **vector manipulation in a learned semantic space**.

Two text prompts (A and B) are each encoded by a T5 language model into 768-dimensional embedding vectors. These are not audio signals — they are points in a high-dimensional space where semantic relationships are encoded as geometric relationships. (For an introduction to embeddings, see Jay Alammar's [Illustrated Word2Vec](https://jalammar.github.io/illustrated-word2vec/).) T5ynth operates on these vectors before any audio is generated:

- **Interpolation and Extrapolation** — A continuous alpha parameter navigates between embedding A and B in embedding space, not in audio. This is not a crossfade or mix of two signals — it is movement through a semantic space. The midpoint is not "half A plus half B" in any audible sense; it is a new point in meaning-space that the model interprets independently. Extrapolation beyond either pole pushes into regions of the embedding space that no text prompt would naturally reach.
- **Magnitude Control** — The length of the embedding vector can be scaled independently of its direction, controlling how strongly the semantic content influences the diffusion process. Low magnitudes drift toward the model's prior (generic, neutral sounds); high magnitudes push toward more extreme, sometimes unstable sonic territory.
- **Noise Injection** — This is not audio noise — it is semantic noise: random perturbation in meaning-space, a dose of chaos applied to the embedding before the model ever generates a sound. Even small values introduce subtle variation between otherwise identical generations.
- **Semantic Axes** — 8 axes derived from pole prompt pairs (e.g., "tonal" vs. "noisy") that define musically meaningful directions in the 768d space, validated via spectral analysis (Mel-cosine distance > 0.80 at 1s). Multiple axes are interrelated — the more you stack, the less predictable each becomes. PCA-based axes were tested but collapse at short durations and are not offered.
- **Dimension Explorer** — Direct access to all 768 individual T5 dimensions, sorted by activation magnitude. Individual dimensions can be offset before generation. What each dimension "does" sonically is largely opaque — the T5 embedding space was trained for language tasks, not audio. This is a research tool for probing the space, not a precision instrument.

The manipulated embedding then conditions a diffusion process (DiT transformer, BrownianTree SDE sampler) followed by VAE decoding to produce 44.1kHz stereo audio.

### Drift & Regenerate

**Drift & Regenerate is what makes T5ynth more than a sample player or wavetable synth.**

Without it, you generate a sound and then shape it with conventional synth tools — the source stays fixed. With Drift & Regenerate, the sound source itself continuously evolves.

Three slow drift LFOs (0.001–2.0 Hz) can target generation-level parameters — Alpha, the semantic axes, Noise, and Magnitude. While the synth is playing, multiple drift LFOs continuously and independently shift these parameters, tracing a complex, never-repeating path through the embedding space. When an auto regen mode is active, T5ynth monitors how far the embedding has drifted and fires new inference requests in the background. The current sound keeps playing; when the new generation arrives, it is crossfaded into the playback buffer. Beat-limited modes (max 1♩/4♩/16♩) let you cap the regeneration rate relative to the current BPM.

The result is an asynchronous feedback loop between the modulation system and the T5 oscillator: a continuous stream of new generations, each from a slightly different position in embedding space. The oscillator is no longer a static waveform — it traces a continuous trajectory through the 768-dimensional embedding space.

### Engine Modes

Two playback modes turn the generated audio into something a synthesizer can work with:

- **Sampler Mode** — Plays back the generated audio with loop points (one-shot, loop, ping-pong) and crossfade. Pitch follows MIDI via time-stretching. Useful for longer textures where the raw character of the generation matters.
- **Wavetable Mode** — Extracts pitch-synchronous single-cycle frames from the audio and builds a scannable wavetable. This turns any generated sound into a pitched, playable oscillator that tracks MIDI notes directly.

### Synthesizer

The T5 Oscillator produces unconventional material — the signal chain that follows is a standard synth architecture so that familiar tools can shape it.

- **Envelopes:** 3 identical ADSR envelopes, each assignable to any modulation destination via target dropdown. There is no hard-wired amplitude envelope — to use one as a VCA, assign its target to DCA.
- **Filter:** State-variable filter (TPT topology), LP/HP/BP, 6-24dB, with keyboard tracking and dry/wet mix
- **LFOs:** 2 LFOs (Sine/Tri/Saw/Square, Free/Trigger mode), each with assignable target
- **Effects:** Delay with feedback/damping, convolution reverb (EMT 140 plate impulse responses, thanks to [Greg Hopkins](https://oramics.github.io/sampled/IR/EMT140-Plate/), CC BY), algorithmic reverb, limiter
- **Sequencer:** Step sequencer (2-32 steps) with per-step note/velocity/gate/glide, 10 built-in patterns, save/load for custom patterns
- **Generative Sequencer:** Euclidean rhythm (Bjorklund) + Turing Machine melodic mutation + parameter drift. Self-evolving patterns with scale quantization.
- **Arpeggiator:** Serial after sequencer (Up/Down/UpDown/Random, 1-4 octaves, musical rate divisions including triplets)
- **Presets:** .t5p format stores parameters + generated audio + embeddings — loading a preset does not require regeneration
- **Platforms:** macOS on Apple Silicon (MPS acceleration), Linux with NVIDIA GPU (CUDA), Windows 11 with NVIDIA GPU (CUDA), CPU fallback on all three

## Architecture

### T5 Oscillator — Embedding to Audio Pipeline

```
                        ┌─────────────────────────────────────────────┐
                        │          EMBEDDING SPACE (768d)             │
                        │                                             │
  Prompt A ──→ T5 Encode ──→ Embedding A ─┐                          │
                        │                  ├─→ Interpolation (alpha)  │
  Prompt B ──→ T5 Encode ──→ Embedding B ─┘        │                 │
                        │                           ▼                 │
                        │    Semantic Axes ──→ Axis Offsets ─┐        │
                        │    (8 navigable                    │        │
                        │     dimensions)                    ▼        │
                        │                        Magnitude + Noise    │
                        │                              │              │
                        │    Dimension Explorer ──→ Per-Dim Offsets   │
                        │    (768 editable bars)       │              │
                        │                              ▼              │
                        │                     Final Embedding (768d)  │
                        └──────────────────────────────┬──────────────┘
                                                       │
                        ┌──────────────────────────────┼──────────────┐
                        │  DIFFUSION (Python Backend)  │              │
                        │                              ▼              │
                        │  ┌────────────────────────────────────────┐ │
                        │  │ DiT (Diffusion Transformer)           │ │
                        │  │ BrownianTree SDE sampler (torchsde)   │ │
                        │  │ CFG guidance                          │ │
                        │  └───────────────────┬────────────────────┘ │
                        │                      ▼                      │
                        │  ┌────────────────────────────────────────┐ │
                        │  │ VAE Decode → 44.1kHz stereo float32   │ │
                        │  └───────────────────┬────────────────────┘ │
                        └──────────────────────┼──────────────────────┘
                              Unix pipes       │
                              (binary protocol)│
                        ┌──────────────────────┼──────────────────────┐
                        │  T5 OSCILLATOR       ▼        (JUCE C++)   │
                        │                                             │
                        │  ┌─────────────┐  ┌────────────────────┐   │
                        │  │   SAMPLER   │  │     WAVETABLE      │   │
                        │  │             │  │                    │   │
                        │  │ Loop modes: │  │ YIN pitch detect   │   │
                        │  │ one-shot    │  │ Frame extraction   │   │
                        │  │ loop        │  │ 2048 smp/frame     │   │
                        │  │ ping-pong   │  │ 8 mip levels (FFT) │   │
                        │  │ crossfade   │  │ Catmull-Rom interp │   │
                        │  │             │  │ Real-time scan     │   │
                        │  └──────┬──────┘  └─────────┬──────────┘   │
                        │         └────────┬──────────┘              │
                        │                  ▼                          │
                        └──────────────────┼──────────────────────────┘
                                           │
                        ┌──────────────────┼──────────────────────────┐
                        │  SYNTHESIZER     ▼                          │
                        │                                             │
                        │  Voice Manager (8 voices, note stealing)    │
                        │         │                                   │
                        │         ▼                                   │
                        │  ┌─────────────────────────────────────┐   │
                        │  │ Per Voice:                          │   │
                        │  │   3× Envelopes (assignable targets) │   │
                        │  │   Filter (SVF: LP/HP/BP, 6-24dB)   │   │
                        │  └────────────────┬────────────────────┘   │
                        │                   ▼                         │
                        │  ┌─────────────────────────────────────┐   │
                        │  │ Global:                             │   │
                        │  │   2× LFO (sin/tri/saw/sq)          │   │
                        │  │   3× Drift LFO (slow modulation)   │   │
                        │  │   Drift → Alpha/Axes → Auto-Regen  │   │
                        │  │   Delay (feedback + damping)        │   │
                        │  │   Reverb (EMT 140 convolution/algo) │   │
                        │  │   Limiter                           │   │
                        │  └────────────────┬────────────────────┘   │
                        │                   ▼                         │
                        │  Sequencer (step/gen) → Arpeggiator        │
                        │                   │                         │
                        │                   ▼                         │
                        │              Stereo Out                     │
                        └─────────────────────────────────────────────┘
```

### IPC

The inference runs in a Python subprocess — the BrownianTree SDE sampler (torchsde) is essential for audio quality and not available in C++. Audio transfers to JUCE via a binary pipe protocol (stdin/stdout): JSON request in, binary header + float32 PCM + embedding stats out. No HTTP overhead, subprocess stays alive between generations.

### Preset Format (.t5p)

Presets store everything needed for instant recall: synthesis parameters (JSON), the generated audio (raw float32 PCM), and the 768d embeddings — so loading a preset does not require regeneration. The format auto-detects legacy JSON and XML presets for backwards compatibility.

---

## Building

### Requirements

- **CMake** >= 3.22
- **C++20** compiler (Clang, GCC, MSVC)
- **LibTorch** (pre-built, not pip torch) — [download here](https://pytorch.org/get-started/locally/)
- **libcurl**
- **Python** >= 3.10 with pip

### Steps

```bash
# Clone
git clone https://github.com/joeriben/t5ynth.git
cd t5ynth

# Python backend dependencies
python3 -m venv .venv
source .venv/bin/activate
pip install -r backend/requirements.txt

# Configure (adjust LibTorch path)
cmake -B build -DCMAKE_PREFIX_PATH=/path/to/libtorch

# Build
cmake --build build

# Run standalone
./build/T5ynth_artefacts/Debug/Standalone/T5ynth.app/Contents/MacOS/T5ynth  # macOS
./build/T5ynth_artefacts/Debug/Standalone/T5ynth                            # Linux
```

### Model Download

T5ynth requires at least one diffusion model. Models are not bundled — they must be downloaded separately. T5ynth never asks for a HuggingFace token; installation works differently depending on how the model is licensed.

Use the **Settings** panel on first launch:

1. **Stable Audio Open Small** — recommended main model. Commercial use is free under $1M annual revenue ([Stability AI Community License](https://stability.ai/community-license-agreement)). The model is gated on HuggingFace, so install is a one-time manual step: click *Open Model Page* in Settings, log in, accept the license, download `model.safetensors`, `model_config.json`, and `LICENSE` to your system Downloads folder, then click *Auto-Scan*. T5ynth finds the files and copies them to the right place automatically.
2. **AudioLDM2** (experimental) — the only model T5ynth can download itself in a single click, because it is ungated. Licensed under [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/) — **non-commercial only, no revenue threshold, no exceptions**.
3. **Stable Audio Open 1.0** — larger, slower, and less suitable for T5ynth's signal flow than SA Small. Because it consists of many files in nested subfolders, the realistic install path is `huggingface-cli` in a terminal. See the in-app instructions.

Manual install locations if you prefer to place files yourself:

| Platform | Models Directory |
| --- | --- |
| macOS | `~/Library/T5ynth/models/<model-id>/` |
| Linux | `~/.local/share/T5ynth/models/<model-id>/` |
| Windows 11 | `%APPDATA%\T5ynth\models\<model-id>\` |

After a manual install, click *Auto-Scan* in Settings to register the model.

---

## License

T5ynth is licensed under the **GNU General Public License v3.0** — see [LICENSE](LICENSE).

This means you are free to use, modify, and redistribute T5ynth, provided that derivative works are also released under GPLv3 with source code available.

### Third-Party Components

- **Stable Audio Open 1.0** — [Stability AI Community License](https://stability.ai/community-license-agreement). The model is not included in this repository. Users download it separately and must accept its license. Powered by Stability AI.
- **AudioLDM2** — [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/). Non-commercial use only. Not included; users download separately.
- **JUCE Framework** — AGPLv3 (vendored in `JUCE/`)
- See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for full details.

### Citation

If you use T5ynth in academic work:

```
Prof. Dr. Benjamin Jörissen / AI4ArtsEd — UCDCAE AI Lab
https://github.com/joeriben/t5ynth
```

---

