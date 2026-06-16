# UI Design Audit — T5ynth main window

Status: **audit only, no code changed.** This document does two things the
maintainer asked for:

1. An inventory of visual/UX inconsistencies in the current main window
   (standard and max resolution, MacBook Pro 16").
2. A clarification of the *technical design foundations* — which reusable
   templates exist, which are missing, and concrete design ideas for visual
   clarity (including text→icon replacements).

It is grounded in the actual source (file:line citations throughout), not only
in the screenshots. Nothing here is implemented yet.

Vocabulary note — the two right-column views are confusingly named in code:
`SynthPanel::modEasyMode == true` draws the **columnar / knob** view whose
umbrella bar reads `CONTROLS` and whose toggle button reads `» adv.`;
`modEasyMode == false` draws the **stacked / horizontal-slider** view with
per-module bars (`ENVELOPES / LFO / DRIFT / REGENERATE`) and a `» easy` button.
Below, "columnar view" = `modEasyMode==true`, "stacked view" = `modEasyMode==false`.

---

## 1. Inconsistency inventory

### A. Typography / type scale

| # | Issue | Evidence |
|---|-------|----------|
| A1 | **No typographic scale.** Sizes are derived ad-hoc per panel from a local base font via magic multipliers (`f * 0.82`, `headerH * 0.85`, `labelH * 0.76`) and arbitrary clamps (`jlimit(12.5, 17, …)`, `jlimit(11, 22, …)`). The only shared constants are three identical minimums. | `GuiHelpers.h:23-25`; `PromptPanel.cpp:38-42`; `SynthPanel.cpp` `fs()` |
| A2 | **"Re-Prompt" caption is ~18 % smaller than its sibling "Duration".** Re-Prompt uses `f * 0.82`; Duration uses plain `f`. Same role, different size. | `PromptPanel.cpp:988` vs `:1057` |
| A3 | **"Resynth" caption is never given a font in `resized()`** — unlike its row-neighbours SNAP/CACHE, which get a responsive `switchFs`. So it does not scale with the window the way every peer does. (Latent bug, not only a style nit.) | `MainPanel.cpp:3106-3118` (sets bounds, no `setFont`); cf. SNAP/CACHE `switchFs` |
| A4 | **No weight hierarchy.** Only the main GENERATE button uses bold + kerning; everything else is default weight, so headings and captions and values carry the same typographic weight. | `MainPanel.cpp:170-243` |
| A5 | **`makeLabel()` sets text/colour/justification but not font.** Font is always applied later in a layout lambda, separately per label — so any label whose layout path forgets (A3) or differs (A2) silently drifts. | `PromptPanel.cpp:119-126` |

### B. Free-standing labels & visual grouping (the maintainer's main complaint)

| # | Issue | Evidence |
|---|-------|----------|
| B1 | **Captions sit directly on the page background with no scaffolding** — Duration, Variation, Resynth, Magnitude/Chaos/Steps/CFG, SNAP/CACHE. There is no visible "module" to bind a label to its control. | left-column layout, `PromptPanel.cpp:1034-1122`; `MainPanel.cpp:3106-3118` |
| B2 | **Duration: the value floats free.** Caption is `kDim` (grey), value `11.00s` is `kOscCol`, and the slider track is `kSurface` (#1e2130) — almost invisible against `kBg` (#0e1018). With no container and an unreadable track, the value reads as orphaned. | `PromptPanel.cpp:271-272`, `:1057-1068`; track colour `GuiHelpers.h:18` |
| B3 | **Variation: label not flush with its controls → "scattering".** The caption is placed in a full-width header row; the none/last/auto buttons are laid out below with their own independent widths, so nothing lines up under the caption. | `PromptPanel.cpp:297`, `:1095-1114` |
| B4 | **Resynth: a lone label+slider row glued under SNAP/CACHE** with no container — it reads as stuck to the bar above rather than as its own control. | `MainPanel.cpp:3106-3118` (comment: "directly beneath the snap/cache row") |
| B5 | **The scaffolding primitive already exists but is unused here.** `paintCard()` (card fill + 1 px border) and `paintSwitchBoxBorder()` are in `GuiHelpers.h`; the floating-label clusters don't call them. | `GuiHelpers.h:104-120` |
| B6 | **Three different label↔control idioms coexist.** Knobs: caption above / value below (the knob anchors the group). Sliders (stacked view): caption left / value right. Left column: caption floating above a faint slider. The eye has to re-learn the grouping per region. | `SynthPanel.cpp` knob rows; `GuiHelpers.h` `SliderRow`; `PromptPanel.cpp` |

### C. Layout / alignment / spacing

| # | Issue | Evidence |
|---|-------|----------|
| C1 | **No spacing tokens or grid.** Gaps and sizes are magic multipliers of a per-panel base font (`f*0.28`, `f*1.4`, `h*0.030`, `f*10.6`, …) scattered through each `resized()`. Cross-row alignment relies on hand-matched constants (e.g. FxPanel value columns hardcoded `56`/`48` px). | `PromptPanel.cpp:851-862`; `FxPanel.cpp:84-90` |
| C2 | **No FlexBox/Grid.** Everything is manual `removeFromTop/Left`. Correct, but every alignment is a hand-tuned constant rather than a declared relationship. | all `resized()` |
| C3 | **Two section-header drawing paths.** Most headers use the `paintSectionHeader()` helper; ENGINE/FILTER/ENVELOPES use a near-identical inline `makeHeader()` lambda. Duplicate logic that can drift. | `GuiHelpers.h:96-102` vs `SynthPanel.cpp:830-838` |
| C4 | **GENERATE button title is a "non-aligned block".** Label and chevrons are drawn as separate rects; the label uses `centredLeft` anchored at `bounds.getY()` (the top edge), not vertically centred as one composite. Two code paths (fits / too-narrow) use different justification. | `MainPanel.cpp:170-243` |

### D. Structure / modularity

| # | Issue | Evidence |
|---|-------|----------|
| D1 | **The umbrella `CONTROLS` bar contradicts the per-module bars.** The columnar view groups ENV+LFO+DRIFT+GENERATE under one `CONTROLS` header; the stacked view already gives each its own bar (`ENVELOPES / LFO / DRIFT / REGENERATE`). The two views express modularity differently. The maintainer wants per-module bars everywhere. | `SynthPanel.cpp:3143` (header text switch); columnar layout `:2448-2502` |
| D2 | **Mixed metaphor inside `CONTROLS`.** Within the columnar `CONTROLS` block, LFO/DRIFT/GENERATE already have per-module sub-chips, but ENV is tabbed and the whole sits under one bar — so it is half-modular, half-unified. | `SynthPanel.cpp:2186-2192`, `:2247-2255`, `:2299` |

### E. Naming / terminology

| # | Issue | Evidence |
|---|-------|----------|
| E1 | **GENERATE vs REGENERATE for the same control.** The auto-regeneration cadence control is titled `GENERATE` (centred chip) in the columnar view and `REGENERATE` (left bar) in the stacked view; init paints it `REGENERATE`. Maintainer flags "Regenerate" as outdated. (Also risks colliding with the left-column main GENERATE button.) | `SynthPanel.cpp:2299` vs `:3251`; init `:1145` |
| E2 | **The `modEasyMode` flag is inverted relative to perceived complexity.** `modEasyMode==true` shows the knob/columnar view and the `» adv.` button; the more detailed stacked view is `modEasyMode==false`. "easy" in code names the view the user associates with the busy `CONTROLS` bar. Maintenance hazard. | `SynthPanel.cpp:1621-1622`, `:3143` |
| E3 | **Header justification differs between views** for the same control (`centred` chip vs `centredLeft` bar). | `SynthPanel.cpp:2299` vs `:3251` |

### F. Colour

| # | Issue | Evidence |
|---|-------|----------|
| F1 | **~62 inline `juce::Colour(0x…)` literals** bypass the central palette — some duplicate existing constants (`kBg`, `kAccent`, `kEnvCol`), some introduce un-named colours (DimensionExplorer reds/greens). | `DimensionExplorer.cpp`, `MainPanel.cpp` |
| F2 | `kFilterCol` is defined but some filter sub-rows fall back to `kEnvCol`. Minor. | `GuiHelpers.h:42`; `SynthPanel.cpp` filter rows |

---

## 2. Design-system foundation — what exists vs. what's missing

The instinct that "there are no / too few templates" is half right. The
**low-level primitives exist and are good**; the **mid-level templates that
compose them into consistent, self-contained parameter units are missing**, so
every panel hand-assembles label + control + value + spacing, and they drift.

### Exists (keep)

- **One LookAndFeel:** `T5ynthLookAndFeel` (rotary, button, combo, toggle). `T5ynthLookAndFeel.{h,cpp}`.
- **Central palette:** `GuiHelpers.h:10-69` (base, text ramp, semantic axes, per-module accents, A/B identity). Verified colours, good documentation.
- **Section-header helper:** `paintSectionHeader()` `GuiHelpers.h:96-102`.
- **Card / border primitives:** `paintCard()`, `paintSwitchBoxBorder()` `GuiHelpers.h:104-120`.
- **Measurement + responsive strip:** `measureTextWidth()` `:122-130`, `layoutResponsiveStrip()` `:157+`.
- **Composite widgets:** `SliderRow` (heavy but solid), `CurveButton`, `ClockButtonLnF`, `AlphaSliderLnF`, `FlippedVerticalSlider`, `UnionJackButton` — all in `GuiHelpers.h`. (`SliderPair` is defined but unused.)

### Missing (the "templates" gap)

1. **A typographic scale** — named roles + sizes, derived from one base. (Today: ad-hoc multipliers.)
2. **A `LabeledControl` composite** — `[caption | control | value]` as one unit, so a caption can never float. (Today: `makeLabel` + manual placement; `SliderRow` exists but is slider-only and heavy.)
3. **A `ModuleBox` container** — `paintCard` background + header strip + consistent inner padding, wrapping a parameter cluster. (Today: `paintCard` exists, nothing wraps the floating clusters with it.)
4. **Spacing tokens / a layout grid** — named units instead of `f*0.28` magic numbers.
5. **A switchbox / radio-group template** — button groups (engine mode, regen cadence, variation, noise colour, filter type) are laid out by hand in every panel.
6. **A central icon registry + icon-button template** — custom glyphs already exist piecemeal (`CurveButton` SVGs, the clock `Path`, the Re-Prompt glyph row) but there is no shared library and no reusable icon-button.
7. **One canonical section-header path** — fold `makeHeader()` into `paintSectionHeader()`.

---

## 3. Proposed template layer

A thin design-system layer (one header + a few small components), then migrate
panels onto it incrementally. Sketches, not final APIs.

### 3.1 Type scale (kills A1, A2, A4, A5)

Four roles, all derived from one responsive base so they scale together:

| Role | Use | Weight | Colour |
|------|-----|--------|--------|
| `Title` | colored section bars | medium | dark-on-accent (existing) |
| `Caption` | "Duration", "Cutoff" | regular | `kTextSecondary` |
| `Value` | "11.00s", "659 Hz" | medium, tabular figures, right-aligned | section accent |
| `Hint` | "Audio length (seconds)" | regular, small | `kTextMuted` |

Define as `kFontTitle/Caption/Value/Hint` (functions of the panel base), and
make `makeLabel()` take a role so font is set with text — never separately.

### 3.2 `LabeledControl` + `ModuleBox` (kills B1–B6)

- **`LabeledControl`**: always lays out caption + control + value with one
  geometry and a shared baseline; the value is visually bound to the control
  (in a value field at the track end, or directly over the track with a tick).
  Generalise `SliderRow`'s good parts; reuse for sliders, combos, button rows.
- **`ModuleBox`**: `paintCard` fill + a thin accent header strip + fixed inner
  padding. Every cluster (Duration, Variation, Resynth, the regen box) lives in
  one. This is the "visuelle Einheit des Moduls" that's missing today.

### 3.3 Visible slider track (kills B2)

The `kSurface` track disappears against `kBg`. Give horizontal sliders a
two-tone track — filled portion in the section accent, unfilled in `kBorder`
(lighter than `kSurface`) — and anchor the value to the track end. Label +
slider + value then read as one unit even without a box.

### 3.4 Spacing tokens (kills C1)

`kSpace1..kSpace4` (or a `Metrics` struct keyed off the base font) replacing
inline `f*0.28` etc., so sibling rows align by construction.

### 3.5 Switchbox template (kills part of C, D)

A `SwitchBox`/radio-group component that lays out N options (text **or** icon)
with `paintSwitchBoxBorder`, manages selected state, and reports its union
bounds — replacing the hand-rolled loops in every panel.

### 3.6 Icon registry + icon-button (enables §4)

A single source for glyphs (the existing `CurveButton`/clock/Re-Prompt art,
plus new ones) and one `IconButton` template (icon + optional micro-label +
tooltip). House stroke weight defined once.

### 3.7 Header consolidation (kills C3)

Delete the `makeHeader()` lambda; route ENGINE/FILTER/ENVELOPES through
`paintSectionHeader()`.

---

## 4. Visual-clarity & icon ideas

Text→icon, building on the Re-Prompt glyph row that already works. Rule of
thumb: **icon + micro-label** for primary controls, **icon-only + tooltip** for
dense secondary controls. Keep glyphs small (consistent with the maintainer's
preference for small mode-symbols). Icons need one stroke weight and one source
(§3.6) or they look like a ransom note.

| Control | Today | Proposed glyph (concept) |
|---------|-------|--------------------------|
| Variation: none / last / auto | three words | ⦸ ban · 🔒 lock-seed · 🎲 dice/shuffle |
| Regenerate cadence: manual / a.s.a.p. / 1·2·4·8·16 bars | word list | clock/metronome group icon + bar-count numerals (numerals stay clearer than note glyphs here) |
| Resynth | word + % | loop/recycle glyph + % |
| SNAP | word | magnet |
| CACHE | word | stack/database |
| A/B blend: Linear / Step-in / Layer / Combo | words + flag | small blend-shape diagrams (the flag already shows this idiom can work) |
| Duration | word + value | optional waveform-length tick on the track |

Caveat: discoverability. Pure icons raise the learning curve, so keep
micro-labels on the primary row and reserve icon-only for the dense secondary
controls, always with tooltips.

---

## 5. Structural & naming decisions to make (not yet decided)

- **CONTROLS → per-module bars.** Recommended: adopt per-module header bars
  (ENVELOPES / LFO / DRIFT / GENERATE) in *both* views — i.e. bring the stacked
  view's modular bars into the columnar view, and drop or demote the umbrella
  `CONTROLS` bar to a thin group label. Emphasises the modular structure the
  maintainer wants. (D1, D2)
- **Name the regen control once.** `REGENERATE` is semantically accurate (it
  controls *auto-re-generation cadence*) but the maintainer calls it outdated,
  and the columnar view says `GENERATE` — which collides with the main GENERATE
  button. Candidate canonical names: `AUTO-REGEN`, `REGEN`, or keep `GENERATE`
  with a "repeat every" subtitle. **Decision needed before any rename.** (E1, E3)
- **Rename `modEasyMode`.** Internal-only, but the inversion is a maintenance
  trap; align the flag name and button semantics. (E2)
- **Fold the 62 colour literals** back into the palette so the templates above
  are actually enforceable. (F1)

---

## 6. If/when we implement (suggested order, deferred)

1. Type scale + role-aware `makeLabel` (low risk, immediate consistency).
2. Visible slider track in the LookAndFeel (fixes the worst legibility issue).
3. `LabeledControl` + `ModuleBox`; migrate the left column first (worst offender).
4. Switchbox template; migrate button groups.
5. Icon registry + icon-button; roll out the §4 glyphs.
6. Structural/naming decisions from §5.
7. Header + colour-literal consolidation (cleanup).

Each step is independently shippable and individually verifiable against the
performance gate in `docs/PERFORMANCE_GUIDE.md` (no new idle repaints).
