# Performance Guide

A practical reference distilled from the May 2026 GUI/DSP optimization
campaign. It catalogues the concrete anti-patterns that showed up in
T5ynth, why each one was expensive, the fix that landed, and what to
look for when adding new code so the same regressions don't reappear.

The campaign reduced idle CPU from **17–20 %** to **2.6–5 %** and
stress-load CPU (16-voice WT, 300 BPM) from **70–85 %** to **35–63 %**
across 14 single-concern commits (`172d7451..b1b3331e`).

---

## 1. The macOS paint cost model

The single most important fact for GUI work on macOS:

> **Every `Component::repaint()` call schedules a deferred AppKit
> paint pass. The pass is dispatched through
> `CFRunLoopWakeUp → CA::Transaction::commit → NSViewBackingLayer
> display → paintComponentAndChildren` and re-paints the *entire*
> backing layer, not just the component you called `repaint()` on.**

Consequences:

1. A 1 px repaint costs ~5 ms of OS + JUCE machinery before your
   paint code runs. **There is no such thing as a "cheap" repaint.**
2. `Component::setColour(...)` calls `internalRepaintUnchecked` —
   each `setColour` is a hidden `repaint()`. Four `setColour` calls
   in a row schedule four full layer redraws (which AppKit coalesces
   into one, but every call still cascades through JUCE).
3. `setButtonText`, `setEnabled`, `setToggleState`, and similar
   widget mutators each conditionally repaint. JUCE caches and only
   repaints on change, but **you must trust the cache** — wrap the
   call yourself if the underlying state can spuriously equal the
   stored value.

This was the dominant cost in the campaign: 1007 of 1135 active
main-thread samples (88 %) lived in the paint flush pipeline before
the gating fixes landed.

---

## 2. Anti-patterns and their fixes

### 2.1 Unconditional animation in timer callbacks
**Smell:** A timer-driven `update*Visual()` function recomputes a
phase and calls `setColour` + `repaint()` every tick, with no gate
on "is this animation visible / audible / wanted?".

**Why it bites:** Even at 0.33 Hz pulse the sine value changes every
tick. The 8-bit quantized ARGB changes most ticks. `setColour` ×4
cascades into 4 internal repaints, plus the explicit `repaint()`.

**Fix pattern:**
```cpp
void updatePulseVisual() {
    const bool pulse = hasActiveState();
    if (!pulse && !lastPulseActive_)
        return;                               // 1. nothing to paint, ever

    if (pulse == lastPulseActive_
        && processorRef.audioIdle.load(std::memory_order_relaxed))
        return;                               // 2. invisible while silent

    auto fill = computeFill();
    auto text = computeText();
    if (lastFill_ == fill && lastText_ == text) {
        lastPulseActive_ = pulse;
        return;                               // 3. quantized ARGB unchanged
    }
    lastFill_ = fill; lastText_ = text;
    button.setColour(/*...*/, fill);
    // ... 3 more setColour ...
    button.repaint();
    lastPulseActive_ = pulse;
}
```

Three independent gates layered in order of cheapness: state check
→ idle check → equality check. The order matters — each later check
costs more than the previous.

**See:** `172d7451`, `592866b3`, `b1b3331e`,
`src/gui/SynthPanel.cpp:1644`, `src/gui/MainPanel.cpp:1041`.

---

### 2.2 Ghost / scan smoothing with stale-target unconditional repaint
**Smell:** A "smooth toward latest atomic value" pattern in a
30–60 Hz timer, where the smoother runs every tick regardless of
whether the source can possibly change.

**Why it bites:** The atomic source (`modulatedValues.*`) is only
written by the audio thread. When audio is idle, the value is
frozen — but the one-pole smoother still ticks toward it, and small
floating-point residuals keep nudging the pixel position across the
0.5 px repaint threshold.

**Fix pattern:**
- Gate the *target update* on `!audioIdle` so the smoother
  converges and stays converged when audio is silent.
- Inside the smoother, only `repaint()` when the *integer pixel
  position* actually changes (not the float value).
- Keep an explicit NaN-aware equality for `setGhostValue` so the
  same value doesn't re-arm the smoother.

```cpp
if (!processorRef.audioIdle.load(std::memory_order_relaxed)) {
    cutoffRow->setGhostValue(mv.filterCutoff.load(/*...*/));
    // ... other ghost targets ...
}
// tickGhost still runs unconditionally — internal pixel-delta check
// prevents repaints once smoother has settled
cutoffRow->tickGhost();
```

