#include "SetupWizard.h"
#include "GuiHelpers.h"
#include <nlohmann/json.hpp>

static const juce::String kModelFileName = "model_index.json";
static const juce::String kModelName     = "stabilityai/stable-audio-open-1.0";
static const juce::String kHfRepoId      = "stabilityai/stable-audio-open-1.0";

juce::File SettingsPage::getAppSupportModelDir()
{
    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
   #if JUCE_LINUX
    appData = appData.getChildFile("share");
   #endif
    return appData.getChildFile("T5ynth/models/stable-audio-open-1.0");
}

juce::File SettingsPage::getSettingsFile() const
{
    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
   #if JUCE_LINUX
    appData = appData.getChildFile("share");
   #endif
    return appData.getChildFile("T5ynth/settings.json");
}

SettingsPage::SettingsPage()
{
    titleLabel.setText("Stable Audio Model", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, kAccent);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    modelStatusLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(modelStatusLabel);

    modelPathLabel.setColour(juce::Label::textColourId, kDim);
    addAndMakeVisible(modelPathLabel);

    backendStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffef4444));
    backendStatusLabel.setText("Backend: Not connected", juce::dontSendNotification);
    addAndMakeVisible(backendStatusLabel);

    instructionsLabel.setColour(juce::Label::textColourId, juce::Colour(0xffcccccc));
    instructionsLabel.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(instructionsLabel);

    tokenLabel.setText("HF Token:", juce::dontSendNotification);
    tokenLabel.setColour(juce::Label::textColourId, kDim);
    addAndMakeVisible(tokenLabel);

    tokenEditor.setPasswordCharacter(0x2022);
    tokenEditor.setColour(juce::TextEditor::backgroundColourId, kSurface);
    tokenEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    tokenEditor.setColour(juce::TextEditor::outlineColourId, kBorder);
    tokenEditor.setTextToShowWhenEmpty("hf_...", kDimmer);
    addAndMakeVisible(tokenEditor);

    downloadStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffcccccc));
    addAndMakeVisible(downloadStatusLabel);

    progressBar.setColour(juce::ProgressBar::foregroundColourId, kAccent);
    progressBar.setColour(juce::ProgressBar::backgroundColourId, kSurface);
    progressBar.setVisible(false);
    addAndMakeVisible(progressBar);

    scanButton.setColour(juce::TextButton::buttonColourId, kSurface);
    scanButton.setColour(juce::TextButton::textColourOffId, kAccent);
    scanButton.onClick = [this] {
        auto found = scanForModel();
        if (found.exists()) setModelPath(found);
        updateStatus();
    };
    addAndMakeVisible(scanButton);

    browseButton.setColour(juce::TextButton::buttonColourId, kSurface);
    browseButton.setColour(juce::TextButton::textColourOffId, kAccent);
    browseButton.onClick = [this] { browseForModel(); };
    addAndMakeVisible(browseButton);

    downloadButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d6a4f));
    downloadButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    downloadButton.onClick = [this] { startDownload(); };
    addAndMakeVisible(downloadButton);

    loadSettings();

    auto found = scanForModel();
    if (found.exists()) modelPath = found;
    updateStatus();

    setSize(500, 480);
}

// ── Scan ────────────────────────────────────────────────────────────────────
juce::File SettingsPage::scanForModel()
{
    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    std::vector<juce::File> candidates = {
        getAppSupportModelDir(),
        home.getChildFile("t5ynth/models/stable-audio-open-1.0"),
        home.getChildFile(".cache/huggingface/hub/models--stabilityai--stable-audio-open-1.0"),
        home.getChildFile("ai/ai4artsed_development/dlbackend/ComfyUI/models/checkpoints/"
                          "OfficialStableDiffusion/stableaudio/stable-audio-open-1.0"),
    };
    for (auto& dir : candidates)
    {
        if (!dir.isDirectory()) continue;
        if (dir.getChildFile(kModelFileName).existsAsFile()) return dir;
        auto snapshotsDir = dir.getChildFile("snapshots");
        if (snapshotsDir.isDirectory())
            for (auto& snapshot : snapshotsDir.findChildFiles(juce::File::findDirectories, false))
                if (snapshot.getChildFile(kModelFileName).existsAsFile()) return snapshot;
    }
    return {};
}

void SettingsPage::setModelPath(const juce::File& dir)
{
    modelPath = dir;
    updateStatus();
    if (modelPath.exists() && onModelReady)
        onModelReady();
}

void SettingsPage::browseForModel()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select the Stable Audio model directory (contains model_index.json)",
        modelPath.exists() ? modelPath : juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "", true);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this](const juce::FileChooser& fc) {
            auto result = fc.getResult();
            if (result == juce::File()) return;
            if (!result.getChildFile(kModelFileName).existsAsFile()) {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                    "Wrong Directory", "This directory does not contain " + kModelFileName + ".\n\n"
                    "Select the folder that contains model_index.json.");
                return;
            }
            auto appDir = getAppSupportModelDir();
            if (result != appDir) {
                appDir.getParentDirectory().createDirectory();
                if (appDir.exists()) appDir.deleteRecursively();
                appDir.createSymbolicLink(result, false);
            }
            setModelPath(result);
        });
}

