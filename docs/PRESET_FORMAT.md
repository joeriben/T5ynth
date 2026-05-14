# T5ynth Preset Format (`.t5p`) — Specification

This document is a reverse-engineered specification of the T5ynth `.t5p`
preset container format, sufficient for a developer to implement a
third-party reader, converter or browser without reading the T5ynth
source.

All byte offsets, field widths and encoder choices cited here are drawn
directly from the implementation in `src/presets/PresetFormat.cpp` and
`src/PluginProcessor.cpp`. Citations point at specific lines in the
codebase at the time of writing. Where a field's purpose could not be
determined from reading the code, this is called out explicitly.

---

## 1. Overview

A `.t5p` file is a binary container that bundles three things into a
single self-contained preset:

1. **Parameter state** — everything needed to restore the synth DSP
   (envelopes, LFOs, drift, filter, FX, sequencer, arpeggiator,
   generative sequencer, wavetable/noise, engine mode, loop points).
2. **Generated audio** — the raw 44.1 kHz stereo float32 buffer that
   feeds the sampler / wavetable oscillator. Saving the audio means
   loading the preset does **not** require re-running text-to-audio
   inference, which is expensive and non-deterministic across devices
   (CPU vs. MPS vs. CUDA).
3. **T5 text embeddings** — two 768-float vectors (prompt A and prompt
   B, averaged over sequence length). These populate the Dimension
   Explorer UI on load so the user can continue editing in embedding
   space without re-tokenising.

In addition, the container stores three pieces of **GUI-only state**
that are not part of the JUCE `AudioProcessorValueTreeState` (APVTS)
and would otherwise be lost: the prompt texts, the semantic axes slot
state (3 slots), and the "last seed / last device / last model"
metadata.

### 1.1 Why a custom format

JUCE's built-in state storage (`getStateInformation` /
`setStateInformation` at `src/PluginProcessor.cpp:1516` and `:1523`)
serialises the APVTS as an XML blob. That is used for DAW session
persistence only. It is unsuitable as a user-facing preset because:

- Prompts, axes, seed, device and model are not APVTS parameters, so
  they would not be preserved.
- It cannot embed the generated audio buffer, so loading a preset in a
  DAW session would require regeneration and stall the audio thread.
- T5 embeddings (~6 kB of floats) do not belong in APVTS either.

The `.t5p` container wraps the JSON export produced by
`T5ynthProcessor::exportJsonPreset()`
(`src/PluginProcessor.cpp:1749`), patches in the GUI-only fields, and
appends the raw audio as interleaved float32 PCM.

### 1.2 Non-goals

- The format is **not** designed for interchange with other plugins or
  for long-term archival. It is tightly coupled to T5ynth's parameter
  set and model list.
- It does **not** carry a schema, XSD, or self-describing chunk
  directory. Readers must know the layout ahead of time.
- It does **not** store the per-token T5 embedding sequence, only the
  sequence-averaged vectors (see section 5).

---

## 2. Container Structure

The binary layout is fixed and sequential — there is no tagged-chunk
system, no TOC, and no checksums. See
`src/presets/PresetFormat.cpp:78-97` for the writer and
`:119-198` for the reader.

```
offset  width  field
------  -----  -----------------------------------------------------
  0     4      Magic bytes: ASCII "T5YN" (0x54 0x35 0x59 0x4E)
  4     4      Version, uint32 little-endian (currently 3)
  8     4      JSON payload length in bytes, uint32 little-endian
 12     N      JSON payload (UTF-8, not NUL-terminated)
 12+N   M      Primary interleaved float32 PCM (may be empty)
 12+N+M K      Optional inference-cache PCM tail (only when described by JSON)
```

Total file size = `12 + N + M + K` bytes. `K` is zero unless the JSON
contains an `inferenceCache` object.

### 2.1 Magic and version

The magic bytes are defined at `src/presets/PresetFormat.h:63`:

```cpp
static constexpr char kMagic[4] = { 'T', '5', 'Y', 'N' };
static constexpr uint32_t kVersion = 3;
```

