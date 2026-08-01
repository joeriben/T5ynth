# Where the oscillator's Csound may come from

akróasys is **GPLv3**. Every instrument body in `backend/dco_lexicon.json` ships
inside that licence and is put in front of a language model as an exemplar to
adapt, so a body with incompatible terms would contaminate both the repository
and everything the author writes from it.

This file is the verdict per source, so the question is settled once instead of
per instrument. It was written after a survey of the field's standard sources;
every licence below was read on the source's own pages, not inferred.

`docs/LCO_CSOUND_SOURCES_AND_LICENCES.md` answers the same question from the
other end — the practice that follows for authoring, and what to do if a source
ever does turn out usable. The two must not disagree: they did, on the manual's
GFDL version, and the version stated here is the one read from the primary
source. Where they differ again, this file is the licence record.

## The rule

**Author fresh against documented opcode argument lists and the acoustics
literature. Copy no orchestra code.**

An opcode's argument list is a functional interface and using it is not copying.
A published measurement — a partial ratio set, a formant frequency, a decay time —
is a fact and not protected expression. The *orchestra that somebody wrote* is
protected expression, and the two richest collections of it are both unusable
here. So the library is authored, and every entry's `why` carries the measurement
it was built from rather than a pointer to code somebody else owns.

This is not a limitation in practice: the substrate already knows the domain.
This build (Csound 6.18, Homebrew, double, **no STK**) carries 1607 opcode names
over 2377 name/signature rows (`csound -z1 | awk '{print $1}' | sort -u | wc -l`),
including `vco2` with a real duty-cycle input, `gbuzz`, `mode`, `barmodel`,
`prepiano`, `wgbow`, `wgbowedbar`, `wgclar`, `wgbrass`, `wgflute`, `repluck`,
`streson`, `marimba`, `gogobel`, `vibes`, `dripwater`, `shaker`, `scanu`,
`lorenz`, `chuap`, `wterrain`, `moogladder`, `nlfilt`, `resonx`. An entry is a
documented opcode plus measured constants — which is what the existing 50 are.

## AVOID — incompatible with GPLv3

| Source | Licence | Why it is out |
|---|---|---|
| **Iain McCurdy, Csound Realtime Examples** (`iainmccurdy.org/csound.html`, 580 `.csd`) | **CC BY-NC-SA 4.0** | The NonCommercial clause cannot be relicensed into GPLv3. The FSF does not count this licence as free ("there are restrictions on charging money for copies"). Unlike CC BY-SA 4.0, which *is* one-way compatible with GPLv3, NC is fatal. The terms are stated once on the page; **not one of the 580 files carries a notice**, so a file taken out of the archive looks unencumbered and is not. This is the most-recommended source in the field and the most dangerous. |
| **CsoundQt's bundled `src/Examples/McCurdy Collection`** (294 paths) | same, undisclosed | CsoundQt's `COPYING` covers the C++ sources (LGPLv2.1) and `opcodes.xml` (GFDL) and is **silent about these examples**, so the NC terms travel with them unannounced. Same for René Jopi's QuteCsound port. |
| **The Canonical Csound Reference Manual** — all 1417 example `.csd` files | **GFDL 1.3** | `docs/intro/copyright-notice.md`: released under the GNU Free Documentation Licence. GFDL is incompatible with the GPL **in both directions** — GFDL material cannot be put into GPL code. No example carries a per-file notice, so they are all GFDL. This is the painful one: the manual is the most obvious idiom source and it is closed to us. Read it to learn an opcode's arguments; do not copy its orchestras. |
| **Jim Woodhouse, *Euphonics — The Science of Musical Instruments*** (`euphonics.org`) — the TEXT, figures and any code on it | **CC BY-NC-SA 4.0** — "Except where otherwise noted, the content on this site is licensed under a Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License." | Same NC clause as McCurdy, same verdict for the same reason: nothing written there may be copied into this GPLv3 tree. **But the MEASURED NUMBERS in it are facts and are usable**, and the rule at the top of this file is what says so — a resonance frequency somebody measured is not protected expression, and citing where it was measured is scholarship. So `euphonics.org` sits in two places on purpose: here for its prose, and in the measurement table below for its physics. Read it, take the numbers, write your own Csound, name the source. Do not paste a sentence of it. |

