# Adding a Model to T5ynth

Practical HOWTO for adding a new diffusion audio engine to T5ynth. Based on
the concrete pattern used for the three currently supported engines:

- **Stable Audio Open 1.0** (`stable-audio-open-1.0`, diffusers format, gated)
- **Stable Audio Open Small** (`stable-audio-open-small`, native
  stable-audio-tools format, gated)
- **AudioLDM2** (`audioldm2`, diffusers format, ungated, experimental)

All citations are `path/to/file:line` into the working tree of the
t5ynth repo.

---

## 1. Overview

"Adding a model" in T5ynth means two independent but paired changes:

1. **Python backend (`backend/pipe_inference.py`)** — teach the inference
   process how to *load* and *generate from* the model. The backend discovers
   model directories on disk, classifies them by format, lazy-loads a pipeline
   per (model, device) pair, and routes JSON requests to the matching
   generator. A new engine needs either a new format branch (new loader +
   new `_generate_*` function) or must fit one of the existing branches.
2. **C++ UI / install flow (`src/gui/SetupWizard.cpp`)** — teach the Model
   Manager about the model's identifier, display name, HuggingFace repo,
   license, and how the user is expected to obtain the weights. This drives
   the Settings dropdown, the per-model walkthrough text, and (if the model
   is ungated) the in-app download flow.

The contract between the two sides is intentionally thin: the backend looks
for a directory at a well-known app-support path whose name matches the
C++-side `id`, and the C++ side tags generation requests with a `"model"`
field that is forwarded verbatim to Python
(`src/inference/PipeInference.cpp:443`). There is no schema registration,
no handshake — just a matching string.

All engines must produce **44.1 kHz stereo float32 PCM** and expose a
**768-dimensional T5-style prompt embedding** for DimensionExplorer and
semantic-axis manipulation. Models that natively produce something else
must be wrapped to hit those contracts (AudioLDM2 is the precedent, see §2.4).

---

## 2. Backend side — Python

### 2.1 Model format detection

Format detection happens in `backend/pipe_inference.py:57-71` in
`_model_format(model_dir)`. Current logic:

- If `model_index.json` exists and its `_class_name` contains `"AudioLDM2"`
  → `"audioldm2"`
- Else if `model_index.json` exists → `"diffusers"`
- Else if `model_config.json` exists → `"native"`
- Else `None` (not a model directory)

`find_models()` at `backend/pipe_inference.py:76-92` walks the known
per-platform model base dirs (macOS `~/Library/T5ynth/models`,
Linux `~/.local/share/T5ynth/models`, legacy `~/t5ynth/models`) and fills
two globals: `_available_models` (name → Path) and `_model_formats`
(name → format string).

**If your new engine fits an existing format**, detection is automatic —
dropping the weights into `<model_base>/<your-id>/` is enough for the
backend to pick them up, and the existing generator runs.

**If your engine needs a new format**, extend `_model_format()` with a
new discriminator (e.g. a marker file, a distinctive key in `model_index.json`,
or a vendor-specific config filename) and return a new format string.

### 2.2 Loader dispatch

`load_pipeline(model_dir, device)` at
`backend/pipe_inference.py:147-155` is the top-level dispatch:

```python
if fmt == "audioldm2":  return _load_audioldm2_pipeline(model_dir, device)
if fmt == "native":     return _load_native_pipeline(model_dir, device)
return _load_diffusers_pipeline(model_dir, device)
```

A loader takes `(model_dir: Path, device: str)` and must return **any**
object that `generate()` can route (see §2.4). It does not need to be a
real `diffusers.DiffusionPipeline` — wrapping is the common case.

Existing loaders:

- `_load_diffusers_pipeline` — `backend/pipe_inference.py:158-174`.
  Thin wrapper around `StableAudioPipeline.from_pretrained`; patches
  the scheduler (`_patch_scheduler`, line 123) for the final-step
  sigma→0 crash and enables attention slicing on `mps`/`cpu`. Returns
  the raw `StableAudioPipeline` instance.
- `_load_audioldm2_pipeline` — `backend/pipe_inference.py:238-251`.
  Loads `AudioLDM2Pipeline.from_pretrained` and returns an
  `AudioLDM2Wrapper` (line 220-235) that carries `sample_rate=16000`,
  `target_sample_rate=44100`, and shims `tokenizer` / `text_encoder`
  to the secondary T5 encoder so the common embedding code works.