// ── Download ────────────────────────────────────────────────────────────────
void SettingsPage::startDownload()
{
    auto token = tokenEditor.getText().trim();
    if (token.isEmpty()) {
        downloadStatusLabel.setText("Enter your HuggingFace token first.", juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffef4444));
        return;
    }
    saveSettings();
    downloading = true;
    downloadButton.setEnabled(false);
    scanButton.setEnabled(false);
    browseButton.setEnabled(false);
    downloadStatusLabel.setText("Fetching file list...", juce::dontSendNotification);
    downloadStatusLabel.setColour(juce::Label::textColourId, kAccent);
    progressBar.setVisible(true);
    downloadProgress = 0.0;

    auto hfToken = token;
    std::thread([this, hfToken]() {
        juce::URL apiUrl("https://huggingface.co/api/models/" + kHfRepoId + "/tree/main?recursive=true");
        auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                        .withExtraHeaders("Authorization: Bearer " + hfToken)
                        .withConnectionTimeoutMs(15000);
        auto stream = apiUrl.createInputStream(opts);
        if (!stream) {
            juce::MessageManager::callAsync([this]() {
                onDownloadFinished(false, "Failed to connect. Check token and internet.");
            });
            return;
        }
        auto response = stream->readEntireStreamAsString();
        try {
            auto json = nlohmann::json::parse(response.toStdString());
            std::vector<DownloadFile> files;
            int64_t total = 0;
            for (auto& item : json) {
                if (item.value("type", "") != "file") continue;
                DownloadFile df;
                df.remotePath = juce::String(item["path"].get<std::string>());
                df.size = item.value("size", (int64_t)0);
                total += df.size;
                files.push_back(df);
            }
            juce::MessageManager::callAsync([this, files, total]() {
                filesToDownload = files;
                totalBytes = total;
                downloadedBytes = 0;
                currentFileIndex = 0;
                downloadStatusLabel.setText(juce::String(filesToDownload.size()) + " files, "
                    + juce::String(static_cast<double>(totalBytes) / (1024.0 * 1024.0), 0) + " MB",
                    juce::dontSendNotification);
                getAppSupportModelDir().createDirectory();
                downloadNextFile();
            });
        } catch (const std::exception& e) {
            auto err = juce::String(e.what());
            juce::MessageManager::callAsync([this, err]() {
                onDownloadFinished(false, "Parse error: " + err +
                    "\n\nAccept the license at huggingface.co/" + kHfRepoId + " first.");
            });
        }
    }).detach();
}

void SettingsPage::downloadNextFile()
{
    if (currentFileIndex >= filesToDownload.size()) { onDownloadFinished(true, {}); return; }
    auto& df = filesToDownload[currentFileIndex];
    auto token = tokenEditor.getText().trim();
    auto targetFile = getAppSupportModelDir().getChildFile(df.remotePath);
    targetFile.getParentDirectory().createDirectory();
    downloadStatusLabel.setText("Downloading: " + df.remotePath + " ("
        + juce::String(currentFileIndex + 1) + "/" + juce::String(filesToDownload.size()) + ")",
        juce::dontSendNotification);
    juce::URL fileUrl("https://huggingface.co/" + kHfRepoId + "/resolve/main/" + df.remotePath);
    currentDownloadTask = fileUrl.downloadToFile(targetFile,
        juce::URL::DownloadTaskOptions().withExtraHeaders("Authorization: Bearer " + token));
    if (!currentDownloadTask) { onDownloadFinished(false, "Failed: " + df.remotePath); return; }
    startTimer(200);
}

void SettingsPage::timerCallback()
{
    if (!currentDownloadTask) return;
    if (currentDownloadTask->isFinished()) {
        stopTimer();
        if (currentDownloadTask->hadError()) {
            onDownloadFinished(false, "Failed: " + filesToDownload[currentFileIndex].remotePath);
            return;
        }
        downloadedBytes += filesToDownload[currentFileIndex].size;
        currentFileIndex++;
        if (totalBytes > 0)
            downloadProgress = static_cast<double>(downloadedBytes) / static_cast<double>(totalBytes);
        currentDownloadTask.reset();
        downloadNextFile();
    } else {
        auto partial = currentDownloadTask->getLengthDownloaded();
        if (totalBytes > 0)
            downloadProgress = static_cast<double>(downloadedBytes + partial) / static_cast<double>(totalBytes);
    }
}

void SettingsPage::onDownloadFinished(bool success, const juce::String& error)
{
    stopTimer();
    downloading = false;
    currentDownloadTask.reset();
    downloadButton.setEnabled(true);
    scanButton.setEnabled(true);
    browseButton.setEnabled(true);
    if (success) {
        downloadProgress = 1.0;
        progressBar.setVisible(false);
        downloadStatusLabel.setText("", juce::dontSendNotification);
        tokenLabel.setVisible(false);
        tokenEditor.setVisible(false);
        downloadButton.setVisible(false);
        scanButton.setVisible(false);
        browseButton.setVisible(false);
        auto found = scanForModel();
        if (found.exists()) setModelPath(found);
        updateStatus();
    } else {
        downloadStatusLabel.setText(error, juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffef4444));
        progressBar.setVisible(false);
    }
}

