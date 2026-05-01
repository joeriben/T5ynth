# T5ynth

**Resonance with meaning.**

With two short impulses, this innovative music synthesizer opens up a hidden
space between two poles of meaning before audio comes into being. Set those poles as
textures, transients, patterns, sonic and musical fragments, field recordings,
everyday noises, orchestral gestures, alien voices, human emotional
expressions, or impossible hybrids.

The fields labeled **Impulse A** and **Impulse B** are not two samples, two song
requests, or two oscillators. They are two markers for the space you want to
explore. You can pull between the markers, push the space harder or softer,
disturb it, bend it along sound qualities, open individual dimensions, change
where one idea enters the other, and let the space drift over time before
audio exists.

Most AI audio tools hide that space and only return an audio result. T5ynth
turns the pre-audio space into the instrument. Generation is not a separate AI
step after the synth; the synth reaches into the generation process itself.
The rendered fragment is one stage in a signal path that continues through
sampler or wavetable playback, filters, envelopes, LFOs, sequencing, delay,
reverb, and limiting.

Links:

- Full user guide: [`resources/T5ynth_Guide.html`](resources/T5ynth_Guide.html)
- Preset collection: [`joeriben/T5ynth-Presets`](https://github.com/joeriben/T5ynth-Presets)
- Current beta release: [`v1.7.0-beta.1`](https://github.com/joeriben/t5ynth/releases/tag/v1.7.0-beta.1)

Current tagged GitHub Releases publish the macOS and Windows installers.
Linux, VST3, and AU build outputs exist in the project/build pipeline, but are
not the current public release assets.

---

## What T5ynth Does

T5ynth is easiest to understand from its signal flow.

In a conventional synth, an oscillator produces audio immediately: sine, saw,
square, noise, sample, wavetable. T5ynth inserts a stage before that. The
T5ynth Oscillator is a meta-oscillator: it does not begin with one waveform or
one sample, but opens a bounded hidden sound space inside the selected audio
model. Within that space, many possible sounds can exist before any one of them
is rendered. The main musical act is to explore that space and decide which
state becomes a short audio fragment.

The fields labeled **Impulse A** and **Impulse B** belong to that pre-audio stage.
They do not mean a ChatGPT conversation, a song request, or a rendered audio
file. A and B are text impulses that mark one inner space. They do not
produce two samples, and they are not two oscillators.

The interface names for those pre-audio operations are **Alpha** for the pull
between A and B, **Magnitude** for how strongly the space is pushed,
**Noise** for perturbation, **sound-character axes** for broad directions,
**Dimension Explorer** for individual dimensions, **Injection Modes** for where
B enters A during generation, and **Drift** for movement over time. They do
not mix audio. They move or reshape the hidden sound space before audio exists.

**Generate** renders the current state of that space into audio, but it is
not where the instrument starts. T5ynth has already shaped the generation
before audio exists. The rendered fragment is then carried further through
sampler playback or wavetable extraction, followed by filter, envelopes, LFOs,
sequencers, delay, reverb, limiter, and presets.

The instrument flow is therefore:

1. Two short impulses, one in A and one in B, mark the hidden sound space, for
   example "steady clean saw wave, C3" and "120 bpm syncopated transient
   pattern".
2. Alpha, Semantic Axes, Noise, Dimension Explorer, Injection Modes, and Drift
   move that space before audio exists.
3. Shape the sonic flow as a **Sampler** or **Wavetable** engine, with filters,
   envelopes, LFOs, sequencers, delay, reverb, limiter, presets, and
   **Drift Modulators & Auto-Regenerate**.

T5ynth can use **Stable Audio Open 1.0**, **Stable Audio Open Small**, or
**AudioLDM2**. Each engine has its own learned response profile, so the same
A/B pair can open a different space depending on the selected model. The
Stable Audio engines are strongest with English sound-oriented phrases:
sound effects, field recordings, drum or instrument loops, ambient sounds,
foley, and production elements. AudioLDM2 is broader in its training goal and
can cover sound effects, speech-like material, and music, but it is
non-commercial only and less tied to T5ynth's newer injection-mode research.

The meaning poles do not have to be narrow "sound ideas". They can evoke sound
sources, materials in motion, acoustic scenes, bodies, gestures, moods,
fictional agents, or impossible combinations. A visual phrase such as "a rose
in a vase" is not a sound by itself; "water in a glass vase, quiet room, petals
brushed by fingers" gives the model acoustic handles. But the abstract phrase
can still be used as a strange marker in the model's space. T5ynth is where you
find out what that marker can become.

About the name: **T5** is the text encoder used by the Stable Audio engines.
It turns a phrase into control data that the audio model can use. You do not
need to know T5 to use T5ynth. The practical point is simpler: meaning opens
a model space before it becomes audio.

From there, T5ynth behaves much more like an instrument than like an audio
generator website:

1. The impulses mark a hidden sound space inside the audio model.
2. If you want to, you move through that space with Alpha, Magnitude,
   model-space noise, sound-character axes, the Dimension Explorer, Injection
   Modes, and Drift.
3. The diffusion backend renders the current pre-audio state into short stereo
   audio.
4. The synth engine plays that fragment as a sampler source or converts it
   into a scannable wavetable.
5. The rest is synthesis: filter, envelopes, LFOs, drift, sequencers, delay,
   reverb, limiter, presets.

The generated audio is not the final output. It is a playable fragment inside
a larger instrument.

## Why This Exists

Generative audio systems are often designed as black boxes: enter a request,
receive a result, consume the output. That positions the musician outside the
model, after the important decisions have already happened.

T5ynth deliberately inverts that relationship:

- **Standard AI audio workflow:** human writes a request -> model hides the
  internal search -> audio result appears. AI company makes money; musicians
  lose commissions, jobs, and control.
- **T5ynth workflow:** human marks and moves through the model's hidden sound
  space -> the model renders a playable fragment -> human plays, shapes,
  rejects, edits, saves, and composes. T5ynth will by no means address these
  challenges; it only tries to show that it ain't necessarily so.

This is why T5ynth matters even, and maybe especially, for musicians who
are skeptical of generative AI music. It does not automate musical judgment. It
makes the hidden space before the result available for musical judgment.

## Research Context

T5ynth is a personal side project by Prof. Dr. Benjamin Jörissen, UNESCO Chair
in Digital Culture and Arts in Education (UCDCAE),
Friedrich-Alexander-Universität Erlangen-Nürnberg, and part of the
[UCDCAE AI Lab Software Collection](https://github.com/joeriben/ucdcae-ai-lab).

It is inspired by two research projects:

- [AI for Arts Education (AI4ArtsEd)](https://kubi-meta.de/ai4artsed),
  conducted together with the University of Cologne and the German Research
  Institute for Artificial Intelligence (DFKI) Kaiserslautern.
- [ComeArts Across](https://comearts.uni-due.de/comenets/artsacross/), a
  research project on digital cultural teacher education.

Both are funded by the Federal Ministry for Education, Family, Senior Citizens,
Women and Youth (BMBFSFJ).

T5ynth is dedicated to my dear colleague at the DFKI, musician and AI
researcher Dr. Stephan Baumann, without whom AI4ArtsEd would not have come
into being.

## What Is New in v1.7.0-beta.1

- **BPM sync for LFOs, Drift LFOs, and Delay Time.** Each of the three LFOs,
  three Drift LFOs, and Delay Time can switch from free rate/time to musical
  divisions.
- **Host/standalone clock resolution.** Sync follows the host transport when
  available, falls back to the in-app sequencer while it runs, and otherwise
  uses the last host BPM or the sequencer BPM field.
- **Safer preset restore.** Old presets now restore missing clock and injection
  defaults explicitly instead of inheriting whatever state was last active.
- **LFO Trigger mode fix.** Per-voice LFO trigger mode now affects voice
  rendering instead of silently behaving like free-running global LFOs.
- **Delay mix fix.** Delay Mix is now a true dry/wet crossfade.

See [`CHANGELOG.md`](CHANGELOG.md) for the full release history.

## Core Concepts

### The Pre-Audio Sound Space

The center of T5ynth is the sound space before audio exists. Traditional
oscillators generate sine, saw, square, noise, or sample playback. T5ynth
starts inside the model, at the stage where meaning still shapes what sound can
become.

Behind the scenes, that space is numerical. You can ignore that while playing,
just as you can use FM without solving the equations.

- **Impulse A/B and Alpha** mark and move through the model's inner sound space.
  This is not an audio crossfade; the midpoint is a new pre-audio state.
- **Magnitude** changes how strongly that state steers the diffusion model.
- **Model-space noise** perturbs the state before audio exists.
- **Sound-character axes** add musically legible directions such as noisy/tonal or
  sustained/rhythmic. Sometimes they work better, sometimes not, depending on
  the impulses.
- **Dimension Explorer** opens all 768 internal control dimensions directly.
  You will mostly learn that AI-generated meaning making is a black box humans
  do not really understand, but sometimes the results are unexpectedly
  interesting.
- **Injection Modes** change where B enters A inside the
  diffusion process: Linear, Step-in, Layer, Combo 1, Combo 2, Combo 3.
- **Drift** keeps the pre-audio state moving over time and can trigger new
  generations in the background.

### Drift Modulators & Auto-Regenerate

Drift Modulators & Auto-Regenerate turn the pre-audio space into something
that can evolve. Three slow Drift LFOs can target generation-level parameters
such as Alpha, Semantic Axes, Noise, and Magnitude. When Auto-Regenerate is active,
T5ynth generates new audio in the background as the pre-audio state moves,
then crossfades the new fragment into playback. Depending on the host machine,
regeneration can range from roughly 0.1 seconds on an RTX 6000-class GPU to
several seconds on a Mac M-series processor without a dedicated AI-capable
graphics card.

With v1.7, those drift cycles can be clock-synced to musical divisions, so
long pre-audio motion can sit inside a DAW, sequencer, or standalone tempo
workflow.

### Sampler and Wavetable Modes

T5ynth can use generated audio in two ways:

- **Sampler Mode** plays the generated fragment directly with loop modes,
  crossfade, normalization, and pitch following through time-stretching.
- **Wavetable Mode** extracts pitch-synchronous single-cycle frames and turns
  the generation into a playable, scannable wavetable oscillator.

## Feature Overview

- **Generation:** Impulse A/B, Alpha, Magnitude, Noise, Duration, Steps, CFG,
  Seed, Start Position, HF Boost.
- **Source controls:** Sound-character axes, 768-dimension explorer, Linear/
  Step-in/Layer/Combo injection modes.
- **Playback engine:** Sampler and Wavetable modes, loop optimization,
  wavetable scan, noise source.
- **Synthesis:** 16-voice voice manager, assignable envelopes, multimode
  filter algorithms, keyboard tracking, drive, modulation ghost indicators.
- **Modulation:** 3 ADSR envelopes, 3 LFOs, 3 Drift LFOs, free or clock-synced
  rates, free/trigger LFO mode.
- **Sequencing:** Step sequencer, arpeggiator, polyphonic generative sequencer
  with up to five strands and a shared pitch field.
- **Effects:** Tempo-syncable delay, convolution and algorithmic reverb,
  limiter.
- **Presets:** `.t5p` files store parameters, A/B texts, generated audio, and
  internal source data, so loading a preset does not require regeneration.

## Architecture Summary

T5ynth has two main parts:

- A JUCE/C++ synthesizer, UI, preset, modulation, sequencing, and DSP engine.
- A Python inference subprocess that runs the diffusion model and communicates
  with JUCE through a binary stdin/stdout pipe protocol.

The Python backend is used because the BrownianTree SDE sampler and model
runtime are not available as equivalent C++ components. The subprocess stays
alive between generations, so repeated generations avoid backend startup
overhead.

For code-level details, see [`ARCHITECTURE.md`](ARCHITECTURE.md),
[`docs/IPC_PROTOCOL.md`](docs/IPC_PROTOCOL.md), and the signal-flow section in
the user guide.

---

## Building

If you just want to install T5ynth, use the tagged GitHub Release assets:
current public releases provide ready-made macOS and Windows installers.

The old minimal snippet in this README was not enough to produce a working
Linux build. The authoritative build guides now live here:

- Linux / Fedora 42 source build on a developer/build host: [docs/LINUX_INSTALLATION.md](docs/LINUX_INSTALLATION.md)
- Linux package-layer docs, currently Fedora RPM and Ubuntu/Debian `.deb` from a prebuilt isolated backend bundle: [docs/LINUX_PACKAGING.md](docs/LINUX_PACKAGING.md)
- Cross-platform developer build guide: [docs/DEV_BUILD.md](docs/DEV_BUILD.md)

Linux now has one common build contract and multiple distribution layers:

- the Ubuntu CI `linux` job produces Linux base archives (`T5ynth` plus sibling `backend/`)
- Fedora RPM wraps that same app/backend layout for installation on Fedora
- Ubuntu/Debian `.deb` wraps that same app/backend layout for installation on Ubuntu-family systems

### Quick Source Build

This is the developer/build-host path. It creates a repo-local `.venv`,
installs Python dependencies there, and freezes the backend bundle locally.
It is not the target-machine installer path.

```bash
# Clone
git clone https://github.com/joeriben/t5ynth.git
cd t5ynth

# Python backend
python3.11 -m venv .venv --clear
source .venv/bin/activate
python -m pip install --upgrade pip setuptools wheel
python -m pip install pyinstaller
python -m pip install torch==2.7.1 torchaudio==2.7.1 torchvision==0.22.1 --index-url https://download.pytorch.org/whl/cu128  # Linux/Windows NVIDIA
# python -m pip install torch==2.7.1 torchaudio==2.7.1 torchvision==0.22.1  # macOS or CPU-only fallback
python -m pip install -r backend/requirements.txt

# Bundle backend
( cd backend && pyinstaller pipe_inference.spec --noconfirm )

# Configure + build
cmake -S . -B build_clean -DCMAKE_BUILD_TYPE=Release
cmake --build build_clean --config Release

# Linux standalone layout
mkdir -p dist/T5ynth/backend
cp build_clean/T5ynth_artefacts/Release/Standalone/T5ynth dist/T5ynth/
cp -R backend/dist/pipe_inference/* dist/T5ynth/backend/
./dist/T5ynth/T5ynth
```

For Linux package-layer installation, do not rebuild Python/Torch on the target
machine. Build the isolated backend bundle once on a build host, stage it into
a named release bundle, then wrap that selected bundle into the RPM or `.deb`
described in [docs/LINUX_PACKAGING.md](docs/LINUX_PACKAGING.md).

### Model Download

T5ynth requires at least one diffusion model. Models are not bundled — they must be downloaded separately.

Use the **Settings** panel on first launch:

1. **Stable Audio Open Small** — licensed under the [Stability AI Community License](https://stability.ai/community-license-agreement). Gated on HuggingFace: install is a one-time manual step. The user downloads `model.safetensors` and `model_config.json` from HuggingFace, then T5ynth picks them up via *Auto-Scan* or *Browse...* in Settings.
2. **AudioLDM2** — an academic latent-diffusion text-to-audio model published by CVSSP / University of Surrey and collaborators ([Liu et al., 2023](https://arxiv.org/abs/2308.05734)), released as an open research artefact for studying generalised audio, music, and speech generation from text. Ungated on HuggingFace and the only engine T5ynth can install directly. Licensed under [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/) — **non-commercial only, no revenue threshold, no exceptions**. Included as an alternative sound source for non-commercial musical exploration.
3. **Stable Audio Open 1.0** — licensed under the [Stability AI Community License](https://stability.ai/community-license-agreement). Gated on HuggingFace. Download `model.safetensors` and `model_config.json` to your Downloads folder, then use *Auto-Scan* in Settings; T5ynth copies the files into its working model folder.

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

- **Stable Audio Open 1.0 / Stable Audio Open Small** — [Stability AI Community License](https://stability.ai/community-license-agreement). The models are not included in this repository. Users download them separately and must accept their license. Powered by Stability AI.
- **AudioLDM2** — [CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/). Non-commercial use only. Not included; users download separately.
- **JUCE Framework** — AGPLv3 (vendored in `JUCE/`). Provides the application framework and DSP building blocks used by the SVF, delay, convolution reverb, algorithmic reverb, limiter, and oversampling paths.
- **EMT 140 plate reverb impulse responses** — Greg Hopkins, Creative Commons Attribution (CC BY). Used for the Dark, Medium, and Bright convolution reverb modes.
- **Signalsmith Stretch** — Geraint Luff / Signalsmith Audio Ltd., MIT. Used for pitch-preserving sample transposition.
- **nlohmann/json** — Niels Lohmann, MIT. Used for configuration and preset parsing.
- **SentencePiece** — Apache 2.0. Used for the native C++ T5 tokenizer.
- **Python inference stack** — `diffusers`, `transformers`, PyTorch, `torchsde`, `soundfile`, and SciPy provide the model pipeline, tensor runtime, sampler support, audio I/O, and signal-processing utilities used by the backend.
- **Huovilainen ladder-filter reference** — Antti Huovilainen's DAFx-04 paper is credited for the non-linear digital ladder topology implemented in T5ynth.
- **Cutoff Warp filter inspiration** — Surge XT is credited for the musical idea of a style-switchable cutoff-warp character control. T5ynth's implementation is written from scratch; no Surge XT source code is copied.

T5ynth would be much poorer without these projects, papers, impulse responses,
and DSP references. See [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt)
for full license details, URLs, and attribution notes.

### Citation

If you use T5ynth in academic work:

```text
Prof. Dr. Benjamin Jörissen / UNESCO Chair in Digital Culture and Arts in Education — UCDCAE AI Lab
https://github.com/joeriben/t5ynth
```

### Documentation Note

Parts of the early project text and user manual were drafted with
AI-assisted co-coding tools and edited by the human author.