- `_load_native_pipeline` — `backend/pipe_inference.py:254-283`.
  Bypasses `diffusers` entirely, loads the stable-audio-tools native
  model via `create_model_from_config` + `load_ckpt_state_dict`, and
  returns a `NativePipeline` wrapper (line 195-217). It also
  mocks a long list of optional deps via `_mock_optional_deps()`
  (line 177-192) — repeat that pattern if your model pulls in a
  similarly hostile dependency graph.

### 2.3 Sample-rate contract — 44.1 kHz stereo

Every generator **must** return `(audio_np: np.ndarray, sample_rate: int,
seed: int, elapsed_s: float, emb_stats)` where `audio_np.shape == (2, N)`
and `sample_rate == 44100`. The send side at
`backend/pipe_inference.py:896-916` assumes this layout.

If your model runs at a different native rate, resample in the generator.
See AudioLDM2 at `backend/pipe_inference.py:601` for the 16 kHz → 44.1 kHz
path via `_resample_audio` (line 302-309, uses
`scipy.signal.resample_poly`).

Mono models: copy the single channel to stereo with `np.vstack([a, a])`.
See AudioLDM2 at `backend/pipe_inference.py:603-604`.

### 2.4 Embedding contract — 768d T5-style

Semantic axes, DimensionExplorer, and alpha interpolation all operate on a
prompt embedding tensor of shape `[1, seq, 768]`. Every engine must produce
one — real or synthetic.

- **Diffusers StableAudio**: the T5 encoder (`pipe.text_encoder`,
  `pipe.tokenizer`) natively produces `[1, 128, 768]` — see the
  `encode_text` closure in `generate()` at
  `backend/pipe_inference.py:776-782`. No work needed.
- **Native stable-audio-tools**: T5 lives inside
  `model.conditioner.conditioners` as a non-standard conditioner.
  `NativePipeline.__init__` at `backend/pipe_inference.py:198-209` walks
  the conditioner dict looking for the object that has both `tokenizer`
  and `model` attributes and exposes it via properties so the common
  code path works.
- **AudioLDM2**: uses *dual* embeddings — a T5-raw `prompt_embeds` at
  `[B, seq, 1024]` for secondary cross-attention and a GPT2
  `generated_prompt_embeds` at `[B, 8, 768]` for primary cross-attention.
  The wrapper at `backend/pipe_inference.py:220-235` exposes
  `tokenizer_2` / `text_encoder_2` as `tokenizer` / `text_encoder`.
  The generator at `_generate_audioldm2` (line 457-607) manipulates both
  embeddings in parallel: alpha interpolation, magnitude, noise, dim
  offsets, and semantic axes are applied to the primary (768d) embedding;
  the 1024d secondary is kept in lockstep where relevant. Note the
  padding gymnastics at line 511-523 — AudioLDM2's T5 uses dynamic
  padding, whereas SA uses fixed 128-token padding.

**If your model does not produce a 768d T5 embedding**, you have two options:

1. **Synthesise one**: run a side T5 encoder on the same prompt purely to
   compute `emb_stats` (the mean-pooled `[768]` numpy array sent back to
   JUCE for DimensionExplorer). This gives the UI something to draw but
   the DimensionExplorer's "inject offset into dim N" feature will be a
   no-op unless the offsets feed back into whatever your model actually
   uses.
2. **Project into 768d**: wrap your native embedding with a fixed linear
   projection. This is the "honest" option but requires the manipulation
   code to be ported.

Either way, the 768 is load-bearing: the IPC send path at
`backend/pipe_inference.py:910-913` emits `num_dims` and two float32
arrays of that length. The JUCE side currently assumes 768
— a different size is not a simple swap.

### 2.5 Generation dispatch

`generate(pipe, request)` at `backend/pipe_inference.py:735-877` is the
top-level generator. It dispatches by isinstance:

```python
if isinstance(pipe, AudioLDM2Wrapper): return _generate_audioldm2(...)
if isinstance(pipe, NativePipeline):   return _generate_native(...)
# else fall through to the diffusers StableAudio path inline
```

Add your dispatch here. Prefer a dedicated `_generate_<engine>` function
rather than extending the inline diffusers fallback — the inline path
assumes `pipe.transformer`, `pipe.vae`, `audio_start_in_s`/`audio_end_in_s`
parameters, and the SA-specific output layout.

Your generator must accept these request fields (defaults in parens):

- `prompt_a` (`""`), `prompt_b` (`""`)
- `alpha` (`0.0`) — interpolation weight, −1 = pure A, +1 = pure B,
  `|alpha| > 1` triggers renormalisation
