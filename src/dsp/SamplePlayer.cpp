#include "SamplePlayer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
constexpr bool kSamplerDebugLogging = false;
constexpr float kNormalizeCeilingDb = -1.0f;
constexpr float kSustainedTargetDb = -18.0f;
constexpr float kTransientPercentileTargetDb = -10.0f;
constexpr float kNearSilentPeakDb = -36.0f;
constexpr float kNearSilentActiveDb = -50.0f;
constexpr float kHotHeadroomDb = 2.0f;
constexpr float kShortTransientSeconds = 0.75f;
constexpr float kActiveBlockThresholdDb = -40.0f;
constexpr float kTransientActiveRatio = 0.35f;
constexpr float kTransientCrestDb = 18.0f;
constexpr float kTransientPeakGapDb = 8.0f;

juce::String samplerPtrTag(const void* ptr)
{
    return "0x" + juce::String::toHexString(static_cast<juce::int64>(reinterpret_cast<std::uintptr_t>(ptr)));
}

void samplerDebugLog(const juce::String& message)
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

const char* loopModeName(SamplePlayer::LoopMode mode)
{
    switch (mode)
    {
        case SamplePlayer::LoopMode::OneShot:  return "OneShot";
        case SamplePlayer::LoopMode::Loop:     return "Loop";
        case SamplePlayer::LoopMode::PingPong: return "PingPong";
    }
    return "?";
}

float dbToGain(float db)
{
    return juce::Decibels::decibelsToGain(db);
}

float gainToDb(float gain)
{
    return juce::Decibels::gainToDecibels(std::max(gain, 1.0e-9f));
}
}

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
    playbackSnapshot_.reset();
    audioLoaded = false;
    playing = false;
    readPosition = 0.0;
    sourceGain_ = 1.0f;
    sharedMode = false;
    stretcher.reset();
}

void SamplePlayer::loadBuffer(const juce::AudioBuffer<float>& buffer, double bufferSampleRate)
{
    if (sharedMode)
    {
        samplerDebugLog("loadBuffer ignored sharedMode player=" + samplerPtrTag(this));
        return; // shared-mode players don't own audio
    }

    samplerDebugLog("loadBuffer player=" + samplerPtrTag(this)
                    + " samples=" + juce::String(buffer.getNumSamples())
                    + " sr=" + juce::String(bufferSampleRate, 2));
    const auto config = capturePrepareConfig();
    applyPreparedBufferLoad(prepareBufferLoad(buffer, bufferSampleRate, config), config);
    playing = true;
}

SamplePlayer::PrepareConfig SamplePlayer::capturePrepareConfig() const
{
    PrepareConfig config;
    config.loopMode = loopMode;
    config.crossfadeMs = crossfadeMsVal;
    config.normalizeOn = normalizeOn;
    config.loopOptimizeLevel = loopOptimizeLevel;
    config.startPosFrac = startPosFrac;
    config.loopStartFrac = loopStartFrac;
    config.loopEndFrac = loopEndFrac;
    return config;
}

SamplePlayer::PreparedBufferLoad SamplePlayer::prepareBufferLoad(const juce::AudioBuffer<float>& buffer,
                                                                double bufferSampleRate,
                                                                const PrepareConfig& config) const
{
    PreparedBufferLoad prepared;
    prepared.originalBuffer.makeCopyOf(buffer);
    trimLeadingSilence(prepared.originalBuffer);
    prepared.playbackState = preparePlaybackState(prepared.originalBuffer, bufferSampleRate, config);
    return prepared;
}

void SamplePlayer::applyPreparedBufferLoad(PreparedBufferLoad prepared, const PrepareConfig& config)
{
    originalBuffer = std::move(prepared.originalBuffer);
    loopMode = config.loopMode;
    crossfadeMsVal = config.crossfadeMs;
    normalizeOn = config.normalizeOn;
    loopOptimizeLevel = config.loopOptimizeLevel;
    startPosFrac = config.startPosFrac;
    loopStartFrac = config.loopStartFrac;
    loopEndFrac = config.loopEndFrac;
    applyPreparedPlaybackState(std::move(prepared.playbackState));
    sharedMode = false;
    needsReprepareFlag = false;
}

