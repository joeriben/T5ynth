# HANDOVER — the LCO

**Written 2026-07-24, updated 2026-07-25.** This is the state of the language-controlled oscillator as it actually runs, the connections between its parts, and what is still open. It is written because the main development session for the LCO cannot assume the next one inherits anything but the tree.

The 2026-07-25 pass added 33 instruments (30 → 63) and 35 parametrisations (7 → 42), and it corrected three things this document previously stated wrongly: the host's clip ceiling, who owns the note-off, and where a note begins in the measuring harness. Those three are in §5 and they invalidate figures measured before that date.

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
| The author reads the index and names what it wants opened | `backend/lco_write.py` `_consult` → `named_entries` → `open_entries` |
| Those entries' Csound is handed over, the model writes, the compiler judges | `backend/lco_write.py` `build_csound_response` |
| The orchestra is compiled and run live | `src/dsp/CsoundEngine.cpp` |

`build_csound_response` returns `{ok, orchestra, params_text, reading, thinking, consultation, repairs, attempts, author_model}`. `params_text` is the authored body; `orchestra` is that body inside the host scaffold; `consultation` is `{named, opened, library_size}` — what the AUTHOR asked for and what was actually put in front of it (the same set, unless no instrument was recognised: then the whole library is opened, and the two must stay distinguishable or the panel reads a full `opened` back as "the author chose nothing"); `repairs` is the list of Csound errors the body had to be repaired past. The LCO panel's trace (HEARD / OPENED / WROTE / REPAIRED / RUNNING) is fed from those last two — `tools/lco_trace_wire_check.py` is what proves they survive the wire.

### The host scaffold — the only contract the model has to meet

`lco_write.wrap()` puts the authored body inside a fixed host. The body's single obligation is to write its output into **`asig`**. Everything else is provided:

