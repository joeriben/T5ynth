# LCO — the source record for the sound words

`docs/LCO_CODE_PROVENANCE.md` records where the lexicon's instrument BODIES come from. Its other
half — the 51 adjectives in `backend/dco_lexicon.json`, `bright`, `hollow`, `rich`, `warm`, which
the author model reads exactly as it reads the bodies — had no source record at all. This file is
that record.

**Status: one source, entered. The evaluation is open and is BJ's** (BJ, 2026-07-29: „Saitis IST
auszuwerten"). This file states what the source says. It does not state what any word in this
instrument should do — that question belongs to `docs/LCO_CONCEPT.md` §3 and to BJ.

---

## [1] Saitis & Weinzierl (2019), *The Semantics of Timbre*

Saitis, Charalampos & Weinzierl, Stefan (2019): „The Semantics of Timbre". In: Siedenburg,
Saitis, McAdams, Popper & Fay (eds.), *Timbre: Acoustics, Perception, and Cognition*, Springer
Handbook of Auditory Research 69, Cham: Springer, pp. 119–149. DOI 10.1007/978-3-030-14832-4_5.

A review chapter, not an experiment. Its subject is the layer this lexicon works in: how
listeners put timbre into words at all.

**The premise.** There is no dedicated sensory vocabulary for hearing. Timbre is described
crossmodally (*bright*, *warm*, *rough*, *sharp*), onomatopoetically (*ringing*, *buzzing*), and
through abstract constructs (*rich*, *harsh*). Roughly sixty years of factor-analytic studies
across languages converge on **three** semantic substrates:

| substrate | typical words |
|---|---|
| **luminance** — brightness / sharpness | bright, sharp, brilliant, dull |
| **texture** — roughness / harshness | rough, harsh, raspy, smooth |
| **mass** — fullness / richness | full, rich, thick, thin |

**The acoustic correlates the chapter reports (§5.4.1), with their published models.** These are
measurement procedures with sources, in the sense of `feedback_no_selfmade_perceptual_meters` —
the reason to have them written down here at all.

- **brightness** ↔ spectral centroid; a shift in pitch is also heard as a shift in brightness.
  Model: Marozeau & de Cheveigné (2007), less pitch-dependent than sharpness.
- **sharpness** ↔ the centre of gravity of weighted specific loudness across critical bands.
  Model: von Bismarck (1974b), standardised as DIN 45692.
- **roughness** ↔ envelope fluctuation between roughly 15 and 300 Hz **within one critical band**
  — i.e. partials close enough in frequency to interact. A steep rise with a slow decay is heard
  as rougher than the reverse (Pressnitzer & McAdams 1999). Model: Daniel & Weber (1997).
  The chapter is explicit that roughness is not musical dissonance.
- **nasality** ↔ energy in the upper partials at the expense of the lower ones.
- **hollowness** ↔ a preponderance of odd harmonics; in the piano, carried by *phantom partials*
  (few = hollow, more = round, very many = metallic; Bensa et al. 2005).
- **mass / thickness / density** ↑ with inharmonicity and with variation of the spectral centroid
  over time, ↓ with f0. **richness** ↔ a low centroid plus strong low harmonics plus high
  spectral variation.
- **dynamic strength** ↔ spectral skewness (Weinzierl et al. 2018b).
- **volume** (Koechlin, Stumpf) ↔ inverse to the spectral centroid.

**Limits the chapter states about itself.** Semantic attributes do NOT capture the perceptual
dimension *sustained vs. impulsive* (Zacharakis et al. 2015); attack and time course are
linguistically under-served. The same word changes meaning with context — soft–loud correlates
with dull–sharp even for loudness-normalised stimuli. Cross-linguistic agreement is broad but not
complete: German *Schärfe* denotes timbre where English *sharpness* denotes pitch, and Greek
*oxýs* loads on texture rather than luminance.

**What the correlates were measured on.** Recordings of acoustic instruments and of the human
voice. The chapter makes no claim about synthesised sound, and this file makes none on its
behalf.

---

## What is still missing from this record

- **The evaluation itself.** Which of the three substrates the 51 words actually populate, and
  what — if anything — follows for the lexicon, is not written here and was not settled.
- **A second source.** One review chapter is the whole record; the bodies have many.
