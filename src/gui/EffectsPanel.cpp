#include "EffectsPanel.h"
#include "../dsp/BlockParams.h"
#include "GuiHelpers.h"

static const auto kGreen  = juce::Colour(0xff4a9eff);
static const auto kDim    = kTextSecondary;

void EffectsPanel::initEnv(EnvSection& env, const juce::String& name,
                           const juce::String& aId, const juce::String& dId,
                           const juce::String& sId, const juce::String& rId,
                           juce::AudioProcessorValueTreeState& apvts)
{
    env.header.setText(name, juce::dontSendNotification);
    env.header.setColour(juce::Label::textColourId, kDim);
    addAndMakeVisible(env.header);

    // EffectsPanel dropdowns are display-only (no APVTS attachment); labels
    // come from the single source of truth in BlockParams.h so they never
    // drift from SynthPanel/APVTS. The previous hardcoded list had "---" at
    // the END (index 13) and only 10 LFO targets; the new layout uses the
    // kEntries order which has "---" at index 0 and all 11 LFO targets.
    juce::StringArray envTargetItems;
    for (const auto& e : EnvTarget::kEntries) envTargetItems.add(e.label);
    env.targetBox.addItemList(envTargetItems, 1);
    // AMP envelope is conceptually the DCA target (index 1 = "DCA");
    // mod envelopes default to the first real modulation target, "Filter"
    // (index 2) — consistent with SynthPanel's initEnv defaults.
    env.targetBox.setSelectedId(name == "AMP" ? 2 : 3, juce::dontSendNotification);
    env.targetBox.onChange = [this] { resized(); };
    addAndMakeVisible(env.targetBox);

    for (auto* knob : { &env.a, &env.d, &env.s, &env.r })
    {
        knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 1, 1);
        knob->setColour(juce::Slider::rotarySliderFillColourId, kGreen);
        knob->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2a2a2a));
        addAndMakeVisible(*knob);
    }
    env.aL.setText("A", juce::dontSendNotification); env.aL.setJustificationType(juce::Justification::centred);
    env.dL.setText("D", juce::dontSendNotification); env.dL.setJustificationType(juce::Justification::centred);
    env.sL.setText("S", juce::dontSendNotification); env.sL.setJustificationType(juce::Justification::centred);
    env.rL.setText("R", juce::dontSendNotification); env.rL.setJustificationType(juce::Justification::centred);
    for (auto* l : { &env.aL, &env.dL, &env.sL, &env.rL })
    {
        l->setColour(juce::Label::textColourId, kDim);
        addAndMakeVisible(*l);
    }

    env.aA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, aId, env.a);
    env.dA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, dId, env.d);
    env.sA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, sId, env.s);
    env.rA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, rId, env.r);
}

void EffectsPanel::initLfo(LfoSection& lfo, const juce::String& name,
                           const juce::String& rateId, const juce::String& depthId,
                           const juce::String& waveId,
                           juce::AudioProcessorValueTreeState& apvts)
{
    lfo.header.setText(name, juce::dontSendNotification);
    lfo.header.setColour(juce::Label::textColourId, kDim);
    addAndMakeVisible(lfo.header);

    // Fix drift against BlockParams.h LfoTarget::kEntries: the old hardcoded
    // list had stale "Xmod Rate"/"Xmod Depth" (which never existed in APVTS),
    // was missing "ENV3 Amt", and had "---" at the end (index 10) instead of
    // the start. The new iteration matches APVTS exactly.
    juce::StringArray lfoTargetItems;
    for (const auto& e : LfoTarget::kEntries) lfoTargetItems.add(e.label);
    lfo.targetBox.addItemList(lfoTargetItems, 1);
    lfo.targetBox.setSelectedId(1, juce::dontSendNotification); // "---" (None)
    lfo.targetBox.onChange = [this] { resized(); };
    addAndMakeVisible(lfo.targetBox);

    juce::StringArray lfoWaveItems;
    for (const auto& e : LfoWave::kEntries) lfoWaveItems.add(e.label);
    lfo.waveBox.addItemList(lfoWaveItems, 1);
    addAndMakeVisible(lfo.waveBox);

    for (auto* knob : { &lfo.rate, &lfo.depth })
    {
        knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 1, 1);
        knob->setColour(juce::Slider::rotarySliderFillColourId, kGreen);
        knob->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2a2a2a));
        addAndMakeVisible(*knob);
    }
    lfo.rateL.setText("Rate", juce::dontSendNotification);
    lfo.depthL.setText("Dep", juce::dontSendNotification);
    for (auto* l : { &lfo.rateL, &lfo.depthL })
    {
        l->setJustificationType(juce::Justification::centred);
        l->setColour(juce::Label::textColourId, kDim);
        addAndMakeVisible(*l);
    }

    lfo.rateA  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, rateId, lfo.rate);
    lfo.depthA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, depthId, lfo.depth);
    lfo.waveA  = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, waveId, lfo.waveBox);
}

