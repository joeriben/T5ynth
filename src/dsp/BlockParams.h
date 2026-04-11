#pragma once

// ── Choice-parameter single-source-of-truth tables ──
//
// Every AudioParameterChoice in T5ynth has its canonical entry list here.
// The `kEntries` arrays are consumed by:
//   - juce::AudioParameterChoice StringArrays in PluginProcessor.cpp
//   - juce::ComboBox::addItemList calls in gui/*.cpp
//   - preset save/load helpers in PluginProcessor.cpp (via the .key column)
//
// Each entry carries a `.key` column (stable snake_case identifier used for
// JSON serialization — never changes once shipped) and a `.label` column
// (user-facing display string — may be renamed without breaking presets).
// Both columns live in the same array at the same index, so adding a new
// choice means editing exactly one place. A `static_assert` below every
// table pins the enum's last value to `kCount - 1` to prevent drift.
//
// Note: the `.key` column is used by preset JSON save/load (Session 2
// onwards). In Session 1 the JSON helpers still use their legacy name
// tables — only the `.label` column is consumed. Keys are committed now
// so that Session 2's helper rewrite is a pure swap.

/** Common struct layout for every choice-parameter entry table. */
struct ChoiceEntry {
    const char* key;
    const char* label;
};

// ── APVTS parameter-ID constants ──
// Every parameter has its canonical ID here; use PID::xxx everywhere
// instead of string literals to get compile-time typo detection.
namespace PID {
    static constexpr const char* oscScan          = "osc_scan";
    static constexpr const char* oscOctave        = "osc_octave";
    static constexpr const char* engineMode       = "engine_mode";
    static constexpr const char* voiceCount       = "voice_count";
    static constexpr const char* masterVol        = "master_vol";
    static constexpr const char* ampAttack        = "amp_attack";
    static constexpr const char* ampDecay         = "amp_decay";
    static constexpr const char* ampSustain       = "amp_sustain";
    static constexpr const char* ampRelease       = "amp_release";
    static constexpr const char* ampAmount        = "amp_amount";
    static constexpr const char* ampVelSens       = "amp_vel_sens";
    static constexpr const char* ampLoop          = "amp_loop";
    static constexpr const char* ampAttackCurve   = "amp_attack_curve";
    static constexpr const char* ampDecayCurve    = "amp_decay_curve";
    static constexpr const char* ampReleaseCurve  = "amp_release_curve";
    static constexpr const char* mod1Attack       = "mod1_attack";
    static constexpr const char* mod1Decay        = "mod1_decay";
    static constexpr const char* mod1Sustain      = "mod1_sustain";
    static constexpr const char* mod1Release      = "mod1_release";
    static constexpr const char* mod1Amount       = "mod1_amount";
    static constexpr const char* mod1VelSens      = "mod1_vel_sens";
    static constexpr const char* mod1Loop         = "mod1_loop";
    static constexpr const char* mod1Target       = "mod1_target";
    static constexpr const char* mod1AttackCurve  = "mod1_attack_curve";
    static constexpr const char* mod1DecayCurve   = "mod1_decay_curve";
    static constexpr const char* mod1ReleaseCurve = "mod1_release_curve";
    static constexpr const char* mod2Attack       = "mod2_attack";
    static constexpr const char* mod2Decay        = "mod2_decay";
    static constexpr const char* mod2Sustain      = "mod2_sustain";
    static constexpr const char* mod2Release      = "mod2_release";
    static constexpr const char* mod2Amount       = "mod2_amount";
    static constexpr const char* mod2VelSens      = "mod2_vel_sens";
    static constexpr const char* mod2Loop         = "mod2_loop";
    static constexpr const char* mod2Target       = "mod2_target";
    static constexpr const char* mod2AttackCurve  = "mod2_attack_curve";
    static constexpr const char* mod2DecayCurve   = "mod2_decay_curve";
    static constexpr const char* mod2ReleaseCurve = "mod2_release_curve";
    static constexpr const char* lfo1Rate         = "lfo1_rate";
    static constexpr const char* lfo1Depth        = "lfo1_depth";
    static constexpr const char* lfo1Wave         = "lfo1_wave";
    static constexpr const char* lfo1Target       = "lfo1_target";
    static constexpr const char* lfo1Mode         = "lfo1_mode";
    static constexpr const char* lfo2Rate         = "lfo2_rate";
    static constexpr const char* lfo2Depth        = "lfo2_depth";
    static constexpr const char* lfo2Wave         = "lfo2_wave";
    static constexpr const char* lfo2Target       = "lfo2_target";
    static constexpr const char* lfo2Mode         = "lfo2_mode";
    static constexpr const char* driftEnabled     = "drift_enabled";
    static constexpr const char* driftRegen       = "drift_regen";
    static constexpr const char* driftCrossfade   = "drift_crossfade";
    static constexpr const char* drift1Rate       = "drift1_rate";
    static constexpr const char* drift1Depth      = "drift1_depth";
    static constexpr const char* drift1Target     = "drift1_target";
    static constexpr const char* drift1Wave       = "drift1_wave";
    static constexpr const char* drift2Rate       = "drift2_rate";
    static constexpr const char* drift2Depth      = "drift2_depth";
    static constexpr const char* drift2Target     = "drift2_target";
    static constexpr const char* drift2Wave       = "drift2_wave";
    static constexpr const char* drift3Rate       = "drift3_rate";
    static constexpr const char* drift3Depth      = "drift3_depth";
    static constexpr const char* drift3Target     = "drift3_target";
    static constexpr const char* drift3Wave       = "drift3_wave";
    static constexpr const char* filterEnabled    = "filter_enabled";
    static constexpr const char* filterType       = "filter_type";
    static constexpr const char* filterSlope      = "filter_slope";
    static constexpr const char* filterCutoff     = "filter_cutoff";
    static constexpr const char* filterResonance  = "filter_resonance";
    static constexpr const char* filterMix        = "filter_mix";
    static constexpr const char* filterKbdTrack   = "filter_kbd_track";
    static constexpr const char* delayType        = "delay_type";
    static constexpr const char* delayTime        = "delay_time";
    static constexpr const char* delayFeedback    = "delay_feedback";
    static constexpr const char* delayMix         = "delay_mix";
    static constexpr const char* delayDamp        = "delay_damp";
    static constexpr const char* reverbType       = "reverb_type";
    static constexpr const char* reverbMix        = "reverb_mix";
    static constexpr const char* algoRoom         = "algo_room";
    static constexpr const char* algoDamping      = "algo_damping";
    static constexpr const char* algoWidth        = "algo_width";
    static constexpr const char* limiterThresh    = "limiter_thresh";
    static constexpr const char* limiterRelease   = "limiter_release";
    static constexpr const char* genAlpha         = "gen_alpha";
    static constexpr const char* genMagnitude     = "gen_magnitude";
    static constexpr const char* genNoise         = "gen_noise";
    static constexpr const char* genDuration      = "gen_duration";
    static constexpr const char* genStart         = "gen_start";
    static constexpr const char* genCfg           = "gen_cfg";
    static constexpr const char* genSeed          = "gen_seed";
    static constexpr const char* genHfBoost       = "gen_hf_boost";
    static constexpr const char* infSteps         = "inf_steps";
    static constexpr const char* loopMode         = "loop_mode";
    static constexpr const char* crossfadeMs      = "crossfade_ms";
    static constexpr const char* normalize        = "normalize";
    static constexpr const char* loopOptimize     = "loop_optimize";
    static constexpr const char* noiseLevel       = "noise_level";
    static constexpr const char* noiseType        = "noise_type";
    static constexpr const char* wtFrames         = "wt_frames";
    static constexpr const char* wtSmooth         = "wt_smooth";
    static constexpr const char* seqMode          = "seq_mode";
    static constexpr const char* seqRunning       = "seq_running";
    static constexpr const char* seqBpm           = "seq_bpm";
    static constexpr const char* seqSteps         = "seq_steps";
    static constexpr const char* seqDivision      = "seq_division";
    static constexpr const char* seqGlideTime     = "seq_glide_time";
    static constexpr const char* seqGate          = "seq_gate";
    static constexpr const char* seqOctave        = "seq_octave";
    static constexpr const char* seqPreset        = "seq_preset";
    static constexpr const char* arpMode          = "arp_mode";
    static constexpr const char* arpRate          = "arp_rate";
    static constexpr const char* arpOctaves       = "arp_octaves";
    static constexpr const char* genSeqRunning    = "gen_seq_running";
    static constexpr const char* genSteps         = "gen_steps";
    static constexpr const char* genPulses        = "gen_pulses";
    static constexpr const char* genRotation      = "gen_rotation";
    static constexpr const char* genMutation      = "gen_mutation";
    static constexpr const char* genRange         = "gen_range";
    static constexpr const char* genFixSteps      = "gen_fix_steps";
    static constexpr const char* genFixPulses     = "gen_fix_pulses";
    static constexpr const char* genFixRotation   = "gen_fix_rotation";
    static constexpr const char* genFixMutation   = "gen_fix_mutation";
    static constexpr const char* scaleRoot        = "scale_root";
    static constexpr const char* scaleType        = "scale_type";
}

