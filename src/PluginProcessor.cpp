#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"
#include "dsp/Tuning.h"
#include "midi/LaunchControlXLLeds.h"
#include <chrono>

#define SAMPLER_PROCESSOR_DEBUG_LOG 0

namespace
{
constexpr float kAlphaAnchorSnapThreshold = 0.04f;
// Centre/linear detent. Deliberately smaller than the ±1 anchor threshold: the
// alpha range's quadratic skew is flattest at 0, so a given value-window maps to
// a much wider pixel-window there. 0.02 gives a clearly findable "linear" detent
// without eating the fine-control band the skew exists to provide.
constexpr float kAlphaLinearSnapThreshold = 0.02f;
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
    value = snapIfNear(value,  0.0f, kAlphaLinearSnapThreshold);  // linear (50/50) centre
    value = snapIfNear(value, -1.0f, kAlphaAnchorSnapThreshold);  // A1 anchor
    value = snapIfNear(value,  1.0f, kAlphaAnchorSnapThreshold);  // B1 anchor
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
    // Whole-second detents across the whole range. For the default 11s slider
    // this is the historical 1..11; for the SA3 120s slider the detents simply
    // extend so round music lengths (30s, 60s, 90s) snap cleanly too. ceil()
    // keeps the 11.0 endpoint behaving exactly as before.
    const int maxSecond = static_cast<int>(std::ceil(rangeEnd));
    for (int seconds = 1; seconds <= maxSecond; ++seconds)
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

#if SAMPLER_PROCESSOR_DEBUG_LOG
void samplerProcessorDebugLog(const juce::String& message)
{
    juce::Logger::writeToLog("[SamplerDebug] " + message);
    juce::FileOutputStream out(juce::File("/tmp/t5ynth_sampler_debug.log"));
    if (out.openedOk())
    {
        out << "[SamplerDebug] " << message << juce::newLine;
        out.flush();
    }
}
#else
// When debug logging is disabled, replace the call with a no-op so the
// expensive string-concat arguments are never evaluated (audio thread safety).
#define samplerProcessorDebugLog(...) ((void)0)
#endif

bool samePrepareConfig(const SamplePlayer::PrepareConfig& a,
                       const SamplePlayer::PrepareConfig& b)
{
    auto sameFloat = [] (float x, float y)
    {
        return std::abs(x - y) <= 1.0e-6f;
    };

    return a.loopMode == b.loopMode
        && sameFloat(a.crossfadeMs, b.crossfadeMs)
        && a.normalizeOn == b.normalizeOn
        && a.loopOptimizeLevel == b.loopOptimizeLevel
        && sameFloat(a.startPosFrac, b.startPosFrac)
        && sameFloat(a.loopStartFrac, b.loopStartFrac)
        && sameFloat(a.loopEndFrac, b.loopEndFrac);
}

juce::AudioBuffer<float> makeFreezeLoadBuffer(const juce::AudioBuffer<float>& source,
                                              double sourceRate,
                                              bool normalizeOn,
                                              float normalizeStartFrac,
                                              float normalizeEndFrac,
                                              const SamplePlayer& normalizer)
{
    juce::AudioBuffer<float> buffer;
    const int numSamples = source.getNumSamples();
    const int numChannels = source.getNumChannels();
    if (numSamples > 0 && numChannels > 0)
    {
        // Freeze renders from a mono snapshot, so normalize the exact folded
        // signal it will play instead of normalizing stereo and losing level
        // later through (L+R)/channels.
        buffer.setSize(1, numSamples, false, false, true);
        buffer.clear();

        const float invChannels = 1.0f / static_cast<float>(numChannels);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* src = source.getReadPointer(ch);
            float* dst = buffer.getWritePointer(0);
            for (int i = 0; i < numSamples; ++i)
                dst[i] += src[i] * invChannels;
        }
    }

    if (!normalizeOn || buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
        return buffer;

    const int freezeSamples = buffer.getNumSamples();
    float startFrac = juce::jlimit(0.0f, 1.0f, normalizeStartFrac);
    float endFrac = juce::jlimit(0.0f, 1.0f, normalizeEndFrac);
    if (endFrac < startFrac)
        std::swap(startFrac, endFrac);

    int normStart = juce::roundToInt(startFrac * static_cast<float>(freezeSamples));
    int normEnd = juce::roundToInt(endFrac * static_cast<float>(freezeSamples));
    normStart = juce::jlimit(0, freezeSamples, normStart);
    normEnd = juce::jlimit(normStart, freezeSamples, normEnd);
    if (normEnd <= normStart)
    {
        normStart = 0;
        normEnd = freezeSamples;
    }

    normalizer.normalizeBuffer(buffer, normStart, normEnd, sourceRate);
    return buffer;
}
}

T5ynthProcessor::T5ynthProcessor()
    : AudioProcessor(BusesProperties()
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "T5ynth", createParameterLayout())
{
    paramCache.init(parameters);

    // Cache the transport params the XL DAW-mode buttons toggle (set from the audio
    // thread via setValueNotifyingHost — same mechanism as the CC binding apply).
    seqRunningParam_    = parameters.getParameter(PID::seqRunning);
    genSeqRunningParam_ = parameters.getParameter(PID::genSeqRunning);

    juce::File("/tmp/t5ynth_sampler_debug.log").deleteFile();
    samplerProcessorDebugLog("session start");
    stepSequencer.setOneShotTriggerCallback(
        [this](const T5ynthStepSequencer::OneShotTrigger& trigger)
        {
            queueSequencerOneShotTrigger(trigger);
        });
    samplerReprepareThread = std::thread([this] { samplerReprepareThreadMain(); });
}

T5ynthProcessor::~T5ynthProcessor()
{
    // Cancel any pending deferred LED burst + AsyncUpdater callback before members
    // are destroyed — both must run while all members are still alive.
    xlLedTimer_.stopTimer();
    cancelPendingUpdate();

    closeMidiOutputDevice();

    samplerReprepareThreadShouldExit.store(true, std::memory_order_release);
    if (samplerReprepareThread.joinable())
        samplerReprepareThread.join();
}

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
    const int seqOctaveIdx = static_cast<int>(paramCache.seqOctave->load());
    const int seqOctaveSemi = (seqOctaveIdx - 2) * 12;
    const int note = juce::jlimit(0, 127, midiNote + seqOctaveSemi);
    const float vel = juce::jlimit(0.0f, 1.0f, velocity);
    const bool lfo1TrigMode = static_cast<int>(paramCache.lfo1Mode->load()) == 1;
    const bool lfo2TrigMode = static_cast<int>(paramCache.lfo2Mode->load()) == 1;
    const bool lfo3TrigMode = static_cast<int>(paramCache.lfo3Mode->load()) == 1;

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

void T5ynthProcessor::beginComputerKeyboardNote(int midiNote, float velocity)
{
    const juce::ScopedLock sl(getCallbackLock());

    const int note = juce::jlimit(0, 127, midiNote);
    const float vel = juce::jlimit(0.0f, 1.0f, velocity);
    const bool lfo1TrigMode = static_cast<int>(paramCache.lfo1Mode->load()) == 1;
    const bool lfo2TrigMode = static_cast<int>(paramCache.lfo2Mode->load()) == 1;
    const bool lfo3TrigMode = static_cast<int>(paramCache.lfo3Mode->load()) == 1;

    static constexpr int kComputerKeyboardSourceId = 15;
    voiceManager.noteOn(note, vel, false, 0.0f,
                        lfo1TrigMode, lfo2TrigMode, lfo3TrigMode,
                        kComputerKeyboardSourceId, 0.0f);

    lastMidiNote.store(note, std::memory_order_relaxed);
    lastMidiVelocity.store(juce::roundToInt(vel * 127.0f), std::memory_order_relaxed);
    lastMidiNoteOn.store(true, std::memory_order_relaxed);
}

void T5ynthProcessor::endComputerKeyboardNote(int midiNote)
{
    const juce::ScopedLock sl(getCallbackLock());

    static constexpr int kComputerKeyboardSourceId = 15;
    voiceManager.noteOff(juce::jlimit(0, 127, midiNote), kComputerKeyboardSourceId);
    if (!voiceManager.hasActiveVoices())
        lastMidiNoteOn.store(false, std::memory_order_relaxed);
}

void T5ynthProcessor::allComputerKeyboardNotesOff()
{
    const juce::ScopedLock sl(getCallbackLock());

    static constexpr int kComputerKeyboardSourceId = 15;
    for (int note = 0; note < 128; ++note)
        voiceManager.noteOff(note, kComputerKeyboardSourceId);
    if (!voiceManager.hasActiveVoices())
        lastMidiNoteOn.store(false, std::memory_order_relaxed);
}

juce::NormalisableRange<float> T5ynthProcessor::makeDurationRange(float maxSeconds)
{
    // Single source for the Duration range's skew + snapping. The APVTS
    // parameter is registered once at the global maximum (120s) so it can hold
    // any model's duration; PromptPanel narrows the *slider* per model (11s
    // default, 120s for SA3) via setNormalisableRange using this same factory,
    // so the slider feel and the snapping never drift from the parameter.
    return juce::NormalisableRange<float>(0.1f, maxSeconds,
        convertSkew03From0To1,
        convertSkew03To0To1,
        snapGenerationDuration);
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
        toChoices(FilterDriveOs::kEntries), FilterDriveOs::X2));
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
    // Resynth (init_audio / i2i): a single Off->Full amount, no separate toggle —
    // the slider's minimum IS off. 0 = ordinary text-only generation; turning up
    // feeds the last raw generation back as the denoise seed so each render evolves
    // from the previous one, and 1 = full effect (output follows the fed-back
    // source most strongly). buildInferenceRequest maps the amount onto SA3's
    // MEASURED useful init_noise band (0.48..0.05); 0 sends no init_audio at all.
    // Default off so normal SA3 generation is unchanged until you opt in.
    // 0.05 grid (21 steps): the six named anchors (0 / .05 / .25 / .5 / .75 / 1 →
    // Off / Min / Subtle / Medium / Strong / Full) all land exactly on the grid so
    // the word readout maps one-to-one to a click-stop and "Full" is unambiguously
    // the rightmost stop; "Min" is the smallest active step (one grid notch above
    // Off). The 0.05 steps in between let you dial a value the words don't name (the
    // readout shows a percentage off-anchor). Drift modulates resynth through an
    // override path, not this param, so the stepped base does not coarsen drift's sweep.
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::resynthAmount, 1}, "Resynth",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.05f), 0.0f));

    // Semantic self-listening loop (CLAP ear → LLM interpreter → next prompt).
    // Read message-thread-only at generation time (PromptPanel::runSemanticLoopStep
    // + buildInferenceRequest's init_noise override) — deliberately NOT in
    // ParamCache / BlockParams / processBlock (no audio-thread consumer; an APVTS
    // lookup there would be a pure idle-CPU regression). Both default to index 0
    // (stance Off → loop disabled; coupling alpha → A anchor / B rewritten).
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::repromptStance, 1}, "Re-Prompt Stance",
        toChoices(RepromptStance::kEntries), 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::repromptCoupling, 1}, "Re-Prompt Coupling",
        toChoices(RepromptCoupling::kEntries), 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::genAxesAmount, 1}, "Axes Amount",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::genDuration, 1}, "Duration",
        // The parameter spans the global maximum (120s). The *slider* ceiling is
        // model-dependent (PromptPanel::applyDurationRangeForCurrentModel): 11s
        // for SAO/AudioLDM2 — T5ynth's short-sound default — and 120s for SA3,
        // whose rotary-DiT generates variable-length, music-scale audio for
        // embedded/deconstructed samples. The slider can't exceed its model's
        // real ceiling; the parameter just has to be able to hold 120s.
        makeDurationRange(120.0f), 3.0f));
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
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::freezeTexture, 1}, "Granular Texture",
        toChoices(FreezeTexture::kEntries), FreezeTexture::Silk));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::freezeStereo, 1}, "Granular Stereo",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.25f));

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

    // MIDI aftertouch performance routing
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{PID::aftertouchTarget, 1}, "Aftertouch Target",
        toChoices(AftertouchTarget::kEntries), 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{PID::aftertouchAmount, 1}, "Aftertouch Amount",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.001f), 0.5f));

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

    // Per-stage velocity sensitivity, signed [-1..+1]. A/D/R = velocity→time,
    // Sustain = velocity→peak. Sustain defaults to 1.0 (classic velocity→loudness);
    // the time stages default to 0 (no velocity effect).
    auto addVelSens = [&params](const char* id, const juce::String& name, float def) {
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{id, 1}, name,
            juce::NormalisableRange<float>(-1.0f, 1.0f), def));
    };
    addVelSens(PID::ampAttackVelSens,  "Amp Attack Vel Sens",  0.0f);
    addVelSens(PID::ampDecayVelSens,   "Amp Decay Vel Sens",   0.0f);
    addVelSens(PID::ampSustainVelSens, "Amp Sustain Vel Sens", 1.0f);
    addVelSens(PID::ampReleaseVelSens, "Amp Release Vel Sens", 0.0f);
    addVelSens(PID::mod1AttackVelSens,  "Mod1 Attack Vel Sens",  0.0f);
    addVelSens(PID::mod1DecayVelSens,   "Mod1 Decay Vel Sens",   0.0f);
    addVelSens(PID::mod1SustainVelSens, "Mod1 Sustain Vel Sens", 1.0f);
    addVelSens(PID::mod1ReleaseVelSens, "Mod1 Release Vel Sens", 0.0f);
    addVelSens(PID::mod2AttackVelSens,  "Mod2 Attack Vel Sens",  0.0f);
    addVelSens(PID::mod2DecayVelSens,   "Mod2 Decay Vel Sens",   0.0f);
    addVelSens(PID::mod2SustainVelSens, "Mod2 Sustain Vel Sens", 1.0f);
    addVelSens(PID::mod2ReleaseVelSens, "Mod2 Release Vel Sens", 0.0f);

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
        juce::ParameterID{PID::ampTarget, 1}, "Amp Target", envTargetChoices, EnvTarget::DCA));
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
    masterFreeze.prepare(sampleRate, samplesPerBlock);
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
    pendingSequencerOneShotCount = 0;
    for (auto& voice : activeSequencerOneShots)
        voice.active = false;

    silentBlockCount = 0;
    // Allow ~10 seconds of reverb tail before deep idle
    tailBlocks = std::max(1, static_cast<int>(10.0 * sampleRate / samplesPerBlock));
}

void T5ynthProcessor::releaseResources()
{
    masterOsc.reset();
    masterSampler.reset();
    masterFreeze.reset();
    voiceManager.reset();
    {
        std::lock_guard<std::mutex> lock(samplerReprepareSourceMutex);
        samplerReprepareSourceBuffer.setSize(0, 0);
        samplerReprepareSourceValid = false;
        ++samplerReprepareSourceVersion;
    }
}

bool T5ynthProcessor::assignSequencerOneShotFromCurrentRegion(int step, int slot)
{
    float regionStart = 0.0f;
    float regionEnd = 1.0f;
    {
        const juce::ScopedLock sl(getCallbackLock());
        regionStart = masterSampler.getStartPos();
        regionEnd = masterSampler.getLoopEnd();
    }

    return assignSequencerOneShotFromRegion(step, slot, regionStart, regionEnd);
}

bool T5ynthProcessor::assignSequencerOneShotFromRegion(int step, int slot, float regionStart, float regionEnd)
{
    if (step < 0 || step >= T5ynthStepSequencer::MAX_STEPS
        || slot < 0 || slot >= T5ynthStepSequencer::ONE_SHOT_SLOTS
        || !std::isfinite(regionStart) || !std::isfinite(regionEnd))
        return false;

    auto sample = std::make_shared<SequencerOneShotSample>();

    {
        const juce::ScopedLock sl(getCallbackLock());

        const auto& source = generatedAudioFull;
        const int sourceSamples = source.getNumSamples();
        const int sourceChannels = source.getNumChannels();
        if (sourceSamples <= 0 || sourceChannels <= 0)
            return false;

        const float start = juce::jlimit(0.0f, 1.0f, regionStart);
        const float end = juce::jlimit(0.0f, 1.0f, regionEnd);
        const float lo = std::min(start, end);
        const float hi = std::max(start, end);

        int startSample = juce::jlimit(0, sourceSamples - 1,
            static_cast<int>(std::floor(lo * static_cast<float>(sourceSamples))));
        int endSample = juce::jlimit(startSample + 1, sourceSamples,
            static_cast<int>(std::ceil(hi * static_cast<float>(sourceSamples))));
        if (endSample <= startSample)
            endSample = juce::jmin(sourceSamples, startSample + 1);

        const int length = endSample - startSample;
        sample->audio.setSize(sourceChannels, length, false, false, true);
        for (int ch = 0; ch < sourceChannels; ++ch)
            sample->audio.copyFrom(ch, 0, source, ch, startSample, length);

        sample->sampleRate = generatedSampleRate > 0.0 ? generatedSampleRate : getSampleRate();
        const double startSec = static_cast<double>(startSample) / sample->sampleRate;
        const double endSec = static_cast<double>(endSample) / sample->sampleRate;
        sample->label = "P1-P3 "
            + juce::String(startSec, 2) + "s-"
            + juce::String(endSec, 2) + "s";
    }

    stepSequencer.setStepOneShotMode(step, slot, T5ynthStepSequencer::OneShotMode::Normal);
    std::atomic_store_explicit(
        &sequencerOneShotSamples[static_cast<size_t>(step)][static_cast<size_t>(slot)],
        SequencerOneShotSamplePtr(std::move(sample)),
        std::memory_order_release);
    return true;
}

bool T5ynthProcessor::hasSequencerOneShotSample(int step, int slot) const
{
    if (step < 0 || step >= T5ynthStepSequencer::MAX_STEPS
        || slot < 0 || slot >= T5ynthStepSequencer::ONE_SHOT_SLOTS)
        return false;

    return std::atomic_load_explicit(
        &sequencerOneShotSamples[static_cast<size_t>(step)][static_cast<size_t>(slot)],
        std::memory_order_acquire) != nullptr;
}

void T5ynthProcessor::clearSequencerOneShotSample(int step, int slot)
{
    if (step < 0 || step >= T5ynthStepSequencer::MAX_STEPS
        || slot < 0 || slot >= T5ynthStepSequencer::ONE_SHOT_SLOTS)
        return;

    std::atomic_store_explicit(
        &sequencerOneShotSamples[static_cast<size_t>(step)][static_cast<size_t>(slot)],
        SequencerOneShotSamplePtr{},
        std::memory_order_release);
    stepSequencer.setStepOneShotMode(step, slot, T5ynthStepSequencer::OneShotMode::Normal);
}

