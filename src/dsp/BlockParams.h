#pragma once

// ── Target index constants (must match APVTS AudioParameterChoice order) ──

namespace EnvTarget {
    enum : int {
        None = 0,
        DCA = 1,
        Filter = 2,
        Scan = 3,
        Pitch = 4,
        DelayTime = 5,
        DelayFB = 6,
        DelayMix = 7,
        ReverbMix = 8,
        LFO1Rate = 9,
        LFO1Depth = 10,
        LFO2Rate = 11,
        LFO2Depth = 12
    };
}

namespace LfoTarget {
    enum : int {
        None = 0,
        Filter = 1,
        Scan = 2,
        Pitch = 3,
        DelayTime = 4,
        DelayFB = 5,
        DelayMix = 6,
        ReverbMix = 7,
        Env1Amt = 8,
        Env2Amt = 9,
        Env3Amt = 10
    };
}

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
    int   ampAttackCurve = 2, ampDecayCurve = 2, ampReleaseCurve = 4; // 0=Log,1=SLog,2=Lin,3=SExp,4=Exp
    bool  ampLoop = false;

    // Mod envelope 1
    float mod1Attack = 0.0f, mod1Decay = 0.0f, mod1Sustain = 1.0f, mod1Release = 0.0f;
    float mod1Amount = 0.0f;
    float mod1VelSens = 1.0f;
    int   mod1Target = 0; // 0 = None
    int   mod1AttackCurve = 2, mod1DecayCurve = 2, mod1ReleaseCurve = 4;
    bool  mod1Loop = false;

    // Mod envelope 2
    float mod2Attack = 0.0f, mod2Decay = 0.0f, mod2Sustain = 1.0f, mod2Release = 0.0f;
    float mod2Amount = 0.0f;
    float mod2VelSens = 1.0f;
    int   mod2Target = 0; // 0 = None
    int   mod2AttackCurve = 2, mod2DecayCurve = 2, mod2ReleaseCurve = 4;
    bool  mod2Loop = false;

    // LFOs (global rates/depths for cross-mod, targets for routing)
    float lfo1Rate = 1.0f, lfo1Depth = 1.0f;
    int   lfo1Wave = 0, lfo1Target = 0; // 0 = None
    float lfo2Rate = 1.0f, lfo2Depth = 1.0f;
    int   lfo2Wave = 0, lfo2Target = 0; // 0 = None

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

    // Noise oscillator
    float noiseLevel = 0.0f;      // 0-1 mix level
    int   noiseType = 0;          // 0=White, 1=Pink, 2=Brown

    // Octave shift (-2..+2)
    int octaveShift = 0;

    // Engine
    bool engineIsWavetable = false;
    bool wtSmooth = true; // Catmull-Rom frame interpolation
};
