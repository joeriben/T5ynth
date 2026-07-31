# `ep_fm3` — the struck electric piano, written down before the first orchestra line

This is the pre-build record CLAUDE.md's Instrument Authoring rules require: method and source
in writing before any code, the incumbent named and rendered first, the capability list of the
entry being replaced enumerated before anything is written that could quietly drop half of it.

**The order, and the rule it stands against.** CLAUDE.md's authoring rule 6 says in as many
words: *„the FM electric piano specifically is 1990s technology this project can do without
(same day), so do not spend a build on one."* That is BJ's, 2026-07-26. His instruction of
2026-07-31 is the later one and supersedes it: *„ep_fm hat ja funktioniert, war nur eintönig
und schlicht, 90 Jahre niveau. aber eine Basis. nimm die neuen erkenntnisse und baue also ein
ep_fm3 oder epiano als parameter im fm3."* Recorded here rather than passed over, because a
rule this project wrote is not something an assistant may walk around silently.

## 1. Which of the two he offered, and why

He offered `ep_fm3` as an entry or `epiano` as a parameter inside `fm3`. It is an entry, for
three reasons that are the project's own and not a preference:

- **A single `epiano` knob would have to drive the tine ratio, the tine's decay and the note's
  decay at once.** Authoring rule 2: *„One control must also not drive two distinct model
  inputs — that is a knob whose meaning cannot be stated."* There is no way to write that knob
  and keep the rule.
- **`fm3` is declared to STAND** (sustain 0.996–0.999), BJ has heard it and called it „sehr
  mächtig", and every other word routed there — brass, trombone, clarinet — depends on it
  standing. Giving it a decay would change what those words get.
- **The electric-piano words are already parked on `fm3` with a declared gap.** Its `heard`
  field says so: *„ERKLÄRTE LÜCKE: `fm_ep` hatte `ring` und `strike`, dieser Eintrag hat weder
  eine Abklingzeit noch einen Anschlag — er STEHT."* An entry closes that gap; a knob would
  not.

The two entries name each other, the way `plucked_wire` and `string` do (`docs/LCO_CONCEPT.md`
§4): `fm3` is the held FM tone, `ep_fm3` is the struck one that falls away.

## 2. Why `fm_ep` was „eintönig", from the record rather than from an opinion

`docs/LCO_CONCEPT.md` §4 already contains the diagnosis, written before this task existed:

> Checked, not assumed: none of `analog_osc`, `fm_ep` or `drum_head` emits an amplitude
> envelope opcode, and all three are continuously sourced. […] **The convention's other half —
> the real instrument, with its own decay — does not exist at all.** It was never rejected; it
> was never reachable.

