#pragma once
#include <array>
#include <algorithm>
#include <cmath>

/**
 * Musical scale quantizer.
 *
 * Snaps a MIDI note number to the nearest degree of a selected scale,
 * preserving the octave. Non-destructive: works on output, not on stored data.
 */
namespace ScaleQuantizer
{

enum Scale
{
    // --- Western diatonic (indices 0-6 unchanged for backward compat) ---
    Off = 0,
    Major,           // Ionian
    Minor,           // Aeolian
    Pentatonic,      // Major pentatonic
    Dorian,
    HarmonicMinor,
    WholeTone,
    // --- Western modes & extensions ---
    Mixolydian,
    Phrygian,
    Lydian,
    Locrian,
    MinorPentatonic,
    Blues,
    MelodicMinor,
    // --- East Asian ---
    Hirajoshi,
    InSen,
    Iwato,
    Kumoi,
    Ryukyu,
    // --- Middle Eastern / South Asian ---
    Hijaz,           // Phrygian dominant
    DoubleHarmonic,  // Byzantine / Bhairav
    RagaTodi,
    RagaPurvi,
    Persian,
    // --- European extended ---
    HungarianMinor,
    NeapolitanMinor,
    // --- Southeast Asian (12-TET approximations) ---
    Pelog,
    Slendro,
    COUNT
};

// Scale degrees as semitone offsets from root (within one octave)
// Using 12-bit bitmasks for O(1) lookup: bit i set = semitone i is in scale
static constexpr std::array<int, COUNT> kScaleMasks = {
    0b111111111111,  // Off            — chromatic
    0b101010110101,  // Major          — 0 2 4 5 7 9 11
    0b010110101101,  // Minor          — 0 2 3 5 7 8 10
    0b001010010101,  // Pentatonic     — 0 2 4 7 9
    0b010101101101,  // Dorian         — 0 2 3 5 7 9 10
    0b100110101101,  // Harm.Min       — 0 2 3 5 7 8 11
    0b010101010101,  // WholeTone      — 0 2 4 6 8 10
    0b010110110101,  // Mixolydian     — 0 2 4 5 7 9 10
    0b010110101011,  // Phrygian       — 0 1 3 5 7 8 10
    0b101011010101,  // Lydian         — 0 2 4 6 7 9 11
    0b010101101011,  // Locrian        — 0 1 3 5 6 8 10
    0b010010101001,  // MinorPent      — 0 3 5 7 10
    0b010011101001,  // Blues          — 0 3 5 6 7 10
    0b101010101101,  // MelodicMin     — 0 2 3 5 7 9 11
    0b000110001101,  // Hirajoshi      — 0 2 3 7 8
    0b010010100011,  // In-sen         — 0 1 5 7 10
    0b010001100011,  // Iwato          — 0 1 5 6 10
    0b001010001101,  // Kumoi          — 0 2 3 7 9
    0b100010110001,  // Ryukyu         — 0 4 5 7 11
    0b010110110011,  // Hijaz          — 0 1 4 5 7 8 10
    0b100110110011,  // DblHarmonic    — 0 1 4 5 7 8 11
    0b100111001011,  // Raga Todi      — 0 1 3 6 7 8 11
    0b100111010011,  // Raga Purvi     — 0 1 4 6 7 8 11
    0b100101110011,  // Persian        — 0 1 4 5 6 8 11
    0b100111001101,  // HungarianMin   — 0 2 3 6 7 8 11
    0b100110101011,  // NeapolitanMin  — 0 1 3 5 7 8 11
    0b000110001011,  // Pelog          — 0 1 3 7 8
    0b010010100101,  // Slendro        — 0 2 5 7 10
};

// Scale degrees as semitone arrays (for generative sequencer melodic construction)
// Each array lists the semitone offsets within one octave, terminated by -1
static constexpr int kScaleDegrees[COUNT][13] = {
    {0,1,2,3,4,5,6,7,8,9,10,11,-1},      // Off (chromatic)
    {0,2,4,5,7,9,11,-1,-1,-1,-1,-1,-1},   // Major
    {0,2,3,5,7,8,10,-1,-1,-1,-1,-1,-1},   // Minor
    {0,2,4,7,9,-1,-1,-1,-1,-1,-1,-1,-1},  // Pentatonic
    {0,2,3,5,7,9,10,-1,-1,-1,-1,-1,-1},   // Dorian
    {0,2,3,5,7,8,11,-1,-1,-1,-1,-1,-1},   // Harmonic Minor
    {0,2,4,6,8,10,-1,-1,-1,-1,-1,-1,-1},  // Whole Tone
    {0,2,4,5,7,9,10,-1,-1,-1,-1,-1,-1},   // Mixolydian
    {0,1,3,5,7,8,10,-1,-1,-1,-1,-1,-1},   // Phrygian
    {0,2,4,6,7,9,11,-1,-1,-1,-1,-1,-1},   // Lydian
    {0,1,3,5,6,8,10,-1,-1,-1,-1,-1,-1},   // Locrian
    {0,3,5,7,10,-1,-1,-1,-1,-1,-1,-1,-1}, // Minor Pentatonic
    {0,3,5,6,7,10,-1,-1,-1,-1,-1,-1,-1},  // Blues
    {0,2,3,5,7,9,11,-1,-1,-1,-1,-1,-1},   // Melodic Minor
    {0,2,3,7,8,-1,-1,-1,-1,-1,-1,-1,-1},  // Hirajoshi
    {0,1,5,7,10,-1,-1,-1,-1,-1,-1,-1,-1}, // In-sen
    {0,1,5,6,10,-1,-1,-1,-1,-1,-1,-1,-1}, // Iwato
    {0,2,3,7,9,-1,-1,-1,-1,-1,-1,-1,-1},  // Kumoi
    {0,4,5,7,11,-1,-1,-1,-1,-1,-1,-1,-1}, // Ryukyu
    {0,1,4,5,7,8,10,-1,-1,-1,-1,-1,-1},   // Hijaz
    {0,1,4,5,7,8,11,-1,-1,-1,-1,-1,-1},   // Double Harmonic
    {0,1,3,6,7,8,11,-1,-1,-1,-1,-1,-1},   // Raga Todi
    {0,1,4,6,7,8,11,-1,-1,-1,-1,-1,-1},   // Raga Purvi
    {0,1,4,5,6,8,11,-1,-1,-1,-1,-1,-1},   // Persian
    {0,2,3,6,7,8,11,-1,-1,-1,-1,-1,-1},   // Hungarian Minor
    {0,1,3,5,7,8,11,-1,-1,-1,-1,-1,-1},   // Neapolitan Minor
    {0,1,3,7,8,-1,-1,-1,-1,-1,-1,-1,-1},  // Pelog
    {0,2,5,7,10,-1,-1,-1,-1,-1,-1,-1,-1}, // Slendro
};

/** Number of degrees per octave for each scale. */
inline int degreesPerOctave(Scale scale)
{
    if (scale <= Off || scale >= COUNT) return 12;
    int count = 0;
    while (count < 13 && kScaleDegrees[scale][count] >= 0) ++count;
    return count;
}

/** Get the MIDI note for a given scale degree index (0-based, spans multiple octaves).
 *  @param degreeIndex  Absolute scale degree (0 = root at baseOctave)
 *  @param root         Scale root (0=C .. 11=B)
 *  @param scale        Scale type
 *  @param baseNote     MIDI note of the lowest root (e.g. 48 = C3)
 *  @return             MIDI note number, clamped to 0-127
 */
inline int degreeToMidi(int degreeIndex, int root, Scale scale, int baseNote)
{
    int dpOct = degreesPerOctave(scale);
    if (dpOct <= 0) dpOct = 12;
    int octave = degreeIndex / dpOct;
    int degree = degreeIndex % dpOct;
    if (degree < 0) { degree += dpOct; octave--; }
    int semitone = kScaleDegrees[scale][degree];
    int midi = baseNote + root + octave * 12 + semitone;
    return std::max(0, std::min(127, midi));
}

/**
 * Quantize a MIDI note to the nearest scale degree.
 *
 * @param midiNote  Input MIDI note (0–127)
 * @param root      Scale root as semitone (0=C, 1=C#, ..., 11=B)
 * @param scale     Scale type
 * @return          Quantized MIDI note, clamped to 0–127
 */
inline int quantize(int midiNote, int root, Scale scale)
{
    if (scale == Off || scale >= COUNT)
        return midiNote;

    int mask = kScaleMasks[static_cast<size_t>(scale)];

    // Relative pitch class (0–11) from root
    int pc = ((midiNote % 12) - root + 12) % 12;

    // If already on a scale degree, no change needed
    if ((mask >> pc) & 1)
        return midiNote;

    // Search outward for nearest scale degree (prefer lower on tie)
    for (int offset = 1; offset <= 6; ++offset)
    {
        int below = (pc - offset + 12) % 12;
        int above = (pc + offset) % 12;
        bool bBelow = (mask >> below) & 1;
        bool bAbove = (mask >> above) & 1;

        if (bBelow && bAbove)
            return std::max(0, std::min(127, midiNote - offset)); // tie → prefer lower
        if (bBelow)
            return std::max(0, std::min(127, midiNote - offset));
        if (bAbove)
            return std::max(0, std::min(127, midiNote + offset));
    }

    return midiNote; // fallback (shouldn't happen with valid scales)
}

} // namespace ScaleQuantizer
