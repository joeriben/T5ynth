#include "SetupWizard.h"
#include "GuiHelpers.h"
#include "BinaryData.h"
#include <nlohmann/json.hpp>
#include <cstring>
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

// One file of a "reassembly" install: an ABSOLUTE source URL (e.g. the Comfy-Org
// ungated weights, or the T5ynt metadata mirror), the canonical path it lands at
// inside the installed model dir (the source path need NOT equal the target path),
// and its published byte size (progress + resume-skip). This is what lets SA3
// install login-free: heavy weights stream from Comfy-Org under their repacked
// names (checkpoints/..., text_encoders/...) into model.safetensors /
// t5gemma-b-b-ul2/model.safetensors, while the small metadata Comfy-Org omits is
// pulled from a T5ynt-owned mirror. Consumed by downloadReassemblyInThread.
struct ReassemblyAsset { const char* sourceUrl; const char* localPath; int64_t expectedSize; };

// Files mirrored at the assets/t5-base-v1 GitHub release tag. Sizes match
// google-t5/t5-base on HuggingFace; used for progress display and the
// "skip if already downloaded" heuristic.
static const GhAsset kT5BaseGhFiles[] = {
    { "config.json",            1208 },
    { "tokenizer.json",      1389353 },
    { "spiece.model",         791656 },
    { "model.safetensors", 891646390 },
};

// Comfy-Org ungated SA3 weights, PINNED to a fixed commit so a silent re-upload
// can never change what users get vs. what was verified (gated=False at this
// revision). Repacked from Stability's gated originals — same architecture, a
// different byte layout, so these are the Comfy-Org sizes (each confirmed by a
// real end-to-end generation), NOT the stabilityai sizes. The t5gemma encoder is
// shared by music+sfx. Everything the repo omits (model_config.json + the t5gemma
// configs + the SentencePiece tokenizer) is supplied at install time by
// finalizeSa3Reassembly (bundled BinaryData + the tokenizer carved from the
// weights) — so SA3 installs with no HuggingFace account, token, or Gemma approval.
#define T5YNTH_COMFY_SA3 \
    "https://huggingface.co/Comfy-Org/stable-audio-3/resolve/a02cbcdcd07426b0150557d0145bc894795823af"
static const ReassemblyAsset kSa3MusicAssets[] = {
    { T5YNTH_COMFY_SA3 "/checkpoints/stable_audio_3_small_music.safetensors",
      "model.safetensors", 2270384940 },
    { T5YNTH_COMFY_SA3 "/text_encoders/t5gemma_b_b_ul2.safetensors",
      "t5gemma-b-b-ul2/model.safetensors", 1187264003 },
};
static const ReassemblyAsset kSa3SfxAssets[] = {
    { T5YNTH_COMFY_SA3 "/checkpoints/stable_audio_3_small_sfx.safetensors",
      "model.safetensors", 2270384940 },
    { T5YNTH_COMFY_SA3 "/text_encoders/t5gemma_b_b_ul2.safetensors",
      "t5gemma-b-b-ul2/model.safetensors", 1187264003 },
};
#undef T5YNTH_COMFY_SA3

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
    // In-repo text-encoder subfolder that must be installed alongside the root
    // weights (e.g. "t5gemma-b-b-ul2" for SA3 Small Music). The default member
    // initializer lets the entries below omit it = self-contained model (and
    // keeps the aggregate warning-clean).
    const char* encoderSubfolder = nullptr;
    // Reassembly install: a fixed list of {absolute source URL, canonical local
    // path, size} fetched directly (no HF account) and reassembled into the model
    // dir. When set, startDownload routes to downloadReassemblyInThread and the
    // single-repo HF/GH paths are bypassed. nullptr = model does not reassemble.
    const ReassemblyAsset* assets = nullptr;
    int assetCount = 0;
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
      nullptr, 0 },
    { "stable-audio-open-small", "Stable Audio Open Small", "stabilityai/stable-audio-open-small",
      nullptr,
      "https://stability.ai/community-license-agreement",
      "This model is licensed under the Stability AI Community License.\n\n"
      "- Non-commercial use: free\n"
      "- Commercial use under $1M annual revenue: free (register at stability.ai)\n"
      "- Commercial use over $1M: enterprise license required\n\n"
      "T5ynth does not provide the model weights. By downloading, you accept\n"
      "the license terms and take responsibility for compliance.", false, true,
      nullptr, 0 },
    { "audioldm2",               "AudioLDM2",                  "cvssp/audioldm2", nullptr,
      "https://creativecommons.org/licenses/by-nc-sa/4.0/",
      "This model is licensed under CC BY-NC-SA 4.0.\n\n"
      "- Non-commercial use only (no revenue threshold, no exceptions)\n"
      "- Commercial use is NOT permitted under this license\n\n"
      "T5ynth does not provide the model weights. By downloading, you accept\n"
      "the license terms and take responsibility for compliance.", true, true,
      nullptr, 0 },
    { "t5-base",                 "T5-Base text encoder",       "t5-base", nullptr,
      "https://www.apache.org/licenses/LICENSE-2.0",
      "T5-base is licensed under Apache License 2.0 (open, no restrictions).\n\n"
      "Required by Stable Audio Open Small as the text encoder. T5ynth does\n"
      "not provide the weights. By downloading you accept the Apache 2.0\n"
      "license.", true, false,
      kT5BaseGhFiles,
      static_cast<int>(sizeof(kT5BaseGhFiles) / sizeof(kT5BaseGhFiles[0])) },
    // SA3 Small Music — the current SA3-generation music checkpoint. Installs
    // login-free from the ungated Comfy-Org mirror (kSa3MusicAssets); the gated
    // hfRepo is kept ONLY so the Auto-Scan fallback can still verify a
    // hand-downloaded Stability copy against the live HuggingFace manifest.
    { "stable-audio-3-small-music", "Stable Audio 3 Small Music", "stabilityai/stable-audio-3-small-music", nullptr,
      "https://stability.ai/community-license-agreement",
      "Stable Audio 3 Small Music bundles two separately licensed components:\n\n"
      "1) The audio model -- Stability AI Community License:\n"
      "   - Non-commercial use: free\n"
      "   - Commercial use under $1M annual revenue: free (register at stability.ai)\n"
      "   - Commercial use over $1M: enterprise license required\n\n"
      "2) The t5gemma text encoder -- Google Gemma Terms of Use and the Gemma\n"
      "   Prohibited Use Policy (ai.google.dev/gemma/terms).\n\n"
      "T5ynth does not provide the weights; they download from the ungated\n"
      "Comfy-Org repository. By downloading you accept BOTH licenses and take\n"
      "responsibility for compliance. Copies are written into the model folder.", true, true,
      nullptr, 0, "t5gemma-b-b-ul2", kSa3MusicAssets, 2 },
    // SA3 Small SFX — the SA3-generation sound-effects checkpoint. Same
    // architecture and t5gemma-b-b-ul2 encoder as SA3 Small Music; the backend
    // swaps the modality prefix to "TrackType: SFX, " by model name, and the
    // SA3 prompt/axes/dim-explorer gating keys on the "stable-audio-3" prefix,
    // so this variant inherits all of it.
    { "stable-audio-3-small-sfx", "Stable Audio 3 Small SFX", "stabilityai/stable-audio-3-small-sfx", nullptr,
      "https://stability.ai/community-license-agreement",
      "Stable Audio 3 Small SFX bundles two separately licensed components:\n\n"
      "1) The audio model -- Stability AI Community License:\n"
      "   - Non-commercial use: free\n"
      "   - Commercial use under $1M annual revenue: free (register at stability.ai)\n"
      "   - Commercial use over $1M: enterprise license required\n\n"
      "2) The t5gemma text encoder -- Google Gemma Terms of Use and the Gemma\n"
      "   Prohibited Use Policy (ai.google.dev/gemma/terms).\n\n"
      "T5ynth does not provide the weights; they download from the ungated\n"
      "Comfy-Org repository. By downloading you accept BOTH licenses and take\n"
      "responsibility for compliance. Copies are written into the model folder.", true, true,
      nullptr, 0, "t5gemma-b-b-ul2", kSa3SfxAssets, 2 },
};
static constexpr int kNumKnownModels = sizeof(kKnownModels) / sizeof(kKnownModels[0]);

