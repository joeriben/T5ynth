#pragma once
#include <JuceHeader.h>
#include <array>
#include "GuiHelpers.h"

class T5ynthProcessor;

/**
 * SequencerPanel — sequencer + arpeggiator controls + step grid.
 *
 * Layout:
 *   Row 1: LED [>][||]  Steps:[5][8][10][16][32]  [Div▾]  BPM[=====] 120
 *   Row 2: [Preset▾]  Gate[=====] 80%  Glide[=====] 80ms  MIDI: D#2 v102
 *   Row 3: Step grid (note offset | active dot | G badge | velocity bar)
 *   Row 4: [Arp ✓]  [Up▾]  [1/16▾]  Oct[=====]  ArpGate[=====] 80%
 */
class SequencerPanel : public juce::Component, private juce::Timer
{
public:
    explicit SequencerPanel(T5ynthProcessor& processor);
    ~SequencerPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void syncStepCount();

    T5ynthProcessor& processorRef;

    // Row 1: Transport + step config
    juce::TextButton playButton { ">" };
    juce::TextButton stopButton { "||" };
    std::array<juce::TextButton, 5> stepCountBtns;
    static constexpr int STEP_COUNTS[] = { 5, 8, 10, 16, 32 };
    juce::ComboBox divisionBox;
    std::unique_ptr<SliderRow> bpmRow;

    // Row 2: Seq params
    juce::ComboBox presetBox;
    std::unique_ptr<SliderRow> gateRow;
    std::unique_ptr<SliderRow> glideRow;
    juce::Label midiMonitor;

    // Row 3: Step grid
    struct StepColumn : public juce::Component
    {
        int stepIndex = 0;
        T5ynthProcessor* processor = nullptr;
        bool isCurrentStep = false;
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
    };
    static constexpr int MAX_COLS = 32;
    std::array<std::unique_ptr<StepColumn>, MAX_COLS> stepCols;
    int numVisibleSteps = 16;
    juce::Rectangle<int> gridArea;

    // Row 4: Arp controls
    juce::ToggleButton arpEnable { "Arp" };
    juce::ComboBox arpModeBox;
    juce::ComboBox arpRateBox;
    std::unique_ptr<SliderRow> arpOctRow;
    std::unique_ptr<SliderRow> arpGateRow;

    // APVTS attachments
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SA> bpmA, gateA, glideA, arpOctA, arpGateA;
    std::unique_ptr<CA> divA, presetA, arpModeA, arpRateA;
    std::unique_ptr<BA> arpEnableA;

    int currentStep = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SequencerPanel)
};
