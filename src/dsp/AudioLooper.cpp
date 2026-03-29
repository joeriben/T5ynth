#include "AudioLooper.h"
#include <cmath>

void AudioLooper::prepare(double sampleRate, int /*samplesPerBlock*/)
{
    playbackSampleRate = sampleRate;
}

void AudioLooper::reset()
{
    originalBuffer.setSize(0, 0);
    playBuffer.setSize(0, 0);
    audioLoaded = false;
    playing = false;
    readPosition = 0.0;
}

void AudioLooper::loadBuffer(const juce::AudioBuffer<float>& buffer, double bufferSampleRate)
{
    if (sharedMode) return; // shared-mode loopers don't own audio
    originalBuffer.makeCopyOf(buffer);
    bufferOriginalSR = bufferSampleRate;
    audioLoaded = true;
    preparePlaybackBuffer();
    playing = true;
}

void AudioLooper::shareBufferFrom(const AudioLooper& master)
{
    sharedMode = true;
    sharedPlayBuffer = &master.playBuffer;
    bufferOriginalSR = master.bufferOriginalSR;
    playStart = master.playStart;
    playEnd = master.playEnd;
    coldStart = master.coldStart;
    loopMode = master.loopMode;
    audioLoaded = master.audioLoaded;
    readPosition = static_cast<double>(coldStart);
    needsReprepareFlag = false;
}

void AudioLooper::setMidiNote(int note)
{
    transposeRatio = std::pow(2.0, (note - 60) / 12.0);
    glideSamplesLeft = 0; // cancel any active glide
}

void AudioLooper::glideToSemitones(int semitones, float durationMs)
{
    double targetRatio = std::pow(2.0, semitones / 12.0);
    double durationSamples = (durationMs / 1000.0) * playbackSampleRate;
    int samples = std::max(1, static_cast<int>(durationSamples));

    glideTargetRatio = targetRatio;
    glideRatioIncr = (targetRatio - transposeRatio) / static_cast<double>(samples);
    glideSamplesLeft = samples;
}

void AudioLooper::retrigger()
{
    readPosition = static_cast<double>(coldStart);
    playing = true;
}

void AudioLooper::setLoopStart(float frac)
{
    float clamped = juce::jlimit(0.0f, loopEndFrac - 0.01f, frac);
    if (clamped != loopStartFrac)
    {
        loopStartFrac = clamped;
        needsReprepareFlag = true;
    }
}

void AudioLooper::setLoopEnd(float frac)
{
    float clamped = juce::jlimit(loopStartFrac + 0.01f, 1.0f, frac);
    if (clamped != loopEndFrac)
    {
        loopEndFrac = clamped;
        needsReprepareFlag = true;
    }
}

void AudioLooper::setLoopMode(LoopMode mode)
{
    if (mode != loopMode) { loopMode = mode; needsReprepareFlag = true; }
}

void AudioLooper::setCrossfadeMs(float ms)
{
    float clamped = juce::jlimit(0.0f, 500.0f, ms);
    if (clamped != crossfadeMsVal) { crossfadeMsVal = clamped; needsReprepareFlag = true; }
}

void AudioLooper::setNormalize(bool on)
{
    if (on != normalizeOn) { normalizeOn = on; needsReprepareFlag = true; }
}

void AudioLooper::setLoopOptimize(bool on)
{
    if (on != loopOptimizeOn) { loopOptimizeOn = on; needsReprepareFlag = true; }
}

// ═══════════════════════════════════════════════════════════════════
// Buffer preparation (mirrors useAudioLooper.ts prepareBuffer)
// ═══════════════════════════════════════════════════════════════════