void T5ynthProcessor::clearSequencerOneShotSamples()
{
    for (int step = 0; step < T5ynthStepSequencer::MAX_STEPS; ++step)
        for (int slot = 0; slot < T5ynthStepSequencer::ONE_SHOT_SLOTS; ++slot)
            clearSequencerOneShotSample(step, slot);
}

bool T5ynthProcessor::copySequencerOneShotSample(int srcStep, int srcSlot, int dstStep, int dstSlot)
{
    if (srcStep < 0 || srcStep >= T5ynthStepSequencer::MAX_STEPS
        || srcSlot < 0 || srcSlot >= T5ynthStepSequencer::ONE_SHOT_SLOTS
        || dstStep < 0 || dstStep >= T5ynthStepSequencer::MAX_STEPS
        || dstSlot < 0 || dstSlot >= T5ynthStepSequencer::ONE_SHOT_SLOTS)
        return false;

    if (srcStep == dstStep && srcSlot == dstSlot)
        return false;

    auto sample = std::atomic_load_explicit(
        &sequencerOneShotSamples[static_cast<size_t>(srcStep)][static_cast<size_t>(srcSlot)],
        std::memory_order_acquire);
    if (!sample || sample->audio.getNumSamples() <= 0)
        return false;

    // Duplicate the source slot's playback mode so the copy behaves identically.
    stepSequencer.setStepOneShotMode(dstStep, dstSlot,
        stepSequencer.getStepOneShotMode(srcStep, srcSlot));

    // Share the immutable sample by pointer — no deep audio copy. The audio
    // thread only atomic-loads these pointers and reads the const buffer, so
    // two slots aliasing one sample is the same lock-free sharing model used
    // for master→voice engine data. The previous dst pointer (if any) is
    // released here on the message thread by atomic_store.
    std::atomic_store_explicit(
        &sequencerOneShotSamples[static_cast<size_t>(dstStep)][static_cast<size_t>(dstSlot)],
        sample,
        std::memory_order_release);
    return true;
}

std::vector<T5ynthProcessor::SequencerOneShotExport>
T5ynthProcessor::exportSequencerOneShotSamples() const
{
    std::vector<SequencerOneShotExport> out;

    for (int step = 0; step < T5ynthStepSequencer::MAX_STEPS; ++step)
    {
        for (int slot = 0; slot < T5ynthStepSequencer::ONE_SHOT_SLOTS; ++slot)
        {
            auto sample = std::atomic_load_explicit(
                &sequencerOneShotSamples[static_cast<size_t>(step)][static_cast<size_t>(slot)],
                std::memory_order_acquire);
            if (!sample || sample->audio.getNumSamples() <= 0)
                continue;

            SequencerOneShotExport e;
            e.step = step;
            e.slot = slot;
            e.mode = stepSequencer.getStepOneShotMode(step, slot);
            e.label = sample->label;
            e.sampleRate = sample->sampleRate;
            e.audio.makeCopyOf(sample->audio);
            out.push_back(std::move(e));
        }
    }

    return out;
}

void T5ynthProcessor::importSequencerOneShotSamples(const std::vector<SequencerOneShotExport>& slots)
{
    clearSequencerOneShotSamples();

    for (const auto& slot : slots)
    {
        if (slot.step < 0 || slot.step >= T5ynthStepSequencer::MAX_STEPS
            || slot.slot < 0 || slot.slot >= T5ynthStepSequencer::ONE_SHOT_SLOTS
            || slot.audio.getNumSamples() <= 0 || slot.audio.getNumChannels() <= 0)
            continue;

        auto sample = std::make_shared<SequencerOneShotSample>();
        sample->audio.makeCopyOf(slot.audio);
        sample->sampleRate = slot.sampleRate > 0.0 ? slot.sampleRate : 44100.0;
        sample->label = slot.label;

        stepSequencer.setStepOneShotMode(slot.step, slot.slot, slot.mode);
        std::atomic_store_explicit(
            &sequencerOneShotSamples[static_cast<size_t>(slot.step)][static_cast<size_t>(slot.slot)],
            SequencerOneShotSamplePtr(std::move(sample)),
            std::memory_order_release);
    }
}

void T5ynthProcessor::queueSequencerOneShotTrigger(const T5ynthStepSequencer::OneShotTrigger& trigger)
{
    if (trigger.stepIndex < 0 || trigger.stepIndex >= T5ynthStepSequencer::MAX_STEPS
        || trigger.slotIndex < 0 || trigger.slotIndex >= T5ynthStepSequencer::ONE_SHOT_SLOTS
        || pendingSequencerOneShotCount >= kMaxSequencerOneShotVoices)
        return;

    auto& pending = pendingSequencerOneShots[static_cast<size_t>(pendingSequencerOneShotCount)];
    // `pending` is normally cleared at consume (startVoice), but a 0-sample block
    // (renderSequencerOneShots early-out) can leave a stale ref here. Retire it to the
    // bin before reusing the slot — and BEFORE loading `sample` below, so a ring-full
    // drop never strands a freshly-loaded shared_ptr local to release on the audio
    // thread. (A null pending is a no-op that returns true, so this falls through.)
    if (! retireOneShotSampleToBin(pending.sample))
        return;

    auto sample = std::atomic_load_explicit(
        &sequencerOneShotSamples[static_cast<size_t>(trigger.stepIndex)]
                                [static_cast<size_t>(trigger.slotIndex)],
        std::memory_order_acquire);
    // Test only for null here. A non-null one-shot is guaranteed non-empty by the
    // store side (assign/copy/import all reject 0-sample / 0-channel buffers), so a
    // getNumSamples()/getNumChannels() test would be dead code — and taking this
    // early return with a *non-null* `sample` would destruct the last reference on
    // the audio thread (CLAUDE.md #4). Returning only on null destructs a null and
    // never frees; renderSequencerOneShots still guards empties defensively.
    if (! sample)
        return;

    pending.sample = std::move(sample);  // assign over null
    pending.gain = trigger.gain;
    pending.sampleOffset = trigger.sampleOffset;
    ++pendingSequencerOneShotCount;
}

bool T5ynthProcessor::hasActiveSequencerOneShots() const
{
    for (const auto& voice : activeSequencerOneShots)
        if (voice.active)
            return true;
    return false;
}

bool T5ynthProcessor::retireOneShotSampleToBin(SequencerOneShotSamplePtr& ptr) noexcept
{
    // Audio thread. Hand a one-shot sample ref to the worker thread for release by
    // MOVING it into the SPSC ring — a move never changes the refcount, so it can
    // never trigger the held juce::AudioBuffer's free on the audio thread
    // (CLAUDE.md #4). A null ptr is a no-op. Returns false only when the ring is
    // full, leaving ptr untouched (still holding its ref) so the caller keeps it
    // in place rather than freeing it here.
    if (ptr == nullptr)
        return true;

    const int head = oneShotRetireHead.load(std::memory_order_relaxed);  // producer owns head
    const int next = (head + 1) & kOneShotRetireMask;
    // acquire pairs with the consumer's tail release-store: it guarantees the
    // consumer has already reset (nulled) slot `head` before we are allowed to
    // reuse it, so the move-assign below writes into a null slot and never
    // releases a ref on the audio thread.
    if (next == oneShotRetireTail.load(std::memory_order_acquire))
        return false;  // ring full — leave ptr in place (safe; retired on a later pass)

    oneShotRetireBin[static_cast<size_t>(head)] = std::move(ptr);
    oneShotRetireHead.store(next, std::memory_order_release);  // publishes the moved-in ptr
    return true;
}

void T5ynthProcessor::drainSequencerOneShotRetireBin() noexcept
{
    // Worker thread (samplerReprepareThreadMain). Releases the one-shot samples
    // the audio thread retired — the actual juce::AudioBuffer frees happen here,
    // off the audio thread. Single consumer: only this thread writes the tail.
    const int head = oneShotRetireHead.load(std::memory_order_acquire);  // see producer's moves
    int tail = oneShotRetireTail.load(std::memory_order_relaxed);        // consumer owns tail
    while (tail != head)
    {
        oneShotRetireBin[static_cast<size_t>(tail)].reset();
        tail = (tail + 1) & kOneShotRetireMask;
    }
    oneShotRetireTail.store(tail, std::memory_order_release);  // publishes the resets (slots now null)
}

void T5ynthProcessor::stopSequencerOneShots()
{
    pendingSequencerOneShotCount = 0;
    for (auto& voice : activeSequencerOneShots)
    {
        voice.active = false;
        retireOneShotSampleToBin(voice.sample);  // off-thread release (never reset() on the audio thread)
        voice.startOffset = 0;
        voice.position = 0.0;
    }
}

void T5ynthProcessor::renderSequencerOneShots(juce::AudioBuffer<float>& buffer)
{
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples <= 0 || numChannels <= 0)
        return;

    auto startVoice = [this, numSamples](PendingSequencerOneShot& pending)
    {
        int target = -1;
        uint64_t oldest = std::numeric_limits<uint64_t>::max();

        for (int i = 0; i < kMaxSequencerOneShotVoices; ++i)
        {
            const auto& voice = activeSequencerOneShots[static_cast<size_t>(i)];
            if (!voice.active)
            {
                target = i;
                break;
            }
            if (voice.age < oldest)
            {
                oldest = voice.age;
                target = i;
            }
        }

        if (target < 0)
            return;

        auto& voice = activeSequencerOneShots[static_cast<size_t>(target)];

        // The chosen slot may still hold a ref: a stolen still-playing voice, or a
        // leftover from prepareToPlay / a previous bin-full retire. Releasing it by
        // overwriting voice.sample would free on the audio thread, so hand it to the
        // retire bin first. If the ring is full we must not overwrite a live ref —
        // drop this trigger instead (a missed one-shot is a glitch, never a crash).
        // A free voice holds null, so this is a no-op in the common case.
        if (! retireOneShotSampleToBin(voice.sample))
            return;

        voice.sample = pending.sample;  // assign over null (copy ⇒ refcount ≥ 2)
        voice.position = 0.0;
        const double hostRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
        voice.increment = juce::jmax(0.0001, pending.sample->sampleRate / hostRate);
        voice.gain = pending.gain;
        voice.startOffset = juce::jlimit(0, juce::jmax(0, numSamples - 1), pending.sampleOffset);
        voice.active = true;
        voice.age = ++sequencerOneShotAgeCounter;

        // The voice now owns a copy, so this drop can never be the last reference —
        // safe to release on the audio thread. Keeping `pending` null means the next
        // block's overwrite in queueSequencerOneShotTrigger also never frees here.
        pending.sample.reset();
    };

    for (int i = 0; i < pendingSequencerOneShotCount; ++i)
        startVoice(pendingSequencerOneShots[static_cast<size_t>(i)]);
    pendingSequencerOneShotCount = 0;

    for (auto& voice : activeSequencerOneShots)
    {
        if (!voice.active || !voice.sample)
            continue;

        const auto& source = voice.sample->audio;
        const int sourceSamples = source.getNumSamples();
        const int sourceChannels = source.getNumChannels();
        if (sourceSamples <= 0 || sourceChannels <= 0)
        {
            voice.active = false;
            continue;
        }

        const int start = juce::jlimit(0, numSamples, voice.startOffset);
        voice.startOffset = 0;

        for (int i = start; i < numSamples; ++i)
        {
            if (voice.position >= static_cast<double>(sourceSamples))
            {
                voice.active = false;
                retireOneShotSampleToBin(voice.sample);  // off-thread release (never reset() on the audio thread)
                break;
            }

            const int idx = juce::jlimit(0, sourceSamples - 1,
                static_cast<int>(voice.position));
            const int idx2 = juce::jmin(idx + 1, sourceSamples - 1);
            const float frac = static_cast<float>(voice.position - static_cast<double>(idx));

            for (int outCh = 0; outCh < numChannels; ++outCh)
            {
                const int srcCh = sourceChannels == 1 ? 0 : juce::jmin(outCh, sourceChannels - 1);
                const float s0 = source.getSample(srcCh, idx);
                const float s1 = source.getSample(srcCh, idx2);
                buffer.addSample(outCh, i, (s0 + (s1 - s0) * frac) * voice.gain);
            }

            voice.position += voice.increment;
        }
    }
}

void T5ynthProcessor::syncSamplerSettingsFromParametersLocked()
{
    int loopModeIdx = static_cast<int>(paramCache.loopMode->load());
    masterSampler.setLoopMode(static_cast<SamplePlayer::LoopMode>(juce::jlimit(0, 2, loopModeIdx)));
    masterSampler.setCrossfadeMs(paramCache.crossfadeMs->load());
    masterSampler.setNormalize(paramCache.normalize->load() > 0.5f);
    masterSampler.setLoopOptimizeLevel(static_cast<int>(paramCache.loopOptimize->load()));
}

void T5ynthProcessor::storeSamplerReprepareSource(const juce::AudioBuffer<float>& buffer,
                                                  double sampleRate,
                                                  float normalizeStartFrac,
                                                  float normalizeEndFrac)
{
    std::lock_guard<std::mutex> lock(samplerReprepareSourceMutex);
    samplerReprepareSourceBuffer.makeCopyOf(buffer);
    samplerReprepareSourceRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    samplerReprepareNormalizeStartFrac = juce::jlimit(0.0f, 1.0f, normalizeStartFrac);
    samplerReprepareNormalizeEndFrac = juce::jlimit(0.0f, 1.0f, normalizeEndFrac);
    samplerReprepareSourceValid = samplerReprepareSourceBuffer.getNumSamples() > 0
                               && samplerReprepareSourceBuffer.getNumChannels() > 0;
    ++samplerReprepareSourceVersion;
}

bool T5ynthProcessor::serviceSamplerReprepare()
{
    SamplePlayer::PrepareConfig config;
    {
        const juce::ScopedLock sl(getCallbackLock());
        syncSamplerSettingsFromParametersLocked();
        if (!masterSampler.hasAudio() || !masterSampler.needsReprepare())
            return false;
        config = masterSampler.capturePrepareConfig();
    }

    juce::AudioBuffer<float> source;
    double sourceRate = 44100.0;
    juce::uint64 sourceVersion = 0;
    float normalizeStartFrac = 0.0f;
    float normalizeEndFrac = 1.0f;
    {
        std::lock_guard<std::mutex> lock(samplerReprepareSourceMutex);
        if (!samplerReprepareSourceValid)
            return false;
        source.makeCopyOf(samplerReprepareSourceBuffer);
        sourceRate = samplerReprepareSourceRate;
        sourceVersion = samplerReprepareSourceVersion;
        normalizeStartFrac = samplerReprepareNormalizeStartFrac;
        normalizeEndFrac = samplerReprepareNormalizeEndFrac;
    }

    auto prepared = masterSampler.prepareBufferLoad(source, sourceRate, config);
    auto preparedFreezeBuffer = makeFreezeLoadBuffer(source,
                                                     sourceRate,
                                                     config.normalizeOn,
                                                     normalizeStartFrac,
                                                     normalizeEndFrac,
                                                     masterSampler);

    {
        std::lock_guard<std::mutex> lock(samplerReprepareSourceMutex);
        if (!samplerReprepareSourceValid || samplerReprepareSourceVersion != sourceVersion)
        {
            samplerReprepareWorkRequested.store(true, std::memory_order_release);
            return false;
        }
    }

    {
        const juce::ScopedLock sl(getCallbackLock());
        syncSamplerSettingsFromParametersLocked();
        const auto currentConfig = masterSampler.capturePrepareConfig();
        if (!samePrepareConfig(config, currentConfig))
        {
            samplerReprepareWorkRequested.store(true, std::memory_order_release);
            return false;
        }

        masterSampler.applyPreparedBufferLoad(std::move(prepared), config);
        masterFreeze.loadBuffer(preparedFreezeBuffer, sourceRate);
        voiceManager.distributeSamplerBuffer(masterSampler);
        // Sampler re-prepare (config change, not a new inference) → keep held
        // granular voices on their current buffer (no live morph).
        voiceManager.distributeFreezeBuffer(masterFreeze, 0.0f, false);
    }

    return true;
}

