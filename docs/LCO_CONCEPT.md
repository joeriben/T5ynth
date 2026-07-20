# The LCO — concept, direction, and current state

**Status: authoritative.** This document supersedes every earlier description of the LCO in `docs/` (the `DCO_*` and `HANDOVER_DCO_*` files describe the predecessor and are historical). Written 2026-07-19, at BJ's instruction, after a session in which the direction had to be corrected four times because it was not written down anywhere.

Read this before touching `backend/dco_lexicon.json`, `backend/csound_orch.py`, or `backend/dco_llm_map.py`.

---

## 1. The goal

Language models can write code, and they can translate natural language into sound worlds. But only frontier models do that *consistently*. The LCO exists to get that capability out of a **small, ideally locally running** model.

The mechanism is not a bigger model. It is a **curated library of sound-world code**, with documented parameters and curated perceptual hints, prepared in advance by parametrised sound research. The library supplies the consistency the small model lacks.

BJ, 2026-07-19, dictated for the record:

> „Die Idee war von Beginn an: LLM können Code schreiben. LLM können natural language in Klangwelten übersetzen. Aber nur Frontier-LLM könnten das konsistent. Wir bauen eine Klangwelt-Code-Bibliothek mit zig Instrumenten. Wir informieren über Parameter jeweils dieses Csound-Codes innerhalb seines Spektrums. Wir haben auch übergreifende Klangeigenschaften die in die Codegenerierung einfließen können ('gritty', 'dirty', 'airy', etc.). So versuchen wir mit — idealerweise — lokal laufenden LLM ästhetisch interessanten Csound-Code aus natural language zu machen ohne ein Riesen-Coding-LLM dahinter, und mit mehr Kontrolle durch vorab erfolgende parametrisierte Klangforschung und Kuration (die aber natürlich dem LLM nur Vorschläge und Vorlagen bietet, und Parametrisierungshinweise wie 'square ist sharp wenn Wert x = y, ist hollow wenn x = y', etc.) und LLM übersetzt natural language Adjektive und Metaphern in diese Instrumentkategorie und innerhalb dieser die Parameter, und das wave-morpht die nach Prompt und mixt bis zu 3 davon."

Every design decision is measured against that paragraph.

---

## 2. The architecture

BJ, 2026-07-19, verbatim:

> „Es gibt: 1 Prompt. 1 LLM-Inferenz die Bibliotheken benutzt. Daraus wird 1 Csound-Code für 1-3 Osc, die in sich morphen und was auch immer tun: es ist 1 - EIN - Csound-Code der resultiert. Der wird gefahren, so entstehen die Schwingungen dieses Osc - oder meinetwegen komplexen Meta-Osc."

**One prompt → one inference → one Csound orchestra → it runs.** That is the whole pipeline.

There is no frame store, no wavetable bake, no capture buffer, no transport, no runtime machinery wrapped around the generated code. Anything the sound does — including morphing — is expressed *in the emitted Csound source*. If a design requires a mechanism outside the emitted code, that design is wrong.

Concretely, today: the prompt goes to a small model (currently `qwen2.5-7b-instruct`) which sees a catalogue built from `backend/dco_lexicon.json`; `build_orchestra()` in `backend/csound_orch.py` turns the reply into an orchestra with `ksmps=64`, `nchnls=16`, one numeric `instr 1`, `ivoice = p4`, 16 always-on voice instances, per-voice channels (`gate/freq/vel/pres/timb/trig`), and up to three oscillator slots each with its own chain, volume and register. The plugin runs it live against `CsoundLib64.framework`.

### Naming

**There is no "DCO."** The LCO is the contemporary version of what a DCO used to be — the predecessor, not a component. Never write "the LCO and the DCO", never ask where the boundary between them runs: there is one oscillator. The name survives on ~53 tracked files (`backend/dco_lexicon.json`, `backend/dco_llm_map.py`, `src/dsp/DcoBaker.*`, `docs/DCO_*`, many `tools/dco_*`); some of those files are live and simply kept the old name. Do not infer a subsystem from a filename.

