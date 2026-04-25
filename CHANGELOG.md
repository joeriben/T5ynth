# Changelog

## v1.3.0-beta.1 - 2026-04-24

- Expanded the instrument from an early beta into a fuller text-to-sound workflow with independent wavetable extraction regions, shared P1/P2/P3 traversal controls, and a clearer wavetable start-point model.
- Added session persistence for the standalone app so working states survive quit/relaunch instead of behaving like disposable test sessions.
- Reworked the preset workflow with factory presets, `INIT`, explicit P1/P2/P3 locks, auto-trim support, and cleaner preset migration/loading behavior.
- Broadened the musical control surface with a microtuning system, non-Western scales, Shruti support, sampler-mode tuning support, and a more expressive generative sequencer.
- Extended modulation and motion design with drift random waveforms, additional drift targets, sample-and-hold LFO support, ghost sliders, and a denser but more legible sequencer layout.
- Moved the user-facing setup flow toward installer-first distribution on macOS, added a Windows installer path, and tightened model/preset placement around per-user defaults with system-wide scan fallbacks.
- Improved the guided onboarding and in-app documentation with the embedded manual overlay, clearer install guidance, and a more robust download/setup flow for model assets.
- Hardened backend startup, model discovery, and download handling so failures surface more clearly and model paths behave consistently across packaged installs.
- Stabilized playback and interaction around generate/regenerate, active-voice sampler behavior, modulation editing, shutdown, and repaint-heavy GUI paths.
- Kept the core dependency baseline unchanged across the beta line: `backend/requirements.txt` and the top-level CMake `FetchContent` pins for `nlohmann/json` and `signalsmith-stretch` did not change between `v1.0.0-beta.1`, `v1.0.0-beta.2`, and `v1.0.0-beta.3`.

## Notes

- `v1.3.0-beta.1` keeps the release on the beta line while the broader rollout (VST3/AU and additional public platforms) is still unfinished; the current tagged release asset remains the macOS installer only.
- The version number stays monotonic with the older internal `1.2.x` build line and avoids another downgrade in bundle / installer versioning.
