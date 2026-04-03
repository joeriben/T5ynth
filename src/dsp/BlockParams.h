#pragma once

/**
 * Snapshot of all block-rate parameters, read once per processBlock from APVTS.
 * Passed to SynthVoice(s) to avoid per-voice atomic reads.
 */
struct BlockParams
{
    // Amp envelope
    float ampAttack = 0.0f, ampDecay = 0.0f, ampSustain = 1.0f, ampRelease = 0.0f;
    float ampAmount = 1.0f;
    float ampVelSens = 1.0f;  // 0=fixed, 1=full velocity
    bool  ampLoop = false;

    // Mod envelope 1
    float mod1Attack = 0.0f, mod1Decay = 0.0f, mod1Sustain = 1.0f, mod1Release = 0.0f;
    float mod1Amount = 0.0f;
    float mod1VelSens = 1.0f;
    int   mod1Target = 8; // 8 = None
    bool  mod1Loop = false;

    // Mod envelope 2
    float mod2Attack = 0.0f, mod2Decay = 0.0f, mod2Sustain = 1.0f, mod2Release = 0.0f;
    float mod2Amount = 0.0f;
    float mod2VelSens = 1.0f;
    int   mod2Target = 8;
    bool  mod2Loop = false;

    // LFOs (global rates/depths for cross-mod, targets for routing)
    float lfo1Rate = 1.0f, lfo1Depth = 1.0f;
    int   lfo1Wave = 0, lfo1Target = 10; // 10 = None
    float lfo2Rate = 1.0f, lfo2Depth = 1.0f;
    int   lfo2Wave = 0, lfo2Target = 10;

    // Filter
    bool  filterEnabled = false;
    float baseCutoff = 20000.0f;
    float baseReso = 0.0f;
    int   filterType = 0;  // 0=LP, 1=HP, 2=BP
    int   filterSlope = 0; // 0=12dB, 1=24dB
    float filterMix = 1.0f;
    float kbdTrack = 0.0f;

    // Scan
    float baseScan = 0.0f;
    float driftScanOffset = 0.0f;

    // Drift offsets for filter and pitch (applied per-voice in SynthVoice)
    float driftFilterOffset = 0.0f;  // multiplicative: cutoff *= (1 + offset * FILTER_DEPTH)
    float driftPitchOffset = 0.0f;   // additive: pitchMod += offset

    // Engine
    bool engineIsWavetable = false;
};
