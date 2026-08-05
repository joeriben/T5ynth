# MPE: what the hand-rolled path does, and what happens to each part on `juce::MPEInstrument`

This is the enumeration the project's Migration & Substrate Discipline requires
*before* the first line of the new implementation: every capability of the
existing hand-written MPE path, with the line it lives on, and for each one
either the proof that it survives on the library or the note that it needs
explicit preservation. Deleting the old code is the last step, not the first.

The old path is not a module with a boundary. It is a set of branches inside
`T5ynthProcessor::processBlock`'s MIDI walk plus the state they read, so the
list below is the boundary — there is nothing else to inherit.

## 0. Where it lives

| Part | Location |
| --- | --- |
| Zone-layout state and `isMpeMasterChannel` | `src/PluginProcessor.h:958-1007` |
| Pitch-bend ranges (master / per-note) | `src/PluginProcessor.h:942-954` |
| RPN selection state (one pair, global) | `src/PluginProcessor.h:940-941` |
| Poly aftertouch → per-note pressure | `src/PluginProcessor.cpp:4851` |
| Channel pressure → zone-wide or per-note | `src/PluginProcessor.cpp:4857` |
| Pitch wheel → global or per-note | `src/PluginProcessor.cpp:4869` |
| RPN 0 → bend ranges | `src/PluginProcessor.cpp:4936` |
| RPN 6 → zone layout (MCM) | `src/PluginProcessor.cpp:4968` |
| CC74 → per-note timbre | `src/PluginProcessor.cpp:5026` |
| RPN deselect on Reset All Controllers | `src/PluginProcessor.cpp:5194` |
| Channel tagging on note-on | `src/dsp/VoiceManager.cpp:147-149` |
| Channel-keyed routing of the three dimensions | `src/dsp/VoiceManager.cpp:517-556` |
| Tag cleared when a voice goes idle | `src/dsp/VoiceManager.cpp:657` |

## 1. Capabilities, one per row

**Survives** = the library does this, and does it the same way.
**Preserve** = the library would do something else; the new code has to keep
the old behaviour deliberately, and the reason is given.

