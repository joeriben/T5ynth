# Where the oscillator's SOUND WORDS come from

The library has two halves. The **bodies** have a provenance record: `docs/LCO_CODE_PROVENANCE.md`
says where an instrument's physics was read, and `CLAUDE.md`'s Instrument Authoring rule 1
requires a named method AND a source before the first orchestra line.

The **words** have had none. `backend/dco_lexicon.json` carries 51 sound words, and each one's
`why` line is the only definition the author model ever sees of what that word means. Those
lines were written from intuition. One of them says so in as many words — `warm`: *„dark +
2nd-harmonic glow — a convention, stated, arguable"*.

This file is the other record. It does for the word layer what the provenance file does for the
code layer: it names a published source, states what that source establishes, and then reads
this lexicon against it — including where the lexicon is right, which is most of it.

---

## The source

> Charalampos Saitis & Stefan Weinzierl, **The Semantics of Timbre**. In: Siedenburg, Saitis,
> McAdams, Popper & Fay (eds), *Timbre: Acoustics, Perception, and Cognition*, Springer Handbook
> of Auditory Research 69, Springer 2019, pp. 119–149. DOI `10.1007/978-3-030-14832-4_5`.

A review, not an experiment: it collects roughly sixty years of semantic-differential studies,
free-verbalisation studies and adjective-dissimilarity scalings, and reports where they agree.
Page numbers below are that chapter's; where a finding is originally somebody else's, both are
named, because the chapter is the secondary source and a claim should be checkable at its origin.

Second-order sources worth having in view, cited there and relevant here: Zacharakis, Pastiadis &
Reiss (*Music Perception* 31/32, 2014/2015) for the three-dimensional interlanguage space;
Wallmark (*Psychology of Music*, 2018) for the corpus analysis of eleven orchestration treatises;
von Bismarck (*Acustica* 30, 1974a/b) for the first systematic semantic space and the sharpness
model; Bensa, Dubois, Kronland-Martinet & Ystad (2005) for phantom partials.

## What the source establishes

Humans have no sensory vocabulary of their own for hearing. Timbre is described in borrowed
terms — crossmodal (*bright*, *warm*), onomatopoeic (*buzzing*, *ringing*), or through abstract
constructs (*rich*, *harsh*) — and Wallmark (p. 126) groups the borrowing into three conceptual
metaphors: *instruments are voices*, *sound is material*, *noise is friction*.

Across methods and languages the descriptions collapse onto **three substrates** (pp. 135, 143),
each with published acoustic correlates:

| Substrate | The words that load on it | Acoustic correlate | Published model |
|---|---|---|---|
| **brightness / sharpness** (*luminance*) | bright, dull, sharp, dark, clear | spectral centroid; a shift in pitch is heard as a shift in brightness too | von Bismarck 1974b (weighted specific loudness in critical bands, now DIN 45692); better, and less pitch-dependent: Marozeau & de Cheveigné 2007 (p. 136) |
| **roughness / harshness** (*texture*) | rough, harsh, raspy, soft, rounded | envelope fluctuation in the **15–300 Hz** band *within a critical band* — not a spectral tilt (p. 137) | Daniel & Weber 1997 |
| **fullness / richness** (*mass*) | full, rich, thick, dense, thin | rises with inharmonicity and with **spectral-centroid fluctuation over the note**, falls with f0 (pp. 138–139) | — none; correlates only |

Smaller correlates the chapter states outright, each usable as an objective signature:

- **nasality** — energy reinforced in the upper partials *at the expense of the lower* ones
  (Garnier et al. 2007; Mores 2011, p. 136); in violins concentrated near 1.5 kHz (Fritz et al.
  2012). Kendall & Carterette's *nasal–rich* dimension is exactly this trade (p. 136).
- **hollowness** — predominance of **odd** harmonics (Helmholtz 1877; Lichte 1941, p. 138). Not
  uncontested: von Bismarck synthesised odd and even spectra deliberately to test thin–full and
  found no relation to his fullness factor.
- **hollow → round → metallic on one axis** — in piano tones the count of *phantom partials*
  (nonlinearly generated, off the harmonic comb) moves the percept monotonically: few = hollow,
  more = round, very many = metallic and aggressive (Bensa et al. 2005, p. 138). Three of this
  lexicon's words on one mechanism.
- **harshness** — associated mainly with **too much high-frequency energy**, and distinct from
  roughness even though ratings on the two correlate (pp. 137–138).
- **richness** — measured the opposite way round from the intuitive one: violin notes described
  as rich had a **low** spectral centroid, or stronger 2nd/3rd/4th harmonics, or a predominant
  fundamental (Saitis et al. 2015, p. 139), which agrees with Helmholtz: the stronger the
  fundamental relative to the upper partials, the richer the sound. Kendall & Carterette add
  the second half — rich combines a low centroid with **increased spectral variation over time**
  (p. 136).
