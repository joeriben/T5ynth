#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

/**
 * Conservative freeze/texture playback engine.
 *
 * This is intentionally not an open granular spray engine. It uses a small
 * fixed set of time-scheduled grains around a held sample position, keeping
 * grain duration/density independent from pitch while staying close to the
 * source material.
 */
class FreezeTextureEngine
{
public:
    FreezeTextureEngine() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void reset();

    void loadBuffer(const juce::AudioBuffer<float>& buffer, double bufferSampleRate);
    void shareBufferFrom(const FreezeTextureEngine& master);

    bool hasAudio() const;

    void retrigger();
    void setPosition(float position);
    float getCurrentPosition() const { return currentPosition_; }

    void setTextureMode(int mode);
    void setTextureLengthMs(float ms);
    void setStereoWidth(float width);
    void setTransposeRatio(double ratio);
    void glideToRatio(double targetRatio, float durationMs);
    void setPitchModulation(float factor);

    float processSample();
    void processSampleStereo(float& left, float& right);

private:
    struct Snapshot
    {
        std::vector<float> samples;
        double sampleRate = 44100.0;
        juce::uint64 generation = 0;
    };

    using SnapshotPtr = std::shared_ptr<const Snapshot>;

    struct Grain
    {
        bool active = false;
        int ageSamples = 0;
        int durationSamples = 1;
        double readPosLeft = 0.0;
        double readPosRight = 0.0;
        double readStep = 1.0;
        float ampLeft = 1.0f;
        float ampRight = 1.0f;
    };

    struct TextureConfig
    {
        float defaultLengthMs = 260.0f;
        int overlap = 8;
        float blur = 0.35f;
        float motionMs = 10.0f;
        float spreadMs = 18.0f;
        float pitchCents = 2.0f;
    };

    SnapshotPtr loadPublishedSnapshot() const;
    void publishSnapshot(SnapshotPtr snapshot);

    void resetGrains();
    TextureConfig getTextureConfig() const;
    void spawnGrain(const Snapshot& snapshot, double effectiveRatio);
    void processGrain(Grain& grain,
                      const Snapshot& snapshot,
                      float& left,
                      float& right,
                      float& weight) const;
    float cubicSample(const Snapshot& snapshot, double position) const;
    int getGrainDurationSamples() const;
    int getNextHopSamples(int grainDurationSamples) const;

    SnapshotPtr publishedSnapshot_;
    juce::uint64 nextGeneration_ = 1;

    double playbackSampleRate_ = 44100.0;
    int maxBlockSize_ = 512;

    double transposeRatio_ = 1.0;
    double glideTargetRatio_ = 1.0;
    double glideRatioIncr_ = 0.0;
    int glideSamplesLeft_ = 0;
    float pitchModFactor_ = 1.0f;
    int spawnSamplesUntilNext_ = 0;
    int nextGrainSlot_ = 0;
    int spawnIndex_ = 0;

    float targetPosition_ = 0.5f;
    float smoothedPosition_ = 0.5f;
    float currentPosition_ = 0.5f;
    float positionSmoothCoeff_ = 1.0f;
    float textureLengthMs_ = 260.0f;
    int textureMode_ = 1;
    float stereoWidth_ = 0.25f;

    static constexpr int kMaxGrains = 12;
    std::array<Grain, kMaxGrains> grains_ {};
};
