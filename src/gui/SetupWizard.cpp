#include "SetupWizard.h"
#include "GuiHelpers.h"
#include <nlohmann/json.hpp>
#include <thread>

// A valid model directory contains one of these marker files
static const juce::String kModelMarkers[] = { "model_index.json", "model_config.json" };

static bool hasModelMarker(const juce::File& dir)
{
    for (auto& marker : kModelMarkers)
        if (dir.getChildFile(marker).existsAsFile()) return true;
    return false;
}

// Known downloadable models — extend this list to add new engines
// ghRelease: if non-empty, download from GitHub Releases (no token needed)
// licenseNotice: shown in confirmation dialog before download
struct KnownModel {
    const char* id;
    const char* displayName;
    const char* hfRepo;       // HuggingFace repo (for models that need HF download)
    const char* ghRelease;    // GitHub Release tag URL base (nullptr = use HF)
    const char* licenseUrl;   // URL to full license text
    const char* licenseNotice;// Shown in confirmation dialog before download
    bool        needsToken;   // true = gated model, HF token required
};
static const KnownModel kKnownModels[] = {
    { "stable-audio-open-1.0",   "Stable Audio Open 1.0",     "stabilityai/stable-audio-open-1.0", nullptr,
      "https://stability.ai/community-license-agreement",
      "This model is licensed under the Stability AI Community License.\n\n"
      "- Non-commercial use: free\n"
      "- Commercial use under $1M annual revenue: free (register at stability.ai)\n"
      "- Commercial use over $1M: enterprise license required\n\n"
      "T5ynth does not provide the model weights. By downloading, you accept\n"
      "the license terms and take responsibility for compliance.", true },
    { "stable-audio-open-small", "Stable Audio Open Small",    "stabilityai/stable-audio-open-small",
      "https://github.com/joeriben/t5ynth/releases/download/models-v1",
      "https://stability.ai/community-license-agreement",
      "This model is licensed under the Stability AI Community License.\n\n"
      "- Non-commercial use: free\n"
      "- Commercial use under $1M annual revenue: free (register at stability.ai)\n"
      "- Commercial use over $1M: enterprise license required\n\n"
      "T5ynth does not provide the model weights. By downloading, you accept\n"
      "the license terms and take responsibility for compliance.", false },
    { "audioldm2",               "AudioLDM2",                  "cvssp/audioldm2", nullptr,
      "https://creativecommons.org/licenses/by-nc-sa/4.0/",
      "This model is licensed under CC BY-NC-SA 4.0.\n\n"
      "- Non-commercial use only (no revenue threshold, no exceptions)\n"
      "- Commercial use is NOT permitted under this license\n\n"
      "T5ynth does not provide the model weights. By downloading, you accept\n"
      "the license terms and take responsibility for compliance.", false },
};
static constexpr int kNumKnownModels = sizeof(kKnownModels) / sizeof(kKnownModels[0]);

juce::File SettingsPage::getAppSupportModelDir()
{
    return getAppSupportModelDir("stable-audio-open-1.0");
}

juce::File SettingsPage::getAppSupportModelDir(const juce::String& modelId)
{
    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
   #if JUCE_LINUX
    appData = appData.getChildFile("share");
   #endif
    return appData.getChildFile("T5ynth/models/" + modelId);
}

juce::String SettingsPage::selectedModelId()
{
    int idx = modelChooser.getSelectedItemIndex();
    if (idx >= 0 && idx < kNumKnownModels)
        return kKnownModels[idx].id;
    return kKnownModels[0].id;
}

juce::String SettingsPage::selectedHfRepo()
{
    int idx = modelChooser.getSelectedItemIndex();
    if (idx >= 0 && idx < kNumKnownModels)
        return kKnownModels[idx].hfRepo;
    return kKnownModels[0].hfRepo;
}

juce::String SettingsPage::selectedGhRelease()
{
    int idx = modelChooser.getSelectedItemIndex();
    if (idx >= 0 && idx < kNumKnownModels && kKnownModels[idx].ghRelease != nullptr)
        return kKnownModels[idx].ghRelease;
    return {};
}

