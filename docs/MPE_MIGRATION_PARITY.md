# MPE: moving the hand-written path onto JUCE's

This is the enumeration the project's Migration & Substrate Discipline requires
*before* the first line of the new implementation: every capability of the
existing hand-written MPE path, with the line it lives on, and for each one
either the proof that it survives on the library or the note that it needs
explicit preservation. Deleting the old code is the last step, not the first.

The old path was not a module with a boundary. It was a set of branches inside
`T5ynthProcessor::processBlock`'s MIDI walk plus the state they read, so the
list below is the boundary — there was nothing else to inherit.

**Outcome, stated up front.** `juce::MPEZoneLayout` took over the zone layout,
the MPE Configuration Message, the pitch-bend-range RPN, the per-channel RPN
detectors and the master/member classification — the part where the channel-16
bug lived. `juce::MPEInstrument`, the note tracker, did **not**, and the reason
is measured rather than argued: see §3. Per-note expression therefore stays
keyed by MIDI channel in `VoiceManager`, as before.

## 0. Where it lived

| Part | Location before |
| --- | --- |
| Zone-layout state and `isMpeMasterChannel` | `PluginProcessor.h:963-1013` |
| Pitch-bend ranges (master / per-note) | `PluginProcessor.h:949-961` |
| RPN selection state (one pair, global) | `PluginProcessor.h:952-953` |
| Poly aftertouch → per-note pressure | `PluginProcessor.cpp:4851` |
| Channel pressure → zone-wide or per-note | `PluginProcessor.cpp:4857` |
| Pitch wheel → global or per-note | `PluginProcessor.cpp:4869` |
| RPN 0 → bend ranges | `PluginProcessor.cpp:4936` |
| RPN 6 → zone layout (MCM) | `PluginProcessor.cpp:4968` |
| CC74 → per-note timbre | `PluginProcessor.cpp:5026` |
| RPN deselect on Reset All Controllers | `PluginProcessor.cpp:5194` |
| Channel tagging on note-on | `dsp/VoiceManager.cpp:147-149` |
| Channel-keyed routing of the three dimensions | `dsp/VoiceManager.cpp:517-556` |
| Tag cleared when a voice goes idle | `dsp/VoiceManager.cpp:657` |

## 1. Capabilities, one per row

**Survives** = the library does this, and does it the same way.
**Preserve** = the library would do something else, or does not reach this far;
the new code keeps the old behaviour deliberately, and the reason is given.

