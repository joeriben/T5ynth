# Linux Packaging

This document covers the Linux package-layer path for T5ynth.

The important distinction is:

- the Ubuntu GitHub Actions `linux` job builds **Linux base artefacts**
- this document describes **package-layer outputs** built on top of that base
  app/backend layout

Today, that package layer is Fedora RPM and Ubuntu/Debian `.deb`. Both consume
the same base artefact contract rather than inventing separate Linux build
paths.

Current scope:

- Fedora RPM for the **standalone app**
- Ubuntu/Debian `.deb` for the **standalone app**
- isolated bundled Python backend included under `/opt/T5ynth/backend/`
- desktop entry, icon, wrapper, and a lightweight preflight helper
- named backend bundle selection via `bundle-id`

The current Linux package paths are intentionally standalone-only. VST3 remains
a separate archive/distribution path for now.

## 1. Base layer vs package layer

Linux packaging is split into two layers:

1. **Base build layer**
   - builds the standalone binary plus isolated backend bundle
   - preserves the sibling layout `T5ynth + backend/`
   - currently produced by the Ubuntu GitHub Actions `linux` job as
     `T5ynth-Linux-Base-x86_64-*.tar.xz`
2. **Package layer**
   - wraps that app/backend layout for a target distribution
   - adds package metadata, wrappers, desktop integration, and runtime deps
   - currently implemented as Fedora RPM and Ubuntu/Debian `.deb`

That keeps Linux packaging coherent:

- one common Linux build contract
- multiple package formats derived from it
- no target machine ever rebuilding Python/Torch

## 2. Why RPM first

For T5ynth, Fedora RPMs are a better first Linux package target than AppImage:

- Fedora 42 is the immediate target environment.
- RPM can express runtime dependencies like `gtk3` and `webkit2gtk4.1`.
- No macOS-style notarization or Developer ID workflow is required.
- The app already has a natural install layout: binary plus sibling backend.

The critical packaging rule is:

- the target host does **not** build Python/Torch
- the target host does **not** need a project `.venv`
- the target host does **not** rely on any globally installed PyTorch
- a build host or CI produces the backend bundle once, and the package wraps it

In other words:

- the release/build pipeline builds backend bundles
- the Fedora RPM packager and Ubuntu/Debian `.deb` packager select a named
  release-built bundle
- the target installer only installs that selected bundle

## 3. Package layout

The current Linux package-layer outputs install:

```text
/opt/T5ynth/T5ynth
/opt/T5ynth/backend/*
/usr/bin/t5ynth
/usr/bin/t5ynth-preflight
/usr/share/T5ynth/presets/*.t5p
/usr/share/applications/t5ynth.desktop
/usr/share/icons/hicolor/1024x1024/apps/t5ynth.png
/usr/share/licenses/t5ynth/{LICENSE.txt,THIRD_PARTY_LICENSES.txt}
```

The wrapper at `/usr/bin/t5ynth` simply launches `/opt/T5ynth/T5ynth`.
The app keeps its expected sibling-backend layout, so no runtime path rewrite
is needed. Linux packages also install the factory `.t5p` presets under
`/usr/share/T5ynth/presets`, while the app mirrors bundled presets into the
user preset directory on first launch for easier browsing/editing.

## 4. What must exist before packaging

The Linux package-layer scripts expect two prebuilt artefacts:

- the standalone app binary:
  `build_clean/T5ynth_artefacts/Release/Standalone/T5ynth`
- the isolated backend bundle:
  `archives/linux-bundles/<bundle-id>/backend/pipe_inference`

That backend bundle is produced on a build host or in CI, not on the target
install machine. The build-host/source-build path is documented in
[`LINUX_INSTALLATION.md`](LINUX_INSTALLATION.md) and
[`DEV_BUILD.md`](DEV_BUILD.md).

## 5. Stage a named release bundle

The packaging host should stage the already-built backend into a named bundle
slot before building the package:

```bash
installer/linux/stage_backend_bundle.sh --bundle-id fedora42-x86_64-cuda
installer/linux/stage_backend_bundle.sh --bundle-id fedora42-x86_64-cuda-blackwell
installer/linux/stage_backend_bundle.sh --bundle-id ubuntu24.04-x86_64-cuda
installer/linux/stage_backend_bundle.sh --bundle-id ubuntu24.04-x86_64-cuda-blackwell
```

