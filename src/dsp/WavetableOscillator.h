#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cmath>
#include <atomic>

/**
 * Mip-mapped wavetable oscillator.
 *
 * Ported from AI4ArtsEd's wavetable-processor.js + useWavetableOsc.ts.
 *
 * Architecture:
 * - Frames extracted from AI-generated audio via pitch-synchronous analysis
 * - 8 mip levels per frame (band-limited: 1024 → 8 harmonics)
 * - Phase accumulator with Catmull-Rom cubic interpolation between frames
 * - Scan position smoothing (one-pole lowpass, ~5ms)
 * - Mip level selection based on playback frequency
 *
 * Thread safety:
 * - Double-buffer (two MipData slots + atomic index) allows the GUI thread
 *   to rebuild frames without blocking the audio thread.
 * - Shared-mode voices read from the master's active slot via pointer.
 */
class WavetableOscillator
{
public:
    static constexpr int FRAME_SIZE = 2048;
    static constexpr int HALF_FRAME = FRAME_SIZE / 2;
    static constexpr int NUM_MIP_LEVELS = 8;
    static constexpr int MIN_FRAMES = 8;

    WavetableOscillator() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void reset();

    /** Extract wavetable frames from an audio buffer (pitch-synchronous or windowed).
     *  startFrac/endFrac define the extraction region as fraction of the buffer (0–1).
     *  Thread-safe: builds into inactive slot, then atomically swaps. */
    void extractFramesFromBuffer(const juce::AudioBuffer<float>& buffer, double bufferSampleRate,
                                 float startFrac = 0.0f, float endFrac = 1.0f,
                                 int maxFrames = 256);

    /** Set playback frequency in Hz. Cancels any active glide. */
    void setFrequency(float hz) { targetFrequency = hz; glideFreqSamplesLeft = 0; }
    float getFrequency() const { return targetFrequency; }

    /** True if a frequency glide is in progress. */
    bool isGliding() const { return glideFreqSamplesLeft > 0; }

    /** Smooth frequency ramp to target Hz over durationMs (portamento). */
    void glideToFrequency(float hz, float durationMs);

    /** Set scan control (0–1).
     *  Without auto-scan this is the absolute frame position.
     *  With auto-scan it acts as a forward bias on the temporal path. */
    void setScanPosition(float pos) { scanControl_ = juce::jlimit(0.0f, 1.0f, pos); }
    float getCurrentScanPosition() const { return juce::jlimit(0.0f, 1.0f, smoothedScan); }

    /** Enable/disable Catmull-Rom interpolation between frames. */
    void setInterpolation(bool enabled) { doInterpolate = enabled; }

    // ── Auto-scan (sampler-style temporal progression) ──────────────

    /** Enable auto-scan: scan position advances per sample at the original
     *  audio rate, independent of playback pitch. Sampling = wavetable + auto-scan. */
    void setAutoScan(bool on) { autoScan_ = on; }
    bool isAutoScan() const { return autoScan_; }

    /** Set auto-scan rate from original audio timing.
     *  Scan advances from 0→1 in (bufferLen / bufferSR) seconds. */
    void setAutoScanRate(double bufferSR, int bufferLen);

    /** Set auto-scan loop region (frame fractions). */
    enum class LoopMode { OneShot, Loop, PingPong };
    void setAutoScanLoop(float startFrac, float endFrac, LoopMode mode);

    /** Set auto-scan start position (P1). Fraction 0–1. */
    void setAutoScanStartPos(float frac);
    float getAutoScanStartPos() const { return autoScanStartPos_; }

    /** Reset scan to start position (call on noteOn). */
    void retriggerAutoScan();

    /** Extract contiguous (non-pitch-synchronous) frames from audio buffer.
     *  For sampler-style playback where frames represent temporal chunks. */
    void extractContiguousFrames(const juce::AudioBuffer<float>& buffer, double bufferSR,
                                 float startFrac = 0.0f, float endFrac = 1.0f);

    /** Process a single sample. */
    float processSample();

    /** True if frames have been loaded. */
    bool hasFrames() const { return getActiveMipData().numFrames > 0; }

    int getNumFrames() const { return getActiveMipData().numFrames; }

    /** Share mip-mapped frames from a master oscillator (for polyphonic voices).
     *  Shared-mode oscillators have their own phase/frequency/scan but read
     *  from the master's frame data. extractFramesFromBuffer is a no-op. */
    void shareFramesFrom(const WavetableOscillator& source);

private:
    struct PitchEstimate {
        float hz = -1.0f;
        float confidence = 0.0f;
    };

    /** Immutable snapshot of mip-mapped frame data.
     *  GUI thread writes to inactive slot, audio thread reads active slot. */
    struct MipData {
        std::vector<std::vector<std::vector<float>>> frames; // [level][frameIdx][sample]
        int numFrames = 0;
        int numLevels = 0;
    };

    MipData mipSlots_[2];
    std::atomic<int> activeSlot_ { 0 };

    // Shared mode: voice reads from master oscillator's active slot
    const WavetableOscillator* sharedSource_ = nullptr;

    double sampleRate = 44100.0;

    // Phase accumulator
    double phase = 0.0;
    float targetFrequency = 440.0f;

    // Frequency glide state
    float glideFreqTarget = 440.0f;
    float glideFreqIncr = 0.0f;
    int   glideFreqSamplesLeft = 0;

    // Scan control + smoothed effective position
    float scanControl_ = 0.0f;
    float smoothedScan = 0.0f;
    float scanSmoothCoeff = 0.0f;

    bool doInterpolate = true;

    // Auto-scan state (per-voice, not shared)
    bool autoScan_ = false;
    double autoScanPos_ = 0.0;           // 0.0–1.0 current position
    double autoScanIncr_ = 0.0;          // per-sample increment
    float autoScanStartPos_ = 0.0f;      // P1: start position (fraction 0–1)
    float autoScanLoopStart_ = 0.0f;     // P2: loop begin (fraction 0–1)
    float autoScanLoopEnd_ = 1.0f;       // P3: loop end (fraction 0–1)
    LoopMode autoScanLoopMode_ = LoopMode::Loop;
    bool  autoScanInFirstPass_ = true;   // true until first loop boundary hit
    int   autoScanDirection_ = 1;        // +1 forward, -1 backward

    /** Return the active mip data (own or shared master's). Lock-free for audio thread. */
    const MipData& getActiveMipData() const
    {
        if (sharedSource_ != nullptr)
            return sharedSource_->mipSlots_[sharedSource_->activeSlot_.load(std::memory_order_acquire)];
        return mipSlots_[activeSlot_.load(std::memory_order_acquire)];
    }

    // FFT helpers for mip-level generation
    static void fft(std::vector<double>& re, std::vector<double>& im);
    static void ifft(std::vector<double>& re, std::vector<double>& im);
    void generateMipLevels(const std::vector<std::vector<float>>& srcFrames);

    // Pitch detection (simplified YIN)
    static PitchEstimate analyzePitchWindow(const float* data, int length, double sr);
    static float detectPitch(const float* data, int length, double sr);
    static int nearestZeroCrossing(const float* data, int length, int pos, int maxSearch);
    static std::vector<float> extractResampledPeriod(const float* data, int totalSamples,
                                                     double start, double periodSamples);
    static double computeLoopBoundaryError(const std::vector<float>& frame);

    // Lanczos sinc interpolation for frame extraction
    static constexpr int SINC_KERNEL_A = 6;
    static float lanczosSample(const float* src, int srcLen, double pos);
};
