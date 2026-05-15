#include "VoiceManager.h"

namespace
{
constexpr bool kSamplerDebugLogging = false;

void samplerVoiceDebugLog(const juce::String& message)
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

const char* engineModeName(SynthVoice::EngineMode mode)
{
    switch (mode)
    {
        case SynthVoice::EngineMode::Sampler:   return "Sampler";
        case SynthVoice::EngineMode::Wavetable: return "Wavetable";
        case SynthVoice::EngineMode::Freeze:    return "Freeze";
    }
    return "?";
}

void equalPowerPan(float pan, float& left, float& right)
{
    pan = juce::jlimit(-1.0f, 1.0f, pan);
    const float angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
    left  = std::cos(angle);
    right = std::sin(angle);
}
}

void VoiceManager::prepare(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlockSize = samplesPerBlock;
    for (auto& v : voices)
        v.prepare(sampleRate, samplesPerBlock);
    for (auto& scratch : voiceScratch)
        scratch.resize(static_cast<size_t>(samplesPerBlock));
    for (auto& scratch : voiceScratchRight)
        scratch.resize(static_cast<size_t>(samplesPerBlock));
    voicePan.fill(0.0f);
    voiceSourceId.fill(-1);
    sustainedVoice.fill(false);
    sostenutoVoice.fill(false);
    sostenutoReleasedVoice.fill(false);
    polyPressureByNote.fill(0.0f);
    channelPressure = 0.0f;
    modWheelPressure = 0.0f;
    breathPressure = 0.0f;
    expressionGain = 1.0f;
    channelVolumeGain = 1.0f;
    pitchBendSemitones = 0.0f;
    sustainPedalDown = false;
    sostenutoPedalDown = false;
    softPedalDown = false;
    currentGain = 1.0f;
    targetGain = 1.0f;
    gainRampSamplesLeft = 0;
}

void VoiceManager::reset()
{
    for (auto& v : voices)
        v.reset();
    noteOnCounter = 0;
    currentGain = 1.0f;
    targetGain = 1.0f;
    gainRampSamplesLeft = 0;
    currentSamplerMaster_ = nullptr;
    currentWavetableMaster_ = nullptr;
    currentFreezeMaster_ = nullptr;
    hasCurrentBlockParams_ = false;
    droneVoiceIndex = -1;
    droneNote = -1;
    voicePan.fill(0.0f);
    voiceSourceId.fill(-1);
    sustainedVoice.fill(false);
    sostenutoVoice.fill(false);
    sostenutoReleasedVoice.fill(false);
    polyPressureByNote.fill(0.0f);
    channelPressure = 0.0f;
    modWheelPressure = 0.0f;
    breathPressure = 0.0f;
    expressionGain = 1.0f;
    channelVolumeGain = 1.0f;
    pitchBendSemitones = 0.0f;
    sustainPedalDown = false;
    sostenutoPedalDown = false;
    softPedalDown = false;
}

void VoiceManager::setBlockParams(const BlockParams& bp)
{
    currentBlockParams_ = bp;
    hasCurrentBlockParams_ = true;
}

// ═══════════════════════════════════════════════════════════════════
// MIDI → Voice allocation
// ═══════════════════════════════════════════════════════════════════

