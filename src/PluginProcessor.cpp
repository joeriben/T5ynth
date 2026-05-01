#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"
#include "dsp/Tuning.h"

namespace
{
constexpr bool kSamplerDebugLogging = false;
constexpr float kAlphaAnchorSnapThreshold = 0.04f;
constexpr float kMagnitudeUnitySnapThreshold = 0.03f;
constexpr float kDurationSecondSnapThreshold = 0.05f;

float snapIfNear(float value, float target, float threshold)
{
    return std::abs(value - target) <= threshold ? target : value;
}

float snapToInterval(float rangeStart, float value, float interval)
{
    return rangeStart + interval * std::floor((value - rangeStart) / interval + 0.5f);
}

float snapGenerationAlpha(float rangeStart, float rangeEnd, float value)
{
    juce::ignoreUnused(rangeStart, rangeEnd);
    value = snapIfNear(value, -1.0f, kAlphaAnchorSnapThreshold);
    value = snapIfNear(value,  1.0f, kAlphaAnchorSnapThreshold);
    return juce::jlimit(rangeStart, rangeEnd, value);
}

float snapGenerationMagnitude(float rangeStart, float rangeEnd, float value)
{
    constexpr float interval = 0.001f;
    value = snapIfNear(value, 1.0f, kMagnitudeUnitySnapThreshold);
    value = snapToInterval(rangeStart, value, interval);
    return juce::jlimit(rangeStart, rangeEnd, value);
}

float snapGenerationDuration(float rangeStart, float rangeEnd, float value)
{
    constexpr float interval = 0.01f;
    for (int seconds = 1; seconds <= 11; ++seconds)
        value = snapIfNear(value, static_cast<float>(seconds), kDurationSecondSnapThreshold);

    value = snapToInterval(rangeStart, value, interval);
    return juce::jlimit(rangeStart, rangeEnd, value);
}

float convertSkew03From0To1(float rangeStart, float rangeEnd, float proportion)
{
    constexpr float skew = 0.3f;
    proportion = juce::jlimit(0.0f, 1.0f, proportion);
    if (proportion > 0.0f)
        proportion = std::exp(std::log(proportion) / skew);
    return rangeStart + (rangeEnd - rangeStart) * proportion;
}

float convertSkew03To0To1(float rangeStart, float rangeEnd, float value)
{
    constexpr float skew = 0.3f;
    auto proportion = juce::jlimit(0.0f, 1.0f, (value - rangeStart) / (rangeEnd - rangeStart));
    return std::pow(proportion, skew);
}

float applyNormalizedOffset(float baseValue, float modulationOffset)
{
    return juce::jlimit(0.0f, 1.0f, baseValue + modulationOffset);
}

void samplerProcessorDebugLog(const juce::String& message)
{
    if constexpr (kSamplerDebugLogging)
    {
        juce::Logger::writeToLog("[SamplerDebug] " + message);
        juce::FileOutputStream out(juce::File("/tmp/t5ynth_sampler_debug.log"));
        if (out.openedOk())
        {
            out << "[SamplerDebug] " << message << juce::newLine;
            out.flush();
        }
    }
}
}

T5ynthProcessor::T5ynthProcessor()
    : AudioProcessor(BusesProperties()
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "T5ynth", createParameterLayout())
{
    juce::File("/tmp/t5ynth_sampler_debug.log").deleteFile();
    samplerProcessorDebugLog("session start");
}

T5ynthProcessor::~T5ynthProcessor() = default;

bool T5ynthProcessor::launchPipeInference(const juce::File& backendDir)
{
    return pipeInference->launch(backendDir);
}

bool T5ynthProcessor::canUseStepHoldPreview() const
{
    // Always available now: VoiceManager reserves the drone voice so the
    // mouse-held step is protected from seq voice-stealing (poly) and
    // suppresses seq noteOns on voice 0 (mono) for as long as the mouse is held.
    return true;
}

void T5ynthProcessor::beginStepHoldPreview(int midiNote, float velocity)
{
    const juce::ScopedLock sl(getCallbackLock());

    // Apply the same seq-wide octave shift the step sequencer adds to its
    // emitted MIDI (see PluginProcessor.cpp:1060 + StepSequencer.cpp:208).
    // The drone is the GUI mirror of the step's effective pitch, so it must
    // include this shift; the per-voice oscOctave is applied later inside
    // SynthVoice::noteOn via octaveShift_ from BlockParams.
    const int seqOctaveIdx = static_cast<int>(parameters.getRawParameterValue(PID::seqOctave)->load());
    const int seqOctaveSemi = (seqOctaveIdx - 2) * 12;
    const int note = juce::jlimit(0, 127, midiNote + seqOctaveSemi);
    const float vel = juce::jlimit(0.0f, 1.0f, velocity);
    const bool lfo1TrigMode = static_cast<int>(parameters.getRawParameterValue(PID::lfo1Mode)->load()) == 1;
    const bool lfo2TrigMode = static_cast<int>(parameters.getRawParameterValue(PID::lfo2Mode)->load()) == 1;
    const bool lfo3TrigMode = static_cast<int>(parameters.getRawParameterValue(PID::lfo3Mode)->load()) == 1;

    voiceManager.setDroneNote(note, vel, lfo1TrigMode, lfo2TrigMode, lfo3TrigMode);

    stepHoldPreviewActive = true;
    stepHoldPreviewNote = note;
    lastMidiNote.store(note, std::memory_order_relaxed);
    lastMidiVelocity.store(juce::roundToInt(vel * 127.0f), std::memory_order_relaxed);
    lastMidiNoteOn.store(true, std::memory_order_relaxed);
}

void T5ynthProcessor::updateStepHoldPreview(int midiNote, float velocity)
{
    beginStepHoldPreview(midiNote, velocity);
}

