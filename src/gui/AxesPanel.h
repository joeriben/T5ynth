#pragma once
#include <JuceHeader.h>
#include <vector>
#include <map>

/**
 * Semantic + PCA axes. Separated sections with dropdown selection.
 * Max 3 semantic slots, max 6 PCA slots.
 */
class AxesPanel : public juce::Component
{
public:
    AxesPanel();
    ~AxesPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::map<juce::String, float> getAxisValues() const;

private:
    float fs() const;

    struct AxisSlot
    {
        std::unique_ptr<juce::ComboBox> dropdown;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> labelA, labelB, valueLabel;
    };

    juce::Label semHeader, pcaHeader;
    std::vector<AxisSlot> semSlots;  // max 3
    std::vector<AxisSlot> pcaSlots;  // max 6

    void initSlot(AxisSlot& slot, const juce::StringArray& options);
    void layoutSlots(std::vector<AxisSlot>& slots, juce::Rectangle<int>& area, float f);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AxesPanel)
};
