#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"
#include "dsp/Tuning.h"

T5ynthProcessor::T5ynthProcessor()
    : AudioProcessor(BusesProperties()
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "T5ynth", createParameterLayout())
{
}

T5ynthProcessor::~T5ynthProcessor() = default;

bool T5ynthProcessor::launchPipeInference(const juce::File& backendDir)
{
    return pipeInference.launch(backendDir);
}

juce::AudioProcessorValueTreeState::ParameterLayout T5ynthProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // Helper: build a juce::StringArray of display labels from any
    // BlockParams.h kEntries table. Keeps AudioParameterChoice construction
    // in sync with the single source of truth.
    auto toChoices = [](const auto& entries) {
        juce::StringArray arr;
        for (const auto& e : entries) arr.add(e.label);
        return arr;
    };

    // Oscillator
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::oscScan, 1}, "Scan Position",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // Voice count: Mono, 4, 6, 8, 12, 16
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::voiceCount, 1}, "Voice Count",
        toChoices(VoiceCount::kEntries), 3)); // default 8

    // Tuning system
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::tuning, 1}, "Tuning",
        toChoices(TuningType::kEntries), 0)); // default 12-TET

    // Amplitude Envelope (A=0, D=200ms, S=10%, R=180ms)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::ampAttack, 1}, "Attack",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f, 0.3f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::ampDecay, 1}, "Decay",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f, 0.3f), 200.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::ampSustain, 1}, "Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::ampRelease, 1}, "Release",
        juce::NormalisableRange<float>(0.0f, 10000.0f, 0.1f, 0.3f), 180.0f));

    // Filter
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::filterCutoff, 1}, "Filter Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 0.1f, 0.25f), 20000.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::filterResonance, 1}, "Filter Resonance",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::filterType, 1}, "Filter Type",
        toChoices(FilterType::kEntries), 1));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::filterSlope, 1}, "Filter Slope",
        toChoices(FilterSlope::kEntries), 1));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::filterMix, 1}, "Filter Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::filterKbdTrack, 1}, "Filter Kbd Track",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // Delay
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::delayTime, 1}, "Delay Time",
        juce::NormalisableRange<float>(1.0f, 2000.0f, 0.1f, 0.35f), 250.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::delayFeedback, 1}, "Delay Feedback",
        juce::NormalisableRange<float>(0.0f, 0.95f), 0.35f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::delayMix, 1}, "Delay Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f, 0.3f), 0.3f));

    // Reverb
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::reverbMix, 1}, "Reverb Mix",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f, 0.3f), 0.25f));

    // Algorithmic reverb parameters (only active when reverb_type == Algo)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::algoRoom, 1}, "Algo Room",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.7f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::algoDamping, 1}, "Algo Damping",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.4f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::algoWidth, 1}, "Algo Width",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));

    // Generation
    // Alpha: quadratic curve around 0 for fine control near center (±0.15 sensitive zone)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::genAlpha, 1}, "Alpha",
        juce::NormalisableRange<float>(-2.0f, 2.0f,
            [](float s, float e, float n) {
                float c = n * 2.0f - 1.0f;
                float curved = (c >= 0.0f ? 1.0f : -1.0f) * c * c;
                return s + (e - s) * (curved * 0.5f + 0.5f);
            },
            [](float s, float e, float v) {
                float norm = (v - s) / (e - s);
                float c = norm * 2.0f - 1.0f;
                float uncurved = (c >= 0.0f ? 1.0f : -1.0f) * std::sqrt(std::abs(c));
                return uncurved * 0.5f + 0.5f;
            },
            [](float s, float e, float v) { return juce::jlimit(s, e, v); }
        ), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::genMagnitude, 1}, "Magnitude",
        juce::NormalisableRange<float>(0.001f, 5.0f, 0.001f, 0.3f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::genNoise, 1}, "Noise",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f, 0.3f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::genDuration, 1}, "Duration",
        // 11s hard cap in the UI — Stable Audio Open Small tops out at 11s
        // and T5ynth is for short sound samples, not music. SA 1.0 can do
        // more internally but the slider stays unified at 11s.
        juce::NormalisableRange<float>(0.1f, 11.0f, 0.1f, 0.3f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::infSteps, 1}, "Steps", 1, 100, 20));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::genCfg, 1}, "CFG Scale",
        juce::NormalisableRange<float>(1.0f, 15.0f, 0.1f), 7.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::genStart, 1}, "Start Position",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::genSeed, 1}, "Seed", -1, 999999999, 123456789));

    // Engine mode
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::engineMode, 1}, "Engine Mode",
        toChoices(EngineMode::kEntries), 0));

    // Mod Envelope 1 (A=0, D=2500ms, S=10%, R=4000ms, Amt=100%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::mod1Attack, 1}, "Mod1 Attack",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f, 0.3f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::mod1Decay, 1}, "Mod1 Decay",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f, 0.3f), 2500.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::mod1Sustain, 1}, "Mod1 Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::mod1Release, 1}, "Mod1 Release",
        juce::NormalisableRange<float>(0.0f, 10000.0f, 0.1f, 0.3f), 4000.0f));

    // Mod Envelope 2 (A=0, D=2500ms, S=10%, R=4000ms, Amt=100%)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::mod2Attack, 1}, "Mod2 Attack",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f, 0.3f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::mod2Decay, 1}, "Mod2 Decay",
        juce::NormalisableRange<float>(0.0f, 5000.0f, 0.1f, 0.3f), 2500.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::mod2Sustain, 1}, "Mod2 Sustain",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.1f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::mod2Release, 1}, "Mod2 Release",
        juce::NormalisableRange<float>(0.0f, 10000.0f, 0.1f, 0.3f), 4000.0f));

    // LFO 1 (reference defaults: rate=2.0, depth=0, sine)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::lfo1Rate, 1}, "LFO1 Rate",
        juce::NormalisableRange<float>(0.01f, 30.0f, 0.01f, 0.3f), 2.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::lfo1Depth, 1}, "LFO1 Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::lfo1Wave, 1}, "LFO1 Wave",
        toChoices(LfoWave::kEntries), 0));

    // LFO 2 (reference defaults: rate=0.5, depth=0, triangle)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::lfo2Rate, 1}, "LFO2 Rate",
        juce::NormalisableRange<float>(0.01f, 30.0f, 0.01f, 0.3f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::lfo2Depth, 1}, "LFO2 Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::lfo2Wave, 1}, "LFO2 Wave",
        toChoices(LfoWave::kEntries), 1));

    // Drift LFO
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::driftEnabled, 1}, "Drift Enabled", false));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::driftRegen, 1}, "Regenerate",
        toChoices(DriftRegen::kEntries), 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::driftCrossfade, 1}, "Drift Crossfade",
        juce::NormalisableRange<float>(0.0f, 2000.0f, 1.0f), 200.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::drift1Rate, 1}, "Drift1 Rate",
        juce::NormalisableRange<float>(0.001f, 2.0f, 0.001f, 0.3f), 0.01f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::drift1Depth, 1}, "Drift1 Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::drift2Rate, 1}, "Drift2 Rate",
        juce::NormalisableRange<float>(0.001f, 2.0f, 0.001f, 0.3f), 0.005f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::drift2Depth, 1}, "Drift2 Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::drift3Rate, 1}, "Drift3 Rate",
        juce::NormalisableRange<float>(0.001f, 2.0f, 0.001f, 0.3f), 0.002f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::drift3Depth, 1}, "Drift3 Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));

    // Drift targets + waveform selection
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::drift1Target, 1}, "Drift1 Target",
        toChoices(DriftTarget::kEntries), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::drift2Target, 1}, "Drift2 Target",
        toChoices(DriftTarget::kEntries), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::drift1Wave, 1}, "Drift1 Wave",
        toChoices(DriftWave::kEntries), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::drift2Wave, 1}, "Drift2 Wave",
        toChoices(DriftWave::kEntries), 0));

    // Drift 3 target + waveform (was missing — drift3 rate/depth existed but had no target/wave)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::drift3Target, 1}, "Drift3 Target",
        toChoices(DriftTarget::kEntries), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::drift3Wave, 1}, "Drift3 Wave",
        toChoices(DriftWave::kEntries), 0));

    // ENV Amount (per envelope)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::ampAmount, 1}, "Amp Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::mod1Amount, 1}, "Mod1 Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::mod2Amount, 1}, "Mod2 Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));

    // Velocity sensitivity (per envelope, 0=fixed, 1=full velocity)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::ampVelSens, 1}, "Amp Vel Sens",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::mod1VelSens, 1}, "Mod1 Vel Sens",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::mod2VelSens, 1}, "Mod2 Vel Sens",
        juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));

    // ENV Loop (per envelope)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::ampLoop, 1}, "Amp Loop", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::mod1Loop, 1}, "Mod1 Loop", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::mod2Loop, 1}, "Mod2 Loop", false));

    // ENV Curve shapes (0=Log, 1=SLog, 2=Lin, 3=SExp, 4=Exp)  —  A/D default Lin(2), R default Exp(4)
    const juce::StringArray curveChoices = toChoices(EnvCurve::kEntries);
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::ampAttackCurve, 2},  "Amp Attack Curve",  curveChoices, 2));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::ampDecayCurve, 2},   "Amp Decay Curve",   curveChoices, 2));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::ampReleaseCurve, 2}, "Amp Release Curve", curveChoices, 4));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod1AttackCurve, 2},  "Mod1 Attack Curve",  curveChoices, 2));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod1DecayCurve, 2},   "Mod1 Decay Curve",   curveChoices, 2));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod1ReleaseCurve, 2}, "Mod1 Release Curve", curveChoices, 4));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod2AttackCurve, 2},  "Mod2 Attack Curve",  curveChoices, 2));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod2DecayCurve, 2},   "Mod2 Decay Curve",   curveChoices, 2));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod2ReleaseCurve, 2}, "Mod2 Release Curve", curveChoices, 4));

    // ENV / LFO target choice lists — the single source of truth lives in
    // src/dsp/BlockParams.h (EnvTarget::kEntries / LfoTarget::kEntries). The
    // enum, this APVTS StringArray and gui/SynthPanel.cpp all iterate the
    // same array, so the index↔label mapping cannot drift.
    juce::StringArray envTargetChoices;
    for (const auto& e : EnvTarget::kEntries) envTargetChoices.add(e.label);
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod1Target, 1}, "Mod1 Target", envTargetChoices, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod2Target, 1}, "Mod2 Target", envTargetChoices, 0));

    juce::StringArray lfoTargetChoices;
    for (const auto& e : LfoTarget::kEntries) lfoTargetChoices.add(e.label);
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::lfo1Target, 1}, "LFO1 Target", lfoTargetChoices, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::lfo2Target, 1}, "LFO2 Target", lfoTargetChoices, 0));

    // LFO Mode
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::lfo1Mode, 1}, "LFO1 Mode",
        toChoices(LfoMode::kEntries), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::lfo2Mode, 1}, "LFO2 Mode",
        toChoices(LfoMode::kEntries), 0));

    // Delay damp
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::delayDamp, 1}, "Delay Damp",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f));

    // Sampler controls
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::loopMode, 1}, "Loop Mode",
        toChoices(LoopMode::kEntries), 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::crossfadeMs, 1}, "Crossfade",
        juce::NormalisableRange<float>(0.0f, 500.0f, 10.0f), 150.0f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::normalize, 1}, "Normalize", true));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::loopOptimize, 2}, "Loop Optimize",
        toChoices(LoopOptimize::kEntries), 0));

    // Effect enables
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::filterEnabled, 1}, "Filter Enabled", true));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::delayType, 1}, "Delay Type",
        toChoices(DelayType::kEntries), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::reverbType, 1}, "Reverb Type",
        toChoices(ReverbType::kEntries), 0));

    // Limiter
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::limiterThresh, 1}, "Limiter Threshold",
        juce::NormalisableRange<float>(-30.0f, 0.0f, 0.1f), -3.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::limiterRelease, 1}, "Limiter Release",
        juce::NormalisableRange<float>(1.0f, 500.0f, 0.1f, 0.3f), 100.0f));

    // (reverb_ir merged into reverb_type switchbox)

    // Sequencer / Arpeggiator
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::seqMode, 1}, "Seq Mode",
        toChoices(SeqMode::kEntries), 0));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::seqRunning, 1}, "Seq Running", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::seqBpm, 1}, "Seq BPM",
        juce::NormalisableRange<float>(20.0f, 300.0f, 0.1f), 120.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::seqSteps, 1}, "Seq Steps", 1, 64, 16));
    // Sequencer note division (reference: 1/1, 1/2, 1/4, 1/8, 1/16)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::seqDivision, 1}, "Seq Division",
        toChoices(SeqDivision::kEntries), 4)); // default 1/16
    // Sequencer glide time (reference: 10-500ms, default 80)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::seqGlideTime, 1}, "Glide Time",
        juce::NormalisableRange<float>(10.0f, 500.0f, 1.0f), 80.0f));
    // Arp rate: musical divisions (reference: 1/4, 1/8, 1/16, 1/32, 1/4T, 1/8T, 1/16T)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::arpRate, 1}, "Arp Rate",
        toChoices(ArpRate::kEntries), 2));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::arpOctaves, 1}, "Arp Octaves", 1, 4, 1));
    // Arp mode (Off = disabled, rest = enabled with that pattern)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::arpMode, 1}, "Arp Mode",
        toChoices(ArpMode::kEntries), 0));
    // Global seq gate + preset
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::seqGate, 1}, "Seq Gate",
        juce::NormalisableRange<float>(0.1f, 1.0f, 0.01f), 0.8f));
    // Seq octave shift: -2..+2 octaves (choice index 0..4, default 2 = no shift)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::seqOctave, 1}, "Seq Octave",
        toChoices(SeqOctave::kEntries), 2));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::seqPreset, 1}, "Seq Preset",
        toChoices(SeqPreset::kEntries), 0));

    // Generative sequencer
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::genSeqRunning, 1}, "Gen Seq Running", false));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::genSteps, 1}, "Gen Steps", 2, 32, 21));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::genPulses, 1}, "Gen Pulses", 1, 32, 16));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::genRotation, 1}, "Gen Rotation", 0, 31, 2));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::genMutation, 1}, "Gen Mutation",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.80f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::genRange, 1}, "Gen Range",
        toChoices(GenRange::kEntries), 2)); // default index 2 = "3" octaves
    // Fix toggles — lock parameters against Euclidean drift
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::genFixSteps, 1}, "Fix Steps", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::genFixPulses, 1}, "Fix Pulses", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::genFixRotation, 1}, "Fix Rotation", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::genFixMutation, 1}, "Fix Mutation", true));
    // Scale (shared between gen seq and future features)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::scaleRoot, 1}, "Scale Root",
        toChoices(ScaleRoot::kEntries), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::scaleType, 1}, "Scale Type",
        toChoices(ScaleType::kEntries), 0));

    // HF boost: compensate VAE decoder high-frequency rolloff
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::genHfBoost, 1}, "HF Boost", true));

    // Octave shift: -2 to +2 (index 0-4, default 2 = 0 octaves)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::oscOctave, 1}, "Octave Shift",
        toChoices(OscOctave::kEntries), 2));

    // Noise oscillator: level + type
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::noiseLevel, 1}, "Noise Level",
        juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::noiseType, 1}, "Noise Type",
        toChoices(NoiseKind::kEntries), 0));

    // Wavetable frame count: 0=32, 1=64, 2=128, 3=256
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::wtFrames, 1}, "WT Frames",
        toChoices(WtFrames::kEntries), 3));

    // Wavetable smooth (Catmull-Rom interpolation between frames)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::wtSmooth, 1}, "WT Smooth", true));

    // Master volume: purely attenuative (0dB max). DAW fader handles boost.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::masterVol, 1}, "Master Volume",
        juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), 0.0f));

    return { params.begin(), params.end() };
}

