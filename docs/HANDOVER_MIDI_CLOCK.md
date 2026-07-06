# Handover: MIDI Clock Input

**Project:** T5ynth — `/Users/joerissen/ai/t5ynth`  
**Branch:** `main` (no PRs, no branches)  
**As of commit:** `0ac7d9f7` (XL module-accent LED colours — MIDI output done)

---

## What to build

Sync T5ynth's step sequencer, generative sequencer, arpeggiator, and BPM-synced LFOs/delays to an incoming external MIDI clock (0xF8 tick messages). When an external clock is active and enabled, it overrides the internal Seq BPM and host transport.

---

## How BPM currently works — READ THIS FIRST

Everything goes through one function in `PluginProcessor.cpp`:

```cpp
float T5ynthProcessor::resolveSyncBpm() const   // ~line 3324
{
    if (hostPlayingNow.load(std::memory_order_relaxed))
        return hostBpmLastSeen.load(std::memory_order_relaxed);
    if (seqRunningNow())
        return paramCache.seqBpm->load();
    const float h = hostBpmLastSeen.load(std::memory_order_relaxed);
    return (h > 0.0f) ? h : paramCache.seqBpm->load();
}
```

**All three sequencers and all BPM-synced LFOs/delays read from here.** The APVTS param `PID::seqBpm` is the internal BPM knob value. Host BPM comes from `getPlayHead()` (read in processBlock, stored into `hostBpmLastSeen`).

The new MIDI clock source should become the **highest priority** source in this function:

```cpp
float T5ynthProcessor::resolveSyncBpm() const
{
    // New: external MIDI clock takes priority when enabled and receiving
    if (midiClockEnabled_.load() && midiClockValid_.load())
        return midiClockBpm_.load();
    if (hostPlayingNow.load(std::memory_order_relaxed))
        return hostBpmLastSeen.load(std::memory_order_relaxed);
    if (seqRunningNow())
        return paramCache.seqBpm->load();
    const float h = hostBpmLastSeen.load(std::memory_order_relaxed);
    return (h > 0.0f) ? h : paramCache.seqBpm->load();
}
```

That single change propagates MIDI clock to ALL consumers: step sequencer (~line 2300), generative sequencer (~lines 2036, 2092), arpeggiator (~line 2338), and all BPM-synced LFOs/delays (via `BpmSync::computeRate`/`computeDelayMs` in `BlockParams.h`).

---

## Architecture

### 1. New atomics in PluginProcessor.h (private section)

```cpp
// ── MIDI Clock Input ─────────────────────────────────────────────────────
std::atomic<bool>  midiClockEnabled_ { false };  // user toggle
std::atomic<bool>  midiClockValid_   { false };  // receiving clock ticks?
std::atomic<float> midiClockBpm_     { 120.0f }; // calculated from tick intervals

// Internals — audio thread only, no atomics needed
uint64_t midiClockProcessedSamples_ = 0;   // running sample counter
uint64_t midiClockLastTickSample_   = 0;
std::array<uint32_t, 24> midiClockTickIntervals_ {};  // ring buffer, 24 = 1 beat
int      midiClockTickIdx_          = 0;
int      midiClockTickCount_        = 0;   // how many ticks seen (for warm-up)
```

### 2. MIDI Clock handler in processBlock

In the MIDI event loop, add handlers for the real-time messages (0xF8/0xFA/0xFB/0xFC). These arrive as JUCE `MidiMessage` objects — check with `msg.isMidiClock()`, `msg.isMidiStart()`, `msg.isMidiContinue()`, `msg.isMidiStop()`.

**Important:** JUCE's `MidiBuffer` iterator gives sample offsets within the block. Track absolute sample position:

```cpp
// At the TOP of processBlock, before the MIDI loop — update the counter:
midiClockProcessedSamples_ += static_cast<uint64_t>(buffer.getNumSamples());
```

Then in the MIDI loop:

```cpp
if (msg.isMidiClock() && midiClockEnabled_.load(std::memory_order_relaxed))
{
    const uint64_t tickSample = midiClockProcessedSamples_
                                - static_cast<uint64_t>(buffer.getNumSamples())
                                + static_cast<uint64_t>(sampleOffset);
    if (midiClockLastTickSample_ > 0)
    {
        const uint32_t interval = static_cast<uint32_t>(tickSample - midiClockLastTickSample_);
        midiClockTickIntervals_[midiClockTickIdx_] = interval;
        midiClockTickIdx_ = (midiClockTickIdx_ + 1) % 24;
        if (midiClockTickCount_ < 24) ++midiClockTickCount_;

        if (midiClockTickCount_ >= 4)  // at least 4 ticks before trusting BPM
        {
            // Average over available ticks
            uint64_t sum = 0;
            for (int i = 0; i < midiClockTickCount_; ++i) sum += midiClockTickIntervals_[i];
            const float avgInterval = static_cast<float>(sum) / static_cast<float>(midiClockTickCount_);
            // 24 ticks per beat → BPM = 60 * sampleRate / (avgInterval * 24)
            const float bpm = 60.0f * static_cast<float>(getSampleRate()) / (avgInterval * 24.0f);
            midiClockBpm_.store(juce::jlimit(20.0f, 300.0f, bpm), std::memory_order_release);
            midiClockValid_.store(true, std::memory_order_release);
        }
    }
    midiClockLastTickSample_ = tickSample;
}
else if (msg.isMidiStop())
{
    midiClockValid_.store(false, std::memory_order_release);
    midiClockTickCount_ = 0;
    midiClockLastTickSample_ = 0;
}
else if (msg.isMidiStart() || msg.isMidiContinue())
{
    // Reset on Start (clean restart); Continue keeps ticks but clears warm-up gap
    if (msg.isMidiStart())
    {
        midiClockTickCount_ = 0;
        midiClockLastTickSample_ = 0;
    }
}
```

