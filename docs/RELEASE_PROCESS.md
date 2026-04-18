# T5ynth Release Process

Authoritative reference for producing T5ynth releases. Derived from
`.github/workflows/build.yml` and the `gh` CLI. If this document and the
workflow file disagree, the workflow file wins — update this document.

Repository: `joeriben/t5ynth`. Default branch: `main`.

---

## 1. Versioning convention

T5ynth uses semantic version tags prefixed with `v`.

- **Current public line:** `v0.3.0-alpha.1`, `v0.3.0-alpha.2`, ...
- **Until macOS end-user installation is proven reliable, do not cut 1.x tags.**
- **Stable releases, once justified:** `v1.0.0`, `v1.0.1`, ...
- **Release candidates / pre-releases after 1.0:** `v1.0.0-rc.1`,
  `v1.0.0-beta.1`, etc.

Implementation detail:

- The Git tag may include a prerelease suffix such as `-alpha.N`.
- The macOS installer package version is translated to a dotted numeric
  version with a fourth component so installer receipts still sort correctly:
  `0.3.0-alpha.2 -> 0.3.0.102`, `0.3.0-beta.1 -> 0.3.0.201`,
  `0.3.0-rc.3 -> 0.3.0.303`, `0.3.0 -> 0.3.0.400`.

The release job in CI auto-detects pre-release suffixes via a shell case
statement:

```bash
case "${{ github.ref_name }}" in
  *-rc*|*-alpha*|*-beta*) PRERELEASE="--prerelease" ;;
esac
```

Any tag matching `*-rc*`, `*-alpha*`, or `*-beta*` is passed to
`gh release create` with `--prerelease`, which marks the GitHub Release as a
pre-release. Stable tags are created as full releases.

---

## 2. When to bump what

Semantic versioning applies to the T5ynth distribution as a whole.

| Component   | Bump   | Triggers                                                    |
|-------------|--------|-------------------------------------------------------------|
| Patch `Z`   | `x.y.Z`| Bug fixes, doc updates, no user-visible behaviour change    |
| Minor `Y`   | `x.Y.z`| New features, new models, new UI                           |
| Major `X`   | `X.y.z`| Breaking changes to preset format, IPC protocol, or public plugin parameters |

Major bumps are not expected frequently. Anything that would force users to
re-create presets or would break compatibility with older hosts counts as
breaking.

---

## 3. Trigger mechanism

The release pipeline lives in `.github/workflows/build.yml`. It runs on:

```yaml
on:
  push:
    branches: [main]
    tags: ['v*']
  pull_request:
    branches: [main]
```

- Pushes to `main` and pull requests run the `macos`, `windows`, and
  `linux` build jobs — but **not** the `release` job.
- Pushes of a tag matching `v*` run all three build jobs **plus** the
  `release` job, which is gated by:

  ```yaml
  release:
    if: startsWith(github.ref, 'refs/tags/v')
    needs: [macos, windows, linux]
  ```

- **Tags are the only way to produce a GitHub Release from CI.** There is no
  manual dispatch. Do not use the GitHub web UI to cut a release directly —
  always push a tag.

---

## 4. Build matrix

Three platforms build in parallel. All three must succeed for the `release`
job to run (declared via `needs: [macos, windows, linux]`).

| Job       | Runner           | Targets                         |
|-----------|------------------|---------------------------------|
| `macos`   | `macos-14`       | macOS app + `.pkg` installer    |
| `linux`   | `ubuntu-latest`  | App, VST3                       |
| `windows` | `windows-latest` | App, VST3                       |

Every job:

1. Checks out the repo (`submodules: true`).
2. Installs system dependencies and Python 3.11.
3. Installs `pyinstaller` plus `backend/requirements.txt`.
   - On Windows and Linux, `torch` is pulled from the CUDA 12.4 wheel
     index (`https://download.pytorch.org/whl/cu124`).
4. Runs `pyinstaller pipe_inference.spec --noconfirm` in `backend/` to
   bundle the Python inference backend.
5. Runs `cmake -B build -DCMAKE_BUILD_TYPE=Release`, then
   `cmake --build build --config Release -j<ncpu>`.
6. Assembles a distribution directory containing the built binary plus the
   bundled backend under `backend/`.
7. Creates `.tar.xz` archives on the build machine (see §5).
8. Uploads each archive with `actions/upload-artifact@v4`.

### Linux-specific notes

- Swap is expanded to 8 GB before install to give PyTorch / PyInstaller
  headroom.
- The Linux CI runner currently produces **x86_64** binaries only, so the
  release asset names must keep the architecture suffix.