| # | Capability | Disposition |
| --- | --- | --- |
| 1 | Lower zone: master ch1, members 2..1+N; upper zone: master ch16, members 16-N..15 | Survives — `MPEZone::isUsingChannelAsMemberChannel`, the code the old comment already cited |
| 2 | Channel 16 is a master only where an upper zone was declared (the `03286a97` fix) | Survives, and structurally: one `isMasterChannel`, so the call sites cannot disagree |
| 3 | MCM (RPN 6) on ch1/ch16 sets a zone; value 0 switches it off; ≥16 is discarded, not clamped | Survives — `MPEZoneLayout::processZoneLayoutRpnMessage`, `rpn.value < 16` |
| 4 | Declaring one zone shrinks the other where they would overlap | Survives — `MPEZoneLayout::setZone` |
| 5 | A layout survives `prepareToPlay`, preset load, panic and Reset All Controllers | Preserve — the layout is a member and nothing resets it; keep it that way, and keep it out of the preset |
| 6 | RPN 0 on a member channel sets the per-note bend range | Survives — `processPitchbendRangeRpnMessage` |
| 7 | RPN 0 on a **master** channel sets the master range **and mirrors it to the per-note range** (LinnStrument transmits Bend Range only there) | **Preserve** — JUCE sets the master range alone. Without the mirror, a LinnStrument's member notes run at the zone default instead of the range the player set |
| 8 | Per-note bend range starts at ±24, not the spec's ±48 (a LinnStrument maxes at ±24, so ±48 over-bends it) | **Preserve** — pass it as the zone's `perNotePitchbendRange` instead of taking JUCE's 48 |
| 9 | Master bend range starts at ±2 | Survives — that is JUCE's default too |
| 10 | Pitch wheel on ch1 is a **global** bend: it moves sequencer and arp voices as well, which have no MPE note at all | **Preserve** — JUCE folds the master bend into each note's `totalPitchbendInSemitones`, which by construction only reaches notes. Keep the global path and take only the per-note component from the library |
| 11 | Pitch wheel on ch16 is a member bend even where an upper zone exists | **Preserve** — deliberate, and documented as such at `PluginProcessor.cpp:4878` |
| 12 | Channel pressure on a master channel is zone-wide, on a member channel per-note | Survives — `updateDimensionMaster` vs `updateDimensionForNote` |
| 13 | A voice's pressure is `max(per-note, zone-wide)`, so master and member compose instead of clobbering | **Preserve** — lives in `VoiceManager::pressureForVoice`, below the library's level. Untouched |
| 14 | Poly key pressure matches by note number across external voices | **Preserve** — `MPEInstrument` routes poly aftertouch per note, which is the same thing when notes are unique, and `VoiceManager::setPolyPressure` already keys by note. Left alone |
| 15 | CC74 on a member channel is per-note timbre | Survives — `handleTimbreMSB` |
| 16 | CC74 on a master channel is **not** timbre: it stays the control-surface default that drives Scan (`kExtMap`, `LaunchControlXLLeds.h:235`) | **Preserve** — JUCE would make it zone-wide timbre. Filter it out before the instrument sees it |
| 17 | CC70 is `seq_steps`, not MPE pressure; CC102/CC106 are not MPE LSBs | **Preserve** — JUCE maps all four. Filter them out. Timbre therefore stays 7-bit, as today |
| 18 | Sustain (CC64) and sostenuto (CC66) are the synth's, held in `VoiceManager` | **Preserve** — `MPEInstrument` would hold notes too, and two owners of note lifetime is a stuck-note bug. Filter them out |
| 19 | In XL DAW mode, channel 16 is the encoder/fader channel and is not musical at all: no notes, no CC6, no CC100 | **Preserve** — filter the whole channel out before the instrument, exactly where the four existing guards sit |
| 20 | After an MCM, the next CC6 must not be swallowed as another MCM (a bound fader would rewrite the zone from its position) | **Preserve** — JUCE's detector latches the selected RPN per channel just as the old global pair did. Achieved through the library: feed it CC100=127 after a zone change, which is the old "LSB only" deselect expressed as MIDI |
| 21 | The RPN selection is per channel, not one global pair (the old code's own comment names this as a defect) | **Improves** — `MidiRPNDetector` holds sixteen states. Nothing to preserve; the defect goes away |
| 22 | Internal notes (sequencer, arp, drone) are channel 0 and are never MPE-tracked | Survives — they never enter the MIDI stream at all; they are typed `VoiceEvent`s |
| 23 | An **arpeggiated** note is an internal note: channel 0, never MPE-tracked, whatever channel the key arrived on (`VoiceEvent.h:32-38`) | Survives — no note ID is wanted here, and none is given |
| 23a | Switching the arpeggiator **off** hands the still-held keys back to the voices **with their MPE channel intact**, so the key keeps per-note expression and stays out of the channel-0 bucket a step-seq slide may hijack (`PluginProcessor.cpp:3851-3862`) | **Preserve** — and it forces a second feed point: while the arp is on it *consumes* the note-ons (`PluginProcessor.cpp:4266`), so the instrument must be fed there too or the handed-back key has no MPE note to be found under |
| 24 | `voiceMidiChannel_` also discriminates origin: a step-seq slide must not continue a held external note | **Preserve** — this is not MPE routing and must not be replaced by a note ID. Both fields coexist |
| 25 | A voice's MPE tag is cleared when it goes idle | Survives — same place, now clearing the note ID as well |
| 26 | Expression is applied at the event's sample position within the block, not at block start | **Preserve** — feed the instrument inside the existing sample-accurate walk, never as a whole-buffer pass |
| 27 | A note starts at the synth's neutral timbre (`64/127`) and zero pressure, ignoring values received on that channel before the note | **Preserve** — JUCE would apply the channel's last-received value as the note's initial value. That is arguably better and is *not* adopted here: it changes how the instrument sounds, which is not this task's licence |

## 2. What the library refuses that the old code allowed

`MPEInstrument::noteOn` returns early unless the channel belongs to a zone
(`juce_MPEInstrument.cpp:354`), and the default layout has no zones at all. The
old code needed no declaration: it routed per-note expression on any non-master
channel whether or not an MCM had ever arrived, and plain MIDI on channel 1
always played.

So the new path declares a **lower zone with 15 members** at construction —
master ch1, members ch2..16, per-note range 24, master range 2. That is the
layout every host defaults to, it makes channel 16 a member (capability 2), and
it means an ordinary MIDI keyboard on channel 1 is a note on the zone master
and plays exactly as before. A real MPE controller's MCM replaces it.

Without that default the synth would go silent for every non-MPE keyboard, which
is the single largest risk in this migration and the reason it is written down
here rather than discovered later.

## 3. Real-time cost, and what is done about it

`MPEInstrument` takes a `CriticalSection` and holds its notes in a `juce::Array`
that grows. Both would land on the audio thread, and the project forbids
allocating or locking there.

- **The lock** stays uncontended because nothing but the audio thread ever
  touches the instrument. No UI read, no message-thread query.
- **The allocation** is removed by growing the array once during
  `prepareToPlay` — sixteen note-ons, then `releaseAllNotes()`. `Array::remove`
  does not release storage, so steady state never allocates again.

## 4. The gate

`tools/test_mpe_parity.cpp` is the frozen corpus: it drives raw MIDI through the
real `T5ynthProcessor::processBlock` and asserts the observable voice state for
every row above that has one. It was written against the hand-rolled code and
was green on it before the library was introduced — that is what makes it a
record of the old path's capability rather than a description of the new one.
The hand-rolled code is deleted only after the same corpus is green on the
library-backed path.
