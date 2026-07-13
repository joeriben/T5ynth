#include "AxesPanel.h"
#include "GuiHelpers.h"
#include "../dsp/BlockParams.h"

static const juce::Colour kAxisColors[] = { kAxis1, kAxis2, kAxis3 };

// Full definition of the per-model axis table entry (forward-declared in
// AxesPanel.h — the header only ever holds a pointer into one of the two
// tables below).
struct AxisDef { int id; const char* display; const char* key; };

// RESERVED abstract-gesture label set (trialled 2026-07-13, then reverted —
// tests on real presets showed the concrete pole names usually DO make sense).
// Kept for possible reuse: SAO ids 2..9 = tilt/slide/nudge/lean/sway/veer/tug/
// coax; SA3 ids 10..12 = push/shift/move. Full mapping + rationale in memory
// project_axis_abstract_labels_reserve. Only display strings change (keys/ids
// stay), so a future swap back is preset-safe.
//
// SAO / AudioLDM2 / default: ids 1..9 (id 1 = "---" placeholder, no key).
// Top 8 axes validated for 1-3s samples (Mel cosine distance > 0.70 at 1s),
// ranked by effectiveness at short duration. PCA axes excluded (collapse at
// 1s). These ids are persisted in presets (AxesPanel::SlotState::dropdownId)
// — never renumber or reassign them.
static const AxisDef kAxesSAO[] = {
    {1,"---",""},
    {2,"noise / music","music_noise"},
    {3,"electronic / acoustic","acoustic_electronic"},
    {4,"composed / improvised","improvised_composed"},
    {5,"raw / refined","refined_raw"},
    {6,"ensemble / solo","solo_ensemble"},
    {7,"secular / sacred","sacred_secular"},
    {8,"noisy / tonal","tonal_noisy"},
    {9,"sustained / rhythmic","rhythmic_sustained"},
};

// SA3: 3 bundled-pole axes (synonym-stacked pole prompts on the backend —
// see SEMANTIC_AXIS_POLES in pipe_inference.py) instead of the 8 SAO
// single-word axes. id 1 = "---" (shared meaning with kAxesSAO), then ids
// 10,11,12 — distinct from the SAO ids 2..9 so a saved dropdownId can never
// collide across model families. (An abstract-gesture alternative label set is
// reserved — see the RESERVED note above kAxesSAO.)
static const AxisDef kAxesSA3[] = {
    {1,"---",""},
    {10,"sustained / rhythmic","rhythmic_sustained_sa3"},
    {11,"smooth / grainy","grainy_smooth_sa3"},
    {12,"sparse / dense","dense_sparse_sa3"},
};

// Repopulate a dropdown's items from an axis table using explicit, stable
// ids (never addItemList's implicit 1..N) so ids stay meaningful across a
// table switch (setModelIsSA3) and across preset save/load.
static void populateAxisDropdown(juce::ComboBox& box, const AxisDef* axes, int count)
{
    box.clear(juce::dontSendNotification);
    for (int i = 0; i < count; ++i)
        box.addItem(axes[i].display, axes[i].id);
}

// Find the backend axis key for a selected dropdown id within a given axis
// table. Returns empty for id 1 ("---") or an id absent from the table
// (e.g. a stale preset id from the other model's table).
static juce::String keyForAxisId(const AxisDef* axes, int count, int id)
{
    for (int i = 0; i < count; ++i)
        if (axes[i].id == id)
            return juce::String(axes[i].key);
    return {};
}

// Whether a dropdown id is present in the given axis table.
static bool idInTable(const AxisDef* axes, int count, int id)
{
    for (int i = 0; i < count; ++i)
        if (axes[i].id == id)
            return true;
    return false;
}

const juce::StringArray& AxesPanel::getAxisLabels()
{
    static const juce::StringArray labels = [] {
        juce::StringArray arr;
        for (auto& ax : kAxesSAO)
            arr.add(ax.display);
        return arr;
    }();
    return labels;
}

juce::String AxesPanel::displayForAxisId(int id)
{
    if (id <= 1)
        return "---"; // id 1 = "---" placeholder (shared by both tables)
    for (auto& ax : kAxesSAO)
        if (ax.id == id) return ax.display;
    for (auto& ax : kAxesSA3)
        if (ax.id == id) return ax.display;
    return "---"; // unknown / stale id
}

