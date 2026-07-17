# Csound Lexicon — Phase 3 tool vocabulary

Every key the deterministic assembler (`backend/csound_assembler.py`) accepts, one line each. The LLM (Phase 4, out of scope here) only ever points at these keys — it never authors Csound or DSP numbers.

## Families (`"tool"`, 12)

- **bell** — inharmonic strike bell — 8 stretched partials, per-partial decay into a quiet living bed
- **metal** — clangorous inharmonic metal bar — denser partial set than bell, faster decay
- **cymbal** — dense bright inharmonic plate wash — no strong fundamental, short decays
- **glass** — sparse high inharmonic partials, thin and very fast decay — a struck-glass crack
- **pluck** — karplus-ish plucked string — harmonic partials, high ones decaying much faster than the fundamental, no sustain bed
- **fm_bell** — true 2-operator FM bell — ratio-2 modulator, index envelope decaying from bright to plain
- **pad** — slow-crossing partial bed — 8 partials, gentle taper, continuous (no intrinsic decay)
- **organ_tone** — clean drawbar-ish partials {1,2,3,4,6,8} at typical registration levels
- **saw_stack** — truncated-Fourier sawtooth (16 harmonics, 1/h) — the canonical bright analogue waveform
- **square_stack** — truncated-Fourier square (8 odd harmonics, 1/h) — hollow, clarinet-adjacent
- **noise_wash** — dense golden-angle-spaced partial cluster reading as a filtered noise bed — deterministic, continuous
- **sub** — pure low sine reinforcement — a clean single partial, typically paired with a low register

## Characters (`"characters": [{"key", "amount"}]`, 6)

- **bright** — spectral tilt toward the upper partials
- **dark** — spectral tilt away from the upper partials
- **warm** — gentle low emphasis plus a touch of soft drive
- **harsh** — waveshaping drive — real intermodulation harmonics, the extreme upper-energy pole
- **glassy** — strong high-partial emphasis plus a fast-decay tilt (shortens upper-partial decay times)
- **metallic** — inharmonic ratio skew toward the bell/cymbal partial sets — a deterministic per-partial detune

## Motion (`"motion"`, per-layer or global, 4)

- **vibrato** — shared per-layer pitch LFO (rate/depth scale with amount) applied via cent() to every partial
- **evolve** — slow spectral crossfade within the layer — a low LFO tilts partial balance dark<->bright and back
- **shimmer** — detuned pair riding the fundamental — audible slow beating
- **breathe** — slow amplitude wave over the whole layer

## Envelope (`"envelope"`, 4)

- **strike** — fast attack, exponential decay down to a sustained bed level
- **swell** — slow attack, no transient — fades up into the held level
- **sustain** — near-instant onset, flat organ-like hold — no decay
- **decay_only** — fast attack, full decay to near-silence — no sustain bed (bell/pluck natural)

## Register (organ footage, per layer)

- **32'** = ratio 0.25
- **16'** = ratio 0.5
- **8'**  = ratio 1.0 (the played note)
- **4'**  = ratio 2.0
- **2'**  = ratio 4.0
- a plain numeric ratio (e.g. `1.5`) is also accepted verbatim.

## Layer cap

At most 4 layers per composition (CPU budget; see `tools/csound_orch_check.cpp`'s `bench` mode).

