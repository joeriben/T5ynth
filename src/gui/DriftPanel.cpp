#include "DriftPanel.h"

static const auto kGreen = juce::Colour(0xff4a9eff);
static const auto kDim   = juce::Colour(0xff888888);

static void makeVSlider(juce::Slider& s, juce::Label& l, const juce::String& text, juce::Component* p)
{
    s.setSliderStyle(juce::Slider::LinearVertical);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 1, 1);
    s.setColour(juce::Slider::trackColourId, kGreen);
    s.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1a1a1a));
    p->addAndMakeVisible(s);
    l.setText(text, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setColour(juce::Label::textColourId, kDim);
    p->addAndMakeVisible(l);
}

DriftPanel::DriftPanel(juce::AudioProcessorValueTreeState& apvts)
{
    // Delay
    delayToggle.setColour(juce::ToggleButton::textColourId, kDim);
    delayToggle.setColour(juce::ToggleButton::tickColourId, kGreen);
    delayToggle.setToggleState(false, juce::dontSendNotification);
    delayToggle.onClick = [this] { resized(); };
    addAndMakeVisible(delayToggle);

    makeVSlider(delayTime, delayTimeL, "Time", this);
    makeVSlider(delayFb, delayFbL, "FB", this);
    makeVSlider(delayMix, delayMixL, "Mix", this);

    // Reverb
    reverbToggle.setColour(juce::ToggleButton::textColourId, kDim);
    reverbToggle.setColour(juce::ToggleButton::tickColourId, kGreen);
    reverbToggle.setToggleState(false, juce::dontSendNotification);
    reverbToggle.onClick = [this] { resized(); };
    addAndMakeVisible(reverbToggle);

    reverbIrBox.addItemList({"Bright", "Medium", "Dark"}, 1);
    addAndMakeVisible(reverbIrBox);

    makeVSlider(reverbMix, reverbMixL, "Mix", this);

    // Limiter
    limHeader.setText("LIM", juce::dontSendNotification);
    limHeader.setColour(juce::Label::textColourId, kDim);
    addAndMakeVisible(limHeader);
    makeVSlider(limThresh, limThreshL, "Thr", this);
    makeVSlider(limRelease, limReleaseL, "Rel", this);

    // APVTS
    delayTimeA = std::make_unique<SA>(apvts, "delay_time", delayTime);
    delayFbA   = std::make_unique<SA>(apvts, "delay_feedback", delayFb);
    delayMixA  = std::make_unique<SA>(apvts, "delay_mix", delayMix);
    reverbMixA = std::make_unique<SA>(apvts, "reverb_mix", reverbMix);
    reverbIrA  = std::make_unique<CA>(apvts, "reverb_ir", reverbIrBox);
    limThreshA = std::make_unique<SA>(apvts, "limiter_thresh", limThresh);
    limReleaseA= std::make_unique<SA>(apvts, "limiter_release", limRelease);
}

float DriftPanel::fs() const
{
    float topH = (getTopLevelComponent() != nullptr)
                     ? static_cast<float>(getTopLevelComponent()->getHeight()) : 800.0f;
    return juce::jlimit(14.0f, 24.0f, topH * 0.026f);
}

void DriftPanel::paint(juce::Graphics&) {}

void DriftPanel::resized()
{
    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());
    int pad = juce::roundToInt(w * 0.08f);
    auto area = getLocalBounds().reduced(pad, juce::roundToInt(h * 0.01f));
    float f = fs();
    int rowH = juce::roundToInt(f * 1.6f);
    int gap = juce::roundToInt(h * 0.01f);
    int labelH = juce::roundToInt(f * 1.2f);
    int tbH = juce::roundToInt(f);
    int sliderW = area.getWidth();

    auto setFs = [](juce::Label& l, float size) { l.setFont(juce::FontOptions(size)); };

    // -- DELAY --
    delayToggle.setBounds(area.removeFromTop(rowH));
    bool delayOn = delayToggle.getToggleState();
    for (auto* c : std::initializer_list<juce::Component*>{&delayTime, &delayFb, &delayMix,
                    &delayTimeL, &delayFbL, &delayMixL})
        c->setVisible(delayOn);

    if (delayOn)
    {
        int sliderH = juce::roundToInt(h * 0.08f);
        int colW = sliderW / 3;
        auto dRow = area.removeFromTop(sliderH + labelH);
        for (int i = 0; i < 3; ++i)
        {
            auto& slider = (i == 0) ? delayTime : (i == 1) ? delayFb : delayMix;
            auto& label  = (i == 0) ? delayTimeL : (i == 1) ? delayFbL : delayMixL;
            int x = dRow.getX() + i * colW;
            slider.setBounds(x, dRow.getY(), colW, sliderH);
            slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, colW - 4, tbH);
            setFs(label, f * 0.75f);
            label.setBounds(x, dRow.getY() + sliderH, colW, labelH);
        }
    }
    area.removeFromTop(gap);

    // -- REVERB --
    reverbToggle.setBounds(area.removeFromTop(rowH));
    bool reverbOn = reverbToggle.getToggleState();
    reverbIrBox.setVisible(reverbOn);
    reverbMix.setVisible(reverbOn);
    reverbMixL.setVisible(reverbOn);

    if (reverbOn)
    {
        reverbIrBox.setBounds(area.removeFromTop(rowH));
        area.removeFromTop(gap / 2);
        int sliderH = juce::roundToInt(h * 0.08f);
        reverbMix.setBounds(area.removeFromTop(sliderH));
        reverbMix.setTextBoxStyle(juce::Slider::TextBoxBelow, false, sliderW, tbH);
        setFs(reverbMixL, f * 0.75f);
        reverbMixL.setBounds(area.removeFromTop(labelH));
    }
    area.removeFromTop(gap);

    // -- LIMITER --
    setFs(limHeader, f);
    limHeader.setBounds(area.removeFromTop(rowH));
    int limSliderH = juce::roundToInt(h * 0.12f);
    int colW = sliderW / 2;
    auto limRow = area.removeFromTop(limSliderH + labelH);
    for (int i = 0; i < 2; ++i)
    {
        auto& slider = (i == 0) ? limThresh : limRelease;
        auto& label  = (i == 0) ? limThreshL : limReleaseL;
        int x = limRow.getX() + i * colW;
        slider.setBounds(x, limRow.getY(), colW, limSliderH);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, colW - 4, tbH);
        setFs(label, f * 0.75f);
        label.setBounds(x, limRow.getY() + limSliderH, colW, labelH);
    }
}
