# Handover — Save-UI Refactor (T5ynth)

**Date:** 2026-05-01
**Branch:** `main`
**Context:** 7-phase refactor of the Save / Browse / Import surface in
`PresetManagerPanel` and `MainPanel`. User is **not satisfied** with the
result. Read the "User critique" section first.

---

## Branch state

### Committed (newest → oldest)

```
44603888 feat(gui): cmd+s opens library in save mode
b8f8ff2d refactor(gui): drop Save As, rename Browse to Library, expand Import
f89a534a refactor(gui): remove SavePresetDialog and its plumbing
14c8069f feat(gui): drag tag chips from detail card to save drawer
74bf57fb feat(gui): save-mode list highlight + sidebar bank routing
2d34524b refactor(gui): library save mode (drawer) replaces save dialog
33fc6056 chore(gui): remove dead PresetPanel
```

(Two unrelated docs commits by the user — `577c92c5`, `f8a0cca7` — sit
between phase 2 and phase 3. Not part of this refactor.)

### Uncommitted in working tree

**One half-finished edit** in `src/gui/PresetManagerPanel.h` (constructor):

```cpp
saveDrawer.onCancelClicked = [this] { if (onCloseRequested) onCloseRequested(); };
```

Was: `[this] { leaveSaveMode(); }` → now: closes overlay entirely. This
addresses User-bug 1 (below) but the related pieces (Esc behaviour, the
Library "Cancel" → "Close" rename, the context-menu placement fix) were
**not** done. Either complete the bundle and commit, or revert this one
line. Right now the working tree compiles but is mid-fix.

The other usual unstaged drift in `git status` (CHANGELOG.md, README.md,
docs/devlog.md, src/dsp/BlockParams.h, src/sequencer/Arpeggiator.{cpp,h},
SetupWizard.cpp, …) was there at session start and is **not mine** —
don't stage it as part of this refactor.

---

## User critique (ground truth)

The user opened the built Standalone, walked through the Save flow, and
pushed back on four points. Quote-paraphrased:

1. **Save → Cancel drops you into Library Browse mode.** Wrong. Cancel
   from a Save action means abort entirely; the user came to save, not
   to browse. Cancel must close the whole overlay.

2. **Library bottom button is labelled "Cancel".** Wrong. The Library is
   not a single-action workflow (you browse, load, import, edit tags,
   rename, delete) — "Cancel" implies "abort the operation", which
   doesn't fit. Should be **"Close"**.

3. **Right-click context menu (Rename… / Delete) opens at the *bottom
   edge of the panel*, not at the cursor.** Looks broken. Cause:
   `showRowContextMenu` uses `withTargetComponent(&presetList)`, which
   anchors the popup at the listbox bounds, not the click position.
   Also: the menu itself is "primitiv" (just two text items, no icons,
   no separator).

4. **Tag input has zero autocomplete / vocabulary support.** "Tippt man
   alle Tags weiterhin ohne jegliche Unterstützung, so dass totales
   Tag-Chaos entsteht nach 15 Presets?" — completely fair. The Sidebar
   already aggregates the user's tag vocabulary (see
   `refreshSidebarVocabulary` ~line 340 in PresetManagerPanel.h), but
   the Save-Drawer's tag-input is a raw `juce::TextEditor` with no
   completion. Drag-from-Detail helps, but only if the source preset
   already carries the tag — discoverable autocomplete is missing.

### And the bigger one

> "Library sieht genau aus wie vorher, und 'save' kopiert lediglich
> deren Design. Mir ist ausgesprochen unklar, wieso Du hier 20 Minuten
> oder länger gecodet hast."

Largely true at the visual level. The Save-Drawer is a near-1:1 port of
the deleted `SavePresetDialog`'s form layout (name / warning / bank /
tags / button strip). The Library list, sidebar, and detail card were
not redesigned. **The architectural win is real (one overlay, no
modal-on-modal, no dead code, conflict-aware highlight, drag&drop) but
the visual transformation the user expected from "sauber und Umbau,
Synth hat es verdient" did not land.**

---

## Was getan wurde — der granulare Inventar

Honestly, per commit, what hit the disk:

### `33fc6056` — chore(gui): remove dead PresetPanel
- Deleted `src/gui/PresetPanel.{h,cpp}` (−136 LoC). The files were
  already not referenced from CMakeLists.txt.
- Verified zero callers via grep before deletion.
- **Weight:** cleanup. Not value.

