# PyTorch Backend Bundling Notes

This document records the Linux/PyTorch backend bundling lessons learned while
bringing T5ynth's frozen inference backend to a working state on Fedora 42 and
an NVIDIA Blackwell GPU. It is intentionally more explicit than the user-facing
Linux docs because the point is not just "how to build T5ynth", but what
actually broke, what proved to be a false assumption, and which fixes were
empirically sound.

The audience is contributors working on:

- `backend/pipe_inference.py`
- `backend/pipe_inference.spec`
- `backend/runtime_hook.py`
- `installer/linux/*`
- other projects that freeze PyTorch/Transformers-style backends into a local
  app bundle

## 1. Executive summary

The important architectural conclusion is:

- the **target host must not build Python/Torch**
- the **target host must not depend on a global PyTorch install**
- the **backend bundle must carry its own isolated ML runtime**
- the **installer must validate GPU/bundle compatibility and fail closed**

For T5ynth that means a three-layer system:

1. **Host runtime**
2. **JUCE app**
3. **isolated frozen backend bundle**

The main Linux bring-up problems did **not** end up being "Fedora is weird" or
"NVIDIA drivers are missing". The recurring failures were mostly in these
classes:

- wrong mental model: developer setup vs. installer path
- over-pruned PyInstaller bundle
- frozen `transformers` lazy exports not resolving where
  `stable_audio_tools` expects them
- missing local auxiliary Hugging Face assets for `t5-base`
- Blackwell host GPU running against an older bundled Torch/CUDA runtime

The RPM/installer architecture is broadly correct. The remaining packaging
problem at the time of writing is the **RPM finalization / digest** issue, not
the runtime model-loading logic.

## 2. Actual runtime dataflow

### 2.1 Launcher path

The JUCE side launches the backend through
[`src/inference/PipeInference.cpp`](../src/inference/PipeInference.cpp).

Important facts:

- `findBundledBinary()` first looks for
  `backend/dist/pipe_inference/pipe_inference`
- for installed Linux/Windows layouts it then looks for a sibling binary at
  `backend/pipe_inference`
- if no compatible frozen backend is found, it falls back to `python3` plus
  `pipe_inference.py`

That means the installed Linux layout is expected to look like:

```text
/opt/T5ynth/T5ynth
/opt/T5ynth/backend/pipe_inference
/opt/T5ynth/backend/...
```

This matters because many early errors were caused by reasoning as if the
target host were still allowed to build or assemble Python at install time. It
is not.

### 2.2 Logging path

`PipeInference.cpp` redirects backend stderr to:

- Linux: `~/.config/T5ynth/Logs/backend_stderr.log`
- historical GUI strings still mention the macOS path in some places

When diagnosing frozen backend failures, this log is the first place to look.

### 2.3 Backend startup path

The frozen executable runs [`backend/pipe_inference.py`](../backend/pipe_inference.py).
At startup it immediately touches:

- `numpy`
- `torch`
- `torchsde`
- logging / protocol setup

The device list is derived by `available_devices()`, which now also enforces
the Blackwell/CUDA compatibility guard.

Only after startup does the backend:

1. scan available models
2. choose a startup model
3. load the selected pipeline
4. emit the `Ready` packet to JUCE

So if the backend dies before `Ready`, the problem is usually in:

- frozen import graph
- runtime libraries
- auxiliary model asset resolution
- device compatibility guard

not in the JUCE app itself.

## 3. Dependency layers: what belongs where

### 3.1 Host runtime

The host must provide:

- NVIDIA driver, if CUDA is intended
- Linux GUI/runtime libraries such as `gtk3`, `webkit2gtk4.1`, `alsa-lib`,
  `libcurl`, `fontconfig`, `freetype`, `libXcomposite`

The host must **not** be expected to provide:

- global `torch`
- global `transformers`
- global `stable-audio-tools`
- global Python packages for the backend

### 3.2 Bundled backend runtime

The backend bundle under `/opt/T5ynth/backend/` must carry:

- the frozen `pipe_inference`
- the bundled Python runtime
- `torch`
- `transformers`
- `diffusers`
- `stable_audio_tools`
- the CUDA userspace pieces required by the chosen Torch wheel

This is the crucial distinction:

- **host driver** stays on the system
- **Torch CUDA userspace runtime** lives inside the bundle

### 3.3 Build host

The build host is where a repo-local `.venv` may exist and where `pip install`
is acceptable.

For T5ynth on Fedora:

- `tools/setup_fedora42.sh` is a **developer/build-host-only** script
- it is not an installer
- it is allowed to build a repo-local `.venv`
- it is not allowed to mutate the target system's global Python/ML stack

## 4. Failure classes and what they actually meant

### 4.1 False assumption: "installer path" can be tested via developer setup

This was wrong.

Running `tools/setup_fedora42.sh` proves only that a **build host** can prepare
its local environment. It does **not** prove that an end-user installation path
is valid.

