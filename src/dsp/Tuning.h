#pragma once
#include <array>
#include <cmath>
#include <algorithm>

/**
 * Tuning system for non-12-TET intonation.
 *
 * Each tuning type defines cent deviations from 12-TET per chromatic step
 * relative to the scale root. The lookup table is 128 entries mapping
 * MIDI note → frequency in Hz.
 */
namespace Tuning
{

enum Type
{
    Equal = 0,       // 12-TET (standard)
    Just,            // 5-limit just intonation
    Pythagorean,     // pure fifths chain
    Maqam,           // neutral 2nd/6th for Arabic maqam
    COUNT
};

// Cent deviations from 12-TET per chromatic step relative to root.
// Index 0 = unison, 1 = minor 2nd position, ..., 11 = major 7th position.
static constexpr float kCentOffsets[COUNT][12] = {
    // Equal (12-TET): no deviations
    { 0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f },

    // Just intonation (5-limit): 1/1 16/15 9/8 6/5 5/4 4/3 45/32 3/2 8/5 5/3 9/5 15/8
    { 0.0f, 11.7f,  3.9f, 15.6f,-13.7f, -2.0f, -9.8f,  2.0f, 13.7f,-15.6f, 17.6f,-11.7f },

    // Pythagorean: circle of pure fifths (3:2)
    { 0.0f, 13.7f,  3.9f, -5.9f,  7.8f, -2.0f, 11.7f,  2.0f, -7.8f,  5.9f, -3.9f,  9.8f },

    // Maqam: neutral 2nd (-50c on position 2) and neutral 6th (-50c on position 9)
    // Creates the quarter-tone intervals essential for Bayati, Rast, Sikah etc.
    { 0.0f,  0.0f,-50.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,-50.0f,  0.0f,  0.0f },
};

/** Convert MIDI note to frequency using the given tuning and root.
 *  @param midiNote  MIDI note number (0-127)
 *  @param type      Tuning type
 *  @param root      Scale root as semitone (0=C .. 11=B)
 */
inline float noteToHz(int midiNote, Type type, int root)
{
    if (type <= Equal || type >= COUNT)
        return 440.0f * std::pow(2.0f, (midiNote - 69.0f) / 12.0f);

    int relPc = ((midiNote % 12) - root + 12) % 12;
    float centDev = kCentOffsets[static_cast<int>(type)][relPc];
    return 440.0f * std::pow(2.0f, (midiNote - 69.0f + centDev / 100.0f) / 12.0f);
}

/** Build a 128-entry frequency lookup table for the given tuning and root. */
inline void buildTable(float* table, Type type, int root)
{
    for (int n = 0; n < 128; ++n)
        table[n] = noteToHz(n, type, root);
}

} // namespace Tuning