- `sr` from the host, `ksmps = 64`, `nchnls = 16` (`== CsoundEngine::kMaxVoices`), `0dbfs = 1`
- ftables `giSine` (1), `giCos` (2, a **cosine** table — `gbuzz`'s harmonics are cosines), `giCheb` (3, a GEN13 odd-harmonic transfer function), `giImp` (4, a strike impulse for models whose strike argument is a table number)
- one numeric `instr 1` with `ivoice = p4`, sixteen always-on voice instances, per-voice channels `gate/freq/vel/pres/timb/trig`
- `kfreq limit kfreqraw, 20, 12000`; `koct1/2/3` and `kvol1/2/3` from the player's knobs
- `knote` — seconds since **this** note, reset on `changed2(ktrig)`, `init 0` (a note already gated high at the first k-cycle gives `changed2` no edge, so any other starting value would freeze)
- `aout = asig * kgate * kpresGain * HEADROOM` (0.32), then `clip aout, 0, 0.95, 0.85` — whose real ceiling is **2.523 transparent / 2.746 absolute** in body units, not `0.95/0.32` (§5). `kvel` was removed from this line 2026-07-25 (BJ): the voice envelope's peak already tracks velocity, so the extra factor made the LRO scale as vel² where every other engine is linear; presets saved before that date carry the old line in their stored orchestra text until re-baked

**The voices are always on.** There is no score event per note; a note is an edge in `ktrig`, and the release is `kgate portk kgateraw, 0.001` in the host — measured 6.98 ms to −40 dB, identical at every body setting (§5). So the body can neither be one-shot excited nor own a decay: every struck instrument in the library is continuously driven (noise, `dust`), the synth owns the envelope, and the oscillator is a spectrum source (`LCO_CONCEPT.md` §4). An idiom whose decay *is* the model cannot stand under this scaffold.

---

## 2. The library, and how to curate it

`backend/dco_lexicon.json` (`lexicon_version` 41 as of 2026-07-25) is the curated source of truth: **64 instruments, 51 adjectives, 17 motions**, every instrument with working Csound. 62 of them ship: `noise`, `pink_noise` are WITHHELD from the library on purpose (the synth already gives the player its own noise) without being deleted, so every count below distinguishes lexicon from shipped. An instrument entry carries

- `key` and **surface forms** — the words that reach it. Validation canon; the model never sees them.
- `why` — what the model *does* see.
- `code` — real, working Csound against the scaffold above.
- optionally `params`: each with a measured `range`, a `default`, and **named anchors with a perceptual gloss** (`bow=1.0 (bowed: drawn with a bow: standing, breathing, the top rubbed off)`).
- optionally `anchor_code` — the same instrument rendered at each anchor of its character axis, so the model can see which numbers move with which word.

`tools/lco_build_library.py` assembles the lexicon into `backend/lco_library.json`, which is what `lco_write.render_library()` turns into the author's prompt. `--check` regenerates in memory and fails on drift, so the two cannot silently fall out of step.

**Curating an instrument means editing the lexicon.** The 29 inherited instruments did not start life hand-written: their Csound was machine-harvested **once** from the parked implementation's own emitters (tag `parked/keys-path-csound-20260721`), whose constants were measured and ear-approved, so the library inherited every idiom the old path could produce rather than a smaller vocabulary wearing its name. That harvest is finished and its result is in the lexicon verbatim. **Do not revive the tag to rebuild the library** — a new instrument has nowhere to live in an emitter that never had it, and the build stopped depending on a deleted file (commit `73f91857`, which proved byte-identity of the harvest against the committed library before baking it in).

### Which instruments carry parameters

**57 of 64** as of 2026-07-25 (it was 7 of 30 on 2026-07-24, and 42 of 63 earlier the same day). The 7 without axes are: `chiptune`, `hiss`, `noise`, `pink_noise`, `ring_mod`, `sine`, `vibraphone`. That list is the growth axis of the library, and `LCO_CONCEPT.md` §1 is what it is for: „Parametrisierungshinweise wie 'square ist sharp wenn Wert x = y, ist hollow wenn x = y'". Note that several of them are exactly the entries open item 4 asks about — a plain `noise` may not want a Csound axis at all.

Every axis in the library is a **measured** range with named anchors, gated by `tools/lco_param_audit.py` (§4): the audit's own counts move with every batch, so run it rather than quoting it here; it exits 2 on a key it does not have instead of printing an empty clean pass and explained in the tool itself rather than in a commit message.

### What the author gets to see — the consultation, BUILT 2026-07-24

**The rule (BJ, 2026-07-24, standing): it is strictly forbidden to constrain the LLM deterministically, in any form.**

The old code violated it. `select(prompt)` matched the prompt's words against the surface forms and **decided by word comparison which library entries the author was allowed to see**; a prompt that matched nothing got a fixed default set (`_STARTER`: saw, pwm, fm_bell, additive, struck_bar), capped at `_MAX_INSTRUMENTS` (8). It never blocked the writing and never picked the sound — the author wrote either way — but it did deterministically decide the orientation, with exactly the holes a word matcher has: `bell` hit, `bells` did not, German nothing.

**BJ's own words for the replacement (2026-07-24):**

> „Es geht eine Liste mit Instrumenten und sonischen Beschreibungen der Parameter in den Prompt. Thinking wird dann nicht-deterministisch entscheiden was im nächsten Zug dem LLM aus der Bibliothek zur Verfügung gestellt wird."

Two turns, and both are one authoring of one sound:

1. The system prompt carries **the index** (`render_index`) — every entry, what it is, and what its parameters do sonically (the `why` line and the anchor glosses: `string` → `bow` "bowed: drawn with a bow: standing, breathing, the top rubbed off", `pick`, `damp`). No Csound. It was **26 965 characters, ~6.7k tokens** on 2026-07-24 with 98 entries — about what the old 8-entry word-matched excerpt cost. With 131 entries and 42 parametrised it is **113 019 characters, ~28k tokens** (2026-07-25). It is still a stable prefix, so it caches, but it is no longer small, and the whole library is 295 483 characters — the index is now 38 % of what it was meant to be a cheap table of contents for. That is open item 6.
2. `_consult()` asks the author to think about what the sound IS and name what it wants opened. `named_entries()` reads its reply, `open_entries()` fetches exactly those, `_writing_turn()` hands their real Csound over as the next user turn in the same conversation. **Python decides nothing** — it recognises the names the model used. A reply that names nothing opens the WHOLE library; there is no default set any more.

The curated code exists in order to be seen; the question was only *when*, and the answer is "in turn two, chosen by the author". The surface forms stop being a routing layer over the user's prompt and remain what they always were underneath: the lexicon's validation canon, now also how Python recognises what the author asked for.

Consequences, all landed: the panel station is **OPENED**, not LOOKED UP — it lists what the author asked for and says so, and when the author named nothing it says the whole library was opened rather than printing 98 chips as if they had been chosen. The reasoning now arrives in the consultation, which streams as **`attempt: 0`** (§4.6 of `docs/IPC_PROTOCOL.md`); the writing turn is asked not to reason twice, so on a first-try authoring no reasoning frame ever arrives under attempt 1.

To see what the author asked for on a given prompt, read `consultation.opened` in the answer — e.g. from `tools/lco_author_offline.py --out …`.

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

renders any library entry (or `--anchor` variant, or `--body file`) through a scaffold **derived from `lco_write`'s own `_HEAD`/`_TAIL` by asserted substitution**, so a rename in the host fails the harness loudly instead of quietly measuring a different instrument. It reports pitch in cents, spectral centroid, RMS, p99.9 peak, `peak_late`, sustain, comb contrast, per-harmonic levels, and **colour travel over the note at two window lengths**.

**Three things about that harness are load-bearing and were wrong until 2026-07-25.**

- **Headroom is judged on `peak_late` — the peak after the first `ONSET_S = 0.050` s — against the host's real ceilings** (`HOST_TRANSPARENT = 2.523`, `HOST_CLIP = 2.746`; §5). The percentile is the right size for *dimensioning* a gain, but the host clips on the true peak, so the guard cannot use p99.9; and the true peak of the whole render condemns any body whose first k-cycles start from zero under `balance`, which the plugin never hears. Both errors were live: the percentile let `ice` through, the true peak condemned `string` and `ice` for a start-up transient.
- **`--preroll` reproduces the host's note-start, and without it two whole classes measure wrongly.** The host scores sixteen always-on instances (`i 1 0 360000 …`) and signals a note through `ktrig`/`kgate`, with `knote init 0` reset by `changed2(ktrig)`. The harness used to score `i 1 0 dur 1` with `ktrig = 1` constant, so the instance *began* at the note. Anything whose reading depends on how long the instance has been running reads a fiction: `balance`, whose RMS denominator starts at zero (`string` reads 3.179 at t = 4.4 ms with `balance`, 0.622 with a fixed gain), and every free-running k-rate LFO, whose phase at note-on is the plugin's uptime. `--preroll` runs the instance before the edge and trims at the k-period boundary (`preroll_edge()`, not `int(preroll*SR)` — the selftest asserts bit-identity from sample 64 across five prerolls).
- **Pitch on an inharmonic set is `tracking()`, never `f0` or the loudest partial.** A bell, a bar, a glass or a gourd has no partial at the played frequency; asking for one produces a confident wrong number and a bug report. The valid question is whether the whole spectrum *transposes* with the note: correlate a 110→880 Hz glide against a fixed reference. Measured `tracks` for `mbira` (r_note 0.64 / r_fixed −0.03), `struck_bar` (0.77), `handpan` (0.717), `fm_bell` (0.828), `glass` (0.493), waterphone (0.675/−0.042).

**The harness renders at 44100 and the plugin no longer does.** Since `5e65c0d4` the plugin compiles Csound at `sampleRate × factor` (1/2/4, user-set in Settings since `667d8bea`) and decimates per voice with a 63-tap halfband, so `%SR%` in a body resolves to the *oversampled* rate. Everything in §5 was measured at 1×. Aliasing verdicts from this harness are therefore a worst case, and anything that depends on the decimator belongs in `tools/audition_lro_oversampling.cpp`, not here — there is deliberately no Python decimator.

```bash
.venv/bin/python tools/lco_author_offline.py --corpus <file> --out run.json --measure
```

drives the **real** author over a whole corpus in one process — the exact call the csound branch of `pipe_inference` makes one line after resolving the model, `accepts_messages` and all — recording per prompt the attempts, the errors repaired past, the body, the reading, what the words reached, and the seconds each inference took. With `--measure` each compiled body is rendered and its movement signature recorded. It is a measurement harness, not a wire test; `tools/lco_trace_wire_check.py` is what proves the IPC path carries the result, and `tools/lco_sanitize_gate.py` guards the prose/code split in the reply.

```bash
.venv/bin/python tools/lco_param_audit.py            # every instrument, every axis
.venv/bin/python tools/lco_axis_probe.py --body cand.csd --all --gate
```

`lco_param_audit.py` (2026-07-25) turns `LCO_CONCEPT.md` §3 and §4 into checks that fail out loud: four structural ones read from the library with no render (parameters exist; each axis has a range, an in-range default, ≥2 named anchors with a perceptual gloss, a `note`, and `anchor_code` variants naming axes that exist), and seven rendered ones through the calibrated meter — renders clean at 55/220/1760 Hz, **tracks** the keyboard across them, stands without self-decay, **moves**, each axis is a colour control and not a volume fader (the failure that already shipped once), each axis actually does something measurable, and the keyboard itself is not a volume control. Findings that are legitimate get a **named exemption inside the tool**, with the reason, rather than an explanation in a commit message nobody will read next to the number. `lco_axis_probe.py --gate` is the same set of questions asked of a *candidate* body before it enters the lexicon.

**Why the movement reading matters, and why one window length is not enough.** Movement by default is a platform fundamental, and a corpus of standing tones passes an implementation that cannot move at all — that is exactly how a static-partial reimplementation once certified itself green while pwm, morph and dirt were silently gone. `pwm`'s duty sweep reads 48 Hz of travel in 0.5 s windows (a full LFO cycle per window, averaged flat) and 696 Hz in 31 ms ones; a standing sine reads 0 at every window length, which is what makes the fast reading evidence rather than noise. Both are reported.

---

## 5. Measured facts — do not re-derive these

Csound 6.18, Homebrew, double precision, no STK.

**The host scaffold — what a body can never own** (all measured 2026-07-25 through `lco_write`'s own `_TAIL`)

- **The clip ceiling is 2.523 transparent / 2.746 absolute in body units, not `0.95 / HEADROOM` = 2.969.** The voice ends on `aout clip aout, 0, 0.95, 0.85`, and in Csound's `clip` with `imeth 0` the FOURTH argument is the fraction of `ilimit` at which limiting *begins* — the curve never reaches 0.95. Ramping 0..3 through that exact line: transparent to 0.78, 0.84 → 0.838393, 0.90 → 0.872578, **≥ 0.96 → 0.878750, rigid, for ever.** So `HOST_TRANSPARENT = 0.85·0.95/HEADROOM = 2.523` and `HOST_CLIP = 0.87875/HEADROOM = 2.746`; with blowing pressure at maximum (`kpresGain = 1 + 0.15·kpres` → 1.15) the transparent bound falls to 2.194. The old constant called fifteen shipped renders clean that the host was already reshaping. `lco_measure._clip_transfer()` re-measures the curve from `_TAIL`'s own arguments in the selftest, because a body's render is `asig` — *upstream* of the clip — so the harness can never observe clipping by rendering a loud body.
- **The host owns the note-off, so a body can never own a ring time.** `kgate portk kgateraw, 0.001` then `aout = asig * kgate * …`: a 1 ms half-time is −40 dB in ~6.6 ms. Measured on `cymbal`, `glass` and `struck_bar` with `ring` at 0.00 / 0.50 / 1.00, gate dropped at t = 2 s: **6.98 / 6.98 / 6.98 ms — identical.** "choked — caught by the hand" and "let ring — a full open wash" are the same release. Such an axis is real, but what it moves is the timbre of the HELD note (comb contrast `glass` 27.6 → 39.0 dB, `struck_bar` 16.5 → 35.5 dB); name it for that.
- **`balance` in a body and a decay in the same body are mutually exclusive.** The AGC pulls the tail back up: a T60 of 3.48 s measures as 5891 s or ∞. Whoever adds `balance` against register tilt cannot afterwards put an envelope in that body.
- **A third thing `balance` costs: the note's start depends on how long the plugin has been open.** Its averager has no note-on reset, so the gain it happens to hold at the edge is a function of uptime. `struck_bar`, `glass`, `cymbal`, `tanpura`, `string` and the FM family all read it; 29 shipped bodies carry a `balance`, and eight of them are ones only this rule made visible (`fm`, `fm_bell`, `fm_ep`, `drum_head`, `metallic_fm`, `struck_bar`, `cymbal`, `glass`). It is REPORTED, not gated: §4 has no rule about it, and the only cure is to key what must happen after the attack to `knote`, which the host does reset.

  ⚠ **There is no per-body ranking for this class, and two attempts at one have now been withdrawn. Do not write a third.** A single grid of four plugin ages gives a number that is a property of the four ages, not of the body: three grids of the same construction read medians 73 / 137 / 107 Hz over the same bodies, `sync` 498 / 1182 / 908, `glass` 67 / 289 / 298 — so the older 303-vs-340 disagreement that was called dead is *inside the spread the grid choice alone produces*. **31 of 63 bodies read 2x apart or more across grids** (`overtone_voice` 4–207, `handpan` 21–182, `free_reed` 22–172, `pink_noise` 132–493). And for a noise-excited body the question cannot be answered by varying the preroll at all, because that varies which noise samples the note gets: the control is to hold the age and change only the seed (`lco_measure.reseed`).

  **What survives is a verdict per body, measured 2026-07-25 by `tools/lco_phase_census.py`** — three grids plus a four-seed control, unrounded, `centroid_audible`:

  - **43 depend on plugin uptime beyond their own noise.** Everything tonal with a slow free-running control: the whole waveform family, the reeds, the FM trio, `string`/`strings`/`supersaw`/`sync`/`tanpura`/`surf`/`wind`, plus `noise` and `hiss`.
  - **11 move, but not separably from the exciter's dice** — `bubbles`, `cymbal`, `drum_head`, `glass`, `handpan`, `ice`, `mbira`, `pink_noise`, `rain`, `struck_bar`, `thunder`. Their seed-only control is at or above their four-age spread. `mbira` 828 and `glass` 67 were both published as uptime figures and are both in this class. Note what this set *is*: the noise-excited resonator banks and the event textures — the same bodies as item 9's second group, and for the same reason.
  - **9 read under 1 Hz on every grid** — `bass_saw`, `brass`, `rhodes`, `sine`, `triangle`, `vibraphone`, `voice_ee`, `voice_oo`, `wurlitzer`.

  Two traps this cost, both now wired into the tool rather than written down. **A body that was not rendered gets no verdict:** the first version printed `free_running`'s skip list as "not uptime-dependent at all" and three of the seven were wrong — `pink_noise` reads 216 Hz, `noise` 250, `hiss` 165, because `free_running`'s `_AGEN` pattern matches no a-rate generator and a bare `anz rand 1.0, 0.5, 1` is invisible to it. It now disagrees with the measurement on **8** bodies and prints which. **And 0.0 Hz was never proof of independence:** `measure()` rounds `centroid_over_note` to whole Hz, which is why `bass_saw` (0.287 Hz) and `voice_ee` (0.117 Hz) once read zero; the census reads `travel()` unrounded, where a true zero (`sine`) is exactly 0.000000.

**Substrate**

- **`gbuzz` normalises to PEAK, not RMS.** Holding a harmonic stack at one loudness while its harmonic count moves needs the closed-form correction `knrm = ((1-kmul^knh)/(1-kmul)) / sqrt((1-kmul^(2*knh))/(1-kmul*kmul)*0.5)`, passed as `0.17 * knrm` in the kamp position.
- **`vco2 imode`: 0 = saw, 2 = pulse/square (`kpw` is the literal ON-duty fraction), 4 = triangle.** `imode 10` is not triangle. Duty is safe to sweep continuously — `imode 2` holds RMS constant at every duty and never clips.
- **`balance astr, aref` against a fixed-loudness yardstick** (`aref poscil 0.35, 400`) holds a resonator's level to within 0.01–0.07 dB across every parameter axis *and* the whole pitch range. The analytic `sqrt(1-g²)` law holds only for a *white* exciter; on a shaped one it drifts. **That 0.01–0.07 dB is the STATIONARY level and nothing else** — the commits claiming "the timbre is untouched" are wrong about the attack. First 20 ms relative to the stationary mean, before → after adding `balance`: `glass` −16.5 → **+2.4 dB**, `struck_bar` −13.7 → +3.0, `cymbal` −6.9 → +2.0, `brass` +4.7 → −0.2, `organ` +2.3 → −0.1. It also starts from a zero denominator, which is why headroom is judged after 50 ms.
- **`pluck` is not broken — it is structurally unusable here.** Its decay *is* the model, and the note-off belongs to the host's `portk`, not to the body (above). The viable string is `streson` with a shaped exciter (measured against `pluck`, `wgbow`, `wgpluck`, `wgpluck2`, `repluck`, `wguide1`).
- **A lowpass smear on a scattered exciter kills the instrument's colour** (centroid 4500 → 500 Hz, every parameter axis flattened). Allpass diffusion spreads impulses in time *without* darkening — that is why `string` runs a chain of eight `alpass`, not a `tone`.

**Measurement**

- **The peak of a random-impulse process is itself random** — the same body measured 2.89 on one render and 0.79 on the next. Size gains against p99.9, never the maximum.
- **"Loudness may travel ≤ 0.5 dB over a note" was never a platform rule.** Shipped instruments measure 1.46 (drum_head) to 6.28 dB (struck_bar) of within-note travel. `LCO_CONCEPT.md` §4 constrains a **parameter axis** — moving a colour control must not move loudness — not a note's life.
- The ways a meter has produced confident wrong numbers on this project (FFT-peak f0 on a comb; autocorrelation octave errors; a render that overflowed; a ceiling read off an opcode's arguments instead of its curve; a note that began with its instance) are all covered by `lco_measure.py --selftest` — 88 cases as of 2026-07-25. That selftest is the calibration; without it the numbers are opinions. **Each new constant goes in with a case that FAILS when the constant is wrong**, not merely one that passes when it is right: the first version of the clip cases stayed green at `HOST_TRANSPARENT × 0.7`, a value that would have condemned `ice` and `bagpipe`.
- **No span bound separates a moving sound from a standing one, and that is asserted as a negative result.** A real sweep travels 959 cents; a static noise bed reads 1005 (`STATIONARY_SPAN_NULL_CENTS`), and a static bed's crest reads 14.55 dB (`STATIONARY_CREST_NULL_DB`). Movement is `span > 60 cents AND motion_coherence > 0.35` — the coherence term is the one doing the work, and anyone tempted to "simplify" the gate to a span threshold has a selftest case waiting.
- **The movement gate has an exemption that is a DECLARATION, not a measurement.** BJ unblocked the event-texture class on 2026-07-25: a body may write `; MOVEMENT: TEXTURE` at the top with its axes, and `lco_param_audit` then *reports* its movement reading instead of failing on it. This exists because no measure separates a rain-like event texture from static noise — so it is deliberately on the author's word. It is the second exemption to movement-by-default and the only one that cannot be verified; keep the count honest (`lco_axis_probe._MOVE_CENSUS`, currently 37 moving / 26 standing / 5 of those standing ones declaring texture — six bodies carry the declaration, and `bubbles` is the one that declares it and moves anyway).

**The author**

- **The one-line morph template was the single cause of two separate reported defects**: "sine > pwm" rendering as a *static* pulse (b's duty frozen to a constant, with the DC-correction line then reading `- 0.6 * (2 * 0.5 - 1)` — dead code that proves the modulation was dropped), and "harmonic bell > pwm" being read as `+` rather than `>`. Both were fixed by teaching the shape explicitly: each end of a morph is a **whole instrument** with all of its own moving controls, in its own variable, then `kmorph = min(knote / 2.0, 1)` and a crossfade.
- **An `=` written before an opcode is the author's dominant compile failure, and NOTHING in the system prompt has fixed it.** The shape is `asig_b = tone asig_b, 400` — an opcode statement typed as an assignment. Csound answers with a bracket complaint that names neither the `=` nor the opcode, so it is easy to chase for rounds without seeing it. A paragraph was added to `_SYSTEM_HEAD` in `b3bd2b19` to prevent it, telling the author that for a morph "what you take from the library has to be renamed" and that "the line KEEPS ITS EXACT SHAPE". **Measured across `tools/lco_morph_corpus.txt`, 8 prompts, same model, greedy, with and without that paragraph: 7/8 compile either way, and the `=` failure simply moves** — with the paragraph it kills `bright shimmer degrading to a dark rumble` (the prompt BJ reported), without it, `sine > pwm > bell`. The paragraph was neither the cause nor the cure. Do not add another prose rule about it; the two mechanisms that DO act on it are the repair turn's `_mechanical_hint` (which names that exact line after the fact) and the shape of the library's own self-referencing statements (`asig tone asig, 1200`, 32 of them — the one form where result and first argument share a name, so a line reads like an assignment).
- **The mirror slip costs just as much: an assignment written WITHOUT its `=`.** `kbow    0.8` — Csound reads the variable as an opcode name and complains about the number after it ("unexpected NUMBER_TOKEN"), naming neither. Measured 2026-07-24 on `a bowed cello`: the author reproduced the line on the retry and the loop stopped, so the whole authoring was lost to two tokens. `_mechanical_hint` recognises it now. Detection is deliberately narrow — a Csound variable prefix, then nothing but a number to the end of the line; no library line and no scaffold line but `instr 1` has that shape, and the hint is only ever attached to a line the compiler already rejected.
- **A prose instruction to retype library lines is forbidden regardless of its statistics** — it deterministically constrains the author, which the standing rule forbids, and it contradicts the architecture: the model WRITES Csound, it does not transcribe. That is why the paragraph goes, not because the corpus improved.
- **`ok` is not a passing morph.** Removing the paragraph made `bright shimmer degrading to a dark rumble` compile on the first attempt — with the correct structure (`kmorph = min(knote / 2.0, 1)` and one crossfade) and a colour trajectory of 867 → 937 Hz, then flat. It does not degrade. Compile success and the named sonic behaviour are separate measurements, and only the second one answers the prompt. `tools/lco_author_offline.py --measure` reports both; read `centroid_travel_hz` before calling a morph fixed.
- **The class a system-prompt change governs must be in the test set, measured BEFORE the change as well as after.** `b3bd2b19` was validated only afterwards, on four compositional prompts chosen after the fact — none of them a morph carrying adjectives, which is exactly what the paragraph was about. `tools/lco_morph_corpus.txt` is the frozen set for this. Note its limit honestly: greedy decoding has no run-to-run variance, but ANY edit to the system prompt reshuffles every generation, so 8 prompts × 1 run cannot separate "this instruction is harmful" from "the outputs moved". Use it to catch a named prompt regressing, not to certify a prompt change as an improvement.
- Compositional prompts landing first try, measured on the shipped author: `sine > saw + square` (2× `vco2`, a real layer at the far end, 46 s), `accordeon > guitar + bell` (2× `streson`, 2× `foscili`, 16 `alpass`, 105 s), `a bowed violin morphing into a bell` (75 s, colour travelling 4754 → 402 Hz across the note), `sine > pwm > bell` (two nested morph positions via `asig_mid`, 63 s). **The architecture change BJ floated — two inferences with the morph done outside — is not needed for these.**

**The consultation, measured the day it landed (2026-07-24, `tools/lco_morph_corpus.txt`, same model, greedy, one process)**

| | compiled | mean attempts |
|---|---|---|
| word-matched `select()`, with the RENAMING paragraph | 7/8 | — |
| word-matched `select()`, without it | 7/8 | — |
| the author's own consultation, syntax gate only | 8/8 | 1.50 |
| **the consultation + the performance gate** | **8/8** | **1.12** |

- Every prompt in the set compiles AND renders, including `sine > pwm > bell`, which failed under both earlier states. Read this as "no regression, one prompt recovered", not as proof the consultation authors better: any prompt change reshuffles every greedy generation (see the corpus's own limit, above).
- **The author opens 10.5 of 98 entries on average** (6 to 16), and the choices are visibly its own: `an elephant calling` → `bass_saw` + `sub_sine`; `accordeon > guitar` → `brass`, `cheby`, `clarinet`, `string` — it reached for the one reed in the library because there is no accordion entry, which is the same hole §6 item 1 records.
- **A compiling orchestra was still silent.** `bright shimmer degrading to a dark rumble` passed at attempt 1 and rendered nothing: `vco2 …, 1`, where imode bit 1 means "skip initialisation". `csound --syntax-check-only` accepts it; the first k-cycle raises `vco2: not initialised`. `vco2 …, 2` without its `kpw` is the same class (`INIT ERROR`, note deleted). Both were reaching the engine as successful authorings. `perform_check()` closes it: the wrapped orchestra is played for `_PERF_SECS` (0.25 s, ~0.1 s of wall clock) through the real CLI with the voice channels preset to a played note (220 Hz, gate 1), and Csound's own runtime message goes back to the author as an ordinary repair. All 94 library snippets pass it. It tests whether the orchestra RUNS, never how it sounds — `asig = 0` passes.
- **The gate alone was not enough.** With it, the author reproduced the same `vco2 …, 1` byte for byte on the retry and the loop stopped: "not initialised" does not tell anyone that a `1` in the third position is the cause. `_mechanical_hint` now recognises that line too and names the one Csound fact the message omits (imode is a bit sum; bit 1 skips initialisation; the waveforms are 0/2/4). With the hint the same prompt repairs on the second attempt — and then authors it on the FIRST, with the colour travelling 1264 → 837 Hz across the note. It degrades.
- **The gate does not reach an installed T5ynth.** `perform_check` needs the csound CLI, because performing means `csoundStart` and doing that in-process would run model-written Csound inside the backend that holds the 12B model. `tools/bundle_csound_macos.sh` ships only `CsoundLib64`. Where there is no CLI the gate returns "unchecked", never a false failure — so the machine where "reports success, sounds silent" actually happens is the one machine the fix does not yet cover. Decide it with the Csound bundling (§6, release blocker).

---

## 6. Open items

**Ordered by BJ, not yet built**

1. **A reed-instrument entry.** BJ, 2026-07-24: „ja, mache einen eintrag für reed-instrumente". The library has **no reed instrument at all** — no accordion, harmonium, bandoneon, concertina, melodica or harmonica, and none of the blown reeds either (saxophone, oboe, bassoon, bagpipe); `clarinet` is the only one, as its own key, and `organ` carries the words "reed organ". The mallet family has the same hole: no vibraphone and no marimba, while `struck_bar` covers glockenspiel, celesta and kalimba. (`rhodes` and `wurlitzer` are not missing — they are `fm_ep`; the concept document's „Instruments 4–6" never became separate entries.)

   The nuance before treating this as a bug report: the author writes a *good* accordion from its own knowledge (BJ: „das accordeon alleine ist überhaupt kein Problem → s. preset → klingt großartig"). What is missing is a curated, measured, ear-approved idiom for that family — not the ability to make the sound. The consultation now shows the hole from the inside: asked for `accordeon > guitar`, the author opens `brass`, `cheby`, `clarinet`, `string` — it reaches for the one reed there is.

2. **The performance gate does not reach an installed T5ynth, and that is solved WITH the Csound bundling** (BJ, 2026-07-24). `perform_check` needs a Csound in a separate process, because performing means `csoundStart` and doing that in-process would run model-written Csound inside the backend that holds the 12B model. `tools/bundle_csound_macos.sh` bundles only `CsoundLib64`, and no workflow runs it at all — so no installer carries Csound and the LCO is silent there regardless (the release blocker below). The decided shape: a short-lived CHILD process of the backend (`sys.executable` plus a flag) that loads the already-bundled `CsoundLib64` through ctypes, plays the quarter second and exits with a return code — process isolation without shipping a CLI binary. Whoever takes the bundling blocker takes this with it.

**Decided by BJ, 2026-07-25, after he asked to see items 3–6 again**

3. **The `ring` axis is misnamed on SEVEN entries, and BJ named the second reason: it reads as ring modulation.** „'ring' meint dann nicht etwa 'ringmodulation'?" The library has `ring_mod` as its own entry, whose `why` opens with „ring modulation / AM at a 2:1 ratio", and an axis called `ring` on `fm`, `fm_bell`, `fm_ep`, `metallic_fm`, `struck_bar`, `cymbal`, `glass` — one word, two meanings, in the same library the author reads. The first reason still stands: it cannot shorten a ring, because the host's `portk` does that in 6.98 ms regardless (§5), measured identical at ring 0.00 / 0.50 / 1.00. What it does move is the held note's timbre (comb contrast `glass` 27.6 → 39.0 dB, `struck_bar` 16.5 → 35.5 dB).
   **Decided:** `ring` leaves the axis vocabulary and stays with the ring modulator. **`touch` was rejected by BJ** — „finde ich nicht sehr aussagekräftig; wer wird wissen was das sonisch bewirken soll?" — and he is right that a name has to say what it does to the sound. **Proposed instead: `open`**, the drummer's own word (open versus closed hi-hat), which works for a hand on glass too, promises no duration, and collides with nothing in the synth's vocabulary; anchors `muted` / `half-open` / `open` / `wide open`. `resonance` was considered and dropped: in a synth it means filter Q, which is the same collision `ring` had. `damping` is physically exact but runs the other way and would flip every anchor. The FM family still needs a name for what it measurably moves, *measured before it is named* (item 17). **Still open:** for a struck membrane `ring` would be a genuine decay time — taboo anyway, or allowed where it is physically true?
4. **De-duplication: `noise` and `pink_noise` are the duplicates. `hiss` is not, and stays.** Measured in the C++: `src/dsp/NoiseGenerator.h` gives White / Pink (Paul Kellet) / Brown, per voice, through that voice's envelope and filter chain, with `noiseLevel` and `noiseType` as user parameters. So `noise` (`rand`, flat) IS the synth's White and `pink_noise` (`pinkish`) IS its Pink. `hiss` is `atone` over `rand` at 6000 Hz — *only the top left* — and all three of the synth's colours are flat or falling, so hiss could only be built by spending the user's own filter, which is the very thing BJ wants to protect: „ansonsten greift sich der Osc selbst immer mehr vom Synth, und damit auch von den User-Konfigurationsmöglichkeiten". His words on this: „'hiss' haben wir nicht, und das ist auch sehr speziell."
   **BUILT, 2026-07-25** („Das lässt sich ja wohl übergangsweise mit auskommentierung regeln?"): JSON has no comments, so the equivalent that loses nothing is a `withheld` key on the entry — its value is the REASON, not a boolean — which `tools/lco_build_library.py` honours and prints at every build. The entry keeps its measured constants and its history in `dco_lexicon.json`, the author simply stops seeing it, and removing one key brings it back. Nothing is deleted before its replacement exists.

   **One consequence, found by review and fixed:** the surface forms are indexed from the LEXICON and matched against the LIBRARY, so an author reply naming a withheld entry precisely — "white noise", "rosa Rauschen", ten forms in all — was silently dropped and looked in the wire's `consultation` exactly like a reply that named no instrument at all. `named_entries` now returns `withheld` and the trace carries `named_withheld`. Reported, not repaired: the entry really is unavailable, and the honest record is that the author asked for something the library does not offer.
5. **DECIDED: the plain-waveform family owes movement, and the movement it owes is MICRO-FLUCTUATION.** BJ, 2026-07-25: „'bewegung' heißt hier mikrofluktuationen (digitale vs. DCO vs. VCO charakteristika)". So there is no exemption for `saw` / `square` / `pulse` / `triangle` / `sub_sine`: they get a character axis — digital (perfect phase increment, no drift), DCO (crystal-stable pitch, the instability is in level and waveform), VCO (pitch drift, jitter, per-voice offsets). This is also the diagnosis for `analog_osc`'s `age` (item 19): two of its three layers run at 0.043 and 0.057 Hz, which on a 3 s note is a constant offset. **Micro-fluctuation has to have components roughly between 0.5 and 15 Hz to be audible inside a note.**
   **Decided: the DEFAULT STAYS CLEAN.** BJ: „ehrlicher und transparenter ist das schon. LLM kann das ja regeln." So the default is `digital`, `saw` stays bit-identical to today, and the micro-fluctuation arrives when the prompt asks for it — the author reaches it through the axis, which is what the axis is for. **The consequence has to be recorded rather than discovered later:** the plain-waveform family then still fails the movement reading *at its default anchor*, and that is now authorized by him for that family, not a defect — the movement exists, the prompt owns whether it is on. This is the second thing `LCO_CONCEPT.md` §4 owes a written exemption for (§4 line 96 already says the tool-level ones „either belong here or they belong nowhere").

   **BUILT and awaiting BJ's ear, 2026-07-25.** A `character` axis on `saw`, `square`, `pulse`, `triangle`, `sub_sine`, `bass_saw`: `digital` 0.0 (bit-identical to today, the default) → `dco` 0.5 → `worn` 0.75 → `vco` 1.0. The DCO region is a 100 kHz master clock with `f = master/round(master/f)`, so the detune is deterministic, identical in every voice, and worse the higher you play (+0.17 cents at 55 Hz, −5.53 at 1760); the VCO region adds a slow thermal layer, a `randi` wander, and a fixed per-voice offset via `frac(ivoice * 0.6180339887)`. Loudness spread ≤0.05 dB everywhere, defaults bit-identical, `moves()` True at 110 and 440 Hz on all six.

   **NOT applied to the lexicon, because one measured figure is a question for his ear.** At the top of the axis the pitch wobbles about **28 cents amplitude at 0.69 Hz** — a quarter of a semitone, twice a second. A real VCO's instability is a few cents, and the anchor's own gloss says „wandering a few cents up and down". So it may read as OUT OF TUNE rather than as analogue character, and no loudness or movement number settles that. Rendered for him at `tools/lro_character/`: every anchor of every entry, held at 110 and 440 Hz, and as a played line with 220 Hz repeated three times so an instability is audible against itself. The 0.69 Hz figure comes from a phase-based instantaneous-frequency estimate validated on a synthetic 8-cent/7-Hz case first (read 8.13 c at 6.88 Hz, 100 % in band) — a window-based pitch tracker CANNOT answer this at 110 Hz, where resolving a few cents needs windows far longer than a 7 Hz wobble allows.

   One thing the audition cannot show: the per-voice offset needs sixteen voices, and a rendered instrument has one. That part has to be heard in the built synth.
6. **DECIDED: keep the whole index, shorten it per entry, and functionality outranks economy.** BJ: „den index ökonomisch halten ABER FUNKTIONALITÄT muss Vorrang haben → hier nicht wild kürzen, hier immer testen an einer Reihe unterschiedlicher instrumente". No declared slot: a window that opens only once the author names a family restricts what it may see in order to orient itself, which is the standing rule (`feedback_llm_writes_csound_preamble`). Measured: the index is 113 019 characters in every prompt, the whole library 295 483, and the author opens 10.5 of 98 entries on its own (6 to 16) — so the second turn is cheap and it is the always-present index that grows.
   **The gate before any gloss is shortened**, because shortening one is a capability change until measured otherwise: a **frozen prompt corpus, fixed before the cut and not derivable from the shortened index**, over the classes the library exists for (pwm, „a morphing into b", dirty/overdriven, moving) plus the families being cut. Measured before and after at three points, because the index has two jobs: (a) does the author still open the RIGHT entries — the index is what it decides from, and a gloss too short makes it miss the entry, which looks like a different bug downstream; (b) does the result still `perform_check`; (c) does the objective signature hold (pwm → duty-cycle sideband asymmetry, morph → two distinct centroid plateaus, dirt → THD above the clean baseline). Any of the three regressing means the gloss stays long.

7. **The metal family plants a high-Q resonator on the played note, so an unpitched sound comes out pitched.** BJ, 2026-07-25, on asking for a crash: „'Crash Cymbal' klingt daher nicht nach Crash, sondern eher wie ein interessantes Glockenspiel mit großen Glocken. Nun sind Becken ja auch irgendwie gestimmt, je nach Umfang und Materialstärke etc., aber nicht in dieser Weise mit Grundton." Measured, and he is exactly right: **`cymbal` has nine modes and the lowest sits at ×1.0000 of the played note with Q 260**; `struck_bar` the same at Q 900, `glass` the same at Q 1400. A 260-Q resonator on the strike pitch *is* a fundamental, and nine high-Q modes at 1.0 / 2.19 / 3.41 / 4.83 / 6.37 / 8.09 are a bell — a real crash has hundreds, dense enough above ~1 kHz to be a continuum.

   **How much of that is the rules:** two places invite it, neither forces it.
   - The author's contract, in *every* prompt (`backend/lco_write.py`): „Read the pitch from `kfreq` … **Never hardcode a pitch or a frequency number**." Absolute, and a cymbal's modes sit where they do because of diameter and thickness, not because of the note. The honest form is that the sound must FOLLOW the keyboard — and following may be a whole cluster shifting; what is forbidden is a body that ignores `kfreq`, not one whose partials are not multiples of it.
   - The tracking exemption is a **hand-picked list of eight nature beds inside `lco_param_audit.py`**, which §4 of the concept document already flags as having no authority there. An unpitched METAL body has no way to declare itself unpitched the way an event texture can declare `; MOVEMENT: TEXTURE`.
   - What the rules do NOT require: a partial *at* the note. Removing the ×1.0 mode and irregularising the ratios is one line per entry and still tracks.

   **He heard it, and DECIDED, 2026-07-25.** The A/B/C went to him per entry, held at two registers and played as a line over two and a half octaves, because playability was his condition („ja - aber das müssen wir vorher mal hör-testen; zumindest glass ist ja ein digitaler Synth-Klassiker der stark mit Spielbarkeit verbunden ist"). His verdict: „die irregular-klänge sind erheblich überzeugend(er) als die anderen, aber bei Glass will ich diesen D50-sound nicht vermissen. Bei glass … sollte man vielleicht 'D50-Glass' oder so etwas unterscheiden von dem echten Naturglass."

   **So the ruling splits per entry, on his own criterion — an instrument with a definite pitch keeps its mode on the played note, one without loses it** (commit `ff127b6b`):
   - `cymbal` loses its ×1.0 mode and takes the irregular cluster (×2.61 2.86 3.52 5.41 7.44 8.19 9.43 14.07). This was the complaint that started the item.
   - `glass` is UNCHANGED — it IS the sound he does not want to lose, the pitched, playable digital glass-bell of the D-50 lineage. Only its `why` now says which glass it is.
   - **`struck_glass` is a NEW entry**: a pane, a jar, a bottle, a bowl — the same glass body with nothing on the note played (×2.61 3.46 5.13 9.53 15.85), both axes carried over.
   - `struck_bar` is UNCHANGED, by the same criterion: a glockenspiel, a celesta and a music box have a definite pitch.

   What shipped is what he heard: the cluster ratios were taken from the audition verbatim rather than re-derived. The silenced ×1.0 resonator was DELETED rather than left at gain 0.000, proven bit-identical first (max sample difference 0.0 at 110 and 440 Hz) — a dead `mode` opcode costs the audio thread and lies to the next author.

   **Still open, and BJ's to decide:** `struck_glass` has two anchors and `glass` five that read `moves=False` at the upper registers, all on COHERENCE and not span — the centroid travels 900–1900 cents but incoherently, which is the noise exciter's dither. `cymbal` is exempt only because it declares `; MOVEMENT: TEXTURE`. Whether that declaration belongs on the whole noise-rung metal family is the same question as item 10's second bullet, and it is not a gate to be quieted.

**Found by the adversarial review of 2026-07-25, not yet fixed**

8. **Nine declared axes on three entries have never been measured, because they have no axis line to set.** 39 of the 42 parametrised entries declare each axis as one line the probe can substitute (`kmus = 0.0 ; musette [0..1]: …`). The three oldest bake theirs into expressions instead, so `lco_axis_probe`/`lco_param_audit` cannot touch them: `drum_head`'s four (`pitched`, `strikepos`, `tension`, `damping`), `analog_osc`'s four (`wave`, `drive`, `fat`, `age`) and `fm_ep`'s `ring`. Nobody has ever checked that any of the nine is a colour control rather than a volume fader — the check that has already caught a shipped defect once. Worse than unmeasured: **`fm_ep`'s `ring` has no mechanism at all**, not in the body and not in its eleven `anchor_code` variants; the author is told "ring [0..1] short/medium/long" about a body with no decay to lengthen. `drum_head`'s three `pitched` variants are also stale — they predate the `balance` its `code` now ends on, so they hand the author back the 9 dB register tilt the entry itself fixed.

   The two laws that are recoverable were recovered before this was written down: `pitched`'s mode ratios interpolate linearly between the ideal-membrane set (1, 1.594, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501 — verified as the Bessel zeros of (0,1), (1,1), (2,1), (0,2), (3,1), (1,2), (4,1), (2,2)) and the kettle-tuned set (1, 1.5, 1.99, 2.44, 2.89, 3.33, 3.77, 4.21), and its per-mode gains fit a quadratic through the three anchors that reproduces the shipped default to 0.0012 (worst mode). `strikepos` is the same Bessel excitation |J_m(z·r)| — physically exact, but its shape has interior zeros no low-order fit reproduces, so it needs stops rather than a polynomial.

9. **Author-facing text that does not match its own code.** `organ`'s `principal` anchor is not one rank (a 30 % floor remains). `jaw_harp` measures 1.30 dB from `free_reed` and its `pluck` axis does almost nothing. The FM trio's `detune = 0` is not the static escape its gloss implies (moves True, coherence 1.000).
10. **Movement fails at named anchors away from 220 Hz — 81 anchors of 355, across 16 entries, not "7 of the 13 new entries".** Measured over the whole library on 2026-07-25 with `tools/lco_anchor_census.py`: every `anchor_code` variant, at 55 / 110 / 220 / 440 / 880 / 1760 Hz, on a settled instance (2130 renders, 274 failing readings). 25 more fail on the five event-texture bodies and are exempt rather than failing. The failures cluster high — 64 at 880 Hz, 61 at 1760, against 26 at 55 — so the gate's 220 Hz was close to the best case.

   **It is two different defects, split on CENTS because that is what the verdict used** (splitting on hertz put 28 readings on the wrong side, all at 55 and 110 Hz where a whole spectrum sits low, and filed `ocarina` under the wrong one):
   - **143 "barely moves" (span ≤ 60 cents)** — `analog_osc` 42, `saw` 23, `triangle` 23, `pulse` 22, `square` 22, `frog` 10, `harmonica` 1. **This is now a work order, not a question: BJ ruled on 2026-07-25 that the movement this family owes is micro-fluctuation, digital vs DCO vs VCO** (item 5 above). `analog_osc`'s 42 is item 19 with the same cause.
   - **126 "incoherent motion" (coherence ≤ 0.35)** across EIGHT entries — `drum_head` 72, `string` 23, `struck_bar` 11, `wind` 6, `glass` 5, `ocarina` 5, `hurdy_gurdy` 3, `overtone_voice` 1. These move hundreds to thousands of cents (`drum_head`'s `pitched=timpani` moves 1753 cents at 55 Hz, 29× the gate) and fail on *coherence*, not span. All are noise-excited, which is the same physics as the event textures, and BJ's own argument for that exemption applies word for word: „being alive means precisely that there is no rate to find". `cymbal` is built the same way and already carries the declaration. Either these eight declare `; MOVEMENT: TEXTURE` or `moves()` needs a second branch. **Put to BJ, not decided.** It converges with his ear: `drum_head`, which he heard as „rauschen unterschiedlicher Farbe", is the worst entry here, and with the *uptime* census, where the same construction reads as noise-bound.
   - **5 readings are not measurements at all: `bagpipe` at `reed=screaming` CLIPS** — peak 2.71 / 2.72 / 2.70 / 2.64 / 2.58 after the first 50 ms at 55–880 Hz, against the host's 2.52 (§5). A shipped anchor whose loudness the host reshapes. The first version of this census discarded the render's `err` and counted those as clean readings of the body, when they are readings of a limited signal.
11. **Noise seeding.** `wobble`, `evolve`, `flutter` and `shimmer` seed from `ivoice`, so sixteen voices are correlated differently than intended. **The other two are fixed** (`ca54e214`): `reseed` rewrote `randi`'s *rate* argument instead of its seed — the seed is the THIRD argument for `randi`/`randh` and the second only for `rand` — which took `ice`'s crack jitter from 0.7 Hz to 0.01 Hz and then compared the body against a differently-MOVING body, and it also edited the word `randi` inside `bubbles`' comment. And `pink_noise` still has no seed, so it is the one body in the library that does not render the same twice; `lco_phase_census` now refuses to classify it (`not-reproducible`) instead of publishing a class that flipped between runs of the same command.
12. **`lco_write` robustness, all reachable from a real reply:** the hyphen in a looked-up name, names found inside the code fence, `ß`, a single-variant `anchor_code` being dropped, and `--selftest` passing with the library file deleted.
13. **The gate does not gate everything it reports.** Pitch is never a verdict; the defaults are exempt from most checks; the axis cube is 3 points; the register list is six A's; the decay exemption is inverted; the texture exemption is unverified by construction (§5); `--registers` is not forwarded; `--steps`/`--freq` can flip a verdict; axes inside block comments and dead axes both pass.
14. **The meter's own soft spots**, each a plausible wrong number waiting: `beat_cap_hz`, `partials` measured against the ask, `event_rate_hz` at the low end, `f0` on `overtone_voice`, `motion_coherence`'s null at 32 Hz, `loudness_travel`'s null at 1/window.
15. **`tools/lco_recover_lost_keys.py` makes a false accusation** on a key it cannot find. (`lco_param_audit.py` had the same hole — a green "0 instruments, 0 findings" and exit 0 for any key it does not have, including the two now withheld — and it is fixed: it names the key, says why an entry is withheld, and exits 2.)
**New, 2026-07-25, each awaiting a decision that is BJ's**

17. **`chiptune`'s `speed` axis cannot be fixed without changing the entry's own sound, and that is his call.** The defect is real and not a measurement artefact: the axis swings 1.11 dB (measured at 3, 12 and 40 s — it converges), because a pulse's RMS depends on its duty while the selector dwells there. The fix is equal-power duty zones, and that changes the DEFAULT's waveform (max sample difference 0.33) because the shipped zones genuinely have unequal power and any equal-power normalisation changes their shape instant by instant. Long-run RMS at the default moves only 0.009–0.013 dB, so it is a change of TEXTURE, not of level. The rebuilt version is measured and ready; it is not applied. A shipped entry's own sound is not something to trade for a green number without being asked.

18. **`vibraphone` and `ring_mod` are parametrised and held back: a compensation fitted at four registers, used at all of them.** `vibraphone.mallet` reads 1.85 dB over its anchors at 55 Hz and 1.29 at 1760 (bound 1.0); `ring_mod.leak` 2.06 dB at 55 Hz. The mechanism in vibraphone is that `kihrdexc` is an exact cubic interpolant in `log2(f/220)` through 110/220/440/880 and `log2` is unbounded. **The acceptance gate tested exactly those four registers, so this passed by construction — the meter shared the fit's blind spot**, which is this project's own recorded failure mode. Clamping the octave variable to the fitted span was tried and MEASURED WORSE (2.61 dB at 1760, 14.24 at 3520): `vibes`'s own response keeps changing above 880 Hz and the runaway cubic happened to track that better than a frozen boundary value. A refit over the registers the instrument is actually played at is in progress. Worth weighing rather than deciding silently: a real vibraphone tops out near F6, about 1397 Hz, so 1760 Hz is already past the instrument.

19. **Three axes shipped two anchors that were the same sound**, found and fixed in the same pass (`5fe0ec8d`): `rhodes.tremolo`, `wurlitzer.tremolo` and `vibraphone.motor` each had `still` at 0.0 and `default` at 0.01, which measure 0.03 dB apart. An anchor is what the author is handed, so two names for one sound is a choice that is not a choice. Recorded here because the shape recurs: an axis whose bottom end is already its default needs its third anchor placed by measurement, not by symmetry.

20. **The whole noise-rung metal family fails movement on COHERENCE, and `cymbal` is exempt only by declaration.** See item 10's second bullet and item 7's tail; they are one question. `glass` 5 anchors, `struck_glass` 22 register-readings, `struck_bar` 11, `drum_head` 72. Either they declare `; MOVEMENT: TEXTURE` — which is BJ's own „being alive means precisely that there is no rate to find" — or `moves()` needs a second branch for a body whose liveness is dither. **Not decided, and not to be quieted by adding declarations to make a gate green.**

16. **The waterphone is still parked, and it ships as written** — `docs/parked/lco_movement_gate_three_sounds.md` records the proof (partials 67.4 / 54.1 / 50.8 / 38.2 dB, `tracks` at r_note 0.675 / r_fixed −0.042). It needs un-parking into the lexicon, and BJ reviews new instruments by ear.

**BJ's ear on the nine new axes, 2026-07-25 — measured green, heard as not there yet**

He auditioned `tools/lro_axis_audition/` and this is his verdict verbatim, which outranks every number above:

- **`drum_head` is not a drum.** „kaum einzuschätzen - das ist rauschen unterschiedlicher Farbe, sicherlich brauchbar für den Rauschanteil von Snares, aber ich höre keine Toms oder so etwas." Four axes that all measure correctly and hold one loudness, on a body that does not read as a struck head at all. This is not an axis problem: the *instrument* is a continuously noise-driven mode bank, which is also why it is the entry that fails movement (item 10). Whatever fixes it is a change to `drum_head` itself, and the audit's `M4` on it was the visible symptom all along.
- **`fm_ep` is otherwise good, but `hollowness` is not hollow.** „alles bis auf Hollow, das hörbar aber subtil ist und nicht wirklich hohl rüberkommt, gut ausgeprägt." The axis is the even/odd harmonic balance of `foscili`; audible, but the word promises a formant-like hollowing the balance alone does not give. `ring`, `ting` and `strike` he called well defined.
- **`analog_osc`: `fat` is not fat, and `drive` does not scream.** „Fat ist pwm, nicht fat" — the detuned second oscillator is being heard as duty-cycle modulation, which means the beating reads as timbre rather than as two instruments. „drive hörbar als eine Art Verdichtung (aber nicht distortion), allerdings kein 'scream' in irgendeiner Form hörbar (soll das überhaupt?)" — and that last question is the one to answer first: the `screaming` anchor's gloss promises something the oscillator may not be the right layer for at all. `wave` he confirmed as clearly distinguishable.

Nothing here is a measurement failure — every one of these axes passes M5 and M6. It is the gap the audit cannot see: an axis can move the spectrum correctly and still not sound like the word on it.

**Waiting on BJ's ear — these cannot be closed by a gate**

16. The FM family's axes (`index`, `ring`, `detune` on `fm`, `fm_bell`, `metallic_fm`) — bite, fade, shimmer. Built, never heard.
17. The `string`'s three anchors (`bow`, `pick`, `damp`). BJ heard v1 and asked for a more scattered exciter; the current entry is the answer to that and has been heard only as the audition set. Where "plucked", "mixed" and "bowed" sit on the axis is curation, not measurement.

**Deferred by BJ, cause known**

18. `analog_osc`'s `age` is inaudible on a held single note: two of its three instabilities run at 0.043 and 0.057 Hz and are a static offset over a few seconds. The fix is a second, faster instability layer — not more depth. (`LCO_CONCEPT.md` §5.)
19. `fm_ep`'s odd/even balance does not travel over the note.

**Known, not changed because it is runtime semantics and nobody ordered it**

20. `string quartet`, `string ensemble` and `string section` reach **both** `strings` and `string`: the bare word "string" is inside the phrase and `_lookup` collects every match with no longest-wins rule. It no longer stands between the user and the library — `_lookup` reads the AUTHOR's reply now — so at worst the author is handed one entry it did not ask for. Leave it: a longest-wins rule would start deciding which of two entries the author meant.

**Housekeeping that needs a decision from BJ**

21. ~457 MB of untracked `tools/*_out/` render directories (over 1 GB counting the ignored ones) and a set of untracked one-off scripts. Deleting anything under `tools/` is BJ's call, not mine. Note before deciding: `backend/dco_frames.py` is untracked but **is imported by the tracked test** `tools/test_dco_author.py:1312`.
22. `docs/plans/HANDOVER_lco_selfcorrect.md` is untracked and describes the self-check/self-correction loop, which was **deactivated 2026-07-21** (`dbd6c153`, `T5YNTH_LCO_SELFCHECK = 0`). It is history, not a plan.

**Traceability gap, reported and not fixable without rewriting history**

23. The morph **shape** in `_SYSTEM_HEAD` — both ends in their own variables, `kmorph`, one crossfade — was swept into commit `b04c8ef4` ("fix(lco): the trace must not claim what nobody recorded"), whose message does not mention it. Someone bisecting the morph behaviour will not find it there. (`git log -S'asiga   = <a' -- backend/lco_write.py` will.)

---

## 7. What in `docs/LCO_CONCEPT.md` is now historical

The concept document is authoritative for **the goal (§1), the architecture (§2), what an instrument is (§3) and the platform invariants (§4)** — none of that has moved. Its account of the *implementation* predates the switch to the model-authored path (2026-07-22) and now points at code that does not exist:

| In the concept document | Actually, today |
|---|---|
| „Read this before touching … `backend/csound_orch.py`, or `backend/dco_llm_map.py`" | `backend/csound_orch.py` is deleted. The write path is `backend/lco_write.py`. |
| §2: „the prompt goes to a small model (currently `qwen2.5-7b-instruct`)" | The author is gemma-4-12B QAT 4-bit GGUF, and it also does translation and re-prompt. |
| §2: „`build_orchestra()` in `backend/csound_orch.py` turns the reply into an orchestra" | The **model writes the orchestra body**. `lco_write.wrap()` only puts it in the host scaffold. The model no longer names keys for Python to assemble. |
| §5 title: „The three instruments (current proof of concept)" | 64 instruments, 57 of them parametrised. §5's *measured facts* about instruments 1–3 all still hold and are still the best record of them. |
| §8: the ten hand-maintained Python sets, `_ADJ_MAP` as post-mix DSP, `_emit_crossfade_morph` | All of that was `csound_orch.py`. The growth blocker it describes is gone with it; adding an instrument is now one lexicon entry plus a library rebuild. |
| §9 items 5, 6, 7 (cross-cutting properties into generation; the morph as a real waveform morph; the ten sets) | Answered by the architecture change: the model writes the code, so adjectives and morphs are in the emitted Csound by construction. §9 items 1, 2, 3, 4 and 8 stand. |

**§2 and §3 item 4 were CORRECTED in place on 2026-07-24** to describe the consultation — the index goes into the system prompt, the author names what it wants opened, Python fetches exactly that. Those two passages are BJ's order recorded, not a state report, and are authoritative; the rest of the table above still stands.

Also superseded by the same change: the plan file `/Users/joerissen/.claude/plans/hashed-chasing-snowflake.md` ("LCO v1: Adjektive in die Codegenerierung + 4 Familien parametrisieren") is written entirely against `backend/csound_orch.py`. Its *intent* — many adjectives must move parameters inside the algorithm — is now satisfied by the model writing the code. Its mechanics are not implementable.

**This needs BJ's decision:** fold these corrections into `LCO_CONCEPT.md` itself (it is his document and parts are dictated verbatim), or leave the concept document as the record of the design and let this file carry the current state.

---

## 8. Release blockers

Neither is fixed, and both make the LCO non-functional for anyone who is not on this machine.

1. **CI ships no Csound.** `CMakeLists.txt:62-79` finds `CsoundLib64.framework` only in a local Homebrew prefix and is explicit that it is *never required* — „CI runners and any dev machine without Csound installed must build exactly as before" — with `T5YNTH_HAS_CSOUND` always defined so every call site branches at runtime. So the published build compiles green and the LCO is silent in it. `tools/bundle_csound_macos.sh` exists and is not wired into `.github/workflows/`.
2. **There is no acquisition path for the author model.** The 12B GGUF is on the maintainer's disk; nothing in the SetupWizard fetches it. Without it there is no oscillator, by design — no model, no fallback, no tone.
3. **The bundle carried no library at all until `df3b3cf5`, and that fix is unverified against a real PyInstaller run.** `lco_write.py` opens `lco_library.json` and `dco_lexicon.json` with a plain `open()` relative to its own `__file__`, which inside a frozen bundle is `_MEIPASS`; `pipe_inference.spec` did not list them, so the first LRO prompt in any installer raised `FileNotFoundError` while working perfectly from source. The spec now adds both and **raises at build time** if either is missing. Nobody has run PyInstaller since. Do that before the next tag.

---

## 9. Standing rules that govern work on the LCO

Not repeated here in full — they live in `CLAUDE.md` and in the session memory — but three decide most arguments:

- **A "port X to Y" task is never a licence to rebuild X smaller** (`CLAUDE.md`, Migration & Substrate Discipline). The trigger is objective: it fires whenever a commit adds a fresh implementation of a capability an existing module already provides, or deletes a module the prompt path reached — regardless of what the task was called.
- **Test only in the built Standalone** for anything user-facing (since 2026-07-18). Offline measurement establishes facts; it does not establish that the feature works.
- **Nothing sound-shaping without an explicit order from BJ.** The oscillator is a spectrum source; the synth owns the envelope.