// ── Modulation envelope targets ──
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
    static constexpr ChoiceEntry kEntries[] = {
        { "none",       "---"        },
        { "dca",        "DCA"        },
        { "filter",     "Filter"     },
        { "scan",       "Scan"       },
        { "pitch",      "Pitch"      },
        { "delay_time", "Dly Time"   },
        { "delay_fb",   "Dly FB"     },
        { "delay_mix",  "Dly Mix"    },
        { "reverb_mix", "Rev Mix"    },
        { "lfo1_rate",  "LFO1 Rate"  },
        { "lfo1_depth", "LFO1 Depth" },
        { "lfo2_rate",  "LFO2 Rate"  },
        { "lfo2_depth", "LFO2 Depth" }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(LFO2Depth + 1 == kCount,
                  "EnvTarget enum and kEntries are out of sync.");
}

// ── LFO targets ──
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
    static constexpr ChoiceEntry kEntries[] = {
        { "none",       "---"       },
        { "filter",     "Filter"    },
        { "scan",       "Scan"      },
        { "pitch",      "Pitch"     },
        { "delay_time", "Dly Time"  },
        { "delay_fb",   "Dly FB"    },
        { "delay_mix",  "Dly Mix"   },
        { "reverb_mix", "Rev Mix"   },
        { "env1_amt",   "ENV1 Amt"  },
        { "env2_amt",   "ENV2 Amt"  },
        { "env3_amt",   "ENV3 Amt"  }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(Env3Amt + 1 == kCount,
                  "LfoTarget enum and kEntries are out of sync.");
}