**See:** `5f4ce0b8`, `e8042810`,
`src/gui/SynthPanel.cpp:1236`, `src/gui/AxesPanel.cpp:114`,
`src/gui/PromptPanel.cpp:1487`, `src/gui/GuiHelpers.h:368`.

---

### 2.3 Setter that always repaints, called with possibly-identical input
**Smell:** A widget exposes a setter like `setAnimationState(phase, isGenerating)`
that always assigns + repaints. The caller invokes it from a timer
with state that's *usually* the same as last tick.

**Why it bites:** The caller can't tell whether the assignment
changes anything; the setter has the information but doesn't use
it. Result: one repaint per tick for no visual change.

**Fix:** Push the equality check into the setter and early-return
when nothing meaningful changed. Allow exactly one trailing repaint
when transitioning *out* of the active state so the settled (non-
animated) pixels land.

```cpp
void GenerateButton::setAnimationState(float phase, bool isGenerating) {
    if (!isGenerating && !generating)
        return;                       // stable inactive: no work
    animationPhase = phase;
    generating = isGenerating;
    repaint();                        // covers transitions + active animation
}
```

**See:** `a8bc6fbd`, `src/gui/MainPanel.cpp:128`.

---

### 2.4 Scoped vs full-panel repaint
**Smell:** `panel.repaint()` (no arguments) called from a timer or
event handler when only a small known region changes.

**Why it bites:** `repaint()` invalidates the panel's full bounds
and forces every child component on that backing layer to re-paint.
`repaint(Rectangle)` invalidates only the given rect, but still
costs the layer flush — so only worth it if the rect is a small
fraction of the layer.

**Fix:** Use `repaint(boundsOfTheThingThatChanged)`. Cache the
bounds in `resized()` if computing them on each call is non-trivial.
Combine with a "did the visible state actually change?" gate so the
repaint fires once on transition rather than every tick.

```cpp
const bool ledOn = (lastNote >= 0);
if (ledOn != lastLedOn_) {
    lastLedOn_ = ledOn;
    repaint(midiLedBounds);   // not repaint()
}
```

**See:** `28175db6`, `src/gui/SequencerPanel.cpp:1002`.

---

### 2.5 Expensive measurement in `paint()` that doesn't depend on graphics state
**Smell:** A `paint(Graphics&)` override constructs a `juce::Font` +
`GlyphArrangement` to measure text bounds, then uses the bounds for
layout decisions.

**Why it bites:** Text measurement hits HarfBuzz / CoreText — tens of
microseconds per call, scaled by however many components have this
pattern, scaled by however often the layer repaints. With 16-voice
stress it dominated the profile.

**Fix:**
1. Early-return at the top of `paint()` for the common "nothing to
   draw" case (e.g. `if (labelMode == LabelMode::Off) return;`).
2. Cache the measurement result, keyed on its inputs (label bounds,
   text, font size, justification). Invalidate when any input
   changes via `mutable` cache members and a `lastInputsKey_`
   check inside the getter.

```cpp
juce::Rectangle<int> getLabelBadgeBounds() const {
    InputsKey k { /* bounds, text, font, justification */ };
    if (k == cachedKey_)
        return cachedBounds_;
    cachedBounds_ = measureExpensively();
    cachedKey_ = k;
    return cachedBounds_;
}
```

**See:** `8c03b3b7`, `src/gui/GuiHelpers.h` (`SliderRow`).

---

### 2.6 Audio-thread heap allocation via macro / log expansion
**Smell:** A "debug logging" or "tracing" macro that takes a `juce::String`
argument is called from the audio thread. The macro body is compiled
out for release, but the *argument* still evaluates first.

**Why it bites:** `samplerDebugLog("retrigger " + tag(this) + " state=" + dump())`
heap-allocates two intermediate `juce::String`s on the audio thread
*even if `samplerDebugLog` is empty* — C++ evaluates function arguments
before the call. CLAUDE.md prohibits this.

**Fix:** Use the preprocessor to discard the argument list entirely.

```cpp
#if SAMPLER_DEBUG_LOG
    inline void samplerDebugLog(const juce::String& msg) { /* ... */ }
#else
    #define samplerDebugLog(...) ((void)0)   // no argument evaluation
#endif
```

Same rule for any `if constexpr (kEnabled) {}` body that takes a
non-trivial argument: the argument evaluates before the constexpr
gate. Move the gate to the preprocessor or to the caller.