// ── Persistence ─────────────────────────────────────────────────────────────
void SettingsPage::loadSettings()
{
    auto file = getSettingsFile();
    if (!file.existsAsFile()) return;
    try {
        auto json = nlohmann::json::parse(file.loadFileAsString().toStdString());
        if (json.contains("hf_token"))
            tokenEditor.setText(juce::String(json["hf_token"].get<std::string>()), false);
    } catch (...) {}
}

void SettingsPage::saveSettings()
{
    auto file = getSettingsFile();
    file.getParentDirectory().createDirectory();
    nlohmann::json json;
    json["hf_token"] = tokenEditor.getText().toStdString();
    file.replaceWithText(juce::String(json.dump(2)));
}

void SettingsPage::setBackendConnected(bool connected)
{
    backendConnected = connected;
    backendStatusLabel.setText(connected ? "Backend: Connected" : "Backend: Not connected",
                              juce::dontSendNotification);
    backendStatusLabel.setColour(juce::Label::textColourId,
        connected ? juce::Colour(0xff4ade80) : juce::Colour(0xffef4444));
}

void SettingsPage::updateStatus()
{
    if (modelPath.exists() && modelPath.getChildFile(kModelFileName).existsAsFile()) {
        modelStatusLabel.setText("Model: Found", juce::dontSendNotification);
        modelStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff4ade80));
        modelPathLabel.setText(modelPath.getFullPathName(), juce::dontSendNotification);
        instructionsLabel.setText(
            backendConnected ? "Ready to generate audio."
                             : "Model ready. Click 'Generate' to start the backend.",
            juce::dontSendNotification);
    } else {
        modelStatusLabel.setText("Model: Not found", juce::dontSendNotification);
        modelStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffef4444));
        modelPathLabel.setText("", juce::dontSendNotification);
        instructionsLabel.setText(
            "Required: " + kModelName + " (HuggingFace diffusers format)\n\n"
            "DOWNLOAD:\n"
            "  1. Go to huggingface.co/" + kHfRepoId + " and accept the license\n"
            "  2. Go to huggingface.co/settings/tokens, click 'Create new token'\n"
            "     Name: T5ynth, Type: Read (read-only is sufficient)\n"
            "  3. Paste the token above and click 'Download'\n"
            "  Downloads to: " + getAppSupportModelDir().getFullPathName() + "\n\n"
            "MANUAL:\n"
            "  huggingface-cli download " + kHfRepoId + " \\\n"
            "    --local-dir \"" + getAppSupportModelDir().getFullPathName() + "\"\n"
            "  Then click 'Auto-Scan'.\n\n"
            "BROWSE:\n"
            "  Select the folder containing model_index.json.",
            juce::dontSendNotification);
    }
}

void SettingsPage::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void SettingsPage::resized()
{
    auto area = getLocalBounds().reduced(8, 4);
    int rowH = 24;
    int gap = 4;

    titleLabel.setFont(juce::FontOptions(15.0f).withStyle("Bold"));
    titleLabel.setBounds(area.removeFromTop(rowH));
    area.removeFromTop(gap);

    modelStatusLabel.setFont(juce::FontOptions(13.0f));
    modelStatusLabel.setBounds(area.removeFromTop(rowH));

    modelPathLabel.setFont(juce::FontOptions(11.0f));
    modelPathLabel.setBounds(area.removeFromTop(18));
    area.removeFromTop(gap);

    backendStatusLabel.setFont(juce::FontOptions(13.0f));
    backendStatusLabel.setBounds(area.removeFromTop(rowH));
    area.removeFromTop(gap);

    // Buttons: Scan | Browse
    auto btnRow = area.removeFromTop(26);
    int btnW = 80;
    scanButton.setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft(6);
    browseButton.setBounds(btnRow.removeFromLeft(btnW));
    area.removeFromTop(gap * 2);

    // Token: label | editor | download
    auto tokenRow = area.removeFromTop(26);
    tokenLabel.setFont(juce::FontOptions(13.0f));
    tokenLabel.setBounds(tokenRow.removeFromLeft(70));
    tokenRow.removeFromLeft(4);
    downloadButton.setBounds(tokenRow.removeFromRight(80));
    tokenRow.removeFromRight(4);
    tokenEditor.setBounds(tokenRow);
    area.removeFromTop(gap);

    // Download progress
    auto progressRow = area.removeFromTop(20);
    if (progressBar.isVisible()) {
        progressBar.setBounds(progressRow.removeFromRight(progressRow.getWidth() / 3));
        progressRow.removeFromRight(4);
    }
    downloadStatusLabel.setFont(juce::FontOptions(11.0f));
    downloadStatusLabel.setBounds(progressRow);
    area.removeFromTop(gap);

    // Instructions
    instructionsLabel.setFont(juce::FontOptions(13.0f));
    instructionsLabel.setBounds(area);
}