### `2d34524b` — refactor(gui): library save mode (drawer) replaces save dialog
- PresetManagerPanel.h: +~430 LoC. Added `enum class Mode { Browse, Save }`,
  `SavePrefill` struct, public `enterSaveMode/leaveSaveMode/getMode`,
  `onSaveRequested` callback, the `SaveDrawer` nested class
  (name edit, bank combo, tag chips with × hit-test, save/cancel/+copy
  buttons, conflict-aware "Replace …" Save button — **about 200 LoC of
  this is a near-1:1 port of the SavePresetDialog form layout**).
- Mode-aware `resized()` (drawer at bottom in Save, status/cancel
  footer in Browse).
- Mode-aware `keyPressed` / `listBoxItemDoubleClicked` /
  `listBoxItemClicked` (loading and rename/delete suppressed in Save).
- MainPanel.{h,cpp}: +92/−21. Added `enterLibrarySaveMode` building
  `SavePrefill` from disk, routing through `presetManager.enterSaveMode`.
  `savePreset` / `saveAsPreset` redirected. `hidePresetManager`
  defensively calls `leaveSaveMode`.
- Two post-review fixes folded in before commit: drawerH 140 → 200
  (chip area was negative-height under the original arithmetic, chips
  would never have rendered) and explicit `~SaveDrawer()` calling
  `removeKeyListener(this)` on `nameEdit` and `tagInput`.
- **Weight:** real architectural change. Modal-on-modal gone. Drawer
  visual is the port people called out.

### `74bf57fb` — feat(gui): save-mode list highlight + sidebar bank routing
- PresetManagerPanel.h only: +99/−9.
- `SaveDrawer` exposes `getConflictPathKey()`, `setBank()`,
  `onConflictChanged` callback; `refreshConflictUi` fires it at the
  end of every name/bank change.
- Outer panel caches the conflict row via new `computeConflictRow()`,
  resets in `leaveSaveMode`, re-runs at the end of `refreshLibrary`
  defensively. `paintListBoxItem` paints that row red (28% wash + red
  3-px left-edge bar) and explicitly excludes factory entries.
- Sidebar's `onChanged` extended to push the active bank into the
  drawer when in Save mode (toggle-off "All" leaves drawer bank).
- **Weight:** real value-add. Most visible Phase-2 outcome.

### `14c8069f` — feat(gui): drag tag chips from detail card to save drawer
- PresetManagerPanel.h only: +90/−3.
- `PresetManagerPanel : public juce::DragAndDropContainer`.
  `SaveDrawer : public juce::DragAndDropTarget` with all four DnD
  overrides + a `dropHover` accent backdrop in `paint`.
- `Detail::mouseDown` reorganised to record `pressedChipIndex` (any
  chip body hit while `dragSourceEnabled`) BEFORE the `×` branch (the
  × branch then clears it to prevent phantom drags after a remove).
  New `mouseDrag` starts DnD past 4 px movement. New `mouseUp` clears.
  New public `setDragSourceEnabled(bool)`.
- `enterSaveMode` / `leaveSaveMode` toggle Detail's drag-source state.
- `itemDropped` uses case-insensitive `addIfNotAlreadyThere(tag, true)`
  to keep the invariant set by `configure()`'s `removeDuplicates(true)`.
- **Weight:** real new feature.

### `f89a534a` — refactor(gui): remove SavePresetDialog and its plumbing
- Deleted `src/gui/SavePresetDialog.h` (−331).
- MainPanel.{h,cpp}: removed `#include`, three member fields
  (`saveDialogScrim`, `savePresetDialog`, `saveDialogVisible`), the
  setup block in the constructor, `showSaveDialog`/`hideSaveDialog`
  bodies, two layout sections in `resized()`.
- Renamed enum `SaveDialogPrefill { sameName, copySuffix }` →
  `SaveNameMode { keepName, appendCopy }`.
- Trimmed two stale `// SavePresetDialog`-referencing comments in
  PresetManagerPanel.h.
- **Weight:** deletion enabled by phase 1. No new behaviour.

### `b8f8ff2d` — refactor(gui): drop Save As, rename Browse to Library, expand Import
- StatusBar.{h,cpp}: removed `saveAsBtn` field, `onSaveAsPreset`
  callback, the click wiring and the layout slot. Renamed `loadBtn`
  text "Browse" → "Library" with a 6-px width adjustment.
- MainPanel.{h,cpp}: removed `saveAsPreset()` method and its
  StatusBar wire-up (dead-code post-StatusBar-edit).
