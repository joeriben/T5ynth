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
    startPosFrac = master.startPosFrac;
    loopStartFrac = master.loopStartFrac;
    loopEndFrac = master.loopEndFrac;
    wtExtractStartFrac_ = master.wtExtractStartFrac_;
    wtExtractEndFrac_ = master.wtExtractEndFrac_;
    startPosOffset_ = master.startPosOffset_;
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

void SamplePlayer::setTransposeRatio(double ratio)
{
    transposeRatio = ratio;
    glideSamplesLeft = 0;
}

void SamplePlayer::glideToSemitones(int semitones, float durationMs)
{
    glideToRatio(std::pow(2.0, semitones / 12.0), durationMs);
}

void SamplePlayer::glideToRatio(double targetRatio, float durationMs)
{
    double durationSamples = (durationMs / 1000.0) * playbackSampleRate;
    int samples = std::max(1, static_cast<int>(durationSamples));

    glideTargetRatio = targetRatio;
    glideRatioIncr = (targetRatio - transposeRatio) / static_cast<double>(samples);
    glideSamplesLeft = samples;
}

void SamplePlayer::retrigger()
{
    const auto& buf = sharedMode ? *sharedPlayBuffer : playBuffer;
    const int bufLen = buf.getNumSamples();
    if (bufLen == 0) return;

    // Effective P1 = base + modulation offset (Scan→P1 in Sampler mode)
    float effectiveP1 = juce::jlimit(0.0f, 1.0f, startPosFrac + startPosOffset_);

    // Determine direction based on P1 vs P3
    bool reversed = (effectiveP1 > loopEndFrac);
    playDirection_ = reversed ? -1 : 1;
    inFirstPass_ = true;

    // Convert P1 fraction to sample position
    int startSample = static_cast<int>(std::floor(effectiveP1 * bufLen));

    // In Loop mode, skip crossfade zone if P1 falls in it
    if (loopMode == LoopMode::Loop && startSample >= playStart && startSample < coldStart)
        startSample = coldStart;

    readPosition = static_cast<double>(juce::jlimit(0, bufLen - 1, startSample));
    playing = true;

    if (stretcherPrepared)
    {
        stretcher.reset();
        stretcherNeedsPriming = true;
    }
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

void SamplePlayer::setStartPos(float frac)
{
    startPosFrac = juce::jlimit(0.0f, 1.0f, frac);
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

void SamplePlayer::setLoopOptimizeLevel(int level)
{
    int clamped = juce::jlimit(0, 2, level);
    if (clamped != loopOptimizeLevel) { loopOptimizeLevel = clamped; needsReprepareFlag = true; }
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

    // All modes: start from a plain copy of the original
    playBuffer.makeCopyOf(originalBuffer);

    int actualEnd = le;

    if (loopMode == LoopMode::Loop)
    {
        // Forward loop: optimize loop point + apply crossfade at boundary
        if (loopOptimizeLevel > 0 && numCh > 0)
            actualEnd = optimizeLoopEnd(originalBuffer.getReadPointer(0), ls, le, bufLen, loopOptimizeLevel);

        int preEnd = actualEnd;
        applyLoopCrossfade(playBuffer, ls, actualEnd);
        int fadeSamples = preEnd - actualEnd;

        playStart = ls;
        playEnd   = actualEnd;
        coldStart = ls + fadeSamples; // past crossfade zone
    }
    else
    {
        // OneShot or PingPong: no crossfade, no palindrome
        // PingPong uses runtime direction reversal instead of palindrome buffer
        playStart = ls;
        playEnd   = actualEnd;
        coldStart = ls;
    }

    // Normalize if enabled.
    // Loop/PingPong: scan P2..P3 (the loop region determines sustained loudness).
    // OneShot: scan P1..P3 (plays once from P1, no loop — P1 is the audible start).
    if (normalizeOn)
    {
        int normStart = playStart; // P2
        if (loopMode == LoopMode::OneShot)
        {
            int p1 = static_cast<int>(std::floor(startPosFrac * bufLen));
            normStart = std::min(p1, playStart);
        }
        normalizeBuffer(playBuffer, normStart, playEnd);
    }

    // Reset read position via retrigger (respects P1)
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
// advancePosition — 3-point loop logic (shared by processSample/readRawSamples)
// ═══════════════════════════════════════════════════════════════════

bool SamplePlayer::advancePosition(double speedMagnitude)
{
    readPosition += speedMagnitude * playDirection_;

    const double pEnd   = static_cast<double>(playEnd);
    const double pStart = static_cast<double>(playStart);

    if (inFirstPass_)
    {
        // During first pass, only check the boundary the playhead is moving toward
        if (playDirection_ > 0 && readPosition >= pEnd)
        {
            inFirstPass_ = false;
            if (loopMode == LoopMode::OneShot) { playing = false; return false; }
            double overshoot = readPosition - pEnd;
            if (loopMode == LoopMode::PingPong)
            {
                readPosition = pEnd - overshoot;
                playDirection_ = -1;
            }
            else // Loop
            {
                readPosition = pStart + overshoot;
            }
        }
        else if (playDirection_ < 0 && readPosition < pStart)
        {
            inFirstPass_ = false;
            if (loopMode == LoopMode::OneShot) { playing = false; return false; }
            double overshoot = pStart - readPosition;
            if (loopMode == LoopMode::PingPong)
            {
                readPosition = pStart + overshoot;
                playDirection_ = 1;
            }
            else // Loop
            {
                readPosition = pEnd - overshoot;
            }
        }
    }
    else
    {
        // Standard looping between P2-P3
        if (readPosition >= pEnd)
        {
            double overshoot = readPosition - pEnd;
            if (loopMode == LoopMode::PingPong)
            {
                readPosition = pEnd - overshoot;
                playDirection_ = -1;
            }
            else
            {
                readPosition = pStart + overshoot;
            }
        }
        else if (readPosition < pStart)
        {
            double overshoot = pStart - readPosition;
            if (loopMode == LoopMode::PingPong)
            {
                readPosition = pStart + overshoot;
                playDirection_ = 1;
            }
            else
            {
                readPosition = pEnd - overshoot;
            }
        }
    }

    return true;
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

    // Advance with 3-point logic (direction, first-pass, boundaries)
    advancePosition(std::abs(speedRatio));

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

        if (!advancePosition(std::abs(srRatio)))
        {
            // OneShot ended — zero-fill remainder
            std::memset(output + i + 1, 0,
                        sizeof(float) * static_cast<size_t>(numSamples - i - 1));
            return;
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

    // Determine mode before glide advancement (use current ratio)
    double effectiveRatio = transposeRatio * static_cast<double>(pitchModFactor);
    float semitones = static_cast<float>(12.0 * std::log2(std::max(effectiveRatio, 1e-6)));
    bool nearUnity = std::abs(semitones) < 0.1f;

    // Bypass: processSample() handles glide per-sample — no block-level advancement
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

    // Stretch path: advance glide at block level (processSample is not called)
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
        // Recalculate after glide advancement
        effectiveRatio = transposeRatio * static_cast<double>(pitchModFactor);
        semitones = static_cast<float>(12.0 * std::log2(std::max(effectiveRatio, 1e-6)));
    }

    if (!stretcherPrepared)
        prepareStretcher();

    // Set transposition BEFORE priming — primeStretcher() runs process()
    // internally, which must use the new note's pitch, not the previous one's.
    stretcher.setTransposeSemitones(semitones);

    if (stretcherNeedsPriming)
    {
        primeStretcher();
        stretcherNeedsPriming = false;
    }

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

void SamplePlayer::primeStretcher()
{
    const double srRatio = bufferOriginalSR / playbackSampleRate;

    // ── Step 1: seek() — fill STFT analysis context with pre-roll audio ──
    // Reads audio BEFORE readPosition (coldStart) so the first STFT frames
    // have real left context instead of post-reset silence.
    int seekLen = stretcher.blockSamples() + stretcher.intervalSamples();
    double seekStart = readPosition - seekLen * srRatio;
    if (seekStart < 0.0)
    {
        seekLen = std::max(0, static_cast<int>(readPosition / srRatio));
        seekStart = readPosition - seekLen * srRatio;
    }
    if (seekLen > 0)
    {
        std::vector<float> seekBuf(static_cast<size_t>(seekLen));
        double pos = seekStart;
        for (int i = 0; i < seekLen; ++i)
        {
            seekBuf[static_cast<size_t>(i)] = cubicSample(pos);
            pos += srRatio;
        }
        float* seekPtr = seekBuf.data();
        stretcher.seek(&seekPtr, seekLen, 1.0);
    }

    // ── Step 2: process()+discard — pay STFT output latency ──
    // After seek, the first inputLatency() output samples correspond to
    // pre-roll audio, not coldStart. Discard them so the next real output
    // starts at approximately coldStart. readPosition advances intentionally.
    int primeSamples = stretcher.inputLatency();
    if (primeSamples <= 0) return;

    std::vector<float> primeBuf(static_cast<size_t>(primeSamples));
    std::vector<float> discardBuf(static_cast<size_t>(primeSamples));

    readRawSamples(primeBuf.data(), primeSamples);

    float* inPtr = primeBuf.data();
    float* outPtr = discardBuf.data();
    stretcher.process(&inPtr, primeSamples, &outPtr, primeSamples);
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

int SamplePlayer::optimizeLoopEnd(const float* data, int loopStart, int loopEnd, int bufLen, int level) const
{
    int win = std::min(XCORR_WINDOW[level], (loopEnd - loopStart) / 4);
    if (win < 16) return loopEnd;

    int searchLo = std::max(loopStart + win * 2, loopEnd - XCORR_SEARCH[level]);
    int searchHi = std::min(bufLen, loopEnd + XCORR_SEARCH[level]);

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
// RMS normalize to -12 dBFS, then peak-limit to 0.95
// ═══════════════════════════════════════════════════════════════════

void SamplePlayer::normalizeBuffer(juce::AudioBuffer<float>& buf, int regionStart, int regionEnd) const
{
    int rs = juce::jlimit(0, buf.getNumSamples(), regionStart);
    int re = juce::jlimit(rs, buf.getNumSamples(), regionEnd);
    if (re <= rs) return;

    // Compute RMS over the play region (all channels)
    double sumSq = 0.0;
    int totalSamples = 0;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const float* d = buf.getReadPointer(ch);
        for (int i = rs; i < re; ++i)
        {
            double s = static_cast<double>(d[i]);
            sumSq += s * s;
        }
        totalSamples += (re - rs);
    }

    if (totalSamples == 0) return;
    float rms = static_cast<float>(std::sqrt(sumSq / totalSamples));
    if (rms < 1e-6f) return; // silence

    // Target: -12 dBFS RMS ≈ 0.25
    static constexpr float kTargetRms = 0.25f;
    float gain = kTargetRms / rms;

    // Apply gain to entire buffer
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        float* d = buf.getWritePointer(ch);
        for (int i = 0; i < buf.getNumSamples(); ++i)
            d[i] *= gain;
    }

    // Soft-clip peaks above 0.95 to prevent clipping without crushing dynamics
    static constexpr float kCeiling = 0.95f;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        float* d = buf.getWritePointer(ch);
        for (int i = 0; i < buf.getNumSamples(); ++i)
        {
            if (d[i] > kCeiling)       d[i] = kCeiling;
            else if (d[i] < -kCeiling) d[i] = -kCeiling;
        }
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
