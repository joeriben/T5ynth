# Where LCO instrument code may come from, and where it may not

BJ's instruction on extending the library was explicit: *„Wo nötig wirst Du recherchieren um
die sonischen qualitäten und sinnvolle parametrisierungen oder weniger bekannt bibliotheken
für csound einbeziehen. (aber Lizenzen immer beachten!)"* This is the licence answer, checked
rather than assumed, and the practice that follows from it.

akróasys is **GPLv3**. Anything embedded in `backend/dco_lexicon.json` ships inside it, so
the question is not "may I read this" but "may this be distributed under GPLv3".

## What was checked

| Source | Licence found | Usable in this library? |
|---|---|---|
| Csound itself (the opcodes) | **LGPL 2.1 or later** — stated in the manual's copyright notice | Yes. Calling `vco2`, `mode`, `gbuzz` is use of the software, not copying of code. This is the whole basis of the LCO. |
| The Canonical Csound Reference Manual, **including its example orchestras** | **GFDL 1.2 or later.** The notice covers "this document" and says nothing separate about the `.csd` examples, so they inherit the documentation licence | **No.** The GFDL is a documentation licence and is not GPL-compatible; there is no clean path from a GFDL example orchestra into a GPLv3 binary. |
| Amsterdam Catalog of Csound Computer Instruments (Gather, 1995; ~100 instruments incl. all 25 Risset Bell Labs ones) | **None stated.** Neither the catalogue's own site nor the Haskell port carries a LICENSE file, badge or copyright grant | **No.** No stated licence means all rights reserved. |
| The Csound Book CD-ROMs (Boulanger, MIT Press 2000) | **None stated** for the instrument files | **No.** Same reason, and it is a commercial publication. |

## The practice that follows

**Take physics, not code.** Every instrument in this library is written from published
acoustics — mode ratios, coupling behaviour, resonator type, the laws that govern them — and
no Csound is copied from any collection. That is not a workaround; it is a better fit for
what this library has to be, for three reasons:

1. **It is licence-clean by construction.** A physical law is not copyrightable. Mode ratios
   of 1 : 2 : 3 for a hammer-tuned handpan area, f = (1/2πR)·√(3γP/ρ) for a bubble, Q/(πf)
   for a ring time, f = St·v/d for an aeolian tone — these are facts, and citing where they
   were measured is scholarship, not attribution of code.
2. **The bodies here have to satisfy constraints no published collection was written for.**
   Every entry must hold ONE loudness across its whole parameter cube and the whole keyboard
   (§4 of `docs/LCO_CONCEPT.md`), must write into `asig` inside this host's scaffold, and
   must move. A catalogue instrument written as a standalone `.csd` with its own score,
   envelope and fixed pitch satisfies none of that, so it would have to be rewritten anyway.
   The physics is the part worth having.
3. **It is what makes the notes usable.** The author LLM reads each entry's `note` to choose
   settings. A note that says *why* — "a Helmholtz resonance fixed in HERTZ, so it does not
   transpose with the keyboard", "`mode` hands a resonance at f/2 twice the amplitude of one
   at f for the same drive" — is worth more to it than provenance would be.

## What this rules in

Reading a catalogue instrument to learn **which opcode idiom** suits a family is fine and is
not copying: that `foscil` is the FM idiom, that `mode` banks model struck bodies, that
`wgclar`/`wgbow` exist at all. Substrate idioms are the documented interface of an LGPL
library, and `CLAUDE.md`'s Migration & Substrate Discipline actively *requires* using them
rather than hand-rolling a crippled subset.

What is ruled out is lifting an orchestra body, a constant table, or a chain of filters with
its tuning, from any of the three unlicensed sources above.

## If a source ever does turn out to be usable

Then it still has to clear the same gate as everything else — `tools/lco_axis_probe.py
--gate`, one loudness across the cube and the keyboard, movement at every register — and its
licence has to be recorded in this file and in the entry's own note before it ships. Nothing
enters the lexicon on provenance alone.
