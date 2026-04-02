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
    // ══════════ DELAY section ══════════
    paintSectionHeader(delayHeader, "DELAY", kFxCol);
    addAndMakeVisible(delayHeader);

    // Delay type switchbox: OFF / Stereo
    delayTypeHidden.addItemList({"Off", "Stereo"}, 1);
    delayTypeHidden.onChange = [this] {
        int id = delayTypeHidden.getSelectedId();
        for (int i = 0; i < kNumDelayBtns; ++i)
            delayTypeBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
        updateVisibility();
    };

    static const char* delayLabels[] = {"OFF", "Stereo"};
    for (int i = 0; i < kNumDelayBtns; ++i)
    {
        delayTypeBtns[i].setButtonText(delayLabels[i]);
        delayTypeBtns[i].setColour(juce::TextButton::buttonColourId, kSurface);
        delayTypeBtns[i].setColour(juce::TextButton::buttonOnColourId, kFxCol);
        delayTypeBtns[i].setColour(juce::TextButton::textColourOffId, kDim);
        delayTypeBtns[i].setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        delayTypeBtns[i].setClickingTogglesState(true);
        delayTypeBtns[i].setRadioGroupId(4001);
        delayTypeBtns[i].onClick = [this, i] { delayTypeHidden.setSelectedId(i + 1); };
        addAndMakeVisible(delayTypeBtns[i]);
    }

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

    // Attach APVTS AFTER buttons are set up (triggers onChange → updateVisibility)
    delayTypeA = std::make_unique<CA>(apvts, "delay_type", delayTypeHidden);

    // ══════════ REVERB section ══════════
    paintSectionHeader(reverbHeader, "REVERB", kFxCol);
    addAndMakeVisible(reverbHeader);

    // Reverb type switchbox: OFF / Dark / Med / Brt / Algo
    reverbTypeHidden.addItemList({"Off", "Dark", "Medium", "Bright", "Algo"}, 1);
    reverbTypeHidden.onChange = [this] {
        int id = reverbTypeHidden.getSelectedId();
        for (int i = 0; i < kNumReverbBtns; ++i)
            reverbTypeBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
        updateVisibility();
    };

    static const char* reverbLabels[] = {"OFF", "Drk", "Med", "Brt", "Algo"};
    for (int i = 0; i < kNumReverbBtns; ++i)
    {
        reverbTypeBtns[i].setButtonText(reverbLabels[i]);
        reverbTypeBtns[i].setColour(juce::TextButton::buttonColourId, kSurface);
        reverbTypeBtns[i].setColour(juce::TextButton::buttonOnColourId, kFxCol);
        reverbTypeBtns[i].setColour(juce::TextButton::textColourOffId, kDim);
        reverbTypeBtns[i].setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        reverbTypeBtns[i].setClickingTogglesState(true);
        reverbTypeBtns[i].setRadioGroupId(4002);
        reverbTypeBtns[i].onClick = [this, i] { reverbTypeHidden.setSelectedId(i + 1); };
        addAndMakeVisible(reverbTypeBtns[i]);
    }

    reverbMixRow  = std::make_unique<SliderRow>("Mix",   fmtF2, kFxCol);
    algoRoomRow   = std::make_unique<SliderRow>("Room",  fmtF2, kFxCol);
    algoDampRow   = std::make_unique<SliderRow>("Damp",  fmtF2, kFxCol);
    algoWidthRow  = std::make_unique<SliderRow>("Width", fmtF2, kFxCol);

    for (auto* r : { reverbMixRow.get(), algoRoomRow.get(), algoDampRow.get(), algoWidthRow.get() })
        addAndMakeVisible(*r);

    reverbMixA = std::make_unique<SA>(apvts, "reverb_mix",   reverbMixRow->getSlider());
    algoRoomA  = std::make_unique<SA>(apvts, "algo_room",    algoRoomRow->getSlider());
    algoDampA  = std::make_unique<SA>(apvts, "algo_damping", algoDampRow->getSlider());
    algoWidthA = std::make_unique<SA>(apvts, "algo_width",   algoWidthRow->getSlider());

    reverbMixRow->updateValue();
    algoRoomRow->updateValue();
    algoDampRow->updateValue();
    algoWidthRow->updateValue();

    // Attach APVTS AFTER buttons are set up
    reverbTypeA = std::make_unique<CA>(apvts, "reverb_type", reverbTypeHidden);
}

