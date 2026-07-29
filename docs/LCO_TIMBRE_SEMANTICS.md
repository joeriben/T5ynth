# LCO — the source record for the sound words

`docs/LCO_CODE_PROVENANCE.md` records where the lexicon's instrument BODIES come from. Its other
half — the 51 adjectives in `backend/dco_lexicon.json`, `bright`, `hollow`, `rich`, `warm`, which
the author model reads exactly as it reads the bodies — had no source record at all. This file is
that record.

**Status.** Three sources, each read in full and evaluated (BJ, 2026-07-29/30). §§1–3 are what the
texts say. §4 holds them against the word stock that exists. **§4 states no conclusion about what
any word in this instrument should DO** — that is `docs/LCO_CONCEPT.md` §3 and BJ's decision, and
every observation there is a comparison of texts and code, not a proposal.

| | source | what it studied | scope here |
|---|---|---|---|
| **[1]** | Saitis & Weinzierl 2019 | review; ~60 years of semantic scaling, mostly acoustic instruments | the vocabulary and the published measurement models |
| **[2]** | Hayes & Saitis 2020 | 30 sound designers acting on a 3-operator FM synth | **FM entries only** (BJ's ruling, §2) |
| **[3]** | Siedenburg & Saitis 2023 | a language model rating instrument sounds it cannot hear | the bounded reliability of a non-listening author |

---

## 1. [1] Saitis & Weinzierl (2019), *The Semantics of Timbre*

Saitis, Charalampos & Weinzierl, Stefan (2019): „The Semantics of Timbre". In: Siedenburg,
Saitis, McAdams, Popper & Fay (eds.), *Timbre: Acoustics, Perception, and Cognition*, Springer
Handbook of Auditory Research 69, Cham: Springer, pp. 119–149. DOI 10.1007/978-3-030-14832-4_5.

A review chapter, not an experiment. Its premise: humans have no dedicated sensory vocabulary for
hearing, so timbre is described crossmodally (*bright*, *warm*), onomatopoetically (*ringing*,
*buzzing*), or through abstract constructs (*rich*, *harsh*).

### 1.1 How people talk about sound (§5.2)

- **Wallmark (2014, 2018): three conceptual metaphors** behind the whole timbre lexicon —
  *instruments are voices* (nasal, howling, open), *sound is material* (bell-like, metallic,
  hollow, velvety), *noise is friction* (harsh, rough). The material metaphor splits into naming
  the source, referencing its physical qualities, blending source with connotation, and
  referencing unrelated objects (*velvety strings*).
- **Porcello (2004)**, on studio discourse: five strategies — vocal imitation, lexical
  onomatopoeia, pure metaphor, association, evaluation. Pure metaphors are „codified, especially
  among musicians and sound engineers".
- **Source-free vocabularies for sound as material**, built on extra-auditory concepts and
  explicitly independent of what produced the sound: Schaeffer's (1966) *sonorous objects* — mass
  (low/high, thick/thin in the pitch field) and, in relation to the intensity of the mass, timbre
  as dark/light, ample/narrow, rich/poor; Smalley's (1997) spectromorphology, where the pitch
  field becomes spectral space described as emptiness–plenitude and diffuseness–concentration;
  Koechlin's *volume* (how much space a sound occupies) and *density* (loud but small volume =
  dense; large volume, low intensity = transparent).

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
  Roughness is **sensory** dissonance, not musical dissonance (Stumpf 1898 against Helmholtz);
  and **harshness**, although its ratings correlate with roughness, is acoustically associated
  mainly with **too much high-frequency energy**.
- **nasality** ↔ energy in upper partials at the expense of lower ones; in violins a strong
  response near 1.5 kHz. Helmholtz's odd-harmonic hypothesis for nasality „remains unexplored".
- **hollowness** ↔ predominance of odd harmonics (clarinet) versus balanced spectra heard as
  full — but von Bismarck, who synthesised odd and even spectra precisely to test this, found no
  relation to his fullness factor. In piano sounds hollowness tracks *phantom partials*: few =
  hollow, more = round, very many = metallic and aggressive (Bensa et al. 2005).
- **mass / thickness / density** ↑ with inharmonicity and with **fluctuation of the spectral
  centroid over time**, ↓ with f0.
