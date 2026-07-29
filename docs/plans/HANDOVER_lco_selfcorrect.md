# HANDOVER — 2026-07-21 — LCO self-correction (parameter-move) + Csound release

> ⚠ **SUPERSEDED — Thread 1 is CANCELLED (commit dbd6c153, 2026-07-21).** BJ
> afterward ordered the whole self-check OFF: *"'self check' ist eine katastrophe.
> Deaktivieren und als deprecated im Code markieren."* The self-listen / compare /
> correct loop is deactivated (`T5YNTH_LCO_SELFCHECK=0` in `triggerDcoBake()`; a
> bake authors once and plays) and its machinery marked DEPRECATED, code retained.
> So **do NOT build the parameter-move fix (Thread 1) or the proximity index** — no
> correction pass exists to fix. **Thread 2 (Csound all-platform release bundling)
> and Thread 3 (CLAP "heard as" description) are unaffected** and still open. See
> memory `project_lco_selfcheck_proximity_index` for the deactivation record.

Session origin: `3049ed30-d2ef-4879-88cc-e25ef7f888b4`. Written on BJ's request.
Read the linked memories first: [[project_lco_selfcorrect_is_parameter_move]],
[[project_lco_csound_not_in_ci]], [[project_lco_offline_orientation_hypotheses]],
[[feedback_no_unauthorized_sound_features]], [[llm-first-never-deterministic-first]].

---

## TL;DR — where things stand

Two threads are open. BJ **paused the Csound release push** ("Stopp") to first fix the
LCO self-check, which is the active thread.

1. **LCO self-correction (ACTIVE).** BJ's directive: on a prompt/sound MISMATCH the
   correction must change **the PARAMETERS of the chosen instrument, NEVER swap the
   instrument**. Current code swaps `flute→sine`, `organ→sine·rich` (nonsense) and often
   shows/plays nothing at all. Root causes are diagnosed and empirically confirmed. A
   **concept for the fix was written and shown to BJ** (below). BJ then asked for a
   **complete per-instrument parameter table** and immediately for this handover — so the
   table + the design approval are the two open items.

2. **Csound release bundling (PAUSED).** macOS self-contained bundling is **proven**
   (`tools/bundle_csound_macos.sh`). BJ decided **all three platforms** must produce LCO
   sound. Linux/Windows Csound build support does not exist yet. Push is halted until both
   this and the self-correction fix land.

3. **CLAP "heard as" description (separate).** The first half of the description is CLAP's
   unreliable guess (dark-inverted); the DSP half is correct. To be fixed separately.

---

## THREAD 1 — LCO self-correction = parameter-move (ACTIVE)

### BJ's directive (verbatim, 2026-07-21)
- "HJob ist es NICHT das Instrument zu wechseln, sondern die PARAMETER des instruments
  entsprechend zu verändern."
- Reacting to the probe result flute→sine / organ→"sine·rich": "was soll dieser Rückfall
  auf 'sine'? wie bitte soll eine sinuswelle 'rich' sein? Das stimmt doch alles überhaupt
  nicht."
- "abgesehen davon dass WEDER etwas zu hören noch im Panel zu sehen ist" (nothing changes
  on a mismatch).

