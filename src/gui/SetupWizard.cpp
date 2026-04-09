#include "SetupWizard.h"
#include "GuiHelpers.h"
#include <nlohmann/json.hpp>
#include <thread>

// A valid model directory contains one of these metadata files…
static const juce::String kModelMarkers[] = { "model_index.json", "model_config.json" };

// …AND enough actual weight bytes to be a real model (not a stale metadata-only
// stub like the HF cache leaves behind when only config was fetched).
static constexpr int64_t kMinWeightBytes = 100ll * 1024 * 1024;  // 100 MB

static bool hasModelMarker(const juce::File& dir)
{
    bool hasMetadata = false;
    for (auto& marker : kModelMarkers)
        if (dir.getChildFile(marker).existsAsFile()) { hasMetadata = true; break; }
    if (!hasMetadata) return false;

    // Sum weight file sizes (recursive, covers both native and diffusers layouts).
    int64_t weightBytes = 0;
    for (auto& f : dir.findChildFiles(juce::File::findFiles, true, "*.safetensors"))
        weightBytes += f.getSize();
    if (weightBytes < kMinWeightBytes)
        for (auto& f : dir.findChildFiles(juce::File::findFiles, true, "*.bin"))
            weightBytes += f.getSize();
    if (weightBytes < kMinWeightBytes)
        for (auto& f : dir.findChildFiles(juce::File::findFiles, true, "*.ckpt"))
            weightBytes += f.getSize();

    return weightBytes >= kMinWeightBytes;
}

// Known models — extend this list to add new engines.
// downloadable: if false, show manual instructions only (no Download button).
//   Both Stable Audio models are gated on HuggingFace and T5ynth never prompts
//   for tokens, so users must fetch them via huggingface-cli once and point
//   Browse… at the resulting folder. AudioLDM2 is the only ungated model and
//   the only one T5ynth downloads directly.
struct KnownModel {
    const char* id;
    const char* displayName;
    const char* hfRepo;       // HuggingFace repo
    const char* ghRelease;    // GitHub Release tag URL base (nullptr = use HF)
    const char* licenseUrl;   // URL to full license text
    const char* licenseNotice;// Shown in confirmation dialog before download
    bool        downloadable; // false = manual-only (no in-app download)
};
static const KnownModel kKnownModels[] = {
    { "stable-audio-open-1.0",   "Stable Audio Open 1.0",     "stabilityai/stable-audio-open-1.0", nullptr,
      "https://stability.ai/community-license-agreement",
      "This model is licensed under the Stability AI Community License.\n\n"
      "- Non-commercial use: free\n"
      "- Commercial use under $1M annual revenue: free (register at stability.ai)\n"
      "- Commercial use over $1M: enterprise license required\n\n"
      "T5ynth does not provide the model weights. By downloading, you accept\n"
      "the license terms and take responsibility for compliance.", false },
    { "stable-audio-open-small", "Stable Audio Open Small", "stabilityai/stable-audio-open-small",
      nullptr,
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
      "the license terms and take responsibility for compliance.", true },
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

bool SettingsPage::selectedDownloadable()
{
    int idx = modelChooser.getSelectedItemIndex();
    if (idx >= 0 && idx < kNumKnownModels)
        return kKnownModels[idx].downloadable;
    return false;
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
    // Default to Stable Audio Open Small.
    // Index in kKnownModels is 1, so ComboBox id (1-based) is 2.
    modelChooser.setSelectedId(2, juce::dontSendNotification);
    modelChooser.onChange = [this] { updateStatus(); resized(); };
    addAndMakeVisible(modelChooser);

    modelStatusLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(modelStatusLabel);

    modelPathLabel.setColour(juce::Label::textColourId, kDim);
    addAndMakeVisible(modelPathLabel);

    backendStatusLabel.setColour(juce::Label::textColourId, kDim);
    backendStatusLabel.setText("Backend: Starting...", juce::dontSendNotification);
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
    scanButton.onClick = [this] { performAutoScan(); };
    addAndMakeVisible(scanButton);

    browseButton.setColour(juce::TextButton::buttonColourId, kSurface);
    browseButton.setColour(juce::TextButton::textColourOffId, kAccent);
    browseButton.onClick = [this] { browseForModel(); };
    addAndMakeVisible(browseButton);

    openPageButton.setColour(juce::TextButton::buttonColourId, kSurface);
    openPageButton.setColour(juce::TextButton::textColourOffId, kAccent);
    openPageButton.onClick = [this] {
        auto repo = selectedHfRepo();
        juce::URL("https://huggingface.co/" + repo).launchInDefaultBrowser();
    };
    addAndMakeVisible(openPageButton);

    downloadButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d6a4f));
    downloadButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    downloadButton.onClick = [this] { startDownload(); };
    addAndMakeVisible(downloadButton);

    auto found = scanForModel();
    if (found.exists()) modelPath = found;
    updateStatus();

    setSize(500, 480);
}