bool SettingsPage::selectedNeedsToken()
{
    int idx = modelChooser.getSelectedItemIndex();
    if (idx >= 0 && idx < kNumKnownModels)
        return kKnownModels[idx].needsToken;
    return true; // default: require token for unknown models
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
    titleLabel.setText("Model Manager", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, kAccent);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    // Model chooser — select which model to manage/download
    modelChooser.setColour(juce::ComboBox::backgroundColourId, kSurface);
    modelChooser.setColour(juce::ComboBox::textColourId, kAccent);
    modelChooser.setColour(juce::ComboBox::outlineColourId, kBorder);
    for (int i = 0; i < kNumKnownModels; ++i)
        modelChooser.addItem(kKnownModels[i].displayName, i + 1);
    modelChooser.setSelectedId(1, juce::dontSendNotification);
    modelChooser.onChange = [this] { updateStatus(); };
    addAndMakeVisible(modelChooser);

    modelStatusLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(modelStatusLabel);

    modelPathLabel.setColour(juce::Label::textColourId, kDim);
    addAndMakeVisible(modelPathLabel);

    backendStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffef4444));
    backendStatusLabel.setText("Backend: Not connected", juce::dontSendNotification);
    addAndMakeVisible(backendStatusLabel);

    instructionsLabel.setMultiLine(true);
    instructionsLabel.setReadOnly(true);
    instructionsLabel.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    instructionsLabel.setColour(juce::TextEditor::textColourId, juce::Colour(0xffcccccc));
    instructionsLabel.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    instructionsLabel.setScrollbarsShown(true);
    addAndMakeVisible(instructionsLabel);

    downloadStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffcccccc));
    addAndMakeVisible(downloadStatusLabel);

    progressBar.setColour(juce::ProgressBar::foregroundColourId, kAccent);
    progressBar.setColour(juce::ProgressBar::backgroundColourId, kSurface);
    addChildComponent(progressBar);  // hidden until download starts

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
    auto id = selectedModelId();
    auto hfRepo = selectedHfRepo();
    // HF cache uses "--" as separator: "models--stabilityai--stable-audio-open-1.0"
    auto hfCacheDir = "models--" + hfRepo.replace("/", "--");

    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    std::vector<juce::File> candidates = {
        getAppSupportModelDir(id),
        home.getChildFile("t5ynth/models/" + id),
        home.getChildFile(".cache/huggingface/hub/" + hfCacheDir),
    };
    for (auto& dir : candidates)
    {
        if (!dir.isDirectory()) continue;
        if (hasModelMarker(dir)) return dir;
        auto snapshotsDir = dir.getChildFile("snapshots");
        if (snapshotsDir.isDirectory())
            for (auto& snapshot : snapshotsDir.findChildFiles(juce::File::findDirectories, false))
                if (hasModelMarker(snapshot)) return snapshot;
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
        "Select model directory",
        modelPath.exists() ? modelPath : juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "", true);
    auto modelId = selectedModelId();
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this, modelId](const juce::FileChooser& fc) {
            auto result = fc.getResult();
            if (result == juce::File()) return;
            if (!hasModelMarker(result)) {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                    "Wrong Directory", "This directory does not contain a valid model.\n\n"
                    "Select a folder that contains model_index.json or model_config.json.");
                return;
            }
            auto appDir = getAppSupportModelDir(modelId);
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
    // Show license confirmation dialog before any download
    int idx = modelChooser.getSelectedItemIndex();
    if (idx >= 0 && idx < kNumKnownModels && kKnownModels[idx].licenseNotice != nullptr
        && !licenseAccepted_)
    {
        auto& km = kKnownModels[idx];
        auto licenseUrl = juce::String(km.licenseUrl);
        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::InfoIcon,
            juce::String(km.displayName) + " \xe2\x80\x94 License",
            juce::String(km.licenseNotice) + "\n\nFull license:\n" + licenseUrl,
            "Accept & Download", "Cancel", this,
            juce::ModalCallbackFunction::create([this](int result) {
                if (result == 1) {
                    licenseAccepted_ = true;
                    startDownload();  // re-enter, this time skips dialog
                }
            }));
        return;  // async — startDownload will be called again from callback
    }
    licenseAccepted_ = false;  // reset for next time

    auto ghRelease = selectedGhRelease();

    // Token only required for gated HF models
    if (ghRelease.isEmpty() && selectedNeedsToken()) {
        auto token = tokenEditor.getText().trim();
        if (token.isEmpty()) {
            downloadStatusLabel.setText("Enter your HuggingFace token first.", juce::dontSendNotification);
            downloadStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffef4444));
            return;
        }
        saveSettings();
    }
    downloading = true;
    downloadButton.setEnabled(false);
    scanButton.setEnabled(false);
    browseButton.setEnabled(false);
    downloadStatusLabel.setColour(juce::Label::textColourId, kAccent);
    progressBar.setVisible(true);
    downloadProgress = 0.0;

    auto modelId = selectedModelId();

    // GitHub Releases: fixed file list, no API call needed
    if (ghRelease.isNotEmpty()) {
        downloadStatusLabel.setText("Downloading from GitHub...", juce::dontSendNotification);
        auto targetDir = getAppSupportModelDir(modelId);
        targetDir.createDirectory();
        downloadGhReleaseInThread();
        return;
    }

    downloadStatusLabel.setText("Fetching file list...", juce::dontSendNotification);
    auto hfToken = tokenEditor.getText().trim();
    auto hfRepo = selectedHfRepo();
    std::thread([this, hfToken, hfRepo, modelId]() {
        juce::URL apiUrl("https://huggingface.co/api/models/" + hfRepo + "/tree/main?recursive=true");
        auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                        .withExtraHeaders(hfToken.isNotEmpty() ? "Authorization: Bearer " + hfToken : juce::String())
                        .withConnectionTimeoutMs(15000);
        auto stream = apiUrl.createInputStream(opts);
        if (!stream) {
            juce::MessageManager::callAsync([this]() {
                onDownloadFinished(false,
                    "Failed to fetch file list from HuggingFace.\n\n"
                    "Possible causes:\n"
                    "- Invalid or expired HuggingFace token\n"
                    "- No internet connection\n"
                    "- HuggingFace API temporarily unavailable");
            });
            return;
        }
        auto response = stream->readEntireStreamAsString();
        try {
            auto json = nlohmann::json::parse(response.toStdString());

            // HF API returns {"error":"..."} on auth/license failures
            if (json.is_object() && json.contains("error")) {
                auto errMsg = juce::String(json["error"].get<std::string>());
                juce::MessageManager::callAsync([this, errMsg, hfRepo]() {
                    onDownloadFinished(false,
                        "HuggingFace API error:\n" + errMsg + "\n\n"
                        "Accept the license at huggingface.co/" + hfRepo);
                });
                return;
            }

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
            juce::MessageManager::callAsync([this, files, total, modelId]() {
                filesToDownload = files;
                totalBytes = total;
                downloadedBytes = 0;
                downloadStatusLabel.setText(juce::String(filesToDownload.size()) + " files, "
                    + juce::String(static_cast<double>(totalBytes) / (1024.0 * 1024.0), 0) + " MB",
                    juce::dontSendNotification);
                auto targetDir = getAppSupportModelDir(modelId);
                targetDir.createDirectory();
                cleanupBadFiles(targetDir);
                downloadAllFilesInThread();
            });
        } catch (const std::exception& e) {
            auto err = juce::String(e.what());
            juce::MessageManager::callAsync([this, err, hfRepo]() {
                onDownloadFinished(false, "Parse error: " + err +
                    "\n\nAccept the license at huggingface.co/" + hfRepo + " first.");
            });
        }
    }).detach();
}