- `magnitude` (`1.0`) — embedding scale
- `noise_sigma` (`0.0`) — additive gaussian noise on the embedding,
  use `np.random.Generator(np.random.PCG64(seed))` for cross-platform
  determinism (see `backend/pipe_inference.py:550-553`)
- `duration` (`3.0`) — seconds, capped at 11 s by the UI
- `steps`, `cfg_scale` — engine-specific sensible defaults
- `seed` (−1 = random)
- `dimension_offsets` — optional `[[idx, val], ...]`
- `semantic_axes` — optional `{axis_key: value}`, apply via
  `_apply_semantic_axes` (line 394-444) with an `encode_fn` closure
  that produces `(emb[1,seq,768], mask[1,seq])`

`mode="interpolate"` and `mode="decode_cached"` at
`backend/pipe_inference.py:990-997` currently hard-reject AudioLDM2
(latent cache not implemented). If your new engine can reuse the
VAE-decode path, add it; if not, raise `ValueError` with a clear message.

### 2.6 Request routing — the `model` field

The main loop at `backend/pipe_inference.py:960-974` reads `request["model"]`
and looks it up in `_available_models`. If the key is missing or unknown,
it falls back to `default_model` (`stable-audio-open-1.0` if present, else
the first discovered model, line 943-946).

The C++ side writes this field in two places:

- `src/inference/PipeInference.cpp:443` — per-request serialisation
- `src/inference/PipeInference.cpp:568` — preload path

The string must match your new engine's directory name (= the `id`
field in `KnownModel`, see §3.1).

### 2.7 PyInstaller bundling — `backend/pipe_inference.spec`

When a new engine pulls in new Python packages, PyInstaller's static
analysis will almost always miss some imports. The spec file at
`backend/pipe_inference.spec` is the surface to patch. Three levers:

1. **Hidden imports** — lazy or string-loaded modules.
   See `backend/pipe_inference.spec:23-51`. Use
   `collect_submodules('your_pkg')` for packages with dynamic
   pipeline/scheduler registries (line 29-30 for diffusers.stable_audio).
2. **Data files** — non-Python assets bundled inside the package
   (configs, yaml, json).
   See `backend/pipe_inference.spec:56-59`. Pattern:
   `datas += collect_data_files('your_pkg', includes=['**/*.json', '**/*.yaml'])`.
3. **Package metadata** — some packages (notably anything that calls
   `importlib.metadata.version()` at import time, which transformers does)
   need their `*.dist-info` copied in.
   See `backend/pipe_inference.spec:63-76`. Pattern:
   `datas += copy_metadata('your_pkg')`.

The CUDA strip filter at `backend/pipe_inference.spec:107-120` drops
training-only CUDA libs (nccl, cufft, cusolver, cupti, triton, etc).
Verify your new engine does not need any of the excluded libs — add
exceptions to the regex if it does.

Hostile gotcha: PyInstaller + macOS + multiprocessing is a known landmine.
Never add multiprocessing-using packages to `runtime_hook.py`, and always
test the bundled binary end-to-end through the JUCE app, not only the
Python source in a venv.

---

## 3. C++ side — Model Manager UI and install flow

### 3.1 The `KnownModel` struct

Declared at `src/gui/SetupWizard.cpp:40-48`:

```cpp
struct KnownModel {
    const char* id;            // identifier; matches backend directory name
    const char* displayName;   // shown in the Settings ComboBox
    const char* hfRepo;        // HuggingFace repo, e.g. "cvssp/audioldm2"
    const char* ghRelease;     // GitHub Release tag URL base (nullptr = use HF)
    const char* licenseUrl;    // full license text
    const char* licenseNotice; // confirmation dialog body
    bool        downloadable;  // true = in-app HF download, false = manual walkthrough
};
```

Field semantics:

- `id` — the string that ends up in the generation request's `model` field
  and as the on-disk directory name under `T5ynth/models/`. Keep it
  filesystem-safe (lowercase, ascii, hyphens). Must match the string
  your Python loader expects.
- `displayName` — ComboBox label. Short, proper-cased.
- `hfRepo` — `namespace/repo`. Used by `performAutoScan()` to probe the
  HF cache, by "Open Model Page" to launch the browser
  (`src/gui/SetupWizard.cpp:179-183`), by the in-app download's tree-walk
  API call (`src/gui/SetupWizard.cpp:589`), and by each file's resolve URL
  (`src/gui/SetupWizard.cpp:799`).
