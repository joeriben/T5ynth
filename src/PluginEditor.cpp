#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "BinaryData.h"
#include "gui/MidiOutputSettingsPanel.h"

#if JucePlugin_Build_Standalone
 // Gives access to juce::StandalonePluginHolder::getInstance() so we can inject our
 // MIDI panel into the standalone "MIDI/Audio Settings" dialog. The shared-code TU
 // is compiled with JucePlugin_Build_Standalone=1, so the holder is in the same lib;
 // the header has no app entry-point, so including it here can't create a second main.
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

T5ynthEditor::T5ynthEditor(T5ynthProcessor& processor)
    : AudioProcessorEditor(processor),
      processorRef(processor),
      mainPanel(processor)
{
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(mainPanel);

    // Model settings are now in our own overlay — no longer in JUCE's dialog.
    // MIDI output + external-clock controls DO live in JUCE's "MIDI/Audio Settings"
    // dialog (standalone): inject our panel via the holder's extraSettingsPanel hook.
   #if JucePlugin_Build_Standalone
    if (juce::JUCEApplicationBase::isStandaloneApp())
    {
        midiSettingsPanel_ = std::make_unique<MidiOutputSettingsPanel>(processorRef);
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            holder->extraSettingsPanel = midiSettingsPanel_.get();
    }
   #endif

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

T5ynthEditor::~T5ynthEditor()
{
    // Clear the borrowed pointer BEFORE midiSettingsPanel_ frees the panel, so a
    // settings dialog that outlives the editor (or is open at teardown) can't read
    // a dangling pointer. No-op in plugin builds (holder is null).
   #if JucePlugin_Build_Standalone
    // Only clear if the hook still points at OUR panel — guards against a
    // create-before-destroy editor swap clobbering a newer editor's hook.
    if (auto* holder = juce::StandalonePluginHolder::getInstance();
        holder != nullptr && holder->extraSettingsPanel == midiSettingsPanel_.get())
        holder->extraSettingsPanel = nullptr;
   #endif
}

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