void T5ynthProcessor::endStepHoldPreview()
{
    const juce::ScopedLock sl(getCallbackLock());

    if (!stepHoldPreviewActive)
        return;

    voiceManager.clearDroneNote();
    stepHoldPreviewActive = false;
    stepHoldPreviewNote = -1;

    if (!voiceManager.hasActiveVoices())
        lastMidiNoteOn.store(false, std::memory_order_relaxed);
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
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::filterDrive, 1}, "Filter Drive",
        juce::NormalisableRange<float>(0.0f, 36.0f, 0.1f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::filterDriveOs, 1}, "Filter Drive OS",
        toChoices(FilterDriveOs::kEntries), FilterDriveOs::X4));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::filterAlgorithm, 1}, "Filter Algorithm",
        toChoices(FilterAlgorithm::kEntries), FilterAlgorithm::SVF));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::filterWarpStyle, 1}, "Filter Warp Style",
        toChoices(FilterWarpStyle::kEntries), FilterWarpStyle::Tanh));

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
    auto alphaRange = juce::NormalisableRange<float>(-2.0f, 2.0f,
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
        snapGenerationAlpha);
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::genAlpha, 1}, "Alpha",
        alphaRange, 0.0f));

    auto magnitudeRange = juce::NormalisableRange<float>(0.001f, 5.0f,
        convertSkew03From0To1,
        convertSkew03To0To1,
        snapGenerationMagnitude);
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::genMagnitude, 1}, "Magnitude",
        magnitudeRange, 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::genNoise, 1}, "Noise",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f, 0.3f), 0.0f));

    auto durationRange = juce::NormalisableRange<float>(0.1f, 11.0f,
        convertSkew03From0To1,
        convertSkew03To0To1,
        snapGenerationDuration);
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::genDuration, 1}, "Duration",
        // 11s hard cap in the UI — Stable Audio Open Small tops out at 11s
        // and T5ynth is for short sound samples, not music. SA 1.0 can do
        // more internally but the slider stays unified at 11s.
        durationRange, 3.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::infSteps, 1}, "Steps", 1, 100, 8));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::genCfg, 1}, "CFG Scale",
        juce::NormalisableRange<float>(1.0f, 15.0f, 0.1f), 1.0f));
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
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f, 0.3f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::lfo1Wave, 1}, "LFO1 Wave",
        toChoices(LfoWave::kEntries), 0));

    // LFO 2 (reference defaults: rate=0.5, depth=0, triangle)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::lfo2Rate, 1}, "LFO2 Rate",
        juce::NormalisableRange<float>(0.01f, 30.0f, 0.01f, 0.3f), 0.5f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::lfo2Depth, 1}, "LFO2 Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f, 0.3f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::lfo2Wave, 1}, "LFO2 Wave",
        toChoices(LfoWave::kEntries), 1));

    // LFO 3 (defaults: rate=0.2, depth=0, sine)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::lfo3Rate, 1}, "LFO3 Rate",
        juce::NormalisableRange<float>(0.01f, 30.0f, 0.01f, 0.3f), 0.2f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::lfo3Depth, 1}, "LFO3 Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f, 0.3f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::lfo3Wave, 1}, "LFO3 Wave",
        toChoices(LfoWave::kEntries), 0));

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
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f, 0.3f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::drift2Rate, 1}, "Drift2 Rate",
        juce::NormalisableRange<float>(0.001f, 2.0f, 0.001f, 0.3f), 0.005f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::drift2Depth, 1}, "Drift2 Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f, 0.3f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::drift3Rate, 1}, "Drift3 Rate",
        juce::NormalisableRange<float>(0.001f, 2.0f, 0.001f, 0.3f), 0.002f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::drift3Depth, 1}, "Drift3 Depth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f, 0.3f), 0.0f));

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
    const juce::StringArray envVelTimeChoices = toChoices(EnvVelTimeMode::kEntries);
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::ampAttackVelMode, 1}, "Amp Attack Vel Mode", envVelTimeChoices, EnvVelTimeMode::Off));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::ampDecayVelMode, 1}, "Amp Decay Vel Mode", envVelTimeChoices, EnvVelTimeMode::Off));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::ampReleaseVelMode, 1}, "Amp Release Vel Mode", envVelTimeChoices, EnvVelTimeMode::Off));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod1AttackCurve, 2},  "Mod1 Attack Curve",  curveChoices, 2));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod1DecayCurve, 2},   "Mod1 Decay Curve",   curveChoices, 2));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod1ReleaseCurve, 2}, "Mod1 Release Curve", curveChoices, 4));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod1AttackVelMode, 1}, "Mod1 Attack Vel Mode", envVelTimeChoices, EnvVelTimeMode::Off));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod1DecayVelMode, 1}, "Mod1 Decay Vel Mode", envVelTimeChoices, EnvVelTimeMode::Off));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod1ReleaseVelMode, 1}, "Mod1 Release Vel Mode", envVelTimeChoices, EnvVelTimeMode::Off));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod2AttackCurve, 2},  "Mod2 Attack Curve",  curveChoices, 2));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod2DecayCurve, 2},   "Mod2 Decay Curve",   curveChoices, 2));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod2ReleaseCurve, 2}, "Mod2 Release Curve", curveChoices, 4));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod2AttackVelMode, 1}, "Mod2 Attack Vel Mode", envVelTimeChoices, EnvVelTimeMode::Off));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod2DecayVelMode, 1}, "Mod2 Decay Vel Mode", envVelTimeChoices, EnvVelTimeMode::Off));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::mod2ReleaseVelMode, 1}, "Mod2 Release Vel Mode", envVelTimeChoices, EnvVelTimeMode::Off));

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
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::lfo3Target, 1}, "LFO3 Target", lfoTargetChoices, 0));

    // LFO Mode
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::lfo1Mode, 1}, "LFO1 Mode",
        toChoices(LfoMode::kEntries), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::lfo2Mode, 1}, "LFO2 Mode",
        toChoices(LfoMode::kEntries), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::lfo3Mode, 1}, "LFO3 Mode",
        toChoices(LfoMode::kEntries), 0));

    // BPM-sync clock mode + division for LFO 1/2/3, Drift 1/2/3, Delay.
    // ClockMode default Off; Division default 1/4 (= ClockDivision::D1_4).
    // No DSP behaviour yet — wired up here so presets save/load and the UI
    // can attach. Sync rate computation lands in a later step.
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::lfo1ClockMode, 1}, "LFO1 Clock Mode",
        toChoices(ClockMode::kEntries), ClockMode::Off));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::lfo1ClockDivision, 1}, "LFO1 Clock Division",
        toChoices(ClockDivision::kEntries), ClockDivision::D1_4));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::lfo2ClockMode, 1}, "LFO2 Clock Mode",
        toChoices(ClockMode::kEntries), ClockMode::Off));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::lfo2ClockDivision, 1}, "LFO2 Clock Division",
        toChoices(ClockDivision::kEntries), ClockDivision::D1_4));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::lfo3ClockMode, 1}, "LFO3 Clock Mode",
        toChoices(ClockMode::kEntries), ClockMode::Off));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::lfo3ClockDivision, 1}, "LFO3 Clock Division",
        toChoices(ClockDivision::kEntries), ClockDivision::D1_4));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::drift1ClockMode, 1}, "Drift1 Clock Mode",
        toChoices(ClockMode::kEntries), ClockMode::Off));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::drift1ClockDivision, 1}, "Drift1 Clock Division",
        toChoices(ClockDivision::kEntries), ClockDivision::D1_4));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::drift2ClockMode, 1}, "Drift2 Clock Mode",
        toChoices(ClockMode::kEntries), ClockMode::Off));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::drift2ClockDivision, 1}, "Drift2 Clock Division",
        toChoices(ClockDivision::kEntries), ClockDivision::D1_4));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::drift3ClockMode, 1}, "Drift3 Clock Mode",
        toChoices(ClockMode::kEntries), ClockMode::Off));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::drift3ClockDivision, 1}, "Drift3 Clock Division",
        toChoices(ClockDivision::kEntries), ClockDivision::D1_4));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::delayClockMode, 1}, "Delay Clock Mode",
        toChoices(ClockMode::kEntries), ClockMode::Off));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::delayClockDivision, 1}, "Delay Clock Division",
        toChoices(ClockDivision::kEntries), ClockDivision::D1_4));

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
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::seqShuffle, 1}, "Seq Shuffle",
        juce::NormalisableRange<float>(0.0f, 0.75f, 0.01f), 0.0f));
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

    // ── Polyphonic generative sequencer — shared pitch field ──
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::genFieldMode, 1}, "Field Mode",
        toChoices(FieldMode::kEntries), FieldMode::Drift));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::genFieldRate, 1}, "Field Rate", 1, 32, 8));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::genFieldCenterPc, 1}, "Field Center PC", 0, 11, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::genFieldPivot, 1}, "Field Pivot",
        toChoices(FieldPivot::kEntries), FieldPivot::m3));

    // ── Inter-strand coordination ──
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::genCoordinationMode, 1}, "Coordination Mode",
        toChoices(CoordinationMode::kEntries), CoordinationMode::DensityBudget));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::genCoordinationCap, 1}, "Coordination Cap", 1, 5, 3));

    // ── Strand 0 — role/octave/divMult/dominance (Euclidean params share legacy gen_* IDs) ──
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::genRole, 1}, "S1 Role",
        toChoices(StrandRole::kEntries), StrandRole::Line));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::genOctave, 1}, "S1 Octave", -2, 2, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::genDivMult, 1}, "S1 Div",
        toChoices(StrandDivMult::kEntries), StrandDivMult::X));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::genDominance, 1}, "S1 Dominance",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));

    // ── Strand 2 ──
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen2Enable, 1}, "S2 Enable", false));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::gen2Role, 1}, "S2 Role",
        toChoices(StrandRole::kEntries), StrandRole::Line));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::gen2Octave, 1}, "S2 Octave", -2, 2, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::gen2DivMult, 1}, "S2 Div",
        toChoices(StrandDivMult::kEntries), StrandDivMult::X));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::gen2Dominance, 1}, "S2 Dominance",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::gen2Steps, 1}, "S2 Steps", 2, 32, 16));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::gen2Pulses, 1}, "S2 Pulses", 1, 32, 5));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::gen2Rotation, 1}, "S2 Rotation", 0, 31, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::gen2Mutation, 1}, "S2 Mutation",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.20f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen2FixSteps, 1}, "S2 Fix Steps", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen2FixPulses, 1}, "S2 Fix Pulses", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen2FixRotation, 1}, "S2 Fix Rotation", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen2FixMutation, 1}, "S2 Fix Mutation", true));

    // ── Strand 3 ──
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen3Enable, 1}, "S3 Enable", false));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::gen3Role, 1}, "S3 Role",
        toChoices(StrandRole::kEntries), StrandRole::Line));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::gen3Octave, 1}, "S3 Octave", -2, 2, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::gen3DivMult, 1}, "S3 Div",
        toChoices(StrandDivMult::kEntries), StrandDivMult::X));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::gen3Dominance, 1}, "S3 Dominance",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::gen3Steps, 1}, "S3 Steps", 2, 32, 16));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::gen3Pulses, 1}, "S3 Pulses", 1, 32, 5));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::gen3Rotation, 1}, "S3 Rotation", 0, 31, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::gen3Mutation, 1}, "S3 Mutation",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.20f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen3FixSteps, 1}, "S3 Fix Steps", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen3FixPulses, 1}, "S3 Fix Pulses", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen3FixRotation, 1}, "S3 Fix Rotation", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen3FixMutation, 1}, "S3 Fix Mutation", true));

    // ── Strand 4 ──
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen4Enable, 1}, "S4 Enable", false));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::gen4Role, 1}, "S4 Role",
        toChoices(StrandRole::kEntries), StrandRole::Line));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::gen4Octave, 1}, "S4 Octave", -2, 2, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::gen4DivMult, 1}, "S4 Div",
        toChoices(StrandDivMult::kEntries), StrandDivMult::X));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::gen4Dominance, 1}, "S4 Dominance",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::gen4Steps, 1}, "S4 Steps", 2, 32, 16));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::gen4Pulses, 1}, "S4 Pulses", 1, 32, 5));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::gen4Rotation, 1}, "S4 Rotation", 0, 31, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::gen4Mutation, 1}, "S4 Mutation",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.20f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen4FixSteps, 1}, "S4 Fix Steps", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen4FixPulses, 1}, "S4 Fix Pulses", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen4FixRotation, 1}, "S4 Fix Rotation", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen4FixMutation, 1}, "S4 Fix Mutation", true));

    // ── Strand 5 ──
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen5Enable, 1}, "S5 Enable", false));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::gen5Role, 1}, "S5 Role",
        toChoices(StrandRole::kEntries), StrandRole::Line));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::gen5Octave, 1}, "S5 Octave", -2, 2, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::gen5DivMult, 1}, "S5 Div",
        toChoices(StrandDivMult::kEntries), StrandDivMult::X));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::gen5Dominance, 1}, "S5 Dominance",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::gen5Steps, 1}, "S5 Steps", 2, 32, 16));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::gen5Pulses, 1}, "S5 Pulses", 1, 32, 5));
    params.push_back(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{PID::gen5Rotation, 1}, "S5 Rotation", 0, 31, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::gen5Mutation, 1}, "S5 Mutation",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.20f));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen5FixSteps, 1}, "S5 Fix Steps", true));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen5FixPulses, 1}, "S5 Fix Pulses", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen5FixRotation, 1}, "S5 Fix Rotation", false));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::gen5FixMutation, 1}, "S5 Fix Mutation", true));

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
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{PID::wtAutoScan, 1}, "WT Auto Scan", true));

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
    lfo3.prepare(sampleRate);
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
    lfo3Buffer.resize(static_cast<size_t>(samplesPerBlock));
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

    // ── Host-transport snapshot (feeds resolveSyncBpm()) ────────────────────
    {
        bool playing = false;
        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                if (pos->getIsPlaying())
                {
                    playing = true;
                    if (auto bpm = pos->getBpm())
                        hostBpmLastSeen.store(static_cast<float>(*bpm),
                                              std::memory_order_relaxed);
                }
            }
        }
        hostPlayingNow.store(playing, std::memory_order_relaxed);
    }
    const float syncBpm = resolveSyncBpm();

    updateDriftState(numSamples, syncBpm);

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
        // Keep free-running modulators phase-accurate.
        lfo1.advancePhase(numSamples);
        lfo2.advancePhase(numSamples);
        lfo3.advancePhase(numSamples);
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
    bp.ampAttackVelMode  = static_cast<int>(parameters.getRawParameterValue(PID::ampAttackVelMode)->load());
    bp.ampDecayVelMode   = static_cast<int>(parameters.getRawParameterValue(PID::ampDecayVelMode)->load());
    bp.ampReleaseVelMode = static_cast<int>(parameters.getRawParameterValue(PID::ampReleaseVelMode)->load());

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
    bp.mod1AttackVelMode  = static_cast<int>(parameters.getRawParameterValue(PID::mod1AttackVelMode)->load());
    bp.mod1DecayVelMode   = static_cast<int>(parameters.getRawParameterValue(PID::mod1DecayVelMode)->load());
    bp.mod1ReleaseVelMode = static_cast<int>(parameters.getRawParameterValue(PID::mod1ReleaseVelMode)->load());

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
    bp.mod2AttackVelMode  = static_cast<int>(parameters.getRawParameterValue(PID::mod2AttackVelMode)->load());
    bp.mod2DecayVelMode   = static_cast<int>(parameters.getRawParameterValue(PID::mod2DecayVelMode)->load());
    bp.mod2ReleaseVelMode = static_cast<int>(parameters.getRawParameterValue(PID::mod2ReleaseVelMode)->load());

    // LFOs (global) — Clock-Sync override: when ClockMode::Sync, the rate
    // displayed on the slider is replaced by the sync-derived rate. We store
    // the effective rate back into `bp.lfo*Rate` so downstream env→LFO-rate
    // modulation (lines further down) scales relative to the sync rate.
    {
        const int   c1 = static_cast<int>(parameters.getRawParameterValue(PID::lfo1ClockMode)->load());
        const float r1 = parameters.getRawParameterValue(PID::lfo1Rate)->load();
        bp.lfo1Rate = (c1 == ClockMode::Off) ? r1
            : ClockSync::computeRate(syncBpm,
                static_cast<int>(parameters.getRawParameterValue(PID::lfo1ClockDivision)->load()));
        bp.lfo1Depth = parameters.getRawParameterValue(PID::lfo1Depth)->load();
        bp.lfo1Wave = static_cast<int>(parameters.getRawParameterValue(PID::lfo1Wave)->load());
        bp.lfo1TrigMode = static_cast<int>(parameters.getRawParameterValue(PID::lfo1Mode)->load()) == LfoMode::Trigger;
        lfo1.setRate(bp.lfo1Rate);
        lfo1.setDepth(1.0f);
        lfo1.setWaveform(bp.lfo1Wave);
        bp.lfo1Target = static_cast<int>(parameters.getRawParameterValue(PID::lfo1Target)->load());
    }
    {
        const int   c2 = static_cast<int>(parameters.getRawParameterValue(PID::lfo2ClockMode)->load());
        const float r2 = parameters.getRawParameterValue(PID::lfo2Rate)->load();
        bp.lfo2Rate = (c2 == ClockMode::Off) ? r2
            : ClockSync::computeRate(syncBpm,
                static_cast<int>(parameters.getRawParameterValue(PID::lfo2ClockDivision)->load()));
        bp.lfo2Depth = parameters.getRawParameterValue(PID::lfo2Depth)->load();
        bp.lfo2Wave = static_cast<int>(parameters.getRawParameterValue(PID::lfo2Wave)->load());
        bp.lfo2TrigMode = static_cast<int>(parameters.getRawParameterValue(PID::lfo2Mode)->load()) == LfoMode::Trigger;
        lfo2.setRate(bp.lfo2Rate);
        lfo2.setDepth(1.0f);
        lfo2.setWaveform(bp.lfo2Wave);
        bp.lfo2Target = static_cast<int>(parameters.getRawParameterValue(PID::lfo2Target)->load());
    }
    {
        const int   c3 = static_cast<int>(parameters.getRawParameterValue(PID::lfo3ClockMode)->load());
        const float r3 = parameters.getRawParameterValue(PID::lfo3Rate)->load();
        bp.lfo3Rate = (c3 == ClockMode::Off) ? r3
            : ClockSync::computeRate(syncBpm,
                static_cast<int>(parameters.getRawParameterValue(PID::lfo3ClockDivision)->load()));
        bp.lfo3Depth = parameters.getRawParameterValue(PID::lfo3Depth)->load();
        bp.lfo3Wave = static_cast<int>(parameters.getRawParameterValue(PID::lfo3Wave)->load());
        bp.lfo3TrigMode = static_cast<int>(parameters.getRawParameterValue(PID::lfo3Mode)->load()) == LfoMode::Trigger;
        lfo3.setRate(bp.lfo3Rate);
        lfo3.setDepth(1.0f);
        lfo3.setWaveform(bp.lfo3Wave);
        bp.lfo3Target = static_cast<int>(parameters.getRawParameterValue(PID::lfo3Target)->load());
    }

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
    bp.filterDriveDb = parameters.getRawParameterValue(PID::filterDrive)->load();
    bp.filterDriveOs = static_cast<int>(parameters.getRawParameterValue(PID::filterDriveOs)->load());
    bp.filterAlgorithm = static_cast<int>(parameters.getRawParameterValue(PID::filterAlgorithm)->load());
    bp.filterWarpStyle = static_cast<int>(parameters.getRawParameterValue(PID::filterWarpStyle)->load());
    bp.filterDriveGain = std::pow(10.0f, bp.filterDriveDb * (1.0f / 20.0f));

    // Scan
    bp.baseScan = parameters.getRawParameterValue(PID::oscScan)->load();

    // Octave shift (APVTS choice 0-4, maps to -2..+2)
    bp.octaveShift = static_cast<int>(parameters.getRawParameterValue(PID::oscOctave)->load()) - 2;

    // Noise oscillator
    bp.noiseLevel = parameters.getRawParameterValue(PID::noiseLevel)->load();
    bp.noiseType = static_cast<int>(parameters.getRawParameterValue(PID::noiseType)->load());

    // Wavetable smooth
    bp.wtSmooth = parameters.getRawParameterValue(PID::wtSmooth)->load() > 0.5f;
    bp.wtAutoScan = parameters.getRawParameterValue(PID::wtAutoScan)->load() > 0.5f;

    // Engine mode — read directly from APVTS (0=Sampler, 1=Wavetable)
    int engineModeRaw = static_cast<int>(parameters.getRawParameterValue(PID::engineMode)->load());
    bp.engineIsWavetable = (engineModeRaw == 1);
    voiceManager.setEngineMode(bp.engineIsWavetable ? SynthVoice::EngineMode::Wavetable
                                                     : SynthVoice::EngineMode::Sampler);

    // Master volume (dB → linear)
    float masterDb = parameters.getRawParameterValue(PID::masterVol)->load();
    float masterGain = juce::Decibels::decibelsToGain(masterDb);

    // Apply drift offsets to their respective targets
    bp.driftScanOffset   = driftLfo.getOffsetForTarget(DriftLFO::TgtWtScan);
    bp.driftFilterOffset = driftLfo.getOffsetForTarget(DriftLFO::TgtFilter);
    bp.driftPitchOffset  = driftLfo.getOffsetForTarget(DriftLFO::TgtPitch);
    // Block-level drift targets (delay/reverb) applied after modDelayTime etc. are declared

    // Drift → envelope amounts (additive, clamped to 0–1)
    bp.ampAmount  = juce::jlimit(0.0f, 1.0f, bp.ampAmount  + driftLfo.getOffsetForTarget(DriftLFO::TgtEnv1Amt));
    bp.mod1Amount = juce::jlimit(0.0f, 1.0f, bp.mod1Amount + driftLfo.getOffsetForTarget(DriftLFO::TgtEnv2Amt));
    bp.mod2Amount = juce::jlimit(0.0f, 1.0f, bp.mod2Amount + driftLfo.getOffsetForTarget(DriftLFO::TgtEnv3Amt));

    // ── Sampler settings ─────────────────────────────────────────────────────
    int loopModeIdx = static_cast<int>(parameters.getRawParameterValue(PID::loopMode)->load());
    masterSampler.setLoopMode(static_cast<SamplePlayer::LoopMode>(loopModeIdx));
    masterSampler.setCrossfadeMs(parameters.getRawParameterValue(PID::crossfadeMs)->load());
    masterSampler.setNormalize(parameters.getRawParameterValue(PID::normalize)->load() > 0.5f);
    masterSampler.setLoopOptimizeLevel(static_cast<int>(parameters.getRawParameterValue(PID::loopOptimize)->load()));

    // Give noteOn/noteOff access to the current envelope/modulation block state.
    voiceManager.setBlockParams(bp);

    // ── Sequencer / Arpeggiator (in series: Seq → Arp → synth) ─────────────
    // (seqRunning already read above for idle detection)
    float seqBpm = parameters.getRawParameterValue(PID::seqBpm)->load();
    int seqSteps = static_cast<int>(parameters.getRawParameterValue(PID::seqSteps)->load());
    int seqOctaveShift = static_cast<int>(parameters.getRawParameterValue(PID::seqOctave)->load()) - 2;
    float seqGate = parameters.getRawParameterValue(PID::seqGate)->load();
    float seqShuffle = parameters.getRawParameterValue(PID::seqShuffle)->load();
    int seqPreset = static_cast<int>(parameters.getRawParameterValue(PID::seqPreset)->load());
    int arpModeRaw = static_cast<int>(parameters.getRawParameterValue(PID::arpMode)->load());
    bool arpEnabled = arpModeRaw > 0;
    int arpMode = arpModeRaw > 0 ? arpModeRaw - 1 : 0; // 0=Up,1=Down,2=UpDown,3=Random
    int arpRate = static_cast<int>(parameters.getRawParameterValue(PID::arpRate)->load());
    int arpOctaves = static_cast<int>(parameters.getRawParameterValue(PID::arpOctaves)->load());

    // Arp false→true edge: the active sequencer's currently-sounding note was
    // emitted direct-to-synth last block, and from this block on the arp will
    // swallow all seq note-offs — so flush that single note now before the
    // engines run. Manual keyboard notes stay untouched.
    if (arpEnabled && !arpWasEnabled)
    {
        if (genModeActiveInAudio)
            generativeSequencer.allNotesOff(midiMessages, 0);
        else
            stepSequencer.allNotesOff(midiMessages, 0);
    }

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

                // Flush step-seq's currently-sounding note before the gen-seq
                // takes over — avoids a hanging voice across the engine swap.
                stepSequencer.allNotesOff(midiMessages, 0);
                stepSequencer.stop();
                generativeSequencer.setBpm(static_cast<double>(seqBpm));
                generativeSequencer.setDivision(seqDivision);
                generativeSequencer.setGate(seqGate);
                generativeSequencer.setShuffle(seqShuffle);
                generativeSequencer.setScale(
                    static_cast<int>(parameters.getRawParameterValue(PID::scaleType)->load()),
                    static_cast<int>(parameters.getRawParameterValue(PID::scaleRoot)->load()));
                generativeSequencer.setPrimaryTransposeSemitones(seqOctaveShift * 12);
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
                // Flush every gen-seq strand's sounding note before handing
                // back to the step-seq — avoids hanging voices across the swap.
                generativeSequencer.allNotesOff(midiMessages, 0);
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
        generativeSequencer.setShuffle(seqShuffle);
        generativeSequencer.setPrimaryTransposeSemitones(seqOctaveShift * 12);

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

        // ── Shared pitch-field setters ──
        generativeSequencer.setFieldMode(static_cast<int>(
            parameters.getRawParameterValue(PID::genFieldMode)->load()));
        generativeSequencer.setFieldChangeRate(static_cast<int>(
            parameters.getRawParameterValue(PID::genFieldRate)->load()));
        // Field Center PC follows Scale Root — they are conceptually the
        // same anchor, and a separate "second tonic" dropdown was confusing.
        const int scaleRootForField = static_cast<int>(
            parameters.getRawParameterValue(PID::scaleRoot)->load());
        generativeSequencer.setFieldCenterPc(scaleRootForField);

        // Field Pivot interval is derived from Scale Type — its musical
        // character chooses one of three universal pivots: P5 for major
        // flavours, m3 for minor flavours, Tri for symmetric / sharp-fourth
        // scales. Pivot mode then transposes the pc-set by this interval.
        {
            const int scaleTypeForField = static_cast<int>(
                parameters.getRawParameterValue(PID::scaleType)->load());
            int pivotSemitones = 7;  // P5 default
            switch (scaleTypeForField)
            {
                case ScaleType::WhlT: case ScaleType::Locr:
                case ScaleType::HunM: case ScaleType::Lyd:
                case ScaleType::Hjz:
                    pivotSemitones = 6; break;   // Tri
                case ScaleType::Min:  case ScaleType::Dor:  case ScaleType::Phry:
                case ScaleType::Harm: case ScaleType::MelM: case ScaleType::MinP:
                case ScaleType::Blu:  case ScaleType::Hira: case ScaleType::InSn:
                case ScaleType::Iwat: case ScaleType::Kumo: case ScaleType::Ryuk:
                case ScaleType::DblH: case ScaleType::Todi: case ScaleType::Purv:
                case ScaleType::Pers: case ScaleType::NeaM:
                    pivotSemitones = 3; break;   // m3
                default:
                    pivotSemitones = 7; break;   // P5
            }
            generativeSequencer.setFieldPivotInterval(pivotSemitones);
        }

        // ── Inter-strand coordination ──
        generativeSequencer.setCoordinationMode(static_cast<int>(
            parameters.getRawParameterValue(PID::genCoordinationMode)->load()));
        generativeSequencer.setCoordinationCap(static_cast<int>(
            parameters.getRawParameterValue(PID::genCoordinationCap)->load()));

        // ── Per-strand setters (0..4) ──
        {
            struct StrandPIDs {
                const char* enable;
                const char* role;
                const char* octave;
                const char* divMult;
                const char* dominance;
                const char* steps;
                const char* pulses;
                const char* rotation;
                const char* mutation;
                const char* fixSteps;
                const char* fixPulses;
                const char* fixRotation;
                const char* fixMutation;
            };
            static const StrandPIDs kStrands[5] = {
                { nullptr,         PID::genRole,  PID::genOctave,  PID::genDivMult,  PID::genDominance,
                  PID::genSteps,   PID::genPulses, PID::genRotation, PID::genMutation,
                  PID::genFixSteps, PID::genFixPulses, PID::genFixRotation, PID::genFixMutation },
                { PID::gen2Enable, PID::gen2Role, PID::gen2Octave, PID::gen2DivMult, PID::gen2Dominance,
                  PID::gen2Steps,  PID::gen2Pulses, PID::gen2Rotation, PID::gen2Mutation,
                  PID::gen2FixSteps, PID::gen2FixPulses, PID::gen2FixRotation, PID::gen2FixMutation },
                { PID::gen3Enable, PID::gen3Role, PID::gen3Octave, PID::gen3DivMult, PID::gen3Dominance,
                  PID::gen3Steps,  PID::gen3Pulses, PID::gen3Rotation, PID::gen3Mutation,
                  PID::gen3FixSteps, PID::gen3FixPulses, PID::gen3FixRotation, PID::gen3FixMutation },
                { PID::gen4Enable, PID::gen4Role, PID::gen4Octave, PID::gen4DivMult, PID::gen4Dominance,
                  PID::gen4Steps,  PID::gen4Pulses, PID::gen4Rotation, PID::gen4Mutation,
                  PID::gen4FixSteps, PID::gen4FixPulses, PID::gen4FixRotation, PID::gen4FixMutation },
                { PID::gen5Enable, PID::gen5Role, PID::gen5Octave, PID::gen5DivMult, PID::gen5Dominance,
                  PID::gen5Steps,  PID::gen5Pulses, PID::gen5Rotation, PID::gen5Mutation,
                  PID::gen5FixSteps, PID::gen5FixPulses, PID::gen5FixRotation, PID::gen5FixMutation }
            };

            for (int i = 0; i < 5; ++i)
            {
                const auto& ids = kStrands[i];

                if (ids.enable != nullptr)
                {
                    const bool en = parameters.getRawParameterValue(ids.enable)->load() > 0.5f;
                    generativeSequencer.setStrandEnabled(i, en);
                }

                generativeSequencer.setStrandRole(i, static_cast<int>(
                    parameters.getRawParameterValue(ids.role)->load()));

                if (i == 0)
                {
                    // Strand 0 is the legacy mono-gen voice: preserve its
                    // original melody/rhythm and let only Seq Octave transpose it.
                    generativeSequencer.setStrandOctave(i, 0);
                    generativeSequencer.setStrandDivMult(i, 1.0f);
                    generativeSequencer.setStrandDominance(i, 0.0f);
                    continue;
                }

                generativeSequencer.setStrandOctave(i, static_cast<int>(
                    parameters.getRawParameterValue(ids.octave)->load()));
                {
                    int dIdx = juce::jlimit(0, StrandDivMult::kCount - 1, static_cast<int>(
                        parameters.getRawParameterValue(ids.divMult)->load()));
                    generativeSequencer.setStrandDivMult(i, StrandDivMult::kFactor[dIdx]);
                }
                generativeSequencer.setStrandDominance(i, static_cast<float>(
                    parameters.getRawParameterValue(ids.dominance)->load()));

                const bool sFix = parameters.getRawParameterValue(ids.fixSteps   )->load() > 0.5f;
                const bool pFix = parameters.getRawParameterValue(ids.fixPulses  )->load() > 0.5f;
                const bool rFix = parameters.getRawParameterValue(ids.fixRotation)->load() > 0.5f;
                const bool mFix = parameters.getRawParameterValue(ids.fixMutation)->load() > 0.5f;
                generativeSequencer.setStrandFixSteps   (i, sFix);
                generativeSequencer.setStrandFixPulses  (i, pFix);
                generativeSequencer.setStrandFixRotation(i, rFix);
                generativeSequencer.setStrandFixMutation(i, mFix);

                const int gs = static_cast<int>(parameters.getRawParameterValue(ids.steps   )->load());
                const int gp = static_cast<int>(parameters.getRawParameterValue(ids.pulses  )->load());
                const int gr = static_cast<int>(parameters.getRawParameterValue(ids.rotation)->load());
                generativeSequencer.setStrandSteps   (i, gs);
                generativeSequencer.setStrandPulses  (i, gp);
                generativeSequencer.setStrandRotation(i, gr);

                const float gm = parameters.getRawParameterValue(ids.mutation)->load();
                generativeSequencer.setStrandBaseMutation(i, gm);
                generativeSequencer.setStrandMutation    (i, gm);
            }
        }

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
        stepSequencer.setShuffle(seqShuffle);
        stepSequencer.setAllGates(seqGate);
        int seqOctaveIdx = static_cast<int>(parameters.getRawParameterValue(PID::seqOctave)->load());
        stepSequencer.setOctaveShiftSemitones((seqOctaveIdx - 2) * 12);

        if (seqRunning)
            stepSequencer.start();
        else
            stepSequencer.stop();
        stepSequencer.processBlock(buffer, midiMessages);
    }

    // Consume bar-start flag (still used for sequencer display)
    if (stepSequencer.barStartFlag.exchange(false))
        barBoundaryFlag.store(true, std::memory_order_relaxed);

    // (driftRegenBpm is now stored in updateDriftState() with the resolved
    // sync BPM — no duplicate write needed here.)

    // Stage 2: Arpeggiator (consumes seq note events, generates arpeggiated output)
    if (arpEnabled)
    {
        arpeggiator.setBpm(static_cast<double>(seqBpm));
        arpeggiator.setRate(arpRate);
        arpeggiator.setOctaveRange(arpOctaves);
        arpeggiator.setShuffle(seqShuffle);
        arpeggiator.setMode(static_cast<T5ynthArpeggiator::Mode>(arpMode));

        juce::MidiBuffer filtered;
        for (const auto metadata : midiMessages)
        {
            auto msg = metadata.getMessage();

            // When a sequencer is driving, only the "lead" channel feeds the
            // arp — otherwise the polyphonic gen-seq's secondary strands
            // (channels 4-7) would chaotically slap the arp's base note
            // between every step. Lead channels:
            //   gen-seq active → channel 3 (strand 0)
            //   step-seq active → channel 1 (normal) or 2 (bind/glide)
            // When seq is stopped (manual keyboard play) any channel feeds
            // the arp, preserving the historical external-input behaviour.
            const int ch = msg.getChannel();
            const bool isLead = !seqRunning
                              || (genModeActiveInAudio
                                  ? (ch == 3)
                                  : (ch == 1 || ch == 2));

            if (msg.isNoteOn())
            {
                if (isLead)
                    arpeggiator.setBaseNote(msg.getNoteNumber(), msg.getFloatVelocity());
                else
                    filtered.addEvent(msg, metadata.samplePosition);
            }
            else if (msg.isNoteOff())
            {
                if (isLead)
                {
                    // When seq is running, don't kill arp on seq gate-offs —
                    // arp runs continuously, seq just updates the base note.
                    if (!seqRunning)
                        arpeggiator.stopArp();
                }
                else
                {
                    filtered.addEvent(msg, metadata.samplePosition);
                }
            }
            else
            {
                filtered.addEvent(msg, metadata.samplePosition);
            }
        }
        midiMessages.swapWith(filtered);
        arpeggiator.processBlock(buffer, midiMessages);
    }
    else
    {
        // Arp off: flush the arp's own sounding note (if any), then drop state.
        arpeggiator.allNotesOff(midiMessages, 0);
        arpeggiator.reset();
    }

    arpWasEnabled = arpEnabled;

    // (barStartFlag consumed + forwarded to barBoundaryFlag above)

    // ── Sample-accurate MIDI + Voice rendering ──────────────────────────────
    const bool lfo1TrigMode = bp.lfo1TrigMode;
    const bool lfo2TrigMode = bp.lfo2TrigMode;
    const bool lfo3TrigMode = bp.lfo3TrigMode;

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
        float baseLfo1Rate = bp.lfo1Rate;
        float baseLfo2Rate = bp.lfo2Rate;
        float baseLfo3Rate = bp.lfo3Rate;
        float baseLfo1Depth = bp.lfo1Depth;
        float baseLfo2Depth = bp.lfo2Depth;
        float baseLfo3Depth = bp.lfo3Depth;
        float baseDrift1Depth = parameters.getRawParameterValue(PID::drift1Depth)->load();
        float baseDrift2Depth = parameters.getRawParameterValue(PID::drift2Depth)->load();
        float baseDrift3Depth = parameters.getRawParameterValue(PID::drift3Depth)->load();

        // Re-prepare master sampler if settings changed, then distribute to all voices
        if (masterSampler.hasAudio())
        {
            if (masterSampler.needsReprepare())
            {
                samplerProcessorDebugLog("processBlock preparePlaybackBuffer master={" + masterSampler.debugStateString() + "}");
                masterSampler.preparePlaybackBuffer();
            }
            voiceManager.distributeSamplerBuffer(masterSampler);
        }
        if (masterOsc.hasFrames() && generatedAudioFull.getNumSamples() > 0)
        {
            syncWavetableTraversal(generatedSampleRate, generatedAudioFull.getNumSamples());
            masterOsc.setMorphTimeMs(parameters.getRawParameterValue(PID::driftCrossfade)->load());
            voiceManager.distributeWavetableFrames(masterOsc);
        }

        // Pre-compute global LFO values for the block (needed by VoiceManager)
        float* lfo1Buf = lfo1Buffer.data();
        float* lfo2Buf = lfo2Buffer.data();
        float* lfo3Buf = lfo3Buffer.data();
        for (int i = 0; i < numSamples; ++i)
        {
            float l1 = lfo1.processSample();
            float l2 = lfo2.processSample();
            float l3 = lfo3.processSample();

            lfo1Buf[i] = l1;
            lfo2Buf[i] = l2;
            lfo3Buf[i] = l3;
        }

        // LFO → normalized amount/depth targets (additive, clamped to 0–1)
        {
            float l1End = numSamples > 0 ? lfo1Buf[numSamples - 1] : 0.0f;
            float l2End = numSamples > 0 ? lfo2Buf[numSamples - 1] : 0.0f;
            float l3End = numSamples > 0 ? lfo3Buf[numSamples - 1] : 0.0f;
            if (bp.lfo1Target == LfoTarget::Env1Amt) bp.ampAmount  = applyNormalizedOffset(bp.ampAmount,  l1End);
            if (bp.lfo1Target == LfoTarget::Env2Amt) bp.mod1Amount = applyNormalizedOffset(bp.mod1Amount, l1End);
            if (bp.lfo1Target == LfoTarget::Env3Amt) bp.mod2Amount = applyNormalizedOffset(bp.mod2Amount, l1End);
            if (bp.lfo1Target == LfoTarget::Drift1Depth) baseDrift1Depth = applyNormalizedOffset(baseDrift1Depth, l1End);
            if (bp.lfo1Target == LfoTarget::Drift2Depth) baseDrift2Depth = applyNormalizedOffset(baseDrift2Depth, l1End);
            if (bp.lfo1Target == LfoTarget::Drift3Depth) baseDrift3Depth = applyNormalizedOffset(baseDrift3Depth, l1End);
            if (bp.lfo2Target == LfoTarget::Env1Amt) bp.ampAmount  = applyNormalizedOffset(bp.ampAmount,  l2End);
            if (bp.lfo2Target == LfoTarget::Env2Amt) bp.mod1Amount = applyNormalizedOffset(bp.mod1Amount, l2End);
            if (bp.lfo2Target == LfoTarget::Env3Amt) bp.mod2Amount = applyNormalizedOffset(bp.mod2Amount, l2End);
            if (bp.lfo2Target == LfoTarget::Drift1Depth) baseDrift1Depth = applyNormalizedOffset(baseDrift1Depth, l2End);
            if (bp.lfo2Target == LfoTarget::Drift2Depth) baseDrift2Depth = applyNormalizedOffset(baseDrift2Depth, l2End);
            if (bp.lfo2Target == LfoTarget::Drift3Depth) baseDrift3Depth = applyNormalizedOffset(baseDrift3Depth, l2End);
            if (bp.lfo3Target == LfoTarget::Env1Amt) bp.ampAmount  = applyNormalizedOffset(bp.ampAmount,  l3End);
            if (bp.lfo3Target == LfoTarget::Env2Amt) bp.mod1Amount = applyNormalizedOffset(bp.mod1Amount, l3End);
            if (bp.lfo3Target == LfoTarget::Env3Amt) bp.mod2Amount = applyNormalizedOffset(bp.mod2Amount, l3End);
            if (bp.lfo3Target == LfoTarget::Drift1Depth) baseDrift1Depth = applyNormalizedOffset(baseDrift1Depth, l3End);
            if (bp.lfo3Target == LfoTarget::Drift2Depth) baseDrift2Depth = applyNormalizedOffset(baseDrift2Depth, l3End);
            if (bp.lfo3Target == LfoTarget::Drift3Depth) baseDrift3Depth = applyNormalizedOffset(baseDrift3Depth, l3End);

            driftLfo.setLfoDepth(0, baseDrift1Depth);
            driftLfo.setLfoDepth(1, baseDrift2Depth);
            driftLfo.setLfoDepth(2, baseDrift3Depth);
        }

        // Scan → P1 modulation offset (Sampler mode: retrigger uses it)
        if (!bp.engineIsWavetable)
        {
            float p1Mod = bp.driftScanOffset;
            float l1 = numSamples > 0 ? lfo1Buf[numSamples - 1] : 0.0f;
            float l2 = numSamples > 0 ? lfo2Buf[numSamples - 1] : 0.0f;
            float l3 = numSamples > 0 ? lfo3Buf[numSamples - 1] : 0.0f;
            if (bp.lfo1Target == LfoTarget::Scan) p1Mod += l1;
            if (bp.lfo2Target == LfoTarget::Scan) p1Mod += l2;
            if (bp.lfo3Target == LfoTarget::Scan) p1Mod += l3;
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
                        lfo1Buf + renderPos, lfo2Buf + renderPos, lfo3Buf + renderPos,
                        renderPos, subLen);

                // Process all MIDI events at this sample position
                while (midiIter != midiMessages.cend()
                       && (*midiIter).samplePosition <= subEnd)
                {
                    const auto msg = (*midiIter).getMessage();
                    if (msg.isNoteOn())
                    {
                        int note = msg.getNoteNumber();
                        float velocity = msg.getFloatVelocity();
                        const int channel = msg.getChannel();
                        bool isBind = (channel == 2);
                        const int genSourceId = (channel >= 3 && channel <= 6) ? channel - 3 : -1;
                        const float sourcePan = genSourceId >= 0
                                              ? genStrandPan[static_cast<size_t>(genSourceId)]
                                              : 0.0f;

                        lastMidiNote.store(note, std::memory_order_relaxed);
                        lastMidiVelocity.store(juce::roundToInt(velocity * 127.0f),
                                               std::memory_order_relaxed);
                        lastMidiNoteOn.store(true, std::memory_order_relaxed);

                        voiceManager.noteOn(note, velocity, isBind,
                            isBind ? 0.0f : stepSequencer.getGlideTime(),
                            lfo1TrigMode, lfo2TrigMode, lfo3TrigMode,
                            genSourceId, sourcePan);
                    }
                    else if (msg.isNoteOff())
                    {
                        const int channel = msg.getChannel();
                        const int genSourceId = (channel >= 3 && channel <= 6) ? channel - 3 : -1;
                        voiceManager.noteOff(msg.getNoteNumber(), genSourceId);
                        if (!voiceManager.hasActiveVoices())
                            lastMidiNoteOn.store(false, std::memory_order_relaxed);
                    }
                    else if (msg.isController()
                             && msg.getControllerNumber() == 10
                             && msg.getChannel() >= 3
                             && msg.getChannel() <= 6)
                    {
                        const int genSourceId = msg.getChannel() - 3;
                        const float value = static_cast<float>(msg.getControllerValue()) / 127.0f;
                        genStrandPan[static_cast<size_t>(genSourceId)] =
                            juce::jlimit(-1.0f, 1.0f, value * 2.0f - 1.0f);
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
        float effectiveLfo1Depth = baseLfo1Depth;
        float effectiveLfo2Depth = baseLfo2Depth;
        float effectiveLfo3Depth = baseLfo3Depth;
        if (bp.mod1Target == EnvTarget::LFO1Depth) effectiveLfo1Depth = applyNormalizedOffset(effectiveLfo1Depth, lastMod1Val);
        if (bp.mod2Target == EnvTarget::LFO1Depth) effectiveLfo1Depth = applyNormalizedOffset(effectiveLfo1Depth, lastMod2Val);
        if (bp.mod1Target == EnvTarget::LFO2Depth) effectiveLfo2Depth = applyNormalizedOffset(effectiveLfo2Depth, lastMod1Val);
        if (bp.mod2Target == EnvTarget::LFO2Depth) effectiveLfo2Depth = applyNormalizedOffset(effectiveLfo2Depth, lastMod2Val);
        if (bp.mod1Target == EnvTarget::LFO3Depth) effectiveLfo3Depth = applyNormalizedOffset(effectiveLfo3Depth, lastMod1Val);
        if (bp.mod2Target == EnvTarget::LFO3Depth) effectiveLfo3Depth = applyNormalizedOffset(effectiveLfo3Depth, lastMod2Val);
        float rawLastLfo1Val = numSamples > 0 ? lfo1Buf[numSamples - 1] : 0.0f;
        float rawLastLfo2Val = numSamples > 0 ? lfo2Buf[numSamples - 1] : 0.0f;
        float rawLastLfo3Val = numSamples > 0 ? lfo3Buf[numSamples - 1] : 0.0f;
        float lastLfo1Val = rawLastLfo1Val * effectiveLfo1Depth;
        float lastLfo2Val = rawLastLfo2Val * effectiveLfo2Depth;
        float lastLfo3Val = rawLastLfo3Val * effectiveLfo3Depth;
        lastLfo1Val_ = lastLfo1Val;
        lastLfo2Val_ = lastLfo2Val;
        lastLfo3Val_ = lastLfo3Val;

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
        if (bp.mod1Target == EnvTarget::LFO1Depth)  effectiveLfo1Depth = applyNormalizedOffset(effectiveLfo1Depth, lastMod1Val);
        if (bp.mod1Target == EnvTarget::LFO2Rate)   lfo2.setRate(baseLfo2Rate * (1.0f + lastMod1Val));
        if (bp.mod1Target == EnvTarget::LFO2Depth)  effectiveLfo2Depth = applyNormalizedOffset(effectiveLfo2Depth, lastMod1Val);
        if (bp.mod1Target == EnvTarget::LFO3Rate)   lfo3.setRate(baseLfo3Rate * (1.0f + lastMod1Val));
        if (bp.mod1Target == EnvTarget::LFO3Depth)  effectiveLfo3Depth = applyNormalizedOffset(effectiveLfo3Depth, lastMod1Val);
        if (bp.mod2Target == EnvTarget::LFO1Rate)   lfo1.setRate(baseLfo1Rate * (1.0f + lastMod2Val));
        if (bp.mod2Target == EnvTarget::LFO1Depth)  effectiveLfo1Depth = applyNormalizedOffset(effectiveLfo1Depth, lastMod2Val);
        if (bp.mod2Target == EnvTarget::LFO2Rate)   lfo2.setRate(baseLfo2Rate * (1.0f + lastMod2Val));
        if (bp.mod2Target == EnvTarget::LFO2Depth)  effectiveLfo2Depth = applyNormalizedOffset(effectiveLfo2Depth, lastMod2Val);
        if (bp.mod2Target == EnvTarget::LFO3Rate)   lfo3.setRate(baseLfo3Rate * (1.0f + lastMod2Val));
        if (bp.mod2Target == EnvTarget::LFO3Depth)  effectiveLfo3Depth = applyNormalizedOffset(effectiveLfo3Depth, lastMod2Val);
        if (bp.lfo1Target == LfoTarget::DelayTime)  modDelayTime += lastLfo1Val;
        if (bp.lfo1Target == LfoTarget::DelayFB)    modDelayFb += lastLfo1Val;
        if (bp.lfo1Target == LfoTarget::DelayMix)   modDelayMix += lastLfo1Val;
        if (bp.lfo1Target == LfoTarget::ReverbMix)  modReverbMix += lastLfo1Val;
        if (bp.lfo2Target == LfoTarget::DelayTime)  modDelayTime += lastLfo2Val;
        if (bp.lfo2Target == LfoTarget::DelayFB)    modDelayFb += lastLfo2Val;
        if (bp.lfo2Target == LfoTarget::DelayMix)   modDelayMix += lastLfo2Val;
        if (bp.lfo2Target == LfoTarget::ReverbMix)  modReverbMix += lastLfo2Val;
        if (bp.lfo3Target == LfoTarget::DelayTime)  modDelayTime += lastLfo3Val;
        if (bp.lfo3Target == LfoTarget::DelayFB)    modDelayFb += lastLfo3Val;
        if (bp.lfo3Target == LfoTarget::DelayMix)   modDelayMix += lastLfo3Val;
        if (bp.lfo3Target == LfoTarget::ReverbMix)  modReverbMix += lastLfo3Val;
    }
    else
    {
        // Free-running LFOs: sample one value for ghost display, advance rest
        if (numSamples > 0)
        {
            lastLfo1Val_ = lfo1.processSample() * bp.lfo1Depth;
            lastLfo2Val_ = lfo2.processSample() * bp.lfo2Depth;
            lastLfo3Val_ = lfo3.processSample() * bp.lfo3Depth;
            if (numSamples > 1)
            {
                lfo1.advancePhase(numSamples - 1);
                lfo2.advancePhase(numSamples - 1);
                lfo3.advancePhase(numSamples - 1);
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
        const int delayClock = static_cast<int>(parameters.getRawParameterValue(PID::delayClockMode)->load());
        const float baseDelayTime = (delayClock == ClockMode::Off)
            ? parameters.getRawParameterValue(PID::delayTime)->load()
            : ClockSync::computeDelayMs(syncBpm,
                static_cast<int>(parameters.getRawParameterValue(PID::delayClockDivision)->load()));
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
        if (reverbIsAlgo)
        {
            algoReverb.setRoomSize(parameters.getRawParameterValue(PID::algoRoom)->load());
            algoReverb.setDamping(parameters.getRawParameterValue(PID::algoDamping)->load());
            algoReverb.setWidth(parameters.getRawParameterValue(PID::algoWidth)->load());
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
        }
        // Reverbs always render pure wet; the outer crossfade handles dry/wet.
        if (reverbIsAlgo) algoReverb.setMix(1.0f);
        else              reverb.setMix(1.0f);
    }

    // Reverb mix is a true crossfade: out = dry*(1-mix) + wet*mix.
    // At mix=1.0 the dry path vanishes — industry-standard insert behaviour
    // (FabFilter/Valhalla) and identical for both Algo and Plate. Plate IRs
    // are normalised and read ~6 dB quieter than the JUCE algo reverb's
    // wetLevel=1.0 output, so the convolution send is gain-compensated.
    constexpr float kPlateWetGain = 2.0f;  // +6 dB IR-normalisation comp

    auto processReverb = [&](juce::AudioBuffer<float>& buf) {
        if (reverbIsAlgo)
            algoReverb.processBlock(buf);
        else
            reverb.processBlock(buf);
    };

    auto crossfadeReverbInto = [&](juce::AudioBuffer<float>& dest, float mix)
    {
        // Algo wet rises faster than the IR plate at mid-mix because the
        // Schroeder reverb keeps wetLevel=1.0 across the whole sweep. Curve
        // the algo's wet contribution so it stays restrained until the upper
        // half of the knob; dry remains linear and mix=1 still hits pure wet.
        // Plate keeps a linear curve (its IR is the natural taper).
        const float wetAmt = reverbIsAlgo ? (mix * mix) : mix;
        const float dryAmt = 1.0f - mix;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto* rev = reverbSendBuffer.getReadPointer(ch);
            auto* out = dest.getWritePointer(ch);
            for (int i = 0; i < numSamples; ++i)
                out[i] = out[i] * dryAmt + rev[i] * wetAmt;
        }
    };

    if (delayEnabled && reverbEnabled)
    {
        // Reverb sends from the original source (pre-delay) so the reverb tail
        // doesn't echo the delay repeats; crossfade sums against post-delay dry.
        for (int ch = 0; ch < numChannels; ++ch)
            reverbSendBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        delay.processBlock(buffer);

        processReverb(reverbSendBuffer);
        if (!reverbIsAlgo)
            reverbSendBuffer.applyGain(kPlateWetGain);

        float revMix = juce::jlimit(0.0f, 1.0f,
            parameters.getRawParameterValue(PID::reverbMix)->load() + modReverbMix);
        crossfadeReverbInto(buffer, revMix);
    }
    else if (delayEnabled)
    {
        delay.processBlock(buffer);
    }
    else if (reverbEnabled)
    {
        for (int ch = 0; ch < numChannels; ++ch)
            reverbSendBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        processReverb(reverbSendBuffer);
        if (!reverbIsAlgo)
            reverbSendBuffer.applyGain(kPlateWetGain);

        float revMix = juce::jlimit(0.0f, 1.0f,
            parameters.getRawParameterValue(PID::reverbMix)->load() + modReverbMix);
        crossfadeReverbInto(buffer, revMix);
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
            bool lfoModFilter = bp.lfo1Target == LfoTarget::Filter || bp.lfo2Target == LfoTarget::Filter
                                || bp.lfo3Target == LfoTarget::Filter;
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
                if (bp.lfo3Target == LfoTarget::Filter) hypo *= std::pow(2.0f, lastLfo3Val_ * FILTER_OCTAVES);
                modulatedValues.filterCutoff.store(juce::jlimit(20.0f, 20000.0f, hypo), std::memory_order_relaxed);
            }
            else
            {
                modulatedValues.filterCutoff.store(NO_GHOST, std::memory_order_relaxed);
            }
        }

        // Scan ghost
        {
            bool lfoModScan = bp.lfo1Target == LfoTarget::Scan || bp.lfo2Target == LfoTarget::Scan
                              || bp.lfo3Target == LfoTarget::Scan;
            bool driftModScan = std::abs(bp.driftScanOffset) > 0.001f && hasVoices;
            bool wtTraversalActive = bp.engineIsWavetable && hasVoices;

            if (wtTraversalActive)
            {
                modulatedValues.scanPosition.store(voiceOut.lastModulatedScan, std::memory_order_relaxed);
            }
            else if (hasVoices && (lfoModScan || driftModScan))
            {
                modulatedValues.scanPosition.store(voiceOut.lastModulatedScan, std::memory_order_relaxed);
            }
            else if (lfoModScan)
            {
                float hypo = bp.baseScan;
                if (bp.lfo1Target == LfoTarget::Scan) hypo += lastLfo1Val_;
                if (bp.lfo2Target == LfoTarget::Scan) hypo += lastLfo2Val_;
                if (bp.lfo3Target == LfoTarget::Scan) hypo += lastLfo3Val_;
                modulatedValues.scanPosition.store(juce::jlimit(0.0f, 1.0f, hypo), std::memory_order_relaxed);
            }
            else
            {
                modulatedValues.scanPosition.store(NO_GHOST, std::memory_order_relaxed);
            }
        }

        // Noise level ghost
        {
            bool lfoModNoise = bp.lfo1Target == LfoTarget::NoiseLevel || bp.lfo2Target == LfoTarget::NoiseLevel
                               || bp.lfo3Target == LfoTarget::NoiseLevel;
            bool envModNoise = bp.mod1Target == EnvTarget::NoiseLevel || bp.mod2Target == EnvTarget::NoiseLevel;

            if (hasVoices && (lfoModNoise || envModNoise))
            {
                modulatedValues.noiseLevel.store(
                    voiceOut.lastModulatedNoiseLevel, std::memory_order_relaxed);
            }
            else if (lfoModNoise)
            {
                float hypo = bp.noiseLevel;
                if (bp.lfo1Target == LfoTarget::NoiseLevel) hypo += lastLfo1Val_;
                if (bp.lfo2Target == LfoTarget::NoiseLevel) hypo += lastLfo2Val_;
                if (bp.lfo3Target == LfoTarget::NoiseLevel) hypo += lastLfo3Val_;
                modulatedValues.noiseLevel.store(
                    juce::jlimit(0.0f, 1.0f, hypo), std::memory_order_relaxed);
            }
            else
            {
                modulatedValues.noiseLevel.store(NO_GHOST, std::memory_order_relaxed);
            }
        }

        // LFO1/2 Rate/Depth ghost (env → LFO modulation, requires active voices)
        if (!skipSynthesis)
        {
            bool lfo1RateMod  = bp.mod1Target == EnvTarget::LFO1Rate  || bp.mod2Target == EnvTarget::LFO1Rate;
            bool lfo1DepthMod = bp.mod1Target == EnvTarget::LFO1Depth || bp.mod2Target == EnvTarget::LFO1Depth;
            modulatedValues.lfo1Rate.store(lfo1RateMod ? lfo1.getRate() : NO_GHOST, std::memory_order_relaxed);
            float ghostLfo1Depth = bp.lfo1Depth;
            if (bp.mod1Target == EnvTarget::LFO1Depth) ghostLfo1Depth = applyNormalizedOffset(ghostLfo1Depth, voiceOut.lastMod1Val);
            if (bp.mod2Target == EnvTarget::LFO1Depth) ghostLfo1Depth = applyNormalizedOffset(ghostLfo1Depth, voiceOut.lastMod2Val);
            modulatedValues.lfo1Depth.store(lfo1DepthMod ? ghostLfo1Depth : NO_GHOST, std::memory_order_relaxed);

            bool lfo2RateMod  = bp.mod1Target == EnvTarget::LFO2Rate  || bp.mod2Target == EnvTarget::LFO2Rate;
            bool lfo2DepthMod = bp.mod1Target == EnvTarget::LFO2Depth || bp.mod2Target == EnvTarget::LFO2Depth;
            modulatedValues.lfo2Rate.store(lfo2RateMod ? lfo2.getRate() : NO_GHOST, std::memory_order_relaxed);
            float ghostLfo2Depth = bp.lfo2Depth;
            if (bp.mod1Target == EnvTarget::LFO2Depth) ghostLfo2Depth = applyNormalizedOffset(ghostLfo2Depth, voiceOut.lastMod1Val);
            if (bp.mod2Target == EnvTarget::LFO2Depth) ghostLfo2Depth = applyNormalizedOffset(ghostLfo2Depth, voiceOut.lastMod2Val);
            modulatedValues.lfo2Depth.store(lfo2DepthMod ? ghostLfo2Depth : NO_GHOST, std::memory_order_relaxed);

            bool lfo3RateMod  = bp.mod1Target == EnvTarget::LFO3Rate  || bp.mod2Target == EnvTarget::LFO3Rate;
            bool lfo3DepthMod = bp.mod1Target == EnvTarget::LFO3Depth || bp.mod2Target == EnvTarget::LFO3Depth;
            modulatedValues.lfo3Rate.store(lfo3RateMod ? lfo3.getRate() : NO_GHOST, std::memory_order_relaxed);
            float ghostLfo3Depth = bp.lfo3Depth;
            if (bp.mod1Target == EnvTarget::LFO3Depth) ghostLfo3Depth = applyNormalizedOffset(ghostLfo3Depth, voiceOut.lastMod1Val);
            if (bp.mod2Target == EnvTarget::LFO3Depth) ghostLfo3Depth = applyNormalizedOffset(ghostLfo3Depth, voiceOut.lastMod2Val);
            modulatedValues.lfo3Depth.store(lfo3DepthMod ? ghostLfo3Depth : NO_GHOST, std::memory_order_relaxed);
        }
        else
        {
            modulatedValues.lfo1Rate.store(NO_GHOST, std::memory_order_relaxed);
            modulatedValues.lfo1Depth.store(NO_GHOST, std::memory_order_relaxed);
            modulatedValues.lfo2Rate.store(NO_GHOST, std::memory_order_relaxed);
            modulatedValues.lfo2Depth.store(NO_GHOST, std::memory_order_relaxed);
            modulatedValues.lfo3Rate.store(NO_GHOST, std::memory_order_relaxed);
            modulatedValues.lfo3Depth.store(NO_GHOST, std::memory_order_relaxed);
        }

        // LFO → Drift depth ghosts
        {
            auto computeDriftDepthGhost = [&](const char* paramId,
                                              int target) -> float
            {
                bool lfo1Mod = bp.lfo1Target == target;
                bool lfo2Mod = bp.lfo2Target == target;
                bool lfo3Mod = bp.lfo3Target == target;
                if (!lfo1Mod && !lfo2Mod && !lfo3Mod)
                    return NO_GHOST;

                float depth = parameters.getRawParameterValue(paramId)->load();
                if (lfo1Mod)
                    depth = applyNormalizedOffset(depth, lastLfo1Val_);
                if (lfo2Mod)
                    depth = applyNormalizedOffset(depth, lastLfo2Val_);
                if (lfo3Mod)
                    depth = applyNormalizedOffset(depth, lastLfo3Val_);
                return depth;
            };

            modulatedValues.drift1Depth.store(
                computeDriftDepthGhost(PID::drift1Depth, LfoTarget::Drift1Depth),
                std::memory_order_relaxed);
            modulatedValues.drift2Depth.store(
                computeDriftDepthGhost(PID::drift2Depth, LfoTarget::Drift2Depth),
                std::memory_order_relaxed);
            modulatedValues.drift3Depth.store(
                computeDriftDepthGhost(PID::drift3Depth, LfoTarget::Drift3Depth),
                std::memory_order_relaxed);
        }

        // Delay/Reverb ghosts (modulated by env or LFO targeting them)
        {
            bool dlyTimeMod = modDelayTime != 0.0f;
            bool dlyFbMod   = modDelayFb != 0.0f;
            bool dlyMixMod  = modDelayMix != 0.0f;
            bool revMixMod  = modReverbMix != 0.0f;
            const int delayClockGhost = static_cast<int>(parameters.getRawParameterValue(PID::delayClockMode)->load());
            const float delayBaseGhost = (delayClockGhost == ClockMode::Off)
                ? parameters.getRawParameterValue(PID::delayTime)->load()
                : ClockSync::computeDelayMs(syncBpm,
                    static_cast<int>(parameters.getRawParameterValue(PID::delayClockDivision)->load()));
            modulatedValues.delayTime.store(dlyTimeMod && delayEnabled
                ? juce::jlimit(1.0f, 2000.0f, delayBaseGhost * (1.0f + modDelayTime))
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

T5ynthProcessor::WtTraversalMapping T5ynthProcessor::makeWtTraversalMapping(int totalSamples) const
{
    const float p1 = masterSampler.getStartPos();
    const float p2 = masterSampler.getLoopStart();
    const float p3 = masterSampler.getLoopEnd();

    return makeWtTraversalMapping(totalSamples, p1, p2, p3);
}

T5ynthProcessor::WtTraversalMapping T5ynthProcessor::makeWtTraversalMapping(int totalSamples,
                                                                            float p1,
                                                                            float p2,
                                                                            float p3) const
{
    WtTraversalMapping mapping;

    mapping.extractStart = juce::jlimit(0.0f, 1.0f, std::min(p1, p2));
    mapping.extractEnd = juce::jlimit(0.0f, 1.0f, std::max(p1, p3));

    const float minWidth = totalSamples > 0
        ? 4.0f / static_cast<float>(totalSamples)
        : 0.01f;
    if (mapping.extractEnd - mapping.extractStart < minWidth)
    {
        mapping.extractEnd = juce::jmin(1.0f, mapping.extractStart + minWidth);
        if (mapping.extractEnd - mapping.extractStart < minWidth)
            mapping.extractStart = juce::jmax(0.0f, mapping.extractEnd - minWidth);
    }

    const float extractWidth = juce::jmax(0.0001f, mapping.extractEnd - mapping.extractStart);
    const float invWidth = 1.0f / extractWidth;

    mapping.startInExtract = juce::jlimit(0.0f, 1.0f, (p1 - mapping.extractStart) * invWidth);
    mapping.loopStartInExtract = juce::jlimit(0.0f, 1.0f, (p2 - mapping.extractStart) * invWidth);
    mapping.loopEndInExtract = juce::jlimit(0.0f, 1.0f, (p3 - mapping.extractStart) * invWidth);
    mapping.regionSamples = juce::jmax(1,
        static_cast<int>(std::round(extractWidth * static_cast<float>(juce::jmax(1, totalSamples)))));

    return mapping;
}

void T5ynthProcessor::syncWavetableTraversal(double bufferSampleRate, int totalSamples)
{
    if (totalSamples <= 0)
        return;

    const auto mapping = makeWtTraversalMapping(totalSamples);
    masterSampler.setWtExtractStart(mapping.extractStart);
    masterSampler.setWtExtractEnd(mapping.extractEnd);

    auto loopMode = masterSampler.getLoopMode();
    WavetableOscillator::LoopMode oscLoopMode;
    switch (loopMode)
    {
        case SamplePlayer::LoopMode::OneShot:  oscLoopMode = WavetableOscillator::LoopMode::OneShot;  break;
        case SamplePlayer::LoopMode::PingPong: oscLoopMode = WavetableOscillator::LoopMode::PingPong; break;
        default:                               oscLoopMode = WavetableOscillator::LoopMode::Loop;     break;
    }

    masterOsc.setAutoScanStartPos(mapping.startInExtract);
    masterOsc.setAutoScanLoop(mapping.loopStartInExtract, mapping.loopEndInExtract, oscLoopMode);
    if (parameters.getRawParameterValue(PID::wtAutoScan)->load() > 0.5f)
    {
        masterOsc.setAutoScan(true);
        masterOsc.setAutoScanRate(bufferSampleRate, mapping.regionSamples);
    }
    else
    {
        masterOsc.setAutoScan(false);
    }
}

void T5ynthProcessor::updateDriftState(int numSamples, float syncBpm)
{
    driftLfo.setRegenMode(static_cast<int>(parameters.getRawParameterValue(PID::driftRegen)->load()));

    const int d1t = static_cast<int>(parameters.getRawParameterValue(PID::drift1Target)->load());
    const int d2t = static_cast<int>(parameters.getRawParameterValue(PID::drift2Target)->load());
    const int d3t = static_cast<int>(parameters.getRawParameterValue(PID::drift3Target)->load());

    bool driftHasTarget = (d1t != 0) || (d2t != 0) || (d3t != 0);
    bool hasOsc = false;
    for (int t : { d1t, d2t, d3t })
        if ((t >= DriftLFO::TgtAlpha && t <= DriftLFO::TgtAxis3)
            || t == DriftLFO::TgtNoise || t == DriftLFO::TgtMagnitude)
            hasOsc = true;

    driftHasOscTarget.store(hasOsc, std::memory_order_relaxed);
    driftRegenMode.store(static_cast<int>(parameters.getRawParameterValue(PID::driftRegen)->load()),
                         std::memory_order_relaxed);
    driftRegenBpm.store(syncBpm, std::memory_order_relaxed);

    bool driftManualEnable = parameters.getRawParameterValue(PID::driftEnabled)->load() > 0.5f;
    driftLfo.setEnabled(driftHasTarget || driftManualEnable);

    // Per-Drift sync-rate resolution — same pattern as LFO override.
    auto driftRate = [&](const char* clockPid, const char* divPid, const char* ratePid) {
        const int cm = static_cast<int>(parameters.getRawParameterValue(clockPid)->load());
        if (cm == ClockMode::Off)
            return parameters.getRawParameterValue(ratePid)->load();
        return ClockSync::computeRate(syncBpm,
            static_cast<int>(parameters.getRawParameterValue(divPid)->load()));
    };

    driftLfo.setLfoRate(0, driftRate(PID::drift1ClockMode, PID::drift1ClockDivision, PID::drift1Rate));
    driftLfo.setLfoDepth(0, parameters.getRawParameterValue(PID::drift1Depth)->load());
    driftLfo.setLfoTarget(0, d1t);
    driftLfo.setLfoWaveform(0, static_cast<int>(parameters.getRawParameterValue(PID::drift1Wave)->load()));
    driftLfo.setLfoRate(1, driftRate(PID::drift2ClockMode, PID::drift2ClockDivision, PID::drift2Rate));
    driftLfo.setLfoDepth(1, parameters.getRawParameterValue(PID::drift2Depth)->load());
    driftLfo.setLfoTarget(1, d2t);
    driftLfo.setLfoWaveform(1, static_cast<int>(parameters.getRawParameterValue(PID::drift2Wave)->load()));
    driftLfo.setLfoRate(2, driftRate(PID::drift3ClockMode, PID::drift3ClockDivision, PID::drift3Rate));
    driftLfo.setLfoDepth(2, parameters.getRawParameterValue(PID::drift3Depth)->load());
    driftLfo.setLfoTarget(2, d3t);
    driftLfo.setLfoWaveform(2, static_cast<int>(parameters.getRawParameterValue(PID::drift3Wave)->load()));
    driftLfo.tick(static_cast<double>(numSamples) / getSampleRate());

    static constexpr float NO_GHOST = std::numeric_limits<float>::quiet_NaN();
    const float alphaOff = driftLfo.getOffsetForTarget(DriftLFO::TgtAlpha);
    const float ax1Off   = driftLfo.getOffsetForTarget(DriftLFO::TgtAxis1);
    const float ax2Off   = driftLfo.getOffsetForTarget(DriftLFO::TgtAxis2);
    const float ax3Off   = driftLfo.getOffsetForTarget(DriftLFO::TgtAxis3);
    const float noiseOff = driftLfo.getOffsetForTarget(DriftLFO::TgtNoise);
    const float magOff   = driftLfo.getOffsetForTarget(DriftLFO::TgtMagnitude);
    const float baseAlpha = parameters.getRawParameterValue(PID::genAlpha)->load();
    const float baseNoise = parameters.getRawParameterValue(PID::genNoise)->load();
    const float baseMag = parameters.getRawParameterValue(PID::genMagnitude)->load();

    modulatedValues.driftAlpha.store(
        std::abs(alphaOff) > 0.001f ? baseAlpha + alphaOff : NO_GHOST,
        std::memory_order_relaxed);
    modulatedValues.driftAxis1.store(
        std::abs(ax1Off) > 0.001f ? ax1Off : NO_GHOST, std::memory_order_relaxed);
    modulatedValues.driftAxis2.store(
        std::abs(ax2Off) > 0.001f ? ax2Off : NO_GHOST, std::memory_order_relaxed);
    modulatedValues.driftAxis3.store(
        std::abs(ax3Off) > 0.001f ? ax3Off : NO_GHOST, std::memory_order_relaxed);
    modulatedValues.driftNoise.store(
        std::abs(noiseOff) > 0.001f ? baseNoise + noiseOff : NO_GHOST,
        std::memory_order_relaxed);
    modulatedValues.driftMagnitude.store(
        std::abs(magOff) > 0.001f ? baseMag + magOff : NO_GHOST,
        std::memory_order_relaxed);
}

bool T5ynthProcessor::seqRunningNow() const
{
    const bool stepOn = parameters.getRawParameterValue(PID::seqRunning)->load() > 0.5f;
    const bool genOn  = parameters.getRawParameterValue(PID::genSeqRunning)->load() > 0.5f;
    return stepOn || genOn;
}

float T5ynthProcessor::resolveSyncBpm() const
{
    if (hostPlayingNow.load(std::memory_order_relaxed))
        return hostBpmLastSeen.load(std::memory_order_relaxed);
    if (seqRunningNow())
        return parameters.getRawParameterValue(PID::seqBpm)->load();
    const float h = hostBpmLastSeen.load(std::memory_order_relaxed);
    return (h > 0.0f) ? h : parameters.getRawParameterValue(PID::seqBpm)->load();
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
    samplerProcessorDebugLog("loadGeneratedAudio begin samples=" + juce::String(audioBuffer.getNumSamples())
                             + " sr=" + juce::String(sr, 2)
                             + " masterBefore={" + masterSampler.debugStateString() + "}");
    // Store raw audio (unmodified) for preset embedding and re-apply on toggle
    if (&audioBuffer != &generatedAudioRaw)
        generatedAudioRaw.makeCopyOf(audioBuffer);

    // Rumble filter — always on, removes DC/sub-bass from VAE output
    juce::AudioBuffer<float> cleanBuffer;
    cleanBuffer.makeCopyOf(audioBuffer);
    applyRumbleFilter(cleanBuffer, sr);

    // Conditionally apply HF boost to compensate VAE decoder rolloff
    bool hfOn = parameters.getRawParameterValue(PID::genHfBoost)->load() > 0.5f;
    if (hfOn)
        applyHfBoost(cleanBuffer, sr);
    const auto& feedBuffer = cleanBuffer;

    SamplePlayer::LoopMode samplerLoopMode = SamplePlayer::LoopMode::Loop;
    SamplePlayer::PrepareConfig samplerConfig;
    bool autoPositionPoints = false;
    float prevP1 = 0.0f;
    {
        const juce::ScopedLock sl (getCallbackLock());
        samplerConfig = masterSampler.capturePrepareConfig();
        samplerLoopMode = samplerConfig.loopMode;
        autoPositionPoints = !masterSampler.getPointsLocked();
        prevP1 = masterSampler.getStartPos();
    }

    // ── Auto-position P1/P2/P3 BEFORE loadBuffer so that preparePlaybackBuffer
    //    (which runs normalization) already sees the correct region. ──────
    float loopStartFrac = 0.0f;
    float loopEndFrac   = 1.0f;
    float activeStartFrac = 0.0f;
    float activeEndFrac   = 1.0f;

    if (autoPositionPoints)
    {
        const int numSamples = feedBuffer.getNumSamples();

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

            activeStartFrac = static_cast<float>(firstActive) / static_cast<float>(numSamples);
            activeEndFrac   = static_cast<float>(lastActive)  / static_cast<float>(numSamples);

            if (activeEndFrac - activeStartFrac < 0.05f)
            {
                activeStartFrac = 0.0f;
                activeEndFrac   = 1.0f;
                firstActive = 0;
                lastActive = numSamples;
            }

            loopStartFrac = activeStartFrac;
            loopEndFrac   = activeEndFrac;

            // Sampler loops should prefer a stable sustain excerpt instead of
            // the full generated evolution. Looping the entire active region of
            // AI material often creates slow macro-dynamics that read like a
            // "broken normalize" even when the gain is static.
            if (samplerLoopMode != SamplePlayer::LoopMode::OneShot)
            {
                const int activeLen = lastActive - firstActive;
                const int minLoopSamples = static_cast<int>(std::round(sr * 1.5));
                const int maxLoopSamples = static_cast<int>(std::round(sr * 6.0));
                int targetLoopSamples = juce::jlimit(minLoopSamples, maxLoopSamples, activeLen / 3);

                if (activeLen > targetLoopSamples + windowSize)
                {
                    std::vector<double> powerPrefix(static_cast<size_t>(numSamples + 1), 0.0);
                    for (int i = 0; i < numSamples; ++i)
                    {
                        double s = static_cast<double>(data[i]);
                        powerPrefix[static_cast<size_t>(i + 1)]
                            = powerPrefix[static_cast<size_t>(i)] + s * s;
                    }

                    int bestStart = firstActive;
                    double bestPower = -1.0;
                    const int latestStart = lastActive - targetLoopSamples;
                    for (int pos = firstActive; pos <= latestStart; pos += windowSize)
                    {
                        const int end = pos + targetLoopSamples;
                        const double sumSq = powerPrefix[static_cast<size_t>(end)]
                                           - powerPrefix[static_cast<size_t>(pos)];
                        const double avgPower = sumSq / static_cast<double>(targetLoopSamples);
                        if (avgPower > bestPower)
                        {
                            bestPower = avgPower;
                            bestStart = pos;
                        }
                    }

                    loopStartFrac = static_cast<float>(bestStart) / static_cast<float>(numSamples);
                    loopEndFrac   = static_cast<float>(bestStart + targetLoopSamples)
                                  / static_cast<float>(numSamples);
                }
            }
        }

    }

    if (autoPositionPoints)
    {
        samplerConfig.loopStartFrac = loopStartFrac;
        samplerConfig.loopEndFrac = loopEndFrac;

        // P1: preserve the user's choice where possible, but clamp against
        // the active audio region rather than the loop window.
        if (prevP1 < activeStartFrac || prevP1 > activeEndFrac)
            samplerConfig.startPosFrac = activeStartFrac;
    }

    const bool wavetableMode = isWavetableMode();
    const auto wtMapping = makeWtTraversalMapping(feedBuffer.getNumSamples(),
                                                  samplerConfig.startPosFrac,
                                                  samplerConfig.loopStartFrac,
                                                  samplerConfig.loopEndFrac);
    const float extractStart = wavetableMode ? wtMapping.extractStart
                                             : samplerConfig.loopStartFrac;
    const float extractEnd   = wavetableMode ? wtMapping.extractEnd
                                             : samplerConfig.loopEndFrac;

    constexpr int frameCounts[] = {32, 64, 128, 256};
    int fcIdx = static_cast<int>(parameters.getRawParameterValue(PID::wtFrames)->load());
    int maxFrames = frameCounts[juce::jlimit(0, 3, fcIdx)];

    auto preparedSamplerLoad = masterSampler.prepareBufferLoad(feedBuffer, sr, samplerConfig);
    juce::AudioBuffer<float> preparedGeneratedAudio;
    preparedGeneratedAudio.makeCopyOf(feedBuffer);
    juce::AudioBuffer<float> preparedWaveformSnapshot;
    const bool normDisplay = wavetableMode
        || (parameters.getRawParameterValue(PID::normalize)->load() > 0.5f);
    if (feedBuffer.getNumChannels() > 0 && feedBuffer.getNumSamples() > 0)
    {
        preparedWaveformSnapshot.setSize(1, feedBuffer.getNumSamples(), false, false, true);
        preparedWaveformSnapshot.copyFrom(0, 0, feedBuffer, 0, 0, feedBuffer.getNumSamples());

        if (normDisplay)
        {
            float peak = 0.0f;
            const float* d = preparedWaveformSnapshot.getReadPointer(0);
            for (int i = 0; i < preparedWaveformSnapshot.getNumSamples(); ++i)
                peak = std::max(peak, std::abs(d[i]));
            if (peak > 0.001f)
                preparedWaveformSnapshot.applyGain(0.95f / peak);
        }
    }

    if (wavetableMode)
        masterOsc.extractFramesFromBuffer(feedBuffer, sr, extractStart, extractEnd, maxFrames);
    else
        masterOsc.extractContiguousFrames(feedBuffer, sr, extractStart, extractEnd);

    {
        // Guard engine-state mutation against the realtime callback. The Linux
        // standalone path takes this same lock around processBlock().
        const juce::ScopedLock sl (getCallbackLock());

        generatedSampleRate = sr;

        // Keep generatedAudioFull in sync (used for waveform display + presets)
        generatedAudioFull = std::move(preparedGeneratedAudio);

        // Keep currently sounding sampler notes on their old snapshot so a
        // regenerate only affects newly triggered notes.
        voiceManager.freezeActiveSamplerVoices();

        // Publish the already-prepared sampler state inside the lock so the
        // audio thread only sees a short atomic handoff.
        masterSampler.applyPreparedBufferLoad(std::move(preparedSamplerLoad), samplerConfig);

        syncWavetableTraversal(sr, feedBuffer.getNumSamples());
        masterOsc.setMorphTimeMs(parameters.getRawParameterValue(PID::driftCrossfade)->load());

        voiceManager.distributeSamplerBuffer(masterSampler);
        voiceManager.distributeWavetableFrames(masterOsc);

        samplerProcessorDebugLog("loadGeneratedAudio end masterAfter={" + masterSampler.debugStateString() + "}");

        // Snapshot channel 0 for waveform display
        if (preparedWaveformSnapshot.getNumSamples() > 0)
        {
            waveformSnapshot = std::move(preparedWaveformSnapshot);
            newWaveformReady.store(true, std::memory_order_release);
        }
    }
}

void T5ynthProcessor::reloadProcessedAudio(const juce::AudioBuffer<float>& processed)
{
    samplerProcessorDebugLog("reloadProcessedAudio begin samples=" + juce::String(processed.getNumSamples())
                             + " masterBefore={" + masterSampler.debugStateString() + "}");
    SamplePlayer::PrepareConfig samplerConfig;
    bool wavetableMode = false;
    {
        const juce::ScopedLock sl (getCallbackLock());
        samplerConfig = masterSampler.capturePrepareConfig();
        wavetableMode = isWavetableMode();
    }
    auto preparedSamplerLoad = masterSampler.prepareBufferLoad(processed, generatedSampleRate, samplerConfig);
    juce::AudioBuffer<float> preparedGeneratedAudio;
    preparedGeneratedAudio.makeCopyOf(processed);
    juce::AudioBuffer<float> preparedWaveformSnapshot;
    if (processed.getNumChannels() > 0 && processed.getNumSamples() > 0)
    {
        preparedWaveformSnapshot.setSize(1, processed.getNumSamples(), false, false, true);
        preparedWaveformSnapshot.copyFrom(0, 0, processed, 0, 0, processed.getNumSamples());
    }

    if (preparedWaveformSnapshot.getNumSamples() > 0 && masterOsc.hasFrames())
    {
        const auto wtMapping = makeWtTraversalMapping(preparedWaveformSnapshot.getNumSamples(),
                                                      samplerConfig.startPosFrac,
                                                      samplerConfig.loopStartFrac,
                                                      samplerConfig.loopEndFrac);
        float start = wavetableMode ? wtMapping.extractStart : samplerConfig.loopStartFrac;
        float end   = wavetableMode ? wtMapping.extractEnd   : samplerConfig.loopEndFrac;

        constexpr int frameCounts[] = {32, 64, 128, 256};
        int fcIdx = static_cast<int>(parameters.getRawParameterValue(PID::wtFrames)->load());
        int maxFrames = frameCounts[juce::jlimit(0, 3, fcIdx)];

        if (wavetableMode)
            masterOsc.extractFramesFromBuffer(preparedWaveformSnapshot, generatedSampleRate, start, end, maxFrames);
        else
            masterOsc.extractContiguousFrames(preparedWaveformSnapshot, generatedSampleRate, start, end);
    }
    {
        const juce::ScopedLock sl (getCallbackLock());

        // Update stored audio and reload into sampler without Rumble/HF/Normalize
        generatedAudioFull = std::move(preparedGeneratedAudio);
        voiceManager.freezeActiveSamplerVoices();
        masterSampler.applyPreparedBufferLoad(std::move(preparedSamplerLoad), samplerConfig);
        if (preparedWaveformSnapshot.getNumSamples() > 0)
            waveformSnapshot = std::move(preparedWaveformSnapshot);
        voiceManager.distributeSamplerBuffer(masterSampler);
        if (masterOsc.hasFrames())
        {
            syncWavetableTraversal(generatedSampleRate, waveformSnapshot.getNumSamples());
            masterOsc.setMorphTimeMs(parameters.getRawParameterValue(PID::driftCrossfade)->load());
            voiceManager.distributeWavetableFrames(masterOsc);
        }

        samplerProcessorDebugLog("reloadProcessedAudio end masterAfter={" + masterSampler.debugStateString() + "}");

        if (waveformSnapshot.getNumSamples() > 0)
        {
            newWaveformReady.store(true, std::memory_order_release);
        }
    }
}

void T5ynthProcessor::reextractWavetable()
{
    const juce::ScopedLock sl (getCallbackLock());

    if (waveformSnapshot.getNumSamples() > 0)
    {
        const auto wtMapping = makeWtTraversalMapping(waveformSnapshot.getNumSamples());
        float start = isWavetableMode() ? wtMapping.extractStart
                                        : masterSampler.getLoopStart();
        float end   = isWavetableMode() ? wtMapping.extractEnd
                                        : masterSampler.getLoopEnd();

        constexpr int frameCounts[] = {32, 64, 128, 256};
        int fcIdx = static_cast<int>(parameters.getRawParameterValue(PID::wtFrames)->load());
        int maxFrames = frameCounts[juce::jlimit(0, 3, fcIdx)];

        if (isWavetableMode())
            masterOsc.extractFramesFromBuffer(waveformSnapshot, generatedSampleRate, start, end, maxFrames);
        else
            masterOsc.extractContiguousFrames(waveformSnapshot, generatedSampleRate, start, end);

        syncWavetableTraversal(generatedSampleRate, waveformSnapshot.getNumSamples());
        masterOsc.setMorphTimeMs(parameters.getRawParameterValue(PID::driftCrossfade)->load());
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
    {
        auto loadedTree = juce::ValueTree::fromXml(*xml);

        // BPM-sync clock params (v1.7.0-beta.1). APVTS::replaceState leaves
        // missing-from-tree params untouched, so a session saved before
        // these existed would inherit whatever clock state was last
        // touched in the host — making D1/L1/etc. mysteriously stick on
        // Sync. Patch defaults straight into the loaded tree so the swap
        // is atomic (no setValueNotifyingHost glitch between a pre-reset
        // and the actual restore).
        struct ClockDefault { const char* pid; int defaultIndex; };
        const ClockDefault clockDefaults[] = {
            { PID::lfo1ClockMode,      ClockMode::Off          },
            { PID::lfo1ClockDivision,  ClockDivision::D1_4     },
            { PID::lfo2ClockMode,      ClockMode::Off          },
            { PID::lfo2ClockDivision,  ClockDivision::D1_4     },
            { PID::lfo3ClockMode,      ClockMode::Off          },
            { PID::lfo3ClockDivision,  ClockDivision::D1_4     },
            { PID::drift1ClockMode,    ClockMode::Off          },
            { PID::drift1ClockDivision,ClockDivision::D1_4     },
            { PID::drift2ClockMode,    ClockMode::Off          },
            { PID::drift2ClockDivision,ClockDivision::D1_4     },
            { PID::drift3ClockMode,    ClockMode::Off          },
            { PID::drift3ClockDivision,ClockDivision::D1_4     },
            { PID::delayClockMode,     ClockMode::Off          },
            { PID::delayClockDivision, ClockDivision::D1_4     },
        };
        auto hasParam = [&](const juce::String& pid) {
            for (int i = 0; i < loadedTree.getNumChildren(); ++i)
                if (loadedTree.getChild(i).getProperty("id").toString() == pid)
                    return true;
            return false;
        };
        for (const auto& cd : clockDefaults)
        {
            if (hasParam(cd.pid)) continue;
            juce::ValueTree node("PARAM");
            node.setProperty("id", cd.pid, nullptr);
            node.setProperty("value", static_cast<float>(cd.defaultIndex), nullptr);
            loadedTree.appendChild(node, nullptr);
        }

        parameters.replaceState(loadedTree);
    }

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

static int filterDriveOsFromString(const juce::String& s) { return choiceFromKey(s, FilterDriveOs::kEntries); }
static juce::String filterDriveOsToString(int i)          { return choiceToKey(i, FilterDriveOs::kEntries); }

static int filterAlgorithmFromString(const juce::String& s) { return choiceFromKey(s, FilterAlgorithm::kEntries); }
static juce::String filterAlgorithmToString(int i)          { return choiceToKey(i, FilterAlgorithm::kEntries); }

static int filterWarpStyleFromString(const juce::String& s) { return choiceFromKey(s, FilterWarpStyle::kEntries); }
static juce::String filterWarpStyleToString(int i)          { return choiceToKey(i, FilterWarpStyle::kEntries); }

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

static int clockModeFromString(const juce::String& s)     { return choiceFromKey(s, ClockMode::kEntries); }
static juce::String clockModeToString(int i)              { return choiceToKey(i, ClockMode::kEntries); }

static int clockDivisionFromString(const juce::String& s) { return choiceFromKey(s, ClockDivision::kEntries); }
static juce::String clockDivisionToString(int i)          { return choiceToKey(i, ClockDivision::kEntries); }

static int curveShapeFromString(const juce::String& s)  { return choiceFromKey(s, EnvCurve::kEntries); }
static juce::String curveShapeToString(int i)           { return choiceToKey(i, EnvCurve::kEntries); }
static int envVelTimeModeFromString(const juce::String& s) { return choiceFromKey(s, EnvVelTimeMode::kEntries); }
static juce::String envVelTimeModeToString(int i)          { return choiceToKey(i, EnvVelTimeMode::kEntries); }

// ── PID group tables for looped save/load of envelopes, LFOs, drift ──
struct EnvPIDs {
    const char* attack; const char* decay; const char* sustain; const char* release;
    const char* amount; const char* velSens; const char* loop; const char* target;
    const char* attackCurve; const char* decayCurve; const char* releaseCurve;
    const char* attackVelMode; const char* decayVelMode; const char* releaseVelMode;
};
static constexpr EnvPIDs kEnvPIDs[] = {
    { PID::ampAttack, PID::ampDecay, PID::ampSustain, PID::ampRelease,
      PID::ampAmount, PID::ampVelSens, PID::ampLoop, nullptr,
      PID::ampAttackCurve, PID::ampDecayCurve, PID::ampReleaseCurve,
      PID::ampAttackVelMode, PID::ampDecayVelMode, PID::ampReleaseVelMode },
    { PID::mod1Attack, PID::mod1Decay, PID::mod1Sustain, PID::mod1Release,
      PID::mod1Amount, PID::mod1VelSens, PID::mod1Loop, PID::mod1Target,
      PID::mod1AttackCurve, PID::mod1DecayCurve, PID::mod1ReleaseCurve,
      PID::mod1AttackVelMode, PID::mod1DecayVelMode, PID::mod1ReleaseVelMode },
    { PID::mod2Attack, PID::mod2Decay, PID::mod2Sustain, PID::mod2Release,
      PID::mod2Amount, PID::mod2VelSens, PID::mod2Loop, PID::mod2Target,
      PID::mod2AttackCurve, PID::mod2DecayCurve, PID::mod2ReleaseCurve,
      PID::mod2AttackVelMode, PID::mod2DecayVelMode, PID::mod2ReleaseVelMode },
};

struct LfoPIDs {
    const char* rate; const char* depth; const char* wave;
    const char* target; const char* mode;
    const char* clockMode; const char* clockDivision;
};
static constexpr LfoPIDs kLfoPIDs[] = {
    { PID::lfo1Rate, PID::lfo1Depth, PID::lfo1Wave, PID::lfo1Target, PID::lfo1Mode,
      PID::lfo1ClockMode, PID::lfo1ClockDivision },
    { PID::lfo2Rate, PID::lfo2Depth, PID::lfo2Wave, PID::lfo2Target, PID::lfo2Mode,
      PID::lfo2ClockMode, PID::lfo2ClockDivision },
    { PID::lfo3Rate, PID::lfo3Depth, PID::lfo3Wave, PID::lfo3Target, PID::lfo3Mode,
      PID::lfo3ClockMode, PID::lfo3ClockDivision },
};

struct DriftPIDs {
    const char* rate; const char* depth; const char* target; const char* wave;
    const char* clockMode; const char* clockDivision;
};
static constexpr DriftPIDs kDriftPIDs[] = {
    { PID::drift1Rate, PID::drift1Depth, PID::drift1Target, PID::drift1Wave,
      PID::drift1ClockMode, PID::drift1ClockDivision },
    { PID::drift2Rate, PID::drift2Depth, PID::drift2Target, PID::drift2Wave,
      PID::drift2ClockMode, PID::drift2ClockDivision },
    { PID::drift3Rate, PID::drift3Depth, PID::drift3Target, PID::drift3Wave,
      PID::drift3ClockMode, PID::drift3ClockDivision },
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
        env->setProperty("attackVelMode", envVelTimeModeToString(static_cast<int>(get(ep.attackVelMode))));
        env->setProperty("decayVelMode", envVelTimeModeToString(static_cast<int>(get(ep.decayVelMode))));
        env->setProperty("releaseVelMode", envVelTimeModeToString(static_cast<int>(get(ep.releaseVelMode))));
        envArr.add(env.get());
    }
    modObj->setProperty("envs", envArr);

    // Modulation: 3 LFOs
    juce::Array<juce::var> lfoArr;
    for (int i = 0; i < 3; ++i)
    {
        const auto& lp = kLfoPIDs[i];
        juce::DynamicObject::Ptr lfo = new juce::DynamicObject();
        lfo->setProperty("rate", get(lp.rate));
        lfo->setProperty("depth", get(lp.depth));
        lfo->setProperty("waveform", lfoWaveToString(static_cast<int>(get(lp.wave))));
        lfo->setProperty("target", lfoTargetToString(static_cast<int>(get(lp.target))));
        lfo->setProperty("mode", lfoModeToString(static_cast<int>(get(lp.mode))));
        lfo->setProperty("clockMode", clockModeToString(static_cast<int>(get(lp.clockMode))));
        lfo->setProperty("clockDivision", clockDivisionToString(static_cast<int>(get(lp.clockDivision))));
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
        d->setProperty("clockMode", clockModeToString(static_cast<int>(get(dp.clockMode))));
        d->setProperty("clockDivision", clockDivisionToString(static_cast<int>(get(dp.clockDivision))));
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
    wt->setProperty("autoScan", get(PID::wtAutoScan) > 0.5f);
    root->setProperty("wavetable", wt.get());

    // Effects
    juce::DynamicObject::Ptr fx = new juce::DynamicObject();
    fx->setProperty("delayType", choiceToKey(static_cast<int>(get(PID::delayType)), DelayType::kEntries));
    fx->setProperty("delayTimeMs", get(PID::delayTime));
    fx->setProperty("delayFeedback", get(PID::delayFeedback));
    fx->setProperty("delayMix", get(PID::delayMix));
    fx->setProperty("delayDamp", get(PID::delayDamp));
    fx->setProperty("delayClockMode",
                    clockModeToString(static_cast<int>(get(PID::delayClockMode))));
    fx->setProperty("delayClockDivision",
                    clockDivisionToString(static_cast<int>(get(PID::delayClockDivision))));
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
    filt->setProperty("drive", get(PID::filterDrive));
    filt->setProperty("driveOs", filterDriveOsToString(static_cast<int>(get(PID::filterDriveOs))));
    filt->setProperty("algorithm", filterAlgorithmToString(static_cast<int>(get(PID::filterAlgorithm))));
    filt->setProperty("warpStyle", filterWarpStyleToString(static_cast<int>(get(PID::filterWarpStyle))));
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
    seq->setProperty("shuffle", get(PID::seqShuffle));
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

    // Shared pitch field
    juce::DynamicObject::Ptr field = new juce::DynamicObject();
    field->setProperty("mode",     choiceToKey(static_cast<int>(get(PID::genFieldMode)),  FieldMode::kEntries));
    field->setProperty("rate",     static_cast<int>(get(PID::genFieldRate)));
    field->setProperty("centerPc", static_cast<int>(get(PID::genFieldCenterPc)));
    field->setProperty("pivot",    choiceToKey(static_cast<int>(get(PID::genFieldPivot)), FieldPivot::kEntries));
    genSeq->setProperty("pitchField", field.get());

    // Strand 0 extras (Euclidean params already serialised above under top-level keys)
    juce::DynamicObject::Ptr strand0 = new juce::DynamicObject();
    strand0->setProperty("role",      choiceToKey(static_cast<int>(get(PID::genRole)),    StrandRole::kEntries));
    strand0->setProperty("octave",    static_cast<int>(get(PID::genOctave)));
    strand0->setProperty("divMult",   choiceToKey(static_cast<int>(get(PID::genDivMult)), StrandDivMult::kEntries));
    strand0->setProperty("dominance", get(PID::genDominance));
    genSeq->setProperty("strand0", strand0.get());

    // Strands 2..5 full state
    struct StrandIds {
        const char* enable;  const char* role;    const char* octave;  const char* divMult;
        const char* dominance; const char* steps; const char* pulses;  const char* rotation;
        const char* mutation; const char* fS;     const char* fP;      const char* fR;      const char* fM;
    };
    static const StrandIds kExtras[4] = {
        { PID::gen2Enable, PID::gen2Role, PID::gen2Octave, PID::gen2DivMult,
          PID::gen2Dominance, PID::gen2Steps, PID::gen2Pulses, PID::gen2Rotation,
          PID::gen2Mutation, PID::gen2FixSteps, PID::gen2FixPulses, PID::gen2FixRotation, PID::gen2FixMutation },
        { PID::gen3Enable, PID::gen3Role, PID::gen3Octave, PID::gen3DivMult,
          PID::gen3Dominance, PID::gen3Steps, PID::gen3Pulses, PID::gen3Rotation,
          PID::gen3Mutation, PID::gen3FixSteps, PID::gen3FixPulses, PID::gen3FixRotation, PID::gen3FixMutation },
        { PID::gen4Enable, PID::gen4Role, PID::gen4Octave, PID::gen4DivMult,
          PID::gen4Dominance, PID::gen4Steps, PID::gen4Pulses, PID::gen4Rotation,
          PID::gen4Mutation, PID::gen4FixSteps, PID::gen4FixPulses, PID::gen4FixRotation, PID::gen4FixMutation },
        { PID::gen5Enable, PID::gen5Role, PID::gen5Octave, PID::gen5DivMult,
          PID::gen5Dominance, PID::gen5Steps, PID::gen5Pulses, PID::gen5Rotation,
          PID::gen5Mutation, PID::gen5FixSteps, PID::gen5FixPulses, PID::gen5FixRotation, PID::gen5FixMutation }
    };
    static const char* kExtraKeys[4] = { "strand2", "strand3", "strand4", "strand5" };
    for (int i = 0; i < 4; ++i)
    {
        const auto& ids = kExtras[i];
        juce::DynamicObject::Ptr sn = new juce::DynamicObject();
        sn->setProperty("enabled",     get(ids.enable) > 0.5f);
        sn->setProperty("role",        choiceToKey(static_cast<int>(get(ids.role)),    StrandRole::kEntries));
        sn->setProperty("octave",      static_cast<int>(get(ids.octave)));
        sn->setProperty("divMult",     choiceToKey(static_cast<int>(get(ids.divMult)), StrandDivMult::kEntries));
        sn->setProperty("dominance",   get(ids.dominance));
        sn->setProperty("steps",       static_cast<int>(get(ids.steps)));
        sn->setProperty("pulses",      static_cast<int>(get(ids.pulses)));
        sn->setProperty("rotation",    static_cast<int>(get(ids.rotation)));
        sn->setProperty("mutation",    get(ids.mutation));
        sn->setProperty("fixSteps",    get(ids.fS) > 0.5f);
        sn->setProperty("fixPulses",   get(ids.fP) > 0.5f);
        sn->setProperty("fixRotation", get(ids.fR) > 0.5f);
        sn->setProperty("fixMutation", get(ids.fM) > 0.5f);
        genSeq->setProperty(kExtraKeys[i], sn.get());
    }

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
        {
            const juce::ScopedLock sl (getCallbackLock());
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
        }
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
                if (env->hasProperty("attackVelMode"))
                    setParam(parameters, ep.attackVelMode,
                             static_cast<float>(envVelTimeModeFromString(env->getProperty("attackVelMode").toString())));
                if (env->hasProperty("decayVelMode"))
                    setParam(parameters, ep.decayVelMode,
                             static_cast<float>(envVelTimeModeFromString(env->getProperty("decayVelMode").toString())));
                if (env->hasProperty("releaseVelMode"))
                    setParam(parameters, ep.releaseVelMode,
                             static_cast<float>(envVelTimeModeFromString(env->getProperty("releaseVelMode").toString())));
                if (ep.target != nullptr)
                    setParam(parameters, ep.target, static_cast<float>(envTargetFromString(env->getProperty("target").toString())));
            }
        }

        auto* lfosArr = mod->getProperty("lfos").getArray();
        if (lfosArr)
        {
            for (int i = 0; i < std::min(3, lfosArr->size()); ++i)
            {
                auto* lfo = (*lfosArr)[i].getDynamicObject();
                if (!lfo) continue;
                const auto& lp = kLfoPIDs[i];
                setParam(parameters, lp.rate, static_cast<float>(lfo->getProperty("rate")));
                setParam(parameters, lp.depth, static_cast<float>(lfo->getProperty("depth")));
                setParam(parameters, lp.wave, static_cast<float>(lfoWaveFromString(lfo->getProperty("waveform").toString())));
                setParam(parameters, lp.target, static_cast<float>(lfoTargetFromString(lfo->getProperty("target").toString())));
                setParam(parameters, lp.mode, static_cast<float>(lfoModeFromString(lfo->getProperty("mode").toString())));
                // Pre-v1.7 presets have no clock fields — default to Off / 1/4
                // explicitly so the previous session's clock state cannot stick.
                setParam(parameters, lp.clockMode, lfo->hasProperty("clockMode")
                    ? static_cast<float>(clockModeFromString(lfo->getProperty("clockMode").toString()))
                    : static_cast<float>(ClockMode::Off));
                setParam(parameters, lp.clockDivision, lfo->hasProperty("clockDivision")
                    ? static_cast<float>(clockDivisionFromString(lfo->getProperty("clockDivision").toString()))
                    : static_cast<float>(ClockDivision::D1_4));
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
            setParam(parameters, dp.clockMode, d->hasProperty("clockMode")
                ? static_cast<float>(clockModeFromString(d->getProperty("clockMode").toString()))
                : static_cast<float>(ClockMode::Off));
            setParam(parameters, dp.clockDivision, d->hasProperty("clockDivision")
                ? static_cast<float>(clockDivisionFromString(d->getProperty("clockDivision").toString()))
                : static_cast<float>(ClockDivision::D1_4));
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
        bool autoScan = wt->hasProperty("autoScan") ? static_cast<bool>(wt->getProperty("autoScan")) : true;
        setParam(parameters, PID::wtAutoScan, autoScan ? 1.0f : 0.0f);
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
        setParam(parameters, PID::delayClockMode, fx->hasProperty("delayClockMode")
            ? static_cast<float>(clockModeFromString(fx->getProperty("delayClockMode").toString()))
            : static_cast<float>(ClockMode::Off));
        setParam(parameters, PID::delayClockDivision, fx->hasProperty("delayClockDivision")
            ? static_cast<float>(clockDivisionFromString(fx->getProperty("delayClockDivision").toString()))
            : static_cast<float>(ClockDivision::D1_4));
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
        // Drive: absent in older presets -> treat as 0 dB.
        setParam(parameters, PID::filterDrive,
                 filt->hasProperty("drive") ? static_cast<float>(filt->getProperty("drive")) : 0.0f);
        // Drive OS: absent in older presets -> Off (bit-identical to pre-OS build).
        setParam(parameters, PID::filterDriveOs,
                 filt->hasProperty("driveOs")
                     ? static_cast<float>(filterDriveOsFromString(filt->getProperty("driveOs").toString()))
                     : static_cast<float>(FilterDriveOs::Off));
        // Filter algorithm: absent in pre-Ladder/Warp presets -> SVF (bit-identical).
        setParam(parameters, PID::filterAlgorithm,
                 filt->hasProperty("algorithm")
                     ? static_cast<float>(filterAlgorithmFromString(filt->getProperty("algorithm").toString()))
                     : static_cast<float>(FilterAlgorithm::SVF));
        setParam(parameters, PID::filterWarpStyle,
                 filt->hasProperty("warpStyle")
                     ? static_cast<float>(filterWarpStyleFromString(filt->getProperty("warpStyle").toString()))
                     : static_cast<float>(FilterWarpStyle::Tanh));
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
        setParam(parameters, PID::seqShuffle,
                 seq->hasProperty("shuffle")
                     ? static_cast<float>(seq->getProperty("shuffle"))
                     : 0.0f);
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

        // Shared pitch field (optional — absent in pre-polyphonic presets)
        if (auto* pf = gs->getProperty("pitchField").getDynamicObject())
        {
            setParam(parameters, PID::genFieldMode,
                     static_cast<float>(choiceFromKey(pf->getProperty("mode").toString(), FieldMode::kEntries)));
            setParam(parameters, PID::genFieldRate, static_cast<float>(static_cast<int>(pf->getProperty("rate"))));
            setParam(parameters, PID::genFieldCenterPc, static_cast<float>(static_cast<int>(pf->getProperty("centerPc"))));
            setParam(parameters, PID::genFieldPivot,
                     static_cast<float>(choiceFromKey(pf->getProperty("pivot").toString(), FieldPivot::kEntries)));
        }

        // Strand 0 extras (optional)
        if (auto* s0 = gs->getProperty("strand0").getDynamicObject())
        {
            setParam(parameters, PID::genRole,
                     static_cast<float>(choiceFromKey(s0->getProperty("role").toString(), StrandRole::kEntries)));
            setParam(parameters, PID::genOctave, static_cast<float>(static_cast<int>(s0->getProperty("octave"))));
            setParam(parameters, PID::genDivMult,
                     static_cast<float>(choiceFromKey(s0->getProperty("divMult").toString(), StrandDivMult::kEntries)));
            setParam(parameters, PID::genDominance, static_cast<float>(s0->getProperty("dominance")));
        }

        // Strands 2..5 (optional — pre-polyphonic presets and 4-strand
        // presets simply skip the missing entries; the affected strand stays
        // at its APVTS default).
        struct StrandImportIds {
            const char* enable;  const char* role;    const char* octave;  const char* divMult;
            const char* dominance; const char* steps; const char* pulses;  const char* rotation;
            const char* mutation; const char* fS;     const char* fP;      const char* fR;      const char* fM;
        };
        static const StrandImportIds kExtrasImport[4] = {
            { PID::gen2Enable, PID::gen2Role, PID::gen2Octave, PID::gen2DivMult,
              PID::gen2Dominance, PID::gen2Steps, PID::gen2Pulses, PID::gen2Rotation,
              PID::gen2Mutation, PID::gen2FixSteps, PID::gen2FixPulses, PID::gen2FixRotation, PID::gen2FixMutation },
            { PID::gen3Enable, PID::gen3Role, PID::gen3Octave, PID::gen3DivMult,
              PID::gen3Dominance, PID::gen3Steps, PID::gen3Pulses, PID::gen3Rotation,
              PID::gen3Mutation, PID::gen3FixSteps, PID::gen3FixPulses, PID::gen3FixRotation, PID::gen3FixMutation },
            { PID::gen4Enable, PID::gen4Role, PID::gen4Octave, PID::gen4DivMult,
              PID::gen4Dominance, PID::gen4Steps, PID::gen4Pulses, PID::gen4Rotation,
              PID::gen4Mutation, PID::gen4FixSteps, PID::gen4FixPulses, PID::gen4FixRotation, PID::gen4FixMutation },
            { PID::gen5Enable, PID::gen5Role, PID::gen5Octave, PID::gen5DivMult,
              PID::gen5Dominance, PID::gen5Steps, PID::gen5Pulses, PID::gen5Rotation,
              PID::gen5Mutation, PID::gen5FixSteps, PID::gen5FixPulses, PID::gen5FixRotation, PID::gen5FixMutation }
        };
        static const char* kExtraKeysImport[4] = { "strand2", "strand3", "strand4", "strand5" };
        for (int i = 0; i < 4; ++i)
        {
            auto* sn = gs->getProperty(kExtraKeysImport[i]).getDynamicObject();
            if (!sn) continue;
            const auto& ids = kExtrasImport[i];
            setParam(parameters, ids.enable,    static_cast<bool>(sn->getProperty("enabled")) ? 1.0f : 0.0f);
            setParam(parameters, ids.role,      static_cast<float>(choiceFromKey(sn->getProperty("role").toString(), StrandRole::kEntries)));
            setParam(parameters, ids.octave,    static_cast<float>(static_cast<int>(sn->getProperty("octave"))));
            setParam(parameters, ids.divMult,   static_cast<float>(choiceFromKey(sn->getProperty("divMult").toString(), StrandDivMult::kEntries)));
            setParam(parameters, ids.dominance, static_cast<float>(sn->getProperty("dominance")));
            setParam(parameters, ids.steps,     static_cast<float>(static_cast<int>(sn->getProperty("steps"))));
            setParam(parameters, ids.pulses,    static_cast<float>(static_cast<int>(sn->getProperty("pulses"))));
            setParam(parameters, ids.rotation,  static_cast<float>(static_cast<int>(sn->getProperty("rotation"))));
            setParam(parameters, ids.mutation,  static_cast<float>(sn->getProperty("mutation")));
            setParam(parameters, ids.fS,        static_cast<bool>(sn->getProperty("fixSteps")) ? 1.0f : 0.0f);
            setParam(parameters, ids.fP,        static_cast<bool>(sn->getProperty("fixPulses")) ? 1.0f : 0.0f);
            setParam(parameters, ids.fR,        static_cast<bool>(sn->getProperty("fixRotation")) ? 1.0f : 0.0f);
            setParam(parameters, ids.fM,        static_cast<bool>(sn->getProperty("fixMutation")) ? 1.0f : 0.0f);
        }
    }

    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new T5ynthProcessor();
}
