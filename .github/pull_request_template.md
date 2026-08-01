## Summary

<!-- 1-3 sentences describing what this PR does and why. -->

## Related issues

<!-- e.g. Fixes #123 / Related to #456 -->

## Type of change

- [ ] Bug fix
- [ ] New feature
- [ ] Refactor
- [ ] Documentation
- [ ] CI / build
- [ ] Other

## Platforms tested locally

- [ ] macOS
- [ ] Linux
- [ ] Windows 11

<!-- Specify which platform(s) you actually verified on, including OS version and hardware. -->

Verified on:

## Contributor checklist

- [ ] I have read `CONTRIBUTING.md`
- [ ] `cmake --build build_clean --config Release` succeeds on my machine
- [ ] I ran the standalone and verified no regressions in the touched areas
- [ ] Relevant documentation is updated (`docs/`, `README.md`, `resources/akroasys_Guide.html`)
- [ ] Commits follow the conventional-commit style used in the repo (`type(scope): subject`)
- [ ] I understand that CI must be green on all three platforms before merge
- [ ] If touching the IPC protocol: `docs/IPC_PROTOCOL.md` is updated
- [ ] If adding a new model: `THIRD_PARTY_LICENSES.txt` and the HTML Manual's Third-Party section are updated
- [ ] If touching DSP: the default preset still plays cleanly after this change

## Notes for reviewer

<!-- Anything the reviewer should know: tricky areas, follow-ups, known limitations. -->