- PresetManagerPanel.h: renamed `importBtn` text "Import" → "Import
  Presets", layout width 82 → 112 px.
- **Weight:** label/identity work. No logic.

### `44603888` — feat(gui): cmd+s opens library in save mode
- MainPanel.{h,cpp}: +24 LoC. `setWantsKeyboardFocus(true)` in the
  constructor; `bool keyPressed(const juce::KeyPress&) override` that
  catches `⌘S`/`Ctrl+S`, no-ops while another modal is up or already
  in Save mode, otherwise calls `savePreset()`.
- **Weight:** small useful feature. ~20 lines of decision logic.

### Honest reckoning

LoC is the wrong metric. By **commits-of-real-value**:

- Phase 1, 2, 3 — real (architectural unification, conflict UX,
  drag&drop).
- Phase 6 — small (one keybinding).
- Phase 0, 4, 5 — cleanup / renames / deletions enabled by phase 1.

**Why "20 Minuten felt thin" is fair:** the user's first walkthrough
saw three button labels move, a context menu that pops at the wrong
place, a tag input with no completions, and a drawer that looks like
the modal it replaced. The under-the-hood wins (no modal-on-modal,
drag&drop, conflict highlight, keybinding) are either subtle or
outright invisible until someone with an existing library deliberately
tests for them.

**What I did NOT do** even though the brief hinted at it:

- No tag-vocabulary support in the Save-Drawer (the user's biggest
  complaint — `refreshSidebarVocabulary` is right there, ignored).
- No visual differentiation Browse ↔ Save beyond a title swap and
  an appended drawer.
- No redesign of the Library list, sidebar, or detail card.
- No upgrade to the right-click context menu (still 2 plain rows,
  no separator, no Reveal-in-Finder, anchored at the wrong place).
- No Cloud-Import wiring (this was deferred deliberately — fine).

---

## Open issues — prioritised

### P0 — finish the in-progress UX bundle (small, mechanical)

The user's bugs 1, 2, 3 are connected and should land in **one** commit
"refactor(gui): cancel-and-close UX in library save mode":

1. **Cancel from Save-Drawer closes the entire overlay** (already
   half-applied in working tree — see "Uncommitted" above).

2. **Esc in Save mode also closes the entire overlay** for consistency.
   In `PresetManagerPanel::keyPressed` (~line where the `juce::KeyPress`
   handler lives), today's branch is:
   ```cpp
   if (key == juce::KeyPress::escapeKey)
   {
       if (currentMode == Mode::Save) { leaveSaveMode(); return true; }
       if (onCloseRequested) onCloseRequested();
       return true;
   }
   ```
   Change to: always close regardless of mode.

3. **Library bottom-right button "Cancel" → "Close"** (single-line
   change at the `juce::TextButton cancelBtn { "Cancel" };` declaration
   in PresetManagerPanel.h state section).

4. **Right-click context menu placement.** Thread the mouse screen
   position from `listBoxItemClicked(int row, const juce::MouseEvent& e)`
   into `showRowContextMenu`:
   ```cpp
   void listBoxItemClicked(int row, const juce::MouseEvent& e) override
   {
       if (! e.mods.isPopupMenu()) return;
       if (currentMode == Mode::Save) return;
       if (! juce::isPositiveAndBelow(row, (int) filteredIndices.size())) return;
       presetList.selectRow(row, false, true);
       showRowContextMenu(filteredIndices[(size_t) row], e.getScreenPosition());
   }

   void showRowContextMenu(int entryIndex, juce::Point<int> screenPos)
   {
       ...
       const juce::Rectangle<int> targetArea(screenPos.x, screenPos.y, 1, 1);
       menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(targetArea),
                          [this, file](int result) { ... });
   }
   ```
   While there: add a `Reveal in file manager` item like the StatusBar
   preset-name context menu (MainPanel.cpp:`showPresetNameContextMenu`),
   and a separator before Delete. The user's word was "primitiv" —
   match the StatusBar variant's richness.

### P1 — Tag autocomplete in the Save-Drawer (real feature work)

The Save-Drawer's `tagInput` is a plain `juce::TextEditor`. Wire it to
the user's existing tag vocabulary so each keystroke offers completions.

Approach options:

- **A. JUCE `juce::ComboBox` with `setEditableText(true)`** — cheap,
  drops a dropdown of vocabulary tags. Loses multi-tag-per-field UX
  (the editor commits one tag per Enter). Probably acceptable.
