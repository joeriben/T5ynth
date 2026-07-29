# Where the oscillator's SOUND WORDS come from

The library has two halves. The **bodies** have a provenance record: `docs/LCO_CODE_PROVENANCE.md`
says where an instrument's physics was read, and `CLAUDE.md`'s Instrument Authoring rule 1
requires a named method AND a source before the first orchestra line.

The **words** have had none. `backend/dco_lexicon.json` carries 51 sound words, and each one's
`why` line is the only definition the author model ever sees of what that word means. Those lines
were written from intuition. One of them says so in as many words — `warm`: *„dark +
2nd-harmonic glow — a convention, stated, arguable"*.

This file is the other record. It does for the word layer what the provenance file does for the
code layer.

> **Rewritten 2026-07-29 after BJ rejected the first version outright** — *„Dein Entwurf geht von
> VÖLLIG falschen Vorstellungen über diese sonischen Eigenschaften aus. Recherchiere mal was das
> sonisch bedeutet."* He was right, and the error was one thing: the first version was built on a
> review of studies of **acoustic instrument recordings** and applied its correlates to a
> **synthesiser**. There is an empirical study of exactly this transfer, and it says the transfer
> does not hold. §„What the first version got wrong" at the end keeps the record.

---

## The two sources, and which one governs here

> **[1] Charalampos Saitis & Stefan Weinzierl, The Semantics of Timbre.** In: Siedenburg, Saitis,
> McAdams, Popper & Fay (eds), *Timbre: Acoustics, Perception, and Cognition*, Springer Handbook
> of Auditory Research 69, Springer 2019, pp. 119–149. DOI `10.1007/978-3-030-14832-4_5`.

A review of ~60 years of semantic studies, almost all of them on recorded acoustic instruments.
It is the standard reference and it names its own limit in its abstract: the mechanisms remain
underexplored *„especially when one looks beyond the case of acoustic musical instruments"*.

> **[2] Ben Hayes, Charalampos Saitis & György Fazekas, Disembodied Timbres: a Study on
> Semantically Prompted FM Synthesis.** *J. Audio Eng. Soc.*, 2020/2022. Short version: *There's
> More to Timbre than Musical Instruments: Semantic Dimensions of FM Sounds*, Proc. 2nd Int. Conf.
> on Timbre (Timbre 2020), Thessaloniki. Data and sounds on Zenodo `10.5281/zenodo.4609790`,
> code at `github.com/ben-hayes/fm-synth-study`.

**This is the one that governs this project.** Thirty experienced sound designers *built* sounds
on a three-operator FM synthesiser to satisfy comparative prompts (*brighter / less bright,
thicker / less thick, rougher / less rough*), at three registers, then rated what they had made
on 27 scales. The vocabulary was not taken from orchestration treatises: it was mined from
**1.4 million posts on a modular-synth forum**, filtered to adjectives co-occurring with
*sound / sounding / tone / timbre*, and pruned to 27. It is the vocabulary of people who build
synthesiser sounds, measured against the controls they actually turned.

Two more, cited by [2] and worth knowing: **Wallmark, Frank & Nghiem (2019)**, *Creating novel
tones from adjectives*, Psychomusicology 29(4) — the same reverse paradigm, 64 musicians, 20
orchestration adjectives on a 2-D FM interface. And **Reymore & Huron's 20-dimensional
orchestral model**, quoted in [2] p. 2, which is useful here because it assigns words this
lexicon uses to dimensions: *nasal/reedy* and *hollow* are **mass** words, *raspy/grainy* is
**texture**, *brassy/metallic* and *shrill/harsh/noisy* are luminance+texture, and *airy/breathy*,
*woody*, *open* and *focused/compact* belong to none of the three.

---

## What [2] establishes — the semantic space of SYNTHESISED sounds

Five factors, not three (parallel analysis, 74.36 % of variance, Oblimin rotation). Loadings from
Table 1; bold in the paper is ≥ .70.

| | **F1 Sharpness** | **F2 Mass** | **F3 Clarity** | **F4 Percussiveness** | **F5 Rawness** |
|---|---|---|---|---|---|
| defining | sharp **.82**, metallic **.75**, bright **.73**, harsh **.72** | big **.87**, thick **.84**, deep **.70**, thin **−.70** | clean **.90**, clear **.78** | plucky **.99**, percussive **.78** | raw **.78** |
| also | aggressive .57, noisy .52, hard .49, complex .48, **gritty .48**, **rough .42**, rich .32 · dull −.69, mellow −.67, woody −.63, **warm −.60**, dark −.58, smooth −.49 | **rich .69**, dark .51, **warm .42**, complex .36, gritty .26 | sweet .43, smooth .40 · noisy −.40, complex −.35, gritty −.32 | — | aggressive .33, rough .29 · sweet −.56 |