// ── Scan ────────────────────────────────────────────────────────────────────
static juce::File scanForModelById(const juce::String& id, const juce::String& hfRepo)
{
    // HF cache uses "--" as separator: "models--stabilityai--stable-audio-open-1.0"
    auto hfCacheDir = "models--" + hfRepo.replace("/", "--");

    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    std::vector<juce::File> candidates = {
        SettingsPage::getAppSupportModelDir(id),
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

juce::File SettingsPage::scanForModel()
{
    return scanForModelById(selectedModelId(), selectedHfRepo());
}

bool SettingsPage::hasAnyInstalledModel()
{
    for (int i = 0; i < kNumKnownModels; ++i)
        if (scanForModelById(kKnownModels[i].id, kKnownModels[i].hfRepo).exists())
            return true;
    return false;
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

// ── Smart Auto-Scan ─────────────────────────────────────────────────────────
// For SA Small the user follows a manual-download walkthrough (fetch 3 files
// from HuggingFace to the system Downloads folder). Auto-Scan then hides all
// path details: it looks there for the files and copies them to the correct
// app-support location, or guides the user if something is missing / wrong.
//
// For SA 1.0 and AudioLDM2, Auto-Scan falls back to the existing known-paths
// scan (scanForModel), because SA 1.0 is a huggingface-cli install (files go
// straight into the target dir) and AudioLDM2 is downloaded in-app.

// Files T5ynth cares about for an SA Small install.
static const char* kSaSmallRequired[] = {
    "model.safetensors",
    "model_config.json",
    "LICENSE",
};
static constexpr int kNumSaSmallRequired =
    sizeof(kSaSmallRequired) / sizeof(kSaSmallRequired[0]);

// Files the user may have fetched by mistake alongside the correct ones.
// When we see these in the source folder, we call them out by name so the
// user can delete them. None of them are needed by T5ynth.
static const char* kSaSmallWrongFiles[] = {
    "model.ckpt",
    "base_model.ckpt",
    "base_model.safetensors",
    "base_model_config.json",
};
static constexpr int kNumSaSmallWrongFiles =
    sizeof(kSaSmallWrongFiles) / sizeof(kSaSmallWrongFiles[0]);

static juce::File getDownloadsFolder()
{
    // macOS/Windows/Linux all use ~/Downloads as the browser default on a
    // fresh user account. If a user has customised their downloads dir via
    // XDG_DOWNLOAD_DIR or similar, the folder-picker fallback catches it.
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
               .getChildFile("Downloads");
}

bool SettingsPage::trySaSmallInstallFromFolder(const juce::File& sourceFolder,
                                               bool reportIfMissing)
{
    if (!sourceFolder.isDirectory())
    {
        if (reportIfMissing)
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Folder not found",
                "T5ynth could not read the folder:\n  " + sourceFolder.getFullPathName());
        return false;
    }

    // Look at the contents.
    juce::Array<juce::File> foundRequired;
    juce::StringArray missingNames;
    for (int i = 0; i < kNumSaSmallRequired; ++i)
    {
        auto candidate = sourceFolder.getChildFile(kSaSmallRequired[i]);
        if (candidate.existsAsFile())
            foundRequired.add(candidate);
        else
            missingNames.add(kSaSmallRequired[i]);
    }

    juce::StringArray wrongFound;
    for (int i = 0; i < kNumSaSmallWrongFiles; ++i)
    {
        auto candidate = sourceFolder.getChildFile(kSaSmallWrongFiles[i]);
        if (candidate.existsAsFile())
            wrongFound.add(kSaSmallWrongFiles[i]);
    }

    const juce::String wrongNote = wrongFound.isEmpty()
        ? juce::String()
        : juce::String("\n\nNote: these files in the same folder are NOT needed "
                       "by T5ynth and can be deleted to save space:\n  ")
              + wrongFound.joinIntoString("\n  ");

    // Scenario (d): all three required files present — copy and done.
    if (missingNames.isEmpty())
    {
        auto targetDir = getAppSupportModelDir("stable-audio-open-small");
        if (!targetDir.createDirectory())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Could not create model folder",
                "T5ynth could not create:\n  " + targetDir.getFullPathName()
                    + "\n\nCheck folder permissions and try again.");
            return false;
        }

        juce::StringArray copyErrors;
        for (auto& f : foundRequired)
        {
            auto dest = targetDir.getChildFile(f.getFileName());
            if (dest.existsAsFile()) dest.deleteFile();
            if (!f.copyFileTo(dest))
                copyErrors.add(f.getFileName());
        }

        if (!copyErrors.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Copy failed",
                "Found all required files, but copying failed for:\n  "
                    + copyErrors.joinIntoString(", ")
                    + "\n\nCheck disk space and folder permissions in:\n  "
                    + targetDir.getFullPathName());
            return false;
        }

        if (!hasModelMarker(targetDir))
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Files copied, but look incomplete",
                "Files were copied to:\n  " + targetDir.getFullPathName()
                    + "\n\nBut the model weights look too small. The download may "
                      "have been interrupted. Re-download model.safetensors from "
                      "HuggingFace (click 'Open Model Page' above) and try again.");
            return false;
        }

        setModelPath(targetDir);
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "Stable Audio Open Small \xe2\x80\x94 Installed",
            "T5ynth copied the model files from:\n  " + sourceFolder.getFullPathName()
                + "\n\nto:\n  " + targetDir.getFullPathName()
                + "\n\nThe originals are still in your Downloads folder \xe2\x80\x94 "
                  "you can delete them now if you want."
                + wrongNote);
        return true;
    }

    // Scenario (a): some but not all required files present.
    if (!foundRequired.isEmpty() && reportIfMissing)
    {
        juce::String foundList;
        for (auto& f : foundRequired)
            foundList += "  \xe2\x9c\x93 " + f.getFileName() + "\n";

        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "Download incomplete",
            "Found in:\n  " + sourceFolder.getFullPathName() + "\n\n"
                + foundList + "\nStill missing:\n  \xe2\x9c\x97 "
                + missingNames.joinIntoString("\n  \xe2\x9c\x97 ")
                + "\n\nPlease download the missing files from the model page "
                  "(click 'Open Model Page' above) and click 'Auto-Scan' again."
                + wrongNote);
        return false;
    }

    // Scenario (b)/(c): nothing useful in this folder.
    if (reportIfMissing)
    {
        if (!wrongFound.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Wrong files in folder",
                "This folder contains files that LOOK related to the model, "
                "but T5ynth does not need them:\n  "
                    + wrongFound.joinIntoString("\n  ")
                    + "\n\nThe files T5ynth actually needs are:\n"
                      "  \xe2\x80\xa2 model.safetensors (NOT model.ckpt, NOT base_model.*)\n"
                      "  \xe2\x80\xa2 model_config.json\n"
                      "  \xe2\x80\xa2 LICENSE\n\n"
                      "Click 'Open Model Page' above, go to 'Files and versions', "
                      "and download exactly those three files.");
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                "No model files found",
                "This folder does not contain the three files T5ynth needs:\n"
                "  \xe2\x80\xa2 model.safetensors\n"
                "  \xe2\x80\xa2 model_config.json\n"
                "  \xe2\x80\xa2 LICENSE\n\n"
                "Click 'Open Model Page' above to fetch them from HuggingFace.");
        }
    }
    return false;
}