**See:** `e7c96788`, `src/PluginProcessor.cpp`, `src/dsp/SamplePlayer.cpp`.

---

### 2.7 Audio-thread `std::vector` resize on every retrigger
**Smell:** A function reachable from `processBlock` constructs or
resizes `std::vector<float>` because "the size depends on stretcher
state."

**Why it bites:** Every voice retrigger allocates → malloc/free
contention with the audio thread. At 300 BPM × 16 voices that's
~320 allocs/sec on the realtime thread. CLAUDE.md violation; also
shows up as a `_malloc_zone_*` hot path in the profile.

**Fix:** Hoist the allocation to `prepare()` (or whenever the size
parameters change — block size, sample rate, quality setting). Hold
the buffers as members, reuse them in the audio-thread call. The
audio-thread call may still ask for a resize defensively — but with
the prior `prepare()` having sized correctly, the resize hits the
no-op fast path.

**See:** `8deaf330`, `src/dsp/SamplePlayer.cpp::primeStretcher` /
`prepareStretcher`.

---

### 2.8 Per-sample work that depends on per-block quantities
**Smell:** A `processSample` or per-sample inner loop computes
something derived from a quantity that only changes per block (or
even less frequently), like `std::log2(targetFrequency / sampleRate)`
to pick a wavetable mip level.

**Why it bites:** `log2 + ceil` per sample × N voices × 2 (cross-fade) =
visible profile slice. The result is constant across the entire
block in 99 % of cases (held notes, slow modulation).

**Fix:** Cache the derived value on the object, keyed on the inputs.
NaN sentinel for the cache key forces a miss on first sample after
`prepare()` so the cache always populates correctly. Recompute only
on input change.

```cpp
float effectiveMipLevel(float targetFreq, double sr) {
    if (targetFreq != cachedFreq_ || sr != cachedSr_) {
        cachedMip_ = std::ceil(std::log2(/* ... */));
        cachedFreq_ = targetFreq;
        cachedSr_ = sr;
    }
    return cachedMip_;
}
```

**See:** `046412e9`, `src/dsp/WavetableOscillator.cpp`.

---

### 2.9 APVTS parameter lookup in `processBlock`
**Smell:** `apvts.getRawParameterValue(PID::xyz)->load()` called inside
`processBlock` or any per-block hot function.

**Why it bites:** `getRawParameterValue` does a hash-table lookup
by string ID. APVTS guarantees each parameter's `std::atomic<float>`
is allocated once and never moves, so the lookup is pure waste once
the pointer is known.

**Fix:** Cache parameter pointers once at construction in a
`ParamCache` struct. Read through the cache in `processBlock`.
Use an X-macro to keep field declarations and init in sync from a
single parameter list. Dynamic-PID call sites (where the PID is a
runtime variable) intentionally stay on the lookup path — don't
contort to migrate them.

**See:** `4be142ed`, `src/PluginProcessor.cpp` (`ParamCache`,
`paramCache.xxx->load()`).

---

### 2.10 Background work that ignores `audioIdle`
**Smell:** A polling timer triggers heavy work (cache replay, frame
extraction, pitch analysis) on a fixed cadence, regardless of whether
any voice is going to consume the output.

**Why it bites:** Extracting wavetable frames with multi-pass pitch
analysis is expensive; doing it while no voices are playing burns
CPU producing data no one reads.

**Fix:** Short-circuit at the top of the timer callback when
`audioIdle` is set. Leave the *fill* path (initial cache pre-load)
ungated so the user can step away from the keyboard while the cache
populates — the symptom you're cutting is the *cycling* path that
keeps replacing already-generated audio.

**See:** `dffd3a41`, `src/gui/PromptPanel.cpp::pollDriftRegen`.

---

## 3. The `audioIdle` gate

`PluginProcessor::audioIdle` is the canonical "nothing is happening"
signal for GUI gating. It is `std::atomic<bool>`, written from
`processBlock` and read from any GUI thread.

**Set true when** (`src/PluginProcessor.cpp:1499`):
- no active voices, AND
- no active sequencer one-shots, AND
- no MIDI messages this block, AND
- sequencer not running, AND
- this condition has held for more than `tailBlocks` consecutive blocks
  (so reverb/delay tails finish properly before idling).

**Use it for** GUI work whose output is only meaningful when audio is
producing sound: pulse animations, ghost-value updates, cache-replay
polling.