void SettingsPage::downloadGhReleaseInThread()
{
    auto ghBase = selectedGhRelease();
    auto modelId = selectedModelId();
    auto targetDir = getAppSupportModelDir(modelId);

    // Files needed for native-format model inference
    struct GhFile { const char* name; int64_t expectedSize; };
    static const GhFile kFiles[] = {
        { "model.safetensors", 1677000000 },
        { "model_config.json", 6000 },
        { "LICENSE",           12000 },
    };
    static constexpr int kNumFiles = sizeof(kFiles) / sizeof(kFiles[0]);

    int64_t total = 0;
    for (auto& f : kFiles) total += f.expectedSize;
    totalBytes = total;
    downloadedBytes = 0;
    startTimer(250);

    std::thread([this, ghBase, targetDir]()
    {
        int64_t bytesCompleted = 0;

        for (int i = 0; i < kNumFiles; ++i)
        {
            auto& gf = kFiles[i];
            auto fileName = juce::String(gf.name);
            auto targetFile = targetDir.getChildFile(fileName);

            // Skip if already exists with reasonable size
            if (targetFile.existsAsFile() && targetFile.getSize() > gf.expectedSize * 9 / 10)
            {
                bytesCompleted += gf.expectedSize;
                downloadedBytes.store(bytesCompleted);
                continue;
            }

            auto fileNum = i + 1;
            juce::MessageManager::callAsync([this, fileName, fileNum]() {
                downloadStatusLabel.setText("Downloading: " + fileName + " ("
                    + juce::String(fileNum) + "/" + juce::String(kNumFiles) + ")",
                    juce::dontSendNotification);
            });

            juce::URL fileUrl(ghBase + "/" + fileName);
            auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                            .withConnectionTimeoutMs(30000);
            auto stream = fileUrl.createInputStream(opts);

            if (!stream)
            {
                juce::MessageManager::callAsync([this, fileName]() {
                    onDownloadFinished(false, "Connection failed for: " + fileName
                        + "\n\nCheck your internet connection.");
                });
                return;
            }

            targetFile.deleteFile();
            auto outStream = targetFile.createOutputStream();
            if (!outStream)
            {
                juce::MessageManager::callAsync([this, fileName]() {
                    onDownloadFinished(false, "Cannot write: " + fileName);
                });
                return;
            }

            char buffer[65536];
            int64_t written = 0;
            while (true)
            {
                auto bytesRead = stream->read(buffer, sizeof(buffer));
                if (bytesRead <= 0) break;
                outStream->write(buffer, static_cast<size_t>(bytesRead));
                written += bytesRead;
                downloadedBytes.store(bytesCompleted + written);
            }
            outStream.reset();

            bytesCompleted += juce::jmax(written, gf.expectedSize);
            downloadedBytes.store(bytesCompleted);
        }

        juce::MessageManager::callAsync([this]() {
            onDownloadFinished(true, {});
        });
    }).detach();
}