AxesPanel::AxesPanel(juce::AudioProcessorValueTreeState& apvts)
{
    // Header is now provided by MainPanel — hide internal one
    header.setVisible(false);

    // Default / initial table is SAO's. MainPanel wires an initial
    // setModelIsSA3() call (via promptPanel.onModelChanged) once the real
    // model is known, which switches this if the model turns out to be SA3.
    activeAxes_ = kAxesSAO;
    activeAxesCount_ = static_cast<int>(sizeof(kAxesSAO) / sizeof(kAxesSAO[0]));

    slots.resize(3);
    for (size_t i = 0; i < slots.size(); ++i)
        initSlot(slots[i], static_cast<int>(i));

    // Master amount: scales all axis deltas before they reach the backend.
    // House-standard SliderRow with a left-header band — single control → left-
    // header, matching RESYNTH / SNAP / CACHE in the same column.
    amountRow = std::make_unique<SliderRow>(
        "Amount", [](double v) { return juce::String(v, 2); }, kOscCol);
    amountRow->setLabelAsBand(true);
    addAndMakeVisible(*amountRow);

    amountAttachment = std::make_unique<Attachment>(apvts, PID::genAxesAmount, amountRow->getSlider());
}

void AxesPanel::initSlot(AxisSlot& slot, int axisIndex)
{
    slot.axisIndex = axisIndex;

    slot.dropdown = std::make_unique<juce::ComboBox>();
    populateAxisDropdown(*slot.dropdown, activeAxes_, activeAxesCount_);
    slot.dropdown->setSelectedId(1, juce::dontSendNotification); // "---"
    addAndMakeVisible(*slot.dropdown);

    juce::Colour sliderColor = (axisIndex >= 0 && axisIndex < 3)
        ? kAxisColors[axisIndex] : kAccent;

    slot.slider = std::make_unique<juce::Slider>();
    slot.slider->setSliderStyle(juce::Slider::LinearHorizontal);
    slot.slider->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slot.slider->setRange(-1.0, 1.0, 0.002);
    slot.slider->setValue(0.0, juce::dontSendNotification);
    slot.slider->setColour(juce::Slider::trackColourId, sliderColor);
    slot.slider->setColour(juce::Slider::backgroundColourId, kBorder);   // visible rail (matches the other sliders)
    addAndMakeVisible(*slot.slider);

    slot.valueLabel = std::make_unique<juce::Label>("", "0.00");
    labelAsCaption(*slot.valueLabel, sliderColor);
    slot.valueLabel->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*slot.valueLabel);

    slot.poleLabelA = std::make_unique<juce::Label>();
    labelAsCaption(*slot.poleLabelA, kTextMuted);
    slot.poleLabelA->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*slot.poleLabelA);

    slot.poleLabelB = std::make_unique<juce::Label>();
    labelAsCaption(*slot.poleLabelB, kTextMuted);
    slot.poleLabelB->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*slot.poleLabelB);

    slot.slider->onValueChange = [this, &slot] {
        slot.valueLabel->setText(juce::String(slot.slider->getValue(), 2), juce::dontSendNotification);
        repaint();
    };

    slot.dropdown->onChange = [this, &slot] {
        // An explicit selection supersedes any not-yet-resolved pending restore
        // for this slot, so getSlotStates() reports the live choice rather than
        // a stale stashed id. (setSlotStates re-stashes AFTER its notifying
        // setSelectedId, so its stash survives this clear; setModelIsSA3 uses
        // dontSendNotification, so it never reaches here.)
        if (slot.axisIndex >= 0 && slot.axisIndex < 3)
            pendingRestoreId_[slot.axisIndex] = 0;

        auto text = slot.dropdown->getText();
        auto slashIdx = text.indexOf(" / ");
        if (slashIdx >= 0)
        {
            slot.poleLabelA->setText(text.substring(0, slashIdx).trim(), juce::dontSendNotification);
            slot.poleLabelB->setText(text.substring(slashIdx + 3).trim(), juce::dontSendNotification);
        }
        else
        {
            slot.poleLabelA->setText("", juce::dontSendNotification);
            slot.poleLabelB->setText("", juce::dontSendNotification);
        }
        resized();
    };
}

