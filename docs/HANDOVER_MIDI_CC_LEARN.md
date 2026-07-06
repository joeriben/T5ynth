# Handover: MIDI CC Learn Implementation

**Project:** T5ynth — `/Users/joerissen/ai/t5ynth`  
**Branch:** `main` (user works directly on main, no PRs)  
**As of commit:** `a5cc6489` (MPE per-note pitch bend, just landed)

---

## What to build

A **MIDI CC Learn** system that lets any of T5ynth's ~217 APVTS parameters be bound to an incoming CC number via right-click → "MIDI Learn". Bindings are saved per-preset in the `.t5p` file. This is unidirectional (controller → T5ynth) for now; MIDI output / LED feedback is a separate future phase.

**Target controller the user mentioned:** Novation Launch Control XL. But the system must be controller-agnostic — the XL is an example, not a constraint.

---

## Architecture decision (already resolved)

**Persistence: bindings live in `.t5p` presets, not globally.**

Rationale: controllers like the Launch Control XL store their own mappings in templates (the user programs the XL with the XL Editor software). T5ynth presets are patch-specific, so the CC bindings belong there too — different presets may want different mappings.

Global bindings (outside presets) are a possible future enhancement but are NOT part of this task.

---

## Relevant existing code

### APVTS
- All parameters: `src/PluginProcessor.cpp` → `createParameterLayout()` (~218 entries).
- Parameter IDs: `src/dsp/BlockParams.h` → `namespace PID` (every `static constexpr const char*`).
- `parameters.getParameter(id)` returns a `juce::RangedAudioParameter*` — usable for both reading the normalised value and writing via `setValueNotifyingHost`.

### Preset format (`.t5p`)
- `src/presets/PresetFormat.h/cpp` — binary format, current version = **4**.
- Save entry point: `PresetFormat::saveToFile(file, processor, ...)`.
- Load entry point: `PresetFormat::loadFromFile(file, processor)`.
- The binary file is a header + FLAC audio blob + JSON section. The JSON section is what you need to extend.
- JSON is built from `processor.exportJsonPreset()` and patched in `saveToFile`. On load, `processor.importJsonPreset(json)` is called. Both live in `PluginProcessor.cpp`.
- **Forwards-compatible pattern already in use:** `hasProperty()` defaults are used throughout `loadFromFile` so old `.t5p` files load gracefully. Follow the same pattern for the new `ccMappings` field.

### MIDI processing
- `PluginProcessor::processBlock()` — the MIDI event loop is at roughly lines 2530–2645 of `PluginProcessor.cpp`.
- The CC handler is the `else if (msg.isController())` branch. CC Learn bindings should be read here and applied via `parameters.getParameter(id)->setValueNotifyingHost(normalised)`.

### GUI component hierarchy
- Main window: `src/gui/MainPanel.cpp`
- All panels: `src/gui/SynthPanel.cpp`, `FxPanel.cpp`, `SequencerPanel.cpp`, `AxesPanel.cpp`, `PromptPanel.cpp`
- Sliders and knobs are `juce::Slider` instances with `SliderAttachment` to APVTS.
- Buttons/toggles are `juce::ToggleButton` / `juce::TextButton` with `ButtonAttachment`.
- There is no shared base class for "learnable controls" — right-click menus will need to be added to individual component types or via a wrapper.

---

## Proposed implementation plan

### 1. Data structure — `CcMapping`

In `PluginProcessor.h`, add:

```cpp
struct CcMapping {
    juce::String paramId;  // APVTS parameter ID (PID::xxx)
    float minNorm = 0.0f;  // normalised range (default 0..1)
    float maxNorm = 1.0f;
};

// Indexed by CC number 0..127. Empty paramId = no binding.
std::array<CcMapping, 128> ccMappings_;

// MIDI Learn state (message-thread only)
std::atomic<bool> midiLearnActive { false };
std::atomic<int>  midiLearnTargetCc { -1 };   // CC that arrived during learn
juce::String      midiLearnParamId;             // param waiting for a CC

void startMidiLearn(const juce::String& paramId);
void cancelMidiLearn();
void applyMidiLearnBinding(int cc);   // called when CC arrives during learn
void clearCcMapping(int cc);
void clearAllCcMappings();
const CcMapping& getCcMapping(int cc) const { return ccMappings_[cc]; }
```