`fm_ep` was the standing reading of an instrument whose whole character is that it falls away.
On top of that it held its loudness with `balance asum0, aref0, 10`, and a corner at 10 Hz
tracks and flattens anything slower — including a tine decaying with a time constant of
0.21 s, which is about 4.8 Hz. The strike was being levelled out by the body's own loudness
guard. `fm_bell` already solved this for its doublet beat with `ihp = 1` (`docs/LCO_CONCEPT.md`
l. 173: *„exists to protect its doublet beat"*), and that is the corner this entry uses.

## 3. Method and sources — rule 1, before the first orchestra line

- **Chowning, John M. (1973):** „The Synthesis of Complex Audio Spectra by Means of Frequency
  Modulation", *JAES* 21(7), 526–534. The struck construction is his: the modulation index
  carries its OWN decaying envelope, so the spectrum darkens as the note falls instead of
  being filtered down. Already this project's source for `fm3`.
- **The DX7 corpus** — `docs/LCO_TIMBRE_SEMANTICS.md` §7, `tools/dx7_corpus.py --profile
  rhodes road roads rhods elpiano wurlitzer wurli tine`, 1386 named voices out of 119,296:
  - algorithm 5 in 854 of them — three carriers, one modulator each — carrier ratio 1;
  - **the tine ratio is a RANGE:** 14.0 in 595 voices, and 10, 12, 18, 22, 26, 28 all present;
  - **the far modulator dies back while the near one stands:** L1 99 → L2 75 at rate 50
    against 99 → 93 for the near family;
  - **the carrier decays too:** 99 → 75 at rate 25.
- **Hayes & Saitis (2020)** — `docs/LCO_TIMBRE_SEMANTICS.md` §2, under BJ's scope ruling that
  it governs FM entries: the modulator TUNING RATIO is the single largest correlate of
  `bright` (0.56/0.54, p<.001). That is why the tine ratio becomes an axis rather than staying
  the constant 14.2 `fm_ep` hardcoded — §4.8 records exactly this defect for the old `fm`:
  *„what the designers reached for hardest has no knob here."*

## 4. What `fm_ep` could do, before anything replaces it

Migration & Substrate Discipline rule 1: enumerate the old capability in writing first, and for
each one show it survives. Read out of `git show 5b5961ca~1:backend/dco_lexicon.json`.

| `fm_ep` | what it did | in `ep_fm3` |
|---|---|---|
| `ting` [0–1] | how much metal is in the strike; at 0 only the wooden body | `ting` — the tine's drive |
| `ring` [0–1] | how long the metal rings before settling into the body | `ring` — the tine's time constant |
| `hollowness` [0–1] | full ↔ odd-only body, „what makes a reed sound hollow and nasal" | `hollow` — same crossfade, full series against even-cancelled |
| `strike` [0–1] | how bright the body starts before it softens | `strike` — the body's index at onset |
| tine ratio | hardcoded 14.2, no knob | **`tine` [6–28] — the corpus's own range, new** |
| amplitude | stood; loudness held flat by `balance … 10` | **`decay` [0–1] — `; DECAY: SELF`, new** |

Nothing is dropped. The two additions are the two things the corpus and §4.8 say were missing.

**`hollow` is a level crossfade and never a ratio sweep.** Moving a modulator's ratio
continuously from 1 to 2 passes through 1.5, and `fm3`'s `ratio 3` note measures what that
does: *„a half-integer sounds an octave below"* the key. So the two bodies are rendered and
mixed, as `fm_ep` did it.

## 5. The two declarations every entry owes

- **Follows `kfreq`:** yes, by construction — all three carriers are `kc = kfreq * koct1`.
- **Stands or decays:** it **DECAYS**, `; DECAY: SELF`, and that is the point of the entry.
  Permitted, and not a new liberty: `docs/LCO_CONCEPT.md` §4, BJ 2026-07-27 — *„‚Pluck' ist ja
  gerade eine Transiente in sich, also qua benennung"* — and the same passage withdraws the
  clause that a name may not carry a decay. `plucked_wire` is the precedent in the lexicon.

## 6. Acceptance — rule 3, and rule 4's order

- **Incumbent, rendered BEFORE the first orchestra line:** `metallic_fm` at its defaults, at
  220 Hz, 6 s — `tools/lco_listening/ep_fm3_ab/nearest_existing_metallic_fm.wav`. It is the
  nearest thing the lexicon has: the same FM family, struck, with its drive decaying, and BJ
  has heard it.
- **Real reference, because the entry claims an instrument's name:** SA3, same protocol as
  `tools/lco_reference.py` (`alpha` pinned to −1, TrackType Instrument, 50 steps, cfg 6,
  6 s), prompt „solo electric piano, one instrument alone, single sustained note, close
  microphone, dry studio recording, unaccompanied, no reverb".
- **The decision is BJ's ear on an HTML page with the parameter combination visible**
  (`tools/lco_param_page.py`), never a measurement made here. Measurements accompany; they do
  not decide.
- **Attempt budget, rule 5: three.** Spent, the entry is dropped rather than optimised
  further.
