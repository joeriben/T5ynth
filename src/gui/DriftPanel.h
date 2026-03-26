#pragma once
#include <JuceHeader.h>

/**
 * FX + VOLUME column (10%): delay, reverb, limiter.
 * Each effect has an enable toggle — controls hidden when off.
 */
class DriftPanel : public juce::Component
{
public:
    explicit DriftPanel(juce::AudioProcessorValueTreeState& apvts);
    ~DriftPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    float fs() const;

    // Delay
    juce::ToggleButton delayToggle { "Delay" };
    juce::Slider delayTime, delayFb, delayMix;
    juce::Label delayTimeL, delayFbL, delayMixL;

    // Reverb
    juce::ToggleButton reverbToggle { "Reverb" };
    juce::ComboBox reverbIrBox;
    juce::Slider reverbMix;
    juce::Label reverbMixL;

    // Limiter
    juce::Label limHeader;
    juce::Slider limThresh, limRelease;
    juce::Label limThreshL, limReleaseL;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SA> delayTimeA, delayFbA, delayMixA;
    std::unique_ptr<SA> reverbMixA;
    std::unique_ptr<CA> reverbIrA;
    std::unique_ptr<SA> limThreshA, limReleaseA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DriftPanel)
};
