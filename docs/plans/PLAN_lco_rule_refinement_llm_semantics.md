# PLAN — what the third study refines in this project's rules

**Status: APPLIED 2026-07-30, on BJ's instruction to execute the plans.** The four wordings below went
in as written, one commit each; the diagnostic in Proposal 5 is still deliberately unbuilt.

| proposal | commit | where |
|---|---|---|
| 1 — a flat measurement is not an absent effect | `25ed8449` | `docs/LCO_CONCEPT.md` §7, new item 10 |
| 2 — N runs of one judgement are one judgement | `a698c5ac` | `CLAUDE.md`, Instrument Authoring rule 3 |
| 3 — the frozen corpus gets an external reason | `77b98cbd` | `CLAUDE.md`, §Migration parity clause |
| 4 — the boundary coincidence, as a note | `628853aa` | `docs/LCO_CONCEPT.md` §4 |

Written 2026-07-30 on his instruction. What the file said when it was only a proposal is kept below,
because the evidence for each wording is the part worth re-reading, not the wording.

## The one framing that decides what this source may touch

Source **[3]** does not measure sound. It measures **how reliably a language model that has never
heard anything reproduces human timbre semantics** — 20 verbally anchored scales, 8 orchestral
instruments, its ratings against 540 people's. So it cannot tell this project what a word should
sound like, and no proposal below touches a word, a body, or the author's prompt.

What it *can* refine is the layer this project has been wrong about repeatedly: **its own
epistemics** — self-scoring, agreement, and the reading of a null result. Three of the nine failure
modes already recorded in `docs/LCO_CONCEPT.md` §7 are „the meter, not the thing, produced the wrong
number". [3] supplies a named, published mechanism for a fourth one that this project committed and
has not yet written down.

## Research contributions this plan rests on

| contribution | what is taken |
|---|---|
| Siedenburg, Kai & Saitis, Charalampos (2023): „The language of sounds unheard: Exploring musical timbre semantics of large language models", arXiv:2304.07830v3 | all four proposals below: the profile-spread ↔ agreement identity (r = .98/.99), the `brassy/metallic` agreement inversion (0.72 vs ≈0), the training-cut-off argument, and the structure of which scales failed |
| Reymore, Lindsey & Huron, David (2020): „Using auditory imagery tasks to map the cognitive linguistic dimensions of musical instrument timbre", *Psychomusicology* 30(3), 124–144 | the 20 scales themselves, and the fact that they came from an **auditory-imagery** task — which is why they are answerable without hearing, and therefore why [3]'s comparison is possible at all |
| Reymore, Lindsey et al. (2023), *Music Perception* | the 540-participant human reference data [3] measures against |
| Horn, John L. (1965): „A rationale and test for the number of factors in factor analysis", *Psychometrika* 30, 179–185 | the parallel-analysis criterion both [3] and [2] use to fix the number of factors — named here because the „three factors on both sides, uncorrelated" result depends on it |

---

## Proposal 1 — a new recorded failure mode: a flat measurement read as an absent effect

**Where:** `docs/LCO_CONCEPT.md` §7, as item 10 (the list's own format: a failure that actually
happened here).

**What happened here.** A sound word was declared inert on the strength of a spectral-centroid
measurement that came back flat across its range. The verdict was wrong in two independent ways —
the meter was one number wide, and a flat average was read as „the word does nothing".

**The mechanism, with a source.** In [3] the standard deviation of a rated profile tracked the
raters' agreement with each other at **r = .99** (model) and **r = .98** (humans). A flat profile is
the *signature of raters disagreeing*, not of a stimulus with no properties. The same identity holds
for repeated authorings of one prompt: average N runs that each moved a different parameter and the
average moves nothing.

**Proposed wording:**

> 10. **A flat measurement read as an absent effect.** A sound word was declared inert because a
>     spectral-centroid sweep across its range came back flat — one number wide, and averaged over
>     runs. A flat average is what *disagreement between the runs* looks like; it is not evidence
>     that the thing measured has no properties. Siedenburg & Saitis (2023) measured this identity
>     directly: the standard deviation of a rated profile tracked inter-rater agreement at r = .98
>     (humans) and r = .99 (a language model), i.e. flat profiles were the ones the raters disagreed
>     about. **A null result about a word or a parameter is not reportable without the spread beside
>     it** — how far the individual runs differed from each other. If the spread is large the finding
>     is „the authoring is inconsistent here", which is a different problem with a different fix.

**What it would have caught:** the `rich`-is-inert verdict, before it was written down as a property
of the word.

---

## Proposal 2 — agreement is not correctness, and more samples do not rescue a self-score

**Where:** `CLAUDE.md`, §Instrument Authoring rule 3 (line 111), appended as one clause. Rule 3
already says own measurements never decide. This closes the loophole that they might if there were
enough of them.

**The evidence.** On `brassy, metallic` in [3], fifty independent conversations of the same model
agreed with each other at an inter-rater correlation of **0.72** — while **540 humans agreed at
≈ 0**. Confident, stable, reproducible, and about a word people do not share. Across the whole
space, the model's three-factor solution correlated with the human three-factor solution at
**r = −.07** (CI [−.17, .25]) — chance — while its *internal* agreement was slightly *higher* than
the humans' (median 0.26 vs 0.20).

**Proposed wording:**

