#include "SetupWizard.h"
#include "GuiHelpers.h"
#include <nlohmann/json.hpp>
#include <set>
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

    // Auxiliary transformer-encoder layout (e.g. t5-base required by SA Open
    // Small): root-level config.json with a tokenizer file. Audio-model dirs
    // contain config.json only inside sub-dirs like text_encoder/, so they
    // are not matched here.
    if (!hasMetadata
        && dir.getChildFile("config.json").existsAsFile()
        && (dir.getChildFile("tokenizer.json").existsAsFile()
            || dir.getChildFile("spiece.model").existsAsFile()))
        hasMetadata = true;

    if (!hasMetadata) return false;

    // Sum safetensors only. Checkpoint/pickle formats are not accepted.
    int64_t weightBytes = 0;
    for (auto& f : dir.findChildFiles(juce::File::findFiles, true, "*.safetensors"))
        weightBytes += f.getSize();

    return weightBytes >= kMinWeightBytes;
}

static void setInstructionsText(juce::TextEditor& editor, const juce::String& text)
{
    editor.setText(text, false);
    editor.setHighlightedRegion({ 0, 0 });
    editor.setCaretPosition(0);
    editor.moveCaretToTop(false);
    editor.scrollEditorToPositionCaret(0, 0);
}

static juce::String backendStderrLogPath()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("T5ynth/Logs/backend_stderr.log")
        .getFullPathName();
}

static juce::String firstErrorLine(juce::String text)
{
    text = text.replace("\r\n", "\n").trim();
    auto firstLine = text.upToFirstOccurrenceOf("\n", false, false).trim();
    return firstLine.isNotEmpty() ? firstLine : "Unknown backend error";
}

static bool isEssentialDiffusersFile(const juce::String& remotePath)
{
    auto path = remotePath.replaceCharacter('\\', '/');

    // Root metadata for diffusers pipelines.
    if (path == "model_index.json")
        return true;

    // Root-level config/tokenizer files for flat transformers repos (t5-base).
    if (path == "config.json"
        || path == "tokenizer.json"
        || path == "tokenizer_config.json"
        || path == "special_tokens_map.json"
        || path == "spiece.model"
        || path == "merges.txt"
        || path == "vocab.json"
        || path == "generation_config.json")
        return true;

    // Same files in sub-component dirs (diffusers/transformers loaders).
    if (path.endsWith("/config.json")
        || path.endsWith("/scheduler_config.json")
        || path.endsWith("/preprocessor_config.json")
        || path.endsWith("/tokenizer.json")
        || path.endsWith("/tokenizer_config.json")
        || path.endsWith("/special_tokens_map.json")
        || path.endsWith("/merges.txt")
        || path.endsWith("/vocab.json")
        || path.endsWith("/spiece.model"))
        return true;

    return false;
}

static bool shouldDownloadHfFile(const juce::String& remotePath,
                                 const std::set<std::string>& allPaths)
{
    auto path = remotePath.replaceCharacter('\\', '/');

    if (isEssentialDiffusersFile(path))
        return true;

    if (path.endsWith(".safetensors"))
        return true;

    // Everything else is optional noise for the in-app downloader
    // (README, .gitattributes, examples, pickle/bin/checkpoint formats, etc.).
    juce::ignoreUnused(allPaths);
    return false;
}

// GitHub-release-mirrored asset descriptor. Used by downloadGhReleaseInThread.
struct GhAsset { const char* name; int64_t expectedSize; };

// Files mirrored at the assets/t5-base-v1 GitHub release tag. Sizes match
// google-t5/t5-base on HuggingFace; used for progress display and the
// "skip if already downloaded" heuristic.
static const GhAsset kT5BaseGhFiles[] = {
    { "config.json",            1208 },
    { "tokenizer.json",      1389353 },
    { "spiece.model",         791656 },
    { "model.safetensors", 891646390 },
};

// Minimal file set T5ynth actually loads from google/t5gemma-b-b-ul2.
// Used by Auto-Scan when the user manually fetched the model from
// HuggingFace into ~/Downloads. The user-facing instructions in
// updateStatus() must stay in sync with this list.
//
// What's NOT here, and why:
//   * .gitattributes / README.md — git metadata + docs, irrelevant.
//   * tokenizer.model — SentencePiece source, only the slow tokenizer
//     reads it. Verified against transformers v4.45.0
//     GemmaTokenizerFast.__init__: vocab_file defaults to None and is
//     only consumed by can_save_slow_tokenizer(); the fast path loads
//     entirely from tokenizer.json.
//   * generation_config.json — defaults for model.generate(); T5ynth
//     calls AutoModel.encoder forward only, never generation.
//
// What IS here even though it MIGHT be redundant — and why:
//   * special_tokens_map.json — GemmaTokenizerFast hardcodes the
//     standard Gemma special tokens ("<bos>", "<eos>", "<pad>",
//     "<unk>") as __init__ defaults, and tokenizer.json carries the
//     full added_tokens list. So FOR STANDARD GEMMA the map is
//     duplicate metadata. But T5Gemma's file is gated on HF (couldn't
//     verify content) and PreTrainedTokenizerFast.from_pretrained
//     overrides the class defaults from this file if present. A
//     missing map with custom mappings would silently produce wrong
//     conditioner input — at 636 bytes the safer call is to include
//     it.
static const char* kT5GemmaRequiredFiles[] = {
    "config.json",
    "tokenizer.json",
    "tokenizer_config.json",
    "special_tokens_map.json",
    "model.safetensors",
};
static constexpr int kNumT5GemmaRequiredFiles =
    sizeof(kT5GemmaRequiredFiles) / sizeof(kT5GemmaRequiredFiles[0]);

// Known models — extend this list to add new engines.
// downloadable: if false, show manual instructions only (no Download button).
//   Both Stable Audio models are gated on HuggingFace and T5ynth never prompts
//   for tokens, so users manually fetch the two root files and Auto-Scan or
//   Browse... imports them. AudioLDM2 is the only ungated model and
//   the only one T5ynth downloads directly.
struct KnownModel {
    const char* id;
    const char* displayName;
    const char* hfRepo;       // HuggingFace repo
    const char* ghRelease;    // GitHub Release tag URL base (nullptr = use HF)
    const char* licenseUrl;   // URL to full license text
    const char* licenseNotice;// Shown in confirmation dialog before download
    bool        downloadable; // false = manual-only (no in-app download)
    bool        isGenerationEngine; // false = auxiliary asset (e.g. text encoder)
    const GhAsset* ghFiles;   // file list for ghRelease download (nullptr if no mirror)
    int         ghFileCount;
    // True when T5ynth offers an in-app `git clone` of the HF repo. Used
    // for gated models where the user-facing pattern of "download these
    // files into Downloads" breaks down because filenames collide across
    // repos (T5Gemma: config.json/tokenizer.json/model.safetensors share
    // names with t5-base and the SAO checkpoints — browser auto-renames
    // produce chaos). git clone puts each repo in a named directory and
    // the user's existing HF auth (huggingface-cli login / credential
    // helper) handles the gate. T5ynth itself never touches the HF token
    // — git does, exactly as in any other clone the user runs.
    bool        gitCloneable;
};
static const KnownModel kKnownModels[] = {
    { "stable-audio-open-1.0",   "Stable Audio Open 1.0",     "stabilityai/stable-audio-open-1.0", nullptr,
      "https://stability.ai/community-license-agreement",
      "This model is licensed under the Stability AI Community License.\n\n"
      "- Non-commercial use: free\n"
      "- Commercial use under $1M annual revenue: free (register at stability.ai)\n"
      "- Commercial use over $1M: enterprise license required\n\n"
      "T5ynth does not provide the model weights. By downloading, you accept\n"
      "the license terms and take responsibility for compliance.", false, true,
      nullptr, 0, /*gitCloneable*/ false },
    { "stable-audio-open-small", "Stable Audio Open Small", "stabilityai/stable-audio-open-small",
      nullptr,
      "https://stability.ai/community-license-agreement",
      "This model is licensed under the Stability AI Community License.\n\n"
      "- Non-commercial use: free\n"
      "- Commercial use under $1M annual revenue: free (register at stability.ai)\n"
      "- Commercial use over $1M: enterprise license required\n\n"
      "T5ynth does not provide the model weights. By downloading, you accept\n"
      "the license terms and take responsibility for compliance.", false, true,
      nullptr, 0, /*gitCloneable*/ false },
    { "audioldm2",               "AudioLDM2",                  "cvssp/audioldm2", nullptr,
      "https://creativecommons.org/licenses/by-nc-sa/4.0/",
      "This model is licensed under CC BY-NC-SA 4.0.\n\n"
      "- Non-commercial use only (no revenue threshold, no exceptions)\n"
      "- Commercial use is NOT permitted under this license\n\n"
      "T5ynth does not provide the model weights. By downloading, you accept\n"
      "the license terms and take responsibility for compliance.", true, true,
      nullptr, 0, /*gitCloneable*/ false },
    { "t5-base",                 "T5-Base text encoder",       "t5-base", nullptr,
      "https://www.apache.org/licenses/LICENSE-2.0",
      "T5-base is licensed under Apache License 2.0 (open, no restrictions).\n\n"
      "Required by Stable Audio Open Small as the text encoder. T5ynth does\n"
      "not provide the weights. By downloading you accept the Apache 2.0\n"
      "license.", true, false,
      kT5BaseGhFiles,
      static_cast<int>(sizeof(kT5BaseGhFiles) / sizeof(kT5BaseGhFiles[0])),
      /*gitCloneable*/ false },
    { "t5gemma-b-b-ul2",         "T5Gemma text encoder (SA3)", "google/t5gemma-b-b-ul2", nullptr,
      "https://ai.google.dev/gemma/terms",
      "T5Gemma is licensed under the Gemma Terms of Use.\n\n"
      "- Use is restricted by Google's Prohibited Use Policy -- review the\n"
      "  full terms before downloading.\n"
      "- A free HuggingFace account is required to accept the terms.\n\n"
      "Required by Stable Audio 3 Small (Music and SFX share the same text\n"
      "encoder). T5ynth does not provide the weights. By accessing the model\n"
      "you accept the Gemma Terms of Use and take responsibility for\n"
      "compliance.", false, false,
      nullptr, 0, /*gitCloneable*/ true },
    // The HF repo "stabilityai/stable-audio-3-small" does NOT exist — the
    // SA3 Small line is two task-specialised checkpoints (music + sfx) built
    // from the same architecture. Both are catalogued; the user picks one
    // to install. Source: HF org listing for stabilityai/, May 2026.
    { "stable-audio-3-small-music", "Stable Audio 3 Small Music", "stabilityai/stable-audio-3-small-music", nullptr,
      "https://stability.ai/community-license-agreement",
      "This model is licensed under the Stability AI Community License.\n\n"
      "- Non-commercial use: free\n"
      "- Commercial use under $1M annual revenue: free (register at stability.ai)\n"
      "- Commercial use over $1M: enterprise license required\n\n"
      "T5ynth does not provide the model weights. By downloading, you accept\n"
      "the license terms and take responsibility for compliance.", false, true,
      nullptr, 0, /*gitCloneable*/ false },
    { "stable-audio-3-small-sfx", "Stable Audio 3 Small SFX", "stabilityai/stable-audio-3-small-sfx", nullptr,
      "https://stability.ai/community-license-agreement",
      "This model is licensed under the Stability AI Community License.\n\n"
      "- Non-commercial use: free\n"
      "- Commercial use under $1M annual revenue: free (register at stability.ai)\n"
      "- Commercial use over $1M: enterprise license required\n\n"
      "T5ynth does not provide the model weights. By downloading, you accept\n"
      "the license terms and take responsibility for compliance.\n\n"
      "Companion to SA3 Small Music — same architecture, trained for sound\n"
      "effects rather than musical content. Native default duration is\n"
      "shorter than the synth's 11s cap; outputs are clamped to whatever\n"
      "the model produces.", false, true,
      nullptr, 0, /*gitCloneable*/ false },
};
static constexpr int kNumKnownModels = sizeof(kKnownModels) / sizeof(kKnownModels[0]);

