# Adding a Modulation Target

Audience: C++/JUCE developer who needs to make a new DSP parameter reachable
from the T5ynth modulation routing system.

Prerequisites: you already know where the DSP parameter you want to modulate
lives (an APVTS parameter, a member of `BlockParams`, or a direct field on a
DSP object), and you have a working `build_clean/` Release build.

This document describes the current (rc-era) routing layout. It is
**decentralised, repetitive, and fragile around enum ordering**. Read the
whole file before you touch anything.

---

## 1. The routing model in one paragraph

Every modulation source owns its own `Target` dropdown (a JUCE
`AudioParameterChoice`). There are three envelopes (internally `amp`, `mod1`,
`mod2`), two global LFOs (`lfo1`, `lfo2`), and three drift LFOs (`drift1-3`).
At the top of `processBlock` the selected index of each source is snapshotted
into a `BlockParams` field (`mod1Target`, `lfo1Target`, …) and the per-sample
/ per-sub-block DSP code then fans out with a chain of
`if (bp.mod1Target == EnvTarget::Filter) cutoffMod *= …` statements. There is
no central `ModulationMatrix::dispatch()` — `src/dsp/ModulationMatrix.{h,cpp}`
is a stub (see `src/dsp/ModulationMatrix.cpp:13-16`). Adding a target means
touching the APVTS layout, the `BlockParams`/target enums, every site in the
DSP code that reads `*Target`, and the GUI dropdown lists.

Modulated sliders show an orange "ghost" dot rendered by `SliderRow::paint`
in `src/gui/GuiHelpers.h:139-150`. The ghost value is published from the
audio thread into `T5ynthProcessor::modulatedValues` (see
`src/PluginProcessor.h:212-233`) and read by the GUI timer in
`src/gui/SynthPanel.cpp:694-705`. A new target will only get a ghost dot if
the destination uses `SliderRow` **and** the GUI reads a matching
`ModulatedValues` atomic.

---

## 2. Three separate target enumerations (important)

Contrary to what the phrase "dropdown-per-source" might suggest, the three
source classes do **not** share a single enum. They share the *concept* of
"index 0 = None" and not much else. The three enumerations are:

### 2.1 Envelope targets (`mod1_target`, `mod2_target`)

Declared in `src/PluginProcessor.cpp:294-304`:

```
0="---"  1=DCA  2=Filter  3=Pitch  4=Dly Time  5=Dly FB
6=Dly Mix 7=Rev Mix 8=Noise Lvl 9=LFO1 Rate 10=LFO1 Depth
11=LFO2 Rate 12=LFO2 Depth 13=LFO3 Rate 14=LFO3 Depth 15=Scan
```

Mirrored as compile-time constants in `src/dsp/BlockParams.h:5-21`
(`namespace EnvTarget`). Mirrored *again* in the GUI dropdown item list at
`src/gui/SynthPanel.cpp:28-29`.

The amp envelope has no target parameter — it is hard-wired to DCA. The
serialisation code in `src/PluginProcessor.cpp:1797` explicitly encodes this
with an empty target id string for index 0 and only serialises targets for
`mod1`/`mod2`.

### 2.2 LFO targets (`lfo1_target`, `lfo2_target`)

Declared in `src/PluginProcessor.cpp:305-314`:

```
0="---"  1=Filter  2=Scan  3=Pitch  4=Dly Time  5=Dly FB
6=Dly Mix 7=Rev Mix 8=ENV1 Amt 9=ENV2 Amt 10=ENV3 Amt
```

Mirrored as `namespace LfoTarget` in `src/dsp/BlockParams.h:23-37`.
Mirrored again in `src/gui/SynthPanel.cpp:99-100`.

Note that this list is a **subset** of the envelope list (no DCA, no LFO
self-modulation) and the indices therefore do **not** line up with
`EnvTarget`. `EnvTarget::Filter == 2` but `LfoTarget::Filter == 1`.

### 2.3 Drift LFO targets (`drift1_target`, `drift2_target`, `drift3_target`)

Declared in `src/PluginProcessor.cpp:221-241` (Drift 1 and 2 in one block,
Drift 3 appended later with the same list):

```
0="---"  1=Alpha  2=Axis 1  3=Axis 2  4=Axis 3  5=WT Scan
6=Filter 7=Pitch 8=Dly Time 9=Dly FB 10=Dly Mix 11=Rev Mix
12=ENV1 Amt 13=ENV2 Amt 14=ENV3 Amt 15=Noise 16=Magnitude
```