void T5ynthProcessor::samplerReprepareThreadMain()
{
    while (!samplerReprepareThreadShouldExit.load(std::memory_order_acquire))
    {
        samplerReprepareWorkRequested.store(false, std::memory_order_release);
        serviceSamplerReprepare();
        drainSequencerOneShotRetireBin();  // release one-shot samples the audio thread retired (off-thread)

        for (int i = 0; i < 5 && !samplerReprepareThreadShouldExit.load(std::memory_order_acquire); ++i)
        {
            if (samplerReprepareWorkRequested.exchange(false, std::memory_order_acq_rel))
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void T5ynthProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    pendingSequencerOneShotCount = 0;


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

    // ── MIDI Clock: loss detection + tick pre-pass (feeds resolveSyncBpm()) ─
    if (midiClockEnabled_.load(std::memory_order_relaxed))
    {
        // Re-enable edge: reset counters so stale intervals from a previous
        // session don't produce a wrong first post-re-enable tick interval.
        if (!midiClockPrevEnabled_)
        {
            midiClockTickCount_ = 0;
            midiClockLastTick_  = 0;
            midiClockPrevEnabled_ = true;
        }

        // Loss detection: no tick for > 1 second → invalidate.
        if (midiClockValid_.load(std::memory_order_relaxed)
            && midiClockLastTick_ > 0
            && midiClockBlockStart_ > midiClockLastTick_
            && (midiClockBlockStart_ - midiClockLastTick_)
               > static_cast<uint64_t>(getSampleRate()))
        {
            midiClockValid_.store(false, std::memory_order_release);
            midiClockTickCount_ = 0;
        }

        for (const auto& meta : midiMessages)
        {
            const auto msg = meta.getMessage();
            if (msg.isMidiClock())
            {
                const uint64_t tickSample = midiClockBlockStart_
                                          + static_cast<uint64_t>(meta.samplePosition);
                if (midiClockLastTick_ > 0 && tickSample > midiClockLastTick_)
                {
                    const uint32_t interval =
                        static_cast<uint32_t>(tickSample - midiClockLastTick_);
                    midiClockIntervals_[midiClockTickIdx_] = interval;
                    midiClockTickIdx_ = (midiClockTickIdx_ + 1) % 24;
                    if (midiClockTickCount_ < 24) ++midiClockTickCount_;

                    if (midiClockTickCount_ >= 4)
                    {
                        uint64_t sum = 0;
                        for (int i = 0; i < midiClockTickCount_; ++i)
                            sum += midiClockIntervals_[i];
                        const float avg = static_cast<float>(sum)
                                        / static_cast<float>(midiClockTickCount_);
                        const float bpm = 60.0f * static_cast<float>(getSampleRate())
                                        / (avg * 24.0f);
                        midiClockBpm_.store(juce::jlimit(20.0f, 300.0f, bpm),
                                            std::memory_order_release);
                        midiClockValid_.store(true, std::memory_order_release);
                    }
                }
                midiClockLastTick_ = tickSample;
            }
            else if (msg.isMidiStop())
            {
                midiClockValid_.store(false, std::memory_order_release);
                midiClockTickCount_ = 0;
                midiClockLastTick_  = 0;
            }
            else if (msg.isMidiStart())
            {
                midiClockValid_.store(false, std::memory_order_release);
                midiClockTickCount_ = 0;
                midiClockLastTick_  = 0;
            }
            // isMidiContinue: keep accumulating ticks without resetting warm-up
        }
    }
    else
    {
        midiClockPrevEnabled_ = false;  // reset so re-enable is detected next time
    }

    const float syncBpm = resolveSyncBpm();

    // ── MIDI Panic (StatusBar button) ────────────────────────────────────
    // GUI sets the flag from any thread; we consume it once here on the
    // audio thread. Mirrors the CC120/123 path below — release all voices,
    // clear sustain/sostenuto/drone, reset performance controllers.
    if (midiPanicRequested.exchange(false, std::memory_order_acq_rel))
    {
        voiceManager.allNotesOff();
        lastMidiNoteOn.store(false, std::memory_order_relaxed);
    }

    // ── Sync LFO/Drift phase alignment to the sequencer downbeat ────────────
    // A Drift/LFO switched to Sync should START its cycle on the beat (the
    // sequencer's "1"), not wherever the toggle happened to land.
    //   • Transport stop→start: the start instant IS step 0 (the downbeat) →
    //     align phase to 0 immediately, as before.
    //   • Switched Off→Sync WHILE the step sequencer already leads the clock:
    //     snapping now would lock the cycle between beats. Instead ARM the
    //     modulator — held silent at phase 0 (see LFO::setArmed /
    //     DriftLFO::setLfoArmed) — and release it on the next bar downbeat
    //     (where barStartFlag is consumed, below) so its first cycle starts on
    //     the "1". ("ggf. kurz warten bis der Beat kommt.")
    // Scope: only while the STEP seq is the lead (not GEN mode, host not
    // playing) — that is the only transport exposing a bar signal to align to;
    // GEN-engine and host-led playback keep the old immediate behaviour.
    // PID::genSeqRunning is a STEP↔GEN toggle, not transport.
    {
        const bool seqRunNow      = paramCache.seqRunning->load() > 0.5f;
        const bool transportStart = seqRunNow && !lastSeqRunning;
        const bool stepLeads      = seqRunNow && !genModeActiveInAudio
                                    && !hostPlayingNow.load(std::memory_order_relaxed);

        const int lc[3] = { static_cast<int>(paramCache.lfo1ClockMode->load()),
                            static_cast<int>(paramCache.lfo2ClockMode->load()),
                            static_cast<int>(paramCache.lfo3ClockMode->load()) };
        const int dc[3] = { static_cast<int>(paramCache.drift1ClockMode->load()),
                            static_cast<int>(paramCache.drift2ClockMode->load()),
                            static_cast<int>(paramCache.drift3ClockMode->load()) };
        LFO* const lfoPtr[3] = { &lfo1, &lfo2, &lfo3 };

        for (int i = 0; i < 3; ++i)
        {
            if (transportStart)
            {
                // Start coincides with the downbeat → align now, clear any arm.
                lfoSyncArmed[i]   = false;
                driftSyncArmed[i] = false;
                lfoPtr[i]->setArmed(false);
                driftLfo.setLfoArmed(i, false);
                if (lc[i] != ClockMode::Off) lfoPtr[i]->reset();
                if (dc[i] != ClockMode::Off) driftLfo.resetLfoPhase(i);
            }
            else
            {
                // Arm on a mid-run Off→Sync edge (only while the step seq leads).
                if (stepLeads && lc[i] != ClockMode::Off && lastLfoClockMode[i] == ClockMode::Off)
                    lfoSyncArmed[i] = true;
                if (stepLeads && dc[i] != ClockMode::Off && lastDriftClockMode[i] == ClockMode::Off)
                    driftSyncArmed[i] = true;

                // Cancel a pending arm if Sync was switched off or the lead was lost.
                if (lc[i] == ClockMode::Off || !stepLeads) lfoSyncArmed[i]   = false;
                if (dc[i] == ClockMode::Off || !stepLeads) driftSyncArmed[i] = false;

                lfoPtr[i]->setArmed(lfoSyncArmed[i]);
                driftLfo.setLfoArmed(i, driftSyncArmed[i]);
            }

            lastLfoClockMode[i]   = lc[i];
            lastDriftClockMode[i] = dc[i];
        }
        lastSeqRunning = seqRunNow;
    }

    updateDriftState(numSamples, syncBpm);

    // ── Idle detection ──────────────────────────────────────────────────────
    bool seqRunning = paramCache.seqRunning->load() > 0.5f;
    // A pending sequencer-preset change must keep the block awake for one cycle
    // so the apply further below runs even while stopped — otherwise picking a
    // preset in the dropdown does nothing until playback starts. Cheap (one
    // atomic load + compare) and self-clearing: once applied, lastSeqPreset
    // matches and the block idles again on the next cycle.
    bool seqPresetPending =
        static_cast<int>(paramCache.seqPreset->load()) != lastSeqPreset.load(std::memory_order_relaxed);
    bool hasActivity = voiceManager.hasActiveVoices()
                       || hasActiveSequencerOneShots()
                       || !midiMessages.isEmpty()
                       || seqRunning
                       || seqPresetPending;

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
        // MIDI Clock sample counter must advance unconditionally — a frozen base
        // would corrupt tick timestamps and break loss detection after idle gaps.
        midiClockBlockStart_ += static_cast<uint64_t>(numSamples);
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
        int vcIdx = static_cast<int>(paramCache.voiceCount->load());
        voiceManager.setVoiceLimit(voiceCounts[juce::jlimit(0, 5, vcIdx)]);
    }

    // ── Tuning table ──────────────────────────────────────────────────────────
    {
        int tuningIdx = static_cast<int>(paramCache.tuning->load());
        int scaleRoot = static_cast<int>(paramCache.scaleRoot->load());
        auto tt = static_cast<Tuning::Type>(juce::jlimit(0, (int)Tuning::COUNT - 1, tuningIdx));
        Tuning::buildTable(tuningTable, tt, scaleRoot);
        voiceManager.setTuningTable(tuningTable);
    }

    // ── Read all parameters into BlockParams ──────────────────────────────────
    BlockParams bp;
    bp.ampAttack  = paramCache.ampAttack->load();
    bp.ampDecay   = paramCache.ampDecay->load();
    bp.ampSustain = paramCache.ampSustain->load();
    bp.ampRelease = paramCache.ampRelease->load();
    bp.ampAmount  = paramCache.ampAmount->load();
    bp.ampTarget  = static_cast<int>(paramCache.ampTarget->load());
    bp.ampLoop    = paramCache.ampLoop->load() > 0.5f;
    bp.ampAttackCurve  = static_cast<int>(paramCache.ampAttackCurve->load());
    bp.ampDecayCurve   = static_cast<int>(paramCache.ampDecayCurve->load());
    bp.ampReleaseCurve = static_cast<int>(paramCache.ampReleaseCurve->load());
    bp.ampAttackVelSens  = paramCache.ampAttackVelSens->load();
    bp.ampDecayVelSens   = paramCache.ampDecayVelSens->load();
    bp.ampSustainVelSens = paramCache.ampSustainVelSens->load();
    bp.ampReleaseVelSens = paramCache.ampReleaseVelSens->load();

    bp.mod1Attack  = paramCache.mod1Attack->load();
    bp.mod1Decay   = paramCache.mod1Decay->load();
    bp.mod1Sustain = paramCache.mod1Sustain->load();
    bp.mod1Release = paramCache.mod1Release->load();
    bp.mod1Amount  = paramCache.mod1Amount->load();
    bp.mod1Target  = static_cast<int>(paramCache.mod1Target->load());
    bp.mod1Loop    = paramCache.mod1Loop->load() > 0.5f;
    bp.mod1AttackCurve  = static_cast<int>(paramCache.mod1AttackCurve->load());
    bp.mod1DecayCurve   = static_cast<int>(paramCache.mod1DecayCurve->load());
    bp.mod1ReleaseCurve = static_cast<int>(paramCache.mod1ReleaseCurve->load());
    bp.mod1AttackVelSens  = paramCache.mod1AttackVelSens->load();
    bp.mod1DecayVelSens   = paramCache.mod1DecayVelSens->load();
    bp.mod1SustainVelSens = paramCache.mod1SustainVelSens->load();
    bp.mod1ReleaseVelSens = paramCache.mod1ReleaseVelSens->load();

    bp.mod2Attack  = paramCache.mod2Attack->load();
    bp.mod2Decay   = paramCache.mod2Decay->load();
    bp.mod2Sustain = paramCache.mod2Sustain->load();
    bp.mod2Release = paramCache.mod2Release->load();
    bp.mod2Amount  = paramCache.mod2Amount->load();
    bp.mod2Target  = static_cast<int>(paramCache.mod2Target->load());
    bp.mod2Loop    = paramCache.mod2Loop->load() > 0.5f;
    bp.mod2AttackCurve  = static_cast<int>(paramCache.mod2AttackCurve->load());
    bp.mod2DecayCurve   = static_cast<int>(paramCache.mod2DecayCurve->load());
    bp.mod2ReleaseCurve = static_cast<int>(paramCache.mod2ReleaseCurve->load());
    bp.mod2AttackVelSens  = paramCache.mod2AttackVelSens->load();
    bp.mod2DecayVelSens   = paramCache.mod2DecayVelSens->load();
    bp.mod2SustainVelSens = paramCache.mod2SustainVelSens->load();
    bp.mod2ReleaseVelSens = paramCache.mod2ReleaseVelSens->load();

    // LFOs (global) — Clock-Sync override: when ClockMode::Sync, the rate
    // displayed on the slider is replaced by the sync-derived rate. We store
    // the effective rate back into `bp.lfo*Rate` so downstream env→LFO-rate
    // modulation (lines further down) scales relative to the sync rate.
    {
        const int   c1 = static_cast<int>(paramCache.lfo1ClockMode->load());
        const float r1 = paramCache.lfo1Rate->load();
        bp.lfo1Rate = (c1 == ClockMode::Off) ? r1
            : ClockSync::computeRate(syncBpm,
                static_cast<int>(paramCache.lfo1ClockDivision->load()));
        bp.lfo1Depth = paramCache.lfo1Depth->load();
        bp.lfo1Wave = static_cast<int>(paramCache.lfo1Wave->load());
        bp.lfo1TrigMode = static_cast<int>(paramCache.lfo1Mode->load()) == LfoMode::Trigger;
        lfo1.setRate(bp.lfo1Rate);
        lfo1.setDepth(1.0f);
        lfo1.setWaveform(bp.lfo1Wave);
        bp.lfo1Target = static_cast<int>(paramCache.lfo1Target->load());
    }
    {
        const int   c2 = static_cast<int>(paramCache.lfo2ClockMode->load());
        const float r2 = paramCache.lfo2Rate->load();
        bp.lfo2Rate = (c2 == ClockMode::Off) ? r2
            : ClockSync::computeRate(syncBpm,
                static_cast<int>(paramCache.lfo2ClockDivision->load()));
        bp.lfo2Depth = paramCache.lfo2Depth->load();
        bp.lfo2Wave = static_cast<int>(paramCache.lfo2Wave->load());
        bp.lfo2TrigMode = static_cast<int>(paramCache.lfo2Mode->load()) == LfoMode::Trigger;
        lfo2.setRate(bp.lfo2Rate);
        lfo2.setDepth(1.0f);
        lfo2.setWaveform(bp.lfo2Wave);
        bp.lfo2Target = static_cast<int>(paramCache.lfo2Target->load());
    }
    {
        const int   c3 = static_cast<int>(paramCache.lfo3ClockMode->load());
        const float r3 = paramCache.lfo3Rate->load();
        bp.lfo3Rate = (c3 == ClockMode::Off) ? r3
            : ClockSync::computeRate(syncBpm,
                static_cast<int>(paramCache.lfo3ClockDivision->load()));
        bp.lfo3Depth = paramCache.lfo3Depth->load();
        bp.lfo3Wave = static_cast<int>(paramCache.lfo3Wave->load());
        bp.lfo3TrigMode = static_cast<int>(paramCache.lfo3Mode->load()) == LfoMode::Trigger;
        lfo3.setRate(bp.lfo3Rate);
        lfo3.setDepth(1.0f);
        lfo3.setWaveform(bp.lfo3Wave);
        bp.lfo3Target = static_cast<int>(paramCache.lfo3Target->load());
    }

    bp.aftertouchTarget = static_cast<int>(paramCache.aftertouchTarget->load());
    bp.aftertouchAmount = paramCache.aftertouchAmount->load();

    // Filter
    // filter_type: 0=Off, 1=LP, 2=HP, 3=BP → filterEnabled from type, DSP type is 0-based
    {
        int ft = static_cast<int>(paramCache.filterType->load());
        bp.filterEnabled = (ft > 0);
        bp.filterType = ft > 0 ? ft - 1 : 0;  // 0=LP, 1=HP, 2=BP for DSP
    }
    bp.baseCutoff = paramCache.filterCutoff->load();
    bp.baseReso = paramCache.filterResonance->load();
    bp.filterSlope = static_cast<int>(paramCache.filterSlope->load());
    bp.filterMix = paramCache.filterMix->load();
    bp.kbdTrack = paramCache.filterKbdTrack->load();
    bp.filterDriveDb = paramCache.filterDrive->load();
    bp.filterDriveOs = static_cast<int>(paramCache.filterDriveOs->load());
    bp.filterAlgorithm = static_cast<int>(paramCache.filterAlgorithm->load());
    bp.filterWarpStyle = static_cast<int>(paramCache.filterWarpStyle->load());
    bp.filterDriveGain = std::pow(10.0f, bp.filterDriveDb * (1.0f / 20.0f));

    // Scan
    bp.baseScan = paramCache.oscScan->load();

    // Octave shift (APVTS choice 0-4, maps to -2..+2)
    bp.octaveShift = static_cast<int>(paramCache.oscOctave->load()) - 2;

    // Noise oscillator
    bp.noiseLevel = paramCache.noiseLevel->load();
    bp.noiseType = static_cast<int>(paramCache.noiseType->load());

    // Wavetable smooth
    bp.wtSmooth = paramCache.wtSmooth->load() > 0.5f;
    bp.wtAutoScan = paramCache.wtAutoScan->load() > 0.5f;
    bp.freezeTexture = juce::jlimit(static_cast<int>(FreezeTexture::Hold),
                                    static_cast<int>(FreezeTexture::Cloud),
                                    static_cast<int>(paramCache.freezeTexture->load()));
    bp.freezeStereo = juce::jlimit(0.0f, 1.0f,
                                   paramCache.freezeStereo->load());

    // Engine mode — read directly from APVTS (0=Sampler, 1=Wavetable, 2=Granular)
    int engineModeRaw = static_cast<int>(paramCache.engineMode->load());
    bp.engineMode = juce::jlimit(static_cast<int>(EngineMode::Sampler),
                                 static_cast<int>(EngineMode::Freeze),
                                 engineModeRaw);
    bp.engineIsWavetable = (bp.engineMode == EngineMode::Wavetable);
    bp.engineIsFreeze = (bp.engineMode == EngineMode::Freeze);
    switch (bp.engineMode)
    {
        case EngineMode::Wavetable:
            voiceManager.setEngineMode(SynthVoice::EngineMode::Wavetable);
            break;
        case EngineMode::Freeze:
            voiceManager.setEngineMode(SynthVoice::EngineMode::Freeze);
            break;
        case EngineMode::Sampler:
        default:
            voiceManager.setEngineMode(SynthVoice::EngineMode::Sampler);
            break;
    }

    // Master volume (dB → linear)
    float masterDb = paramCache.masterVol->load();
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

    // Give noteOn/noteOff access to the current envelope/modulation block state.
    voiceManager.setBlockParams(bp);

    // ── Sequencer / Arpeggiator (in series: Seq → Arp → synth) ─────────────
    // (seqRunning already read above for idle detection)
    float seqBpm = paramCache.seqBpm->load();
    int seqSteps = static_cast<int>(paramCache.seqSteps->load());
    int seqOctaveShift = static_cast<int>(paramCache.seqOctave->load()) - 2;
    float seqGate = paramCache.seqGate->load();
    float seqShuffle = paramCache.seqShuffle->load();
    int seqPreset = static_cast<int>(paramCache.seqPreset->load());
    int arpModeRaw = static_cast<int>(paramCache.arpMode->load());
    bool arpEnabled = arpModeRaw > 0;
    int arpMode = arpModeRaw > 0 ? arpModeRaw - 1 : 0; // 0=Up,1=Down,2=UpDown,3=Random
    int arpRate = static_cast<int>(paramCache.arpRate->load());
    int arpOctaves = static_cast<int>(paramCache.arpOctaves->load());

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
    const int prevSeqPreset = lastSeqPreset.load(std::memory_order_relaxed);
    if (seqPreset != prevSeqPreset)
    {
        stepSequencer.loadPreset(seqPreset);
        lastSeqPreset.store(seqPreset, std::memory_order_relaxed);

        // Adopt the preset's length into the seqSteps param on a genuine preset
        // application — fresh-startup default OR a runtime dropdown change — but
        // NOT on the first apply after a DAW state-restore. getStateInformation
        // persists seqPreset and seqSteps independently, so a restored session
        // can legitimately hold a hand-set step count (e.g. 24); forcing the
        // preset's natural length there would silently destroy the saved count on
        // every reload. setStateInformation sets seqStateRestored, which we
        // consume exactly once here: true → restore, skip the push (and clear the
        // flag so the *next* genuine change pushes again); false → fresh start or
        // runtime change, adopt the preset length. (The earlier prevSeqPreset>=0
        // test conflated fresh-startup with restore — both land at -1 — which was
        // harmless while every preset was 16 = the seqSteps default, but a 32-step
        // preset must push its length at startup or it'd be truncated back to 16.)
        // The push sticks because the non-GEN branch re-asserts setNumSteps(
        // seqSteps) every block and the GUI reads this param, not the sequencer;
        // we update the local copy so this block already uses it.
        if (! seqStateRestored.exchange(false, std::memory_order_relaxed))
        {
            seqSteps = stepSequencer.getNumSteps();
            if (auto* par = parameters.getParameter(PID::seqSteps))
                par->setValueNotifyingHost(par->convertTo0to1(static_cast<float>(seqSteps)));
        }
    }

    // GEN mode toggle — PLAY is master transport, GEN switches engine
    bool genModeWanted = paramCache.genSeqRunning->load() > 0.5f;
    int seqDivision = static_cast<int>(paramCache.seqDivision->load());

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
                stopSequencerOneShots();
                stepSequencer.stop();
                generativeSequencer.setBpm(static_cast<double>(seqBpm));
                generativeSequencer.setDivision(seqDivision);
                generativeSequencer.setGate(seqGate);
                generativeSequencer.setShuffle(seqShuffle);
                generativeSequencer.setScale(
                    static_cast<int>(paramCache.scaleType->load()),
                    static_cast<int>(paramCache.scaleRoot->load()));
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
        bool fxS = paramCache.genFixSteps->load() > 0.5f;
        bool fxP = paramCache.genFixPulses->load() > 0.5f;
        bool fxR = paramCache.genFixRotation->load() > 0.5f;
        bool fxM = paramCache.genFixMutation->load() > 0.5f;
        generativeSequencer.setFixSteps(fxS);
        generativeSequencer.setFixPulses(fxP);
        generativeSequencer.setFixRotation(fxR);
        generativeSequencer.setFixMutation(fxM);

        // Steps/Pulses/Rotation: if FIXED, overwrite every block.
        // If UNFIXED, only overwrite when user changes the slider.
        {
            int gs = static_cast<int>(paramCache.genSteps->load());
            int gp = static_cast<int>(paramCache.genPulses->load());
            int gr = static_cast<int>(paramCache.genRotation->load());

            if (fxS || gs != lastGenSteps)    { generativeSequencer.setSteps(gs);    lastGenSteps = gs; }
            if (fxP || gp != lastGenPulses)   { generativeSequencer.setPulses(gp);   lastGenPulses = gp; }
            if (fxR || gr != lastGenRotation) { generativeSequencer.setRotation(gr); lastGenRotation = gr; }
        }

        // Mutation: always update base; only overwrite effective rate if fixed or user changed slider
        {
            float gm = paramCache.genMutation->load();
            generativeSequencer.setBaseMutation(gm);
            if (fxM || gm != lastGenMutation) { generativeSequencer.setMutation(gm); lastGenMutation = gm; }
        }

        generativeSequencer.setRange(static_cast<int>(paramCache.genRange->load()) + 1);
        generativeSequencer.setScale(
            static_cast<int>(paramCache.scaleType->load()),
            static_cast<int>(paramCache.scaleRoot->load()));

        // ── Shared pitch-field setters ──
        generativeSequencer.setFieldMode(static_cast<int>(
            paramCache.genFieldMode->load()));
        generativeSequencer.setFieldChangeRate(static_cast<int>(
            paramCache.genFieldRate->load()));
        // Field Center PC follows Scale Root — they are conceptually the
        // same anchor, and a separate "second tonic" dropdown was confusing.
        const int scaleRootForField = static_cast<int>(
            paramCache.scaleRoot->load());
        generativeSequencer.setFieldCenterPc(scaleRootForField);

        // Field Pivot interval is derived from Scale Type — its musical
        // character chooses one of three universal pivots: P5 for major
        // flavours, m3 for minor flavours, Tri for symmetric / sharp-fourth
        // scales. Pivot mode then transposes the pc-set by this interval.
        {
            const int scaleTypeForField = static_cast<int>(
                paramCache.scaleType->load());
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
            paramCache.genCoordinationMode->load()));
        generativeSequencer.setCoordinationCap(static_cast<int>(
            paramCache.genCoordinationCap->load()));

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
        int seqOctaveIdx = static_cast<int>(paramCache.seqOctave->load());
        stepSequencer.setOctaveShiftSemitones((seqOctaveIdx - 2) * 12);

        if (seqRunning)
            stepSequencer.start();
        else
            stepSequencer.stop();
        stepSequencer.processBlock(buffer, midiMessages);
    }

    // Consume bar-start flag (sequencer display + sync-arm release)
    if (stepSequencer.barStartFlag.exchange(false))
    {
        barBoundaryFlag.store(true, std::memory_order_relaxed);

        // Release any sync-armed Drift/LFO on the downbeat: they have been held
        // silent at phase 0 since the Off→Sync switch, so from here their cycle
        // runs locked to the "1". (Released here, right after the step seq sets
        // barStartFlag for the bar it just crossed.)
        LFO* const lfoPtr[3] = { &lfo1, &lfo2, &lfo3 };
        for (int i = 0; i < 3; ++i)
        {
            if (lfoSyncArmed[i])   { lfoSyncArmed[i]   = false; lfoPtr[i]->setArmed(false); }
            if (driftSyncArmed[i]) { driftSyncArmed[i] = false; driftLfo.setLfoArmed(i, false); }
        }
    }

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
            //   step-seq active → channel 1 (normal), 2 (bind) or 8 (glide)
            // When seq is stopped (manual keyboard play) any channel feeds
            // the arp, preserving the historical external-input behaviour.
            const int ch = msg.getChannel();
            const bool isLead = !seqRunning
                              || (genModeActiveInAudio
                                  ? (ch == 3)
                                  : (ch == T5ynthStepSequencer::kNormalChannel
                                  || ch == T5ynthStepSequencer::kBindChannel
                                  || ch == T5ynthStepSequencer::kGlideChannel));

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
    if (voiceManager.hasActiveVoices() || hasActiveSequencerOneShots() || !midiMessages.isEmpty())
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
        float baseDrift1Depth = paramCache.drift1Depth->load();
        float baseDrift2Depth = paramCache.drift2Depth->load();
        float baseDrift3Depth = paramCache.drift3Depth->load();

        // Re-prepare runs on samplerReprepareThread; the audio thread keeps
        // using the last published snapshot and only distributes it.
        if (masterSampler.hasAudio())
            voiceManager.distributeSamplerBuffer(masterSampler);
        if (masterFreeze.hasAudio())
            // Audio-thread per-block redistribution → allowMorph MUST be false
            // (morphToBufferFrom may free a retired snapshot off-thread). The
            // generation guard makes this a no-op when the buffer is unchanged.
            voiceManager.distributeFreezeBuffer(masterFreeze, 0.0f, false);
        if (masterOsc.hasFrames() && generatedAudioFull.getNumSamples() > 0)
        {
            syncWavetableTraversal(generatedSampleRate, generatedAudioFull.getNumSamples());
            masterOsc.setMorphTimeMs(paramCache.driftCrossfade->load());
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

        // Scan → P1 modulation offset (Sampler mode only: retrigger uses it).
        // Granular reads Scan directly inside the voice as its held position.
        if (bp.engineMode == EngineMode::Sampler)
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
                        // Step-seq bind-mode channels: kBindChannel = instant pitch
                        // change, kGlideChannel = ramped. Both take the no-retrigger
                        // glide path; they differ only in the glide time below.
                        const bool isBind = (channel == T5ynthStepSequencer::kBindChannel
                                          || channel == T5ynthStepSequencer::kGlideChannel);
                        const int genSourceId = (channel >= 3 && channel <= 6) ? channel - 3 : -1;
                        const float sourcePan = genSourceId >= 0
                                              ? genStrandPan[static_cast<size_t>(genSourceId)]
                                              : 0.0f;

                        lastMidiNote.store(note, std::memory_order_relaxed);
                        lastMidiVelocity.store(juce::roundToInt(velocity * 127.0f),
                                               std::memory_order_relaxed);
                        lastMidiNoteOn.store(true, std::memory_order_relaxed);

                        // Bind = instant (glideMs 0); Glide + everything else =
                        // getGlideTime() (the non-bind value still feeds mono legato).
                        voiceManager.noteOn(note, velocity, isBind,
                            channel == T5ynthStepSequencer::kBindChannel
                                ? 0.0f : stepSequencer.getGlideTime(),
                            lfo1TrigMode, lfo2TrigMode, lfo3TrigMode,
                            genSourceId, sourcePan, channel);
                    }
                    else if (msg.isNoteOff())
                    {
                        const int channel = msg.getChannel();
                        const int genSourceId = (channel >= 3 && channel <= 6) ? channel - 3 : -1;
                        voiceManager.noteOff(msg.getNoteNumber(), genSourceId);
                        if (!voiceManager.hasActiveVoices())
                            lastMidiNoteOn.store(false, std::memory_order_relaxed);
                    }
                    else if (msg.isAftertouch())
                    {
                        const int channel = msg.getChannel();
                        const int genSourceId = (channel >= 3 && channel <= 6) ? channel - 3 : -1;
                        const float pressure = static_cast<float>(msg.getAfterTouchValue()) / 127.0f;
                        voiceManager.setPolyPressure(msg.getNoteNumber(), pressure, genSourceId);
                    }
                    else if (msg.isChannelPressure())
                    {
                        const float pressure = static_cast<float>(msg.getChannelPressureValue()) / 127.0f;
                        voiceManager.setChannelPressure(pressure);
                    }
                    else if (msg.isPitchWheel())
                    {
                        const int pbChannel = msg.getChannel();
                        const float centered = (static_cast<float>(msg.getPitchWheelValue()) - 8192.0f) / 8192.0f;
                        // Ch1 = MPE master / standard MIDI global bend.
                        // Ch2-16 = MPE per-note bend; VoiceManager only forwards to voices
                        // that were tagged with that channel on noteOn (external notes only —
                        // internal sequencer notes are tagged channel 0 and never match).
                        if (pbChannel == 1)
                        {
                            voiceManager.setPitchBendSemitones(
                                juce::jlimit(-1.0f, 1.0f, centered) * masterPitchBendRangeSemitones_);
                        }
                        else
                        {
                            voiceManager.setPerVoicePitchBend(pbChannel,
                                centered * notePitchBendRangeSemitones_);
                        }
                    }
                    else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                    {
                        voiceManager.allNotesOff();
                        lastMidiNoteOn.store(false, std::memory_order_relaxed);
                    }
                    else if (msg.isController())
                    {
                        const int cc = msg.getControllerNumber();
                        const int value7 = msg.getControllerValue();
                        // ── CC routing priority ──────────────────────────────────────
                        //  1. CC Learn: capture ANY incoming CC (incl. reserved) as target.
                        //  2. RPN state machine (pitch-bend range / MPE) + gen-seq strand pan:
                        //     internal/standard routing — kept ABOVE user bindings so a bound
                        //     CC6/CC10 can never shadow MPE bend-range or strand pan.
                        //  3. Explicit user binding (XL Map / CC Learn): wins over the built-in
                        //     GM performance CCs below — e.g. an XL fader on CC7/CC11 drives its
                        //     mapped param instead of channel-volume / expression.
                        //  4. Built-in GM / system CCs: fallback for unbound CCs.
                        if (midiLearnActive.load(std::memory_order_relaxed))
                        {
                            // CC Learn intercept: signal the message thread with the CC number.
                            midiLearnTargetCc.store(cc, std::memory_order_release);
                            triggerAsyncUpdate();
                        }
                        else if (cc == 101)
                        {
                            midiRpnMsb_ = value7;
                        }
                        else if (cc == 100
                                 && ! (dawModeActive_.load(std::memory_order_relaxed)
                                       && msg.getChannel() == 16))
                        {
                            // CC100 = RPN LSB for normal controllers. BUT in DAW mode the XL's
                            // Row-3 rightmost encoder (Drift3 Amt) transmits RELATIVE CC100 on
                            // ch16 (abs CC36 + 64). Without this guard it was swallowed here as
                            // RPN and never reached the relative-encoder handler → Drift3 Amt
                            // dead. Other channels keep working as RPN (keyboard pitch-bend range
                            // / MPE), so this only excludes the XL's own encoder channel.
                            midiRpnLsb_ = value7;
                        }
                        else if (cc == 6 && midiRpnMsb_ == 0 && midiRpnLsb_ == 0
                                 && ! (dawModeActive_.load(std::memory_order_relaxed)
                                       && msg.getChannel() == 16))
                        {
                            // RPN 0x0000 Data Entry MSB = semitones for pitch-bend range.
                            // Guarded off for the XL's ch16 CC6 fader (Resynth): when no RPN is in
                            // flight this branch would otherwise swallow the fader → Resynth dead.
                            // Ch1 sets the master (global) range; all other channels set the
                            // per-note MPE range. Value 0 is treated as 1 (1 semitone min).
                            const float rangeS = static_cast<float>(std::max(1, value7));
                            if (msg.getChannel() == 1)
                                masterPitchBendRangeSemitones_ = rangeS;
                            else
                                notePitchBendRangeSemitones_ = rangeS;
                        }
                        else if (cc == 10 && msg.getChannel() >= 3 && msg.getChannel() <= 6)
                        {
                            const int genSourceId = msg.getChannel() - 3;
                            const float value = static_cast<float>(value7) / 127.0f;
                            genStrandPan[static_cast<size_t>(genSourceId)] =
                                juce::jlimit(-1.0f, 1.0f, value * 2.0f - 1.0f);
                        }
                        else if (dawModeActive_.load(std::memory_order_relaxed)
                                 && msg.getChannel() == 1
                                 && ((cc >= 37 && cc <= 52) || cc == 116 || cc == 118))
                        {
                            // XL DAW-mode buttons (ch1): the two bottom rows CC 37-52 plus
                            // the left transport buttons Play ▶ = 116 / Record ● = 118
                            // (programmer's ref p.9). Act on press (value>=64); the release
                            // (0) is ignored. The device sends exactly one 127 per press, and
                            // transport toggles are de-duplicated by the atomic-request
                            // exchange in handleAsyncUpdate, so one press = one action with no
                            // per-button latch needed. Gated by dawModeActive_ so these CCs
                            // only act as buttons while the XL is driving us.
                            if (value7 >= 64)
                                handleXLButtonPress(cc);
                        }
                        else if (dawModeActive_.load(std::memory_order_relaxed)
                                 && msg.getChannel() == 16
                                 && cc >= 77 && cc <= 100)
                        {
                            // XL DAW-mode encoders in RELATIVE mode (endless encoders): the
                            // ch16 guard matches the manual (p.9 "Encoders and faders output on
                            // channel 16") and is consistent with the cc100/cc6 guards above — it
                            // stops a second controller's CC 77-100 from nudging XL-mapped params.
                            // value is a signed delta around 64, and the relative CC is the
                            // absolute CC + 64. Nudge the bound param from its CURRENT value
                            // (no absolute position → no jump). Same setValueNotifyingHost /
                            // ScopedTryLock path as the absolute fader binding below.
                            const int absCc = cc - 64;          // 77-100 → 13-36
                            const int delta = value7 - 64;      // signed: >0 CW, <0 CCW
                            if (delta != 0)
                            {
                                const juce::SpinLock::ScopedTryLockType tryLock(ccMappingLock_);
                                if (tryLock.isLocked())
                                {
                                    const auto& mapping = resolveCcMapping(absCc);
                                    if (mapping.param != nullptr)
                                    {
                                        // span is signed so an inverted binding keeps its direction.
                                        const float span = mapping.maxNorm - mapping.minNorm;
                                        // One detent is delta≈±1 → ±1/127≈0.008 normalized. For a
                                        // param with a fine interval AND low-end skew (e.g. Attack:
                                        // 0-5000 ms, interval 0.1 ms, skew 0.3) that step converts to
                                        // ~0.0005 ms and snaps back to 0 — so getValue() never advances
                                        // and the encoder is dead at its 0 default. Carry the unrealized
                                        // remainder across detents so it eventually crosses the snap.
                                        float& acc = relEncoderAccum_[static_cast<size_t>(absCc)];
                                        acc += static_cast<float>(delta) / 127.0f * span;
                                        const float cur  = mapping.param->getValue();
                                        const float next = juce::jlimit(0.0f, 1.0f, cur + acc);
                                        mapping.param->setValueNotifyingHost(next);
                                        if (next <= 0.0f || next >= 1.0f)
                                            acc = 0.0f;  // at a rail: drop remainder (no reversal dead-zone)
                                        else
                                            acc -= (mapping.param->getValue() - cur);  // keep sub-snap remainder
                                        const uint64_t seq =
                                            (midiTouchPacked_.load(std::memory_order_relaxed) >> 32) + 1;
                                        midiTouchPacked_.store(
                                            (seq << 32) | static_cast<uint32_t>(absCc),
                                            std::memory_order_release);
                                    }
                                }
                            }
                        }
                        else
                        {
                            // Explicit user binding wins over the built-in GM CCs.
                            // ScopedTryLockType: if the message thread holds the lock (writing
                            // a new binding) we skip the apply — the binding becomes visible on
                            // the next CC event — and fall through to the GM handling below.
                            bool boundHandled = false;
                            {
                                const juce::SpinLock::ScopedTryLockType tryLock(ccMappingLock_);
                                if (tryLock.isLocked())
                                {
                                    // param* and minNorm/maxNorm only — no String access.
                                    const auto& mapping = resolveCcMapping(cc);
                                    if (mapping.param != nullptr)
                                    {
                                        const float norm = juce::jmap(
                                            static_cast<float>(value7), 0.f, 127.f,
                                            mapping.minNorm, mapping.maxNorm);
                                        mapping.param->setValueNotifyingHost(
                                            juce::jlimit(0.0f, 1.0f, norm));
                                        // Record the touch so the editor can make the easy-mode
                                        // ENV/LFO/Drift tab follow this controller (cosmetic).
                                        // Pack (seq+1, cc) into one word — single writer here.
                                        const uint64_t seq =
                                            (midiTouchPacked_.load(std::memory_order_relaxed) >> 32) + 1;
                                        midiTouchPacked_.store(
                                            (seq << 32) | static_cast<uint32_t>(cc),
                                            std::memory_order_release);
                                        boundHandled = true;
                                    }
                                }
                            }

                            if (! boundHandled)
                            {
                                if (cc == 64)
                                {
                                    voiceManager.setSustainPedal(value7 >= 64);
                                }
                                else if (cc == 1)
                                {
                                    voiceManager.setModWheel(static_cast<float>(value7) / 127.0f);
                                }
                                else if (cc == 2)
                                {
                                    voiceManager.setBreathController(static_cast<float>(value7) / 127.0f);
                                }
                                else if (cc == 7)
                                {
                                    voiceManager.setChannelVolume(static_cast<float>(value7) / 127.0f);
                                }
                                else if (cc == 11)
                                {
                                    voiceManager.setExpression(static_cast<float>(value7) / 127.0f);
                                }
                                else if (cc == 66)
                                {
                                    voiceManager.setSostenutoPedal(value7 >= 64);
                                }
                                else if (cc == 67)
                                {
                                    voiceManager.setSoftPedal(value7 >= 64);
                                }
                                else if (cc == 120 || cc == 123)
                                {
                                    voiceManager.allNotesOff();
                                    lastMidiNoteOn.store(false, std::memory_order_relaxed);
                                }
                                else if (cc == 121)
                                {
                                    voiceManager.resetPerformanceControllers();
                                }
                            }
                        }
                    }
                    ++midiIter;
                }

                renderPos = subEnd;
            }
        }
        lastTriggeredNote = voiceOut.lastTriggeredNote;

        // Capture last LFO values for block-rate modulation + ghost display
        float lastAmpVal = voiceOut.lastAmpVal;
        float lastMod1Val = voiceOut.lastMod1Val;
        float lastMod2Val = voiceOut.lastMod2Val;
        float effectiveLfo1Depth = baseLfo1Depth;
        float effectiveLfo2Depth = baseLfo2Depth;
        float effectiveLfo3Depth = baseLfo3Depth;
        if (bp.ampTarget == EnvTarget::LFO1Depth) effectiveLfo1Depth = applyNormalizedOffset(effectiveLfo1Depth, lastAmpVal);
        if (bp.mod1Target == EnvTarget::LFO1Depth) effectiveLfo1Depth = applyNormalizedOffset(effectiveLfo1Depth, lastMod1Val);
        if (bp.mod2Target == EnvTarget::LFO1Depth) effectiveLfo1Depth = applyNormalizedOffset(effectiveLfo1Depth, lastMod2Val);
        if (bp.ampTarget == EnvTarget::LFO2Depth) effectiveLfo2Depth = applyNormalizedOffset(effectiveLfo2Depth, lastAmpVal);
        if (bp.mod1Target == EnvTarget::LFO2Depth) effectiveLfo2Depth = applyNormalizedOffset(effectiveLfo2Depth, lastMod1Val);
        if (bp.mod2Target == EnvTarget::LFO2Depth) effectiveLfo2Depth = applyNormalizedOffset(effectiveLfo2Depth, lastMod2Val);
        if (bp.ampTarget == EnvTarget::LFO3Depth) effectiveLfo3Depth = applyNormalizedOffset(effectiveLfo3Depth, lastAmpVal);
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

        // Filter is now per-voice (in SynthVoice::renderBlock)

        // ── Accumulate block-rate modulation for delay/reverb ─────────────────
        // (Pitch modulation is handled per-sample in SynthVoice::renderBlock)
        if (bp.ampTarget == EnvTarget::DelayTime)   modDelayTime += lastAmpVal;
        if (bp.ampTarget == EnvTarget::DelayFB)     modDelayFb += lastAmpVal;
        if (bp.ampTarget == EnvTarget::DelayMix)    modDelayMix += lastAmpVal;
        if (bp.ampTarget == EnvTarget::ReverbMix)   modReverbMix += lastAmpVal;
        if (bp.mod1Target == EnvTarget::DelayTime)  modDelayTime += lastMod1Val;
        if (bp.mod1Target == EnvTarget::DelayFB)    modDelayFb += lastMod1Val;
        if (bp.mod1Target == EnvTarget::DelayMix)   modDelayMix += lastMod1Val;
        if (bp.mod1Target == EnvTarget::ReverbMix)  modReverbMix += lastMod1Val;
        if (bp.mod2Target == EnvTarget::DelayTime)  modDelayTime += lastMod2Val;
        if (bp.mod2Target == EnvTarget::DelayFB)    modDelayFb += lastMod2Val;
        if (bp.mod2Target == EnvTarget::DelayMix)   modDelayMix += lastMod2Val;
        if (bp.mod2Target == EnvTarget::ReverbMix)  modReverbMix += lastMod2Val;

        // Env → LFO modulation
        if (bp.ampTarget == EnvTarget::LFO1Rate)    lfo1.setRate(baseLfo1Rate * (1.0f + lastAmpVal));
        if (bp.ampTarget == EnvTarget::LFO1Depth)   effectiveLfo1Depth = applyNormalizedOffset(effectiveLfo1Depth, lastAmpVal);
        if (bp.ampTarget == EnvTarget::LFO2Rate)    lfo2.setRate(baseLfo2Rate * (1.0f + lastAmpVal));
        if (bp.ampTarget == EnvTarget::LFO2Depth)   effectiveLfo2Depth = applyNormalizedOffset(effectiveLfo2Depth, lastAmpVal);
        if (bp.ampTarget == EnvTarget::LFO3Rate)    lfo3.setRate(baseLfo3Rate * (1.0f + lastAmpVal));
        if (bp.ampTarget == EnvTarget::LFO3Depth)   effectiveLfo3Depth = applyNormalizedOffset(effectiveLfo3Depth, lastAmpVal);
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

    renderSequencerOneShots(buffer);

    // ── Effects (parallel send-bus: dry + delay + reverb → limiter) ───────
    int delayType = static_cast<int>(paramCache.delayType->load());
    bool delayEnabled = delayType > 0;
    int reverbType = static_cast<int>(paramCache.reverbType->load());
    bool reverbEnabled = reverbType > 0;
    bool reverbIsAlgo = reverbType == 4;

    if (delayEnabled)
    {
        const int delayClock = static_cast<int>(paramCache.delayClockMode->load());
        const float baseDelayTime = (delayClock == ClockMode::Off)
            ? paramCache.delayTime->load()
            : ClockSync::computeDelayMs(syncBpm,
                static_cast<int>(paramCache.delayClockDivision->load()));
        float baseDelayFb = paramCache.delayFeedback->load();
        float baseDelayMix = paramCache.delayMix->load();
        // Apply modulation offsets to delay params
        delay.setTime(juce::jlimit(1.0f, 5000.0f, baseDelayTime * (1.0f + modDelayTime)));
        delay.setFeedback(juce::jlimit(0.0f, 0.95f, baseDelayFb + modDelayFb));
        delay.setMix(juce::jlimit(0.0f, 1.0f, baseDelayMix + modDelayMix));
        delay.setDamp(paramCache.delayDamp->load());
        delay.setMode(delayType);
    }

    if (reverbEnabled)
    {
        if (reverbIsAlgo)
        {
            algoReverb.setRoomSize(paramCache.algoRoom->load());
            algoReverb.setDamping(paramCache.algoDamping->load());
            algoReverb.setWidth(paramCache.algoWidth->load());
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
            paramCache.reverbMix->load() + modReverbMix);
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
            paramCache.reverbMix->load() + modReverbMix);
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
            bool envModFilter = (bp.ampTarget == EnvTarget::Filter
                                 || bp.mod1Target == EnvTarget::Filter || bp.mod2Target == EnvTarget::Filter
                                 || bp.kbdTrack > 0.0f) && hasVoices;
            bool aftertouchModFilter = bp.filterEnabled
                                    && bp.aftertouchTarget == AftertouchTarget::Cutoff && hasVoices;

            if (hasVoices && (lfoModFilter || envModFilter || aftertouchModFilter))
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

        // Filter resonance ghost
        {
            const bool aftertouchModResonance = bp.filterEnabled
                                             && bp.aftertouchTarget == AftertouchTarget::Resonance && hasVoices;
            modulatedValues.filterResonance.store(aftertouchModResonance
                ? voiceOut.lastModulatedResonance
                : NO_GHOST, std::memory_order_relaxed);
        }

        // Scan ghost
        {
            bool lfoModScan = bp.lfo1Target == LfoTarget::Scan || bp.lfo2Target == LfoTarget::Scan
                              || bp.lfo3Target == LfoTarget::Scan;
            bool envModScan = bp.ampTarget == EnvTarget::Scan
                           || bp.mod1Target == EnvTarget::Scan || bp.mod2Target == EnvTarget::Scan;
            bool driftModScan = std::abs(bp.driftScanOffset) > 0.001f && hasVoices;
            bool scanDrivenEngineActive = (bp.engineIsWavetable || bp.engineIsFreeze) && hasVoices;

            if (scanDrivenEngineActive)
            {
                modulatedValues.scanPosition.store(voiceOut.lastModulatedScan, std::memory_order_relaxed);
            }
            else if (hasVoices && (lfoModScan || envModScan || driftModScan))
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
            bool envModNoise = bp.ampTarget == EnvTarget::NoiseLevel
                            || bp.mod1Target == EnvTarget::NoiseLevel || bp.mod2Target == EnvTarget::NoiseLevel;

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
            bool lfo1RateMod  = bp.ampTarget == EnvTarget::LFO1Rate
                              || bp.mod1Target == EnvTarget::LFO1Rate  || bp.mod2Target == EnvTarget::LFO1Rate;
            bool lfo1DepthMod = bp.ampTarget == EnvTarget::LFO1Depth
                              || bp.mod1Target == EnvTarget::LFO1Depth || bp.mod2Target == EnvTarget::LFO1Depth;
            modulatedValues.lfo1Rate.store(lfo1RateMod ? lfo1.getRate() : NO_GHOST, std::memory_order_relaxed);
            float ghostLfo1Depth = bp.lfo1Depth;
            if (bp.ampTarget == EnvTarget::LFO1Depth) ghostLfo1Depth = applyNormalizedOffset(ghostLfo1Depth, voiceOut.lastAmpVal);
            if (bp.mod1Target == EnvTarget::LFO1Depth) ghostLfo1Depth = applyNormalizedOffset(ghostLfo1Depth, voiceOut.lastMod1Val);
            if (bp.mod2Target == EnvTarget::LFO1Depth) ghostLfo1Depth = applyNormalizedOffset(ghostLfo1Depth, voiceOut.lastMod2Val);
            modulatedValues.lfo1Depth.store(lfo1DepthMod ? ghostLfo1Depth : NO_GHOST, std::memory_order_relaxed);

            bool lfo2RateMod  = bp.ampTarget == EnvTarget::LFO2Rate
                              || bp.mod1Target == EnvTarget::LFO2Rate  || bp.mod2Target == EnvTarget::LFO2Rate;
            bool lfo2DepthMod = bp.ampTarget == EnvTarget::LFO2Depth
                              || bp.mod1Target == EnvTarget::LFO2Depth || bp.mod2Target == EnvTarget::LFO2Depth;
            modulatedValues.lfo2Rate.store(lfo2RateMod ? lfo2.getRate() : NO_GHOST, std::memory_order_relaxed);
            float ghostLfo2Depth = bp.lfo2Depth;
            if (bp.ampTarget == EnvTarget::LFO2Depth) ghostLfo2Depth = applyNormalizedOffset(ghostLfo2Depth, voiceOut.lastAmpVal);
            if (bp.mod1Target == EnvTarget::LFO2Depth) ghostLfo2Depth = applyNormalizedOffset(ghostLfo2Depth, voiceOut.lastMod1Val);
            if (bp.mod2Target == EnvTarget::LFO2Depth) ghostLfo2Depth = applyNormalizedOffset(ghostLfo2Depth, voiceOut.lastMod2Val);
            modulatedValues.lfo2Depth.store(lfo2DepthMod ? ghostLfo2Depth : NO_GHOST, std::memory_order_relaxed);

            bool lfo3RateMod  = bp.ampTarget == EnvTarget::LFO3Rate
                              || bp.mod1Target == EnvTarget::LFO3Rate  || bp.mod2Target == EnvTarget::LFO3Rate;
            bool lfo3DepthMod = bp.ampTarget == EnvTarget::LFO3Depth
                              || bp.mod1Target == EnvTarget::LFO3Depth || bp.mod2Target == EnvTarget::LFO3Depth;
            modulatedValues.lfo3Rate.store(lfo3RateMod ? lfo3.getRate() : NO_GHOST, std::memory_order_relaxed);
            float ghostLfo3Depth = bp.lfo3Depth;
            if (bp.ampTarget == EnvTarget::LFO3Depth) ghostLfo3Depth = applyNormalizedOffset(ghostLfo3Depth, voiceOut.lastAmpVal);
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
            const int delayClockGhost = static_cast<int>(paramCache.delayClockMode->load());
            const float delayBaseGhost = (delayClockGhost == ClockMode::Off)
                ? paramCache.delayTime->load()
                : ClockSync::computeDelayMs(syncBpm,
                    static_cast<int>(paramCache.delayClockDivision->load()));
            modulatedValues.delayTime.store(dlyTimeMod && delayEnabled
                ? juce::jlimit(1.0f, 2000.0f, delayBaseGhost * (1.0f + modDelayTime))
                : NO_GHOST, std::memory_order_relaxed);
            modulatedValues.delayFeedback.store(dlyFbMod && delayEnabled
                ? juce::jlimit(0.0f, 0.95f, paramCache.delayFeedback->load() * (1.0f + modDelayFb))
                : NO_GHOST, std::memory_order_relaxed);
            modulatedValues.delayMix.store(dlyMixMod && delayEnabled
                ? juce::jlimit(0.0f, 1.0f, paramCache.delayMix->load() + modDelayMix)
                : NO_GHOST, std::memory_order_relaxed);
            modulatedValues.reverbMix.store(revMixMod && reverbEnabled
                ? juce::jlimit(0.0f, 1.0f, paramCache.reverbMix->load() + modReverbMix)
                : NO_GHOST, std::memory_order_relaxed);
        }
    }

    // ── Master volume ───────────────────────────────────────────────────────
    buffer.applyGain(masterGain);

    // ── Limiter (always on, internal safety) ────────────────────────────────
    limiter.setThreshold(paramCache.limiterThresh->load());
    limiter.setRelease(paramCache.limiterRelease->load());
    limiter.processBlock(buffer);

    midiClockBlockStart_ += static_cast<uint64_t>(numSamples);
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
    if (paramCache.wtAutoScan->load() > 0.5f)
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
    driftLfo.setRegenMode(static_cast<int>(paramCache.driftRegen->load()));

    const int d1t = static_cast<int>(paramCache.drift1Target->load());
    const int d2t = static_cast<int>(paramCache.drift2Target->load());
    const int d3t = static_cast<int>(paramCache.drift3Target->load());

    bool driftHasTarget = (d1t != 0) || (d2t != 0) || (d3t != 0);
    bool hasOsc = false;
    for (int t : { d1t, d2t, d3t })
        if ((t >= DriftLFO::TgtAlpha && t <= DriftLFO::TgtAxis3)
            || t == DriftLFO::TgtNoise || t == DriftLFO::TgtMagnitude
            || t == DriftLFO::TgtResynth)
            hasOsc = true;

    driftHasOscTarget.store(hasOsc, std::memory_order_relaxed);
    driftRegenMode.store(static_cast<int>(paramCache.driftRegen->load()),
                         std::memory_order_relaxed);
    driftRegenBpm.store(syncBpm, std::memory_order_relaxed);

    bool driftManualEnable = paramCache.driftEnabled->load() > 0.5f;
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
    driftLfo.setLfoDepth(0, paramCache.drift1Depth->load());
    driftLfo.setLfoTarget(0, d1t);
    driftLfo.setLfoWaveform(0, static_cast<int>(paramCache.drift1Wave->load()));
    driftLfo.setLfoRate(1, driftRate(PID::drift2ClockMode, PID::drift2ClockDivision, PID::drift2Rate));
    driftLfo.setLfoDepth(1, paramCache.drift2Depth->load());
    driftLfo.setLfoTarget(1, d2t);
    driftLfo.setLfoWaveform(1, static_cast<int>(paramCache.drift2Wave->load()));
    driftLfo.setLfoRate(2, driftRate(PID::drift3ClockMode, PID::drift3ClockDivision, PID::drift3Rate));
    driftLfo.setLfoDepth(2, paramCache.drift3Depth->load());
    driftLfo.setLfoTarget(2, d3t);
    driftLfo.setLfoWaveform(2, static_cast<int>(paramCache.drift3Wave->load()));
    driftLfo.tick(static_cast<double>(numSamples) / getSampleRate());

    static constexpr float NO_GHOST = std::numeric_limits<float>::quiet_NaN();
    const float alphaOff = driftLfo.getOffsetForTarget(DriftLFO::TgtAlpha);
    const float ax1Off   = driftLfo.getOffsetForTarget(DriftLFO::TgtAxis1);
    const float ax2Off   = driftLfo.getOffsetForTarget(DriftLFO::TgtAxis2);
    const float ax3Off   = driftLfo.getOffsetForTarget(DriftLFO::TgtAxis3);
    const float noiseOff = driftLfo.getOffsetForTarget(DriftLFO::TgtNoise);
    const float magOff   = driftLfo.getOffsetForTarget(DriftLFO::TgtMagnitude);
    const float resynthOff = driftLfo.getOffsetForTarget(DriftLFO::TgtResynth);
    const float baseAlpha = paramCache.genAlpha->load();
    const float baseNoise = paramCache.genNoise->load();
    const float baseMag = paramCache.genMagnitude->load();
    const float baseResynth = paramCache.resynthAmount->load();

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
    modulatedValues.driftResynth.store(
        std::abs(resynthOff) > 0.001f ? juce::jlimit(0.0f, 1.0f, baseResynth + resynthOff)
                                      : NO_GHOST,
        std::memory_order_relaxed);
}

