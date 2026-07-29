# LCO — the source record for the sound words

`docs/LCO_CODE_PROVENANCE.md` records where the lexicon's instrument BODIES come from. Its other
half — the 51 adjectives in `backend/dco_lexicon.json`, `bright`, `hollow`, `rich`, `warm`, which
the author model reads exactly as it reads the bodies — had no source record at all. This file is
that record.

**Status.** One source, read in full and evaluated (BJ, 2026-07-29: „Saitis IST auszuwerten").
§1 is what the chapter says. §2 holds it against the word stock that exists. **§2 states no
conclusion about what any word in this instrument should DO** — that is `docs/LCO_CONCEPT.md` §3
and BJ's decision, and every observation below is a comparison of two texts, not a proposal.

---

## 1. [1] Saitis & Weinzierl (2019), *The Semantics of Timbre*

Saitis, Charalampos & Weinzierl, Stefan (2019): „The Semantics of Timbre". In: Siedenburg,
Saitis, McAdams, Popper & Fay (eds.), *Timbre: Acoustics, Perception, and Cognition*, Springer
Handbook of Auditory Research 69, Cham: Springer, pp. 119–149. DOI 10.1007/978-3-030-14832-4_5.

A review chapter, not an experiment. Its premise: humans have no dedicated sensory vocabulary for
hearing, so timbre is described crossmodally (*bright*, *warm*), onomatopoetically (*ringing*,
*buzzing*), or through abstract constructs (*rich*, *harsh*).

### 1.1 How people talk about sound (§5.2)

The half of the chapter that is about vocabulary rather than about numbers.

- **Wallmark (2014, 2018): three conceptual metaphors** behind the whole timbre lexicon —
  *instruments are voices* (nasal, howling, open), *sound is material* (bell-like, metallic,
  hollow, velvety), *noise is friction* (harsh, rough). The material metaphor splits into naming
  the source, referencing its physical qualities, blending source with connotation, and
  referencing unrelated objects (*velvety strings*).
- **Porcello (2004)**, on studio discourse: five strategies — vocal imitation, lexical
  onomatopoeia, pure metaphor, association, evaluation. Pure metaphors are „codified, especially
  among musicians and sound engineers".
- **Source-free vocabularies for sound as material**, all built on extra-auditory concepts and
  all explicitly independent of what produced the sound: Schaeffer's (1966) *sonorous objects*
  — mass (low/high, thick/thin in the pitch field) and, in relation to the intensity of the mass,
  timbre as dark/light, ample/narrow, rich/poor; Smalley's (1997) spectromorphology, where the
  pitch field becomes spectral space described as emptiness–plenitude and
  diffuseness–concentration; Koechlin's *volume* (how much space a sound occupies) and *density*
  (loud but small volume = dense; large volume, low intensity = transparent).
- **Wallmark's corpus of eleven orchestration treatises** reduces to three latent dimensions of
  how instruments get described: *material*, *sensory*, *activity*.

### 1.2 The three semantic substrates (§5.3, §5.4)

Roughly sixty years of factor analyses, across languages and stimulus types, converge on three:

| substrate | the pole words the chapter's summary studies use |
|---|---|
| **luminance** — brightness / sharpness | brilliant / sharp — deep (Zacharakis et al. 2014); gloomy/dark — clear/bright (Moravec & Štěpánek 2005) |
| **texture** — roughness / harshness | soft / rounded / **warm** — rough / harsh (Zacharakis); harsh/rough — delicate (Moravec & Štěpánek) |
| **mass** — fullness / richness | dense / **rich** / full / thick — light (Zacharakis); full/wide — narrow (Moravec & Štěpánek) |

Case-specific dimensions recur outside these three: nasality, resonance/projection,
tonalness–noisiness, compact–scattered.

### 1.3 The acoustic correlates and their published models (§5.4.1)

- **brightness** ↔ spectral centroid; but pitch shifts are *also* heard as brightness changes.
  Model: **Marozeau & de Cheveigné (2007)**, weighted partial loudness in critical bands, less
  pitch-dependent than the plain centroid.
- **sharpness** ↔ midpoint of weighted specific loudness across critical bands. Model:
  **von Bismarck (1974b)**, standardised as **DIN 45692** (Fastl & Zwicker 2007).
- **roughness** ↔ envelope fluctuation **within one critical band**, from amplitude modulation
  in the region of **15–300 Hz** — physiologically, the cochlea cannot resolve a frequency pair
  whose interval is narrower than the critical band. An abrupt rise with a slow decay produces
  more roughness than the reverse (Pressnitzer & McAdams 1999). Model: **Daniel & Weber (1997)**.
  The chapter separates two things that are often conflated: roughness is **sensory** dissonance,
  not musical dissonance (Stumpf 1898 against Helmholtz); and **harshness**, although its ratings
  correlate with roughness, is acoustically associated mainly with **too much high-frequency
  energy**.
- **nasality** ↔ energy in upper partials at the expense of lower ones; in violins a strong
  response near 1.5 kHz. Helmholtz's odd-harmonic hypothesis for nasality „remains unexplored".
- **hollowness** ↔ predominance of odd harmonics (clarinet) versus balanced spectra heard as
  full — but von Bismarck, who synthesised odd and even spectra precisely to test this, found no
  relation to his fullness factor. In piano sounds hollowness tracks *phantom partials*: few =
  hollow, more = round, very many = metallic and aggressive (Bensa et al. 2005).
- **mass / thickness / density** ↑ with inharmonicity and with **fluctuation of the spectral
  centroid over time**, ↓ with f0.
- **richness** — the chapter reports two findings side by side without resolving them. Kendall &
  Carterette (1993b) and Saitis et al. (2015): rich = a **low** centroid plus **high spectral
  variation over time**, or a strong 2nd/3rd/4th harmonic, or a predominant fundamental, in
  agreement with Helmholtz (stronger fundamental relative to upper partials = richer). Bensa et
  al. (2005): synthetic piano tones with the *fewest* high-frequency inharmonic partials were
  heard as **poor**, and increasing their number gave **richer** timbres.
- **dynamic strength** ↔ spectral skewness (Weinzierl et al. 2018b); left-skewed = strong.
- **volume** (Koechlin, Stumpf) ↔ inverse to the spectral centroid; energy concentrated low
  raises perceived volume.
- **evolving**: Disley et al. (2006) found a fourth dimension defined solely by this word, which
  listeners reported difficulty understanding; the moderate loading of *rich* on the same
  component may indicate a **spectral flux** dimension.

### 1.4 What the chapter says about its own limits

- **Semantic attributes do not capture sustained versus impulsive** — the perceptual dimension
  that separates struck from bowed — at all (Zacharakis et al. 2015). Attack and time course are
  linguistically under-served.
- Words divide by which part of the note they describe: *bright* and *rich* refer mostly to the
  **sustained** part, *soft* to **transients** (Brent 2010; Bell 2015). For percussive timbres,
  brightness and sharpness/hardness are **two independent dimensions**, and sharp/hard relates to
  **attack time** rather than to spectrum.
- The same word changes meaning with context: even with loudness- and pitch-normalised stimuli,
  ratings on soft–loud correlated with dull–sharp, and low–high with dark–bright.
- **The psychoacoustic models were built on wideband noise spectra** and „may not be applicable
  for more natural and tonal sounds" (Nykänen et al. 2009); Almeida et al. (2017) found the
  sharpness model insufficiently predicted brightness scaling for tonal sounds. The one place the
  chapter reports a model transferring well outside instrument notes is Grill (2012), where
  bright–dull ratings of **electroacoustic textures** correlated strongly with the sharpness model.
- Language: German *Schärfe* denotes timbre where English *sharpness* denotes pitch (Kendall &
  Carterette 1993a, p. 456); Greek *oxýs* loads on texture rather than luminance, *pakhús*
  („thick") on luminance rather than mass.
- **What all of it was measured on:** recorded or synthesised notes of acoustic instruments, the
  voice, and — in a few studies — electroacoustic material. The chapter makes no claim about
  synthesiser sound, and this file makes none on its behalf.

---

## 2. The evaluation — the source held against the 51 words that exist

Six observations. Each is a comparison between what the chapter reports and what
`backend/dco_lexicon.json` contains. None of them says what a word should do.

### 2.1 The word stock is one substrate wide

Sorting the 51 keys by the pole words the chapter's summary studies use: the great majority are
**luminance** words (`bright`, `dark`, `dull`, `sharp`, `piercing`, `crisp`, `smooth`, `mellow`,
`gentle`, `round`, `clean`, `flat`, `muddy`, `boxy`, `cold`, `icy`, `airy`, `breathy`,
`shimmering`, `brassy`, `edgy`, `aggressive`, `harsh`, `washed_out`, `old` …). **Mass** has a
handful (`full`, `thick`, `fat`, `thin`, `deep`, `rich`, `sparse`). **Texture** — the substrate
with the physiological mechanism and the published model — has almost nothing; see §2.3.

Two glosses place a word on a different substrate than the chapter's factor solutions do:
`warm` („dark + 2nd-harmonic glow"), where Zacharakis et al. put *warm* on the **texture** pole
beside soft and rounded; and `rich` („many harmonics, deliberately the inverse of sparse"), where
the same study puts *rich* on **mass** and the correlates in §1.3 point to a low centroid with
high spectral variation, not to a count of harmonics. The lexicon's own note on `warm` already
says „a convention, stated, arguable" — this is the source it can be argued against.

### 2.2 One in eight of the words names a material, and the chapter warns about exactly that class

`metallic`, `woody`/`wooden`, `glassy`, `brittle`, `clangorous`, `brassy`, `velvety`, `icy` are
Wallmark's *sound is material* metaphors. Disley et al. (2006) is the specific caution: listeners
told the experimenters they used **metallic** and **wooden** to describe the recognised material
of the **instrument** rather than a quality of the sound, which is why those scales loaded on a
component of their own instead of with bright/harsh and warm/rich. For a project whose stated
position is material rather than imitation (`project_lco_material_not_imitation`), that is the
finding to have on record: these words carry source identification with them.

### 2.3 Texture is the substrate the lexicon does not have, and the near-misses are a different thing

There is no `rough` and no `rauh`, as key or as surface form. `gritty` exists only as a surface
form of `dirty`. What sits nearest — `dirty`, `distorted`, `raspy`, `growling`, `buzzy`, `harsh`
— the chapter separates from roughness on acoustic grounds: harshness is mainly too much
high-frequency energy, while roughness is 15–300 Hz envelope fluctuation *inside a critical band*,
i.e. partials packed closely enough that the cochlea cannot resolve them. Grill (2012) labels the
effect of sudden loudness changes across broad frequency ranges „coarse and raspy", which is the
one place the chapter connects a raspy-type word to a mechanism.

### 2.4 Four words name a quality the literature locates in the attack

`punchy` („a percussive-reading harmonic push"), `edgy` („a percussive-reading edge"), `crisp`,
and `sharp` (as hardness) sit where Brent (2010) and Bell (2015) put attack time, not spectrum —
and where the chapter states outright that semantic attributes fail to capture sustained versus
impulsive at all. In this instrument the synth owns the amplitude envelope and the oscillator is
a spectrum source (`docs/LCO_CONCEPT.md` §4). Where that leaves these four is a question for the
architecture, not something this file answers.

### 2.5 The German surface forms assume an equivalence the chapter documents failing

Every key carries German inflections alongside the English. The chapter names one hard case:
German *Schärfe* is a timbre word, English *sharpness* a pitch word — and the lexicon's `sharp`
key holds `scharf` and its inflections as the same thing. Two more pairings merge scales the
literature keeps apart: `warm` also carries `weich` (soft), and `dark` also carries `dumpf`
(muffled, nearer to *dull*) while `dull` is a separate key — in von Bismarck's factor solution
dull–sharp and dark–bright were **different factors**, with dull–sharp alone explaining almost
half the variance.

### 2.6 The study whose material most resembles this instrument is Grill (2012)

Everything else in the chapter is instrument notes. Grill's stimuli are electroacoustic
**textures**, and his semantic space adds, to the familiar bright–dull / smooth–coarse /
soft–raspy / tonal–noisy, a set of dimensions about how the sound is **organised over time**:
ordered–chaotic, coherent–erratic, homogeneous–heterogeneous, uniform–differentiated. The lexicon
has no word on that axis at all. Given that movement by default is a platform fundamental
(`project_lco_movement_by_default`), and that the only „evolving" scale reported elsewhere in the
chapter (Disley et al. 2006) confused its listeners, this is the part of the literature that
speaks most directly to what this instrument makes.

---

## 3. What the record now holds for measurement

Named procedures with sources, in the sense of `feedback_no_selfmade_perceptual_meters`:
**sharpness** — von Bismarck (1974b) / DIN 45692; **roughness** — Daniel & Weber (1997);
**brightness** — Marozeau & de Cheveigné (2007); **dynamic strength** — spectral skewness
(Weinzierl et al. 2018b). Implementations of the first two exist in MOSQITO (Apache-2.0).

**With the caveat that comes from the same chapter and must travel with them:** these models were
designed on wideband noise spectra, and the chapter twice records them under-performing on tonal
material. Adopting one here would mean adopting a model outside the class it was fitted on —
which is the shape of the error this file's own history is a record of.

---

## 4. Open

- What follows from §2, if anything, is BJ's. Nothing in this file is a proposal.
- The record still has one source where the instrument bodies have many.
