#include "SequencerPanel.h"
#include "../PluginProcessor.h"

SequencerPanel::SequencerPanel(T5ynthProcessor& processor)
    : processorRef(processor)
{
    playButton.setColour(juce::TextButton::buttonColourId, kSurface);
    playButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff4caf50));
    playButton.onClick = [this] {
        // Push step states and notes to processor's sequencer
        auto& seq = processorRef.getStepSequencer();
        int oct = juce::roundToInt(octRow->getSlider().getValue());
        int baseNote = juce::jlimit(0, 108, oct * 12);
        for (int i = 0; i < numVisibleSteps; ++i)
        {
            seq.setStepEnabled(i, stepStates[static_cast<size_t>(i)]);
            seq.setStepNote(i, baseNote + i);
        }

        // Set seq_running parameter (audio thread picks it up)
        if (auto* param = processorRef.getValueTreeState().getParameter("seq_running"))
            param->setValueNotifyingHost(1.0f);

        playing = true;
        repaint();
    };
    addAndMakeVisible(playButton);

    stopButton.setColour(juce::TextButton::buttonColourId, kSurface);
    stopButton.setColour(juce::TextButton::textColourOffId, kDim);
    stopButton.onClick = [this] {
        if (auto* param = processorRef.getValueTreeState().getParameter("seq_running"))
            param->setValueNotifyingHost(0.0f);

        playing = false;
        currentStep = -1;
        beatLabel.setText("", juce::dontSendNotification);
        midiMonitor.setText("", juce::dontSendNotification);
        repaint();
    };
    addAndMakeVisible(stopButton);

    modeBox.addItemList({"Seq", "Arp Up", "Arp Dn", "Arp UD", "Arp Rnd"}, 1);
    addAndMakeVisible(modeBox);

    beatLabel.setColour(juce::Label::textColourId, kAccent);
    beatLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(beatLabel);

    midiMonitor.setColour(juce::Label::textColourId, juce::Colour(0xff4ade80));
    midiMonitor.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(midiMonitor);

    // Always run timer for MIDI monitor
    startTimerHz(30);

    bpmRow = std::make_unique<SliderRow>("BPM", [](double v) {
        return juce::String(juce::roundToInt(v));
    });
    addAndMakeVisible(*bpmRow);

    octRow = std::make_unique<SliderRow>("Oct", [](double v) {
        return juce::String(juce::roundToInt(v));
    });
    addAndMakeVisible(*octRow);

    auto& apvts = processor.getValueTreeState();
    bpmAttach  = std::make_unique<SA>(apvts, "seq_bpm",     bpmRow->getSlider());
    octAttach  = std::make_unique<SA>(apvts, "arp_octaves", octRow->getSlider());
    modeAttach = std::make_unique<CA>(apvts, "seq_mode",    modeBox);

    bpmRow->updateValue();
    octRow->updateValue();

    bpmRow->getSlider().onValueChange = [this] {
        bpmRow->updateValue();
    };

    // Octave changes update step notes
    octRow->getSlider().onValueChange = [this] {
        octRow->updateValue();
        if (playing)
        {
            auto& seq = processorRef.getStepSequencer();
            int oct = juce::roundToInt(octRow->getSlider().getValue());
            int baseNote = juce::jlimit(0, 108, oct * 12);
            for (int i = 0; i < numVisibleSteps; ++i)
                seq.setStepNote(i, baseNote + i);
        }
    };

    for (int i = 0; i < 4; ++i)
        stepStates[static_cast<size_t>(i)] = true;
}

static juce::String midiNoteToName(int note)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String(names[note % 12]) + juce::String(note / 12 - 1);
}

