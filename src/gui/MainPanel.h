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
#include "PresetManagerPanel.h"
#include "SavePresetDialog.h"
#include "../presets/PresetFormat.h"

class T5ynthProcessor;

class MainPanel : public juce::Component, private juce::Timer
{
public:
    explicit MainPanel(T5ynthProcessor& processor);
    ~MainPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void toggleSettings();
    SettingsPage& getModelPanel() { return settingsPage; }

private:
    class GenerateButton : public juce::TextButton
    {
    public:
        explicit GenerateButton(const juce::String& label);

        void setAnimationState(float phase, bool isGenerating);
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

    private:
        float animationPhase = 0.0f;
        bool generating = false;
    };

    T5ynthProcessor& processorRef;

    // Col 1: GENERATION — three cards with headers
    juce::Label oscHeader, axesHeader, dimHeader, axesNote, poweredByLabel;
    PromptPanel promptPanel;
    AxesPanel axesPanel;
    GenerateButton mainGenerateBtn { "GENERATE" };
    float glowPhase = 0.0f;
    double glowLastTimeSec = 0.0;
    bool glowGenerating = false;
    void timerCallback() override;

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
    void tryLoadInferenceModels(bool forceRestart = false);
    void savePreset();        // quick save (overwrite current; fall back to dialog)
    void saveAsPreset();      // always opens the save dialog
    void loadPreset();        // open library browser
    void renameCurrentPreset();
    void deleteCurrentPreset();
    void showPresetNameContextMenu(juce::Point<int> screenPos);
    void importPresetFile();
    void exportWav();
    void loadDefaultPreset();
    void loadInitPreset();
    void ensureBundledPresetsExist();
    bool savePresetToFile(const juce::File& file);
    bool loadPresetFromFile(const juce::File& file);
    void applyLoadedPreset(const PresetFormat::LoadResult& result, const juce::File& sourceFile = {});
    void syncGuiStateForPresetSave();
    void showPresetManager();
    void hidePresetManager();
    enum class SaveDialogPrefill { sameName, copySuffix };
    void showSaveDialog(SaveDialogPrefill mode = SaveDialogPrefill::sameName);
    void hideSaveDialog();
    juce::String getCurrentPresetDisplayName() const;
    juce::StringArray suggestTagsForCurrent();
    /** Re-write a .t5p file's embedded JSON `name` field without touching
     *  any other field or the audio PCM. Used by the rename flow so the
     *  metadata stored inside the file stays consistent with the new
     *  filename. Returns true on success. */
    static bool patchPresetNameField(const juce::File& file, const juce::String& newName);

    /** Re-write a .t5p file's embedded JSON `tags` array in place — audio,
     *  prompts, axes etc. are untouched. This lets the user edit tags on
     *  ANY preset in the library without first loading it (a full re-save
     *  would otherwise overwrite the file with the engine's current state). */
    static bool patchPresetTagsField(const juce::File& file, const juce::StringArray& newTags);
    // Shared implementation used by loadDefaultPreset / loadInitPreset:
    // writes the embedded binary to a temp file and routes it through the
    // standard PresetFormat loader. Returns false on failure.
    bool loadBundledPreset(const char* data, int size, const juce::String& tempName);
    void showSettings();
    void hideSettings();
    void showManual();
    void hideManual();

    // Model settings overlay
    SettingsPage settingsPage;
    Scrim settingsScrim;
    bool settingsVisible = false;
    bool pendingInferenceReload = false;

    // Preset manager overlay
    Scrim presetScrim;
    PresetManagerPanel presetManager;
    bool presetManagerVisible = false;
    juce::File currentPresetFile;

    // Save-preset modal (independent of the library browser)
    Scrim saveDialogScrim;
    SavePresetDialog savePresetDialog;
    bool saveDialogVisible = false;

    // Manual overlay — native WebView renders the shipped HTML guide
    // (resources/T5ynth_Guide.html), bundled via juce_add_binary_data.
    Scrim manualScrim;
    juce::Component manualPanel;
    juce::WebBrowserComponent manualWeb { juce::WebBrowserComponent::Options{} };
    juce::TextButton manualCloseBtn { "Close" };
    bool manualVisible = false;
    bool manualLoaded = false;
    juce::File manualHtmlOnDisk;  // temp extraction of the bundled HTML

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainPanel)
};