The version is currently `3`, written via `out.writeInt(...)` at
`src/presets/PresetFormat.cpp:80`. In JUCE, `FileOutputStream::writeInt`
writes a **little-endian int32**. Despite the field being typed
`uint32_t` in the header, the writer casts it to `int` before writing.
Readers should treat the field as little-endian 32-bit. The reader
enforces strict equality with `kVersion` — see section 8.

### 2.2 JSON length

Written at `src/presets/PresetFormat.cpp:83` and read at `:126`:

```cpp
uint32_t jsonLen = *reinterpret_cast<const uint32_t*>(bytes + 8);
```

Little-endian, unsigned. The reader bails out if `12 + jsonLen > size`
(line 128), so a truncated file will fail cleanly.

### 2.3 Endianness

All multi-byte integers are **little-endian**. The writer uses JUCE's
`FileOutputStream::writeInt` (LE) and the reader dereferences a
`uint32_t*` directly into the file buffer. On a big-endian host the
reader would be wrong, but T5ynth only ships for little-endian targets
(macOS x86_64/arm64, Windows x64, Linux x64), so this is not a
practical concern.

### 2.4 Chunking

There is no chunk framing. Section boundaries are implicit: header
(12 bytes), then JSON (length-prefixed), then primary PCM, then optional
cache PCM. A reader must parse the JSON to discover how many primary
audio samples to expect; if `inferenceCache` is present, its entry list
describes the optional cache tail.

---

## 3. Parameter Payload (JSON)

The payload at offset 12 is the UTF-8 encoding of a single JSON object
produced by `juce::JSON::toString(parsed, /*pretty=*/true)`
(`src/presets/PresetFormat.cpp:68`). It is pretty-printed with
indentation; third-party readers must not assume a minified form.

The object is built in two stages. First, `exportJsonPreset()` emits
the APVTS-derived parameter tree. Second, `saveToFile` parses that
string back into a `DynamicObject` and patches in GUI-only fields
before re-serialising.

### 3.1 Top-level keys

Produced by `exportJsonPreset` at `src/PluginProcessor.cpp:1749-1962`,
then patched at `src/presets/PresetFormat.cpp:18-66`:

| Key             | Source        | Description                                    |
| --------------- | ------------- | ---------------------------------------------- |
| `version`       | export        | Integer, currently `1`. This is the **JSON schema** version, not the container version. |
| `name`          | save/export   | Preset display name when saved through `.t5p`; legacy raw exports may contain `"T5ynth Export"` and should fall back to the filename for UI display. |
| `timestamp`     | export        | ISO-8601 UTC timestamp of the save. |
| `synth`         | export+patch  | Core synth params + prompts + seed + device + model + randomSeed (see 3.2). |
| `engine`        | export        | Engine mode, loop mode, crossfade, normalise, loop optimise, loop/start fractions. |
| `modulation`    | export        | `envs` (3 envelopes) + `lfos` (2 LFOs). |
| `driftLfos`     | export        | Array of 3 drift LFO objects. |
| `driftEnabled`  | export        | Bool. |
| `driftCrossfade`| export        | Float. |
| `regenMode`     | export        | `"manual"` / `"auto"` / `"max_1beat"` / `"max_4beats"` / `"max_16beats"`. |
| `wavetable`     | export        | `scan`, `octaveShift`, `noiseLevel`, `noiseType`, `frames`, `smooth`. |
| `effects`       | export        | Delay, reverb, limiter. |
| `filter`        | export        | `enabled`, `type`, `slope`, `cutoff` (normalised 0..1), `resonance`, `mix`, `kbdTrack`. |
| `sequencer`     | export        | Step sequencer state including scale, steps array, division, glide, gate. |
| `arpeggiator`   | export        | `enabled`, `pattern`, `rate`, `octaveRange`. |
| `generativeSeq` | export        | Euclidean generator params + fix-flags. |
| `semanticAxes`  | patch (t5p)   | Array of exactly 3 objects, GUI-only (see 6). |
| `audio_meta`    | patch (t5p)   | `sampleRate`, `channels`, `numSamples` for the PCM tail (see 4). |
| `embeddingA`    | patch (t5p)   | Array of floats, typically 768 entries (see 5). Omitted if not yet generated. |
| `embeddingB`    | patch (t5p)   | As above. |
| `inferenceCache`| patch (t5p, optional) | Metadata for an optional raw inference-cache tail appended after the primary PCM payload (see 4.4). |

