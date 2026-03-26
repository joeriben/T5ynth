#include "AxesPanel.h"

static const auto kGreen  = juce::Colour(0xff4a9eff);
static const auto kDim    = juce::Colour(0xff888888);
static const auto kDimmer = juce::Colour(0xff606060);

static const juce::StringArray kSemanticAxes {
    "—",
    "tonal / noisy (d=4.81)",
    "rhythmic / sustained (d=2.60)",
    "bright / dark (d=1.28)",
    "loud / quiet (d=1.02)",
    "smooth / harsh (d=0.81)",
    "fast / slow (d=0.80)",
    "close / distant (d=0.76)",
    "dense / sparse (d=0.74)"
};

static const juce::StringArray kPcaAxes {
    "—",
    "PC1: natural / synthetic",
    "PC2: sonic / physical",
    "PC3: tonal / atonal",
    "PC4: continuous / impulsive",
    "PC5: harmonic / inharmonic",
    "PC6: wet / dry",
    "PC7: melodic / percussive",
    "PC8: soft / aggressive",
    "PC9: clean / distorted",
    "PC10: ambient / direct"
};

AxesPanel::AxesPanel()
{
    semHeader.setText("SEMANTIC AXES", juce::dontSendNotification);
    semHeader.setColour(juce::Label::textColourId, kDim);
    addAndMakeVisible(semHeader);

    pcaHeader.setText("PCA AXES", juce::dontSendNotification);
    pcaHeader.setColour(juce::Label::textColourId, kDim);
    addAndMakeVisible(pcaHeader);

    semSlots.resize(3);
    for (auto& slot : semSlots)
        initSlot(slot, kSemanticAxes);

    pcaSlots.resize(6);
    for (auto& slot : pcaSlots)
        initSlot(slot, kPcaAxes);
}

void AxesPanel::initSlot(AxisSlot& slot, const juce::StringArray& options)
{
    slot.dropdown = std::make_unique<juce::ComboBox>();
    slot.dropdown->addItemList(options, 1);
    slot.dropdown->setSelectedId(1, juce::dontSendNotification); // "—"
    slot.dropdown->onChange = [this] { resized(); };
    addAndMakeVisible(*slot.dropdown);

    slot.slider = std::make_unique<juce::Slider>();
    slot.slider->setSliderStyle(juce::Slider::LinearHorizontal);
    slot.slider->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slot.slider->setRange(-1.0, 1.0, 0.01);
    slot.slider->setValue(0.0, juce::dontSendNotification);
    slot.slider->setColour(juce::Slider::trackColourId, kGreen);
    slot.slider->setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1a1a1a));
    addAndMakeVisible(*slot.slider);

    slot.valueLabel = std::make_unique<juce::Label>("", "0.00");
    slot.valueLabel->setColour(juce::Label::textColourId, kGreen);
    slot.valueLabel->setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(*slot.valueLabel);

    slot.slider->onValueChange = [&slot] {
        slot.valueLabel->setText(juce::String(slot.slider->getValue(), 2), juce::dontSendNotification);
    };
}

float AxesPanel::fs() const
{
    float topH = (getTopLevelComponent() != nullptr)
                     ? static_cast<float>(getTopLevelComponent()->getHeight()) : 800.0f;
    return juce::jlimit(14.0f, 26.0f, topH * 0.030f);
}

void AxesPanel::paint(juce::Graphics&) {}

void AxesPanel::layoutSlots(std::vector<AxisSlot>& slots, juce::Rectangle<int>& area, float f)
{
    int rowH = juce::roundToInt(f * 1.4f);
    int sliderH = juce::roundToInt(f * 1.2f);
    int gap = juce::roundToInt(f * 0.2f);
    int valW = juce::roundToInt(f * 3.0f);

    for (auto& slot : slots)
    {
        bool active = slot.dropdown->getSelectedId() != 1; // 1 = "—"

        slot.dropdown->setBounds(area.removeFromTop(rowH));
        slot.slider->setVisible(active);
        slot.valueLabel->setVisible(active);

        if (active)
        {
            auto sliderRow = area.removeFromTop(sliderH);
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

    semHeader.setFont(juce::FontOptions(f * 0.85f));
    semHeader.setBounds(area.removeFromTop(headerH));
    layoutSlots(semSlots, area, f * 0.75f);

    area.removeFromTop(juce::roundToInt(f * 0.5f));

    pcaHeader.setFont(juce::FontOptions(f * 0.85f));
    pcaHeader.setBounds(area.removeFromTop(headerH));
    layoutSlots(pcaSlots, area, f * 0.65f);
}

std::map<juce::String, float> AxesPanel::getAxisValues() const
{
    std::map<juce::String, float> vals;
    // TODO: map dropdown selection back to axis names
    return vals;
}