- **richness** — two findings side by side, unresolved. Kendall & Carterette (1993b) and Saitis
  et al. (2015): rich = a **low** centroid plus **high spectral variation over time**, or a strong
  2nd/3rd/4th harmonic, or a predominant fundamental, in agreement with Helmholtz. Bensa et al.
  (2005): synthetic piano tones with the *fewest* high-frequency inharmonic partials were heard as
  **poor**, and increasing their number gave **richer** timbres.
- **dynamic strength** ↔ spectral skewness (Weinzierl et al. 2018b); left-skewed = strong.
- **volume** (Koechlin, Stumpf) ↔ inverse to the spectral centroid.
- **evolving**: Disley et al. (2006) found a fourth dimension defined solely by this word, which
  listeners reported difficulty understanding; the moderate loading of *rich* on the same
  component may indicate a **spectral flux** dimension.

### 1.4 What the chapter says about its own limits

- **Semantic attributes do not capture sustained versus impulsive** at all (Zacharakis et al.
  2015). Attack and time course are linguistically under-served.
- Words divide by which part of the note they describe: *bright* and *rich* refer mostly to the
  **sustained** part, *soft* to **transients** (Brent 2010; Bell 2015). For percussive timbres,
  brightness and sharpness/hardness are **two independent dimensions**, and sharp/hard relates to
  **attack time** rather than to spectrum. Attack time itself is defined spectrally: „the time
  needed by spectral components to stabilize into nearly periodic oscillations".
- The same word changes meaning with context: even with loudness- and pitch-normalised stimuli,
  ratings on soft–loud correlated with dull–sharp, and low–high with dark–bright.
- **The psychoacoustic models were built on wideband noise spectra** and „may not be applicable
  for more natural and tonal sounds" (Nykänen et al. 2009); Almeida et al. (2017) found the
  sharpness model insufficiently predicted brightness scaling for tonal sounds. The one place the
  chapter reports a model transferring well outside instrument notes is Grill (2012), where
  bright–dull ratings of **electroacoustic textures** correlated strongly with the sharpness model.
