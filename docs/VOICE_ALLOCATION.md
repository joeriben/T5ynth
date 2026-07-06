# Voice allocation — design and historical grounding

How T5ynth assigns the 16-voice pool to notes, and the classic-polysynth
research that grounds the choice. Written 2026-07-06 after the repeated-note
"neue Attack / kein Release" bug; the research was prompted by the question
"does the Juno-106 actually work the way the fix assumed?" (it did not).

## T5ynth's scheme

Poly path (`VoiceManager::noteOn`, `src/dsp/VoiceManager.cpp`):

```
int idx = findFreeVoice();        // rotate: take a fresh voice
if (idx < 0) idx = stealVoice();  // pool full: steal the oldest (lowest noteOnTimestamp)
```

- A repeated note of the **same pitch** lands on a **fresh voice**; the previous
  note keeps its own voice and rings out its release (poly overlap). With a long
  release the old tail crossfades under the new attack — no re-attack, no cut.
- Steal victim = **oldest note** (lowest `noteOnTimestamp`).
- Mono (`voiceLimit == 1`) is a separate legato branch, not covered here.
- **Rejected:** a held-vs-releasing same-note branch (reuse the voice if the
  repeat's predecessor is still *held*, rotate if it is *releasing*). Shipped as
  `findHeldVoiceForNote` in commit `b5cf802d`, reverted in `1c346727` — no
  hardware precedent (see below).

This corresponds to the **ROTATE** pole of a real, hardware-attested design fork
(Juno-106 "ROTARY", Oberheim Eight Voice "Poly 1", Jupiter-8 "Poly 1"), as
opposed to the **REUSE** pole (Prophet-5 default, Juno-106 "POLY", Eight Voice
"Poly 2").

## The two design poles

Classic polysynths resolve a repeated same-pitch note one of two ways, and
several exposed the choice as a front-panel switch:

- **REUSE / NON-REASSIGN** — the repeat re-triggers the *same* voice. One voice,
  re-attacks. (Prophet-5 default; Juno-106 POLY; Oberheim Eight Voice Poly-2.)
- **ROTATE / REASSIGN** — the repeat takes a *new* voice; the previous one rings
  out. Voices thicken/overlap. (Juno-106 ROTARY; Eight Voice Poly-1; JP-8
  Poly-1.) **← T5ynth uses this.**

Neither pole branches on the predecessor's envelope state; the choice is
unconditional per mode.

## Per-machine findings (verbatim, with source tier)

**Roland Juno-106** — Service Notes, 1984 (PRIMARY):
> "POLY 1 & POLY 2: The voices are assigned to the keys played in the order CH1
> to CH6 as long as the previous keys are held down. One-key staccato always
> sounds CH1 only. ROTARY: The voices are assigned in cyclic manner; 7th key
> steals the voice from the 1st key."

So the machine the fix was named after has a **switchable** mode: POLY reuses one
voice for a repeated key ("staccato always sounds CH1 only"); ROTARY rotates and
steals the oldest. Neither is release-state-aware.

**Roland Juno-6 / Juno-60** — Service Notes (PRIMARY):
> "when the 7th key is played while previously played 6 keys are still held, the
> 7th key steals the first voice." … "The software steals a voice from a channel
> whose gate first turned ON among 6 channels." (oldest-note steal.)

**Korg Polysix** — Owner's Manual, 1981 (PRIMARY):
> "using 'rotary' assignment with last-note priority. Each key pressed activates
> a new voice; thus, the old voices can continue to sound or release for a more
> natural or spacious sound. If more than six notes are held down at once, the
> 'oldest' voice (or voices) are reassigned…"

On repeats: *"Each key repetition will 'stack' the next voice in sequence on that
note."* — i.e. **same-pitch stacking under hold is documented, intended
behavior**, not a defect. This is the closest match to T5ynth's scheme.

**Sequential Prophet-5** — Owner's Manual §1-7 + Service Manual §2-2 (PRIMARY,
one author/corroborated):
> "If the same key is struck repeatedly, the microcomputer assigns the same
> voice. … the earliest-used voice is reassigned to each new note played."

The Prophet-5 is the **REUSE** pole — a repeated note re-triggers one voice
(this is exactly the cut-release symptom the bug report described).

**Roland Jupiter-8** — Owner's Manual (PRIMARY):
> "Poly-1, Poly-2 and Unison Modes all follow… first note priority. When eight
> notes are played and held, no new notes can be sounded until one of the
> original eight is released."
> Poly-1: notes reach "full natural release length." Poly-2: "only the last note
> … receive their natural Release length. Any notes played previously will have
> instantaneous Releases… very similar to a Piano with the damper pedal held
> down or left up."