// ── Drift LFO targets ──
namespace DriftTarget {
    enum : int {
        None = 0,
        Alpha = 1,
        Noise = 2,
        Magnitude = 3,
        Axis1 = 4,
        Axis2 = 5,
        Axis3 = 6,
        WtScan = 7,
        Filter = 8,
        Pitch = 9,
        DelayTime = 10,
        DelayFB = 11,
        DelayMix = 12,
        ReverbMix = 13,
        Env1Amt = 14,
        Env2Amt = 15,
        Env3Amt = 16
    };
    static constexpr ChoiceEntry kEntries[] = {
        { "none",       "---"        },
        { "alpha",      "Alpha"      },
        { "noise",      "Emb. Noise" },
        { "magnitude",  "Magnitude"  },
        { "axis_1",     "Axis 1"     },
        { "axis_2",     "Axis 2"     },
        { "axis_3",     "Axis 3"     },
        { "wt_scan",    "WT Scan"    },
        { "filter",     "Filter"     },
        { "pitch",      "Pitch"      },
        { "delay_time", "Dly Time"   },
        { "delay_fb",   "Dly FB"     },
        { "delay_mix",  "Dly Mix"    },
        { "reverb_mix", "Rev Mix"    },
        { "env1_amt",   "ENV1 Amt"   },
        { "env2_amt",   "ENV2 Amt"   },
        { "env3_amt",   "ENV3 Amt"   }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(Env3Amt + 1 == kCount,
                  "DriftTarget enum and kEntries are out of sync.");
}

// ── Engine mode ──
namespace EngineMode {
    enum : int { Sampler = 0, Wavetable = 1 };
    static constexpr ChoiceEntry kEntries[] = {
        { "sampler",   "Sampler"   },
        { "wavetable", "Wavetable" }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(Wavetable + 1 == kCount, "EngineMode out of sync.");
}

// ── Sample loop mode ──
namespace LoopMode {
    enum : int { OneShot = 0, Loop = 1, PingPong = 2 };
    static constexpr ChoiceEntry kEntries[] = {
        { "oneshot",  "One-shot"  },
        { "loop",     "Loop"      },
        { "pingpong", "Ping-Pong" }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(PingPong + 1 == kCount, "LoopMode out of sync.");
}

// ── Loop optimization level ──
namespace LoopOptimize {
    enum : int { Off = 0, Low = 1, High = 2 };
    static constexpr ChoiceEntry kEntries[] = {
        { "off",  "Off"  },
        { "low",  "Low"  },
        { "high", "High" }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(High + 1 == kCount, "LoopOptimize out of sync.");
}

// ── Filter type ──
namespace FilterType {
    enum : int { Off = 0, Lowpass = 1, Highpass = 2, Bandpass = 3 };
    static constexpr ChoiceEntry kEntries[] = {
        { "off",      "Off"      },
        { "lowpass",  "Lowpass"  },
        { "highpass", "Highpass" },
        { "bandpass", "Bandpass" }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(Bandpass + 1 == kCount, "FilterType out of sync.");
}

// ── Filter slope ──
namespace FilterSlope {
    enum : int { Slope6 = 0, Slope12 = 1, Slope18 = 2, Slope24 = 3 };
    static constexpr ChoiceEntry kEntries[] = {
        { "6db",  "6dB"  },
        { "12db", "12dB" },
        { "18db", "18dB" },
        { "24db", "24dB" }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(Slope24 + 1 == kCount, "FilterSlope out of sync.");
}

// ── Delay type ──
namespace DelayType {
    enum : int { Off = 0, Stereo = 1 };
    static constexpr ChoiceEntry kEntries[] = {
        { "off",    "Off"    },
        { "stereo", "Stereo" }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(Stereo + 1 == kCount, "DelayType out of sync.");
}

// ── Reverb type ──
namespace ReverbType {
    enum : int { Off = 0, Dark = 1, Medium = 2, Bright = 3, Algo = 4 };
    static constexpr ChoiceEntry kEntries[] = {
        { "off",    "Off"    },
        { "dark",   "Dark"   },
        { "medium", "Medium" },
        { "bright", "Bright" },
        { "algo",   "Algo"   }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(Algo + 1 == kCount, "ReverbType out of sync.");
}

// ── Noise oscillator type (namespace name avoids clash with the global
//    `enum class NoiseType` in dsp/NoiseGenerator.h). ──
namespace NoiseKind {
    enum : int { White = 0, Pink = 1, Brown = 2 };
    static constexpr ChoiceEntry kEntries[] = {
        { "white", "White" },
        { "pink",  "Pink"  },
        { "brown", "Brown" }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(Brown + 1 == kCount, "NoiseKind out of sync.");
}

// ── LFO waveform (5 entries including S&H) ──
namespace LfoWave {
    enum : int { Sine = 0, Tri = 1, Saw = 2, Square = 3, SampleHold = 4 };
    static constexpr ChoiceEntry kEntries[] = {
        { "sine",            "Sin"  },
        { "triangle",        "Tri"  },
        { "sawtooth",        "Saw"  },
        { "square",          "Sq"   },
        { "sample_and_hold", "S&H"  }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(SampleHold + 1 == kCount, "LfoWave out of sync.");
}

// ── LFO trigger mode ──
namespace LfoMode {
    enum : int { Free = 0, Trigger = 1 };
    static constexpr ChoiceEntry kEntries[] = {
        { "free",    "Free" },
        { "trigger", "Trig" }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(Trigger + 1 == kCount, "LfoMode out of sync.");
}

// ── Drift LFO waveform (label "Sq" differs from LfoWave "Square"!) ──
namespace DriftWave {
    enum : int { Sine = 0, Tri = 1, Saw = 2, Square = 3 };
    static constexpr ChoiceEntry kEntries[] = {
        { "sine",     "Sine" },
        { "triangle", "Tri"  },
        { "sawtooth", "Saw"  },
        { "square",   "Sq"   }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(Square + 1 == kCount, "DriftWave out of sync.");
}

// ── Envelope curve shape (namespace name avoids clash with the global
//    `enum class CurveShape` in dsp/ADSREnvelope.h, which is the DSP-side
//    strongly-typed version of this choice list). ──
namespace EnvCurve {
    enum : int { Log = 0, SLog = 1, Lin = 2, SExp = 3, Exp = 4 };
    static constexpr ChoiceEntry kEntries[] = {
        { "log",     "Log"  },
        { "softlog", "SLog" },
        { "lin",     "Lin"  },
        { "softexp", "SExp" },
        { "exp",     "Exp"  }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(Exp + 1 == kCount, "EnvCurve out of sync.");
}

// ── Drift regenerate mode (UTF-8 quarter-note glyph in labels) ──
namespace DriftRegen {
    enum : int { Manual = 0, Auto = 1, Max1 = 2, Max4 = 3, Max16 = 4 };
    static constexpr ChoiceEntry kEntries[] = {
        { "manual",      "Manual"                                          },
        { "auto",        "Auto"                                            },
        { "max_1beat",   "max 1\xe2\x99\xa9"   /* UTF-8 ♩ */                },
        { "max_4beats",  "max 4\xe2\x99\xa9"                                },
        { "max_16beats", "max 16\xe2\x99\xa9"                               }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(Max16 + 1 == kCount, "DriftRegen out of sync.");
}

// ── Voice count ──
namespace VoiceCount {
    enum : int { Mono = 0, V4 = 1, V6 = 2, V8 = 3, V12 = 4, V16 = 5 };
    static constexpr ChoiceEntry kEntries[] = {
        { "mono", "Mono" },
        { "4",    "4"    },
        { "6",    "6"    },
        { "8",    "8"    },
        { "12",   "12"   },
        { "16",   "16"   }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(V16 + 1 == kCount, "VoiceCount out of sync.");
}

// ── Wavetable frame count ──
namespace WtFrames {
    enum : int { F32 = 0, F64 = 1, F128 = 2, F256 = 3 };
    static constexpr ChoiceEntry kEntries[] = {
        { "32",  "32"  },
        { "64",  "64"  },
        { "128", "128" },
        { "256", "256" }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(F256 + 1 == kCount, "WtFrames out of sync.");
}

// ── Oscillator octave shift (pitch compensation for inferred fundamental) ──
namespace OscOctave {
    enum : int { Neg2 = 0, Neg1 = 1, Zero = 2, Pos1 = 3, Pos2 = 4 };
    static constexpr ChoiceEntry kEntries[] = {
        { "-2", "-2" },
        { "-1", "-1" },
        { "0",  "0"  },
        { "+1", "+1" },
        { "+2", "+2" }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(Pos2 + 1 == kCount, "OscOctave out of sync.");
}

// ── Sequencer octave shift (melodic range, semantically different from
//    OscOctave — kept as a separate namespace on purpose). ──
namespace SeqOctave {
    enum : int { Neg2 = 0, Neg1 = 1, Zero = 2, Pos1 = 3, Pos2 = 4 };
    static constexpr ChoiceEntry kEntries[] = {
        { "-2", "-2" },
        { "-1", "-1" },
        { "0",  "0"  },
        { "+1", "+1" },
        { "+2", "+2" }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(Pos2 + 1 == kCount, "SeqOctave out of sync.");
}

// ── Sequencer mode ──
namespace SeqMode {
    enum : int { Seq = 0, ArpUp = 1, ArpDown = 2, ArpUpDown = 3, ArpRandom = 4 };
    static constexpr ChoiceEntry kEntries[] = {
        { "seq",        "Seq"     },
        { "arp_up",     "Arp Up"  },
        { "arp_down",   "Arp Dn"  },
        { "arp_updown", "Arp UD"  },
        { "arp_random", "Arp Rnd" }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(ArpRandom + 1 == kCount, "SeqMode out of sync.");
}

// ── Sequencer note division ──
namespace SeqDivision {
    enum : int { D1_1 = 0, D1_2 = 1, D1_4 = 2, D1_8 = 3, D1_16 = 4 };
    static constexpr ChoiceEntry kEntries[] = {
        { "1_1",  "1/1"  },
        { "1_2",  "1/2"  },
        { "1_4",  "1/4"  },
        { "1_8",  "1/8"  },
        { "1_16", "1/16" }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(D1_16 + 1 == kCount, "SeqDivision out of sync.");
}

// ── Arpeggiator rate (straight + triplet divisions) ──
namespace ArpRate {
    enum : int {
        R1_4 = 0, R1_8 = 1, R1_16 = 2, R1_32 = 3,
        R1_4T = 4, R1_8T = 5, R1_16T = 6
    };
    static constexpr ChoiceEntry kEntries[] = {
        { "1_4",   "1/4"   },
        { "1_8",   "1/8"   },
        { "1_16",  "1/16"  },
        { "1_32",  "1/32"  },
        { "1_4t",  "1/4T"  },
        { "1_8t",  "1/8T"  },
        { "1_16t", "1/16T" }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(R1_16T + 1 == kCount, "ArpRate out of sync.");
}

// ── Arpeggiator mode ──
namespace ArpMode {
    enum : int { Off = 0, Up = 1, Down = 2, UpDown = 3, Random = 4 };
    static constexpr ChoiceEntry kEntries[] = {
        { "off",    "Off"    },
        { "up",     "Up"     },
        { "down",   "Down"   },
        { "updown", "UpDown" },
        { "random", "Random" }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(Random + 1 == kCount, "ArpMode out of sync.");
}

// ── Musical scale root ──
namespace ScaleRoot {
    enum : int {
        C = 0, Cs = 1, D = 2, Ds = 3, E = 4, F = 5,
        Fs = 6, G = 7, Gs = 8, A = 9, As = 10, B = 11
    };
    static constexpr ChoiceEntry kEntries[] = {
        { "c",  "C"  },
        { "cs", "C#" },
        { "d",  "D"  },
        { "ds", "D#" },
        { "e",  "E"  },
        { "f",  "F"  },
        { "fs", "F#" },
        { "g",  "G"  },
        { "gs", "G#" },
        { "a",  "A"  },
        { "as", "A#" },
        { "b",  "B"  }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(B + 1 == kCount, "ScaleRoot out of sync.");
}

// ── Musical scale type ──
namespace ScaleType {
    enum : int {
        Off = 0, Maj, Min, Pent, Dor, Harm, WhlT,           // 0-6: unchanged
        Mixo, Phry, Lyd, Locr, MinP, Blu, MelM,             // 7-13: western ext.
        Hira, InSn, Iwat, Kumo, Ryuk,                        // 14-18: east asian
        Hjz, DblH, Todi, Purv, Pers,                         // 19-23: middle east / south asia
        HunM, NeaM,                                           // 24-25: european ext.
        Pelg, Slnd                                             // 26-27: southeast asian
    };
    static constexpr ChoiceEntry kEntries[] = {
        { "off",   "Off"       },
        { "maj",   "Major"     },
        { "min",   "Minor"     },
        { "pent",  "Penta"     },
        { "dor",   "Dorian"    },
        { "harm",  "Harm.Min"  },
        { "whole", "WhlTone"   },
        { "mixo",  "Mixolyd"   },
        { "phry",  "Phrygian"  },
        { "lyd",   "Lydian"    },
        { "locr",  "Locrian"   },
        { "minp",  "MinPent"   },
        { "blu",   "Blues"     },
        { "melm",  "Mel.Min"   },
        { "hira",  "Hirajoshi" },
        { "insn",  "In-sen"    },
        { "iwat",  "Iwato"     },
        { "kumo",  "Kumoi"     },
        { "ryuk",  "Ryukyu"    },
        { "hjz",   "Hijaz"     },
        { "dblh",  "DblHarm"   },
        { "todi",  "R.Todi"    },
        { "purv",  "R.Purvi"   },
        { "pers",  "Persian"   },
        { "hunm",  "Hung.Min"  },
        { "neam",  "Neap.Min"  },
        { "pelg",  "Pelog"     },
        { "slnd",  "Slendro"   },
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(Slnd + 1 == kCount, "ScaleType out of sync.");
}

// ── Generative sequencer octave range ──
namespace GenRange {
    enum : int { R1 = 0, R2 = 1, R3 = 2, R4 = 3 };
    static constexpr ChoiceEntry kEntries[] = {
        { "1", "1" },
        { "2", "2" },
        { "3", "3" },
        { "4", "4" }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(R4 + 1 == kCount, "GenRange out of sync.");
}

// ── Sequencer pattern preset ──
namespace SeqPreset {
    enum : int {
        OctaveBounce = 0,
        WideLeap = 1,
        OffBeatMinor = 2,
        GlideGroove = 3,
        SparseStab = 4,
        RisingArc = 5,
        Scatter = 6,
        Chromatic = 7,
        BassWalk = 8,
        GatedPulse = 9
    };
    static constexpr ChoiceEntry kEntries[] = {
        { "octave_bounce",  "Octave Bounce"  },
        { "wide_leap",      "Wide Leap"      },
        { "off_beat_minor", "Off-Beat Minor" },
        { "glide_groove",   "Glide Groove"   },
        { "sparse_stab",    "Sparse Stab"    },
        { "rising_arc",     "Rising Arc"     },
        { "scatter",        "Scatter"        },
        { "chromatic",      "Chromatic"      },
        { "bass_walk",      "Bass Walk"      },
        { "gated_pulse",    "Gated Pulse"    }
    };
    static constexpr int kCount = sizeof(kEntries) / sizeof(kEntries[0]);
    static_assert(GatedPulse + 1 == kCount, "SeqPreset out of sync.");
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
    int   ampAttackCurve = 2, ampDecayCurve = 2, ampReleaseCurve = 4; // CurveShape indices
    bool  ampLoop = false;