void AxesPanel::setModelIsSA3(bool sa3)
{
    if (sa3 == isSA3_)
        return;
    isSA3_ = sa3;
    activeAxes_ = sa3 ? kAxesSA3 : kAxesSAO;
    activeAxesCount_ = sa3 ? static_cast<int>(sizeof(kAxesSA3) / sizeof(kAxesSA3[0]))
                           : static_cast<int>(sizeof(kAxesSAO) / sizeof(kAxesSAO[0]));

    // Mirrors the dropdown onChange handler's " / " pole-label split above,
    // run manually here since the repopulate/reselect below uses
    // dontSendNotification (no onChange firing on a programmatic change).
    auto refreshPoleLabels = [](AxisSlot& s) {
        auto text = s.dropdown->getText();
        auto slashIdx = text.indexOf(" / ");
        if (slashIdx >= 0)
        {
            s.poleLabelA->setText(text.substring(0, slashIdx).trim(), juce::dontSendNotification);
            s.poleLabelB->setText(text.substring(slashIdx + 3).trim(), juce::dontSendNotification);
        }
        else
        {
            s.poleLabelA->setText("", juce::dontSendNotification);
            s.poleLabelB->setText("", juce::dontSendNotification);
        }
    };

    for (auto& slot : slots)
    {
        const int idx = slot.axisIndex;
        const int prevId = slot.dropdown->getSelectedId();
        populateAxisDropdown(*slot.dropdown, activeAxes_, activeAxesCount_);

        // A pending-restore id (an axis restored before its model was resolved)
        // takes precedence over the current selection. For a normal user model
        // switch there is no pending id, so this preserves the prior behaviour:
        // keep the selection if it exists in the new table, else reset to "---".
        const bool havePending = (idx >= 0 && idx < 3 && pendingRestoreId_[idx] != 0);
        const int want = havePending ? pendingRestoreId_[idx] : prevId;
        const bool exists = idInTable(activeAxes_, activeAxesCount_, want);

        slot.dropdown->setSelectedId(exists ? want : 1, juce::dontSendNotification); // 1 = "---"
        if (havePending && exists)
            pendingRestoreId_[idx] = 0; // pending restore satisfied
        refreshPoleLabels(slot);
    }

    resized();
}

float AxesPanel::fs() const
{
    float topH = (getTopLevelComponent() != nullptr)
                     ? static_cast<float>(getTopLevelComponent()->getHeight()) : 800.0f;
    return juce::jlimit(12.0f, 22.0f, topH * 0.022f);
}

void AxesPanel::setGhostOffsets(float o1, float o2, float o3)
{
    auto same = [](float a, float b) {
        return (std::isnan(a) && std::isnan(b)) || a == b;
    };
    if (same(ghostOffsets_[0], o1) && same(ghostOffsets_[1], o2) && same(ghostOffsets_[2], o3))
        return;
    ghostOffsets_[0] = o1;
    ghostOffsets_[1] = o2;
    ghostOffsets_[2] = o3;
    repaint();
}

void AxesPanel::paint(juce::Graphics& g)
{
    // Background + card painted by MainPanel. Axis dots removed: the slot colour
    // is carried by the slider track + value, and the dropdown names the axis.

    for (size_t i = 0; i < slots.size(); ++i)
    {
        auto& slot = slots[i];

        // Ghost circle for drift-modulated axis value
        if (i < 3 && !std::isnan(ghostOffsets_[i]) && slot.slider && slot.slider->isVisible())
        {
            auto sb = slot.slider->getBounds();
            float ghostVal = static_cast<float>(slot.slider->getValue()) + ghostOffsets_[i];
            double norm = slot.slider->valueToProportionOfLength(static_cast<double>(ghostVal));
            norm = juce::jlimit(0.0, 1.0, norm);

            int thumbW = slot.slider->getLookAndFeel().getSliderThumbRadius(*slot.slider) * 2;
            int trackX = sb.getX() + thumbW / 2;
            int trackW = sb.getWidth() - thumbW;
            float gx = static_cast<float>(trackX) + static_cast<float>(trackW) * static_cast<float>(norm);
            float gy = static_cast<float>(sb.getCentreY());
            float gr = static_cast<float>(sb.getHeight()) * 0.28f;

            g.setColour(juce::Colour(0xccff9800)); // orange ghost
            g.fillEllipse(gx - gr, gy - gr, gr * 2.0f, gr * 2.0f);
        }
    }
}

