# Preset Library Maintenance — publishing the "UCDCAE AI Lab" bank

How the published preset bank is maintained. The workflow lives partly
*outside* this repository: the bank's source of truth is the maintainer
machine's **live preset directory**, which is itself a git checkout of the
public distribution repo. Nothing here applies to end users — they get the
bank via the **Update Library** button (`src/presets/PresetUpdater`).

## One directory, two roles

On the maintainer Mac, `~/Library/T5ynth/presets/` is simultaneously

1. the plugin's live, user-writable preset directory (everything the
   preset browser shows), and
2. a git checkout of <https://github.com/joeriben/T5ynth-Presets> — the
   repo the in-app updater downloads from.

There is no separate clone and no copy step. Saving a preset in the plugin
writes directly into the git working tree; `git status` shows it
immediately. Saving into the bank folder under an existing preset's name
**overwrites the tracked file** — that is the intended editing mechanism
(see devlog 2026-05-22, "Bank Collapse": no read-only gating). Every such
save is therefore a publish *candidate*; git review is the publish *gate*.

Path note: the directory is `~/Library/T5ynth/presets/`, **not**
`~/Library/Application Support/…`. `PresetFormat::getUserPresetsDirectory()`
builds on `juce::File::userApplicationDataDirectory`, which resolves to
`~/Library` on macOS, `%APPDATA%\` on Windows, `$XDG_CONFIG_HOME`
(default `~/.config`) on Linux.

## What is tracked, what stays private

| Path | Tracked | Meaning |
|---|---|---|
| `UCDCAE AI Lab/*.t5p` | yes | the published bank — the repo's payload |
| `*.t5p` at the root | no | personal presets / not-yet-published candidates |
| `* (mine).t5p` at the root | no | **pending update** to the same-named bank preset — apply + publish, do NOT treat as private (see below) |
| `manifest.json`, `README.md`, `LICENSE` | yes, **not checked out** | excluded by sparse-checkout (see below) |

The checkout uses `git sparse-checkout` with patterns `*.t5p`, `*.t5seq`,
`scripts`, `.github` — repo furniture (`manifest.json`, `README.md`,
`LICENSE`) is tracked upstream but not materialized in the live directory,
so the app's folder holds only sound files. This is why git reports
"partial checkout (92%)"; it is configuration, not damage.

## "(mine)" forks — maintainer-machine semantics

On end-user installations, saving or tag-editing a preset that lives in
the UCDCAE bank forks it to `<name> (mine).t5p` at the root — correct
there, because the bank copy would be clobbered back to upstream by the
next **Update Library** run. On the maintainer checkout that same rule
only produced stale duplicates of pending updates, so it is disabled in
code: when the presets dir contains `.git`
(`PresetFormat::userPresetsDirIsGitCheckout()`), bank saves prefill the
original name + bank and overwrite directly (via the normal Replace
confirmation), and tag edits patch the bank file in place. Publishing is
then the usual explicit-path add + commit + push.

Any `* (mine).t5p` still sitting at the root is therefore un-applied
backlog from before that change: overwrite the same-named bank file with
the fork's bytes (patching the JSON `name` field back to the suffix-less
name), archive the fork outside `presets/` (never delete), publish.
(Done 2026-06-12 for Echoes of a Laughing Kalimba Gran, frenzy dream,
Evil Beauty → preset-repo commit cdaf5bc.)

## Publishing, step by step

```bash
cd ~/Library/T5ynth/presets
git fetch origin && git status -sb   # 1. ALWAYS fetch first (see pitfalls)
git pull --ff-only                   # 2. take upstream manifest commits
git status --short                   # 3. review: which bank presets changed?
git add "UCDCAE AI Lab/<Name>.t5p"   # 4. stage EXPLICIT paths only
git commit -m "update: <Name>"       # 5. add:/update:/remove: convention
git push origin main                 # 6.
```

After the push, GitHub Actions regenerates `manifest.json` (~30 s; the
workflow is path-filtered to `UCDCAE AI Lab/*.t5p`, so its own commit does
not re-trigger). Users then receive the change via **Update Library**
(diff keyed on `path` + `sha256`; the updater never deletes local files).
Optional hygiene afterwards: `git pull --ff-only` to take the manifest
commit locally.

Removing a preset: `git rm "UCDCAE AI Lab/<Name>.t5p"`, commit, push.
Users who already downloaded it keep their copy (no pruning by design).

## Rules and pitfalls

- **Never `git add -A` / `git add .`** — the repo is public; root-level
  personal presets must never be staged. Explicit paths only.
- **Always fetch before reading `git status`.** The checkout lags origin
  routinely: every push triggers a CI manifest commit, and pushes can
  originate outside this checkout. (2026-06-12 case: 8 presets showed as
  locally "modified" that were in fact the already-published June-1
  model-id fix — the checkout was simply 3 commits behind.)
- **Never hand-edit or locally regenerate `manifest.json` here.** CI owns
  it; under sparse-checkout the file is not even materialized. The
  "manual generation" section in the repo template README applies to
  normal clones only.
- **Never force-push.** `.t5p` is binary — diverged histories on the same
  file cannot be merged, only chosen between.
- **Don't use Update Library on this machine.** `git pull` is the sync
  here; the updater would revert any unpublished local bank edit back to
  upstream (sha256 mismatch → re-download).
- Plugin saves are atomic (`TemporaryFile::overwriteTargetFileWithTemporary`,
  see `docs/PRESET_FORMAT.md`), so a commit cannot capture a half-written
  file even while the app is running.
- The repo's CI workflow and manifest generator originate from
  `scripts/preset-repo-template/` in this repository; change them there
  first, then copy over.
