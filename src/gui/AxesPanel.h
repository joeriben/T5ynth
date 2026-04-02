#pragma once
#include <JuceHeader.h>
#include <vector>
#include <map>

/**
 * 3 semantic axis slots — each choosable from the 8 most effective axes
 * for short (1-3s) audio samples. Color-coded with pink/blue/green dots.
 *
 * Validated via spectral analysis: these 8 axes show Mel-distance > 0.70
 * at 1s duration. PCA axes collapse at short durations and are not offered.
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
        std::unique_ptr<juce::Label> valueLabel;
        std::unique_ptr<juce::Label> poleLabelA; // left pole
        std::unique_ptr<juce::Label> poleLabelB; // right pole
        int axisIndex = -1;
    };

    juce::Label header;
    juce::Label noteLabel;
    std::vector<AxisSlot> slots;  // 3

    void initSlot(AxisSlot& slot, const juce::StringArray& options, int axisIndex);
    void layoutSlots(std::vector<AxisSlot>& slots, juce::Rectangle<int>& area, float f, int dotOffset);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AxesPanel)
};
