# Developer Documentation Status

Working document tracking the developer-documentation pass initiated in the
session of 2026-04-09. Not a contributor-facing document — intended as the
editor's monitoring file across multiple passes with parallel agent runs.

## Scope

Eleven documents, grouped into two batches by dependency:

**Batch 1 — code-research-heavy (independent, can reference each other only
via cross-links after the fact):**

| ID | File | Task |
|----|------|------|
| D3 | `ARCHITECTURE.md` | #38 |
| D4 | `docs/IPC_PROTOCOL.md` | #39 |
| D5 | `docs/ADDING_A_MODEL.md` | #40 |
| D6 | `docs/ADDING_A_MODULATION_TARGET.md` | #41 |
| D7 | `docs/PRESET_FORMAT.md` | #42 |

**Batch 2 — meta / policy / build (can link into batch 1 results):**

| ID | File | Task |
|----|------|------|
| D1 | `CONTRIBUTING.md` | #36 |
| D2 | `docs/DEV_BUILD.md` | #37 |
| D8 | `docs/RELEASE_PROCESS.md` | #43 |
| D9 | `docs/TESTING.md` | #44 |
| D10 | `docs/README.md` | #45 |
| D11 | `.github/ISSUE_TEMPLATE/*`, `.github/pull_request_template.md` | #46 |

## Pass plan

1. **Pass 1** — spawn all Batch 1 agents in parallel. Review output, note gaps.
2. **Pass 1b** — spawn all Batch 2 agents in parallel (they can now link to
   Batch 1 files). Review output, note gaps.
3. **Pass 2** — follow-up agents for any missing sections or factual errors
   found during review. One agent per gap.
4. **Pass 3** (optional) — final polish if still needed.

## Shared style guide for all agents

- Target reader: experienced C++/JUCE developer new to T5ynth. May also touch
  Python for the inference backend.
- Strict technical/factual tone. No consumer-product marketing language
  ("higher quality", "faster than", "recommended main model"). No editorial
  or philosophical commentary.
- English only, to match the existing README and in-app Manual.
- Reference concrete file paths with `path/to/file.cpp:123` format where
  they clarify the point. Verify citations against the actual code; do not fabricate line
  numbers.
- Be honest about gaps and non-existence — if a feature isn't implemented, a
  convention isn't established, or tests don't exist, say so directly.
- No emojis.
- Keep length proportional to actual complexity. Do not pad.
- Do not duplicate content that already lives in README.md or
  resources/T5ynth_Guide.html — link to it.
- Write in a way that stays correct as the code evolves: prefer patterns,
  flows and conventions over literal snapshots.

## Status tracking

Legend: ☐ pending · ◐ drafted · ☒ reviewed+complete · ✗ needs rework

### Pass 1 (Batch 1) — drafts

- ☒ D3 `ARCHITECTURE.md` (545 lines)
- ☒ D4 `docs/IPC_PROTOCOL.md` (624 lines)
- ☒ D5 `docs/ADDING_A_MODEL.md` (768 lines — after Pass 2 edits)
- ☒ D6 `docs/ADDING_A_MODULATION_TARGET.md` (445 lines)
- ☒ D7 `docs/PRESET_FORMAT.md` (675 lines)

### Pass 1b (Batch 2) — drafts

- ☒ D1 `CONTRIBUTING.md` (213 lines — after Pass 2 edits)
- ☒ D2 `docs/DEV_BUILD.md` (537 lines — after Pass 2 edits)
- ☒ D8 `docs/RELEASE_PROCESS.md` (453 lines)
- ☒ D9 `docs/TESTING.md` (191 lines)
- ☒ D10 `docs/README.md` (76 lines)
- ☒ D11 `.github/ISSUE_TEMPLATE/{bug_report,feature_request}.md` + `.github/pull_request_template.md` (125 lines total)

### Pass 2 — targeted editor fixes applied

Seven surgical edits after reviewing the Pass 1/1b drafts:

1. **`CONTRIBUTING.md` §Pull requests** — corrected the claim that CI runs a
   smoke test "on each platform"; the launch smoke-test is macOS-only.
2. **`docs/DEV_BUILD.md` §11.4** — replaced stale "About dialog" phrasing
   with "in-app Manual overlay".
3. **`docs/DEV_BUILD.md` §12** — removed the fabricated
   `echo '{"op":"ping"}' | python pipe_inference.py` example. `op: ping` is
   not a valid request; the real schema is specified in
   `docs/IPC_PROTOCOL.md`. The section now points the reader at the protocol
   spec instead of inventing a request.
4. **`docs/ADDING_A_MODEL.md` §2.7 (bundle gotcha)** — removed a leaked
   reference to the editor's private memory file
   `feedback_pyinstaller_multiprocessing.md`; replaced with a pointer to
   historical PyInstaller notes that used to live at the repo root.