bool T5ynthProcessor::seqRunningNow() const
{
    const bool stepOn = paramCache.seqRunning->load() > 0.5f;
    const bool genOn  = paramCache.genSeqRunning->load() > 0.5f;
    return stepOn || genOn;
}

float T5ynthProcessor::resolveSyncBpm() const
{
    if (midiClockEnabled_.load(std::memory_order_relaxed)
        && midiClockValid_.load(std::memory_order_relaxed))
        return midiClockBpm_.load(std::memory_order_relaxed);
    if (hostPlayingNow.load(std::memory_order_relaxed))
        return hostBpmLastSeen.load(std::memory_order_relaxed);
    if (seqRunningNow())
        return paramCache.seqBpm->load();
    const float h = hostBpmLastSeen.load(std::memory_order_relaxed);
    return (h > 0.0f) ? h : paramCache.seqBpm->load();
}

bool  T5ynthProcessor::isMidiClockActive()  const noexcept
{
    return midiClockEnabled_.load(std::memory_order_relaxed)
        && midiClockValid_.load(std::memory_order_relaxed);
}
float T5ynthProcessor::getMidiClockBpm()    const noexcept { return midiClockBpm_.load(std::memory_order_relaxed); }
bool  T5ynthProcessor::isMidiClockEnabled() const noexcept { return midiClockEnabled_.load(std::memory_order_relaxed); }

