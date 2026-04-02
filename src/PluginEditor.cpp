#include "PluginEditor.h"
#include "PluginProcessor.h"

T5ynthEditor::T5ynthEditor(T5ynthProcessor& processor)
    : AudioProcessorEditor(processor),
      processorRef(processor),
      mainPanel(processor)
{
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(mainPanel);

    // Model settings are now in our own overlay — no longer in JUCE's dialog

    setSize(1200, 800);
    setResizable(true, true);
    setResizeLimits(1050, 700, 2400, 1600);
    getConstrainer()->setFixedAspectRatio(3.0 / 2.0);
}

T5ynthEditor::~T5ynthEditor()
{
    setLookAndFeel(nullptr);
}

void T5ynthEditor::paint(juce::Graphics&) {}

void T5ynthEditor::resized()
{
    mainPanel.setBounds(getLocalBounds());
}