bool SettingsPage::isLfsPointer(const juce::File& file)
{
    if (!file.existsAsFile() || file.getSize() > 1024)
        return false;
    return file.loadFileAsString().startsWith("version https://git-lfs.github.com");
}

void SettingsPage::cleanupBadFiles(const juce::File& dir)
{
    if (!dir.isDirectory()) return;
    for (auto& file : dir.findChildFiles(juce::File::findFiles, true))
    {
        if (isLfsPointer(file))
        {
            juce::Logger::writeToLog("Removing LFS pointer: " + file.getFullPathName());
            file.deleteFile();
        }
    }
}

void SettingsPage::downloadAllFilesInThread()
{
    auto token = tokenEditor.getText().trim();
    auto modelId = selectedModelId();
    auto hfRepo = selectedHfRepo();
    auto targetDir = getAppSupportModelDir(modelId);
    auto files = filesToDownload;  // copy for thread
    auto total = totalBytes;

    startTimer(250);  // timer updates progress bar from atomic downloadedBytes

    std::thread([this, token, hfRepo, targetDir, files]()
    {
        int64_t bytesCompleted = 0;

        for (size_t i = 0; i < files.size(); ++i)
        {
            auto& df = files[i];
            auto targetFile = targetDir.getChildFile(df.remotePath);
            targetFile.getParentDirectory().createDirectory();

            // Skip already downloaded files (correct size)
            if (targetFile.existsAsFile() && !isLfsPointer(targetFile)
                && (df.size == 0 || targetFile.getSize() >= df.size * 9 / 10))
            {
                bytesCompleted += df.size;
                downloadedBytes.store(bytesCompleted);
                continue;
            }

            // Update status on UI thread
            auto fileName = df.remotePath;
            auto fileNum = i + 1;
            auto fileCount = files.size();
            juce::MessageManager::callAsync([this, fileName, fileNum, fileCount]() {
                downloadStatusLabel.setText("Downloading: " + fileName + " ("
                    + juce::String(fileNum) + "/" + juce::String(fileCount) + ")",
                    juce::dontSendNotification);
            });

            // Download via createInputStream — follows HF's LFS redirects
            juce::URL fileUrl("https://huggingface.co/" + hfRepo + "/resolve/main/" + fileName);
            auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                            .withExtraHeaders(token.isNotEmpty() ? "Authorization: Bearer " + token : juce::String())
                            .withConnectionTimeoutMs(30000);
            auto stream = fileUrl.createInputStream(opts);

            if (!stream)
            {
                juce::MessageManager::callAsync([this, fileName]() {
                    onDownloadFinished(false,
                        "Connection failed for: " + fileName + "\n\n"
                        "Possible causes:\n"
                        "- Invalid or expired HuggingFace token\n"
                        "- License not accepted at the model page\n"
                        "- No internet connection");
                });
                return;
            }

            // Write to file in chunks
            targetFile.deleteFile();
            auto outStream = targetFile.createOutputStream();
            if (!outStream)
            {
                juce::MessageManager::callAsync([this, fileName]() {
                    onDownloadFinished(false, "Cannot write: " + fileName);
                });
                return;
            }

            char buffer[65536];
            int64_t written = 0;
            while (!juce::Thread::currentThreadShouldExit())
            {
                auto bytesRead = stream->read(buffer, sizeof(buffer));
                if (bytesRead <= 0) break;
                outStream->write(buffer, static_cast<size_t>(bytesRead));
                written += bytesRead;
                downloadedBytes.store(bytesCompleted + written);
            }
            outStream.reset();

            // Check for server error responses (HTML/JSON error instead of data)
            if (written > 0 && written < 100000 && df.size > 100000)
            {
                auto content = targetFile.loadFileAsString().trim();
                juce::String serverMsg;

                // HF returns JSON {"error":"..."} for auth/license failures
                try {
                    auto errJson = nlohmann::json::parse(content.toStdString());
                    if (errJson.contains("error"))
                        serverMsg = juce::String(errJson["error"].get<std::string>());
                } catch (...) {}

                if (serverMsg.isEmpty() &&
                    (content.startsWithIgnoreCase("<!") || content.startsWithIgnoreCase("<html")))
                    serverMsg = "Server returned an HTML error page instead of the file.";

                if (serverMsg.isNotEmpty())
                {
                    targetFile.deleteFile();
                    juce::MessageManager::callAsync([this, fileName, serverMsg]() {
                        onDownloadFinished(false,
                            "Server rejected download of: " + fileName + "\n\n"
                            + serverMsg + "\n\n"
                            "Make sure you have accepted the license and your token is valid.");
                    });
                    return;
                }
            }

            // Validate: large files should not be tiny
            if (df.size > 100000 && written < df.size / 2)
            {
                targetFile.deleteFile();
                auto expected = df.size;
                auto expectedStr = juce::String(expected / (1024 * 1024)) + " MB";
                auto gotStr = (written < 1024 * 1024)
                    ? juce::String(written / 1024) + " KB"
                    : juce::String(written / (1024 * 1024)) + " MB";
                juce::MessageManager::callAsync([this, fileName, expectedStr, gotStr]() {
                    onDownloadFinished(false,
                        "Download incomplete: " + fileName + "\n"
                        "Expected " + expectedStr + ", got " + gotStr + "\n\n"
                        "Possible causes:\n"
                        "- License not accepted on HuggingFace\n"
                        "- Token expired or insufficient permissions\n"
                        "- Network connection interrupted");
                });
                return;
            }

            bytesCompleted += juce::jmax(written, df.size);
            downloadedBytes.store(bytesCompleted);
        }

        juce::MessageManager::callAsync([this]() {
            onDownloadFinished(true, {});
        });
    }).detach();
}

