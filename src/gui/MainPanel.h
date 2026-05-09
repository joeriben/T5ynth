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
#include "../presets/PresetFormat.h"

class T5ynthProcessor;

class MainPanel : public juce::Component,
                  public juce::DragAndDropContainer,
                  private juce::Timer
{
public:
    explicit MainPanel(T5ynthProcessor& processor);
    ~MainPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
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
    static constexpr int kNumInfCacheButtons = 8;

    // Cache-capacity button — when selected and the cache is still filling,
    // the fill colour pulses subtly. Replaces the textual status row that
    // previously sat below the button strip.
    class CacheCapButton : public juce::TextButton
    {
    public:
        CacheCapButton() = default;
        void setPulsing(bool p);
        void setPulsePhase(float phase);
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

    private:
        bool pulsing = false;
        float lastPhase = 0.0f;
    };

    CacheCapButton infCacheButtons[kNumInfCacheButtons];
    int lastInfCacheUiCapacity = -1;
    int lastInfCacheUiFill = -1;
    bool lastInfCacheUiFull = false;
    float glowPhase = 0.0f;
    double glowLastTimeSec = 0.0;
    bool glowGenerating = false;
    // Independent phase for the cache-fill pulse — runs at a fixed rate
    // (~2 Hz) whenever the cache is filling, decoupled from glowPhase which
    // crawls at 0.05 Hz when idle and would make the cache-pulse invisible.
    float cachePulsePhase = 0.0f;
    // Cache-hit feedback: when the inference cache is full, the primary
    // generation actions become cache playback actions and stay labelled
    // "cache hit" until the cache is cleared/disabled.
    bool cacheHitActive = false;
    double cacheHitUntilSec = 0.0;
    void timerCallback() override;
    void syncInferenceCacheUi();
    void updateGenerateButtonsForCacheState(bool pulseCacheHit);

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
    void savePreset();
    void loadPreset();
    void renameCurrentPreset();
    void deleteCurrentPreset();
    void showPresetNameContextMenu(juce::Point<int> screenPos);
    void importPresetFile();
    void exportWav();
    void loadDefaultPreset();
    void loadInitPreset();
    void ensureBundledPresetsExist();
    bool savePresetToFile(const juce::File& file, bool includeInferenceCache = false);
    bool loadPresetFromFile(const juce::File& file);
    void applyLoadedPreset(const PresetFormat::LoadResult& result, const juce::File& sourceFile = {});
    void syncGuiStateForPresetSave();
    void showPresetManager();
    void hidePresetManager();
    enum class SaveNameMode { keepName, appendCopy };
    void enterLibrarySaveMode(SaveNameMode mode = SaveNameMode::keepName);
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

    // Preset manager overlay (also hosts the Save-Drawer in Save mode)
    Scrim presetScrim;
    PresetManagerPanel presetManager;
    bool presetManagerVisible = false;
    juce::File currentPresetFile;

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
