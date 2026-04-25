# Contributing to T5ynth

T5ynth is a JUCE-based synthesizer plugin (Standalone / VST3 / AU) that uses
diffusion audio models as oscillators via a Python inference backend connected
over a named pipe. It is licensed under GPLv3. For background and user-facing
features see [README.md](README.md).

This document is the entry point for code contributions. Detailed topics live
in the linked sibling docs — please read those before working on anything that
touches them.

---

## Before you contribute

1. Read [ARCHITECTURE.md](ARCHITECTURE.md) to understand the JUCE/Python split,
   the IPC pipe, and the high-level module layout.
2. Get a local Release build working — see [docs/DEV_BUILD.md](docs/DEV_BUILD.md).
3. Launch the standalone build, run the first-time setup, generate at least
   one sound, and play notes through the full signal chain. If you cannot
   reproduce a working baseline locally, do not start changing code.
4. Open the in-app Manual (Manual button in the top bar) and read it. It is
   the authoritative description of user-facing behaviour and the source of
   truth for what the synth is supposed to do.

---

## License

T5ynth is GPLv3 — see [LICENSE.txt](LICENSE.txt).

The vendored `JUCE/` directory is JUCE 8, dual-licensed under AGPLv3 and a
commercial license; this project uses it under the AGPLv3 terms, which is
GPLv3-compatible for the resulting binary.

By submitting a contribution (PR, patch, commit) you license that contribution
under GPLv3. There is no separate CLA.

Contributions must not introduce code under licenses incompatible with GPLv3.
This rules out, among others:

- proprietary or closed-source code
- non-commercial-only licenses (CC BY-NC, etc.) for *code*
- patent-encumbered code without an explicit patent grant

Currently used third-party libraries (all GPLv3-compatible):

- JUCE 8 — AGPLv3 (vendored under `JUCE/`)
- nlohmann/json — MIT
- Signalsmith Stretch — MIT
- SentencePiece — Apache 2.0

Models that are downloaded at runtime or shipped alongside the binary are a
separate licensing concern and are tracked per-model in
[THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt). When adding a model see
[docs/ADDING_A_MODEL.md](docs/ADDING_A_MODEL.md).

---

## Git workflow

- Branch from `main`. Branch names are not strictly conventionalised;
  `fix/foo`, `feat/bar`, `docs/baz` are all fine — just keep them short and
  descriptive.
- Commit messages follow a Conventional-Commits-style prefix observed in the
  existing history:

  ```
  type(scope): subject
  ```

  with `type` ∈ `feat | fix | docs | refactor | chore | ci`. The `scope` is
  optional and short, e.g. `drift`, `ui`, `setup`, `cmake`, `linux`, `genseq`,
  `backend`, `preset`, `engine`. The subject is imperative and lowercase
  (sentence-style is also acceptable as long as it is consistent within the
  message).
- One logical change per commit. Do not bundle unrelated fixes.
- Use the commit body to explain *why* the change is needed. The diff already
  shows *what* changed.

### Commit message examples

From the actual history:

```
fix(ui): Model Manager consistency, About dialog, WT layout fixes      (16422dc)
fix(drift+genseq): single-load crossfade, symmetric evolve direction   (f9e7c43)
fix(drift): single-load crossfade prevents volume jumps during regen   (8724269)
feat(noise): add White/Pink/Brown noise oscillator, restructure ...    (6adeeb6)
ci(linux): add libgtk-3-dev for JUCE WebBrowserComponent               (dba774a)
```

Match this style.

---

## Pull requests

- Target `main`.
- CI must be green on all three platforms (macOS / Linux / Windows) before a
  PR is merged. The matrix is defined in
  [`.github/workflows/build.yml`](.github/workflows/build.yml). CI verifies
  that the C++ plugin and the PyInstaller backend build cleanly on every
  platform, and runs a launch smoke-test on the macOS job only. There is no
  automated smoke-test on Linux or Windows yet — see
  [docs/TESTING.md](docs/TESTING.md).
- Link any related issues in the PR description.
- If your change touches build config, dependencies, CMake, or
  platform-specific code paths: state explicitly in the PR description which
  platforms you tested locally. CI catches link/build errors but not runtime
  regressions on platforms you did not run yourself.