The correct installer model is:

- build host/CI creates the backend bundle
- staging script adds metadata
- installer/RPM installs that bundle
- target host never does `pip install torch`

### 4.2 Frozen startup crash around Torch distributed/RPC

Several earlier experiments drifted into `torch.distributed` / RPC problems.
The key lesson was:

- semantic non-usage does **not** automatically mean a module can be excluded
  from a frozen Torch bundle

Over-aggressive exclusion of `torch.distributed` and adjacent modules caused
new failures. The eventual stable direction was:

- stop fighting the import graph blindly
- keep the bundle conservative
- remove only clearly non-runtime tooling/data

### 4.3 `runtime_hook.py` preimport of `torchsde`

Preimporting `torchsde` from the PyInstaller runtime hook turned out to be a
bad idea. It moved Torch-dependent imports too early into the frozen startup
sequence and contributed to fragile startup behavior.

The current fix is simple:

- `runtime_hook.py` preimports only `safetensors`
- `torchsde` is patched inside `pipe_inference.py`, not forced in the runtime
  hook

This is more robust and should be considered the default pattern for similar
projects: keep runtime hooks minimal.

### 4.4 Frozen `transformers` package root missing expected names

`stable_audio_tools` expects several names directly on the package root:

- `transformers.GenerationMixin`
- `transformers.AutoTokenizer`
- `transformers.T5EncoderModel`

In the frozen bundle these lazy exports were present in the package, but not
always resolvable where `stable_audio_tools` expected them.

The durable fix was:

- explicitly hidden-import the relevant `transformers` generation and auto/T5
  modules in [`backend/pipe_inference.spec`](../backend/pipe_inference.spec)
- explicitly import and assign those names in `_load_native_pipeline()`

This is a general PyInstaller lesson:

- "module exists somewhere in site-packages" is not enough
- if an upstream package relies on lazy package-root exports, freeze the
  concrete implementation modules and, if necessary, bind them explicitly

### 4.5 Missing local Hugging Face assets for `t5-base`

`stable-audio-open-small` is not self-sufficient with only:

- `model.safetensors`
- `model_config.json`

For the **native** `stable_audio_tools` path, the backend also needs local
Transformer assets for the T5 text encoder.

The fix was:

- extend model search to Linux's current app data path:
  `~/.config/share/T5ynth/models`
- add local Hugging Face asset resolution helpers in `pipe_inference.py`
- rewrite the native model config at load time so `t5_model_name` points to a
  local self-contained T5 directory
- patch `stable_audio_tools.models.conditioners.T5Conditioner` so the resolved
  local path is accepted

The local T5 directory must look like a real self-contained HF model, i.e.
contain at least:

- `config.json`
- tokenizer files
- weights such as `model.safetensors`

### 4.6 Over-pruning CUDA libraries

Earlier bundle reduction attempts removed too much of the CUDA userspace set.
This manifested as runtime failures such as missing:

- `libcupti.so.12`
- `libcufft.so.11`

The correction was:

- stop trying to infer "training-only" vs. "runtime" too aggressively
- keep the CUDA userspace set conservative
- only strip obviously non-runtime helper binaries/data

For PyTorch packaging in general:

- it is safer to start from a working conservative bundle and then measure
  reduction opportunities
- it is unsafe to assume that libraries like CUPTI or NVTX are irrelevant just
  because your own code does not call them directly

### 4.7 Blackwell on `cu124`: `no kernel image is available for execution on the device`

This was the final runtime failure on the actual host.

Observed host:

- NVIDIA RTX PRO 6000 Blackwell Workstation Edition

Observed old bundle:

- `torch 2.6.0+cu124`

Meaning:

- the bundle was old enough that the host GPU could be detected
- but too old to contain the necessary GPU kernels/runtime compatibility for
  that Blackwell device

The result was not a startup failure but a runtime CUDA launch failure:

- `CUDA error: no kernel image is available for execution on the device`

The fix was **not** "do more runtime hacks".
The fix was:

- build the backend on a Blackwell-compatible Torch/CUDA line
- stage it under an explicit Blackwell bundle id
- reject generic/old bundles on Blackwell hosts

## 5. Known-good and known-bad Torch lines

### 5.1 Known-bad for Blackwell

For this host class, the old generic bundle based on:

- `torch 2.6.0+cu124`

must be treated as incompatible.

### 5.2 Known-good local reference for this work

The currently adopted Blackwell build-host stack in this repo is:

- `torch 2.10.0+cu128`
- `torchaudio 2.10.0+cu128`
- `torchvision 0.25.0+cu128`

This lives in:

- [`backend/requirements-torch-blackwell.txt`](../backend/requirements-torch-blackwell.txt)

Why not the original local AI4ArtsEd `cu130` nightly pin?

- it existed as a historical local reference
- but the exact pinned nightly date was no longer reproducible from the current
  index