void VoiceManager::noteOn(int note, float velocity, bool isBind, float glideMs,
                           bool lfo1TrigMode, bool lfo2TrigMode, bool lfo3TrigMode,
                           int sourceId, float pan)
{
    sourceId = sourceId >= 0 ? juce::jlimit(0, 15, sourceId) : -1;
    pan = juce::jlimit(-1.0f, 1.0f, pan);

    // ── Mono mode: always voice 0, legato (no retrigger if held) ──
    if (voiceLimit == 1)
    {
        // Drone owns voice 0 in mono: seq noteOns are fully suppressed while held.
        if (droneVoiceIndex == 0)
            return;
        auto& v = voices[0];
        v.setTuningTable(tuningHz_);
        if (hasCurrentBlockParams_)
            v.configureForBlock(applyPerformanceControllers(currentBlockParams_));
        bool legato = v.isActive() && !v.isReleasing();
        if (legato || (isBind && v.isActive()))
        {
            voiceSourceId[0] = sourceId;
            voicePan[0] = pan;
            sustainedVoice[0] = false;
            sostenutoVoice[0] = false;
            sostenutoReleasedVoice[0] = false;
            v.setAftertouch(pressureForNote(note));
            // Glide pitch without retriggering envelopes
            // (If voice is releasing, re-hold it so it stays alive during glide)
            if (v.isReleasing())
            {
                v.noteOn(v.getCurrentNote(), velocity, false);
                // Don't retrigger sampler — keep audio continuous
            }
            v.glideToNote(note, glideMs > 0.0f ? glideMs : 30.0f);
            return;
        }
        if (v.isActive())
            v.beginRestartFade();
        sustainedVoice[0] = false;
        sostenutoVoice[0] = false;
        sostenutoReleasedVoice[0] = false;
        v.setAftertouch(pressureForNote(note));
        if (v.getEngineMode() == SynthVoice::EngineMode::Sampler && currentSamplerMaster_ != nullptr)
        {
            v.getSampler().shareBufferFrom(*currentSamplerMaster_);
            samplerVoiceDebugLog("noteOn mono share voice=0 note=" + juce::String(note)
                                 + " engine=" + juce::String(engineModeName(v.getEngineMode())));
        }
        if (v.getEngineMode() == SynthVoice::EngineMode::Wavetable && currentWavetableMaster_ != nullptr)
            v.getOsc().shareFramesFrom(*currentWavetableMaster_);
        if (v.getEngineMode() == SynthVoice::EngineMode::Freeze && currentFreezeMaster_ != nullptr)
            v.getFreezeEngine().shareBufferFrom(*currentFreezeMaster_);
        v.noteOn(note, velocity, false);
        voiceSourceId[0] = sourceId;
        voicePan[0] = pan;
        samplerVoiceDebugLog("noteOn mono trigger voice=0 note=" + juce::String(note)
                             + " velocity=" + juce::String(velocity, 3)
                             + " engine=" + juce::String(engineModeName(v.getEngineMode())));
        v.noteOnTimestamp = ++noteOnCounter;
        if (lfo1TrigMode) v.getPerVoiceLfo1().reset();
        if (lfo2TrigMode) v.getPerVoiceLfo2().reset();
        if (lfo3TrigMode) v.getPerVoiceLfo3().reset();
        if (v.getEngineMode() == SynthVoice::EngineMode::Sampler && v.getSampler().hasAudio())
        {
            v.getSampler().retrigger();
            samplerVoiceDebugLog("noteOn mono retrigger voice=0 note=" + juce::String(note));
        }
        if (v.getEngineMode() == SynthVoice::EngineMode::Wavetable)
        {
            v.getSampler().stop();
            v.getOsc().retriggerAutoScan();
        }
        updateGainTarget();
        return;
    }

    // ── Poly: glide handling ──
    if (isBind)
    {
        // Glide: change pitch of most recently triggered active voice (exclude drone).
        int newest = -1;
        uint64_t maxTs = 0;
        for (int i = 0; i < voiceLimit; ++i)
        {
            if (i == droneVoiceIndex) continue;
            if (voices[static_cast<size_t>(i)].isActive()
                && voices[static_cast<size_t>(i)].noteOnTimestamp >= maxTs)
            {
                maxTs = voices[static_cast<size_t>(i)].noteOnTimestamp;
                newest = i;
            }
        }
        if (newest >= 0)
        {
            voices[static_cast<size_t>(newest)].setTuningTable(tuningHz_);
            voices[static_cast<size_t>(newest)].glideToNote(note, glideMs);
            return;
        }
        // No active voice to glide — fall through to normal noteOn
    }

    // Find a voice: re-trigger same note > free > steal oldest
    int idx = findVoiceForNote(note, sourceId);
    if (idx < 0) idx = findFreeVoice();
    if (idx < 0) idx = stealVoice();

    auto& v = voices[static_cast<size_t>(idx)];
    v.setTuningTable(tuningHz_);
    if (hasCurrentBlockParams_)
        v.configureForBlock(applyPerformanceControllers(currentBlockParams_));

    if (v.isActive())
        v.beginRestartFade();
    sustainedVoice[static_cast<size_t>(idx)] = false;
    sostenutoVoice[static_cast<size_t>(idx)] = false;
    sostenutoReleasedVoice[static_cast<size_t>(idx)] = false;
    v.setAftertouch(pressureForNote(note));

    if (v.getEngineMode() == SynthVoice::EngineMode::Sampler && currentSamplerMaster_ != nullptr)
    {
        v.getSampler().shareBufferFrom(*currentSamplerMaster_);
        samplerVoiceDebugLog("noteOn poly share voice=" + juce::String(idx)
                             + " note=" + juce::String(note)
                             + " engine=" + juce::String(engineModeName(v.getEngineMode())));
    }
    if (v.getEngineMode() == SynthVoice::EngineMode::Wavetable && currentWavetableMaster_ != nullptr)
        v.getOsc().shareFramesFrom(*currentWavetableMaster_);
    if (v.getEngineMode() == SynthVoice::EngineMode::Freeze && currentFreezeMaster_ != nullptr)
        v.getFreezeEngine().shareBufferFrom(*currentFreezeMaster_);
    v.noteOn(note, velocity, false);
    voiceSourceId[static_cast<size_t>(idx)] = sourceId;
    voicePan[static_cast<size_t>(idx)] = pan;
    samplerVoiceDebugLog("noteOn poly trigger voice=" + juce::String(idx)
                         + " note=" + juce::String(note)
                         + " velocity=" + juce::String(velocity, 3)
                         + " engine=" + juce::String(engineModeName(v.getEngineMode())));
    v.noteOnTimestamp = ++noteOnCounter;

    // LFO trigger mode: reset per-voice LFO phase
    if (lfo1TrigMode)
        v.getPerVoiceLfo1().reset();
    if (lfo2TrigMode)
        v.getPerVoiceLfo2().reset();
    if (lfo3TrigMode)
        v.getPerVoiceLfo3().reset();

    // Retrigger sampler if in sampler mode
    if (v.getEngineMode() == SynthVoice::EngineMode::Sampler && v.getSampler().hasAudio())
    {
        v.getSampler().retrigger();
        samplerVoiceDebugLog("noteOn poly retrigger voice=" + juce::String(idx)
                             + " note=" + juce::String(note));
    }
    if (v.getEngineMode() == SynthVoice::EngineMode::Wavetable)
    {
        v.getSampler().stop();
        v.getOsc().retriggerAutoScan();
    }

    updateGainTarget();
}