void SequencerPanel::timerCallback()
{
    // Step position
    int step = processorRef.getStepSequencer().currentStepForGui.load(std::memory_order_relaxed);
    if (step != currentStep)
    {
        currentStep = step;
        if (step >= 0)
        {
            int bar = step / 4 + 1;
            int beat = step % 4 + 1;
            beatLabel.setText(juce::String(bar) + "." + juce::String(beat), juce::dontSendNotification);
        }
        repaint();
    }

    // MIDI monitor
    int note = processorRef.lastMidiNote.load(std::memory_order_relaxed);
    bool on = processorRef.lastMidiNoteOn.load(std::memory_order_relaxed);
    int vel = processorRef.lastMidiVelocity.load(std::memory_order_relaxed);
    if (note >= 0)
    {
        juce::String txt = on ? ("MIDI: " + midiNoteToName(note) + " v" + juce::String(vel))
                              : ("MIDI: " + midiNoteToName(note) + " off");
        midiMonitor.setText(txt, juce::dontSendNotification);
        midiMonitor.setColour(juce::Label::textColourId,
                              on ? juce::Colour(0xff4ade80) : kDim);
    }
}

void SequencerPanel::paint(juce::Graphics& g)
{
    g.fillAll(kCard);

    for (int i = 0; i < numVisibleSteps; ++i)
    {
        auto r = getStepBounds(i);
        if (r.isEmpty()) continue;

        bool on = stepStates[static_cast<size_t>(i)];
        bool active = (i == currentStep);

        if (active)
            g.setColour(kAccent.withAlpha(0.5f));
        else if (on)
            g.setColour(kSurface);
        else
            g.setColour(kBg);

        g.fillRect(r.reduced(1));

        // Beat group border every 4 steps
        if (i % 4 == 0)
        {
            g.setColour(kBorder);
            g.drawRect(r, 1);
        }

        // Active step indicator dot
        if (active)
        {
            g.setColour(kAccent);
            int dotSize = juce::jmin(6, r.getWidth() / 4);
            g.fillEllipse(static_cast<float>(r.getCentreX() - dotSize / 2),
                          static_cast<float>(r.getY() + 2),
                          static_cast<float>(dotSize),
                          static_cast<float>(dotSize));
        }
    }

    // Playing LED
    int ledSize = 8;
    int ledX = playButton.getX() - ledSize - 4;
    int ledY = playButton.getBounds().getCentreY() - ledSize / 2;
    g.setColour(playing ? juce::Colour(0xff4caf50) : kDimmer);
    g.fillEllipse(static_cast<float>(ledX), static_cast<float>(ledY),
                  static_cast<float>(ledSize), static_cast<float>(ledSize));
}

void SequencerPanel::resized()
{
    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());
    int pad = juce::roundToInt(w * 0.01f);

    auto area = getLocalBounds().reduced(pad, juce::roundToInt(h * 0.05f));

    // Left controls strip (18% of width)
    int controlsW = juce::roundToInt(w * 0.18f);
    auto controls = area.removeFromLeft(controlsW);

    int rowH = juce::roundToInt(h * 0.28f);

    // Play / Stop + beat display
    auto btnRow = controls.removeFromTop(rowH);
    int btnW = btnRow.getWidth() / 3 - 2;
    btnRow.removeFromLeft(12); // space for LED
    playButton.setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft(2);
    stopButton.setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft(4);
    beatLabel.setFont(juce::FontOptions(static_cast<float>(rowH) * 0.7f));
    beatLabel.setBounds(btnRow);

    controls.removeFromTop(2);
    modeBox.setBounds(controls.removeFromTop(rowH));
    controls.removeFromTop(2);
    midiMonitor.setFont(juce::FontOptions(juce::jmax(9.0f, static_cast<float>(rowH) * 0.55f)));
    midiMonitor.setBounds(controls);

    // BPM + Oct
    area.removeFromLeft(pad);
    int sliderStripW = juce::roundToInt(w * 0.18f);
    auto sliderStrip = area.removeFromLeft(sliderStripW);

    int sliderRowH = juce::jmin(juce::roundToInt(h * 0.38f), 24);
    int sliderGap = juce::roundToInt(h * 0.08f);
    sliderStrip.removeFromTop((sliderStrip.getHeight() - sliderRowH * 2 - sliderGap) / 2);
    bpmRow->setBounds(sliderStrip.removeFromTop(sliderRowH));
    sliderStrip.removeFromTop(sliderGap);
    octRow->setBounds(sliderStrip.removeFromTop(sliderRowH));

    // Step grid
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
            processorRef.getStepSequencer().setStepEnabled(i, stepStates[static_cast<size_t>(i)]);
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
