# Handover — T5 Oscillator "Easy" consolidation (free the Advanced view for the DCO)

Status: **in progress, and the last 3 commits have a WRONG layout that must be fixed first.**
Branch: `main` (user works directly on main, commits concurrently — see "Working style").

---

## 1. Why this work exists (context)

Long-term goal: turn the **T5Osc's Advanced ("back") view into a new classical DCO oscillator** — an LLM-authored, *non-neural* oscillator (see §7). Prerequisite: **move every T5-generation control off the Advanced view onto the Easy view**, so Advanced empties out and can host the DCO surface later. This handover is that UI migration only — NOT the DCO engine.

The user's organizing axis is **T5 vs T5Osc** (not Easy/Adv, not play/program): the neural/semantic core (prompts, embeddings) vs the oscillator's operational knobs (seed, steps, cfg, magnitude, chaos, duration). All of it consolidates onto Easy.

---

## 2. ⚠️ THE LAYOUT REQUIREMENT — this is where I failed repeatedly. Get it right.

The Easy generation-param block is **TWO ROWS, each a side-by-side PAIR** — exactly like the old layout, which already had Duration|Variation side by side in ONE row:

```
Row 1:  [ Duration  (inline slider) ]   [ Variation (VAR switchbox: 3 seed icons) ]
Row 2:  [ Magnitude (inline slider) ]   [ Chaos     (inline slider) ]
```

- **TWO rows. NOT four. NOT full-width stacked.** The user said "ZWEI Zeilen / 2" several times; I kept stacking. Do not.
- **Standard element height.** Do not squish, do not inflate/stretch to full width.
- This is the existing **`layoutCompactPair(...)`** pattern (2 params side by side, one row). Reuse/extend it: one call for Duration|Variation, one for Magnitude|Chaos.
- The user's reference screenshot shows Row 1 (DURATION | VARIATION side by side, each in a small framed box with a header icon) as the target.
- **OPEN DETAIL — confirm with the user before implementing:** whether each cell keeps the framed **card** look (clock/shuffle header icon, as in the old layout and the screenshot) or is a bare inline `SliderRow`. His text said "inline slider / switchbox"; his screenshot shows framed boxes. The row *arrangement* (2×2 side-by-side) is the hard requirement; the card-vs-bare styling is the thing to confirm.

---

## 3. Current (BROKEN) state — fix this first

The element *conversions* are correct; only the **layout arrangement + its height accounting** is wrong (4 stacked full-width rows instead of 2×2). Keep the element wiring, redo the arrangement.

Commits on `main` this session (newest first):
- `2b022cc2` Magnitude & Chaos → inline SliderRows (`magRow`/`noiseRow`), removed from Advanced. **Layout wrong (stacked).**
- `44e71cf2` Variation → switchbox row ("VAR" + 3 seed icons). **Part of the wrong stacking.**
- `07b08013` Duration → inline SliderRow (`durationRow`), removed from Advanced. **Part of the wrong stacking.**
- `b2e1cded` Revert of `7acbcb5a` (a rejected DimExplorer "overlay-only" attempt). DimExplorer is back to its baseline inline mini-view — leave it until §4.2.
- `c3ca9596`, `d5abeee4` housekeeping (gitignore + track accumulated tools/docs).
- Interleaved USER commits (his own parallel work, not part of this task): `a92717a9` (delete dead advanced-view code from SynthPanel), `0148215b` (remove the Easy/Advanced modulation toggle), `7db25fdd` (AdsrGraph → APVTS).

### The fix (2×2 layout)  — all in `src/gui/PromptPanel.cpp`
1. **`resized()` → the `layoutEasyGenParamsBlock` lambda.** It currently lays out 4 stacked rows (`easyRowH`/`stackGap`). Rewrite to **2 rows of side-by-side pairs**, mirroring `layoutCompactPair`: Duration|Variation (row 1), Magnitude|Chaos (row 2). (You can literally call a compact-pair-style helper twice.)
2. **`getPreferredHeightForWidth`** — easy branch currently sums `easyRowH*4 + stackGap*3 + gap`. Recompute for **2 side-by-side rows** (each row = one `layoutCompactPair` block ≈ `compactRowH + compactCtrlH + gap`; the Variation switchbox cell must fit the seed icons — size to match).
3. **`kPromptEasyContentUnits`** (PromptPanel.cpp:87, currently `23.48f`) **MUST equal the new easy-branch unit sum**, or the whole panel mis-scales its font. Recompute term-by-term. Unit values: `kPromptCompactRow=1.15`, `kPromptCompactCtrl=0.9`, `kPromptSeedCtrl=1.75`, `kPromptGap=0.28` (PromptPanel.cpp:51-62). `kPromptContentUnits` (Advanced, `23.19f`) is already correct — leave it.
4. The `paramsH` centering block inside `resized()` uses the same easy/advanced heights — keep it consistent.

**Keep (already correct):** `durationRow`/`magRow`/`noiseRow` are `SliderRow`s bound to `gen_duration`/`gen_magnitude`/`gen_noise`, with `onRightClick`→`showMidiLearnMenu` and SliderRow-native drift ghosts (`setGhostValue`/`tickGhost` in `timerCallback`). `varSwitchLabel` + `seedModeBtns` are the VAR switchbox (framed by `paintSwitchBoxBorder` via `seedModeSwitchBounds`). Magnitude/Chaos were removed from the Advanced grid and from `hasHiddenActiveState`. Only the *arrangement* is wrong.