void T5ynthProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    masterOsc.prepare(sampleRate, samplesPerBlock);
    masterSampler.prepare(sampleRate, samplesPerBlock);
    voiceManager.prepare(sampleRate, samplesPerBlock);
    lfo1.prepare(sampleRate);
    lfo2.prepare(sampleRate);
    postFilter.prepare(sampleRate, samplesPerBlock);
    delay.prepare(sampleRate, samplesPerBlock);
    reverb.prepare(sampleRate, samplesPerBlock);
    algoReverb.prepare(sampleRate, samplesPerBlock);
    // Load default IR (medium plate)
    reverb.loadImpulseResponse(BinaryData::emt_140_plate_medium_wav,
                               static_cast<size_t>(BinaryData::emt_140_plate_medium_wavSize));
    lastReverbIr = 1; // 0=Bright, 1=Medium, 2=Dark
    limiter.prepare(sampleRate, samplesPerBlock);
    stepSequencer.prepare(sampleRate, samplesPerBlock);
    generativeSequencer.prepare(sampleRate, samplesPerBlock);
    arpeggiator.prepare(sampleRate, samplesPerBlock);
    lfo1Buffer.resize(static_cast<size_t>(samplesPerBlock));
    lfo2Buffer.resize(static_cast<size_t>(samplesPerBlock));
    reverbSendBuffer.setSize(2, samplesPerBlock);

    silentBlockCount = 0;
    // Allow ~10 seconds of reverb tail before deep idle
    tailBlocks = std::max(1, static_cast<int>(10.0 * sampleRate / samplesPerBlock));
}

void T5ynthProcessor::releaseResources()
{
    masterOsc.reset();
    masterSampler.reset();
    voiceManager.reset();
}