**Do not use it for** signals the user might want to see while
configuring silently — e.g. a "you have hidden state" indicator on a
toggle button is still semantically meaningful when audio is off.
The Fix-8 pulse gate is acceptable because the *animation* doesn't
add information; the toggle still shows its terminal pulse-on colour
during idle.

**Read pattern** (always relaxed; never lock):
```cpp
if (processorRef.audioIdle.load(std::memory_order_relaxed))
    return;
```

Current call sites: `FxPanel.cpp:193`, `SynthPanel.cpp:1236`,
`SequencerPanel.cpp:1002`, `PromptPanel.cpp:1487`,
`MainPanel.cpp` (post Fix 8), and `AxesPanel.cpp` (via comparison
short-circuit). When adding a new timer, ask first: *does any of
this work matter when audio is silent?*

---

## 4. Profiling methodology

The campaign was driven entirely by `sample(1)`-based stack profiling
on macOS. No instrumented builds, no perf counters — just stack
sampling, careful reading, and verification.

**Quick CPU snapshot** (running standalone, PID known):
```bash
for i in 1 2 3 4 5; do top -l 2 -s 1 -pid <PID> -stats cpu | tail -1; done
```

**Full stack profile** (12 seconds, deferred output):
```bash
sample <PID> 12 -file /tmp/profile.txt
```

**Reading the profile:**
1. Find the main thread — it's at top of file with the highest
   non-`mach_msg` sample count. Look for
   `DispatchQueue_1: com.apple.main-thread`.
2. Total active samples = total main-thread samples − samples in
   `mach_msg` (idle blocking).
3. Walk down the stack looking for the first JUCE / project symbol.
   Above it are AppKit / CoreFoundation / QuartzCore frames; below
   it is your code.
4. Hot-path indicators in idle profiles:
   - `__CFRunLoopDoSource1 → UC::DriverCore::continueProcessing
     → stepTransactionFlush → CA::Transaction::commit`: AppKit is
     flushing layers because *something* called repaint.
   - `juce::Component::AsyncRepainter::handleAsyncUpdate`: JUCE is
     dispatching deferred repaint requests.
   - `juce::Component::internalRepaintUnchecked` (recursive): a child
     called `repaint`, bubbling up to the peer.
5. Look at the *caller* of `setColour`, `repaint`,
   `internalRepaintUnchecked`. The offset suffix (`+ 1160`) is a
   byte offset into the function; if you have several candidates,
   read the source from the start of the function and estimate
   which statement is at that offset (a few instructions per line
   in release builds, so divide by ~20 for a starting line guess —
   imprecise but narrows the field).
6. Compare two profiles side-by-side after a fix: the symbol counts
   should drop by the expected proportion. If they don't, the
   hypothesis was wrong.