void T5ynthProcessor::setMidiClockEnabled(bool e)
{
    // Only flip the atomics here — called from message thread.
    // midiClockTickCount_ / midiClockLastTick_ are audio-thread-only;
    // the audio thread resets them when it first sees enabled=false.
    midiClockEnabled_.store(e, std::memory_order_release);
    if (!e)
        midiClockValid_.store(false, std::memory_order_release);
}

bool T5ynthProcessor::isWavetableMode() const
{
    return static_cast<int>(paramCache.engineMode->load()) == EngineMode::Wavetable;
}

bool T5ynthProcessor::isFreezeMode() const
{
    return static_cast<int>(paramCache.engineMode->load()) == EngineMode::Freeze;
}

bool T5ynthProcessor::isSamplerMode() const
{
    return static_cast<int>(paramCache.engineMode->load()) == EngineMode::Sampler;
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
    bool hfOn = paramCache.genHfBoost->load() > 0.5f;
    if (hfOn)
        applyHfBoost(cleanBuffer, sr);

    // Pre-trim leading silence BEFORE computing activeStartFrac/activeEndFrac.
    // prepareBufferLoad() also trims internally; doing it here makes the trim
    // idempotent and ensures the fractions we compute below align with the
    // buffer the sampler ultimately plays. Without this the new sustained-RMS
    // trim would shift the buffer after we'd already measured an audible-start
    // fraction, landing P1 past the real attack.
    masterSampler.trimLeadingSilencePublic(cleanBuffer);

    // Symmetric trailing trim: diffusion models emit the full requested duration
    // even when the sound is short, leaving a dead near-silent tail. The granular
    // engine (scan 0..1 across the whole buffer, no playhead) otherwise parks in
    // that pure-zero field, and the waveform/playhead show a flat tail. Drop it
    // here — before the active-region fractions below are computed — so sampler,
    // wavetable, freeze and the display all end at real content. No-op when the
    // content already runs to the end.
    masterSampler.trimTrailingSilencePublic(cleanBuffer);

    const auto& feedBuffer = cleanBuffer;

    SamplePlayer::LoopMode samplerLoopMode = SamplePlayer::LoopMode::Loop;
    SamplePlayer::PrepareConfig samplerConfig;
    bool autoPositionPoints = false;
    float prevP1 = 0.0f;
    {
        const juce::ScopedLock sl (getCallbackLock());
        syncSamplerSettingsFromParametersLocked();
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

            // Sustained windowed-RMS detection. The previous peak-per-window
            // algorithm tripped on isolated noise samples — VAE/decoder
            // artefacts in the -45..-55 dB band would anchor the start point
            // long before the actual content began. We now require ≥3
            // consecutive ~10 ms windows of meaningful RMS energy.
            const int windowSize = juce::jmax(64, static_cast<int>(std::round(sr * 0.010)));
            const int numWindows = numSamples / windowSize;
            constexpr int minSustainedWindows = 3;

            int firstActive = 0;
            int lastActive  = numSamples;

            if (numWindows >= minSustainedWindows)
            {
                std::vector<float> windowRms(static_cast<size_t>(numWindows), 0.0f);
                float globalPeakRms = 0.0f;
                for (int w = 0; w < numWindows; ++w)
                {
                    const float* base = data + w * windowSize;
                    double sumSq = 0.0;
                    for (int i = 0; i < windowSize; ++i)
                    {
                        double s = static_cast<double>(base[i]);
                        sumSq += s * s;
                    }
                    float rms = std::sqrt(static_cast<float>(sumSq / windowSize));
                    windowRms[static_cast<size_t>(w)] = rms;
                    globalPeakRms = std::max(globalPeakRms, rms);
                }

                // -35 dB from peak RMS captures musically present content while
                // rejecting low-level decoder hiss; floor at -50 dB absolute so
                // genuinely quiet generations don't collapse to threshold 0.
                const float relThreshold = globalPeakRms * 0.01778f; // -35 dB
                constexpr float absFloor = 0.00316f;                 // -50 dB
                const float threshold = std::max(relThreshold, absFloor);

                int run = 0;
                int firstRunStart = -1;
                int lastRunEnd    = -1;
                for (int w = 0; w < numWindows; ++w)
                {
                    if (windowRms[static_cast<size_t>(w)] > threshold)
                    {
                        ++run;
                        if (run >= minSustainedWindows)
                        {
                            if (firstRunStart < 0)
                                firstRunStart = w - minSustainedWindows + 1;
                            lastRunEnd = w + 1;
                        }
                    }
                    else
                    {
                        run = 0;
                    }
                }

                if (firstRunStart >= 0)
                {
                    firstActive = firstRunStart * windowSize;
                    lastActive  = juce::jmin(lastRunEnd * windowSize, numSamples);
                }
            }

            // Small margin (one window each side) — preserves natural attack/release
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
    int fcIdx = static_cast<int>(paramCache.wtFrames->load());
    int maxFrames = frameCounts[juce::jlimit(0, 3, fcIdx)];

    auto preparedSamplerLoad = masterSampler.prepareBufferLoad(feedBuffer, sr, samplerConfig);
    storeSamplerReprepareSource(preparedSamplerLoad.originalBuffer, sr, activeStartFrac, activeEndFrac);
    auto preparedFreezeBuffer = makeFreezeLoadBuffer(feedBuffer,
                                                     sr,
                                                     samplerConfig.normalizeOn,
                                                     activeStartFrac,
                                                     activeEndFrac,
                                                     masterSampler);
    juce::AudioBuffer<float> preparedGeneratedAudio;
    preparedGeneratedAudio.makeCopyOf(feedBuffer);
    juce::AudioBuffer<float> preparedWaveformSnapshot;
    const bool normDisplay = wavetableMode
        || (paramCache.normalize->load() > 0.5f);
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
    masterFreeze.loadBuffer(preparedFreezeBuffer, sr);

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
        masterOsc.setMorphTimeMs(paramCache.driftCrossfade->load());

        voiceManager.distributeSamplerBuffer(masterSampler);
        voiceManager.distributeWavetableFrames(masterOsc);
        // New inference → held granular voices crossfade-adopt it live (near
        // real-time), mirroring distributeWavetableFrames above. Off the audio
        // thread (under getCallbackLock), so morphToBufferFrom is RT-safe here.
        voiceManager.distributeFreezeBuffer(masterFreeze, paramCache.driftCrossfade->load(), true);

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
        syncSamplerSettingsFromParametersLocked();
        samplerConfig = masterSampler.capturePrepareConfig();
        wavetableMode = isWavetableMode();
    }
    auto preparedSamplerLoad = masterSampler.prepareBufferLoad(processed, generatedSampleRate, samplerConfig);
    storeSamplerReprepareSource(preparedSamplerLoad.originalBuffer, generatedSampleRate);
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
        int fcIdx = static_cast<int>(paramCache.wtFrames->load());
        int maxFrames = frameCounts[juce::jlimit(0, 3, fcIdx)];

        if (wavetableMode)
            masterOsc.extractFramesFromBuffer(preparedWaveformSnapshot, generatedSampleRate, start, end, maxFrames);
        else
            masterOsc.extractContiguousFrames(preparedWaveformSnapshot, generatedSampleRate, start, end);
    }
    auto preparedFreezeBuffer = makeFreezeLoadBuffer(processed,
                                                     generatedSampleRate,
                                                     samplerConfig.normalizeOn,
                                                     0.0f,
                                                     1.0f,
                                                     masterSampler);
    masterFreeze.loadBuffer(preparedFreezeBuffer, generatedSampleRate);
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
            masterOsc.setMorphTimeMs(paramCache.driftCrossfade->load());
            voiceManager.distributeWavetableFrames(masterOsc);
        }
        // Reprocessed audio (e.g. Rumble/HF/Normalize changed) → held granular
        // voices crossfade-adopt it live, like Wavetable above. Off the audio
        // thread (under getCallbackLock), so morphToBufferFrom is RT-safe here.
        voiceManager.distributeFreezeBuffer(masterFreeze, paramCache.driftCrossfade->load(), true);

        samplerProcessorDebugLog("reloadProcessedAudio end masterAfter={" + masterSampler.debugStateString() + "}");

        if (waveformSnapshot.getNumSamples() > 0)
        {
            newWaveformReady.store(true, std::memory_order_release);
        }
    }
}

