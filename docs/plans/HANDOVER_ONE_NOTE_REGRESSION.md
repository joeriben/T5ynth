# HANDOVER — "exactly one note, then silence"

**Written 2026-07-31, ~17:00, for a parallel session.** The main session is not
working on this; it is on the LRO plan. Everything needed to start is here.

---

## 1. The symptom, in BJ's words

> „seit dem letzt build lässt sich EXAKT EIN Ton spielen. danach bleibt alles
> leise, bei voll aufgedrehter Luattärke bis ich einneues Prest lade. ggf.
> speifisch für singing bowl"

So: one note sounds. After it, nothing sounds, at full volume, until a preset is
loaded — then it works again (for one note, presumably; not established). He was
in LRO mode and suspected the `singing_bowl` entry.

**And the constraint that governs the whole search:**

> „Das Problem besteht seit HEUTE. zum ersten Mal in diesem Projekt. Wenn die
> architektur so falsch ist wie Du sagst, hätte es IRGENDWO mal auftauchen
> müssen. es ist NIEMALS aufgetaucht."

**This is a regression from a change made today, 2026-07-31.** Any explanation
that rests on architecture older than today is wrong by construction — that
architecture has been played for months without this. §4 lists today's diff.

**And the standing instruction on how it may be fixed:**

> „KEINE QUICK FIXES. hier werden AUSSCHLiESSLICH URSACHEN REpARIERT. ich
> erlaube KEINERLEI SCHNELLE PATCHES auf Grundlegene Architekturprobleme. ZERO."

A guard that suppresses a bad value, a scan-and-reset, a validator bolted onto a
boundary — none of these are acceptable, even when they work. Find what changed
and repair that.

---

## 2. State of the tree — read before building

**The working tree does not compile.** `src/gui/SynthPanel.cpp` uses
`lroInstrumentKeys_` while its own header declares `lroInstruments_`; six errors.
That file is **uncommitted work from a different session** and must not be
touched. The same applies to `src/inference/PipeInference.{cpp,h}` (334 changed
lines across the four files, adding `getCuratedInstrumentKeys()` and a
`backendDirMutex_`).

Consequence: the binary BJ is running (built 15:33) was made from an **earlier**
state of those files, and the current tree cannot reproduce that build. Either
wait for that session to finish its rename, or build at a commit.

`src/dsp/AmpEffects.cpp.tmpbak` (empty, untracked) is also from that session.

Everything else is committed on `main`; HEAD is `ba1a14a3`.

---

## 3. What is already ruled out — with the evidence, so it is not re-run

**The `singing_bowl` entry is NOT the cause.** Three consecutive notes on one
always-on Csound instance, rendered at 176400 Hz (the rate the plugin actually
compiles at — `lroOsFactor_ = 4`), peaks 0.2575 → 0.4708 → 0.6108, zero Csound
errors. It gets louder, not quieter: the `mode` bank is still ringing when the
next strike lands. The reproduction script is
`/private/tmp/claude-502/.../scratchpad/multinote.py` — if it is gone, it is
twenty lines: build the host scaffold by hand with `ktrig` as a monotone counter
and `kgate` high/low around three notes, score `i 1 0 8.5`.

Note that `tools/lco_measure.py` **cannot** see this class at all: its scaffold
emits exactly one trigger edge (`ktrig = timeinsts() >= preroll ? 1 : 0`), so any
defect that appears at the SECOND note is invisible to it. That is a real gap in
the harness, independent of this bug.

