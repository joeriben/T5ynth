#include "FreezeTextureEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
double deterministicUnit(int index, std::uint32_t salt)
{
    std::uint32_t x = static_cast<std::uint32_t>(index) + salt;
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return static_cast<double>(x) / static_cast<double>(std::numeric_limits<std::uint32_t>::max());
}

double deterministicBipolar(int index, std::uint32_t salt)
{
    return deterministicUnit(index, salt) * 2.0 - 1.0;
}
}

void FreezeTextureEngine::prepare(double sampleRate, int samplesPerBlock)
{
    playbackSampleRate_ = sampleRate > 0.0 ? sampleRate : 44100.0;
    maxBlockSize_ = samplesPerBlock;
    positionSmoothCoeff_ = 1.0f - std::exp(-1.0f / static_cast<float>(playbackSampleRate_ * 0.020));
}

void FreezeTextureEngine::reset()
{
    publishSnapshot(nullptr);
    resetGrains();
    transposeRatio_ = 1.0;
    glideTargetRatio_ = 1.0;
    glideRatioIncr_ = 0.0;
    glideSamplesLeft_ = 0;
    pitchModFactor_ = 1.0f;
    targetPosition_ = 0.5f;
    smoothedPosition_ = 0.5f;
    currentPosition_ = 0.5f;
}

void FreezeTextureEngine::loadBuffer(const juce::AudioBuffer<float>& buffer, double bufferSampleRate)
{
    if (buffer.getNumSamples() <= 0 || buffer.getNumChannels() <= 0)
    {
        publishSnapshot(nullptr);
        return;
    }

    auto snapshot = std::make_shared<Snapshot>();
    snapshot->sampleRate = bufferSampleRate > 0.0 ? bufferSampleRate : 44100.0;
    snapshot->generation = nextGeneration_++;
    snapshot->samples.resize(static_cast<size_t>(buffer.getNumSamples()), 0.0f);

    const int channels = buffer.getNumChannels();
    const float invChannels = 1.0f / static_cast<float>(channels);
    for (int ch = 0; ch < channels; ++ch)
    {
        const float* src = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            snapshot->samples[static_cast<size_t>(i)] += src[i] * invChannels;
    }

    publishSnapshot(std::move(snapshot));
}

void FreezeTextureEngine::shareBufferFrom(const FreezeTextureEngine& master)
{
    publishSnapshot(master.loadPublishedSnapshot());
    textureLengthMs_ = master.textureLengthMs_;
    textureMode_ = master.textureMode_;
    stereoWidth_ = master.stereoWidth_;
    resetGrains();
}

bool FreezeTextureEngine::hasAudio() const
{
    auto snapshot = loadPublishedSnapshot();
    return snapshot != nullptr && snapshot->samples.size() >= 8;
}

void FreezeTextureEngine::retrigger()
{
    resetGrains();
    smoothedPosition_ = targetPosition_;
    currentPosition_ = targetPosition_;
}

void FreezeTextureEngine::setPosition(float position)
{
    targetPosition_ = juce::jlimit(0.0f, 1.0f, position);
}

void FreezeTextureEngine::setTextureMode(int mode)
{
    const int clamped = juce::jlimit(0, 3, mode);
    if (clamped == textureMode_)
        return;

    textureMode_ = clamped;
    textureLengthMs_ = getTextureConfig().defaultLengthMs;
}

void FreezeTextureEngine::setTextureLengthMs(float ms)
{
    textureLengthMs_ = juce::jlimit(120.0f, 700.0f, ms);
}

void FreezeTextureEngine::setStereoWidth(float width)
{
    stereoWidth_ = juce::jlimit(0.0f, 1.0f, width);
}

void FreezeTextureEngine::setTransposeRatio(double ratio)
{
    transposeRatio_ = juce::jlimit(0.0625, 16.0, ratio);
    glideSamplesLeft_ = 0;
}

void FreezeTextureEngine::glideToRatio(double targetRatio, float durationMs)
{
    const int samples = std::max(1, static_cast<int>(durationMs * 0.001f * static_cast<float>(playbackSampleRate_)));
    glideTargetRatio_ = juce::jlimit(0.0625, 16.0, targetRatio);
    glideRatioIncr_ = (glideTargetRatio_ - transposeRatio_) / static_cast<double>(samples);
    glideSamplesLeft_ = samples;
}

void FreezeTextureEngine::setPitchModulation(float factor)
{
    pitchModFactor_ = juce::jlimit(0.0625f, 16.0f, factor);
}

float FreezeTextureEngine::processSample()
{
    float left = 0.0f;
    float right = 0.0f;
    processSampleStereo(left, right);
    return 0.5f * (left + right);
}