void T5ynthProcessor::setInferenceCacheCapacity(int capacity)
{
    static constexpr int kAllowed[] = { 0, 2, 4, 8, 16, 32, 64 };
    int sanitized = 0;
    for (int allowed : kAllowed)
        if (capacity == allowed)
            sanitized = allowed;
    constexpr int maxAllowed = kAllowed[sizeof(kAllowed) / sizeof(kAllowed[0]) - 1];
    if (capacity > maxAllowed)
        sanitized = maxAllowed;

    if (sanitized == inferenceCacheCapacity)
        return;

    inferenceCacheCapacity = sanitized;
    clearInferenceCache();
}

void T5ynthProcessor::clearInferenceCache()
{
    inferenceCacheEntries.clear();
    inferenceCachePlaybackIndex = 0;
}

bool T5ynthProcessor::addInferenceCacheEntry(const juce::AudioBuffer<float>& buffer, double sampleRate)
{
    if (inferenceCacheCapacity <= 0
        || static_cast<int>(inferenceCacheEntries.size()) >= inferenceCacheCapacity
        || buffer.getNumSamples() <= 0
        || buffer.getNumChannels() <= 0)
        return false;

    InferenceCacheEntry entry;
    entry.audio.makeCopyOf(buffer);
    entry.sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    inferenceCacheEntries.push_back(std::move(entry));
    if (isInferenceCacheFull())
        inferenceCachePlaybackIndex = 0;
    return true;
}

bool T5ynthProcessor::playNextInferenceCacheEntry()
{
    if (!isInferenceCacheFull())
        return false;

    const int index = juce::jlimit(0, static_cast<int>(inferenceCacheEntries.size()) - 1,
                                   inferenceCachePlaybackIndex);
    const auto& entry = inferenceCacheEntries[static_cast<size_t>(index)];
    inferenceCachePlaybackIndex = (index + 1) % static_cast<int>(inferenceCacheEntries.size());
    loadGeneratedAudio(entry.audio, entry.sampleRate);
    return true;
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
        int fcIdx = static_cast<int>(paramCache.wtFrames->load());
        int maxFrames = frameCounts[juce::jlimit(0, 3, fcIdx)];

        if (isWavetableMode())
            masterOsc.extractFramesFromBuffer(waveformSnapshot, generatedSampleRate, start, end, maxFrames);
        else
            masterOsc.extractContiguousFrames(waveformSnapshot, generatedSampleRate, start, end);

        syncWavetableTraversal(generatedSampleRate, waveformSnapshot.getNumSamples());
        masterOsc.setMorphTimeMs(paramCache.driftCrossfade->load());
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
    xml->setAttribute("midiOutputDeviceId", midiOutputDeviceId_);
    xml->setAttribute("midiClockEnabled", midiClockEnabled_.load());
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

        // Per-stage velocity sensitivity (continuous signed) replaced the old
        // global velSens + discrete A/D/R vel modes. Convert a pre-redesign
        // session's params straight into the loaded tree so velocity response
        // survives the reload (same mapping as the .t5p preset migration):
        //   A/D/R velSens = sign(oldMode) * oldVelSens ;  Sustain velSens = oldVelSens.
        // Legacy IDs are gone from PID:: by design — match them as raw strings.
        {
            auto paramVal = [&](const juce::String& pid, float fallback) -> float {
                for (int i = 0; i < loadedTree.getNumChildren(); ++i)
                    if (loadedTree.getChild(i).getProperty("id").toString() == pid)
                        return static_cast<float>(loadedTree.getChild(i).getProperty("value"));
                return fallback;
            };
            auto appendParam = [&](const char* pid, float value) {
                juce::ValueTree node("PARAM");
                node.setProperty("id", pid, nullptr);
                node.setProperty("value", value, nullptr);
                loadedTree.appendChild(node, nullptr);
            };
            auto modeSign = [](float idx) -> float {
                const int m = juce::roundToInt(idx);
                return m == EnvVelTimeMode::Positive ?  1.0f
                     : m == EnvVelTimeMode::Negative ? -1.0f : 0.0f;
            };
            struct VsMig {
                const char* oldVelSens;
                const char* oldAtkMode; const char* oldDecMode; const char* oldRelMode;
                const char* newAtk; const char* newDec; const char* newSus; const char* newRel;
            };
            const VsMig vsMig[] = {
                { "amp_vel_sens",  "amp_attack_vel_mode",  "amp_decay_vel_mode",  "amp_release_vel_mode",
                  PID::ampAttackVelSens,  PID::ampDecayVelSens,  PID::ampSustainVelSens,  PID::ampReleaseVelSens },
                { "mod1_vel_sens", "mod1_attack_vel_mode", "mod1_decay_vel_mode", "mod1_release_vel_mode",
                  PID::mod1AttackVelSens, PID::mod1DecayVelSens, PID::mod1SustainVelSens, PID::mod1ReleaseVelSens },
                { "mod2_vel_sens", "mod2_attack_vel_mode", "mod2_decay_vel_mode", "mod2_release_vel_mode",
                  PID::mod2AttackVelSens, PID::mod2DecayVelSens, PID::mod2SustainVelSens, PID::mod2ReleaseVelSens },
            };
            for (const auto& m : vsMig)
            {
                // Only when genuinely pre-redesign: old global velSens present AND
                // the new per-stage params absent (don't clobber a new session).
                if (!hasParam(m.oldVelSens) || hasParam(m.newSus))
                    continue;
                const float vs = paramVal(m.oldVelSens, 1.0f);
                appendParam(m.newAtk, modeSign(paramVal(m.oldAtkMode, 0.0f)) * vs);
                appendParam(m.newDec, modeSign(paramVal(m.oldDecMode, 0.0f)) * vs);
                appendParam(m.newSus, vs);
                appendParam(m.newRel, modeSign(paramVal(m.oldRelMode, 0.0f)) * vs);
            }
        }

        parameters.replaceState(loadedTree);
    }

    // Never auto-start sequencers on session restore — no acoustic surprises
    parameters.getParameter(PID::seqRunning)->setValueNotifyingHost(0.0f);
    parameters.getParameter(PID::genSeqRunning)->setValueNotifyingHost(0.0f);

    // Same anti-surprise rule for the Re-Prompt loop, but ONLY on this DAW host-state
    // path: a non-Off stance with a non-Manual cadence is a self-running generation
    // driver (Manual no longer auto-runs it — pollDriftRegen early-outs there), the
    // moral equivalent of seqRunning=1. A host silently re-opening a project must not
    // spontaneously render, so force the stance Off here. (A preset LOAD, by contrast,
    // is a deliberate user gesture and DOES restore the stance — see importJsonPreset.)
    // The coupling is a passive mode and is left untouched.
    parameters.getParameter(PID::repromptStance)->setValueNotifyingHost(0.0f);

    // Treat the restored seqPreset as not-yet-applied so the next processBlock
    // reloads its canned pattern (the step pattern isn't part of the saved
    // state). seqStateRestored tells that apply to SUPPRESS the step-count push
    // this one time — the restored seqSteps is its own saved param and must
    // survive, even though the reload makes seqPreset look freshly changed.
    // (Without the flag the apply can't tell a restore from a fresh launch, and
    // a 32-step preset would clobber a hand-set count on every project load.)
    // Resetting lastSeqPreset to -1 (rather than leaving a stale value) also
    // covers hosts that reuse a live instance across project loads.
    lastSeqPreset.store(-1, std::memory_order_relaxed);
    seqStateRestored.store(true, std::memory_order_relaxed);

    // Restore MIDI output device (per-installation setting, not per-preset).
    {
        const auto deviceId = xml->getStringAttribute("midiOutputDeviceId");
        if (deviceId.isNotEmpty())
            openMidiOutputDevice(deviceId);

        if (xml->getBoolAttribute("midiClockEnabled", false))
            setMidiClockEnabled(true);
    }
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
    const char* amount; const char* loop; const char* target;
    const char* attackCurve; const char* decayCurve; const char* releaseCurve;
    const char* attackVelSens; const char* decayVelSens; const char* sustainVelSens; const char* releaseVelSens;
};
static constexpr EnvPIDs kEnvPIDs[] = {
    { PID::ampAttack, PID::ampDecay, PID::ampSustain, PID::ampRelease,
      PID::ampAmount, PID::ampLoop, PID::ampTarget,
      PID::ampAttackCurve, PID::ampDecayCurve, PID::ampReleaseCurve,
      PID::ampAttackVelSens, PID::ampDecayVelSens, PID::ampSustainVelSens, PID::ampReleaseVelSens },
    { PID::mod1Attack, PID::mod1Decay, PID::mod1Sustain, PID::mod1Release,
      PID::mod1Amount, PID::mod1Loop, PID::mod1Target,
      PID::mod1AttackCurve, PID::mod1DecayCurve, PID::mod1ReleaseCurve,
      PID::mod1AttackVelSens, PID::mod1DecayVelSens, PID::mod1SustainVelSens, PID::mod1ReleaseVelSens },
    { PID::mod2Attack, PID::mod2Decay, PID::mod2Sustain, PID::mod2Release,
      PID::mod2Amount, PID::mod2Loop, PID::mod2Target,
      PID::mod2AttackCurve, PID::mod2DecayCurve, PID::mod2ReleaseCurve,
      PID::mod2AttackVelSens, PID::mod2DecayVelSens, PID::mod2SustainVelSens, PID::mod2ReleaseVelSens },
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
    synth->setProperty("axesAmount", get(PID::genAxesAmount));
    synth->setProperty("resynth", get(PID::resynthAmount));
    // Re-Prompt (semantic loop): stance + coupling, saved as KEY strings so the
    // enum order can change without breaking presets. A preset saved before
    // Re-Prompt existed lacks both -> choiceFromKey("") -> 0 -> stance Off on load.
    synth->setProperty("repromptStance",
                       choiceToKey(static_cast<int>(get(PID::repromptStance)), RepromptStance::kEntries));
    synth->setProperty("repromptCoupling",
                       choiceToKey(static_cast<int>(get(PID::repromptCoupling)), RepromptCoupling::kEntries));
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
    // voiceCount and tuning are part of the engine config (polyphony + the
    // tuning table the VoiceManager applies). Both are in
    // MainPanel::kMainSnapshotParamIds so per-snapshot save round-trips
    // them, but they used to be omitted from the main preset JSON --
    // saving a preset with "12 voices / Maqam" would silently reset to
    // the APVTS defaults (8 voices / 12-TET) on reload.
    engine->setProperty("voiceCount", choiceToKey(static_cast<int>(get(PID::voiceCount)), VoiceCount::kEntries));
    engine->setProperty("tuning",     choiceToKey(static_cast<int>(get(PID::tuning)),     TuningType::kEntries));
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
        env->setProperty("target", envTargetToString(static_cast<int>(get(ep.target))));
        env->setProperty("loop", get(ep.loop) > 0.5f);
        env->setProperty("attackCurve", curveShapeToString(static_cast<int>(get(ep.attackCurve))));
        env->setProperty("decayCurve", curveShapeToString(static_cast<int>(get(ep.decayCurve))));
        env->setProperty("releaseCurve", curveShapeToString(static_cast<int>(get(ep.releaseCurve))));
        env->setProperty("attackVelSens", get(ep.attackVelSens));
        env->setProperty("decayVelSens", get(ep.decayVelSens));
        env->setProperty("sustainVelSens", get(ep.sustainVelSens));
        env->setProperty("releaseVelSens", get(ep.releaseVelSens));
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

    // MIDI aftertouch routing. Both params are in
    // MainPanel::kMainSnapshotParamIds so per-snapshot save persisted
    // them, but they were absent from the main preset JSON -- saving a
    // preset that routed aftertouch to (e.g.) Cutoff at 80% reset to
    // None/50% on reload. Same root cause and same fix shape as the
    // voiceCount / tuning case.
    juce::DynamicObject::Ptr aftertouch = new juce::DynamicObject();
    aftertouch->setProperty("target", choiceToKey(static_cast<int>(get(PID::aftertouchTarget)), AftertouchTarget::kEntries));
    aftertouch->setProperty("amount", get(PID::aftertouchAmount));
    modObj->setProperty("aftertouch", aftertouch.get());

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

    // Granular
    juce::DynamicObject::Ptr freeze = new juce::DynamicObject();
    freeze->setProperty("texture", choiceToKey(static_cast<int>(get(PID::freezeTexture)),
                                               FreezeTexture::kEntries));
    freeze->setProperty("stereo", get(PID::freezeStereo));
    root->setProperty("freeze", freeze.get());

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
        // bindMode is authoritative (0=off,1=bind,2=glide); keep the legacy "bind"
        // bool so older builds still load these presets as instant binds.
        s->setProperty("bindMode", static_cast<int>(step.bindMode));
        s->setProperty("bind", step.bindMode != T5ynthStepSequencer::BindMode::Off);
        juce::Array<juce::var> oneShots;
        for (int slot = 0; slot < T5ynthStepSequencer::ONE_SHOT_SLOTS; ++slot)
        {
            juce::DynamicObject::Ptr shot = new juce::DynamicObject();
            shot->setProperty("mode", static_cast<int>(step.oneShotModes[static_cast<size_t>(slot)]));
            shot->setProperty("hasSample", hasSequencerOneShotSample(i, slot));
            oneShots.add(shot.get());
        }
        s->setProperty("oneShots", oneShots);
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

    // CC bindings — sparse: only write entries that have a param assigned.
    // Old presets that lack this key default to no bindings on load.
    juce::Array<juce::var> ccArr;
    {
        const juce::SpinLock::ScopedLockType lock(ccMappingLock_);
        for (int cc = 0; cc < 128; ++cc)
        {
            const auto& m = ccMappings_[static_cast<size_t>(cc)];
            if (m.paramId.isEmpty()) continue;
            juce::DynamicObject::Ptr entry = new juce::DynamicObject();
            entry->setProperty("cc",    cc);
            entry->setProperty("param", m.paramId);
            entry->setProperty("min",   static_cast<double>(m.minNorm));
            entry->setProperty("max",   static_cast<double>(m.maxNorm));
            ccArr.add(entry.get());
        }
    }
    root->setProperty("ccMappings", ccArr);

    return juce::JSON::toString(root.get(), true);
}