### 3.2 The `synth` object

Emitted at `src/PluginProcessor.cpp:1760-1774` with the following
fields:

| Field           | Type    | Source                                          |
| --------------- | ------- | ----------------------------------------------- |
| `promptA`       | string  | Initially `""`, patched to `getLastPromptA()`   |
| `promptB`       | string  | Initially `""`, patched to `getLastPromptB()`   |
| `alpha`         | float   | APVTS `gen_alpha`                               |
| `magnitude`     | float   | APVTS `gen_magnitude`                           |
| `noise`         | float   | APVTS `gen_noise`                               |
| `duration`      | float   | APVTS `gen_duration`                            |
| `startPosition` | float   | APVTS `gen_start`                               |
| `steps`         | int     | APVTS `inf_steps`                               |
| `cfg`           | float   | APVTS `gen_cfg`                                 |
| `seed`          | int     | Initially APVTS `gen_seed`, patched to `getLastSeed()` |
| `device`        | string  | Initially `lastDevice` member, patched to `getLastDevice()` |
| `model`         | string  | Initially `lastModel` member, patched to `getLastModel()` |
| `hfBoost`       | bool    | APVTS `gen_hf_boost > 0.5`                      |
| `randomSeed`    | bool    | Patched at `PresetFormat.cpp:28`, `gen_seed == -1` |

The `promptA`/`promptB` fields are intentionally blank in the raw
`exportJsonPreset()` output because prompts are GUI-only state and not
APVTS parameters. The `.t5p` save path overwrites them at
`PresetFormat.cpp:21-22`. When exporting a plain `.json` file via
`PresetPanel::exportPreset` (`src/gui/PresetPanel.cpp:78`), no such
patch happens, so `.json` exports lose the prompt text. This is by
design for the JSON export path (see section 7.2).

### 3.3 What is not saved

The following APVTS parameters are defined but not written to the
preset:

- `master_vol` — master output volume. Treated as a runtime / session
  level parameter. It is stored in the DAW session via
  `getStateInformation` at `src/PluginProcessor.cpp:1516` but has no
  entry in `exportJsonPreset`.
- `seq_running` and `gen_seq_running` on the load path — to avoid
  acoustic surprises, the importer deliberately does **not** restart
  sequencers (see `src/PluginProcessor.cpp:2208-2209` for step seq and
  `:2319` for the generative sequencer, which is re-enabled).
  Note: the exporter does write `sequencer.enabled` on save, but the
  step sequencer import path ignores it. This is an asymmetric design
  choice, not a bug.

---

## 4. Audio Payload

After the JSON, the remainder of the file is raw interleaved PCM.

### 4.1 Sample format

- **Sample type:** IEEE-754 float32, native (little-endian on all
  supported hosts).
- **Channel layout:** interleaved. For stereo, the order is L, R, L,
  R, ... See the writer at `src/presets/PresetFormat.cpp:91-95`:

  ```cpp
  for (int s = 0; s < numSamples; ++s)
      for (int c = 0; c < numChannels; ++c)
          interleaved[s * numChannels + c] = audioBuf.getSample(c, s);
  ```

  The reader mirrors this at `src/presets/PresetFormat.cpp:193-195`.

- **Channel count:** not hard-coded. Read from `audio_meta.channels`.
  In practice, `generatedAudioFull` is always stereo (2 channels), but
  a third-party reader must honour the `channels` field.

- **Sample rate:** not hard-coded. Read from `audio_meta.sampleRate`.
  T5ynth currently generates at 44.1 kHz
  (`T5ynthProcessor::getGeneratedSampleRate()` at
  `src/PluginProcessor.h:88`, backed by `generatedSampleRate` which
  defaults to 44100.0 at `:155`).

- **Sample count:** read from `audio_meta.numSamples`. Total PCM byte
  count is `numSamples * numChannels * sizeof(float)`.

### 4.2 Audio metadata block

The `audio_meta` JSON object is written at
`src/presets/PresetFormat.cpp:50-54`:

```json
"audio_meta": {
    "sampleRate": 44100.0,
    "channels": 2,
    "numSamples": 132300
}
```

`sampleRate` is a JSON number (double), `channels` and `numSamples`
are integers.

### 4.3 Absent audio

If the user saves a preset before any generation has happened,
`numSamples` is `0` and no bytes are written past the JSON. The reader
gates audio extraction on `numSamples > 0 && numChannels > 0 &&
audioOffset + audioBytes <= size` at
`src/presets/PresetFormat.cpp:189`, so missing or truncated audio
leaves `result.hasAudio == false` without erroring the load.

A consumer must therefore always check `LoadResult::hasAudio` before
using `LoadResult::audio`.

### 4.4 Optional inference-cache tail

When the Inference Cache is active, full, and the user explicitly
enables **include Inference-Cache** in the Save drawer, the writer adds
an `inferenceCache` JSON object and appends additional raw float32 PCM
buffers after the primary audio payload.

```json
"inferenceCache": {
    "capacity": 16,
    "entries": [
        { "sampleRate": 44100.0, "channels": 2, "numSamples": 132300 }
    ]
}
```

The cache stores raw inference audio only. It does not duplicate prompt,
seed, model, device, embedding, or drift state per cache entry. Each
entry's PCM layout is identical to the primary audio payload: interleaved
float32 with dimensions taken from that entry's metadata. Entries are
laid out sequentially in the same order as the `entries` array.

Older T5ynth readers that do not know `inferenceCache` still load the
preset's primary audio, because they stop reading after
`audio_meta.numSamples * channels * sizeof(float)` and ignore any extra
tail bytes.

---

## 5. Embedding Payload

The T5 text embeddings are **not** appended as binary — they are
embedded in the JSON payload as arrays of floats.

### 5.1 Format

Written at `src/presets/PresetFormat.cpp:57-66`:

```cpp
const auto& embA = processor.getLastEmbeddingA();
const auto& embB = processor.getLastEmbeddingB();
if (!embA.empty())
{
    juce::Array<juce::var> arrA, arrB;
    for (float v : embA) arrA.add(static_cast<double>(v));
    for (float v : embB) arrB.add(static_cast<double>(v));
    root->setProperty("embeddingA", arrA);
    root->setProperty("embeddingB", arrB);
}
```

Values are widened to `double` during serialisation (JSON has no
float32) and narrowed back to `float` on load at
`:168-177`. Some precision is lost in the double->string->double round
trip, but JSON round-tripping of IEEE-754 through `juce::JSON` is
exact to within 17 significant digits, so for practical purposes the
values are preserved.

### 5.2 Vector length

Both vectors nominally contain 768 floats (the hidden dimension of
the FLAN-T5-Large encoder used by Stable Audio Open). The format does
**not** enforce this; the reader simply copies whatever length the
JSON array contains. Third-party tools should assume 768 but validate.

### 5.3 What they represent

Both vectors are **sequence-averaged** — the T5 encoder output for
the prompt tokens is mean-pooled over the time dimension before being
stored. The per-token sequence is not preserved because the generation
pipeline only consumes the averaged vector anyway. A third-party tool
cannot reconstruct per-token attention from a `.t5p`.

### 5.4 Use on load

On load, the reader populates `LoadResult::embeddingA` and
`embeddingB`. `MainPanel::loadPreset` (`src/gui/MainPanel.cpp:751-755`)
then calls `processor.setLastEmbeddings(...)` and
`dimensionExplorer.setEmbeddings(...)`, which lets the Dimension
Explorer UI render the 768-bar A-B diff plot without rerunning
inference.

### 5.5 Absent embeddings

If the preset was saved before the first generation, `embA.empty()`
is true and the `embeddingA` / `embeddingB` keys are omitted from the
JSON entirely. The reader handles this by simply not populating
`result.embeddingA`/`B`, leaving them empty.

---

## 6. Semantic Axes State

T5ynth exposes three "semantic axes" slots in the GUI. Each slot is
two things: a dropdown selection (integer ID identifying which
precomputed axis to use) and a continuous value (usually in some
bounded range — range not enforced by the format).