void VoiceManager::noteOff(int note, int sourceId)
{
    sourceId = sourceId >= 0 ? juce::jlimit(0, 15, sourceId) : -1;
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        if (i == droneVoiceIndex) continue; // drone holds independent of MIDI noteOff
        auto& v = voices[static_cast<size_t>(i)];
        const bool sourceMatches = sourceId < 0
                                || voiceSourceId[static_cast<size_t>(i)] == sourceId;
        if (v.isActive() && !v.isReleasing() && v.getCurrentNote() == note && sourceMatches)
        {
            if (hasCurrentBlockParams_)
                v.configureForBlock(applyPerformanceControllers(currentBlockParams_));
            const bool heldBySostenuto = sourceId < 0
                                      && sostenutoPedalDown
                                      && sostenutoVoice[static_cast<size_t>(i)];
            if (heldBySostenuto)
                sostenutoReleasedVoice[static_cast<size_t>(i)] = true;

            if (sourceId < 0 && sustainPedalDown)
            {
                sustainedVoice[static_cast<size_t>(i)] = true;
                continue;
            }
            if (heldBySostenuto)
                continue;
            v.noteOff();
            sustainedVoice[static_cast<size_t>(i)] = false;
            sostenutoVoice[static_cast<size_t>(i)] = false;
            sostenutoReleasedVoice[static_cast<size_t>(i)] = false;
        }
    }
    // Update gain: held voice count decreased (releasing voices don't count).
    updateGainTarget();
}