bool T5ynthProcessor::importJsonPreset(const juce::String& json)
{
    auto parsed = juce::JSON::parse(json);
    if (!parsed.isObject()) return false;

    auto* root = parsed.getDynamicObject();
    if (!root) return false;
    const bool importingSequencePattern = root->getProperty("kind").toString() == "t5seq";

    // ── Synth params ──
    if (auto* synth = root->getProperty("synth").getDynamicObject())
    {
        setParam(parameters, PID::genAlpha, static_cast<float>(synth->getProperty("alpha")));
        setParam(parameters, PID::genMagnitude, static_cast<float>(synth->getProperty("magnitude")));
        setParam(parameters, PID::genNoise, static_cast<float>(synth->getProperty("noise")));
        if (synth->hasProperty("axesAmount"))
            setParam(parameters, PID::genAxesAmount, static_cast<float>(synth->getProperty("axesAmount")));
        // resynth default is 0 (off), so an unconditional read is correct: a preset
        // saved before Resynth existed lacks the property -> var() -> 0.0f -> the
        // Resynth slider resets to off on load, as a preset's full state should.
        setParam(parameters, PID::resynthAmount, static_cast<float>(synth->getProperty("resynth")));
        // Re-Prompt: restore BOTH the stance and the coupling — a preset is a full
        // patch, and a self-listening "machine" preset (a stance + a non-Manual
        // cadence + Resynth) cannot reproduce without the stance that drives the
        // prompt-rewriting loop. Forcing the stance Off here was the "Re-Prompt mode
        // is not saved" bug. Safety is preserved by CADENCE, not by nuking the stance:
        // Manual cadence never auto-runs a restored stance (pollDriftRegen early-outs
        // in Manual — the old "fires even in Manual" claim was fixed); a non-Manual
        // cadence DOES resume the loop on load, which is the whole point of loading
        // such a patch (and mirrors the Resynth loop, which already resumes on load).
        // loadPresetData resets the per-session loop runtime (loopEngaged_/seed) so the
        // restored stance re-captures the just-loaded prompts on its first step. The
        // DAW host-state path (setStateInformation) STILL forces the stance Off — a
        // host re-opening a project is a passive load that must not render unbidden.
        // An older .t5p missing the keys -> choiceFromKey("") -> 0 -> Off / B-only.
        setParam(parameters, PID::repromptStance, static_cast<float>(
                     choiceFromKey(synth->getProperty("repromptStance").toString(), RepromptStance::kEntries)));
        setParam(parameters, PID::repromptCoupling, static_cast<float>(
                     choiceFromKey(synth->getProperty("repromptCoupling").toString(), RepromptCoupling::kEntries)));
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
        // Old .t5p files predate voiceCount / tuning being saved; guard
        // with hasProperty so they keep loading with their previous
        // (now-default) polyphony and tuning instead of being rejected.
        if (engine->hasProperty("voiceCount"))
            setParam(parameters, PID::voiceCount,
                     static_cast<float>(choiceFromKey(engine->getProperty("voiceCount").toString(), VoiceCount::kEntries)));
        if (engine->hasProperty("tuning"))
            setParam(parameters, PID::tuning,
                     static_cast<float>(choiceFromKey(engine->getProperty("tuning").toString(), TuningType::kEntries)));
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
                // Velocity sensitivity. New format: 4 signed per-stage velSens.
                // Legacy format: a single "velSens" (0..1) + per-stage A/D/R vel
                // *modes* (off/+/-). Convert legacy → signed per-stage on load:
                //   A/D/R velSens = sign(mode) * velSens ;  Sustain velSens = velSens.
                if (env->hasProperty("sustainVelSens"))
                {
                    setParam(parameters, ep.attackVelSens,  static_cast<float>(env->getProperty("attackVelSens")));
                    setParam(parameters, ep.decayVelSens,   static_cast<float>(env->getProperty("decayVelSens")));
                    setParam(parameters, ep.sustainVelSens, static_cast<float>(env->getProperty("sustainVelSens")));
                    setParam(parameters, ep.releaseVelSens, static_cast<float>(env->getProperty("releaseVelSens")));
                }
                else if (env->hasProperty("velSens"))
                {
                    const float legacyVs = static_cast<float>(env->getProperty("velSens"));
                    auto signFromMode = [](int m) -> float {
                        return m == EnvVelTimeMode::Positive ?  1.0f
                             : m == EnvVelTimeMode::Negative ? -1.0f : 0.0f;
                    };
                    const int aMode = env->hasProperty("attackVelMode")
                        ? envVelTimeModeFromString(env->getProperty("attackVelMode").toString()) : EnvVelTimeMode::Off;
                    const int dMode = env->hasProperty("decayVelMode")
                        ? envVelTimeModeFromString(env->getProperty("decayVelMode").toString()) : EnvVelTimeMode::Off;
                    const int rMode = env->hasProperty("releaseVelMode")
                        ? envVelTimeModeFromString(env->getProperty("releaseVelMode").toString()) : EnvVelTimeMode::Off;
                    setParam(parameters, ep.attackVelSens,  signFromMode(aMode) * legacyVs);
                    setParam(parameters, ep.decayVelSens,   signFromMode(dMode) * legacyVs);
                    setParam(parameters, ep.sustainVelSens, legacyVs);
                    setParam(parameters, ep.releaseVelSens, signFromMode(rMode) * legacyVs);
                }
                if (env->hasProperty("attackCurve"))
                    setParam(parameters, ep.attackCurve,
                             static_cast<float>(curveShapeFromString(env->getProperty("attackCurve").toString())));
                if (env->hasProperty("decayCurve"))
                    setParam(parameters, ep.decayCurve,
                             static_cast<float>(curveShapeFromString(env->getProperty("decayCurve").toString())));
                if (env->hasProperty("releaseCurve"))
                    setParam(parameters, ep.releaseCurve,
                             static_cast<float>(curveShapeFromString(env->getProperty("releaseCurve").toString())));
                if (env->hasProperty("target"))
                    setParam(parameters, ep.target,
                             static_cast<float>(envTargetFromString(env->getProperty("target").toString())));
                else if (i == 0)
                    setParam(parameters, ep.target, static_cast<float>(EnvTarget::DCA));
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

        // MIDI aftertouch routing. Gated with hasProperty so .t5p files
        // written before this commit keep loading with their previous
        // (now-default) None / 0.5 routing instead of being rejected.
        if (auto* at = mod->getProperty("aftertouch").getDynamicObject())
        {
            if (at->hasProperty("target"))
                setParam(parameters, PID::aftertouchTarget,
                         static_cast<float>(choiceFromKey(at->getProperty("target").toString(), AftertouchTarget::kEntries)));
            if (at->hasProperty("amount"))
                setParam(parameters, PID::aftertouchAmount,
                         static_cast<float>(at->getProperty("amount")));
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
    if (auto* freeze = root->getProperty("freeze").getDynamicObject())
    {
        setParam(parameters, PID::freezeTexture,
                 static_cast<float>(choiceFromKey(freeze->getProperty("texture").toString(),
                                                  FreezeTexture::kEntries)));
        setParam(parameters, PID::freezeStereo, static_cast<float>(freeze->getProperty("stereo")));
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
        // Drive OS: absent in older presets -> current default 2x.
        setParam(parameters, PID::filterDriveOs,
                 filt->hasProperty("driveOs")
                     ? static_cast<float>(filterDriveOsFromString(filt->getProperty("driveOs").toString()))
                     : static_cast<float>(FilterDriveOs::X2));
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
        // We're about to write a custom step pattern straight into the
        // sequencer. Sync lastSeqPreset to the live dropdown value first, so the
        // audio-thread preset-apply (see processBlock) sees no pending change and
        // won't reload the canned preset over the pattern we just imported.
        lastSeqPreset.store(static_cast<int>(parameters.getRawParameterValue(PID::seqPreset)->load()),
                            std::memory_order_relaxed);

        // Preserve current seq_running state — don't stop playback on preset load
        // bool seqEnabled = seq->getProperty("enabled");
        // setParam(parameters, PID::seqRunning, seqEnabled ? 1.0f : 0.0f);
        setParam(parameters, PID::seqBpm, static_cast<float>(seq->getProperty("bpm")));
        int stepCount = static_cast<int>(seq->getProperty("stepCount"));
        setParam(parameters, PID::seqSteps, static_cast<float>(stepCount));
        stepSequencer.setNumSteps(stepCount);
        if (!importingSequencePattern)
            clearSequencerOneShotSamples();

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
                if (s->hasProperty("bindMode"))
                    stepSequencer.setStepBindMode(i, static_cast<T5ynthStepSequencer::BindMode>(
                        juce::jlimit(0, 2, static_cast<int>(s->getProperty("bindMode")))));
                else if (s->hasProperty("bind"))
                    stepSequencer.setStepBindMode(i, static_cast<bool>(s->getProperty("bind"))
                        ? T5ynthStepSequencer::BindMode::Bind : T5ynthStepSequencer::BindMode::Off);
                else if (s->hasProperty("glide"))  // ancient pre-rename presets meant a ramped glide
                    stepSequencer.setStepBindMode(i, static_cast<bool>(s->getProperty("glide"))
                        ? T5ynthStepSequencer::BindMode::Glide : T5ynthStepSequencer::BindMode::Off);
                if (!importingSequencePattern)
                {
                    if (auto* oneShots = s->getProperty("oneShots").getArray())
                    {
                        for (int slot = 0; slot < std::min(T5ynthStepSequencer::ONE_SHOT_SLOTS, oneShots->size()); ++slot)
                        {
                            auto* shot = (*oneShots)[slot].getDynamicObject();
                            if (shot == nullptr || !shot->hasProperty("mode"))
                                continue;

                            const int mode = juce::jlimit(0, 2, static_cast<int>(shot->getProperty("mode")));
                            stepSequencer.setStepOneShotMode(i, slot,
                                static_cast<T5ynthStepSequencer::OneShotMode>(mode));
                        }
                    }
                }
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

    // CC bindings — resolve entries outside the lock (getParameter is lock-free
    // but non-trivial), then hold the lock only for fill + field writes.
    // Matches the handleAsyncUpdate() pattern: never call getParameter under
    // the SpinLock; doing so lengthens the audio thread's contended window.
    {
        struct PendingEntry { int cc; CcMapping m; };
        std::vector<PendingEntry> pending;
        if (const auto* arr = root->getProperty("ccMappings").getArray())
        {
            for (const auto& entry : *arr)
            {
                auto* obj = entry.getDynamicObject();
                if (!obj) continue;
                const int cc = static_cast<int>(obj->getProperty("cc"));
                if (cc < 0 || cc >= 128) continue;
                juce::String pid = obj->getProperty("param").toString();
                if (pid.isEmpty()) continue;
                pid = LaunchControlXLLeds::migrateLegacyKnobParam(cc, pid);   // repair pre-fix swapped XL knob layout
                auto* param = parameters.getParameter(pid);
                if (!param) continue;
                CcMapping m;
                m.paramId = pid;
                m.param   = param;
                m.minNorm = obj->hasProperty("min") ? static_cast<float>(obj->getProperty("min")) : 0.0f;
                m.maxNorm = obj->hasProperty("max") ? static_cast<float>(obj->getProperty("max")) : 1.0f;
                pending.push_back({ cc, std::move(m) });
            }
        }
        const juce::SpinLock::ScopedLockType lock(ccMappingLock_);
        ccMappings_.fill({});
        for (auto& e : pending)
            ccMappings_[static_cast<size_t>(e.cc)] = std::move(e.m);
    }
    // NOTE: this fill replaces ONLY ccMappings_ (user/preset bindings). The XL controller
    // bindings live in the separate xlDefaults_ device layer (resolveCcMapping), which is
    // not touched here and not serialized — so a preset load can no longer wipe the XL
    // faders/encoders. No re-apply needed.

    // Pin engine-mode to the loaded value so the audio thread's Step↔Gen
    // transition (which copies pattern data between the two sequencers) does
    // not fire on the next block and overwrite the freshly imported step/gen
    // state. Without this, loading a preset while the in-memory mode differs
    // from the preset's mode silently corrupts the just-loaded sequencer
    // state.
    {
        const juce::ScopedLock sl (getCallbackLock());
        const bool wantGen = paramCache.genSeqRunning->load() > 0.5f;
        genModeActiveInAudio = wantGen;
        lastGenSteps = lastGenPulses = lastGenRotation = -1;
        lastGenMutation = -1.0f;
    }

    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new T5ynthProcessor();
}

// ── MIDI CC Learn ──────────────────────────────────────────────────────────

void T5ynthProcessor::startMidiLearn(const juce::String& paramId)
{
    midiLearnParamId = paramId;
    midiLearnTargetCc.store(-1, std::memory_order_release);
    midiLearnActive.store(true, std::memory_order_release);
    sendLearnLed(true, -1);
    if (onMidiLearnStateChanged) onMidiLearnStateChanged(true, -1);
}

void T5ynthProcessor::cancelMidiLearn()
{
    midiLearnActive.store(false, std::memory_order_release);
    midiLearnParamId.clear();
    midiLearnTargetCc.store(-1, std::memory_order_release);
    // NOTE: do NOT cancelPendingUpdate() here — the AsyncUpdater is shared with the XL
    // button toggles, and cancelling would silently drop (then later replay) a pending
    // toggle. Cleanup is done inline below; if a learn-capture async is still queued it
    // runs harmlessly (handleAsyncUpdate returns at the !midiLearnActive guard).
    sendLearnLed(false, -1);
    if (onMidiLearnStateChanged) onMidiLearnStateChanged(false, -1);
}

void T5ynthProcessor::clearCcMapping(int cc)
{
    if (cc < 0 || cc >= 128) return;
    // Turn off the LED before clearing the binding.
    const int note = LaunchControlXLLeds::ccToLedNote(cc);
    if (note >= 0)
        sendMidiOutputMessage(LaunchControlXLLeds::ledOff(note));
    const juce::SpinLock::ScopedLockType lock(ccMappingLock_);
    ccMappings_[static_cast<size_t>(cc)] = {};
}

void T5ynthProcessor::clearAllCcMappings()
{
    const juce::SpinLock::ScopedLockType lock(ccMappingLock_);
    ccMappings_.fill({});
}

T5ynthProcessor::CcMapping T5ynthProcessor::getCcMappingCopy(int cc) const
{
    if (cc < 0 || cc >= 128) return {};
    // Hold the lock only for POD fields that the audio thread also reads.
    // paramId is message-thread-only, so it is safe to read after the lock.
    CcMapping result;
    {
        const juce::SpinLock::ScopedLockType lock(ccMappingLock_);
        // Resolve through the SAME precedence the audio thread uses (XL device layer wins,
        // else user/preset) so GUI consumers — e.g. the easy-mode "ENV/LFO/Drift tab follows
        // the controller" feature in SynthPanel — see the active XL binding for CC 5-36,
        // which now lives in xlDefaults_, not ccMappings_.
        const auto& m = resolveCcMapping(cc);
        result.param   = m.param;
        result.minNorm = m.minNorm;
        result.maxNorm = m.maxNorm;
        result.paramId = m.paramId;  // must be read under the lock (juce::String is not free-threaded)
    }
    return result;
}

void T5ynthProcessor::handleAsyncUpdate()
{
    // XL DAW-mode transport buttons: toggle on the message thread (setValueNotifyingHost
    // locks, so it must not run on the audio thread). Consumed before — and independently
    // of — CC-Learn. exchange() collapses duplicate requests to a single toggle.
    if (xlSeqToggleReq_.exchange(false, std::memory_order_acq_rel) && seqRunningParam_ != nullptr)
        seqRunningParam_->setValueNotifyingHost(
            paramCache.seqRunning->load() > 0.5f ? 0.0f : 1.0f);
    if (xlSeqModeToggleReq_.exchange(false, std::memory_order_acq_rel) && genSeqRunningParam_ != nullptr)
        genSeqRunningParam_->setValueNotifyingHost(
            paramCache.genSeqRunning->load() > 0.5f ? 0.0f : 1.0f);

    // XL "Generate" button (CC 37): trigger a generation on the message thread.
    // onGenerateRequested → MainPanel::triggerMainGeneration (which itself no-ops if a
    // generation is already in flight, and forces drift_regen = Manual). exchange()
    // collapses duplicate presses.
    if (xlGenerateReq_.exchange(false, std::memory_order_acq_rel) && onGenerateRequested)
        onGenerateRequested();

    // XL Re-Prompt stance buttons (CC 38-44): set reprompt_stance to the requested index
    // (0-6). The PromptPanel stance bar is attached to the param, so the UI follows. -1
    // (the reset sentinel) means no press is pending; last-press-wins on a duplicate.
    const int stanceReq = xlRepromptStanceReq_.exchange(-1, std::memory_order_acq_rel);
    if (stanceReq >= 0)
        if (auto* p = parameters.getParameter(PID::repromptStance))
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(stanceReq)));

    // XL snapshot buttons (CC 45-48): recall slot 1-4 via the editor (activateSnapshot
    // restores the params and refreshes the snapshot UI). -1 = no press pending.
    const int snapReq = xlSnapshotReq_.exchange(-1, std::memory_order_acq_rel);
    if (snapReq >= 0 && onSnapshotRequested)
        onSnapshotRequested(snapReq);

    // XL cache button (CC 49): toggle the inference cache 4 ↔ Off via the editor (keeps the
    // on-screen radio buttons in sync).
    if (xlCacheToggleReq_.exchange(false, std::memory_order_acq_rel) && onCacheToggleRequested)
        onCacheToggleRequested();

    // XL generate-timing button (CC 50): toggle drift_regen between a.s.a.p. (Auto=1) and
    // 4 bars (Bar4=4). From any other mode (e.g. Manual after a Generate press) it engages
    // a.s.a.p. The REGENERATE switchbox is attached to drift_regen, so the UI follows.
    if (xlGenTimingToggleReq_.exchange(false, std::memory_order_acq_rel))
        if (auto* p = parameters.getParameter(PID::driftRegen))
        {
            const int cur  = static_cast<int>(p->convertFrom0to1(p->getValue()));
            const int next = (cur == DriftRegen::Auto) ? DriftRegen::Bar4 : DriftRegen::Auto;
            p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(next)));
        }

    // XL auto-(re)apply: a Launch Control XL output was selected or a session was restored.
    // (Re)enter DAW mode, repopulate the Page-1 bindings, and relight the LEDs — all on the
    // message thread — so selecting the port is self-sufficient and the faders/encoders are
    // never left unbound waiting for a manual "XL Map" click.
    if (xlAutoApplyReq_.exchange(false, std::memory_order_acq_rel))
        applyXLDefaultBindings();

    // The rest is CC-Learn, which only triggers this async update while a learn is
    // armed; a button-only update (above) has nothing more to do.
    if (! midiLearnActive.load(std::memory_order_acquire))
        return;

    const int cc = midiLearnTargetCc.load(std::memory_order_acquire);
    if (cc < 0 || cc >= 128 || midiLearnParamId.isEmpty())
    {
        midiLearnActive.store(false, std::memory_order_release);
        midiLearnParamId.clear();
        sendLearnLed(false, -1);
        if (onMidiLearnStateChanged) onMidiLearnStateChanged(false, -1);
        return;
    }

    auto* param = parameters.getParameter(midiLearnParamId);
    if (param == nullptr)
    {
        midiLearnActive.store(false, std::memory_order_release);
        midiLearnParamId.clear();
        sendLearnLed(false, -1);
        if (onMidiLearnStateChanged) onMidiLearnStateChanged(false, -1);
        return;
    }

    {
        const juce::SpinLock::ScopedLockType lock(ccMappingLock_);
        auto& m = ccMappings_[static_cast<size_t>(cc)];
        m.paramId  = midiLearnParamId;
        m.param    = param;
        m.minNorm  = 0.0f;
        m.maxNorm  = 1.0f;
    }

    midiLearnActive.store(false, std::memory_order_release);
    midiLearnParamId.clear();
    midiLearnTargetCc.store(-1, std::memory_order_release);
    sendLearnLed(false, cc);
    if (onMidiLearnStateChanged) onMidiLearnStateChanged(false, cc);
}