Mirrored as `enum DriftLFO::Target` in `src/dsp/DriftLFO.h:21-41`.
Mirrored again in `src/gui/SynthPanel.cpp:137-139`.

Drift is the only source that can target pre-inference generation parameters
(Alpha, the three semantic axes, Noise, Magnitude) because those targets
require re-running inference to take audible effect.

**When you add a target, figure out which of the three enumerations is
affected.** Some targets are only meaningful for a subset of sources. For
example, "DCA" is only on the envelope list because only an envelope ever
acts as a VCA. "Alpha" is only on the drift list because the fast envelopes
and LFOs would require continuous re-inference (not possible in realtime).

---

## 3. Where the DSP actually reads the target

There is no central dispatch. Modulation is applied at five distinct
locations, each with its own calling convention.

### 3.1 Per-sample, per-voice (envelopes + LFOs, audio-rate targets)

`src/dsp/SynthVoice.cpp:125-233` (`renderSample`) and
`src/dsp/SynthVoice.cpp:235-383` (`renderBlock`). Both contain explicit
`if (p.mod1Target == EnvTarget::X) …` chains for:

- Pitch: `SynthVoice.cpp:144-149`, `:322-328`
- Scan (wavetable): `SynthVoice.cpp:164-172`, `:337-343`
- DCA (VCA): `SynthVoice.cpp:182-184`, `:357-361`
- Filter cutoff: `SynthVoice.cpp:196-214`, `:280-289`

`renderBlock` is the production path; `renderSample` is kept around for a
legacy/unit-test call site. **Update both** when you add a new audio-rate
envelope or LFO target.

Drift offsets for filter, pitch, and scan are pre-computed in
`PluginProcessor::processBlock` and passed in as block-rate scalars
(`bp.driftFilterOffset`, `bp.driftPitchOffset`, `bp.driftScanOffset`) set at
`src/PluginProcessor.cpp:664-666`; `SynthVoice.cpp` consumes them alongside
the env/LFO contributions.

### 3.2 Block-rate in `processBlock` (envelopes + LFOs → delay / reverb / LFO
cross-mod)

`src/PluginProcessor.cpp:1077-1102` is the block-rate dispatch for delay
time/feedback/mix and reverb mix modulation. It uses the envelope values
captured during voice rendering (`voiceOut.lastMod1Val/lastMod2Val`) and the
last sample of the global LFO buffers.

`src/PluginProcessor.cpp:1087-1094` additionally handles envelope → LFO
cross-modulation (mod1/mod2 targeting LFO1/2 rate/depth).

`src/PluginProcessor.cpp:1003-1013` handles LFO → envelope-amount
cross-modulation (LFO1/2 targeting ENV1-3 Amt).

### 3.3 Drift LFO dispatch (global)

Drift is a separate engine. Routing lives entirely in `DriftLFO`.

- Target enum: `src/dsp/DriftLFO.h:21-41`
- Per-target half-range (modulation depth in parameter-native units):
  `src/dsp/DriftLFO.cpp:26-48`
- Offset accumulator: `src/dsp/DriftLFO.cpp:74-92`
  (`getOffsetForTarget` sums contributions from the three internal LFOs
  whose `.target` field matches)

The processor then pulls the offset out for each drift-capable destination:

- Scan / Filter / Pitch → `BlockParams` (`PluginProcessor.cpp:664-666`)
- Env1/2/3 Amount → additive to `bp.ampAmount`/`mod1Amount`/`mod2Amount`
  (`PluginProcessor.cpp:670-672`)
- Delay time/FB/mix and reverb mix → `modDelay*`/`modReverbMix`
  (`PluginProcessor.cpp:969-972`)
- Alpha / Axis 1-3 / Noise / Magnitude → `modulatedValues.drift*` atomics
  for the GUI (`PluginProcessor.cpp:677-701`). These are pre-inference
  targets: the drift offset is written to the ghost and, if an oscillator
  target is set and regen mode is active, a new inference run is triggered
  (see `driftHasOscTarget` logic at `PluginProcessor.cpp:641-644`).

The "does this drift target force oscillator regeneration" check is
duplicated in two places and must be kept in sync:

- Audio thread: `PluginProcessor.cpp:641-644`
  (`t >= DriftLFO::TgtAlpha && t <= DriftLFO::TgtAxis3 || t == TgtNoise ||
  t == TgtMagnitude`)