juce::File SettingsPage::getAppSupportModelDir()
{
    return getAppSupportModelDir("stable-audio-open-1.0");
}

juce::File SettingsPage::getAppSupportModelDir(const juce::String& modelId)
{
   #if JUCE_MAC
    // Per-user path: model licenses are personal (each user accepts individually).
    // System-wide path is scan-only (admin may pre-install models for all users).
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("T5ynth/models/" + modelId);
   #elif JUCE_LINUX
    auto appData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                       .getChildFile("share");
    return appData.getChildFile("T5ynth/models/" + modelId);
   #else
    // Windows: per-user %APPDATA% (same reasoning — licenses are personal)
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("T5ynth/models/" + modelId);
   #endif
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

bool SettingsPage::selectedIsGenerationEngine()
{
    int idx = modelChooser.getSelectedItemIndex();
    if (idx >= 0 && idx < kNumKnownModels)
        return kKnownModels[idx].isGenerationEngine;
    return true;  // default-safe for unknown indices
}

bool SettingsPage::selectedGitCloneable()
{
    int idx = modelChooser.getSelectedItemIndex();
    if (idx >= 0 && idx < kNumKnownModels)
        return kKnownModels[idx].gitCloneable;
    return false;
}

juce::String SettingsPage::selectedModelDisplay()
{
    int idx = modelChooser.getSelectedItemIndex();
    if (idx >= 0 && idx < kNumKnownModels)
        return kKnownModels[idx].displayName;
    return {};
}

const void* SettingsPage::selectedGhFiles()
{
    int idx = modelChooser.getSelectedItemIndex();
    if (idx >= 0 && idx < kNumKnownModels)
        return static_cast<const void*>(kKnownModels[idx].ghFiles);
    return nullptr;
}

int SettingsPage::selectedGhFileCount()
{
    int idx = modelChooser.getSelectedItemIndex();
    if (idx >= 0 && idx < kNumKnownModels)
        return kKnownModels[idx].ghFileCount;
    return 0;
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
    instructionsLabel.setCaretVisible(false);
    instructionsLabel.setSelectAllWhenFocused(false);
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

    // Same colour family as downloadButton — both are "install action"
    // buttons that perform a long network operation. Visibility toggles
    // per-model in updateStatus() based on selectedGitCloneable().
    gitCloneButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2d6a4f));
    gitCloneButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    gitCloneButton.onClick = [this] { cloneHfRepoInThread(); };
    addAndMakeVisible(gitCloneButton);

    auto found = scanForModel();
    if (found.exists()) modelPath = found;
    updateStatus();

    setSize(500, 480);
}

void SettingsPage::setModelInstallBusy(bool busy, const juce::String& statusText)
{
    modelInstallBusy_.store(busy);
    modelChooser.setEnabled(!busy);
    scanButton.setEnabled(!busy && !downloading.load());
    browseButton.setEnabled(!busy && !downloading.load());
    openPageButton.setEnabled(!busy);
    downloadButton.setEnabled(!busy && !downloading.load());
    gitCloneButton.setEnabled(!busy && !downloading.load());

    if (statusText.isNotEmpty())
    {
        downloadStatusLabel.setText(statusText, juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId,
                                      busy ? kAccent : juce::Colour(0xffcccccc));
    }
}

static bool modelHasRequiredAuxAssets(const juce::String& id, const juce::File& modelDir)
{
    juce::ignoreUnused(id);
    if (!modelDir.exists() || !hasModelMarker(modelDir))
        return false;
    return true;
}

// ── Scan ────────────────────────────────────────────────────────────────────
static juce::File scanForModelById(const juce::String& id, const juce::String& hfRepo)
{
    // HF cache uses "--" as separator: "models--stabilityai--stable-audio-open-1.0"
    auto hfCacheDir = "models--" + hfRepo.replace("/", "--");

    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    auto oldAppData = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
    std::vector<juce::File> candidates = {
        SettingsPage::getAppSupportModelDir(id),               // preferred (system or user, see fallback logic)
       #if JUCE_MAC
        juce::File("/Library/Application Support/T5ynth/models/" + id),  // system-wide (.pkg)
       #endif
        oldAppData.getChildFile("T5ynth/models/" + id),        // ~/Library/Application Support/ (per-user)
        home.getChildFile("Library/T5ynth/models/" + id),      // legacy macOS
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
    {
        if (!kKnownModels[i].isGenerationEngine)
            continue;  // auxiliary assets alone (e.g. t5-base) do not count
        auto found = scanForModelById(kKnownModels[i].id, kKnownModels[i].hfRepo);
        if (modelHasRequiredAuxAssets(kKnownModels[i].id, found))
            return true;
    }
    return false;
}

juce::Result SettingsPage::importModelDirectoryForId(const juce::String& modelId,
                                                     const juce::File& sourceDir,
                                                     juce::File& activeDir,
                                                     bool replaceExistingTarget)
{
    activeDir = juce::File();

    if (!sourceDir.isDirectory() || !hasModelMarker(sourceDir))
        return juce::Result::fail("This directory does not contain a valid model.");

    auto targetDir = getAppSupportModelDir(modelId);
    const auto sourcePath = sourceDir.getFullPathName();
    const auto targetPath = targetDir.getFullPathName();

    if (sourcePath == targetPath)
    {
        activeDir = targetDir;
        return juce::Result::ok();
    }

    const bool targetPresent = targetDir.exists() || targetDir.isSymbolicLink();
    if (targetPresent)
    {
        const auto linkedTarget = targetDir.isSymbolicLink() ? targetDir.getLinkedTarget()
                                                             : juce::File();
        if (hasModelMarker(targetDir)
            && linkedTarget.getFullPathName() == sourcePath)
        {
            activeDir = targetDir;
            return juce::Result::ok();
        }

        if (!replaceExistingTarget && hasModelMarker(targetDir))
        {
            activeDir = targetDir;
            return juce::Result::ok();
        }

        if (!targetDir.deleteRecursively())
            return juce::Result::fail("Could not replace the existing model slot:\n  "
                                      + targetPath);
    }

    auto parentDir = targetDir.getParentDirectory();
    if (!parentDir.isDirectory() && !parentDir.createDirectory())
        return juce::Result::fail("Could not create the model directory:\n  "
                                  + parentDir.getFullPathName());

    if (!sourceDir.createSymbolicLink(targetDir, false))
        return juce::Result::fail("Could not import the model into:\n  "
                                  + targetPath
                                  + "\n\nSource:\n  "
                                  + sourcePath);

    activeDir = targetDir;
    return juce::Result::ok();
}

void SettingsPage::importDiscoveredModels()
{
    for (int i = 0; i < kNumKnownModels; ++i)
    {
        const auto& km = kKnownModels[i];
        auto found = scanForModelById(km.id, km.hfRepo);
        if (!found.exists())
            continue;

        juce::File activeDir;
        auto result = importModelDirectoryForId(km.id, found, activeDir, false);
        if (result.failed())
        {
            juce::Logger::writeToLog("Model auto-import skipped for "
                                     + juce::String(km.id) + ": "
                                     + result.getErrorMessage());
        }
    }
}

void SettingsPage::setModelPath(const juce::File& dir)
{
    modelPath = dir;
    const bool usableModel = modelHasRequiredAuxAssets(selectedModelId(), modelPath);

    if (modelPath.exists() && usableModel && !backendConnected)
    {
        backendFailReason = {};
        backendStatusLabel.setText("Backend: Starting...", juce::dontSendNotification);
        backendStatusLabel.setColour(juce::Label::textColourId, kDim);
    }

    updateStatus();
    if (modelPath.exists() && usableModel && onModelReady)
        onModelReady();
}

void SettingsPage::browseForModel()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select model directory",
        modelPath.exists() ? modelPath : juce::File::getSpecialLocation(juce::File::userHomeDirectory),
        "", true);
    auto modelId = selectedModelId();
    juce::Component::SafePointer<SettingsPage> safeThis(this);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis, modelId](const juce::FileChooser& fc) {
            juce::ignoreUnused(modelId);
            auto* self = safeThis.getComponent();
            if (self == nullptr) return;
            auto result = fc.getResult();
            if (result == juce::File()) return;
            if (!hasModelMarker(result)) {
                juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                    "Wrong Directory", "This directory does not contain a valid model.\n\n"
                    "Select a folder that contains model_index.json or model_config.json.");
                return;
            }
            juce::File activeDir;
            auto importResult = self->importModelDirectoryForId(modelId, result, activeDir, true);
            if (importResult.failed())
            {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Import failed",
                    importResult.getErrorMessage());
                return;
            }

            self->setModelPath(activeDir);
        });
}

// ── Smart Auto-Scan ─────────────────────────────────────────────────────────
// For native Stability models the user follows a manual-download walkthrough
// (fetch 2 files from HuggingFace to the system Downloads folder). Auto-Scan
// then hides all path details: it looks there for the files and copies them to
// the correct app-support location, or guides the user if something is missing.

// Files T5ynth cares about for a native Stability install.
static const char* kNativeStabilityRequired[] = {
    "model.safetensors",
    "model_config.json",
};
static constexpr int kNumNativeStabilityRequired =
    sizeof(kNativeStabilityRequired) / sizeof(kNativeStabilityRequired[0]);

// Files the user may have fetched by mistake alongside the correct ones.
// When we see these in the source folder, we call them out by name so the
// user can delete them. None of them are needed by T5ynth.
static const char* kNativeStabilityWrongFiles[] = {
    "model.ckpt",
    "vae_model.ckpt",
    "base_model.ckpt",
    "base_model.safetensors",
    "base_model_config.json",
};
static constexpr int kNumNativeStabilityWrongFiles =
    sizeof(kNativeStabilityWrongFiles) / sizeof(kNativeStabilityWrongFiles[0]);

