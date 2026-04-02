# T5ynth

(note: this description has been written by the co-coding AI, Claude Opus 4.6)


**A text-to-sound synthesizer that navigates the T5 embedding space of a diffusion audio model.**

T5ynth is a JUCE-based synthesizer plugin (Standalone / VST3 / AU) that repurposes Stability AI's [Stable Audio Open](https://huggingface.co/stabilityai/stable-audio-open-1.0) model as an oscillator — not to generate finished music, but to produce raw sonic material for human-nonhuman artistic collaboration.

> *T5ynth is developed by [Prof. Dr. Benjamin Jörissen](https://github.com/joeriben) as part of the [AI4ArtsEd](https://github.com/joeriben/ucdcae-ai-lab) project at the UCDCAE AI Lab.*

---

## Context: Why This Exists

T5ynth emerged from [AI4ArtsEd](https://github.com/joeriben/ucdcae-ai-lab), a research project investigating alternative, non-standard uses of AI for educational purposes. AI4ArtsEd deliberately subverts consumerist user-subject positions and approaches AI from a critical, empowerment-oriented perspective.

### The Problem with Generative Audio AI

Text-to-audio models like Stable Audio are designed for a specific purpose: generating finished audio content from text prompts. Their intended use case is the substitution of creative labor — type a description, get a result. This positions the user as a consumer of AI output rather than an active creative agent.

### Open Source as Strategy

Stable Audio Open deserves credit: Unlike its commercial siblings which rob artists of their works, it was trained on ~486,000 Creative Commons-licensed recordings from Freesound and the Free Music Archive — not on copyrighted music. Stability AI conducted copyright verification and removed flagged content before training. This is genuinely better practice than much of the industry.

At the same time, "open source" in the AI industry operates as strategic marketing. Releasing a smaller, less capable model (Open) builds community and ecosystem around commercial products (Stable Audio Pro). The openness is real and useful, but it serves a business strategy. We should be honest about this dialectic rather than either dismissing or celebrating it uncritically.

### What T5ynth Does Differently

T5ynth takes this openly released model and implements a non-intended use: instead of generating finished audio, it treats the model's 768-dimensional T5 text embedding space as a navigable sonic terrain. The diffusion model becomes an oscillator — a sound source that a musician shapes, filters, modulates, and sequences like any other synthesizer component.

This inverts the appropriation relationship:

- **Intended use:** Human types prompt → AI produces finished content → Human consumes
- **T5ynth:** Human navigates embedding space → AI produces raw material → Human creates

The generated audio is not the output — it is the starting point. It requires human musicianship, sound design, and compositional decisions to become anything. T5ynth does not make music. It makes material for making music.

Whether this actually succeeds in reframing the human-AI relationship is an open question, not a solved one. That's the research.

---

## Features

### The T5 Oscillator

The core of T5ynth is a new kind of oscillator that doesn't exist in any conventional synthesizer. Where traditional oscillators generate sound from mathematical waveforms (sine, saw, square) or from recorded samples, the T5 Oscillator generates sound from *meaning*.

A text prompt is encoded by a T5 language model into a 768-dimensional embedding vector. This vector — a point in semantic space — conditions a diffusion process that synthesizes audio. The result is a waveform whose timbral character is shaped not by parameters like "detune" or "pulse width" but by semantic proximity: the distance between "glass breaking" and "ice cracking", the interpolation between "warm analog pad" and "frozen digital texture", the unexplored spaces between concepts that have no name.

Two playback modes make this musically useful:

- **Sampler Mode** — The generated audio is played back directly with loop points (one-shot, loop, ping-pong), crossfade control, and silence-trimmed auto-bracketing. Useful for longer textures and evolving material.
- **Wavetable Mode** — The audio is analyzed via pitch detection (YIN algorithm), sliced into pitch-synchronous frames of 2048 samples, band-limited across 8 mip levels via FFT, and scanned in real-time. This turns any generated sound into a playable, pitch-tracked wavetable oscillator with Catmull-Rom interpolation between frames.

The embedding space itself is navigable: A/B prompt interpolation blends two semantic poles, 8 semantic axes (tonal/noisy, bright/dark, harmonic/inharmonic, etc.) provide musically meaningful navigation dimensions, and the Dimension Explorer gives direct access to all 768 individual T5 dimensions — sorted by perceptual magnitude — for precise sculpting of the embedding before generation.

### Synthesizer

- **Full Synthesizer Architecture:** ADSR envelopes (amplitude + 2 modulation), 2 LFOs, 3 drift LFOs, state-variable filter (LP/HP/BP, 6-24dB), modulation matrix
- **Effects:** Delay with damping, convolution reverb (EMT 140 plate), limiter
- **Sequencer & Arpeggiator:** 16-step sequencer with per-step note/velocity/gate/glide, arpeggiator (up/down/updown/random)
- **Presets with Embedded Audio:** .t5p format stores parameters + generated audio + embeddings — instant recall without regeneration
- **Cross-Platform:** macOS (MPS acceleration), Linux (CUDA), CPU fallback

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
                        │  │ 20 denoising steps, CFG guidance      │ │
                        │  │ BrownianTree SDE sampler (torchsde)   │ │
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
                        │  │   Amp Envelope (ADSR + loop)       │   │
                        │  │   2× Mod Envelopes → mod matrix    │   │
                        │  │   Filter (SVF: LP/HP/BP, 6-24dB)   │   │
                        │  └────────────────┬────────────────────┘   │
                        │                   ▼                         │
                        │  ┌─────────────────────────────────────┐   │
                        │  │ Global:                             │   │
                        │  │   2× LFO (sin/tri/saw/sq/S&H)     │   │
                        │  │   3× Drift LFO (slow modulation)   │   │
                        │  │   Delay (feedback + damping)        │   │
                        │  │   Reverb (EMT 140 convolution)      │   │
                        │  │   Limiter                           │   │
                        │  └────────────────┬────────────────────┘   │
                        │                   ▼                         │
                        │  Sequencer (16-step) / Arpeggiator         │
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

T5ynth requires the Stable Audio Open 1.0 model (~11GB). On first launch, use the Settings panel to either:

1. **Auto-download:** Enter your [HuggingFace token](https://huggingface.co/settings/tokens) (requires accepting the [model license](https://huggingface.co/stabilityai/stable-audio-open-1.0)) and click Download
2. **Manual:** Place the model files in `~/Library/T5ynth/models/stable-audio-open-1.0/` (macOS) or `~/.local/share/T5ynth/models/stable-audio-open-1.0/` (Linux)

---

## License

T5ynth is licensed under the **GNU General Public License v3.0** — see [LICENSE](LICENSE).

This means you are free to use, modify, and redistribute T5ynth, provided that derivative works are also released under GPLv3 with source code available.

### Third-Party Components

- **Stable Audio Open 1.0** — [Stability AI Community License](https://stability.ai/community-license-agreement). The model is not included in this repository. Users download it separately and must accept its license. Powered by Stability AI.
- **JUCE Framework** — AGPLv3 (vendored in `JUCE/`)
- See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for full details.

### Citation

If you use T5ynth in academic work:

```
Prof. Dr. Benjamin Jörissen / AI4ArtsEd — UCDCAE AI Lab
https://github.com/joeriben/t5ynth
```

---

*T5ynth is a research artifact. It is an argument in code form about what generative AI could be when we refuse the subject positions it was designed to produce.*
