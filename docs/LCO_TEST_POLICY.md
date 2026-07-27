# Test policy for the LRO library

**What the testing is for.** The author model never measures anything. It reads the
library index — entry descriptions, parameter notes, anchor words — and from those words
it writes Csound. The library's *prose is the interface*. So the only thing worth testing
is whether a word in that prose reliably reaches the sound it names: whether `hollow` at
`pick 0.33` is heard as hollow, whether the direction `ringing → dry` is the direction a
listener travels, whether a dominant impression exists that no word in the entry covers
and that the model therefore can never ask for.

A test earns its place by what its possible answers would *make us do*, and that is a
property of the design, not of the result. Before building, write down each answer the task
can produce and the action that follows from it. If every answer leads to the same action,
there is no question here. At least one answer must change a word in
`backend/dco_lexicon.json` — a rewritten `note`, a replaced anchor word, a new anchor, a
narrowed range.

**A run in which every word holds is a full result, not a wasted one.** It says the anchors
reach the sounds they name, which is exactly what the author model depends on. Demanding a
diff as the price of admission would reward tasks built to find fault — the same
self-serving shape as a meter that scores its own instrument.

## The two failures this policy exists to prevent

Both happened on 2026-07-27, within an hour of each other, on the same parameter.

1. **Testing the physics instead of the connection.** The first task asked the listener to
   order four stimuli by ring length. The lengths are 0.53, 0.83, 1.31 and 2.05 s, known
   from the envelope to the millisecond. Ranking them by ear confirms an envelope
   follower. Nothing in the library would have changed either way.
2. **Testing the mechanism instead of the connection.** The replacement asked what changes
   between the two ends and revealed the opcode's filter structure. Better, but still
   framed around what `krefl` *is* rather than around what the author model is told it
   *does*.

The corrective is stated positively below and is checkable before a single stimulus is
rendered.

## The gate before building any test

Answer all four in writing. A "no" anywhere means the test is not built.

1. **Which word is on trial?** Name the exact string in the lexicon — an anchor gloss, a
   direction in a `note`, an entry's `why`. Not a parameter; a word.
2. **Could `lco_measure` settle it?** If yes, measure it and stop. Duration, level,
   centroid, partial amplitudes, beat rate are not ear questions.
3. **What follows from each possible answer?** Write them all down before listening — the
   confirming one included. "The words hold, nothing changes" is a legitimate branch. The
   test fails this gate only when *every* branch leads to the same action.
4. **What is already known?** The opcode source in `csound/csound` (`Opcodes/*.c`) and the
   published listening tests for this model class — see
   `feedback_hypothesis_policy_empirical_lookup`. Where a perceptual tolerance exists it
   decides whether a distinction is even expected to be audible, and a set built inside
   that tolerance is a test that cannot be passed.

## The default task: word–sound matching

The library's anchors already are the hypothesis. `plucked_wire.refl` declares *ringing*
(0.70), *plucked* (0.78), *dry* (0.85); `pick` declares *wiry* (0.25), *hollow* (0.33),
*round* (0.35). So:

- Render one stimulus per anchor, at the anchor's own value. Not an even sweep — the model
  reads anchors, so anchors are what must hold.
- Present them unlabelled and scrambled, with the entry's own anchor words listed beside
  them, and have the listener assign word to sound.
- **The right answer is the entry's assignment.** A mismatch is the finding: that word does
  not reach that sound, and the entry gets the listener's word instead.
- Same pitch, same length, same loudness across the set, so nothing but the named
  distinction can carry the assignment. Where the peak differs, say so on the page.

Two anchors is a forced choice and settles little; three or four is a real assignment. With
three words there are six assignments, with four there are twenty-four — so one clean run
already means something, and a second run in a different scramble reads back the listener's
own consistency, which is the only reliability a single-listener test can produce.

## The second task: the missing word

Free text, after the matching and never before it: *what changes that none of these words
covers?* This is the only question here without a right answer, and it is the one that
catches the case the anchors cannot — an impression that dominates and has no name in the
entry, so the author model can never reach it. `refl` is the live example: it is described
purely as ring length, while the opcode's loop filter necessarily moves the colour too
(measured at 220 Hz: spectral centroid 266 Hz at the long end against 231 Hz at the short
one). If that darkening is what carries the impression, the entry is telling the model the
wrong thing about its own control.

Free text and not multiple choice, because the answer becomes library prose verbatim. The
listener's words are the deliverable, not a vote between mine.

## What is reserved, and what is banned

**Ordering** is reserved — and deliberately not yet implemented — for an attribute whose
order is *not* already in the signal: where a range stops being usable material, or whether
two axes stay perceptually separable (does `pick` still mean the same thing at a short ring
as at a long one). It gets built when such a case is actually in front of us, against the
gate above like anything else. Ordering by a measurable quantity is failure 1 above.

**ABX** only for the single question "is this step audible at all", and only where no
published tolerance already answers it. Twelve trials, ten correct is p < 0.02. It costs
about five minutes per pair, so it is never the default.

**Banned outright:** rating scales and any question of the form "how X is this" — there is
nothing to be wrong about, and the result cannot be scored or turned into a diff. Also
banned: asking the listener to confirm a number, and asking anything whose answer I could
have obtained by rendering and measuring.

## Stimulus and page

- One `<audio>` player. A click plays, a click on another clip replaces the running one, a
  click on the yellow one stops. Nothing is stopped by hand. The pattern comes from
  `tools/sem_axes_research/listening_final.html` and is not to be reinvented.
- Anchor values as declared, one pitch per page, identical duration.
- The entry's claim, the opcode source and the measured numbers are revealed **after** the
  answer is given, never before, so the wording is not led.
- The page states what a single listener can deliver — audibility, direction,
  self-consistency — and that it says nothing about listeners in general.

## Budget and scope

One instrument at a time. Three hours per instrument is the agreed ceiling (BJ,
2026-07-27). No quantity targets and nothing left optimising unattended
(`feedback_quality_bar_omit_mediocre`, CLAUDE.md "Instrument Authoring" rule 5). When the
budget is spent, the words that did not survive are removed rather than defended — an
anchor with no reliable connection is worse than no anchor, because the author model
routes on it.

## Related

- `docs/LCO_CONCEPT.md` §4 — the platform invariants the entries must hold to.
- `feedback_hypothesis_policy_empirical_lookup` — the research duty before any test.
- `feedback_no_selfmade_perceptual_meters` — why a self-built number cannot stand in for
  the listener, and what the measuring tools may be quoted for.
- `tools/lco_listening_test.py` — the generator that implements this.
