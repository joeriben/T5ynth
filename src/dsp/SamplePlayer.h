#pragma once
#include <JuceHeader.h>
#include <memory>
#include <vector>
#include "signalsmith-stretch.h"

/**
 * Sample playback engine (ported from useSamplePlayer.ts).
 *
 * Features:
 *   - Forward loop, one-shot, ping-pong (palindrome buffer)
 *   - Equal-power crossfade baked into buffer at loop boundary
 *   - Cross-correlation loop-point optimization
 *   - Signal-aware normalization (sustained vs. transient vs. hot/silent)
 *   - Fractional loop start/end ("brackets")
 *   - MIDI transposition via Signalsmith Stretch (pitch-preserving)
 *   - 6-tap Lanczos sinc interpolation for buffer reads
 *   - Retrigger (hard restart for non-legato)
 */
class SamplePlayer
{
public:
    enum class LoopMode { OneShot, Loop, PingPong };
    enum class PitchShiftQuality { Bypass, Efficient, Default, HighQuality };

    struct PrepareConfig
    {
        LoopMode loopMode = LoopMode::Loop;
        float crossfadeMs = 150.0f;
        bool normalizeOn = false;
        int loopOptimizeLevel = 0;
        float startPosFrac = 0.0f;
        float loopStartFrac = 0.0f;
        float loopEndFrac = 1.0f;
    };

    struct PreparedPlaybackState
    {
        juce::AudioBuffer<float> playBuffer;
        double bufferOriginalSR = 44100.0;
        int playStart = 0;
        int playEnd = 0;
        int coldStart = 0;
        bool audioLoaded = false;
    };

    struct PreparedBufferLoad
    {
        juce::AudioBuffer<float> originalBuffer;
        PreparedPlaybackState playbackState;
    };

    SamplePlayer() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void reset();

    /** Load audio data. Calls preparePlaybackBuffer() internally. */
    void loadBuffer(const juce::AudioBuffer<float>& buffer, double bufferSampleRate);

    /** Capture the settings which shape prepared playback data. */
    PrepareConfig capturePrepareConfig() const;

    /** Build immutable playback data off the audio thread. */
    PreparedBufferLoad prepareBufferLoad(const juce::AudioBuffer<float>& buffer,
                                         double bufferSampleRate,
                                         const PrepareConfig& config) const;

    /** Publish already-prepared playback data with matching config. */
    void applyPreparedBufferLoad(PreparedBufferLoad prepared, const PrepareConfig& config);

    /** Process a block (writes into output buffer). */
    void processBlock(juce::AudioBuffer<float>& output);

    /** Render one mono sample (channel 0, Lanczos sinc interpolation).
     *  Uses speed-based transposition (Bypass mode). Advances read position. */
    float processSample();

    /** Render a block of pitch-shifted mono samples.
     *  Uses Signalsmith Stretch for pitch-preserving transposition.
     *  Falls back to speed-based transposition in Bypass mode. */
    void renderPitchedBlock(float* output, int numSamples);

    bool hasAudio() const { return audioLoaded; }

    void play()  { playing = true; }
    void stop()  { playing = false; }

    /** Hard stop + restart from loop start. For non-legato MIDI retrigger. */
    void retrigger();

    /** Set transposition from MIDI note (60 = original pitch, 12-TET). */
    void setMidiNote(int note);

    /** Set transposition ratio directly (1.0 = original pitch).
     *  Use for microtuning: pass tunedHz(note) / tunedHz(60). */
    void setTransposeRatio(double ratio);

    /** Smooth pitch ramp to target semitones over durationMs (portamento, 12-TET). */
    void glideToSemitones(int semitones, float durationMs);

    /** Smooth pitch ramp to target ratio over durationMs (portamento, tuning-aware). */
    void glideToRatio(double targetRatio, float durationMs);

    /** Set pitch modulation factor (1.0 = no mod). Applied on top of transposeRatio
     *  in renderPitchedBlock. Use for envelope/LFO pitch modulation at block rate. */
    void setPitchModulation(float factor) { pitchModFactor = factor; }
    void setSourceGain(float gain) { sourceGain_ = juce::jmax(0.0f, gain); }
    float getSourceGain() const { return sourceGain_; }

    bool isPlaying() const { return playing; }

    // ─── Loop region ("brackets") ───
    /** Set loop start as fraction of buffer (0.0–1.0). P2. */
    void setLoopStart(float frac);
    /** Set loop end as fraction of buffer (0.0–1.0). P3. */
    void setLoopEnd(float frac);
    /** Set playback start position as fraction of buffer (0.0–1.0). P1. */
    void setStartPos(float frac);