Three things in that table matter more than the rest, and all three contradict the intuition the
lexicon was written from:

1. **F1 is luminance and texture fused.** *bright* and *harsh*, *sharp* and *metallic* load on
   ONE factor. The paper's explanation is mechanical, not cultural: in FM „the introduction of
   brightness (in the form of high frequency energy) is closely linked to the introduction of
   inharmonicity through phase modulation", so you cannot add highs without adding inharmonicity.
   Measured on the designers' hands: *„the same controls were used when participants were asked
   to modulate the perceived brightness as when asked to decrease the perceived roughness."*
2. **`rich` is a MASS word (.69), not a brightness word (.32).** It sits with *big*, *thick*,
   *deep* and against *thin* — and `dark` sits on the same factor at .51. Rich and dark are
   allies here, not opposites.
3. **`warm` is the anti-sharp pole plus mass** (−.60 on F1, +.42 on F2). Not a filter setting: a
   position on two axes at once.

**And what F2 is NOT made of.** Mass „did not show significant correlations with any of the
principal components of acoustic variation", and **F0 had no influence on any of the five
factors.** So for synthesised sounds there is no centroid, no inharmonicity figure and no
fundamental frequency that predicts *rich / thick / big / deep*. Any rule of the form „mass = low
f0 + inharmonicity" is an acoustic-instrument finding and does not survive the move to a synth.

## What [2] establishes — how the words are MADE

This is the part the first version of this file did not have. The paper divides the synthesiser
into four control groups and reports which ones each factor moves (§4.2):

| control group | what it is | on an FM operator |
|---|---|---|
| 1 | **amplitude temporal evolution** | carrier ADSR |
| 2 | **spacing between sideband frequencies** | modulator tuning ratio |
| 3 | **sideband energy distribution** | modulator volume / index |
| 4 | **sideband energy temporal evolution** | modulator ADSR |

- **Sharpness up** = faster amplitude envelope (↓ carrier attack, ↓ release) · **wider sideband
  spacing** (↑ modulator tuning) · **more energy in the sidebands** (↑ modulator volume) ·
  **shorter sideband-energy envelope** (↓ modulator attack).
- **Mass up** = slower amplitude envelope with more sustain (↑ carrier decay/sustain/release) ·
  **narrower sideband spacing** (↓ modulator tuning) · **no change to sideband energy
  distribution** · **slower sideband-energy envelopes with more sustain** (↑ modulator
  attack/decay/sustain/release).
- **Percussiveness up** = shorter envelopes plus more energy in sidebands.

**Read that against this platform's invariant and the answer falls out.** Group 1 — the amplitude
envelope — belongs to the synth here and always will (`LCO_CONCEPT.md` §4). Groups **2, 3 and 4
belong to the oscillator**: partial spacing, partial energy, and how partial energy moves over
the note. Those three are generation, not filtering, and they are enough to reach sharpness,
mass and percussiveness without touching the player's envelope. Group 4 in particular — a
spectral envelope with no amplitude envelope — is what movement-by-default has been asking for
all along, and the mechanism already shipped: a short `T` in `min(knote / T, 1)` makes a
transient out of a morph without an envelope (commit `269e1b85`).

---

## This lexicon, read against that

Checked against `backend/dco_lexicon.json` (`lexicon_version` 12, 28 instruments, 51 adjectives,
17 motions) on 2026-07-29.

### The words are not made of what words in this position have to be made of

Counted by classifying each `code` block's opcodes:

| what the word's code actually is | how many |
|---|---|
| **a whole instrument body** — a generator reading `kfreq` (`brittle`, `clangorous`, `distorted`, `fat`, `glassy`, `hollow`, `metallic`, `nasal`, `old`, `reedy`, `thick`, `thin`, `warm`) | **13** |
| **the synth's FILTER** — `tone`/`atone` on the finished signal | **25** |
| **the synth's filter AND drive** | **10** |
| **the synth's DRIVE** — a `tanh` waveshaper (`dirty`, `edgy`, `raspy`) | **3** |
| partial spacing, partial energy, or the movement of partial energy (groups 2–4 above) | **0** |

