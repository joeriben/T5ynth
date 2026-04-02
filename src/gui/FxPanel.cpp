#include "FxPanel.h"

static juce::String fmtMs(double v)  { return juce::String(juce::roundToInt(v)) + "ms"; }
static juce::String fmtF2(double v)  { return juce::String(v, 2); }

static juce::String fmtDampHz(double v)
{
    // Must match DSP mapping in DelayLine::setDamp: freq = 20000 * pow(500/20000, d)
    // 0 = bright (20kHz cutoff), 1 = dark (500Hz cutoff)
    double hz = 20000.0 * std::pow(500.0 / 20000.0, v);
    if (hz >= 1000.0) return juce::String(hz / 1000.0, 1) + "k";
    return juce::String(juce::roundToInt(hz)) + "Hz";
}

FxPanel::FxPanel(juce::AudioProcessorValueTreeState& apvts)
{
    // Section header
    paintSectionHeader(fxHeader, "EFFECTS", kFxCol);
    addAndMakeVisible(fxHeader);

    // ── Delay ──
    delayToggle.setColour(juce::ToggleButton::textColourId, kDim);
    delayToggle.setColour(juce::ToggleButton::tickColourId, kFxCol);
    delayToggle.onClick = [this] { if (initialized) { repaint(); resized(); } };
    addAndMakeVisible(delayToggle);

    delayTimeRow = std::make_unique<SliderRow>("Time", fmtMs, kFxCol);
    delayFbRow   = std::make_unique<SliderRow>("FB",   fmtF2, kFxCol);
    delayDampRow = std::make_unique<SliderRow>("Damp", fmtDampHz, kFxCol);
    delayMixRow  = std::make_unique<SliderRow>("Mix",  fmtF2, kFxCol);

    for (auto* r : { delayTimeRow.get(), delayFbRow.get(), delayDampRow.get(), delayMixRow.get() })
        addAndMakeVisible(*r);

    delayTimeA = std::make_unique<SA>(apvts, "delay_time",     delayTimeRow->getSlider());
    delayFbA   = std::make_unique<SA>(apvts, "delay_feedback", delayFbRow->getSlider());
    delayDampA = std::make_unique<SA>(apvts, "delay_damp",     delayDampRow->getSlider());
    delayMixA  = std::make_unique<SA>(apvts, "delay_mix",      delayMixRow->getSlider());

    delayTimeRow->updateValue();
    delayFbRow->updateValue();
    delayDampRow->updateValue();
    delayMixRow->updateValue();

    // ── Reverb ──
    reverbToggle.setColour(juce::ToggleButton::textColourId, kDim);
    reverbToggle.setColour(juce::ToggleButton::tickColourId, kFxCol);
    reverbToggle.onClick = [this] { if (initialized) { repaint(); resized(); } };
    addAndMakeVisible(reverbToggle);

    reverbIrBox.addItemList({"Bright", "Medium", "Dark"}, 1);
    addAndMakeVisible(reverbIrBox);

    reverbMixRow = std::make_unique<SliderRow>("Mix", fmtF2, kFxCol);
    addAndMakeVisible(*reverbMixRow);

    reverbMixA = std::make_unique<SA>(apvts, "reverb_mix", reverbMixRow->getSlider());
    reverbIrA  = std::make_unique<CA>(apvts, "reverb_ir",  reverbIrBox);
    reverbMixRow->updateValue();

    // All components ready
    initialized = true;
    delayEnableA  = std::make_unique<BA>(apvts, "delay_enabled", delayToggle);
    reverbEnableA = std::make_unique<BA>(apvts, "reverb_enabled", reverbToggle);
}

float FxPanel::fs() const
{
    float topH = (getTopLevelComponent() != nullptr)
                     ? static_cast<float>(getTopLevelComponent()->getHeight()) : 800.0f;
    return juce::jlimit(12.0f, 22.0f, topH * 0.022f);
}

void FxPanel::paint(juce::Graphics& g)
{
    g.fillAll(kCard);

    // Vertical separator on left edge (between Seq and FX)
    g.setColour(kBorder);
    g.drawVerticalLine(0, 0.0f, static_cast<float>(getHeight()));
}

void FxPanel::resized()
{
    auto area = getLocalBounds().reduced(6, 0);
    float topH = getTopLevelComponent()
                     ? static_cast<float>(getTopLevelComponent()->getHeight()) : 800.0f;
    int headerH = juce::jlimit(14, 20, juce::roundToInt(topH * 0.022f));
    fxHeader.setFont(juce::FontOptions(static_cast<float>(headerH) * 0.85f));
    fxHeader.setBounds(area.removeFromTop(headerH));
    area.removeFromTop(juce::jmax(3, headerH / 5));

    int rowH = juce::jmin(juce::roundToInt(static_cast<float>(getHeight()) * 0.22f), 20);
    int gap = 2;

    // Stacked: Delay on top, Reverb below
    // ── Delay ──
    delayToggle.setBounds(area.removeFromTop(rowH));
    area.removeFromTop(gap);

    bool delayOn = delayToggle.getToggleState();
    for (auto* r : { delayTimeRow.get(), delayFbRow.get(), delayDampRow.get(), delayMixRow.get() })
        r->setVisible(delayOn);

    if (delayOn)
    {
        int colW = (area.getWidth() - 2) / 2;
        auto row1 = area.removeFromTop(rowH);
        delayTimeRow->setBounds(row1.removeFromLeft(colW));
        row1.removeFromLeft(2);
        delayFbRow->setBounds(row1);

        area.removeFromTop(gap);
        auto row2 = area.removeFromTop(rowH);
        delayDampRow->setBounds(row2.removeFromLeft(colW));
        row2.removeFromLeft(2);
        delayMixRow->setBounds(row2);
    }

    area.removeFromTop(gap * 2);

    // ── Reverb ──
    reverbToggle.setBounds(area.removeFromTop(rowH));
    area.removeFromTop(gap);

    bool reverbOn = reverbToggle.getToggleState();
    reverbIrBox.setVisible(reverbOn);
    reverbMixRow->setVisible(reverbOn);

    if (reverbOn)
    {
        int colW = (area.getWidth() - 2) / 2;
        auto row = area.removeFromTop(rowH);
        reverbIrBox.setBounds(row.removeFromLeft(colW));
        row.removeFromLeft(2);
        reverbMixRow->setBounds(row);
    }
}