- **dynamic strength** — spectral skewness; a left-skewed spectral shape reads as played hard
  (Weinzierl et al. 2018b, p. 137).
- **noisiness** — adding audible noise to a synthesis model is known to *increase* its perceived
  naturalness (Serra 1997, p. 138).
- **roughness has a shape, not only a depth** — for the same modulation depth, an envelope with
  an **abrupt rise and a slow decay is rougher** than one with a slow rise and an abrupt decay
  (Pressnitzer & McAdams 1999, p. 137).

And two limits the chapter names against itself, both of which land on this project:

- **Language does not carry the attack.** Semantic attributes are „generally unable to capture
  the salient perceptual dimension responsible for discriminating between sustained and impulsive
  sounds" (Zacharakis et al. 2015, p. 141). Where percussive timbre *has* been separated, it took
  two independent measures: brightness = centroid **during the attack**, sharp/hard = **attack
  time** itself (Brent 2010; Bell 2015, p. 136).
- **A word's dimension is not language-invariant, even where the space is.** German *Schärfe*
  refers to timbre where English *sharpness* pertains to pitch; Greek *oxýs* loads on texture
  where English *sharp* loads on luminance, and *pakhús* (thick) on luminance rather than mass
  (p. 141).

---

## This lexicon, read against it

All 51 words checked against `backend/dco_lexicon.json` (`lexicon_version` 12, 28 instruments,
51 adjectives, 17 motions) on 2026-07-29. Nothing below has been changed — this is a reading.