- If your change touches the IPC protocol between JUCE and Python: update
  [docs/IPC_PROTOCOL.md](docs/IPC_PROTOCOL.md) in the same PR.
- If your change adds a new inference engine: update
  [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt) and the Third-Party
  section of `resources/T5ynth_Guide.html`. Update
  [docs/ADDING_A_MODEL.md](docs/ADDING_A_MODEL.md) only if the integration
  pattern itself changed.
- If your change adds a new modulation destination: see
  [docs/ADDING_A_MODULATION_TARGET.md](docs/ADDING_A_MODULATION_TARGET.md).
- If your change touches the preset format: see
  [docs/PRESET_FORMAT.md](docs/PRESET_FORMAT.md). Preset format changes must
  be backward-compatible or include a clearly bumped version field.
- If your change affects user-facing behaviour: update
  `resources/T5ynth_Guide.html` accordingly.

---

## Coding conventions

- C++20.
- JUCE house style: `juce::` qualifiers (no `using namespace juce`),
  camelCase for methods and members, member-initialiser lists in
  constructors, `override` on every overridden virtual, `[[nodiscard]]` where
  it adds value.
- No clang-format config is enforced. Match the surrounding code in the file
  you are editing.
- Prefer explicit types over `auto` when the type is not obvious from the
  right-hand side. `auto x = makeFoo();` is fine; `auto x = thing.value;` is
  not.
- Comment the *why*, not the *what*. Do not paraphrase the code in a comment
  above it.
- DSP code: prefer sample-accurate logic. No per-sample heap allocations.
- Audio thread: never block. No file I/O, no locks (other than lock-free
  primitives where unavoidable), no `new` / `delete`, no logging that
  allocates, no calls into Python or the IPC layer.

---

## Testing

There is no formal C++ unit test suite at the moment. The CI matrix in
`.github/workflows/build.yml` builds and smoke-tests the standalone
application on macOS, Linux, and Windows; that is the floor, not the ceiling.

For details and the current state of testing tooling see
[docs/TESTING.md](docs/TESTING.md).

Before opening a PR, you are expected to manually verify your change locally:

1. Build the standalone in Release.
2. Launch it, complete first-time setup if needed.
3. Load the default preset.
4. Trigger a generation and play notes through the full signal chain
   (oscillator → filter → envelopes → effects → output).
5. Exercise the specific code path your change touches under realistic
   conditions, not just an isolated unit.

A change that has only been compiled but never run is not ready for review.

---

## Releases and versioning

Releases are tag-driven. Tagging `v*` on `main` triggers the tag pipeline in
`.github/workflows/build.yml`. Pull requests and pushes to `main` still build
the macOS / Linux / Windows matrix, but tagged GitHub Releases currently
publish only the macOS installer. See
[docs/RELEASE_PROCESS.md](docs/RELEASE_PROCESS.md) for the full procedure.

Versioning convention:

- Public prerelease sequence: `v1.0.0-beta.N`, then `v1.0.0-rc.N`
- Do not cut stable `v1.0.x` until the installer path has been validated on end-user machines.
- Stable releases, once justified: `v1.0.x`
- Release candidates / pre-releases after 1.0: `v1.0.x-rc.N` (also `-alpha.N`,
  `-beta.N`)
- macOS installer packages map those tags to numeric receipt versions, e.g.
  `v1.0.0-beta.1 -> 1.0.0.201`

---

## Reporting bugs and asking questions

Use GitHub Issues. Issue templates are provided for bug reports and feature
requests — please fill them in rather than submitting freeform issues. For
bugs, include OS, hardware (especially GPU/accelerator), T5ynth version, and
reproducible steps.

Security-relevant issues that should not be public: contact the maintainer
directly rather than filing a public issue.

---

## Code of conduct

Be civil and stay technical. Personal attacks, harassment, and off-topic
content will be removed.

---

## Maintainer

T5ynth is maintained by Prof. Dr. Benjamin Jörissen — UNESCO Chair in Digital
Culture and Arts in Education (UCDCAE), Friedrich-Alexander-Universität
Erlangen-Nürnberg. The repository is a personal side-project carried out
alongside academic work. Response times on issues and PRs are not guaranteed.