## UNKNOWN — no licence statement found, therefore unusable

Absence of a licence is not permission.

- **The Amsterdam Catalog** (John-Philipp Gather, ~100 instruments incl. Risset's
  25 Bell Labs instruments). Its home at `music.buffalo.edu/hiller/accci/` now
  redirects to the university root — **the site is gone**; the codemist mirror
  refuses connections. No licence on any reachable page, and the Haskell port
  `spell-music/amsterdam` has none either.
- **`csound/book`** and **`csound/examples`** — both full trees walked, **no
  LICENSE or COPYING anywhere**, READMEs two lines. Csound's own LGPL does **not**
  reach them: they are separate repositories. This answers the obvious hope
  directly — the LGPL covers only examples physically inside `csound/csound`.
- **Boulanger's Csound Book CD material** — csounds.com unreachable.
- **The csoundforlive "Csound Instrument Catalog v2.5"** (~14 GB) — host
  unreachable, no licence verified.
- **`ReneNyffenegger/csound-instruments`** — no licence detected.

## USABLE

| Source | Licence | Note |
|---|---|---|
| **Examples inside `csound/csound`** (`trapped.csd`, `xanadu.csd`, `par.csd`) | **LGPL-2.1** via the repo-wide `COPYING` | LGPL-2.1 → GPLv3 is permitted (LGPL-2.1 §3). Residual caveat: no per-file notice, and `trapped.csd`'s header credits only "Richard Boulanger", so authorship is ambiguous even though the terms are not. |
| **Steven Yi, `csound-live-code`** | **MIT**, declared in `package.json` only | GitHub's licence API returns 404 (no LICENSE file) and `livecode.orc` names only its author. MIT is GPLv3-compatible and this is plain orchestra code (`.orc` UDOs), so it is the best-fitting usable source found. The declaration lives in one metadata field — worth an email to get a LICENSE file before relying on it. |
| **`spell-music/csound-catalog`** | **BSD-3-Clause** | Licence is fine, shape is wrong: it is Haskell that *generates* Csound, not orchestra bodies. |

## The measurement sources the entries are built from

The rule above says a published measurement is a fact and not protected expression, and that
every entry's `why` carries the measurement it was built from. This table is the other half of
that: the studies themselves, in one place, so a reader can check an entry against its source
without reading 32 `why` fields. Nothing here is Csound code — that is the point.

**The lexicon's other half has its own record.** This file covers the instrument BODIES. The 51
sound words — `bright`, `hollow`, `rich`, `warm` — are read by the author model in the same way
and had no source at all; `docs/LCO_TIMBRE_SEMANTICS.md` is where theirs is kept.

