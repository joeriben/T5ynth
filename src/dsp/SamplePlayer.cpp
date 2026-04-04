#include "SamplePlayer.h"
#include <cmath>
#include <cstring>

void SamplePlayer::prepare(double sampleRate, int samplesPerBlock)
{
    playbackSampleRate = sampleRate;
    maxBlockSize = samplesPerBlock;
    rawReadBuf.resize(static_cast<size_t>(samplesPerBlock));
    prepareStretcher();
}

void SamplePlayer::reset()
{
    originalBuffer.setSize(0, 0);
    playBuffer.setSize(0, 0);
    audioLoaded = false;
    playing = false;
    readPosition = 0.0;
    stretcher.reset();
}

void SamplePlayer::loadBuffer(const juce::AudioBuffer<float>& buffer, double bufferSampleRate)
{
    if (sharedMode) return; // shared-mode players don't own audio
    originalBuffer.makeCopyOf(buffer);
    bufferOriginalSR = bufferSampleRate;
    trimLeadingSilence();
    audioLoaded = true;
    preparePlaybackBuffer();
    playing = true;
}

void SamplePlayer::shareBufferFrom(const SamplePlayer& master)
{
    bool wasShared = sharedMode;
    sharedMode = true;
    sharedPlayBuffer = &master.playBuffer;
    bufferOriginalSR = master.bufferOriginalSR;
    playStart = master.playStart;
    playEnd = master.playEnd;
    coldStart = master.coldStart;
    loopMode = master.loopMode;
    audioLoaded = master.audioLoaded;
    // Only reset read position on first share, not on subsequent syncs
    if (!wasShared)
        readPosition = static_cast<double>(coldStart);
    needsReprepareFlag = false;
}

void SamplePlayer::setMidiNote(int note)
{
    transposeRatio = std::pow(2.0, (note - 60) / 12.0);
    glideSamplesLeft = 0; // cancel any active glide
}

void SamplePlayer::glideToSemitones(int semitones, float durationMs)
{
    double targetRatio = std::pow(2.0, semitones / 12.0);
    double durationSamples = (durationMs / 1000.0) * playbackSampleRate;
    int samples = std::max(1, static_cast<int>(durationSamples));

    glideTargetRatio = targetRatio;
    glideRatioIncr = (targetRatio - transposeRatio) / static_cast<double>(samples);
    glideSamplesLeft = samples;
}

void SamplePlayer::retrigger()
{
    readPosition = static_cast<double>(coldStart);
    playing = true;
    if (stretcherPrepared)
        stretcher.reset();
}

void SamplePlayer::setLoopStart(float frac)
{
    float clamped = juce::jlimit(0.0f, loopEndFrac - 0.01f, frac);
    if (clamped != loopStartFrac)
    {
        loopStartFrac = clamped;
        needsReprepareFlag = true;
    }
}

void SamplePlayer::setLoopEnd(float frac)
{
    float clamped = juce::jlimit(loopStartFrac + 0.01f, 1.0f, frac);
    if (clamped != loopEndFrac)
    {
        loopEndFrac = clamped;
        needsReprepareFlag = true;
    }
}

void SamplePlayer::setLoopMode(LoopMode mode)
{
    if (mode != loopMode) { loopMode = mode; needsReprepareFlag = true; }
}

void SamplePlayer::setCrossfadeMs(float ms)
{
    float clamped = juce::jlimit(0.0f, 500.0f, ms);
    if (clamped != crossfadeMsVal) { crossfadeMsVal = clamped; needsReprepareFlag = true; }
}

void SamplePlayer::setNormalize(bool on)
{
    if (on != normalizeOn) { normalizeOn = on; needsReprepareFlag = true; }
}

void SamplePlayer::setLoopOptimize(bool on)
{
    if (on != loopOptimizeOn) { loopOptimizeOn = on; needsReprepareFlag = true; }
}

// ═══════════════════════════════════════════════════════════════════
// Buffer preparation (mirrors useSamplePlayer.ts prepareBuffer)
// ═══════════════════════════════════════════════════════════════════

void SamplePlayer::preparePlaybackBuffer()
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

    // Normalize if enabled (only scan the play region for peak)
    if (normalizeOn)
        normalizeBuffer(playBuffer, playStart, playEnd);

    // Reset read position to cold start
    readPosition = static_cast<double>(coldStart);
    needsReprepareFlag = false;
}