static juce::File getDownloadsFolder()
{
    // macOS/Windows/Linux all use ~/Downloads as the browser default on a
    // fresh user account. If a user has customised their downloads dir via
    // XDG_DOWNLOAD_DIR or similar, the folder-picker fallback catches it.
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
               .getChildFile("Downloads");
}

// Find the file in sourceFolder that should serve as `canonicalName`.
// Browsers append disambiguation suffixes when the user downloads a file
// whose name already exists in the folder: Chrome/Edge use "name (1).ext",
// Firefox/Safari use "name-2.ext", etc. A literal lookup for
// "model.safetensors" therefore misses the SFX checkpoint the user just
// downloaded if a Music checkpoint with the same canonical name is still
// sitting in the folder from an earlier install — the user ends up
// installing the OLD file under the NEW model id.
//
// Strategy: search siblings matching <base>*<ext>, pick the newest by
// last-modified time. That's the one the user most recently downloaded,
// which is the one they meant to install for the currently-selected model
// in the wizard.
//
// Returns an invalid File when no candidate exists.
static juce::File pickNewestCandidate(const juce::File& sourceFolder,
                                      const juce::String& canonicalName)
{
    const auto dot  = canonicalName.lastIndexOfChar('.');
    const auto base = (dot > 0) ? canonicalName.substring(0, dot) : canonicalName;
    const auto ext  = (dot > 0) ? canonicalName.substring(dot)    : juce::String();

    juce::Array<juce::File> matches;
    sourceFolder.findChildFiles(matches,
                                juce::File::findFiles,
                                /*searchRecursively*/ false,
                                base + "*" + ext);
    if (matches.isEmpty())
        return {};

    juce::File best = matches[0];
    juce::Time bestTime = best.getLastModificationTime();
    for (int i = 1; i < matches.size(); ++i)
    {
        const auto t = matches[i].getLastModificationTime();
        if (t > bestTime)
        {
            best = matches[i];
            bestTime = t;
        }
    }
    return best;
}

// Walk the standard install roots and look for any *other* model directory
// that already holds a same-sized file under `canonicalName`. Used as a
// last-line check before completing an install: if the user dropped a
// browser-renamed file that turns out to be a byte-for-byte duplicate of
// what they already have under a different model id, the install is
// almost certainly a mistake (they downloaded the wrong variant).
//
// Returns the offending model id, or an empty string when no collision.
// Size-based fingerprint is cheap (no file I/O for content) and effective
// — Stable Audio variants differ by hundreds of MB across music vs sfx
// vs medium, and identical sizes across variants are vanishingly rare.
static juce::String findSameSizeInstalledModel(const juce::String& selfModelId,
                                                const juce::String& canonicalName,
                                                int64_t fileSize)
{
    if (fileSize <= 0)
        return {};

    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    juce::Array<juce::File> roots;
   #if JUCE_MAC
    roots.add(juce::File("/Library/Application Support/T5ynth/models"));
   #endif
    roots.add(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                  .getChildFile("T5ynth/models"));
    roots.add(home.getChildFile("Library/T5ynth/models"));
    roots.add(home.getChildFile("t5ynth/models"));

    for (auto& root : roots)
    {
        if (!root.isDirectory())
            continue;
        for (auto& sub : root.findChildFiles(juce::File::findDirectories, false))
        {
            const auto otherId = sub.getFileName();
            if (otherId == selfModelId)
                continue;
            auto candidate = sub.getChildFile(canonicalName);
            if (candidate.existsAsFile() && candidate.getSize() == fileSize)
                return otherId;
        }
    }
    return {};
}

SettingsPage::InstallOutcome SettingsPage::tryNativeStabilityInstallFromFolder(
    const juce::File& sourceFolder,
    const juce::String& modelId,
    const juce::String& modelDisplayName,
    bool reportIfMissing)
{
    if (!sourceFolder.isDirectory())
    {
        if (reportIfMissing)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Folder not found",
                "T5ynth could not read the folder:\n  " + sourceFolder.getFullPathName());
            return InstallOutcome::AbortedWithDialog;
        }
        return InstallOutcome::NotInstalled;
    }

    // Look at the contents. `pickNewestCandidate` matches both the exact
    // canonical name and any browser-rename variants (Music + SFX safetensors
    // both arrive as "model.safetensors" from HF; the second download lands as
    // "model-2.safetensors" / "model (1).safetensors" / etc.). The newest one
    // wins so installing for the SFX dropdown picks up the SFX checkpoint the
    // user just downloaded, not the older Music file that's still sitting in
    // ~/Downloads.
    struct Staged { juce::File source; juce::String destName; };
    std::vector<Staged> staged;
    juce::StringArray missingNames;
    for (int i = 0; i < kNumNativeStabilityRequired; ++i)
    {
        const juce::String canonical = kNativeStabilityRequired[i];
        auto candidate = pickNewestCandidate(sourceFolder, canonical);
        if (candidate.existsAsFile())
            staged.push_back({ candidate, canonical });
        else
            missingNames.add(canonical);
    }

    juce::StringArray wrongFound;
    for (int i = 0; i < kNumNativeStabilityWrongFiles; ++i)
    {
        auto candidate = sourceFolder.getChildFile(kNativeStabilityWrongFiles[i]);
        if (candidate.existsAsFile())
            wrongFound.add(kNativeStabilityWrongFiles[i]);
    }

    const juce::String wrongNote = wrongFound.isEmpty()
        ? juce::String()
        : juce::String("\n\nNote: these files in the same folder are NOT needed "
                       "by T5ynth and can be deleted to save space:\n  ")
              + wrongFound.joinIntoString("\n  ");

    // Scenario (d): both required files present — copy and done.
    if (missingNames.isEmpty())
    {
        // Same-size duplicate guard: if the to-be-installed safetensors is
        // byte-identical in size to one already installed under a DIFFERENT
        // model id, the user probably picked the wrong file in Downloads
        // (e.g. installed SFX while ~/Downloads still held the old Music
        // checkpoint). Surface a confirmation rather than silently writing
        // the same checkpoint into both directories.
        juce::String duplicateOf;
        for (const auto& sf : staged)
        {
            if (!sf.destName.endsWithIgnoreCase(".safetensors"))
                continue;
            duplicateOf = findSameSizeInstalledModel(modelId, sf.destName, sf.source.getSize());
            if (duplicateOf.isNotEmpty())
                break;
        }

        if (duplicateOf.isNotEmpty())
        {
            const auto sourceName = staged.front().source.getFileName();
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Duplicate of " + duplicateOf + "?",
                "T5ynth picked the file:\n  " + sourceName
                + "\n\nfrom your Downloads folder for " + modelDisplayName
                + ", but its size matches the file already installed for '"
                + duplicateOf + "'. The two checkpoints are most likely the "
                  "SAME variant. If you meant to install a different one, "
                  "re-download it from the model page (the browser saves the "
                  "second copy as model-2.safetensors / model (1).safetensors), "
                  "then click Auto-Scan again. Install aborted.");
            return InstallOutcome::AbortedWithDialog;
        }

        auto targetDir = getAppSupportModelDir(modelId);
        setModelInstallBusy(true,
            "Copying " + modelDisplayName + " into T5ynth. This can take a moment...");

        juce::Component::SafePointer<SettingsPage> safeThis(this);

        std::thread([safeThis, sourceFolder, targetDir, staged, wrongNote, modelDisplayName]()
        {
            juce::String errorTitle;
            juce::String errorBody;

            if (!targetDir.createDirectory())
            {
                errorTitle = "Could not create model folder";
                errorBody = "T5ynth could not create:\n  " + targetDir.getFullPathName()
                          + "\n\nCheck folder permissions and try again.";
            }
            else
            {
                juce::StringArray copyErrors;
                for (const auto& sf : staged)
                {
                    // Copy under the CANONICAL name even if the source on
                    // disk is "model-2.safetensors" — the backend loads by
                    // canonical name, not by whatever the browser saved.
                    auto dest = targetDir.getChildFile(sf.destName);
                    if (dest.existsAsFile()) dest.deleteFile();
                    if (!sf.source.copyFileTo(dest))
                        copyErrors.add(sf.source.getFileName());
                }

                if (!copyErrors.isEmpty())
                {
                    errorTitle = "Copy failed";
                    errorBody = "Found all required files, but copying failed for:\n  "
                              + copyErrors.joinIntoString(", ")
                              + "\n\nCheck disk space and folder permissions in:\n  "
                              + targetDir.getFullPathName();
                }
                else if (!hasModelMarker(targetDir))
                {
                    errorTitle = "Files copied, but look incomplete";
                    errorBody = "Files were copied to:\n  " + targetDir.getFullPathName()
                              + "\n\nBut the model weights look too small. The download may "
                                "have been interrupted. Re-download model.safetensors from "
                                "HuggingFace (click 'Open Model Page' above) and try again.";
                }
            }

            juce::MessageManager::callAsync(
                [safeThis, sourceFolder, targetDir, wrongNote, errorTitle, errorBody, modelDisplayName]()
                {
                    auto* self = safeThis.getComponent();
                    if (self == nullptr) return;

                    self->setModelInstallBusy(false);

                    if (errorTitle.isNotEmpty())
                    {
                        self->downloadStatusLabel.setText("Model install failed",
                                                          juce::dontSendNotification);
                        self->downloadStatusLabel.setColour(juce::Label::textColourId,
                                                            juce::Colour(0xffef4444));
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            errorTitle,
                            errorBody);
                        return;
                    }

                    self->downloadStatusLabel.setText("Model copied. Activating...",
                                                       juce::dontSendNotification);
                    self->downloadStatusLabel.setColour(
                        juce::Label::textColourId,
                        juce::Colour(0xff4ade80));

                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::InfoIcon,
                        modelDisplayName + " -- Installed",
                        "T5ynth copied the model files from:\n  "
                            + sourceFolder.getFullPathName()
                            + "\n\nto:\n  " + targetDir.getFullPathName()
                            + "\n\nThe originals are still in your Downloads folder -- "
                              "you can delete them now."
                            + wrongNote,
                        "OK",
                        self,
                        juce::ModalCallbackFunction::create(
                            [safeThis, targetDir](int)
                            {
                                if (auto* page = safeThis.getComponent())
                                    page->setModelPath(targetDir);
                            }));
                });
        }).detach();
        return InstallOutcome::Installed;
    }

    // Scenario (a): some but not all required files present.
    if (!staged.empty() && reportIfMissing)
    {
        juce::String foundList;
        for (const auto& sf : staged)
            foundList += "  [OK] " + sf.source.getFileName() + "\n";

        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "Download incomplete",
            "Found in:\n  " + sourceFolder.getFullPathName() + "\n\n"
                + foundList + "\nStill missing:\n  [X] "
                + missingNames.joinIntoString("\n  [X] ")
                + "\n\nPlease download the missing files from the model page "
                  "(click 'Open Model Page' above) and click 'Auto-Scan' again."
                + wrongNote);
        return InstallOutcome::AbortedWithDialog;
    }

    // Scenario (b)/(c): nothing valid in this folder.
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
                      "  * model.safetensors (NOT model.ckpt, NOT base_model.*)\n"
                      "  * model_config.json\n\n"
                      "Click 'Open Model Page' above, go to 'Files and versions', "
                      "and download exactly those two files.");
        }
        else
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                "No model files found",
                "This folder does not contain the two files T5ynth needs:\n"
                "  * model.safetensors\n"
                "  * model_config.json\n\n"
                "Click 'Open Model Page' above to fetch them from HuggingFace.");
        }
        return InstallOutcome::AbortedWithDialog;
    }
    return InstallOutcome::NotInstalled;
}

