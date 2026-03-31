#pragma once
#include <JuceHeader.h>
#include "PromptPanel.h"
#include "AxesPanel.h"
#include "DimensionExplorer.h"
#include "SynthPanel.h"
#include "FxPanel.h"
#include "SequencerPanel.h"
#include "StatusBar.h"
#include "SetupWizard.h"
#include "PresetPanel.h"

class T5ynthProcessor;

class MainPanel : public juce::Component
{
public:
    explicit MainPanel(T5ynthProcessor& processor);
    ~MainPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void toggleSettings();
    SettingsPage& getModelPanel() { return settingsPage; }

private:
    T5ynthProcessor& processorRef;

    // Col 1: GENERATION
    PromptPanel promptPanel;
    AxesPanel axesPanel;

    // Col 2: ENGINE + FILTER + MODULATION
    SynthPanel synthPanel;

    // FX
    FxPanel fxPanel;

    // Presets
    PresetPanel presetPanel;

    // Bottom
    SequencerPanel sequencerPanel;
    StatusBar statusBar;

    // Master volume
    juce::Slider masterVolKnob;
    juce::Label masterVolLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterVolA;

    // Overlay: Dimension Explorer
    DimensionExplorer dimensionExplorer;
    juce::TextButton dimExplorerClose { "Close" };
    juce::TextButton dimExplorerReset { "Reset" };
    bool dimExplorerVisible = false;

    void showDimExplorer();
    void hideDimExplorer();
    void tryLoadInferenceModels();

    // Model settings (embedded in JUCE Audio/MIDI Settings dialog)
    SettingsPage settingsPage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainPanel)
};
