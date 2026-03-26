#include "SynthPanel.h"
#include "../PluginProcessor.h"

static const auto kGreen   = juce::Colour(0xff4a9eff);
static const auto kDim     = juce::Colour(0xff888888);
static const auto kDimmer  = juce::Colour(0xff606060);
static const auto kSurface = juce::Colour(0xff1a1a1a);
static const auto kGreenBg = juce::Colour(0xff1a2a3a);

static void makeLinearSlider(juce::Slider& s, juce::Label& label, juce::Label& value,
                             const juce::String& name, juce::Component* p)
{
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    s.setColour(juce::Slider::trackColourId, kGreen);
    s.setColour(juce::Slider::backgroundColourId, kSurface);
    p->addAndMakeVisible(s);
    label.setText(name, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, kDim);
    label.setJustificationType(juce::Justification::centredLeft);
    p->addAndMakeVisible(label);
    value.setColour(juce::Label::textColourId, kGreen);
    value.setJustificationType(juce::Justification::centredRight);
    p->addAndMakeVisible(value);
}

SynthPanel::SynthPanel(T5ynthProcessor& processor)
    : processorRef(processor)
{
    // Horizontal engine switch
    looperBtn.setColour(juce::TextButton::buttonColourId, kGreenBg);
    looperBtn.setColour(juce::TextButton::buttonOnColourId, kGreen);
    looperBtn.setColour(juce::TextButton::textColourOffId, kDim);
    looperBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    looperBtn.setClickingTogglesState(true);
    looperBtn.setRadioGroupId(1001);
    looperBtn.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(looperBtn);

    wavetableBtn.setColour(juce::TextButton::buttonColourId, kSurface);
    wavetableBtn.setColour(juce::TextButton::buttonOnColourId, kGreen);
    wavetableBtn.setColour(juce::TextButton::textColourOffId, kDim);
    wavetableBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    wavetableBtn.setClickingTogglesState(true);
    wavetableBtn.setRadioGroupId(1001);
    addAndMakeVisible(wavetableBtn);

    // Hidden combo for APVTS attachment
    engineModeHidden.addItemList({"Looper", "Wavetable"}, 1);
    engineModeHidden.onChange = [this] {
        bool isLooper = engineModeHidden.getSelectedId() == 1;
        looperBtn.setToggleState(isLooper, juce::dontSendNotification);
        wavetableBtn.setToggleState(!isLooper, juce::dontSendNotification);
    };
    looperBtn.onClick = [this] { engineModeHidden.setSelectedId(1); };
    wavetableBtn.onClick = [this] { engineModeHidden.setSelectedId(2); };

    addAndMakeVisible(waveformDisplay);

    // Scan
    makeLinearSlider(scanSlider, scanLabel, scanValue, "Scan Position", this);
    scanHint.setText("Morph between frames (0 = start, 1 = end)", juce::dontSendNotification);
    scanHint.setColour(juce::Label::textColourId, kDimmer);
    addAndMakeVisible(scanHint);
    scanSlider.onValueChange = [this] {
        scanValue.setText(juce::String(scanSlider.getValue(), 2), juce::dontSendNotification);
    };

    // Filter
    filterToggle.setColour(juce::ToggleButton::textColourId, kDim);
    filterToggle.setColour(juce::ToggleButton::tickColourId, kGreen);
    filterToggle.setToggleState(true, juce::dontSendNotification);
    filterToggle.onClick = [this] { resized(); repaint(); };
    addAndMakeVisible(filterToggle);

    filterTypeBox.addItemList({"LP", "HP", "BP"}, 1);
    addAndMakeVisible(filterTypeBox);
    filterSlopeBox.addItemList({"12dB", "24dB"}, 1);
    addAndMakeVisible(filterSlopeBox);

    auto makeFilterKnob = [this](juce::Slider& s, juce::Label& label, juce::Label& value, const juce::String& name)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 1, 1);
        s.setColour(juce::Slider::rotarySliderFillColourId, kGreen);
        s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2a2a2a));
        addAndMakeVisible(s);
        label.setText(name, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, kDim);
        label.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(label);
        value.setColour(juce::Label::textColourId, kGreen);
        value.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(value);
    };

    makeFilterKnob(cutoffSlider, cutoffLabel, cutoffValue, "Cutoff");
    cutoffSlider.onValueChange = [this] {
        cutoffValue.setText(juce::String(juce::roundToInt(cutoffSlider.getValue())) + " Hz", juce::dontSendNotification);
    };

    makeFilterKnob(resoSlider, resoLabel, resoValue, "Reso");
    resoSlider.onValueChange = [this] {
        resoValue.setText(juce::String(resoSlider.getValue(), 2), juce::dontSendNotification);
    };

    makeFilterKnob(filterMixSlider, filterMixLabel, filterMixValue, "Mix");
    filterMixSlider.onValueChange = [this] {
        filterMixValue.setText(juce::String(juce::roundToInt(filterMixSlider.getValue() * 100.0)) + "%", juce::dontSendNotification);
    };

    makeFilterKnob(kbdTrackSlider, kbdTrackLabel, kbdTrackValue, "Kbd");
    kbdTrackSlider.onValueChange = [this] {
        kbdTrackValue.setText(juce::String(juce::roundToInt(kbdTrackSlider.getValue() * 100.0)) + "%", juce::dontSendNotification);
    };

    // APVTS
    auto& apvts = processor.getValueTreeState();
    engineModeA = std::make_unique<CA>(apvts, "engine_mode", engineModeHidden);
    scanA       = std::make_unique<SA>(apvts, "osc_scan", scanSlider);
    cutoffA     = std::make_unique<SA>(apvts, "filter_cutoff", cutoffSlider);
    resoA       = std::make_unique<SA>(apvts, "filter_resonance", resoSlider);
    filterTypeA = std::make_unique<CA>(apvts, "filter_type", filterTypeBox);
    filterSlopeA= std::make_unique<CA>(apvts, "filter_slope", filterSlopeBox);
    filterMixA  = std::make_unique<SA>(apvts, "filter_mix", filterMixSlider);
    kbdTrackA   = std::make_unique<SA>(apvts, "filter_kbd_track", kbdTrackSlider);
}