- GUI regen-button enable: `SynthPanel.cpp:810-824` (hard-coded as
  `tgt >= 1 && tgt <= 4) || tgt == 15 || tgt == 16` — **note the magic
  numbers, not enum symbols**)

If you append a new drift target that requires regeneration you must update
both conditions.

### 3.4 Ghost-indicator publishing

`src/PluginProcessor.cpp:1239-1293` builds the modulated-value snapshot
after each block and stores NaN when nothing is currently modulating the
destination. This is where "is this target active" gets translated into
"should the orange dot be visible". The struct is
`T5ynthProcessor::ModulatedValues` at `src/PluginProcessor.h:212-233`.

### 3.5 Ghost-indicator consumption

`src/gui/SynthPanel.cpp:694-705`. Only a small subset of modulated values
are actually wired to a `SliderRow::setGhostValue` call: `cutoffRow`,
`lfo1/lfo2 rate/depth`, and `waveformDisplay.setScanPosition`. Delay/reverb
ghost values are *populated* by the audio thread but are not currently read
by any GUI component — a pre-existing gap, not something your change caused.

The drift → Alpha / Axes / Noise / Magnitude ghost path is a second system
in `PromptPanel.cpp` and `AxesPanel.cpp` which read
`processorRef.modulatedValues.driftAlpha` etc. directly; see
`PromptPanel.cpp:259`.

---

## 4. Preset serialisation — known broken for env/LFO targets

`.t5p` presets are a T5YN-header wrapper around a JSON blob (see
`src/presets/PresetFormat.cpp:9-101`). The JSON blob is produced by
`T5ynthProcessor::exportJsonPreset` (`PluginProcessor.cpp:1749-1963`) and
consumed by `importJsonPreset` (`PluginProcessor.cpp:1965-…`).

Targets are serialised **as strings**, not as raw indices:

- Envelopes: `envTargetToString` / `envTargetFromString`
  (`PluginProcessor.cpp:1590-1604`)
- LFOs: `lfoTargetToString` / `lfoTargetFromString`
  (`PluginProcessor.cpp:1606-1624`)
- Drift: `driftTargetToString` / `driftTargetFromString`
  (`PluginProcessor.cpp:1642-1667`)

This is the good news: appending a new target at the end of the APVTS
enumeration does not break existing binary presets, because presets round-
trip through a stable string identifier.

The bad news:

1. **`envTargetToString` and `lfoTargetToString` are stale.** Their string
   tables were written for an earlier APVTS layout and have not been updated
   to the current `EnvTarget`/`LfoTarget` enums. The index → string mapping
   does not match the APVTS index → name mapping. For example,
   `envTargetToString(0)` returns `"dca"` even though APVTS index 0 is
   `"---"` (None). Save/load still round-trips for any index in range
   because the stale mapping is a self-consistent bijection, but the stored
   string is lying about what the slot actually means. LFO1/2 rate and
   depth targets (`EnvTarget::LFO1Rate` = 9 … `LFO2Depth` = 12) are **out
   of range** of `envTargetToString`'s 9-entry table and will round-trip as
   "none".
2. **`driftTargetToString` is correct** and matches `DriftLFO::Target`.

When you add a new target:

- For drift: extend `driftTargetToString`/`driftTargetFromString` at the end
  of their tables, matching the new enum value. Preserve existing strings.
- For envelopes/LFOs: **do not append to the broken tables without first
  fixing them**. The safest minimal change is to rewrite both pairs to be
  1:1 with the current `EnvTarget`/`LfoTarget` enums and bump a preset
  version key so old presets (which used the stale mapping) can still be
  read through a legacy code path. This is out of scope for a "just add
  one target" change and should be handled as its own refactor.

If your new target is envelope/LFO reachable but you want to ship without
fixing the preset mapping, the target will **not** persist correctly across
save/load. Document this in your PR.

---

## 5. Hard rule: append targets, never insert

Even though presets are stored as strings, several other systems rely on
numeric ordering:

- `DriftLFO::halfRangeForTarget` (`DriftLFO.cpp:26-48`) is a switch on the
  target index. New targets get a new case and `NumTargets` moves, but
  existing cases stay valid.