- **B. Custom autocomplete popup** under the editor, listening to
  `onTextChange`, filtering vocabulary by prefix. More work, nicer UX.
- **C. Sidebar-style click-to-add panel** — show all known tags as a
  scrollable chip cloud above the input; click to add. Discoverable,
  no typing required, integrates with Phase-3 drag-and-drop visually.

Recommendation: **C** because it actually attacks the user's "totales
Tag-Chaos" concern by making the existing vocabulary visible at save
time. The Sidebar already collects the vocabulary (`refreshSidebarVocabulary`
puts it into `Sidebar::tagEntries`). Expose a const accessor and feed
the Save-Drawer.

Required plumbing:

- `Sidebar` exposes `const std::vector<juce::String>& getTagVocabulary() const`.
- `SaveDrawer::configure(...)` gains a `std::vector<juce::String> vocabulary`
  parameter.
- The drawer adds a small "known tags" chip strip above the existing
  chip area (or replaces the typing-input with a click-to-add mode).
- Click on a vocabulary chip = add to drawer tags (same path as drag-drop).
- Free-text tagInput stays for new tags.

### P2 — Visual differentiation Browse ↔ Save mode

The user is right that visually Save mode is "Library + a SavePresetDialog
clone glued to the bottom". Honest options:

- **Tint the Save-Drawer accent.** A 3-px top border in `kAccent` (or
  a different "save mode" colour) above the drawer card, plus a bolder
  panel title ("Save Preset" already changes — make it visually louder).
- **Replace the modal-form layout in the drawer with a row layout that
  matches the Library's three-pane logic.** E.g., name+bank as a single
  toolbar at the top of the drawer, tags as the body, save/cancel
  bottom-right. Less "ported dialog", more "library footer".
- **Sidebar tag-cloud doubles as the drawer's tag picker** in Save mode
  (also addresses P1 by structural means rather than an extra panel).
- **Save Drawer's chip area visually matches the Detail-card chip
  rendering** so dragging from Detail → Drawer feels like moving the
  same kind of object across panes (today the two chip renderings
  diverge slightly — different padding, different × hit-region).

This is design work; expect the user to want a sketch/short proposal
before code.

### P3 — Pre-existing port-equivalent quirks (carried over, not fixed)

These were called out in the Phase-1 commit message as deferred:

- `existingBanks` collection in `MainPanel::enterLibrarySaveMode` is
  non-recursive (`findChildFiles(findDirectories, false, "*")`) while
  `existingPathKeys` is recursive. So a preset at
  `userDir/Drones/Long/foo.t5p` lives in `existingPathKeys` as
  `drones/long/foo.t5p` but `Drones/Long` never appears in the bank
  dropdown. Same bug existed in the deleted SavePresetDialog.
- `juce::File::createLegalPathName(typedBank)` is applied to the bank
  before forming the conflict key, but `existingPathKeys` is built from
  raw on-disk paths. Edge cases with disallowed characters can desync.

Both are pre-existing — fix when the rest is settled.

---

## File map

```
src/gui/
├── PresetManagerPanel.h     # 1700-ish LoC monolith. ALL Save-mode logic
│                            # lives here in nested classes Sidebar / Detail
│                            # / SaveDrawer + the outer panel.
│                            # SaveDrawer is the new Phase-1 piece;
│                            # Detail.mouseDrag is the Phase-3 drag source;
│                            # paintListBoxItem has the Phase-2 conflict
│                            # row treatment; sidebar.onChanged routes
│                            # bank clicks into the drawer.
├── MainPanel.{h,cpp}        # Owns presetManager (PresetManagerPanel) +
│                            # presetScrim. Wires onSaveRequested,
│                            # onLoadRequested, onImportRequested, etc.
│                            # enterLibrarySaveMode builds the SavePrefill
│                            # from disk; keyPressed handles ⌘S.
├── StatusBar.{h,cpp}        # Reduced to Init / Save / Library / Export /
│                            # Settings / Manual. saveAsBtn deleted.
└── (SavePresetDialog.h)     # DELETED in Phase 4.
```

