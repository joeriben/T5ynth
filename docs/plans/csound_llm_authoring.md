# Plan — LLM-authored Csound (the write-path) — v2 (post adversarial review)

Status: **PROPOSAL, NOT approved, NOT yet decided by BJ.** This plan exists so BJ
can decide *whether* to take the write-path at all, with adversarial scrutiny in
hand. v1 was double-adversarially reviewed (two independent Opus reviewers,
read-only); both returned **"does not comply as written — repeats the July-17
regression in a new form."** v2 folds their blocking findings in. The findings +
resolutions are preserved verbatim-condensed in §A at the end (the evidence).

Author: assistant · v1 2026-07-18 · v2 2026-07-18 (after review).

---

## 0. Decision status (CORRECTED — v1 overstated this)

v1 claimed "BJ chose the write-path." That was wrong and a reviewer rightly flagged
it: BJ has **not** decided. His line of questioning ("why a dumb parser?", "the 7B
would use tools like you do — where's the difference?") pushed *toward* the
write-path and dissolved my categorical objection to LLM authoring — but he then
asked for *this vetted plan to decide from*, not for a build. Also on record one
day earlier ([[dco-one-wavetable-no-harmonic-inharmonic-split]], 2026-07-17) is the
**keys-path** direction (LLM → lexicon assembler → orchestra). The write-path is
therefore a **reversal of a day-old recorded direction** and, under
[[feedback_no_silent_fundamental_changes]], needs BJ's explicit written blessing —
including agreeing that "LLM authors against verified idioms" is what he means by
the [[llm-first-never-deterministic-first]] "LLM triggers the deterministic
construction." **Prerequisite 0, gating everything below.**

## 1. Goal (unchanged)

Prompt → the 7B writes a full `CsInstruments` body, composing against a curated
library of verified idioms → an in-process gate compiles + **behaviorally**
validates it → on failure the specific error is fed back for bounded repair → on
pass the existing `CsoundEngine` compiles & plays it; on unrecoverable failure an
honest `ok=false` (never a junk tone, never silence dressed as success).

## 2. The core finding that reshapes the whole plan

Both reviewers converged on this: **the v1 gate checked only STRUCTURE (compiles /
non-silent / peak≤1 / channels resolve / one real opcode). Every platform
fundamental that matters is BEHAVIORAL and invisible to a structural gate.** An
LLM authoring freely will, for ordinary prompts, produce orchestras that pass the
structural gate yet violate:

- **No envelope/glide in the oscillator** ([[feedback_no_unauthorized_sound_features]];
  enforced in code 2026-07-17, `CsoundEngine.cpp:48-54`). "pluck/bell/pad/smooth"
  ⇒ the model writes `linen/madsr/expon/transeg` on `asig` or `portk/port` on
  `kfreq`. Compiles, non-silent, peak≤1 → **passes**. Re-opens the day-old fix.
- **Held-note-live-follow** (BLOCKING Platform Invariant). A decaying orchestra on
  a held note goes silent; an authored free-running/absolute-score-time or
  multi-second trajectory desyncs the `primeForTakeover` (0.25 s pump) crossfade.
- **Movement-by-default** ([[project_lco_movement_by_default]]). Gate asserts
  non-silence, never movement; a static tone passes for "evolving" prompts.

**Therefore the gate must be BEHAVIORAL, not structural, and that is the central
build item — not a detail.** If we cannot gate these behaviorally, the write-path
must not go live (it would ship the exact regression it was meant to avoid).

## 3. Architecture (v2)

```
prompt ─▶ 7B (contract skeleton + idiom library, few-shot; gen-params set for
          │   repetitive code — NOT the default repetition_penalty=1.1)
          │ emits a full CsInstruments body
          ▼
    BEHAVIORAL GATE  (one-shot fork/exec of the check binary per orchestra,
          │           setrlimit(address space) + hard wall-clock timeout)
          │   STRUCTURE: compiles; %SR% marker present & no numeric `sr=`;
          │              exact 16 score lines w/ large p3; 16×6 channels resolve;
          │              real opcode present; no alloc/ftgen/delayr inside reinit.
          │   BOUNDS (swept, not a point): pitch MIDI 21..108, vel/pres/timb at
          │              {0,1}, ≥10 s sustained, all 16 voices, peak≤1, no NaN,
          │              no denormal/idle-CPU spike, per-block CPU within bench.
          │   BEHAVIOR: standing-tone (post-attack RMS does NOT decay over 5 s);
          │              NO amp-envelope/glide opcode on the amp/kfreq path;
          │              movement present when the prompt is not tagged static
          │              (centroid travel / spectral flux); motion is trig-epoch
          │              driven (reinit present), not free-running score time.
          ├─ FAIL ─▶ feed exact error/assertion back; bounded retries (repair
          │           regime + convergence MEASURED, not assumed) ─▶ loop
          └─ PASS ─▶ orchestra text ─▶ CsoundEngine.compile/swap ─▶ audio
                      └─ preset-freeze ONLY once the gate above is complete (§3.5)
```

### 3.1 Idiom library (unchanged intent)
Contract skeleton + few-shot references of the verified idioms (`vco2`+`kpw`,
`foscili`, `tanh` dirt, additive+`linseg`+`changed2`/`reinit` morph, motion LFOs;
`pvsmorph`/`ftmorf` flagged budget-hazardous, see §3.3). Real, capability-complete.

### 3.2 Generation contract (unchanged intent, see §A for the holes the gate must close)

### 3.3 The BEHAVIORAL gate (rewritten — this is now the heart of the plan)
The runtime validator (promoted from `tools/csound_orch_check`) must assert **all**
of the STRUCTURE / BOUNDS / BEHAVIOR checks in the diagram above. Note `pvsanal`+
`pvsmorph`+`pvsynth` × 16 always-on voices is an xrun risk → the gate must run the
`bench` budget (133 µs/ksmps) at N gated voices and reject over-budget orchestras.
Denormals/idle-CPU are neither NaN nor peak>1 — a dedicated check is required
(idle-CPU is the #1 historical bug class).

### 3.4 Verify-repair loop
Repair sampling regime specified and **isolated to the csound branch only** (the
shared `run_instruct` determinism trap forbids a global param change). Convergence
and oscillation rate are an **empirical unknown** (§7), not an assumed mechanism.

### 3.5 Preset reproducibility (guarded)
Freeze the verified orchestra text into the `.t5p` **only after §3.3 is complete** —
otherwise the freeze makes gate-passing-but-flawed orchestras permanent and
unregenerable. Store a **contract-version stamp**; on a future contract change,
surface an explicit "saved orchestra incompatible with current engine" error
rather than silent inertness. `%SR%` presence is a hard gate precondition (else a
frozen literal-rate orchestra detunes on reload).

## 4. The Csound-in-the-backend gate (CORRECTED)
**One-shot `fork/exec` of the check binary per orchestra**, with `setrlimit`
(address space) + wall-clock timeout. This gives full crash isolation, a fresh
`CSOUND*` each time (OS reclaims all memory/handles on exit), and reuses the
proven CLI unchanged. v1's "persistent helper" was wrong: `csound_orch_check` is a
one-shot argv CLI (no server exists), so a long-lived helper is net-new unproven
code that also accumulates Csound per-instance leaks and loses crash isolation.
(Rejected: in-process ctypes — a malformed graph that aborts Csound takes down the
whole inference backend.)

## 5. Parity corpus (CORRECTED — v1's was self-certifying)
`tools/csound_slice_parity.py` feeds the implementation's own keys directly and
**never invokes the LLM** — it is not the author path and is regenerable from the
new vocabulary, exactly the blind-spot suite the migration rule forbids. The real
parity artifact is a **frozen natural-language PROMPT corpus** (BJ's own cases:
"pwm square wave", "glass morphing into a sine", dirty/moving bass, metallic bell,
plus the keys-path's full capability surface), whose objective signatures
(centroid travel / THD / duty-sideband) are captured **end-to-end from the
keys-path** (prompt→7B→orchestra→render) and stored so they **cannot** be
regenerated from the author vocabulary. The write-path must reproduce those
frozen signatures on the same prompt path before the keys-path is retired.

## 6. Phasing (CORRECTED — shadow, not a live regression)

- **Phase 0 — decide the blind bet.** Prerequisite 0 (§0) + accept that, because
  BJ has (reasonably) forbidden pre-testing, the write-path's two viability
  unknowns — **wall-clock latency** of an ~800+-token greedy generation on the
  target Mac, and **7B author+repair reliability** — are bet on BLIND, with the
  gate as the only safety net. If latency turns out to be minutes, the write-path
  is not a live prompt path but an offline "author-once-freeze" tool, a different
  product. BJ decides knowing this.
- **Phase 1 — behavioral gate as a runtime component** (fork/exec, §3.3/§4), run
  first on the CURRENT keys-path output (known-good) to prove the gate. Commit.
- **Phase 2 — 7B authors in SHADOW.** Author + gate + log pass-rate; **the
  keys-path stays the LIVE `mode=="csound"` behavior** — the write-path routes no
  audio until Phase-5 parity. (v1's "ship the raw pass-rate as product behaviour"
  was a user-facing silence regression; corrected.) Commit.
- **Phase 3 — verify-repair loop** with the measured regime (§3.4). Commit.
- **Phase 4 — preset freeze** (guarded, §3.5). Commit.
- **Phase 5 — parity sign-off** on the frozen PROMPT corpus (§5); only then flip
  the live route and retire the keys-path source into the frozen corpus. Commit.

## 7. Risks & open questions (honest, expanded)

- **Latency may disqualify the LIVE path** (~10× the token budget v1 wrongly
  invoked; path is pinned at `_MAX_NEW_TOKENS=120` today). Unknown until measured;
  BJ forbade measuring first → bet blind or relax that for one measurement.
- **7B Csound author+repair reliability unproven**; greedy decoding can oscillate
  on repair; Csound is niche in a 7B-instruct's training. Surfaces in Phase 2
  shadow as real pass-rate, not a throwaway PoC.
- **`repetition_penalty=1.1` fights the required repetition** (16 score lines, 12
  `oscili`, 6 `sprintf`/`chnget`) → drift/omission. Must set params deliberately,
  verified at the function boundary ([[project_run_instruct_determinism_trap]]).
- **Held-note takeover** needs a Csound-specific audition mirroring
  `tools/audition_sampler_follow.cpp` (the named guard doesn't cover authored
  orchestras).
- **Two-path window**: during Phases 1–4 the keys-path is live and the write-path
  is shadow; the shared layer (`dco_llm_map`, lexicon, `_parse_reply`,
  `_validate_keys`) must not bit-rot the keys-path baseline unnoticed.

## 8. Acceptance criteria (unchanged intent; now gated by the behavioral gate + frozen prompt corpus)
BJ's canonical prompts author, **behaviorally** gate-pass, and play in the running
plugin (BJ plays it + objective signatures on the real author→gate output — no
demo WAVs); the capability CLASS passes; presets reproduce with a contract stamp;
parity on the frozen prompt corpus demonstrated before retiring the keys-path.

## 9. The working baseline (currently uncommitted)
Keys-path runs end-to-end: slice `febda214` + two uncommitted fixes on disk in
`backend/csound_orch.py` (comma-tolerant technique parse; morph endpoints for
every waveform) — BJ's failing cases work through the real 7B *today*. Stays as
the live behavior/parity reference; safe on disk; commit-or-not is BJ's call.

---

## A. Adversarial review v1 — confirmed findings (the evidence, preserved)

Two independent Opus reviewers, read-only. "R1" = technical/RT-safety lens, "R2" =
fundamentals/migration lens. Both verdicts: **does not comply as written.** Findings
where they independently converged are marked ⚑ (higher confidence).

1. ⚑ **Standing-tone / envelope-in-oscillator undetectable** (R1#1, R2#1). Gate
   holds gate+trig constant 1 s, asserts only non-silence/NaN/peak — a decay/
   envelope/glide passes. Re-opens `no_unauthorized_sound_features` (code'd
   2026-07-17) and breaks held-note-follow. → §2, §3.3 (standing-tone RMS + negative
   envelope/glide check).
2. ⚑ **Held-note-live-follow breakable, untested** (R1#7, R2#2). `primeForTakeover`
   pumps 0.25 s; authored multi-second/free-running trajectory desyncs the
   crossfade; guard covers sampler/WT/freeze only. → §3.3 (trig-epoch assertion),
   §7 (Csound takeover audition).
3. ⚑ **Parity corpus self-certifying** (R2#3, both). `csound_slice_parity.py` feeds
   own keys, never the LLM → migration-rule blind-spot suite. → §5 (frozen prompt
   corpus, end-to-end, non-regenerable).
4. ⚑ **Bounds gate insufficient** (R1#4, R2#4). One (voice,220Hz,1s,fixed
   vel/pres/timb) point ≠ 16-voice, full pitch/pres/timb range, sustained;
   self-oscillating filters at extremes, pres→FM-index blowup at aftertouch=1,
   integrators/feedback over time, denormals; `pvsmorph`×16 xrun. → §3.3 (swept
   bounds + bench).
5. ⚑ **Latency categorically wrong** (R1#2, R2 corroborates). ~600–1200 tokens vs
   120; pinned at `_MAX_NEW_TOKENS=120`; ×3 repair ≈ 4 gens; "budget already
   tolerated" false by ~10×. → §6 Phase 0, §7.
6. **No timeout/rlimit → hang/OOM** (R1#5). `ftgen 1,0,1GB` / `delayr 1e6` allocs at
   compile before the 1 s bound; none exists in the gate. → §4 (fork/exec +
   setrlimit + timeout).
7. **Csound-in-backend recommendation wrong** (R1#9). No persistent server exists;
   persistent helper = unproven + leaks + no isolation. → §4 (one-shot fork/exec).
8. **Compiles+passes yet violates contract later** (R1#6). Short score p3 (voices
   die after N s); literal `sr=48000` (no `%SR%`-presence check; no compiled-SR vs
   host-SR check in `prepare()`; detunes on reload); allocating code inside `reinit`
   (audio-thread alloc per note). → §3.3 (score/p3, %SR%, reinit checks).
9. ⚑ **Movement-by-default untested on live path; owning-layer unstated** (R2#5,
   R1). → §2, §3.3 (movement signature), state Csound owns movement via trig-epoch.
10. **`repetition_penalty=1.1` fights required repetition** (R1#8, R2#8). → §3.4, §7.
11. **Phase 2 routed live = silence regression** (R1#10, R2#9). → §6 (shadow).
12. **Preset-freeze makes flawed orchestras permanent/unregenerable; future
    contract change → silent inert preset** (R1#11). → §3.5 (guard + version stamp).
13. **§0 premise unverified / reverses day-old direction; LLM-first only thinly
    satisfied** (R2#6). → §0 (Prerequisite 0: BJ's written blessing).
