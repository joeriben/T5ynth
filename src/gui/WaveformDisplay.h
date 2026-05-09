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

    /** Set waveform peak data to display. */
    void setWaveform(const float* data, int numSamples);

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

private:
    void timerCallback() override;

    std::vector<float> waveformData;
    juce::CriticalSection dataLock;

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

    LockButton lockButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};