void SettingsPage::performAutoScan()
{
    // 1. Already installed anywhere we recognise?
    auto found = scanForModel();
    if (found.exists())
    {
        setModelPath(found);
        downloadStatusLabel.setText("Model found: " + found.getFullPathName(),
                                    juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId,
                                      juce::Colour(0xff4ade80));
        return;
    }

    // 2. Model-specific smart scan. Only SA Small is designed for the
    //    "download 3 files to Downloads, click Auto-Scan" flow.
    auto modelId = selectedModelId();
    if (modelId != "stable-audio-open-small")
    {
        updateStatus();
        downloadStatusLabel.setText(
            "No model found in standard locations. Follow the instructions below.",
            juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId,
                                      juce::Colour(0xffef4444));
        return;
    }

    // 3. Look in the system Downloads folder first.
    auto downloads = getDownloadsFolder();
    if (trySaSmallInstallFromFolder(downloads, /*reportIfMissing*/ false))
        return;  // success path already showed a dialog

    // Re-run the check to see *why* Downloads didn't work (missing / wrong /
    // nothing), so we can decide whether to nag or to open the picker.
    // We re-use the same helper with reportIfMissing=false to classify.
    int foundInDownloads = 0;
    for (int i = 0; i < kNumSaSmallRequired; ++i)
        if (downloads.getChildFile(kSaSmallRequired[i]).existsAsFile())
            ++foundInDownloads;

    // Scenario (a): some files present in Downloads — tell the user which
    // are still missing instead of opening a picker.
    if (foundInDownloads > 0)
    {
        trySaSmallInstallFromFolder(downloads, /*reportIfMissing*/ true);
        return;
    }

    // Scenario (b)/(c): nothing correct in Downloads. Offer a folder picker,
    // pre-opened at the Downloads folder.
    fileChooser = std::make_unique<juce::FileChooser>(
        "Where did you save the model files?",
        downloads.isDirectory()
            ? downloads
            : juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "");
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectDirectories,
        [this, downloads](const juce::FileChooser& fc)
        {
            auto folder = fc.getResult();
            if (folder == juce::File())
            {
                // Cancelled — leave a trail so the user knows nothing happened.
                downloadStatusLabel.setText(
                    "Auto-Scan cancelled. Follow the instructions below.",
                    juce::dontSendNotification);
                downloadStatusLabel.setColour(juce::Label::textColourId,
                                              juce::Colour(0xffef4444));
                return;
            }
            trySaSmallInstallFromFolder(folder, /*reportIfMissing*/ true);
        });
}