float SynthPanel::fs() const
{
    float topH = (getTopLevelComponent() != nullptr)
                     ? static_cast<float>(getTopLevelComponent()->getHeight()) : 800.0f;
    return juce::jlimit(14.0f, 26.0f, topH * 0.030f);
}

void SynthPanel::paint(juce::Graphics& g)
{
    float f = fs();
    float h = static_cast<float>(getHeight());
    float w = static_cast<float>(getWidth());
    float pad = w * 0.03f;

    // Filter section separator
    g.setColour(juce::Colour(0xff1a1a1a));
    int filterY = juce::roundToInt(h * 0.48f);
    g.drawHorizontalLine(filterY, pad, w - pad);

    g.setFont(juce::FontOptions(f * 0.85f));
    g.setColour(kDim);
    g.drawText("FILTER", juce::roundToInt(pad), filterY + 2,
               juce::roundToInt(w * 0.3f), juce::roundToInt(f * 1.3f),
               juce::Justification::centredLeft);
}

void SynthPanel::resized()
{
    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());
    int pad = juce::roundToInt(w * 0.03f);
    auto area = getLocalBounds().reduced(pad);
    float f = fs();
    float fSmall = f * 0.8f;
    int rowH = juce::roundToInt(f * 1.5f);
    int sliderH = juce::roundToInt(f * 1.3f);
    int hintH = juce::roundToInt(fSmall * 1.2f);
    int gap = juce::roundToInt(h * 0.01f);

    auto setFs = [](juce::Label& l, float size) { l.setFont(juce::FontOptions(size)); };

    // --- MODE: horizontal switch ---
    auto modeRow = area.removeFromTop(juce::roundToInt(f * 2.0f));
    int halfW = modeRow.getWidth() / 2;
    looperBtn.setBounds(modeRow.removeFromLeft(halfW));
    wavetableBtn.setBounds(modeRow);
    area.removeFromTop(gap);

    // Waveform display
    int waveH = juce::roundToInt(h * 0.18f);
    waveformDisplay.setBounds(area.removeFromTop(waveH));
    area.removeFromTop(gap);

    // Scan
    setFs(scanLabel, f);
    setFs(scanValue, f);
    auto scanRow = area.removeFromTop(rowH);
    scanLabel.setBounds(scanRow.removeFromLeft(scanRow.getWidth() / 2));
    scanValue.setBounds(scanRow);
    scanSlider.setBounds(area.removeFromTop(sliderH));
    setFs(scanHint, fSmall);
    scanHint.setBounds(area.removeFromTop(hintH));

    // --- FILTER section ---
    area.removeFromTop(juce::roundToInt(h * 0.05f)); // space for header in paint()

    bool filterOn = filterToggle.getToggleState();
    auto filterHeaderRow = area.removeFromTop(rowH);
    filterToggle.setBounds(filterHeaderRow.removeFromLeft(juce::roundToInt(w * 0.22f)));
    if (filterOn)
    {
        filterTypeBox.setBounds(filterHeaderRow.removeFromLeft(juce::roundToInt(w * 0.14f)));
        filterHeaderRow.removeFromLeft(4);
        filterSlopeBox.setBounds(filterHeaderRow.removeFromLeft(juce::roundToInt(w * 0.14f)));
    }
    filterTypeBox.setVisible(filterOn);
    filterSlopeBox.setVisible(filterOn);
    area.removeFromTop(gap);

    // Conditional visibility for all filter controls
    for (auto* c : std::initializer_list<juce::Component*>{
            &cutoffSlider, &cutoffLabel, &cutoffValue,
            &resoSlider, &resoLabel, &resoValue,
            &filterMixSlider, &filterMixLabel, &filterMixValue,
            &kbdTrackSlider, &kbdTrackLabel, &kbdTrackValue })
        c->setVisible(filterOn);

    if (filterOn)
    {
        int cols = 4;
        int colW = area.getWidth() / cols;
        int knobDia = juce::jmin(juce::roundToInt(h * 0.12f), colW - 8);
        int labelH = juce::roundToInt(f * 1.0f);
        int tbW = juce::roundToInt(knobDia * 0.9f);
        int tbH = juce::roundToInt(fSmall);

        auto knobRow = area.removeFromTop(knobDia + labelH * 2);

        auto placeKnob = [&](juce::Slider& knob, juce::Label& label, juce::Label& value, int col)
        {
            int x = knobRow.getX() + col * colW + (colW - knobDia) / 2;
            int y = knobRow.getY();
            setFs(label, fSmall);
            label.setBounds(x, y, knobDia, labelH);
            knob.setBounds(x, y + labelH, knobDia, knobDia);
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, tbW, tbH);
        };

        placeKnob(cutoffSlider, cutoffLabel, cutoffValue, 0);
        placeKnob(resoSlider, resoLabel, resoValue, 1);
        placeKnob(filterMixSlider, filterMixLabel, filterMixValue, 2);
        placeKnob(kbdTrackSlider, kbdTrackLabel, kbdTrackValue, 3);
    }
}