SettingsPage::InstallOutcome SettingsPage::tryFlatEncoderInstallFromFolder(
    const juce::File& sourceFolder,
    const juce::String& modelId,
    const juce::String& modelDisplayName,
    const char* const* requiredFiles,
    int numFiles,
    bool reportIfMissing)
{
    // Sibling of tryNativeStabilityInstallFromFolder for flat-transformers
    // encoders (T5Gemma). Same install mechanics — required-files check,
    // scenario reporting, copy-then-verify — but no "wrong files"
    // heuristic: HF's Files tab for these repos doesn't surface
    // misleading alternatives the way Stability's two-checkpoint repos do.
    if (!sourceFolder.isDirectory())
    {
        if (reportIfMissing)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon,
                "Folder not found",
                "T5ynth could not read the folder:\n  " + sourceFolder.getFullPathName());
            return InstallOutcome::AbortedWithDialog;
        }
        return InstallOutcome::NotInstalled;
    }

    juce::Array<juce::File> foundRequired;
    juce::StringArray missingNames;
    for (int i = 0; i < numFiles; ++i)
    {
        auto candidate = sourceFolder.getChildFile(requiredFiles[i]);
        if (candidate.existsAsFile())
            foundRequired.add(candidate);
        else
            missingNames.add(requiredFiles[i]);
    }

    // All present → copy to the target app-support dir.
    if (missingNames.isEmpty())
    {
        auto targetDir = getAppSupportModelDir(modelId);
        setModelInstallBusy(true,
            "Copying " + modelDisplayName + " into T5ynth. This can take a moment...");

        juce::Component::SafePointer<SettingsPage> safeThis(this);
        std::vector<juce::File> requiredVec;
        requiredVec.reserve(static_cast<size_t>(foundRequired.size()));
        for (auto& f : foundRequired)
            requiredVec.push_back(f);

        std::thread([safeThis, sourceFolder, targetDir, requiredVec, modelDisplayName]()
        {
            juce::String errorTitle;
            juce::String errorBody;

            if (!targetDir.createDirectory())
            {
                errorTitle = "Could not create model folder";
                errorBody = "T5ynth could not create:\n  " + targetDir.getFullPathName()
                          + "\n\nCheck folder permissions and try again.";
            }
            else
            {
                juce::StringArray copyErrors;
                for (const auto& f : requiredVec)
                {
                    auto dest = targetDir.getChildFile(f.getFileName());
                    if (dest.existsAsFile()) dest.deleteFile();
                    if (!f.copyFileTo(dest))
                        copyErrors.add(f.getFileName());
                }

                if (!copyErrors.isEmpty())
                {
                    errorTitle = "Copy failed";
                    errorBody = "Found all required files, but copying failed for:\n  "
                              + copyErrors.joinIntoString(", ")
                              + "\n\nCheck disk space and folder permissions in:\n  "
                              + targetDir.getFullPathName();
                }
                else if (!hasModelMarker(targetDir))
                {
                    // hasModelMarker checks config.json + tokenizer.json/spiece.model
                    // + safetensors size. A failure here means the safetensors
                    // either didn't copy or is truncated.
                    errorTitle = "Files copied, but look incomplete";
                    errorBody = "Files were copied to:\n  " + targetDir.getFullPathName()
                              + "\n\nBut the model weights look too small. The download "
                                "may have been interrupted. Re-download model.safetensors "
                                "from HuggingFace and try Auto-Scan again.";
                }
            }

            juce::MessageManager::callAsync(
                [safeThis, sourceFolder, targetDir, errorTitle, errorBody, modelDisplayName]()
                {
                    auto* self = safeThis.getComponent();
                    if (self == nullptr) return;

                    self->setModelInstallBusy(false);

                    if (errorTitle.isNotEmpty())
                    {
                        self->downloadStatusLabel.setText("Model install failed",
                                                          juce::dontSendNotification);
                        self->downloadStatusLabel.setColour(juce::Label::textColourId,
                                                            juce::Colour(0xffef4444));
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            errorTitle,
                            errorBody);
                        return;
                    }

                    self->downloadStatusLabel.setText("Model copied. Activating...",
                                                       juce::dontSendNotification);
                    self->downloadStatusLabel.setColour(juce::Label::textColourId,
                                                        juce::Colour(0xff4ade80));

                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::InfoIcon,
                        modelDisplayName + " -- Installed",
                        "T5ynth copied the model files from:\n  "
                            + sourceFolder.getFullPathName()
                            + "\n\nto:\n  " + targetDir.getFullPathName()
                            + "\n\nThe originals are still in your Downloads folder -- "
                              "you can delete them now.",
                        "OK",
                        self,
                        juce::ModalCallbackFunction::create(
                            [safeThis, targetDir](int)
                            {
                                if (auto* page = safeThis.getComponent())
                                    page->setModelPath(targetDir);
                            }));
                });
        }).detach();
        return InstallOutcome::Installed;
    }

    // Some present, some missing — name the missing ones explicitly.
    if (!foundRequired.isEmpty() && reportIfMissing)
    {
        juce::String foundList;
        for (auto& f : foundRequired)
            foundList += "  [OK] " + f.getFileName() + "\n";

        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "Download incomplete",
            "Found in:\n  " + sourceFolder.getFullPathName() + "\n\n"
                + foundList + "\nStill missing:\n  [X] "
                + missingNames.joinIntoString("\n  [X] ")
                + "\n\nPlease download the missing files from the model page "
                  "(click 'Open Model Page' above) and click 'Auto-Scan' again.");
        return InstallOutcome::AbortedWithDialog;
    }

    // Nothing in this folder.
    if (reportIfMissing)
    {
        juce::String fileList;
        for (int i = 0; i < numFiles; ++i)
            fileList += juce::String("  * ") + requiredFiles[i] + "\n";

        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::InfoIcon,
            "No model files found",
            "This folder does not contain the files T5ynth needs:\n"
                + fileList
                + "\nClick 'Open Model Page' above to fetch them from HuggingFace.");
        return InstallOutcome::AbortedWithDialog;
    }
    return InstallOutcome::NotInstalled;
}

// ── git-clone install path ───────────────────────────────────────────────────
// For gated models with generic filenames (T5Gemma's config.json /
// tokenizer.json / model.safetensors collide with t5-base and SAO when
// dropped into the same Downloads folder), we shell out to git instead.
// The clone lands in a named subdirectory inside T5ynth's app-support
// model dir; the user's existing HF auth (huggingface-cli login or git
// credential helper) handles the gate without T5ynth ever seeing a token.
//
// git-lfs is required: HF stores weights via LFS, and `git clone` only
// smudges (downloads) the real bytes when git-lfs is installed. Without
// it the safetensors file is a ~132-byte pointer, which hasModelMarker's
// kMinWeightBytes threshold catches and surfaces as an error.

static juce::String platformGitInstallHint()
{
   #if JUCE_MAC
    return "Install Apple's Command Line Tools (run `xcode-select --install` "
           "in a terminal) or Git for macOS from https://git-scm.com/download/mac.";
   #elif JUCE_WINDOWS
    return "Install Git for Windows from https://git-scm.com/download/win. "
           "The default installer bundles git-lfs too.";
   #else
    return "Install git from your distribution's package manager "
           "(e.g. `sudo apt install git`).";
   #endif
}

static juce::String platformLfsInstallHint()
{
   #if JUCE_MAC
    return "Install git-lfs with `brew install git-lfs` (Homebrew) or from "
           "https://git-lfs.com. After installing, run `git lfs install` once "
           "to register the smudge filter.";
   #elif JUCE_WINDOWS
    return "git-lfs ships with Git for Windows. Reinstall the latest Git for "
           "Windows from https://git-scm.com/download/win and the LFS filter "
           "comes with it. After installing, run `git lfs install` once.";
   #else
    return "Install git-lfs from your distribution's package manager "
           "(e.g. `sudo apt install git-lfs`), then run `git lfs install` "
           "once to register the smudge filter.";
   #endif
}

