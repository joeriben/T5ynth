#!/usr/bin/env bash
set -euo pipefail

version_ge() {
    local left="$1"
    local right="$2"
    if [[ -z "$left" || -z "$right" ]]; then
        return 1
    fi
    [[ "$(printf '%s\n%s\n' "$right" "$left" | sort -V | tail -n 1)" == "$left" ]]
}

fatal=0
bundle_gpu_family="unknown"
bundle_torch_version="unknown"
bundle_cuda_version="unknown"
host_gpu_family="unknown"
gpu_report=""

echo "T5ynth Linux preflight"
echo

if [[ -x /opt/T5ynth/T5ynth ]]; then
    echo "[ok] standalone binary: /opt/T5ynth/T5ynth"
else
    echo "[!!] missing standalone binary: /opt/T5ynth/T5ynth"
fi

if [[ -x /opt/T5ynth/backend/pipe_inference ]]; then
    echo "[ok] bundled backend: /opt/T5ynth/backend/pipe_inference"
else
    echo "[!!] missing bundled backend: /opt/T5ynth/backend/pipe_inference"
fi

if [[ -f /opt/T5ynth/backend/bundle.env ]]; then
    echo "[ok] bundle metadata: /opt/T5ynth/backend/bundle.env"
    # shellcheck disable=SC1091
    source /opt/T5ynth/backend/bundle.env
    bundle_gpu_family="${BUNDLE_GPU_FAMILY:-unknown}"
    bundle_torch_version="${BUNDLE_TORCH_VERSION:-unknown}"
    bundle_cuda_version="${BUNDLE_CUDA_VERSION:-unknown}"
    echo "     bundle-id=${BUNDLE_ID:-unknown} arch=${BUNDLE_ARCH:-unknown} distro-family=${BUNDLE_DISTRO_FAMILY:-unknown}"
    echo "     gpu-family=${bundle_gpu_family} torch=${bundle_torch_version} cuda=${bundle_cuda_version}"
else
    echo "[warn] bundle metadata missing: /opt/T5ynth/backend/bundle.env"
fi

echo
if command -v nvidia-smi >/dev/null 2>&1; then
    echo "GPU:"
    gpu_report="$(nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv,noheader 2>/dev/null || true)"
    if [[ -n "$gpu_report" ]]; then
        printf '%s\n' "$gpu_report"
        if printf '%s\n' "$gpu_report" | grep -qi 'Blackwell'; then
            host_gpu_family="blackwell"
        else
            host_gpu_family="generic"
        fi
    fi
else
    echo "[warn] nvidia-smi not found. T5ynth may still run, but CUDA availability is not confirmed."
fi

if [[ "$host_gpu_family" == "blackwell" ]]; then
    echo
    if [[ "$bundle_gpu_family" == "cpu" ]]; then
        echo "[warn] Blackwell host detected, but the installed bundle is CPU-only."
    elif [[ "$bundle_gpu_family" != "blackwell" ]]; then
        echo "[!!] incompatible bundle: Blackwell host detected, but bundle GPU class is '${bundle_gpu_family}'."
        echo "     Install a Blackwell-specific backend bundle instead of a generic CUDA or unknown bundle."
        fatal=1
    elif ! version_ge "$bundle_cuda_version" "12.8"; then
        echo "[!!] incompatible bundle: Blackwell host detected, but bundle CUDA runtime is '${bundle_cuda_version}'."
        echo "     Blackwell requires a backend bundle built with CUDA 12.8+ (prefer the pinned Blackwell torch stack)."
        fatal=1
    fi
fi

echo
echo "Runtime library check:"
ldd /opt/T5ynth/T5ynth || true

if [[ $fatal -ne 0 ]]; then
    exit 1
fi