// ═══════════════════════════════════════════════════════════════════
// processBlock
// ═══════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════
// Catmull-Rom cubic interpolation (4-point, no trig — ~20× faster than Lanczos)
// ═══════════════════════════════════════════════════════════════════

float SamplePlayer::cubicSample(double pos) const
{
    const auto& buf = sharedMode ? *sharedPlayBuffer : playBuffer;
    const float* data = buf.getReadPointer(0);
    const int bufLen = buf.getNumSamples();

    int i1 = static_cast<int>(std::floor(pos));
    float t = static_cast<float>(pos - i1);

    // Clamp indices to buffer bounds
    int i0 = (i1 > 0) ? i1 - 1 : 0;
    if (i1 < 0) i1 = 0;
    else if (i1 >= bufLen) i1 = bufLen - 1;
    int i2 = (i1 + 1 < bufLen) ? i1 + 1 : bufLen - 1;
    int i3 = (i1 + 2 < bufLen) ? i1 + 2 : bufLen - 1;

    float p0 = data[i0], p1 = data[i1], p2 = data[i2], p3 = data[i3];

    // Catmull-Rom spline: 12 mul + 8 add, zero trig
    float a = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
    float b =         p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
    float c = -0.5f * p0              + 0.5f * p2;
    // d = p1
    return ((a * t + b) * t + c) * t + p1;
}

// ═══════════════════════════════════════════════════════════════════
// processSample (Bypass mode / legacy — speed-based transposition)
// ═══════════════════════════════════════════════════════════════════