// ── Download ────────────────────────────────────────────────────────────────
void SettingsPage::startDownload()
{
    // Gated/manual-only models never start a download.
    if (!selectedDownloadable())
        return;

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
    auto hfRepo = selectedHfRepo();
    std::thread([this, hfRepo, modelId]() {
        juce::URL apiUrl("https://huggingface.co/api/models/" + hfRepo + "/tree/main?recursive=true");
        auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                        .withConnectionTimeoutMs(15000);
        auto stream = apiUrl.createInputStream(opts);
        if (!stream) {
            juce::MessageManager::callAsync([this, hfRepo]() {
                onDownloadFinished(false,
                    "Failed to contact https://huggingface.co/api/models/"
                    + hfRepo + "/tree/main\n\n"
                    "Check your network connection and try again.");
            });
            return;
        }
        auto response = stream->readEntireStreamAsString();
        try {
            auto json = nlohmann::json::parse(response.toStdString());

            // HF API returns {"error":"..."} for any failure — surface verbatim.
            if (json.is_object() && json.contains("error")) {
                auto errMsg = juce::String(json["error"].get<std::string>());
                juce::MessageManager::callAsync([this, errMsg, hfRepo]() {
                    onDownloadFinished(false,
                        "HuggingFace API error for " + hfRepo + ":\n\n" + errMsg);
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
                onDownloadFinished(false,
                    "Could not parse HuggingFace response for " + hfRepo + ":\n\n" + err);
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
    auto modelId = selectedModelId();
    auto hfRepo = selectedHfRepo();
    auto targetDir = getAppSupportModelDir(modelId);
    auto files = filesToDownload;  // copy for thread

    startTimer(250);  // timer updates progress bar from atomic downloadedBytes

    std::thread([this, hfRepo, targetDir, files]()
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
                            .withConnectionTimeoutMs(30000);
            auto stream = fileUrl.createInputStream(opts);

            if (!stream)
            {
                juce::MessageManager::callAsync([this, fileName, hfRepo]() {
                    onDownloadFinished(false,
                        "Could not open:\n"
                        "  https://huggingface.co/" + hfRepo + "/resolve/main/" + fileName
                        + "\n\nCheck your network connection.");
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

                // HF returns JSON {"error":"..."} for any failure — surface verbatim.
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
                    juce::MessageManager::callAsync([this, fileName, serverMsg, hfRepo]() {
                        onDownloadFinished(false,
                            "HuggingFace rejected download of " + fileName + " from "
                            + hfRepo + ":\n\n" + serverMsg);
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
                juce::MessageManager::callAsync([this, fileName, expectedStr, gotStr, hfRepo]() {
                    onDownloadFinished(false,
                        "Transfer ended early for " + fileName + " (" + hfRepo + ")\n"
                        "Expected " + expectedStr + ", received " + gotStr + ".\n\n"
                        "Retry the download, or use Browse\xe2\x80\xa6 to point at an "
                        "existing copy of the model.");
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

void SettingsPage::setBackendConnected(bool connected)
{
    backendConnected = connected;
    if (connected) backendFailReason = {};
    backendStatusLabel.setText(connected ? "Backend: Connected" : "Backend: Not connected",
                              juce::dontSendNotification);
    backendStatusLabel.setColour(juce::Label::textColourId,
        connected ? juce::Colour(0xff4ade80) : juce::Colour(0xffef4444));
    updateStatus();
}

void SettingsPage::setBackendFailed(const juce::String& reason)
{
    backendConnected = false;
    backendFailReason = reason;
    backendStatusLabel.setText("Backend: " + reason, juce::dontSendNotification);
    backendStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffef4444));
    updateStatus();
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
    bool downloadable = selectedDownloadable();

    // Download button is only shown for models T5ynth can fetch itself.
    downloadButton.setVisible(downloadable);

    if (modelPath.exists() && hasModelMarker(modelPath)) {
        modelStatusLabel.setText(id + ": Installed", juce::dontSendNotification);
        modelStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff4ade80));
        modelPathLabel.setText(modelPath.getFullPathName(), juce::dontSendNotification);
        if (backendConnected) {
            instructionsLabel.setText("Ready to generate audio.", false);
        } else if (backendFailReason.isNotEmpty()) {
            instructionsLabel.setText(
                "Model files found, but the Python backend is not available.\n\n"
                "Make sure the backend is included in the app bundle\n"
                "and all Python dependencies are installed.", false);
        } else {
            instructionsLabel.setText("Model files found. Starting backend...", false);
        }
    } else {
        modelStatusLabel.setText(id + ": Not installed", juce::dontSendNotification);
        modelStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffef4444));
        modelPathLabel.setText("", juce::dontSendNotification);

        // Per-model honest instructions. AudioLDM2 is the only model T5ynth
        // can fetch itself; the two Stability models are both gated on HF
        // and require a one-time manual install via huggingface-cli.
        auto targetPath = targetDir.getFullPathName();

        if (id == "audioldm2") {
            instructionsLabel.setText(
                "AUDIOLDM2\n"
                "Academic latent-diffusion text-to-audio model published by CVSSP / "
                "University of Surrey and collaborators (Liu et al., 2023), released "
                "as an open research artefact for studying generalised audio, music "
                "and speech generation from text. Ungated on HuggingFace and the only "
                "engine T5ynth can install directly. Click 'Download from HuggingFace' "
                "above and wait for the download to finish.\n\n"
                "  Source: https://huggingface.co/" + hfRepo + "\n"
                "  Target: " + targetPath + "\n\n"
                "License: CC BY-NC-SA 4.0 \xe2\x80\x94 non-commercial use only, no revenue "
                "threshold, no exceptions.", false);
        } else if (id == "stable-audio-open-small") {
            instructionsLabel.setText(
                "STABLE AUDIO OPEN SMALL\n"
                "Licensed under the Stability AI Community License. Gated on "
                "HuggingFace \xe2\x80\x94 a free HuggingFace account is required once to "
                "accept the license and download the files. No terminal required.\n\n"
                "  Source: https://huggingface.co/" + hfRepo + "\n"
                "  Target: " + targetPath + "\n\n"
                "INSTALL:\n"
                "  1. Click 'Open Model Page' above. Your browser opens the\n"
                "     HuggingFace page for this model.\n"
                "  2. On that page, sign up or log in (top-right corner).\n"
                "  3. Click 'Agree and access repository' to accept the license.\n"
                "  4. On the same page, click the 'Files and versions' tab.\n"
                "  5. Download exactly these three files to your usual Downloads\n"
                "     folder (one click each on the filename, then the download\n"
                "     icon on the right):\n"
                "        model.safetensors\n"
                "        model_config.json\n"
                "        LICENSE\n"
                "     Do not download model.ckpt, base_model.ckpt,\n"
                "     base_model.safetensors, or base_model_config.json \xe2\x80\x94\n"
                "     they are alternative formats T5ynth does not use.\n"
                "  6. Come back here and click 'Auto-Scan' above.\n"
                "     T5ynth finds the files in your Downloads folder and copies\n"
                "     them into the target directory. You can delete the originals\n"
                "     from Downloads afterwards.\n\n"
                "If you saved them somewhere other than Downloads, Auto-Scan will "
                "open a folder picker and ask you to point at the folder.", false);
        } else {
            // SA 1.0 (and any future gated Stability model)
            instructionsLabel.setText(
                "STABLE AUDIO OPEN 1.0\n"
                "Licensed under the Stability AI Community License. Gated on "
                "HuggingFace. The model consists of many files in nested "
                "subfolders, so the install path is the terminal:\n\n"
                "  1. Click 'Open Model Page' above, sign in, and accept the\n"
                "     license on the HuggingFace page.\n"
                "  2. In a terminal:\n"
                "       huggingface-cli login    # paste your HF access token\n"
                "       huggingface-cli download " + hfRepo + " \\\n"
                "         --local-dir \"" + targetPath + "\"\n"
                "  3. Click 'Auto-Scan' above, or 'Browse\xe2\x80\xa6' and select that folder.", false);
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

    // Buttons: Auto-Scan | Browse… | Open Model Page
    auto btnRow = area.removeFromTop(26);
    int btnW = 90;
    scanButton.setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft(6);
    browseButton.setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft(6);
    openPageButton.setBounds(btnRow.removeFromLeft(130));
    area.removeFromTop(gap * 2);

    // Download button — only visible for models T5ynth can fetch itself.
    auto dlRow = area.removeFromTop(26);
    if (downloadButton.isVisible())
        downloadButton.setBounds(dlRow.removeFromLeft(220));
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
