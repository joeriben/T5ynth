## Installation

### macOS — Standalone
1. Extract `T5ynth-macOS-Standalone.tar.xz`
2. Move **T5ynth.app** to `/Applications/`
3. Open Terminal and run:
```bash
xattr -cr /Applications/T5ynth.app
```
4. Launch T5ynth from `/Applications/`

### macOS — VST3 plugin
**Requires T5ynth Standalone to be installed at `/Applications/T5ynth.app`** — the VST3 plugin borrows the Python backend from the Standalone bundle to keep the plugin download small.

1. Install the Standalone first (see above).
2. Extract `T5ynth-macOS-VST3.tar.xz`
3. Move **T5ynth.vst3** to `/Library/Audio/Plug-Ins/VST3/`
4. Open Terminal and run:
```bash
xattr -cr /Library/Audio/Plug-Ins/VST3/T5ynth.vst3
```
5. Restart your DAW and re-scan plugins.

### macOS — AU plugin
Same Standalone requirement as VST3.

1. Install the Standalone first.
2. Extract `T5ynth-macOS-AU.tar.xz`
3. Move **T5ynth.component** to `/Library/Audio/Plug-Ins/Components/`
4. Open Terminal and run:
```bash
xattr -cr /Library/Audio/Plug-Ins/Components/T5ynth.component
```
5. Restart your DAW and re-scan AU plugins.

> **Why is `xattr -cr` needed?** T5ynth is open-source software and not signed with an Apple Developer certificate. macOS quarantines everything downloaded from the internet; without removing the quarantine flag, Gatekeeper will either show "damaged" errors (Standalone) or silently refuse to register the plugin (VST3/AU). This is a one-time step per download.

### Linux
Extract and run. You may need to `chmod +x T5ynth` and `chmod +x backend/pipe_inference` if your archive tool does not preserve permissions.

---