**Wavetables are dead for the LCO** (BJ, 2026-07-19: „wavetable sind TOT für LCO"). `src/dsp/DcoBaker::bake` has no caller anywhere; `backend/lco_author.py`'s entry points are unreachable from the running path. When reasoning about how an LCO sound is produced, go to the lexicon and the orchestra emitter — not to frames.

---

## 3. What an instrument is

The unit of the library is an **instrument**: real, capability-complete Csound code, plus its own parameters, plus what those parameters do.

An instrument entry carries:

1. **The Csound code** — real substrate idioms. Csound was chosen because it natively knows PWM, FM, waveguides, modal resonators and waveshaping. Hand-authoring a crippled substitute for something the substrate already provides throws away the entire reason for the choice.
2. **Its parameters, with measured ranges.** Not "it has a brightness" — the range, measured on this build.
3. **Named anchors on each range**, each with a perceptual gloss: `square 0.55 — hollow, reedy, odd harmonics only`. The anchors are what the small model aims at. This is BJ's „square ist sharp wenn x=y, ist hollow wenn x=z".
4. **Surface forms** — the words that reach it. Validation canon; the model never sees them.
5. **A `why` text** — what the model *does* see, alongside the parameters and anchors.

### Parameters are per instrument

Settled 2026-07-19. BJ:

> „Ich bin noch skeptisch ob Deine one size fits all Idee überhaupt gut ist, versus jedes instrument hat seine besonderheiten und ergo seine eigene Parametrisierungsentscheidung. Das käme mir erheblich plausibler vor."

He is right, and his original wording already said it: „Wir informieren über Parameter **jeweils dieses** Csound-Codes." "Wave" is meaningless on a drum-head resonator; "membrane tension" is meaningless on an analogue oscillator.

**What is shared is the vocabulary, not the parameter list.** `gritty`, `dirty`, `airy` must mean something in every instrument — but each instrument decides which of *its own* parameters that word moves. A recurring concept (harmonic rolloff appears in seven existing keys; FM index in six) may be given a shared *name* for consistency, but it is never a shared mechanism imposed on an instrument that does not have it.

### The model's job

Translate natural language, adjectives and **metaphors** into (a) the instrument category and (b) the parameters within it. Anchor *words* are what a small model produces reliably; bare numbers are allowed for interpolation between anchors. The curation offers suggestions and templates — not a straitjacket.

**A construction that enumerates permitted timbres, waveforms or shapes ahead of the prompt is on-sight suspect.** That is the old DCO's waveform selector switch rebuilt, and it is a regression to the predecessor, not progress. (This was attempted and reverted on 2026-07-19; see §7.)

### Combination

Instruments wave-morph according to the prompt, and up to three are mixed. Both are properties the library must carry: an entry that only knows how it sounds alone is half an entry.

---

## 4. Platform invariants every instrument must obey

These are user-observable fundamentals, not preferences. They disqualify otherwise attractive Csound opcodes, and they are the reason several obvious choices are unavailable.

- **The oscillator is a SPECTRUM SOURCE.** The synth owns the amplitude envelope, glide, filter and expression. A generator that brings its own amplitude decay fights the player's envelope and takes the choice away.
- **Colour may travel over the note; loudness may not.** A struck tine that *darkens* as it rings is the oscillator's business. A tone that fades to silence on its own is not.
- **A self-decay is acceptable ONLY where there is no other way** (BJ, 2026-07-19: „Hüllkurven bei Naturinstrumenten können ja ok sein, aber NUR dort wo es nicht anders geht"). Where an alternative construction exists, it is taken.
  - **Open, and it may replace this rule of thumb with a user's choice.** After hearing instrument 3, BJ named a prompt-level convention: **`taiko drum wave` = without envelope, `taiko drum` = the real Csound instrument.** The word `wave` would select the spectrum-source reading; its absence would mean the instrument with its own decay. His reasoning is the honest objection to the rule above — taking "the other way" can cost effort *to make the result worse*: a continuously excited drum is a noise you then shape back into a drum with filter, pitch and envelope. He was explicit that this is **not important now**, and it is recorded rather than built.
  - **What the convention reveals about what has been built.** Hearing instrument 2 the next day, BJ named a second instance unprompted — *„das wäre schon ein kandidat für ‚epiano wave'"* — and that is the remark that makes the pattern visible. **All three instruments are `wave` readings.** Checked, not assumed: none of `analog_osc`, `fm_ep` or `drum_head` emits an amplitude envelope opcode, and all three are continuously sourced. The invariant at the top of this section has been read as *the* rule, and under it every instrument that could be built was a `wave`. The convention's other half — the real instrument, with its own decay — **does not exist at all.** It was never rejected; it was never reachable.
  - **Which is why §6's rejection list is its material, not a dead end.** `fmrhode`/`fmwurlie` were rejected because no decay argument exists in the signature — i.e. they have a decay that cannot be switched off. `marimba`/`vibes` were rejected because `idec` is inert *and* pitch freezes at strike. Every one of those disqualifications is a disqualification **for the `wave` reading only**, and several are the physically correct behaviour for a struck instrument. About a dozen already-measured opcodes are sitting there. The one piece of genuine new work is that the routing layer has no concept of a modifier word today; `wave` would be the first.
- **Pitch belongs to the synth.** An opcode that invents its own register is unusable however good it sounds. `kfreq` is k-rate and glides; everything must track it.
- **Movement by default.** Every sound moves; only an explicit "static" request is delegated to the standing-tone escape hatch.
- **The LLM is the entrance.** No LLM, no oscillator. No deterministic fallback, no "not understood".
- **Nothing sound-shaping without an explicit order.**
- **Test in the built Standalone.** Offline measurement is legitimate for objective facts and never as evidence of sound quality.

---

## 5. The three instruments (current proof of concept)

BJ, 2026-07-19: „nein, eigentlich Kernidee zuerst. d.h. wir beginnen mit 3 unterschiedlichen Instrumenten. die parametrisieren wir." And: „Möglicherweise war auch der Fehler von einer quantitativen Mächtigkeit auszugehen statt einem sinnlich-musikalisch getragenen Aufbau dieses Osc. Der kann dann ja auch über Monate wachsen."

Not coverage. Few parameters, musically meaningful, allowed to grow.

### Instrument 1 — analogue oscillator (`analog_osc`) — BUILT, ear-approved

BJ's verdict after testing in the built Standalone: „Klanglich schon überzeugend. Übersteuerung ok, Old funktioniert noch nicht."

Four parameters: `wave`, `drive`, `fat`, `age`.

Measured facts it rests on (Csound 6.18, Homebrew, double, no STK — do not re-derive):

- **The wave axis is two phase-locked `vco2` calls.** Segment A `imode 4`, `kpw` 0.5 → 0.02: a symmetric triangle continuously skewed into a sawtooth, converging *exactly* on `imode 0`'s saw spectrum. Segment B `imode 2`, `kpw` 0.5 → 0.10: square to narrow pulse, where `kpw` is the literal ON-duty fraction. Measured RMS is flat within each segment (0.2887 and 0.4991 at kamp 0.5) and the segments differ by exactly ×0.578 (−4.8 dB), applied as compensation.
- **The seam is a real waveform blend, not two beating oscillators.** Given the same `kphs=0` and the same frequency the two instances phase-lock: fundamentals at −93.6° and −90.0°, a 50/50 sum bit-stable over time (peak 0.5436 / rms 0.3799 identical at t=0.1/0.4/0.7 s) and adding coherently (|H1| 0.4804 measured vs 0.4806 predicted). Therefore the crossfade across the seam is **linear**, not equal-power — the two signals are coherent.
- **A table-based construction was tried and rejected on measurement**: `vco2ft`+`tableikt` read with a shared phasor reproduces all four shapes exactly, but the resulting axis swings 9.5 dB in peak and clips past ~8% duty, where native `vco2 imode 2` holds RMS exactly constant at every duty and never clips. The simpler path is also the better one.
- **`tanh` is the saturator.** Zero DC at every drive level, odd harmonics only, self-bounding; THD 4.6% (k=1) → 45% (k≥16). Rejected on measurement: `distort1` does not distort at all in this build (THD <0.01% across 12+ argument combinations); `powershape` and `distort` get *quieter* as drive rises and `distort` gains DC; `pdclip`/`pdhalf` are phase-distortion shapers and add huge DC; `waveset` fails pitch glide (stayed ~140–220 Hz through a glide reaching 392 Hz); `wshape` does not exist in this build.
- **Minimoog overdrive, and what it actually does.** Two stages in the real instrument: the mixer (pre-filter — the one modelled here, classically overdriven by feeding the output back into the External Input, internally normalled on the current Model D) and the ladder's own differential-pair limiting (which belongs to the *filter*, and the filter belongs to the synth). The perceptual result is documented as **losing high harmonics — flatter, more closed, but rounder and warmer**. So drive on a saw or narrow pulse makes it **rounder and darker**; on a triangle it **adds** harmonics. The catalogue text says this explicitly, because "drive = brighter" is the intuitive and wrong model. On a Minimoog there is no drive knob at all — the mixer is simply pushed past unity, which is why drive and level are modelled as one motion.
- **Unison is free.** 0.064 µs/kcycle per extra copy against a 1333 µs/kcycle block budget. Measured beat rates for a detuned pair: 2¢→0.23 Hz, 7¢→0.83 Hz, 20¢→2.34 Hz, 40¢→4.67 Hz.
- **Analogue instability is THREE things, not one.** (a) Per-voice pitch drift — the existing `_emit_vco_drift`; depth→cents is exact (0.0007→1.21¢, 0.003→5.19¢, 0.007→12.08¢, 0.015→25.78¢) — but on a *single* held note it produces essentially nothing (envelope CV 0.02%); it needs a second voice to beat against. (b) Amplitude wobble — ±3% raises single-voice envelope CV to 1.08%, fifty times more. (c) Shape/duty wobble — moves the spectral centroid ~0.42% with no amplitude change at all. **A single "age" control must move all three or it is inaudible on a held single note.**

**Open: `age` is not audible, and the cause is in the rates, not in the wiring.** All three instabilities *are* connected (verified by reading the emitter, not assumed): pitch via `kvdr` in the `vco2` frequency expression, shape via `kdty` added to `kpw`, amplitude via `kagw` on the output. But the pitch drift runs at `0.043 Hz` (a 23-second period) and the shape wobble at `0.057 Hz` (17.5 seconds), because both were inherited from the per-voice drift idiom, whose job is to pull *voices apart from each other* over time — not to make one held note waver. On a note of a few seconds they are a static offset, not motion. The third, amplitude, is fast enough at 0.7 Hz but reaches only ±2.4% at `age=0.8` ≈ 0.2 dB, below the threshold for slow amplitude modulation. **The fix is a second, faster instability layer, not more depth on these three.** Deliberately deferred by BJ — „Nicht wichtig; semantiken können wir später kalibrieren, wir sind back to PoC".

### Instrument 2 — FM electric piano (`fm_ep`) — BUILT, ear-approved

Four parameters: `ting`, `ring`, `reed`, `strike`. Two body operator pairs crossfaded, plus a high-ratio tine pair, through a `balance` stage. Every ratio is `car=1, mod=R`.

**Why the old key had to change.** `fm_ep` was a single 1:1 `foscili` with a softening index. Measured against Csound's own `fmrhode`/`fmwurlie`, it had the woody body and **no metallic attack whatsoever** — 0.01% of its energy above the 8th harmonic, where the two references have 11.7% and 72.8% — and it could never have been tuned into one, because a 1:1 ratio is harmonic by construction and cannot place a partial off the comb. `fm_ep` answers to `piano` as well as `rhodes`, so this changes every prompt that says piano.

**The migration took nothing away**: at `ting`=0 the old sound is still there (0.01% above the 8th harmonic late, exactly as before), the body index still softens over the note and then holds, and `strike`'s default reproduces the old 4.20 → 1.30 ramp exactly.

The measured facts (do not re-derive):

- **DC appears iff carrier/modulator is a positive integer.** So `car=1, mod=R` is DC-safe unconditionally for any R>1. Moving off 1:1 deleted the DC problem outright: measured 0.17% of peak across the parameter space, against the old 1:1's 23.4%.
- **The old 1:1 trap was worse than its own comment knew.** Its DC is *history-dependent*, not a function of the current index: the same index 1.30 measures −11.2% held statically, +35.5% after a step, +21.8% down the shipped ramp. A fitted correction was only ever valid for the exact ramp it was fitted against — which is precisely what a parametrised strike control would have invalidated. This is why the polynomial is gone rather than re-fitted.
- **For a DC-safe ratio the index needs no level compensation at all** — RMS flat to under 0.001 dB across index 0.5→12. Unlike instrument 1's drive, which needed an explicit output compensation.
- **A modulator-ratio sweep cannot serve as the Rhodes↔Wurlitzer axis, and was refuted twice.** Harmonicity is a step function of the ratio's *rationality*, not a continuous function of the ratio (mod 1.5 → 0.411, mod 1.6 → 0.003): that is number theory and finer stepping cannot fix it. And approaching mod 2.0 beats at exactly |2−mod|·f₀, pitch-proportional, reaching 0.118 modulation against the project's own 0.08 movement floor — at 220 Hz a 22 Hz beat, the same roughness the `fm_bell` history already rejected once. Exactly 2.0 is the only beat-free point, and stopping just short buys nothing: 1.98 measures the same odd character *and* the beating.
- **The replacement is a two-body crossfade, 1:3 ↔ 1:2.** `car=1, mod=2` puts sidebands at f·(1±2n) — odd harmonics only. Measured odd/even across the control: −1.2, +6.5, +12.3, **+19.7**, +74.7 dB. Monotonic, and +19.7 dB lands on `fmrhode`'s own measured +19.9.
- **The tine is `car=1, mod=14.2`, not 14.0.** An exact integer ratio is perfectly harmonic and reads as a bright buzz; 14.2 places its partials between harmonics and reads as metal.
- **`balance` needs `ihp=10`, NOT `fm_bell`'s `ihp=1`.** At ihp=1 the follower's ~0.16 s time constant cannot track an 86 ms tine decay and loudness travels up to 1.24 dB at 1760 Hz. `fm_bell`'s 1 exists to protect its doublet beat; this construction has no doublet by design. Measured across the parameter space at ihp=10: **worst loudness travel −0.35 dB** over 34 corners at 220 and 1760 Hz.
- **`balance` is not spectrally transparent.** Its gain follower ripples at 2·f₀ and injects even harmonics into an odd-only signal — a pure 1:2 body measures +135.5 dB odd/even raw but +66.6 dB through the stage. Here that is welcome, landing near `fmwurlie`'s +75.9, but it is a side effect and not a coincidence to lean on blindly.
- **What separates the two references, objectively.** `fmrhode`: 11.7% above the 8th harmonic at onset, half-life ~86 ms, 40–60% of that brightness genuinely *off* the harmonic comb, odd/even −0.4 dB early → +19.9 late, crest 9.5 dB. `fmwurlie`: 72.8%, half-life ~770 ms, ≥98.7% *on* the comb at every window, odd/even +17.6 dB already in the first window → +75.9 late, crest 5.4 dB. They differ on **two independent axes** — how long the bright attack lasts, and whether the brightness is inharmonic or odd-harmonic — moved by different, non-interacting mechanisms. A single Rhodes↔Wurlitzer control would only be a diagonal through that plane, which is why there are two controls, not one.

**Known structural limit, honestly stated.** Above f₀ = sr/(2·14.2) — about 1553 Hz at 44.1 kHz — the tine's modulator exceeds Nyquist and folds. A `limit` keeps it legal but a cap is not a fix: at 1760 Hz the effective ratio becomes 11.28, odd/even collapses from 29.2 to 1.4 dB and above-8th energy from 44.7% to 16.3%. **The top octave genuinely will not sound like the bottom.** It does not read as aliasing and no aliasing check will catch it — energy below the fundamental stays at 0.00% — it simply gets quietly duller.

**BJ's verdict, 2026-07-20:** *„e-piano ist sehr ordentlich dafür dass es nicht das csound rhodes ist. nette helle transiente, klangcharacter passt"* — and, in the same breath, *„das wäre schon ein kandidat für ‚epiano wave'"*. That names the second instance of the §4 convention, and it is the remark that makes the pattern visible: see the note there.

**Open:** the odd/even balance does not travel over the note the way `fmrhode`'s does (−0.4 → +19.9 dB). Measured: a decaying inharmonic tine cannot move it at all, because a ratio-14.2 pair places partials *between* harmonics and so adds off-comb energy without adding even-harmonic energy. Reproducing that travel needs the body mix itself to move over the note — legal, since it is spectrum and not amplitude, but untested.

### Instrument 3 — drum head (`drum_head`) — BUILT, ear-approved

Four parameters: `pitched`, `spot`, `tension`, `damping`. A `mode` filter bank at membrane ratios under continuous noise excitation — the idiom `cymbal`/`glass`/`struck_bar` already use. Because the excitation is continuous, a held note stands and no self-decay problem arises. A membrane's mode ratios are not a harmonic series; that is the instrument's substance, not a detail of it.

A real drum is a struck, dying thing. What this actually is, stated honestly, is a **bowed or rubbed head** — a membrane held in excitation. That is a deliberate reading of §4's rule that a self-decay is acceptable only where there is no other way: here there is another way, so it is taken, and the strike belongs to the player's envelope.

Each control measured at 110 Hz: `pitched` moves autocorrelation definiteness 0.26 → 0.53, `spot` centroid 346 → 491 Hz, `tension` 378 → 455 Hz, `damping` in-band energy fraction 0.302 → 0.196. `pitched` moves only whether the drum *has* a note — the played pitch never moves, because pitch belongs to the synth.

The measured facts (do not re-derive):

- **A noise-driven `mode` emits power proportional to Q/f** — fitted over a frequency × Q grid as P ~ Q^1.020 / f^1.015, worst residual 0.63 dB. Not read from the opcode; rendered.
- **The modes share one exciter, so overlapping bands add in amplitude, not power.** Two modes at Q=5 a fifth apart put out **+0.70 dB** over their power sum; at 1.075 spacing, **+2.74 dB**. The drum's own ratio set contains a 2.136/2.296 pair exactly that close. **No power sum can see this**, which is why the normalisation is a numerical integral of |Σ Hᵢ|² and not a sum. The integral predicts the rendered level within 0.21 dB across 16 corners; `sum(a²)` is 4.62 dB worst.
- **This is what makes four colour controls colour.** Under `sum(a²)`, `damping` was also a −4 dB fader and `spot` swung 3–5 dB. After: worst full-travel swing 0.24 dB at ≥110 Hz, 0.46 dB at 55 Hz, 1.92 dB at 20 Hz.
- **Q is bounded by ring-up time, not taste.** A `mode` reaches steady state in ~Q/(π·f) s, and a drum lives at 50–200 Hz. Q=220 at 110 Hz is a 0.64 s time constant — the note is still growing seconds in, measured as a +2.61 dB swell. Q=28 is what held within-note travel inside 1 dB down there.
- **The master gain is not comparable to the neighbouring banks' gains.** Those scale a peak-normalised spectrum; this scales a power-levelled one. Set on RMS, which is what the ear compares when switching keys: neighbours 0.083–0.093, this 0.085.

**Two measurement traps, both of which produced confident wrong numbers here before being caught.** (a) Two unit-amplitude modes peak near 2.9 against `0dbfs=1`, so a **16-bit render hard-clips** and returns a plausible-looking power-law fit plus an apparent cancellation at Q=28 that does not exist. Render to float. (b) **Csound's `rand` carries its own `iseed` and ignores a global `seed` statement** — so an "ensemble" of renders under different seeds is bit-identical, max abs diff 0.0. Narrowband noise genuinely needs an ensemble and ~1 s windows; check two renders actually differ before averaging many.

**The timpani octave — settled by ear, and worth knowing about before re-measuring.** The air-loaded ratios are 1.00 : 1.50 : 1.99 = 2:3:4 on f/2, a missing-fundamental series an octave below the played note, and autocorrelation duly puts the strongest periodicity at 2.03–2.04× the played period, strengthening with `pitched`. A harmonic sieve disagrees and prefers f, since modes 0/2/4 also approximate 1:2:3 on f. The percept is bistable, the estimators contradict each other, and no measurement settles it. BJ's ear did: *„Ja, es reagiert."* The pitch follows the keyboard, the ratios stay as they are. **Anyone re-measuring this will rediscover the octave-below periodicity and take it for a defect. It is not.**

**BJ's verdict, 2026-07-20** — the four controls read as colour and not as volume (*„ja, gleich laut"*), and it reads as a drum, *„eher ein rauschen. lässt sich mit filter, pitch und env gut bearbeiten"*. That observation is what produced the `wave` convention now recorded in §4: a continuously excited head is a noise you shape back into a drum, which is real effort spent to make the result worse.

---

## 6. What the 28 unused physical-model opcodes actually deliver

Measured 2026-07-19 against §4's constraints. The result is much smaller than the backlog assumed, and the reason is structural, not a bug: struck and plucked models' decay *is* the model.

> **Re-measured 2026-07-20, and three of the verdicts below did not survive.** The rejections were made through a **malformed opcode call**, not through the opcodes. See the corrections inline and failure mode 9 in §7. Nothing in this section should be acted on without checking it against `tools/csound_model_probe.py`'s `MODELS` table, which has been corrected.

**Ready as-is:** ~~`wgclar`~~ — **this line contradicts the probe tool that produced it.** `tools/csound_model_probe.py`'s own header records that `wgclar` was adopted for `clarinet` and then *thrown out again*: inside 110–880 Hz it looks immaculate (+1 cent, sustain 1.056), but it emits near-pure DC above sr/10.4 and plays +2792…+6864 cents below 25 Hz. Both statements come from the same day. The probe's is the one grounded in a range sweep. `fmb3` is untouched by this and stands.

**Usable with a named, verified fix:** `fmmetal` (pre-scale `kfreq` ×1.3654), `fmpercfl` (×0.665), `fmvoice` (only ≥ ~880 Hz — its formants are fixed, so this is a structural floor, not a scale fix), `wgbow` (only 220–880 Hz, and it does not render twice the same — bow stick-slip is chaotic).

**Unusable for the `wave` reading, with the measurement.** Every entry here was disqualified for having a decay that cannot be switched off — which, under the `wave` convention (§4), disqualifies it *only* from the spectrum-source reading. Re-measured 2026-07-20 at `--format=float` (so nothing clips) with 1 Hz FFT bins, `kamp=0.3`, `0dbfs=1`, over 55–1760 Hz:

| opcode | peak @440 | half-life | tuning 55–1760 Hz | verdict |
|---|---|---|---|---|
| `fmrhode` | 0.23 | 0.60 s | ±0 ¢ throughout | **usable as an instrument** |
| `fmwurlie` | 0.18 | 0.73 s | ±0 ¢ throughout | **usable as an instrument** |
| `vibes` | 0.97 | 0.30 s | ±0 ¢ throughout | **usable as an instrument** |
| `marimba` | 2.92 | 0.04 s | ±0 ¢ until it collapses at 1760 Hz (−7586 ¢) | held back: a click at 3× full scale with a broken top |
| `mandol` | 0.00 | — | nonsense | silent at `kpluck=0`; rejection stands |
| `gogobel` | 2.40 | — | **20 Hz at every asked pitch** | rejection stands, but NOT for the documented reason |

**What the old verdicts got wrong, and why it matters more than the verdicts.** `marimba`/`vibes` were rejected here for „`idec` is provably inert — renders bit-identical at 0.05 and 500". `idec` is not inert. The probe passed the scalar `0.02` where `marimba`'s **`imp` argument requires a function-table number** (the strike impulse), so Csound raised *„No table for Marimba strike"* and deleted the note. The two „bit-identical renders" were two silences. With the correct signature `idec` plainly changes the render. The companion claim that „pitch freezes at strike, ignoring glide" is true and is the *correct* behaviour for a struck bar — it was only ever a disqualification for the `wave` reading. `gogobel` does fail, but it ignores `kfreq` outright rather than spiking on it; the probe tool's own header already warned that its argument types were wrong and *„fix the line before concluding anything"*, and that warning went unread.

Untouched by the re-measurement: `barmodel` (no frequency argument exists); `prepiano` (segfaults or hangs); `wgflute` (confirms the project's existing rejection — off-pitch, worst +2999 cents at 110 Hz).

**Eleven shakers and scrapers** (`bamboo cabasa crunch dripwater guiro sandpaper sekere shaker sleighbells stix tambourine`) are genuinely unpitched. That is **not a failure — it is the wrong family.** They belong with the existing nature beds (`wind rain surf thunder hiss crackle`), which are unpitched by design and are already built from repeated particle impulses (`dust2` at 1400/s for rain, 22/s for crackle). They cannot sustain — the energy-input parameter was tested directly and does not revive them (silent after ~0.5 s regardless) — but they re-strike cleanly, so a continuous texture is reachable through the existing particle-bed architecture. **This is the route for the natural-sound and animal part of the library, not the opcodes as drop-in sustained tones.**

---

## 7. Failure modes committed on this project — do not repeat

Each of these actually happened. They are recorded because prose rules that depend on self-recognition are exactly what failed.

1. **A whitelist of permitted waveforms.** Told to make the morph a real waveform morph, the response was to define a set of "real waveforms" allowed to morph (saw/square/pulse/triangle/sine/cheby) and leave everything else on the audio crossfade. That is the DCO's selector switch rebuilt. BJ: „ES GIBT HIER KEINE FUCKING VORGEGEBENEN WAVEFORMS. ES GIBT DEN FUCKING OUTPUT VON ZWEI FUCKING VORHER NICHT BEKANNTEN ALGORITHMEN AUS DER LIBRARY." Reverted.
2. **Collapsing per-instrument parameters into one shared list.** See §3. The same reflex — reduce to a convenient uniform structure — one level up.
3. **Treating the morph as machinery around two blocks.** Frames, capture buffers, table interpolation: all of it presumed a runtime wrapper. There is one Csound code. See §2.
4. **A gate that certified the broken state as correct.** `tools/csound_morph_liveness_gate.py` states in its own header that stages „render their own idiom and are equal-power crossfaded", and its travel assertion only measures whether the summed spectrum's centroid moves — which two co-sounding oscillators do exactly as well as one that transforms. It cannot see the difference it exists to police. The backlog item was marked complete while undone.
5. **Precision in the wrong place.** Hours of correct measurement (vco2 table semantics to three decimals, fundamental phase alignment) spent on a premise that was wrong. Measurement is not a substitute for checking the premise.
6. **Building without checking what exists.** `tools/csound_model_probe.py` already vetted opcodes for pitch tracking and sustain; a fresh harness was commissioned without looking.
7. **Measuring through a broken instrument, and writing the result down as physics.** Building instrument 3, the two-mode overlap and the `mode` power law were measured through a **16-bit render that was hard-clipping** (two unit-amplitude modes peak near 2.9 against `0dbfs=1`). The numbers that came back were not obviously wrong — a clean-looking power-law fit, and an apparent −0.6 dB *cancellation* at Q=28 that does not exist (+0.01 dB). They were about to be committed as measured fact, with the residual attributed to the opcode rather than to the meter. Separately, every "8-seed ensemble" quoted while building that instrument was **eight bit-identical renders**: Csound's `rand` carries its own `iseed` and ignores a global `seed` statement, so the averaging that the noise genuinely required was never happening. Both were caught by adversarial review, not by the measurements themselves. **A measurement harness is an instrument and needs its own calibration** — check that the signal is not clipping and that two supposedly-different runs actually differ, before averaging or fitting anything.
8. **Levelling a bank on the wrong law, and shipping colour controls that were volume controls.** Instrument 3's mode bank was normalised on `sum(a²)` — the intuitive "add the powers" — which ignores both that a noise-driven resonator's output scales with Q/f and that modes sharing one exciter add coherently where they overlap. Two of the four documented colour controls were therefore also faders (`damping` −4 dB, `spot` 3–5 dB). Every offline measurement of *the controls* passed; what was wrong was the physics underneath. The lesson is narrow and repeatable: **when a control changes a filter's Q or moves energy between modes, it changes loudness unless something is explicitly holding loudness** — and §4's invariant makes that non-optional.

9. **Rejecting a component on a call the component never accepted.** §6 disqualified `marimba` and `vibes` — two of the six instruments this project actually wants — on the finding that their decay argument was inert, "renders bit-identical at 0.05 and 500". They were bit-identical because they were both **silence**: `marimba`'s `imp` argument takes a function-table number for the strike impulse and the probe passed the scalar `0.02`, so Csound printed *"No table for Marimba strike"* and deleted the note. The verdict then travelled into §6 as a measured property of the opcode. Two things make this worse than a slip. First, **the tool's own header already said so** — *"gogobel/moog/pluck FAIL here on argument types, not availability … fix the line before concluding anything"* — and the same defect in `marimba`'s line went unnoticed while that warning sat six lines above it. Second, **the failure was loud, not silent**: Csound printed an INIT ERROR on every run, and a harness that reads a WAV without checking whether the render happened cannot tell "the model decayed" from "the note was deleted". This is the third time on this project that the meter, not the thing, produced the wrong number (see 7 and 8) — and the first time it cost capability rather than accuracy, because a wrong rejection removes an instrument silently and forever. **A negative result about a component needs the same proof of contact as a positive one:** assert that the render is non-empty and the tool exited clean before reading a single sample.

---

## 8. What is structurally in the way

- **Adding one key today requires hand-editing ~10 Python sets** that do not derive from the lexicon (`_TONAL_KEYS`, `_NOISE_TECH`, `_MODAL_TECH`+`_MODAL_SPECTRA`+`_MODAL_PARAMS`, `_VOICE_TECH`, `_LIVE_TECH`, `_CHEAP_TECH`, `_SPARSE_TECH`, `_SELF_MOVING_TECH`, `_PULSE_FAMILY`, `_CS_TERMINALS`). Miss one and the key falls through to a bare `poscil` — and a tools file reports the omission only as a non-fatal "GAP". A library of dozens of instruments cannot grow through ten manual edits per entry with a silent failure path.
- **Cross-cutting properties are on the wrong side.** `_ADJ_MAP` applies `gritty`/`dirty`/`airy` as bounded DSP operations on the **mixed** signal — a waveshaper hung on the end. §1 requires them to flow into the **code generation**, i.e. to move the instrument's own parameters, falling back to a post-effect only where the instrument has no parameter for that quality.
- **The morph is still an amplitude crossfade** (`_emit_crossfade_morph`): both stages render their full idiom and their audio is equal-power blended, so mid-morph both are heard. The emitter's own docstring says so.
- **Nine of the 27 existing keys have no steerable parameter at all** — including `saw`, `square`, `pulse`, `triangle`, the most-played sounds. `square`'s duty is nailed to 0.5 and `pulse`'s to 0.30 as structural constants, which is precisely why BJ's own example („square ist sharp wenn x=y, hollow wenn x=z") was impossible before instrument 1.
- **The parametrisation layer was built twice before it was ever connected — check the wire, not the feature.** For two instruments the lexicon carried the parameters, the emitters consumed them, the parser accepted them, every offline measurement showed the controls moving exactly what they promised — and the routing model never saw them at all. `_build_catalogue` emitted only `key: why`; there was no `params:` line, and not one anchor word (`clangy`, `hot`, `worn`) appeared anywhere in the prompt. The system prompt meanwhile *instructed* the model to read a "params:" line that had never been written. Nothing failed loudly: an anchor the model guessed wrong was silently dropped back to the default, and the reading then correctly hid what was never applied, so the hole was invisible from both ends. It took BJ's ear, on `rhodes` routing to `saw`, to surface it. **A capability is not built until the layer that must reach it can see it** — and the test for that is not "do my measurements pass", it is "does the thing that drives this have the words". Fixed 2026-07-19; the same session found the sibling defect that the parameter regex could not match a multi-word surface form, so `analog oscillator(drive=hot)` shipped silently losing every parameter.
- **Much of the curation already exists as prose.** `backend/csound_orch.py`'s comments carry dozens of measured ranges and rejected values with numbers (cheby pinning 34% of samples at drive 0.91; sub_sine's third harmonic at −26 dB as an audible hollow fifth; ring-mod drift at 0.0009 measuring 1.01× travel, i.e. nothing). That is the beginning of the parameter spectra — written where the model cannot read it.

---

## 9. Open items, ranked

1. `age` inaudible on instrument 1 (deferred by BJ; cause **found**, see §5 — two of its three components run at 0.04–0.06 Hz and are a static offset on a short note; needs a faster layer, not more depth).
2. **The proof of concept is complete and all three instruments are ear-approved.** What BJ asked for — „wir beginnen mit 3 unterschiedlichen Instrumenten. die parametrisieren wir" — is done, so **what comes next is a direction decision, not a fourth instrument.** Each still carries one open item of its own: `age` on instrument 1 (item 1), the odd/even travel on instrument 2 (§5), nothing outstanding on instrument 3.
3. **The `wave` convention** (§4) — named by BJ and explicitly deferred by him, then named again unprompted the next day on a second instrument. It would turn the self-decay rule of thumb into a choice made in the prompt. The reason it ranks this high despite being deferred: **all three built instruments turn out to be `wave` readings, and the convention's other half does not exist** — so this is not a refinement of what has been built, it is the half that has not been. Its material is §6's rejection list, whose disqualifications are disqualifications for the `wave` reading only. Not to be built without BJ's say-so.
4. The anchors are **calculated, not heard.** Where "sharp", "hollow", "old" sit on each axis is BJ's ear, not a measurement. This is the curation step and it cannot be delegated to a gate.
5. Cross-cutting properties into generation (§8).
6. The morph as a real waveform morph (§8) — backlog item #9, reopened.
7. The ten hand-maintained sets (§8) — the growth blocker.
8. The whole modal family (`glass`, `struck_bar`, `cymbal`, and now `drum_head`) drops ~20 dB and hits the limiter when `kfreq` jumps mid-voice — reachable with legato and glide. Every measurement of this family so far was taken on a *settled* bank, so "1.07 dB of within-note travel" must not be read as "this family has no loudness travel".
