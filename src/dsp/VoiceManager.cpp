#include "VoiceManager.h"

void VoiceManager::prepare(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    maxBlockSize = samplesPerBlock;
    for (auto& v : voices)
        v.prepare(sampleRate, samplesPerBlock);
    for (auto& scratch : voiceScratch)
        scratch.resize(static_cast<size_t>(samplesPerBlock));
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

void VoiceManager::noteOn(int note, float velocity, bool isBind, float glideMs,
                           bool lfo1TrigMode, bool lfo2TrigMode)
{
    // ── Mono mode: always voice 0, legato (no retrigger if held) ──
    if (voiceLimit == 1)
    {
        auto& v = voices[0];
        bool legato = v.isActive() && !v.isReleasing();
        if (legato || (isBind && v.isActive()))
        {
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
        if (v.isActive()) v.noteOff();
        v.noteOn(note, velocity, false);
        v.noteOnTimestamp = ++noteOnCounter;
        if (lfo1TrigMode) v.getPerVoiceLfo1().reset();
        if (lfo2TrigMode) v.getPerVoiceLfo2().reset();
        if (v.getEngineMode() == SynthVoice::EngineMode::Sampler && v.getSampler().hasAudio())
            v.getSampler().retrigger();
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
        // Glide: change pitch of most recently triggered active voice
        int newest = -1;
        uint64_t maxTs = 0;
        for (int i = 0; i < voiceLimit; ++i)
        {
            if (voices[static_cast<size_t>(i)].isActive()
                && voices[static_cast<size_t>(i)].noteOnTimestamp >= maxTs)
            {
                maxTs = voices[static_cast<size_t>(i)].noteOnTimestamp;
                newest = i;
            }
        }
        if (newest >= 0)
        {
            voices[static_cast<size_t>(newest)].glideToNote(note, glideMs);
            return;
        }
        // No active voice to glide — fall through to normal noteOn
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

    // Retrigger sampler if in sampler mode
    if (v.getEngineMode() == SynthVoice::EngineMode::Sampler && v.getSampler().hasAudio())
        v.getSampler().retrigger();
    if (v.getEngineMode() == SynthVoice::EngineMode::Wavetable)
    {
        v.getSampler().stop();
        v.getOsc().retriggerAutoScan();
    }

    updateGainTarget();
}

void VoiceManager::noteOff(int note)
{
    for (auto& v : voices)
    {
        if (v.isActive() && !v.isReleasing() && v.getCurrentNote() == note)
            v.noteOff();
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
}

// ═══════════════════════════════════════════════════════════════════
// Per-block rendering
// ═══════════════════════════════════════════════════════════════════

VoiceManager::VoiceOutput VoiceManager::renderBlock(
    juce::AudioBuffer<float>& buffer, const BlockParams& bp,
    const float* lfo1Buf, const float* lfo2Buf,
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
    for (auto& v : voices)
    {
        v.setTuningTable(tuningHz_);
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

    // ── Voice-first rendering: each voice renders its full block ──
    int activeCount = 0;
    int activeIndices[MAX_VOICES];

    for (int vi = 0; vi < MAX_VOICES; ++vi)
    {
        auto& v = voices[static_cast<size_t>(vi)];
        if (!v.isActive()) continue;

        // Use sub-block renderBlock for active voices
        float* scratch = voiceScratch[static_cast<size_t>(vi)].data();
        v.renderBlock(scratch, bp, lfo1Buf, lfo2Buf, numSamples);

        if (!v.isActive())
            anyBecameInactive = true;

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

        float sum = 0.0f;
        for (int a = 0; a < activeCount; ++a)
            sum += voiceScratch[static_cast<size_t>(activeIndices[a])][static_cast<size_t>(i)];

        sum *= currentGain;

        for (int ch = 0; ch < numChannels; ++ch)
            buffer.setSample(ch, startSample + i, sum);
    }

    // Update gain target if voices became inactive during this block
    if (anyBecameInactive)
        updateGainTarget();

    // Capture mod values from newest voice (end-of-block snapshot)
    if (newestIdx >= 0)
    {
        auto& nv = voices[static_cast<size_t>(newestIdx)];
        out.lastMod1Val = nv.getLastMod1Val();
        out.lastMod2Val = nv.getLastMod2Val();
        out.lastModulatedCutoff = nv.getLastModulatedCutoff();
        out.lastModulatedScan = nv.getLastModulatedScan();
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

void VoiceManager::distributeSamplerBuffer(const SamplePlayer& master)
{
    for (auto& v : voices)
        v.getSampler().shareBufferFrom(master);
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
    for (int i = 0; i < voiceLimit; ++i)
    {
        if (voices[static_cast<size_t>(i)].isActive()
            && voices[static_cast<size_t>(i)].getCurrentNote() == note)
            return i;
    }
    return -1;
}

int VoiceManager::findFreeVoice() const
{
    for (int i = 0; i < voiceLimit; ++i)
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
    for (int i = 1; i < voiceLimit; ++i)
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
    // Only count held (non-releasing) voices — releasing voices are already
    // fading via their own amplitude envelope and don't need extra attenuation.
    int n = getHeldVoiceCount();
    float newTarget = (n > 0) ? 1.0f / std::pow(static_cast<float>(n), 0.3f) : 1.0f;

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