| Entry | Source | What was taken |
|---|---|---|
| **`supersaw`** | Adam Szabo, *How to Emulate the Super Saw*, BSc thesis in Media Technology, KTH Royal Institute of Technology, Stockholm 2010. TRITA-CSC-E 2010:131, ISRN-KTH/CSC/E--10/131--SE, ISSN-1653-5715. | A Roland JP-8000 and JP-8080 measured with an FFT analyser and an oscilloscope. Four sets of numbers: the seven detune offsets (table 1), the 11th degree polynomial the detune knob follows (fig 7), the two mix curves — centre linear, sides parabolic, meeting at 0.75 (fig 11), and the pitch-tracking high pass on the first harmonic (fig 14/15). Plus one structural fact: the saws are deliberately NOT band limited, and the fold-back is the sound. The thesis states in its own abstract that no Roland source code or copyrighted technique was used and that the recreation is an emulation built from standard components. |
| **`plucked_wire`** | `Opcodes/repluck.c` in `csound/csound` (John ffitch 1996, Victor Lazzarini 1998) · Jaffe & Smith, CMJ 7(2), 1983 · Järveläinen & Tolonen, *Perceptual Tolerances for Decay Parameters in Plucked String Synthesis*, JAES 49(11), 2001, 1049–1059 · Karjalainen, Välimäki & Tolonen, CMJ 22(3), 1998. | The loop filter `state = state·refl + y·(1−refl)` and what `refl` therefore is (a one-pole pole, not a per-period damping); the pickup comb `\|sin(nπβ)\|`; and the published tolerance that decay-time variations of 25–40 % are inaudible, which decides what step size a listening test may use at all. |
| **the bowed string** (in build, `tools/lco_ab_string_bowed/`) | Julius O. Smith III, *Efficient Simulation of the Reed-Bore and Bow-String Mechanisms*, Proc. ICMC 1986, 275–280 · Jim Woodhouse, *Euphonics — The Science of Musical Instruments*, §5.3 "Signature modes and formants", https://euphonics.org/5-3-signature-modes-and-formants/ (numbers originally from Bissinger's modal surveys in JASA) | From Smith: the waveguide-plus-nonlinear-junction formulation the bow's stick-slip needs, which is the model Csound's `wgbow` implements. From Woodhouse: **what a violin body does to the string's force at the bridge** — the four signature modes A0 272 Hz (the internal air breathing through the f-holes), CBR 407 Hz, B1− 462 Hz, B1+ 551 Hz, and the bridge hill peaking around 2.3 kHz; also that a cello bridge has its own three (sway ~1.5 kHz, bend ~2.2, bounce ~3.1). Four frequencies and a formant, written into a `mode` bank of our own. **The Q values were NOT taken from there** — that source does not give them, and the entry says so rather than borrowing the authority. Note the licence: the site is CC BY-NC-SA, so its numbers are usable and its text is not (see the AVOID table). |

Reading a measurement out of a paper and writing fresh Csound to it is the practice
`docs/LCO_CSOUND_SOURCES_AND_LICENCES.md` calls *take physics, not code*. It is also what
`CLAUDE.md`'s Instrument Authoring rule 1 requires before the first orchestra line: a named
method AND a source, written down first.

## Irrelevant regardless of licence: binary opcode plugins

`csound-plugins` (per-collection `risset.json` declares LGPL; `src/else/else.c`
carries a proper LGPL-2.1-or-later header; `src/jsfx/LICENSE` is Apache-2.0),
`csound/plugins` and `vlazzarini/opcode_compiler` are all **compiled plugin
opcodes**. The oscillator runs inside the plugin against whatever Csound the user
has; a plugin dependency is not available to it, so these are out on capability
grounds before licence enters into it. (Note for anyone searching: the risset
collection is `pathtools`, not "pathway", and it is path/string handling, not
synthesis.)

## Authoring hazards in this build, found the hard way

Not licence, but the same file serves: these are the traps an author hits when
writing against opcode signatures rather than copying working examples.

1. **`gendy`/`gendyc`/`gendyx` ignore their amplitude argument as a scalar.**
   With amp 0.3 they produced peaks of 534 and 538017 (23542 clipped samples).
   They need `balance` against a reference plus `clip`.
2. **A variable must never be named after an opcode.** `atone resonx ...` fails
   with `syntax error, unexpected T_OPCODE` — which reads like a typo, not a
   naming rule.
3. **Argument RATES are load-bearing.** `squinewave` needs a-rate inputs,
   `repluck`'s excitation must be a-rate, and **`prepiano` returns two signals**.
   The wrong rate gives "Unable to find opcode entry with matching argument
   types", which reads like a missing opcode rather than a signature error. The
   bagpipe in this library hit exactly this: `kch gbuzz ...`, a k-rate name on an
   a-rate output.
4. **`vco2 imode` is a BIT SUM, and bit 1 skips initialisation.** `vco2 …, 1`
   compiles and is SILENT — which is why `lco_write.perform_check` plays 0.25 s
   instead of only compiling.