BJ's ruling on the two he looked at, 2026-07-29: *„das gehört in den analog_osc"* (on `warm`
carrying a whole `vco2` body) and *„‚rich' und ‚sparse' — ergeben so keinen Sinn. Es gibt Filter
und drive im Synth"*. The count says that is the whole layer, not two entries: 38 of 51 spend the
player's own filter and drive, 13 replace the sound instead of shaping it, and none reaches the
three control groups that actually carry the semantics.

This is `LCO_CONCEPT.md` §8's own diagnosis — „`_ADJ_MAP` applies `gritty`/`dirty`/`airy` as
bounded DSP operations on the **mixed** signal — a waveshaper hung on the end" — which §9 item 5
marked ANSWERED on 2026-07-22. The **wiring** was fixed: no Python post-mix stage. The
**material** was not: the exemplars the author reads are still filters and waveshapers hung on
the end, so what the author writes is shaped by examples embodying the defect the architecture
change removed.

### Word by word, where the lexicon disagrees with the measured synth vocabulary

- **`rich`.** Lexicon: „many harmonics, deliberately the inverse of sparse", implemented as a
  2200 Hz highpass added back plus a `tanh`. Measured: `rich` is a **mass** word (.69) — big,
  thick, deep — and mass is made by *narrower* partial spacing and slower sideband-energy
  envelopes. Adding highs is the sharpness axis, i.e. the other factor. Its declared opposite
  `sparse` does not exist in the synth vocabulary at all; the measured opposite of thick is
  **`thin`** (−.70 on the same factor).
- **`warm`.** Lexicon: a whole `vco2` body with a `tanh` drive, glossed as an arguable
  convention. Measured: −.60 on sharpness, +.42 on mass. Warm is where you are when you are not
  sharp and you have body — a position on two axes, not a tilt.
- **`rough`, `gritty`, `harsh`, `raspy`, `dirty`.** These are not a separate „texture mechanism"
  the library is missing. `rough` (.42) and `gritty` (.48) load on the **same factor as `bright`
  and `sharp`**, and are made by the same controls: wider partial spacing and more partial
  energy, i.e. inharmonic sidebands. The library already owns that mechanism — the inharmonic
  pushes in `metallic`, `glassy`, `clangorous`, `brittle`, and the FM family's index and detune
  axes. What it does not own is the recognition that these are one axis, and it implements three
  of them (`raspy`, `dirty`, `edgy`) as a `tanh` waveshaper, which is the synth's drive.