> Repeating a self-assessment does not convert it into evidence. High agreement between runs is a
> property of the thing generating them: in Siedenburg & Saitis (2023) fifty independent
> conversations of one model agreed on `brassy, metallic` at r = 0.72 where 540 listeners agreed at
> ≈ 0, and the same model's overall timbre space matched the human one at chance. **N runs of my own
> judgement are one judgement, N times.**

**What it would have caught:** the 39 reverted entries, whose author „optimised them against a meter
it had built itself" and never heard one — and any future attempt to substitute an ensemble of model
judgements for BJ's ear.

---

## Proposal 3 — one clause of independent support for the frozen-corpus rule

**Where:** `CLAUDE.md` line 95, the „Parity = the PRIOR system's own capability corpus" clause. The
rule is right and stays as it is; this adds the reason it is right, from outside this project.

**The evidence.** [3]'s human dataset (Reymore et al. 2023) postdated its model's training data.
That is what made the comparison mean anything: the model could not have absorbed the answers.

**Proposed wording:**

> The reason this is a rule and not a preference: a corpus the system under test could have absorbed
> measures its memory, not its capability. Siedenburg & Saitis (2023) rest their whole comparison on
> the human data postdating the model's training material.

---

## Proposal 4 — a recorded coincidence: where the split falls is where a non-listening author is reliable

**Where:** `docs/LCO_CONCEPT.md` §4, as a note under the invariants — **not** as a new invariant, and
not as a derivation of the existing ones. The oscillator/synth split is BJ's and predates every
source in this record.

**The finding.** Of the eight scales [3]'s model failed to track, **six name things this instrument
does not give the oscillator at all**: loudness (*projecting/commanding/powerful*), the amplitude
envelope (*percussive/sharp beginning*, *sustained/even*), attenuation (*muted/veiled*), and aperture
or space (*open*, *focused/compact*). **Both** of the two scales that came out *significantly
inverted* — the model rating the opposite of what listeners rated — are in that group (*percussive*
r = −.35, *open* r = −.19). The twelve scales it did track are spectral-distribution words, which is
what an oscillator in this architecture writes. Mapped onto the lexicon's own 51 words: 10 of the 12
successes are lexicon words, 2 of the 6 nulls, **0 of the 2 inversions**.

**Why record it at all.** Not as vindication — as a **check with a list**. If a word ever migrates
into the oscillator, or a new word is proposed, this names the five word families where a
non-listening author is measured to be unreliable and sometimes exactly backwards.

**Proposed wording:**

> **Note, from Siedenburg & Saitis (2023) — a coincidence, not a derivation.** The invariants above
> are BJ's and predate this source. But where they put the boundary turns out to be where a
> non-listening language model is measurably reliable: of the eight timbre scales that study's model
> failed to reproduce, six name loudness, amplitude envelope, attenuation, aperture and space — all
> of which this architecture gives the synth, not the oscillator — and both of the scales it rated
> *backwards* are in that group. The twelve it did reproduce are spectral-distribution words, which
> is what an oscillator here writes. **Read as a list to be careful with:** projection/power,
> attack, steadiness, mutedness, openness. A word from one of those families, proposed for the
> oscillator, needs BJ's ear before anything else.

**The honest remainder, to be written into the note rather than left out:** two of the six nulls are
*not* explainable by the split — *pure/clear/clean* and *smooth/singing/sweet* are spectral words the
model still failed on, and they are the lexicon's only two words in that set. And all of these
numbers are one chatbot's, in February 2023.

---

## Proposal 5 — one diagnostic, specified and deliberately not built

**What:** `tools/lco_authoring_spread.py`. One prompt, **N independent authorings in fresh contexts**
(no shared conversation), each rendered, each measured with the project's existing calibrated meter
(`tools/lco_measure.py`), reported as the **spread per measured quantity** against a shuffled-label
baseline.

**Method and source:** [3] §2.2/§3.2 — independent conversations as independent raters, median
pairwise correlation as the agreement statistic, and a shuffled baseline to establish what zero looks
like (theirs came out at 0.0001 and 0.022).

**What it is for, and the only claim it may make.** It answers the one question Proposal 1 leaves
open and this project currently cannot: when a word or an axis measures flat, *was it the word or was
it the author*. It measures **stability only**. It can never say a sound is good — that is Proposal 2,
and it is BJ's ear.

**Not built, on purpose.** It would be the fourth measurement harness on this project and three of
the previous ones produced wrong numbers (`docs/LCO_CONCEPT.md` §7 items 7, 8, 9). It gets built when
there is a specific flat result worth explaining, and it gets calibrated first — a run of N *identical*
authorings must report a spread of zero before any non-zero spread is believed.

---

## What must not be taken from this source

- **No number from it becomes a value here.** Its ratings are of eight named orchestral instruments
  by one model in one month.
- **No language model enters the acceptance loop as a rater.** [3]'s own result is that its model's
  timbre space matched the human one at chance. Rule 3 stands unchanged: BJ's ear decides.
- **Nothing goes into the author's system prompt.** The task [3] measures — rate an instrument you
  have never heard, by name — is not the task the author performs, which is to write a spectrum from
  a description. `docs/LCO_TIMBRE_SEMANTICS.md` §4.11 records the mismatch in full; a prompt rule
  derived from the wrong task is exactly the overreach that got a report parked.
- **No word is added, removed or reglossed.** This source contains no sonic instruction.
