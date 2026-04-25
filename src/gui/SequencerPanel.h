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
 *   Row 2: [Preset▾ wider]  Gate[==]
 *   Row 3: Step grid (dot | vertical note slider | glide dot | horiz velocity)
 *   Row 4: [Arp ✓]  [Up▾]  [1/16▾]  Oct[==]  Gate[==]
 */
class SequencerPanel : public juce::Component, private juce::Timer
{
public:
    explicit SequencerPanel(T5ynthProcessor& processor);
    ~SequencerPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void syncStepCount();
    void savePatternAsync();
    void loadPatternAsync();
    void showHeaderOverflowMenu();

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
    juce::Rectangle<float> midiLedBounds;
    juce::TextButton headerOverflowBtn { "..." };

    // Octave shift [-2][-1][0][+1][+2]
    static constexpr int kNumOctShiftBtns = 5;
    juce::TextButton octShiftBtns[kNumOctShiftBtns];
    juce::ComboBox octShiftHidden;

    // Icon LookAndFeel for save/load buttons (declared before buttons — destroyed after)
    struct IconLnF : juce::LookAndFeel_V4
    {
        juce::Path icon;
        void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
        void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
    };
    IconLnF saveLnf, loadLnf;

    // Row 2: Seq params
    juce::ComboBox presetBox;
    juce::TextButton seqSaveBtn { "S" };
    juce::TextButton seqLoadBtn { "L" };
    std::unique_ptr<SliderRow> gateRow;
    std::unique_ptr<SliderRow> shuffleRow;

    // Row 3: Step grid
    struct StepColumn : public juce::Component
    {
        int stepIndex = 0;
        T5ynthProcessor* processor = nullptr;
        bool isCurrentStep = false;
        int dragZone = -1;        // 0=dot, 1=note, 2=glide, 3=velocity
        float dragStartVal = 0.f;
        bool noteHoldPreviewActive = false;
        int noteHoldPreviewNote = -1;
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;
        void mouseDrag(const juce::MouseEvent& e) override;
        void mouseUp(const juce::MouseEvent& e) override;
        // Zone geometry (set by paint based on bounds)
        int noteBottom() const { return juce::roundToInt(getHeight() * 0.68f); }
        int velBottom() const { return juce::roundToInt(getHeight() * 0.84f); }
        // bottom 16%: [On][Bind] buttons (half height, like velocity bar)
    };
    static constexpr int MAX_COLS = 32;
    std::array<std::unique_ptr<StepColumn>, MAX_COLS> stepCols;
    int numVisibleSteps = 16;
    juce::Rectangle<int> gridArea;

    // Generative sequencer controls
    juce::TextButton genTransportBtn { "GEN" };  // mode toggle, not transport
    std::unique_ptr<SliderRow> genStepsRow, genPulsesRow, genRotationRow, genMutationRow;
    juce::ComboBox genScaleRootBox, genScaleTypeBox;
    // Range switchbox [1][2][3][4]
    static constexpr int kNumRangeBtns = 4;
    juce::TextButton genRangeBtns[kNumRangeBtns];
    juce::ComboBox genRangeHidden;  // hidden, for APVTS
    juce::Label genRangeLabel;

    // Fix toggle buttons (lock/unlock icons)
    juce::TextButton genFixStepsBtn, genFixPulsesBtn, genFixRotationBtn, genFixMutationBtn;

    // ── Polyphony (Phase 5) ──────────────────────────────────────────────
    // Shared pitch-field controls
    juce::ComboBox genFieldModeBox;
    std::unique_ptr<SliderRow> genFieldRateRow;   // TODO: inline-value variant of SliderRow
    // Per-extra-strand (indices 0..3 map to strands 2..5)
    static constexpr int kNumExtraStrands = 4;
    juce::TextButton strandEnableBtns[kNumExtraStrands];
    juce::ComboBox   strandRoleBoxes[kNumExtraStrands];
    // Per-strand differentiators on the second GEN row, vertically aligned
    // beneath the [Sx][Role] cluster they belong to.
    //   Div:  ComboBox showing "1/4x".."4x" — value is its own label
    //   Oct:  5-switch-button row "-2 -1 0 +1 +2" mirroring SeqOctave's
    //         convention; the slider stays as a hidden APVTS bridge.
    //   Dom:  small Label "Dom" + slider 0..1
    static constexpr int kStrandOctBtns = 5;
    juce::ComboBox   strandDivBoxes[kNumExtraStrands];
    juce::TextButton strandOctBtns[kNumExtraStrands][kStrandOctBtns];
    juce::Slider     strandOctaveSliders[kNumExtraStrands]; // hidden APVTS bridge
    juce::Slider     strandDomSliders[kNumExtraStrands];
    juce::Label      strandDomLabels[kNumExtraStrands];


    // Gen visualisation (painted in paint(), positioned in resized())
    juce::Rectangle<int> genVisArea;
    bool genModeActive = false;
    int genCurrentStep = -1;

    // Row 4: Arp controls (SwitchBox: OFF/Up/Dn/U-D/Rnd)
    static constexpr int kNumModeBtns = 5;
    juce::TextButton arpModeBtns[kNumModeBtns]; // [OFF][Up][Dn][U/D][Rnd]
    juce::ComboBox arpModeBox;                   // hidden, for APVTS
    juce::ComboBox arpRateBox;
    juce::Label arpOctLabel;
    static constexpr int kNumOctBtns = 4;
    juce::TextButton arpOctBtns[kNumOctBtns];  // [1][2][3][4]
    juce::ComboBox arpOctHidden;                 // hidden, for APVTS

    // APVTS attachments
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<SA> bpmA, gateA, shuffleA, genStepsA, genPulsesA, genRotationA, genMutationA;
    std::unique_ptr<CA> divA, presetA, arpModeA, arpRateA, arpOctA, octShiftA,
                        genScaleRootA, genScaleTypeA, genRangeA;
    std::unique_ptr<BA> genRunningA;
    std::unique_ptr<BA> genFixStepsA, genFixPulsesA, genFixRotationA, genFixMutationA;

    // Polyphony attachments
    std::unique_ptr<CA> genFieldModeA;
    std::unique_ptr<SA> genFieldRateA;
    std::unique_ptr<BA> strandEnableA[kNumExtraStrands];
    std::unique_ptr<CA> strandRoleA[kNumExtraStrands];
    std::unique_ptr<CA> strandDivA[kNumExtraStrands];
    std::unique_ptr<SA> strandOctaveA[kNumExtraStrands];
    std::unique_ptr<SA> strandDomA[kNumExtraStrands];

    int currentStep = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SequencerPanel)
};