The JP-8 **refuses** new notes when full rather than stealing. Poly-1 vs Poly-2
is a release-handling choice (let ring vs hard-cut), not an assignment choice.

**Oberheim Eight Voice + Dave Rossum** — Cherry Audio Eight Voice manual
(REPRINT) + Rossum interview, Amazona.de 2015 (PRIMARY):
> Eight Voice: "Poly 1 — Voice modules play in order of notes played… voice 1
> would be stolen, then voice 2. Poly 2 — … if a note is repeated it will always
> attempt to play the same voice… useful when playing a repeated bass note that
> you don't want to get stolen."
>
> Rossum: "We also had logic for when you played the same note over and over
> again: it would assign it to the same voice, eliminating an annoying cycling
> variation in the sound due to the inevitable slight variations in the analog
> synthesizer voices."

Reuse (Poly-2) was a **deliberate, motivated** design — but the motivation was
masking **analog voice-card mismatch**. It does not apply to T5ynth, whose
voices share one sample buffer (rotating causes no timbral cycling).

**Yamaha CS-80** — Milson interactive KAS-board diagram (SECONDARY; page could
not be fetched directly, quoted via search extract):
> "when a key is released, the associated voice becomes idle and is moved to the
> end of the queue."

A release-to-end-of-queue policy — the fuller round-robin that reuses the
longest-released voice last.

**Rossum voice-assignment patent US3986423A** (Oberheim/E-mu, filed 1974,
PRIMARY) — defines NON-REASSIGN vs REASSIGN modes; a channel is "busy" purely by
gate-high (key-down), and a released voice keeps decaying but is immediately
available for reassignment (envelope state is not read back into the assigner).

## Steal policy

Every documented machine steals the **oldest** note (earliest gate-on).
T5ynth's `stealVoice()` picks the lowest `noteOnTimestamp` — matches.

Optional refinements, **not implemented** (flagged for the record):

- **True round-robin queue** (CS-80: released voice → end of queue) rather than
  `findFreeVoice`'s lowest-free-index. Maximizes ring-out before a slot is reused.
- **Release-aware steal victim** — when the pool is full, prefer a voice already
  in its release phase and protect held top/bottom notes. Precedent: JUCE's own
  default (`"re-use the oldest notes first… protect the lowest & topmost, even if
  sustained, but not if released"`); period patents Yamaha US3981217 / US4082027
  ("channel in which decay has progressed furthest"), Baldwin US4442746
  (released-sustain notes stolen oldest-first). This is a steal-*victim* choice,
  distinct from the same-note-repeat decision.

## Why the held-vs-releasing branch was rejected

No period synth branches *allocation* of a repeated note on whether the
predecessor voice is held vs releasing. Reuse (Poly-2 / Juno POLY) is
unconditional; rotate (Poly-1 / Juno ROTARY) is unconditional. Release-state
awareness appears **only** in the steal-victim choice (which voice to sacrifice
when the pool is full), never in the same-note-repeat path.

`b5cf802d` added exactly such a branch to stop pedal-sustained repeats from
stacking. But the stacking it "fixed" is authentic (Polysix: *"each key
repetition will stack the next voice"*), and the branch matched nothing in the
hardware record — so it was reverted (`1c346727`). Sustain-held repeats stack and
thicken, self-limited by the 16-voice pool + oldest-steal, as on the Polysix.

## Sources and caveats

- **Primary:** service/owner manuals (Juno-106/60, Polysix, Prophet-5, JP-8);
  patents US3986423A (Rossum/Oberheim), US3981217/US4082027 (Yamaha/Nippon
  Gakki), US4442746 (Baldwin); Dave Rossum interview (Amazona.de 2015, via
  Wayback).
- **Reputable secondary:** Sound On Sound "Synth Secrets" (Gordon Reid, Parts
  18/21); Cherry Audio Eight Voice manual (modern reprint of 1977 behavior);
  CS-80 KAS-board diagram.
- **Gaps (stated honestly):** the microprocessor-era Oberheim OB-X/OB-Xa/OB-8
  firmware assignment algorithm is undocumented in any accessible primary source
  (manuals stop at the ROM boundary). The MIDI 1.0 spec explicitly leaves
  polyphony-overflow handling to the manufacturer ("the receiver is free to use
  any method"). No firsthand Dave Smith quote on the Prophet-5 algorithm was
  found; its behavior comes from Sequential's own manuals.
- **Verification note:** quotes were cross-checked against raw manual/patent text
  where possible. A WebFetch summarizer was caught inserting "round-robin" into
  the Prophet-5 manual (the raw text says only "last note priority / earliest
  used voice reassigned") — corrected against a direct grep. "Round robin" as a
  named mode is a **modern reissue** feature (Prophet-5/10 Rev4), not the 1978
  algorithm.
