# fm3 — the A/B for the ear

All files: A3 = 220 Hz, 5 s, matched loudness. Rendered 2026-07-30 from
`backend/lco_library.json` at commit 872f7ca6.

## The comparison the rule asks for

- `nearest_existing.wav` — the `fm` entry, plain 2-operator FM. The entry itself names
  this as what it was heard against.
- `fm3_found_default.wav` — fm3 exactly as it ships.

## The one thing to decide

The shipped default sounds **an octave below the key** — measured −1200 ct, exactly.
`ratio 3` at 5.5 is a half-integer, which with `ratio 2` at 1.0 puts the whole spectrum
on a grid of kfreq/2. The entry declares this in its own text rather than avoiding it.

The four files let you hear what moving off the half-integer costs:

- `fm3_ratio3_5.5_found.wav` — the found setting (an octave under)
- `fm3_ratio3_5_at_pitch.wav`
- `fm3_ratio3_6_at_pitch.wav`
- `fm3_ratio3_7_at_pitch.wav`

All three whole numbers measure +0 ct, i.e. they play the key.

## The axes, at the found setting otherwise

- `trade_*` — what fraction of the drive changes hands between the two modulators.
  This is the entry's distinguishing feature; two operators cannot do it.
- `index3_*` — the brightness axis.
- `onset_*` — how long the sidebands take to arrive. A spectral attack, not a loudness one.