### Root causes — DIAGNOSED + EMPIRICALLY CONFIRMED
1. **Why "nothing is heard nor seen":** the C++ correction loop
   [`src/gui/PromptPanel.cpp`](../../src/gui/PromptPanel.cpp) (commit `4a015031`,
   `kMaxSelfCorrections=5`, loop at ~2447–2555) breaks at **line ~2461**
   `if (! canContinue && attempt > 0) break;` **before** `finding.clear()`. So when a
   correction author returns failure/empty, the panel keeps attempt-0's reading AND
   attempt-0's finding and never swaps the sound. The correction author fails
   intermittently (MPS non-determinism — the loop's own comment at ~2453 documents this).
2. **Why the swap to "sine":** the correction runs through the **full author**
   (`backend/csound_orch.py build_csound_response`, ~3808), which re-selects the technique.
   The old `_CS_CORRECTION_RULE` (~791) even said literally "add or exchange keys". Probe
   (`scratchpad/probe_correction.py`, real 7B over IPC):
   - OLD rule: `flute` → `sine · warm`; `church organ` → `sine · rich, deep`.
   - After I rewrote the rule to FORBID the swap: `flute` → `sine · warm, fat`;
     `church organ` → `sine · bright`. **Still swaps.**
   → **Prompt-only cannot hold the instrument.** A code-level lock is required.

### The concept shown to BJ (his answer: "Erst Konzept zeigen" → shown → then asked for the table)
A **code-locked parameter-move** correction. 7 points:
1. **Boundary.** Locked (= instrument): the `OSC` lines — technique keys, morph chains
   `a > b`, layer count, octave registers. Movable (= parameters): `ADJECTIVES`, the named
   per-key `PARAMS(name=value)`, `VOL`, `MOTION`. A morph chain counts as instrument (its
   endpoints are instruments) → locked.
2. **Correction prompt = narrow parameter task**, not the full author: "the instrument is
   fixed, change no oscillators, move only the parameters toward the request; output only
   ADJECTIVES/PARAMS/VOL/MOTION." Turn carries: original request, the fixed OSC lines
   verbatim, the CURRENT parameters, the "heard as", the missed quality. Because the 7B no
   longer chooses an instrument, its parameter choices are FOR the fixed instrument (this
   is what fixes the entanglement that made prompt-only fail — the full author let it
   re-choose).
3. **Code enforcement:** discard any OSC lines in the 7B reply, substitute the previous
   instrument's chains+registers, take only ADJECTIVES/PARAMS/MOTION/VOL from the reply
   (per-key params only for keys surviving in the locked chain). Instrument is guaranteed
   fixed. This enforces BJ's invariant; it does not replace the 7B (LLM-first intact).
4. **Threading the fixed instrument:** the backend response ALREADY returns the structured
   `oscillators` field ([`csound_orch.py`](../../backend/csound_orch.py) ~4080). Recommend:
   the C++ loop stashes it and passes it back on the correction (one extra IPC field) —
   robust, vs. parsing the display gloss `_reading` (~1151, params are lossy there).
5. **This also removes "nothing happens":** a reliable valid correction means the author
   no longer fails → the silent break (2461) no longer fires → the corrected sound plays
   and shows. Plus: make a genuinely-failed correction SAY so instead of silently keeping
   the old finding.
6. **Honest limit:** if an idiom's parameters cannot reach the requested quality (e.g. the
   `flute` idiom sounds like a covered organ and no adjective brightens it enough), the
   correction moves as far as the idiom allows and the self-check may still report a
   residual miss. That is then an IDIOM-QUALITY problem (a separate work order), not a
   correction-mechanism problem.
7. **CLAP description is separate** — see Thread 3.