void SamplePlayer::shareBufferFrom(const SamplePlayer& master)
{
    bool wasShared = sharedMode;
    sharedMode = true;
    playbackSnapshot_ = master.playbackSnapshot_;
    if (playbackSnapshot_ == nullptr)
        playBuffer.setSize(0, 0);
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
    sourceGain_ = master.sourceGain_;
    audioLoaded = master.audioLoaded;
    // Only reset read position on first share, not on subsequent syncs
    if (!wasShared)
        readPosition = static_cast<double>(coldStart);
    needsReprepareFlag = false;

    samplerDebugLog("shareBufferFrom dst=" + samplerPtrTag(this)
                    + " src=" + samplerPtrTag(&master)
                    + " wasShared=" + juce::String(wasShared ? 1 : 0)
                    + " readPos=" + juce::String(readPosition, 2)
                    + " state={" + debugStateString() + "}");
}

void SamplePlayer::freezeSharedBuffer()
{
    if (!sharedMode || playbackSnapshot_ == nullptr)
        return;

    samplerDebugLog("freezeSharedBuffer player=" + samplerPtrTag(this)
                    + " before={" + debugStateString() + "}");
    sharedMode = false;
    audioLoaded = playbackSnapshot_->playBuffer.getNumSamples() > 0;
    needsReprepareFlag = false;
    samplerDebugLog("freezeSharedBuffer player=" + samplerPtrTag(this)
                    + " after={" + debugStateString() + "}");
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
    const auto& buf = currentPlaybackBuffer();
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

    samplerDebugLog("retrigger player=" + samplerPtrTag(this)
                    + " startSample=" + juce::String(startSample)
                    + " effectiveP1=" + juce::String(effectiveP1, 4)
                    + " state={" + debugStateString() + "}");
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
    if (originalBuffer.getNumSamples() == 0)
        return;

    applyPreparedPlaybackState(
        preparePlaybackState(originalBuffer, bufferOriginalSR, capturePrepareConfig()));
    needsReprepareFlag = false;

    samplerDebugLog("preparePlaybackBuffer player=" + samplerPtrTag(this)
                    + " bufLen=" + juce::String(originalBuffer.getNumSamples())
                    + " norm=" + juce::String(normalizeOn ? 1 : 0)
                    + " state={" + debugStateString() + "}");
}

SamplePlayer::PreparedPlaybackState SamplePlayer::preparePlaybackState(
    const juce::AudioBuffer<float>& sourceBuffer, double sourceSampleRate, const PrepareConfig& config) const
{
    PreparedPlaybackState prepared;
    prepared.bufferOriginalSR = sourceSampleRate;
    prepared.audioLoaded = sourceBuffer.getNumSamples() > 0 && sourceBuffer.getNumChannels() > 0;

    if (!prepared.audioLoaded)
        return prepared;

    const int bufLen = sourceBuffer.getNumSamples();
    const int numCh  = sourceBuffer.getNumChannels();

    int ls = static_cast<int>(std::floor(config.loopStartFrac * bufLen));
    int le = std::min(bufLen, static_cast<int>(std::ceil(config.loopEndFrac * bufLen)));

    if (le - ls < 4)
        le = std::min(bufLen, ls + 4);

    prepared.playBuffer.makeCopyOf(sourceBuffer);

    int actualEnd = le;

    if (config.loopMode == LoopMode::Loop)
    {
        if (config.loopOptimizeLevel > 0 && numCh > 0)
            actualEnd = optimizeLoopEnd(sourceBuffer.getReadPointer(0), ls, le, bufLen, config.loopOptimizeLevel);

        int preEnd = actualEnd;
        applyLoopCrossfade(prepared.playBuffer, ls, actualEnd, config.crossfadeMs, sourceSampleRate);
        int fadeSamples = preEnd - actualEnd;

        prepared.playStart = ls;
        prepared.playEnd   = actualEnd;
        prepared.coldStart = ls + fadeSamples;
    }
    else
    {
        prepared.playStart = ls;
        prepared.playEnd   = actualEnd;
        prepared.coldStart = ls;
    }

    if (config.normalizeOn)
    {
        int p1 = static_cast<int>(std::floor(config.startPosFrac * bufLen));
        int normStart = std::min(p1, prepared.playStart);
        normalizeBuffer(prepared.playBuffer, normStart, prepared.playEnd, sourceSampleRate);
    }

    return prepared;
}