- `ghRelease` — set to `nullptr` unless weights are mirrored to a
  GitHub Release with a fixed file list. None of the current three
  engines use it (all `nullptr`), but the code path is live
  (`downloadGhReleaseInThread`, `src/gui/SetupWizard.cpp:648-738`).
- `licenseUrl` / `licenseNotice` — shown in the confirm dialog at
  `src/gui/SetupWizard.cpp:544-563` before any download begins.
  Required for `downloadable == true`; used by both for "Open Model Page"
  fallback. Non-commercial licenses must state the restriction in plain
  prose, for example AudioLDM2's notice at line 69-72:
  `"Non-commercial use only (no revenue threshold, no exceptions)"`.
- `downloadable` — if `true`, the "Download from HuggingFace" button is
  visible (`updateStatus`, `src/gui/SetupWizard.cpp:977`) and `startDownload`
  runs the in-app flow. If `false`, the button is hidden and the user gets
  a manual walkthrough instead (see §3.3). Gated models (anything requiring
  HF auth, including all Stability models) **must** be `downloadable=false`
  — T5ynth never prompts for HF tokens.

### 3.2 The `kKnownModels[]` array

At `src/gui/SetupWizard.cpp:49-74`. The order matters: it is the order
the entries appear in the Settings ComboBox
(`src/gui/SetupWizard.cpp:134-135`). The default selection is pinned to
ComboBox id 2 = array index 1 = Stable Audio Open Small at
`src/gui/SetupWizard.cpp:138`; update that line if you want a different
default. `kNumKnownModels` is computed at line 75 — nothing to bump.

Append your entry to the end unless you have a concrete reason to
re-order (e.g. shifting the default). Each entry is a single C++
struct literal.

### 3.3 Per-model walkthrough text — `updateStatus()`

`SettingsPage::updateStatus()` at `src/gui/SetupWizard.cpp:962-1061` is
where the instructions panel is populated. The "not installed" branch at
line 993-1060 is an if/else-if chain on `id`:

- `id == "audioldm2"` → `src/gui/SetupWizard.cpp:1003-1015`: in-app download
  walkthrough, because it's the only ungated model.
- `id == "stable-audio-open-small"` → `src/gui/SetupWizard.cpp:1016-1044`:
  6-step browser-based manual walkthrough using the Downloads-folder
  Auto-Scan pattern (see §5).
- else (currently SA 1.0) → `src/gui/SetupWizard.cpp:1045-1059`: terminal
  walkthrough via `huggingface-cli`.

**Add a new `else if (id == "your-id")` branch** with an honest, technical
install description. Style rules from the walkthrough audit:

- No marketing adjectives. No "fast", "high quality", "recommended".
- Name the publisher and academic context (if any) in one sentence.
- Always echo the target directory via
  `instructionsLabel.setText("... Target: " + targetPath + " ...")` —
  users need to see where files will land.
- Always name the license. If it is non-commercial, say so without
  weasel words.
- For downloadable models: tell the user to click "Download from
  HuggingFace" and wait. Nothing more.
- For gated/manual models: a numbered step list ending with
  "click Auto-Scan" or "click Browse...".

### 3.4 Accessor plumbing

Three functions read the selected entry out of `kKnownModels` based on
the ComboBox index:

- `selectedModelId()` — `src/gui/SetupWizard.cpp:91-97`
- `selectedHfRepo()` — `src/gui/SetupWizard.cpp:99-105`
- `selectedGhRelease()` — `src/gui/SetupWizard.cpp:107-113`
- `selectedDownloadable()` — `src/gui/SetupWizard.cpp:115-121`

These feed into:

- `browseForModel` — `src/gui/SetupWizard.cpp:242-267`
- `performAutoScan` — `src/gui/SetupWizard.cpp:459-534`
- `startDownload` — `src/gui/SetupWizard.cpp:537-646`
- `openPageButton.onClick` — `src/gui/SetupWizard.cpp:179-183`

They fall back to `kKnownModels[0]` if nothing is selected, so an empty
array would crash — never ship with `kNumKnownModels == 0`.

---

## 4. Install-marker heuristic — `hasModelMarker()`

At `src/gui/SetupWizard.cpp:13-32`. Two checks, both required:

1. **Metadata file present**: one of `kModelMarkers[]` =
   `{"model_index.json", "model_config.json"}`
   (`src/gui/SetupWizard.cpp:7`) must exist as a regular file in the
   directory. This distinguishes "diffusers/native model dir" from
   "random directory that happens to contain a .safetensors".