- Language: German *Schärfe* denotes timbre where English *sharpness* denotes pitch; Greek *oxýs*
  loads on texture rather than luminance, *pakhús* („thick") on luminance rather than mass.
- **What it was measured on:** recorded or synthesised notes of acoustic instruments, the voice,
  and in a few studies electroacoustic material. The chapter makes no claim about synthesiser
  sound, and this file makes none on its behalf.

---

## 2. [2] Hayes & Saitis (2020), *There's more to timbre than musical instruments*

Hayes, Ben & Saitis, Charalampos (2020): „There's more to timbre than musical instruments:
semantic dimensions of FM sounds". *Proceedings of the 2nd International Conference on Timbre
(Timbre 2020)*, 3–4 September 2020, Thessaloniki (online), Greece. Centre for Digital Music,
Queen Mary University of London. Study code: `github.com/ben-hayes/fm-synth-study`.

Not a review and **not a rating study**. Its subject is what experienced sound designers *do to a
synthesiser* when a word is the instruction — which makes it the one source here whose
experimental act is the same act the LRO performs.

**Scope, ruled by BJ 2026-07-29: this source can only concern FM instruments.** Everything it
measured was measured on one three-operator FM synth, and the authors themselves leave open whether
their own extra factors are „an inherent property of the synthesiser". So its findings apply to
this library's FM entries and to nothing else in it. Two things are not substrate-bound and are
therefore the exception: the **word list** (§2.1 — a forum corpus, not an FM artefact) and the
**method** (a word is an instruction, and what it changes is the datum).

### 2.1 What was actually done

Thirty participants (of forty; ten excluded on age or language), mean age 28.7, range 21–55, all
with formative years in an English-speaking country and prior synthesis experience from music
production or sound design. Online, WebAudio API plus the `lab.js` framework.

The instrument: **three operators**, operators 2 and 3 modulating the phase of operator 1 in
linear combination, each operator with its own **attack-decay-sustain-release** envelope.

The task: a reference sound is given, and the participant adjusts parameters until the synth
produces a new sound matching a **comparative** prompt — „brighter", „less thick". Nine trials per
participant: three prompts (**bright**, **rough**, **thick**) × three pitches (E2, A3, D5), the
positive or negative form chosen at random per trial. Afterwards they rate the magnitude of the
difference in terms of the prompt, the difference in terms of the other two prompts, and in terms
of a further 24 descriptors. Reference sounds were drawn at random from the sounds **previous
participants had made**, so the space explored is the participants' own rather than the authors'.

The descriptor set: mined from the synthesis forum *MuffWiggler* — words co-occurring in bigrams
with „sound", „sounding", „tone", „timbre", frequency-sorted, top 100 adjectives, pruned by two
raters, leaving **27**. The three prompts are the highest-frequency corpus words that also load
significantly on Zacharakis et al.'s English luminance–texture–mass factors.

### 2.2 Five factors, not three

Exploratory factor analysis, maximum likelihood, non-orthogonal **Oblimin** rotation, factor count
by parallel analysis (Horn 1965) at the 95th percentile. **Five factors, 74.36 % of variance:**

| | what the text names it by | defining words the text names |
|---|---|---|
| factor 1 | luminance **and** texture fused | *sharp*; *metallic*, *harsh* |
| factor 2 | mass | *big*, *thick*, negatively *thin* |
| factor 3 | clarity | *clean*, *clear* |
| factor 4 | „pluckiness" | *plucky*, *percussive* |
| factor 5 | rawness | *raw* |

The authors' reading: factor 1 is „an amalgamation of luminance and texture dimensions", factor 2
resembles Zacharakis's mass, and factors 3–5 are „attributes not entirely accounted for by LTM
factors". Under Oblimin the factors are allowed to correlate and do — most strongly factor 1 with
factors 3 (0.51), 4 (−0.42) and 5 (0.37), and factor 4 with 5 (−0.44).

Prompt-to-factor correlations (Fig. 3) confirm the first two: *bright* → f1 0.87\*\*\*,
*thick* → f2 0.92\*\*\*. *rough* loads almost as hard on f1 (0.85\*\*\*) as *bright* does, and on
f5 (0.83\*\*\*) and negatively on f3 (−0.76\*\*\*).

### 2.3 What the words made people change (Fig. 4, Spearman)

- ***bright*** and ***rough*** produce **very similar parameter patterns**: raise the modulators'
  gains (bright 0.41/0.52\*\*\*, rough 0.63/0.51\*\*\*) and their tuning ratios (bright
  0.56/0.54\*\*\*, rough 0.42/0.56\*\*\*) — i.e. more sideband energy and sidebands further out —
  and **shorten the attack**, on the carrier (bright −0.44\*\*\*, rough −0.39\*\*\*) and on the
  modulators (bright −0.27\*\*/−0.26\*, rough −0.34\*\*\* on operator 3).
- The authors attribute that similarity to **the synthesiser, not to the words**: „it is
  challenging to increase the energy in high frequency components (by increasing the modulator
  tuning or gain) without also increasing inharmonicity."
- ***thick*** is a different kind of word entirely: its strongest correlations are the three
  **sustain** levels, strongest on the carrier (0.50\*\*\*), with the modulator tuning ratio going
  the *other* way (−0.28\*\*). It is about what stands, not about what happens at the front.

### 2.4 Its own limits, as stated

Each stimulus was rated by only a **single** participant; the ratings are **comparative** rather
than classical semantic ratings, so „it will be necessary to confirm its efficacy and the
structure of the resulting semantic space with a classical semantic rating design" before
comparing these factors to earlier ones. Whether the non-independence of factors 3–5 from 1–2 is
„an inherent property of the synthesiser or the interpretation of the descriptors themselves is
left to future analysis."

---

## 3. [3] Siedenburg & Saitis (2023), *The language of sounds unheard*

Siedenburg, Kai & Saitis, Charalampos (2023): „The language of sounds unheard: Exploring musical
timbre semantics of large language models". arXiv:2304.07830v3, 5 May 2023 (equal contribution;
Dept. of Medical Physics and Acoustics, University of Oldenburg / Centre for Digital Music, QMUL).

The subject is a language model assigning sound semantics to sounds it cannot hear — which is what
the LRO's author does every time it writes an orchestra. This is the source that says where that is
reliable and where it is not.

### 3.1 What was actually done

