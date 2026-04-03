#pragma once
#include <JuceHeader.h>
#include <array>
#include "GuiHelpers.h"

class T5ynthProcessor;

/**
 * SequencerPanel — sequencer + arpeggiator controls + step grid.
 *
 * Layout:
 *   Row 1: LED [>][||]  [Steps▾]  [1/4|1/8|1/16]  BPM[==] 120  MIDI: D#2 v102
 *   Row 2: [Preset▾ wider]  Gate[==]  Glide[==]
 *   Row 3: Step grid (dot | vertical note slider | glide dot | horiz velocity)
 *   Row 4: [Arp ✓]  [Up▾]  [1/16▾]  Oct[==]  Gate[==]
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

    // Section header
    juce::Label seqHeader;

    // Row 1: Transport + step config
    juce::TextButton transportBtn { "PLAY" };
    juce::ComboBox stepCountBox;                  // dropdown 2-32
    static constexpr int kNumDivBtns = 5;
    juce::TextButton divBtns[kNumDivBtns];        // [1/1][1/2][1/4][1/8][1/16]
    juce::ComboBox divisionHidden;                 // hidden, for APVTS attachment
    std::unique_ptr<SliderRow> bpmRow;
    juce::Label midiMonitor;

    // Row 2: Seq params
    juce::ComboBox presetBox;
    std::unique_ptr<SliderRow> gateRow;
    std::unique_ptr<SliderRow> glideRow;

    // Row 3: Step grid
    struct StepColumn : public juce::Component
    {
        int stepIndex = 0;
        T5ynthProcessor* processor = nullptr;
        bool isCurrentStep = false;
        int dragZone = -1;        // 0=dot, 1=note, 2=glide, 3=velocity
        float dragStartVal = 0.f;
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        // Zone geometry (set by paint based on bounds)
        int noteBottom() const { return juce::roundToInt(getHeight() * 0.55f); }
        int velBottom() const { return juce::roundToInt(getHeight() * 0.68f); }
        // bottom 32%: [On][Glide] buttons
    };
    static constexpr int MAX_COLS = 32;
    std::array<std::unique_ptr<StepColumn>, MAX_COLS> stepCols;
    int numVisibleSteps = 16;
    juce::Rectangle<int> gridArea;

    // Row 4: Arp controls (SwitchBox: OFF/Up/Dn/U-D/Rnd)
    static constexpr int kNumModeBtns = 5;
    juce::TextButton arpModeBtns[kNumModeBtns]; // [OFF][Up][Dn][U/D][Rnd]
    juce::ComboBox arpModeBox;                   // hidden, for APVTS
    juce::ComboBox arpRateBox;
    juce::Label arpOctLabel;
    static constexpr int kNumOctBtns = 4;
    juce::TextButton arpOctBtns[kNumOctBtns];  // [1][2][3][4]
    juce::ComboBox arpOctHidden;                 // hidden, for APVTS
    std::unique_ptr<SliderRow> arpGateRow;

    // APVTS attachments
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<SA> bpmA, gateA, glideA, arpGateA;
    std::unique_ptr<CA> divA, presetA, arpModeA, arpRateA, arpOctA;

    int currentStep = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SequencerPanel)
};