    float getLoopStart() const { return loopStartFrac; }
    float getLoopEnd()   const { return loopEndFrac; }
    float getStartPos()  const { return startPosFrac; }

    /** True if P1/P2/P3 are locked (preset-preserved / user-adjusted).
     *  When set, auto-positioning on regeneration is skipped. Serialized
     *  as engine.pointsLocked in the preset JSON. */
    bool  getPointsLocked() const { return pointsLocked_; }
    void  setPointsLocked(bool v) { pointsLocked_ = v; }

    // ─── Wavetable extraction region (independent of Sampler P2/P3) ───
    void  setWtExtractStart(float frac) { wtExtractStartFrac_ = juce::jlimit(0.0f, 1.0f, frac); }
    void  setWtExtractEnd(float frac)   { wtExtractEndFrac_   = juce::jlimit(0.0f, 1.0f, frac); }
    float getWtExtractStart() const { return wtExtractStartFrac_; }
    float getWtExtractEnd()   const { return wtExtractEndFrac_; }

    // ─── Start position modulation offset (Scan→P1 in Sampler mode) ───
    void  setStartPosOffset(float v) { startPosOffset_ = v; }
    float getStartPosOffset() const  { return startPosOffset_; }

    /** Share playback buffer from a master player (for polyphonic voices).
     *  Shared-mode players have their own read position but read from the
     *  master's prepared buffer. loadBuffer/preparePlaybackBuffer are no-ops. */
    void shareBufferFrom(const SamplePlayer& master);

    /** Detach from the shared master buffer and keep the current playback source
     *  as a local snapshot for an already-running note. */
    void freezeSharedBuffer();

    // ─── Modes and processing ───
    void setLoopMode(LoopMode mode);
    void setCrossfadeMs(float ms);
    void setNormalize(bool on);
    void setLoopOptimizeLevel(int level);   // 0=Off, 1=Low, 2=High

    LoopMode getLoopMode()  const { return loopMode; }
    float getCrossfadeMs()  const { return crossfadeMsVal; }
    bool  getNormalize()    const { return normalizeOn; }

    /** True if settings changed and buffer needs re-preparation. */
    bool needsReprepare() const { return needsReprepareFlag; }

    /** Re-build playBuffer from originalBuffer with current settings. */
    void preparePlaybackBuffer();

    /** Estimate mono RMS/peak over the untransposed P1/P2/P3 playback path.
     *  `gains` is typically a synthetic DCA envelope used only for analysis. */
    float estimatePlaybackRms(const float* gains, int numSamples, float* outPeak = nullptr) const;

    /** Estimated audible path length in output samples for one reference pass. */
    int estimateReferenceLengthSamples() const;

    /** One-line state dump for temporary diagnostics. */
    juce::String debugStateString() const;

    // ─── Pitch shift quality ───
    void setPitchShiftQuality(PitchShiftQuality quality);
    PitchShiftQuality getPitchShiftQuality() const { return pitchQuality; }

private:
    enum class NormalizeMode { Bypass, PeakCap, Transient, Sustained };

    struct NormalizeAnalysis
    {
        float durationSeconds = 0.0f;
        float peak = 0.0f;
        float percentilePeak = 0.0f;
        float rms = 0.0f;
        float activeRms = 0.0f;
        float crestDb = 0.0f;
        float peakToPercentileDb = 0.0f;
        float activeRatio = 0.0f;
    };

    struct PlaybackSnapshot
    {
        juce::AudioBuffer<float> playBuffer;
        double bufferOriginalSR = 44100.0;
    };

    // Original (unprocessed) buffer — kept for re-preparation when settings change
    juce::AudioBuffer<float> originalBuffer;
    // Prepared (crossfaded/palindromed/normalized) playback buffer
    juce::AudioBuffer<float> playBuffer;
    std::shared_ptr<const PlaybackSnapshot> playbackSnapshot_;

    double playbackSampleRate = 44100.0;
    double bufferOriginalSR   = 44100.0;
    double readPosition       = 0.0;
    double transposeRatio     = 1.0;
    float  pitchModFactor     = 1.0f;  // block-rate pitch mod from envelopes/LFOs
    float  sourceGain_        = 1.0f;  // constant per-voice gain applied before stretch
    double glideTargetRatio   = 1.0;
    double glideRatioIncr     = 0.0;  // per-sample increment
    int    glideSamplesLeft   = 0;
    bool   audioLoaded        = false;
    bool   playing            = false;

