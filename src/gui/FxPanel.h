#pragma once
#include <JuceHeader.h>
#include "GuiHelpers.h"

/**
 * FX column (20%): Delay + Reverb.
 * Each section has a SwitchBox header (OFF + variants) and slider rows.
 * Limiter is internal only — no GUI controls.
 */
class FxPanel : public juce::Component
{
public:
    explicit FxPanel(juce::AudioProcessorValueTreeState& apvts);
    ~FxPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    float fs() const;
    void updateVisibility();

    // Delay section
    juce::Label delayHeader;
    static constexpr int kNumDelayBtns = 2;
    juce::TextButton delayTypeBtns[kNumDelayBtns]; // [OFF][Stereo]
    juce::ComboBox delayTypeHidden;
    juce::Rectangle<int> delayTypeSwitchBounds;
    std::unique_ptr<SliderRow> delayTimeRow, delayFbRow, delayDampRow, delayMixRow;

    // Reverb section
    juce::Label reverbHeader;
    static constexpr int kNumReverbBtns = 5;
    juce::TextButton reverbTypeBtns[kNumReverbBtns]; // [OFF][Dark][Med][Brt][Algo]
    juce::ComboBox reverbTypeHidden;
    juce::Rectangle<int> reverbTypeSwitchBounds;
    std::unique_ptr<SliderRow> reverbMixRow;
    std::unique_ptr<SliderRow> algoRoomRow, algoDampRow, algoWidthRow;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SA> delayTimeA, delayFbA, delayDampA, delayMixA;
    std::unique_ptr<SA> reverbMixA, algoRoomA, algoDampA, algoWidthA;
    std::unique_ptr<CA> delayTypeA, reverbTypeA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxPanel)
};