2. **≥100 MB of weights**: recursively sum the sizes of every
   `*.safetensors`, then `*.bin`, then `*.ckpt` file under the directory
   until the total exceeds `kMinWeightBytes = 100 * 1024 * 1024`
   (`src/gui/SetupWizard.cpp:11, 21-29`).

The 100 MB floor exists because the HuggingFace cache routinely leaves
stale metadata-only stubs after a failed / cancelled download; without
it, a metadata-only stub would falsely register as "installed" and the
backend would then fail to load.

**When the 100 MB floor is wrong**: a genuinely tiny experimental model
(say, a 50 MB distilled variant) would fail this check. Your options:

- Lower the global floor (affects *all* models; risk of false positives).
- Add a per-model override. There is currently no plumbing for this —
  `hasModelMarker` does not take a model id. The cleanest patch is to
  turn `hasModelMarker` into a thin wrapper over a helper that takes
  an explicit floor, and look up the floor from `kKnownModels` via a
  new field.
- Add a marker file unique to the small model (e.g. a vendor config)
  and short-circuit `hasModelMarker` when that marker is present,
  regardless of weight bytes.

None of the current three engines trigger this case. The default
metadata-marker set (`model_index.json` or `model_config.json`) covers
every diffusers and stable-audio-tools model format in the wild today.

`cleanupBadFiles()` at `src/gui/SetupWizard.cpp:747-758` and
`isLfsPointer()` at line 740-745 sweep the target directory for
git-LFS pointer files left behind by failed downloads. No changes
needed for a new model.

---

## 5. Target install directory convention

`getAppSupportModelDir(modelId)` at
`src/gui/SetupWizard.cpp:82-89` is the single source of truth for the
install location:

```cpp
appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_LINUX
appData = appData.getChildFile("share");   // userApplicationDataDirectory
                                            // returns $HOME on Linux; add "share"
                                            // to get ~/.local/share
#endif
return appData.getChildFile("T5ynth/models/" + modelId);
```

Resolved paths:

| Platform | Install root |
|---|---|
| macOS | `~/Library/T5ynth/models/<id>/` |
| Linux | `~/.local/share/T5ynth/models/<id>/` |
| Windows | `%APPDATA%\T5ynth\models\<id>\` |

The backend's `find_models()` at `backend/pipe_inference.py:77-82`
enumerates the same three roots plus `~/t5ynth/models` (legacy).
If you add a new platform (or a new legacy path) **both sides must
agree** — keep the lists in sync.

`scanForModelById` at `src/gui/SetupWizard.cpp:198-219` also checks
`~/.cache/huggingface/hub/models--<namespace>--<repo>/snapshots/*`, so
users who installed via `huggingface-cli download` without
`--local-dir` still get auto-detected.

### 5.1 Downloads-folder walkthrough flow (`trySaSmallInstallFromFolder`)

The `trySaSmallInstallFromFolder` pattern at
`src/gui/SetupWizard.cpp:309-457` is currently **hard-coded to
stable-audio-open-small**, but it is a general-purpose shape worth
reusing for any gated model whose HuggingFace file list is short
and flat (≤ 5 files, all at the repo root, no nested subfolders).

The relevant constants:

- `kSaSmallRequired[]` — `src/gui/SetupWizard.cpp:280-284`: the files
  that must be present (`model.safetensors`, `model_config.json`,
  `LICENSE`).
- `kSaSmallWrongFiles[]` — `src/gui/SetupWizard.cpp:291-296`: files the
  user may have fetched by mistake alongside the correct ones. When
  detected, the UI tells the user by name to delete them.

The flow:

1. `performAutoScan()` (`src/gui/SetupWizard.cpp:459-534`) first probes
   the known on-disk paths via `scanForModel()`.
2. If not found and the selected model is SA Small, it calls
   `trySaSmallInstallFromFolder(downloads, /*reportIfMissing*/ false)`.
   On success, files are copied to the app-support dir.
3. On failure, if *some* required files are present in Downloads, it
   re-runs with `reportIfMissing=true` to show the incomplete-download
   dialog (line 502-506).
4. Otherwise it pops a folder picker pre-opened at the Downloads folder
   (line 510-533) so users with non-default Downloads dirs can point
   at their save location.

**To reuse for a new gated model**, refactor the three pieces
(`kSaSmallRequired`, `kSaSmallWrongFiles`, `trySaSmallInstallFromFolder`)
into a per-model-id dispatch, then drop the hard-coded id check at
`src/gui/SetupWizard.cpp:476`. This refactor has not been done yet —
if you are the first to need it, you are on the hook for it.

If you are adding an **ungated** model (the AudioLDM2 case), skip this
section entirely: the in-app download (§6) is simpler.

---

## 6. In-app download flow — `downloadable=true`

Entry point: `startDownload()` at `src/gui/SetupWizard.cpp:537-646`.

1. **License gate** (`src/gui/SetupWizard.cpp:540-563`). If
   `licenseNotice` is set and not yet accepted in this session, show an
   OK/Cancel dialog with the license text and full URL; on "Accept &
   Download", re-enter `startDownload`. This is synchronous w.r.t. the
   download — no weights are fetched before acceptance.
2. **Optional GitHub Releases path** (`src/gui/SetupWizard.cpp:578-584`).
   If `ghRelease != nullptr`, hand off to `downloadGhReleaseInThread()`
   (line 648-738) which uses a fixed file list. Currently unused by any
   shipping model. File list is defined inline at line 656-660 with
   expected sizes — not generalised.
3. **HuggingFace tree API path** (`src/gui/SetupWizard.cpp:586-645`).
   Spawns a detached thread that:
   - `GET https://huggingface.co/api/models/<hfRepo>/tree/main?recursive=true`
     (line 589) with a 15 s connection timeout
   - parses the JSON response; if the HF API returned an `"error"` field,
     surfaces it verbatim (line 607-614)
   - builds a `std::vector<DownloadFile>` of `{remotePath, size}` for
     every `type == "file"` entry (line 618-625)
   - marshals back to the UI thread and calls
     `downloadAllFilesInThread()` (line 636)
