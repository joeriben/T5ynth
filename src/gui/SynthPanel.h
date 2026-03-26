#pragma once
#include <JuceHeader.h>
#include "WaveformDisplay.h"

class T5ynthProcessor;

/**
 * MODE + FILTER column (40%).
 * Engine: horizontal switch (Looper | Wavetable), waveform, scan.
 * Filter: toggle, type, slope, cutoff, reso, mix, kbd track.
 */
class SynthPanel : public juce::Component
{
public:
    explicit SynthPanel(T5ynthProcessor& processor);
    ~SynthPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    float fs() const;

    T5ynthProcessor& processorRef;

    // Engine mode: horizontal switch
    juce::TextButton looperBtn { "Looper" };
    juce::TextButton wavetableBtn { "Wavetable" };
    WaveformDisplay waveformDisplay;

    // Scan
    juce::Slider scanSlider;
    juce::Label scanLabel, scanValue, scanHint;

    // Filter
    juce::ToggleButton filterToggle { "Filter" };
    juce::ComboBox filterTypeBox, filterSlopeBox;
    juce::Slider cutoffSlider, resoSlider, filterMixSlider, kbdTrackSlider;
    juce::Label cutoffLabel, cutoffValue;
    juce::Label resoLabel, resoValue;
    juce::Label filterMixLabel, filterMixValue;
    juce::Label kbdTrackLabel, kbdTrackValue;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SA> scanA, cutoffA, resoA, filterMixA, kbdTrackA;
    std::unique_ptr<CA> filterTypeA, filterSlopeA;

    // Engine mode is driven by APVTS but we use buttons, not ComboBox
    std::unique_ptr<CA> engineModeA;
    juce::ComboBox engineModeHidden; // hidden, just for APVTS attachment

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthPanel)
};
