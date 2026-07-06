# Handover — Filter Oversampling (Ladder / Warp)

Status: **DONE** (2026-06-21). Teil A `164d8777` (DSP) · Teil B `09837075` (global
store) · Teil C `04e1271c` (tabs + dropdown). Kbd-track prerequisite `b25dd960`.
Built green on the combined HEAD — coexists cleanly with the parallel cutoff-bus
refactor `631e5090` (orthogonal: modulation *value* vs filter *rate*). Each commit
adversarially reviewed (Teil A review caught the prepare()-desync bug, fixed).
Remaining: the in-app A/B ear-check (Off/2×/4× via the new Settings tab).

## Problem & evidence

The nonlinear filters (Ladder, Warp) "sound bad" — confirmed as **aliasing**. They
carry saturation/waveshaping *inside the feedback loop* and run at base sample
rate, so harmonics generated above Nyquist fold back into the passband. The
linear SVF doesn't generate harmonics, doesn't alias, and sounds fine.

Measured offline against the real filter classes (`tools/filter_os_test/`,
pure 9 kHz sine into a driven resonant filter — every spurious line is the
filter's own aliasing):

| Oversample | Alias/signal (dB, ↓ = cleaner) | CPU worst-case¹ (% of one core) |
|---|---|---|
| 1× (today) | **−27.8** (audible, in-passband: ghost tones at 3/15/21 kHz) | 7.4 % |
| **2×** | **−67.2** (inaudible) | **14.8 %** |
| 4× | −138 (numerical floor) | 29.6 % |
| 8× | −136 (floor) | 59.2 % |

¹ M3 Max, 16 voices × stereo (freeze worst case; mono = half). Scales up on
slower Macs — a reason to make it a setting, not forced-on.

**Decision: default 2×** (40 dB win, inaudible alias, ~⅓ the cost of 4×; same
default Vital ships). Setting `Off / 2× / 4×`, **stored globally** (machine-wide,
not per-preset — it's a CPU/quality tradeoff, not a sound-design choice).

Reproduce: `cd tools/filter_os_test && ./run.sh` (renders WAVs + spectra + bench).

## Architecture note (why this isn't a one-liner)

Phase B drive oversampling ([SynthVoice.cpp:941](../src/dsp/SynthVoice.cpp:941))
runs **only for SVF** — for Ladder/Warp it's a no-op (their drive goes inward via
`setInputDrive`). So the existing `driveOsNx_` oversamplers hang off the one
filter that doesn't need them, and the two that do have none. But those OS
instances are **free** whenever the algorithm is Ladder/Warp (Phase B skipped),
so we reuse them. `MoogLadderFilter::prepare()` only sets `sr`/`reset`/coeffs —
**allocation-free**, therefore safe to re-`prepare` at the oversampled rate from
the audio thread when the factor changes.

## Teil A — DSP (the core)

1. **BlockParams**: add `int filterOsFactor = 2;` (1 = Off, 2, 4). Filled in
   `processBlock` from the processor's global value (Teil B).
2. **Voice prepare-at-N×**: track `preparedOsFactor_` per voice. In Phase C, if
   `algo ∈ {Ladder,Warp}` and `bp.filterOsFactor != preparedOsFactor_`, re-`prepare`
   `filterLadder/Warp(+R)` at `sr × factor` and store it. Self-correcting, no
   separate change-detection path, allocation-free.
3. **OS region around Phase C** ([SynthVoice.cpp:994](../src/dsp/SynthVoice.cpp:994)):
   for Ladder/Warp with `factor > 1`, wrap the per-sample loop —
   `driveOs{2,4}x_->processSamplesUp(block)` → `filter.processSample` per
   oversampled sample (ch0 → `filterLadder`, ch1 → `filterLadderR`) →
   `processSamplesDown(block)`. Mirror today's stereo/mono handling
   (mono syncs R-state). `reset()` the OS instance on algorithm switch (SVF↔nonlinear)
   so stale IIR state doesn't glitch.
4. **Coefficients unchanged**: `setCutoff(cutoffMod)` at the sub-block boundary
   already produces correct `g` because the filter is prepared at `sr × factor`.
   No param-math change. The 20 kHz clamp ([SynthVoice.cpp:715](../src/dsp/SynthVoice.cpp:715)) stays.
5. **Cost is conditional**: SVF and `Off` pay exactly nothing.
6. **Latency**: the IIR half-band OS (already used for drive) is low-latency and
   not reported as plugin latency today (per-voice, conditional). Consistent;
   sub-millisecond; acceptable for a synth voice path. Verify, don't over-engineer.

## Teil B — Global settings store (NEW infra)

- Add a `juce::PropertiesFile` at `~/Library/Application Support/T5ynth/settings.xml`
  (matches the `userApplicationDataDirectory/"T5ynth"` convention used throughout).
- Processor owns `std::atomic<int> filterOsQuality_{1}` (index: 0=Off,1=2×,2=4×;
  default 2×). Loaded from the file at construction; `processBlock` maps it to
  `bp.filterOsFactor`. **Never read the file on the audio thread** — only the atomic.
- Setter `setFilterOsQuality(int)` writes the file **and** updates the atomic.
- This is global on purpose: survives preset changes and is shared across
  plugin instances reading the same file.

## Teil C — UI: SettingsPage → tabs

The current `SettingsPage` ([SetupWizard.h:20](../src/gui/SetupWizard.h:20)) is the
**Model Manager**; it's a direct member of MainPanel
([MainPanel.h:302](../src/gui/MainPanel.h:302), `getModelPanel()`).

- Introduce a `juce::TabbedComponent` wrapper holding:
  - **Tab 0 "Modelle"** — the existing `SettingsPage`. **First and default-selected**
    (`setCurrentTabIndex(0)`).
  - **Tab 1 "Settings"** — a new `GeneralSettingsPage` for global options.
- Replace the `SettingsPage settingsPage;` member with the tab wrapper; keep
  `getModelPanel()` returning the models page for existing callers.
- `GeneralSettingsPage`: a `ComboBox` "Filter Oversampling: Off / 2× / 4×"
  bound to the processor's `setFilterOsQuality`, initialized from the stored
  value. Room for future global settings here.

## Teil D — Verification (before each commit; audition before the DSP commit)

1. Offline harness re-run against the integrated build (alias gone).
2. CPU: `sample(1)` under full 16-voice polyfony (PERFORMANCE_GUIDE §4); confirm
   worst-case headroom; note slower-Mac scaling.
3. xrun monitoring in the standalone under load.
4. **Ear check**: `Off` vs `2×` on a resonant Ladder/Warp patch (the whole point).

## Sequence (one concern per commit)

1. **Teil A** — DSP OS region + prepare plumbing, factor from a temporary
   processor member defaulting to 2×. Ear + alias verification.
2. **Teil B** — global PropertiesFile store + atomic; wire member to it.
3. **Teil C** — SettingsPage tabs + `GeneralSettingsPage` dropdown.

## Risks / open details

- Low overall: SVF untouched, `Off` = exact status quo, no param-math change.
- `driveOsNx_` reuse vs dedicated `filterOsNx_` instances — reuse is memory-lean
  and safe (mutually exclusive per block) but needs `reset()` on algo switch;
  dedicated is simpler but +3 OS instances × 16 voices of RAM. Start with reuse.
- Confirm the JUCE Oversampling instances are sized/`initProcessing`-d for the
  Phase-C block length (they're `SUB_BLOCK_SIZE` today — matches).