| # | Capability | Disposition |
| --- | --- | --- |
| 1 | Lower zone: master ch1, members 2..1+N; upper zone: master ch16, members 16-N..15 | Survives — `MPEZone::isUsingChannelAsMemberChannel`, the code the old comment already cited |
| 2 | Channel 16 is a master only where an upper zone was declared (the `03286a97` fix) | Survives, and structurally: one zone object, so the call sites cannot disagree |
| 3 | MCM (RPN 6) on ch1/ch16 sets a zone; 0 switches it off; ≥16 is discarded, not clamped | Survives — `MPEZoneLayout::processZoneLayoutRpnMessage`, `rpn.value < 16` |
| 4 | Declaring one zone shrinks the other where they would overlap | Survives — `MPEZoneLayout::setZone` |
| 5 | A layout survives `prepareToPlay`, preset load, panic and Reset All Controllers | Preserve — the layout is a member and nothing resets it; it stays out of the preset |
| 6 | RPN 0 on a member channel sets the per-note bend range | Survives — `processPitchbendRangeRpnMessage` |
| 7 | RPN 0 on a **master** channel sets the master range **and mirrors it to the per-note range** (a LinnStrument transmits Bend Range only there) | **Preserve** — JUCE sets the master range alone. Without the mirror, a LinnStrument's member notes bend at the zone default instead of the range the player set |
| 8 | The per-note range starts at ±24, not the spec's ±48 (a LinnStrument maxes at ±24, so ±48 over-bends it) | **Preserve** |
| 8a | There is ONE range pair for the whole instrument, and a zone declaration does not disturb it | **Preserve** — and this is where the library and the synth genuinely part company. `MPEZoneLayout` keeps a pair per zone and resets both to the spec's defaults on every zone declaration, because an MCM carries no range of its own. Reading the ranges back out of it would therefore reset a range the player had already dialled in, so the pair stays this synth's (`mpePerNoteBendRangeInForce_`, `mpeMasterBendRangeInForce_`) and only the layout and the parsing are the library's |
| 9 | Master bend range starts at ±2 | Survives — JUCE's default too |
| 10 | Pitch wheel on ch1 is a **global** bend: it moves sequencer and arp voices as well, which have no MPE note at all | Unchanged |
| 11 | Pitch wheel on ch16 is a member bend even where an upper zone exists | Unchanged — deliberate, documented at the call site |
| 12 | Channel pressure on a master channel is zone-wide, on a member channel per-note | Unchanged routing; the master/member question is the zone's to answer now |
| 13 | A voice's pressure is `max(per-note, zone-wide)`, so master and member compose instead of clobbering | Unchanged — `VoiceManager::pressureForVoice`, below the library's level |
| 14 | Poly key pressure matches by note number across external voices | Unchanged |
| 15 | CC74 on a member channel is per-note timbre | Unchanged |
| 16 | CC74 on a master channel is **not** timbre: it stays the control-surface default that drives Scan (`kExtMap`, `midi/LaunchControlXLLeds.h:235`) | Unchanged — and it is why `MPEInstrument` could not simply be handed the stream: it reads CC74 on the master channel as zone-wide timbre |
| 17 | CC70 is `seq_steps`; CC102/CC106 are not MPE LSBs, so timbre stays 7-bit | Unchanged — `MPEInstrument` maps all three |
| 18 | Sustain (CC64) and sostenuto (CC66) are the synth's, held in `VoiceManager` | Unchanged — `MPEInstrument` would hold notes too, and two owners of note lifetime is a stuck-note bug |
| 19 | In XL DAW mode, channel 16 is the encoder/fader channel and is not musical: no notes, no CC6, no CC100 | **Preserve** — the guard moved to the feed site, where it also now covers CC101. The XL does not send CC101, so nothing changes in practice; the widening is so that "in DAW mode channel 16 is not a MIDI control channel for us" holds without exception |
| 20 | After an MCM, the next CC6 must not be swallowed as another one (a bound fader would rewrite the zone from its position) | **Preserve** — JUCE's detector latches the selected RPN per channel exactly as the old global pair did. Done through the library: `deselectMpeRpn` hands it the CC100=127 a controller would send, which is the old "LSB only" deselect expressed as MIDI |
| 21 | The RPN selection was ONE global pair where the MIDI spec has sixteen (the old code's own comment named this as a defect) | **Improves** — `MidiRPNDetector` holds sixteen states |
| 22 | A CC6 that completes no RPN still reaches a user binding | **Preserve** — an else-if cannot both consume a message and decline it, so `handleMpeRpnByte` returns whether it took the byte and the branch is chosen from that |
| 23 | An **arpeggiated** note is an internal note: channel 0, never MPE-tracked, whatever channel the key arrived on (`dsp/VoiceEvent.h:32-38`) | Unchanged |
| 23a | Switching the arpeggiator **off** hands the still-held keys back **with their MPE channel intact** (`PluginProcessor.cpp:3851-3862`) | Unchanged — and the reason note IDs would have been expensive: while the arp is on it *consumes* the note-ons, so a note tracker would have had to be fed from a second place |
| 24 | `voiceMidiChannel_` also discriminates origin: a step-seq slide must not continue a held external note | Unchanged — this is not MPE routing and must not be replaced by a note ID |
| 25 | A voice's MPE tag is cleared when it goes idle | Unchanged |
| 26 | Expression is applied at the event's sample position within the block, not at block start | Unchanged — the feed sits inside the existing sample-accurate walk |
| 27 | A note starts at the synth's neutral timbre (`64/127`) and zero pressure, ignoring values received on that channel before the note | Unchanged — `MPEInstrument` would apply the channel's last-received value as the note's initial value. Arguably better, and deliberately not adopted: it changes how the instrument sounds, which is not this task's licence |

## 2. What the library refuses that the old code allowed

`MPEInstrument::noteOn` returns early unless the channel belongs to a zone
(`juce_MPEInstrument.cpp:354`), and the same rule governs which channels
`MPEZoneLayout` treats as members. The default layout has no zones at all. The
old code needed no declaration: it routed per-note expression on any non-master
channel whether or not an MCM had ever arrived.

So the new path declares a **lower zone with 15 members** at construction —
master ch1, members ch2..16, per-note range 24, master range 2. That is the
layout every host defaults to, it makes channel 16 a member (capability 2), and
an ordinary MIDI keyboard on channel 1 is a note on the zone master.

A channel outside every declared zone keeps its bend rather than losing it:
`mpePerNoteBendRange` falls back to the lower zone's range, which is what the
single hand-written range used to give it.

## 3. Why `juce::MPEInstrument` is not here

Measured, not argued — `tools/measure_mpe_instrument_rt.cpp`:

```
juce::Array<MPENote> -- 50 remove/add cycles at 16 held
  storage buffer moved                  : 0 times  (no reallocation)
  one note on/off, 50 times             : 2 moves
  a  4-note chord down and up, 50 times : 150 moves  <-- REALLOCATES WHILE PLAYING
  a 10-note chord down and up, 50 times : 250 moves  <-- REALLOCATES WHILE PLAYING
  a 15-note chord down and up, 50 times : 250 moves  <-- REALLOCATES WHILE PLAYING
```

`MPEInstrument` holds its live notes in a `juce::Array<MPENote>`, and
`Array::remove` calls `minimiseStorageAfterRemoval` (`juce_Array.h:1120-1130`)
on every single removal. So the storage shrinks as a chord is released and has
to grow again when the next one is played: five reallocations per chord cycle,
for as long as the player plays. A warm-up does not help, because the shrink
happens on release. `MPEInstrument` would have to be fed from `processBlock`,
and the project forbids allocating on the audio thread — so it stays out.

The counters used to reach that conclusion are worth naming, because the first
two could not see it. `operator new` misses it (`juce::Array` goes through
`HeapBlock`, i.e. `std::malloc`), and the malloc-zone block count misses it too
(a grow is free-then-alloc, which nets to zero live blocks). Watching the
storage pointer move is what shows it.

What ships instead measures clean, on the shipped path and not just the library
in isolation:

```
juce::MPEZoneLayout alone -- RPN traffic x300 : new=0 (this thread)

T5ynthProcessor::processBlock -- 200 blocks
  baseline, one held note, no MIDI       : new=0 (this thread)
  the same, plus 81 MPE messages a block : new=0 (this thread)
```

The allocation counter is filtered to the measuring thread on purpose: the
processor starts an event-log writer and JUCE a message thread, and both
allocate throughout, so a process-wide count is noise.

## 4. What was given up by not taking the note tracker

Note IDs would buy one thing the channel key cannot: correct behaviour when a
controller reuses a member channel while the previous note on it is still
releasing. Today both notes follow that channel's expression. On a real MPE
controller the case is rare — one note per channel is the point of the format —
and it costs a released note a little extra bend, not a wrong pitch on a held
one. That is the whole of the difference.

## 5. The gate

`tools/test_mpe_parity.cpp` is the frozen corpus: 52 assertions driven as raw
MIDI through the real `T5ynthProcessor::processBlock`, reading the result off
the voices. It was written against the hand-written code and was green on it
before the library was introduced — that is what makes it a record of the old
path's capability rather than a description of the new one, and it is why it
could not simply be regenerated afterwards.

It is mutation-checked in both directions, because a suite that cannot fail
certifies nothing: reinstating the pre-`03286a97` channel-16 bug fails cases 4
and 6; the spec's ±48 per-note range instead of ±24 fails 2, 17 and 19;
dropping the master-to-member mirror fails 9; dropping the RPN deselect fails 7.

Writing it corrected this enumeration twice. Row 23, where the arpeggiator
turned out to do the opposite of what was written down here first. And case 20,
which is a capability the corpus did not originally cover and the first cut of
the new code therefore lost: a controller that transmits its bend range BEFORE
declaring its zone had that range reset by the declaration. It was added after
the fact, so it was checked against the hand-written code as well before being
trusted — a case only the new implementation has ever passed is not evidence.

Not reachable from an offline harness, and stated in the tool rather than
skipped quietly: the Launch Control XL DAW-mode exemptions, because
`dawModeActive_` is only set when a real XL output device is opened.
