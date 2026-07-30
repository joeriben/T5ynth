# PLAN — a three-operator FM entry for the LRO

**Status: EXECUTED 2026-07-30 — the entry ships as `fm3`. See §7 for what was built, what the plan
got wrong, and what is still BJ's.** Written 2026-07-30 on BJ's instruction, after
the three timbre-semantics sources were entered in `docs/LCO_TIMBRE_SEMANTICS.md`. This plan derives
entirely from source **[2]** of that record and from the method paper it cites; §6 of that record is
the pre-build record this plan executes.

**Why this entry rather than any other.** [2] is the only research contribution in the project's
possession that measures **what experienced people actually change on a synthesiser when a word is
the instruction** — thirty sound designers, three words, one topology. That topology is a
three-operator FM instrument, and **no entry in the library sums two modulators into one carrier's
phase**: the six bodies that use FM are a `foscili` pair (`fm`, `fm_bell`, `metallic_fm`), a stack of
such pairs (`fm_ep`), or one pair inside a larger body (`clarinet`, `theremin`) — every one of them a
carrier with a single modulator. The entry exists to make a sourced word-to-parameter mapping
reachable at all. It is not
an FM electric piano and does not reach for FM where a physical model would cover the body, so it
sits inside `CLAUDE.md` §Instrument Authoring rule 6 rather than against it.

## Research contributions this plan rests on

| contribution | what is taken |
|---|---|
| Chowning, John M. (1973): „The Synthesis of Complex Audio Spectra by Means of Frequency Modulation", *JAES* 21(7), 526–534 | the synthesis method itself — the named, published mechanism required by `CLAUDE.md` rule 1 |
| Hayes, Ben & Saitis, Charalampos (2020): „There's more to timbre than musical instruments: semantic dimensions of FM sounds", *Proc. Timbre 2020*, Thessaloniki | the topology (operators 2 and 3 modulating operator 1's phase in linear combination) and the word-to-parameter **directions** (their Fig. 4, Spearman) |
| this project's own measured idioms | loudness discipline (`balance` against a steady reference, as `fm`/`fm_bell`/`metallic_fm` already do) and the sideband-room limit computed from `sr` and `kfreq` |

Nothing else. In particular **no numbers from [2] are used as values** — only as directions and as
the naming of which end of a range is which. [2]'s scope ruling (BJ, 2026-07-29) is that it concerns
FM instruments only; this entry is an FM instrument, which is the whole reason it is the place the
mapping may be used.

---

## 1. Order of work — comparison first, code second

`CLAUDE.md` rule 4 is an order reversal, not a preference, so it is step zero.

0. **Render `nearest_existing.wav` from the existing `fm` entry** at **its own default** — `index`
   0.2 on its 0–1 scale (the entry's `why` describes the body as „ratio 2, index 1.5", which is the
   Csound-side ratio and peak, not the parameter value) — at one pitch, one length, one loudness, and
   put it on disk. Name it in the folder. Only then may an orchestra line of the new entry be written.
1. **Verify the mechanism compiles at all.** There is no generic N-operator opcode on this build —
   verified against `csound -z1`: `foscil`/`foscili` are one carrier and one modulator, the `cross*`
   family is two mutually modulating oscillators, and the `fm*` family are fixed algorithms. Two
   modulators summed into one carrier's phase has to be written out: a `phasor` on the carrier plus
   both modulator signals added into a `tablei` read. **If that does not compile or aliases
   unacceptably, stop and report.** There is no fallback to chained `foscili` calls dressed up as
   three operators — that would be a subset pretending to be the thing, which is the failure
   `CLAUDE.md` §Migration rule 1 exists for.
2. Write the body (§2), against the loudness and aliasing idioms the library already uses.
3. Measure the parameter ranges to set anchors (§3). Computing the input a model needs is ordinary
   engineering and is not the fitted-curve-over-output that rule 2 forbids.
4. **A/B for BJ's ear**: one folder, `nearest_existing.wav` and `new.wav`, same pitch, length and
   loudness. His ear decides; own measurements may accompany the result and never decide it.
5. Only after that: the entry goes into `backend/lco_library.json`.

