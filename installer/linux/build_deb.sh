#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: installer/linux/build_deb.sh [--version X.Y.Z] [--build-dir DIR] [--bundle-id ID] [--backend-bundle DIR] [--skip-build]

Build an Ubuntu/Debian .deb package for the T5ynth standalone app. The package installs:
  - /opt/T5ynth/T5ynth
  - /opt/T5ynth/backend/*
  - /usr/bin/t5ynth
  - /usr/bin/t5ynth-preflight
  - desktop entry + icon
  - license files

This Linux package path is standalone-only. VST3 distribution remains a
separate archive path for now.

Important: this script does not build Python/Torch on the target machine. It
expects a prebuilt backend bundle produced earlier on a build host or in CI.
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

version_from_cmake() {
    sed -n 's/^project(T5ynth VERSION \([0-9.]*\)).*/\1/p' CMakeLists.txt | head -n 1
}

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

version="$(version_from_cmake)"
build_dir="build_clean"
bundle_id="ubuntu24.04-x86_64-cuda"
backend_bundle_dir=""
skip_build=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --version)
            version="$2"
            shift
            ;;
        --build-dir)
            build_dir="$2"
            shift
            ;;
        --bundle-id)
            bundle_id="$2"
            shift
            ;;
        --backend-bundle)
            backend_bundle_dir="$2"
            shift
            ;;
        --skip-build)
            skip_build=1
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

require_cmd ar
require_cmd gzip
require_cmd md5sum
require_cmd tar
require_cmd xz

standalone_bin="$repo_root/$build_dir/T5ynth_artefacts/Release/Standalone/T5ynth"
if [[ -z "$backend_bundle_dir" ]]; then
    backend_bundle_arg="archives/linux-bundles/$bundle_id/backend"
else
    backend_bundle_arg="$backend_bundle_dir"
fi

if ! backend_bundle_dir="$(resolve_dir "$backend_bundle_arg")"; then
    echo "Backend bundle directory not found: $backend_bundle_arg" >&2
    exit 1
fi
bundle_root="$(cd "$backend_bundle_dir/.." && pwd)"
bundle_manifest="$bundle_root/bundle.env"
if [[ ! -f "$bundle_manifest" ]]; then
    echo "Bundle metadata file not found: $bundle_manifest" >&2
    echo "Use installer/linux/stage_backend_bundle.sh so the bundle is staged with release metadata." >&2
    exit 1
fi
backend_bin="$backend_bundle_dir/pipe_inference"

if [[ $skip_build -eq 0 ]]; then
    require_cmd cmake
    cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$build_dir" --config Release -j4
fi

if [[ ! -x "$standalone_bin" ]]; then
    echo "Standalone binary not found: $standalone_bin" >&2
    exit 1
fi
if [[ ! -x "$backend_bin" ]]; then
    echo "Prebuilt backend bundle not found: $backend_bin" >&2
    echo "Build backend/dist/pipe_inference on a build host first, stage it into a named bundle, or pass --backend-bundle DIR." >&2
    exit 1
fi

# shellcheck disable=SC1090
source "$bundle_manifest"
bundle_distro_family="${BUNDLE_DISTRO_FAMILY:-unknown}"
if [[ "$bundle_distro_family" != "ubuntu" && "$bundle_distro_family" != "debian" && "$bundle_distro_family" != "unknown" ]]; then
    echo "Warning: building a Debian package from bundle '$bundle_id' with distro family '$bundle_distro_family'." >&2
    echo "         For a real Ubuntu release build, stage an Ubuntu-family bundle instead." >&2
fi

deb_root="$repo_root/archives/deb"
staging_root="$deb_root/staging"
payload_tree="$staging_root/data"
control_tree="$staging_root/control"
package_name="t5ynth"
arch="amd64"
package_file="$deb_root/${package_name}_${version}_${arch}.deb"

rm -rf "$staging_root"
mkdir -p \
    "$payload_tree/opt/T5ynth/backend" \
    "$payload_tree/usr/bin" \
    "$payload_tree/usr/share/applications" \
    "$payload_tree/usr/share/icons/hicolor/1024x1024/apps" \
    "$payload_tree/usr/share/T5ynth/presets" \
    "$payload_tree/usr/share/licenses/t5ynth" \
    "$control_tree" \
    "$deb_root"

install -m 0755 "$standalone_bin" "$payload_tree/opt/T5ynth/T5ynth"
cp -a "$backend_bundle_dir/." "$payload_tree/opt/T5ynth/backend/"
chmod -R a+rX "$payload_tree/opt/T5ynth/backend"
install -m 0644 "$bundle_manifest" "$payload_tree/opt/T5ynth/backend/bundle.env"

install -m 0644 LICENSE.txt "$payload_tree/usr/share/licenses/t5ynth/LICENSE.txt"
install -m 0644 THIRD_PARTY_LICENSES.txt "$payload_tree/usr/share/licenses/t5ynth/THIRD_PARTY_LICENSES.txt"
install -m 0644 resources/presets/*.t5p "$payload_tree/usr/share/T5ynth/presets/"
install -m 0644 resources/logos/t5ynth_icon.png \
    "$payload_tree/usr/share/icons/hicolor/1024x1024/apps/t5ynth.png"
install -m 0755 installer/linux/t5ynth-wrapper.sh "$payload_tree/usr/bin/t5ynth"
install -m 0755 installer/linux/t5ynth-preflight.sh "$payload_tree/usr/bin/t5ynth-preflight"
sed 's|@PREFIX@|/opt/T5ynth|g' installer/linux/t5ynth.desktop.in \
    > "$payload_tree/usr/share/applications/t5ynth.desktop"

installed_size_kb="$(du -sk "$payload_tree" | awk '{print $1}')"

cat > "$control_tree/control" <<EOF
Package: $package_name
Version: $version
Section: sound
Priority: optional
Architecture: $arch
Maintainer: T5ynth Maintainers <noreply@github.com>
Depends: bash, libc6, libgcc-s1, libstdc++6, libgtk-3-0t64 | libgtk-3-0, libwebkit2gtk-4.1-0, libasound2t64 | libasound2, libcurl4t64 | libcurl4, libfontconfig1, libfreetype6, libx11-6, libxrandr2, libxinerama1, libxcursor1, libxcomposite1, libxext6, libxrender1
Installed-Size: $installed_size_kb
Homepage: https://github.com/joeriben/t5ynth
Description: Text-to-sound synthesizer standalone app
 T5ynth is a JUCE-based text-to-sound synthesizer. This Debian package installs
 the standalone app and its isolated bundled Python inference backend under
 /opt/T5ynth.
 .
 The backend bundle comes from a release-built bundle set selected by bundle-id.
 Model weights are not bundled and must be installed separately after launch.
EOF

(
    cd "$payload_tree"
    find . -type f -printf '%P\0' \
        | sort -z \
        | xargs -0 md5sum > "$control_tree/md5sums"
)

printf '2.0\n' > "$staging_root/debian-binary"
tar \
    --owner=0 --group=0 --numeric-owner \
    -czf "$staging_root/control.tar.gz" \
    -C "$control_tree" .
tar \
    --owner=0 --group=0 --numeric-owner \
    -cJf "$staging_root/data.tar.xz" \
    -C "$payload_tree" .

rm -f "$package_file"
(
    cd "$staging_root"
    ar rcs "$package_file" debian-binary control.tar.gz data.tar.xz
)

echo
echo "DEB build complete."
printf '%s\n' "$package_file"
