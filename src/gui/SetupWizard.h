#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <optional>

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

    // git-based install path for repos whose user-facing pattern of "download
    // these specific files into Downloads" is broken by filename collisions
    // with other models (T5Gemma vs t5-base both ship config.json /
    // tokenizer.json / model.safetensors). We shell out to /usr/bin/git so
    // T5ynth itself never touches the user's HF token — git picks up the
    // existing credentials store (huggingface-cli login, credential helper,
    // SSH agent, whatever the user already has set up for their CLI work).
    struct GitPreflightResult {
        bool        ok = false;
        juce::String missingTool;  // "git" or "git-lfs"
        juce::String installHint;  // platform-specific install instructions
    };
    GitPreflightResult gitPreflight();
    void cloneHfRepoInThread();

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

    // Try to install a native Stability model from the given source folder: checks for
    // the required files, reports missing / wrong / success, then copies to
    // the target app-support dir on success. Used for both the Downloads
    // folder (primary path) and any folder chosen via the picker fallback.
    InstallOutcome tryNativeStabilityInstallFromFolder(const juce::File& sourceFolder,
                                                       const juce::String& modelId,
                                                       const juce::String& modelDisplayName,
                                                       bool reportIfMissing);

    // Variant for flat-transformers encoders (e.g. T5Gemma). Same install
    // mechanics as the Stability path — required-files check, scenario
    // reporting, copy-then-verify — but the file list is parameterised and
    // there is no "wrong files" heuristic (HF's Files tab for these repos
    // doesn't surface misleading alternatives the way Stability's does).
    // requiredFiles must point to a const char* array of length numFiles;
    // file names are matched verbatim in the source folder root.
    InstallOutcome tryFlatEncoderInstallFromFolder(const juce::File& sourceFolder,
                                                   const juce::String& modelId,
                                                   const juce::String& modelDisplayName,
                                                   const char* const* requiredFiles,
                                                   int numFiles,
                                                   bool reportIfMissing);

    void downloadAllFilesInThread();
    void downloadGhReleaseInThread();
    void onDownloadFinished(bool success, const juce::String& error);
    static bool isLfsPointer(const juce::File& file);
    void cleanupBadFiles(const juce::File& dir);

    juce::String selectedModelId();
    juce::String selectedHfRepo();
    juce::String selectedGhRelease();
    bool selectedDownloadable();
    bool selectedIsGenerationEngine();
    bool selectedGitCloneable();
    juce::String selectedModelDisplay();
    // GhAsset list returned as opaque pointer to avoid header pollution;
    // implementation in SetupWizard.cpp pulls from the kKnownModels catalog.
    const void* selectedGhFiles();
    int         selectedGhFileCount();

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
    // Visible only for gitCloneable models (currently T5Gemma): shells out
    // to `git clone` so the repo lands in a named subdir, sidestepping the
    // filename-collision problem the file-list Auto-Scan path hits when
    // multiple models share generic names like config.json / tokenizer.json.
    juce::TextButton gitCloneButton     { "Clone via git" };

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
    // Cache for gitPreflight: spawning two ChildProcesses on every model
    // selector change is wasteful, and the answer doesn't change during a
    // session. Cleared lazily on cache miss. std::optional gives us a
    // distinct "not yet checked" state without a separate bool flag.
    std::optional<GitPreflightResult> gitPreflightCached_;
    std::shared_ptr<std::atomic<int64_t>> downloadCounter_;
    std::shared_ptr<std::atomic<bool>> downloadCancelFlag_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPage)
};
