# T5ynth Performance Audit Harness

**Purpose**: Standardized protocol for spawning agents to audit T5ynth performance WITHOUT rediscovering the same code patterns each time. Agents inherit the checklist, known bottleneck locations, and profiling methodology from this document.

**Frequency**: Every 3–6 months, or after major refactoring that touches GUI timers, DSP hot paths, or voice rendering.

---

## Model Tiers (which agent needs how much intelligence)

This is grounded in an actual failure: the first draft of this audit was written by Haiku. It
was reliable at grep-and-report (line numbers, timer locations, repaint count exact) but produced
a *material* error (claimed `computeEffectiveLfoDepth` was hoistable when its body consumes
per-sample envelope values), a ~5× inflated call count (131K vs ~24.5K — sub-blocks partition
samples, they don't multiply them), a misread (`performanceOutputGain` "per voice" when it's on
the sum), and fabricated cycle counts presented as fact. All three failure modes were *reasoning*,
not search: data-flow tracing, frequency arithmetic, and epistemic discipline (estimate vs fact).

**Rule of thumb: search/collect → Sonnet/medium. Data-flow judgment + final synthesis → Opus/high.**

| Role | Model | Effort | Why |
|------|-------|--------|-----|
| **Agent 1 — GUI Refresh** | Sonnet | medium | Mostly pattern-matching: does this timer have an audioIdle gate, is repaint conditional. Verifiable. Haiku is risky judging "visible state change". |
| **Agent 2 — DSP Hot-Path** | **Opus** | **high** | The hard role. Must read a function body and decide whether a per-sample input *genuinely* varies and a hoist is safe. This is exactly where Haiku failed. Sonnet/high is only acceptable *because this doc pre-marks the known trap* (#3) — for a NEW hot-path question, use Opus. |
| **Agent 3 — Audio-Thread Safety** | Sonnet | medium | Grep for new/lock/malloc + judge "is this in a hot path". Mostly mechanical. |
| **Synthesis / Verifier** (reads agent outputs, decides what is real) | **Opus** | **high–xhigh** | The most critical role. It took Opus to catch Haiku's confidently-presented errors. Fan-out may be cheaper; the *judgment* must not be. |

**Do NOT use Haiku for any judging role.** As a raw grep-collector feeding a stronger synthesizer
it is fine, but in an audit it confidently fabricates (wrong numbers, wrong "all clear"), and a
false all-clear is more expensive than no audit. This matches the project norm (CLAUDE.md:
verification via an Opus agent; defined coding → Sonnet, Opus writes the spec and verifies).

Cost-efficient run: 3 Sonnet/medium agents in parallel (Agent 1 + 3, plus pure candidate-location
for Agent 2), then ONE Opus/high pass that data-flow-verifies the Agent-2 candidates and synthesizes.
The expensive step is small and one-off.

---

## Quick-Start: How to Invoke

When you want to re-audit T5ynth performance:

```bash
# 1. Read this harness (you're here ✓)
# 2. Spawn the audit agents — see "Model Tiers" above for who runs on what.
#    Search/collect roles: Sonnet/medium. DSP data-flow + synthesis: Opus/high.
# 3. Each agent gets the context-prefilled prompt from the section below
# 4. Agents output: verdict + violations with line numbers
# 5. An Opus/high pass verifies DSP candidates by data-flow and synthesizes
# 6. Run verification profiling (see §5 Profiling)
```

---

## Agent 1: GUI Refresh Audit (Explore, ~3 min)

**Task**: Verify all timer-driven UI updates are properly gated.

**Input Prompt to Agent**:

```
CONTEXT: T5ynth has 10 timer subclasses running at varying frequencies (30 Hz for animations, 10 Hz for polling). 
The PERFORMANCE_GUIDE documents that every repaint() costs ~5ms OS+JUCE machinery (full layer redraw, not pixel-by-pixel).

AUDIT CHECKLIST: For each timer callback, verify:

1. Does the timer have an audioIdle gate?
   Pattern: if (processorRef.audioIdle.load(std::memory_order_relaxed)) return;
   Location: docs/PERFORMANCE_GUIDE.md §2; the atomic is set in PluginProcessor.cpp
            (store(true) ~2164, store(false) ~2174 — grep "audioIdle.store" to confirm,
            line numbers drift)
   
2. Does repaint() correspond to a VISIBLE state change? (Not float residuals)
   Anti-pattern: Ghost/scan smoothing with stale-target unconditional repaint
   
3. Does setColour() correspond to a NEW ARGB? (Not recalculated every tick)
   Anti-pattern: Setter that always repaints with possibly-identical input
   
4. Is scoped repaint(rect) used instead of unscoped repaint()?
   Anti-pattern: Scoped vs full-panel repaint (full invalidates entire backing layer)

KNOWN TIMER LOCATIONS (for reference):
- MainPanel.cpp:1170 (Generate glow, 50ms / 20Hz)
- SnapshotButton (MainPanel.cpp:289-330, 30ms / 33Hz)
- PromptPanel.cpp:626 (Ghost updates, 100ms / 10Hz)
- SequencerPanel.cpp:955 (State polling, 100ms / 10Hz)
- FxPanel.cpp:206 (Ghost sliders, 33ms / 30Hz)
- SynthPanel.cpp:1330 (Tab follow, 33ms / 30Hz)
- WaveformDisplay.cpp:33 (Drag updates, 200ms / 5Hz, conditional)
- UnionJackButton (GuiHelpers.h:2196, 33ms / 30Hz, conditional)

OUTPUT: For each timer, report:
✓ gated (has audioIdle gate)
✗ missing gate (needs gating)
? needs clarification (conditional logic unclear)

Also report: any float-residual repaints, unconditional setColour calls, unscoped repaint() calls 
with line numbers.
```

---

## Agent 2: DSP Hot-Path Audit (Explore, ~4 min)

**Task**: Verify DSP per-sample work is not redundant.

**Input Prompt to Agent**:

```
CONTEXT: T5ynth uses a sub-block architecture (32-sample blocks) to amortize modulation updates.
Previous audit found these per-sample calculations — verify they cannot be hoisted to sub-block level.

⚠️ ALL cycle counts and budget % below are UNMEASURED hand-estimates, NOT profiled.
   sample(1) cannot do per-function wall-clock attribution (see PERFORMANCE_GUIDE.md §profiling).
   Treat them as "worth investigating", not as facts. Verify magnitude before acting.

KNOWN CANDIDATES (line numbers verified 2026-06-26 against HEAD; re-grep, they drift):

1. VoiceManager::renderBlock():642 — equalPowerPan() per voice per sample [VERIFIED REDUNDANT]
   Cost: cos + sin + jlimit per call (est. tens of cycles)
   Frequency: per active gen-strand voice (sourceId 0-3) × every sample in the block
   FINDING: voicePan[] is written ONLY at note/setter events (VoiceManager.cpp 156/192/297/598/990),
            NEVER inside the sample loop (612-664). So equalPowerPan recomputes an IDENTICAL value
            every sample → genuine redundancy.
   Fix (simplest, lowest risk): precompute leftGain[vi]/rightGain[vi] for each active voice ONCE,
            before the sample loop at line 612. NOT "at note-on" — pan is block-constant, so a
            per-block hoist inside renderBlock is enough and auto-tracks pan changes between blocks.

2. VoiceManager::renderBlock():648 — performanceOutputGain() per sample [VERIFIED REDUNDANT]
   Body (line 929): softPedalDown ? 0.65 : 1.0, then jlimit(channelVolumeGain*expressionGain*softGain).
   All inputs are member vars, constant within a block.
   NOTE: gain is ALREADY applied to the summed output (monoSum/leftSum/rightSum @ 649-651), it was
         NEVER per-voice — so "apply to final sum instead of per voice" is a misread.
   Fix: hoist the performanceOutputGain() CALL to a const before the sample loop; keep the
        `currentGain *` factor INSIDE the loop (currentGain ramps per-sample at 614-619).

3. SynthVoice::renderBlock():855-863 — computeEffectiveLfoDepth() 3x per sample [NOT A CLEAN HOIST]
   Body (line 39-50): returns baseDepth, BUT if p.ampTarget/mod1Target/mod2Target == this LFO-depth
            target, it folds in ampEnvVal/mod1EnvVal/mod2EnvVal — which are computed PER SAMPLE
            (envelope outputs, lines 848-850).
   FINDING: when an envelope targets LFO depth, the value GENUINELY varies per sample and CANNOT be
            hoisted. Only the un-targeted case is constant, and there it's ~3 branch-predicted int
            comparisons (~negligible). This is NOT a clean quick win — earlier "1.7% / hoist to
            sub-block" framing was wrong.
   Rough frequency (corrected): 3 LFOs × samples-in-block × active voices ≈ 3×512×16 ≈ 24.5K/block
            for a 512-sample block, 16 voices — NOT the previously claimed 131K (that erroneously
            multiplied by sub-block count; sub-blocks PARTITION the samples, they don't multiply them).

4. SynthVoice::renderBlock():957 — noise.setType() per sample (only when noiseLevel > 0.001) [MICRO]
   p.noiseType is constant within a block, so calling setType every sample is redundant — but tiny.
   Candidate: set type once per block if unchanged. Lowest priority.

FOR EACH CANDIDATE:
- Explain WHY it cannot be hoisted (modulation/real-time requirement) — see #3 for the trap
- OR CONFIRM it's a safe optimization candidate
- Include: location, hoist requirement, risk of regression. If you state a cost, MEASURE it.

REFERENCE: See docs/PERFORMANCE_GUIDE.md §3 (anti-pattern #8: per-sample work from per-block quantities)
```

---

## Agent 3: Audio Thread Safety Audit (Explore, ~2 min)

**Task**: Confirm no allocations, locks, or file I/O on audio thread.

**Input Prompt to Agent**:

```
CONTEXT: T5ynth uses ParamCache (lock-free pointers) and PipeInference on background thread.
Previous audit confirmed lock-free design, but re-verify for any regressions.

SCAN FOR (in processBlock and voice rendering):

1. Heap allocation: any malloc/new/make_unique outside pre-allocation structures
   Anti-pattern: Audio-thread heap allocation via macro/log expansion
   
2. Lock contention: any mutex locks (even .tryLock())
   
3. File operations or logging with string expansion
   Anti-pattern: juce::String("note ") + getName() heap alloc on audio thread
   
4. Vector operations: any std::vector::resize or push_back
   Anti-pattern: Per-sample std::vector resize on retrigger

EXPECTED: All clean (previous audits confirm lock-free design). 
But verify no regressions in:
- PluginProcessor::processBlock() (line 1932)
- VoiceManager::renderBlock() (line 541)
- SynthVoice::renderBlock() (line 722)

OUTPUT: Confirm all clear, OR list violations with:
- filename:line
- operation (what: new/lock/log/resize)
- frequency (per-sample/per-block/per-voice)
- risk assessment (data race / dropout risk / CPU spike)
```

---

## 5. Verification Checklist (Manual, ~5 min)

After agents report findings, run profiling before and after any optimizations:

### **Step 1: Idle Profile (silence, no audio activity)**

```bash
# Start standalone in background (silent, Generate button not pressed)
./build_clean/T5ynth_artefacts/Release/Standalone/T5ynth.app/Contents/MacOS/T5ynth &
PID=$!
sleep 3  # Let it settle

# Capture 12-second profile
sample $PID 12 -file /tmp/idle-before.txt

# Read output — watch for these stack patterns:
# 1. CA::Transaction::commit (repaint flushing) — should be minimal
# 2. juce::Component::AsyncRepainter::handleAsyncUpdate — deferred repaint dispatch
# 3. juce::Component::internalRepaintUnchecked (recursive) — child repaint bubbling

# Count samples in mach_msg (OS blocking idle) — high count = good (idle blocking)
grep "mach_msg" /tmp/idle-before.txt | wc -l  # Should be ~11–12 (most of profile)
```

### **Step 2: Stress Profile (16-voice polyphony, 300 BPM)**

```bash
# In T5ynth: 
# - Load any WT preset (Wavetable engine)
# - Set BPM to 300
# - Start sequencer (StepSeq or GenSeq, 16 steps)
# - Play 16-voice chord
# Wait 3 sec, then:

sample $PID 12 -file /tmp/stress-before.txt

# Extract top hotspots:
# Look for: voice rendering, filter coefficient updates, pan/gain calculations
```

### **Step 3: Listen for Artifacts**

After any DSP optimization (pan caching, output gain hoist):
- Play a gliding note with pan automation + velocity modulation
- Listen for: clicks, pops, discontinuities, attack artifacts
- Pan should remain smooth and responsive

### **Step 4: Compare Before/After**

After implementing an optimization:

```bash
# Run profiling again (same conditions)
sample $PID 12 -file /tmp/idle-after.txt
sample $PID 12 -file /tmp/stress-after.txt

# Compare symbol counts (should drop by ~expected proportion)
# E.g., if you gated 30 Hz animation: CA::Transaction::commit should drop ~10–15%

# Diff-friendly comparison:
grep "CA::Transaction::commit" /tmp/idle-before.txt | wc -l > before.txt
grep "CA::Transaction::commit" /tmp/idle-after.txt | wc -l > after.txt
```

---

## 6. Known Bottlenecks (Reference Table)

⚠️ Cost/Budget columns are UNMEASURED estimates — verify with sample(1) before treating as fact.
   Line numbers verified 2026-06-26 against HEAD; re-grep before relying on them.

| Priority | Location | Operation | Cost (est.) | Status |
|----------|----------|-----------|-------------|--------|
| **HIGH** | VoiceManager:642 | equalPowerPan() per voice per sample | cos+sin/call | ✅ VERIFIED redundant — voicePan block-constant; precompute per active voice before loop @612 |
| **MEDIUM** | VoiceManager:648 | performanceOutputGain() per sample | jlimit+mul | ✅ VERIFIED redundant — block-constant; hoist the CALL, keep `currentGain *` per-sample. (Already applied to the sum, not per-voice.) |
| **LOW** | SynthVoice:855–863 | computeEffectiveLfoDepth() 3×/sample | ~3 branches | ⚠️ NOT a clean hoist — value varies per-sample WHEN an env targets LFO depth. Only negligible un-targeted case is redundant. |
| **LOW** | SynthVoice:957 | noise.setType() per sample (when active) | enum store | Candidate — set once/block if p.noiseType unchanged. Tiny. |
| **LOW** | VoiceManager:614–619 | Gain ramp per sample | counter+add | Negligible — currentGain must ramp per-sample by design. |
| **LOW** | SynthVoice:~719+ | Filter coeff updates per sub-block | — | Already sub-block amortized (not per-sample). |

---

## 7. Session History (Backfill)

For context, here are documented performance optimizations:

| Date | Commit | Category | Change | Impact |
|------|--------|----------|--------|--------|
| 2026-06-23 | 568621d4 | GUI | GenSeq CRT Monitor (event-driven repaint only) | Perf-only, no idle regression |
| 2026-06-22 | c2c165f7 | GUI | Fix 10Hz syncStepCount churn (mode re-sync thrash) | Reduced 10 Hz polling overhead |
| 2026-06-06 | 0f45beeb | IPC | LCXL3 LED deferred timer (avoid blocking burst) | Isolated LED SysEx I/O from UI thread |
| 2026-05–06 | 172d7451..b1b3331e | GUI | 14-commit audioIdle gating campaign | ~14% idle CPU reduction, 30%+ stress reduction |

---

## 8. Related Documentation

- `docs/PERFORMANCE_GUIDE.md` — Complete anti-pattern catalogue, audioIdle gate rules, profiling methodology
- `src/PluginProcessor.cpp` — audioIdle atomic (store ~2164/2174; grep "audioIdle.store")
- `src/dsp/ParamCache.h` — Lock-free APVTS pointer caching (already optimized)

---

## Notes for Future Auditors

- **Avoid speculation**: If the code is unclear, report it as "? needs clarification" rather than guessing.
- **Measure, don't assume**: Use `sample(1)` profiling before and after changes; CPU savings are hard to predict without measurement.
- **Regression risk**: DSP optimizations (hoisting, caching) risk glitches if not carefully verified. Always A/B test before committing.
- **Batch by concern**: Each optimization should be a single-concern commit (per CLAUDE.md §Process).
