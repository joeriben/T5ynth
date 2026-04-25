#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: installer/linux/stage_backend_bundle.sh --bundle-id ID [--source DIR] [--gpu-family FAMILY] [--distro-family FAMILY]

Stage a prebuilt Linux backend bundle into the release/packaging layout used by
installer/linux/build_rpm.sh and installer/linux/build_deb.sh.

This script does not build Python/Torch. It copies an already-built
backend/dist/pipe_inference tree into:

  archives/linux-bundles/<bundle-id>/

and writes bundle metadata to bundle.env so the packaging step can select the
correct release-built bundle by ID.
EOF
}

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "Required command not found: $1" >&2
        exit 1
    fi
}

resolve_dir() {
    local path="$1"
    if [[ "$path" != /* ]]; then
        path="$repo_root/$path"
    fi
    if [[ ! -d "$path" ]]; then
        return 1
    fi
    (
        cd "$path" >/dev/null 2>&1 && pwd
    )
}

version_ge() {
    local left="$1"
    local right="$2"
    if [[ -z "$left" || -z "$right" ]]; then
        return 1
    fi
    [[ "$(printf '%s\n%s\n' "$right" "$left" | sort -V | tail -n 1)" == "$left" ]]
}

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

find_patchelf() {
    if [[ -x "$repo_root/.venv/bin/patchelf" ]]; then
        printf '%s\n' "$repo_root/.venv/bin/patchelf"
        return 0
    fi
    command -v patchelf 2>/dev/null || return 1
}

sanitize_rpath() {
    local file="$1"
    local patchelf_bin="$2"
    local current_rpath=""
    local new_rpath=""
    local rel_path=""

    current_rpath="$("$patchelf_bin" --print-rpath "$file" 2>/dev/null || true)"
    if [[ -z "$current_rpath" ]]; then
        return 0
    fi

    case "$file" in
        */_internal/torchvision/_C.so)
            new_rpath='$ORIGIN:$ORIGIN/../torch/lib:$ORIGIN/../torchvision.libs'
            ;;
        */_internal/scipy/special/cython_special.cpython-311-*.so)
            new_rpath='$ORIGIN:$ORIGIN/../../scipy.libs'
            ;;
        */_internal/nvidia/nvshmem/lib/libnvshmem_host.so.3)
            new_rpath='$ORIGIN'
            ;;
        *)
            local IFS=':'
            local entry=""
            local -a kept=()
            for entry in $current_rpath; do
                entry="${entry//'${ORIGIN}'/'$ORIGIN'}"
                case "$entry" in
                    /__w/*|/opt/_internal/*)
                        continue
                        ;;
                    '$ORIGIN'*)
                        kept+=("$entry")
                        ;;
                esac
            done

            if [[ ${#kept[@]} -eq 0 ]]; then
                "$patchelf_bin" --remove-rpath "$file"
                return 0
            fi

            new_rpath="${kept[0]}"
            for entry in "${kept[@]:1}"; do
                if [[ ":$new_rpath:" != *":$entry:"* ]]; then
                    new_rpath="${new_rpath}:$entry"
                fi
            done
            ;;
    esac

    "$patchelf_bin" --set-rpath "$new_rpath" "$file"
}

sanitize_bundle_rpaths() {
    local bundle_backend_dir="$1"
    local patchelf_bin=""
    local file=""
    local nvshmem_host=""

    if ! patchelf_bin="$(find_patchelf)"; then
        echo "patchelf is required to sanitize bundled ELF RPATHs before RPM packaging." >&2
        exit 1
    fi

    while IFS= read -r -d '' file; do
        sanitize_rpath "$file" "$patchelf_bin"
    done < <(find "$bundle_backend_dir" -type f \( -name '*.so' -o -name 'pipe_inference' \) -print0)

    nvshmem_host="$bundle_backend_dir/_internal/nvidia/nvshmem/lib/libnvshmem_host.so.3"
    if [[ -f "$nvshmem_host" ]]; then
        "$patchelf_bin" --set-rpath '$ORIGIN' "$nvshmem_host"
    fi
}

detect_bundle_torch_version() {
    local bundle_backend_dir="$1"
    local version_file="$bundle_backend_dir/_internal/torch/version.py"
    if [[ ! -f "$version_file" ]]; then
        return 1
    fi
    sed -n "s/^__version__ = ['\"]\\([^'\"]*\\)['\"]/\\1/p" "$version_file" | head -n 1
}

detect_bundle_cuda_version() {
    local bundle_backend_dir="$1"
    local version_file="$bundle_backend_dir/_internal/torch/version.py"
    if [[ ! -f "$version_file" ]]; then
        return 1
    fi
    sed -n "s/^cuda[^=]*= ['\"]\\([^'\"]*\\)['\"]/\\1/p" "$version_file" | head -n 1
}

infer_gpu_family_from_bundle_id() {
    local id="$1"
    case "$id" in
        *blackwell*)
            printf '%s\n' "blackwell"
            ;;
        *cpu*)
            printf '%s\n' "cpu"
            ;;
        *)
            printf '%s\n' "generic"
            ;;
    esac
}

infer_distro_family_from_bundle_id() {
    local id="$1"
    case "$id" in
        ubuntu*|*ubuntu*)
            printf '%s\n' "ubuntu"
            ;;
        debian*|*debian*)
            printf '%s\n' "debian"
            ;;
        fedora*|*fedora*)
            printf '%s\n' "fedora"
            ;;
        *)
            printf '%s\n' "linux"
            ;;
    esac
}

bundle_id=""
source_dir="backend/dist/pipe_inference"
distro_family=""
gpu_family=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bundle-id)
            bundle_id="$2"
            shift
            ;;
        --source)
            source_dir="$2"
            shift
            ;;
        --gpu-family)
            gpu_family="$2"
            shift
            ;;
        --distro-family)
            distro_family="$2"
            shift
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

if [[ -z "$bundle_id" ]]; then
    echo "--bundle-id is required" >&2
    usage >&2
    exit 1
fi

if ! source_dir="$(resolve_dir "$source_dir")"; then
    echo "Source backend bundle directory not found: $source_dir" >&2
    exit 1
fi

require_cmd cp
require_cmd find

backend_bin="$source_dir/pipe_inference"
if [[ ! -x "$backend_bin" ]]; then
    echo "Expected prebuilt backend binary at: $backend_bin" >&2
    exit 1
fi

bundle_root="$repo_root/archives/linux-bundles/$bundle_id"
backend_dest="$bundle_root/backend"
bundle_torch_version=""
bundle_cuda_version=""

rm -rf "$bundle_root"
mkdir -p "$backend_dest"
cp -a "$source_dir/." "$backend_dest/"
sanitize_bundle_rpaths "$backend_dest"

bundle_torch_version="$(detect_bundle_torch_version "$backend_dest" || true)"
bundle_cuda_version="$(detect_bundle_cuda_version "$backend_dest" || true)"

if [[ -z "$gpu_family" ]]; then
    gpu_family="$(infer_gpu_family_from_bundle_id "$bundle_id")"
fi
if [[ -z "$distro_family" ]]; then
    distro_family="$(infer_distro_family_from_bundle_id "$bundle_id")"
fi

if [[ "$gpu_family" == "blackwell" ]]; then
    if [[ "$bundle_id" != *blackwell* ]]; then
        echo "Blackwell bundle ids must say 'blackwell' explicitly: $bundle_id" >&2
        exit 1
    fi
    if ! version_ge "$bundle_cuda_version" "12.8"; then
        echo "Blackwell bundle '$bundle_id' requires torch/CUDA 12.8+ inside the staged backend, found: ${bundle_cuda_version:-unknown}" >&2
        exit 1
    fi
fi

cat > "$bundle_root/bundle.env" <<EOF
BUNDLE_ID=$bundle_id
BUNDLE_PLATFORM=linux
BUNDLE_DISTRO_FAMILY=$distro_family
BUNDLE_ARCH=$(uname -m)
BUNDLE_GPU_FAMILY=$gpu_family
BUNDLE_TORCH_VERSION=${bundle_torch_version:-unknown}
BUNDLE_CUDA_VERSION=${bundle_cuda_version:-unknown}
BUNDLE_BACKEND_EXECUTABLE=pipe_inference
EOF

echo
echo "Staged backend bundle:"
echo "  $bundle_root"