4. **File streaming** — `downloadAllFilesInThread()` at
   `src/gui/SetupWizard.cpp:760-894`. For each file:
   - `GET https://huggingface.co/<hfRepo>/resolve/main/<path>`
     (line 799), JUCE follows HF's LFS redirects
   - streams to disk in 64 KB chunks (line 818-835) while updating
     `downloadedBytes` (atomic, polled by a 250 ms UI-thread timer
     at line 896-901)
   - server-error detection (line 839-865): if a large file came back
     as a tiny (< 100 KB) HTML/JSON error page, delete it and fail
     with the HF error text
   - size mismatch detection (line 868-884): if `written < df.size / 2`
     for any large file, fail with expected/received sizes
   - resume heuristic (line 780-786): skip files that are already
     present at ≥ 90 % of expected size
5. **Post-download patching** (`src/gui/SetupWizard.cpp:914-926`). This is
   the AudioLDM2-specific workaround for the `GPT2Model` →
   `GPT2LMHeadModel` change in transformers ≥ 4.45. If your new model
   has a similar "ships broken, needs a one-line rewrite to work with
   modern deps" case, add it here — but only after confirming the fix
   upstream is not landing soon.

**No HuggingFace tokens.** T5ynth never prompts for, stores, or sends an
HF auth token. Gated models are therefore never `downloadable=true`;
they go through the manual walkthrough (§3.3) instead.

`onDownloadFinished()` at `src/gui/SetupWizard.cpp:903-940` is the
UI-thread rendezvous for both success and failure. It re-scans on
success, shows the error text in the instructions panel on failure.

---

## 7. License and attribution

Every new engine requires three pieces of attribution work. Failing to do
these breaks the `LICENSE`-compliant redistribution of T5ynth itself.

### 7.1 `THIRD_PARTY_LICENSES.txt`

Append a new section between existing model sections, matching the
format at `THIRD_PARTY_LICENSES.txt:7-29` (Stable Audio Open 1.0) or
`THIRD_PARTY_LICENSES.txt:32-53` (Stable Audio Open Small). Required
fields:

- `**Model:**` — HF repo
- `**License:**` — full license name (not just "CC" or "Stability")
- `**URL:**` — HF page
- Prose paragraph: what T5ynth uses it for, whether weights are
  bundled (they should not be), and the terms.
- Any attribution/acknowledgement text the license requires.

**Gap noted:** AudioLDM2 is missing from
`THIRD_PARTY_LICENSES.txt` as of this writing (only the two Stability
models are documented at lines 7-54). That file should be updated in the
same commit that lands AudioLDM2 support — if you are adding a fourth
engine, add the AudioLDM2 section while you are there.

### 7.2 `resources/T5ynth_Guide.html` — Third-Party Components