- the `cu128` line is both available now and sufficient for Blackwell support
  in this project's tested path

That is an important operational lesson:

- historical working nightlies are not automatically reproducible later
- repo-level build requirements should prefer currently available, reproducible
  versions over dead historical pins

## 6. Guardrails now in place

### 6.1 Build-host guardrail

[`tools/setup_fedora42.sh`](../tools/setup_fedora42.sh) now:

- supports `--cuda-blackwell`
- refuses `--cuda` on a detected Blackwell host

So the build host no longer silently rebuilds a Blackwell-targeted backend on
`cu124`.

### 6.2 Bundle metadata

[`installer/linux/stage_backend_bundle.sh`](../installer/linux/stage_backend_bundle.sh)
now writes:

- `BUNDLE_GPU_FAMILY`
- `BUNDLE_TORCH_VERSION`
- `BUNDLE_CUDA_VERSION`

into `bundle.env`.

For Blackwell bundles it also enforces:

- explicit `blackwell` in the bundle id
- bundled CUDA runtime `>= 12.8`

### 6.3 Installed preflight guardrail

[`installer/linux/t5ynth-preflight.sh`](../installer/linux/t5ynth-preflight.sh)
now:

- reads `bundle.env`
- inspects the host GPU name via `nvidia-smi`
- fails closed on Blackwell if the bundle is generic/unknown or too old

This prevents users from landing in a misleading "everything installed" state
with a doomed `cu124` backend.

### 6.4 Backend runtime guardrail

[`backend/pipe_inference.py`](../backend/pipe_inference.py) now checks:

- if CUDA is available
- if the host has an `sm_120+` GPU
- if the bundled Torch runtime is at least CUDA 12.8

If not, backend startup fails with a direct, explanatory error instead of
letting inference continue to a later kernel-launch crash.

## 7. Reusable rules for other PyTorch-frozen apps

These are the general takeaways worth carrying into other projects.

### 7.1 Separate four concerns explicitly

Do not blur together:

1. developer setup
2. target installation
3. backend bundling
4. host GPU compatibility

Most of the wasted effort came from mixing these layers mentally.

### 7.2 Never rely on host-global PyTorch for an installer

For a packaged desktop app, global PyTorch is the wrong contract.

Reasons:

- version conflicts
- accidental reuse of unrelated Nightly stacks
- silent incompatibility on newer GPUs
- non-reproducible installs

The correct contract is:

- host provides driver + standard runtime libs
- app bundle provides its own ML runtime

### 7.3 Freeze package-root lazy exports deliberately

If an upstream package expects objects on the root package namespace, PyInstaller
may require:

- explicit hidden imports
- explicit early import/binding in your own code

Treat lazy-export packages like `transformers` as special cases.

### 7.4 Be conservative with CUDA pruning

Over-pruning usually wastes more time than a slightly larger first bundle.

Start with:

- a working bundle
- verified startup
- verified inference

Then reduce cautiously.

### 7.5 GPU compatibility must be a hard gate, not a warning

If a bundle is incompatible with a GPU class, do not:

- silently continue
- silently fall back
- wait for a later CUDA launch failure

Instead:

- detect the host GPU
- detect the bundle's Torch/CUDA runtime
- stop immediately if they do not match

## 8. What is still open

At the time of writing, the runtime/backend problem is solved enough that:

- the unfrozen backend starts
- the frozen backend starts
- the installed bundle can be manually updated and runs on Blackwell

The still-open packaging problem is:

- the locally rebuilt RPM reaches a late stage of `rpmbuild`
- but the produced RPM still ends up with bad digests / invalid finalization

So the state is:

- **runtime model-loading fix:** yes
- **Blackwell compatibility guard:** yes
- **installer architecture:** yes
- **fresh locally valid RPM on the new backend:** not yet

That open issue should be treated as a packaging/finalization bug, not as a
backend-bundling or Blackwell-runtime bug.

## 9. Files most relevant to this topic

- [`../src/inference/PipeInference.cpp`](../src/inference/PipeInference.cpp)
- [`../backend/pipe_inference.py`](../backend/pipe_inference.py)
- [`../backend/pipe_inference.spec`](../backend/pipe_inference.spec)
- [`../backend/runtime_hook.py`](../backend/runtime_hook.py)
- [`../backend/requirements-torch-blackwell.txt`](../backend/requirements-torch-blackwell.txt)
- [`../tools/setup_fedora42.sh`](../tools/setup_fedora42.sh)
- [`../installer/linux/stage_backend_bundle.sh`](../installer/linux/stage_backend_bundle.sh)
- [`../installer/linux/t5ynth-preflight.sh`](../installer/linux/t5ynth-preflight.sh)
- [`LINUX_INSTALLATION.md`](LINUX_INSTALLATION.md)
- [`LINUX_PACKAGING.md`](LINUX_PACKAGING.md)
