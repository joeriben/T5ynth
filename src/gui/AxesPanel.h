#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>
#include <map>
#include <limits>
#include <cmath>

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
    /** Display labels for the semantic-axis dropdown — index N corresponds
     *  to ComboBox `dropdownId == N + 1`. Slot 0 is the disabled placeholder. */
    static const juce::StringArray& getAxisLabels();

    AxesPanel();
    ~AxesPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::map<juce::String, float> getAxisValues() const;

    /** Return axis values with per-slot additive offsets from drift LFO. */
    std::map<juce::String, float> getAxisValuesWithOffsets(float off1, float off2, float off3) const;

    /** Set per-slot ghost offsets for drift visualization. NaN = no ghost. */
    void setGhostOffsets(float o1, float o2, float o3);

    /** Per-slot state for preset save/load. */
    struct SlotState { int dropdownId = 1; float value = 0.0f; };
    std::array<SlotState, 3> getSlotStates() const;
    void setSlotStates(const std::array<SlotState, 3>& states);

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

    static constexpr float NO_GHOST = std::numeric_limits<float>::quiet_NaN();
    float ghostOffsets_[3] = { NO_GHOST, NO_GHOST, NO_GHOST };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AxesPanel)
};