SettingsPage::GitPreflightResult SettingsPage::gitPreflight()
{
    // Only cache the positive result: a negative result is a state the
    // user is likely to remediate ("install git-lfs and click again"),
    // and caching it would make the dialog's "click again" advice lie.
    // Re-running `git --version` + `git lfs version` is ~50 ms total —
    // cheap enough to do on every retry click.
    if (gitPreflightCached_.has_value() && gitPreflightCached_->ok)
        return *gitPreflightCached_;

    GitPreflightResult result;

    // ChildProcess::start returns false if the executable can't be found
    // in PATH; we treat that as the same failure as a non-zero exit.
    // 5 s is generous for `git --version`.
    auto runWithTimeout = [](const juce::StringArray& args, int timeoutMs) -> int
    {
        juce::ChildProcess proc;
        if (!proc.start(args, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
            return -1;
        if (!proc.waitForProcessToFinish(timeoutMs))
        {
            proc.kill();
            return -1;
        }
        return static_cast<int>(proc.getExitCode());
    };

    if (runWithTimeout({"git", "--version"}, 5000) != 0)
    {
        // Do NOT cache: user might install git and retry.
        return { false, "git", platformGitInstallHint() };
    }

    // `git lfs version` exits non-zero when the LFS filter isn't
    // installed; `git lfs` alone prints help with exit 0 even without
    // LFS, so `lfs version` is the reliable presence check.
    if (runWithTimeout({"git", "lfs", "version"}, 5000) != 0)
    {
        // Do NOT cache: user might install git-lfs and retry.
        return { false, "git-lfs", platformLfsInstallHint() };
    }

    result.ok = true;
    gitPreflightCached_ = result;
    return result;
}

void SettingsPage::cloneHfRepoInThread()
{
    if (modelInstallBusy_.load())
        return;

    auto pf = gitPreflight();
    if (!pf.ok)
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            pf.missingTool + " not found",
            "T5ynth needs `" + pf.missingTool + "` on the PATH to clone this "
            "model from HuggingFace, but the preflight check failed.\n\n"
            + pf.installHint
            + "\n\nAfter installing, click 'Clone via git' again.");
        return;
    }

    auto modelId       = selectedModelId();
    auto hfRepo        = selectedHfRepo();
    auto modelDisplay  = selectedModelDisplay();
    auto targetDir     = getAppSupportModelDir(modelId);

    // Clone into a sibling tempdir, then atomic-rename to targetDir on
    // success. This way a failed clone never leaves a half-populated
    // model dir that hasModelMarker would have to puzzle over, and a
    // user re-clicking the button gets a clean retry.
    auto cloningParent = targetDir.getParentDirectory();
    auto tempDir       = cloningParent.getChildFile(targetDir.getFileName() + ".cloning");

    setModelInstallBusy(true,
        "Cloning " + modelDisplay + " from HuggingFace via git...\n"
        "This downloads ~1.2 GB and can take a few minutes.");

    juce::Component::SafePointer<SettingsPage> safeThis(this);
    std::thread([safeThis, hfRepo, tempDir, targetDir, modelDisplay]()
    {
        juce::String errorTitle;
        juce::String errorBody;

        // Make sure no stale .cloning dir survives from a previous attempt.
        tempDir.deleteRecursively();
        if (!tempDir.getParentDirectory().createDirectory())
        {
            errorTitle = "Could not prepare clone directory";
            errorBody  = "T5ynth could not create:\n  "
                       + tempDir.getParentDirectory().getFullPathName()
                       + "\n\nCheck folder permissions and try again.";
        }
        else
        {
            // GIT_TERMINAL_PROMPT=0 — without this, a missing credential
            // helper triggers an interactive prompt on the controlling
            // terminal, which a GUI app does not have, and the process
            // hangs forever. Setting the env var to 0 converts the prompt
            // into an immediate non-zero exit.
            //
            // On POSIX we use the `env` command to set it for the child
            // only. On Windows `env.exe` ships inside Git for Windows'
            // usr/bin but is NOT on PATH after a default install — so the
            // env wrapper itself wouldn't be found. Instead on Windows we
            // skip the wrapper and rely on Git Credential Manager (the
            // default helper) to pop its own GUI dialog if auth is
            // missing — exactly what a GUI app wants.
            //
            // --depth 1 keeps the clone shallow (no commit history). For
            // model repos this trims tens of MB of pack data the user
            // doesn't need; the LFS payload is unaffected.
            //
            // --filter=blob:none would be even leaner but breaks the LFS
            // smudge filter on some git versions — keep it off.
            const juce::String url = "https://huggingface.co/" + hfRepo;
            juce::ChildProcess proc;
           #if JUCE_WINDOWS
            const juce::StringArray args {
                "git", "clone", "--depth", "1",
                url, tempDir.getFullPathName()
            };
           #else
            const juce::StringArray args {
                "env", "GIT_TERMINAL_PROMPT=0",
                "git", "clone", "--depth", "1",
                url, tempDir.getFullPathName()
            };
           #endif

            if (!proc.start(args, juce::ChildProcess::wantStdOut
                                  | juce::ChildProcess::wantStdErr))
            {
                errorTitle = "Could not start git";
                errorBody  = "T5ynth could not launch git. The preflight check "
                             "passed but spawning the process failed.";
            }
            else if (!proc.waitForProcessToFinish(10 * 60 * 1000))  // 10 min cap
            {
                proc.kill();
                errorTitle = "Clone timed out";
                errorBody  = "git clone did not finish within 10 minutes. The "
                             "connection may be unusably slow, or git-lfs is "
                             "stuck on a large blob.\n\nRetry with a better "
                             "connection.";
            }
            else if (proc.getExitCode() != 0)
            {
                // git writes both progress (stdout) and errors (stderr)
                // — we asked for both, so readAllProcessOutput is the
                // merged stream. Show the user the LAST few lines (where
                // the actual error usually lives, after the progress
                // spinner) directly in the dialog rather than pointing at
                // a log file that doesn't contain git's output.
                const auto fullOut = proc.readAllProcessOutput().trim();
                juce::StringArray lines;
                lines.addLines(fullOut);
                while (lines.size() > 0 && lines[0].trim().isEmpty())
                    lines.remove(0);
                while (lines.size() > 0 && lines[lines.size() - 1].trim().isEmpty())
                    lines.remove(lines.size() - 1);
                const int tailCount = juce::jmin(lines.size(), 8);
                juce::String tail;
                for (int i = lines.size() - tailCount; i < lines.size(); ++i)
                    tail += "  " + lines[i] + "\n";
                if (tail.isEmpty())
                    tail = "  (no output captured)\n";

                errorTitle = "git clone failed";
                errorBody  = "git exited with status " + juce::String(proc.getExitCode())
                           + ".\n\nLast lines of git output:\n" + tail
                           + "\nCommon causes:\n"
                             "  - Not logged in: run `huggingface-cli login` once.\n"
                             "  - License not accepted: click 'Open Model Page'\n"
                             "    above and click 'Agree and access repository'.\n"
                             "  - git-lfs not registered: run `git lfs install`.\n"
                             "  - Network failure mid-clone: retry.";
                juce::Logger::writeToLog("git clone failed: " + fullOut);
                tempDir.deleteRecursively();
            }
            else if (!hasModelMarker(tempDir))
            {
                // Clone returned success but the safetensors weights are too
                // small — almost always means git-lfs didn't smudge the
                // pointer files. The preflight check covers the install
                // case; this branch catches "lfs installed but not
                // registered for this user" (the `git lfs install` step
                // some installers skip).
                errorTitle = "Clone finished but weights are missing";
                errorBody  = "git completed without error, but the model.safetensors "
                             "file is too small to be the real weights. This usually "
                             "means git-lfs is installed but not registered for your "
                             "user account.\n\nFix with one terminal command:\n"
                             "  git lfs install\n\n"
                             "Then click 'Clone via git' again.";
                tempDir.deleteRecursively();
            }
            else
            {
                // Atomic-ish replace: rename the .cloning sibling onto the
                // final path. juce::File::moveFileTo handles both creation
                // and overwrite on POSIX; on Windows the target must not
                // exist, so we delete first.
                if (targetDir.exists())
                    targetDir.deleteRecursively();
                if (!tempDir.moveFileTo(targetDir))
                {
                    errorTitle = "Could not move clone into place";
                    errorBody  = "Clone succeeded into:\n  " + tempDir.getFullPathName()
                               + "\n\nBut renaming to the final location failed:\n  "
                               + targetDir.getFullPathName()
                               + "\n\nMove the clone manually with Finder/Explorer.";
                }
            }
        }

        juce::MessageManager::callAsync(
            [safeThis, targetDir, modelDisplay, errorTitle, errorBody]()
            {
                auto* self = safeThis.getComponent();
                if (self == nullptr) return;

                self->setModelInstallBusy(false);

                if (errorTitle.isNotEmpty())
                {
                    self->downloadStatusLabel.setText("git clone failed",
                                                      juce::dontSendNotification);
                    self->downloadStatusLabel.setColour(juce::Label::textColourId,
                                                        juce::Colour(0xffef4444));
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        errorTitle, errorBody);
                    return;
                }

                self->downloadStatusLabel.setText("Cloned. Activating...",
                                                   juce::dontSendNotification);
                self->downloadStatusLabel.setColour(juce::Label::textColourId,
                                                    juce::Colour(0xff4ade80));

                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon,
                    modelDisplay + " -- Installed",
                    "T5ynth cloned the model into:\n  "
                        + targetDir.getFullPathName()
                        + "\n\nReady to use.",
                    "OK",
                    self,
                    juce::ModalCallbackFunction::create(
                        [safeThis, targetDir](int)
                        {
                            if (auto* page = safeThis.getComponent())
                                page->setModelPath(targetDir);
                        }));
            });
    }).detach();
}