**The human side** is Reymore et al. (2023): 540 online participants rated notes from **eight**
Western orchestral instruments across three registers (low, medium, high) on **20 verbally
anchored 5-point scales**, each scale named by up to three words — 1) *deep, thick, heavy*;
2) *smooth, singing, sweet*; 3) *projecting, commanding, powerful*; 4) *nasal, buzzy, pinched*;
5) *shrill, harsh, noisy*; 6) *percussive (sharp beginning)*; 7) *pure, clear, clean*;
8) *brassy, metallic*; 9) *raspy, grainy, gravelly*; 10) *ringing, long decay*;
11) *sparkling, brilliant, bright*; 12) *airy, breathy*; 13) *resonant, vibrant*; 14) *hollow*;
15) *woody*; 16) *muted, veiled*; 17) *sustained, even*; 18) *open*; 19) *focused, compact*;
20) *watery, fluid*. The scales come from interviews and rating tasks about **imagined** typical
instrument sounds (Reymore & Huron 2020) — auditory imagery, not a listening task, which is why
they suit a non-listening rater at all.

**The machine side:** the chatbot was asked to rate how well each description applies to the sound
of a named instrument in a low, mid and high register note, 1–5, answering as a matrix. **Fifty
ratings per instrument triplet, each collected in a separate conversation** so that each
conversation counts as one independent „rater" and no rating can be informed by an earlier one
(four raters discarded for missing values). Probed early and mid February 2023 through the public
web interface.

