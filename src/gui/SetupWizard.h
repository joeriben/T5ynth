#pragma once
#include <JuceHeader.h>
#include <atomic>

/**
 * Model settings panel.
 *
 * Shows model status, auto-scans known paths, browse for model directory,
 * download from HuggingFace with token.
 * Embedded in the JUCE Audio/MIDI Settings dialog.
 */
class SettingsPage : public juce::Component,
                     private juce::Timer
{
public:
    SettingsPage();
    ~SettingsPage() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    juce::File scanForModel();
    void setModelPath(const juce::File& dir);
    juce::File getModelPath() const { return modelPath; }
    void setBackendConnected(bool connected);

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

    void downloadAllFilesInThread();
    void downloadGhReleaseInThread();
    void onDownloadFinished(bool success, const juce::String& error);
    static bool isLfsPointer(const juce::File& file);
    void cleanupBadFiles(const juce::File& dir);

    juce::String selectedModelId();
    juce::String selectedHfRepo();
    juce::String selectedGhRelease();
    bool selectedNeedsToken();

    juce::File modelPath;

    // UI elements
    juce::Label titleLabel;
    juce::Label modelStatusLabel;
    juce::Label modelPathLabel;
    juce::Label backendStatusLabel;
    juce::TextEditor instructionsLabel;
    juce::Label downloadStatusLabel;
    bool backendConnected = false;

    juce::Label tokenLabel;
    juce::TextEditor tokenEditor;

    double downloadProgress = 0.0;
    juce::ProgressBar progressBar { downloadProgress };

    juce::ComboBox modelChooser;
    juce::TextButton scanButton     { "Auto-Scan" };
    juce::TextButton browseButton   { "Browse..." };
    juce::TextButton downloadButton { "Download" };

    std::unique_ptr<juce::FileChooser> fileChooser;

    struct DownloadFile {
        juce::String remotePath;
        int64_t size = 0;
    };
    std::vector<DownloadFile> filesToDownload;
    int64_t totalBytes = 0;
    std::atomic<int64_t> downloadedBytes { 0 };
    std::atomic<bool> downloading { false };
    bool licenseAccepted_ = false;

    void loadSettings();
    void saveSettings();
    juce::File getSettingsFile() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsPage)
};
