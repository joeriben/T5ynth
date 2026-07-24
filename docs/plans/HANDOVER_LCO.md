# HANDOVER — the LCO

**Written 2026-07-24.** This is the state of the language-controlled oscillator as it actually runs, the connections between its parts, and what is still open. It is written because the main development session for the LCO cannot assume the next one inherits anything but the tree.

`docs/LCO_CONCEPT.md` remains authoritative **for the goal, the architecture and the invariants**. Its description of *the current code* has gone stale in specific, listed ways — see §7. Read the concept document first; then read §7 of this one before believing any file name in it.

---

## 1. What runs today

**One prompt → one inference against the curated library → the model WRITES one Csound orchestra → it runs.** There is no fallback: no author model, no oscillator. Nothing is prefabricated, nothing is selected from a menu, and nothing outside the emitted Csound shapes the sound.

The path, end to end:

| Step | Where |
|---|---|
| The user types a prompt and hits Generate | `src/gui/PromptPanel.cpp` |
| `mode=csound` request over the stdin/stdout IPC | `src/inference/PipeInference.cpp` → `docs/IPC_PROTOCOL.md` |
| The author model is located | `backend/pipe_inference.py` `_resolve_coder_model_dir` |
| The single model surface is built (`csound_llm`, `accepts_messages = True`) | `backend/pipe_inference.py`, csound branch |
| The prompt is assembled, the model writes, the compiler judges | `backend/lco_write.py` `build_csound_response` |
| The orchestra is compiled and run live | `src/dsp/CsoundEngine.cpp` |

`build_csound_response` returns `{ok, orchestra, params_text, reading, thinking, consultation, repairs, attempts, author_model}`. `params_text` is the authored body; `orchestra` is that body inside the host scaffold; `consultation` is which library entries the prompt's own words reached and which the author was shown anyway; `repairs` is the list of Csound errors the body had to be repaired past. The LCO panel's trace (HEARD / LOOKED UP / WROTE / REPAIRED / RUNNING) is fed from those last two — `tools/lco_trace_wire_check.py` is what proves they survive the wire.

### The host scaffold — the only contract the model has to meet

`lco_write.wrap()` puts the authored body inside a fixed host. The body's single obligation is to write its output into **`asig`**. Everything else is provided:

- `sr` from the host, `ksmps = 64`, `nchnls = 16` (`== CsoundEngine::kMaxVoices`), `0dbfs = 1`
- ftables `giSine` (1), `giCos` (2, a **cosine** table — `gbuzz`'s harmonics are cosines), `giCheb` (3, a GEN13 odd-harmonic transfer function), `giImp` (4, a strike impulse for models whose strike argument is a table number)
- one numeric `instr 1` with `ivoice = p4`, sixteen always-on voice instances, per-voice channels `gate/freq/vel/pres/timb/trig`
- `kfreq limit kfreqraw, 20, 12000`; `koct1/2/3` and `kvol1/2/3` from the player's knobs
- `knote` — seconds since **this** note, reset on `changed2(ktrig)`, `init 0` (a note already gated high at the first k-cycle gives `changed2` no edge, so any other starting value would freeze)
- `aout = asig * kgate * kvel * kpresGain * HEADROOM` (0.32), then `clip aout, 0, 0.95, 0.85`

**The voices are always on.** There is no note-off and no score event per note — which is why every struck instrument in the library is continuously driven (noise, `dust`) rather than one-shot excited: the synth owns the envelope, the oscillator is a spectrum source (`LCO_CONCEPT.md` §4). An idiom whose decay *is* the model cannot stand under this scaffold. See §5.

---

## 2. The library, and how to curate it

`backend/dco_lexicon.json` (`lexicon_version` 10) is the curated source of truth: **30 instruments, 51 adjectives, 17 motions.** An instrument entry carries

- `key` and **surface forms** — the words that reach it. Validation canon; the model never sees them.
- `why` — what the model *does* see.
- `code` — real, working Csound against the scaffold above.
- optionally `params`: each with a measured `range`, a `default`, and **named anchors with a perceptual gloss** (`bow=1.0 (bowed: drawn with a bow: standing, breathing, the top rubbed off)`).
- optionally `anchor_code` — the same instrument rendered at each anchor of its character axis, so the model can see which numbers move with which word.

`tools/lco_build_library.py` assembles the lexicon into `backend/lco_library.json`, which is what `lco_write.render_library()` turns into the author's prompt. `--check` regenerates in memory and fails on drift, so the two cannot silently fall out of step.

**Curating an instrument means editing the lexicon.** The 29 inherited instruments did not start life hand-written: their Csound was machine-harvested **once** from the parked implementation's own emitters (tag `parked/keys-path-csound-20260721`), whose constants were measured and ear-approved, so the library inherited every idiom the old path could produce rather than a smaller vocabulary wearing its name. That harvest is finished and its result is in the lexicon verbatim. **Do not revive the tag to rebuild the library** — a new instrument has nowhere to live in an emitter that never had it, and the build stopped depending on a deleted file (commit `73f91857`, which proved byte-identity of the harvest against the committed library before baking it in).

### Which instruments carry parameters

Seven of thirty:

| key | axes |
|---|---|
| `analog_osc` | `wave`, `drive`, `fat`, `age` |
| `fm_ep` | `ting`, `ring`, `hollowness`, `strike` |
| `drum_head` | `pitched`, `strikepos`, `tension`, `damping` |
| `fm`, `fm_bell`, `metallic_fm` | `index`, `ring`, `detune` |
| `string` | `bow`, `pick`, `damp` |

The other 23 are fixed idioms. That is the growth axis of the library, and `LCO_CONCEPT.md` §1 is what it is for: „Parametrisierungshinweise wie 'square ist sharp wenn Wert x = y, ist hollow wenn x = y'".

### What a prompt reaches

`select(prompt)` looks its words up in the surface forms. A prompt that reaches **nothing** is not an error and does not fail: the author is still shown a default orientation set (`_STARTER`: saw, pwm, fm_bell, additive, struck_bar) up to `_MAX_INSTRUMENTS` (8), and writes the sound from its own knowledge. So a gap in the library shows up as *unoriented*, never as *broken* — which is exactly why gaps are invisible from the sound. Check with:

```bash
.venv/bin/python -c "import sys;sys.path.insert(0,'backend');import lco_write as W;print(W._lookup('accordeon', W._indexes()[0]))"
```

---

## 3. The author, and the repair loop

The author is **gemma-4-12B QAT 4-bit GGUF via llama.cpp** (`~/Library/T5ynth/models/coder/gemma-4-12b-it-qat-q4_0`), greedy (`temperature 0.0`), **`max_tokens=-1`** — no invented output cap; a number there cuts an orchestra off mid-line. `n_ctx` 65536, overridable by `T5YNTH_CODER_N_CTX`. Since 2026-07-22 the same model also does translation and re-prompt; the separate 1.5B translator is gone.

The loop: one inference writes the body, the compiler judges it, and a failure goes back to the model with Csound's own error, up to `MAX_TRIES` (6, `T5YNTH_LCO_MAX_TRIES`). Four properties of it are load-bearing and were each measured into place:

1. **A repair CONTINUES the attempt.** `_continue()` returns `[user, assistant, user]` — the request, the author's own failed answer, the compiler's complaints — so the model edits what it wrote instead of writing the whole sound again from nothing. It keeps the instrument and the movement it already chose, and **the reasoning paragraph it produced the first time stays the answer** (`first_thinking`) rather than being regenerated on every attempt. This requires `llm.accepts_messages = True` on the model surface; a surface without it still works — `_flatten_turn` folds the list back into one user turn — and loses only the KV-cache reuse.
2. **Every distinct error seen so far is shown, not just the latest.** A single-error view makes the author oscillate: told to fix line A it rewrites and breaks line B, told to fix B it reintroduces A, and at temperature 0 that is a permanent 2-cycle no number of retries escapes. Measured on `bright tone -> dark growl`: 6/6 attempts alternating between an inline-`poscil` body and a bare-`atone` one.
3. **An identical reply with no new error stops the loop.** Same conversation in, same output out; the remaining attempts cannot differ, they can only cost. Measured on `accordeon > guitar`: attempts 3 to 6 were the same reply four times.
4. **A mechanical hint on the mechanical mistake.** `_mechanical_hint` recognises `var = opcode arg…` and says so in the model's own terms — Csound's complaint about brackets is a *consequence* of that `=`, and a model told "bracket problem" rewrites the arguments forever.

Python never writes Csound. It names the compiler's complaints; the model writes every fix.

---

## 4. How anything gets measured

Two tools, both committed 2026-07-24, both new — before them there was nothing in `tools/` that drove the shipped author or rendered a body with a meter that had been checked.

```bash
.venv/bin/python tools/lco_measure.py --selftest
```

**Run this before believing any number.** It renders signals whose answer is known — pure sine, saw, a gbuzz stack, a *missing* fundamental (energy only at 2f and 3f), white noise, a comb — and asserts the meter returns them, refuses to invent a pitch for noise, and treats silence, non-finite samples and a body the host's clip would act on as errors rather than results. Then:

```bash
.venv/bin/python tools/lco_measure.py --key string --anchor bowed
```

renders any library entry (or `--anchor` variant, or `--body file`) through a scaffold **derived from `lco_write`'s own `_HEAD`/`_TAIL` by asserted substitution**, so a rename in the host fails the harness loudly instead of quietly measuring a different instrument. It reports pitch in cents, spectral centroid, RMS, p99.9 peak, sustain, comb contrast, per-harmonic levels, and **colour travel over the note at two window lengths**.

```bash
.venv/bin/python tools/lco_author_offline.py --corpus <file> --out run.json --measure
```

drives the **real** author over a whole corpus in one process — the exact call the csound branch of `pipe_inference` makes one line after resolving the model, `accepts_messages` and all — recording per prompt the attempts, the errors repaired past, the body, the reading, what the words reached, and the seconds each inference took. With `--measure` each compiled body is rendered and its movement signature recorded. It is a measurement harness, not a wire test; `tools/lco_trace_wire_check.py` is what proves the IPC path carries the result, and `tools/lco_sanitize_gate.py` guards the prose/code split in the reply.

**Why the movement reading matters, and why one window length is not enough.** Movement by default is a platform fundamental, and a corpus of standing tones passes an implementation that cannot move at all — that is exactly how a static-partial reimplementation once certified itself green while pwm, morph and dirt were silently gone. `pwm`'s duty sweep reads 48 Hz of travel in 0.5 s windows (a full LFO cycle per window, averaged flat) and 696 Hz in 31 ms ones; a standing sine reads 0 at every window length, which is what makes the fast reading evidence rather than noise. Both are reported.

---

## 5. Measured facts — do not re-derive these

Csound 6.18, Homebrew, double precision, no STK.

**Substrate**

- **`gbuzz` normalises to PEAK, not RMS.** Holding a harmonic stack at one loudness while its harmonic count moves needs the closed-form correction `knrm = ((1-kmul^knh)/(1-kmul)) / sqrt((1-kmul^(2*knh))/(1-kmul*kmul)*0.5)`, passed as `0.17 * knrm` in the kamp position.
- **`vco2 imode`: 0 = saw, 2 = pulse/square (`kpw` is the literal ON-duty fraction), 4 = triangle.** `imode 10` is not triangle. Duty is safe to sweep continuously — `imode 2` holds RMS constant at every duty and never clips.
- **`balance astr, aref` against a fixed-loudness yardstick** (`aref poscil 0.35, 400`) holds a resonator's level to within 0.01–0.07 dB across every parameter axis *and* the whole pitch range. The analytic `sqrt(1-g²)` law holds only for a *white* exciter; on a shaped one it drifts.
- **`pluck` is not broken — it is structurally unusable here.** Its decay *is* the model, and there is no note-off under the always-on scaffold. The viable string is `streson` with a shaped exciter (measured against `pluck`, `wgbow`, `wgpluck`, `wgpluck2`, `repluck`, `wguide1`).
- **A lowpass smear on a scattered exciter kills the instrument's colour** (centroid 4500 → 500 Hz, every parameter axis flattened). Allpass diffusion spreads impulses in time *without* darkening — that is why `string` runs a chain of eight `alpass`, not a `tone`.

**Measurement**

- **The peak of a random-impulse process is itself random** — the same body measured 2.89 on one render and 0.79 on the next. Size gains against p99.9, never the maximum.
- **"Loudness may travel ≤ 0.5 dB over a note" was never a platform rule.** Shipped instruments measure 1.46 (drum_head) to 6.28 dB (struck_bar) of within-note travel. `LCO_CONCEPT.md` §4 constrains a **parameter axis** — moving a colour control must not move loudness — not a note's life.
- The three ways a meter has produced confident wrong numbers on this project (FFT-peak f0 on a comb; autocorrelation octave errors; a render that overflowed) are all covered by `lco_measure.py --selftest`. That selftest is the calibration; without it the numbers are opinions.

**The author**

- **The one-line morph template was the single cause of two separate reported defects**: "sine > pwm" rendering as a *static* pulse (b's duty frozen to a constant, with the DC-correction line then reading `- 0.6 * (2 * 0.5 - 1)` — dead code that proves the modulation was dropped), and "harmonic bell > pwm" being read as `+` rather than `>`. Both were fixed by teaching the shape explicitly: each end of a morph is a **whole instrument** with all of its own moving controls, in its own variable, then `kmorph = min(knote / 2.0, 1)` and a crossfade.
- **Renaming for a morph is where `=` creeps in.** All three lines that failed to compile in the reported `accordeon > guitar` run carried rename suffixes (`aacc_base`, `kstr_g`, `kbrth_g`). The instrument was never the problem — the accordion alone is one of the good sounds. The system prompt now says renaming changes the name and nothing else, and that the line keeps its exact shape.
- Compositional prompts landing first try, measured on the shipped author: `sine > saw + square` (2× `vco2`, a real layer at the far end, 46 s), `accordeon > guitar + bell` (2× `streson`, 2× `foscili`, 16 `alpass`, 105 s), `a bowed violin morphing into a bell` (75 s, colour travelling 4754 → 402 Hz across the note), `sine > pwm > bell` (two nested morph positions via `asig_mid`, 63 s). **The architecture change BJ floated — two inferences with the morph done outside — is not needed for these.**

---

## 6. Open items

**Ordered by BJ, not yet built**

1. **A reed-instrument entry.** BJ, 2026-07-24: „ja, mache einen eintrag für reed-instrumente". Today `accordeon`, `accordion`, `akkordeon`, `harmonium`, `bandoneon`, `concertina`, `melodica` and `harmonica` reach **nothing** in the lexicon; only `reed organ` reaches `organ`. The same hole covers the blown reeds — `saxophone`, `oboe`, `bassoon`, `bagpipe` reach nothing either; `clarinet` exists as its own key. Note the nuance before treating this as a bug report: the author writes a *good* accordion from its own knowledge (BJ: „das accordeon alleine ist überhaupt kein Problem → s. preset → klingt großartig"). What is missing is orientation, not sound.

   The mallet family has the same shape of hole: `vibraphone`, `vibes` and `marimba` reach nothing, while `glockenspiel`, `celesta` and `kalimba` reach `struck_bar`. (`rhodes` and `wurlitzer` are fine — they are surface forms of `fm_ep`, not missing instruments; the concept document's „Instruments 4–6" never became separate lexicon entries.)

**Waiting on BJ's ear — these cannot be closed by a gate**

2. The FM family's axes (`index`, `ring`, `detune` on `fm`, `fm_bell`, `metallic_fm`) — bite, fade, shimmer. Built, never heard.
3. The `string`'s three anchors (`bow`, `pick`, `damp`). BJ heard v1 and asked for a more scattered exciter; the current entry is the answer to that and has been heard only as the audition set. Where "plucked", "mixed" and "bowed" sit on the axis is curation, not measurement.

**Deferred by BJ, cause known**

5. `analog_osc`'s `age` is inaudible on a held single note: two of its three instabilities run at 0.043 and 0.057 Hz and are a static offset over a few seconds. The fix is a second, faster instability layer — not more depth. (`LCO_CONCEPT.md` §5.)
6. `fm_ep`'s odd/even balance does not travel over the note.

**Known, not changed because it is runtime semantics and nobody ordered it**

7. `string quartet`, `string ensemble` and `string section` reach **both** `strings` and `string`: the bare word "string" is inside the phrase and `_lookup` collects every match with no longest-wins rule. Harmless today (the author is oriented by both and picks), but it is a real ambiguity in the lookup.

**Housekeeping that needs a decision from BJ**

8. ~457 MB of untracked `tools/*_out/` render directories (over 1 GB counting the ignored ones) and a set of untracked one-off scripts. Deleting anything under `tools/` is BJ's call, not mine. Note before deciding: `backend/dco_frames.py` is untracked but **is imported by the tracked test** `tools/test_dco_author.py:1312`.
9. `docs/plans/HANDOVER_lco_selfcorrect.md` is untracked and describes the self-check/self-correction loop, which was **deactivated 2026-07-21** (`dbd6c153`, `T5YNTH_LCO_SELFCHECK = 0`). It is history, not a plan.

**Traceability gap, reported and not fixable without rewriting history**

10. The morph-template fix to `_SYSTEM_HEAD` was swept into commit `b04c8ef4` ("fix(lco): the trace must not claim what nobody recorded"), whose message does not mention it. Someone bisecting the morph behaviour will not find it there.

---

## 7. What in `docs/LCO_CONCEPT.md` is now historical

The concept document is authoritative for **the goal (§1), the architecture (§2), what an instrument is (§3) and the platform invariants (§4)** — none of that has moved. Its account of the *implementation* predates the switch to the model-authored path (2026-07-22) and now points at code that does not exist:

| In the concept document | Actually, today |
|---|---|
| „Read this before touching … `backend/csound_orch.py`, or `backend/dco_llm_map.py`" | `backend/csound_orch.py` is deleted. The write path is `backend/lco_write.py`. |
| §2: „the prompt goes to a small model (currently `qwen2.5-7b-instruct`)" | The author is gemma-4-12B QAT 4-bit GGUF, and it also does translation and re-prompt. |
| §2: „`build_orchestra()` in `backend/csound_orch.py` turns the reply into an orchestra" | The **model writes the orchestra body**. `lco_write.wrap()` only puts it in the host scaffold. The model no longer names keys for Python to assemble. |
| §5 title: „The three instruments (current proof of concept)" | 30 instruments, 7 of them parametrised. §5's *measured facts* about instruments 1–3 all still hold and are still the best record of them. |
| §8: the ten hand-maintained Python sets, `_ADJ_MAP` as post-mix DSP, `_emit_crossfade_morph` | All of that was `csound_orch.py`. The growth blocker it describes is gone with it; adding an instrument is now one lexicon entry plus a library rebuild. |
| §9 items 5, 6, 7 (cross-cutting properties into generation; the morph as a real waveform morph; the ten sets) | Answered by the architecture change: the model writes the code, so adjectives and morphs are in the emitted Csound by construction. §9 items 1, 2, 3, 4 and 8 stand. |

Also superseded by the same change: the plan file `/Users/joerissen/.claude/plans/hashed-chasing-snowflake.md` ("LCO v1: Adjektive in die Codegenerierung + 4 Familien parametrisieren") is written entirely against `backend/csound_orch.py`. Its *intent* — many adjectives must move parameters inside the algorithm — is now satisfied by the model writing the code. Its mechanics are not implementable.

**This needs BJ's decision:** fold these corrections into `LCO_CONCEPT.md` itself (it is his document and parts are dictated verbatim), or leave the concept document as the record of the design and let this file carry the current state.

---

## 8. Release blockers

Neither is fixed, and both make the LCO non-functional for anyone who is not on this machine.

1. **CI ships no Csound.** `CMakeLists.txt:62-79` finds `CsoundLib64.framework` only in a local Homebrew prefix and is explicit that it is *never required* — „CI runners and any dev machine without Csound installed must build exactly as before" — with `T5YNTH_HAS_CSOUND` always defined so every call site branches at runtime. So the published build compiles green and the LCO is silent in it. `tools/bundle_csound_macos.sh` exists and is not wired into `.github/workflows/`.
2. **There is no acquisition path for the author model.** The 12B GGUF is on the maintainer's disk; nothing in the SetupWizard fetches it. Without it there is no oscillator, by design — no model, no fallback, no tone.

---

## 9. Standing rules that govern work on the LCO

Not repeated here in full — they live in `CLAUDE.md` and in the session memory — but three decide most arguments:

- **A "port X to Y" task is never a licence to rebuild X smaller** (`CLAUDE.md`, Migration & Substrate Discipline). The trigger is objective: it fires whenever a commit adds a fresh implementation of a capability an existing module already provides, or deletes a module the prompt path reached — regardless of what the task was called.
- **Test only in the built Standalone** for anything user-facing (since 2026-07-18). Offline measurement establishes facts; it does not establish that the feature works.
- **Nothing sound-shaping without an explicit order from BJ.** The oscillator is a spectrum source; the synth owns the envelope.