5. **`docs/ADDING_A_MODEL.md` §9 (testing checklist)** — removed a leaked
   reference to the editor's private memory file `feedback_build_folder.md`
   and corrected a misattribution of `tools/generate_reference.py`
   (it generates WAV via the diffusers pipeline, not IPC requests).
   Section now points at `docs/IPC_PROTOCOL.md` for the request schema.
6. **`ARCHITECTURE.md` §12** — the `docs/PRESET_FORMAT.md` cross-reference
   claimed the format doc covered "v1/v2 backwards-compat rules". In fact
   the version byte is currently unread (documented as such in
   `PRESET_FORMAT.md` §2.1 and §8). Updated the cross-ref to match reality.

All Pass 2 fixes are surgical Edit calls, verified by re-grepping the final
files for `feedback_`, `subagent`, `Claude Opus`, `Anthropic`, `Higher quality`,
`recommended main`, etc. — zero remaining hits in contributor-facing docs.
The only remaining match is in this status file itself (the style guide
above lists the forbidden slop phrases as examples).

## Review notes

### Pass 1 — findings from agent drafts (2026-04-09)

The five Batch 1 agents read the code in depth and surfaced a number of
real issues that are **out of scope for this documentation pass** but
worth tracking for follow-up. These are NOT doc bugs — they are code
findings the agents ran into while researching their respective topics.

**Code findings to review separately (not fixed here):**

1. **`src/dsp/ModulationMatrix.{h,cpp}` is a stub.** `process()` does
   nothing (`.cpp:13-16`). Real modulation routing is inline in
   `PluginProcessor::processBlock` and `SynthVoice::renderBlock`. The
   class name is misleading for new contributors. Candidate for either
   removal or actual implementation.

2. **Three parallel modulation-target enumerations maintained by hand.**
   APVTS `StringArray` (`PluginProcessor.cpp:294-314`), `BlockParams.h`
   enum (`:5-37`), and `SynthPanel.cpp` `addItemList` calls (`:28-29`,
   `:99-100`, `:137-139`) are three independent sources of truth that
   can drift. One is already stale — see (3).

3. **`envTargetToString`/`lfoTargetToString` are stale.** The string
   tables in `PluginProcessor.cpp:1590-1604` and `:1606-1624` do not
   cover all current target enum values. `EnvTarget::LFO1Rate..LFO2Depth`
   (indices 9..12) fall outside the 9-entry table and round-trip to
   `"none"` through the `.t5p` preset path. Self-consistent bijection
   inside the table so save→load works coincidentally for tracked
   targets, but any preset that modulates LFO rate/depth from an
   envelope silently loses the routing on reload. **Real preset bug.**

4. **Drift regen gating uses magic numbers in one place and enum
   symbols in another.** `PluginProcessor.cpp:641-644` uses symbolic
   enum constants; `SynthPanel.cpp:810-815` hard-codes `tgt == 15`
   and `tgt == 16`. Inserting any target before Noise/Magnitude in the
   drift enum silently breaks regen-button gating.

5. **Ghost indicators partially wired.** Delay and reverb parameters
   publish to `ModulatedValues` atomics
   (`PluginProcessor.cpp:1239-1293`) but no slider reads them. Dead
   publication path.

6. **IPC `timeMs` unit is mis-named.** Python packs
   `elapsed_seconds * 1000` into the field but the C++ UI divides by
   1000 again for display. Net effect: the wire value is neither
   milliseconds nor microseconds as the name suggests. Works in
   practice because both sides agree on the bogus unit, but any
   replacement server must reproduce the quirk exactly.

7. **IPC `flag` field is dead.** Python writes `1`, C++ never reads.

8. **`start_pos` is silently ignored by AudioLDM2 and native backends.**
   The request field is always serialized but only the SA Open diffusers
   path uses it. Preset recall across engines changes behaviour
   invisibly.

9. **`.t5p` version field is written but never read.** Line
   `PresetFormat.cpp:125` has the read commented out. No dispatch, no
   validation, no migration path. First breaking format change will
   silently corrupt old presets.

10. **Sequencer `enabled` export/import asymmetry.** `PluginProcessor.
    cpp:1901` writes it, `:2208-2209` intentionally skips reading.
    Round-trip loses the flag.

11. **`AudioLDM2` is missing from `THIRD_PARTY_LICENSES.txt`.** The
    model is installed and listed in the Manual, but the standalone
    licenses file covers only SA 1.0 and SA Small. Compliance gap.

12. **`trySaSmallInstallFromFolder` is hardcoded to `stable-audio-open-small`**
    (`SetupWizard.cpp:476`). Any future gated Stability model would
    need the pattern generalised.

13. **`backend/routes/` and `backend/server.py` are legacy HTTP
    scaffolding** no longer wired to the plugin. Candidates for
    removal.

14. **`CMakeLists.txt` project version is `0.1.0`** despite the
    v1.0.x tag series. Cosmetic but confusing.

These are all candidates for individual GitHub issues. None are
fixed in this doc-writing session.

### Pass 1 — doc quality review

(Pending — next step is to read each of the five drafted files and
score them against the task descriptions. Then spawn Batch 2.)