- The apt install list must cover all JUCE Linux dependencies. The current
  list includes `libgtk-3-dev` and `libwebkit2gtk-4.1-dev` for the in-app
  manual `WebBrowserComponent`. If a new JUCE module is enabled that pulls
  in additional pkg-config requirements, both the apt list and the
  `CMakeLists.txt` link step must be updated (see §12).

### macOS-specific notes

- The `macos` job runs on `macos-14` (Apple Silicon).
- End-user macOS releases should be both `Developer ID` signed and notarized.
  The release pipeline now supports that directly in
  `installer/macos/build_pkg.sh`.
- `CMakeLists.txt` contains `POST_BUILD` re-signing commands for the VST3
  and AU bundles (`codesign --force --sign - --deep`). This works around
  two JUCE 8.0.6 issues:
  1. JUCE signs the VST3 bundle before `juce_vst3_helper` writes
     `Contents/Resources/moduleinfo.json`, invalidating the sealed
     signature and causing DAWs to refuse to register the plugin.
  2. JUCE 8.0.6 does not call `_juce_adhoc_sign` for the AU target,
     leaving a minimal Mach-O signature that expects a sealed
     `_CodeSignature` folder that never gets created.
  The re-signing commands run as `POST_BUILD` on `T5ynth_VST3` and
  `T5ynth_AU` after all JUCE manifest / sign steps. Do not remove them.
- The macOS job also runs a smoke test that launches the built
  `T5ynth.app` for 5 seconds and fails the job if the process exits
  early.
- `installer/macos/build_pkg.sh` accepts signing and notarization options
  either as CLI flags or environment variables:
  - `MACOS_APP_SIGN_IDENTITY` or `--sign-app-identity`
  - `MACOS_PKG_SIGN_IDENTITY` or `--sign-pkg-identity`
  - `MACOS_NOTARY_KEYCHAIN_PROFILE` or `--notary-keychain-profile`
  - `MACOS_NOTARY_APPLE_ID` / `MACOS_NOTARY_PASSWORD` /
    `MACOS_NOTARY_TEAM_ID`
  - `MACOS_NOTARY_API_KEY_PATH` / `MACOS_NOTARY_API_KEY_ID` /
    `MACOS_NOTARY_API_ISSUER`
- The script signs the staged `T5ynth.app` with hardened runtime, signs
  the final `.pkg` with `Developer ID Installer`, then submits the `.pkg`
  with `xcrun notarytool`, waits for acceptance, and staples the ticket
  before writing the final output archive.

### Required macOS signing secrets in GitHub Actions

The workflow stays unsigned when these secrets are absent. To produce a
shipping macOS installer, add all of the following:

- `MACOS_APP_CERT_P12_BASE64`
- `MACOS_APP_CERT_P12_PASSWORD`
- `MACOS_PKG_CERT_P12_BASE64`
- `MACOS_PKG_CERT_P12_PASSWORD`
- `MACOS_KEYCHAIN_PASSWORD`

For notarization, provide one of these authentication sets:

- Apple ID flow:
  - `MACOS_NOTARY_APPLE_ID`
  - `MACOS_NOTARY_PASSWORD`
  - `MACOS_NOTARY_TEAM_ID`
- App Store Connect API key flow:
  - `MACOS_NOTARY_API_PRIVATE_KEY`
  - `MACOS_NOTARY_API_KEY_ID`
  - `MACOS_NOTARY_API_ISSUER`

Local release builds can use the same environment variables directly
without GitHub Actions.

---

## 5. Artifact layout

Linux and Windows archives are built with `tar -cJf` (xz-compressed tar)
**before** upload. This is deliberate: `actions/upload-artifact` strips
Unix permission bits, which would break the executable bit on the
`T5ynth` binary and on `backend/pipe_inference`. By tarring on the build
machine, permissions survive the round-trip. macOS end-user releases are
currently installer-only (`T5ynth-macOS-Installer.pkg`).

Each archive contains the platform binary plus:

- `LICENSE.txt`
- `THIRD_PARTY_LICENSES.txt`
- `DO_NOT_FORGET.txt` (legacy release notes, bundled on archive-based
  platforms for simplicity)

On Windows the backend is copied into the distribution alongside the
binary:

- Windows: into `T5ynth/backend/` next to `T5ynth.exe`
- Linux: into `T5ynth/backend/` next to `T5ynth`

On macOS the backend is embedded directly into
`T5ynth.app/Contents/Resources/backend/` before the installer is built.

The `release` job downloads all artifacts with `actions/download-artifact`,
flattens them with `find artifacts -name '*.tar.xz' -exec mv {} release/ \;`,
also collects `.pkg` / `.exe` installers, and hands the combined set to
`gh release create`.