### OPEN — BJ must decide / next session must deliver
- **[#1 DELIVERABLE] Complete per-instrument PARAMETER TABLE.** BJ: "Tabelle mit
  vollständiger Parameter-Übersicht pro Instrument". This is the "movable" layer made
  explicit so BJ can approve the correction scope. **Data source:** the catalogue the 7B is
  shown, i.e. `dco_llm_map._build_catalogue(lex_cs)` as assembled inside
  `build_csound_response`; per-key params come from `dco_llm_map._params_line(technique)`;
  the technique/adjective/motion lists are `backend/dco_lexicon.json` plus the csound-local
  extensions in `csound_orch.py` (`_CS_TECH_EXTRA`, `_CS_WAVE_HANDOVER`, `_CS_WHY_OVERRIDE`,
  `_CS_WAVE_ALSO`, `_CS_TERMINALS`). Table columns should be: **instrument key** | what it
  is (`why`) | **named params** (name + anchor words + default) | which **adjectives** apply
  | applicable **motions**. Fastest way to produce it: dump `_build_catalogue` output and
  reshape into a table (it already contains every key's `why` + `params:` line, verbatim
  what the 7B sees). Only "a few catalogue keys" carry named params (e.g. `analog_osc`,
  `fm_ep`); most are bare keys whose only movable parameters are adjectives + motion.
- **[#2 DECISION] Approve the design** (build the code-locked parameter-move as above?).
- **[#3 DECISION] Parameter scope:** adjectives + named per-key params + motion
  (recommended), or adjectives + motion only.

### Code map (self-correction path)
- C++ loop + publish + silent break: `src/gui/PromptPanel.cpp` ~2350–2555
  (loop 2447, silent break 2461, `renderBareOscillator` 2484, `analyze` 2490,
  selfcheck `interpret` 2504–2507, final `callAsync` 2527, `formatSelfCheck` in
  `PromptPanel.h` ~199/184).
- Selfcheck stance + mismatch decode: `src/inference/RepromptStances.cpp`
  `syspSelfCheck` (102), `selfCheckReportsMismatch` (313), `buildSelfCheckUserTurn` (336),
  `composeHeardDescription` (~299). NOTE: `syspSelfCheck` was recalibrated to
  dominant-character in commit `3cd55a9f` (in the built binary; verdict correctly returns
  "asked for a flute, but…" i.e. a mismatch — that part works).
- Backend author + correction: `backend/csound_orch.py` `build_csound_response` (3808),
  `_CS_CORRECTION_RULE` (791), `_CS_SYSTEM_PROMPT_HEAD` (810), `_reading` (1151),
  response dict with `"oscillators"` (4075/4080). Wire in `pipe_inference.py` at 3438–3459
  (`mode=="csound"`).
- C++ IPC glue: `PipeInference::authorCsoundOrchestra` (`PipeInference.cpp` 1241; sends
  `correction`/`previous`, timeout 180 s at 1307), `CsoundAuthorResult` (`PipeInference.h`
  ~231).

### UNCOMMITTED intermediate edit
- `backend/csound_orch.py` `_CS_CORRECTION_RULE` was rewritten to remove the "add or
  exchange keys" license and forbid the instrument swap. It is **insufficient alone**
  (probe proved the 7B still swaps) but is a correct improvement the full redesign builds
  on. Keep it; do not revert. It is NOT committed.

---

## THREAD 2 — Csound release bundling (PAUSED, resume after Thread 1)

- **macOS bundling PROVEN.** `tools/bundle_csound_macos.sh` (new, uncommitted) makes a JUCE
  bundle self-contained: flattens Homebrew's `CsoundLib64.framework` to a plain dylib in
  `Contents/libs`, uses `dylibbundler` to pull the whole non-system tree (10 dylibs:
  CsoundLib64 + libsndfile → ogg/vorbis/vorbisenc/FLAC/opus/mpg123/lame + libintl), rewrites
  every load command to `@loader_path/../libs` (NOT `@executable_path` — that resolves to
  the host DAW for VST3/AU plugins), dedupes the duplicate `LC_RPATH` dyld hard-fails on,
  re-seals. **Verified:** zero `/opt/homebrew` refs in the bundle; dlopen loads
  `csoundGetVersion=6181` with every lib from inside the bundle; `codesign --verify` valid.
  The LCO uses only CORE opcodes (vco2/vco/buzz/gbuzz/oscili/poscil/lfo/reson/butterlp) so
  no plugin/opcode dir needs bundling.
- **BJ decided platform scope: ALL THREE** (macOS + Linux + Windows must produce LCO sound).
- **Linux/Windows need Csound build support ADDED** — `CMakeLists.txt:71` gates detection on
  `if(APPLE)` only, so `CsoundEngine.cpp` has NEVER compiled on GCC/MSVC. Each needs
  detection + link + include path + bundling (.so + patchelf `$ORIGIN` on Linux; csound64.dll
  + deps next to the exe on Windows). None locally verifiable on this Mac → CI iteration.
- **CI today bundles no Csound** (`.github/workflows/build.yml`): macOS = `brew install cmake`
  only; Linux apt without csound; Windows nothing. Must add per-platform Csound install +
  the bundling step + a smoke test asserting no `/opt/homebrew` ref survives.
- **Version chosen (tentative): v2.6.0-beta.0.** Last tag was v2.5.3-beta.1.
- **4 LCO release-prep commits already on main:** `3cd55a9f` (selfcheck dominant-char),
  `06c8fa18` (7B resolver path), `135fbbf6` (Alpha panel label), `20b44097`
  (preset-activates-osc). HEAD = `20b44097`.
- **BJ's standing objection:** the absolute-Homebrew-path linkage was the wrong approach for
  shipping; bundling (above) is the correct fix. Do NOT ship the absolute path.

---

## THREAD 3 — CLAP "heard as" description (separate fix)

`analyze_audio` (`pipe_inference.py` 1308) returns `tags` + `spectral`, joined as the
"heard as" line by `composeHeardDescription`.
- `tags` = CLAP top-k from a 110-word vocabulary `CLAP_NAIVE_VOCAB` (1131). CLAP is
  documented to **invert dark sounds** ([[project_lco_offline_orientation_hypotheses]]) —
  hence "bright, glassy, buzzing" for a dark covered-organ tone. **Unreliable.**
- `spectral` = `spectral_words` (1278), a real DSP measurement (centroid/flatness/low-band).
  For a covered organ tone it correctly yields "warm/dark, bass-heavy, tonal". **Reliable.**
- Fix direction (needs BJ, touches the critical-aesthetic "expose the machine's bias"
  mission): demote / qualify / drop the unreliable CLAP half so the self-check and the card
  are not driven by a wrong description. NOT yet done.

---

## Artifacts & session state

- **Uncommitted files:** `tools/bundle_csound_macos.sh` (new), `backend/csound_orch.py`
  (`_CS_CORRECTION_RULE` edit), `docs/plans/HANDOVER_lco_selfcorrect.md` (this file),
  memory: `MEMORY.md` + `project_lco_csound_not_in_ci.md` +
  `project_lco_selfcorrect_is_parameter_move.md`.
- **Scratchpad probes (reusable, IPC path):** `probe_correction.py` (author plain vs
  corrected — the Thread-1 test), `probe_selfcheck.py`, `bare_osc_ear.py` (render + CLAP
  analyze). Backend runs via `.venv/bin/python backend/pipe_inference.py`; 7B lazy-loads
  ~10 s; `flute`/`church organ` are the reproducing prompts.
- **Build:** `build_clean` Standalone mtime Jul 21 01:20 — has the correction loop
  (`4a015031`); may lag HEAD by the last commits. Dev backend is loaded from
  `backend/pipe_inference.py` (no rebuild needed for backend-only changes; BJ tests sound in
  the built Standalone).
- **Process reminders:** work on main, commit per single concern, no branches/PRs; after
  code changes spawn the opus verification agent ("This code has a bug. Find it."); test
  sound only in the built Standalone; nothing sound-shaping without BJ's explicit order
  (the parameter-move IS ordered; its exact scope is being confirmed).

## Immediate next actions (in order)
1. Produce the **per-instrument parameter table** (Thread 1, #1 deliverable — data source
   above) and show BJ.
2. Get BJ's **approval of the parameter-move design + scope** (#2, #3).
3. Build it (backend parameter-move + code lock + thread `oscillators`; C++ silent-break
   honesty), verify with `probe_correction.py` that `flute` stays `flute` with moved params.
4. Fix the CLAP description (Thread 3) with BJ.
5. Resume the Csound all-three-platform bundling + release (Thread 2).
