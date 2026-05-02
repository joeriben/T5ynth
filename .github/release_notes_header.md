**T5ynth runs on macOS, Windows and Linux.** macOS and Windows ship with installers (Standalone + VST3 plugin; macOS also Audio Unit). Linux is available via source build.

## Installation

### macOS
1. Download **`T5ynth-macOS-Installer.pkg`**
2. Double-click the `.pkg`. The installer itself is functional; on current macOS versions you may first get the usual Gatekeeper warning because this build is not Apple-signed/notarized.
3. If macOS blocks the installer, open **System Settings > Privacy & Security**, scroll to the security message for T5ynth, and click **Open Anyway**.
4. Enter your admin password when prompted, then confirm once more if macOS asks again.
5. The installer places **T5ynth.app** in `/Applications/` and creates the preset/model folders under `/Library/Application Support/T5ynth/`.
6. Launch **T5ynth.app** from `/Applications/`

> **Note:** On some macOS versions, right-clicking the `.pkg` and choosing **Open** also works. If the installer later blocks the app on first launch, use the same **Privacy & Security > Open Anyway** flow once for `T5ynth.app`.

The macOS installer ships **Standalone**, **VST3** and **Audio Unit (AU)** in a single `.pkg`. The plugin choices are pre-selected in the installer; deselect them at install time if you only want the Standalone.

### Platform Scope
This beta release ships public installers for **macOS** (Standalone + VST3 + AU) and **Windows** (Standalone + VST3).

Linux is **best-effort**: build from source via [`docs/DEV_BUILD.md`](https://github.com/joeriben/T5ynth/blob/main/docs/DEV_BUILD.md) or [`docs/LINUX_INSTALLATION.md`](https://github.com/joeriben/T5ynth/blob/main/docs/LINUX_INSTALLATION.md). The CI produces Linux Standalone / VST3 archives plus an Ubuntu `.deb` on every push — they are downloadable from the [*Actions* tab](https://github.com/joeriben/T5ynth/actions/workflows/build.yml) as workflow artifacts but are intentionally not attached to release pages and are not officially supported.

### Windows
1. Download **`T5ynth-Windows-Setup.exe`** and every **`T5ynth-Windows-Setup-*.bin`** file.
2. Put all Windows setup files in the same folder.
3. Run `T5ynth-Windows-Setup.exe` and follow the setup prompts.
4. Launch T5ynth from the installed Start Menu shortcut or installation folder.

### Linux
No installer is published. See *Platform Scope* above.

---