void T5ynthProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();


    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // ── Idle detection ──────────────────────────────────────────────────────
    bool seqRunning = parameters.getRawParameterValue(PID::seqRunning)->load() > 0.5f;
    bool hasActivity = voiceManager.hasActiveVoices()
                       || !midiMessages.isEmpty()
                       || seqRunning;

    if (hasActivity)
        silentBlockCount = 0;
    else
        ++silentBlockCount;

    // PHASE 2: Deep idle (tails fully decayed) → buffer already cleared, just return
    if (silentBlockCount > tailBlocks)
    {
        audioIdle.store(true, std::memory_order_relaxed);
        // Keep free-running LFOs phase-accurate
        lfo1.advancePhase(numSamples);
        lfo2.advancePhase(numSamples);
        return;
    }
    audioIdle.store(false, std::memory_order_relaxed);

    // ── GAIN STAGING ────────────────────────────────────────────────────────
    // Per Voice: Osc +-1.0 → VCA up to +-4.0 → Filter (gain-neutral, reso +12dB)
    // Sum:       N voices * 1/sqrt(N) scaling → ~constant perceived loudness
    // Post-Sum:  Delay+Reverb up to ~2.7x → Master 0dB max → Limiter -3dB
    // ────────────────────────────────────────────────────────────────────────

    // ── Voice count ──────────────────────────────────────────────────────────
    {
        static constexpr int voiceCounts[] = { 1, 4, 6, 8, 12, 16 };
        int vcIdx = static_cast<int>(parameters.getRawParameterValue(PID::voiceCount)->load());
        voiceManager.setVoiceLimit(voiceCounts[juce::jlimit(0, 5, vcIdx)]);
    }

    // ── Tuning table ──────────────────────────────────────────────────────────
    {
        int tuningIdx = static_cast<int>(parameters.getRawParameterValue(PID::tuning)->load());
        int scaleRoot = static_cast<int>(parameters.getRawParameterValue(PID::scaleRoot)->load());
        auto tt = static_cast<Tuning::Type>(juce::jlimit(0, (int)Tuning::COUNT - 1, tuningIdx));
        Tuning::buildTable(tuningTable, tt, scaleRoot);
        voiceManager.setTuningTable(tuningTable);
    }

    // ── Read all parameters into BlockParams ──────────────────────────────────
    BlockParams bp;
    bp.ampAttack  = parameters.getRawParameterValue(PID::ampAttack)->load();
    bp.ampDecay   = parameters.getRawParameterValue(PID::ampDecay)->load();
    bp.ampSustain = parameters.getRawParameterValue(PID::ampSustain)->load();
    bp.ampRelease = parameters.getRawParameterValue(PID::ampRelease)->load();
    bp.ampAmount  = parameters.getRawParameterValue(PID::ampAmount)->load();
    bp.ampVelSens = parameters.getRawParameterValue(PID::ampVelSens)->load();
    bp.ampLoop    = parameters.getRawParameterValue(PID::ampLoop)->load() > 0.5f;
    bp.ampAttackCurve  = static_cast<int>(parameters.getRawParameterValue(PID::ampAttackCurve)->load());
    bp.ampDecayCurve   = static_cast<int>(parameters.getRawParameterValue(PID::ampDecayCurve)->load());
    bp.ampReleaseCurve = static_cast<int>(parameters.getRawParameterValue(PID::ampReleaseCurve)->load());

    bp.mod1Attack  = parameters.getRawParameterValue(PID::mod1Attack)->load();
    bp.mod1Decay   = parameters.getRawParameterValue(PID::mod1Decay)->load();
    bp.mod1Sustain = parameters.getRawParameterValue(PID::mod1Sustain)->load();
    bp.mod1Release = parameters.getRawParameterValue(PID::mod1Release)->load();
    bp.mod1Amount  = parameters.getRawParameterValue(PID::mod1Amount)->load();
    bp.mod1VelSens = parameters.getRawParameterValue(PID::mod1VelSens)->load();
    bp.mod1Target  = static_cast<int>(parameters.getRawParameterValue(PID::mod1Target)->load());
    bp.mod1Loop    = parameters.getRawParameterValue(PID::mod1Loop)->load() > 0.5f;
    bp.mod1AttackCurve  = static_cast<int>(parameters.getRawParameterValue(PID::mod1AttackCurve)->load());
    bp.mod1DecayCurve   = static_cast<int>(parameters.getRawParameterValue(PID::mod1DecayCurve)->load());
    bp.mod1ReleaseCurve = static_cast<int>(parameters.getRawParameterValue(PID::mod1ReleaseCurve)->load());

    bp.mod2Attack  = parameters.getRawParameterValue(PID::mod2Attack)->load();
    bp.mod2Decay   = parameters.getRawParameterValue(PID::mod2Decay)->load();
    bp.mod2Sustain = parameters.getRawParameterValue(PID::mod2Sustain)->load();
    bp.mod2Release = parameters.getRawParameterValue(PID::mod2Release)->load();
    bp.mod2Amount  = parameters.getRawParameterValue(PID::mod2Amount)->load();
    bp.mod2VelSens = parameters.getRawParameterValue(PID::mod2VelSens)->load();
    bp.mod2Target  = static_cast<int>(parameters.getRawParameterValue(PID::mod2Target)->load());
    bp.mod2Loop    = parameters.getRawParameterValue(PID::mod2Loop)->load() > 0.5f;
    bp.mod2AttackCurve  = static_cast<int>(parameters.getRawParameterValue(PID::mod2AttackCurve)->load());
    bp.mod2DecayCurve   = static_cast<int>(parameters.getRawParameterValue(PID::mod2DecayCurve)->load());
    bp.mod2ReleaseCurve = static_cast<int>(parameters.getRawParameterValue(PID::mod2ReleaseCurve)->load());

    // LFOs (global)
    lfo1.setRate(parameters.getRawParameterValue(PID::lfo1Rate)->load());
    lfo1.setDepth(parameters.getRawParameterValue(PID::lfo1Depth)->load());
    lfo1.setWaveform(static_cast<int>(parameters.getRawParameterValue(PID::lfo1Wave)->load()));
    bp.lfo1Target = static_cast<int>(parameters.getRawParameterValue(PID::lfo1Target)->load());

    lfo2.setRate(parameters.getRawParameterValue(PID::lfo2Rate)->load());
    lfo2.setDepth(parameters.getRawParameterValue(PID::lfo2Depth)->load());
    lfo2.setWaveform(static_cast<int>(parameters.getRawParameterValue(PID::lfo2Wave)->load()));
    bp.lfo2Target = static_cast<int>(parameters.getRawParameterValue(PID::lfo2Target)->load());

    // Filter
    // filter_type: 0=Off, 1=LP, 2=HP, 3=BP → filterEnabled from type, DSP type is 0-based
    {
        int ft = static_cast<int>(parameters.getRawParameterValue(PID::filterType)->load());
        bp.filterEnabled = (ft > 0);
        bp.filterType = ft > 0 ? ft - 1 : 0;  // 0=LP, 1=HP, 2=BP for DSP
    }
    bp.baseCutoff = parameters.getRawParameterValue(PID::filterCutoff)->load();
    bp.baseReso = parameters.getRawParameterValue(PID::filterResonance)->load();
    bp.filterSlope = static_cast<int>(parameters.getRawParameterValue(PID::filterSlope)->load());
    bp.filterMix = parameters.getRawParameterValue(PID::filterMix)->load();
    bp.kbdTrack = parameters.getRawParameterValue(PID::filterKbdTrack)->load();

    // Scan
    bp.baseScan = parameters.getRawParameterValue(PID::oscScan)->load();

    // Octave shift (APVTS choice 0-4, maps to -2..+2)
    bp.octaveShift = static_cast<int>(parameters.getRawParameterValue(PID::oscOctave)->load()) - 2;

    // Noise oscillator
    bp.noiseLevel = parameters.getRawParameterValue(PID::noiseLevel)->load();
    bp.noiseType = static_cast<int>(parameters.getRawParameterValue(PID::noiseType)->load());

    // Wavetable smooth
    bp.wtSmooth = parameters.getRawParameterValue(PID::wtSmooth)->load() > 0.5f;

    // Engine mode — read directly from APVTS (0=Sampler, 1=Wavetable)
    int engineModeRaw = static_cast<int>(parameters.getRawParameterValue(PID::engineMode)->load());
    bp.engineIsWavetable = (engineModeRaw == 1);
    voiceManager.setEngineMode(bp.engineIsWavetable ? SynthVoice::EngineMode::Wavetable
                                                     : SynthVoice::EngineMode::Sampler);

    // Master volume (dB → linear)
    float masterDb = parameters.getRawParameterValue(PID::masterVol)->load();
    float masterGain = juce::Decibels::decibelsToGain(masterDb);

    // Drift LFOs (block-rate)
    driftLfo.setRegenMode(static_cast<int>(parameters.getRawParameterValue(PID::driftRegen)->load()));
    int d1t = static_cast<int>(parameters.getRawParameterValue(PID::drift1Target)->load());
    int d2t = static_cast<int>(parameters.getRawParameterValue(PID::drift2Target)->load());
    int d3t = static_cast<int>(parameters.getRawParameterValue(PID::drift3Target)->load());
    // Auto-enable drift when any target is set (target 0 = "---" = None)
    bool driftHasTarget = (d1t != 0) || (d2t != 0) || (d3t != 0);
    // Osc targets (Alpha, Axis1-3, Noise, Magnitude) require regeneration
    bool hasOsc = false;
    for (int t : {d1t, d2t, d3t})
        if ((t >= DriftLFO::TgtAlpha && t <= DriftLFO::TgtAxis3)
            || t == DriftLFO::TgtNoise || t == DriftLFO::TgtMagnitude) hasOsc = true;
    driftHasOscTarget.store(hasOsc, std::memory_order_relaxed);
    driftRegenMode.store(static_cast<int>(parameters.getRawParameterValue(PID::driftRegen)->load()),
                         std::memory_order_relaxed);
    bool driftManualEnable = parameters.getRawParameterValue(PID::driftEnabled)->load() > 0.5f;
    driftLfo.setEnabled(driftHasTarget || driftManualEnable);
    driftLfo.setLfoRate(0, parameters.getRawParameterValue(PID::drift1Rate)->load());
    driftLfo.setLfoDepth(0, parameters.getRawParameterValue(PID::drift1Depth)->load());
    driftLfo.setLfoTarget(0, d1t);
    driftLfo.setLfoWaveform(0, static_cast<int>(parameters.getRawParameterValue(PID::drift1Wave)->load()));
    driftLfo.setLfoRate(1, parameters.getRawParameterValue(PID::drift2Rate)->load());
    driftLfo.setLfoDepth(1, parameters.getRawParameterValue(PID::drift2Depth)->load());
    driftLfo.setLfoTarget(1, d2t);
    driftLfo.setLfoWaveform(1, static_cast<int>(parameters.getRawParameterValue(PID::drift2Wave)->load()));
    driftLfo.setLfoRate(2, parameters.getRawParameterValue(PID::drift3Rate)->load());
    driftLfo.setLfoDepth(2, parameters.getRawParameterValue(PID::drift3Depth)->load());
    driftLfo.setLfoTarget(2, d3t);
    driftLfo.setLfoWaveform(2, static_cast<int>(parameters.getRawParameterValue(PID::drift3Wave)->load()));
    driftLfo.tick(static_cast<double>(numSamples) / getSampleRate());

    // Apply drift offsets to their respective targets
    bp.driftScanOffset   = driftLfo.getOffsetForTarget(DriftLFO::TgtWtScan);
    bp.driftFilterOffset = driftLfo.getOffsetForTarget(DriftLFO::TgtFilter);
    bp.driftPitchOffset  = driftLfo.getOffsetForTarget(DriftLFO::TgtPitch);
    // Block-level drift targets (delay/reverb) applied after modDelayTime etc. are declared

    // Drift → envelope amounts (additive, clamped to 0–1)
    bp.ampAmount  = juce::jlimit(0.0f, 1.0f, bp.ampAmount  + driftLfo.getOffsetForTarget(DriftLFO::TgtEnv1Amt));
    bp.mod1Amount = juce::jlimit(0.0f, 1.0f, bp.mod1Amount + driftLfo.getOffsetForTarget(DriftLFO::TgtEnv2Amt));
    bp.mod2Amount = juce::jlimit(0.0f, 1.0f, bp.mod2Amount + driftLfo.getOffsetForTarget(DriftLFO::TgtEnv3Amt));

    // Drift → Osc targets (Alpha, Axes, Noise, Magnitude) — store effective values for GUI ghost + auto-regen
    {
        static constexpr float NO_GHOST = std::numeric_limits<float>::quiet_NaN();
        float alphaOff = driftLfo.getOffsetForTarget(DriftLFO::TgtAlpha);
        float ax1Off   = driftLfo.getOffsetForTarget(DriftLFO::TgtAxis1);
        float ax2Off   = driftLfo.getOffsetForTarget(DriftLFO::TgtAxis2);
        float ax3Off   = driftLfo.getOffsetForTarget(DriftLFO::TgtAxis3);
        float noiseOff = driftLfo.getOffsetForTarget(DriftLFO::TgtNoise);
        float magOff   = driftLfo.getOffsetForTarget(DriftLFO::TgtMagnitude);
        float baseAlpha = parameters.getRawParameterValue(PID::genAlpha)->load();
        modulatedValues.driftAlpha.store(
            std::abs(alphaOff) > 0.001f ? baseAlpha + alphaOff : NO_GHOST,
            std::memory_order_relaxed);
        modulatedValues.driftAxis1.store(
            std::abs(ax1Off) > 0.001f ? ax1Off : NO_GHOST, std::memory_order_relaxed);
        modulatedValues.driftAxis2.store(
            std::abs(ax2Off) > 0.001f ? ax2Off : NO_GHOST, std::memory_order_relaxed);
        modulatedValues.driftAxis3.store(
            std::abs(ax3Off) > 0.001f ? ax3Off : NO_GHOST, std::memory_order_relaxed);
        float baseNoise = parameters.getRawParameterValue(PID::genNoise)->load();
        modulatedValues.driftNoise.store(
            std::abs(noiseOff) > 0.001f ? baseNoise + noiseOff : NO_GHOST,
            std::memory_order_relaxed);
        float baseMag = parameters.getRawParameterValue(PID::genMagnitude)->load();
        modulatedValues.driftMagnitude.store(
            std::abs(magOff) > 0.001f ? baseMag + magOff : NO_GHOST,
            std::memory_order_relaxed);
    }

    // ── Sampler settings ─────────────────────────────────────────────────────
    int loopModeIdx = static_cast<int>(parameters.getRawParameterValue(PID::loopMode)->load());
    masterSampler.setLoopMode(static_cast<SamplePlayer::LoopMode>(loopModeIdx));
    masterSampler.setCrossfadeMs(parameters.getRawParameterValue(PID::crossfadeMs)->load());
    masterSampler.setNormalize(parameters.getRawParameterValue(PID::normalize)->load() > 0.5f);
    masterSampler.setLoopOptimizeLevel(static_cast<int>(parameters.getRawParameterValue(PID::loopOptimize)->load()));

    // ── Sequencer / Arpeggiator (in series: Seq → Arp → synth) ─────────────
    // (seqRunning already read above for idle detection)
    float seqBpm = parameters.getRawParameterValue(PID::seqBpm)->load();
    int seqSteps = static_cast<int>(parameters.getRawParameterValue(PID::seqSteps)->load());
    float seqGate = parameters.getRawParameterValue(PID::seqGate)->load();
    int seqPreset = static_cast<int>(parameters.getRawParameterValue(PID::seqPreset)->load());
    int arpModeRaw = static_cast<int>(parameters.getRawParameterValue(PID::arpMode)->load());
    bool arpEnabled = arpModeRaw > 0;
    int arpMode = arpModeRaw > 0 ? arpModeRaw - 1 : 0; // 0=Up,1=Down,2=UpDown,3=Random
    int arpRate = static_cast<int>(parameters.getRawParameterValue(PID::arpRate)->load());
    int arpOctaves = static_cast<int>(parameters.getRawParameterValue(PID::arpOctaves)->load());

    // Preset change detection
    if (seqPreset != lastSeqPreset)
    {
        stepSequencer.loadPreset(seqPreset);
        lastSeqPreset = seqPreset;
    }

    // GEN mode toggle — PLAY is master transport, GEN switches engine
    bool genModeWanted = parameters.getRawParameterValue(PID::genSeqRunning)->load() > 0.5f;
    int seqDivision = static_cast<int>(parameters.getRawParameterValue(PID::seqDivision)->load());

    // Detect mode switch: apply at step 0 boundary
    if (genModeWanted != genModeActiveInAudio)
    {
        // Check if either sequencer is at step 0 (or not running yet)
        bool atBarBoundary = !seqRunning
            || stepSequencer.getCurrentStep() == 0
            || generativeSequencer.currentStepForGui.load(std::memory_order_relaxed) <= 0;

        if (atBarBoundary)
        {
            if (genModeWanted)
            {
                // Step → Gen: seed from current step seq pattern, then switch
                int seqCount = stepSequencer.getNumSteps();
                int seedNotes[T5ynthStepSequencer::MAX_STEPS]{};
                bool seedEnabled[T5ynthStepSequencer::MAX_STEPS]{};
                int pulseCount = 0;
                for (int i = 0; i < seqCount; ++i)
                {
                    const auto& step = stepSequencer.getStep(i);
                    seedNotes[i] = step.note;
                    seedEnabled[i] = step.enabled;
                    if (step.enabled) pulseCount++;
                }

                stepSequencer.stop();
                generativeSequencer.setBpm(static_cast<double>(seqBpm));
                generativeSequencer.setDivision(seqDivision);
                generativeSequencer.setGate(seqGate);
                generativeSequencer.setScale(
                    static_cast<int>(parameters.getRawParameterValue(PID::scaleType)->load()),
                    static_cast<int>(parameters.getRawParameterValue(PID::scaleRoot)->load()));
                generativeSequencer.seedFromSteps(seedNotes, seedEnabled, seqCount);

                // Update gen params to match seeded pattern
                if (auto* par = parameters.getParameter(PID::genSteps))
                    par->setValueNotifyingHost(par->convertTo0to1(static_cast<float>(seqCount)));
                if (auto* par = parameters.getParameter(PID::genPulses))
                    par->setValueNotifyingHost(par->convertTo0to1(static_cast<float>(pulseCount)));

                lastGenSteps = lastGenPulses = lastGenRotation = -1;
                lastGenMutation = -1.0f;
                genModeActiveInAudio = true;
            }
            else
            {
                // Gen → Step: copy generated pattern into step data, then switch
                int genSteps = generativeSequencer.numStepsForGui.load(std::memory_order_relaxed);
                if (genSteps > 0)
                {
                    stepSequencer.setNumSteps(genSteps);
                    if (auto* par = parameters.getParameter(PID::seqSteps))
                        par->setValueNotifyingHost(par->convertTo0to1(static_cast<float>(genSteps)));
                    for (int i = 0; i < genSteps; ++i)
                    {
                        int note = generativeSequencer.notePatternForGui[static_cast<size_t>(i)]
                            .load(std::memory_order_relaxed);
                        if (note > 0)
                        {
                            stepSequencer.setStepNote(i, note);
                            stepSequencer.setStepEnabled(i, true);
                        }
                        else
                        {
                            stepSequencer.setStepEnabled(i, false);
                        }
                    }
                }
                generativeSequencer.stop();
                genModeActiveInAudio = false;
            }
        }
    }

    // Configure + run the active engine
    if (genModeActiveInAudio)
    {
        generativeSequencer.setBpm(static_cast<double>(seqBpm));
        generativeSequencer.setDivision(seqDivision);
        generativeSequencer.setGate(seqGate);

        // Fix flags
        bool fxS = parameters.getRawParameterValue(PID::genFixSteps)->load() > 0.5f;
        bool fxP = parameters.getRawParameterValue(PID::genFixPulses)->load() > 0.5f;
        bool fxR = parameters.getRawParameterValue(PID::genFixRotation)->load() > 0.5f;
        bool fxM = parameters.getRawParameterValue(PID::genFixMutation)->load() > 0.5f;
        generativeSequencer.setFixSteps(fxS);
        generativeSequencer.setFixPulses(fxP);
        generativeSequencer.setFixRotation(fxR);
        generativeSequencer.setFixMutation(fxM);

        // Steps/Pulses/Rotation: if FIXED, overwrite every block.
        // If UNFIXED, only overwrite when user changes the slider.
        {
            int gs = static_cast<int>(parameters.getRawParameterValue(PID::genSteps)->load());
            int gp = static_cast<int>(parameters.getRawParameterValue(PID::genPulses)->load());
            int gr = static_cast<int>(parameters.getRawParameterValue(PID::genRotation)->load());

            if (fxS || gs != lastGenSteps)    { generativeSequencer.setSteps(gs);    lastGenSteps = gs; }
            if (fxP || gp != lastGenPulses)   { generativeSequencer.setPulses(gp);   lastGenPulses = gp; }
            if (fxR || gr != lastGenRotation) { generativeSequencer.setRotation(gr); lastGenRotation = gr; }
        }

        // Mutation: always update base; only overwrite effective rate if fixed or user changed slider
        {
            float gm = parameters.getRawParameterValue(PID::genMutation)->load();
            generativeSequencer.setBaseMutation(gm);
            if (fxM || gm != lastGenMutation) { generativeSequencer.setMutation(gm); lastGenMutation = gm; }
        }

        generativeSequencer.setRange(static_cast<int>(parameters.getRawParameterValue(PID::genRange)->load()) + 1);
        generativeSequencer.setScale(
            static_cast<int>(parameters.getRawParameterValue(PID::scaleType)->load()),
            static_cast<int>(parameters.getRawParameterValue(PID::scaleRoot)->load()));

        if (seqRunning)
            generativeSequencer.start();
        else
            generativeSequencer.stop();
        generativeSequencer.processBlock(buffer, midiMessages);

        // Write effective (post-drift) values back to APVTS so sliders move
        {
            int effS = generativeSequencer.effectiveStepsForGui.load(std::memory_order_relaxed);
            int effP = generativeSequencer.effectivePulsesForGui.load(std::memory_order_relaxed);
            float effM = generativeSequencer.effectiveMutationForGui.load(std::memory_order_relaxed);

            if (!fxS && effS != lastGenSteps)
            {
                if (auto* par = parameters.getParameter(PID::genSteps))
                    par->setValueNotifyingHost(par->convertTo0to1(static_cast<float>(effS)));
                lastGenSteps = effS;
            }
            if (!fxP && effP != lastGenPulses)
            {
                if (auto* par = parameters.getParameter(PID::genPulses))
                    par->setValueNotifyingHost(par->convertTo0to1(static_cast<float>(effP)));
                lastGenPulses = effP;
            }
            if (!fxM && effM != lastGenMutation)
            {
                if (auto* par = parameters.getParameter(PID::genMutation))
                    par->setValueNotifyingHost(par->convertTo0to1(effM));
                lastGenMutation = effM;
            }
        }
    }
    else
    {
        stepSequencer.setBpm(static_cast<double>(seqBpm));
        stepSequencer.setNumSteps(seqSteps);
        stepSequencer.setDivision(seqDivision);
        stepSequencer.setAllGates(seqGate);

        if (seqRunning)
            stepSequencer.start();
        else
            stepSequencer.stop();
        stepSequencer.processBlock(buffer, midiMessages);
    }

    // Octave transposition (both modes)
    int seqOctaveIdx = static_cast<int>(parameters.getRawParameterValue(PID::seqOctave)->load());
    int semitoneShift = (seqOctaveIdx - 2) * 12;
    if (semitoneShift != 0 && seqRunning)
    {
        juce::MidiBuffer transposed;
        for (const auto metadata : midiMessages)
        {
            auto msg = metadata.getMessage();
            if (msg.isNoteOnOrOff())
                msg.setNoteNumber(juce::jlimit(0, 127, msg.getNoteNumber() + semitoneShift));
            transposed.addEvent(msg, metadata.samplePosition);
        }
        midiMessages.swapWith(transposed);
    }

    // Consume bar-start flag (still used for sequencer display)
    if (stepSequencer.barStartFlag.exchange(false))
        barBoundaryFlag.store(true, std::memory_order_relaxed);

    // Expose current BPM for drift regen cooldown (GUI reads)
    driftRegenBpm.store(seqBpm, std::memory_order_relaxed);

    // Stage 2: Arpeggiator (consumes seq note events, generates arpeggiated output)
    if (arpEnabled)
    {
        arpeggiator.setBpm(static_cast<double>(seqBpm));
        arpeggiator.setRate(arpRate);
        arpeggiator.setOctaveRange(arpOctaves);
        arpeggiator.setMode(static_cast<T5ynthArpeggiator::Mode>(arpMode));

        juce::MidiBuffer filtered;
        for (const auto metadata : midiMessages)
        {
            auto msg = metadata.getMessage();
            if (msg.isNoteOn())
                arpeggiator.setBaseNote(msg.getNoteNumber(), msg.getFloatVelocity());
            else if (msg.isNoteOff())
            {
                // When seq is running, don't kill arp on seq gate-offs —
                // arp runs continuously, seq just updates the base note
                if (!seqRunning)
                    arpeggiator.stopArp();
            }
            else
                filtered.addEvent(msg, metadata.samplePosition);
        }
        midiMessages.swapWith(filtered);
        arpeggiator.processBlock(buffer, midiMessages);
    }
    else
    {
        // Clean up any hanging arp note before resetting
        if (arpeggiator.getLastPlayedNote() >= 0)
            midiMessages.addEvent(juce::MidiMessage::noteOff(1, arpeggiator.getLastPlayedNote()), 0);
        arpeggiator.reset();
    }

    // (barStartFlag consumed + forwarded to barBoundaryFlag above)

    // ── Sample-accurate MIDI + Voice rendering ──────────────────────────────
    bool lfo1TrigMode = static_cast<int>(parameters.getRawParameterValue(PID::lfo1Mode)->load()) == 1;
    bool lfo2TrigMode = static_cast<int>(parameters.getRawParameterValue(PID::lfo2Mode)->load()) == 1;

    // Re-check: seq/arp may have generated notes
    if (voiceManager.hasActiveVoices() || !midiMessages.isEmpty())
        silentBlockCount = 0;

    bool skipSynthesis = (silentBlockCount > 0 && !voiceManager.hasActiveVoices()
                          && midiMessages.isEmpty());

    // Modulation values (zero when skipping synthesis)
    float modDelayTime = 0.0f, modDelayFb = 0.0f, modDelayMix = 0.0f, modReverbMix = 0.0f;

    // Drift → block-level FX targets (runs even during tail for smooth drift)
    modDelayTime += driftLfo.getOffsetForTarget(DriftLFO::TgtDelayTime);
    modDelayFb   += driftLfo.getOffsetForTarget(DriftLFO::TgtDelayFb);
    modDelayMix  += driftLfo.getOffsetForTarget(DriftLFO::TgtDelayMix);
    modReverbMix += driftLfo.getOffsetForTarget(DriftLFO::TgtReverbMix);
    VoiceManager::VoiceOutput voiceOut;

    if (!skipSynthesis)
    {
        // ── Audio generation via VoiceManager + global LFOs ─────────────────────
        float baseLfo1Rate = parameters.getRawParameterValue(PID::lfo1Rate)->load();
        float baseLfo2Rate = parameters.getRawParameterValue(PID::lfo2Rate)->load();
        float baseLfo1Depth = parameters.getRawParameterValue(PID::lfo1Depth)->load();
        float baseLfo2Depth = parameters.getRawParameterValue(PID::lfo2Depth)->load();

        // Re-prepare master sampler if settings changed, then distribute to all voices
        if (masterSampler.hasAudio())
        {
            if (masterSampler.needsReprepare())
                masterSampler.preparePlaybackBuffer();
            voiceManager.distributeSamplerBuffer(masterSampler);
        }

        // Pre-compute global LFO values for the block (needed by VoiceManager)
        float* lfo1Buf = lfo1Buffer.data();
        float* lfo2Buf = lfo2Buffer.data();
        for (int i = 0; i < numSamples; ++i)
        {
            float l1 = lfo1.processSample();
            float l2 = lfo2.processSample();

            lfo1Buf[i] = l1;
            lfo2Buf[i] = l2;
        }

        // LFO → envelope amounts (multiplicative, clamped to 0–1)
        {
            float l1End = numSamples > 0 ? lfo1Buf[numSamples - 1] : 0.0f;
            float l2End = numSamples > 0 ? lfo2Buf[numSamples - 1] : 0.0f;
            if (bp.lfo1Target == LfoTarget::Env1Amt) bp.ampAmount  = juce::jlimit(0.0f, 1.0f, bp.ampAmount  * (1.0f + l1End));
            if (bp.lfo1Target == LfoTarget::Env2Amt) bp.mod1Amount = juce::jlimit(0.0f, 1.0f, bp.mod1Amount * (1.0f + l1End));
            if (bp.lfo1Target == LfoTarget::Env3Amt) bp.mod2Amount = juce::jlimit(0.0f, 1.0f, bp.mod2Amount * (1.0f + l1End));
            if (bp.lfo2Target == LfoTarget::Env1Amt) bp.ampAmount  = juce::jlimit(0.0f, 1.0f, bp.ampAmount  * (1.0f + l2End));
            if (bp.lfo2Target == LfoTarget::Env2Amt) bp.mod1Amount = juce::jlimit(0.0f, 1.0f, bp.mod1Amount * (1.0f + l2End));
            if (bp.lfo2Target == LfoTarget::Env3Amt) bp.mod2Amount = juce::jlimit(0.0f, 1.0f, bp.mod2Amount * (1.0f + l2End));
        }

        // Scan → P1 modulation offset (Sampler mode: retrigger uses it)
        if (!bp.engineIsWavetable)
        {
            float p1Mod = bp.driftScanOffset;
            float l1 = numSamples > 0 ? lfo1Buf[numSamples - 1] : 0.0f;
            float l2 = numSamples > 0 ? lfo2Buf[numSamples - 1] : 0.0f;
            if (bp.lfo1Target == LfoTarget::Scan) p1Mod += l1;
            if (bp.lfo2Target == LfoTarget::Scan) p1Mod += l2;
            masterSampler.setStartPosOffset(p1Mod);
        }
        else
        {
            masterSampler.setStartPosOffset(0.0f);
        }

        // ── Sample-accurate rendering: split block at MIDI event boundaries ──
        {
            auto midiIter = midiMessages.cbegin();
            int renderPos = 0;

            while (renderPos < numSamples)
            {
                // Next sub-block ends at the next MIDI event or block end
                int subEnd = numSamples;
                if (midiIter != midiMessages.cend())
                    subEnd = juce::jmin((*midiIter).samplePosition, numSamples);

                // Render voices up to this point
                int subLen = subEnd - renderPos;
                if (subLen > 0)
                    voiceOut = voiceManager.renderBlock(buffer, bp,
                        lfo1Buf + renderPos, lfo2Buf + renderPos, renderPos, subLen);

                // Process all MIDI events at this sample position
                while (midiIter != midiMessages.cend()
                       && (*midiIter).samplePosition <= subEnd)
                {
                    const auto msg = (*midiIter).getMessage();
                    if (msg.isNoteOn())
                    {
                        int note = msg.getNoteNumber();
                        float velocity = msg.getFloatVelocity();
                        bool isBind = (msg.getChannel() == 2);

                        lastMidiNote.store(note, std::memory_order_relaxed);
                        lastMidiVelocity.store(juce::roundToInt(velocity * 127.0f),
                                               std::memory_order_relaxed);
                        lastMidiNoteOn.store(true, std::memory_order_relaxed);

                        voiceManager.noteOn(note, velocity, isBind,
                            isBind ? 0.0f : stepSequencer.getGlideTime(),
                            lfo1TrigMode, lfo2TrigMode);
                    }
                    else if (msg.isNoteOff())
                    {
                        voiceManager.noteOff(msg.getNoteNumber());
                        if (!voiceManager.hasActiveVoices())
                            lastMidiNoteOn.store(false, std::memory_order_relaxed);
                    }
                    ++midiIter;
                }

                renderPos = subEnd;
            }
        }
        lastTriggeredNote = voiceOut.lastTriggeredNote;

        // Capture last LFO values for block-rate modulation + ghost display
        float lastMod1Val = voiceOut.lastMod1Val;
        float lastMod2Val = voiceOut.lastMod2Val;
        float lastLfo1Val = numSamples > 0 ? lfo1Buf[numSamples - 1] : 0.0f;
        float lastLfo2Val = numSamples > 0 ? lfo2Buf[numSamples - 1] : 0.0f;
        lastLfo1Val_ = lastLfo1Val;
        lastLfo2Val_ = lastLfo2Val;

        // Filter is now per-voice (in SynthVoice::renderSample)

        // ── Accumulate block-rate modulation for delay/reverb ─────────────────
        // (Pitch modulation is handled per-sample in SynthVoice::renderSample)
        if (bp.mod1Target == EnvTarget::DelayTime)  modDelayTime += lastMod1Val;
        if (bp.mod1Target == EnvTarget::DelayFB)    modDelayFb += lastMod1Val;
        if (bp.mod1Target == EnvTarget::DelayMix)   modDelayMix += lastMod1Val;
        if (bp.mod1Target == EnvTarget::ReverbMix)  modReverbMix += lastMod1Val;
        if (bp.mod2Target == EnvTarget::DelayTime)  modDelayTime += lastMod2Val;
        if (bp.mod2Target == EnvTarget::DelayFB)    modDelayFb += lastMod2Val;
        if (bp.mod2Target == EnvTarget::DelayMix)   modDelayMix += lastMod2Val;
        if (bp.mod2Target == EnvTarget::ReverbMix)  modReverbMix += lastMod2Val;

        // Env → LFO modulation
        if (bp.mod1Target == EnvTarget::LFO1Rate)   lfo1.setRate(baseLfo1Rate * (1.0f + lastMod1Val));
        if (bp.mod1Target == EnvTarget::LFO1Depth)  lfo1.setDepth(std::max(0.0f, baseLfo1Depth + lastMod1Val * baseLfo1Depth));
        if (bp.mod1Target == EnvTarget::LFO2Rate)   lfo2.setRate(baseLfo2Rate * (1.0f + lastMod1Val));
        if (bp.mod1Target == EnvTarget::LFO2Depth)  lfo2.setDepth(std::max(0.0f, baseLfo2Depth + lastMod1Val * baseLfo2Depth));
        if (bp.mod2Target == EnvTarget::LFO1Rate)   lfo1.setRate(baseLfo1Rate * (1.0f + lastMod2Val));
        if (bp.mod2Target == EnvTarget::LFO1Depth)  lfo1.setDepth(std::max(0.0f, baseLfo1Depth + lastMod2Val * baseLfo1Depth));
        if (bp.mod2Target == EnvTarget::LFO2Rate)   lfo2.setRate(baseLfo2Rate * (1.0f + lastMod2Val));
        if (bp.mod2Target == EnvTarget::LFO2Depth)  lfo2.setDepth(std::max(0.0f, baseLfo2Depth + lastMod2Val * baseLfo2Depth));
        if (bp.lfo1Target == LfoTarget::DelayTime)  modDelayTime += lastLfo1Val;
        if (bp.lfo1Target == LfoTarget::DelayFB)    modDelayFb += lastLfo1Val;
        if (bp.lfo1Target == LfoTarget::DelayMix)   modDelayMix += lastLfo1Val;
        if (bp.lfo1Target == LfoTarget::ReverbMix)  modReverbMix += lastLfo1Val;
        if (bp.lfo2Target == LfoTarget::DelayTime)  modDelayTime += lastLfo2Val;
        if (bp.lfo2Target == LfoTarget::DelayFB)    modDelayFb += lastLfo2Val;
        if (bp.lfo2Target == LfoTarget::DelayMix)   modDelayMix += lastLfo2Val;
        if (bp.lfo2Target == LfoTarget::ReverbMix)  modReverbMix += lastLfo2Val;
    }
    else
    {
        // Free-running LFOs: sample one value for ghost display, advance rest
        if (numSamples > 0)
        {
            lastLfo1Val_ = lfo1.processSample();
            lastLfo2Val_ = lfo2.processSample();
            if (numSamples > 1)
            {
                lfo1.advancePhase(numSamples - 1);
                lfo2.advancePhase(numSamples - 1);
            }
        }
    }

    // ── Effects (parallel send-bus: dry + delay + reverb → limiter) ───────
    int delayType = static_cast<int>(parameters.getRawParameterValue(PID::delayType)->load());
    bool delayEnabled = delayType > 0;
    int reverbType = static_cast<int>(parameters.getRawParameterValue(PID::reverbType)->load());
    bool reverbEnabled = reverbType > 0;
    bool reverbIsAlgo = reverbType == 4;

    if (delayEnabled)
    {
        float baseDelayTime = parameters.getRawParameterValue(PID::delayTime)->load();
        float baseDelayFb = parameters.getRawParameterValue(PID::delayFeedback)->load();
        float baseDelayMix = parameters.getRawParameterValue(PID::delayMix)->load();
        // Apply modulation offsets to delay params
        delay.setTime(juce::jlimit(1.0f, 5000.0f, baseDelayTime * (1.0f + modDelayTime)));
        delay.setFeedback(juce::jlimit(0.0f, 0.95f, baseDelayFb + modDelayFb));
        delay.setMix(juce::jlimit(0.0f, 1.0f, baseDelayMix + modDelayMix));
        delay.setDamp(parameters.getRawParameterValue(PID::delayDamp)->load());
    }

    if (reverbEnabled)
    {
        float baseRevMix = parameters.getRawParameterValue(PID::reverbMix)->load();
        float revMix = juce::jlimit(0.0f, 1.0f, baseRevMix + modReverbMix);

        if (reverbIsAlgo)
        {
            algoReverb.setRoomSize(parameters.getRawParameterValue(PID::algoRoom)->load());
            algoReverb.setDamping(parameters.getRawParameterValue(PID::algoDamping)->load());
            algoReverb.setWidth(parameters.getRawParameterValue(PID::algoWidth)->load());
            algoReverb.setMix(revMix);
        }
        else
        {
            // Convolution: map reverb_type 1=Dark→2, 2=Medium→1, 3=Bright→0
            int irIndex = reverbType == 1 ? 2 : reverbType == 2 ? 1 : 0;
            if (irIndex != lastReverbIr)
            {
                const void* irData = nullptr;
                size_t irSize = 0;
                switch (irIndex)
                {
                    case 0: irData = BinaryData::emt_140_plate_bright_wav;
                            irSize = static_cast<size_t>(BinaryData::emt_140_plate_bright_wavSize); break;
                    case 1: irData = BinaryData::emt_140_plate_medium_wav;
                            irSize = static_cast<size_t>(BinaryData::emt_140_plate_medium_wavSize); break;
                    case 2: irData = BinaryData::emt_140_plate_dark_wav;
                            irSize = static_cast<size_t>(BinaryData::emt_140_plate_dark_wavSize); break;
                }
                if (irData != nullptr)
                {
                    reverb.loadImpulseResponse(irData, irSize);
                    lastReverbIr = irIndex;
                }
            }
            reverb.setMix(revMix);
        }
    }

    // Parallel send-bus architecture (reference useEffects.ts):
    //   source → dry(compensated) ──→ sum → limiter
    //          → delaySend ─────────→ sum
    //          → reverbSend ────────→ sum
    // Combined peak: delay ~1.7x + reverb wet additive → up to ~2.7x before master.
    //
    // Delay already implements send-bus internally (dry*comp + wet*mix).
    // When both FX are active, reverb needs the ORIGINAL source, not delay output.
    // Helper lambda: process reverb (convolution or algo) on a buffer
    auto processReverb = [&](juce::AudioBuffer<float>& buf) {
        if (reverbIsAlgo)
            algoReverb.processBlock(buf);
        else
            reverb.processBlock(buf);
    };

    if (delayEnabled && reverbEnabled)
    {
        // Save original source for reverb (pre-allocated buffer, no heap alloc)
        for (int ch = 0; ch < numChannels; ++ch)
            reverbSendBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        // Delay modifies buffer in-place: output = dry*comp + delayed*mix
        delay.processBlock(buffer);

        // Process reverb on original source (wet-only: set mix=1 temporarily)
        float savedRevMix = juce::jlimit(0.0f, 1.0f,
            parameters.getRawParameterValue(PID::reverbMix)->load() + modReverbMix);
        if (reverbIsAlgo) { algoReverb.setMix(1.0f); } else { reverb.setMix(1.0f); }
        processReverb(reverbSendBuffer);
        if (reverbIsAlgo) { algoReverb.setMix(savedRevMix); } else { reverb.setMix(savedRevMix); }

        // Add reverb send to output: buffer += convolved * reverbMix
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* rev = reverbSendBuffer.getReadPointer(ch);
            auto* out = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
                out[i] += rev[i] * savedRevMix;
        }
    }
    else if (delayEnabled)
    {
        delay.processBlock(buffer);
    }
    else if (reverbEnabled)
    {
        // Reverb as send: dry signal stays, wet is added on top
        for (int ch = 0; ch < numChannels; ++ch)
            reverbSendBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        float revMixVal = juce::jlimit(0.0f, 1.0f,
            parameters.getRawParameterValue(PID::reverbMix)->load() + modReverbMix);
        if (reverbIsAlgo) { algoReverb.setMix(1.0f); } else { reverb.setMix(1.0f); }
        processReverb(reverbSendBuffer);
        if (reverbIsAlgo) { algoReverb.setMix(revMixVal); } else { reverb.setMix(revMixVal); }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* rev = reverbSendBuffer.getReadPointer(ch);
            auto* out = buffer.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
                out[i] += rev[i] * revMixVal;
        }
    }

    // ── Update modulated values for GUI ghost indicators ────────────────────
    // LFO-driven ghosts run continuously (LFOs are free-running) so the user
    // sees modulation movement even between notes. Envelope-driven ghosts
    // still require active voices (envelopes only produce values when a
    // voice is playing).
    constexpr float NO_GHOST = std::numeric_limits<float>::quiet_NaN();

    {
        bool hasVoices = voiceOut.hasActiveVoices;

        // Filter cutoff ghost
        {
            bool lfoModFilter = bp.lfo1Target == LfoTarget::Filter || bp.lfo2Target == LfoTarget::Filter;
            bool envModFilter = (bp.mod1Target == EnvTarget::Filter || bp.mod2Target == EnvTarget::Filter
                                 || bp.kbdTrack > 0.0f) && hasVoices;

            if (hasVoices && (lfoModFilter || envModFilter))
            {
                modulatedValues.filterCutoff.store(voiceOut.lastModulatedCutoff, std::memory_order_relaxed);
            }
            else if (lfoModFilter)
            {
                // Hypothetical cutoff from base + LFO (no envelope, no kbd track)
                constexpr float FILTER_OCTAVES = 10.0f;
                float hypo = bp.baseCutoff;
                if (bp.lfo1Target == LfoTarget::Filter) hypo *= std::pow(2.0f, lastLfo1Val_ * FILTER_OCTAVES);
                if (bp.lfo2Target == LfoTarget::Filter) hypo *= std::pow(2.0f, lastLfo2Val_ * FILTER_OCTAVES);
                modulatedValues.filterCutoff.store(juce::jlimit(20.0f, 20000.0f, hypo), std::memory_order_relaxed);
            }
            else
            {
                modulatedValues.filterCutoff.store(NO_GHOST, std::memory_order_relaxed);
            }
        }

        // Scan ghost
        {
            bool lfoModScan = bp.lfo1Target == LfoTarget::Scan || bp.lfo2Target == LfoTarget::Scan;
            bool envModScan = (bp.mod1Target == EnvTarget::Scan || bp.mod2Target == EnvTarget::Scan
                               || std::abs(bp.driftScanOffset) > 0.001f) && hasVoices;

            if (hasVoices && (lfoModScan || envModScan))
            {
                modulatedValues.scanPosition.store(voiceOut.lastModulatedScan, std::memory_order_relaxed);
            }
            else if (lfoModScan)
            {
                float hypo = bp.baseScan;
                if (bp.lfo1Target == LfoTarget::Scan) hypo += lastLfo1Val_;
                if (bp.lfo2Target == LfoTarget::Scan) hypo += lastLfo2Val_;
                modulatedValues.scanPosition.store(juce::jlimit(0.0f, 1.0f, hypo), std::memory_order_relaxed);
            }
            else
            {
                modulatedValues.scanPosition.store(NO_GHOST, std::memory_order_relaxed);
            }
        }

        // LFO1/2 Rate/Depth ghost (env → LFO modulation, requires active voices)
        if (!skipSynthesis)
        {
            bool lfo1RateMod  = bp.mod1Target == EnvTarget::LFO1Rate  || bp.mod2Target == EnvTarget::LFO1Rate;
            bool lfo1DepthMod = bp.mod1Target == EnvTarget::LFO1Depth || bp.mod2Target == EnvTarget::LFO1Depth;
            modulatedValues.lfo1Rate.store(lfo1RateMod ? lfo1.getRate() : NO_GHOST, std::memory_order_relaxed);
            modulatedValues.lfo1Depth.store(lfo1DepthMod ? lfo1.getDepth() : NO_GHOST, std::memory_order_relaxed);

            bool lfo2RateMod  = bp.mod1Target == EnvTarget::LFO2Rate  || bp.mod2Target == EnvTarget::LFO2Rate;
            bool lfo2DepthMod = bp.mod1Target == EnvTarget::LFO2Depth || bp.mod2Target == EnvTarget::LFO2Depth;
            modulatedValues.lfo2Rate.store(lfo2RateMod ? lfo2.getRate() : NO_GHOST, std::memory_order_relaxed);
            modulatedValues.lfo2Depth.store(lfo2DepthMod ? lfo2.getDepth() : NO_GHOST, std::memory_order_relaxed);
        }

        // Delay/Reverb ghosts (modulated by env or LFO targeting them)
        {
            bool dlyTimeMod = modDelayTime != 0.0f;
            bool dlyFbMod   = modDelayFb != 0.0f;
            bool dlyMixMod  = modDelayMix != 0.0f;
            bool revMixMod  = modReverbMix != 0.0f;
            modulatedValues.delayTime.store(dlyTimeMod && delayEnabled
                ? juce::jlimit(1.0f, 2000.0f, parameters.getRawParameterValue(PID::delayTime)->load() * (1.0f + modDelayTime))
                : NO_GHOST, std::memory_order_relaxed);
            modulatedValues.delayFeedback.store(dlyFbMod && delayEnabled
                ? juce::jlimit(0.0f, 0.95f, parameters.getRawParameterValue(PID::delayFeedback)->load() * (1.0f + modDelayFb))
                : NO_GHOST, std::memory_order_relaxed);
            modulatedValues.delayMix.store(dlyMixMod && delayEnabled
                ? juce::jlimit(0.0f, 1.0f, parameters.getRawParameterValue(PID::delayMix)->load() + modDelayMix)
                : NO_GHOST, std::memory_order_relaxed);
            modulatedValues.reverbMix.store(revMixMod && reverbEnabled
                ? juce::jlimit(0.0f, 1.0f, parameters.getRawParameterValue(PID::reverbMix)->load() + modReverbMix)
                : NO_GHOST, std::memory_order_relaxed);
        }
    }

    // ── Master volume ───────────────────────────────────────────────────────
    buffer.applyGain(masterGain);

    // ── Limiter (always on, internal safety) ────────────────────────────────
    limiter.setThreshold(parameters.getRawParameterValue(PID::limiterThresh)->load());
    limiter.setRelease(parameters.getRawParameterValue(PID::limiterRelease)->load());
    limiter.processBlock(buffer);
}