void FreezeTextureEngine::processSampleStereo(float& left, float& right)
{
    auto snapshot = loadPublishedSnapshot();
    if (snapshot == nullptr || snapshot->samples.size() < 8)
    {
        left = 0.0f;
        right = 0.0f;
        return;
    }

    if (glideSamplesLeft_ > 0)
    {
        transposeRatio_ += glideRatioIncr_;
        --glideSamplesLeft_;
        if (glideSamplesLeft_ == 0)
            transposeRatio_ = glideTargetRatio_;
    }

    smoothedPosition_ += (targetPosition_ - smoothedPosition_) * positionSmoothCoeff_;
    currentPosition_ = juce::jlimit(0.0f, 1.0f, smoothedPosition_);

    const double effectiveRatio = juce::jlimit(0.0625, 16.0,
        transposeRatio_ * static_cast<double>(pitchModFactor_));

    const int grainDurationSamples = getGrainDurationSamples();
    if (spawnSamplesUntilNext_ <= 0)
    {
        spawnGrain(*snapshot, effectiveRatio);
        spawnSamplesUntilNext_ += getNextHopSamples(grainDurationSamples);
    }
    --spawnSamplesUntilNext_;

    float sum = 0.0f;
    float sumRight = 0.0f;
    float weightSum = 0.0f;
    for (auto& grain : grains_)
    {
        float weight = 0.0f;
        float grainLeft = 0.0f;
        float grainRight = 0.0f;
        processGrain(grain, *snapshot, grainLeft, grainRight, weight);
        sum += grainLeft;
        sumRight += grainRight;
        weightSum += weight;
    }

    if (weightSum <= 1.0e-5f)
    {
        left = 0.0f;
        right = 0.0f;
        return;
    }

    left = sum / weightSum;
    right = sumRight / weightSum;
}

FreezeTextureEngine::SnapshotPtr FreezeTextureEngine::loadPublishedSnapshot() const
{
    return std::atomic_load_explicit(&publishedSnapshot_, std::memory_order_acquire);
}

void FreezeTextureEngine::publishSnapshot(SnapshotPtr snapshot)
{
    std::atomic_store_explicit(&publishedSnapshot_, snapshot, std::memory_order_release);
}

void FreezeTextureEngine::resetGrains()
{
    for (auto& grain : grains_)
        grain = {};
    spawnSamplesUntilNext_ = 0;
    nextGrainSlot_ = 0;
    spawnIndex_ = 0;
}

FreezeTextureEngine::TextureConfig FreezeTextureEngine::getTextureConfig() const
{
    switch (textureMode_)
    {
        case 0:  return { 380.0f, 10, 0.10f,  0.0f,  6.0f, 0.5f }; // Hold
        case 2:  return { 190.0f,  8, 0.55f, 22.0f, 28.0f, 3.0f }; // Air
        case 3:  return { 145.0f,  8, 0.75f, 42.0f, 42.0f, 4.5f }; // Cloud
        case 1:
        default: return { 260.0f,  9, 0.35f, 10.0f, 18.0f, 2.0f }; // Silk
    }
}