**Thread safety:** `ccMappings_` is read on the audio thread (processBlock) and written on the message thread (learn completion, preset load). Use a `juce::SpinLock` or copy-on-write pattern. Simplest: protect with a `juce::CriticalSection ccMappingLock_` — the audio thread holds it only for the brief array read.

Actually even simpler: since the array is 128 entries of small structs, use `std::atomic_flag`-guarded double-buffer or just a `juce::SpinLock`. Or: keep a secondary `std::array<CcMapping, 128> ccMappingsShadow_` on the message thread, and push a full-copy to the audio thread via an `std::atomic<bool> ccMappingsDirty_` flag that processBlock checks once per block (no per-sample lock).

### 2. Audio thread — apply bindings in processBlock

In the CC handler (`else if (msg.isController())`), after the existing CC checks, add:

```cpp
// CC Learn: intercept during learn mode
if (midiLearnActive.load(std::memory_order_relaxed) && !midiLearnParamId.isEmpty()) {
    // Post to message thread — can't do string ops on audio thread
    // Use an atomic CC number; message thread polls or uses AsyncUpdater
    midiLearnTargetCc.store(cc, std::memory_order_release);
} else {
    // Normal operation: apply any binding
    const auto& mapping = ccMappings_[cc];  // (lock-protected or shadow copy)
    if (mapping.paramId.isNotEmpty()) {
        if (auto* param = parameters.getParameter(mapping.paramId)) {
            const float norm = juce::jmap(static_cast<float>(value7),
                                          0.f, 127.f,
                                          mapping.minNorm, mapping.maxNorm);
            param->setValueNotifyingHost(norm);
        }
    }
}
```

**Caution:** `juce::String` operations on the audio thread are forbidden (heap alloc). The mapping lookup by `paramId` string must NOT happen on the audio thread. Instead, store the mapping as a pre-resolved `juce::RangedAudioParameter*` pointer alongside the paramId, resolved once when a binding is created/loaded.

Revised struct:
```cpp
struct CcMapping {
    juce::String paramId;
    juce::RangedAudioParameter* param = nullptr;  // resolved pointer, null = no binding
    float minNorm = 0.0f;
    float maxNorm = 1.0f;
};
```

Audio thread only touches `param`, `minNorm`, `maxNorm` — no string ops.

### 3. MIDI Learn state machine (message thread)

```
startMidiLearn(paramId)
  → sets midiLearnParamId, midiLearnActive = true
  → GUI highlights the control in "learn" state

processBlock detects incoming CC while midiLearnActive
  → writes cc number to midiLearnTargetCc (atomic)
  → triggers AsyncUpdater (or timer poll)

AsyncUpdater::handleAsyncUpdate()
  → reads midiLearnTargetCc
  → calls applyMidiLearnBinding(cc)
    → creates CcMapping{paramId, resolved param*, 0, 1}
    → stores in ccMappings_[cc]
    → pushes shadow copy to audio thread
  → clears midiLearnActive
  → GUI returns to normal state
```

Use `juce::AsyncUpdater` in `PluginProcessor` for the message-thread callback.

### 4. Preset persistence

In `exportJsonPreset()` / `importJsonPreset()`:

```json
"ccMappings": [
  { "cc": 74, "param": "filter_cutoff", "min": 0.0, "max": 1.0 },
  { "cc": 71, "param": "filter_reso",   "min": 0.0, "max": 1.0 }
]
```

In `saveToFile`: iterate `ccMappings_`, write non-empty entries.  
In `loadFromFile`: read array, resolve `param*` via `parameters.getParameter(id)`, populate `ccMappings_`. Use `hasProperty()` default (empty array) for old presets.

No version bump needed — the JSON section is forwards-compatible already.