// Look up a model's in-repo text-encoder subfolder by id (empty when the model
// is self-contained — i.e. has no encoderSubfolder in the catalog).
static juce::String encoderSubfolderForModelId(const juce::String& modelId)
{
    for (int i = 0; i < kNumKnownModels; ++i)
        if (modelId == kKnownModels[i].id)
            return juce::String(kKnownModels[i].encoderSubfolder != nullptr
                                    ? kKnownModels[i].encoderSubfolder : "");
    return {};
}

// True for repo files T5ynth never needs to install: VCS / doc / licence /
// preview files that sit next to the real weights and configs.
static bool isRepoDocFile(const juce::String& basename)
{
    const auto n = basename.toLowerCase();
    return n == ".gitattributes"
        || n == "notice"
        || n.startsWith("license")
        || n.endsWith(".md")
        || n.endsWith(".png")
        || n.endsWith(".jpg")
        || n.endsWith(".jpeg");
}

// Pull the LIVE manifest of a gated native Stability model from HuggingFace's
// public tree API (GET /api/models/<repo>/tree/main?recursive=true returns HTTP
// 200 with each file's path+size even for the gated Stability repos — while the
// file BYTES require auth, so T5ynth can VERIFY but the user must FETCH). The
// manifest is: root model.safetensors + model_config.json, plus — when the model
// declares a text-encoder subfolder — every engine file under it (docs excluded).
// Sizes are the CURRENT published sizes, never hardcoded, so a silent re-upload
// can't make a baked-in size reject the correct file. Returns an empty vector on
// any failure (offline / API error) so callers fail closed.
// Blocking network call — run it off the message thread.
static std::vector<ManifestEntry> fetchModelManifest(const juce::String& hfRepo,
                                                     const juce::String& encoderSubfolder)
{
    std::vector<ManifestEntry> manifest;
    if (hfRepo.isEmpty())
        return manifest;
    juce::URL apiUrl("https://huggingface.co/api/models/" + hfRepo
                     + "/tree/main?recursive=true");
    auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                    .withConnectionTimeoutMs(15000);
    auto stream = apiUrl.createInputStream(opts);
    if (!stream)
        return manifest;
    const auto response = stream->readEntireStreamAsString();
    const juce::String subPrefix = encoderSubfolder.isNotEmpty()
        ? encoderSubfolder + "/" : juce::String();
    try
    {
        auto json = nlohmann::json::parse(response.toStdString());
        if (json.is_object() && json.contains("error"))
            return {};
        for (auto& item : json)
        {
            if (item.value("type", std::string()) != "file")
                continue;
            const juce::String path =
                juce::String(item.value("path", std::string())).replaceCharacter('\\', '/');
            const int64_t size = item.value("size", (int64_t) -1);
            if (size < 0)
                continue;

            const bool isRoot = (path == "model.safetensors" || path == "model_config.json");
            const bool isEncoder = subPrefix.isNotEmpty()
                && path.startsWith(subPrefix)
                && !isRepoDocFile(path.fromLastOccurrenceOf("/", false, false));
            if (isRoot || isEncoder)
                manifest.push_back({ path, (juce::int64) size });
        }
    }
    catch (const std::exception&)
    {
        return {};
    }
    return manifest;
}

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

const void* SettingsPage::selectedAssets()
{
    int idx = modelChooser.getSelectedItemIndex();
    if (idx >= 0 && idx < kNumKnownModels)
        return static_cast<const void*>(kKnownModels[idx].assets);
    return nullptr;
}

int SettingsPage::selectedAssetCount()
{
    int idx = modelChooser.getSelectedItemIndex();
    if (idx >= 0 && idx < kNumKnownModels)
        return kKnownModels[idx].assetCount;
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

    if (statusText.isNotEmpty())
    {
        downloadStatusLabel.setText(statusText, juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId,
                                      busy ? kAccent : juce::Colour(0xffcccccc));
    }
}