void SamplePlayer::applyPreparedPlaybackState(PreparedPlaybackState preparedState)
{
    auto snapshot = std::make_shared<PlaybackSnapshot>();
    snapshot->playBuffer = std::move(preparedState.playBuffer);
    snapshot->bufferOriginalSR = preparedState.bufferOriginalSR;
    playbackSnapshot_ = std::move(snapshot);

    playBuffer.setSize(0, 0);
    bufferOriginalSR = preparedState.bufferOriginalSR;
    playStart = preparedState.playStart;
    playEnd = preparedState.playEnd;
    coldStart = preparedState.coldStart;
    audioLoaded = preparedState.audioLoaded;

    readPosition = static_cast<double>(coldStart);
}

// ═══════════════════════════════════════════════════════════════════
// processBlock
// ═══════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════
// Catmull-Rom cubic interpolation (4-point, no trig — ~20× faster than Lanczos)
// ═══════════════════════════════════════════════════════════════════

const juce::AudioBuffer<float>& SamplePlayer::currentPlaybackBuffer() const
{
    if (playbackSnapshot_ != nullptr)
        return playbackSnapshot_->playBuffer;
    return playBuffer;
}

double SamplePlayer::currentBufferOriginalSR() const
{
    if (playbackSnapshot_ != nullptr)
        return playbackSnapshot_->bufferOriginalSR;
    return bufferOriginalSR;
}

