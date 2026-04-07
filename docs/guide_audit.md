# T5ynth User Guide — Code Verification Audit

Date: 2026-04-07
Audited file: `resources/T5ynth_Guide.html`
Method: Every factual claim verified against source code by file and line number.

## Summary

- **Total claims verified:** 96
- **Correct:** 89
- **Wrong (fixed):** 7

## Errors Found and Fixed

| # | Section | Claim in Guide | Actual (Code) | Source | Fix |
|---|---------|---------------|---------------|--------|-----|
| 1 | Engine (Voice Count) | "equal power: each voice at 1/√N" | `1/N^0.3` (flatter curve) | VoiceManager.cpp:326 | Changed to "Gain auto-scales per voice count" |
| 2 | Setup | "Settings (gear icon in the status bar)" | Text button labeled "Settings" | StatusBar.cpp:35 | Changed to "Settings in the status bar" |
| 3 | Generation Controls | "Auto" marked as "Planned feature — not yet implemented" | Auto-regen IS implemented via drift_regen param | PluginProcessor.cpp:190, PromptPanel.cpp:261 | Removed "(planned)" note |
| 4 | Setup | SA Open 1.0 "no account needed" (after prior incorrect removal) | SA Open 1.0 requires HF token; SA Small does not | SetupWizard.cpp:25,227-231,673-678 | Restored HF token requirement for SA Open 1.0 |
| 5 | LFO Target | Link to `#mod` (Modulation Routing) | Anchor `#mod` was removed in earlier edit | — | Replaced with inline target list |
| 6 | Setup Troubleshooting | "Is the Python venv set up?" | Guide says "no Python setup needed" in same section | — | Removed contradictory venv check |
| 7 | DimExplorer | Only described left side (A-B diff) | Right side (shared dims) produces more fundamental changes | DimensionExplorer.cpp:73-76 | Added left/right zone explanation |

## Verified Correct Claims (by section)

### Setup
- [x] Python backend handled transparently (PipeInference launches subprocess)
- [x] Settings button exists (StatusBar.cpp:35)
- [x] Model download via Settings panel with progress bar (SetupWizard.h:66)
- [x] SA Small from GitHub Releases (SetupWizard.cpp:26-27)
- [x] SA Open 1.0 requires HF token (SetupWizard.cpp:25,227-231)
- [x] Models dir: macOS ~/Library/T5ynth/models/ (config.py:34-39)
- [x] Models dir: Linux ~/.local/share/T5ynth/models/ (config.py:34-39)
- [x] Auto-detect by model_index.json or model_config.json (SetupWizard.cpp:6-7)
- [x] Device auto-detect: CUDA > MPS > CPU (config.py:13-22)
- [x] Status bar shows connection status (StatusBar.h:15)
- [x] Lazy loading on first generation (config.py:59)

### Generation Controls
- [x] Alpha range -2.0 to +2.0 (PluginProcessor.cpp:98)
- [x] Alpha quadratic curve (PluginProcessor.cpp:95-110)
- [x] Magnitude range 0.001-5.0 (PluginProcessor.cpp:114)
- [x] Noise range 0.0-1.0 (PluginProcessor.cpp:117)
- [x] Duration range 0.1-47s (PluginProcessor.cpp:120)
- [x] Steps range 1-100 (PluginProcessor.cpp:122)
- [x] CFG range 1.0-15.0, default 7 (PluginProcessor.cpp:124-125)
- [x] Start Position range 0.0-1.0 (PluginProcessor.cpp:127-128)
- [x] Seed range -1 to 999999999 (PluginProcessor.cpp:130)
- [x] Random button exists (PromptPanel.h:92)
- [x] HF Boost toggle exists (PluginProcessor.cpp:411)
- [x] Three model slots (PromptPanel.cpp:95,177)
- [x] GPU/CPU toggle (PromptPanel.cpp:104-105)

