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

    // UI elements
    juce::Label titleLabel;
    juce::Label modelStatusLabel;
    juce::Label modelPathLabel;
    juce::Label backendStatusLabel;
    juce::TextEditor instructionsLabel;
    juce::Label downloadStatusLabel;
    bool backendConnected = false;
    juce::String backendFailReason;

    double downloadProgress = 0.0;
    juce::ProgressBar progressBar { downloadProgress };

    juce::ComboBox modelChooser;
    juce::TextButton scanButton         { "Auto-Scan" };
    juce::TextButton browseButton       { "Browse..." };
    juce::TextButton openPageButton     { "Open Model Page" };
    juce::TextButton downloadButton     { "Download from HuggingFace" };

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
    std::shared_ptr<std::atomic<int64_t>> downloadCounter_;
    std::shared_ptr<std::atomic<bool>> downloadCancelFlag_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPage)
};