void VoiceManager::allNotesOff()
{
    for (auto& v : voices)
    {
        if (v.isActive())
            v.noteOff();
    }
    // Panic also ends a drone hold (DAW reset / host-driven silence).
    droneVoiceIndex = -1;
    droneNote = -1;
    sustainedVoice.fill(false);
    sostenutoVoice.fill(false);
    sostenutoReleasedVoice.fill(false);
    resetPerformanceControllers();
}

void VoiceManager::setSustainPedal(bool down)
{
    if (sustainPedalDown == down)
        return;

    sustainPedalDown = down;
    if (!sustainPedalDown)
        releaseSustainedVoices();
}

void VoiceManager::setSostenutoPedal(bool down)
{
    if (sostenutoPedalDown == down)
        return;

    sostenutoPedalDown = down;
    if (sostenutoPedalDown)
    {
        for (int i = 0; i < MAX_VOICES; ++i)
        {
            auto& v = voices[static_cast<size_t>(i)];
            const bool manualVoice = voiceSourceId[static_cast<size_t>(i)] < 0;
            sostenutoVoice[static_cast<size_t>(i)] = manualVoice && v.isActive() && !v.isReleasing();
            sostenutoReleasedVoice[static_cast<size_t>(i)] = false;
        }
    }
    else
    {
        releaseSostenutoVoices();
    }
}

void VoiceManager::setSoftPedal(bool down)
{
    softPedalDown = down;
}

void VoiceManager::setPitchBendSemitones(float semitones)
{
    pitchBendSemitones = juce::jlimit(-24.0f, 24.0f, semitones);
}

void VoiceManager::setModWheel(float value)
{
    modWheelPressure = juce::jlimit(0.0f, 1.0f, value);
    refreshPerformancePressure();
}

void VoiceManager::setBreathController(float value)
{
    breathPressure = juce::jlimit(0.0f, 1.0f, value);
    refreshPerformancePressure();
}

void VoiceManager::setExpression(float value)
{
    expressionGain = juce::jlimit(0.0f, 1.0f, value);
}

void VoiceManager::setChannelVolume(float value)
{
    channelVolumeGain = juce::jlimit(0.0f, 1.0f, value);
}

void VoiceManager::setChannelPressure(float pressure)
{
    channelPressure = juce::jlimit(0.0f, 1.0f, pressure);
    refreshPerformancePressure();
}

void VoiceManager::setPolyPressure(int note, float pressure, int sourceId)
{
    note = juce::jlimit(0, 127, note);
    sourceId = sourceId >= 0 ? juce::jlimit(0, 15, sourceId) : -1;
    polyPressureByNote[static_cast<size_t>(note)] = juce::jlimit(0.0f, 1.0f, pressure);

    for (int i = 0; i < MAX_VOICES; ++i)
    {
        auto& v = voices[static_cast<size_t>(i)];
        const bool sourceMatches = sourceId < 0
                                || voiceSourceId[static_cast<size_t>(i)] == sourceId;
        if (v.isActive() && v.getCurrentNote() == note && sourceMatches)
            v.setAftertouch(pressureForNote(note));
    }
}

void VoiceManager::resetPerformanceControllers()
{
    if (sustainPedalDown)
        releaseSustainedVoices();
    if (sostenutoPedalDown)
        releaseSostenutoVoices();
    sustainPedalDown = false;
    sostenutoPedalDown = false;
    softPedalDown = false;
    sustainedVoice.fill(false);
    sostenutoVoice.fill(false);
    sostenutoReleasedVoice.fill(false);
    channelPressure = 0.0f;
    modWheelPressure = 0.0f;
    breathPressure = 0.0f;
    expressionGain = 1.0f;
    channelVolumeGain = 1.0f;
    pitchBendSemitones = 0.0f;
    polyPressureByNote.fill(0.0f);
    for (auto& v : voices)
        if (v.isActive())
            v.setAftertouch(0.0f);
}

// ═══════════════════════════════════════════════════════════════════
// Per-block rendering
// ═══════════════════════════════════════════════════════════════════