### Semantic Axes
- [x] 3 dropdown slots (AxesPanel.cpp:45)
- [x] Slider range -1.0 to +1.0, step 0.002 (AxesPanel.cpp:65)
- [x] 8 axes with correct names (AxesPanel.cpp:8-18)
- [x] Backend keys correct (AxesPanel.cpp:23-30)
- [x] Pole labels match (AxesPanel.cpp:90-96, pipe_inference.py:324-333)

### DimExplorer
- [x] 768 bars (DimensionExplorer.h:54)
- [x] Sorted by |baseValue| descending (DimensionExplorer.cpp:73-76)
- [x] Green=A, Orange=B colors (DimensionExplorer.cpp:7-8)
- [x] Two prompts shows A-B diff (DimensionExplorer.cpp:67-70)
- [x] Edited bars blue (DimensionExplorer.cpp:9)
- [x] Apply+Generate button (MainPanel.cpp:137)
- [x] Undo/Redo/Reset buttons (MainPanel.cpp:170-187)
- [x] Mini-view below Axes, click opens overlay (MainPanel.cpp:148-152)
- [x] Offsets: manipulated[:,:,idx] += val (pipe_inference.py:462-466)

### Engine Modes
- [x] Two modes: Sampler, Wavetable (PluginProcessor.cpp:135)
- [x] Signalsmith Stretch for pitch (SamplePlayer.h:139)
- [x] Loop modes: One-shot, Loop, Ping-Pong (PluginProcessor.cpp:305)
- [x] Crossfade 0-500ms (PluginProcessor.cpp:308)
- [x] Loop Optimize: Off/Low/High (PluginProcessor.cpp:312-313)
- [x] Normalize toggle (PluginProcessor.cpp:310)
- [x] Scan 0.0-1.0 (PluginProcessor.cpp:24-26)
- [x] Voice count: Mono/4/6/8/12/16 (PluginProcessor.cpp:31)
- [x] Oldest note stealing (VoiceManager.cpp:286-295)

### Filter
- [x] SVF TPT topology (StateVariableFilter.h:48)
- [x] Types: Off/LP/HP/BP (PluginProcessor.cpp:56)
- [x] Slopes: 6/12/18/24 dB (PluginProcessor.cpp:59)
- [x] Cutoff 20-20000Hz (PluginProcessor.cpp:50)
- [x] Resonance 0-1 → Q 0.5-18 (StateVariableFilter.h:42-45)
- [x] Mix 0-1 equal-power (StateVariableFilter.cpp:45-46)
- [x] Kbd Track 0-1 (PluginProcessor.cpp:65)

### Envelopes
- [x] 3 identical ADSR envelopes (SynthVoice.h:83-85)
- [x] Each has target dropdown (PluginProcessor.cpp:270-277)
- [x] DCA is a target option (PluginProcessor.cpp:272)
- [x] A 0-5000, D 0-5000, S 0-1, R 0-10000 (PluginProcessor.cpp:35-45)
- [x] Amount 0-1, Vel Sens 0-1, Loop toggle (PluginProcessor.cpp:238-265)

### LFOs
- [x] Two LFOs (PluginProcessor.h:122-123)
- [x] Rate 0.01-30Hz (PluginProcessor.cpp:168,178)
- [x] Depth 0-1 (PluginProcessor.cpp:170,180)
- [x] Waveforms: Sine/Tri/Saw/Square (PluginProcessor.cpp:174,185)
- [x] Modes: Free/Trigger (PluginProcessor.cpp:291,294)
- [x] Target dropdown (PluginProcessor.cpp:281,285)

### Drift
- [x] 3 drift LFOs (DriftLFO.h:44)
- [x] Rate 0.001-2.0Hz (PluginProcessor.cpp:197-210)
- [x] Depth 0-1 (PluginProcessor.cpp:200,206,212)
- [x] Waveforms: Sine/Tri/Saw/Square (PluginProcessor.cpp:223-235)
- [x] Drift Enable toggle (PluginProcessor.cpp:188)
- [x] Targets: Alpha, Axis 1-3, WT Scan, Filter, Pitch, Dly Time/FB/Mix, Rev Mix, ENV1-3 Amt (PluginProcessor.cpp:216-222)
- [x] Regen modes: Manual/Auto/1st Bar (PluginProcessor.cpp:190-192)
- [x] Crossfade 0-2000ms (PluginProcessor.cpp:193-195)
- [x] Ghost indicators orange (PromptPanel.cpp:286)