bool T5ynthProcessor::isWavetableMode() const
{
    return static_cast<int>(parameters.getRawParameterValue(PID::engineMode)->load()) == 1;
}

bool T5ynthProcessor::isSamplerMode() const
{
    return !isWavetableMode();
}

void T5ynthProcessor::loadGeneratedAudio(const juce::AudioBuffer<float>& audioBuffer, double sr)
{
    // Store raw audio (unmodified) for preset embedding and re-apply on toggle
    if (&audioBuffer != &generatedAudioRaw)
        generatedAudioRaw.makeCopyOf(audioBuffer);
    generatedSampleRate = sr;

    // Rumble filter — always on, removes DC/sub-bass from VAE output
    juce::AudioBuffer<float> cleanBuffer;
    cleanBuffer.makeCopyOf(audioBuffer);
    applyRumbleFilter(cleanBuffer, sr);

    // Conditionally apply HF boost to compensate VAE decoder rolloff
    bool hfOn = parameters.getRawParameterValue(PID::genHfBoost)->load() > 0.5f;
    if (hfOn)
        applyHfBoost(cleanBuffer, sr);
    const auto& feedBuffer = cleanBuffer;

    // Keep generatedAudioFull in sync (used for waveform display + presets)
    generatedAudioFull.makeCopyOf(feedBuffer);

    // ── Auto-trim P2/P3 BEFORE loadBuffer so that preparePlaybackBuffer
    //    (which runs normalization) already sees the correct region. ──────
    if (!masterSampler.getPointsLocked())
    {
        float prevP1 = masterSampler.getStartPos();

        const int numSamples = feedBuffer.getNumSamples();
        float startFrac = 0.0f;
        float endFrac   = 1.0f;

        if (numSamples > 0)
        {
            const float* data = feedBuffer.getReadPointer(0);

            // Find global peak for relative threshold (-48 dB from peak).
            // -48 dB is ~0.004× peak — catches the perceptual silence floor
            // while tolerating low-level reverb tails that VAE decoders
            // commonly produce. Fixed-absolute thresholds fail on quiet
            // generations; pure psychoacoustic thresholds (-96 dB) are too
            // sensitive for machine-generated audio with low-level artefacts.
            float globalPeak = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                globalPeak = std::max(globalPeak, std::abs(data[i]));

            const float threshold = globalPeak * 0.004f; // -48 dB from peak
            const int windowSize = 256;

            int firstActive = 0;
            int lastActive = numSamples;

            for (int pos = 0; pos + windowSize <= numSamples; pos += windowSize)
            {
                float peak = 0.0f;
                for (int i = 0; i < windowSize; ++i)
                    peak = std::max(peak, std::abs(data[pos + i]));
                if (peak > threshold)
                {
                    firstActive = pos;
                    break;
                }
            }

            for (int pos = numSamples - windowSize; pos >= 0; pos -= windowSize)
            {
                float peak = 0.0f;
                int len = juce::jmin(windowSize, numSamples - pos);
                for (int i = 0; i < len; ++i)
                    peak = std::max(peak, std::abs(data[pos + i]));
                if (peak > threshold)
                {
                    lastActive = juce::jmin(pos + windowSize, numSamples);
                    break;
                }
            }

            // Small margin (one window each side)
            firstActive = juce::jmax(0, firstActive - windowSize);
            lastActive  = juce::jmin(numSamples, lastActive + windowSize);

            startFrac = static_cast<float>(firstActive) / static_cast<float>(numSamples);
            endFrac   = static_cast<float>(lastActive)  / static_cast<float>(numSamples);

            if (endFrac - startFrac < 0.05f)
            {
                startFrac = 0.0f;
                endFrac   = 1.0f;
            }
        }

        masterSampler.setLoopStart(startFrac);
        masterSampler.setLoopEnd(endFrac);

        // P1: preserve user's aesthetic choice, but clamp into active region
        if (prevP1 < startFrac || prevP1 > endFrac)
            masterSampler.setStartPos(startFrac);
    }

    // Feed the sampler/voice chain — preparePlaybackBuffer now sees the
    // correct P2/P3, so normalization targets the right region.
    masterSampler.loadBuffer(feedBuffer, sr);

    // Sync WT extraction region from P2/P3 when not locked (first load)
    if (!masterSampler.getPointsLocked())
    {
        masterSampler.setWtExtractStart(masterSampler.getLoopStart());
        masterSampler.setWtExtractEnd(masterSampler.getLoopEnd());
    }

    // Extract frames: WT uses its own region, Sampler uses P2/P3
    float extractStart = isWavetableMode() ? masterSampler.getWtExtractStart()
                                           : masterSampler.getLoopStart();
    float extractEnd   = isWavetableMode() ? masterSampler.getWtExtractEnd()
                                           : masterSampler.getLoopEnd();

    // WT frame count: 0=32, 1=64, 2=128, 3=256
    constexpr int frameCounts[] = {32, 64, 128, 256};
    int fcIdx = static_cast<int>(parameters.getRawParameterValue(PID::wtFrames)->load());
    int maxFrames = frameCounts[juce::jlimit(0, 3, fcIdx)];

    if (isWavetableMode())
    {
        // Pitch-synchronous extraction (one frame per detected period)
        masterOsc.extractFramesFromBuffer(feedBuffer, sr, extractStart, extractEnd, maxFrames);
    }
    else
    {
        // Contiguous extraction for sampler mode (one frame per FRAME_SIZE chunk)
        masterOsc.extractContiguousFrames(feedBuffer, sr, extractStart, extractEnd);
    }

    // Set auto-scan rate: scan 0→1 matches the original audio duration
    int regionSamples = static_cast<int>((extractEnd - extractStart) * feedBuffer.getNumSamples());
    masterOsc.setAutoScanRate(sr, regionSamples);

    // Sampler loop region → auto-scan loop (relative to extraction region)
    auto loopMode = masterSampler.getLoopMode();
    WavetableOscillator::LoopMode oscLoopMode;
    switch (loopMode)
    {
        case SamplePlayer::LoopMode::OneShot:  oscLoopMode = WavetableOscillator::LoopMode::OneShot;  break;
        case SamplePlayer::LoopMode::PingPong: oscLoopMode = WavetableOscillator::LoopMode::PingPong; break;
        default:                               oscLoopMode = WavetableOscillator::LoopMode::Loop;     break;
    }
    masterOsc.setAutoScanLoop(0.0f, 1.0f, oscLoopMode);

    voiceManager.distributeSamplerBuffer(masterSampler);
    voiceManager.distributeWavetableFrames(masterOsc);

    // Snapshot channel 0 for waveform display
    if (feedBuffer.getNumChannels() > 0 && feedBuffer.getNumSamples() > 0)
    {
        waveformSnapshot.setSize(1, feedBuffer.getNumSamples(), false, false, true);
        waveformSnapshot.copyFrom(0, 0, feedBuffer, 0, 0, feedBuffer.getNumSamples());

        // Normalize display when wavetable mode or sampler normalize is active
        bool normDisplay = isWavetableMode()
            || (parameters.getRawParameterValue(PID::normalize)->load() > 0.5f);
        if (normDisplay)
        {
            float peak = 0.0f;
            const float* d = waveformSnapshot.getReadPointer(0);
            for (int i = 0; i < waveformSnapshot.getNumSamples(); ++i)
                peak = std::max(peak, std::abs(d[i]));
            if (peak > 0.001f)
                waveformSnapshot.applyGain(0.95f / peak);
        }

        newWaveformReady.store(true, std::memory_order_release);
    }

}