VoiceManager::VoiceOutput VoiceManager::renderBlock(
    juce::AudioBuffer<float>& buffer, const BlockParams& bp,
    const float* lfo1Buf, const float* lfo2Buf, const float* lfo3Buf,
    int startSample, int numSamples)
{
    VoiceOutput out;

    // Early exit when no voices are active — buffer already cleared by caller
    if (!hasActiveVoices())
    {
        out.hasActiveVoices = false;
        return out;
    }

    const int numChannels = buffer.getNumChannels();

    // Pass tuning table and configure all active voices once per block
    const BlockParams performanceParams = applyPerformanceControllers(bp);
    for (auto& v : voices)
    {
        v.setTuningTable(tuningHz_);
        if (v.isActive())
            v.configureForBlock(performanceParams);
    }

    // Track which voice was triggered most recently (for pitch mod / DCF)
    int newestIdx = -1;
    uint64_t maxTs = 0;
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        if (voices[static_cast<size_t>(i)].isActive()
            && voices[static_cast<size_t>(i)].noteOnTimestamp >= maxTs)
        {
            maxTs = voices[static_cast<size_t>(i)].noteOnTimestamp;
            newestIdx = i;
        }
    }

    bool anyBecameInactive = false;

    // ── Voice-first rendering: each voice renders its full block ──
    int activeCount = 0;
    int activeIndices[MAX_VOICES];

    for (int vi = 0; vi < MAX_VOICES; ++vi)
    {
        auto& v = voices[static_cast<size_t>(vi)];
        if (!v.isActive()) continue;

        // Use sub-block renderBlock for active voices
        float* scratch = voiceScratch[static_cast<size_t>(vi)].data();
        float* scratchRight = voiceScratchRight[static_cast<size_t>(vi)].data();
        v.renderBlock(scratch, scratchRight, performanceParams, lfo1Buf, lfo2Buf, lfo3Buf, numSamples);

        if (!v.isActive())
        {
            voiceSourceId[static_cast<size_t>(vi)] = -1;
            voicePan[static_cast<size_t>(vi)] = 0.0f;
            sustainedVoice[static_cast<size_t>(vi)] = false;
            sostenutoVoice[static_cast<size_t>(vi)] = false;
            sostenutoReleasedVoice[static_cast<size_t>(vi)] = false;
            anyBecameInactive = true;
        }

        activeIndices[activeCount++] = vi;
    }

    // ── Sum voice buffers + apply gain ramp ──
    for (int i = 0; i < numSamples; ++i)
    {
        if (gainRampSamplesLeft > 0)
        {
            currentGain += gainRampIncr;
            if (--gainRampSamplesLeft == 0)
                currentGain = targetGain;
        }

        float monoSum = 0.0f;
        float leftSum = 0.0f;
        float rightSum = 0.0f;
        for (int a = 0; a < activeCount; ++a)
        {
            const int vi = activeIndices[a];
            const float sampleLeft = voiceScratch[static_cast<size_t>(vi)][static_cast<size_t>(i)];
            const float sampleRight = voiceScratchRight[static_cast<size_t>(vi)][static_cast<size_t>(i)];
            const float sampleMono = 0.5f * (sampleLeft + sampleRight);
            monoSum += sampleMono;

            if (voiceSourceId[static_cast<size_t>(vi)] < 0
                || voiceSourceId[static_cast<size_t>(vi)] > 3)
            {
                leftSum += sampleLeft;
                rightSum += sampleRight;
            }
            else
            {
                float leftGain = 1.0f;
                float rightGain = 1.0f;
                equalPowerPan(voicePan[static_cast<size_t>(vi)], leftGain, rightGain);
                leftSum  += sampleMono * leftGain;
                rightSum += sampleMono * rightGain;
            }
        }

        const float outputGain = currentGain * performanceOutputGain();
        monoSum *= outputGain;
        leftSum *= outputGain;
        rightSum *= outputGain;

        if (numChannels <= 1)
        {
            buffer.setSample(0, startSample + i, monoSum);
        }
        else
        {
            buffer.setSample(0, startSample + i, leftSum);
            buffer.setSample(1, startSample + i, rightSum);
            for (int ch = 2; ch < numChannels; ++ch)
                buffer.setSample(ch, startSample + i, 0.5f * (leftSum + rightSum));
        }
    }

    // Update gain target if voices became inactive during this block
    if (anyBecameInactive)
        updateGainTarget();

    // Capture mod values from newest voice (end-of-block snapshot)
    if (newestIdx >= 0)
    {
        auto& nv = voices[static_cast<size_t>(newestIdx)];
        out.lastAmpVal = nv.getAmpEnvLevel();
        out.lastMod1Val = nv.getLastMod1Val();
        out.lastMod2Val = nv.getLastMod2Val();
        out.lastModulatedCutoff = nv.getLastModulatedCutoff();
        out.lastModulatedScan = nv.getLastModulatedScan();
        out.lastModulatedNoiseLevel = nv.getLastModulatedNoiseLevel();
        out.lastTriggeredNote = nv.getCurrentNote();
        out.hasActiveVoices = true;
    }
    else
    {
        out.hasActiveVoices = hasActiveVoices();
    }

    return out;
}

