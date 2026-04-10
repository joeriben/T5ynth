## Installation

### macOS (recommended)
1. Download **`T5ynth-macOS.pkg`**
2. Double-click to run the installer
3. Launch T5ynth from `/Applications/`

The installer places the app, VST3, and AU plugins in their standard locations and removes the quarantine flag automatically.

> **Note:** T5ynth is not signed with an Apple Developer certificate. You may need to right-click the `.pkg` and choose "Open" the first time, then confirm in the dialog.

<details>
<summary>Manual install (without .pkg)</summary>

#### Standalone
1. Extract `T5ynth-macOS-Standalone.tar.xz`
2. Move **T5ynth.app** to `/Applications/`
3. Open Terminal and run: `xattr -cr /Applications/T5ynth.app`

#### VST3 plugin
Requires T5ynth Standalone at `/Applications/T5ynth.app` (backend lives in the app bundle).

1. Extract `T5ynth-macOS-VST3.tar.xz`
2. Move **T5ynth.vst3** to `/Library/Audio/Plug-Ins/VST3/`
3. `xattr -cr /Library/Audio/Plug-Ins/VST3/T5ynth.vst3`
4. Restart your DAW.

#### AU plugin
Same Standalone requirement as VST3.

1. Extract `T5ynth-macOS-AU.tar.xz`
2. Move **T5ynth.component** to `/Library/Audio/Plug-Ins/Components/`
3. `xattr -cr /Library/Audio/Plug-Ins/Components/T5ynth.component`
4. Restart your DAW.

</details>

### Windows
1. Download **`T5ynth-Windows-Setup.exe`**
2. Run the installer (may require "Run as administrator")
3. Launch T5ynth from the Start Menu

The installer places the standalone app, VST3 plugin, and factory presets in their standard locations.

<details>
<summary>Manual install (without Setup.exe)</summary>

1. Extract `T5ynth-Windows-Standalone.tar.xz` to a folder of your choice
2. For VST3: extract `T5ynth-Windows-VST3.tar.xz` to `C:\Program Files\Common Files\VST3\`
3. Restart your DAW.

</details>

### Linux
Extract and run. You may need to `chmod +x T5ynth` and `chmod +x backend/pipe_inference` if your archive tool does not preserve permissions.

---