void AxesPanel::layoutSlots(std::vector<AxisSlot>& slotsVec, juce::Rectangle<int>& area, float f)
{
    int rowH = juce::roundToInt(f * 1.4f);
    int gap = juce::roundToInt(f * 0.2f);
    int valW = juce::roundToInt(f * 3.0f);

    for (auto& slot : slotsVec)
    {
        bool active = slot.dropdown->getSelectedId() != 1; // 1 = "---"

        auto row = area.removeFromTop(rowH);

        // Pole labels no longer shown (axis name visible in dropdown text)
        slot.poleLabelA->setVisible(false);
        slot.poleLabelB->setVisible(false);
        slot.slider->setVisible(active);
        slot.valueLabel->setVisible(active);

        // Uniform dropdown width whether or not the slot is active (a consistent
        // grid — no width jump on select). When active the slider + value fill
        // the space to its right; when empty that space stays clear (exactly
        // where those controls will appear once an axis is chosen).
        int dropW = juce::roundToInt(row.getWidth() * 0.45f);
        slot.dropdown->setBounds(row.removeFromLeft(dropW));
        if (active)
        {
            setUiFont(*slot.valueLabel, TextRole::Value, f * 0.8f);
            slot.valueLabel->setBounds(row.removeFromRight(valW));
            slot.slider->setBounds(row);
        }

        area.removeFromTop(gap);
    }
}

void AxesPanel::resized()
{
    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());
    int pad = juce::roundToInt(w * 0.04f);
    auto area = getLocalBounds().reduced(pad, juce::roundToInt(h * 0.01f));
    float f = fs();

    // Header provided by MainPanel — skip internal header allocation

    layoutSlots(slots, area, f * 0.75f);

    // Amount row: the master attenuator as a single left-header control. SliderRow
    // owns its label-band / slider / value layout — the house standard.
    float fa = f * 0.75f;
    int rowH = juce::roundToInt(fa * 1.4f);
    amountRow->setBounds(area.removeFromTop(rowH));
}

std::map<juce::String, float> AxesPanel::getAxisValues() const
{
    std::map<juce::String, float> vals;
    for (auto& slot : slots)
    {
        const int selId = slot.dropdown->getSelectedId();
        if (selId > 1)
        {
            auto key = keyForAxisId(activeAxes_, activeAxesCount_, selId);
            if (key.isNotEmpty())
                vals[key] = static_cast<float>(slot.slider->getValue());
        }
    }
    return vals;
}

std::map<juce::String, float> AxesPanel::getAxisValuesWithOffsets(float off1, float off2, float off3) const
{
    const float offsets[] = { off1, off2, off3 };
    std::map<juce::String, float> vals;
    for (size_t i = 0; i < slots.size(); ++i)
    {
        auto& slot = slots[i];
        const int selId = slot.dropdown->getSelectedId();
        if (selId > 1)
        {
            auto key = keyForAxisId(activeAxes_, activeAxesCount_, selId);
            if (key.isNotEmpty())
                vals[key] = static_cast<float>(slot.slider->getValue()) + offsets[i];
        }
    }
    return vals;
}

std::array<AxesPanel::SlotState, 3> AxesPanel::getSlotStates() const
{
    std::array<SlotState, 3> states;
    for (size_t i = 0; i < slots.size() && i < 3; ++i)
    {
        // Surface a stashed pending-restore id (an SA3 axis restored before its
        // model resolved, currently shown as "---") so a save/quit during that
        // window persists the real id, not the degraded "---". Without this,
        // quitting before the backend resolves the model — or with SA3 not
        // installed this session — would drop the saved axis permanently.
        const int pending = pendingRestoreId_[i];
        states[i].dropdownId = (pending != 0) ? pending : slots[i].dropdown->getSelectedId();
        states[i].value = static_cast<float>(slots[i].slider->getValue());
    }
    return states;
}

void AxesPanel::clearPendingRestore()
{
    for (auto& id : pendingRestoreId_)
        id = 0;
}

void AxesPanel::setSlotStates(const std::array<SlotState, 3>& states)
{
    for (size_t i = 0; i < slots.size() && i < 3; ++i)
    {
        // If the restored id isn't in the currently-active table (e.g. an SA3
        // axis restored during the standalone-startup window while the SAO
        // table is still active, because the backend hasn't resolved the
        // model yet), JUCE would drop it — getSelectedId() returns 0 for an
        // absent id. Show "---" for now and stash the id so setModelIsSA3
        // re-applies it once the matching table becomes active. The slider
        // value is staged regardless, so it's already correct when the slot
        // later becomes visible.
        const int id = states[i].dropdownId;
        const bool present = idInTable(activeAxes_, activeAxesCount_, id);
        slots[i].dropdown->setSelectedId(present ? id : 1, juce::sendNotificationSync);
        pendingRestoreId_[i] = present ? 0 : id;
        slots[i].slider->setValue(static_cast<double>(states[i].value), juce::dontSendNotification);
        slots[i].valueLabel->setText(juce::String(states[i].value, 2), juce::dontSendNotification);
    }
    resized();
}
