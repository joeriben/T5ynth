#pragma once
#include <JuceHeader.h>
#include <limits>

/**
 * Waveform display with draggable loop region brackets.
 *
 * Shows the loaded audio waveform with two green circle handles
 * for loop start and loop end. Regions outside the loop are dimmed.
 * Displays "Loop interval: 0.000s – 3.000s" above the waveform.
 */
class WaveformDisplay : public juce::Component,
                        private juce::Timer
{
public:
    /**
     * Small square button with a Material lock / lock_open icon inside a
     * thin border. Sits inside the waveform rectangle, just above the
     * P1/P2/P3 handle line. Click toggles the bound lock state.
     */
    class LockButton : public juce::Component
    {
    public:
        LockButton() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
        void setLocked(bool v) { if (locked_ != v) { locked_ = v; repaint(); } }
        bool isLocked() const { return locked_; }
        std::function<void(bool)> onToggled;
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent&) override;
    private:
        bool locked_ = false;
    };

    WaveformDisplay();
    ~WaveformDisplay() override { stopTimer(); }

    static constexpr const char* kSequencerOneShotDragDescription = "t5ynth-waveform-region";

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    /** Set waveform peak data to display. Clears wavetable mode: the widget goes
     *  back to the sample-with-brackets view (sampler / freeze / neural wavetable). */
    void setWaveform(const float* data, int numSamples);

    /** Switch to WAVETABLE mode: draw the baked table as a frame-decimated,
     *  cycle-readable morph across the full width (front-lit frame 0), instead of
     *  a sample peak-waveform. The strip is N single cycles of frameSize samples
     *  laid end-to-end (mono ch 0); a decimated copy is kept, the caller may
     *  discard the buffer. Suppresses the extraction/loop brackets and the lock
     *  button (a baked table has no source region to slice); the live scan line
     *  (motion playhead) still draws. Message thread. */
    void setWavetableFrames(const juce::AudioBuffer<float>& strip, int frameSize);

    /** True while showing a baked wavetable (setWavetableFrames) rather than a
     *  sample (setWaveform). */
    bool isWavetableMode() const { return wtMode; }

    /** Leave wavetable mode WITHOUT loading a sample: drop the fan + cache,
     *  restore the interactive sample view (brackets + lock button). For an
     *  engine switch OFF a DCO table when no new sample arrives (setWaveform is
     *  the reset when one does). No-op when already a sample view. */
    void exitWavetableMode();

    /** Set total buffer duration in seconds (for time labels). */
    void setBufferDuration(float seconds) { bufferDurationSec = seconds; repaint(); }

    /** Set the region label (e.g. "Loop interval" or "Extraction region"). */
    void setRegionLabel(const juce::String& label) { regionLabel = label; repaint(); }

    /** Reserve pixels at the bottom for the bracket/scan line area. */
    void setBottomReserve(int px) { bottomReserve = px; repaint(); }

    /** Set scan position target (0–1). Smoothing happens in tickScan(). */
    void setScanPosition(float pos) { scanTarget = pos; }
    void setScanVisible(bool v) { scanVisible = v; repaint(); }
    std::function<void(float)> onScanChanged;

    /** Advance scan smoothing one frame. Call from a 30 Hz timer. */
    void tickScan();

    /** Get/set loop region fractions (0–1). P2 = loopStart, P3 = loopEnd. */
    float getLoopStart() const { return loopStart; }
    float getLoopEnd() const { return loopEnd; }
    void setLoopStart(float frac);
    void setLoopEnd(float frac);

    /** Get/set playback start position (P1). Fraction 0–1. */
    float getStartPos() const { return startPos; }
    void setStartPos(float frac);

    /** Callback when brackets are dragged. */
    std::function<void(float start, float end)> onLoopRegionChanged;

    /** Callback when P1 (start position) is dragged. */
    std::function<void(float)> onStartPosChanged;

    /** Callback when a P1/P2/P3 drag gesture finishes. */
    std::function<void()> onMarkerDragFinished;

    /** Access the lock button (owner wires onToggled + setLocked from preset). */
    LockButton& getLockButton() { return lockButton; }

    /** Make the widget inert: no clicks on it or its children, no lock button.
     *  For the LRO, where the owner PAINTS OVER this area (library list) and the
     *  display underneath still holds the last sampler buffer — without this, a
     *  click on that card silently drags P1/P2/P3 and moves a loop the user
     *  cannot see. Sticky: it survives setWaveform / setWavetableFrames /
     *  exitWavetableMode, which each restore interaction when data arrives. */
    void setInert(bool shouldBeInert);
    bool isInert() const { return inert; }

private:
    void timerCallback() override;

    /** The one place that decides whether clicks land: `inert` always wins.
     *  Argument as in juce::Component: allowClicksOnChildComponents. */
    void applyMouseInterception(bool allowClicksOnChildren);
    bool inert = false;

    std::vector<float> waveformData;
    juce::CriticalSection dataLock;

    // Wavetable mode (setWavetableFrames): the baked table drawn as a 2.5D fan
    // along X — frames march left→right with a gentle slant — and a scan cursor
    // that follows the DCO motion sweep. The static fan is cached to an image
    // (rebuilt ONLY on a new bake or a resize); paint() blits that image and
    // overlays just the live cursor cycle + a vertical guide, so the per-tick
    // cost during playback is one image draw + one path, not 256 strokes.
    // wtMode gates the sample-vs-table paint and mutes the extraction brackets /
    // lock button. Guarded by dataLock like waveformData.
    bool wtMode = false;
    std::vector<std::vector<float>> wtFrames;       // decimated single cycles (frame-major)
    juce::Image wtFanCache;                         // the static fan, rebuilt on bake/resize
    bool wtFanDirty = false;
    void renderWtFan();                             // (re)build wtFanCache at the current size
    static constexpr int kWtMaxDrawnFrames = 256;   // full table depth along X
    static constexpr int kWtPointsPerFrame = 128;   // per-cycle resolution kept
    static constexpr float kWtDepthXFrac = 0.68f;   // frames spread across X (approved geometry)
    static constexpr float kWtDepthYFrac = 0.16f;   // gentle 2.5D slant

    float startPos  = 0.0f;   // P1: playback start position
    float loopStart = 0.0f;   // P2: loop begin
    float loopEnd   = 1.0f;   // P3: loop end
    float bufferDurationSec = 0.0f;
    juce::String regionLabel { "Loop interval" };

    enum DragTarget { None, Start, End, StartPos, Scan };
    DragTarget dragging = None;
    bool regionDragArmed = false;
    bool scanVisible = false;
    float scanTarget   = std::numeric_limits<float>::quiet_NaN();
    float scanPos      = std::numeric_limits<float>::quiet_NaN();   // smoothed display value
    float lastScanPx   = -100.0f;

public:
    static constexpr float HANDLE_RADIUS = 7.0f;
    static constexpr float LABEL_HEIGHT  = 18.0f;
private:
    int bottomReserve = 0;

    juce::Rectangle<float> getWaveformArea() const;
    float fracToX(float frac) const;
    float xToFrac(float x) const;
    // Map a pointer X to a scan fraction. In wtMode this inverts the 2.5D fan
    // geometry (paint's cursorX) so a press lands on the frame under the pointer;
    // otherwise it is the plain linear xToFrac.
    float xToScanFrac(float x) const;

    LockButton lockButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};
