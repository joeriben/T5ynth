# CUDA Bundle Targets

This is the short operational companion to
[`PYTORCH_BUNDLING_NOTES.md`](PYTORCH_BUNDLING_NOTES.md).

Its purpose is practical:

- which CUDA bundle classes should T5ynth support next
- how those classes map to typical NVIDIA GPUs
- what the Windows installer should do with them

This document intentionally stays shorter and more prescriptive than the longer
bundling notes.

## 1. Current recommendation

For the next packaging phase, treat T5ynth as needing **at least two** NVIDIA
bundle classes:

1. `cuda-generic`
2. `cuda-blackwell`

Do **not** treat "NVIDIA/CUDA" as a single installer class anymore.

## 2. Target classes

### 2.1 `cuda-generic`

This is the class for the GPUs most users are more likely to have in the near
term, such as:

- GeForce RTX 3080 / 3090
- other Ampere consumer cards
- likely Ada cards that do not require a Blackwell-specific runtime class

Why this class exists:

- NVIDIA's Ampere compatibility documentation treats Ampere as its own
  compatibility target.
- CUDA 11.1 release notes explicitly added support for Ampere GA10x GPUs
  (compute capability 8.6), including the GeForce RTX 30 series.
- PyTorch's official previous-version installation page shows Linux **and**
  Windows wheels on `cu124` for the mainstream stable 2.6.0 line.

Practical consequence:

- a generic bundle line based on `cu124` remains a reasonable first target for
  Ampere/Ada-style Windows and Linux machines
- this is the bundle class we should validate next on hardware like an RTX 3080

Important nuance:

- this is a **targeting recommendation**, not proof that every Ada card has
  already been empirically validated in T5ynth
- the correct next move is still to test the generic bundle on a real Ampere
  host

### 2.2 `cuda-blackwell`

This is the class for Blackwell GPUs, such as:

- RTX PRO 6000 Blackwell Workstation Edition
- other Blackwell workstation/consumer cards in the CC 12.0 family

Why this class exists:

- NVIDIA's compute capability table lists Blackwell workstation/consumer GPUs
  at compute capability 12.0
- NVIDIA states that CUDA 12.8 is the first toolkit version with Blackwell
  support across the toolkit
- official PyTorch `cu128` wheels exist for both Linux and Windows

Practical consequence:

- Blackwell must stay a distinct bundle class
- the bundle must use `cu128` or newer
- generic `cu124` bundles must be rejected on Blackwell hosts

## 3. Proposed bundle ids

For clarity and predictable installer behavior, use explicit ids like:

- `fedora42-x86_64-cuda-generic`
- `fedora42-x86_64-cuda-blackwell`
- `windows-x86_64-cuda-generic`
- `windows-x86_64-cuda-blackwell`
- optionally `*-cpu`

Avoid vague ids such as:

- `*-cuda`

Those ids make it too easy for an installer to put the wrong bundle on the
wrong GPU class.

## 4. What the installer should do

### 4.1 Linux

Linux should continue on the current architecture:

- prebuilt backend bundle
- bundle metadata in `bundle.env`
- host preflight checks
- fail closed on mismatch

The missing piece is not architecture but coverage:

- validate `cuda-generic`
- repair RPM finalization

### 4.2 Windows

Windows should use the **same bundle-selection model**, not a different one.

That means:

- do not let the Windows installer build Python/Torch locally
- do not rely on a global PyTorch install
- ship a prebuilt backend bundle
- select the bundle by GPU class
- fail closed if the selected bundle and host GPU class do not match

In other words:

- Linux and Windows should differ in installer mechanics
- they should **not** differ in backend-runtime contract

## 5. Windows installer rules

The Windows installer should follow these rules from day one:

1. Detect whether the host is NVIDIA-capable at all.
2. Detect whether the GPU belongs to the generic class or the Blackwell class.
3. Install only the matching prebuilt backend bundle.
4. Refuse a Blackwell install if only a generic bundle is present.
5. Never install or change global Python/Torch for the user.
6. Never silently fall back from an intended CUDA install to CPU.

The Windows equivalent of Linux `bundle.env` should carry at least:

- bundle id
- GPU family
- bundled torch version
- bundled CUDA version

## 6. Immediate next hardware targets

Based on both user expectations and technical payoff, the next empirical target
order should be:

1. **RTX 3080 / Ampere generic host**
2. **Windows NVIDIA generic host**
3. **Windows Blackwell host**, if available

Why this order:

- RTX 3080-class hosts are common and should validate the `cuda-generic`
  assumptions quickly
- Windows installer work should build on the already-correct Linux bundle
  contract
- the Blackwell path is now a solved special class, but it should not become
  the only supported CUDA class

## 7. Sources

Primary sources used for this target matrix:

- PyTorch previous versions page:
  <https://docs.pytorch.org/get-started/previous-versions/>
- PyTorch `cu128` wheel indexes for Linux/Windows:
  <https://download.pytorch.org/whl/cu128/torchaudio/>
  <https://download.pytorch.org/whl/cu128/torchvision/>
- NVIDIA compute capability table:
  <https://developer.nvidia.com/cuda/gpus>
- NVIDIA Blackwell CUDA 12.8 announcement:
  <https://developer.nvidia.com/blog/cuda-toolkit-12-8-delivers-nvidia-blackwell-support>
- NVIDIA CUDA 11.1 release notes for Ampere GA10x / RTX 30 support:
  <https://docs.nvidia.com/cuda/archive/11.1.0/cuda-toolkit-release-notes/>
