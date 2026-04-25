# T5ynth on Linux

This document is the Linux **developer / build-host** path for T5ynth. It is
for people who need to build the standalone app and the isolated backend bundle
from source.

It is **not** the production installer path. End-user / production packaging
belongs in [`LINUX_PACKAGING.md`](LINUX_PACKAGING.md).

The reference distro in this document is Fedora 42 Workstation on x86_64. The
same source build also works on Ubuntu 24.04 with the package set documented in
[`DEV_BUILD.md`](DEV_BUILD.md).

## 1. Fedora 42 dependencies

Install the C++ toolchain, JUCE/Linux GUI dependencies, and Python headers:

```bash
sudo dnf install -y \
  cmake gcc-c++ make git pkgconf-pkg-config \
  python3.11 python3.11-devel python3-pip \
  alsa-lib-devel libcurl-devel freetype-devel \
  libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel \
  libXcomposite-devel libXext-devel libXrender-devel fontconfig-devel \
  webkit2gtk4.1-devel gtk3-devel
```

If you prefer a scripted setup, the repo now includes
[`tools/setup_fedora42.sh`](../tools/setup_fedora42.sh).
That script is build-host-only: it installs Fedora development packages and
creates a repo-local `.venv`. Do not treat it as the Linux installer.

Before you start, check the package names on the target machine:

```bash
dnf search webkit2gtk
```

T5ynth's JUCE build currently expects the WebKitGTK 4.1 ABI. On Fedora 42 that
usually means `webkit2gtk4.1-devel`, but verify the exact package name on the
host before assuming the docs are current.

## 2. Python backend setup

Create and activate the venv. `--clear` matters here: if `.venv/` already
contains a broken or cross-version environment, Python recreates it in place
instead of mixing old 3.10/3.13 state into the new one.

```bash
python3.11 -m venv .venv --clear
source .venv/bin/activate
python --version
python -m pip install --upgrade pip setuptools wheel
python -m pip install pyinstaller
```

After activation, `python --version` should print `Python 3.11.x`. If it does
not, stop there and fix the interpreter selection first.

Install PyTorch before the generic requirements so the correct wheel wins.

NVIDIA / CUDA 12.4:

```bash
python -m pip install torch --index-url https://download.pytorch.org/whl/cu124
```

NVIDIA Blackwell:

```bash
python -m pip install -r backend/requirements-torch-blackwell.txt
```

That file pins the tested torch triplet used for Blackwell-class GPUs.
Keep those three packages on the same CUDA 12.8 line.

CPU-only fallback:

```bash
python -m pip install torch
```

Then install the remaining backend dependencies:

```bash
python -m pip install -r backend/requirements.txt
```

## 3. Backend smoke test

Before building the app, make sure the Python backend starts cleanly:

```bash
cd backend
python pipe_inference.py
```

It should print startup messages and then wait for stdin. Exit with `Ctrl+D`.

## 4. Bundle the backend

Still inside the activated venv:

```bash
cd backend
pyinstaller pipe_inference.spec --noconfirm
cd ..
```

Expected output: `backend/dist/pipe_inference/pipe_inference`

This one-folder PyInstaller output is the isolated backend bundle later
consumed by the Linux package-layer scripts.

On the packaging/release host, the next step is to stage it into a named
bundle slot with:

```bash
installer/linux/stage_backend_bundle.sh --bundle-id fedora42-x86_64-cuda
installer/linux/stage_backend_bundle.sh --bundle-id fedora42-x86_64-cuda-blackwell
```

## 5. Build T5ynth

```bash
cmake -S . -B build_clean -DCMAKE_BUILD_TYPE=Release
cmake --build build_clean --config Release -j4
```

Start with `-j4`, not `-j$(nproc)`, on a first Fedora run. Parallel C++ builds
plus a large `torch` environment can push an 8-16 GB machine into swap or OOM.
Raise the job count only after you know the host stays stable.

The standalone binary ends up at:

```text
build_clean/T5ynth_artefacts/Release/Standalone/T5ynth
```

The VST3 bundle ends up at:

```text
build_clean/T5ynth_artefacts/Release/VST3/T5ynth.vst3
```

## 6. Assemble the runnable Linux layout

The standalone app expects the bundled backend in a sibling `backend/`
directory, matching CI:

```bash
mkdir -p dist/T5ynth/backend
cp build_clean/T5ynth_artefacts/Release/Standalone/T5ynth dist/T5ynth/
cp -R backend/dist/pipe_inference/* dist/T5ynth/backend/
```

Run it with:

```bash
./dist/T5ynth/T5ynth
```

For the Linux package-layer path, this same backend bundle is first staged into
a named release bundle and then wrapped into the RPM or `.deb` described in
[`LINUX_PACKAGING.md`](LINUX_PACKAGING.md).

## 7. Model installation on Linux

T5ynth does not ship model weights. The Linux model directory is:

```text
~/.local/share/T5ynth/models/<model-id>/
```

For `stable-audio-open-small`, the user downloads the files manually from
HuggingFace. T5ynth only scans or copies them into the model directory. The
required files are:

```text
model.safetensors
model_config.json
```

Use `Auto-Scan` or `Browse...` in the Settings panel after downloading them.
Do not fetch `model.ckpt` or `base_model.*` files for that model; T5ynth
ignores them.

## 8. Troubleshooting

`gtk/gtk.h: No such file or directory`

Install both `gtk3-devel` and `webkit2gtk4.1-devel`. JUCE's Linux web view
dependencies require both. If Fedora does not know `webkit2gtk4.1-devel`, run
`dnf search webkit2gtk` and install the matching 4.1 development package.

The machine hard-freezes, the terminal vanishes, or the build dies without a
useful Python traceback

Treat that as a likely memory-pressure problem first, not as an application
error. Check:

```bash
free -h
swapon --show
dmesg | rg -i 'killed process|out of memory|oom'
```

If you see OOM killer messages, reduce parallelism (`cmake --build ... -j2` or
`-j4`) and make sure swap is enabled before retrying.

`Bundled backend has incompatible binary format for this platform`

You copied a backend binary built on another OS. Rebuild the backend locally on
Linux with `pyinstaller pipe_inference.spec --noconfirm`.

The terminal dies or fills with Python import errors

That usually means the command is running outside the activated venv, the venv
contains leftovers from another Python version, or `torch` was installed from
the wrong index. Recreate the venv with `python3.11 -m venv .venv --clear`,
activate it again, run `python --version`, then reinstall `torch` before
`backend/requirements.txt`.

`CUDA error: no kernel image is available for execution on the device`

Treat that as a build/runtime mismatch first. On Blackwell GPUs, a backend
bundle built with `torch ... +cu124` is too old and must not be packaged.
Rebuild the repo-local venv with:

```bash
bash tools/setup_fedora42.sh --cuda-blackwell
```

then rebuild and restage the backend bundle with an explicit Blackwell bundle
id such as `fedora42-x86_64-cuda-blackwell`.