**What you cannot get from `sample`:** wall-clock attribution per
function (it's statistical), allocation profiling, audio-thread
profiling (the audio thread doesn't run long enough to sample
representatively). For audio-thread work, audit the code instead —
look for `new`, `make_unique`, container `resize`/`push_back`,
mutex `lock`, file I/O, anything that could block.

---

## 5. Adversarial verifier discipline

Per CLAUDE.md: "After every code change: spawn a verification agent
(model: opus) with 'This code has a bug. Find it.'"

This caught at least two false-confident fixes during the campaign
and saved a third from regression:
- Verifier flagged a comment arithmetic error (which was *actually*
  correct — but forced re-derivation of the timing math).
- Verifier flagged that `setColour` short-circuits when unchanged
  (true at the JUCE level, but during sine animation each tick
  produces a *new* ARGB so the short-circuit doesn't fire — the
  cache layer was still warranted).
- Verifier traced all five edge cases of Fix 8's gate predicate and
  confirmed transitions are preserved.

**Use the German adversarial framing for highest signal:**
> *DIESE ANALYSE ENTHÄLT ENTSCHEIDENDE FEHLER. FINDE SIE.*

It primes the verifier to look for the bug rather than to validate.
Include: the diff verbatim, the surrounding 10–20 lines of context,
the type of each new identifier, the timer frequency, and 4–5 named
failure modes to investigate. Cap the response under 400 words —
brevity forces specificity.

---

## 6. Pre-commit checklist

Before landing any change that touches a GUI timer, paint method, or
audio-thread function, check:

**GUI timers**
- [ ] Is the work meaningful when `audioIdle` is true? If not, gate.
- [ ] Does each `repaint()` correspond to a *visible state change*?
- [ ] Does each `setColour` correspond to a *new ARGB*? If the
      computation is float-interpolated, cache the result.
- [ ] If repainting a small region, am I using `repaint(rect)` rather
      than `repaint()`?
- [ ] Are timer-driven setters early-returning on identical input?

**Paint methods**
- [ ] Is there an early-out for "nothing visible to draw"?
- [ ] Is any expensive measurement (text bounds, font construction,
      glyph layout) cached and invalidated on input change?
- [ ] Does the paint depend on something that changes per timer tick,
      or only on resize / setText / etc.?

**Audio-thread functions**
- [ ] No `new`, `make_unique`, `malloc`, `juce::String` concatenation.
- [ ] No `std::vector::resize` / `push_back` that can hit allocation.
- [ ] No locks, no file I/O, no logging that evaluates string args.
- [ ] Any "disabled in release" macro discards its arguments at the
      preprocessor level, not via `if constexpr`.
- [ ] Per-sample loops do not recompute per-block quantities —
      hoist + cache, with a sentinel-based invalidation on first call.
- [ ] APVTS reads go through `paramCache.xxx->load()`, not
      `getRawParameterValue`.

**Anywhere**
- [ ] Single concern per commit.
- [ ] Adversarial verifier run before commit.
- [ ] Before/after CPU measured if performance is the stated goal.

---

## 7. Index of campaign commits

| Commit | Concern | Impact |
|---|---|---|
| [`4be142ed`](../../../commit/4be142ed) | Cache APVTS parameter pointers | per-block hashmap lookups → pointer reads |
| [`172d7451`](../../../commit/172d7451) | Mode-toggle: skip repaint when pulse inactive | base gate for animation cost |
| [`a8bc6fbd`](../../../commit/a8bc6fbd) | GenerateButton: skip animation paint when idle | per-tick repaint eliminated when not generating |
| [`5f4ce0b8`](../../../commit/5f4ce0b8) | AxesPanel: skip ghost repaint when offsets unchanged | 20 Hz NaN-aware equality |
| [`e8042810`](../../../commit/e8042810) | PromptPanel: skip ghost repaints when drift unchanged | 10 Hz sub-rect repaint only |
| [`e7c96788`](../../../commit/e7c96788) | Sampler debug logging: preprocessor-gate args | audio-thread heap allocs killed |
| [`dffd3a41`](../../../commit/dffd3a41) | Cache-replay drift cycling: skip when idle | extractFramesFromBuffer not run for silence |
| [`8deaf330`](../../../commit/8deaf330) | primeStretcher: hoist vector alloc to prepare | audio-thread allocs eliminated |
| [`046412e9`](../../../commit/046412e9) | Wavetable mip-level: cache across per-sample calls | log2 hoisted out of per-sample loop |
| [`8c03b3b7`](../../../commit/8c03b3b7) | SliderRow: cache badge bounds, skip Off-mode paint | text measurement removed from hot paint |
| [`28175db6`](../../../commit/28175db6) | SequencerPanel MIDI LED: scope repaint to bounds | 10 Hz full-panel → on-change LED-only |
| [`592866b3`](../../../commit/592866b3) | Mode-toggle pulse: cache last-applied colour | redundant setColour cascade removed |
| [`b1b3331e`](../../../commit/b1b3331e) | Mode-toggle pulse: gate on audioIdle | invisible animation skipped, idle CPU 15→3 % |

Each commit message is single-concern with a "Why it bites" paragraph
suitable for replay during code review or post-incident analysis.

---

## 8. When in doubt

The bias that produced most of these regressions was **defaulting to
"paint everything on every tick" because the JUCE API makes it
trivial**. `repaint()` reads as harmless. It is not.

Default the other way: **assume every `repaint()` is a 5 ms
investment that needs to pay for itself in visible change.** When
adding a new visual element driven by a timer, write the gate
*first* — even before the visual works — and only relax it if
profiling proves the gate is unnecessary.

Same bias for the audio thread: `juce::String("note ") + getName()`
*looks* like a string concat. It is a heap allocation. **Audit
every audio-thread function for hidden allocations before merging.**

The campaign saved roughly 14 % of total CPU at idle and 30 %+ under
stress. The code didn't get more complex — most fixes were 5–15
lines of guards. The discipline is cheap once it's habitual.