void T5ynthProcessor::reloadProcessedAudio(const juce::AudioBuffer<float>& processed)
{
    // Update stored audio and reload into sampler without Rumble/HF/Normalize
    generatedAudioFull.makeCopyOf(processed);
    masterSampler.loadBuffer(processed, generatedSampleRate);
    voiceManager.distributeSamplerBuffer(masterSampler);

    if (processed.getNumChannels() > 0 && processed.getNumSamples() > 0)
    {
        waveformSnapshot.setSize(1, processed.getNumSamples(), false, false, true);
        waveformSnapshot.copyFrom(0, 0, processed, 0, 0, processed.getNumSamples());
        newWaveformReady.store(true, std::memory_order_release);
    }
}

void T5ynthProcessor::reextractWavetable()
{
    if (waveformSnapshot.getNumSamples() > 0)
    {
        float start = isWavetableMode() ? masterSampler.getWtExtractStart()
                                        : masterSampler.getLoopStart();
        float end   = isWavetableMode() ? masterSampler.getWtExtractEnd()
                                        : masterSampler.getLoopEnd();

        constexpr int frameCounts[] = {32, 64, 128, 256};
        int fcIdx = static_cast<int>(parameters.getRawParameterValue(PID::wtFrames)->load());
        int maxFrames = frameCounts[juce::jlimit(0, 3, fcIdx)];

        if (isWavetableMode())
            masterOsc.extractFramesFromBuffer(waveformSnapshot, getSampleRate(), start, end, maxFrames);
        else
            masterOsc.extractContiguousFrames(waveformSnapshot, getSampleRate(), start, end);

        int regionSamples = static_cast<int>((end - start) * waveformSnapshot.getNumSamples());
        masterOsc.setAutoScanRate(getSampleRate(), regionSamples);
        voiceManager.distributeWavetableFrames(masterOsc);
    }
}

