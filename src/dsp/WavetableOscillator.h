#pragma once
#include <JuceHeader.h>
#include <vector>
#include <cmath>

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

    /** Extract wavetable frames from an audio buffer (pitch-synchronous or windowed). */
    void extractFramesFromBuffer(const juce::AudioBuffer<float>& buffer, double bufferSampleRate);

    /** Set playback frequency in Hz. */
    void setFrequency(float hz) { targetFrequency = hz; glideFreqSamplesLeft = 0; }

    /** Smooth frequency ramp to target Hz over durationMs (portamento). */
    void glideToFrequency(float hz, float durationMs);

    /** Set scan position (0–1, morphs between frames). */
    void setScanPosition(float pos) { targetScanPosition = juce::jlimit(0.0f, 1.0f, pos); }

    /** Enable/disable Catmull-Rom interpolation between frames. */
    void setInterpolation(bool enabled) { doInterpolate = enabled; }

    /** Process a single sample. */
    float processSample();

    /** True if frames have been loaded. */
    bool hasFrames() const { return numFrames > 0; }

    int getNumFrames() const { return numFrames; }

    /** Share mip-mapped frames from a master oscillator (for polyphonic voices).
     *  Shared-mode oscillators have their own phase/frequency/scan but read
     *  from the master's frame data. extractFramesFromBuffer is a no-op. */
    void shareFramesFrom(const WavetableOscillator& source);

private:
    // Mip-mapped frames: mipFrames[level][frameIndex] = vector<float>(FRAME_SIZE)
    std::vector<std::vector<std::vector<float>>> mipFrames;
    const std::vector<std::vector<std::vector<float>>>* sharedMipFrames = nullptr;
    bool sharedMode = false;

    int numFrames = 0;
    int numLevels = 0;
    double sampleRate = 44100.0;

    // Phase accumulator
    double phase = 0.0;
    float targetFrequency = 440.0f;

    // Frequency glide state
    float glideFreqTarget = 440.0f;
    float glideFreqIncr = 0.0f;
    int   glideFreqSamplesLeft = 0;

    // Scan position with smoothing
    float targetScanPosition = 0.0f;
    float smoothedScan = 0.0f;
    float scanSmoothCoeff = 0.0f;

    bool doInterpolate = true;

    /** Return the active frames (own or shared). */
    const std::vector<std::vector<std::vector<float>>>& getActiveFrames() const
    {
        return sharedMode ? *sharedMipFrames : mipFrames;
    }

    // FFT helpers for mip-level generation
    static void fft(std::vector<double>& re, std::vector<double>& im);
    static void ifft(std::vector<double>& re, std::vector<double>& im);
    void generateMipLevels(const std::vector<std::vector<float>>& srcFrames);

    // Pitch detection (simplified YIN)
    static float detectPitch(const float* data, int length, double sr);

    // Lanczos sinc interpolation for frame extraction
    static constexpr int SINC_KERNEL_A = 6;
    static float lanczosSample(const float* src, int srcLen, double pos);
};
