#pragma once
#include <JuceHeader.h>
#include "GuiHelpers.h"

/**
 * FX column (20%): Delay + Reverb.
 * Each section has a SwitchBox header (OFF + variants) and slider rows.
 * Limiter is internal only — no GUI controls.
 */
class T5ynthProcessor; // forward decl

class FxPanel : public juce::Component,
                private juce::Timer
{
public:
    FxPanel(juce::AudioProcessorValueTreeState& apvts, T5ynthProcessor& processor);
    ~FxPanel() override { stopTimer(); }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    class WordmarkComponent : public juce::Component
    {
    public:
        void paint(juce::Graphics& g) override;
    };

    void timerCallback() override;
    float fs() const;
    void updateVisibility();
    T5ynthProcessor& processorRef;

    // Clock-button LnF — declared BEFORE the button using it so destruction
    // order is button-first, LnF-second. Never call setLookAndFeel(nullptr)
    // on the button during teardown.
    ClockButtonLnF delayClockLnf;

    // Delay section
    juce::Label delayHeader;
    static constexpr int kNumDelayBtns = 4;
    juce::TextButton delayTypeBtns[kNumDelayBtns]; // [OFF][Dig][PP][Tape]
    juce::ComboBox delayTypeHidden;
    juce::Rectangle<int> delayTypeSwitchBounds;
    juce::Rectangle<int> delayCardBounds;      // framed module card (header + content)
    juce::TextButton delayClockBtn;            // BPM-sync clock button
    juce::ComboBox   delayClockModeHidden;     // hidden, APVTS-attached
    std::unique_ptr<SliderRow> delayTimeRow, delayFbRow, delayDampRow, delayMixRow;
    std::unique_ptr<SliderRow> delayDivisionRow; // same rect as delayTimeRow when sync

    // Reverb section
    juce::Label reverbHeader;
    static constexpr int kNumReverbBtns = 5;
    juce::TextButton reverbTypeBtns[kNumReverbBtns]; // [OFF][Dark][Med][Brt][Algo]
    juce::ComboBox reverbTypeHidden;
    juce::Rectangle<int> reverbTypeSwitchBounds;
    juce::Rectangle<int> reverbCardBounds;     // framed module card (header + content)
    std::unique_ptr<SliderRow> reverbMixRow;
    std::unique_ptr<SliderRow> algoRoomRow, algoDampRow, algoWidthRow;

    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    std::unique_ptr<SA> delayTimeA, delayFbA, delayDampA, delayMixA;
    std::unique_ptr<SA> delayDivisionA;
    std::unique_ptr<SA> reverbMixA, algoRoomA, algoDampA, algoWidthA;
    std::unique_ptr<CA> delayTypeA, reverbTypeA, delayClockModeA;

    WordmarkComponent wordmark;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FxPanel)
};
