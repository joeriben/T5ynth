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

    /** Live, click-free crossfade to the master's current buffer.
     *  Counterpart to WavetableOscillator::morphToFramesFrom: a held granular
     *  voice adopts a newly generated buffer by spraying both old and new
     *  snapshots and equal-power crossfading over `morphMs` (the global Drift
     *  Crossfade time). Generation-guarded — same buffer ⇒ no-op (never
     *  restarts an in-flight morph); `morphMs<=0` or no current buffer ⇒
     *  instant adopt. MUST be called off the audio thread (it may release the
     *  retained old snapshot); see distributeFreezeBuffer's allowMorph gate. */
    void morphToBufferFrom(const FreezeTextureEngine& master, float morphMs);

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

    static constexpr int kMaxGrains = 12;

    // A self-contained grain sprayer: a grain pool plus its spawn-scheduling
    // state. The engine runs one primary cloud; during a crossfade it also
    // runs a second "morph" cloud so the old and new buffers spray at once.
    // Trivially copyable (POD only) — handing the primary cloud to the morph
    // cloud at morph start is a plain value copy, safe on any thread.
    struct GrainCloud
    {
        std::array<Grain, kMaxGrains> grains {};
        int spawnSamplesUntilNext = 0;
        int nextGrainSlot = 0;
        int spawnIndex = 0;
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
    SnapshotPtr loadMorphFromSnapshot() const;
    void publishMorphFromSnapshot(SnapshotPtr snapshot);

    void resetCloud(GrainCloud& cloud);
    TextureConfig getTextureConfig() const;
    void spawnGrain(GrainCloud& cloud, const Snapshot& snapshot, double effectiveRatio);
    void renderCloud(GrainCloud& cloud,
                     const Snapshot& snapshot,
                     double effectiveRatio,
                     int grainDurationSamples,
                     float& left,
                     float& right);
    void processGrain(Grain& grain,
                      const Snapshot& snapshot,
                      float& left,
                      float& right,
                      float& weight) const;
    float cubicSample(const Snapshot& snapshot, double position) const;
    int getGrainDurationSamples() const;
    int getNextHopSamples(const GrainCloud& cloud, int grainDurationSamples) const;

    SnapshotPtr publishedSnapshot_;
    SnapshotPtr morphFromSnapshot_;   // old buffer retained during a crossfade

    // Off-audio-thread reclaim bin. morphToBufferFrom overwrites the two snapshot
    // members on a voice the audio thread is actively rendering; the audio thread
    // holds a one-sample-lived local of each (processSampleStereo's `snapshot`/
    // `oldSnap`). To guarantee that local can never be the last reference (which
    // would free the std::vector<float> on the audio thread — CLAUDE.md #4), the
    // current snapshots are parked here before being overwritten and released only
    // at the top of the NEXT morphToBufferFrom (message thread). morph calls are at
    // least an inference apart — orders of magnitude longer than a one-sample local
    // — so by the next call no audio local can still alias them. The audio thread
    // NEVER reads these, so overwriting/clearing them never races an audio local.
    SnapshotPtr retiredPublished_;
    SnapshotPtr retiredMorphFrom_;

    juce::uint64 nextGeneration_ = 1;

    double playbackSampleRate_ = 44100.0;
    int maxBlockSize_ = 512;

    double transposeRatio_ = 1.0;
    double glideTargetRatio_ = 1.0;
    double glideRatioIncr_ = 0.0;
    int glideSamplesLeft_ = 0;
    float pitchModFactor_ = 1.0f;

    float targetPosition_ = 0.5f;
    float smoothedPosition_ = 0.5f;
    float currentPosition_ = 0.5f;
    float positionSmoothCoeff_ = 1.0f;
    float textureLengthMs_ = 260.0f;
    int textureMode_ = 1;
    float stereoWidth_ = 0.25f;

    // Crossfade-morph state (all morph work in processSampleStereo is gated on
    // morphActive_, so steady state costs nothing).
    //
    // morphActive_ is atomic and doubles as the publication gate for the whole
    // morph setup. morphToBufferFrom (message/reprepare thread) writes morphCloud_,
    // the snapshots, morphAlpha_ and morphIncrement_ and THEN stores morphActive_
    // with release; processSampleStereo (audio thread) loads it with acquire FIRST.
    // That happens-before edge makes the setup visible across the message→audio
    // hand-off on weakly-ordered cores (Apple Silicon). NOTE: processBlock DOES hold
    // getCallbackLock() on every shipped format (Standalone/VST3/AU — the JUCE wrapper
    // locks it), and the off-thread morphToBufferFrom takes the same lock, so the two
    // are in fact mutually excluded — this release/acquire is belt-and-suspenders, not
    // the sole guard; do NOT drop the lock trusting it. Were that lock guarantee ever
    // lost, the morphAlpha_/grain writes during a *re*-morph (morphActive_ already
    // true) would race — but only as the same crash-safe, glitch-only POD race
    // Wavetable accepts (aligned fields, clamped reads, no pointers in Grain).
    std::atomic<bool> morphActive_ { false };
    float morphAlpha_ = 1.0f;       // 0 = all old buffer, 1 = all new buffer
    float morphIncrement_ = 0.0f;

    GrainCloud cloud_;        // primary cloud (the new/current buffer)
    GrainCloud morphCloud_;   // old buffer's cloud, only live during a crossfade
};