    // Loop region (fractions of original buffer length)
    float startPosFrac  = 0.0f;   // P1: playback start position
    float loopStartFrac = 0.0f;   // P2: loop begin
    float loopEndFrac   = 1.0f;   // P3: loop end
    bool  pointsLocked_ = false;  // true → Generate never touches P1/P2/P3

    // WT extraction region (independent of Sampler P2/P3)
    float wtExtractStartFrac_ = 0.0f;
    float wtExtractEndFrac_   = 1.0f;

    // Modulation offset for P1 (Scan→StartPos in Sampler mode)
    float startPosOffset_ = 0.0f;

    // Playback bounds in samples (within playBuffer)
    int playStart  = 0;
    int playEnd    = 0;
    int coldStart  = 0; // past crossfade zone (Loop mode only)

    // 3-point playback state (per-voice, reset on retrigger)
    bool inFirstPass_   = true;   // true until first loop boundary hit
    int  playDirection_ = 1;      // +1 forward, -1 backward

    // Settings
    LoopMode loopMode     = LoopMode::Loop;
    float crossfadeMsVal  = 150.0f;
    bool  normalizeOn     = false;
    int   loopOptimizeLevel = 0;   // 0=Off, 1=Low, 2=High

    // Dirty flag — when settings change, re-prepare on next processBlock
    bool needsReprepareFlag = false;

    // Shared-buffer mode: follows a published snapshot from another player.
    bool sharedMode = false;

    // ─── Pitch shifting (Signalsmith Stretch) ───
    signalsmith::stretch::SignalsmithStretch<float> stretcher;
    PitchShiftQuality pitchQuality = PitchShiftQuality::Default;
    bool stretcherPrepared = false;
    bool stretcherNeedsPriming = false;
    std::vector<float> rawReadBuf;
    int maxBlockSize = 512;

    // ─── Catmull-Rom cubic interpolation (fast, high quality) ───

    /** 4-point Catmull-Rom cubic interpolation at fractional buffer position. */
    float cubicSample(double pos) const;

    const juce::AudioBuffer<float>& currentPlaybackBuffer() const;
    double currentBufferOriginalSR() const;

    /** Read raw samples at 1:1 speed (SR-corrected only, no transposition).
     *  Advances readPosition and handles loop wrapping. */
    void readRawSamples(float* output, int numSamples);

    /** Advance read position by speedMagnitude in current direction.
     *  Handles first-pass logic, loop wrapping, and direction reversal.
     *  Returns false if playback stopped (OneShot end). */
    bool advancePosition(double speedMagnitude);

    /** Initialize/reconfigure the Signalsmith Stretch instance. */
    void prepareStretcher();

    /** Prime the stretcher by feeding half-window of audio (output discarded).
     *  Eliminates STFT ramp-up latency so first real output sample is valid. */
    void primeStretcher();

    PreparedPlaybackState preparePlaybackState(const juce::AudioBuffer<float>& sourceBuffer,
                                               double sourceSampleRate,
                                               const PrepareConfig& config) const;
    void applyPreparedPlaybackState(PreparedPlaybackState preparedState);

    /** Cross-correlation loop-end optimizer (channel 0). */
    int optimizeLoopEnd(const float* data, int loopStart, int loopEnd, int bufLen, int level) const;

    /** Apply equal-power crossfade at loop boundary. */
    static void applyLoopCrossfade(juce::AudioBuffer<float>& buf, int loopStart, int& loopEnd,
                                   float crossfadeMs, double bufferSampleRate);

    /** Analyze the audible region and choose a linear normalization mode. */
    NormalizeAnalysis analyzeNormalizeRegion(const juce::AudioBuffer<float>& buf,
                                             int regionStart,
                                             int regionEnd,
                                             double bufferSampleRate) const;
    static const char* normalizeModeName(NormalizeMode mode);
    NormalizeMode chooseNormalizeMode(const NormalizeAnalysis& analysis) const;
    float chooseNormalizeGain(const NormalizeAnalysis& analysis, NormalizeMode mode) const;

    /** Apply signal-aware linear normalization to the audible play region only. */
    void normalizeBuffer(juce::AudioBuffer<float>& buf,
                         int regionStart,
                         int regionEnd,
                         double bufferSampleRate) const;

    /** Remove leading near-zero samples (< ~-60 dB) from a source buffer. */
    void trimLeadingSilence(juce::AudioBuffer<float>& buffer) const;

    // Per-level xcorr parameters: [0]=unused, [1]=Low, [2]=High
    static constexpr int XCORR_WINDOW[3] = { 0,  512, 2048 };
    static constexpr int XCORR_SEARCH[3] = { 0, 2000, 8000 };
};