void SettingsPage::performAutoScan()
{
    if (modelInstallBusy_.load())
        return;

    // 1. Already installed anywhere we recognise?
    auto found = scanForModel();
    if (found.exists())
    {
        juce::File activeDir;
        auto importResult = importModelDirectoryForId(selectedModelId(), found, activeDir, true);
        if (importResult.failed())
        {
            updateStatus();
            downloadStatusLabel.setText("Model import failed", juce::dontSendNotification);
            downloadStatusLabel.setColour(juce::Label::textColourId,
                                          juce::Colour(0xffef4444));
            setInstructionsText(instructionsLabel, importResult.getErrorMessage());
            return;
        }

        setModelPath(activeDir);
        downloadStatusLabel.setText("Model imported: " + activeDir.getFullPathName(),
                                    juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId,
                                      juce::Colour(0xff4ade80));
        return;
    }

    // 2. Model-specific smart scan. Two manual-install categories:
    //    - Native Stability (2-file diffusion model checkpoint + config)
    //    - Flat-transformers encoder (T5Gemma: config + tokenizer + weights)
    // The first uses tryNativeStabilityInstallFromFolder with its hardcoded
    // 2-file list and "wrong files" detection; the second uses
    // tryFlatEncoderInstallFromFolder with a per-model file list.
    auto modelId = selectedModelId();
    const bool isNativeStabilityModel =
        modelId == "stable-audio-open-small"
        || modelId == "stable-audio-open-1.0"
        || modelId == "stable-audio-3-small-music"
        || modelId == "stable-audio-3-small-sfx";
    const bool isFlatEncoderModel =
        modelId == "t5gemma-b-b-ul2";

    if (!isNativeStabilityModel && !isFlatEncoderModel)
    {
        updateStatus();
        downloadStatusLabel.setText(
            "No model found in standard locations. Follow the instructions below.",
            juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId,
                                      juce::Colour(0xffef4444));
        return;
    }
    juce::String modelDisplayName;
    if      (modelId == "stable-audio-open-1.0")     modelDisplayName = "Stable Audio Open 1.0";
    else if (modelId == "stable-audio-open-small")    modelDisplayName = "Stable Audio Open Small";
    else if (modelId == "stable-audio-3-small-music") modelDisplayName = "Stable Audio 3 Small Music";
    else if (modelId == "stable-audio-3-small-sfx")   modelDisplayName = "Stable Audio 3 Small SFX";
    else                                              modelDisplayName = "T5Gemma text encoder";

    // Pick the right required-files list and install helper for the category.
    // A small lambda keeps the rest of the function category-agnostic so the
    // Downloads-then-picker fallback stays a single code path.
    const char* const* requiredFiles = nullptr;
    int                numRequiredFiles = 0;
    if (isNativeStabilityModel)
    {
        requiredFiles    = kNativeStabilityRequired;
        numRequiredFiles = kNumNativeStabilityRequired;
    }
    else
    {
        requiredFiles    = kT5GemmaRequiredFiles;
        numRequiredFiles = kNumT5GemmaRequiredFiles;
    }

    auto tryInstall = [this, isNativeStabilityModel, modelId, modelDisplayName,
                       requiredFiles, numRequiredFiles]
                      (const juce::File& folder, bool reportIfMissing)
    {
        if (isNativeStabilityModel)
            return tryNativeStabilityInstallFromFolder(
                folder, modelId, modelDisplayName, reportIfMissing);
        return tryFlatEncoderInstallFromFolder(
            folder, modelId, modelDisplayName,
            requiredFiles, numRequiredFiles, reportIfMissing);
    };

    // 3. Look in the system Downloads folder first.
    auto downloads = getDownloadsFolder();
    auto outcome = tryInstall(downloads, /*reportIfMissing*/ false);
    if (outcome == InstallOutcome::Installed
        || outcome == InstallOutcome::AbortedWithDialog)
        return;  // dialog (success OR duplicate-guard) already shown

    // Re-run the check to see *why* Downloads didn't work (missing / wrong /
    // nothing), so we can decide whether to nag or to open the picker.
    // Use the same browser-rename-aware lookup as the install path so a
    // user-renamed file (model-2.safetensors, model (1).safetensors) counts
    // as "present" here too — otherwise the picker would pop up for a folder
    // the file IS in, just under a slightly different name.
    int foundInDownloads = 0;
    for (int i = 0; i < numRequiredFiles; ++i)
        if (pickNewestCandidate(downloads, requiredFiles[i]).existsAsFile())
            ++foundInDownloads;

    // Scenario (a): some files present in Downloads — tell the user which
    // are still missing instead of opening a picker.
    if (foundInDownloads > 0)
    {
        tryInstall(downloads, /*reportIfMissing*/ true);
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
    juce::Component::SafePointer<SettingsPage> safeThis(this);
    // The category flag + file-list pointer are captured by value; the file
    // list itself lives in static storage (kT5GemmaRequiredFiles /
    // kNativeStabilityRequired) so the pointers stay valid for the lifetime
    // of the async wait.
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis, downloads, isNativeStabilityModel, modelId, modelDisplayName,
         requiredFiles, numRequiredFiles](const juce::FileChooser& fc)
        {
            juce::ignoreUnused(downloads);
            auto* self = safeThis.getComponent();
            if (self == nullptr) return;
            auto folder = fc.getResult();
            if (folder == juce::File())
            {
                // Cancelled — leave a trail so the user knows nothing happened.
                self->downloadStatusLabel.setText(
                    "Auto-Scan cancelled. Follow the instructions below.",
                    juce::dontSendNotification);
                self->downloadStatusLabel.setColour(juce::Label::textColourId,
                                                    juce::Colour(0xffef4444));
                return;
            }
            if (isNativeStabilityModel)
                self->tryNativeStabilityInstallFromFolder(
                    folder, modelId, modelDisplayName, /*reportIfMissing*/ true);
            else
                self->tryFlatEncoderInstallFromFolder(
                    folder, modelId, modelDisplayName,
                    requiredFiles, numRequiredFiles, /*reportIfMissing*/ true);
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
        juce::Component::SafePointer<SettingsPage> safeThis(this);
        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::InfoIcon,
            juce::String(km.displayName) + " -- License",
            juce::String(km.licenseNotice) + "\n\nFull license:\n" + licenseUrl,
            "Accept & Download", "Cancel", this,
            juce::ModalCallbackFunction::create([safeThis](int result) {
                if (result == 1) {
                    if (auto* self = safeThis.getComponent())
                    {
                        self->licenseAccepted_ = true;
                        self->startDownload();  // re-enter, this time skips dialog
                    }
                }
            }));
        return;  // async — startDownload will be called again from callback
    }
    licenseAccepted_ = false;  // reset for next time

    auto ghRelease = selectedGhRelease();

    downloading = true;
    downloadButton.setEnabled(false);
    gitCloneButton.setEnabled(false);
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
    juce::Component::SafePointer<SettingsPage> safeThis(this);
    std::thread([safeThis, hfRepo, modelId]() {
        juce::URL apiUrl("https://huggingface.co/api/models/" + hfRepo + "/tree/main?recursive=true");
        auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                        .withConnectionTimeoutMs(15000);
        auto stream = apiUrl.createInputStream(opts);
        if (!stream) {
            juce::MessageManager::callAsync([safeThis, hfRepo]() {
                if (auto* self = safeThis.getComponent())
                    self->onDownloadFinished(false,
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
                juce::MessageManager::callAsync([safeThis, errMsg, hfRepo]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false,
                            "HuggingFace API error for " + hfRepo + ":\n\n" + errMsg);
                });
                return;
            }

            std::vector<DownloadFile> files;
            std::set<std::string> allPaths;
            for (auto& item : json)
            {
                if (item.value("type", "") != "file")
                    continue;
                allPaths.insert(item["path"].get<std::string>());
            }

            int64_t total = 0;
            for (auto& item : json) {
                if (item.value("type", "") != "file") continue;
                DownloadFile df;
                df.remotePath = juce::String(item["path"].get<std::string>());
                if (!shouldDownloadHfFile(df.remotePath, allPaths))
                    continue;
                df.size = item.value("size", (int64_t)0);
                total += df.size;
                files.push_back(df);
            }
            juce::MessageManager::callAsync([safeThis, files, total, modelId]() {
                if (auto* self = safeThis.getComponent())
                {
                    self->filesToDownload = files;
                    self->totalBytes = total;
                    self->downloadedBytes = 0;
                    self->downloadStatusLabel.setText(juce::String(self->filesToDownload.size()) + " files, "
                        + juce::String(static_cast<double>(self->totalBytes) / (1024.0 * 1024.0), 0) + " MB",
                        juce::dontSendNotification);
                    auto targetDir = getAppSupportModelDir(modelId);
                    targetDir.createDirectory();
                    self->cleanupBadFiles(targetDir);
                    self->downloadAllFilesInThread();
                }
            });
        } catch (const std::exception& e) {
            auto err = juce::String(e.what());
            juce::MessageManager::callAsync([safeThis, err, hfRepo]() {
                if (auto* self = safeThis.getComponent())
                    self->onDownloadFinished(false,
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

    const auto* ghFiles = static_cast<const GhAsset*>(selectedGhFiles());
    int ghFileCount = selectedGhFileCount();

    if (ghFiles == nullptr || ghFileCount <= 0)
    {
        // Catalog row has ghRelease set but no ghFiles list — programmer error.
        onDownloadFinished(false,
            "Internal error: model is configured for GitHub-release download "
            "but has no file list defined in the SetupWizard catalog.");
        return;
    }

    int64_t total = 0;
    for (int i = 0; i < ghFileCount; ++i) total += ghFiles[i].expectedSize;
    totalBytes = total;
    downloadedBytes = 0;
    downloadCounter_ = std::make_shared<std::atomic<int64_t>>(0);
    downloadCancelFlag_ = std::make_shared<std::atomic<bool>>(false);
    startTimer(250);

    juce::Component::SafePointer<SettingsPage> safeThis(this);
    auto progressCounter = downloadCounter_;
    auto cancelFlag = downloadCancelFlag_;
    std::thread([safeThis, progressCounter, cancelFlag, ghBase, targetDir,
                 ghFiles, ghFileCount]()
    {
        int64_t bytesCompleted = 0;

        for (int i = 0; i < ghFileCount; ++i)
        {
            if (cancelFlag && cancelFlag->load())
                return;

            auto& gf = ghFiles[i];
            auto fileName = juce::String(gf.name);
            auto targetFile = targetDir.getChildFile(fileName);

            // Resume-aware existing-file check:
            //   == expectedSize      → already complete, skip
            //   > expectedSize       → not our file (different version) — start fresh
            //   in (0, expectedSize) → partial — request HTTP Range and append
            //   == 0                 → fresh download
            int64_t existingBytes = targetFile.existsAsFile() ? targetFile.getSize() : 0;
            if (existingBytes == gf.expectedSize)
            {
                bytesCompleted += gf.expectedSize;
                if (progressCounter) progressCounter->store(bytesCompleted);
                continue;
            }
            if (existingBytes > gf.expectedSize)
            {
                targetFile.deleteFile();
                existingBytes = 0;
            }
            const bool tryResume = (existingBytes > 0);

            auto fileNum = i + 1;
            juce::String statusText = (tryResume ? juce::String("Resuming: ")
                                                 : juce::String("Downloading: "))
                + fileName + " (" + juce::String(fileNum) + "/"
                + juce::String(ghFileCount) + ")";
            juce::MessageManager::callAsync([safeThis, statusText]() {
                if (auto* self = safeThis.getComponent())
                    self->downloadStatusLabel.setText(statusText, juce::dontSendNotification);
            });

            juce::URL fileUrl(ghBase + "/" + fileName);
            juce::String extraHeaders;
            if (tryResume)
                extraHeaders = "Range: bytes=" + juce::String(existingBytes) + "-";

            int statusCode = 0;
            auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                            .withConnectionTimeoutMs(30000)
                            .withExtraHeaders(extraHeaders)
                            .withStatusCode(&statusCode);
            auto stream = fileUrl.createInputStream(opts);

            if (!stream)
            {
                juce::MessageManager::callAsync([safeThis, fileName]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false, "Connection failed for: " + fileName
                            + "\n\nCheck your internet connection.");
                });
                return;
            }

            // 416 Range Not Satisfiable: the file on the server is smaller
            // than our partial copy (mirror re-uploaded with different size,
            // e.g.). Clear the partial so the next click starts fresh —
            // otherwise we'd send the same Range again and re-trigger 416.
            if (statusCode == 416 && tryResume)
            {
                targetFile.deleteFile();
                juce::MessageManager::callAsync([safeThis, fileName]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false,
                            "The file on the server changed since your previous "
                            "download attempt: " + fileName + "\n\n"
                            "Your partial download has been cleared. Click "
                            "Download again to start fresh.");
                });
                return;
            }

            if (statusCode >= 400)
            {
                juce::MessageManager::callAsync([safeThis, fileName, statusCode]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false, "Server returned HTTP "
                            + juce::String(statusCode) + " for: " + fileName);
                });
                return;
            }

            // 206 Partial Content → append. 200 OK with tryResume → server
            // ignored Range, restart from byte 0.
            const bool appendMode = tryResume && statusCode == 206;
            if (!appendMode)
                targetFile.deleteFile();

            auto outStream = targetFile.createOutputStream();
            if (!outStream)
            {
                juce::MessageManager::callAsync([safeThis, fileName]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false, "Cannot write: " + fileName);
                });
                return;
            }
            // FileOutputStream's constructor seeks to end-of-file when the
            // file already exists, so the write cursor is already correct
            // for appendMode == true.

            const int64_t initialBytes = appendMode ? existingBytes : 0;
            char buffer[65536];
            int64_t written = 0;
            while (true)
            {
                if (cancelFlag && cancelFlag->load())
                    return;
                auto bytesRead = stream->read(buffer, sizeof(buffer));
                if (bytesRead <= 0) break;
                outStream->write(buffer, static_cast<size_t>(bytesRead));
                written += bytesRead;
                if (progressCounter) progressCounter->store(bytesCompleted + initialBytes + written);
            }
            outStream.reset();

            // Skip the truncation check if cancellation interrupted the read.
            if (cancelFlag && cancelFlag->load())
                return;

            // Network dropped mid-stream. Keep the partial file so the next
            // click resumes from where it stopped — don't delete it.
            const int64_t finalBytes = targetFile.getSize();
            if (finalBytes < gf.expectedSize)
            {
                auto expectedStr = (gf.expectedSize < 1024 * 1024)
                    ? juce::String(gf.expectedSize) + " bytes"
                    : juce::String(gf.expectedSize / (1024 * 1024)) + " MB";
                auto gotStr = (finalBytes < 1024 * 1024)
                    ? juce::String(finalBytes) + " bytes"
                    : juce::String(finalBytes / (1024 * 1024)) + " MB";
                juce::MessageManager::callAsync([safeThis, fileName, expectedStr, gotStr]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false,
                            "Download was interrupted for " + fileName + ":\n"
                            "Expected " + expectedStr + ", received " + gotStr + ".\n\n"
                            "Click Download again to resume from where it stopped.");
                });
                return;
            }

            bytesCompleted += gf.expectedSize;
            if (progressCounter) progressCounter->store(bytesCompleted);
        }

        juce::MessageManager::callAsync([safeThis]() {
            if (auto* self = safeThis.getComponent())
                self->onDownloadFinished(true, {});
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

    downloadCounter_ = std::make_shared<std::atomic<int64_t>>(0);
    downloadCancelFlag_ = std::make_shared<std::atomic<bool>>(false);
    startTimer(250);  // timer updates progress bar from atomic downloadedBytes

    juce::Component::SafePointer<SettingsPage> safeThis(this);
    auto progressCounter = downloadCounter_;
    auto cancelFlag = downloadCancelFlag_;
    std::thread([safeThis, progressCounter, cancelFlag, hfRepo, targetDir, files]()
    {
        int64_t bytesCompleted = 0;

        for (size_t i = 0; i < files.size(); ++i)
        {
            if (cancelFlag && cancelFlag->load())
                return;

            auto& df = files[i];
            auto targetFile = targetDir.getChildFile(df.remotePath);
            targetFile.getParentDirectory().createDirectory();

            // Resume-aware existing-file check:
            //   == df.size           → already complete, skip
            //   > df.size            → different version — start fresh
            //   in (0, df.size)      → partial — request HTTP Range and append
            //   == 0 OR LFS pointer  → fresh download
            //   df.size == 0         → HF didn't report a size; can't reason
            //                          about resume — always restart and skip
            //                          end-of-loop truncation check.
            int64_t existingBytes = 0;
            if (targetFile.existsAsFile() && !isLfsPointer(targetFile))
                existingBytes = targetFile.getSize();

            if (df.size > 0 && existingBytes == df.size)
            {
                bytesCompleted += df.size;
                if (progressCounter) progressCounter->store(bytesCompleted);
                continue;
            }
            if (df.size > 0 && existingBytes > df.size)
            {
                targetFile.deleteFile();
                existingBytes = 0;
            }
            const bool tryResume = (df.size > 0 && existingBytes > 0);

            // Update status on UI thread
            auto fileName = df.remotePath;
            auto fileNum = i + 1;
            auto fileCount = files.size();
            juce::String statusText = (tryResume ? juce::String("Resuming: ")
                                                 : juce::String("Downloading: "))
                + fileName + " (" + juce::String(fileNum) + "/"
                + juce::String((int) fileCount) + ")";
            juce::MessageManager::callAsync([safeThis, statusText]() {
                if (auto* self = safeThis.getComponent())
                    self->downloadStatusLabel.setText(statusText, juce::dontSendNotification);
            });

            // Download via createInputStream — follows HF's LFS redirects
            juce::URL fileUrl("https://huggingface.co/" + hfRepo + "/resolve/main/" + fileName);
            juce::String extraHeaders;
            if (tryResume)
                extraHeaders = "Range: bytes=" + juce::String(existingBytes) + "-";

            int statusCode = 0;
            auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                            .withConnectionTimeoutMs(30000)
                            .withExtraHeaders(extraHeaders)
                            .withStatusCode(&statusCode);
            auto stream = fileUrl.createInputStream(opts);

            if (!stream)
            {
                juce::MessageManager::callAsync([safeThis, fileName, hfRepo]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false,
                            "Could not open:\n"
                            "  https://huggingface.co/" + hfRepo + "/resolve/main/" + fileName
                            + "\n\nCheck your network connection.");
                });
                return;
            }

            // 416 Range Not Satisfiable: partial-file size exceeds the file
            // on HF (e.g. the model was reuploaded). Clear partial so the
            // next click starts fresh.
            if (statusCode == 416 && tryResume)
            {
                targetFile.deleteFile();
                juce::MessageManager::callAsync([safeThis, fileName, hfRepo]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false,
                            "The file on HuggingFace (" + hfRepo + ") changed "
                            "since your previous download attempt: " + fileName
                            + "\n\nYour partial download has been cleared. "
                            "Click Download again to start fresh.");
                });
                return;
            }

            if (statusCode >= 400)
            {
                juce::MessageManager::callAsync([safeThis, fileName, statusCode, hfRepo]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false, "HuggingFace returned HTTP "
                            + juce::String(statusCode) + " for " + fileName
                            + " (" + hfRepo + ")");
                });
                return;
            }

            // 206 → server honored Range → append.
            // 200 with tryResume → server ignored Range, full file incoming.
            const bool appendMode = tryResume && statusCode == 206;
            if (!appendMode)
                targetFile.deleteFile();

            auto outStream = targetFile.createOutputStream();
            if (!outStream)
            {
                juce::MessageManager::callAsync([safeThis, fileName]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false, "Cannot write: " + fileName);
                });
                return;
            }
            // FileOutputStream auto-seeks to end on existing files; no
            // explicit positioning needed for appendMode.

            const int64_t initialBytes = appendMode ? existingBytes : 0;
            char buffer[65536];
            int64_t written = 0;
            while (!(cancelFlag && cancelFlag->load()))
            {
                auto bytesRead = stream->read(buffer, sizeof(buffer));
                if (bytesRead <= 0) break;
                outStream->write(buffer, static_cast<size_t>(bytesRead));
                written += bytesRead;
                if (progressCounter) progressCounter->store(bytesCompleted + initialBytes + written);
            }
            outStream.reset();

            if (cancelFlag && cancelFlag->load())
                return;

            // Check for server error responses (HTML/JSON error instead of
            // data). Only relevant in non-append mode: a 206 Partial Content
            // body is required to be the raw bytes from the requested range,
            // so a small `written` while resuming is just a tail-end chunk.
            if (!appendMode && written > 0 && written < 100000 && df.size > 100000)
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
                    juce::MessageManager::callAsync([safeThis, fileName, serverMsg, hfRepo]() {
                        if (auto* self = safeThis.getComponent())
                            self->onDownloadFinished(false,
                                "HuggingFace rejected download of " + fileName + " from "
                                + hfRepo + ":\n\n" + serverMsg);
                    });
                    return;
                }
            }

            // Truncation check against HF-API-reported size. Read the bytes
            // actually on disk (not the in-memory `written` counter) so a
            // failed write (e.g. disk-full) is detected — outStream->write
            // returns success-as-void and the byte counter advances even when
            // the OS rejected the data.
            const int64_t finalBytes = targetFile.getSize();
            if (df.size > 0 && finalBytes < df.size)
            {
                auto expected = df.size;
                auto expectedStr = (expected < 1024 * 1024)
                    ? juce::String(expected / 1024) + " KB"
                    : juce::String(expected / (1024 * 1024)) + " MB";
                auto gotStr = (finalBytes < 1024 * 1024)
                    ? juce::String(finalBytes / 1024) + " KB"
                    : juce::String(finalBytes / (1024 * 1024)) + " MB";
                juce::MessageManager::callAsync([safeThis, fileName, expectedStr, gotStr, hfRepo]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false,
                            "Transfer ended early for " + fileName + " (" + hfRepo + ")\n"
                            "Expected " + expectedStr + ", received " + gotStr + ".\n\n"
                            "Click Download again to resume from where it stopped, "
                            "or use Browse... to point at an existing copy of the model.");
                });
                return;
            }

            bytesCompleted += (df.size > 0 ? df.size : finalBytes);
            if (progressCounter) progressCounter->store(bytesCompleted);
        }

        juce::MessageManager::callAsync([safeThis]() {
            if (auto* self = safeThis.getComponent())
                self->onDownloadFinished(true, {});
        });
    }).detach();
}