Analysis: Pearson correlations of machine against human averages; inter-rater correlations (IRC)
within each group; exploratory factor analysis (principal axis factoring, oblimin, factor count by
Horn's parallel analysis) to compare the two latent spaces.

### 3.2 Three results

**(a) Agreement is selective, and strongest exactly where perception is most salient.**
Twelve of the twenty scales gave positive human–machine correlations with confidence intervals not
overlapping zero: *deep, nasal, shrill, brassy, raspy, ringing, sparkling, airy, resonant, hollow,
woody, watery*. The two strongest are *deep, thick, heavy* at **r = .86** [.82, .89] and
*sparkling, brilliant, bright* at **r = .81** [.72, .87] — which the authors note are the two most
salient perceptual dimensions of musical sound, **pitch height** and **brightness** (the bright
scale also showed a marked offset toward higher ratings than humans gave). Two scales came out
**significantly negative** — the model had them backwards: *percussive (sharp beginning)*
**r = −.35** [−.50, −.18] and *open* r = −.19 [−.33, −.01]. Six more were indistinguishable from
zero: *smooth/singing/sweet* (−.04), *projecting/commanding/powerful* (−.07), *pure/clear/clean*
(.12), *muted/veiled* (−.11), *sustained/even* (.16), *focused/compact* (−.11).

**(b) Its inconsistency is of human magnitude — and its confidence is not evidence.**
Median inter-rater correlation across scales: **0.26 for the chatbot, 0.20 for humans**, the
difference only marginal (Wilcoxon z = −1.9, p = .057). A shuffled-stimulus bootstrap put the
chance baseline at 0.0001 / 0.022, so even the small IRCs are not noise. In both groups the
standard deviation of the average profile correlated almost perfectly with the median IRC
(human r = .99, machine r = .98): **a flat average profile means the raters disagreed, not that the
sounds are alike.** And one scale stands out — ***brassy, metallic*: IRC 0.72 for the chatbot, close
to zero for humans.** The model was reproducibly, confidently consistent about a scale on which
humans share no judgement at all.

**(c) Same dimensionality, unrelated configuration.**
Parallel analysis supported **three factors for both** (82 % of variance human, 70 % machine). But
correlations between individual human and machine factors ran only |r| = .003 to .38 with intervals
overlapping zero, and **the overall correlation between the two semantic spaces was
indistinguishable from chance, r = −.07** [−.17, .25]. Robust agreement on selected scales, no
correspondence in the shape of the space.

### 3.3 Its own limits, and its scope here

The model is one GPT-3-era chatbot probed in February 2023 through the public interface (default
sampling temperature 1 against the API's 0.7); the authors call their work „a snapshot into how one
specific version of CGPT construes sound semantics". **So the numbers belong to that model, not to
language models in general and not to this project's author.** What generalises is the method —
separate conversations as independent raters, comparison against a human dataset, and checking
consistency *and* configuration rather than either alone — and the shape of the finding.

One methodological strength worth keeping: the human data appeared in early 2023 while the model
was trained on text and code from before Q4 2021, so it **cannot have been memorised** — unlike
the older datasets in the study they compare themselves to. Two stated weaknesses: many ratings
were collected per response for efficiency where one per prompt would have matched the human
design better; and „an insurmountable disparity … humans listened to sounds whereas CGPT was
simply informed about the instrument name and pitch register."

Their closing move, on Schaeffer's four modes of listening (écouter, ouïr, entendre, comprendre):
one might add a fifth for what the chatbot does successfully, **„to pretend to understand"** — and
„the fundamental gap between a listening participant and a non-listening machine remains."

---

## 4. The evaluation — the sources held against the 51 words that exist

Comparisons between what the texts report and what `backend/dco_lexicon.json` contains. None of
them says what a word should do.

### 4.1 The word stock is one substrate wide

Sorting the 51 keys by the pole words of [1]'s summary studies: the great majority are
**luminance** words (`bright`, `dark`, `dull`, `sharp`, `piercing`, `crisp`, `smooth`, `mellow`,
`gentle`, `round`, `clean`, `flat`, `muddy`, `boxy`, `cold`, `icy`, `airy`, `breathy`,
`shimmering`, `brassy`, `edgy`, `aggressive`, `harsh`, `washed_out`, `old` …). **Mass** has a
handful (`full`, `thick`, `fat`, `thin`, `deep`, `rich`, `sparse`). **Texture** — the substrate
with the physiological mechanism and the published model — has almost nothing; see §4.3.

Two glosses place a word on a different substrate than [1]'s factor solutions do: `warm` („dark +
2nd-harmonic glow"), where Zacharakis et al. put *warm* on the **texture** pole beside soft and
rounded; and `rich` („many harmonics, deliberately the inverse of sparse"), where the same study
puts *rich* on **mass** and the correlates in §1.3 point to a low centroid with high spectral
variation rather than to a count of harmonics. The lexicon's own note on `warm` already says „a
convention, stated, arguable" — this is the source it can be argued against.

### 4.2 One in eight of the words names a material, and [1] warns about exactly that class

`metallic`, `woody`/`wooden`, `glassy`, `brittle`, `clangorous`, `brassy`, `velvety`, `icy` are
Wallmark's *sound is material* metaphors. Disley et al. (2006) is the specific caution: listeners
told the experimenters they used **metallic** and **wooden** to describe the recognised material
of the **instrument** rather than a quality of the sound, which is why those scales loaded on a
component of their own instead of with bright/harsh and warm/rich. For a project whose stated
position is material rather than imitation (`project_lco_material_not_imitation`), that is the
finding to have on record: these words carry source identification with them. [3] §4.10 adds the
sharpest instance of the same thing.

### 4.3 Texture is the substrate the lexicon does not have, and the near-misses are a different thing

There is no `rough` and no `rauh`, as key or as surface form. `gritty` exists only as a surface
form of `dirty`. What sits nearest — `dirty`, `distorted`, `raspy`, `growling`, `buzzy`, `harsh` —
[1] separates from roughness on acoustic grounds: harshness is mainly too much high-frequency
energy, while roughness is 15–300 Hz envelope fluctuation *inside a critical band*, i.e. partials
packed closely enough that the cochlea cannot resolve them. Grill (2012) labels the effect of
sudden loudness changes across broad frequency ranges „coarse and raspy", the one place [1]
connects a raspy-type word to a mechanism.

### 4.4 Four words name a quality both [1] and [2] locate in the attack

`punchy` („a percussive-reading harmonic push"), `edgy` („a percussive-reading edge"), `crisp` and
`sharp` (as hardness) sit where [1] puts attack time rather than spectrum, and where [2] measured
sound designers actually going: told „brighter" or „rougher", they **shortened the attack** —
including the attack of the *modulators*, which is a change in how fast the sidebands arrive
rather than in loudness. In this instrument the synth owns the amplitude envelope and the
oscillator is a spectrum source (`docs/LCO_CONCEPT.md` §4); [1]'s own definition of attack time is
spectral („the time needed by spectral components to stabilize"). Where that leaves these four is
a question for the architecture, not something this file answers. [3] adds a caution specific to a
non-listening author: *percussive* is the scale its model got **most wrong**, and inverted.

### 4.5 The German surface forms assume an equivalence [1] documents failing

Every key carries German inflections alongside the English. [1] names one hard case: German
*Schärfe* is a timbre word, English *sharpness* a pitch word — and the lexicon's `sharp` key holds
`scharf` and its inflections as the same thing. Two more pairings merge scales the literature keeps
apart: `warm` also carries `weich` (soft), and `dark` also carries `dumpf` (muffled, nearer to
*dull*) while `dull` is a separate key — in von Bismarck's factor solution dull–sharp and
dark–bright were **different factors**, dull–sharp alone explaining almost half the variance.

### 4.6 The study in [1] whose material most resembles this instrument is Grill (2012)

Everything else in [1] is instrument notes. Grill's stimuli are electroacoustic **textures**, and
his semantic space adds, to bright–dull / smooth–coarse / soft–raspy / tonal–noisy, a set of
dimensions about how the sound is **organised over time**: ordered–chaotic, coherent–erratic,
homogeneous–heterogeneous, uniform–differentiated. In the lexicon that axis lives in `motions`,
which carries an explicit `kind` field (`gesture`, `periodic`, `unsteady`, `wandering`, `none`) —
not in the adjectives.

### 4.7 The lexicon knows [2]'s first two factors and almost nothing of its last three

[2]'s 27 descriptors are the closest thing in either source to this instrument's own user
vocabulary: mined from a synthesis forum, not from orchestration treatises. Of the 27, **16 are
keys** in the lexicon (`thick`, `sharp`, `dark`, `mellow`, `dull`, `woody`, `thin`, `deep`, `rich`,
`aggressive`, `metallic`, `harsh`, `warm`, `smooth`, `clean`, `bright`), **one** is only a surface
form (`gritty` → `dirty`), and **ten are absent**: `plucky`, `big`, `clear`, `noisy`, `hard`,
`raw`, `sweet`, `complex`, `percussive`, `rough`.

Sorted by [2]'s own factors, the absences are not scattered. The lexicon covers factor 1
(luminance/texture fused) and factor 2 (mass) well. It is missing **both** words that define
factor 4, pluckiness (`plucky`, `percussive`); the single word that defines factor 5, rawness
(`raw`); and half of factor 3, clarity (`clean` is present, `clear` is not).

Read the two halves of that with different weight. **Which words synth players use** is a forum
corpus and holds generally; **that they organise into these five factors** was measured on one FM
synth and is bounded by §2's scope ruling.

### 4.8 Where [2]'s numbers reach: the FM entries only, and what they find there

Six of the 28 instruments use an FM opcode: `fm`, `fm_bell`, `fm_ep`, `metallic_fm`, and `foscili`
inside `clarinet` and `theremin`. All of them are **two**-operator — `foscili` is one carrier, one
modulator, one index — each of the three bell-family bodies doubled into a detuned pair for the
doublet. Their steerable parameters are `index`, `ring`, `detune`.

- `index` **is** the paper's modulator gain, and the mapping is directly usable: raise it for
  *bright* (0.41/0.52\*\*\*) and harder for *rough* (0.63/0.51\*\*\*).
- The paper's **other** strong axis, the modulator **tuning ratio** — the single largest correlate
  for *bright* (0.56/0.54\*\*\*) and joint-largest for *rough* (0.42/0.56\*\*\*) — is **hardcoded**
  in every one of these bodies: 2.0 in `fm`, 1.41 in `fm_bell`, 2.41 in `metallic_fm`. It has no
  parameter. What the designers reached for hardest has no knob here.
- **For a three-operator entry the topology matches exactly** (operators 2 and 3 modulating
  operator 1's phase in linear combination), so the paper's mapping transfers without
  reinterpretation: two gains, two tuning ratios, two modulator attacks. The library has no such
  entry.
- **What does not cross the platform boundary:** carrier attack, sustain and release are the
  player's envelope. *thick*'s entire signature is sustain level (carrier 0.50\*\*\*), so of the
  three words measured, *thick* lies largely outside the oscillator; only its negative
  tuning-ratio correlate (−0.28\*\*) is inside it. The modulators' attacks are inside it, because
  they set how fast the sidebands arrive rather than how loud the note gets.
- **The warning it gives before anything is built:** on FM, *bright* and *rough* are nearly the
  same move, and the authors say why. An FM entry can carry *bright* faithfully; it will not
  separate *rough* from it by these means. Per the authoring rules that is a property to declare,
  not something to correct with a fitted number — and it says where roughness would have to come
  from instead: partials close enough together to interact, which a modal bank or a detuned
  partial stack produces directly and FM sidebands do not.

One cross-check while reading these bodies: each carries its doublet as a fixed **+1.1 to +1.3 Hz**
offset, which is a pitch-independent beat rate and is deliberate. Any reformulation of the
„never hardcode a frequency" rule has to keep that legal — a beat rate belongs to the thing, not to
the note.

### 4.9 The lexicon's vocabulary sits where a non-listening model is reliable

Mapping [3]'s twenty scales onto the lexicon, split by whether its model agreed with humans:

| [3]'s result | scales | lexicon has a word for |
|---|---|---|
| agrees with humans (CI excludes zero) | 12 | **10** — `deep`/`thick`, `nasal`/`buzzy`, `harsh`, `brassy`/`metallic`, `raspy`, `bright`, `airy`/`breathy`, `resonant`/`vibrant`, `hollow`, `woody` |
| no correlation | 6 | 2 — `smooth`, `clean` |
| **significantly inverted** (*percussive*, *open*) | 2 | **0** |

The two agreeing scales the lexicon has no word for are *ringing, long decay* — a decay word, and
the host owns the note-off — and *watery, fluid*, a word class absent entirely.

That is the one place in this record where the existing lexicon comes out ahead rather than short:
its vocabulary lies overwhelmingly in the region where a language model's sound semantics tracks
human judgement, and it carries neither of the two words on which [3]'s model was measurably
backwards.

### 4.10 Confident agreement across samples is not evidence — and it is measured, not asserted

[3]'s *brassy, metallic* scale: inter-rater correlation **0.72** among the chatbot's fifty separate
conversations, close to **zero** among 540 humans. A non-listening model can be reproducibly,
confidently unanimous about a word on which people share no judgement whatsoever. Two consequences
for this project's own practice, both of which it already holds as rules and now has a source for:

- A self-scoring loop cannot be rescued by sampling it more times
  (`feedback_no_selfmade_perceptual_meters`, `feedback_chained_to_existing_synthesis`).
- And the scale it happened to be unanimous about is a **material** word — which §4.2 already
  flags as the class listeners use for the recognised instrument rather than for the sound. The
  most likely thing that unanimity is about is the instrument's name, not its timbre.

The companion finding cuts the other way and is just as operational: the standard deviation of an
average profile tracked the median inter-rater correlation at r = .98/.99, so **a flat result means
the raters disagreed, not that the stimuli were alike**. A null must not be read as „these words do
not differ".

### 4.11 [3] measures a task this instrument does not perform — which is why only part of it carries

Read the task, not the headline. [3]'s model was asked *how well a description fits a named
instrument in a named register* — an association between an instrument's **name** and a descriptor,
scored against a human average over the same imagined-instrument task. The LRO does the reverse and
it is not a rating at all: a word arrives, usually with **no instrument named** („a dark growl",
„rusty machinery", „like wet glass"), and the model must construct a spectrum. Whether the author's
association between „oboe" and „brassy" matches the average listener's is not a question this
instrument asks, and „does the model understand sound" is not answerable by correlating it with a
panel on instrument stereotypes. So the study's own conclusion — Schaeffer's fifth mode, „to pretend
to understand" — is about its task, not about writing an orchestra.

What survives that mismatch is the **structure of its failures**, and it lands somewhere useful. Of
the eight scales its model did not track, **six name things that in this instrument are not the
oscillator's at all**: *projecting, commanding, powerful* (loudness), *percussive (sharp beginning)*
and *sustained, even* (the amplitude envelope), *muted, veiled* (attenuation), *open* and *focused,
compact* (aperture and space). Both of the scales it got **inverted** are in that group. The twelve
it did track are predominantly spectral-distribution words — which is exactly what the oscillator
owns under `docs/LCO_CONCEPT.md` §4.

The division of labour in this instrument therefore puts the author's reliable half in the
oscillator and its unreliable half in the synth. That was not designed from this finding — the split
comes from BJ's platform invariants — but the measurement says the line falls in a good place, and
it names which words to distrust if one ever migrates across it: aperture, projection, steadiness.

Two honest remainders. The failure set also contains two scales that *are* spectral — *pure, clear,
clean* and *smooth, singing, sweet* — and the architecture does not explain those away; `smooth` and
`clean` are the lexicon's only two words in the failure set. And [3]'s numbers belong to one
GPT-3-era chatbot probed in February 2023 at temperature 1 (§3.3), so nothing here is a measurement
of this project's author. The re-prompt ear remains the open question it already was, with BJ's own
ruling on it (`project_lco_reprompt_ear_provisional`).

---

## 5. What the record now holds for measurement

Named procedures with sources, in the sense of `feedback_no_selfmade_perceptual_meters`:
**sharpness** — von Bismarck (1974b) / DIN 45692; **roughness** — Daniel & Weber (1997);
**brightness** — Marozeau & de Cheveigné (2007); **dynamic strength** — spectral skewness
(Weinzierl et al. 2018b). Implementations of the first two exist in MOSQITO (Apache-2.0).

**With the caveat that comes from [1] and must travel with them:** these models were designed on
wideband noise spectra, and [1] twice records them under-performing on tonal material. Adopting
one here would mean adopting a model outside the class it was fitted on.

---

## 6. Recorded for a prospective three-operator FM entry

Held on BJ's instruction, 2026-07-30. This is the pre-build record the authoring rules in
`CLAUDE.md` require — method and source in writing before the first orchestra line, nearest
existing entry named before the new one is written. **It is a record, not an authorisation to
build.**

**Method and source.** FM as Chowning, John M. (1973): „The Synthesis of Complex Audio Spectra by
Means of Frequency Modulation", *JAES* 21(7), 526–534 — the source [2] itself cites. Topology:
three operators, operators 2 and 3 modulating operator 1's phase **in linear combination**
([2] §2.1). Word-to-parameter directions: [2] §2.3, Spearman rank correlations over thirty
experienced sound designers acting on that exact topology.

**Csound realisation, verified against `csound -z1` on this build.** Available: `foscil`/`foscili`
(one carrier, one modulator), the `cross*` family (`crossfm`, `crossfmi`, `crosspm`, `crosspmi`,
`crossfmpm`, `crossfmpmi` — two mutually modulating oscillators), and the fixed model algorithms
(`fmbell`, `fmb3`, `fmmetal`, `fmpercfl`, `fmrhode`, `fmvoice`, `fmwurlie`). **There is no generic
N-operator opcode**, so two modulators summed into one carrier's phase has to be written out —
`phasor` on the carrier plus both modulator signals added into a `tablei` read. That it compiles
and sounds is unverified; nothing here may be claimed until it has been.

**What transfers, because it is spectral and therefore the oscillator's:**

| axis | *bright* | *rough* | *thick* |
|---|---|---|---|
| modulator gains (the two indices) | ↑ 0.41 / 0.52\*\*\* | ↑ 0.63 / 0.51\*\*\* | — |
| modulator tuning ratios | ↑ 0.56 / 0.54\*\*\* | ↑ 0.42 / 0.56\*\*\* | ↓ −0.28\*\* |
| modulator attacks (how fast the sidebands arrive) | ↓ −0.27\*\* / −0.26\* | ↓ −0.34\*\*\* (op 3) | — |

**What does not transfer, because it is the player's:** carrier attack, sustain and release.
Consequence: of the three words [2] measured, ***thick* is not realisable on this entry** beyond
its tuning-ratio component — its signature is the sustain levels, strongest the carrier's
(0.50\*\*\*).

**The property to declare rather than correct** (authoring rule 2): *bright* and *rough* are not
separable on FM, because high-frequency energy there cannot rise without inharmonicity rising with
it. An entry built this way carries *bright*; it does not distinguish *rough* from it. Roughness
needs a substrate that packs partials close enough to interact.

**The two declarations every entry owes:** it follows `kfreq` by construction (the carrier phasor
is driven by it). It must **STAND**, not decay — the host owns the note-off — which means the
index-decay that the existing `fm`/`fm_bell`/`metallic_fm` bodies use for their ring is not the
model for this one.

**Comparison target, to be rendered before any new orchestra line** (authoring rules 3 and 4): the
nearest existing entry is **`fm`** — plain two-operator, ratio 2, index 1.5. `nearest_existing.wav`
comes from it. The attempt budget is BJ's to fix and is not set here.

---

## 7. Open

What follows from §4, if anything, is BJ's. Nothing in this file is a proposal.