EffectsPanel::EffectsPanel(juce::AudioProcessorValueTreeState& apvts)
{
    initEnv(ampEnv, "AMP", PID::ampAttack, PID::ampDecay, PID::ampSustain, PID::ampRelease, apvts);
    initEnv(mod1Env, "MOD 1", PID::mod1Attack, PID::mod1Decay, PID::mod1Sustain, PID::mod1Release, apvts);
    initEnv(mod2Env, "MOD 2", PID::mod2Attack, PID::mod2Decay, PID::mod2Sustain, PID::mod2Release, apvts);

    initLfo(lfo1, "LFO 1", PID::lfo1Rate, PID::lfo1Depth, PID::lfo1Wave, apvts);
    initLfo(lfo2, "LFO 2", PID::lfo2Rate, PID::lfo2Depth, PID::lfo2Wave, apvts);

    // Drift
    driftToggle.setColour(juce::ToggleButton::textColourId, kDim);
    driftToggle.setColour(juce::ToggleButton::tickColourId, kGreen);
    driftToggle.onClick = [this] { resized(); };
    addAndMakeVisible(driftToggle);
    for (auto* s : { &d1Rate, &d1Depth, &d2Rate, &d2Depth, &d3Rate, &d3Depth })
    {
        s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s->setColour(juce::Slider::rotarySliderFillColourId, kGreen);
        s->setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2a2a2a));
        addAndMakeVisible(*s);
    }

    driftEnableA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, PID::driftEnabled, driftToggle);
    d1RA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, PID::drift1Rate, d1Rate);
    d1DA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, PID::drift1Depth, d1Depth);
    d2RA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, PID::drift2Rate, d2Rate);
    d2DA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, PID::drift2Depth, d2Depth);
    d3RA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, PID::drift3Rate, d3Rate);
    d3DA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, PID::drift3Depth, d3Depth);
}

float EffectsPanel::fs() const
{
    float topH = (getTopLevelComponent() != nullptr)
                     ? static_cast<float>(getTopLevelComponent()->getHeight()) : 800.0f;
    return juce::jlimit(14.0f, 26.0f, topH * 0.028f);
}

void EffectsPanel::paint(juce::Graphics&) {}

void EffectsPanel::layoutEnv(EnvSection& env, juce::Rectangle<int>& area, float f, int knobDia)
{
    int rowH = juce::roundToInt(f * 1.5f);
    int gap = juce::roundToInt(f * 0.3f);
    int labelH = juce::roundToInt(f * 1.2f);
    int tbW = juce::roundToInt(knobDia * 0.85f);
    int tbH = juce::roundToInt(f);

    env.header.setFont(juce::FontOptions(f));
    auto hdr = area.removeFromTop(rowH);
    env.header.setBounds(hdr.removeFromLeft(juce::roundToInt(hdr.getWidth() * 0.35f)));
    env.targetBox.setBounds(hdr.removeFromLeft(juce::roundToInt(hdr.getWidth() * 0.7f)));

    bool active = env.targetBox.getSelectedId() != env.targetBox.getNumItems();
    for (auto* c : std::initializer_list<juce::Component*>{ &env.a, &env.d, &env.s, &env.r,
                     &env.aL, &env.dL, &env.sL, &env.rL })
        c->setVisible(active);

    if (active)
    {
        // 2x2 grid of knobs
        int colW = area.getWidth() / 2;
        auto row1 = area.removeFromTop(knobDia + labelH);
        auto row2 = area.removeFromTop(knobDia + labelH);

        auto placeKnob = [&](juce::Slider& knob, juce::Label& label, juce::Rectangle<int>& row, int col)
        {
            int x = row.getX() + col * colW + (colW - knobDia) / 2;
            knob.setBounds(x, row.getY(), knobDia, knobDia);
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, tbW, tbH);
            label.setFont(juce::FontOptions(f * 0.8f));
            label.setBounds(x, row.getY() + knobDia, knobDia, labelH);
        };
        placeKnob(env.a, env.aL, row1, 0);
        placeKnob(env.d, env.dL, row1, 1);
        placeKnob(env.s, env.sL, row2, 0);
        placeKnob(env.r, env.rL, row2, 1);
    }
    area.removeFromTop(gap);
}

