#include "AxesPanel.h"
#include "GuiHelpers.h"

static const juce::Colour kAxisColors[] = { kAxis1, kAxis2, kAxis3 };

// Top 8 axes validated for 1-3s samples (Mel cosine distance > 0.70 at 1s).
// Ranked by effectiveness at short duration. PCA axes excluded (collapse at 1s).
static const juce::StringArray kEffectiveAxes {
    "---",
    "music / noise",
    "acoustic / electronic",
    "improvised / composed",
    "refined / raw",
    "solo / ensemble",
    "sacred / secular",
    "tonal / noisy",
    "rhythmic / sustained"
};

// Map display names → backend axis keys (cross_aesthetic_backend.py SEMANTIC_AXES)
static juce::String axisDisplayToKey(const juce::String& display)
{
    if (display.contains("music / noise"))          return "music_noise";
    if (display.contains("acoustic / electronic"))  return "acoustic_electronic";
    if (display.contains("improvised / composed"))  return "improvised_composed";
    if (display.contains("refined / raw"))           return "refined_raw";
    if (display.contains("solo / ensemble"))         return "solo_ensemble";
    if (display.contains("sacred / secular"))        return "sacred_secular";
    if (display.contains("tonal / noisy"))           return "tonal_noisy";
    if (display.contains("rhythmic / sustained"))    return "rhythmic_sustained";
    return {};
}

AxesPanel::AxesPanel()
{
    // Header is now provided by MainPanel — hide internal one
    header.setVisible(false);

    noteLabel.setText("Experimental -- labels approximate, more axes = weaker effect per axis",
                      juce::dontSendNotification);
    noteLabel.setColour(juce::Label::textColourId, kDimmer);
    noteLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(noteLabel);

    slots.resize(3);
    for (size_t i = 0; i < slots.size(); ++i)
        initSlot(slots[i], kEffectiveAxes, static_cast<int>(i));
}

void AxesPanel::initSlot(AxisSlot& slot, const juce::StringArray& options, int axisIndex)
{
    slot.axisIndex = axisIndex;

    slot.dropdown = std::make_unique<juce::ComboBox>();
    slot.dropdown->addItemList(options, 1);
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
    slot.slider->setColour(juce::Slider::backgroundColourId, kSurface);
    addAndMakeVisible(*slot.slider);

    slot.valueLabel = std::make_unique<juce::Label>("", "0.00");
    slot.valueLabel->setColour(juce::Label::textColourId, sliderColor);
    slot.valueLabel->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*slot.valueLabel);

    slot.poleLabelA = std::make_unique<juce::Label>();
    slot.poleLabelA->setColour(juce::Label::textColourId, kDimmer);
    slot.poleLabelA->setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(*slot.poleLabelA);

    slot.poleLabelB = std::make_unique<juce::Label>();
    slot.poleLabelB->setColour(juce::Label::textColourId, kDimmer);
    slot.poleLabelB->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*slot.poleLabelB);

    slot.slider->onValueChange = [&slot] {
        slot.valueLabel->setText(juce::String(slot.slider->getValue(), 2), juce::dontSendNotification);
    };

    slot.dropdown->onChange = [this, &slot] {
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

float AxesPanel::fs() const
{
    float topH = (getTopLevelComponent() != nullptr)
                     ? static_cast<float>(getTopLevelComponent()->getHeight()) : 800.0f;
    return juce::jlimit(12.0f, 22.0f, topH * 0.022f);
}

void AxesPanel::paint(juce::Graphics& g)
{
    // Background + card painted by MainPanel

    float f = fs();
    int dotSize = juce::roundToInt(f * 0.55f);

    for (size_t i = 0; i < slots.size(); ++i)
    {
        auto& slot = slots[i];
        auto dropBounds = slot.dropdown->getBounds();
        int dotX = dropBounds.getX() - dotSize - 4;
        int dotY = dropBounds.getCentreY() - dotSize / 2;
        g.setColour(kAxisColors[i]);
        g.fillEllipse(static_cast<float>(dotX), static_cast<float>(dotY),
                       static_cast<float>(dotSize), static_cast<float>(dotSize));
    }
}

void AxesPanel::layoutSlots(std::vector<AxisSlot>& slotsVec, juce::Rectangle<int>& area, float f, int dotOffset)
{
    int rowH = juce::roundToInt(f * 1.4f);
    int sliderH = juce::roundToInt(f * 1.2f);
    int gap = juce::roundToInt(f * 0.2f);
    int valW = juce::roundToInt(f * 3.0f);

    for (auto& slot : slotsVec)
    {
        bool active = slot.dropdown->getSelectedId() != 1; // 1 = "---"

        auto dropRow = area.removeFromTop(rowH);
        if (dotOffset > 0)
            dropRow.removeFromLeft(dotOffset);
        slot.dropdown->setBounds(dropRow);

        slot.slider->setVisible(active);
        slot.valueLabel->setVisible(active);
        slot.poleLabelA->setVisible(active);
        slot.poleLabelB->setVisible(active);

        if (active)
        {
            int poleH = juce::roundToInt(f * 0.9f);
            auto poleRow = area.removeFromTop(poleH);
            if (dotOffset > 0)
                poleRow.removeFromLeft(dotOffset);
            auto poleRight = poleRow.removeFromRight(valW);
            int poleLabelW = poleRow.getWidth() / 2;
            slot.poleLabelA->setFont(juce::FontOptions(f * 0.7f));
            slot.poleLabelB->setFont(juce::FontOptions(f * 0.7f));
            slot.poleLabelA->setBounds(poleRow.removeFromLeft(poleLabelW));
            slot.poleLabelB->setBounds(poleRow);

            auto sliderRow = area.removeFromTop(sliderH);
            if (dotOffset > 0)
                sliderRow.removeFromLeft(dotOffset);
            slot.valueLabel->setFont(juce::FontOptions(f * 0.8f));
            slot.valueLabel->setBounds(sliderRow.removeFromRight(valW));
            slot.slider->setBounds(sliderRow);
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
    int headerH = juce::roundToInt(f * 1.3f);
    int dotOffset = juce::roundToInt(f * 0.8f);

    // Header provided by MainPanel — skip internal header allocation

    // Reserve space for note at bottom
    int noteH = juce::roundToInt(f * 0.85f);
    noteLabel.setFont(juce::FontOptions(f * 0.6f));
    noteLabel.setBounds(area.removeFromBottom(noteH));

    layoutSlots(slots, area, f * 0.75f, dotOffset);
}

std::map<juce::String, float> AxesPanel::getAxisValues() const
{
    std::map<juce::String, float> vals;
    for (auto& slot : slots)
    {
        if (slot.dropdown->getSelectedId() > 1)
        {
            auto key = axisDisplayToKey(slot.dropdown->getText());
            if (key.isNotEmpty())
                vals[key] = static_cast<float>(slot.slider->getValue());
        }
    }
    return vals;
}