This state is **GUI-only** (stored on `T5ynthProcessor` as
`lastAxes`, see `src/PluginProcessor.h:76-78`, `:151`) and would be
lost without explicit preservation.

### 6.1 Serialisation

Written at `src/presets/PresetFormat.cpp:31-43`:

```json
"semanticAxes": [
    { "dropdownId": 4, "value": 0.25 },
    { "dropdownId": 7, "value": -0.15 },
    { "dropdownId": 1, "value": 0.0 }
]
```

Exactly 3 entries, in slot order. `dropdownId` is an `int`, `value` a
`double` in JSON (float32 in memory).

### 6.2 Deserialisation

Read at `src/presets/PresetFormat.cpp:152-165`. The reader iterates
up to 3 elements and sets `result.hasAxes = true` whenever the
`semanticAxes` key is present and parses as an array — even if the
array is empty or contains garbage dropdownIds. Third-party writers
should include all 3 slots.

### 6.3 Meaning of `dropdownId`

The dropdown ID is an internal index into the semantic axes catalogue
in the T5ynth UI. The mapping is not part of the preset format and may
change between T5ynth versions. Treat it as opaque: a third-party
viewer should display the raw integer and not attempt to resolve a
human-readable axis name unless it has an out-of-band mapping.

---

## 7. Legacy Format Detection

The loader in `PresetFormat::loadFromFile`
(`src/presets/PresetFormat.cpp:107`) can handle three formats:

1. **Binary `T5YN` container** (current, version 3)
2. **Legacy plain JSON** (`.t5p` or `.json` containing a JSON object)
3. **Legacy XML** (`.t5p` containing a raw APVTS dump)

### 7.1 Detection logic

```cpp
bool isBinary = (size >= 12 && std::memcmp(data, kMagic, 4) == 0);
```
(`src/presets/PresetFormat.cpp:119`)

If the first four bytes are not `T5YN`, the loader reads the file as
text and dispatches on the first non-whitespace character:

```cpp
if (fileText.trimStart().startsWith("{"))          // JSON branch
else if (fileText.trimStart().startsWith("<"))     // XML branch
```
(`src/presets/PresetFormat.cpp:209`, `:233`)

There is no other fallback. A corrupt file that begins with neither
`T5YN`, `{`, nor `<` will produce a `LoadResult` with `success =
false` and all other fields default-initialised.

### 7.2 Legacy JSON (and `.json` export)

The JSON branch calls `processor.importJsonPreset(fileText)` directly
and then extracts prompts/seed/device/model from the
`synth` object for the UI (`src/presets/PresetFormat.cpp:211-230`).

**Separate flow — `.json` export/import via Preset Panel:** the
"Export Preset" button at `src/gui/PresetPanel.cpp:78` writes the
raw output of `exportJsonPreset()` to a `.json` file. Because the
prompt-patch step only happens in `PresetFormat::saveToFile`, a
`.json` export has empty `promptA`/`promptB`, no embeddings, no audio,
no `semanticAxes` key, and no `audio_meta` key. It is a
parameter-only snapshot. The "Import Preset" button at
`src/gui/PresetPanel.cpp:35` consumes the same format and is not part
of the `.t5p` flow.

The effect is: `.json` files are a lossy subset of `.t5p` and are
treated as "legacy JSON" by the `.t5p` loader. They round-trip
parameters but not prompts, audio, axes, or embeddings.

### 7.3 Legacy XML

The XML branch at `src/presets/PresetFormat.cpp:233-245` parses the
file as an XML document and reconstructs a `ValueTree` via
`juce::ValueTree::fromXml`, then replaces the APVTS state wholesale.
This is the raw APVTS dump format that JUCE's `createXml()` produces.
Historically T5ynth used this as its preset format. It has no prompts,
no audio, no embeddings. Only the APVTS-resident parameters survive.

No version detection is performed on XML: any `ValueTree` that
`fromXml` accepts is loaded. A malformed or unrelated XML document
will produce `success = false` without further diagnostics.

---

## 8. Versioning and Migration