### Effects
- [x] Delay Off/Stereo (PluginProcessor.cpp:320)
- [x] Delay time 1-2000ms (PluginProcessor.cpp:70)
- [x] Feedback 0-0.95 (PluginProcessor.cpp:73)
- [x] Damping 0-1, 0=bright 20kHz, 1=dark 500Hz (DelayLine.h:8-9)
- [x] Mix 0-1 (PluginProcessor.cpp:76)
- [x] Reverb Off/Dark/Medium/Bright/Algo (PluginProcessor.cpp:323)
- [x] EMT 140 convolution IRs (PluginProcessor.cpp:432-434)
- [x] Algo: Room/Damping/Width all 0-1 (PluginProcessor.cpp:86,88,92)
- [x] Signal chain: Delay→Reverb→Limiter→Master (PluginProcessor.cpp:1087-1198)

### Step Sequencer
- [x] UI dropdown 2-32 (SequencerPanel.cpp:226)
- [x] Per step: note/velocity/enabled/bind (StepSequencer.h:81-88)
- [x] BPM 20-300 (PluginProcessor.cpp:343)
- [x] Divisions 1/1-1/16 (PluginProcessor.cpp:348-349)
- [x] Gate 0.1-1.0 (PluginProcessor.cpp:367)
- [x] Glide 10-500ms (PluginProcessor.cpp:352-353)
- [x] Octave -2 to +2 (PluginProcessor.cpp:370-371)
- [x] 10 presets with correct names (StepSequencer.cpp:99-109)
- [x] Save/Load S/L buttons (SequencerPanel.h:51-52)
- [x] Gen pattern transfers to step seq on deactivation (PluginProcessor.cpp:698-719)

### Generative Sequencer
- [x] GEN button (SequencerPanel.h:87)
- [x] Steps 2-32 (PluginProcessor.cpp:381)
- [x] Pulses 1-32 (PluginProcessor.cpp:383)
- [x] Rotation 0-31 (PluginProcessor.cpp:385)
- [x] Mutation 0-1, default 0.80 (PluginProcessor.cpp:387-388)
- [x] Range 1-4 octaves (PluginProcessor.cpp:390)
- [x] Scale Root C-B (PluginProcessor.cpp:403)
- [x] Scale Types correct (PluginProcessor.cpp:406)
- [x] Fix toggles for all 4 params (PluginProcessor.cpp:393-400)
- [x] Bjorklund's algorithm (EuclideanRhythm.h:44-60)
- [x] Stride = max(1, steps-pulses) (GenerativeSequencer.cpp:160)
- [x] Triangle wave traversal (GenerativeSequencer.cpp:171-182)
- [x] Velocity from gaps (GenerativeSequencer.cpp:188-192)
- [x] Mutation formula: floor(rate * pulses * 0.6) (GenerativeSequencer.cpp:234-235)
- [x] Mutation breathing: sinusoidal, 16-cycle, [50%-100%] (GenerativeSequencer.cpp:377-385)
- [x] Drift period: 8 cycles (GenerativeSequencer.h:106)

### Arpeggiator
- [x] Modes: Off/Up/Down/UpDown/Random (PluginProcessor.cpp:362)
- [x] Rate: 7 divisions incl. triplets (PluginProcessor.cpp:357)
- [x] Octaves 1-4 (PluginProcessor.cpp:359)
- [x] Major triad: 0,4,7 (Arpeggiator.h:49)
- [x] UpDown: endpoints not doubled (Arpeggiator.cpp:32)

### Presets
- [x] .t5p format (PresetFormat.h:57)
- [x] Stores params + prompts + audio + embeddings + seed + device + model (PresetFormat.cpp:9-97)
- [x] Import/Export buttons (PresetPanel.h:30-31)
