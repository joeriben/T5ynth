## Installation

### macOS (recommended)
1. Download **`T5ynth-macOS-Installer.pkg`**
2. Double-click the `.pkg` -- macOS will block it because T5ynth is not signed with an Apple Developer certificate.
3. Open **System Settings > Privacy & Security**, scroll down, and click **"Open Anyway"** next to the T5ynth message.
4. Enter your admin password when prompted.
5. The installer runs and places the app, VST3, and AU plugins in their standard locations.
6. Launch T5ynth from `/Applications/`

> **Note:** On older macOS versions (before Ventura), you can also right-click the `.pkg` and choose "Open" instead of steps 3-4.

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