### 5. GUI — right-click "MIDI Learn"

**Approach A (minimal):** Override `mouseDown` on each learnable component class and add a `juce::PopupMenu` when it's a right-click. About 5-6 component types need it: `juce::Slider`, `juce::ComboBox`, `juce::ToggleButton`, `juce::TextButton`.

**Approach B (cleaner):** A `LernableComponentMixin` — a thin helper that any component can inherit from, providing `showMidiLearnMenu(processor, paramId)`. Attach it to the existing component classes without changing their core behaviour.

The popup menu should show:
- "MIDI Learn" — starts learn mode for this param
- "Clear CC binding" (only if one exists) — removes it
- "Show current CC: [CCn]" (read-only info, only if one exists)

### 6. Visual feedback during learn mode

When `midiLearnActive` is true, the waiting control should show a distinct visual state (e.g., pulsing border in `kWarning` colour). A simple `juce::Timer` at 200ms intervals can toggle a bool and call `repaint()` on the learning control.

Alternatively: on learn-mode entry, `StatusBar` shows "MIDI Learn: touch a CC on your controller…" and returns to normal on completion/cancel.

---

## What NOT to do

- No global CC binding file — bindings are per-preset only.
- No CC Learn for the sequencer step buttons (those are event-driven, not continuous params).
- Don't add MIDI output / LED feedback in this task — that is a separate phase.
- Don't add a dedicated "MIDI" panel to the GUI — the right-click menu per control is sufficient.
- Don't use `juce::String` on the audio thread for the binding lookup — always use the resolved `param*` pointer.

---

## Files to touch

| File | Change |
|---|---|
| `src/PluginProcessor.h` | `CcMapping` struct, `ccMappings_[]`, learn state, `AsyncUpdater` inheritance |
| `src/PluginProcessor.cpp` | `exportJsonPreset`, `importJsonPreset`, processBlock CC handler, learn methods |
| `src/presets/PresetFormat.cpp` | No change needed (JSON handled by export/import) |
| `src/gui/GuiHelpers.h` | Helper: `showMidiLearnMenu(T5ynthProcessor&, juce::String paramId, juce::Component*)` |
| `src/gui/SynthPanel.cpp` | Right-click on Slider/ComboBox instances |
| `src/gui/FxPanel.cpp` | Right-click on FX sliders |
| `src/gui/AxesPanel.cpp` | Right-click on axis sliders |
| `src/gui/PromptPanel.cpp` | Right-click on duration/alpha sliders |
| `src/gui/StatusBar.cpp` | Learn-mode status message |
| `src/gui/MainPanel.cpp` | Possibly master-vol knob |

---

## Known constraints / gotchas

- `CLAUDE.md`: read ALL relevant files before writing code. Read `SynthPanel.cpp` and `FxPanel.cpp` fully before adding right-click menus — they're large.
- `CLAUDE.md`: stopTimer() in destructor before any member is destroyed (if using Timer for pulsing).
- `CLAUDE.md`: never `setLookAndFeel(nullptr)`.
- `CLAUDE.md`: audio thread safety — no heap alloc, no mutex, no String on audio thread.
- `CLAUDE.md`: one concern per commit.
- No branches, no PRs. Work directly on `main`.
- After every code change, spawn a verification agent (model: opus) with "This code has a bug. Find it."
- Build: `cmake --build build_clean --config Release -j$(sysctl -n hw.ncpu)`
- The user works directly with the build at `build_clean/T5ynth_artefacts/Release/`.

---

## Suggested commit sequence

1. `feat(midi): CcMapping struct + ccMappings_ array in PluginProcessor` — data structure only, no behaviour yet
2. `feat(midi): apply CC bindings in processBlock` — audio thread reads bindings
3. `feat(midi): MIDI Learn state machine + AsyncUpdater` — learn flow
4. `feat(presets): persist CC bindings in .t5p JSON section` — save/load
5. `feat(gui): right-click MIDI Learn menu on learnable controls` — UI
6. `feat(gui): learn-mode status indicator in StatusBar` — feedback
