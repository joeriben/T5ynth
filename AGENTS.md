# Repository Instructions

- Check twice before asking the user to restart servers, hard reload pages, rerun apps, or repeat an installation step.
- Before making any release, versioning, tag, or release-scope statement, read `docs/RELEASE_PROCESS.md` first. Treat its current public release scope as authoritative: tagged GitHub Releases currently publish the macOS and Windows installers; Linux/VST3/AU outputs are not current public release assets.
- Never upload locally built artifacts to GitHub Releases. Public release assets must come from GitHub Actions artifacts.
- For changes touching `.github/workflows/build.yml`, `CMakeLists.txt`, installers, backend bundling, or packaging paths:
  - run a local Release build first,
  - use PR/main GitHub CI to validate before any release tag,
  - keep `docs/RELEASE_PROCESS.md` in sync with the workflow.
- Do not cut, move, or reuse a release tag until the relevant GitHub Actions run is green.
