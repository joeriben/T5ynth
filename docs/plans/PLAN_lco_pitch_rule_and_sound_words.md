# PLAN — the pitch rule's intent, and the five changes that follow

**Status: confirmed by BJ 2026-07-29 („ok. behalte das so."), not yet executed.** Five changes, one
commit each. This file is the plan's record in the repository; it was previously only in the session
work-order file. Nothing here is new — it is the plan as BJ left it, including what he struck.

**Source.** The one observation that starts it is BJ's own, on reading the author's HARD RULE:

> „das ist ja auch eine zu starre formulierung. Resonatoren haben feste freqs, insofern. der geist
> der regel, ihre Intention, entspricht nicht ihrer formuleirung"

The one research contribution used, for item D only: **Saitis, Charalampos & Weinzierl, Stefan
(2019): „The Semantics of Timbre"**, in Siedenburg et al. (eds.), *Timbre: Acoustics, Perception, and
Cognition*, SHAR 69, Springer, 119–149 — evaluated in `docs/LCO_TIMBRE_SEMANTICS.md` §1, where §7
records every contribution this project leans on. Items A, B, C and E follow from reading the code
against BJ's ruling, not from any paper.

---

## A. One rule becomes three

**Where:** `backend/lco_write.py:453` (`_SYSTEM_HEAD`) **and** `docs/LCO_CONCEPT.md:103` — both carry
the same too-rigid wording („everything you write must track `kfreq`, never hardcode a frequency
number").

The intent behind it survives intact; the formulation forbids things the library itself already does
and does correctly. Three rules instead of one:

1. **Pitch** is `kfreq` times a ratio, never a number.
2. **A resonance may have fixed Hz.** A formant, a body resonance, a noise band, an LFO rate belong
   to the **thing**, not to the note — and they get marked the way `brass` and `strings` already mark
   theirs: `; bell formant, FIXED in Hz` (1200), `; body resonance, FIXED` (460).
3. **A landmark** („above the 6th harmonic") is a **ratio** to `kfreq`. 2200 Hz is a different word at
   55 Hz than at 880 Hz.

The strongest evidence this reformulation is right: it describes the library as it already is.
`docs/LCO_CONCEPT.md:223` already knows the distinction for `fmvoice` („only ≥ ~880 Hz — its formants
are fixed, so this is a structural floor, not a scale fix").

## B. `shimmer`'s absolute corner

**Where:** `backend/dco_lexicon.json`, motion `shimmer`: `butterhp asig, 2200` → a ratio to `kfreq`.
One line. It is the one place in the whole file where a literal frequency is neither a resonance nor
an LFO rate, i.e. the only real violation the audit found.

## C. Mark the four formants

**Where:** `backend/dco_lexicon.json` — `woody` (600 Hz), `boxy` (500), `brassy` (1600), `resonant`
(1100). Their `reson` formants get A.2's annotation. **Marking only, no change to any sound.**

## D. Attack is spectral, not amplitudinal

**Where:** next to the amplitude-envelope rule in `_SYSTEM_HEAD` (`backend/lco_write.py:462`). BJ:
„trivial, ja."

Basis: [1] defines attack time as „the time needed by spectral components to stabilize into nearly
periodic oscillations". The oscillator does not own the amplitude envelope, so it cannot write an
attack — but it *can* write how fast its spectrum settles, and that is what the four attack words in
the lexicon are actually about.

## E. The six words whose fundament sits below the fundamental

**Where:** `backend/dco_lexicon.json` — `deep`, `punchy`, `muddy`, `full`, `velvety`, `vibrant`, all
six carrying `albd tone asig, 220`.

220 Hz is A3. Above that note the mixed-in fundament lies **under** the fundamental and adds nothing.
The word stops working exactly where most playing happens. Defective under any reading of the pitch
rule, which is why it is in this plan and not in the struck list.

---

## Struck by BJ — do not reintroduce

- **The 38 `tone`/`atone` tilt words.** „filter wird extern verdrahtet von parallelsession, insofern
  dann - anders - legitim." What remains is only the absolute corner, and item A covers it.
- **The 51 `why` lines.** „das wird allenfalls nach adversarialem Check angefasst. LRO funktioniert
  bereits, die Behauptung die Du hier machst ist einigermaßen kühn."
- **The German inflected surface forms.** „das ist ein en synth, nicht de."
- **„A missing time-organisation axis."** It exists — in `motions`, as the field `kind`
  (gesture / periodic / unsteady / wandering / none). My own search error in `adjectives`.

## Open, with BJ's own valuation

- **Formants matter.** `nasal` carries a copy of the `analog_osc` body although its own description
  names a formant. BJ: „geht gar nicht, aber formanten wären schon wichtig." The real question is
  whether the **author** is offered a formant as a means at all — the system prompt says nothing
  about it. To be drafted after A–E.
- **Roughness and `rumble` are not understood here**, stated by BJ explicitly. Neither exists in the
  lexicon (`rumble` appears only as an example prompt in a comment, `backend/lco_write.py:2225`).
  Before any orchestra line: named method + source, then `nearest_existing.wav` against the nearest
  existing entry, BJ's ear decides, no self-built meter. Roughness is also the one substrate [1]
  documents for which this library has no mechanism
  (`docs/LCO_TIMBRE_SEMANTICS.md` §4.3).
- **The three-class line (material / friction / voice) stays parked.** BJ: „naja. du hast es selbst
  noch nicht verstanden." It sorts by friction, and while friction is not understood it sorts wrong.