void SettingsPage::timerCallback()
{
    if (!downloading) { stopTimer(); return; }
    if (downloadCounter_)
        downloadedBytes.store(downloadCounter_->load());
    if (totalBytes > 0)
        downloadProgress = static_cast<double>(downloadedBytes.load()) / static_cast<double>(totalBytes);
}

void SettingsPage::onDownloadFinished(bool success, const juce::String& error)
{
    stopTimer();
    downloading = false;
    if (downloadCancelFlag_)
        downloadCancelFlag_->store(true);
    downloadCounter_.reset();
    downloadCancelFlag_.reset();
    downloadButton.setEnabled(true);
    gitCloneButton.setEnabled(true);
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

        downloadStatusLabel.setText("Download complete. Starting backend...", juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId, kDim);
        auto found = scanForModel();
        if (found.exists()) setModelPath(found);
    } else {
        downloadStatusLabel.setText("Download failed", juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffef4444));
        // Show full error in the multi-line instructions area
        setInstructionsText(instructionsLabel, error);
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
    if (connected && modelPath.exists())
    {
        downloadStatusLabel.setText("Model active.", juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff4ade80));
        progressBar.setVisible(false);
    }
    updateStatus();
}

void SettingsPage::setBackendStarting()
{
    backendConnected = false;
    backendFailReason = {};
    backendStatusLabel.setText("Backend: Starting...", juce::dontSendNotification);
    backendStatusLabel.setColour(juce::Label::textColourId, kDim);
    if (!downloading.load() && modelPath.exists())
    {
        downloadStatusLabel.setText("Starting backend...", juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId, kDim);
    }
    updateStatus();
}