**Clock loss detection:** if no tick arrives for > 1 second, set `midiClockValid_ = false`. Implement via a `uint64_t midiClockLastTickSample_` check at the top of processBlock: if `midiClockEnabled_` and `midiClockValid_` and `(midiClockProcessedSamples_ - midiClockLastTickSample_) > sampleRate`, clear `midiClockValid_`.

### 3. resolveSyncBpm() — one-liner change

Add the two-line guard at the top of `resolveSyncBpm()` as shown above. That's the only change to the BPM consumers.

### 4. Toggle and UI

Add an **"Ext. Clock"** toggle button in `StatusBar` (same row as the existing MIDI output device selector from the LED feedback session). When toggled on, set `midiClockEnabled_ = true`.

When enabled but `midiClockValid_ = false`: show "Ext. Clock — No Signal" in a subtle warning colour.  
When enabled and `midiClockValid_ = true`: show "Ext. Clock — 120.4 BPM" (reading from `midiClockBpm_`).

Use a `juce::Timer` at 200ms to poll and update the StatusBar label. The label already exists (StatusBar has a timer-driven update loop — check `StatusBar.cpp` for where `timerCallback()` runs and piggyback there).

When external clock is active, the Seq BPM slider in the sequencer panel should visually indicate it is overridden (e.g., greyed or labelled "EXT"). Read `processor.isMidiClockActive()` (add this public getter) from the slider's paint or label update. Do NOT disable the APVTS attachment — just display-only dimming so the value is preserved if the user switches back.

### 5. Public getter

```cpp
// In PluginProcessor.h (public):
bool isMidiClockActive() const
{
    return midiClockEnabled_.load(std::memory_order_relaxed)
        && midiClockValid_.load(std::memory_order_relaxed);
}
float getMidiClockBpm() const { return midiClockBpm_.load(std::memory_order_relaxed); }
void  setMidiClockEnabled(bool e) { midiClockEnabled_.store(e); if (!e) midiClockValid_.store(false); }
bool  isMidiClockEnabled() const  { return midiClockEnabled_.load(); }
```

### 6. Plugin state persistence

The "Ext. Clock enabled" toggle is a device/session preference (not per-preset). Persist in `getStateInformation`/`setStateInformation` alongside the `midiOutputDeviceId_` that was added by the LED feedback session. Same pattern: `state.setProperty("midiClockEnabled", midiClockEnabled_.load(), nullptr)` on save, `setMidiClockEnabled(...)` on load.

---

## What NOT to do

- Don't write BPM back to the `PID::seqBpm` APVTS parameter — leave the knob value intact so it's restored when clock is disabled.
- Don't use a mutex on the audio thread — the atomics above are sufficient.
- Don't add a separate MIDI input device selector — T5ynth already receives all MIDI on its plugin MIDI input. The clock messages arrive in processBlock's MidiBuffer naturally.
- Don't sync note scheduling to the clock's Start/Stop — T5ynth's sequencer runs on its own internal step logic; sync only the BPM.
- Don't add MIDI Clock output — not requested.

---

## Files to touch

| File | Change |
|---|---|
| `src/PluginProcessor.h` | New atomics + audio-thread state + public API |
| `src/PluginProcessor.cpp` | Clock handler in processBlock, `midiClockProcessedSamples_` update, loss detection, `resolveSyncBpm()` guard, `getStateInformation`/`setStateInformation` |
| `src/gui/StatusBar.h/.cpp` | "Ext. Clock" toggle + BPM display, piggyback on existing `timerCallback()` |
| `src/gui/SequencerPanel.cpp` | Seq BPM slider visual override indication when clock active |

---

## JUCE safety / CLAUDE.md constraints

- No allocation on audio thread — all state is atomics and fixed-size arrays.
- No `juce::String` on audio thread — BPM display is built on the message thread from `getMidiClockBpm()`.
- `stopTimer()` in destructor if a Timer is added (piggyback on existing StatusBar timer instead).
- Never `setLookAndFeel(nullptr)`.
- One concern per commit.
- After every code change: spawn verification agent (model: opus) "This code has a bug. Find it."
- Build: `cmake --build build_clean --config Release -j$(sysctl -n hw.ncpu)`

---

## Suggested commit sequence

1. `feat(midi): MIDI Clock input handler + BPM calculation in processBlock`
2. `feat(midi): wire external clock into resolveSyncBpm() with clock-loss detection`
3. `feat(presets): persist Ext. Clock enabled in plugin state`
4. `feat(gui): Ext. Clock toggle + BPM display in StatusBar`
5. `feat(gui): Seq BPM slider dim when external clock active`
