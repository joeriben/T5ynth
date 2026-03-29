#include "VoiceManager.h"

void VoiceManager::prepare(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    for (auto& v : voices)
        v.prepare(sampleRate, samplesPerBlock);
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
}

// ═══════════════════════════════════════════════════════════════════
// MIDI → Voice allocation
// ═══════════════════════════════════════════════════════════════════

void VoiceManager::noteOn(int note, float velocity, bool isGlide, float glideMs,
                           bool lfo1TrigMode, bool lfo2TrigMode)
{
    if (isGlide)
    {
        // Glide: change pitch of most recently triggered active voice
        int newest = -1;
        uint64_t maxTs = 0;
        for (int i = 0; i < MAX_VOICES; ++i)
        {
            if (voices[static_cast<size_t>(i)].isActive()
                && voices[static_cast<size_t>(i)].noteOnTimestamp >= maxTs)
            {
                maxTs = voices[static_cast<size_t>(i)].noteOnTimestamp;
                newest = i;
            }
        }
        if (newest >= 0)
            voices[static_cast<size_t>(newest)].glideToNote(note, glideMs);
        return;
    }

    // Find a voice: re-trigger same note > free > steal oldest
    int idx = findVoiceForNote(note);
    if (idx < 0) idx = findFreeVoice();
    if (idx < 0) idx = stealVoice();

    auto& v = voices[static_cast<size_t>(idx)];

    // If stealing an active voice, give it a fast release to avoid click
    if (v.isActive())
        v.noteOff();

    v.noteOn(note, velocity, false);
    v.noteOnTimestamp = ++noteOnCounter;

    // LFO trigger mode: reset per-voice LFO phase
    if (lfo1TrigMode)
        v.getPerVoiceLfo1().reset();
    if (lfo2TrigMode)
        v.getPerVoiceLfo2().reset();

    // Retrigger looper if in looper mode
    if (v.getEngineMode() == SynthVoice::EngineMode::Looper && v.getLooper().hasAudio())
        v.getLooper().retrigger();
    if (v.getEngineMode() == SynthVoice::EngineMode::Wavetable)
        v.getLooper().stop();

    updateGainTarget();
}

void VoiceManager::noteOff(int note)
{
    for (auto& v : voices)
    {
        if (v.isActive() && !v.isReleasing() && v.getCurrentNote() == note)
            v.noteOff();
    }
    // Don't updateGainTarget here — voices are still active during release.
    // Gain updates when voices actually become inactive (in renderBlock).
}

void VoiceManager::allNotesOff()
{
    for (auto& v : voices)
    {
        if (v.isActive())
            v.noteOff();
    }
}

// ═══════════════════════════════════════════════════════════════════
// Per-block rendering
// ═══════════════════════════════════════════════════════════════════

VoiceManager::VoiceOutput VoiceManager::renderBlock(
    juce::AudioBuffer<float>& buffer, const BlockParams& bp,
    const float* lfo1Buf, const float* lfo2Buf, int numSamples)
{
    VoiceOutput out;
    const int numChannels = buffer.getNumChannels();

    // Configure all active voices' envelopes once per block
    for (auto& v : voices)
    {
        if (v.isActive())
            v.configureForBlock(bp);
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

    // Per-sample rendering
    for (int i = 0; i < numSamples; ++i)
    {
        // Gain ramp for voice-count transitions
        if (gainRampSamplesLeft > 0)
        {
            currentGain += gainRampIncr;
            gainRampSamplesLeft--;
            if (gainRampSamplesLeft == 0)
                currentGain = targetGain;
        }

        float sumL = 0.0f;

        for (int vi = 0; vi < MAX_VOICES; ++vi)
        {
            auto& v = voices[static_cast<size_t>(vi)];
            if (!v.isActive()) continue;

            auto result = v.renderSample(bp, lfo1Buf[i], lfo2Buf[i]);
            sumL += result.sample;

            // Capture mod values from newest voice
            if (vi == newestIdx)
            {
                out.lastMod1Val = result.mod1EnvVal;
                out.lastMod2Val = result.mod2EnvVal;
            }

            // Check if voice just became inactive
            if (!v.isActive())
                anyBecameInactive = true;
        }

        // Apply 1/sqrt(N) gain scaling
        sumL *= currentGain;

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample(ch, i, sumL);
    }

    // Update gain target if voices became inactive during this block
    if (anyBecameInactive)
        updateGainTarget();

    // Set output
    if (newestIdx >= 0)
    {
        out.lastTriggeredNote = voices[static_cast<size_t>(newestIdx)].getCurrentNote();
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

void VoiceManager::distributeLooperBuffer(const AudioLooper& masterLooper)
{
    for (auto& v : voices)
        v.getLooper().shareBufferFrom(masterLooper);
}

void VoiceManager::distributeWavetableFrames(const WavetableOscillator& masterOsc)
{
    for (auto& v : voices)
        v.getOsc().shareFramesFrom(masterOsc);
}

// ═══════════════════════════════════════════════════════════════════
// Voice allocation helpers
// ═══════════════════════════════════════════════════════════════════

int VoiceManager::findVoiceForNote(int note) const
{
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        if (voices[static_cast<size_t>(i)].isActive()
            && voices[static_cast<size_t>(i)].getCurrentNote() == note)
            return i;
    }
    return -1;
}

int VoiceManager::findFreeVoice() const
{
    for (int i = 0; i < MAX_VOICES; ++i)
    {
        if (!voices[static_cast<size_t>(i)].isActive())
            return i;
    }
    return -1;
}

int VoiceManager::stealVoice() const
{
    // Oldest-note policy: steal voice with lowest noteOnTimestamp
    int oldest = 0;
    uint64_t minTs = voices[0].noteOnTimestamp;
    for (int i = 1; i < MAX_VOICES; ++i)
    {
        if (voices[static_cast<size_t>(i)].noteOnTimestamp < minTs)
        {
            minTs = voices[static_cast<size_t>(i)].noteOnTimestamp;
            oldest = i;
        }
    }
    return oldest;
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
    int n = getActiveVoiceCount();
    float newTarget = (n > 0) ? 1.0f / std::sqrt(static_cast<float>(n)) : 1.0f;

    if (newTarget != targetGain)
    {
        targetGain = newTarget;
        int rampSamples = std::max(1, static_cast<int>(GAIN_RAMP_MS * 0.001f * static_cast<float>(sr)));
        gainRampIncr = (targetGain - currentGain) / static_cast<float>(rampSamples);
        gainRampSamplesLeft = rampSamples;
    }
}
