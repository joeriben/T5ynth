#include "SequencerPanel.h"
#include "../PluginProcessor.h"

SequencerPanel::SequencerPanel(T5ynthProcessor& processor)
    : processorRef(processor)
{
    addAndMakeVisible(playButton);
    addAndMakeVisible(stopButton);

    modeBox.addItemList({"Seq", "Arp Up", "Arp Dn", "Arp UD", "Arp Rnd"}, 1);
    addAndMakeVisible(modeBox);

    bpmKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    bpmKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 1, 1);
    bpmKnob.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff4a9eff));
    bpmKnob.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2a2a2a));
    addAndMakeVisible(bpmKnob);
    bpmLabel.setText("BPM", juce::dontSendNotification);
    bpmLabel.setJustificationType(juce::Justification::centred);
    bpmLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
    addAndMakeVisible(bpmLabel);

    octKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    octKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 1, 1);
    octKnob.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff4a9eff));
    octKnob.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff2a2a2a));
    addAndMakeVisible(octKnob);
    octLabel.setText("Oct", juce::dontSendNotification);
    octLabel.setJustificationType(juce::Justification::centred);
    octLabel.setColour(juce::Label::textColourId, juce::Colour(0xff888888));
    addAndMakeVisible(octLabel);

    auto& apvts = processor.getValueTreeState();
    bpmAttach  = std::make_unique<SA>(apvts, "seq_bpm", bpmKnob);
    octAttach  = std::make_unique<SA>(apvts, "arp_octaves", octKnob);
    modeAttach = std::make_unique<CA>(apvts, "arp_mode", modeBox);

    for (int i = 0; i < 4; ++i)
        stepStates[static_cast<size_t>(i)] = true;
}

void SequencerPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0a0a0a));

    // Draw step grid
    for (int i = 0; i < numVisibleSteps; ++i)
    {
        auto r = getStepBounds(i);
        if (r.isEmpty()) continue;

        bool on = stepStates[static_cast<size_t>(i)];
        g.setColour(on ? juce::Colour(0xff2a2a2a) : juce::Colour(0xff131313));
        g.fillRect(r.reduced(1));

        if (i % 4 == 0)
        {
            g.setColour(juce::Colour(0xff333333));
            g.drawRect(r, 1);
        }
    }
}

void SequencerPanel::resized()
{
    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());
    int pad = juce::roundToInt(w * 0.01f);
    float topH = (getTopLevelComponent() != nullptr) ? static_cast<float>(getTopLevelComponent()->getHeight()) : 800.0f;
    float fs = juce::jlimit(14.0f, 24.0f, topH * 0.026f);

    bpmLabel.setFont(juce::FontOptions(fs));
    octLabel.setFont(juce::FontOptions(fs));

    auto area = getLocalBounds().reduced(pad, juce::roundToInt(h * 0.05f));

    // Left controls strip (15% of width)
    int controlsW = juce::roundToInt(w * 0.15f);
    auto controls = area.removeFromLeft(controlsW);

    int btnH = juce::roundToInt(h * 0.35f);
    int btnW = controls.getWidth() / 2 - 2;
    auto btnRow = controls.removeFromTop(btnH);
    playButton.setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft(4);
    stopButton.setBounds(btnRow.removeFromLeft(btnW));

    controls.removeFromTop(2);
    modeBox.setBounds(controls.removeFromTop(juce::roundToInt(h * 0.3f)));

    // BPM + Oct knobs
    area.removeFromLeft(pad);
    int knobDia = juce::jmin(juce::roundToInt(h * 0.55f), juce::roundToInt(w * 0.05f));
    int tbW = juce::roundToInt(knobDia * 0.9f);
    int tbH = juce::roundToInt(fs + 2.0f);
    int labelH = juce::roundToInt(h * 0.15f);

    auto bpmArea = area.removeFromLeft(knobDia + 8);
    bpmKnob.setBounds(bpmArea.removeFromTop(knobDia).withSizeKeepingCentre(knobDia, knobDia));
    bpmKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, tbW, tbH);
    bpmLabel.setBounds(bpmArea.withHeight(labelH));

    auto octArea = area.removeFromLeft(knobDia + 8);
    octKnob.setBounds(octArea.removeFromTop(knobDia).withSizeKeepingCentre(knobDia, knobDia));
    octKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, tbW, tbH);
    octLabel.setBounds(octArea.withHeight(labelH));

    // Step grid: remaining space
    area.removeFromLeft(pad);
    gridArea = area;
}

void SequencerPanel::mouseDown(const juce::MouseEvent& e)
{
    for (int i = 0; i < numVisibleSteps; ++i)
    {
        if (getStepBounds(i).contains(e.getPosition()))
        {
            stepStates[static_cast<size_t>(i)] = !stepStates[static_cast<size_t>(i)];
            repaint();
            return;
        }
    }
}

juce::Rectangle<int> SequencerPanel::getStepBounds(int step) const
{
    if (gridArea.isEmpty() || gridArea.getWidth() < numVisibleSteps) return {};
    int stepW = gridArea.getWidth() / numVisibleSteps;
    return { gridArea.getX() + step * stepW, gridArea.getY(), stepW, gridArea.getHeight() };
}