juce::AudioProcessorEditor* T5ynthProcessor::createEditor()
{
    return new T5ynthEditor(*this);
}

void T5ynthProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void T5ynthProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xml));

    // Never auto-start sequencers on session restore — no acoustic surprises
    parameters.getParameter(PID::seqRunning)->setValueNotifyingHost(0.0f);
    parameters.getParameter(PID::genSeqRunning)->setValueNotifyingHost(0.0f);
}

// ═══════════════════════════════════════════════════════════════════
// JSON Preset Import/Export (compatible with Vue reference format)
// ═══════════════════════════════════════════════════════════════════

// Conversion helpers matching useFilter.ts:
//   normalizedToFreq(n) = 20 * pow(1000, n)     // 0→20Hz, 1→20kHz
//   freqToNormalized(f) = log(f/20) / log(1000)
static float cutoffNormToHz(float n) { return 20.0f * std::pow(1000.0f, juce::jlimit(0.0f, 1.0f, n)); }
static float cutoffHzToNorm(float hz) { return std::log(juce::jlimit(20.0f, 20000.0f, hz) / 20.0f) / std::log(1000.0f); }

// ── Choice-parameter string↔index helpers ──
//
// All *FromString/*ToString helpers route through the single source of
// truth in BlockParams.h: every choice parameter has a kEntries[] table
// with a stable snake_case `.key` column used for JSON serialization.
// These helpers are 3-line wrappers over `choiceFromKey` / `choiceToKey`
// below, which do a linear scan over the .key column (kCount is small).

template <std::size_t N>
static int choiceFromKey(const juce::String& s, const ChoiceEntry (&entries)[N]) {
    for (std::size_t i = 0; i < N; ++i)
        if (s == entries[i].key) return static_cast<int>(i);
    return 0; // first entry (typically "none"/"off"/"---")
}
template <std::size_t N>
static juce::String choiceToKey(int i, const ChoiceEntry (&entries)[N]) {
    return juce::String(i >= 0 && i < static_cast<int>(N) ? entries[i].key : entries[0].key);
}

static int filterTypeFromString(const juce::String& s)  { return choiceFromKey(s, FilterType::kEntries); }
static juce::String filterTypeToString(int i)           { return choiceToKey(i, FilterType::kEntries); }

static int filterSlopeFromString(const juce::String& s) { return choiceFromKey(s, FilterSlope::kEntries); }
static juce::String filterSlopeToString(int i)          { return choiceToKey(i, FilterSlope::kEntries); }

static int envTargetFromString(const juce::String& s)   { return choiceFromKey(s, EnvTarget::kEntries); }
static juce::String envTargetToString(int i)            { return choiceToKey(i, EnvTarget::kEntries); }

static int lfoTargetFromString(const juce::String& s)   { return choiceFromKey(s, LfoTarget::kEntries); }
static juce::String lfoTargetToString(int i)            { return choiceToKey(i, LfoTarget::kEntries); }

static int lfoWaveFromString(const juce::String& s)     { return choiceFromKey(s, LfoWave::kEntries); }
static juce::String lfoWaveToString(int i)              { return choiceToKey(i, LfoWave::kEntries); }

static int lfoModeFromString(const juce::String& s)     { return choiceFromKey(s, LfoMode::kEntries); }
static juce::String lfoModeToString(int i)              { return choiceToKey(i, LfoMode::kEntries); }

static int driftTargetFromString(const juce::String& s) { return choiceFromKey(s, DriftTarget::kEntries); }
static juce::String driftTargetToString(int i)          { return choiceToKey(i, DriftTarget::kEntries); }

static int driftWaveFromString(const juce::String& s)   { return choiceFromKey(s, DriftWave::kEntries); }
static juce::String driftWaveToString(int i)            { return choiceToKey(i, DriftWave::kEntries); }

static int curveShapeFromString(const juce::String& s)  { return choiceFromKey(s, EnvCurve::kEntries); }
static juce::String curveShapeToString(int i)           { return choiceToKey(i, EnvCurve::kEntries); }

// ── PID group tables for looped save/load of envelopes, LFOs, drift ──
struct EnvPIDs {
    const char* attack; const char* decay; const char* sustain; const char* release;
    const char* amount; const char* velSens; const char* loop; const char* target;
    const char* attackCurve; const char* decayCurve; const char* releaseCurve;
};
static constexpr EnvPIDs kEnvPIDs[] = {
    { PID::ampAttack, PID::ampDecay, PID::ampSustain, PID::ampRelease,
      PID::ampAmount, PID::ampVelSens, PID::ampLoop, nullptr,
      PID::ampAttackCurve, PID::ampDecayCurve, PID::ampReleaseCurve },
    { PID::mod1Attack, PID::mod1Decay, PID::mod1Sustain, PID::mod1Release,
      PID::mod1Amount, PID::mod1VelSens, PID::mod1Loop, PID::mod1Target,
      PID::mod1AttackCurve, PID::mod1DecayCurve, PID::mod1ReleaseCurve },
    { PID::mod2Attack, PID::mod2Decay, PID::mod2Sustain, PID::mod2Release,
      PID::mod2Amount, PID::mod2VelSens, PID::mod2Loop, PID::mod2Target,
      PID::mod2AttackCurve, PID::mod2DecayCurve, PID::mod2ReleaseCurve },
};

struct LfoPIDs {
    const char* rate; const char* depth; const char* wave;
    const char* target; const char* mode;
};
static constexpr LfoPIDs kLfoPIDs[] = {
    { PID::lfo1Rate, PID::lfo1Depth, PID::lfo1Wave, PID::lfo1Target, PID::lfo1Mode },
    { PID::lfo2Rate, PID::lfo2Depth, PID::lfo2Wave, PID::lfo2Target, PID::lfo2Mode },
};

struct DriftPIDs {
    const char* rate; const char* depth; const char* target; const char* wave;
};
static constexpr DriftPIDs kDriftPIDs[] = {
    { PID::drift1Rate, PID::drift1Depth, PID::drift1Target, PID::drift1Wave },
    { PID::drift2Rate, PID::drift2Depth, PID::drift2Target, PID::drift2Wave },
    { PID::drift3Rate, PID::drift3Depth, PID::drift3Target, PID::drift3Wave },
};

// Helper to safely set a parameter value
static void setParam(juce::AudioProcessorValueTreeState& p, const juce::String& id, float val) {
    if (auto* param = p.getParameter(id))
        param->setValueNotifyingHost(param->convertTo0to1(val));
}

// ═══════════════════════════════════════════════════════════════════
// HF boost — two-band high shelf to compensate VAE decoder rolloff
// ═══════════════════════════════════════════════════════════════════

void T5ynthProcessor::applyHfBoost(juce::AudioBuffer<float>& buffer, double sampleRate)
{
    auto shelf1 = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, 4000.0, 0.6, juce::Decibels::decibelsToGain(3.0f));
    auto shelf2 = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, 10000.0, 0.6, juce::Decibels::decibelsToGain(4.5f));

    juce::dsp::IIR::Filter<float> f1, f2;
    f1.coefficients = shelf1;
    f2.coefficients = shelf2;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        f1.reset();
        f2.reset();
        auto* data = buffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            data[i] = f1.processSample(data[i]);
            data[i] = f2.processSample(data[i]);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
// Rumble filter — 2nd-order Butterworth HP at 25 Hz
// Removes DC offset and sub-bass rumble from VAE decoder output
// ═══════════════════════════════════════════════════════════════════