void AudioLooper::preparePlaybackBuffer()
{
    if (originalBuffer.getNumSamples() == 0) return;

    const int bufLen = originalBuffer.getNumSamples();
    const int numCh  = originalBuffer.getNumChannels();

    int ls = static_cast<int>(std::floor(loopStartFrac * bufLen));
    int le = std::min(bufLen, static_cast<int>(std::ceil(loopEndFrac * bufLen)));

    if (le - ls < 4) { le = std::min(bufLen, ls + 4); }

    if (loopMode == LoopMode::PingPong)
    {
        // Ping-pong: optionally optimize, then create palindrome (no crossfade)
        int actualEnd = le;
        if (loopOptimizeOn && numCh > 0)
            actualEnd = optimizeLoopEnd(originalBuffer.getReadPointer(0), ls, le, bufLen);

        // Copy original into working buffer
        juce::AudioBuffer<float> working;
        working.makeCopyOf(originalBuffer);

        // Create palindrome
        int palindromeEnd = 0;
        createPalindrome(working, ls, actualEnd, playBuffer, palindromeEnd);

        playStart = ls;
        playEnd   = palindromeEnd;
        coldStart = ls; // no crossfade zone in palindrome
    }
    else
    {
        // Forward loop or one-shot: apply crossfade at boundary
        playBuffer.makeCopyOf(originalBuffer);

        int actualEnd = le;
        if (loopOptimizeOn && numCh > 0 && loopMode == LoopMode::Loop)
            actualEnd = optimizeLoopEnd(originalBuffer.getReadPointer(0), ls, le, bufLen);

        int fadeSamples = 0;
        if (loopMode == LoopMode::Loop)
        {
            // Apply crossfade — modifies playBuffer and moves actualEnd back
            int preEnd = actualEnd;
            applyLoopCrossfade(playBuffer, ls, actualEnd);
            fadeSamples = preEnd - actualEnd;
        }

        playStart = ls;
        playEnd   = actualEnd;
        // Cold start: past crossfade zone (head samples are modified tail audio)
        coldStart = ls + fadeSamples;
    }

    // Normalize if enabled
    if (normalizeOn)
        normalizeBuffer(playBuffer);

    // Reset read position to cold start
    readPosition = static_cast<double>(coldStart);
    needsReprepareFlag = false;
}

// ═══════════════════════════════════════════════════════════════════
// processBlock
// ═══════════════════════════════════════════════════════════════════

float AudioLooper::processSample()
{
    const int regionLen = playEnd - playStart;
    if (regionLen <= 0 || !playing) return 0.0f;

    // Apply pitch glide (per-sample linear ramp)
    if (glideSamplesLeft > 0)
    {
        transposeRatio += glideRatioIncr;
        glideSamplesLeft--;
        if (glideSamplesLeft == 0)
            transposeRatio = glideTargetRatio;
    }

    const double srRatio = bufferOriginalSR / playbackSampleRate;
    double speedRatio = srRatio * transposeRatio;

    // Map readPosition into the play region
    double posInRegion = readPosition - static_cast<double>(playStart);

    int pos0 = static_cast<int>(std::floor(posInRegion));
    float frac = static_cast<float>(posInRegion - pos0);

    // Clamp into region
    if (pos0 < 0) pos0 = 0;
    int absPos0 = playStart + (pos0 % regionLen);
    int absPos1 = playStart + ((pos0 + 1) % regionLen);

    // Read mono (channel 0) with linear interpolation
    const auto& buf = sharedMode ? *sharedPlayBuffer : playBuffer;
    const auto* bufPtr = buf.getReadPointer(0);
    float s0 = bufPtr[absPos0];
    float s1 = bufPtr[absPos1];
    float result = s0 + (s1 - s0) * frac;

    readPosition += speedRatio;

    // Handle wrapping / stopping based on mode
    if (readPosition >= static_cast<double>(playEnd))
    {
        if (loopMode == LoopMode::OneShot)
        {
            playing = false;
        }
        else
        {
            // Loop or PingPong: wrap back to loop start
            readPosition -= static_cast<double>(regionLen);
        }
    }

    return result;
}

void AudioLooper::processBlock(juce::AudioBuffer<float>& output)
{
    if (!audioLoaded || !playing || playBuffer.getNumSamples() == 0)
        return;

    // Re-prepare if settings changed (shared-mode loopers skip this)
    if (needsReprepareFlag && !sharedMode)
        preparePlaybackBuffer();

    const int numOutChannels = output.getNumChannels();
    const int numOutSamples  = output.getNumSamples();

    for (int i = 0; i < numOutSamples; ++i)
    {
        float sample = processSample();
        if (!playing) break; // one-shot ended

        for (int ch = 0; ch < numOutChannels; ++ch)
            output.addSample(ch, i, sample);
    }
}

// ═══════════════════════════════════════════════════════════════════
// Cross-correlation loop optimizer (from useAudioLooper.ts)
// ═══════════════════════════════════════════════════════════════════