void SettingsPage::timerCallback()
{
    if (!downloading) { stopTimer(); return; }
    if (totalBytes > 0)
        downloadProgress = static_cast<double>(downloadedBytes.load()) / static_cast<double>(totalBytes);
}

void SettingsPage::onDownloadFinished(bool success, const juce::String& error)
{
    stopTimer();
    downloading = false;
    downloadButton.setEnabled(true);
    scanButton.setEnabled(true);
    browseButton.setEnabled(true);
    if (success) {
        downloadProgress = 1.0;
        progressBar.setVisible(false);

        // AudioLDM2 ships with GPT2Model in model_index.json but transformers >=4.45
        // removed GenerationMixin from PreTrainedModel — patch to GPT2LMHeadModel
        auto modelId = selectedModelId();
        auto modelIdx = getAppSupportModelDir(modelId).getChildFile("model_index.json");
        if (modelIdx.existsAsFile())
        {
            auto content = modelIdx.loadFileAsString();
            if (content.contains("\"GPT2Model\""))
            {
                content = content.replace("\"GPT2Model\"", "\"GPT2LMHeadModel\"");
                modelIdx.replaceWithText(content);
            }
        }

        downloadStatusLabel.setText("Download complete! Restart T5ynth to use the new model.", juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff4ade80));
        auto found = scanForModel();
        if (found.exists()) setModelPath(found);
        updateStatus();
    } else {
        downloadStatusLabel.setText("Download failed", juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffef4444));
        // Show full error in the multi-line instructions area
        instructionsLabel.setText(error, false);
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
    // Re-scan for the currently selected model
    auto found = scanForModel();
    if (found.exists())
        modelPath = found;
    else
        modelPath = juce::File();

    auto id = selectedModelId();
    auto hfRepo = selectedHfRepo();
    auto targetDir = getAppSupportModelDir(id);

    if (modelPath.exists() && hasModelMarker(modelPath)) {
        modelStatusLabel.setText(id + ": Installed", juce::dontSendNotification);
        modelStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff4ade80));
        modelPathLabel.setText(modelPath.getFullPathName(), juce::dontSendNotification);
        instructionsLabel.setText(
            backendConnected ? "Ready to generate audio."
                             : "Model ready. Restart to load.", false);
    } else {
        modelStatusLabel.setText(id + ": Not installed", juce::dontSendNotification);
        modelStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffef4444));
        modelPathLabel.setText("", juce::dontSendNotification);

        int idx = modelChooser.getSelectedItemIndex();
        bool hasGhRelease = idx >= 0 && idx < kNumKnownModels && kKnownModels[idx].ghRelease != nullptr;

        if (hasGhRelease) {
            instructionsLabel.setText(
                "Click 'Download' to get this model (no token needed).\n"
                "  Downloads to: " + targetDir.getFullPathName() + "\n\n"
                "Or use 'Browse...' to select an existing model directory.", false);
        } else if (selectedNeedsToken()) {
            instructionsLabel.setText(
                "DOWNLOAD:\n"
                "  1. Go to huggingface.co/" + hfRepo + " and accept the license\n"
                "  2. Go to huggingface.co/settings/tokens, click 'Create new token'\n"
                "     Name: T5ynth, Type: Read (read-only is sufficient)\n"
                "  3. Paste the token above and click 'Download'\n"
                "  Downloads to: " + targetDir.getFullPathName() + "\n\n"
                "MANUAL:\n"
                "  huggingface-cli download " + hfRepo + " \\\n"
                "    --local-dir \"" + targetDir.getFullPathName() + "\"\n"
                "  Then click 'Auto-Scan'.", false);
        } else {
            instructionsLabel.setText(
                "Click 'Download' to get this model (no token needed).\n"
                "  Downloads to: " + targetDir.getFullPathName() + "\n\n"
                "Or use 'Browse...' to select an existing model directory.\n\n"
                "MANUAL:\n"
                "  huggingface-cli download " + hfRepo + " \\\n"
                "    --local-dir \"" + targetDir.getFullPathName() + "\"\n"
                "  Then click 'Auto-Scan'.", false);
        }
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
    auto titleRow = area.removeFromTop(rowH);
    titleLabel.setBounds(titleRow.removeFromLeft(titleRow.getWidth() / 3));
    modelChooser.setBounds(titleRow);
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

    // Download button
    auto dlRow = area.removeFromTop(26);
    downloadButton.setBounds(dlRow.removeFromLeft(100));
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

    // Instructions (selectable/copyable text)
    instructionsLabel.setFont(juce::FontOptions(13.0f));
    instructionsLabel.setBounds(area);
    instructionsLabel.setCaretPosition(0);
}