float SamplePlayer::cubicSample(double pos) const
{
    const auto& buf = currentPlaybackBuffer();
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

    const double srRatio = currentBufferOriginalSR() / playbackSampleRate;
    double speedRatio = srRatio * transposeRatio;

    // Read with cubic interpolation
    float result = cubicSample(readPosition) * sourceGain_;

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

    const double srRatio = currentBufferOriginalSR() / playbackSampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        output[i] = cubicSample(readPosition) * sourceGain_;

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

int SamplePlayer::estimateReferenceLengthSamples() const
{
    const auto& buf = currentPlaybackBuffer();
    const int bufLen = buf.getNumSamples();
    if (bufLen <= 0 || !audioLoaded)
        return 0;

    const double srRatio = currentBufferOriginalSR() / playbackSampleRate;
    if (srRatio <= 0.0)
        return 0;

    const float effectiveP1 = juce::jlimit(0.0f, 1.0f, startPosFrac + startPosOffset_);
    int startSample = static_cast<int>(std::floor(effectiveP1 * static_cast<float>(bufLen)));
    startSample = juce::jlimit(0, bufLen - 1, startSample);

    if (loopMode == LoopMode::Loop && startSample >= playStart && startSample < coldStart)
        startSample = coldStart;

    const bool reversed = (effectiveP1 > loopEndFrac);
    const int loopLen = juce::jmax(1, playEnd - playStart);
    int sourceSamples = 0;

    if (!reversed)
        sourceSamples = juce::jmax(0, playEnd - startSample);
    else
        sourceSamples = juce::jmax(0, startSample - playStart);

    if (loopMode == LoopMode::Loop)
        sourceSamples += loopLen;
    else if (loopMode == LoopMode::PingPong)
        sourceSamples += loopLen * 2;

    return juce::jmax(1, static_cast<int>(std::ceil(static_cast<double>(sourceSamples) / srRatio)));
}

juce::String SamplePlayer::debugStateString() const
{
    const auto& buf = currentPlaybackBuffer();

    return "shared=" + juce::String(sharedMode ? 1 : 0)
        + " playing=" + juce::String(playing ? 1 : 0)
        + " loaded=" + juce::String(audioLoaded ? 1 : 0)
        + " read=" + juce::String(readPosition, 2)
        + " playStart=" + juce::String(playStart)
        + " coldStart=" + juce::String(coldStart)
        + " playEnd=" + juce::String(playEnd)
        + " len=" + juce::String(buf.getNumSamples())
        + " p1=" + juce::String(startPosFrac, 4)
        + " p2=" + juce::String(loopStartFrac, 4)
        + " p3=" + juce::String(loopEndFrac, 4)
        + " p1Off=" + juce::String(startPosOffset_, 4)
        + " dir=" + juce::String(playDirection_)
        + " firstPass=" + juce::String(inFirstPass_ ? 1 : 0)
        + " mode=" + juce::String(loopModeName(loopMode))
        + " sourceGain=" + juce::String(sourceGain_, 4)
        + " needsReprepare=" + juce::String(needsReprepareFlag ? 1 : 0);
}

float SamplePlayer::estimatePlaybackRms(const float* gains, int numSamples, float* outPeak) const
{
    const auto& buf = currentPlaybackBuffer();
    const int bufLen = buf.getNumSamples();
    if (gains == nullptr || numSamples <= 0 || bufLen <= 0 || !audioLoaded)
    {
        if (outPeak != nullptr) *outPeak = 0.0f;
        return 0.0f;
    }

    const double srRatio = currentBufferOriginalSR() / playbackSampleRate;
    const float effectiveP1 = juce::jlimit(0.0f, 1.0f, startPosFrac + startPosOffset_);

    bool inFirstPass = true;
    int playDirection = (effectiveP1 > loopEndFrac) ? -1 : 1;
    double analysisReadPosition = static_cast<double>(
        std::floor(effectiveP1 * static_cast<float>(bufLen)));

    if (loopMode == LoopMode::Loop
        && analysisReadPosition >= static_cast<double>(playStart)
        && analysisReadPosition < static_cast<double>(coldStart))
    {
        analysisReadPosition = static_cast<double>(coldStart);
    }

    double sumSq = 0.0;
    int renderedSamples = 0;
    float peak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        float sample = cubicSample(analysisReadPosition);
        float weighted = sample * gains[i];
        sumSq += static_cast<double>(weighted) * static_cast<double>(weighted);
        peak = std::max(peak, std::abs(weighted));
        ++renderedSamples;

        analysisReadPosition += srRatio * static_cast<double>(playDirection);

        const double pEnd = static_cast<double>(playEnd);
        const double pStart = static_cast<double>(playStart);

        if (inFirstPass)
        {
            if (playDirection > 0 && analysisReadPosition >= pEnd)
            {
                inFirstPass = false;
                if (loopMode == LoopMode::OneShot)
                    break;

                const double overshoot = analysisReadPosition - pEnd;
                if (loopMode == LoopMode::PingPong)
                {
                    analysisReadPosition = pEnd - overshoot;
                    playDirection = -1;
                }
                else
                {
                    analysisReadPosition = pStart + overshoot;
                }
            }
            else if (playDirection < 0 && analysisReadPosition < pStart)
            {
                inFirstPass = false;
                if (loopMode == LoopMode::OneShot)
                    break;

                const double overshoot = pStart - analysisReadPosition;
                if (loopMode == LoopMode::PingPong)
                {
                    analysisReadPosition = pStart + overshoot;
                    playDirection = 1;
                }
                else
                {
                    analysisReadPosition = pEnd - overshoot;
                }
            }
        }
        else
        {
            if (analysisReadPosition >= pEnd)
            {
                const double overshoot = analysisReadPosition - pEnd;
                if (loopMode == LoopMode::PingPong)
                {
                    analysisReadPosition = pEnd - overshoot;
                    playDirection = -1;
                }
                else
                {
                    analysisReadPosition = pStart + overshoot;
                }
            }
            else if (analysisReadPosition < pStart)
            {
                const double overshoot = pStart - analysisReadPosition;
                if (loopMode == LoopMode::PingPong)
                {
                    analysisReadPosition = pStart + overshoot;
                    playDirection = 1;
                }
                else
                {
                    analysisReadPosition = pEnd - overshoot;
                }
            }
        }
    }

    if (outPeak != nullptr)
        *outPeak = peak;

    if (renderedSamples <= 0)
        return 0.0f;

    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(renderedSamples)));
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
    const double srRatio = currentBufferOriginalSR() / playbackSampleRate;

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
    if (!audioLoaded || !playing || currentPlaybackBuffer().getNumSamples() == 0)
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