int T5ynthProcessor::findBoundCc(const juce::String& paramId) const
{
    const juce::SpinLock::ScopedLockType lock(ccMappingLock_);
    for (int cc = 0; cc < 128; ++cc)
        if (ccMappings_[static_cast<size_t>(cc)].paramId == paramId)
            return cc;
    return -1;
}

// ── MIDI Output (LED feedback) ─────────────────────────────────────────────

void T5ynthProcessor::openMidiOutputDevice(const juce::String& deviceId)
{
    closeMidiOutputDevice();
    if (deviceId.isEmpty()) return;

    // openDevice can block — call before acquiring the lock.
    auto device = juce::MidiOutput::openDevice(deviceId);
    if (!device) return;

    bool isXL = false;
    {
        const juce::SpinLock::ScopedLockType lock(midiOutputLock_);
        midiOutputDevice_   = std::move(device);
        midiOutputDeviceId_ = deviceId;
        // The Mk3 enumerates its ports as "LCXL3 1 DAW In" — it does NOT contain the
        // string "Launch Control XL", so the original check silently never matched and
        // auto-apply never fired (the XL stayed in Custom mode → warm default LEDs =
        // the "orange cast"). Match both the abbreviation and the full product name.
        const auto outName = midiOutputDevice_->getName();
        isXL = outName.containsIgnoreCase("LCXL")
            || outName.containsIgnoreCase("Launch Control XL");
    }

    // Selecting (or restoring) a Launch Control XL output is self-sufficient: auto-apply the
    // DAW-mode mapping so the faders/encoders bind and the LEDs light without a manual "XL
    // Map" click ("Dropdown = verbindlich aktiv"). Deferred to the message thread via the
    // shared AsyncUpdater because applyXLDefaultBindings sends SysEx + starts a Timer +
    // takes setValueNotifyingHost-class locks; triggerAsyncUpdate is safe from any thread,
    // so this also covers the setStateInformation restore path that may run off-message.
    if (isXL)
    {
        xlAutoApplyReq_.store(true, std::memory_order_release);
        triggerAsyncUpdate();
    }
}

void T5ynthProcessor::closeMidiOutputDevice()
{
    {
        const juce::SpinLock::ScopedLockType lock(midiOutputLock_);
        if (midiOutputDevice_ != nullptr && dawModeActive_.load(std::memory_order_acquire))
        {
            // Restore the device to standalone/custom mode before we stop driving it.
            // Sent directly (not via sendMidiOutputMessage) because we already hold the
            // lock — the SpinLock is non-recursive.
            try { midiOutputDevice_->sendMessageNow(LaunchControlXLLeds::dawMode(false)); } catch (...) {}
        }
        dawModeActive_.store(false, std::memory_order_release);
        midiOutputDevice_.reset();
        midiOutputDeviceId_.clear();
    }
    // Tear down the XL device layer so a disconnected/!XL output stops shadowing user
    // CC-learns. Separate lock scope — ccMappingLock_ is NEVER nested with midiOutputLock_.
    {
        const juce::SpinLock::ScopedLockType lock(ccMappingLock_);
        xlDefaults_.fill({});
        relEncoderAccum_.fill(0.0f);   // drop any parked relative-encoder remainder with the bindings
    }
}

void T5ynthProcessor::sendMidiOutputMessage(const juce::MidiMessage& msg)
{
    // Message thread only — MidiOutput::sendMessageNow is not audio-thread-safe.
    const juce::SpinLock::ScopedLockType lock(midiOutputLock_);
    if (!midiOutputDevice_) return;
    try { midiOutputDevice_->sendMessageNow(msg); } catch (...) {}
}

void T5ynthProcessor::sendLearnLed(bool learning, int boundCc)
{
    if (learning)
    {
        // No LED change on learn-start — the XL has no knob-ring LEDs to blink.
        // Amber pulse could be added here in future (button-row Phase 2).
        return;
    }
    if (boundCc >= 0)
    {
        const int note = LaunchControlXLLeds::ccToLedNote(boundCc);
        if (note >= 0)
            sendMidiOutputMessage(LaunchControlXLLeds::ledOn(note, LaunchControlXLLeds::kColorBound));
    }
}

void T5ynthProcessor::applyXLDefaultBindings()
{
    // Host LED control requires the device in DAW mode (programmer's ref p.8-12);
    // a Custom Mode's LEDs cannot be recoloured by the host. Enable it first so the
    // deferred LED burst below is honoured. (User must select the XL's "DAW" USB
    // port; faders/knobs then transmit on ch16 — handled by the channel-agnostic
    // binding apply in processBlock.) Flag DAW mode active ONLY if a device actually
    // received the enable, and write the flag under the lock that guards it.
    {
        const juce::SpinLock::ScopedLockType lock(midiOutputLock_);
        if (midiOutputDevice_ != nullptr)
        {
            try { midiOutputDevice_->sendMessageNow(LaunchControlXLLeds::dawMode(true)); } catch (...) {}
            dawModeActive_.store(true, std::memory_order_release);
        }
    }

    populateXLDefaultBindings();

    // Defer the LED burst so the device has time to finish entering DAW mode before
    // the colours arrive — otherwise the first XL-Map click leaves the LEDs dark.
    // Owned one-shot timer (not callAfterDelay) so ~T5ynthProcessor can cancel a
    // pending burst deterministically; capturing `this` is safe because the timer's
    // lifetime is bounded by the processor's and it is stopped before teardown.
    xlLedTimer_.fn = [this] { lightXLLeds(); };
    xlLedTimer_.startTimer(100);
}

void T5ynthProcessor::populateXLDefaultBindings()
{
    // Build the XL DEVICE layer from the fixed Page-1 layout. This writes xlDefaults_ — a
    // table separate from the preset-controlled ccMappings_ — so it is immune to preset
    // loads and is never serialized. No SysEx / no LED / no DAW-mode handshake here, so it
    // is safe to call from any thread. Resolve param pointers off the lock first.
    struct PendingEntry { int cc; CcMapping m; };
    std::vector<PendingEntry> pending;
    pending.reserve(LaunchControlXLLeds::kPage1Count + LaunchControlXLLeds::kExtMapCount);

    // Helper: resolve one Binding into a PendingEntry (shared by kPage1 + kExtMap)
    auto addBinding = [&](const lcxl3_detail::Binding& b)
    {
        if (b.cc < 0 || b.cc >= 128) return;
        auto* param = parameters.getParameter(b.paramId);
        if (!param) return;
        CcMapping m;
        m.paramId = b.paramId;
        m.param   = param;
        m.minNorm = b.minNorm;
        m.maxNorm = b.maxNorm;
        pending.push_back({ b.cc, std::move(m) });
    };

    for (int i = 0; i < LaunchControlXLLeds::kPage1Count; ++i)
        addBinding(LaunchControlXLLeds::kPage1[i]);

    for (int i = 0; i < LaunchControlXLLeds::kExtMapCount; ++i)
        addBinding(LaunchControlXLLeds::kExtMap[i]);

    {
        const juce::SpinLock::ScopedLockType lock(ccMappingLock_);
        xlDefaults_.fill({});                                          // rebuild the whole device layer
        relEncoderAccum_.fill(0.0f);                                   // reset relative-encoder remainder on rebind
        for (auto& e : pending)
            xlDefaults_[static_cast<size_t>(e.cc)] = std::move(e.m);
    }
}

// XL DAW-mode action-button assignments. The SAME constants drive both the LED colour
// (lightXLLeds) and the action dispatch (handleXLButtonPress) so a button's light always
// matches what it does. The two bottom button rows (programmer's ref p.9) are laid out:
//   TOP row    CC 37-44: Generate, then the 7 Re-Prompt stances (reprompt_stance 0-6).
//   BOTTOM row CC 45-52: Snap 1-4, Cache 4/Off, a.s.a.p./4 bars, Step/Gen, Panic.
// Transport lives on the dedicated LEFT transport buttons (Play 116 / Record 118).
static constexpr int kXLBtnGenerate    = 37;   // top row 1 → trigger generation (+ force regen = Manual)
static constexpr int kXLReStanceFirst  = 38;   // top row 2-8 → reprompt_stance index 0-6
static constexpr int kXLReStanceLast   = 44;
static constexpr int kXLSnapFirst      = 45;   // bottom row 1-4 → snapshot slots 1-4
static constexpr int kXLSnapLast       = 48;
static constexpr int kXLBtnCache       = 49;   // bottom row 5 → toggle inference cache 4↔Off
static constexpr int kXLBtnGenTiming   = 50;   // bottom row 6 → toggle drift_regen a.s.a.p.↔4 bars
static constexpr int kXLBtnStepGen     = 51;   // bottom row 7 → toggle gen_seq_running (Step/Gen)
static constexpr int kXLBtnPanic       = 52;   // bottom row 8 → MIDI panic (all notes off)
static constexpr int kXLBtnPlay        = 116;  // ▶ left transport → toggle seq_running (transport)
static constexpr int kXLBtnRecord      = 118;  // ● left transport → toggle gen_seq_running (Step/Gen)

void T5ynthProcessor::lightXLLeds()
{
    // Deferred DAW-mode device setup (runs ~100 ms after the mode switch, once the XL
    // has entered DAW mode). First put the three encoder rows into RELATIVE mode so the
    // endless encoders nudge the bound param instead of jumping to an absolute position.
    for (int row = 0; row < 3; ++row)
        sendMidiOutputMessage(LaunchControlXLLeds::encoderRelativeMode(row, true));

    // NOTE: Fader Pickup (soft takeover, LaunchControlXLLeds::faderPickup(true)) is left
    // OFF on purpose. The user reported jumping ENDLESS ENCODERS — fixed by relative mode
    // above; physical faders are absolute and SHOULD move the param immediately on touch.
    // Enabling pickup would make a fader dead until swept past its current value, an
    // unrequested trade-off. Re-enable here only if jump-free faders are explicitly wanted.

    // Light all XL Page-1 LEDs in their module accent colour (honoured only in DAW
    // mode). ccToLedNote()==-1 skips controls with no LED; sendMidiOutputMessage is
    // a no-op when no output device is open.
    for (const auto& b : LaunchControlXLLeds::kPage1)
    {
        const int note = LaunchControlXLLeds::ccToLedNote(b.cc);
        if (note >= 0)
            sendMidiOutputMessage(LaunchControlXLLeds::ledOn(note, b.color));
    }

    // Bottom two button rows — FUNCTION legend (DAW mode only). The faders themselves have
    // NO LED (programmer's ref p.11), so their kPage1 module colours are invisible; these
    // buttons carry the action legend instead. Control-index == CC for buttons (p.9), so
    // send the CC directly. Colours are STATIC group accents drawn from the existing module
    // palette — LEDs that track the live active stance / cache / timing / Step-Gen state are
    // a deliberate follow-up.
    //
    // TOP row CC 37-44: Generate (white) + the 7 Re-Prompt stances (periwinkle = Gen family).
    sendMidiOutputMessage(LaunchControlXLLeds::ledOn(kXLBtnGenerate, lcxl3_detail::rgb(127, 127, 127))); // ◆ Generate
    for (int cc = kXLReStanceFirst; cc <= kXLReStanceLast; ++cc)
        sendMidiOutputMessage(LaunchControlXLLeds::ledOn(cc, LaunchControlXLLeds::kColorGen));            // Re-Prompt stance
    //
    // BOTTOM row CC 45-52: Snap 1-4 (gold = LFO) · Cache (amber = Env) · a.s.a.p./4 bars (red-orange = Drift) ·
    // Step/Gen (violet = Filter) · Panic (red).
    for (int cc = kXLSnapFirst; cc <= kXLSnapLast; ++cc)
        sendMidiOutputMessage(LaunchControlXLLeds::ledOn(cc, LaunchControlXLLeds::kColorLfo));            // Snap 1-4
    sendMidiOutputMessage(LaunchControlXLLeds::ledOn(kXLBtnCache,     LaunchControlXLLeds::kColorEnv));    // Cache 4/Off
    sendMidiOutputMessage(LaunchControlXLLeds::ledOn(kXLBtnGenTiming, LaunchControlXLLeds::kColorDrift));  // a.s.a.p./4 bars
    sendMidiOutputMessage(LaunchControlXLLeds::ledOn(kXLBtnStepGen,   LaunchControlXLLeds::kColorFilter)); // Step/Gen
    sendMidiOutputMessage(LaunchControlXLLeds::ledOn(kXLBtnPanic,     lcxl3_detail::rgb(127, 0, 0)));      // Panic
    //
    // LEFT transport buttons (unchanged): Play ▶ green, Record ● periwinkle (Step/Gen).
    sendMidiOutputMessage(LaunchControlXLLeds::ledOn(kXLBtnPlay,   LaunchControlXLLeds::kColorVol));       // ▶ transport
    sendMidiOutputMessage(LaunchControlXLLeds::ledOn(kXLBtnRecord, LaunchControlXLLeds::kColorGen));       // ● Step/Gen
}

void T5ynthProcessor::handleXLButtonPress(int cc)
{
    // Audio-thread dispatch for an XL DAW-mode button press edge. Every action that sets a
    // parameter or calls into the editor is DEFERRED to the message thread
    // (handleAsyncUpdate) via an atomic request + triggerAsyncUpdate, because
    // setValueNotifyingHost / editor callbacks lock and must never run on the audio thread.
    // Only Panic acts inline (it just raises an atomic flag consumed in processBlock).
    // The two range groups (stances, snapshots) are handled first; the rest are exact CCs.

    if (cc >= kXLReStanceFirst && cc <= kXLReStanceLast)   // top row 2-8 → reprompt_stance 0-6
    {
        xlRepromptStanceReq_.store(cc - kXLReStanceFirst, std::memory_order_release);
        triggerAsyncUpdate();
        return;
    }
    if (cc >= kXLSnapFirst && cc <= kXLSnapLast)           // bottom row 1-4 → snapshot slot 1-4
    {
        xlSnapshotReq_.store(cc - kXLSnapFirst + 1, std::memory_order_release);
        triggerAsyncUpdate();
        return;
    }

    switch (cc)
    {
        case kXLBtnGenerate:
            xlGenerateReq_.store(true, std::memory_order_release);
            triggerAsyncUpdate();
            break;
        case kXLBtnCache:
            xlCacheToggleReq_.store(true, std::memory_order_release);
            triggerAsyncUpdate();
            break;
        case kXLBtnGenTiming:
            xlGenTimingToggleReq_.store(true, std::memory_order_release);
            triggerAsyncUpdate();
            break;
        case kXLBtnStepGen:   // dedicated bottom-row Step/Gen, same toggle as Record
        case kXLBtnRecord:
            xlSeqModeToggleReq_.store(true, std::memory_order_release);
            triggerAsyncUpdate();
            break;
        case kXLBtnPlay:
            xlSeqToggleReq_.store(true, std::memory_order_release);
            triggerAsyncUpdate();
            break;
        case kXLBtnPanic:
            requestMidiPanic();
            break;
        default:
            break;
    }
}