**Voice liveness is NOT the cause.** A full probe of the voice pool on the real
processor: 8 consecutive note-on/off pairs all sound with flat peaks; the
five-envelope growth (`kNumModEnvs = 4`, `PID::kNumEnvs = 1 + kNumModEnvs`, both
`static_assert`ed) is in lockstep everywhere; no mod-envelope configuration
(loop, sustain 1.0, 5 s release, mono, percussive amp) holds a voice; voices free
at `SynthVoice.cpp:1107` (`ampEnv.isIdle() && !noteHeld`) and `VoiceManager.cpp:646`
clears the bookkeeping. **Caveat: that probe could not exercise the LRO/Csound
path** — in a console harness Csound resolves its opcode directory from the app
bundle and never compiles, so every note was silent there for a harness reason.
**The Csound voice path is therefore still unprobed and is the biggest open gap.**
`tools/audition_csound_engine.cpp` is the harness that CAN reach it (its build
recipe is in its own header, lines 46-64; it links
`build_clean/T5ynth_artefacts/Release/libakroasys_SharedCode.a` — note the
artefact is now named `akroasys`, the header still says `T5ynth`).

---

## 4. Today's diff — the search space

Everything in `src/` that changed today, by cluster:

**A. The amplifier chain — brand new, in the signal path behind every voice.**
`df9df844` (distortion/tremolo/chorus/phaser), `15dfcfd0`, `b061e86a`,
`3ad10de0`, `3f21e076` + its revert `684a3390`.
Files: `src/dsp/AmpEffects.{h,cpp}` (new), `src/PluginProcessor.{cpp,h}`,
`src/dsp/BlockParams.h`, `src/dsp/ParamCache.h`.
This is the only substantial new DSP in the audio path today.

**B. The filter rename.** `bc690177`, `ee6e2d20`.
`MoogLadderFilter` → `LadderFilter`, and the `FilterWarpStyle` key `ojd` →
`algebraic` with a compatibility reader (`filterWarpStyleFromString`).
Files: `src/dsp/BlockParams.h`, `src/dsp/LadderFilter.h`,
`src/dsp/CutoffWarpFilter.h`, `src/dsp/SynthVoice.h`,
`src/presets/CalibrationMigration.h`, `src/PluginProcessor.cpp`,
`src/gui/PromptPanel.cpp`, `src/inference/RepromptStances.cpp`.
Worth checking: `choiceFromKey` falls through to **index 0** on an unknown key
without warning (`src/PluginProcessor.cpp:6675-6679`), and `BlockParams.h` was
touched by BOTH clusters — an APVTS layout or index shift would misload stored
values silently.

**C. The uncommitted foreign work** in `SynthPanel` / `PipeInference` (§2). It
was compiled into the 15:33 binary in whatever state it had then.

**D. Not in `src/` but live at runtime:** `backend/dco_lexicon.json` and
`backend/lco_library.json` changed today (the singing bowl, and the brand-name
pass over routing keys and prose). The backend reads the library from the repo at
run time, so these are live without a rebuild. Ruled out for the bowl body
itself (§3); the surface-form/prose changes could still alter WHICH entries an
author opens, hence which orchestra is written.

---

## 5. What was found, and exactly how far it goes

**Finding 1 — the amp stages latch permanently on one non-finite sample, and
their `reset()` has no caller.** Verified empirically against the real classes.

- Latch sites: the distortion's DC blocker (`AmpEffects.cpp:244-245`, a one-pole
  that recomputes NaN from its own memory), `load_` (`:203`), the oversampler's
  IIR state, `juce::dsp::Chorus`'s delay line, `juce::dsp::Phaser`'s feedback.
- `prepare()` is called from exactly one place, `PluginProcessor.cpp:2174-2177`
  in `prepareToPlay`. **The four `reset()` methods have no caller anywhere in
  `src/`.** A preset load does not re-prepare.
- **The limiter cannot stop it**: `Limiter.cpp:22` gates on
  `buffer.getMagnitude(...) < 1e-6f`; `getMagnitude` folds through `jmax`, and
  `0 < NaN` is false, so an all-NaN buffer reports magnitude **0.0** and takes
  the silence early-out. Measured.
- Measured sequence: `note 1 fx ON rms 0.308 NON-FINITE` → `note 2 fx ON rms
  0.000 NON-FINITE` → `note 3 fx OFF rms 0.558` (the preset load, which writes
  all twelve fx params and defaults absent ones to 0, so the stages bypass) →
  raise a mix again → `NON-FINITE` immediately. **The NaN is still in there.**