void T5ynthProcessor::applyRumbleFilter(juce::AudioBuffer<float>& buffer, double sampleRate)
{
    auto hp = juce::dsp::IIR::Coefficients<float>::makeHighPass(sampleRate, 25.0, 0.707);
    juce::dsp::IIR::Filter<float> f;
    f.coefficients = hp;

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        f.reset();
        auto* data = buffer.getWritePointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            data[i] = f.processSample(data[i]);
    }
}

juce::String T5ynthProcessor::exportJsonPreset() const
{
    auto* p = const_cast<juce::AudioProcessorValueTreeState*>(&parameters);
    auto get = [&](const juce::String& id) -> float {
        auto* val = p->getRawParameterValue(id);
        jassert(val != nullptr); // fires in debug if PID is missing
        return val ? val->load() : 0.0f;
    };

    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", 1);
    root->setProperty("name", "T5ynth Export");
    root->setProperty("timestamp", juce::Time::getCurrentTime().toISO8601(true));

    // Synth params
    juce::DynamicObject::Ptr synth = new juce::DynamicObject();
    synth->setProperty("promptA", ""); // prompts are GUI-only, not in APVTS
    synth->setProperty("promptB", "");
    synth->setProperty("alpha", get(PID::genAlpha));
    synth->setProperty("magnitude", get(PID::genMagnitude));
    synth->setProperty("noise", get(PID::genNoise));
    synth->setProperty("duration", get(PID::genDuration));
    synth->setProperty("startPosition", get(PID::genStart));
    synth->setProperty("steps", static_cast<int>(get(PID::infSteps)));
    synth->setProperty("cfg", get(PID::genCfg));
    synth->setProperty("seed", static_cast<int>(get(PID::genSeed)));
    synth->setProperty("device", lastDevice);
    synth->setProperty("model", lastModel);
    synth->setProperty("hfBoost", get(PID::genHfBoost) > 0.5f);
    root->setProperty("synth", synth.get());

    // Engine
    juce::DynamicObject::Ptr engine = new juce::DynamicObject();
    engine->setProperty("mode", choiceToKey(static_cast<int>(get(PID::engineMode)), EngineMode::kEntries));
    engine->setProperty("loopMode", choiceToKey(static_cast<int>(get(PID::loopMode)), LoopMode::kEntries));
    engine->setProperty("loopStartFrac", static_cast<double>(masterSampler.getLoopStart()));
    engine->setProperty("loopEndFrac", static_cast<double>(masterSampler.getLoopEnd()));
    engine->setProperty("startPosFrac", static_cast<double>(masterSampler.getStartPos()));
    engine->setProperty("wtExtractStart", static_cast<double>(masterSampler.getWtExtractStart()));
    engine->setProperty("wtExtractEnd", static_cast<double>(masterSampler.getWtExtractEnd()));
    engine->setProperty("pointsLocked", masterSampler.getPointsLocked());
    engine->setProperty("crossfadeMs", get(PID::crossfadeMs));
    engine->setProperty(PID::normalize, get(PID::normalize) > 0.5f);
    engine->setProperty("loopOptimize", choiceToKey(static_cast<int>(get(PID::loopOptimize)), LoopOptimize::kEntries));
    root->setProperty("engine", engine.get());

    // Modulation: 3 envelopes
    juce::DynamicObject::Ptr modObj = new juce::DynamicObject();
    juce::Array<juce::var> envArr;
    for (int i = 0; i < 3; ++i)
    {
        const auto& ep = kEnvPIDs[i];
        juce::DynamicObject::Ptr env = new juce::DynamicObject();
        env->setProperty("attackMs", get(ep.attack));
        env->setProperty("decayMs", get(ep.decay));
        env->setProperty("sustain", get(ep.sustain));
        env->setProperty("releaseMs", get(ep.release));
        env->setProperty("amount", get(ep.amount));
        env->setProperty("velSens", get(ep.velSens));
        if (ep.target == nullptr)
            env->setProperty("target", "dca"); // amp env is always DCA
        else
            env->setProperty("target", envTargetToString(static_cast<int>(get(ep.target))));
        env->setProperty("loop", get(ep.loop) > 0.5f);
        env->setProperty("attackCurve", curveShapeToString(static_cast<int>(get(ep.attackCurve))));
        env->setProperty("decayCurve", curveShapeToString(static_cast<int>(get(ep.decayCurve))));
        env->setProperty("releaseCurve", curveShapeToString(static_cast<int>(get(ep.releaseCurve))));
        envArr.add(env.get());
    }
    modObj->setProperty("envs", envArr);

    // Modulation: 2 LFOs
    juce::Array<juce::var> lfoArr;
    for (int i = 0; i < 2; ++i)
    {
        const auto& lp = kLfoPIDs[i];
        juce::DynamicObject::Ptr lfo = new juce::DynamicObject();
        lfo->setProperty("rate", get(lp.rate));
        lfo->setProperty("depth", get(lp.depth));
        lfo->setProperty("waveform", lfoWaveToString(static_cast<int>(get(lp.wave))));
        lfo->setProperty("target", lfoTargetToString(static_cast<int>(get(lp.target))));
        lfo->setProperty("mode", lfoModeToString(static_cast<int>(get(lp.mode))));
        lfoArr.add(lfo.get());
    }
    modObj->setProperty("lfos", lfoArr);
    root->setProperty("modulation", modObj.get());

    // Drift LFOs
    juce::Array<juce::var> driftArr;
    for (int i = 0; i < 3; ++i)
    {
        const auto& dp = kDriftPIDs[i];
        juce::DynamicObject::Ptr d = new juce::DynamicObject();
        d->setProperty("rate", get(dp.rate));
        d->setProperty("depth", get(dp.depth));
        d->setProperty("waveform", driftWaveToString(static_cast<int>(get(dp.wave))));
        d->setProperty("target", driftTargetToString(static_cast<int>(get(dp.target))));
        driftArr.add(d.get());
    }
    root->setProperty("driftLfos", driftArr);
    root->setProperty("driftEnabled", get(PID::driftEnabled) > 0.5f);
    root->setProperty("driftCrossfade", get(PID::driftCrossfade));
    root->setProperty("regenMode", choiceToKey(static_cast<int>(get(PID::driftRegen)), DriftRegen::kEntries));

    // Wavetable + Noise
    juce::DynamicObject::Ptr wt = new juce::DynamicObject();
    wt->setProperty("scan", get(PID::oscScan));
    wt->setProperty("octaveShift", choiceToKey(static_cast<int>(get(PID::oscOctave)), OscOctave::kEntries));
    wt->setProperty("noiseLevel", get(PID::noiseLevel));
    wt->setProperty("noiseType", choiceToKey(static_cast<int>(get(PID::noiseType)), NoiseKind::kEntries));
    wt->setProperty("frames", choiceToKey(static_cast<int>(get(PID::wtFrames)), WtFrames::kEntries));
    wt->setProperty("smooth", get(PID::wtSmooth) > 0.5f);
    root->setProperty("wavetable", wt.get());

    // Effects
    juce::DynamicObject::Ptr fx = new juce::DynamicObject();
    fx->setProperty("delayType", choiceToKey(static_cast<int>(get(PID::delayType)), DelayType::kEntries));
    fx->setProperty("delayTimeMs", get(PID::delayTime));
    fx->setProperty("delayFeedback", get(PID::delayFeedback));
    fx->setProperty("delayMix", get(PID::delayMix));
    fx->setProperty("delayDamp", get(PID::delayDamp));
    fx->setProperty("reverbType", choiceToKey(static_cast<int>(get(PID::reverbType)), ReverbType::kEntries));
    fx->setProperty("reverbMix", get(PID::reverbMix));
    fx->setProperty("algoRoom", get(PID::algoRoom));
    fx->setProperty("algoDamping", get(PID::algoDamping));
    fx->setProperty("algoWidth", get(PID::algoWidth));
    // Limiter
    fx->setProperty("limiterThreshold", get(PID::limiterThresh));
    fx->setProperty("limiterRelease", get(PID::limiterRelease));
    root->setProperty("effects", fx.get());

    // Filter — store NORMALIZED cutoff (0-1), not Hz
    juce::DynamicObject::Ptr filt = new juce::DynamicObject();
    int ftRaw = static_cast<int>(get(PID::filterType));
    filt->setProperty("enabled", ftRaw > 0);
    filt->setProperty("type", filterTypeToString(ftRaw));
    filt->setProperty("slope", filterSlopeToString(static_cast<int>(get(PID::filterSlope))));
    filt->setProperty("cutoff", cutoffHzToNorm(get(PID::filterCutoff)));
    filt->setProperty("resonance", get(PID::filterResonance));
    filt->setProperty("mix", get(PID::filterMix));
    filt->setProperty("kbdTrack", get(PID::filterKbdTrack));
    root->setProperty("filter", filt.get());

    // Sequencer
    juce::DynamicObject::Ptr seq = new juce::DynamicObject();
    seq->setProperty("enabled", get(PID::seqRunning) > 0.5f);
    seq->setProperty("bpm", get(PID::seqBpm));
    int stepCount = static_cast<int>(get(PID::seqSteps));
    seq->setProperty("stepCount", stepCount);
    juce::Array<juce::var> stepArr;
    for (int i = 0; i < stepCount; ++i)
    {
        const auto& step = stepSequencer.getStep(i);
        juce::DynamicObject::Ptr s = new juce::DynamicObject();
        s->setProperty("active", step.enabled);
        s->setProperty("semitone", step.note - 60); // MIDI → semitone offset from C3
        s->setProperty("velocity", static_cast<double>(step.velocity));
        s->setProperty("gate", static_cast<double>(step.gate));
        s->setProperty("bind", step.bind);
        stepArr.add(s.get());
    }
    seq->setProperty("steps", stepArr);
    seq->setProperty("octaveShift", choiceToKey(static_cast<int>(get(PID::seqOctave)), SeqOctave::kEntries));
    seq->setProperty("division", choiceToKey(static_cast<int>(get(PID::seqDivision)), SeqDivision::kEntries));
    seq->setProperty("glideTime", get(PID::seqGlideTime));
    seq->setProperty("gate", get(PID::seqGate));
    seq->setProperty("scaleRoot", choiceToKey(static_cast<int>(get(PID::scaleRoot)), ScaleRoot::kEntries));
    seq->setProperty("scaleType", choiceToKey(static_cast<int>(get(PID::scaleType)), ScaleType::kEntries));
    root->setProperty("sequencer", seq.get());

    // Arpeggiator — new v3 format stores pattern as a single key
    // (ArpMode::Off is "off", replacing the old `enabled` bool + pattern).
    juce::DynamicObject::Ptr arp = new juce::DynamicObject();
    arp->setProperty("pattern", choiceToKey(static_cast<int>(get(PID::arpMode)), ArpMode::kEntries));
    arp->setProperty("rate", choiceToKey(static_cast<int>(get(PID::arpRate)), ArpRate::kEntries));
    arp->setProperty("octaveRange", static_cast<int>(get(PID::arpOctaves)));
    root->setProperty("arpeggiator", arp.get());

    // Generative sequencer
    juce::DynamicObject::Ptr genSeq = new juce::DynamicObject();
    genSeq->setProperty("enabled", get(PID::genSeqRunning) > 0.5f);
    genSeq->setProperty("steps", static_cast<int>(get(PID::genSteps)));
    genSeq->setProperty("pulses", static_cast<int>(get(PID::genPulses)));
    genSeq->setProperty("rotation", static_cast<int>(get(PID::genRotation)));
    genSeq->setProperty("mutation", get(PID::genMutation));
    genSeq->setProperty("range", choiceToKey(static_cast<int>(get(PID::genRange)), GenRange::kEntries));
    genSeq->setProperty("fixSteps",    get(PID::genFixSteps) > 0.5f);
    genSeq->setProperty("fixPulses",   get(PID::genFixPulses) > 0.5f);
    genSeq->setProperty("fixRotation", get(PID::genFixRotation) > 0.5f);
    genSeq->setProperty("fixMutation", get(PID::genFixMutation) > 0.5f);
    root->setProperty("generativeSeq", genSeq.get());

    return juce::JSON::toString(root.get(), true);
}