// ═══════════════════════════════════════════════════════════════════
// Engine data distribution
// ═══════════════════════════════════════════════════════════════════

void VoiceManager::setEngineMode(SynthVoice::EngineMode mode)
{
    for (auto& v : voices)
        v.setEngineMode(mode);
}

void VoiceManager::freezeActiveSamplerVoices()
{
    int frozen = 0;
    for (auto& v : voices)
    {
        if (v.isActive() && v.getEngineMode() == SynthVoice::EngineMode::Sampler)
        {
            v.getSampler().freezeSharedBuffer();
            ++frozen;
        }
    }

    if (frozen > 0)
        samplerVoiceDebugLog("freezeActiveSamplerVoices count=" + juce::String(frozen));
}

void VoiceManager::distributeSamplerBuffer(const SamplePlayer& master)
{
    currentSamplerMaster_ = &master;
    for (auto& v : voices)
    {
        if (v.isActive() && v.getEngineMode() == SynthVoice::EngineMode::Sampler)
            continue;

        v.getSampler().shareBufferFrom(master);
    }
}

void VoiceManager::distributeWavetableFrames(const WavetableOscillator& masterOsc)
{
    currentWavetableMaster_ = &masterOsc;
    for (auto& v : voices)
    {
        if (v.isActive() && v.getEngineMode() == SynthVoice::EngineMode::Wavetable)
            v.getOsc().morphToFramesFrom(masterOsc);
        else
            v.getOsc().shareFramesFrom(masterOsc);
    }
}

void VoiceManager::distributeFreezeBuffer(const FreezeTextureEngine& masterFreeze)
{
    currentFreezeMaster_ = &masterFreeze;
    for (auto& v : voices)
    {
        if (v.isActive()
            && v.getEngineMode() == SynthVoice::EngineMode::Freeze
            && v.getFreezeEngine().hasAudio())
            continue;

        v.getFreezeEngine().shareBufferFrom(masterFreeze);
    }
}

// ═══════════════════════════════════════════════════════════════════
// Voice allocation helpers
// ═══════════════════════════════════════════════════════════════════

int VoiceManager::findVoiceForNote(int note, int sourceId) const
{
    for (int i = 0; i < voiceLimit; ++i)
    {
        if (i == droneVoiceIndex) continue;
        const bool sourceMatches = sourceId >= 0
                                ? voiceSourceId[static_cast<size_t>(i)] == sourceId
                                : voiceSourceId[static_cast<size_t>(i)] < 0;
        if (voices[static_cast<size_t>(i)].isActive()
            && sourceMatches
            && voices[static_cast<size_t>(i)].getCurrentNote() == note)
            return i;
    }
    return -1;
}

int VoiceManager::findFreeVoice() const
{
    for (int i = 0; i < voiceLimit; ++i)
    {
        if (i == droneVoiceIndex) continue;
        if (!voices[static_cast<size_t>(i)].isActive())
            return i;
    }
    return -1;
}

