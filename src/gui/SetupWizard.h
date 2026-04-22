#pragma once
#include <JuceHeader.h>
#include <atomic>

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
    juce::Result importModelDirectory(const juce::File& sourceDir,
                                      juce::File& activeDir,
                                      bool replaceExistingTarget);

    // Smart Auto-Scan entry point: checks known install paths, and for
    // SA Small walks the user's Downloads folder looking for the two
    // files they were told to fetch manually from HuggingFace.
    void performAutoScan();

    // Try to install SA Small from the given source folder: checks for
    // the required files, reports missing / wrong / success, then copies to
    // the target app-support dir on success. Used for both the Downloads
    // folder (primary path) and any folder chosen via the picker fallback.
    // Returns true if the install completed.
    bool trySaSmallInstallFromFolder(const juce::File& sourceFolder,
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
