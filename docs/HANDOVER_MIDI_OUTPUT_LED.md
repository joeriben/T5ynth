# Handover: MIDI Output / LED Feedback for CC Learn

**Project:** T5ynth — `/Users/joerissen/ai/t5ynth`  
**Branch:** `main` (no PRs, no branches)  
**As of commit:** `6ea8ed00` (MIDI Learn status indicator — CC Learn fully shipped)

---

## What to build

When CC Learn binds a control to a CC number, send LED feedback to the hardware controller so the user sees which knob is bound. The first target controller is the **Novation Launch Control XL**.

**Scope for this session:**
- Open a MIDI output connection to a user-selected device
- On CC Learn bound → send LED-on in "bound" color (e.g. green)
- On CC Learn active/waiting → send LED-blink or amber color
- On CC clear → send LED-off
- Device selection persists in plugin state (not per-preset — it's a device setting)

**NOT in scope:**
- Continuous value mirroring (knob position → LED brightness) — next phase
- Launch-Control-specific template programming / SysEx device init
- Program Change, MIDI clock, or any other MIDI audit item

---

## Existing hooks to use

Everything you need is already in `PluginProcessor.h`:

```cpp
// Fired on message thread whenever learn state changes.
// learning=true → waiting for a CC
// learning=false, boundCc≥0 → CC was bound (boundCc is the CC number)
// learning=false, boundCc<0 → cancelled
std::function<void(bool learning, int boundCc)> onMidiLearnStateChanged;

// Find which CC number (0-127) is bound to a param. Returns -1 if none.
int findBoundCc(const juce::String& paramId) const;

// All CC mappings (read copy, thread-safe)
CcMapping getCcMappingCopy(int cc) const;
```

The GUI already calls `onMidiLearnStateChanged` when learn state changes (set up in `StatusBar.cpp` and the right-click menu plumbing in MainPanel). You just need to wire a MIDI output send into this callback.

---

## Architecture

### 1. MIDI output in PluginProcessor

Add to `PluginProcessor.h` (private section, after existing CC Learn members):

```cpp
// ── MIDI Output (LED feedback) ───────────────────────────────────────────
std::unique_ptr<juce::MidiOutput> midiOutputDevice_;
juce::String                      midiOutputDeviceId_;   // persisted in plugin state
juce::SpinLock                    midiOutputLock_;       // protects midiOutputDevice_ pointer

void openMidiOutputDevice(const juce::String& deviceId);
void closeMidiOutputDevice();
void sendMidiOutputMessage(const juce::MidiMessage& msg);  // thread-safe
const juce::String& getMidiOutputDeviceId() const { return midiOutputDeviceId_; }
```

`sendMidiOutputMessage()` acquires `midiOutputLock_` briefly, then calls `midiOutputDevice_->sendMessageNow(msg)`. **Must only be called from the message thread** — `MidiOutput::sendMessageNow` is NOT audio-thread-safe.

### 2. LED message construction

Create `src/midi/LaunchControlXLLeds.h` (new file, thin static utility):

```cpp
#pragma once
#include <JuceHeader.h>

struct LaunchControlXLLeds
{
    // LED indices for the 24 knobs (top/mid/bottom row × 8 cols)
    // and 8 faders and 16 send/track buttons.
    // VERIFY THESE against Novation's "Launch Control XL Programmer's Reference"
    // (PDF available on novationmusic.com/downloads → Launch Control XL).
    //
    // The XL sends and receives on MIDI channel 9 (index 8) by default.
    // LED on: NoteOn ch9, note=<index>, vel=<color>
    // LED off: NoteOn ch9, note=<index>, vel=0
    //
    // Color velocities (verify against spec):
    //   0     = off
    //   12    = green low
    //   15    = green high    ← use for "bound"
    //   9     = amber low
    //   10    = amber high    ← use for "learning"
    //   3     = red low
    //   5     = red high
    //
    // The XL has 3 rows of 8 knobs = 24 knobs total.
    // Top row note numbers start at 13, mid at 29, bottom at 45.
    // Faders start at 77. Buttons (focus/control) at 41, 57.
    // Again: VERIFY BEFORE COMMITTING.

    static juce::MidiMessage ledOn(int noteIndex, int colorVelocity)
    {
        return juce::MidiMessage::noteOn(9, noteIndex, static_cast<uint8_t>(colorVelocity));
    }

    static juce::MidiMessage ledOff(int noteIndex)
    {
        return juce::MidiMessage::noteOn(9, noteIndex, static_cast<uint8_t>(0));
    }

    // Map CC number (0-127) to the LED note index on the XL.
    // The XL sends knob CC values: top row = CC13-20, mid = CC29-36, bot = CC45-52,
    // faders = CC77-84. Build the reverse map here.
    // Returns -1 if the CC doesn't correspond to an XL control.
    static int ccToLedIndex(int cc)
    {
        // Top row knobs: CC 13-20 → LED notes 13-20
        if (cc >= 13 && cc <= 20) return cc;
        // Mid row: CC 29-36 → LED 29-36
        if (cc >= 29 && cc <= 36) return cc;
        // Bottom row: CC 45-52 → LED 45-52
        if (cc >= 45 && cc <= 52) return cc;
        // Faders: CC 77-84 → LED 77-84
        if (cc >= 77 && cc <= 84) return cc;
        return -1;
    }
};
```

**IMPORTANT:** The note numbers, CC numbers, and color velocities above are derived from the original Launch Control (not XL) and may be wrong for the XL. Download Novation's "Launch Control XL Programmer's Reference" and verify every constant before committing. The structure of the code is correct; only the magic numbers need checking.

### 3. Wiring learn callbacks → LED messages

In `PluginProcessor.cpp`, in the constructor (or in a new `initMidiLearnCallbacks()` method called from the constructor), assign `onMidiLearnStateChanged`:

```cpp
onMidiLearnStateChanged = [this](bool learning, int boundCc)
{
    if (!midiOutputDevice_)
        return;

    if (learning)
    {
        // The param waiting is midiLearnParamId.
        // We don't yet know which CC it'll land on — light nothing,
        // or light all un-bound XL controls amber to indicate "touch one".
        // Simple approach: just let StatusBar handle the visual; no LED yet.
        return;
    }

    if (boundCc >= 0)
    {
        // A binding was just made.
        const int led = LaunchControlXLLeds::ccToLedIndex(boundCc);
        if (led >= 0)
            sendMidiOutputMessage(LaunchControlXLLeds::ledOn(led, 15)); // green hi
    }
};
```

For "CC cleared" (`clearCcMapping(cc)`): call `sendMidiOutputMessage(LaunchControlXLLeds::ledOff(led))` inside `clearCcMapping()` itself, before the spinlock modify.

### 4. Device selection UI

Add a device picker to `StatusBar` (rightmost area, after the existing elements). Keep it minimal: a `juce::ComboBox` that lists `juce::MidiOutput::getAvailableDevices()` with a "No output" entry at index 0.

On selection change → calls `processor.openMidiOutputDevice(deviceId)`.

On plugin startup: if `midiOutputDeviceId_` is set (restored from `getStateInformation`), auto-open the device (with graceful fallback if it's not available).

### 5. Plugin state persistence (NOT preset persistence)

CC Learn bindings live in `.t5p` presets. The MIDI output device selection is per-installation, not per-patch — it goes in JUCE's own state via `getStateInformation` / `setStateInformation`.

In `getStateInformation()` (already writes APVTS state):
```cpp
// Append MIDI output device ID after the existing APVTS xml
state.setProperty("midiOutputDeviceId", midiOutputDeviceId_, nullptr);
```

In `setStateInformation()`:
```cpp
const auto deviceId = state.getProperty("midiOutputDeviceId", "").toString();
if (deviceId.isNotEmpty())
    openMidiOutputDevice(deviceId);
```

---

## Files to touch

| File | Change |
|---|---|
| `src/PluginProcessor.h` | `midiOutputDevice_`, `midiOutputDeviceId_`, `midiOutputLock_`, method declarations |
| `src/PluginProcessor.cpp` | `openMidiOutputDevice`, `closeMidiOutputDevice`, `sendMidiOutputMessage`, learn callback wiring, `getStateInformation`/`setStateInformation` extension, `clearCcMapping` LED-off call |
| `src/midi/LaunchControlXLLeds.h` | New file — LED index/color mapping (verify all constants!) |
| `src/gui/StatusBar.h/.cpp` | MIDI output device ComboBox |

---

## Known constraints / gotchas

- `MidiOutput::sendMessageNow()` is NOT audio-thread-safe — only send from message thread.
- `juce::MidiOutput::openDevice()` can block — call from message thread, not in processBlock.
- If the XL is unplugged: `sendMessageNow` will silently fail or throw; wrap in try/catch or check `isConnected()` (JUCE 7+).
- `CLAUDE.md`: `stopTimer()` before any member destroyed (if you add a Timer for blink).
- `CLAUDE.md`: no `setLookAndFeel(nullptr)`.
- `CLAUDE.md`: one concern per commit.
- No branches, no PRs. Work directly on `main`.
- After every code change: spawn a verification agent (model: opus) with "This code has a bug. Find it."
- Build: `cmake --build build_clean --config Release -j$(sysctl -n hw.ncpu)`
- **Verify the XL LED protocol** before shipping. The constants in `LaunchControlXLLeds.h` are best-guess — the programmer reference PDF is the authoritative source. Test physically on the device.

---

## Suggested commit sequence

1. `feat(midi): MidiOutput device open/close in PluginProcessor` — device management, no UI yet
2. `feat(midi): persist MIDI output device in plugin state` — getState/setState
3. `feat(midi): LED feedback on CC Learn bind/clear` — wire onMidiLearnStateChanged
4. `feat(midi): LaunchControlXLLeds — CC→LED index + color constants` — new header (verify constants first!)
5. `feat(gui): MIDI output device selector in StatusBar` — UI

---

## What comes after (not in scope here)

- Continuous value mirroring: on APVTS param change → send LED brightness proportional to param value. Requires APVTS listener per bound param — non-trivial (128 potential listeners, start/stop on bind/unbind).
- Program Change → preset recall (separate MIDI audit item).
- MIDI clock input (separate MIDI audit item).
