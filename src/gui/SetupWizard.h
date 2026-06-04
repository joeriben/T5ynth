#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <vector>

// One required file of a model, with its CURRENT published byte size from
// HuggingFace's tree API. A model's "manifest" is the vector of these — the
// authoritative packing list (root weights/config plus, for SA3, every engine
// file in the t5gemma text-encoder subfolder). relPath is the canonical path
// inside the installed model dir (may contain a "subfolder/" prefix).
struct ManifestEntry { juce::String relPath; juce::int64 size = 0; };

/**
 * Model settings panel.
 *
 * Shows model status, auto-scans known paths, browses for model directory,
 * downloads ungated models directly from HuggingFace (no token).
 * Embedded in the JUCE Audio/MIDI Settings dialog.
 */
class SettingsPage : public juce::Component,
                     private juce::Timer
{
public:
    SettingsPage();
    ~SettingsPage() override
    {
        stopTimer();
        if (downloadCancelFlag_)
            downloadCancelFlag_->store(true);
    }

    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::File scanForModel();
    bool hasAnyInstalledModel();
    void importDiscoveredModels();
    void setModelPath(const juce::File& dir);
    juce::File getModelPath() const { return modelPath; }
    void setBackendConnected(bool connected);
    void setBackendStarting();
    void setBackendFailed(const juce::String& reason);

    std::function<void()> onClose;

    /** Called when a model becomes available (after download or browse). */
    std::function<void()> onModelReady;

    static juce::File getAppSupportModelDir();
    static juce::File getAppSupportModelDir(const juce::String& modelId);

private:
    void browseForModel();
    void startDownload();
    void updateStatus();
    // Recompute every engine row's installed-light / status / action button from
    // the on-disk scan. Cheap and idempotent: each ModelRow caches its visual
    // state and only repaints on an actual change (idle-CPU safe).
    void refreshAllRows();
    // Refresh the optional translation-model section's button text + enabled
    // state from the on-disk install check. Called at the end of refreshAllRows().
    void refreshTranslationRow();
    // True iff the optional prompt-translation LLM is fully installed on disk
    // (config.json + tokenizer.json + model weights — mirrors the backend's
    // _is_local_transformers_model_dir gate).
    bool translationModelInstalled() const;
    void timerCallback() override;
    void setModelInstallBusy(bool busy, const juce::String& statusText = {});
    juce::Result importModelDirectoryForId(const juce::String& modelId,
                                           const juce::File& sourceDir,
                                           juce::File& activeDir,
                                           bool replaceExistingTarget);

    // Smart Auto-Scan entry point: checks known install paths, and for
    // native Stability models walks the user's Downloads folder looking for
    // the two files they were told to fetch manually from HuggingFace.
    void performAutoScan();

    // Tri-state result from the install helpers. The Auto-Scan dispatcher
    // needs to distinguish "tried something silently and nothing was there"
    // (let the next fallback try) from "we already surfaced a dialog to the
    // user, do NOT chain into the next fallback".
    //  - Installed:         success path completed; success dialog shown.
    //  - AbortedWithDialog: refused to install (e.g. duplicate of an
    //                       already-installed model); error/warning dialog
    //                       shown. Caller must NOT chain further fallbacks.
    //  - NotInstalled:      nothing actionable in this source; caller may
    //                       try the next fallback (e.g. folder picker).
    enum class InstallOutcome { Installed, AbortedWithDialog, NotInstalled };

    // Install a gated native Stability model from the given source folder by
    // matching every entry of its live HF manifest to a file in the folder
    // (canonical name + browser-rename tolerance; the two equally-named
    // model.safetensors are told apart by their manifest sizes). Reports
    // missing / wrong-size / success, reconstructs the t5gemma subfolder, then
    // copies into the target app-support dir on success. Used for both the
    // Downloads folder (primary) and any folder chosen via the picker fallback.
    // The manifest is fetched ONCE up front and threaded through both attempts.
    InstallOutcome installFromManifestFolder(const juce::File& sourceFolder,
                                             const juce::String& modelId,
                                             const juce::String& modelDisplayName,
                                             const std::vector<ManifestEntry>& manifest,
                                             bool reportIfMissing);

    void downloadAllFilesInThread();
    void downloadGhReleaseInThread();
    void downloadReassemblyInThread();
    // Stand-alone t5-base fetch for the manual SAO import paths (no reassembly
    // thread to piggyback on); the in-app SAO download chains t5-base inline.
    void startT5BaseChainDownload();
    void onDownloadFinished(bool success, const juce::String& error);
    static bool isLfsPointer(const juce::File& file);
    void cleanupBadFiles(const juce::File& dir);

    juce::String selectedModelId();
    juce::String selectedHfRepo();
    juce::String selectedGhRelease();
    bool selectedDownloadable();
    bool selectedIsGenerationEngine();
    juce::String selectedModelDisplay();
    // GhAsset list returned as opaque pointer to avoid header pollution;
    // implementation in SetupWizard.cpp pulls from the kKnownModels catalog.
    const void* selectedGhFiles();
    int         selectedGhFileCount();
    // ReassemblyAsset list (SA3 multi-source install), opaque for the same reason.
    const void* selectedAssets();
    int         selectedAssetCount();

    juce::File modelPath;

    // One row of the Model Manager: a generation engine's name + a one-line
    // "what encoder ships with it" sublabel, a status string, a single
    // contextual action button, and a right-edge "installed" light. Secondary
    // actions (open page / browse / reveal) live on a right-click menu so the
    // visible row stays Ableton-clean. The text encoders (t5-base, t5gemma)
    // never get a row of their own — they install with their engine.
    class ModelRow : public juce::Component
    {
    public:
        enum class Action { Download, GetFiles, Installed };

        ModelRow(juce::String id, juce::String displayName, juce::String sublabel);
        void resized() override;
        void paint(juce::Graphics& g) override;
        void mouseDown(const juce::MouseEvent& e) override;

        // Update visual state; repaints only when something actually changed.
        void setState(bool installed, const juce::String& statusText,
                      Action action, bool actionEnabled);

        const juce::String& modelId() const { return modelId_; }

        std::function<void(juce::String)> onAction;     // primary action button
        std::function<void(juce::String)> onOpenPage;   // right-click menu
        std::function<void(juce::String)> onBrowse;     // right-click menu
        std::function<void(juce::String)> onReveal;     // right-click menu (installed)

    private:
        juce::String modelId_, displayName_, sublabel_, statusText_;
        bool installed_ = false;
        Action action_ = Action::Download;
        juce::TextButton actionBtn_;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModelRow)
    };

    // UI elements
    juce::Label titleLabel;
    juce::Label backendStatusLabel;        // separate footer line (backend state)
    juce::TextEditor instructionsLabel;    // shared detail strip: focused-model
                                           // info, manual-install steps, errors
    juce::Label downloadStatusLabel;
    bool backendConnected = false;
    juce::String backendFailReason;

    double downloadProgress = 0.0;
    juce::ProgressBar progressBar { downloadProgress };

    // The five generation-engine rows (built in the constructor) plus the
    // family-header rects painted above each family's first row. Declared AFTER
    // the shared members so they (and their child buttons) destruct first.
    std::vector<std::unique_ptr<ModelRow>> rows_;
    struct FamilyHeader { juce::String text; juce::Rectangle<int> bounds; };
    std::vector<FamilyHeader> familyHeaders_;

    // Optional prompt-translation section — a separate area below the engine
    // rows (NOT a ModelRow). The model is an auxiliary asset, so it never
    // activates as a generation engine; this section only downloads it onto disk
    // where the backend auto-discovers it. Declared late (with the other leaf
    // widgets) so translationBtn destructs before the shared members it doesn't
    // depend on; its onClick only fires on user interaction, never at teardown.
    juce::Label translationSectionLabel;
    juce::Label translationDescLabel;
    juce::TextButton translationBtn { "Download" };

    std::unique_ptr<juce::FileChooser> fileChooser;

    struct DownloadFile {
        juce::String remotePath;
        int64_t size = 0;
    };
    std::vector<DownloadFile> filesToDownload;
    int64_t totalBytes = 0;
    std::atomic<int64_t> downloadedBytes { 0 };
    std::atomic<bool> downloading { false };
    std::atomic<bool> modelInstallBusy_ { false };
    bool licenseAccepted_ = false;
    // The model an in-flight action targets — the single source selectedXxx()
    // reads (decoupled from any UI widget). Set from a ModelRow's button/menu
    // callback the moment an action starts; defaults to the first row.
    juce::String activeOpModelId_;
    std::shared_ptr<std::atomic<int64_t>> downloadCounter_;
    std::shared_ptr<std::atomic<bool>> downloadCancelFlag_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPage)
};