The header contains a version field (`kVersion = 3` at
`src/presets/PresetFormat.h:64`). The reader at
`src/presets/PresetFormat.cpp:125` enforces a strict equality check:

```cpp
uint32_t version = *reinterpret_cast<const uint32_t*>(bytes + 4);
if (version != kVersion) { /* reject, return LoadResult{success=false} */ }
```

**No migration logic exists.** Version 1 predates the `T5YN` magic
(it was the plain JSON / XML branches). Version 2 was the previous
binary container; version 3 is the current binary container. A future
version 4 must either (a) add a branching dispatch in `loadFromFile`
that handles both versions, or (b) bump `kVersion` and accept that older
loaders will reject the new file cleanly rather than silently
mis-interpret it.

Consequences:

- Any breaking change to the JSON schema or the PCM layout should bump
  `kVersion`. Older T5ynth builds will then report load failure instead
  of silently dropping unknown fields.
- A preset written with an unknown version number (including 0, 99, or
  0xFFFFFFFF) is rejected up front — the loader returns
  `LoadResult{success = false}` and, in debug builds, prints the
  offending version via `DBG`.
- Third-party writers must write `version = 2` to produce files that
  the current loader will accept.

---

## 9. Entry Points

| API                                    | File / Line                                  |
| -------------------------------------- | -------------------------------------------- |
| `PresetFormat::saveToFile`             | `src/presets/PresetFormat.cpp:9`             |
| `PresetFormat::loadFromFile`           | `src/presets/PresetFormat.cpp:107`           |
| `PresetFormat::LoadResult` (struct)    | `src/presets/PresetFormat.h:26`              |
| `PresetFormat::getPresetsDirectory()`  | `src/presets/PresetFormat.cpp:252`           |
| `T5ynthProcessor::exportJsonPreset()`  | `src/PluginProcessor.cpp:1749`               |
| `T5ynthProcessor::importJsonPreset()`  | `src/PluginProcessor.cpp:1965`               |
| `MainPanel::savePreset()`              | `src/gui/MainPanel.cpp:678`                  |
| `MainPanel::loadPreset()`              | `src/gui/MainPanel.cpp:711`                  |
| `MainPanel::loadDefaultPreset()`       | `src/gui/MainPanel.cpp:539`                  |
| `PresetPanel::exportPreset()` (JSON)   | `src/gui/PresetPanel.cpp:78`                 |
| `PresetPanel::importPreset()` (JSON)   | `src/gui/PresetPanel.cpp:35`                 |

### 9.1 `LoadResult` contract

```cpp
struct LoadResult {
    bool success = false;
    juce::String presetName;
    juce::String promptA, promptB;
    int seed = 123456789;
    bool randomSeed = false;
    juce::String device;
    juce::String model;

    juce::AudioBuffer<float> audio;
    double sampleRate = 44100.0;
    bool hasAudio = false;

    struct AxisState { int dropdownId = 1; float value = 0.0f; };
    std::array<AxisState, 3> axes;
    bool hasAxes = false;

    std::vector<float> embeddingA, embeddingB;
};
```
(`src/presets/PresetFormat.h:26-48`)

The consumer is expected to check `success`, then `hasAudio` and
`hasAxes`, and test `embeddingA.empty()` before using the embeddings.

### 9.2 `saveToFile` ordering

`saveToFile` at `src/presets/PresetFormat.cpp:9-101` performs these
steps in order:

1. Call `exportJsonPreset()` to get the base parameter JSON.
2. Parse it back to a `DynamicObject`.
3. Patch prompts, seed, device, model, randomSeed into `synth`.
4. Add the `semanticAxes` array.
5. Add the `audio_meta` object.
6. Optionally add `embeddingA` / `embeddingB` arrays.
7. Re-serialise to a string, compute byte length.
8. Write header: magic, version, JSON length.
9. Write JSON bytes.
10. If `numSamples > 0 && numChannels > 0`, interleave and write PCM.
11. Flush, return success.

### 9.3 Bundled factory presets

Factory presets are baked into the binary via `juce_add_binary_data`
in `CMakeLists.txt`:

