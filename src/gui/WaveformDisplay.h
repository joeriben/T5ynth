#pragma once
#include <JuceHeader.h>

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
    WaveformDisplay();
    ~WaveformDisplay() override = default;

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

    /** Get/set loop region fractions (0–1). */
    float getLoopStart() const { return loopStart; }
    float getLoopEnd() const { return loopEnd; }
    void setLoopStart(float frac);
    void setLoopEnd(float frac);

    /** Callback when brackets are dragged. */
    std::function<void(float start, float end)> onLoopRegionChanged;

private:
    void timerCallback() override;

    std::vector<float> waveformData;
    juce::CriticalSection dataLock;

    float loopStart = 0.0f;
    float loopEnd   = 1.0f;
    float bufferDurationSec = 0.0f;
    juce::String regionLabel { "Loop interval" };

    enum DragTarget { None, Start, End };
    DragTarget dragging = None;

    static constexpr float HANDLE_RADIUS = 7.0f;
    static constexpr float LABEL_HEIGHT  = 18.0f;

    juce::Rectangle<float> getWaveformArea() const;
    float fracToX(float frac) const;
    float xToFrac(float x) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveformDisplay)
};
