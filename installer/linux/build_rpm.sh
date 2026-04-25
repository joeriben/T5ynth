#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: installer/linux/build_rpm.sh [--version X.Y.Z] [--build-dir DIR] [--bundle-id ID] [--backend-bundle DIR] [--skip-build]

Build a Fedora RPM for the T5ynth standalone app. The RPM installs:
  - /opt/T5ynth/T5ynth
  - /opt/T5ynth/backend/*
  - /usr/bin/t5ynth
  - /usr/bin/t5ynth-preflight
  - desktop entry + icon
  - license files

This first Linux package path is standalone-only. VST3 distribution remains a
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
bundle_id="fedora42-x86_64-cuda"
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

require_cmd rpmbuild
require_cmd tar

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

rpm_root="$repo_root/archives/rpm"
payload_root="$rpm_root/payload"
payload_tree="$payload_root/t5ynth-$version"
topdir="$rpm_root/rpmbuild"
tmppath="$rpm_root/tmp"
source_tar="$topdir/SOURCES/t5ynth-$version-linux-bundle.tar.gz"
specfile="$topdir/SPECS/t5ynth.spec"

rm -rf "$payload_root" "$topdir" "$tmppath"
mkdir -p \
    "$payload_tree/opt/T5ynth/backend" \
    "$payload_tree/usr/bin" \
    "$payload_tree/usr/share/applications" \
    "$payload_tree/usr/share/icons/hicolor/1024x1024/apps" \
    "$payload_tree/usr/share/T5ynth/presets" \
    "$payload_tree/usr/share/licenses/t5ynth" \
    "$topdir/SOURCES" "$topdir/SPECS" "$topdir/BUILD" "$topdir/RPMS" "$topdir/SRPMS" "$topdir/BUILDROOT" \
    "$tmppath"

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

cat > "$specfile" <<EOF
%global debug_package %{nil}
%global __os_install_post_build_reproducibility %{nil}
%global __brp_strip %{nil}
%global __brp_strip_comment_note %{nil}
%global __brp_strip_lto %{nil}
%global __brp_strip_static_archive %{nil}
%global __provides_exclude_from ^/opt/T5ynth/backend/.*$
%global __requires_exclude_from ^/opt/T5ynth/backend/.*$
Name:           t5ynth
Version:        $version
Release:        1%{?dist}
Summary:        Text-to-sound synthesizer standalone app
License:        GPL-3.0-only
URL:            https://github.com/joeriben/t5ynth
BuildArch:      x86_64
Requires:       bash
Requires:       gtk3
Requires:       webkit2gtk4.1
Requires:       libXcomposite
Requires:       alsa-lib
Requires:       libcurl
Requires:       fontconfig
Requires:       freetype

%description
T5ynth is a JUCE-based text-to-sound synthesizer. This Fedora RPM installs the
standalone app and its isolated bundled Python inference backend under /opt/T5ynth.
The backend bundle comes from a release-built bundle set selected by bundle-id.
Model weights are not bundled and must be installed separately after launch.

%prep

%build

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}
cp -a $payload_tree/* %{buildroot}/

%files
/opt/T5ynth
/usr/bin/t5ynth
/usr/bin/t5ynth-preflight
/usr/share/applications/t5ynth.desktop
/usr/share/icons/hicolor/1024x1024/apps/t5ynth.png
/usr/share/T5ynth/presets
/usr/share/licenses/t5ynth/LICENSE.txt
/usr/share/licenses/t5ynth/THIRD_PARTY_LICENSES.txt
EOF

QA_RPATHS=$((0x0010)) rpmbuild \
    --define "_topdir $topdir" \
    --define "_tmppath $tmppath" \
    -bb "$specfile"

echo
echo "RPM build complete."
find "$topdir/RPMS" -type f -name '*.rpm' -print