The Third-Party Components section starts at
`resources/T5ynth_Guide.html:1113` (heading `<h2 id="thirdparty">`).
Existing model subsections:

- Stable Audio Open 1.0 & Small — `resources/T5ynth_Guide.html:1118-1129`
  (shared `<h3>`)
- AudioLDM2 — `resources/T5ynth_Guide.html:1131-1139`

Add a new `<h3>` subsection with publisher, license, URL, and the license
terms in one paragraph. Mirror the HTML format of the AudioLDM2 block for
non-commercial models; the Stability block for revenue-threshold models.

### 7.3 `resources/T5ynth_Guide.html` — Setup: Model Installation

Separate from the legal section, the Setup chapter at
`resources/T5ynth_Guide.html:246` ("Setup: Model Installation") has a
`<h4>` per model:

- Stable Audio Open Small — line 256-276 (6-step browser walkthrough)
- AudioLDM2 — line 278-284 (one-click)
- Stable Audio Open 1.0 — line 286-298 (terminal walkthrough)

Add a `<h4>` for the new model with the same walkthrough content as the
in-app instructions from §3.3. Keep the two in sync — users will see both.

### 7.4 Non-commercial licenses

If the license is CC BY-NC-SA, CC BY-NC, or any other non-commercial
license, the walkthrough text (both `updateStatus()` and the HTML guide)
**must** state:

> non-commercial use only, no revenue threshold, no exceptions

verbatim or paraphrased with equivalent force. See the AudioLDM2
precedent at `src/gui/SetupWizard.cpp:1014-1015` and
`resources/T5ynth_Guide.html:283`. This is not legal advice — it is how
T5ynth has chosen to be honest with its users about the difference
between "commercial up to $1M" and "no commercial".

---

## 8. Manual updates — prose style

The "Third-Party Components" prose (§7.2) and the "Setup" walkthrough
prose (§7.3) have been audited for slop. When editing either section,
follow the existing rules:

- No "higher quality" — quality is subjective and model-dependent.
- No "faster" — speed depends on hardware, model, and steps.
- No "recommended" — we don't rank models for users.
- No "fallback" — AudioLDM2 is not a fallback, it's an alternative.
- Name the publisher. Name the academic context if there is one
  (e.g. "CVSSP, University of Surrey; Liu et al., 2023").
- Name the license literally (spelled-out name, not "open").
- Say how the user installs it. That is it.

Keep prose factual. Nobody reading developer docs or the Guide needs a
marketing pitch.

---

## 9. Testing checklist

Before opening a PR with a new engine, run this checklist in order:

1. **Python smoke test** — from a venv with deps installed, run
   `python backend/pipe_inference.py` with a fixture request on stdin.
   Verify the output header matches the expected channel/rate/sample
   layout. The exact request schema is in `docs/IPC_PROTOCOL.md`.
2. **Release build** — `cmake --build build_clean --config Release`.
   Always build under `build_clean/`; do not create alternate build
   dirs. Build must complete with zero warnings related to your
   changes.
3. **Model Manager UI** — launch the standalone, open Settings,
   select your new model in the ComboBox. Verify:
   - The walkthrough text in the `instructionsLabel` is your new
     branch from `updateStatus()`
   - "Download from HuggingFace" button is visible iff
     `downloadable == true`
   - "Open Model Page" launches the HF repo in a browser
4. **Install path** —
   - Downloadable: click "Download from HuggingFace", accept the
     license dialog, wait, verify the target directory is populated
     and `hasModelMarker()` returns true.
   - Manual: follow the walkthrough end-to-end in a *clean* state
     (delete `~/Library/T5ynth/models/<id>/` first), either reaching
     a successful Auto-Scan or a successful Browse... install.
5. **Backend handshake** — confirm the Model Manager status shows
   "<id>: Installed" and the backend status shows "Connected" after
   the install. The backend must discover the new directory via
   `find_models()` and register the format.
6. **End-to-end generation** — back in the main UI, select the new
   model in the Model & Device panel, type a prompt, hit Generate.
   Wait for audio. Play it. Verify:
   - No stdout corruption (stable-audio-tools is particularly bad
     at this — see the stdout redirect at
     `backend/pipe_inference.py:269-270`). Symptom: JUCE reports
     "parse failed" or "bad header".
   - Duration matches request (no over/undershoot beyond ±1 sample).
   - Sample rate is 44100 (check the status-bar readout or the
     engine code for resampling bugs).
7. **Sampler chain** — drop the generated clip into the Sampler, play
   a few notes, verify pitch-shift and loop points work.
