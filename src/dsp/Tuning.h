#pragma once
#include <array>
#include <cmath>
#include <algorithm>

/**
 * Tuning system for non-12-TET intonation.
 *
 * WT mode: osc uses tunedHz() directly.
 * Sampler mode: SynthVoice computes tunedHz(note)/tunedHz(60) as transpose ratio.
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
    Maqam,           // neutral 2nd/3rd/6th/7th for Arabic maqam
    Shruti,          // Indian 22-shruti system (just ratios)
    Pelog,           // Javanese gamelan (12-TET approximation)
    Slendro,         // Javanese gamelan (near-equidistant 5-tone)
    COUNT
};

// Cent deviations from 12-TET per chromatic step relative to root.
// Index 0 = unison, 1 = minor 2nd position, ..., 11 = major 7th position.
static constexpr float kCentOffsets[COUNT][12] = {
    // Equal (12-TET): no deviations
    { 0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f },

    // Maqam: neutral 2nd, 3rd, 6th, 7th (quarter-tone positions)
    // Covers Bayati (neutral 2nd), Rast (neutral 3rd), Sikah, Saba etc.
    { 0.0f,  0.0f,-50.0f,-50.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,-50.0f,-50.0f,  0.0f },

    // Shruti: Indian 22-shruti just intonation
    // Pure ratios: 1/1 16/15 9/8 6/5 5/4 4/3 45/32 3/2 8/5 5/3 9/5 15/8
    { 0.0f,+12.0f, +4.0f,+16.0f,-14.0f, -2.0f,-10.0f, +2.0f,-8.0f,-16.0f, -4.0f,-12.0f },

    // Pelog: Javanese gamelan tuning (central Javanese average, Surjodiningrat 1972)
    // Mapped to 12-TET keyboard: 1=C, 2=D, 3=E, 4=G, 5=A, 6=C, 7=D
    // Actual cents from unison: 0, 120, 260, 540, 680, 800, 940 (approx)
    // Deviations from 12-TET positions:
    { 0.0f,  0.0f,+20.0f,-40.0f,  0.0f, +40.0f,  0.0f,-20.0f,  0.0f,  0.0f,  0.0f,-60.0f },

    // Slendro: Javanese gamelan (near-equidistant 5-tone, ~240 cents apart)
    // Mapped to 12-TET: 1=C, 2=D, 3=F, 4=G, 5=Bb
    // Actual cents: 0, 240, 480, 720, 960 (ideal equidistant)
    // Deviations from 12-TET: D+40, F-20, G+20, Bb-40
    { 0.0f,  0.0f,+40.0f,  0.0f,  0.0f,-20.0f,  0.0f,+20.0f,  0.0f,  0.0f,-40.0f,  0.0f },
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