- `SynthPanel::updateVisibility` (`SynthPanel.cpp:810-815`) encodes the
  oscillator-target range with magic numbers (`tgt >= 1 && tgt <= 4`,
  `tgt == 15`, `tgt == 16`). Inserting a new target in the middle of the
  Alpha/Axes range or before Noise/Magnitude will silently break the
  regen-button gating.
- The `BlockParams` enums are raw integers loaded from APVTS
  `AudioParameterChoice` indices. Reordering the APVTS `StringArray`
  without updating the enum constants will cause every DSP-side comparison
  to target the wrong destination.
- The DAW state-file path (JUCE default `getStateInformation`) serialises
  the raw APVTS indices, not strings. If a preset is saved through a
  DAW (not through `PresetFormat`) and the enumeration is later reordered,
  the preset will route to the wrong destination on reload.

**Rule: new targets go at the end of the list. Never insert, never reorder,
never renumber.**

If a reorder is unavoidable (e.g. to group related targets), you must bump
the APVTS parameter version tag (the second field of `juce::ParameterID`,
currently `1` for target params) and write a migration in
`importJsonPreset` and in the DAW state path.

---

## 6. Step-by-step: add a new envelope/LFO target `MyNewTarget`

Assume the new target is a float DSP parameter you want both envelopes and
LFOs to be able to modulate. Skip steps that are not relevant to your
source class.

1. **APVTS StringArray.** Append `"MyNewTarget"` to the end of the
   `StringArray` in the relevant parameter declarations:
   - Envelope: `PluginProcessor.cpp:298-300` and `:302-304`
     (both `mod1_target` and `mod2_target`, identical lists).
   - LFO: `PluginProcessor.cpp:308-310` and `:312-314`
     (both `lfo1_target` and `lfo2_target`, identical lists).
   - Drift: `PluginProcessor.cpp:224`, `:227`, `:238`
     (all three drift target params, identical lists).
   The new index will be `previous_max + 1`. Record it.

2. **BlockParams enum constant.** Append to `EnvTarget` or `LfoTarget` in
   `src/dsp/BlockParams.h:5-37`:
   ```cpp
   namespace EnvTarget { enum : int { …, LFO2Depth = 12, MyNewTarget = 13 }; }
   ```
   For drift, extend `enum DriftLFO::Target` in
   `src/dsp/DriftLFO.h:21-41` and keep `NumTargets` as the last entry.

3. **GUI dropdown lists.** Append `"MyNewTarget"` to the matching
   `addItemList` calls in `SynthPanel.cpp`:
   - Envelope: `:28-29`
   - LFO: `:99-100`
   - Drift: `:137-139`
   The strings must match the APVTS strings exactly so the
   `ComboBoxParameterAttachment` can resolve them. Mismatch here is silent
   — the dropdown shows one thing, the parameter reports another.

4. **DSP dispatch.** Decide where the modulation is applied:
   - Per-voice audio-rate: add `if (p.mod1Target == EnvTarget::MyNewTarget)
     …` to the appropriate block in `src/dsp/SynthVoice.cpp:235-383`
     (`renderBlock`). Update `renderSample` as well if the target makes
     sense there. You likely also need a new field on `BlockParams` for
     the base value and a new `bp.xxx = parameters.getRawParameterValue(...)`
     read in `PluginProcessor.cpp` around line 547.
   - Block-rate FX or LFO cross-mod: add to
     `src/PluginProcessor.cpp:1077-1102`.
   - Drift target: add the `halfRangeForTarget` case in
     `src/dsp/DriftLFO.cpp:26-48`, then a new call site in
     `PluginProcessor::processBlock` where you pull the offset via
     `driftLfo.getOffsetForTarget(DriftLFO::TgtMyNewTarget)` and fold it
     into whatever carries the base value (follow the pattern at
     `PluginProcessor.cpp:664-701` or `:966-972`).

5. **Ghost indicator (optional but expected for consistency).**
   - If the destination slider is a `SliderRow`, add a new
     `std::atomic<float>` to `ModulatedValues` in
     `src/PluginProcessor.h:212-233`.
   - Publish a value from the audio thread in
     `src/PluginProcessor.cpp:1239-1293`, following the `filterCutoff`
     pattern: NaN when nothing is modulating, current effective value
     otherwise.
   - Read it in `SynthPanel.cpp:694-705` and call
     `mySliderRow->setGhostValue(mv.myNewValue.load(std::memory_order_relaxed))`.
   - Ensure the slider is constructed via `SliderRow`, not a raw
     `juce::Slider`, or the ghost will not render. The orange circle is
     drawn by `SliderRow::paint` (`GuiHelpers.h:139-150`); there is no
     LookAndFeel hook involved.

