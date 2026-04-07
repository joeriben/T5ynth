## Installation

### macOS
1. Extract the `.tar.xz` archive
2. Move **T5ynth.app** to `/Applications/`
3. Open Terminal and run:
```bash
xattr -cr /Applications/T5ynth.app
```
4. Launch T5ynth from `/Applications/`

> **Why is this needed?** T5ynth is open-source software and not signed with an Apple Developer certificate. macOS quarantines all apps downloaded from the internet and will show "T5ynth.app is damaged" until the quarantine flag is removed. This is a one-time step.

### Linux
Extract and run. You may need to `chmod +x T5ynth` and `chmod +x backend/pipe_inference` if your archive tool does not preserve permissions.

---
