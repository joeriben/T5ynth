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
    setResizeLimits(1050, 700, 2400, 1600);
    getConstrainer()->setFixedAspectRatio(3.0 / 2.0);

    // The peer may not exist yet in the constructor. Apply once now for the
    // standalone case where it already does, and again when the hierarchy
    // attaches to a native peer.
    applyWindowIcon();
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