int VoiceManager::stealVoice() const
{
    // Oldest-note policy: steal voice with lowest noteOnTimestamp, skipping the
    // drone voice so a mouse-held step never loses its voice to a seq trigger.
    int oldest = -1;
    uint64_t minTs = 0;
    for (int i = 0; i < voiceLimit; ++i)
    {
        if (i == droneVoiceIndex) continue;
        if (oldest < 0 || voices[static_cast<size_t>(i)].noteOnTimestamp < minTs)
        {
            minTs = voices[static_cast<size_t>(i)].noteOnTimestamp;
            oldest = i;
        }
    }
    // If every voice in range is the drone (poly=1 with drone on 0), callers
    // already handle the mono-drone case via the early-return in noteOn(), so
    // this path is not exercised. Fallback keeps the return value defined.
    return oldest >= 0 ? oldest : 0;
}

int VoiceManager::getActiveVoiceCount() const
{
    int count = 0;
    for (const auto& v : voices)
    {
        if (v.isActive()) count++;
    }
    return count;
}

bool VoiceManager::hasActiveVoices() const
{
    for (const auto& v : voices)
    {
        if (v.isActive()) return true;
    }
    return false;
}

void VoiceManager::updateGainTarget()
{
    // Very gentle per-voice compensation (1/N^0.1):
    //   n=2  → -0.30 dB     n=8  → -0.90 dB
    //   n=4  → -0.60 dB     n=16 → -1.20 dB
    // Sub-perceptual per-voice so engaging a drone or adding a chord note
    // doesn't audibly duck the mix, while still leaving ~1.2 dB of headroom
    // at full 16-voice polyphony before the end limiter starts working.
    int n = getHeldVoiceCount();
    float newTarget = (n > 0) ? 1.0f / std::pow(static_cast<float>(n), 0.1f) : 1.0f;

    if (newTarget != targetGain)
    {
        targetGain = newTarget;
        int rampSamples = std::max(1, static_cast<int>(GAIN_RAMP_MS * 0.001f * static_cast<float>(sr)));
        gainRampIncr = (targetGain - currentGain) / static_cast<float>(rampSamples);
        gainRampSamplesLeft = rampSamples;
    }
}

int VoiceManager::getHeldVoiceCount() const
{
    int count = 0;
    for (const auto& v : voices)
    {
        if (v.isActive() && !v.isReleasing()) count++;
    }
    return count;
}

void VoiceManager::releaseSustainedVoices()
{
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        auto& v = voices[static_cast<size_t>(i)];
        if (sustainedVoice[static_cast<size_t>(i)] && v.isActive() && !v.isReleasing())
        {
            if (hasCurrentBlockParams_)
                v.configureForBlock(applyPerformanceControllers(currentBlockParams_));
            if (sostenutoPedalDown && sostenutoVoice[static_cast<size_t>(i)])
                sostenutoReleasedVoice[static_cast<size_t>(i)] = true;
            else
                v.noteOff();
        }
        sustainedVoice[static_cast<size_t>(i)] = false;
    }
    updateGainTarget();
}

void VoiceManager::releaseSostenutoVoices()
{
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        auto& v = voices[static_cast<size_t>(i)];
        if (sostenutoVoice[static_cast<size_t>(i)]
            && sostenutoReleasedVoice[static_cast<size_t>(i)]
            && v.isActive()
            && !v.isReleasing())
        {
            if (hasCurrentBlockParams_)
                v.configureForBlock(applyPerformanceControllers(currentBlockParams_));
            v.noteOff();
        }
        sostenutoVoice[static_cast<size_t>(i)] = false;
        sostenutoReleasedVoice[static_cast<size_t>(i)] = false;
    }
    updateGainTarget();
}

void VoiceManager::refreshPerformancePressure()
{
    for (auto& v : voices)
        if (v.isActive())
            v.setAftertouch(pressureForNote(v.getCurrentNote()));
}

float VoiceManager::pressureForNote(int note) const
{
    note = juce::jlimit(0, 127, note);
    return juce::jmax(juce::jmax(channelPressure, modWheelPressure),
                      juce::jmax(breathPressure, polyPressureByNote[static_cast<size_t>(note)]));
}

float VoiceManager::performanceOutputGain() const
{
    const float softGain = softPedalDown ? 0.65f : 1.0f;
    return juce::jlimit(0.0f, 1.0f, channelVolumeGain * expressionGain * softGain);
}