    // Mod envelope 1
    float mod1Attack = 0.0f, mod1Decay = 0.0f, mod1Sustain = 1.0f, mod1Release = 0.0f;
    float mod1Amount = 0.0f;
    float mod1VelSens = 1.0f;
    int   mod1Target = 0; // EnvTarget::None
    int   mod1AttackCurve = 2, mod1DecayCurve = 2, mod1ReleaseCurve = 4;
    bool  mod1Loop = false;

    // Mod envelope 2
    float mod2Attack = 0.0f, mod2Decay = 0.0f, mod2Sustain = 1.0f, mod2Release = 0.0f;
    float mod2Amount = 0.0f;
    float mod2VelSens = 1.0f;
    int   mod2Target = 0; // EnvTarget::None
    int   mod2AttackCurve = 2, mod2DecayCurve = 2, mod2ReleaseCurve = 4;
    bool  mod2Loop = false;

    // LFOs (global rates/depths for cross-mod, targets for routing)
    float lfo1Rate = 1.0f, lfo1Depth = 1.0f;
    int   lfo1Wave = 0, lfo1Target = 0; // LfoTarget::None
    float lfo2Rate = 1.0f, lfo2Depth = 1.0f;
    int   lfo2Wave = 0, lfo2Target = 0; // LfoTarget::None

    // Filter
    bool  filterEnabled = false;
    float baseCutoff = 20000.0f;
    float baseReso = 0.0f;
    int   filterType = 0;  // FilterType index
    int   filterSlope = 0; // FilterSlope index
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
    int   noiseType = 0;          // NoiseType index

    // Octave shift (-2..+2)
    int octaveShift = 0;

    // Engine
    bool engineIsWavetable = false;
    bool wtSmooth = true; // Catmull-Rom frame interpolation
};