float SamplePlayer::processSample()
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

    // Read with cubic interpolation
    float result = cubicSample(readPosition);

    readPosition += speedRatio;

    // Handle wrapping / stopping based on mode
    if (readPosition >= static_cast<double>(playEnd))
    {
        if (loopMode == LoopMode::OneShot)
            playing = false;
        else
            readPosition -= static_cast<double>(regionLen);
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════
// readRawSamples — read at 1:1 speed (SR-corrected, no transposition)
// ═══════════════════════════════════════════════════════════════════

void SamplePlayer::readRawSamples(float* output, int numSamples)
{
    const int regionLen = playEnd - playStart;
    if (regionLen <= 0 || !playing)
    {
        std::memset(output, 0, sizeof(float) * static_cast<size_t>(numSamples));
        return;
    }

    const double srRatio = bufferOriginalSR / playbackSampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        output[i] = cubicSample(readPosition);
        readPosition += srRatio; // 1:1 speed, only SR correction

        if (readPosition >= static_cast<double>(playEnd))
        {
            if (loopMode == LoopMode::OneShot)
            {
                playing = false;
                std::memset(output + i + 1, 0,
                            sizeof(float) * static_cast<size_t>(numSamples - i - 1));
                return;
            }
            else
            {
                readPosition -= static_cast<double>(regionLen);
            }
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
// renderPitchedBlock — pitch-preserving transposition via Signalsmith Stretch
// ═══════════════════════════════════════════════════════════════════

void SamplePlayer::renderPitchedBlock(float* output, int numSamples)
{
    if (!audioLoaded || !playing)
    {
        std::memset(output, 0, sizeof(float) * static_cast<size_t>(numSamples));
        return;
    }

    // Re-prepare buffer if settings changed
    if (needsReprepareFlag && !sharedMode)
        preparePlaybackBuffer();

    // Advance glide state for the entire block
    if (glideSamplesLeft > 0)
    {
        int steps = std::min(glideSamplesLeft, numSamples);
        transposeRatio += glideRatioIncr * steps;
        glideSamplesLeft -= steps;
        if (glideSamplesLeft <= 0)
        {
            transposeRatio = glideTargetRatio;
            glideSamplesLeft = 0;
        }
    }

    // Bypass: speed-based transposition (with cubic interpolation)
    // Also bypass when transposition is negligible (< 0.1 semitone)
    float semitones = static_cast<float>(12.0 * std::log2(std::max(transposeRatio, 1e-6)));
    bool nearUnity = std::abs(semitones) < 0.1f;

    if (pitchQuality == PitchShiftQuality::Bypass || nearUnity)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            output[i] = processSample();
            if (!playing)
            {
                std::memset(output + i + 1, 0,
                            sizeof(float) * static_cast<size_t>(numSamples - i - 1));
                return;
            }
        }
        return;
    }

    if (!stretcherPrepared)
        prepareStretcher();

    // Set pitch transposition and process the full block at once
    stretcher.setTransposeSemitones(semitones);

    // Read raw samples at original speed (SR-corrected only)
    readRawSamples(rawReadBuf.data(), numSamples);

    // Pitch-shift through Signalsmith Stretch (mono: 1 channel)
    float* inPtr = rawReadBuf.data();
    stretcher.process(&inPtr, numSamples, &output, numSamples);
}

// ═══════════════════════════════════════════════════════════════════
// Pitch shifter configuration
// ═══════════════════════════════════════════════════════════════════

void SamplePlayer::prepareStretcher()
{
    float sr = static_cast<float>(playbackSampleRate);
    switch (pitchQuality)
    {
        case PitchShiftQuality::Bypass:
            break;
        case PitchShiftQuality::Efficient:
            stretcher.presetCheaper(1, sr);
            break;
        case PitchShiftQuality::Default:
            stretcher.presetCheaper(1, sr); // cheap enough for polyphonic real-time
            break;
        case PitchShiftQuality::HighQuality:
            stretcher.presetDefault(1, sr);
            break;
        default:
            stretcher.presetCheaper(1, sr);
            break;
    }
    stretcherPrepared = (pitchQuality != PitchShiftQuality::Bypass);
}

void SamplePlayer::setPitchShiftQuality(PitchShiftQuality quality)
{
    if (quality != pitchQuality)
    {
        pitchQuality = quality;
        prepareStretcher();
    }
}

void SamplePlayer::processBlock(juce::AudioBuffer<float>& output)
{
    if (!audioLoaded || !playing || playBuffer.getNumSamples() == 0)
        return;

    // Re-prepare if settings changed (shared-mode players skip this)
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
// Cross-correlation loop optimizer (from useSamplePlayer.ts)
// ═══════════════════════════════════════════════════════════════════

int SamplePlayer::optimizeLoopEnd(const float* data, int loopStart, int loopEnd, int bufLen) const
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
// Equal-power crossfade at loop boundary (from useSamplePlayer.ts)
// ═══════════════════════════════════════════════════════════════════

void SamplePlayer::applyLoopCrossfade(juce::AudioBuffer<float>& buf, int loopStart, int& loopEnd) const
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
// Palindrome for ping-pong (from useSamplePlayer.ts)
// ═══════════════════════════════════════════════════════════════════

void SamplePlayer::createPalindrome(const juce::AudioBuffer<float>& src, int loopStart, int loopEnd,
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
// Peak normalize to 0.95 (from useSamplePlayer.ts)
// ═══════════════════════════════════════════════════════════════════

void SamplePlayer::normalizeBuffer(juce::AudioBuffer<float>& buf, int regionStart, int regionEnd) const
{
    // Find peak only within the play region (avoid spikes outside brackets)
    int rs = juce::jlimit(0, buf.getNumSamples(), regionStart);
    int re = juce::jlimit(rs, buf.getNumSamples(), regionEnd);
    if (re <= rs) return;

    float peak = 0.0f;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const float* d = buf.getReadPointer(ch);
        for (int i = rs; i < re; ++i)
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

// ═══════════════════════════════════════════════════════════════════
// Trim leading silence — removes near-zero head with no aesthetic value
// ═══════════════════════════════════════════════════════════════════

void SamplePlayer::trimLeadingSilence()
{
    const int numSamples = originalBuffer.getNumSamples();
    const int numCh      = originalBuffer.getNumChannels();
    if (numSamples == 0 || numCh == 0) return;

    // Threshold: ~-60 dB — anything below is acoustically inaudible
    constexpr float threshold = 0.001f;

    // Find first sample above threshold (across all channels)
    int firstActive = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        bool active = false;
        for (int ch = 0; ch < numCh; ++ch)
        {
            if (std::abs(originalBuffer.getReadPointer(ch)[i]) > threshold)
            {
                active = true;
                break;
            }
        }
        if (active)
        {
            firstActive = i;
            break;
        }
        // If we reach the end, everything is silent — keep as-is
        if (i == numSamples - 1) return;
    }

    if (firstActive == 0) return; // nothing to trim

    // Truncate: copy from firstActive onward into a new buffer
    int newLen = numSamples - firstActive;
    juce::AudioBuffer<float> trimmed(numCh, newLen);
    for (int ch = 0; ch < numCh; ++ch)
        trimmed.copyFrom(ch, 0, originalBuffer, ch, firstActive, newLen);

    originalBuffer = std::move(trimmed);
}