---

## 4. Remaining planned work (after the 2×2 fix), in the user's order

1. **Seed dialog.** Double-click the "fixed seed" icon (the `Icon::Lock` seed-mode button, one of the 3) → open a dialog for manual seed number entry.
2. **Semantic Axes ↔ Dim Explorer switch.** The "SEMANTIC AXES" title bar (`AxesPanel`, in MainPanel's oscillator column) becomes a **2-segment switch [Semantic Axes | Dim Explorer]** — active segment bright, inactive dark. Toggling swaps the box below between the Axes controls and the **DimExplorer mini-view** (which still click-enlarges to the full overlay). Keep the mini-view — a prior "overlay-only, remove the mini-view" attempt was explicitly REJECTED (that's the `b2e1cded` revert). Not tied to Easy/Adv. All T5-semantic content lives on Easy.
3. **Steps & CFG.** Remove from the UI entirely; **always force the correct per-model values** on load/generation, even overriding wrong preset values. Correct values are per-model — source them from the backend/model defaults (SA3 small = 8 steps / CFG 1.0 per the screenshots). ⚠️ This revises the documented "adv STAYS (Mag/Noise/Steps/CFG/DimExplorer)" decision for **Steps/CFG only** — surface it and update the doc/memory.
4. **End state:** Advanced view emptied of T5 controls → becomes the DCO oscillator surface.

---

## 5. Key files / patterns / gotchas

- **`src/gui/PromptPanel.{h,cpp}`** — the GENERATION column = "T5Osc". Easy/Advanced via `easyMode_` / `setEasyMode`; the "» adv." toggle button lives in `MainPanel`.
- **`layoutCompactPair(lbl1,sl1,val1, lbl2,sl2,val2)`** (PromptPanel.cpp ~1138) — THE side-by-side 2-column row. Use it (or its shape) for the 2×2.
- **`SliderRow`** (`src/gui/GuiHelpers.h:858`) — house inline-bar row: `getSlider()`, `setInlineLabel(true)`, `onRightClick`→`showMidiLearnMenu(processorRef, PID, p)`, `setGhostValue`/`tickGhost` for drift ghosts. Reference usage: `resynthRow` in `MainPanel.cpp:1056-1079`.
- **Switchbox**: connected icon/radio buttons + `paintSwitchBoxBorder(g, <bounds>)` in `paint()`; set the `<...>SwitchBounds` rect to the union of the buttons. `seedModeBtns` are already switchbox-styled (`Icon::Ban/Lock/Shuffle`).
- **Font-scale constants** (PromptPanel.cpp:85-87): `kPromptContentUnits` (Advanced) and `kPromptEasyContentUnits` (Easy) MUST equal the respective `getPreferredHeightForWidth` unit sums — a stale value mis-scales the whole panel font. Change in lockstep with any height change; verify term-by-term.
- **JUCE safety**: SliderRow attachments (`durA/magA/noiseA`) declared AFTER their `unique_ptr<SliderRow>` (reverse destruction order). Never `setLookAndFeel(nullptr)`. `stopTimer()` in dtor (PromptPanel already does).
- **Build**: `cmake --build build_clean --config Release -j$(sysctl -n hw.ncpu)`. `build_clean` is the dev build; artifacts under `build_clean/T5ynth_artefacts/Release/{Standalone,VST3,AU}/`.

---

## 6. User working style (HARD — violate these and you waste his time)

- **Layout: two rows, side-by-side pairs, standard element height. Never stack into full-width rows; never inflate.** (The failure of this session.)
- **Don't ask him to "review" a standard/trivial UI element** — a plain inline slider exists many times already in the GenSeq. Just build it right; only surface genuine forks.
- He **commits directly to main concurrently** (no branches, no PRs). Always: verify HEAD before and after; stage only your specific files (**never `git add -A`**); **never touch his uncommitted files**; commit each single-concern unit.
- Verify every change (build + adversarial review) before committing — but never ship a known-wrong state.
- No AI-slop / no groveling; direct, source-grounded, honest.

---

## 7. The DCO-DSL concept (the end goal — captured so it isn't lost)

An LLM authors a **classical, non-neural oscillator** ("reinvent the DCO"): it emits a compact recipe = **keyframe additive spectra + a motion sequence (segments: target-keyframe, duration, curve) + loop**. Runs **offline** (background, like any generation) → bakes band-limited single-cycle wavetables → the audio thread only plays back + crossfades (the existing Wavetable morph). **Perfectly loopable by construction** (single-cycle tables are seamless at any pitch + the motion closes on keyframe[0]) — the payoff sample/diffusion generation can't give.

Predictability boundary = techniques with a **closed-form param→spectrum map** (additive, classic waves, band-limited PWM, hard sync, ring/AM, Chebyshev waveshaping, 2-op FM); exclude chaotic (multi-op/feedback FM). The real risk is **natural-language ambiguity**, not the math — a small code-LLM handles the named-synth-vocab translation and should FLAG timbre words ("warm/glassy") rather than hallucinate a spectrum; those go to a separate analysis branch (fit math to real softsynths). Scratchpad proofs from the design session: `tzpwm_demo.py`, `morph_loop_demo.py` (band-limited PWM + a saw→square→pulse→saw morph loop). This engine lives on the freed Advanced view.