This copies `backend/dist/pipe_inference/` into:

```text
archives/linux-bundles/fedora42-x86_64-cuda/
```

and writes a `bundle.env` metadata file beside it. That metadata is then
installed into `/opt/T5ynth/backend/bundle.env` for runtime inspection.

Bundle ids must encode the runtime class when CUDA compatibility matters. For
Blackwell, do not stage or package a vague `...-cuda` bundle. Use an explicit
id such as `fedora42-x86_64-cuda-blackwell`, and stage it from a backend bundle
whose internal torch runtime reports CUDA 12.8 or newer.

## 6. Build the RPM

Prerequisites:

- `rpmbuild` available on the packaging host
- the prebuilt standalone binary
- the staged named backend bundle

Run:

```bash
installer/linux/build_rpm.sh
```

Outputs land under:

```text
archives/rpm/rpmbuild/RPMS/x86_64/
```

Useful flags:

```bash
installer/linux/build_rpm.sh --skip-build
installer/linux/build_rpm.sh --bundle-id fedora42-x86_64-cuda
installer/linux/build_rpm.sh --build-dir build_clean --bundle-id fedora42-x86_64-cuda --version 1.0.0
installer/linux/build_rpm.sh --build-dir build_clean --backend-bundle /path/to/pipe_inference --version 1.0.0
```

By default the packager selects:

```text
archives/linux-bundles/<bundle-id>/backend/
```

`--backend-bundle` is an override for ad-hoc packaging hosts. The preferred
release path is the named bundle slot plus `bundle.env`.

## 7. Build the Debian package

Prerequisites:

- `ar`, `tar`, `gzip`, `xz`, and `md5sum` available on the packaging host
- the prebuilt standalone binary
- the staged named backend bundle

Run:

```bash
installer/linux/build_deb.sh
```

Outputs land under:

```text
archives/deb/
```

Useful flags:

```bash
installer/linux/build_deb.sh --skip-build
installer/linux/build_deb.sh --bundle-id ubuntu24.04-x86_64-cuda
installer/linux/build_deb.sh --build-dir build_clean --bundle-id ubuntu24.04-x86_64-cuda --version 1.0.0
installer/linux/build_deb.sh --build-dir build_clean --backend-bundle /path/to/pipe_inference --version 1.0.0
```

As with the RPM path, the preferred release path is the named bundle slot plus
`bundle.env`.

## 8. Install and validate

Install on Fedora locally with:

```bash
sudo dnf install ./archives/rpm/rpmbuild/RPMS/x86_64/t5ynth-*.rpm
```

Install on Ubuntu/Debian locally with:

```bash
sudo apt install ./archives/deb/t5ynth_*_amd64.deb
```

Run the installed preflight:

```bash
t5ynth-preflight
```

Launch:

```bash
t5ynth
```

## 9. Runtime model

The RPM and `.deb` do **not** touch or depend on a global Python/Torch
installation. The bundled backend under `/opt/T5ynth/backend/` carries its own
isolated ML runtime. The host must still provide:

- a working NVIDIA driver if CUDA is expected
- standard Linux runtime libraries such as GTK, WebKit, ALSA, and libcurl

The package does **not** bundle the NVIDIA driver. CUDA usability is a
host-level concern and must be checked on the target machine.

Recommended host checks:

```bash
nvidia-smi
t5ynth-preflight
```

If `nvidia-smi` fails, do not silently treat that as “CPU is fine”. Fix the
driver stack first if the target install is meant to be a CUDA machine.

`t5ynth-preflight` also prints the installed `bundle-id` and torch/CUDA bundle
metadata from `/opt/T5ynth/backend/bundle.env`. On Blackwell hosts it fails
closed if the installed bundle is not explicitly Blackwell-class or if the
bundled torch runtime is older than CUDA 12.8.

## 10. Known limits of the current package-layer paths

- standalone only, no VST3 packaging yet
- Fedora RPM is not yet uploaded from GitHub Actions
- Ubuntu/Debian `.deb` is a CI artefact, but not yet a GitHub Release asset
- model weights are still installed separately after launch