**Confirmed, correlate for correlate.** `bright` („more upper-harmonic energy"), `dark`,
`hollow` („odd-dominant = clarinet family"), `harsh` („extreme upper energy") and `deep` name
the same physical quantity the literature names. `harsh` is worth singling out because it is the
one usually got wrong: excess high-frequency energy really is harshness, and it is *not*
roughness.

**Right mechanism, narrower than the source.** `nasal` is „mid-harmonic formant-ish bump"; the
literature's nasality is a *trade* — upper partials reinforced **at the expense of** the lower
ones. `fat`/`thick` carry low-frequency weight and not the other two thirds of mass
(inharmonicity, centroid fluctuation over the note). `metallic`, `glassy`, `brittle` and
`clangorous` all push inharmonicity separately; Bensa's phantom-partial count would put
`hollow → round → metallic` on one graded axis instead of three unrelated tilts.

**Contradicted, and these are the two that matter.**

- **`rich` — „many harmonics, deliberately the inverse of sparse".** The literature reads
  richness the other way: a *low* centroid with strong low-order partials or a predominant
  fundamental, plus spectral variation over time. Under the lexicon's definition „rich" and
  „bright" push the same direction; under the literature's they push opposite ones. `rich` is
  among the most-reached-for words in any prompt, so this is not a marginal entry.
- **`warm` — „dark + 2nd-harmonic glow — a convention, stated, arguable".** The gloss asked the
  question; the literature answers it. Warm sits on **texture** (soft/rounded/warm against
  rough/harsh — Zacharakis) and on richness (warm/rich/mellow against metallic/cold/harsh —
  Fritz et al. 2012), i.e. it is not primarily a brightness word at all. The current mechanism
  is a −2 dB/oct tilt plus a 2nd-harmonic boost: a luminance move for a texture word.

**The hole: there is no roughness in this library.** Checked as a fact, not an impression —
neither `rough` nor `rauh`/`rau` exists as a key or as any word's surface form. What stands in
its place is spectral: `raspy` is „odd-harmonic emphasis reads as a reed-like rasp", and BJ's own
founding word `gritty` is a surface form of `dirty`, whose mechanism is a tanh waveshaper, i.e.
harmonic distortion. Distortion adds partials; roughness is amplitude fluctuation between
partials inside one critical band. **So one of the three substrates has no mechanism here**, and
the closest one is a proxy of a different physical kind.

The meter has the same edge. `lco_measure.beat()` measures amplitude beating over **0.5–25 Hz**
and its own docstring says correctly that no spectral meter can see it — but the roughness band
is 15–300 Hz, so the tool stops almost exactly where roughness starts to dominate. Half of that
meter already exists; it was built to measure a musette's beat, not a reed's rasp.

**No correlate to have — and that closes a question rather than opening one.** `punchy`, `crisp`
and `edgy` describe the attack, which the chapter says language does not carry stably (p. 141),
and which on this platform the body cannot own anyway: the host holds the note-off and the
synth owns the envelope (`docs/LCO_CONCEPT.md` §4, `docs/plans/HANDOVER_LCO.md` §5). The layer
that owns the attack and the layer that owns the vocabulary are the same layer, and it is not
the oscillator. Expect these words to stay weak, and do not spend a build on making them strong.

**Outside the literature by construction, and legitimately so.** `analog`, `old` and
`washed_out` are not sensory metaphors — they name a device, an era, a medium. In Porcello's
taxonomy (p. 123) that is *association*, a different class of description with a different
anchor: their reference is a machine, so they are checked against the machine (a VCO's actual
instability, a worn tape's actual roll-off), never against a psychoacoustic correlate.

**The bilingual trap, recorded because this lexicon is bilingual by design.** `sharp` carries the
German surface form `scharf`, and German *Schärfe* is a timbre word where English *sharpness* is
a pitch word (p. 141). The German prompt is the unambiguous one here; the English one is not.
Nothing to fix — the synth may define its own convention — but a report that an English prompt
saying „sharp" produced the wrong thing has a second candidate explanation now.

---

## What this record does NOT license

The 39-entry revert of 2026-07-26 was caused by invention plus self-scoring, and a literature
reference is a cure for the first only. So, explicitly:

- **It is not a licence to rewrite 51 glosses.** Every `why` line is read by the author model
  and therefore shapes the sound; changing one is sound-shaping and needs BJ's order
  (`feedback_no_unauthorized_sound_features`). One word at a time, with a fixed attempt budget
  (`CLAUDE.md`, Instrument Authoring rule 5).
- **It is not an ear and it is not a grader.** A descriptor with a published model can *falsify*
  a claim — „the word `raspy` moves no roughness at all" — and can never certify one. BJ's ear
  decides, against a rendered A/B on disk, as for every instrument.
- **It does not retire „compare against a real reference".** A correlate says what a word means;
  it does not say whether the sound is any good.

---

## The plan that follows

Ordered so that nothing later depends on a decision that has not been made yet. Phase 0 is this
file.

**1 — One word, the one that is missing: a roughness mechanism.** The unit of work is ONE
instrument-or-word, not a sweep of the vocabulary. Method and source written down first, as rule
1 requires: amplitude modulation in the 15–300 Hz band within a critical band (Helmholtz 1877 →
Fastl & Zwicker 2007 → Daniel & Weber 1997 → Vassilakis & Kendall 2010), with Pressnitzer &
McAdams' asymmetry — abrupt rise, slow decay — as the envelope shape rather than a symmetric LFO.
Comparison first, code second (rule 4): render `dirty` and `raspy`, the two nearest existing
words, before writing a line of the new one. Sound-shaping, so **BJ's order gates the start, and
his ear gates the end.**

**2 — The meter, tools-only, from a published implementation.** MOSQITO 1.2.1 (PyPI, Apache-2.0,
depends only on numpy/scipy/pyuff) implements DIN 45692 sharpness — von Bismarck's model,
standardised — and Daniel & Weber roughness. Using it rather than deriving one satisfies
`feedback_no_selfmade_perceptual_meters` at both ends: procedure and source. It goes in `tools/`
and never into `backend/`, so `pipe_inference.spec`, the PyInstaller bundle and the plugin are
untouched. Calibrated before it is believed, in this repo's own way — known-answer cases first
(an unmodulated tone must read ≈0 asper; a 1 kHz carrier fully modulated at 70 Hz must read
≈1 asper, which is the definition of the unit), then bodies. Purpose: a falsifier for word
claims, wired into the same place `lco_axis_probe` already asks its questions.

**3 — The two contradicted glosses, one at a time, to BJ.** `rich` first (it is reached for far
more often), `warm` second (its own gloss already asked the question). Each is a sound change:
nearest-existing rendered first, A/B on disk, his ear decides, drop it if it does not win.

**4 — The index, through the gate that already exists.** Grouping the 51 words by the three
substrates instead of listing them flat is an index change, and open item 6 of
`docs/plans/HANDOVER_LCO.md` already fixed the procedure for exactly this: a frozen prompt corpus
that cannot be derived from the changed index, measured before and after at three points — does
the author still open the right entries, does the result still `perform_check`, does the
objective signature still hold. No new decision is needed to run it, only the corpus.

**And one thing this contributes to a decision already taken.** BJ decided on 2026-07-28 that the
re-prompt should hear, with CLAP as the provisional ear and the reservation that it ranks against
a fixed vocabulary and is probably unsuitable — the audio-LLM being the real candidate
(`docs/DCO_REPROMPT_CONCEPT.md`, commit `60add3f8`). This chapter offers no ear and should not be
read as offering one. What it offers is a way to **check** whichever ear is used: when the ear
says „bright", the centroid either agrees or it does not; when it says „rough", there is now a
published model that can be asked the same question. A control set for the ear, not a substitute
for it.