BlockParams VoiceManager::applyPerformanceControllers(const BlockParams& bp) const
{
    auto out = bp;
    out.performancePitchRatio = std::pow(2.0f, pitchBendSemitones / 12.0f);
    if (softPedalDown)
        out.baseCutoff *= 0.72f;
    return out;
}

// ═══════════════════════════════════════════════════════════════════
// Drone (step-hold) handling
// ═══════════════════════════════════════════════════════════════════

void VoiceManager::setDroneNote(int note, float velocity, bool lfo1TrigMode, bool lfo2TrigMode, bool lfo3TrigMode)
{
    note = juce::jlimit(0, 127, note);
    velocity = juce::jlimit(0.0f, 1.0f, velocity);

    // Same pitch as current drone → no-op (mouse stayed at same drag-Y).
    if (droneVoiceIndex >= 0 && droneNote == note)
        return;

    if (droneVoiceIndex < 0)
    {
        // ── First drone trigger: pick a voice and do a full noteOn on it. ──
        int idx;
        if (isMono())
        {
            idx = 0;
        }
        else
        {
            idx = findFreeVoice();
            if (idx < 0) idx = stealVoice();
        }

        auto& v = voices[static_cast<size_t>(idx)];
        v.setTuningTable(tuningHz_);
        if (hasCurrentBlockParams_)
            v.configureForBlock(applyPerformanceControllers(currentBlockParams_));
        if (v.isActive())
            v.beginRestartFade();
        sustainedVoice[static_cast<size_t>(idx)] = false;
        sostenutoVoice[static_cast<size_t>(idx)] = false;
        sostenutoReleasedVoice[static_cast<size_t>(idx)] = false;
        v.setAftertouch(pressureForNote(note));
        if (v.getEngineMode() == SynthVoice::EngineMode::Sampler && currentSamplerMaster_ != nullptr)
            v.getSampler().shareBufferFrom(*currentSamplerMaster_);
        if (v.getEngineMode() == SynthVoice::EngineMode::Wavetable && currentWavetableMaster_ != nullptr)
            v.getOsc().shareFramesFrom(*currentWavetableMaster_);
        if (v.getEngineMode() == SynthVoice::EngineMode::Freeze && currentFreezeMaster_ != nullptr)
            v.getFreezeEngine().shareBufferFrom(*currentFreezeMaster_);

        v.noteOn(note, velocity, false);
        voiceSourceId[static_cast<size_t>(idx)] = -1;
        voicePan[static_cast<size_t>(idx)] = 0.0f;
        v.noteOnTimestamp = ++noteOnCounter;
        if (lfo1TrigMode) v.getPerVoiceLfo1().reset();
        if (lfo2TrigMode) v.getPerVoiceLfo2().reset();
        if (lfo3TrigMode) v.getPerVoiceLfo3().reset();
        if (v.getEngineMode() == SynthVoice::EngineMode::Sampler && v.getSampler().hasAudio())
            v.getSampler().retrigger();
        if (v.getEngineMode() == SynthVoice::EngineMode::Wavetable)
        {
            v.getSampler().stop();
            v.getOsc().retriggerAutoScan();
        }
        droneVoiceIndex = idx;
    }
    else
    {
        // ── Drone pitch change while held: glide on same voice, no env retrigger. ──
        auto& v = voices[static_cast<size_t>(droneVoiceIndex)];
        v.setTuningTable(tuningHz_);
        if (hasCurrentBlockParams_)
            v.configureForBlock(applyPerformanceControllers(currentBlockParams_));
        v.setAftertouch(pressureForNote(note));
        v.glideToNote(note, 15.0f);  // short glide keeps mouse-drag scrubs click-free
    }

    droneNote = note;
    updateGainTarget();
}

void VoiceManager::clearDroneNote()
{
    if (droneVoiceIndex < 0) return;
    auto& v = voices[static_cast<size_t>(droneVoiceIndex)];
    if (v.isActive())
        v.noteOff();
    droneVoiceIndex = -1;
    droneNote = -1;
    updateGainTarget();
}