- **`clean` / `clear`.** These are their own factor in synth sounds (.90 / .78), independent of
  brightness and mass, and correlated with spectral **flatness over time** (F3 × PC3 = −.44).
  The lexicon has `clean` („minimal added coloration; just limits harmonic extension") and treats
  it as an absence. It is a dimension.
- **`plucky` / `percussive` / `raw`.** Absent from the lexicon entirely, and two of them are the
  cleanest factor in the whole study (plucky .99). On this platform they are reachable **without
  an envelope**, through the sideband-energy envelope — group 4 — which is exactly what
  `269e1b85` made writable.
- **`analog`, `old`, `washed_out`.** Outside both studies by construction: they name a device, an
  era, a medium, not a percept. In Porcello's taxonomy ([1] p. 123) that is *association*, whose
  anchor is the machine — so they are checked against the machine (a VCO's real instability, a
  worn tape's real roll-off), never against a psychoacoustic correlate. Legitimate, differently
  grounded.
- **The bilingual trap.** `sharp` carries the German surface form `scharf`, and German *Schärfe*
  is a timbre word where English *sharpness* is a pitch word ([1] p. 141). Nothing to fix — the
  synth may define its own convention — but a report that an English prompt saying „sharp"
  produced the wrong thing now has a second candidate explanation.

---

## What this record does NOT license

The 39-entry revert of 2026-07-26 was caused by invention plus self-scoring, and a citation cures
only the first.

- **Not a licence to rewrite 51 glosses.** Every `why` line is read by the author and therefore
  shapes the sound; changing one is sound-shaping and needs BJ's order
  (`feedback_no_unauthorized_sound_features`). One word at a time, fixed attempt budget.
- **Not an ear and not a grader.** A published correlate can *falsify* a claim and can never
  certify one. BJ's ear decides, against a rendered A/B, as for every instrument.
- **Not a warrant for spectral centroid.** [2] measured 17 descriptors reduced to four principal
  components; the sharpness factor correlates with spectrotemporal *distribution and shape*
  (−.58) and with spectral *flatness* (+.49), and mass correlates with **none of them**. A single
  centroid number is not the meter for any of these words, which is precisely how the first
  version of this file went wrong.

---

## The plan that follows

**1 — The question that gates everything else, and it is BJ's.** What is a sound word allowed to
be, once filter and drive belong to the synth? [2] narrows it usefully: the three control groups
that carry the semantics and that the oscillator legitimately owns are **partial spacing, partial
energy, and the movement of partial energy over the note**. Three shapes are consistent with
that, and they are not exclusive — a word moves an existing instrument's own parameter and
carries no code; a word carries generation code no synth control can produce; a word stays a
filter and is labelled honestly as spending the user's filter. Nothing below is worth building
before this is answered.

**2 — No roughness word. That item is withdrawn.** The first version of this plan proposed
building one as amplitude modulation at 15–300 Hz. In this substrate roughness is not a modulator
on a parameter — it is inharmonic sideband density, the same gesture as brightness, and the
library already has it. Building a fourth `tanh` under a better citation would have been the same
category error with a footnote.

**3 — The vocabulary has holes that the count of 51 hides.** `plucky`, `percussive` and `raw` are
three of the five measured factors of synthesised timbre and none of them is in the lexicon,
while the lexicon carries `velvety`, `boxy` and `washed_out`. Whether the word list should be
re-derived from the synth corpus rather than extended one word at a time is BJ's call and is the
same question as item 1 one level up.

**4 — The index, through the gate that already exists.** Grouping the words by dimension is an
index change, and open item 6 of `docs/plans/HANDOVER_LCO.md` already fixed the procedure: a
frozen prompt corpus that cannot be derived from the changed index, measured before and after at
three points. If it is done, it is grouped by the **five** factors of [2] and not by the three of
[1]. Vorlage first (BJ, 2026-07-29).

**5 — A meter, if and when there is something to falsify.** MOSQITO 1.2.1 (PyPI, Apache-2.0, deps
numpy/scipy/pyuff; `LICENSE` read from `Eomys/MoSQITo`, no `NOTICE`) implements DIN 45692
sharpness and Daniel & Weber roughness, in `tools/` only, never in `backend/`. Ranked last on
purpose: [2] says these words are not single-descriptor quantities, so a sharpness number would
answer a question nobody in this project has yet asked.

**And one contribution to a decision already taken.** BJ decided on 2026-07-28 that the re-prompt
should hear, with CLAP provisional and probably unsuitable and an audio-LLM the real candidate
(`docs/DCO_REPROMPT_CONCEPT.md`, `60add3f8`). Neither paper offers an ear. What [2] offers is a
**control set**: 27 synth-native descriptors with measured factor loadings, and a published
dataset of sounds with ratings — something an ear's vocabulary can be checked against instead of
being taken on trust.

---

## What the first version of this file got wrong

Kept, because this project records its wrong turns rather than quietly correcting them, and
because every error below has the same single cause: correlates measured on **recordings of
acoustic instruments** were applied to a **synthesiser**.

| claimed on 2026-07-29, first version | what [2] measures |
|---|---|
| Three semantic substrates: luminance, texture, mass | **Five** for synthesised sounds, and the first is luminance and texture **fused** |
| brightness ↔ spectral centroid | F1 correlates with spectrotemporal distribution/shape (−.58) and flatness (+.49); no single centroid |
| roughness ↔ envelope fluctuation 15–300 Hz in a critical band; „the library has no roughness at all" | roughness rides on the **same factor and the same controls** as brightness — inharmonic sidebands, which the library has |
| mass ↑ with inharmonicity and centroid fluctuation, ↓ with f0 | mass correlates with **none** of the four acoustic components, and **F0 has no influence on any factor** |
| richness ↔ a LOW spectral centroid (from violin studies) | `rich` is a **mass** word (.69), and shares its factor with `dark` (.51) |
| „language does not carry the attack; do not spend a build on those words" | *plucky* .99 and *percussive* .78 are the **cleanest factor in the study** — the listening paradigm misses attack, the vocabulary does not |
| `rich` „measures inert" (centroid moved < 0.1 %) | measured with the one descriptor that does not track this word's factor at all; the verdict is void, not merely imprecise |
