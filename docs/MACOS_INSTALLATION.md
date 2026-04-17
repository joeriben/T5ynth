# macOS Installation

`T5ynth-macOS-Installer.pkg` installs `T5ynth.app` into `/Applications` and
creates shared support folders under:

- `/Library/Application Support/T5ynth/presets`
- `/Library/Application Support/T5ynth/models`

The installer itself works. The only extra step on many Macs is the usual
Gatekeeper confirmation, because current public builds are not yet
Apple-signed/notarized.

## Install steps

1. Download `T5ynth-macOS-Installer.pkg`.
2. Double-click the package.
3. If macOS blocks it, open `System Settings > Privacy & Security`.
4. Scroll to the security message mentioning `T5ynth-macOS-Installer.pkg`.
5. Click `Open Anyway`.
6. Enter your admin password if prompted.
7. Complete the installer.
8. Open `T5ynth.app` from `/Applications`.

## If macOS blocks the app on first launch

Some systems allow the installer but block the app once on first launch. If
that happens:

1. Try to open `T5ynth.app` once from `/Applications`.
2. Open `System Settings > Privacy & Security`.
3. Scroll to the security message mentioning `T5ynth.app`.
4. Click `Open Anyway`.
5. Confirm once more if macOS asks again.

After that first approval, T5ynth should launch normally.

## Older macOS versions

On some older macOS versions, you can also right-click the `.pkg` or
`T5ynth.app` and choose `Open` instead of using the `Privacy & Security`
panel.