bool T5ynthProcessor::importJsonPreset(const juce::String& json)
{
    auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject()) return false;

    auto* root = parsed.getDynamicObject();
    if (!root) return false;

    // ── Synth params ──
    if (auto* synth = root->getProperty("synth").getDynamicObject())
    {
        setParam(parameters, PID::genAlpha, static_cast<float>(synth->getProperty("alpha")));
        setParam(parameters, PID::genMagnitude, static_cast<float>(synth->getProperty("magnitude")));
        setParam(parameters, PID::genNoise, static_cast<float>(synth->getProperty("noise")));
        setParam(parameters, PID::genDuration, static_cast<float>(synth->getProperty("duration")));
        setParam(parameters, PID::genStart, static_cast<float>(synth->getProperty("startPosition")));
        setParam(parameters, PID::infSteps, static_cast<float>(static_cast<int>(synth->getProperty("steps"))));
        setParam(parameters, PID::genCfg, static_cast<float>(synth->getProperty("cfg")));
        setParam(parameters, PID::genSeed, static_cast<float>(static_cast<int>(synth->getProperty("seed"))));
        if (synth->hasProperty("hfBoost"))
            setParam(parameters, PID::genHfBoost, static_cast<bool>(synth->getProperty("hfBoost")) ? 1.0f : 0.0f);
    }

    // ── Engine ──
    if (auto* engine = root->getProperty("engine").getDynamicObject())
    {
        setParam(parameters, PID::engineMode,
                 static_cast<float>(choiceFromKey(engine->getProperty("mode").toString(), EngineMode::kEntries)));
        setParam(parameters, PID::loopMode,
                 static_cast<float>(choiceFromKey(engine->getProperty("loopMode").toString(), LoopMode::kEntries)));

        // Restore P1/P2/P3 directly — the explicit pointsLocked flag gates
        // auto-bracketing in loadGeneratedAudio so no pending-apply dance is
        // needed. Older v3 presets without the flag default to unlocked.
        masterSampler.setLoopStart(static_cast<float>(engine->getProperty("loopStartFrac")));
        masterSampler.setLoopEnd(static_cast<float>(engine->getProperty("loopEndFrac")));
        masterSampler.setStartPos(static_cast<float>(engine->getProperty("startPosFrac")));
        // WT extraction region (fallback to P2/P3 for presets without it)
        if (engine->hasProperty("wtExtractStart"))
        {
            masterSampler.setWtExtractStart(static_cast<float>(engine->getProperty("wtExtractStart")));
            masterSampler.setWtExtractEnd(static_cast<float>(engine->getProperty("wtExtractEnd")));
        }
        else
        {
            masterSampler.setWtExtractStart(masterSampler.getLoopStart());
            masterSampler.setWtExtractEnd(masterSampler.getLoopEnd());
        }
        masterSampler.setPointsLocked(static_cast<bool>(engine->getProperty("pointsLocked")));
        setParam(parameters, PID::crossfadeMs, static_cast<float>(engine->getProperty("crossfadeMs")));
        setParam(parameters, PID::normalize, static_cast<bool>(engine->getProperty(PID::normalize)) ? 1.0f : 0.0f);
        setParam(parameters, PID::loopOptimize,
                 static_cast<float>(choiceFromKey(engine->getProperty("loopOptimize").toString(), LoopOptimize::kEntries)));
    }

    // ── Modulation ──
    if (auto* mod = root->getProperty("modulation").getDynamicObject())
    {
        auto* envsArr = mod->getProperty("envs").getArray();
        if (envsArr)
        {
            for (int i = 0; i < std::min(3, envsArr->size()); ++i)
            {
                auto* env = (*envsArr)[i].getDynamicObject();
                if (!env) continue;
                const auto& ep = kEnvPIDs[i];
                setParam(parameters, ep.attack, static_cast<float>(env->getProperty("attackMs")));
                setParam(parameters, ep.decay, static_cast<float>(env->getProperty("decayMs")));
                setParam(parameters, ep.sustain, static_cast<float>(env->getProperty("sustain")));
                setParam(parameters, ep.release, static_cast<float>(env->getProperty("releaseMs")));
                setParam(parameters, ep.amount, static_cast<float>(env->getProperty("amount")));
                setParam(parameters, ep.loop, env->getProperty("loop") ? 1.0f : 0.0f);
                if (env->hasProperty("velSens"))
                    setParam(parameters, ep.velSens, static_cast<float>(env->getProperty("velSens")));
                if (env->hasProperty("attackCurve"))
                    setParam(parameters, ep.attackCurve,
                             static_cast<float>(curveShapeFromString(env->getProperty("attackCurve").toString())));
                if (env->hasProperty("decayCurve"))
                    setParam(parameters, ep.decayCurve,
                             static_cast<float>(curveShapeFromString(env->getProperty("decayCurve").toString())));
                if (env->hasProperty("releaseCurve"))
                    setParam(parameters, ep.releaseCurve,
                             static_cast<float>(curveShapeFromString(env->getProperty("releaseCurve").toString())));
                if (ep.target != nullptr)
                    setParam(parameters, ep.target, static_cast<float>(envTargetFromString(env->getProperty("target").toString())));
            }
        }

        auto* lfosArr = mod->getProperty("lfos").getArray();
        if (lfosArr)
        {
            for (int i = 0; i < std::min(2, lfosArr->size()); ++i)
            {
                auto* lfo = (*lfosArr)[i].getDynamicObject();
                if (!lfo) continue;
                const auto& lp = kLfoPIDs[i];
                setParam(parameters, lp.rate, static_cast<float>(lfo->getProperty("rate")));
                setParam(parameters, lp.depth, static_cast<float>(lfo->getProperty("depth")));
                setParam(parameters, lp.wave, static_cast<float>(lfoWaveFromString(lfo->getProperty("waveform").toString())));
                setParam(parameters, lp.target, static_cast<float>(lfoTargetFromString(lfo->getProperty("target").toString())));
                setParam(parameters, lp.mode, static_cast<float>(lfoModeFromString(lfo->getProperty("mode").toString())));
            }
        }
    }

    // ── Drift LFOs ──
    auto* driftArr = root->getProperty("driftLfos").getArray();
    if (driftArr)
    {
        for (int i = 0; i < std::min(3, driftArr->size()); ++i)
        {
            auto* d = (*driftArr)[i].getDynamicObject();
            if (!d) continue;
            const auto& dp = kDriftPIDs[i];
            setParam(parameters, dp.rate, static_cast<float>(d->getProperty("rate")));
            setParam(parameters, dp.depth, static_cast<float>(d->getProperty("depth")));
            setParam(parameters, dp.wave, static_cast<float>(driftWaveFromString(d->getProperty("waveform").toString())));
            setParam(parameters, dp.target, static_cast<float>(driftTargetFromString(d->getProperty("target").toString())));
        }
    }
    setParam(parameters, PID::driftEnabled, static_cast<bool>(root->getProperty("driftEnabled")) ? 1.0f : 0.0f);
    setParam(parameters, PID::driftCrossfade, static_cast<float>(root->getProperty("driftCrossfade")));
    setParam(parameters, PID::driftRegen,
             static_cast<float>(choiceFromKey(root->getProperty("regenMode").toString(), DriftRegen::kEntries)));

    // ── Wavetable + Noise ──
    if (auto* wt = root->getProperty("wavetable").getDynamicObject())
    {
        setParam(parameters, PID::oscScan, static_cast<float>(wt->getProperty("scan")));
        setParam(parameters, PID::oscOctave,
                 static_cast<float>(choiceFromKey(wt->getProperty("octaveShift").toString(), OscOctave::kEntries)));
        setParam(parameters, PID::noiseLevel, static_cast<float>(wt->getProperty("noiseLevel")));
        setParam(parameters, PID::noiseType,
                 static_cast<float>(choiceFromKey(wt->getProperty("noiseType").toString(), NoiseKind::kEntries)));
        setParam(parameters, PID::wtFrames,
                 static_cast<float>(choiceFromKey(wt->getProperty("frames").toString(), WtFrames::kEntries)));
        setParam(parameters, PID::wtSmooth, wt->getProperty("smooth") ? 1.0f : 0.0f);
    }

    // ── Effects ──
    if (auto* fx = root->getProperty("effects").getDynamicObject())
    {
        setParam(parameters, PID::delayType,
                 static_cast<float>(choiceFromKey(fx->getProperty("delayType").toString(), DelayType::kEntries)));
        setParam(parameters, PID::delayTime, static_cast<float>(fx->getProperty("delayTimeMs")));
        setParam(parameters, PID::delayFeedback, static_cast<float>(fx->getProperty("delayFeedback")));
        setParam(parameters, PID::delayMix, static_cast<float>(fx->getProperty("delayMix")));
        setParam(parameters, PID::delayDamp, static_cast<float>(fx->getProperty("delayDamp")));
        setParam(parameters, PID::reverbType,
                 static_cast<float>(choiceFromKey(fx->getProperty("reverbType").toString(), ReverbType::kEntries)));
        setParam(parameters, PID::reverbMix, static_cast<float>(fx->getProperty("reverbMix")));
        setParam(parameters, PID::algoRoom, static_cast<float>(fx->getProperty("algoRoom")));
        setParam(parameters, PID::algoDamping, static_cast<float>(fx->getProperty("algoDamping")));
        setParam(parameters, PID::algoWidth, static_cast<float>(fx->getProperty("algoWidth")));
        setParam(parameters, PID::limiterThresh, static_cast<float>(fx->getProperty("limiterThreshold")));
        setParam(parameters, PID::limiterRelease, static_cast<float>(fx->getProperty("limiterRelease")));
    }

    // ── Filter — CRITICAL: cutoff is normalized 0-1, convert to Hz ──
    if (auto* filt = root->getProperty("filter").getDynamicObject())
    {
        // Merge enabled + type: if enabled=false, force type to Off
        bool filtEnabled = filt->getProperty("enabled");
        int filtType = filterTypeFromString(filt->getProperty("type").toString());
        if (!filtEnabled) filtType = FilterType::Off;
        setParam(parameters, PID::filterType, static_cast<float>(filtType));
        setParam(parameters, PID::filterSlope,
                 static_cast<float>(filterSlopeFromString(filt->getProperty("slope").toString())));
        // Convert normalized cutoff to Hz: 20 * pow(1000, n)
        setParam(parameters, PID::filterCutoff,
                 cutoffNormToHz(static_cast<float>(filt->getProperty("cutoff"))));
        setParam(parameters, PID::filterResonance, static_cast<float>(filt->getProperty("resonance")));
        setParam(parameters, PID::filterMix, static_cast<float>(filt->getProperty("mix")));
        setParam(parameters, PID::filterKbdTrack, static_cast<float>(filt->getProperty("kbdTrack")));
    }

    // ── Sequencer ──
    if (auto* seq = root->getProperty("sequencer").getDynamicObject())
    {
        // Preserve current seq_running state — don't stop playback on preset load
        // bool seqEnabled = seq->getProperty("enabled");
        // setParam(parameters, PID::seqRunning, seqEnabled ? 1.0f : 0.0f);
        setParam(parameters, PID::seqBpm, static_cast<float>(seq->getProperty("bpm")));
        int stepCount = static_cast<int>(seq->getProperty("stepCount"));
        setParam(parameters, PID::seqSteps, static_cast<float>(stepCount));
        stepSequencer.setNumSteps(stepCount);

        auto* stepsArr = seq->getProperty("steps").getArray();
        if (stepsArr)
        {
            for (int i = 0; i < std::min(stepCount, stepsArr->size()); ++i)
            {
                auto* s = (*stepsArr)[i].getDynamicObject();
                if (!s) continue;
                int semitone = static_cast<int>(s->getProperty("semitone"));
                stepSequencer.setStepNote(i, 60 + semitone); // C3 + semitone offset
                stepSequencer.setStepVelocity(i, static_cast<float>(s->getProperty("velocity")));
                stepSequencer.setStepEnabled(i, static_cast<bool>(s->getProperty("active")));
                if (s->hasProperty("gate"))
                    stepSequencer.setStepGate(i, static_cast<float>(s->getProperty("gate")));
                if (s->hasProperty("bind"))
                    stepSequencer.setStepBind(i, static_cast<bool>(s->getProperty("bind")));
                else if (s->hasProperty("glide"))  // backward compat
                    stepSequencer.setStepBind(i, static_cast<bool>(s->getProperty("glide")));
            }
        }
        setParam(parameters, PID::seqOctave,
                 static_cast<float>(choiceFromKey(seq->getProperty("octaveShift").toString(), SeqOctave::kEntries)));
        setParam(parameters, PID::seqDivision,
                 static_cast<float>(choiceFromKey(seq->getProperty("division").toString(), SeqDivision::kEntries)));
        setParam(parameters, PID::seqGlideTime, static_cast<float>(seq->getProperty("glideTime")));
        setParam(parameters, PID::seqGate, static_cast<float>(seq->getProperty("gate")));
        setParam(parameters, PID::scaleRoot,
                 static_cast<float>(choiceFromKey(seq->getProperty("scaleRoot").toString(), ScaleRoot::kEntries)));
        setParam(parameters, PID::scaleType,
                 static_cast<float>(choiceFromKey(seq->getProperty("scaleType").toString(), ScaleType::kEntries)));
    }

    // ── Arpeggiator ──
    if (auto* arp = root->getProperty("arpeggiator").getDynamicObject())
    {
        int arpModeIdx = choiceFromKey(arp->getProperty("pattern").toString(), ArpMode::kEntries);
        setParam(parameters, PID::arpMode, static_cast<float>(arpModeIdx));
        setParam(parameters, PID::arpRate,
                 static_cast<float>(choiceFromKey(arp->getProperty("rate").toString(), ArpRate::kEntries)));
        setParam(parameters, PID::arpOctaves, static_cast<float>(static_cast<int>(arp->getProperty("octaveRange"))));
    }

    // ── Generative sequencer ──
    if (auto* gs = root->getProperty("generativeSeq").getDynamicObject())
    {
        setParam(parameters, PID::genSeqRunning, static_cast<bool>(gs->getProperty("enabled")) ? 1.0f : 0.0f);
        setParam(parameters, PID::genSteps,    static_cast<float>(static_cast<int>(gs->getProperty("steps"))));
        setParam(parameters, PID::genPulses,   static_cast<float>(static_cast<int>(gs->getProperty("pulses"))));
        setParam(parameters, PID::genRotation, static_cast<float>(static_cast<int>(gs->getProperty("rotation"))));
        setParam(parameters, PID::genMutation, static_cast<float>(gs->getProperty("mutation")));
        setParam(parameters, PID::genRange,
                 static_cast<float>(choiceFromKey(gs->getProperty("range").toString(), GenRange::kEntries)));
        setParam(parameters, PID::genFixSteps,    static_cast<bool>(gs->getProperty("fixSteps")) ? 1.0f : 0.0f);
        setParam(parameters, PID::genFixPulses,   static_cast<bool>(gs->getProperty("fixPulses")) ? 1.0f : 0.0f);
        setParam(parameters, PID::genFixRotation, static_cast<bool>(gs->getProperty("fixRotation")) ? 1.0f : 0.0f);
        setParam(parameters, PID::genFixMutation, static_cast<bool>(gs->getProperty("fixMutation")) ? 1.0f : 0.0f);
    }

    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new T5ynthProcessor();
}