void FxPanel::updateVisibility()
{
    // Guard: called by APVTS attachment before all components are created
    if (!reverbMixRow)
        return;

    constexpr float dimAlpha = 0.3f;

    // Delay: always visible, dimmed when OFF
    bool delayOn = delayTypeHidden.getSelectedId() > 1;
    float delayAlpha = delayOn ? 1.0f : dimAlpha;
    for (auto* r : { delayTimeRow.get(), delayFbRow.get(), delayDampRow.get(), delayMixRow.get() })
    {
        r->setAlpha(delayAlpha);
        r->setEnabled(delayOn);
    }

    // Reverb: always visible; dim params based on mode
    bool reverbOn = reverbTypeHidden.getSelectedId() > 1;
    bool algoOn   = reverbTypeHidden.getSelectedId() == 5;
    float reverbAlpha = reverbOn ? 1.0f : dimAlpha;
    // Room/Damp/Width: active only for Algo, dimmed for Convolution and OFF
    float algoParamAlpha = algoOn ? 1.0f : dimAlpha;
    for (auto* r : { algoRoomRow.get(), algoDampRow.get(), algoWidthRow.get() })
    {
        r->setAlpha(algoParamAlpha);
        r->setEnabled(algoOn);
    }
    // Mix: active whenever reverb is on
    reverbMixRow->setAlpha(reverbAlpha);
    reverbMixRow->setEnabled(reverbOn);

    resized();
    repaint();
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

    // SwitchBox borders
    paintSwitchBoxBorder(g, delayTypeSwitchBounds);
    paintSwitchBoxBorder(g, reverbTypeSwitchBounds);
}

void FxPanel::resized()
{
    auto area = getLocalBounds().reduced(6, 0);
    float topH = getTopLevelComponent()
                     ? static_cast<float>(getTopLevelComponent()->getHeight()) : 800.0f;
    int headerH = juce::jlimit(14, 20, juce::roundToInt(topH * 0.022f));
    float f = static_cast<float>(headerH);

    int rowH = juce::jmin(juce::roundToInt(static_cast<float>(getHeight()) * 0.14f), 20);
    int gap = 2;

    // ── DELAY header ──
    delayHeader.setFont(juce::FontOptions(f * 0.85f));
    delayHeader.setBounds(area.removeFromTop(headerH));
    area.removeFromTop(gap);

    // Delay type switchbox
    auto delaySwRow = area.removeFromTop(rowH);
    int delayCellW = delaySwRow.getWidth() / kNumDelayBtns;
    for (int i = 0; i < kNumDelayBtns; ++i)
    {
        int edges = 0;
        if (i > 0) edges |= juce::Button::ConnectedOnLeft;
        if (i < kNumDelayBtns - 1) edges |= juce::Button::ConnectedOnRight;
        delayTypeBtns[i].setConnectedEdges(edges);
        delayTypeBtns[i].setBounds(delaySwRow.removeFromLeft(delayCellW));
    }
    delayTypeSwitchBounds = delayTypeBtns[0].getBounds()
        .getUnion(delayTypeBtns[kNumDelayBtns - 1].getBounds());
    area.removeFromTop(gap);

    // Delay params — always laid out, dimmed when OFF
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

    // ── REVERB header ──
    reverbHeader.setFont(juce::FontOptions(f * 0.85f));
    reverbHeader.setBounds(area.removeFromTop(headerH));
    area.removeFromTop(gap);

    // Reverb type switchbox
    auto revSwRow = area.removeFromTop(rowH);
    int revCellW = revSwRow.getWidth() / kNumReverbBtns;
    for (int i = 0; i < kNumReverbBtns; ++i)
    {
        int edges = 0;
        if (i > 0) edges |= juce::Button::ConnectedOnLeft;
        if (i < kNumReverbBtns - 1) edges |= juce::Button::ConnectedOnRight;
        reverbTypeBtns[i].setConnectedEdges(edges);
        reverbTypeBtns[i].setBounds(revSwRow.removeFromLeft(revCellW));
    }
    reverbTypeSwitchBounds = reverbTypeBtns[0].getBounds()
        .getUnion(reverbTypeBtns[kNumReverbBtns - 1].getBounds());
    area.removeFromTop(gap);

    // Reverb params — always 2 rows: Room+Damp, Width+Mix (dimmed when inactive)
    {
        int colW = (area.getWidth() - 2) / 2;
        auto row1 = area.removeFromTop(rowH);
        algoRoomRow->setBounds(row1.removeFromLeft(colW));
        row1.removeFromLeft(2);
        algoDampRow->setBounds(row1);

        area.removeFromTop(gap);
        auto row2 = area.removeFromTop(rowH);
        algoWidthRow->setBounds(row2.removeFromLeft(colW));
        row2.removeFromLeft(2);
        reverbMixRow->setBounds(row2);
    }
}