static bool modelHasRequiredAuxAssets(const juce::String& id, const juce::File& modelDir)
{
    if (!modelDir.exists() || !hasModelMarker(modelDir))
        return false;
    // Models that ship an in-repo text encoder (SA3 -> t5gemma-b-b-ul2) are only
    // usable once that subfolder is present too. hasModelMarker passes on the
    // root weights alone, so check the encoder's own weights + config explicitly
    // -- otherwise the wizard flags SA3 "installed" while the backend refuses to
    // load (missing t5gemma encoder).
    const auto sub = encoderSubfolderForModelId(id);
    if (sub.isNotEmpty())
    {
        auto encDir = modelDir.getChildFile(sub);
        if (!encDir.getChildFile("model.safetensors").existsAsFile()
            || !encDir.getChildFile("config.json").existsAsFile())
            return false;
    }
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
// For gated native Stability models the user manually fetches the repo files
// from HuggingFace to ~/Downloads. Auto-Scan pulls the live manifest, matches
// every required file in the Downloads folder (the two equally-named
// model.safetensors disambiguated by size), reconstructs the t5gemma subfolder,
// and copies the lot into the app-support model dir -- or shows exactly which
// files are still missing / wrong-size.

static juce::File getDownloadsFolder()
{
    // macOS/Windows/Linux all use ~/Downloads as the browser default on a
    // fresh user account. If a user has customised their downloads dir via
    // XDG_DOWNLOAD_DIR or similar, the folder-picker fallback catches it.
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
               .getChildFile("Downloads");
}

// Gather the files in sourceFolder that could be `canonicalName`, tolerating the
// disambiguation suffixes browsers add when the name already exists in the folder
// (Chrome/Edge "name (1).ext", Firefox/Safari "name-2.ext"): every sibling
// matching <stem>*<ext>. The caller picks among them — by manifest size first
// (the two equally-named model.safetensors differ by ~1 GB), then by recency.
// Empty array when nothing matches.
static juce::Array<juce::File> findRenameCandidates(const juce::File& sourceFolder,
                                                    const juce::String& canonicalName)
{
    const auto dot  = canonicalName.lastIndexOfChar('.');
    const auto stem = (dot > 0) ? canonicalName.substring(0, dot) : canonicalName;
    const auto ext  = (dot > 0) ? canonicalName.substring(dot)    : juce::String();
    juce::Array<juce::File> matches;
    sourceFolder.findChildFiles(matches, juce::File::findFiles,
                                /*searchRecursively*/ false, stem + "*" + ext);
    return matches;
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
// — Stable Audio checkpoints differ by hundreds of MB across model
// variants, and identical sizes across variants are vanishingly rare.
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

// How many of a manifest's files are present (any size) in `folder` — used only
// to decide between "report what's missing" and "open a folder picker".
static int countManifestFilesPresent(const juce::File& folder,
                                     const std::vector<ManifestEntry>& manifest)
{
    int n = 0;
    for (const auto& e : manifest)
    {
        const auto base = e.relPath.fromLastOccurrenceOf("/", false, false);
        if (!findRenameCandidates(folder, base).isEmpty()
            || folder.getChildFile(e.relPath).existsAsFile())
            ++n;
    }
    return n;
}

SettingsPage::InstallOutcome SettingsPage::installFromManifestFolder(
    const juce::File& sourceFolder,
    const juce::String& modelId,
    const juce::String& modelDisplayName,
    const std::vector<ManifestEntry>& manifest,
    bool reportIfMissing)
{
    if (manifest.empty())
        return InstallOutcome::NotInstalled;  // offline: caller already reported

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

    auto toMB = [](juce::int64 b) {
        return juce::String(static_cast<double>(b) / (1024.0 * 1024.0), 1) + " MB";
    };

    // Match every manifest entry to a file in the folder. Candidates are gathered
    // by canonical basename (browser-rename tolerant); the right one is the
    // candidate whose size equals the manifest size — this is what tells the two
    // equally-named model.safetensors (root ~2.27 GB vs t5gemma ~1.18 GB) apart.
    // A basename match of the WRONG size is reported as wrong-size (truncated /
    // wrong model), distinct from missing entirely.
    struct Staged { juce::File source; juce::String relPath; };
    std::vector<Staged> staged;
    juce::StringArray missing;    // manifest paths with no candidate at all
    juce::StringArray wrongSize;  // present, but no candidate of the right size

    for (const auto& e : manifest)
    {
        const auto base = e.relPath.fromLastOccurrenceOf("/", false, false);
        auto cands = findRenameCandidates(sourceFolder, base);
        auto exact = sourceFolder.getChildFile(e.relPath);  // structured-clone case
        if (exact.existsAsFile())
            cands.addIfNotAlreadyThere(exact);

        if (cands.isEmpty()) { missing.add(e.relPath); continue; }

        juce::File sized;
        juce::Time sizedTime;
        for (auto& c : cands)
            if (c.getSize() == e.size)
            {
                const auto t = c.getLastModificationTime();
                if (!sized.exists() || t > sizedTime) { sized = c; sizedTime = t; }
            }
        if (sized.exists()) staged.push_back({ sized, e.relPath });
        else                wrongSize.add(e.relPath);
    }

    const bool complete = (missing.isEmpty() && wrongSize.isEmpty()
                           && staged.size() == manifest.size());

    // Per-file checklist for the panel (and the dialog) — minimal status lines.
    auto buildChecklist = [&]() -> juce::String
    {
        std::set<std::string> ok, bad;
        for (const auto& st : staged)   ok.insert(st.relPath.toStdString());
        for (const auto& w  : wrongSize) bad.insert(w.toStdString());
        juce::String s;
        s << modelDisplayName << " -- " << (int) staged.size() << " of "
          << (int) manifest.size() << " files ready\n\n";
        for (const auto& e : manifest)
        {
            const char* mark = ok.count(e.relPath.toStdString())  ? "[OK]"
                             : bad.count(e.relPath.toStdString()) ? "[!!]"
                                                                   : "[  ]";
            s << "  " << mark << " " << e.relPath << "  (" << toMB(e.size) << ")\n";
        }
        if (!complete)
            s << "\n[  ] = still missing    [!!] = wrong size (re-download)\n\n"
                 "Download the marked files from the model page (button above) into "
                 "your Downloads folder, then click Auto-Scan again. The t5gemma "
                 "files live inside the t5gemma-b-b-ul2 folder on the model page.";
        return s;
    };

    if (!complete)
    {
        setInstructionsText(instructionsLabel, buildChecklist());
        if (reportIfMissing)
        {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                modelDisplayName + " -- files still needed",
                "Looked in:\n  " + sourceFolder.getFullPathName() + "\n\n" + buildChecklist());
            return InstallOutcome::AbortedWithDialog;
        }
        return InstallOutcome::NotInstalled;
    }

    // Complete. Same-size duplicate guard on the ROOT weights: if the main
    // model.safetensors is byte-for-byte the size of one already installed under
    // a DIFFERENT model id, the user likely picked the wrong file — confirm
    // rather than write the same checkpoint into two slots.
    for (const auto& st : staged)
    {
        if (st.relPath != "model.safetensors")
            continue;
        const auto dup = findSameSizeInstalledModel(modelId, "model.safetensors", st.source.getSize());
        if (dup.isEmpty())
            continue;

        // Stable Audio 3 Small *Music* and *SFX* legitimately ship the IDENTICAL
        // model.safetensors — verified byte-for-byte against HuggingFace: the two
        // repos carry the same LFS sha256 for both the DiT weights AND the t5gemma
        // encoder. The variants differ ONLY in model_config.json (the "TrackType:"
        // prompt prefix). A size/content collision between two SA3 variants is thus
        // the EXPECTED case, not a wrong-file mistake — install normally and reuse
        // the shared weights. The guard below still protects every other model family
        // (where identical sizes across variants really are vanishingly rare).
        const bool bothSA3 = modelId.startsWithIgnoreCase("stable-audio-3")
                          && dup.startsWithIgnoreCase("stable-audio-3");
        if (bothSA3)
            continue;

        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Duplicate of " + dup + "?",
            "The model.safetensors picked for " + modelDisplayName
            + " is the same size as the one already installed for '" + dup
            + "'. They are almost certainly the SAME checkpoint. If you meant a "
              "different model, re-download its model.safetensors and click "
              "Auto-Scan again. Install aborted.");
        return InstallOutcome::AbortedWithDialog;
    }

    auto targetDir = getAppSupportModelDir(modelId);
    setModelInstallBusy(true, "Copying " + modelDisplayName + " ("
                        + juce::String((int) staged.size()) + " files)...");

    juce::Component::SafePointer<SettingsPage> safeThis(this);
    std::vector<Staged> toCopy = staged;

    std::thread([safeThis, sourceFolder, targetDir, toCopy, modelDisplayName]()
    {
        juce::String errorTitle, errorBody;

        if (!targetDir.createDirectory())
        {
            errorTitle = "Could not create model folder";
            errorBody = "T5ynth could not create:\n  " + targetDir.getFullPathName()
                      + "\n\nCheck folder permissions and try again.";
        }
        else
        {
            juce::StringArray copyErrors;
            for (const auto& st : toCopy)
            {
                // Copy to the CANONICAL relative path (subfolders included), even
                // if the source on disk was "model (1).safetensors": the backend
                // loads by canonical path, not by whatever the browser saved.
                auto dest = targetDir.getChildFile(st.relPath);
                dest.getParentDirectory().createDirectory();
                if (dest.existsAsFile()) dest.deleteFile();
                if (!st.source.copyFileTo(dest))
                    copyErrors.add(st.relPath);
            }
            if (!copyErrors.isEmpty())
            {
                errorTitle = "Copy failed";
                errorBody = "Copying failed for:\n  " + copyErrors.joinIntoString("\n  ")
                          + "\n\nCheck disk space and folder permissions in:\n  "
                          + targetDir.getFullPathName();
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
                        juce::MessageBoxIconType::WarningIcon, errorTitle, errorBody);
                    return;
                }

                self->downloadStatusLabel.setText("Model copied. Activating...",
                                                  juce::dontSendNotification);
                self->downloadStatusLabel.setColour(juce::Label::textColourId,
                                                    juce::Colour(0xff4ade80));

                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::InfoIcon,
                    modelDisplayName + " -- Installed",
                    "T5ynth copied the model files from:\n  " + sourceFolder.getFullPathName()
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


// ── git-clone install path (removed) ────────────────────────────────────────
// An earlier git-clone helper (added to work around filename collisions
// across HF repos that made the "drop into Downloads" pattern unreliable)
// has no remaining caller and has been removed. The git/git-lfs preflight,
// platform install hints, and the threaded clone driver all left with it.


void SettingsPage::performAutoScan()
{
    if (modelInstallBusy_.load())
        return;

    // 1. Already FULLY installed anywhere we recognise? Require the encoder
    //    subfolder too (modelHasRequiredAuxAssets), not just the root weights —
    //    otherwise an encoder-less SA3 dir would short-circuit here and report
    //    "imported" while the backend refuses to load. An incomplete dir falls
    //    through to the manifest checklist below, which lists what's missing.
    auto found = scanForModel();
    if (found.exists() && modelHasRequiredAuxAssets(selectedModelId(), found))
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

    // 2. Native-Stability smart scan: the only remaining manual-install
    //    category. SAO 1.0 / SAO Small are a 2-file checkpoint; SA3 Small Music
    //    additionally ships the t5gemma-b-b-ul2 text-encoder subfolder. All are
    //    driven by the live HF manifest the user pulls into ~/Downloads.
    auto modelId = selectedModelId();
    const bool isNativeStabilityModel =
        modelId == "stable-audio-open-small"
        || modelId == "stable-audio-open-1.0"
        || modelId == "stable-audio-3-small-music"
        || modelId == "stable-audio-3-small-sfx";

    if (!isNativeStabilityModel)
    {
        updateStatus();
        downloadStatusLabel.setText(
            "No model found in standard locations. Follow the instructions below.",
            juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId,
                                      juce::Colour(0xffef4444));
        return;
    }
    const auto modelDisplayName = selectedModelDisplay();
    const auto hfRepo           = selectedHfRepo();
    const auto encoderSub       = encoderSubfolderForModelId(modelId);

    // 3. Pull the live manifest off the message thread, then match it against the
    //    Downloads folder (with a picker fallback). The manifest is fetched ONCE
    //    and threaded through both attempts.
    setModelInstallBusy(true, "Fetching the file list from HuggingFace...");
    juce::Component::SafePointer<SettingsPage> safeThis(this);
    std::thread([safeThis, hfRepo, encoderSub, modelId, modelDisplayName]()
    {
        auto manifest = fetchModelManifest(hfRepo, encoderSub);
        juce::MessageManager::callAsync([safeThis, manifest, modelId, modelDisplayName]()
        {
            auto* self = safeThis.getComponent();
            if (self == nullptr) return;
            self->setModelInstallBusy(false);

            if (manifest.empty())
            {
                self->downloadStatusLabel.setText("Could not reach HuggingFace",
                                                  juce::dontSendNotification);
                self->downloadStatusLabel.setColour(juce::Label::textColourId,
                                                    juce::Colour(0xffef4444));
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Could not fetch the file list",
                    "T5ynth could not reach HuggingFace to look up which files "
                    + modelDisplayName + " needs.\n\nYou download the model from "
                    "HuggingFace, so this is almost certainly a temporary network "
                    "problem. Check your connection and click Auto-Scan again.");
                return;
            }

            // Primary: the system Downloads folder.
            auto downloads = getDownloadsFolder();
            auto outcome = self->installFromManifestFolder(
                downloads, modelId, modelDisplayName, manifest, /*reportIfMissing*/ false);
            if (outcome == InstallOutcome::Installed
                || outcome == InstallOutcome::AbortedWithDialog)
                return;  // copying, or a dialog (duplicate guard) already shown

            // Some files already in Downloads → report exactly what's still
            // missing rather than popping a picker over a half-done download.
            if (countManifestFilesPresent(downloads, manifest) > 0)
            {
                self->installFromManifestFolder(
                    downloads, modelId, modelDisplayName, manifest, /*reportIfMissing*/ true);
                return;
            }

            // Nothing in Downloads → offer a folder picker (saved elsewhere?).
            self->fileChooser = std::make_unique<juce::FileChooser>(
                "Where did you save the model files?",
                downloads.isDirectory()
                    ? downloads
                    : juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                "");
            self->fileChooser->launchAsync(
                juce::FileBrowserComponent::openMode
                    | juce::FileBrowserComponent::canSelectDirectories,
                [safeThis, modelId, modelDisplayName, manifest](const juce::FileChooser& fc)
                {
                    auto* s2 = safeThis.getComponent();
                    if (s2 == nullptr) return;
                    auto folder = fc.getResult();
                    if (folder == juce::File())
                    {
                        s2->downloadStatusLabel.setText(
                            "Auto-Scan cancelled. Follow the checklist below.",
                            juce::dontSendNotification);
                        s2->downloadStatusLabel.setColour(juce::Label::textColourId,
                                                          juce::Colour(0xffef4444));
                        return;
                    }
                    s2->installFromManifestFolder(
                        folder, modelId, modelDisplayName, manifest, /*reportIfMissing*/ true);
                });
        });
    }).detach();
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
    scanButton.setEnabled(false);
    browseButton.setEnabled(false);
    downloadStatusLabel.setColour(juce::Label::textColourId, kAccent);
    progressBar.setVisible(true);
    downloadProgress = 0.0;

    auto modelId = selectedModelId();

    // Reassembly install (SA3): every asset has its OWN source URL + target path,
    // possibly across two sources (Comfy-Org ungated weights + the T5ynt metadata
    // mirror). Checked before the single-repo GH/HF paths it supersedes.
    if (selectedAssets() != nullptr && selectedAssetCount() > 0) {
        downloadStatusLabel.setText("Downloading model files...", juce::dontSendNotification);
        auto targetDir = getAppSupportModelDir(modelId);
        targetDir.createDirectory();
        downloadReassemblyInThread();
        return;
    }

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

// Reassembly install path: fetch a fixed list of assets, each from its OWN
// absolute source URL, into its OWN canonical path inside the model dir (source
// path need NOT equal target path). This is how SA3 installs login-free — the
// ungated heavy weights come from the Comfy-Org mirror under their repacked
// names and land as model.safetensors / t5gemma-b-b-ul2/model.safetensors, while
// the small metadata Comfy-Org omits comes from the T5ynt metadata mirror.
// Resume / Range / 416 / truncation handling mirrors downloadGhReleaseInThread;
// the structural difference is the per-asset source-URL + target-path decoupling
// (with parent-dir creation for the t5gemma-b-b-ul2/ subfolder).
// ── SA3 reassembly finalize ───────────────────────────────────────────────────
// The ungated Comfy-Org weight repos ship ONLY weights (the SA3 checkpoint and the
// single-file t5gemma encoder). The small metadata the stable-audio-tools loader
// needs is supplied here, at install time, so SA3 installs with no HuggingFace
// account: the ~60 KB JSON configs are bundled in the app (BinaryData) and the
// SentencePiece tokenizer is carved out of the t5gemma weights themselves. All of
// this is file I/O — it runs on the download thread, never the message thread.

// Write a bundled blob to a target file, creating parent dirs. replaceWithData
// writes via a temp + rename, so a crash can't leave a half-written config.
static bool writeBundledFile(const juce::File& target, const char* data, int size,
                             juce::String& err)
{
    target.getParentDirectory().createDirectory();
    if (! target.replaceWithData(data, (size_t) size))
    {
        err = "Cannot write bundled config:\n  " + target.getFullPathName();
        return false;
    }
    return true;
}

// Carve the embedded SentencePiece model (the `spiece_model` tensor — raw .model
// bytes, dtype U8) out of a Comfy-Org-style single-file t5gemma safetensors and
// write it to outTokenizerModel. safetensors layout is
// [u64 LE headerLen][JSON header][tensor data]; a tensor's bytes live at
// data_offsets RELATIVE to the data section, so absolute = 8 + headerLen + start.
static bool extractSpieceModel(const juce::File& safetensors,
                               const juce::File& outTokenizerModel,
                               juce::String& err)
{
    juce::FileInputStream in(safetensors);
    if (! in.openedOk())
    {
        err = "Cannot open the t5gemma weights to extract the tokenizer:\n  "
            + safetensors.getFullPathName();
        return false;
    }

    char lenBuf[8] = {};
    if (in.read(lenBuf, 8) != 8)
    {
        err = "t5gemma weights are truncated (no safetensors header length).";
        return false;
    }
    // littleEndianInt64 returns uint64; treat it as signed so a corrupt high-bit
    // value reads as negative and is rejected by the <= 0 guard below.
    const juce::int64 headerLen = (juce::int64) juce::ByteOrder::littleEndianInt64(lenBuf);
    if (headerLen <= 0 || headerLen > 64ll * 1024 * 1024
        || (8 + headerLen) > safetensors.getSize())
    {
        err = "t5gemma weights have an invalid safetensors header (length "
            + juce::String(headerLen) + ").";
        return false;
    }

    juce::MemoryBlock headerBytes;
    if ((juce::int64) in.readIntoMemoryBlock(headerBytes, (ssize_t) headerLen) != headerLen)
    {
        err = "Could not read the safetensors header from the t5gemma weights.";
        return false;
    }

    juce::int64 start = -1, end = -1;
    try
    {
        auto header = nlohmann::json::parse(
            std::string(static_cast<const char*>(headerBytes.getData()), headerBytes.getSize()));
        if (! header.contains("spiece_model"))
        {
            err = "The downloaded t5gemma weights contain no embedded tokenizer "
                  "(`spiece_model` tensor missing). The file may be the wrong "
                  "encoder or an incomplete download.";
            return false;
        }
        auto offs = header["spiece_model"].at("data_offsets");
        if (! offs.is_array() || offs.size() != 2)
        {
            err = "The `spiece_model` tensor has malformed data_offsets.";
            return false;
        }
        start = offs[0].get<juce::int64>();
        end   = offs[1].get<juce::int64>();
    }
    catch (const std::exception& e)
    {
        err = juce::String("Could not parse the t5gemma safetensors header: ") + e.what();
        return false;
    }

    if (start < 0 || end <= start)
    {
        err = "The `spiece_model` tensor has an invalid byte range.";
        return false;
    }
    const juce::int64 absStart = 8 + headerLen + start;
    const juce::int64 length   = end - start;
    if (absStart + length > safetensors.getSize())
    {
        err = "The `spiece_model` tensor extends past the end of the t5gemma "
              "weights — the download is incomplete.";
        return false;
    }
    if (! in.setPosition(absStart) || in.getPosition() != absStart)
    {
        err = "Could not seek to the tokenizer bytes in the t5gemma weights.";
        return false;
    }
    juce::MemoryBlock spiece;
    if ((juce::int64) in.readIntoMemoryBlock(spiece, (ssize_t) length) != length)
    {
        err = "Short read while extracting the tokenizer from the t5gemma weights.";
        return false;
    }
    if (! outTokenizerModel.replaceWithData(spiece.getData(), spiece.getSize()))
    {
        err = "Cannot write the extracted tokenizer to:\n  "
            + outTokenizerModel.getFullPathName();
        return false;
    }
    return true;
}

// Write the bundled configs + extract the tokenizer into a freshly reassembled SA3
// model dir. encoderSub is the text-encoder subfolder (e.g. "t5gemma-b-b-ul2").
// In-folder license documentation written next to every reassembled SA3 model.
// SA3 bundles two separately licensed third-party components (Stability's audio
// model + Google's Gemma text encoder); this records both, carries the verbatim
// Gemma redistribution notice required by the Gemma Terms, and points to both
// canonical full-license texts. T5ynth orchestrates the download but neither owns
// nor relicenses the weights. (`a02cbcd…` is the pinned Comfy-Org revision.)
static const char* const kSa3LicenseNotice =
R"NOTICE(T5ynth -- Stable Audio 3 model components and their licenses
==============================================================

Powered by Stability AI.

This folder holds a Stable Audio 3 Small model (Music or SFX) assembled by
T5ynth. It is made of two SEPARATELY LICENSED third-party components. T5ynth
neither owns nor relicenses either of them -- it only orchestrated the download
and wrote the configuration files. You are responsible for complying with BOTH
licenses below; the FULL license texts are in this folder.

--------------------------------------------------------------------------------
1. Audio model  --  model.safetensors, model_config.json
--------------------------------------------------------------------------------
   This Stability AI Model is licensed under the Stability AI Community License,
   Copyright (C) Stability AI Ltd. All Rights Reserved.

   Full text:  STABILITY_AI_COMMUNITY_LICENSE.txt (in this folder), or
               https://stability.ai/community-license-agreement
   Summary (NOT a substitute for the full text):
     - Free for non-commercial use.
     - Free for commercial use by individuals/organisations with annual revenue
       under US $1,000,000 (registration at stability.ai required).
     - Organisations at or above US $1,000,000 annual revenue require a separate
       Stability AI Enterprise License.

--------------------------------------------------------------------------------
2. Text encoder  --  t5gemma-b-b-ul2/
--------------------------------------------------------------------------------
   Built from Google's Gemma (T5Gemma).

   Gemma is provided under and subject to the Gemma Terms of Use found at
   ai.google.dev/gemma/terms

   Full texts: GEMMA_TERMS_OF_USE.txt and GEMMA_PROHIBITED_USE_POLICY.txt
               (in this folder), or
               https://ai.google.dev/gemma/terms
               https://ai.google.dev/gemma/prohibited_use_policy

   Your use of this text encoder is subject to the Gemma Terms of Use and the
   Gemma Prohibited Use Policy. Google LLC is the provider of Gemma; Google is
   not affiliated with and does not endorse T5ynth.

--------------------------------------------------------------------------------
Provenance
--------------------------------------------------------------------------------
The model weights were downloaded by you, with no account or token, from the
ungated Comfy-Org repository:
  https://huggingface.co/Comfy-Org/stable-audio-3
  (pinned revision a02cbcdcd07426b0150557d0145bc894795823af)

Before downloading you accepted both licenses above in T5ynth's download dialog.
T5ynth itself is free software under the GNU GPL v3; that license covers T5ynth's
own source code, NOT these third-party model weights.

UCDCAE AI Lab.
)NOTICE";

static bool finalizeSa3Reassembly(const juce::File& targetDir,
                                  const juce::String& encoderSub,
                                  juce::String& err)
{
    // License documentation for the two third-party components (written first; it
    // is not a completeness marker, so its ordering vs. model_config.json is free).
    if (! writeBundledFile(targetDir.getChildFile("MODEL_LICENSES.txt"),
                           kSa3LicenseNotice, (int) std::strlen(kSa3LicenseNotice), err))
        return false;

    // The full verbatim third-party license texts. The Stability AI Community
    // License IV(a)(i) requires distributing a copy of the Agreement to any
    // recipient of a product that uses the materials; the Gemma Terms + Prohibited
    // Use Policy are the t5gemma encoder's governing terms.
    if (! writeBundledFile(targetDir.getChildFile("STABILITY_AI_COMMUNITY_LICENSE.txt"),
                           BinaryData::STABILITY_AI_COMMUNITY_LICENSE_txt,
                           BinaryData::STABILITY_AI_COMMUNITY_LICENSE_txtSize, err))
        return false;
    if (! writeBundledFile(targetDir.getChildFile("GEMMA_TERMS_OF_USE.txt"),
                           BinaryData::GEMMA_TERMS_OF_USE_txt,
                           BinaryData::GEMMA_TERMS_OF_USE_txtSize, err))
        return false;
    if (! writeBundledFile(targetDir.getChildFile("GEMMA_PROHIBITED_USE_POLICY.txt"),
                           BinaryData::GEMMA_PROHIBITED_USE_POLICY_txt,
                           BinaryData::GEMMA_PROHIBITED_USE_POLICY_txtSize, err))
        return false;

    // ORDER MATTERS: model_config.json is the install-completeness marker
    // (hasModelMarker keys on it, and modelHasRequiredAuxAssets does NOT check the
    // carved tokenizer.model). So write it LAST — only after every encoder config
    // AND the tokenizer carve have succeeded. If any earlier step fails, the marker
    // is absent and the dir correctly reads "Not installed" instead of masquerading
    // as ready-but-unloadable; a re-click re-runs this idempotently. (We can't just
    // add tokenizer.model to the completeness check: the manual Auto-Scan install
    // ships tokenizer.json instead, and requiring tokenizer.model would reject it.)
    const auto encDir = targetDir.getChildFile(encoderSub);

    // The four Gemma encoder/tokenizer configs.
    if (! writeBundledFile(encDir.getChildFile("config.json"),
                           BinaryData::sa3_t5gemma_config_json,
                           BinaryData::sa3_t5gemma_config_jsonSize, err))
        return false;
    if (! writeBundledFile(encDir.getChildFile("generation_config.json"),
                           BinaryData::sa3_t5gemma_generation_config_json,
                           BinaryData::sa3_t5gemma_generation_config_jsonSize, err))
        return false;
    if (! writeBundledFile(encDir.getChildFile("tokenizer_config.json"),
                           BinaryData::sa3_t5gemma_tokenizer_config_json,
                           BinaryData::sa3_t5gemma_tokenizer_config_jsonSize, err))
        return false;
    if (! writeBundledFile(encDir.getChildFile("special_tokens_map.json"),
                           BinaryData::sa3_t5gemma_special_tokens_map_json,
                           BinaryData::sa3_t5gemma_special_tokens_map_jsonSize, err))
        return false;

    // tokenizer.model — carved out of the downloaded t5gemma weights (no 34 MB
    // tokenizer.json needed; AutoTokenizer rebuilds the fast tokenizer from this).
    const auto encWeights = encDir.getChildFile("model.safetensors");
    if (! encWeights.existsAsFile())
    {
        err = "t5gemma weights missing after download:\n  " + encWeights.getFullPathName();
        return false;
    }
    if (! extractSpieceModel(encWeights, encDir.getChildFile("tokenizer.model"), err))
        return false;

    // model_config.json LAST — the Stability SA3 config (shared by music+sfx; the
    // backend derives the TrackType modality from the dir NAME, not this file).
    // Its presence is what flips the dir to "Installed", so it goes last.
    return writeBundledFile(targetDir.getChildFile("model_config.json"),
                            BinaryData::sa3_model_config_json,
                            BinaryData::sa3_model_config_jsonSize, err);
}

void SettingsPage::downloadReassemblyInThread()
{
    auto modelId = selectedModelId();
    auto targetDir = getAppSupportModelDir(modelId);

    // SA3-style reassembly carries bundled metadata + a tokenizer to extract:
    // any reassembly model that declares a text-encoder subfolder runs the
    // finalize step once the weights land. A plain reassembly (no encoder
    // subfolder) skips it.
    const auto encoderSub = encoderSubfolderForModelId(modelId);
    const bool needsMetadata = encoderSub.isNotEmpty();

    const auto* assets = static_cast<const ReassemblyAsset*>(selectedAssets());
    int assetCount = selectedAssetCount();

    if (assets == nullptr || assetCount <= 0)
    {
        // Catalog row routed here but defined no asset list — programmer error.
        onDownloadFinished(false,
            "Internal error: model is configured for reassembly download but has "
            "no asset list defined in the SetupWizard catalog.");
        return;
    }

    int64_t total = 0;
    for (int i = 0; i < assetCount; ++i) total += assets[i].expectedSize;
    totalBytes = total;
    downloadedBytes = 0;
    downloadCounter_ = std::make_shared<std::atomic<int64_t>>(0);
    downloadCancelFlag_ = std::make_shared<std::atomic<bool>>(false);
    startTimer(250);

    juce::Component::SafePointer<SettingsPage> safeThis(this);
    auto progressCounter = downloadCounter_;
    auto cancelFlag = downloadCancelFlag_;
    std::thread([safeThis, progressCounter, cancelFlag, targetDir, assets, assetCount,
                 encoderSub, needsMetadata]()
    {
        int64_t bytesCompleted = 0;

        for (int i = 0; i < assetCount; ++i)
        {
            if (cancelFlag && cancelFlag->load())
                return;

            auto& a = assets[i];
            const auto sourceUrl = juce::String(a.sourceUrl);
            const auto localPath = juce::String(a.localPath);
            auto targetFile = targetDir.getChildFile(localPath);
            targetFile.getParentDirectory().createDirectory();
            const auto displayName = localPath.fromLastOccurrenceOf("/", false, false);

            // Resume-aware existing-file check (ignore stale LFS pointers):
            //   == expectedSize      → already complete, skip
            //   > expectedSize       → not our file — start fresh
            //   in (0, expectedSize) → partial — request HTTP Range and append
            int64_t existingBytes = 0;
            if (targetFile.existsAsFile() && !isLfsPointer(targetFile))
                existingBytes = targetFile.getSize();

            if (existingBytes == a.expectedSize)
            {
                bytesCompleted += a.expectedSize;
                if (progressCounter) progressCounter->store(bytesCompleted);
                continue;
            }
            if (existingBytes > a.expectedSize)
            {
                targetFile.deleteFile();
                existingBytes = 0;
            }
            const bool tryResume = (existingBytes > 0);

            auto fileNum = i + 1;
            juce::String statusText = (tryResume ? juce::String("Resuming: ")
                                                 : juce::String("Downloading: "))
                + displayName + " (" + juce::String(fileNum) + "/"
                + juce::String(assetCount) + ")";
            juce::MessageManager::callAsync([safeThis, statusText]() {
                if (auto* self = safeThis.getComponent())
                    self->downloadStatusLabel.setText(statusText, juce::dontSendNotification);
            });

            // createInputStream follows redirects, so HF LFS (Comfy-Org weights)
            // and direct GitHub-release assets both resolve transparently.
            juce::URL fileUrl(sourceUrl);
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
                juce::MessageManager::callAsync([safeThis, displayName]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false, "Connection failed for: " + displayName
                            + "\n\nCheck your internet connection.");
                });
                return;
            }

            // 416 Range Not Satisfiable: the server file is smaller than our
            // partial (source re-uploaded). Clear the partial so the next click
            // starts fresh instead of re-sending the same unsatisfiable Range.
            if (statusCode == 416 && tryResume)
            {
                targetFile.deleteFile();
                juce::MessageManager::callAsync([safeThis, displayName]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false,
                            "The file on the server changed since your previous "
                            "download attempt: " + displayName + "\n\n"
                            "Your partial download has been cleared. Click "
                            "Download again to start fresh.");
                });
                return;
            }

            if (statusCode >= 400)
            {
                juce::MessageManager::callAsync([safeThis, displayName, statusCode]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false, "Server returned HTTP "
                            + juce::String(statusCode) + " for: " + displayName);
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
                juce::MessageManager::callAsync([safeThis, displayName]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false, "Cannot write: " + displayName);
                });
                return;
            }
            // FileOutputStream seeks to end-of-file for existing files, so the
            // write cursor is already correct for appendMode == true.

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

            if (cancelFlag && cancelFlag->load())
                return;

            // Network dropped mid-stream. Keep the partial so the next click
            // resumes from where it stopped — don't delete it.
            const int64_t finalBytes = targetFile.getSize();
            if (finalBytes < a.expectedSize)
            {
                auto toStr = [](int64_t b) {
                    return (b < 1024 * 1024) ? juce::String(b) + " bytes"
                                             : juce::String(b / (1024 * 1024)) + " MB";
                };
                auto expectedStr = toStr(a.expectedSize);
                auto gotStr = toStr(finalBytes);
                juce::MessageManager::callAsync([safeThis, displayName, expectedStr, gotStr]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false,
                            "Download was interrupted for " + displayName + ":\n"
                            "Expected " + expectedStr + ", received " + gotStr + ".\n\n"
                            "Click Download again to resume from where it stopped.");
                });
                return;
            }

            bytesCompleted += a.expectedSize;
            if (progressCounter) progressCounter->store(bytesCompleted);
        }

        // Post-reassembly finalize: write the bundled metadata the ungated weight
        // repos omit and carve the tokenizer out of the t5gemma weights. Only the
        // SA3-style reassembly (a model that declares a text-encoder subfolder)
        // needs this; a plain reassembly skips it.
        if (needsMetadata)
        {
            juce::String ferr;
            if (! finalizeSa3Reassembly(targetDir, encoderSub, ferr))
            {
                juce::MessageManager::callAsync([safeThis, ferr]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false, ferr);
                });
                return;
            }
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

    // Download button is only shown for models T5ynth can fetch itself.
    downloadButton.setVisible(downloadable);

    // "Installed" requires the model to be COMPLETE — for SA3 that includes the
    // t5gemma encoder subfolder, not just the root weights hasModelMarker sees.
    if (modelPath.exists() && modelHasRequiredAuxAssets(id, modelPath)) {
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
            const bool sfx = (id == "stable-audio-3-small-sfx");
            setInstructionsText(
                instructionsLabel,
                juce::String(sfx
                    ? "STABLE AUDIO 3 SMALL SFX\n"
                      "The current SA3-generation small-format checkpoint, tuned for\n"
                      "sound-effects content. Ships its own t5gemma text encoder.\n\n"
                    : "STABLE AUDIO 3 SMALL MUSIC\n"
                      "The current SA3-generation small-format checkpoint, tuned for\n"
                      "musical content. Ships its own t5gemma text encoder.\n\n")
                + "Click 'Download' above and wait for it to finish -- no HuggingFace\n"
                "account or token needed. T5ynth fetches the ungated weights directly\n"
                "(~2.3 GB checkpoint + ~1.2 GB encoder), then writes the encoder config\n"
                "and rebuilds the tokenizer locally.\n\n"
                "  Target: " + targetPath + "\n\n"
                "Already have the files from Stability by hand? Click 'Auto-Scan'\n"
                "instead -- T5ynth imports them from your Downloads folder.\n\n"
                "LICENSE: Stability AI Community License (the audio model) AND the\n"
                "Google Gemma Terms of Use + Prohibited Use Policy (the t5gemma text\n"
                "encoder). Both license texts are written into the model folder; by\n"
                "downloading you accept both.");
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

    // Install action button — only the in-app HF downloader uses this row.
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