6. **Drift regen gating** (only if your new drift target requires pre-
   inference regeneration — Alpha / Axes / Noise / Magnitude style):
   - Update the oscillator-target predicate in
     `PluginProcessor.cpp:641-644`.
   - Update the hard-coded magic numbers in
     `SynthPanel.cpp:810-815`. Yes, it's two places. No, they do not
     reference each other.

7. **Preset serialisation.**
   - Drift: extend `driftTargetToString` / `driftTargetFromString` at
     `PluginProcessor.cpp:1642-1667`. Preserve existing strings.
   - Envelope/LFO: see the caveat in section 4. The stale mapping tables
     mean a clean "just append" is not possible without a refactor. If
     you are only adding an env/LFO target and do not want to fix the
     preset format in the same PR, accept that presets will not persist
     the new target and note this in your PR description.

8. **Rebuild and verify.**
   - `cmake --build build_clean --config Release` (per project convention
     — never create an alternate build directory).
   - Launch the standalone app.
   - Select the new target from every source dropdown that should expose
     it. Confirm the modulation is audible and that the slider's ghost
     dot appears if you wired one.
   - Save a preset. Reload it. Confirm the target index survives.
   - Load an existing preset that uses an older target (e.g. the DEMO
     preset) and confirm it is not regressed — specifically that no
     existing target now resolves to the wrong destination, which would
     indicate an enum index drift.
   - For drift targets: toggle Auto-regen on and verify that oscillator
     regeneration triggers (or doesn't, depending on your target).

9. **Documentation.** Update the user-facing guide at
   `resources/T5ynth_Guide.html`:
   - Envelope target list: line 566
     (`<tr><td><strong>Target</strong></td>…`)
   - LFO target list: line 587
   - Drift target list: line 631
   These are plain `<td>` enumerations, edit in place.

---

## 7. Gaps and known complications

These are pre-existing issues, not things you need to fix in your patch,
but you should know about them because they shape what "it works" means.

- **`ModulationMatrix` is a stub.** `src/dsp/ModulationMatrix.cpp:13-16`
  is a no-op `process()`. Do not expect to add your target here. There is
  no central dispatcher.
- **Three parallel target enumerations.** APVTS `StringArray`, `BlockParams`
  enum namespace, and GUI `addItemList` call — all must be kept in sync
  manually. There is no single source of truth.
- **Preset env/LFO target strings are stale** (section 4). Any new
  envelope or LFO target will not persist correctly until the string
  tables are rewritten to match the current enum layout.
- **Drift regen gating is duplicated** as magic numbers
  (`SynthPanel.cpp:810-815` vs `PluginProcessor.cpp:641-644`) and as enum
  symbols. Easy to forget one of the two.
- **Ghost indicators are partial.** Delay, reverb, and several other
  modulated parameters publish to `ModulatedValues` but nothing reads
  them. Your new target may look correctly ghosted in one place and
  completely silent in another.
- **`renderSample` and `renderBlock` both exist** and both need updating
  for any per-voice audio-rate change. `renderBlock` is the live path.
- **Three drift target declarations.** Drift 1/2 are declared together at
  `PluginProcessor.cpp:223-227`, Drift 3 is tacked on separately at
  `:236-238` with the same list — easy to update one and miss the other.
- **DAW state serialisation** uses raw APVTS indices (JUCE default); only
  the `PresetFormat` path routes through the string tables. Any reorder
  of the APVTS enumeration will silently corrupt DAW-saved sessions.

---

## 8. Testing checklist

Minimum acceptable testing for a new target:

- [ ] Appears in every source dropdown where it should be selectable.
- [ ] Does not appear in any source dropdown where it should not be
      selectable (e.g. DCA should never be on the LFO list).
- [ ] With modulation depth > 0, the destination parameter audibly changes.
- [ ] Ghost indicator appears on the destination slider (if wired).
- [ ] Save a preset that uses the new target; reload it; confirm the
      target selection survives.
- [ ] Load the project DEMO preset and confirm no existing targets have
      shifted to the wrong destination.
- [ ] (Drift oscillator targets only) Auto-regen triggers when the drift
      LFO crosses its re-inference threshold.
- [ ] Build is clean: `cmake --build build_clean --config Release` with
      no new warnings.