void SamplePlayer::applyLoopCrossfade(juce::AudioBuffer<float>& buf, int loopStart, int& loopEnd,
                                      float crossfadeMs, double bufferSampleRate)
{
    int loopLen = loopEnd - loopStart;
    int fadeSamples = std::min(
        static_cast<int>(crossfadeMs / 1000.0f * static_cast<float>(bufferSampleRate)),
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
// Signal-aware linear normalization:
//   - near-silence: bypass
//   - already-hot material: ceiling cap only
//   - sparse/transient material: percentile-based upward normalize
//   - sustained material: active-RMS normalize with peak ceiling
// The gain is applied only to the measured playback region.
// ═══════════════════════════════════════════════════════════════════

const char* SamplePlayer::normalizeModeName(NormalizeMode mode)
{
    switch (mode)
    {
        case NormalizeMode::Bypass:    return "Bypass";
        case NormalizeMode::PeakCap:   return "PeakCap";
        case NormalizeMode::Transient: return "Transient";
        case NormalizeMode::Sustained: return "Sustained";
    }
    return "?";
}

SamplePlayer::NormalizeAnalysis SamplePlayer::analyzeNormalizeRegion(const juce::AudioBuffer<float>& buf,
                                                                    int regionStart,
                                                                    int regionEnd,
                                                                    double bufferSampleRate) const
{
    NormalizeAnalysis analysis;

    const int rs = juce::jlimit(0, buf.getNumSamples(), regionStart);
    const int re = juce::jlimit(rs, buf.getNumSamples(), regionEnd);
    const int numChannels = buf.getNumChannels();
    if (re <= rs || numChannels <= 0 || bufferSampleRate <= 0.0)
        return analysis;

    const int regionFrames = re - rs;
    analysis.durationSeconds = static_cast<float>(regionFrames / bufferSampleRate);

    std::vector<float> framePeaks;
    framePeaks.reserve(static_cast<size_t>(regionFrames));

    const int blockFrames = juce::jmax(1, static_cast<int>(std::round(bufferSampleRate * 0.050)));
    const float activeThreshold = dbToGain(kActiveBlockThresholdDb);

    double totalSq = 0.0;
    int totalSamples = 0;
    double activeSq = 0.0;
    int activeSamples = 0;
    int activeBlocks = 0;
    int totalBlocks = 0;
    double blockSq = 0.0;
    int framesInBlock = 0;

    for (int i = rs; i < re; ++i)
    {
        float framePeak = 0.0f;
        double frameSq = 0.0;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float sample = buf.getReadPointer(ch)[i];
            const float absSample = std::abs(sample);
            framePeak = std::max(framePeak, absSample);
            frameSq += static_cast<double>(sample) * static_cast<double>(sample);
        }

        framePeaks.push_back(framePeak);
        analysis.peak = std::max(analysis.peak, framePeak);
        totalSq += frameSq;
        totalSamples += numChannels;
        blockSq += frameSq;
        ++framesInBlock;

        if (framesInBlock == blockFrames || i == re - 1)
        {
            ++totalBlocks;
            const float blockRms = static_cast<float>(std::sqrt(
                blockSq / static_cast<double>(framesInBlock * numChannels)));
            if (blockRms > activeThreshold)
            {
                activeSq += blockSq;
                activeSamples += framesInBlock * numChannels;
                ++activeBlocks;
            }
            blockSq = 0.0;
            framesInBlock = 0;
        }
    }

    if (totalSamples <= 0 || framePeaks.empty())
        return analysis;

    analysis.rms = static_cast<float>(std::sqrt(totalSq / static_cast<double>(totalSamples)));
    analysis.activeRms = activeSamples > 0
        ? static_cast<float>(std::sqrt(activeSq / static_cast<double>(activeSamples)))
        : analysis.rms;
    analysis.activeRatio = totalBlocks > 0
        ? static_cast<float>(activeBlocks) / static_cast<float>(totalBlocks)
        : 0.0f;
    analysis.crestDb = gainToDb(analysis.peak / std::max(analysis.rms, 1.0e-9f));

    auto percentilePeaks = framePeaks;
    const size_t n = percentilePeaks.size();
    const size_t percentileIndex = juce::jlimit<size_t>(
        0u,
        n - 1,
        static_cast<size_t>(std::floor(0.999 * static_cast<double>(n - 1))));
    std::nth_element(percentilePeaks.begin(),
                     percentilePeaks.begin() + static_cast<std::ptrdiff_t>(percentileIndex),
                     percentilePeaks.end());
    analysis.percentilePeak = percentilePeaks[percentileIndex];
    analysis.peakToPercentileDb = gainToDb(
        analysis.peak / std::max(analysis.percentilePeak, 1.0e-9f));

    return analysis;
}

SamplePlayer::NormalizeMode SamplePlayer::chooseNormalizeMode(const NormalizeAnalysis& analysis) const
{
    if (analysis.peak <= 0.0f)
        return NormalizeMode::Bypass;

    const float peakDb = gainToDb(analysis.peak);
    const float activeDb = gainToDb(std::max(analysis.activeRms, analysis.rms));
    const float headroomDb = -peakDb;

    if (peakDb <= kNearSilentPeakDb && activeDb <= kNearSilentActiveDb)
        return NormalizeMode::Bypass;

    if (headroomDb < kHotHeadroomDb)
        return NormalizeMode::PeakCap;

    if (analysis.durationSeconds < kShortTransientSeconds
        || analysis.activeRatio < kTransientActiveRatio
        || analysis.crestDb > kTransientCrestDb
        || analysis.peakToPercentileDb > kTransientPeakGapDb)
    {
        return NormalizeMode::Transient;
    }

    return NormalizeMode::Sustained;
}

float SamplePlayer::chooseNormalizeGain(const NormalizeAnalysis& analysis, NormalizeMode mode) const
{
    if (analysis.peak <= 0.0f)
        return 1.0f;

    const float ceilingGain = dbToGain(kNormalizeCeilingDb) / analysis.peak;

    switch (mode)
    {
        case NormalizeMode::Bypass:
            return 1.0f;

        case NormalizeMode::PeakCap:
            return std::min(1.0f, ceilingGain);

        case NormalizeMode::Transient:
        {
            if (analysis.percentilePeak <= 0.0f)
                return std::min(1.0f, ceilingGain);

            const float transientGain = dbToGain(kTransientPercentileTargetDb) / analysis.percentilePeak;
            return std::min(ceilingGain, std::max(1.0f, transientGain));
        }

        case NormalizeMode::Sustained:
        {
            const float reference = std::max(analysis.activeRms, analysis.rms);
            if (reference <= 0.0f)
                return std::min(1.0f, ceilingGain);

            const float sustainedGain = dbToGain(kSustainedTargetDb) / reference;
            return std::min(ceilingGain, sustainedGain);
        }
    }

    return 1.0f;
}

void SamplePlayer::normalizeBuffer(juce::AudioBuffer<float>& buf,
                                   int regionStart,
                                   int regionEnd,
                                   double bufferSampleRate) const
{
    const int rs = juce::jlimit(0, buf.getNumSamples(), regionStart);
    const int re = juce::jlimit(rs, buf.getNumSamples(), regionEnd);
    const int numChannels = buf.getNumChannels();
    if (re <= rs || numChannels <= 0)
        return;

    const NormalizeAnalysis analysis = analyzeNormalizeRegion(buf, rs, re, bufferSampleRate);
    const NormalizeMode mode = chooseNormalizeMode(analysis);
    const float gain = chooseNormalizeGain(analysis, mode);
    if (!std::isfinite(gain) || std::abs(gain - 1.0f) < 1.0e-4f)
        return;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* data = buf.getWritePointer(ch);
        for (int i = rs; i < re; ++i)
            data[i] *= gain;
    }

    samplerDebugLog("normalizeBuffer mode=" + juce::String(normalizeModeName(mode))
                    + " gainDb=" + juce::String(gainToDb(gain), 2)
                    + " peakDb=" + juce::String(gainToDb(analysis.peak), 2)
                    + " p999Db=" + juce::String(gainToDb(analysis.percentilePeak), 2)
                    + " rmsDb=" + juce::String(gainToDb(analysis.rms), 2)
                    + " activeDb=" + juce::String(gainToDb(analysis.activeRms), 2)
                    + " crestDb=" + juce::String(analysis.crestDb, 2)
                    + " peakGapDb=" + juce::String(analysis.peakToPercentileDb, 2)
                    + " activeRatio=" + juce::String(analysis.activeRatio, 3)
                    + " dur=" + juce::String(analysis.durationSeconds, 3));
}

// ═══════════════════════════════════════════════════════════════════
// Trim leading silence — removes near-zero head with no aesthetic value
// ═══════════════════════════════════════════════════════════════════

void SamplePlayer::trimLeadingSilence(juce::AudioBuffer<float>& buffer) const
{
    const int numSamples = buffer.getNumSamples();
    const int numCh      = buffer.getNumChannels();
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
            if (std::abs(buffer.getReadPointer(ch)[i]) > threshold)
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
        trimmed.copyFrom(ch, 0, buffer, ch, firstActive, newLen);

    buffer = std::move(trimmed);
}
