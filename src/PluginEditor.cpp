#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "BinaryData.h"

T5ynthEditor::T5ynthEditor(T5ynthProcessor& processor)
    : AudioProcessorEditor(processor),
      processorRef(processor),
      mainPanel(processor)
{
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(mainPanel);

    // Model settings are now in our own overlay — no longer in JUCE's dialog

    setSize(1300, 867);
    setResizable(true, true);
    setResizeLimits(1200, 800, 2400, 1600);
    getConstrainer()->setFixedAspectRatio(3.0 / 2.0);

    // The peer may not exist yet in the constructor. Apply once now for the
    // standalone case where it already does, and again when the hierarchy
    // attaches to a native peer.
    applyWindowIcon();
    applyStandaloneWindowTitle();
}

T5ynthEditor::~T5ynthEditor() = default;

void T5ynthEditor::paint(juce::Graphics&) {}

void T5ynthEditor::resized()
{
    mainPanel.setBounds(getLocalBounds());
}

void T5ynthEditor::parentHierarchyChanged()
{
    AudioProcessorEditor::parentHierarchyChanged();
    applyWindowIcon();
    applyStandaloneWindowTitle();
}

void T5ynthEditor::applyWindowIcon()
{
   #if JUCE_LINUX
    if (auto* peer = getPeer())
    {
        auto icon = juce::ImageCache::getFromMemory(BinaryData::t5ynth_icon_png,
                                                    BinaryData::t5ynth_icon_pngSize);
        if (icon.isValid())
            peer->setIcon(icon);
    }
   #endif
}

void T5ynthEditor::applyStandaloneWindowTitle()
{
    if (!juce::JUCEApplicationBase::isStandaloneApp())
        return;

    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
    {
        const juce::String title = juce::String(ProjectInfo::projectName)
                                 + " "
                                 + ProjectInfo::versionString;

        if (window->getName() != title)
            window->setName(title);
    }
}