void EffectsPanel::layoutLfo(LfoSection& lfo, juce::Rectangle<int>& area, float f, int knobDia)
{
    int rowH = juce::roundToInt(f * 1.5f);
    int gap = juce::roundToInt(f * 0.3f);
    int labelH = juce::roundToInt(f * 1.2f);
    int tbW = juce::roundToInt(knobDia * 0.85f);
    int tbH = juce::roundToInt(f);

    lfo.header.setFont(juce::FontOptions(f));
    auto hdr = area.removeFromTop(rowH);
    lfo.header.setBounds(hdr.removeFromLeft(juce::roundToInt(hdr.getWidth() * 0.3f)));
    lfo.targetBox.setBounds(hdr.removeFromLeft(juce::roundToInt(hdr.getWidth() * 0.5f)));

    bool active = lfo.targetBox.getSelectedId() != lfo.targetBox.getNumItems();
    for (auto* c : std::initializer_list<juce::Component*>{ &lfo.rate, &lfo.depth,
                     &lfo.rateL, &lfo.depthL, &lfo.waveBox })
        c->setVisible(active);

    if (active)
    {
        // Wave selector
        lfo.waveBox.setBounds(area.removeFromTop(rowH).reduced(0, 2));

        // Rate + Depth side by side
        int colW = area.getWidth() / 2;
        auto knobRow = area.removeFromTop(knobDia + labelH);
        auto placeKnob = [&](juce::Slider& knob, juce::Label& label, int col)
        {
            int x = knobRow.getX() + col * colW + (colW - knobDia) / 2;
            knob.setBounds(x, knobRow.getY(), knobDia, knobDia);
            knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, tbW, tbH);
            label.setFont(juce::FontOptions(f * 0.8f));
            label.setBounds(x, knobRow.getY() + knobDia, knobDia, labelH);
        };
        placeKnob(lfo.rate, lfo.rateL, 0);
        placeKnob(lfo.depth, lfo.depthL, 1);
    }
    area.removeFromTop(gap);
}

void EffectsPanel::resized()
{
    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());
    int pad = juce::roundToInt(w * 0.06f);
    auto area = getLocalBounds().reduced(pad, juce::roundToInt(h * 0.01f));
    float f = fs();
    int knobDia = juce::jmin(juce::roundToInt(h * 0.065f), juce::roundToInt(w * 0.35f));

    layoutEnv(ampEnv, area, f, knobDia);
    layoutEnv(mod1Env, area, f, knobDia);
    layoutEnv(mod2Env, area, f, knobDia);
    layoutLfo(lfo1, area, f, knobDia);
    layoutLfo(lfo2, area, f, knobDia);

    // Drift
    int rowH = juce::roundToInt(f * 1.5f);
    driftToggle.setBounds(area.removeFromTop(rowH));
    bool driftOn = driftToggle.getToggleState();
    for (auto* s : { &d1Rate, &d1Depth, &d2Rate, &d2Depth, &d3Rate, &d3Depth })
        s->setVisible(driftOn);

    if (driftOn)
    {
        int dDia = juce::jmin(knobDia, juce::roundToInt(w * 0.2f));
        int dStep = dDia + 2;
        auto dRow = area.removeFromTop(dDia + 4);
        int x = dRow.getX();
        for (auto* s : { &d1Rate, &d1Depth, &d2Rate, &d2Depth, &d3Rate, &d3Depth })
        {
            s->setBounds(x, dRow.getY(), dDia, dDia);
            x += dStep;
            if (x + dDia > dRow.getRight()) { x = dRow.getX(); }
        }
    }
}
