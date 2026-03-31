#include "PluginEditor.h"
#include "PluginProcessor.h"
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

T5ynthEditor::T5ynthEditor(T5ynthProcessor& processor)
    : AudioProcessorEditor(processor),
      processorRef(processor),
      mainPanel(processor)
{
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(mainPanel);

    // Register model panel in the standalone settings dialog
    if (auto* holder = StandalonePluginHolder::getInstance())
        holder->extraSettingsPanel = &mainPanel.getModelPanel();

    setSize(1200, 800);
    setResizable(true, true);
    setResizeLimits(1050, 700, 2400, 1600);
    getConstrainer()->setFixedAspectRatio(3.0 / 2.0);
}

T5ynthEditor::~T5ynthEditor()
{
    if (auto* holder = StandalonePluginHolder::getInstance())
        holder->extraSettingsPanel = nullptr;

    setLookAndFeel(nullptr);
}

void T5ynthEditor::paint(juce::Graphics&) {}

void T5ynthEditor::resized()
{
    mainPanel.setBounds(getLocalBounds());
}