int AudioLooper::optimizeLoopEnd(const float* data, int loopStart, int loopEnd, int bufLen) const
{
    int win = std::min(XCORR_WINDOW, (loopEnd - loopStart) / 4);
    if (win < 16) return loopEnd;

    int searchLo = std::max(loopStart + win * 2, loopEnd - XCORR_SEARCH);
    int searchHi = std::min(bufLen, loopEnd + XCORR_SEARCH);

    float bestCorr = -1e30f;
    int   bestEnd  = loopEnd;

    for (int cand = searchLo; cand < searchHi; ++cand)
    {
        int eStart = cand - win;
        if (eStart < loopStart) continue;

        float sum = 0.0f, normA = 0.0f, normB = 0.0f;
        for (int j = 0; j < win; ++j)
        {
            float a = data[loopStart + j];
            float b = data[eStart + j];
            sum   += a * b;
            normA += a * a;
            normB += b * b;
        }
        float denom = std::sqrt(normA * normB);
        float corr  = denom > 0.0f ? sum / denom : 0.0f;

        if (corr > bestCorr)
        {
            bestCorr = corr;
            bestEnd  = cand;
        }
    }
    return bestEnd;
}

// ═══════════════════════════════════════════════════════════════════
// Equal-power crossfade at loop boundary (from useAudioLooper.ts)
// ═══════════════════════════════════════════════════════════════════

void AudioLooper::applyLoopCrossfade(juce::AudioBuffer<float>& buf, int loopStart, int& loopEnd) const
{
    int loopLen = loopEnd - loopStart;
    int fadeSamples = std::min(
        static_cast<int>(crossfadeMsVal / 1000.0f * static_cast<float>(bufferOriginalSR)),
        loopLen / 2
    );

    if (fadeSamples < 2) return;

    const float pi_half = juce::MathConstants<float>::halfPi;

    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        float* d = buf.getWritePointer(ch);
        for (int i = 0; i < fadeSamples; ++i)
        {
            float t     = static_cast<float>(i) / static_cast<float>(fadeSamples);
            float gHead = std::sin(t * pi_half); // 0 → 1
            float gTail = std::cos(t * pi_half); // 1 → 0

            int headIdx = loopStart + i;
            int tailIdx = loopEnd - fadeSamples + i;
            // Blend fading-out tail into fading-in head
            d[headIdx] = d[headIdx] * gHead + d[tailIdx] * gTail;
        }
    }

    // Shorten loop: tail samples are baked into head, never played directly
    loopEnd -= fadeSamples;
}

// ═══════════════════════════════════════════════════════════════════
// Palindrome for ping-pong (from useAudioLooper.ts)
// ═══════════════════════════════════════════════════════════════════

void AudioLooper::createPalindrome(const juce::AudioBuffer<float>& src, int loopStart, int loopEnd,
                                    juce::AudioBuffer<float>& dest, int& palindromeEnd) const
{
    int loopLen = loopEnd - loopStart;
    if (loopLen < 4)
    {
        dest.makeCopyOf(src);
        palindromeEnd = loopEnd;
        return;
    }

    int reverseLen = loopLen - 2; // skip endpoints to avoid doubling
    int newLen = src.getNumSamples() + reverseLen;
    int numCh  = src.getNumChannels();

    dest.setSize(numCh, newLen, false, false, true);

    for (int ch = 0; ch < numCh; ++ch)
    {
        const float* s = src.getReadPointer(ch);
        float*       d = dest.getWritePointer(ch);

        // Copy everything up to loopEnd
        for (int i = 0; i < loopEnd; ++i)
            d[i] = s[i];

        // Insert reversed loop (skip endpoints)
        for (int i = 0; i < reverseLen; ++i)
            d[loopEnd + i] = s[loopEnd - 2 - i];

        // Copy post-loop data (shifted)
        for (int i = loopEnd; i < src.getNumSamples(); ++i)
            d[i + reverseLen] = s[i];
    }

    palindromeEnd = loopEnd + reverseLen;
}

// ═══════════════════════════════════════════════════════════════════
// Peak normalize to 0.95 (from useAudioLooper.ts)
// ═══════════════════════════════════════════════════════════════════

void AudioLooper::normalizeBuffer(juce::AudioBuffer<float>& buf) const
{
    float peak = 0.0f;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const float* d = buf.getReadPointer(ch);
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            float a = std::abs(d[i]);
            if (a > peak) peak = a;
        }
    }

    if (peak < 0.001f) return;

    float gain = 0.95f / peak;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        float* d = buf.getWritePointer(ch);
        for (int i = 0; i < buf.getNumSamples(); ++i)
            d[i] *= gain;
    }
}