### Expected release assets for the current stable line (1 total)

```
T5ynth-macOS-Installer.pkg
```

For the current stable release process, GitHub Releases publish **only** the
macOS installer. Windows, Linux, VST3 and AU remain planned work, but they are
not attached to stable tags until each distribution path has been validated on
its own.

If the release page does not contain `T5ynth-macOS-Installer.pkg`, something
went wrong — investigate before announcing the release.

---

## 6. Release notes

Release notes are assembled by the `release` job:

1. `.github/release_notes_header.md` is copied to `release_notes.md` as a
   fixed prefix. This file is evergreen — it should not reference a
   specific version number. It currently documents the macOS installer
   flow and the Linux `chmod +x` fallback.
2. `gh api repos/joeriben/t5ynth/releases/generate-notes` is called with
   the new tag and the previous tag; the `.body` field from the response
   is appended to `release_notes.md`. This produces a "What's Changed" /
   contributor list section automatically from commit history.
3. The combined file is passed to `gh release create --notes-file
   release_notes.md`, currently with the macOS installer asset only.

### Customising release notes

- **Before tagging:** edit `.github/release_notes_header.md` and commit.
  This is the right place for installation instructions, compatibility
  notes, or platform warnings that should appear on every release.
- **Version-specific notes:** edit the release on GitHub after creation
  (`gh release edit vX.Y.Z --notes-file ...` or via the web UI). The
  auto-generated commit list usually covers the changes; add a
  hand-written summary above it if the release needs one.

---

## 7. Pre-release checklist (MANDATORY)

This checklist exists because of the v1.0.1 incident (§12). Work through
it every time before pushing a release tag — no exceptions.

1. **Local build succeeds** on at least one platform:

   macOS / Linux:
   ```bash
   cmake -B build_clean -DCMAKE_BUILD_TYPE=Release
   cmake --build build_clean --config Release -j$(sysctl -n hw.ncpu)   # or nproc on Linux
   ```
   Windows (PowerShell):
   ```powershell
   cmake -B build_clean -DCMAKE_BUILD_TYPE=Release
   cmake --build build_clean --config Release -j $env:NUMBER_OF_PROCESSORS
   ```

2. **App launches cleanly.** No crash, no missing-dylib errors on
   stdout, UI renders.

3. **Default preset loads, generation runs, audio comes out.** Hit
   Generate once with a default prompt and confirm audio.

4. **Cross-platform CI audit when `CMakeLists.txt` changed.** If the
   commit range touches any of:

   - `target_compile_definitions`
   - `target_link_libraries`
   - `find_package`
   - `juce_add_binary_data`
   - any `juce_add_*` flag (especially `NEEDS_WEB_BROWSER`,
     `NEEDS_CURL`, module enable flags)

   ...then walk `.github/workflows/build.yml` line by line and verify that
   **every** platform install step covers any new dependencies. A passing
   local macOS build tells you nothing about Linux apt packages or
   Windows MSVC flags.

5. **JUCE module audit.** JUCE 8 modules may have undeclared Linux
   `linuxPackages:` requirements and need manual
   `juce::pkgconfig_<NAME>` linking in `CMakeLists.txt`. If you enabled a
   new JUCE feature, verify both the apt install and the pkgconfig link
   step.

6. **Docs reflect reality.** `docs/README.md`, any HTML manual, and
   `.github/release_notes_header.md` must describe the actual current
   state.

7. **Working tree is clean.** `git status` shows nothing pending. All
   intended changes are committed to `main`.

8. **Commit range review.** `git log --oneline <previous-tag>..HEAD`
   shows exactly the commits you expect in this release — no stray WIP,
   no forgotten fixups.

---

## 8. Tagging and pushing

Use an annotated tag so the message is preserved in the object store:

```bash
git tag -a vX.Y.Z -m "T5ynth vX.Y.Z

- First notable change
- Second notable change
- Third notable change"
```

Then push the tag to `origin`:

```bash
git push origin vX.Y.Z
```

CI picks up the tag push within seconds. You do not need to push the
branch separately — the workflow triggers on the tag ref alone — but make
sure `main` already contains the tagged commit before tagging.

---

## 9. Monitoring the release build

Use the `gh` CLI to watch the build from the terminal.

```bash
# List the three most recent runs of build.yml
gh run list --workflow=build.yml --limit 3

# Inspect a specific run's job summary
gh run view <run-id>

# Show only the failed step logs for a run
gh run view <run-id> --log-failed

# Block until a run finishes
gh run watch <run-id> --exit-status
```

**Caveat:** `gh run watch --exit-status` is unreliable in some `gh`
versions — it may report success for a failed run. Always verify with
`gh run view <run-id>` afterwards before assuming the release is out.

