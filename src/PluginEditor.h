#pragma once
#include <JuceHeader.h>
#include "gui/MainPanel.h"
#include "gui/T5ynthLookAndFeel.h"

class T5ynthProcessor;
class MidiOutputSettingsPanel;

class T5ynthEditor : public juce::AudioProcessorEditor
{
public:
    explicit T5ynthEditor(T5ynthProcessor& processor);
    ~T5ynthEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void parentHierarchyChanged() override;

private:
    void applyWindowIcon();
    void applyStandaloneWindowTitle();

    T5ynthProcessor& processorRef;
    T5ynthLookAndFeel lookAndFeel;
    MainPanel mainPanel;
    // Standalone only: MIDI output + external-clock controls injected into JUCE's
    // "MIDI/Audio Settings" dialog via StandalonePluginHolder::extraSettingsPanel.
    // Owned here; the dialog only borrows it. nullptr in plugin builds. The hook is
    // cleared in the destructor BEFORE this unique_ptr frees the panel.
    std::unique_ptr<MidiOutputSettingsPanel> midiSettingsPanel_;
    // Displays the setTooltip(...) text of any hovered component (Re-Prompt glyphs,
    // coupling buttons, and the previously-dormant tooltips across the UI). 700 ms
    // hover delay. JUCE's standard mouse-position poll timer — a cheap idle cost
    // (a position compare, no repaint when nothing is hovered).
    juce::TooltipWindow tooltipWindow { this, 700 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(T5ynthEditor)
};