```cmake
juce_add_binary_data(T5ynthData SOURCES
    resources/ir/emt_140_plate_bright.wav
    resources/ir/emt_140_plate_medium.wav
    resources/ir/emt_140_plate_dark.wav
    "resources/presets/Evil Beauty.t5p"
    "resources/presets/Samba Getdown.t5p"
    "resources/presets/Talking about aliens.t5p"
    resources/T5ynth_Guide.html
)
```

`MainPanel::ensureBundledPresetsExist` copies these blobs into the user
preset directory when they are missing. Startup itself does not load a
factory demo preset; it keeps the APVTS defaults unless the standalone
session buffer is restored.

---

## 10. Worked Example: Generic `.t5p` Header

A `.t5p` container begins with a fixed 12-byte header:

```
offset  bytes                                    decoded
------  ---------------------------------------  ------------------
0x00    54 35 59 4E                              magic = "T5YN"
0x04    03 00 00 00                              version = 3 (LE)
0x08    NN NN NN NN                              JSON length (LE)
0x0C    7B 22 76 65 72 73 69 6F 6E 22 3A ...     JSON begins: {"version":...
```

The JSON length is little-endian and determines where the optional raw
stereo float32 PCM tail begins. If the file contains audio,
`numSamples` and `channels` must be read from `audio_meta` before
decoding that tail.

---

## 11. Known Limitations

### 11.1 Embedding averaging is lossy

`embeddingA` and `embeddingB` store the sequence-averaged T5 encoder
output. The per-token sequence is not preserved. A third-party tool
cannot, for example, recompute cross-attention weights or re-tokenise.
For T5ynth's own generation pipeline this is not a problem because
only the averaged vector is consumed downstream.

### 11.2 Model identifier is opaque and non-portable

The `synth.model` string is the model directory / identifier as seen
by the running T5ynth installation (e.g. a Stable Audio Open variant
name). The loader matches it against installed models by exact string
comparison at `src/gui/PromptPanel.cpp:485-492`:

```cpp
for (int i = 0; i < kNumModelSlots; ++i)
{
    if (modelSlotIds[i] == model)
    {
        modelBtns[i].setToggleState(true, juce::dontSendNotification);
        break;
    }
}
```

If the model is not installed, no button is toggled. There is **no**
error raised, no status message, and no fallback — the preset is
otherwise fully loaded (audio, params, embeddings) but the user's
model selector will show whatever was previously active. This is
graceful to the point of being silent; a third-party tool that wants
to warn on missing models must do its own check.

### 11.3 No checksum or integrity check

The format has no CRC, no SHA, and no magic trailer. A corrupted PCM
tail will simply produce distorted audio on load (as long as the file
size still matches `audio_meta.numSamples * channels * 4`). A
corrupted JSON will be caught by `juce::JSON::parse` returning a
non-object and the loader returning an empty `LoadResult`.

### 11.4 Dependency on JUCE's JSON pretty-printer

The writer uses `juce::JSON::toString(parsed, /*pretty=*/true)`. A
third-party reader must not assume a specific whitespace layout —
only that the bytes between offset 12 and 12+jsonLen are valid UTF-8
JSON describing a single root object.

### 11.5 `version` field is validated by strict equality

The reader at `src/presets/PresetFormat.cpp:125` enforces
`version == kVersion` (currently 3). Any other value — including
0, 99, 0xFFFFFFFF, or a future version 4 — is rejected with
`LoadResult{success = false}`. This is intentional: there is no
migration logic, so accepting an unknown version would silently
mis-interpret the payload under the v3 schema.

The flip side is that a hypothetical v1 binary writer (none exists
in practice) would also be rejected. Version 1 was never used with
the `T5YN` magic — it referred to the pre-binary JSON/XML fallback
branches handled in the non-`isBinary` code path.

### 11.6 Asymmetric handling of `sequencer.enabled`

The exporter writes `sequencer.enabled` into the JSON, but the
importer at `src/PluginProcessor.cpp:2207-2209` deliberately does not
restore it (the lines are commented with "Preserve current seq_running
state — don't stop playback on preset load"). The generative
sequencer's `enabled` field **is** restored at `:2319`. This is an
intentional UX choice for the step sequencer but may be surprising to
a third-party tool that expects symmetry.