void FreezeTextureEngine::spawnGrain(const Snapshot& snapshot, double effectiveRatio)
{
    auto& grain = grains_[static_cast<size_t>(nextGrainSlot_)];
    nextGrainSlot_ = (nextGrainSlot_ + 1) % kMaxGrains;

    const auto config = getTextureConfig();
    const int index = spawnIndex_++;
    const int sampleCount = static_cast<int>(snapshot.samples.size());
    const double sourceRate = snapshot.sampleRate > 0.0 ? snapshot.sampleRate : 44100.0;
    const double durationJitter = 0.04 + 0.12 * static_cast<double>(config.blur);
    const double durationScale = 1.0 + deterministicBipolar(index, 0x3d20adeau) * durationJitter;
    const int durationSamples = std::max(8, static_cast<int>(std::round(
        static_cast<double>(getGrainDurationSamples()) * durationScale)));
    const double cents = deterministicBipolar(index, 0x79b4c3d1u) * static_cast<double>(config.pitchCents);
    const double readStep = (sourceRate / playbackSampleRate_)
        * effectiveRatio
        * std::pow(2.0, cents / 1200.0);
    const double readSpan = std::max(1.0, static_cast<double>(durationSamples - 1) * readStep);
    const double centreSample = 2.0 + currentPosition_ * static_cast<double>(std::max(1, sampleCount - 5));

    const double motionPhase = static_cast<double>(index) * 0.071;
    const double motionSamples = std::sin(motionPhase) * sourceRate * static_cast<double>(config.motionMs) * 0.001
        + std::sin(motionPhase * 0.37 + 1.91) * sourceRate * static_cast<double>(config.motionMs) * 0.00045;
    const double spreadSamples = std::min(sourceRate * static_cast<double>(config.spreadMs) * 0.001,
                                          readSpan * (0.08 + 0.34 * static_cast<double>(config.blur)));
    const double deterministicOffset = deterministicBipolar(index, 0x9e3779b9u) * spreadSamples;
    const double stereoOffset = static_cast<double>(stereoWidth_)
        * sourceRate
        * (0.0015 + 0.010 * static_cast<double>(config.blur))
        * deterministicBipolar(index, 0x45d9f3bu);

    const double proposedStart = centreSample + motionSamples + deterministicOffset - readSpan * 0.5;
    const double minStart = 1.0;
    const double maxStart = std::max(minStart, static_cast<double>(sampleCount) - 2.0 - readSpan);
    const double start = juce::jlimit(minStart, maxStart, proposedStart);

    const float pan = static_cast<float>(deterministicBipolar(index, 0xa511e9b3u))
        * stereoWidth_
        * (0.15f + 0.30f * config.blur);
    const float leftGain = juce::jlimit(0.65f, 1.15f, 1.0f - pan);
    const float rightGain = juce::jlimit(0.65f, 1.15f, 1.0f + pan);

    grain.active = true;
    grain.ageSamples = 0;
    grain.durationSamples = durationSamples;
    grain.readPosLeft = juce::jlimit(minStart, maxStart, start - stereoOffset);
    grain.readPosRight = juce::jlimit(minStart, maxStart, start + stereoOffset);
    grain.readStep = readStep;
    grain.ampLeft = leftGain;
    grain.ampRight = rightGain;
}

void FreezeTextureEngine::processGrain(Grain& grain,
                                       const Snapshot& snapshot,
                                       float& left,
                                       float& right,
                                       float& weight) const
{
    if (!grain.active)
    {
        left = 0.0f;
        right = 0.0f;
        weight = 0.0f;
        return;
    }

    const float phase = juce::jlimit(0.0f, 1.0f,
        static_cast<float>(grain.ageSamples) / static_cast<float>(std::max(1, grain.durationSamples - 1)));
    weight = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * phase);
    left = cubicSample(snapshot, grain.readPosLeft) * weight * grain.ampLeft;
    right = cubicSample(snapshot, grain.readPosRight) * weight * grain.ampRight;

    grain.readPosLeft += grain.readStep;
    grain.readPosRight += grain.readStep;
    ++grain.ageSamples;
    if (grain.ageSamples >= grain.durationSamples)
        grain.active = false;
}

float FreezeTextureEngine::cubicSample(const Snapshot& snapshot, double position) const
{
    const int size = static_cast<int>(snapshot.samples.size());
    if (size <= 0)
        return 0.0f;

    position = juce::jlimit(0.0, static_cast<double>(size - 1), position);
    int i1 = static_cast<int>(std::floor(position));
    const float t = static_cast<float>(position - static_cast<double>(i1));

    const int i0 = juce::jmax(0, i1 - 1);
    i1 = juce::jlimit(0, size - 1, i1);
    const int i2 = juce::jmin(size - 1, i1 + 1);
    const int i3 = juce::jmin(size - 1, i1 + 2);

    const float p0 = snapshot.samples[static_cast<size_t>(i0)];
    const float p1 = snapshot.samples[static_cast<size_t>(i1)];
    const float p2 = snapshot.samples[static_cast<size_t>(i2)];
    const float p3 = snapshot.samples[static_cast<size_t>(i3)];

    const float a = -0.5f * p0 + 1.5f * p1 - 1.5f * p2 + 0.5f * p3;
    const float b = p0 - 2.5f * p1 + 2.0f * p2 - 0.5f * p3;
    const float c = -0.5f * p0 + 0.5f * p2;
    return ((a * t + b) * t + c) * t + p1;
}

int FreezeTextureEngine::getGrainDurationSamples() const
{
    const auto requested = static_cast<int>(std::round(
        static_cast<double>(textureLengthMs_) * 0.001 * playbackSampleRate_));
    return std::max(8, requested);
}

int FreezeTextureEngine::getNextHopSamples(int grainDurationSamples) const
{
    const auto config = getTextureConfig();
    const int baseHop = std::max(1, grainDurationSamples / std::max(1, config.overlap));
    const double jitterDepth = 0.04 + 0.14 * static_cast<double>(config.blur);
    const double jitter = 1.0 + deterministicBipolar(spawnIndex_, 0x632be59bu) * jitterDepth;
    return std::max(1, static_cast<int>(std::round(static_cast<double>(baseHop) * jitter)));
}