void SettingsPage::setBackendFailed(const juce::String& reason)
{
    backendConnected = false;
    backendFailReason = reason;
    backendStatusLabel.setText("Backend: Start failed", juce::dontSendNotification);
    backendStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffef4444));
    progressBar.setVisible(false);
    downloadStatusLabel.setText("Activation failed: " + firstErrorLine(reason),
                                juce::dontSendNotification);
    downloadStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffef4444));
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
    bool gitCloneable = selectedGitCloneable();

    // Download button is only shown for models T5ynth can fetch itself.
    // git-clone button is shown only when the model is gated AND has
    // generic filenames that make the file-list Auto-Scan unsafe
    // (T5Gemma); the two buttons are mutually exclusive in practice.
    downloadButton.setVisible(downloadable);
    gitCloneButton.setVisible(gitCloneable);

    if (modelPath.exists() && hasModelMarker(modelPath)) {
        const bool isEngine = selectedIsGenerationEngine();
        modelPathLabel.setText(modelPath.getFullPathName(), juce::dontSendNotification);
        if (backendConnected) {
            modelStatusLabel.setText(id + ": Installed", juce::dontSendNotification);
            modelStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff4ade80));
            setInstructionsText(instructionsLabel,
                isEngine ? "Ready to generate audio."
                         : "Text encoder installed.");
        } else if (backendFailReason.isNotEmpty()) {
            modelStatusLabel.setText(id + ": Files found, not active", juce::dontSendNotification);
            modelStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xfffbbf24));  // amber
            setInstructionsText(
                instructionsLabel,
                "Model files were found, but backend startup failed while validating or loading a model.\n\n"
                "Model path:\n  " + modelPath.getFullPathName() + "\n\n"
                "Backend error:\n  " + backendFailReason + "\n\n"
                "This usually means the model download is incomplete, incompatible, or the app bundle "
                "could not load one of its own libraries.\n\n"
                "Backend log:\n  " + backendStderrLogPath());
        } else {
            modelStatusLabel.setText(id + ": Installed", juce::dontSendNotification);
            modelStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff4ade80));
            setInstructionsText(instructionsLabel,
                isEngine ? "Model files found. Starting backend..."
                         : "Text encoder installed.");
        }
    } else {
        modelStatusLabel.setText(id + ": Not installed", juce::dontSendNotification);
        modelStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xffef4444));
        modelPathLabel.setText("", juce::dontSendNotification);

        // Per-model honest instructions. AudioLDM2 is the only model T5ynth
        // can fetch itself; the two Stability models are gated on HF and use
        // a two-file manual install.
        auto targetPath = targetDir.getFullPathName();

        if (id == "audioldm2") {
            setInstructionsText(
                instructionsLabel,
                "AUDIOLDM2\n"
                "Academic latent-diffusion text-to-audio model published by CVSSP / "
                "University of Surrey and collaborators (Liu et al., 2023), released "
                "as an open research artefact for studying generalised audio, music "
                "and speech generation from text. Ungated on HuggingFace and the only "
                "engine T5ynth can install directly. Click 'Download from HuggingFace' "
                "above and wait for the download to finish.\n\n"
                "  Source: https://huggingface.co/" + hfRepo + "\n"
                "  Target: " + targetPath + "\n\n"
                "License: CC BY-NC-SA 4.0 -- non-commercial use only, no revenue "
                "threshold, no exceptions.");
        } else if (id == "stable-audio-open-small") {
            setInstructionsText(
                instructionsLabel,
                "STABLE AUDIO OPEN SMALL\n"
                "Licensed under the Stability AI Community License. Gated on "
                "HuggingFace -- a free HuggingFace account is required once to "
                "accept the license and download the files. No terminal required.\n\n"
                "  Source: https://huggingface.co/" + hfRepo + "\n\n"
                "INSTALL:\n"
                "  1. Click 'Open Model Page' above. Your browser opens the\n"
                "     HuggingFace page for this model.\n"
                "  2. On that page, sign up or log in (top-right corner).\n"
                "  3. Click 'Agree and access repository' to accept the license.\n"
                "  4. On the same page, click the 'Files and versions' tab.\n"
                "  5. Download exactly these two files to your usual Downloads\n"
                "     folder (one click each on the filename, then the download\n"
                "     icon on the right):\n"
                "        model.safetensors\n"
                "        model_config.json\n"
                "     Do not download model.ckpt, base_model.ckpt,\n"
                "     base_model.safetensors, or base_model_config.json --\n"
                "     they are alternative formats T5ynth does not use.\n"
                "  6. Come back here and click 'Auto-Scan' above.\n"
                "     T5ynth finds the files in your Downloads folder and copies\n"
                "     them into its working model folder. You can delete the originals\n"
                "     from Downloads afterwards.\n\n"
                "If you saved them somewhere other than Downloads, Auto-Scan will "
                "open a folder picker and ask you to point at the folder.");
        } else if (id == "stable-audio-3-small-music" || id == "stable-audio-3-small-sfx") {
            const juce::String variantTitle =
                (id == "stable-audio-3-small-music") ? "STABLE AUDIO 3 SMALL MUSIC"
                                                     : "STABLE AUDIO 3 SMALL SFX";
            const juce::String variantBlurb =
                (id == "stable-audio-3-small-music")
                    ? "Trained for musical content."
                    : "Trained for sound effects (companion to SA3 Small Music; "
                      "same architecture, different fine-tune).";
            setInstructionsText(
                instructionsLabel,
                variantTitle + "\n"
                + variantBlurb + "\n\n"
                "Licensed under the Stability AI Community License. Gated on "
                "HuggingFace -- a free HuggingFace account is required once to "
                "accept the license. T5ynth uses only two files from this repo "
                "(model.safetensors and model_config.json).\n\n"
                "  Source: https://huggingface.co/" + hfRepo + "\n\n"
                "SA3 Small requires the T5Gemma text encoder. Install T5Gemma\n"
                "first (select it in the dropdown above) -- without that encoder\n"
                "the backend cannot load SA3.\n\n"
                "INSTALL:\n"
                "  1. Click 'Open Model Page' above, sign up or log in, and click\n"
                "     'Agree and access repository' to accept the license.\n"
                "  2. Open the 'Files and versions' tab.\n"
                "  3. Download exactly these two files to your usual Downloads\n"
                "     folder:\n"
                "        model.safetensors\n"
                "        model_config.json\n"
                "  4. Come back here and click 'Auto-Scan' above. T5ynth finds\n"
                "     the files in Downloads and copies them into its working\n"
                "     model folder.\n\n"
                "If you saved them somewhere other than Downloads, Auto-Scan will "
                "open a folder picker and ask you to point at the folder.");
        } else if (id == "t5gemma-b-b-ul2") {
            setInstructionsText(
                instructionsLabel,
                "T5GEMMA TEXT ENCODER (Stable Audio 3)\n"
                "Licensed under the Gemma Terms of Use. Gated on HuggingFace --\n"
                "a free HuggingFace account is required once to accept the terms.\n"
                "T5Gemma is the shared text encoder for both SA3 Small Music\n"
                "and SA3 Small SFX.\n\n"
                "  Source: https://huggingface.co/" + hfRepo + "\n"
                "  Target: " + targetPath + "\n\n"
                "INSTALL (Clone via git):\n"
                "  T5Gemma's filenames (config.json, tokenizer.json,\n"
                "  model.safetensors) collide with other models when downloaded\n"
                "  individually, so T5ynth uses `git clone` instead. The clone\n"
                "  lands in its own named directory with all files intact.\n\n"
                "  1. Click 'Open Model Page' above, sign up or log in, and\n"
                "     click 'Agree and access repository' to accept the Gemma\n"
                "     Terms of Use.\n"
                "  2. If you haven't yet, run once in any terminal:\n"
                "        huggingface-cli login\n"
                "     paste the access token from your HuggingFace settings page,\n"
                "     and answer 'Y' to add it to git credentials. T5ynth itself\n"
                "     never sees this token -- git fetches it from your system\n"
                "     credential store when cloning.\n"
                "  3. Click 'Clone via git' above. T5ynth runs `git clone`\n"
                "     under the hood, downloads ~1.2 GB, and verifies the\n"
                "     weights are real before activating the model.\n\n"
                "Requirements: `git` and `git-lfs` on the PATH. T5ynth checks\n"
                "both before starting the clone and points you at install\n"
                "instructions if either is missing.\n\n"
                "Already have a clone elsewhere? Click 'Browse...' and point at\n"
                "the folder -- T5ynth imports it as a symlink without re-downloading.");
        } else if (id == "t5-base") {
            setInstructionsText(
                instructionsLabel,
                "T5-BASE TEXT ENCODER (Stable Audio Open Small)\n"
                "The original T5 encoder published by Google Research "
                "(Raffel et al., 2020). Stable Audio Open Small uses it to "
                "turn the text prompt into the conditioning embedding the "
                "diffusion model consumes. Ungated and openly licensed under "
                "Apache-2.0 -- no HuggingFace account, no terms to accept. "
                "Click 'Download' above and wait for the download to finish; "
                "T5ynth fetches the GitHub release mirror by default and "
                "transparently falls back to HuggingFace if the mirror is "
                "unreachable. Both paths work without an account.\n\n"
                "  Source: https://huggingface.co/" + hfRepo + "\n"
                "  Target: " + targetPath + "\n\n"
                "Footprint: ~890 MB single safetensors plus tokenizer and "
                "config (config.json, tokenizer.json, spiece.model).\n\n"
                "License: Apache-2.0 -- unrestricted use, commercial or "
                "otherwise.");
        } else {
            // SA 1.0 (and any future gated Stability model)
            setInstructionsText(
                instructionsLabel,
                "STABLE AUDIO OPEN 1.0\n"
                "Licensed under the Stability AI Community License. Gated on "
                "HuggingFace -- a free HuggingFace account is required once to "
                "accept the license. T5ynth uses only two files from this repo "
                "(model.safetensors ~4.9 GB and model_config.json), so you don't "
                "need to download the rest.\n\n"
                "  Source: https://huggingface.co/" + hfRepo + "\n\n"
                "INSTALL:\n"
                "  1. Click 'Open Model Page' above, sign up or log in, and click\n"
                "     'Agree and access repository' to accept the license.\n"
                "  2. Open the 'Files and versions' tab.\n"
                "  3. Download exactly these two files to your usual Downloads\n"
                "     folder:\n"
                "        model.safetensors  (~4.9 GB)\n"
                "        model_config.json\n"
                "     Do not download model.ckpt, vae_model.ckpt, or anything\n"
                "     inside transformer/, vae/, text_encoder/, tokenizer/,\n"
                "     scheduler/, or projection_model/ -- T5ynth does not load them.\n"
                "  4. Come back here and click 'Auto-Scan' above. T5ynth finds\n"
                "     the files in Downloads and copies them into its working\n"
                "     model folder.\n\n"
                "If you saved them somewhere other than Downloads, Auto-Scan will "
                "open a folder picker and ask you to point at the folder.");
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

    // Install action button — at most one of download / git-clone is
    // visible at a time (downloadable / gitCloneable are mutually
    // exclusive per-model). They share the same row so the layout
    // stays stable across model selection changes.
    auto dlRow = area.removeFromTop(26);
    if (downloadButton.isVisible())
        downloadButton.setBounds(dlRow.removeFromLeft(220));
    else if (gitCloneButton.isVisible())
        gitCloneButton.setBounds(dlRow.removeFromLeft(220));
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