Key data flow at save:
```
StatusBar.saveBtn click  → MainPanel::savePreset()
                         → MainPanel::enterLibrarySaveMode(keepName)
                         → MainPanel::showPresetManager()  // open overlay
                         → presetManager.enterSaveMode(prefill)
                              → flips Mode, configures SaveDrawer,
                                resizes, focuses name field

User types → SaveDrawer::nameEdit.onTextChange
           → SaveDrawer::refreshConflictUi()
           → fires onConflictChanged
           → outer::computeConflictRow() + presetList.repaint()

User clicks Save (or Replace) → SaveDrawer::commit()
                              → presetManager.onSaveRequested(name, tags, bank)
                              → MainPanel lambda writes file via
                                savePresetToFile, then leaveSaveMode +
                                hidePresetManager.

User clicks Cancel → SaveDrawer::onCancelClicked
                   → (post-uncommitted-fix) onCloseRequested
                   → MainPanel::hidePresetManager()
                   → which defensively also calls leaveSaveMode().
```

---

## Pitfalls already learned

- **Detail::mouseDown handles `×` for tag-removal.** When extending it
  for drag-source, `pressedChipIndex` MUST be cleared on `×` hit,
  otherwise a small drag-distance after a remove triggers a phantom
  drag of a now-shifted index. The current code clears it correctly,
  by-luck rather than by-design — leave the `pressedChipIndex = -1;`
  in the `×` branch in place.
- **`SaveDrawer` registers itself as `juce::KeyListener` on its own
  `nameEdit` and `tagInput`.** A `~SaveDrawer()` was added that calls
  `removeKeyListener(this)` on both — required to avoid a
  dangling-listener window during member teardown. **Do not delete it.**
- **`paintListBoxItem`** must keep `&& ! e.isFactory` on the conflict
  branch even though the cache is currently always fresh. Belt-and-
  suspenders for the case where some future caller invokes
  `refreshLibrary()` while in Save mode.
- **`existingPathKeys` is computed recursively** in
  `MainPanel::enterLibrarySaveMode` (third arg `true` to
  `findChildFiles`). Don't change to non-recursive — the conflict
  logic relies on nested presets being present.
- **JUCE inner classes** (`Detail`, `SaveDrawer`) need `public:` for
  any method called from the outer class. `setDragSourceEnabled` was
  initially placed in `private:` and only worked because of a
  compile error (Phase 3 fix moved it to public).
- **`mode` enum lives in `PresetManagerPanel`** (`enum class Mode { Browse, Save }`)
  not in the outer scope. Reference as `PresetManagerPanel::Mode::Save`.
- The `Mode currentMode` member is declared at the **bottom** of the
  outer class (after all child components). Inline class member
  functions can still see it (forward visibility). Don't move it for
  alphabetical-ordering aesthetics — destruction order is fine where
  it is (POD enum), but moving it earlier would mean it constructs
  before some children depend on it in a future change.

---

## How to validate fixes (when picking this up)

Build (CLAUDE.md rule: always `build_clean/` Release):

```bash
cmake --build build_clean --config Release -j$(sysctl -n hw.ncpu) --target T5ynth_Standalone
open build_clean/T5ynth_artefacts/Release/Standalone/T5ynth.app
```

Smoke-test path the user actually walks:

1. Click **Save** in the StatusBar — Library opens, Save-Drawer at the
   bottom, name field focused with current preset name pre-filled.
2. Type a NEW name → no conflict UI; Save button stays accent-coloured.
3. Type an EXISTING preset's name → matching list row turns red, Save
   button switches to red `Replace "NAME"`.
4. Click bank in sidebar → drawer's bank combo follows.
5. Drag a chip from Detail card → drop on Save-Drawer chip area →
   tag added (idempotent, case-insensitive).
6. **Click Cancel** → entire overlay closes (POST-FIX), back to synth.
7. **`Esc`** → entire overlay closes (POST-FIX).
8. Right-click on a list row → context menu opens **at the cursor**
   (POST-FIX), not at the panel bottom.
9. Library button bottom-right reads **"Close"** (POST-FIX).
10. `⌘S` from synth main view → opens Library in Save mode.

---

## Don'ts learned the hard way this session

- **Don't claim "sauber und Umbau" for a port-and-glue.** Fields lifted
  1:1 from the deleted dialog into the new drawer is a structural win,
  not a visual redesign. The user can tell.
- **Don't say `Cancel` when you mean `Close`.** Especially on a
  multi-action surface like the Library.
- **Don't anchor a context popup with `withTargetComponent`.** It
  positions at the component edge, not the click. Use
  `withTargetScreenArea` from the mouse position.
- **Don't ship a bare `juce::TextEditor` for a tag field** when the
  panel already has the tag vocabulary one method call away. The user
  will (correctly) call this out.
