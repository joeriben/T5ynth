#pragma once
#include <JuceHeader.h>

/**
 * Sparse early-reflection bank — the front half of Moorer's 1979 reverb.
 *
 * JUCE's juce::dsp::Reverb is Freeverb, which implements only the Schroeder-Moorer
 * LATE field: eight recursive combs (with Moorer's damping in the feedback) plus
 * four allpasses. That half is cheap, because the recursion does the work. The half
 * Freeverb omits is the one you cannot get for free: a NON-recursive multitap delay
 * whose taps encode an actual room geometry.
 *
 * Two consequences of the omission, both measurable in juce_Reverb.h:
 *   - roomSize touches no delay length at all (only comb feedback), so "room size"
 *     is really decay time and every room has identical reflection timing;
 *   - the entire stereo image is stereoSpread = 23 samples = 0.52 ms.
 *
 * This stage supplies both. Tap times scale with Room, so Room becomes geometry;
 * L and R use entirely different times, so the image comes from real decorrelation
 * rather than half a millisecond of offset.
 *
 * Perceptually the point is not "more reverb": reflections that arrive in the
 * precedence-effect window fuse INTO the direct sound instead of masking it, so the
 * attack transient survives at high Mix — the opposite of what a bare diffuse wash
 * does to it.
 *
 * Real-time safe: the delay line is sized in prepare(); process() allocates nothing,
 * locks nothing, and reads only cached per-block state.
 */
class EarlyReflections
{
public:
    EarlyReflections() = default;

    void prepare(double sampleRate, int samplesPerBlock);
    void reset();

    /** Room size 0..1 — scales every tap time and therefore the sense of space. */
    void setRoomSize(float size);

    /** Stereo width 0..1. 0 collapses the reflection pattern to mono. */
    void setWidth(float w);

    /** Replaces `buffer` with its early reflections. */
    void process(juce::AudioBuffer<float>& buffer);

    /** Longest tap currently in use, in samples — how long content stays pending
        inside the bank after the input goes quiet. */
    float longestTapSamples() const;

    static constexpr int kNumTaps = 7;

private:
    // Tap times in ms at scale 1.0 (= Room 0.7, the default). Incommensurate, so no
    // two taps form a simple ratio and the bank cannot ring like a comb; spacing
    // widens with time, the way a real room's reflections thin out before the
    // diffuse field takes over. L and R share NO time — that is the stereo image.
    static constexpr float kTapMsL[kNumTaps] = { 10.9f, 17.3f, 24.7f, 33.1f, 43.9f, 56.3f, 71.9f };
    static constexpr float kTapMsR[kNumTaps] = { 13.7f, 20.3f, 28.9f, 38.7f, 49.1f, 62.9f, 79.7f };

    // Room -> time scale. 0.27x at Room 0 (a tight room: first reflection ~3 ms),
    // 1.0x at the 0.7 default, 1.31x at Room 1.0 (a hall: taps out past 100 ms).
    static float roomScale(float room) { return (0.35f + 1.35f * room) / 1.295f; }

    // Longest tap at maximum Room, plus headroom, decides the buffer size.
    static constexpr float kMaxTapMs = 79.7f;

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> line { 8192 };

    double sr = 44100.0;
    bool   prepared = false;

    float  targetScale  = 1.0f;   // set by setRoomSize
    float  currentScale = 1.0f;   // approaches targetScale one step per block...
    float  width = 1.0f;

    // ...and the tap positions are then interpolated PER SAMPLE from the previous
    // block's scale to this one. Advancing the scale only once per block would move
    // every read position discontinuously — a splice, i.e. a click, at every block
    // boundary for the whole duration of a Room drag (measured at -6 dBFS, 86 times
    // a second). The comment that used to sit here claimed a per-block one-pole
    // prevented that; it does not, because during a continuous drag the pole's step
    // per block simply equals the knob's.
    float  tapBaseL[kNumTaps] {};   // tap time in samples at scale 1.0
    float  tapBaseR[kNumTaps] {};
    float  tapGain[kNumTaps] {};
    float  maxTapSamples = 0.0f;

    void resolveTapBases();
};