8. **Wavetable chain** — switch to Wavetable engine, verify extraction
   runs on the new clip without crashing. WT is sensitive to short
   clips — re-test with a 1-second generation.
9. **DimensionExplorer** — open DimExplorer, verify the 768 bars appear
   and respond when you regenerate with a different prompt. If they
   don't, your `emb_stats` return value is broken (§2.4).
10. **Semantic axes** — sweep at least two axes on the new model. If
    the sound doesn't change, either `_apply_semantic_axes` isn't
    being called in your generator, or the embedding manipulation path
    is not wired in.
11. **Bundle test** (if you edited `pipe_inference.spec`) —
    `cd backend && pyinstaller pipe_inference.spec`, then launch
    T5ynth against the bundled binary. The venv-only test is
    insufficient; PyInstaller bundling failures only show up here.

If any step fails, do not ship. Failing to test end-to-end through the
bundled Python is the #1 historical cause of broken T5ynth releases.

---

## 10. Worked example — adding a hypothetical `foo-audio` engine

Suppose `foo-audio` is an ungated diffusers-format audio model at
`acme/foo-audio` on HuggingFace, Apache 2.0, native 44.1 kHz stereo,
with a working T5 encoder. Minimum patch:

**`backend/pipe_inference.py`** — nothing to change. The diffusers format
loader and the inline StableAudio generator will handle it as-is,
**assuming** `StableAudioPipeline.from_pretrained` actually works on
this repo. If it is a different pipeline class, add a format branch
(§2.1) and a loader (§2.2) and a `_generate_foo` function (§2.5).

**`backend/pipe_inference.spec`** — if `foo-audio` needs any new Python
packages, add them to `hidden` / `datas` (§2.7).

**`src/gui/SetupWizard.cpp`** — append one entry to `kKnownModels[]`
at line 74:

```cpp
{ "foo-audio", "Foo Audio", "acme/foo-audio", nullptr,
  "https://www.apache.org/licenses/LICENSE-2.0",
  "This model is licensed under Apache 2.0.\n\n"
  "- Free for any use including commercial\n"
  "- See license for attribution requirements\n\n"
  "T5ynth does not provide the model weights.", true },
```

Add one `else if` branch to `updateStatus()` at
`src/gui/SetupWizard.cpp:1015` (before the current `else if
(id == "stable-audio-open-small")`):

```cpp
} else if (id == "foo-audio") {
    instructionsLabel.setText(
        "FOO AUDIO\n"
        "Diffusion audio model published by Acme Research. Ungated on "
        "HuggingFace; click 'Download from HuggingFace' above and wait.\n\n"
        "  Source: https://huggingface.co/" + hfRepo + "\n"
        "  Target: " + targetPath + "\n\n"
        "License: Apache 2.0.", false);
}
```

**`THIRD_PARTY_LICENSES.txt`** — add a `## Foo Audio` section between
the existing model sections (see §7.1).

**`resources/T5ynth_Guide.html`** — add a `<h4>Foo Audio</h4>` block
in the Setup chapter (after line 284) and a `<h3>Foo Audio</h3>` block
in the Third-Party Components chapter (after line 1139).

Total: one Python spec touch-up (if new deps), one struct literal, one
`else if` branch, two prose additions. Build, test per §9, ship.

---

## Appendix — open refactoring opportunities

These are known rough edges you may hit while adding an engine. None
block a new engine, but fixing them would clean things up:

- `hasModelMarker()` is global; per-model weight-size floor requires a
  signature change. See §4.
- `trySaSmallInstallFromFolder` is hard-coded to
  `stable-audio-open-small`. If a second gated model wants the
  Downloads-folder flow, this needs to be generalised to a
  per-model file-list lookup. See §5.1.
- `downloadGhReleaseInThread()` has an inline file list. Only the
  release URL is parameterised — the actual files are hard-coded for
  the SA Small mirror. See `src/gui/SetupWizard.cpp:656-660`.
- The 768d embedding dimension is load-bearing at
  `backend/pipe_inference.py:910-913` and on the JUCE side. A model
  with a different embedding width needs either projection
  (§2.4 option 2) or a protocol extension — the latter touches the
  IPC spec.
- The `GPT2Model` → `GPT2LMHeadModel` rewrite at
  `src/gui/SetupWizard.cpp:914-926` is AudioLDM2-specific fixup in
  a generic code path. If a second model ever needs post-download
  patching, make this a per-model hook.
