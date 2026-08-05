#pragma once
#include <JuceHeader.h>
#include "GuiHelpers.h"
#include <array>
#include <vector>
#include <map>
#include <limits>
#include <cmath>

// Per-model axis table entry (id/display/key). Full definition and the two
// tables (SAO vs SA3) live file-scope in AxesPanel.cpp — this header only
// ever holds a pointer to one of them.
struct AxisDef;

/**
 * 3 semantic axis slots — each choosable from a per-model set of axes.
 * SAO / AudioLDM2 offer the 8 most effective single-word axes for short
 * (1-3s) audio samples. SA3 offers 3 bundled-pole axes instead (see
 * setModelIsSA3). Each active axis tints its slider track + value
 * (pink/blue/green) so multiple active axes stay distinguishable.
 *
 * Validated via spectral analysis: the SAO axes show Mel-distance > 0.70
 * at 1s duration. PCA axes collapse at short durations and are not offered.
 */
class AxesPanel : public juce::Component
{
public:
    /** Display labels for the semantic-axis dropdown (SAO / default set). */
    static const juce::StringArray& getAxisLabels();

    /** Display label for a saved axis dropdown id across BOTH per-model tables
     *  (SAO ids 2-9, SA3 ids 10-12; id 1 or unknown → "---"). The preset
     *  browser needs this because a preset can carry ids from either model,
     *  and the ids are not a contiguous 1..N range shared by both tables. */
    static juce::String displayForAxisId(int id);

    AxesPanel(juce::AudioProcessorValueTreeState& apvts);
    ~AxesPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::map<juce::String, float> getAxisValues() const;

    /** Return axis values with per-slot additive offsets from drift LFO. */
    std::map<juce::String, float> getAxisValuesWithOffsets(float off1, float off2, float off3) const;

    /** Set per-slot ghost offsets for drift visualization. NaN = no ghost. */
    void setGhostOffsets(float o1, float o2, float o3);

    /** Switch the active axis table (SAO's 8 single-word axes vs SA3's 3
     *  bundled-pole axes) and repopulate all 3 slot dropdowns. Idempotent:
     *  a no-op when `sa3` already matches the current state. Ids are
     *  globally stable across both tables, so a slot's previously-selected
     *  id is re-selected if it still exists in the new table, otherwise the
     *  slot resets to "---". */
    void setModelIsSA3(bool sa3);

    /** Per-slot state for preset save/load. */
    struct SlotState { int dropdownId = 1; float value = 0.0f; };
    std::array<SlotState, 3> getSlotStates() const;
    void setSlotStates(const std::array<SlotState, 3>& states);

    /** Drop any pending-restore stash without disturbing the visible slots.
     *  Called at the start of every preset load (before the model is applied)
     *  so a not-yet-resolved id from a PRIOR state can't be promoted into the
     *  visible dropdown by the incoming model switch and leak into the loaded
     *  preset. The load then re-establishes this preset's own pending, if any. */
    void clearPendingRestore();

private:
    float fs() const;

    struct AxisSlot
    {
        std::unique_ptr<juce::ComboBox> dropdown;
        std::unique_ptr<MidiLearnSlider> slider;
        std::unique_ptr<juce::Label> valueLabel;
        std::unique_ptr<juce::Label> poleLabelA; // left pole
        std::unique_ptr<juce::Label> poleLabelB; // right pole
        int axisIndex = -1;
    };

    juce::Label header;
    std::vector<AxisSlot> slots;  // 3

    // Master attenuation that scales every axis delta before it is sent to
    // the backend. Non-trained vector directions can push the embedding off
    // the manifold and produce harsh output; this knob lets the user dial
    // the effect back without losing the per-axis balance.
    std::unique_ptr<SliderRow> amountRow;   // house-standard left-header band (single control)
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Attachment> amountAttachment;

    // Active per-model axis table — points at kAxesSAO or kAxesSA3 (defined
    // file-scope in AxesPanel.cpp). Defaults to kAxesSAO (set in the ctor).
    const AxisDef* activeAxes_ = nullptr;
    int activeAxesCount_ = 0;
    bool isSA3_ = false;

    // Per-slot stash for an axis id that could not be applied to the table that
    // was active at restore time — e.g. an SA3 axis (id 10-12) restored from a
    // preset during the standalone-startup window, before the backend resolves
    // the model, while the SAO table is still active. 0 = nothing pending.
    // setModelIsSA3 re-applies these once the matching table becomes active;
    // without it the SA3 selection would be silently dropped on relaunch.
    int pendingRestoreId_[3] = { 0, 0, 0 };

    void initSlot(AxisSlot& slot, int axisIndex);
    void layoutSlots(std::vector<AxisSlot>& slots, juce::Rectangle<int>& area, float f);

    static constexpr float NO_GHOST = std::numeric_limits<float>::quiet_NaN();
    float ghostOffsets_[3] = { NO_GHOST, NO_GHOST, NO_GHOST };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AxesPanel)
};
