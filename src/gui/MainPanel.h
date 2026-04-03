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
#include "../presets/PresetFormat.h"

class T5ynthProcessor;

class MainPanel : public juce::Component
{
public:
    explicit MainPanel(T5ynthProcessor& processor);
    ~MainPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void toggleSettings();
    SettingsPage& getModelPanel() { return settingsPage; }

private:
    T5ynthProcessor& processorRef;

    // Col 1: GENERATION — three cards with headers
    juce::Label oscHeader, axesHeader, dimHeader, axesNote;
    PromptPanel promptPanel;
    AxesPanel axesPanel;
    juce::TextButton mainGenerateBtn { "Generate" };
    bool generateHighlight = false;

    // Col 2: ENGINE + FILTER + MODULATION
    SynthPanel synthPanel;

    // FX
    FxPanel fxPanel;

    // Bottom
    SequencerPanel sequencerPanel;
    StatusBar statusBar;

    // Master volume
    juce::Slider masterVolKnob;
    juce::Label masterVolLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterVolA;

    // Overlay: Dimension Explorer
    // Scrim catches clicks outside DimExplorer to close the overlay
    struct Scrim : public juce::Component {
        std::function<void()> onClick;
        void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }
        void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(0xdd101016)); }
    };
    Scrim dimScrim;
    DimensionExplorer dimensionExplorer;
    juce::TextButton dimApplyBtn    { "Apply + Generate" };
    juce::TextButton dimUndoBtn     { "Undo" };
    juce::TextButton dimRedoBtn     { "Redo" };
    juce::TextButton dimResetBtn    { "Reset All" };
    bool dimExplorerVisible = false;

    void showDimExplorer();
    void hideDimExplorer();
    void tryLoadInferenceModels();
    void tryLoadNativeInference();
    void savePreset();
    void loadPreset();
    void exportWav();
    void showSettings();
    void hideSettings();
    void showAbout();
    void hideAbout();

    // Model settings overlay
    SettingsPage settingsPage;
    Scrim settingsScrim;
    bool settingsVisible = false;

    // About overlay
    Scrim aboutScrim;
    juce::TextEditor aboutText;
    bool aboutVisible = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainPanel)
};
