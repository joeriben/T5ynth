#pragma once
#include <cstdint>

/**
 * Internal note event — the typed hand-off between the in-process note
 * sources (step sequencer, generative sequencer, arpeggiator) and the
 * VoiceManager.
 *
 * MIDI is an EXTERNAL wire protocol; it is not used to communicate between
 * modules inside this process. Routing intent (slide articulation, which
 * generative strand, stereo pan) travels here as typed fields — never encoded
 * in MIDI channel numbers. External controller input keeps its own
 * juce::MidiBuffer with real channels so MPE per-note expression works on the
 * full 1–16 range without colliding with anything internal.
 */
struct VoiceEvent
{
    enum class Type         : uint8_t { NoteOn, NoteOff };
    // Step-sequencer slide handling (TB-303 convention):
    //   Normal — fresh note, retrigger envelopes
    //   Bind   — continue the live voice, instant pitch change (glide 0)
    //   Glide  — continue the live voice, ramped pitch change
    enum class Articulation : uint8_t { Normal, Bind, Glide };

    int          sampleOffset = 0;                     // position within the block
    Type         type         = Type::NoteOn;
    int          note         = 60;
    float        velocity     = 0.8f;                  // 0..1 (NoteOn only)
    Articulation artic        = Articulation::Normal;
    int          strandId     = -1;                    // gen-seq strand id, else -1
    float        pan          = 0.0f;                  // gen-seq strand pan (-1..1)
    // 0 for everything a sequencer or the arpeggiator plays — those ARE internal
    // notes and must never be MPE-tracked. Non-zero only where an EXTERNAL key is
    // handed back to the voices through this stream (the arpeggiator's off-edge):
    // dropping its channel there would file that voice under "internal, channel 0",
    // the exact bucket a step-seq slide is allowed to hijack (VoiceManager's
    // originMatches guard), which would strand the key's voice until a panic.
    int          mpeChannel   = 0;                     // 1-16 = external controller channel
};