- The chain does **not** manufacture the NaN: a fuzz over input amplitudes to
  1e34, all drives, all mixes, mono and stereo, five sample rates and oversized
  blocks produced zero non-finite outputs. It inherits it.

**This fits BJ's constraint**: `AmpEffects.cpp` was written TODAY. It is the
strongest lead. What is missing is the source of the first non-finite sample —
and note that at least one of `fx_dist_mix` / `fx_chorus_mix` / `fx_phaser_mix`
must have been above 0 for any of this to reach the output, which today means a
preset saved since the feature landed, or the LRO author writing them through the
KNOBS shelf (all twelve `fx_*` are on it, `PluginProcessor.cpp:6836-6841`).

**Finding 2 — the live Csound path does not check what it writes into the voice.**
`CsoundEngine.cpp:952-980` de-interleaves `spout` straight into the per-voice
buffer. The **offline** render path of the same file checks `std::isfinite` at
`:1219` with a comment naming exactly this risk. Commit `5e04cb5f` records a
`sqrt` of a negative in an authored body doing precisely that.

Related and measured: **`perform_check` never looks at a sample.** It runs
Csound with `-n` (render to nothing) and judges the exit code. Proven with the
real function — a body whose output is NaN (`sqrt(-1)` and `0/0`), and one that
outputs digital silence, all return `BESTANDEN`.

**BJ rejected building a rendering gate for this** — correctly, as a quick fix,
and because the symptom is in the key path, not the authoring path. It is
recorded here as a fact about the gate, not as a proposed repair.

**Finding 3 — the DCA has no key arm. REAL, but it does NOT explain this bug.**
BJ's spec: *„jeder Zustand einer Stimme ist zu JEDER Zeit ein XODER: Wert
(ENV→DCA) oder Wert Taste an/aus."*

```cpp
float computeDcaGain(const BlockParams& p, float ampEnvVal, const float* modEnvVals)
{
    float vca = (p.ampTarget == EnvTarget::DCA) ? ampEnvVal : 1.0f;
    ...
```

The else branch is a constant `1.0f`, not the key. And `noteHeld` appears in
`SynthVoice.cpp` exactly four times — set at 177/224/273, read once at 1107 in
the voice-free test. **The key never multiplies into the level.** The only level
path is `vcaScratch`, filled solely from `computeDcaGain`.

**Do not chase this as the cause of the one-note bug.** It has been true for
months and BJ's constraint rules it out. It is a separate, real defect worth its
own work order, and it only shows when the amp envelope is routed off the DCA.

---

## 6. Dead ends — do not repeat these

- Reading the voice pool for an off-by-one from the 3→5 envelope growth. Done,
  clean (§3).
- Rendering the singing bowl offline to look for the fault. Done, clean (§3).
- Building a NaN guard at the Csound boundary and a reset-on-detect in
  `processBlock`. Both were written and **reverted** on BJ's instruction; they
  are quick fixes and he forbade them. The tree is clean of them.
- Proposing that `perform_check` render and judge audio. Rejected as above.

---

## 7. Where to start

The one experiment that has not been run and would settle it: **play repeated
notes through the real `VoiceManager` + `CsoundEngine`** using
`tools/audition_csound_engine.cpp`, at a commit that builds (the tree does not,
§2), and see whether note 2 reaches the voice buffer. Its five existing cases
already cover note-on latency, release integrity and standing tone; it needs a
sixth that plays N notes in a row. That closes the one gap in §3.

In parallel, the second experiment: bisect today's `src/` commits (§4) against
the symptom. Cluster A is one revert away from being excluded or confirmed.

Governing rules for whatever is found: `CLAUDE.md` (JUCE safety, audio-thread
rules, one concern per commit, an opus verification agent after every behaviour
change), and §1's „AUSSCHLIESSLICH URSACHEN".
