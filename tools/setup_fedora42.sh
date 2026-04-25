#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: tools/setup_fedora42.sh [--cuda|--cuda-blackwell|--cpu] [--skip-system] [--skip-python]

Prepares a Fedora 42 build host for a local T5ynth source build.

This script is developer/build-host only. It is not the Linux installer path
for end users or production machines.

Options:
  --cuda         Install the generic CUDA 12.4 PyTorch wheel
  --cuda-blackwell
                 Install the pinned Blackwell PyTorch stack (CUDA 12.8)
  --cpu          Install the CPU-only PyTorch wheel
  --skip-system  Skip dnf package installation
  --skip-python  Skip venv and pip installation
  -h, --help     Show this help
EOF
}

torch_mode="cuda"
skip_system=0
skip_python=0

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Required command not found: $1" >&2
        exit 1
    fi
}

warn_low_memory() {
    local mem_kb swap_kb
    mem_kb="$(awk '/MemTotal/ {print $2}' /proc/meminfo 2>/dev/null || echo 0)"
    swap_kb="$(awk '/SwapTotal/ {print $2}' /proc/meminfo 2>/dev/null || echo 0)"

    if [[ "$mem_kb" -gt 0 && "$mem_kb" -lt 16777216 ]]; then
        echo "Warning: less than 16 GB RAM detected. Expect torch install and C++ build pressure." >&2
    fi
    if [[ "$swap_kb" -eq 0 ]]; then
        echo "Warning: no swap detected. If the terminal dies during pip or CMake, check OOM first." >&2
    fi
}

detect_host_gpu_family() {
    if ! command -v nvidia-smi >/dev/null 2>&1; then
        printf '%s\n' "unknown"
        return 0
    fi

    if nvidia-smi --query-gpu=name --format=csv,noheader 2>/dev/null | grep -qi 'Blackwell'; then
        printf '%s\n' "blackwell"
        return 0
    fi

    printf '%s\n' "generic"
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --cuda)
            torch_mode="cuda"
            ;;
        --cuda-blackwell)
            torch_mode="cuda-blackwell"
            ;;
        --cpu)
            torch_mode="cpu"
            ;;
        --skip-system)
            skip_system=1
            ;;
        --skip-python)
            skip_python=1
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
    shift
done

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

if [[ -r /etc/os-release ]]; then
    # shellcheck disable=SC1091
    source /etc/os-release
    if [[ "${ID:-}" != "fedora" || "${VERSION_ID:-}" != "42" ]]; then
        echo "Warning: this script is tuned for Fedora 42, current host is ${PRETTY_NAME:-unknown}." >&2
    fi
fi

require_cmd git
warn_low_memory
host_gpu_family="$(detect_host_gpu_family)"

if [[ $skip_system -eq 0 ]]; then
    require_cmd sudo
    require_cmd dnf
    echo "Fedora WebKit candidates:"
    dnf search webkit2gtk || true
    sudo dnf install -y \
      cmake gcc-c++ make git pkgconf-pkg-config \
      python3.11 python3.11-devel python3-pip \
      alsa-lib-devel libcurl-devel freetype-devel \
      libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel \
      libXcomposite-devel libXext-devel libXrender-devel fontconfig-devel \
      webkit2gtk4.1-devel gtk3-devel
fi

if [[ $skip_python -eq 0 ]]; then
    if ! command -v python3.11 >/dev/null 2>&1; then
        echo "python3.11 is required for this setup script. Install it first." >&2
        exit 1
    fi

    if [[ "$torch_mode" == "cuda" && "$host_gpu_family" == "blackwell" ]]; then
        echo "Detected a Blackwell GPU on this build host." >&2
        echo "Refusing the generic cu124 torch path. Use --cuda-blackwell so the repo-local venv gets the pinned Blackwell torch stack." >&2
        exit 1
    fi

    python3.11 -m venv .venv --clear
    # shellcheck disable=SC1091
    source .venv/bin/activate
    python --version

    python -m pip install --upgrade pip setuptools wheel
    python -m pip install pyinstaller

    if [[ "$torch_mode" == "cuda" ]]; then
        python -m pip install torch --index-url https://download.pytorch.org/whl/cu124
    elif [[ "$torch_mode" == "cuda-blackwell" ]]; then
        python -m pip install -r backend/requirements-torch-blackwell.txt
    else
        python -m pip install torch
    fi

    python -m pip install -r backend/requirements.txt
fi

cat <<'EOF'

Fedora 42 setup complete.

Next steps on the build host:
  source .venv/bin/activate
  cd backend && pyinstaller pipe_inference.spec --noconfirm && cd ..
  cmake -S . -B build_clean -DCMAKE_BUILD_TYPE=Release
  cmake --build build_clean --config Release -j4
  mkdir -p dist/T5ynth/backend
  cp build_clean/T5ynth_artefacts/Release/Standalone/T5ynth dist/T5ynth/
  cp -R backend/dist/pipe_inference/* dist/T5ynth/backend/
  ./dist/T5ynth/T5ynth

For a Fedora installer package, stop after creating backend/dist/pipe_inference
and use installer/linux/build_rpm.sh on a packaging host.

If the target bundle is for Blackwell GPUs, stage it with an explicit bundle id,
for example:
  installer/linux/stage_backend_bundle.sh --bundle-id fedora42-x86_64-cuda-blackwell
EOF
