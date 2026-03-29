#pragma once
#include <JuceHeader.h>

/**
 * Sample playback engine (ported from useAudioLooper.ts).
 *
 * Features:
 *   - Forward loop, one-shot, ping-pong (palindrome buffer)
 *   - Equal-power crossfade baked into buffer at loop boundary
 *   - Cross-correlation loop-point optimization
 *   - Peak normalization (0.95 target)
 *   - Fractional loop start/end ("brackets")
 *   - MIDI transposition via playback rate
 *   - Retrigger (hard restart for non-legato)
 */
class AudioLooper
{
public:
    enum class LoopMode { OneShot, Loop, PingPong };

    AudioLooper() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void reset();

    /** Load audio data. Calls preparePlaybackBuffer() internally. */
    void loadBuffer(const juce::AudioBuffer<float>& buffer, double bufferSampleRate);

    /** Process a block (writes into output buffer). */
    void processBlock(juce::AudioBuffer<float>& output);

    /** Render one mono sample (channel 0, linear interpolation). Advances read position. */
    float processSample();

    bool hasAudio() const { return audioLoaded; }

    void play()  { playing = true; }
    void stop()  { playing = false; }

    /** Hard stop + restart from loop start. For non-legato MIDI retrigger. */
    void retrigger();

    /** Set transposition from MIDI note (60 = original pitch). */
    void setMidiNote(int note);

    /** Smooth pitch ramp to target semitones over durationMs (portamento). */
    void glideToSemitones(int semitones, float durationMs);

    bool isPlaying() const { return playing; }

    // ─── Loop region ("brackets") ───
    /** Set loop start as fraction of buffer (0.0–1.0). */
    void setLoopStart(float frac);
    /** Set loop end as fraction of buffer (0.0–1.0). */
    void setLoopEnd(float frac);

    float getLoopStart() const { return loopStartFrac; }
    float getLoopEnd()   const { return loopEndFrac; }

    /** Share playback buffer from a master looper (for polyphonic voices).
     *  Shared-mode loopers have their own read position but read from the
     *  master's prepared buffer. loadBuffer/preparePlaybackBuffer are no-ops. */
    void shareBufferFrom(const AudioLooper& master);

    // ─── Modes and processing ───
    void setLoopMode(LoopMode mode);
    void setCrossfadeMs(float ms);
    void setNormalize(bool on);
    void setLoopOptimize(bool on);

    LoopMode getLoopMode()  const { return loopMode; }
    float getCrossfadeMs()  const { return crossfadeMsVal; }
    bool  getNormalize()    const { return normalizeOn; }

    /** True if settings changed and buffer needs re-preparation. */
    bool needsReprepare() const { return needsReprepareFlag; }

    /** Re-build playBuffer from originalBuffer with current settings. */
    void preparePlaybackBuffer();

private:
    // Original (unprocessed) buffer — kept for re-preparation when settings change
    juce::AudioBuffer<float> originalBuffer;
    // Prepared (crossfaded/palindromed/normalized) playback buffer
    juce::AudioBuffer<float> playBuffer;

    double playbackSampleRate = 44100.0;
    double bufferOriginalSR   = 44100.0;
    double readPosition       = 0.0;
    double transposeRatio     = 1.0;
    double glideTargetRatio   = 1.0;
    double glideRatioIncr     = 0.0;  // per-sample increment
    int    glideSamplesLeft   = 0;
    bool   audioLoaded        = false;
    bool   playing            = false;

    // Loop region (fractions of original buffer length)
    float loopStartFrac = 0.0f;
    float loopEndFrac   = 1.0f;

    // Playback bounds in samples (within playBuffer)
    int playStart  = 0;
    int playEnd    = 0;
    int coldStart  = 0; // past crossfade zone for cold starts

    // Settings
    LoopMode loopMode     = LoopMode::Loop;
    float crossfadeMsVal  = 150.0f;
    bool  normalizeOn     = false;
    bool  loopOptimizeOn  = false;

    // Dirty flag — when settings change, re-prepare on next processBlock
    bool needsReprepareFlag = false;

    // Shared-buffer mode: reads from external playBuffer, no ownership
    bool sharedMode = false;
    const juce::AudioBuffer<float>* sharedPlayBuffer = nullptr;

    /** Cross-correlation loop-end optimizer (channel 0). */
    int optimizeLoopEnd(const float* data, int loopStart, int loopEnd, int bufLen) const;

    /** Apply equal-power crossfade at loop boundary. */
    void applyLoopCrossfade(juce::AudioBuffer<float>& buf, int loopStart, int& loopEnd) const;

    /** Create palindrome for ping-pong mode. */
    void createPalindrome(const juce::AudioBuffer<float>& src, int loopStart, int loopEnd,
                          juce::AudioBuffer<float>& dest, int& palindromeEnd) const;

    /** Peak-normalize buffer to 0.95. */
    void normalizeBuffer(juce::AudioBuffer<float>& buf) const;

    static constexpr int XCORR_WINDOW = 512;
    static constexpr int XCORR_SEARCH = 2000;
};