### Typical wall-clock times

| Job       | Approximate duration                    |
|-----------|-----------------------------------------|
| `macos`   | ~12 min                                 |
| `windows` | ~19 min                                 |
| `linux`   | ~28 min (apt install dominates)         |
| `release` | ~2 min                                  |
| **Total** | ~30 min from tag push to finished release |

The `release` job cannot start until all three build jobs finish, so the
total is gated by the slowest platform (usually Linux).

---

## 10. Verifying the release

After CI reports the `release` job as green:

```bash
gh release view vX.Y.Z
```

Check:

- **Title:** `T5ynth vX.Y.Z`.
- **Prerelease flag:** set if and only if the tag matched `-rc*`, `-alpha*`,
  or `-beta*`.
- **Asset count:** exactly 7 (§5). Any missing asset means a platform
  build silently skipped its upload step — investigate.
- **Release notes:** begin with the content of
  `.github/release_notes_header.md`, followed by the auto-generated
  "What's Changed" block.

Public release URL:

```
https://github.com/joeriben/t5ynth/releases/tag/vX.Y.Z
```

---

## 11. Recovering from a failed release build

When a CI matrix job fails on a tag push, the `release` job is blocked by
its `needs:` dependency and **no GitHub Release is created**. Only the
tag exists in the repository. This is the normal failure mode and is
recoverable.

### Option A — preferred for a broken pre-release

Use this when no release assets were published (i.e. the `release` job
never ran).

1. Fix the issue on `main` with a normal commit.
2. Wait for the `main` build to go green in CI — this proves the fix
   works on all three platforms.
3. Force-move the tag to the fixed commit:

   ```bash
   git tag -f -a vX.Y.Z <fixed-commit-sha> -m "T5ynth vX.Y.Z"
   git push --force origin vX.Y.Z
   ```

   Because the broken tag never produced release assets, there is
   nothing to corrupt downstream.

### Option B — if a release was already published

Use this when `gh release view vX.Y.Z` shows a real release with assets,
and something is wrong with one of those assets.

1. Fix the issue on `main`.
2. Bump the patch number (`vX.Y.Z+1`) and tag the fix.
3. Optionally mark the broken release as a pre-release or delete it via
   `gh release edit` / `gh release delete` — but **do not force-move the
   tag**, because users may already have downloaded the assets.

### Hard rules

- **Never** force-push `main`.
- **Never** force-move a tag that already has a published GitHub Release
  associated with it.
- **Never** use `--no-verify` to bypass pre-commit or pre-push hooks. If a
  hook fails, fix the root cause.

---

## 12. Cautionary example — the v1.0.1 incident (2026-04-09)

The `v1.0.1` release was initially pushed on a commit that enabled
`JUCE_WEB_BROWSER=1` in `CMakeLists.txt` for the new in-app Manual
`WebBrowserComponent` overlay. The local macOS build passed. The tag was
pushed. CI then failed on Linux with:

```
fatal error: gtk/gtk.h: No such file or directory
```

Two things were wrong:

1. `libgtk-3-dev` was not in the apt install list in
   `.github/workflows/build.yml` — only `libwebkit2gtk-4.1-dev` was
   installed.
2. Even after adding `libgtk-3-dev`, the build still failed because
   JUCE 8's `juce_gui_extra` module header has no `linuxPackages:`
   declaration. The `JUCE_BROWSER_LINUX_DEPS` pkgconfig target had to be
   linked **explicitly** from `CMakeLists.txt` via
   `juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS`, gated on UNIX AND NOT
   APPLE.

Recovery flow:

1. Fixed both issues on `main` (apt list + CMake link step).
2. Verified `main` CI was green on all three platforms.
3. Force-moved `v1.0.1` to the fixed commit (Option A in §11).

Total wasted CI time: roughly one hour.

**Lesson, embedded here as a standing rule:** always check cross-platform
CI dependencies before pushing a release tag. A passing local macOS
build tells you nothing about Linux apt packages, JUCE `linuxPackages:`
declarations, or Windows toolchain flags. When `CMakeLists.txt` changes,
the checklist in §7 step 4 is not optional.

---

## 13. Release notes header maintenance

`.github/release_notes_header.md` is prepended to every release. Keep it
evergreen:

- **Do not** mention a specific version number.
- **Do not** mention a specific release date.
- **Do** document any cross-cutting installation quirks — e.g. the macOS
  installer-only delivery path, the Linux `chmod +x` fallback — that
  users need to see on every release.
- Update it as part of the commit that introduces a new user-facing
  installation requirement, not as part of the release commit itself.
