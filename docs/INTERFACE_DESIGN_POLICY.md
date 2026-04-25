# T5ynth Interface Design Policy

This policy is the default for all T5ynth UI work. The synth is a dense
instrument panel, so readability has priority over decorative minimalism.

## Text Roles

- Primary text: current values, selected items, action labels, warnings.
  Use `kTextPrimary` or white when the element is active.
- Secondary text: control labels such as BPM, Gate, Shuffle, Cutoff, Rate,
  Range, Oct, Vol, section metadata, and unselected but available controls.
  Use `kTextSecondary`. Do not use near-black grey for readable labels.
- Muted text: placeholders, inactive hints, empty states, disabled indicators.
  Use `kTextMuted` or `kTextDisabled`, and only where low priority is clear.
- Accent text: numeric values may use the module color when that helps connect
  the readout to its slider or control group.

## Minimum Sizes

- Parameter labels and value readouts must not render below 11 px.
- Buttons, ComboBox text, and ToggleButton labels must not render below 11 px.
- Section headers should stay visually compact, but their text must remain
  readable at the minimum plugin size.
- Micro text below 11 px is allowed only inside fixed symbolic handles or
  meters where the glyph is not the main way to identify the control.

## Contrast

- Labels must be readable against `kCard`, `kSurface`, and `kBg` without
  relying on hover or selection.
- A disabled state may reduce alpha, but the base color should still be drawn
  from the shared text tokens.
- Module color is for tracks, selected controls, and value emphasis. It should
  not replace legible neutral label color.

## Layout

- Horizontal parameter rows use `SliderRow` unless there is a strong reason not
  to. That keeps label, slider, and value sizing consistent across oscillator,
  filter, modulation, sequencer, and FX panels.
- Switchboxes should use the global TextButton font from `T5ynthLookAndFeel`.
  Do not let switchbox labels visually overpower adjacent parameter labels.
- If a row becomes too narrow, collapse optional controls into an overflow menu
  before shrinking labels below the minimum size.

## Implementation Rules

- Use the shared tokens in `src/gui/GuiHelpers.h`: `kTextPrimary`,
  `kTextSecondary`, `kTextMuted`, `kTextDisabled`, `kUiLabelFontMin`,
  `kUiValueFontMin`, and `kUiControlFontMin`.
- Do not introduce raw greys such as `0xff606060` or `0xff888888` for label
  text in active UI.
- New parameter controls should inherit `SliderRow` defaults instead of setting
  ad hoc grey label colors or custom tiny font sizes.