**Attempt budget** (rule 5 requires one fixed up front, and it is BJ's to fix): proposed **three**
attempts. When spent, the entry is dropped rather than optimised further. — **BJ fixed it at 15**
(2026-07-30), together with keeping all six axes. Two attempts were heard; the second is what
shipped.

---

## 2. The body — what it must and must not contain

**Must:**

- Three operators: a carrier whose phase is the sum of its own `phasor` and both modulator signals,
  in linear combination. Chowning 1973; topology per [2].
- **Track `kfreq`** by construction — the carrier phasor is driven by `kfreq * koct1`, and both
  modulators by `kfreq * koct1 * ratio`.
- **STAND, not decay.** The host owns the note-off, so the index-decay that `fm`, `fm_bell` and
  `metallic_fm` use for their ring is explicitly **not** the model here. That is the one structural
  difference from the three existing bodies.
- **Hold its loudness across the index range** the way the library already does it: build the
  signal, `balance` it once against a steady `poscil` reference at the same pitch. Raising an FM
  index raises brightness and level together; the existing bodies solve this and the solution is
  mirrored, not reinvented (`CLAUDE.md` §Platform Invariants: „Mirror an existing engine — never
  invent a one-off").
- **Limit its sidebands to the band** with the same computation the existing FM bodies use —
  `limit (sr * 0.45 / (kfreq * koct1) - 1) / ratio - 1, 0, 40` — so a high index at a high pitch
  does not fold back.
- **Move.** Movement by default is a platform fundamental (`docs/LCO_CONCEPT.md` §4). **[2] contains
  no movement data at all** — it measured static parameter deltas — so the movement is the library's
  own convention (a free-running `oscili`/`poscil` on a driving parameter, as `analog_osc` and
  `driven_metal` do) and must be declared in the plan and in the entry as **not derived from [2]**.
  The driven parameter is a modulator index or ratio, not the output level.

**Must not:**

- No amplitude envelope on the output. No built-in vibrato or tremolo
  (`feedback_no_builtin_vibrato`).
- No index decay standing in for a ring.
- No fitted correction laid over the model's output to flatten a behaviour (rule 2).
- No single control driving two distinct model inputs (rule 2) — see §3.

---

## 3. Parameters — the user surface

The lexicon's parameters are the surface the player sees in „Prompt Orchestra"
(`project_lco_params_are_the_user_surface`), and rule 2 forbids one control from driving two distinct
model inputs. [2] measured both modulators moving **together** for *bright* and *rough*, but one knob
for both gains would be exactly that forbidden knob, so they stay separate:

| parameter | model input | what [2] says about its direction |
|---|---|---|
| `index2` | operator 2's modulation depth | ↑ for *bright* (0.41\*\*\*), ↑ harder for *rough* (0.63\*\*\*) |
| `index3` | operator 3's modulation depth | ↑ for *bright* (0.52\*\*\*), ↑ for *rough* (0.51\*\*\*) |
| `ratio2` | operator 2's tuning ratio to the carrier | ↑ for *bright* (0.56\*\*\*) and *rough* (0.42\*\*\*), ↓ for *thick* (−0.28\*\*) |
| `ratio3` | operator 3's tuning ratio to the carrier | ↑ for *bright* (0.54\*\*\*) and *rough* (0.56\*\*\*) |
| `onset` | how fast the modulators reach their depth | ↓ (faster) for *bright* and *rough*; this is a **spectral** onset — how quickly the sidebands arrive — and never the note's loudness attack |

Five parameters is more than any existing FM entry carries (`fm` has three). That is the honest count
for this topology and it is flagged rather than hidden; if BJ wants it smaller, the axis to drop is a
whole operator, not a knob merged onto two inputs.

Each parameter needs a **measured** range, a default, a `note`, and named anchors with glosses, in
the library's existing format — derived by rendering the axis and reading it, exactly as the other
entries' anchors were.

---

## 4. Declarations the entry owes

`CLAUDE.md` rule 3 requires two objective declarations per entry, and this one owes a third.

- **Follows `kfreq`:** yes, by construction.
- **Stands or decays:** **stands.** The host owns the note-off.
- **The property to declare rather than correct** (rule 2): ***bright* and *rough* are not separable
  on FM.** [2]'s own explanation — „it is challenging to increase the energy in high frequency
  components … without also increasing inharmonicity" — makes this a property of the substrate, not
  a defect of the entry. It is written into the entry's text so the author is never routed to this
  body for roughness, and so nobody later tries to fix it with a fitted number. Roughness needs a
  substrate that packs partials close enough to interact; this is not that substrate.
- ***thick* is not realisable here** beyond its tuning-ratio component. Its entire signature in [2]
  is the sustain levels, strongest the carrier's (0.50\*\*\*), and sustain level is the player's.

---

## 5. Surface forms — one collision to resolve

`fm` currently owns the bare word „fm". `docs/LCO_CONCEPT.md` records the convention that **a key
only gives up a word to a key that exists**, and the trade is made per key on the day the
counterpart lands, never in advance. So the new entry gets its own forms and the trade with `fm` is
decided when the entry actually exists and has been heard — not in this plan. Proposed key: **`fm3`**.
Nothing is taken from `fm` here.

---

## 6. Gates, and what would kill the entry

- `tools/csound_orch_check` — compiles and renders.
- The gate must **play**, not merely parse (`project_lco_gate_must_play_not_only_parse`).
- Objective checks with the project's calibrated meter (`tools/lco_measure.py`): the body moves; the
  loudness does not travel; no fold-back across the pitch range; the index axis actually changes the
  spectrum at every pitch rather than only in one octave.
- **Kill criteria, either of which ends the work rather than triggering another attempt:** the
  hand-written phase modulation does not compile or aliases unacceptably (§1 step 1); or the A/B does
  not convince BJ within the three attempts, in which case the entry is dropped
  (`feedback_quality_bar_omit_mediocre`).

---

## 7. Executed — what shipped, what the plan got wrong, what is still BJ's

| commit | what |
|---|---|
| `61d7d750` | `feat(lexicon): fm3 — two modulators, and the drive changes hands` — the entry, six axes, 25 anchors, gated |
| `675a865c` | `fix(lexicon): fm3 keeps its whole drive inside the band, and its notes say what it plays` — the three defects below |

**BJ's reading, 2026-07-30, against `nearest_existing.wav` from `fm`:** `fm` is Rhodes-like; `fm3` is
bassoon-like, with a kind of pulse-width modulation. The entry takes neither name. Attempt 1 was heard
as static — the trade oscillator had been copied from `driven_metal` at 0.09 Hz, a 9 s period inside a
3 s file; at 0.45 Hz the movement is in the note. That is the whole distance between the two attempts.

**Three things this plan did not anticipate, all found after the first commit and fixed in the
second.** They are recorded here because each is a class, not an incident.

1. **The sideband-room limit in §1's idiom list is wrong when there are two modulators.** Bounding
   each operator as if it were the only one ignores that the travelling drive arrives *on top of* an
   operator's own index: the effective index reached twice its bound, and at ratio 8/8, both indices
   0.6 and `trade` 0.75 a 440 Hz note at sr 44100 carried a sideband at 49 kHz — 42.7 % of its energy
   off the grid. It is now Carson's rule summed over both operators, one scale factor for the whole
   drive. The library's existing idiom was written for one-modulator bodies and does not generalise.
2. **A coincidence guard is a control-space edit and has to be bounded like one.** The first guard
   fired within 1.5 of `ratio 2` and pushed up by 1.75: it produced 9.75, outside the axis's own
   declared range, and flattened up to 43 % of `ratio 3` onto one value. Only exact coincidence is
   static (0–9 cents through the sustain; a separation of 0.01 already moves 1130), so it now fires
   within 0.02 and pushes toward the middle of the range.
3. **The entry read its own spectrum against the key instead of against its own fundamental**, and
   told the author the wrong thing about it. With `ratio 2` at 1.0 a half-integer `ratio 3` puts the
   spectrum on a grid of `kfreq`/2 — the note sounds an octave *below* the key, and everything on that
   grid is harmonic. Measured f0 is exactly `kfreq`/2 at 1.5, 2.5, 3.5, 4.5, 5.5, 6.5 and exactly
   `kfreq` at 2, 3, 4, 7, 8, at every register from 55 to 1760 Hz. The „62.7 % off-series" the first
   commit reported was that octave counted against the key; off the series it actually has, the body
   is 1.3–2.1 % everywhere. **Measure f0 before quoting a harmonic ratio** — §6's meter list did not
   ask for it, and the gate does not check absolute pitch.

**A limit of the gate, found here and now recorded in `tools/lco_axis_probe.py`'s own limits text:**
the movement question is asked of the whole note, so a spectral attack alone answers it. With its
trade oscillator frozen this body reads 864 cents at coherence 0.99 over the note and 6 cents through
the sustain — and passes. Whether „it moves" should mean the sustain is a change of standard, and
BJ's.

**Still BJ's:**

- **The default of `ratio 3`.** It ships at 5.5 — an octave under the key, which is the material he
  heard and read as bassoon-like. A whole number (2, 3, 4, 7, 8) plays at the key. Both are declared
  in the entry; which one is the default is a listening decision, not a measurement.
- Whether the gate's movement rule should require sustained movement (above).
- `sr * 0.45` versus the plugin's oversampling: `%SR%` resolves to the *oversampled* rate, so the
  taught clamp sits at 79 380 Hz against a delivered band ending at 22 050. Raised, unresolved, and
  now visible here as a side effect — the 338 corner-renders at 1760 Hz the gate reports as static
  exist only at sr 44100; at 176400 the same corners move 1066–1151 cents.
