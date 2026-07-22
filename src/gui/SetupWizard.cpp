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

// Mirrors the backend's `_is_local_transformers_model_dir`
// (backend/pipe_inference.py): a config, a tokenizer, and weights transformers
// can actually RESOLVE -- single-file, or the shard map of a multi-file
// checkpoint.
//
// hasModelMarker above is NOT a substitute for a transformers model dir. It
// proves only that some .safetensors exist somewhere below the directory, so a
// sharded install whose model.safetensors.index.json never arrived passes it:
// the row then reports "Installed", hides the Download button, and leaves no way
// to repair -- while from_pretrained finds no weights at all and the backend's
// resolver quietly walks on to whatever OTHER coder it can find.
static bool isLoadableTransformersDir(const juce::File& dir)
{
    if (! dir.isDirectory())
        return false;
    const bool hasConfig    = dir.getChildFile("config.json").existsAsFile();
    const bool hasTokenizer = dir.getChildFile("tokenizer.json").existsAsFile()
                           || dir.getChildFile("spiece.model").existsAsFile();
    const bool hasWeights   = dir.getChildFile("model.safetensors").existsAsFile()
                           || dir.getChildFile("model.safetensors.index.json").existsAsFile();
    return hasConfig && hasTokenizer && hasWeights;
}

// A model must live in its OWN folder. Importing a SHARED user folder (Downloads,
// Desktop, the home dir, a volume root) is refused, for two reasons: the import
// MOVES the selected directory into the app's model folder, so accepting
// ~/Downloads would move the user's entire Downloads folder; and hasModelMarker
// only proves that SOME .safetensors exist somewhere below the directory, so two
// unrelated encoder files lying in Downloads were enough to light a model slot as
// "Installed" while the actual model was never there.
static bool isProtectedUserFolder(const juce::File& dir)
{
    if (dir.getParentDirectory() == dir)          // volume root ("/")
        return true;

    for (auto id : { juce::File::userHomeDirectory,      juce::File::userDesktopDirectory,
                     juce::File::userDocumentsDirectory, juce::File::userMusicDirectory,
                     juce::File::userPicturesDirectory,  juce::File::userMoviesDirectory,
                     juce::File::userApplicationDataDirectory,
                     juce::File::commonApplicationDataDirectory,
                     juce::File::commonDocumentsDirectory,
                     juce::File::globalApplicationsDirectory })
        if (juce::File::getSpecialLocation(id) == dir)
            return true;

    // JUCE has no Downloads special location, so match it by name directly under
    // the home directory — the exact case this guard exists for.
    if (dir.getFileName().equalsIgnoreCase("Downloads")
        && dir.getParentDirectory() == juce::File::getSpecialLocation(juce::File::userHomeDirectory))
        return true;

    return false;
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

    // The shard map of a MULTI-FILE transformers checkpoint. It ends in ".json",
    // so the ".safetensors" rule below does NOT catch it -- and without it
    // from_pretrained cannot find any weights at all and reports the misleading
    // "no file named model.safetensors found in directory", even though all four
    // shards are sitting right there. Measured on Qwen2.5-Coder-7B-Instruct
    // (4 shards, 15 GB): the download completed, the Model Manager showed
    // "Installed", and the model could not be loaded. Every sharded repo the
    // in-app downloader installs is affected.
    // Deliberately NOT `.bin.index.json`: the rule below fetches only
    // `.safetensors` weights, so a bin-sharded repo would land its shard map with
    // zero shards behind it -- which flips the backend's load gate from
    // correctly-false to wrongly-true and hands a weightless directory to
    // from_pretrained instead of failing cleanly. Add bin shards first if such a
    // repo is ever added to the catalogue.
    if (path.endsWith(".safetensors.index.json"))
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

// The t5-base text encoder files, fetched ungated from google-t5/t5-base on
// HuggingFace (Apache-2.0). google-t5/t5-base is a frozen legacy model, so these
// four files + sizes are fixed (each verified byte-for-byte against HF) and drive
// the resume / skip / safetensors-integrity checks in downloadFileSet. This is
// the set the Stable Audio Open engines auto-chain (they condition on t5-base).
static const GhAsset kT5BaseGhFiles[] = {
    { "config.json",            1208 },
    { "tokenizer.json",      1389353 },
    { "spiece.model",         791656 },
    { "model.safetensors", 891646390 },
};
static constexpr int kT5BaseGhFileCount =
    static_cast<int>(sizeof(kT5BaseGhFiles) / sizeof(kT5BaseGhFiles[0]));
// Resolve base for the t5-base files above. "t5-base" is HF's canonical redirect
// to google-t5/t5-base; the resolve URL works ungated with no account or token.
static const char* const kT5BaseHfBase = "https://huggingface.co/t5-base/resolve/main";

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
// SA3 Medium — ONE checkpoint that renders both Music and SFX (the modality is
// driven by the request's track_type field, not the dir name). The distilled
// final checkpoint (stable_audio_3_medium.safetensors, sha256 48d9c65e…), NOT
// the _base twin. Same shared t5gemma-b-b-ul2 encoder as the small tiers. The
// medium model_config.json (SAME-L autoencoder) is written by finalizeSa3-
// Reassembly from bundled BinaryData.
static const ReassemblyAsset kSa3MediumAssets[] = {
    { T5YNTH_COMFY_SA3 "/checkpoints/stable_audio_3_medium.safetensors",
      "model.safetensors", 9222116660 },
    { T5YNTH_COMFY_SA3 "/text_encoders/t5gemma_b_b_ul2.safetensors",
      "t5gemma-b-b-ul2/model.safetensors", 1187264003 },
};
#undef T5YNTH_COMFY_SA3

// Comfy-Org ungated Stable Audio Open 1.0 weights ("_repackaged"), PINNED to a
// fixed commit so a silent re-upload can never change what users get vs. what was
// verified (gated=False at this revision; anonymous fetch = 200 OK; the
// safetensors header carries native stable-audio-tools namespaces
// conditioner.conditioners.* / model.model.* / pretransform.model.*, i.e. the
// format T5ynth's loader expects). The repo ships ONLY the checkpoint, so the
// canonical model_config.json (verified byte-identical across three independent
// ungated mirrors) is supplied at install by finalizeSaoOpen10Reassembly from
// bundled BinaryData. SAO 1.0 conditions on t5-base (Apache, ungated) — there is
// no in-dir text-encoder subfolder, so this reassembly carries no tokenizer carve.
#define T5YNTH_COMFY_SAO10 \
    "https://huggingface.co/Comfy-Org/stable-audio-open-1.0_repackaged/resolve/a12a5d8f364768d908d417610a9e7f6ab8a6b5ac"
static const ReassemblyAsset kSaoOpen10Assets[] = {
    { T5YNTH_COMFY_SAO10 "/stable-audio-open-1.0.safetensors",
      "model.safetensors", 4853889016 },
};
#undef T5YNTH_COMFY_SAO10

// Stable Audio Open Small has NO third-party ungated mirror (Comfy-Org never
// repackaged it; the only other source is a Diffusers-format conversion T5ynth's
// native loader can't use). So T5ynth self-hosts the weights UNMODIFIED in its own
// ungated repo, PINNED to a fixed commit (gated=False at this revision; anonymous
// fetch verified). The weights are byte-identical to the official gated
// stabilityai/stable-audio-open-small (model.safetensors sha256
// 0ee8b9edbe3104cb079dd163b62589d67d9c321ea8942cbbd81e6f7d3f390be2, checked before
// upload). Redistribution is permitted by SA Community License §2 + §4(a) provided
// the license travels with the weights and "Powered by Stability AI" is shown --
// finalizeSaoSmallReassembly writes both. Unlike SAO 1.0's Comfy-Org mirror, this
// repo ALSO ships model_config.json, so it is a second asset (no bundled config).
// SAO Small conditions on t5-base (Apache, ungated, auto-chained) -- no in-dir
// text encoder, so this reassembly carries no tokenizer carve.
#define T5YNTH_SAO_SMALL \
    "https://huggingface.co/joeriben/stable-audio-open-small/resolve/b9e923dea7584371999e965da8b22e57ef6935ef"
static const ReassemblyAsset kSaoSmallAssets[] = {
    { T5YNTH_SAO_SMALL "/model.safetensors", "model.safetensors", 1676829212 },
    { T5YNTH_SAO_SMALL "/model_config.json", "model_config.json", 5622 },
};
#undef T5YNTH_SAO_SMALL

// Known models — extend this list to add new engines.
// downloadable: if false, show manual instructions only (no Download button).
//   Stable Audio Open 1.0 and SA3 install login-free via the reassembly path from
//   the ungated Comfy-Org mirrors. Stable Audio Open Small has no ungated mirror,
//   so it stays gated-manual: users fetch the two root files and Auto-Scan or
//   Browse... imports them. AudioLDM2 is ungated and downloads directly from HF.
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
    // Installs login-free from the ungated Comfy-Org mirror (kSaoOpen10Assets);
    // the gated hfRepo is kept ONLY so the Auto-Scan fallback can verify a
    // hand-downloaded Stability copy against the live HuggingFace manifest.
    { "stable-audio-open-1.0",   "Stable Audio Open 1.0",     "stabilityai/stable-audio-open-1.0", nullptr,
      "https://stability.ai/community-license-agreement",
      "This model is licensed under the Stability AI Community License.\n\n"
      "- Non-commercial use: free\n"
      "- Commercial use under $1M annual revenue: free (register at stability.ai)\n"
      "- Commercial use over $1M: enterprise license required\n\n"
      "T5ynth does not provide the weights; they download from the ungated\n"
      "Comfy-Org repository. By downloading you accept the license terms and take\n"
      "responsibility for compliance. A copy is written into the model folder.\n\n"
      "This engine also installs the T5-Base text encoder (Apache-2.0,\n"
      "unrestricted), which it requires.", true, true,
      nullptr, 0, nullptr, kSaoOpen10Assets, 1 },
    // Installs login-free from T5ynth's own ungated mirror (kSaoSmallAssets) -- the
    // only Stable Audio Open Small source T5ynth's native loader can use; the gated
    // hfRepo is kept ONLY so the Auto-Scan fallback can verify a hand-downloaded
    // Stability copy against the live HuggingFace manifest.
    { "stable-audio-open-small", "Stable Audio Open Small", "stabilityai/stable-audio-open-small",
      nullptr,
      "https://stability.ai/community-license-agreement",
      "This model is licensed under the Stability AI Community License.\n\n"
      "- Non-commercial use: free\n"
      "- Commercial use under $1M annual revenue: free (register at stability.ai)\n"
      "- Commercial use over $1M: enterprise license required\n\n"
      "T5ynth does not provide the weights; they download from an ungated mirror\n"
      "that redistributes the official weights unmodified. By downloading you accept\n"
      "the license terms and take responsibility for compliance. A copy is written\n"
      "into the model folder.\n\n"
      "This engine also installs the T5-Base text encoder (Apache-2.0,\n"
      "unrestricted), which it requires.", true, true,
      nullptr, 0, nullptr, kSaoSmallAssets, 2 },
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
      "Required by the Stable Audio Open engines (1.0 and Small) as the text\n"
      "encoder. T5ynth does not provide the weights. By downloading you accept\n"
      "the Apache 2.0 license.", true, false,
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
    // SA3 Medium — the larger (2.3B-param, ~9.2 GB) SA3 checkpoint. ONE model
    // renders both Music and SFX; the domain is chosen per request via track_type
    // (set by the engine slot), not by the dir name. Installs login-free from the
    // ungated Comfy-Org mirror (kSa3MediumAssets); the gated hfRepo is kept ONLY
    // so the Auto-Scan fallback can verify a hand-downloaded Stability copy against
    // the live HuggingFace manifest. Same t5gemma-b-b-ul2 encoder as the small
    // tiers; needs ~16 GB unified memory / VRAM (small needs ~8).
    { "stable-audio-3-medium", "Stable Audio 3 Medium", "stabilityai/stable-audio-3-medium", nullptr,
      "https://stability.ai/community-license-agreement",
      "Stable Audio 3 Medium bundles two separately licensed components:\n\n"
      "1) The audio model -- Stability AI Community License:\n"
      "   - Non-commercial use: free\n"
      "   - Commercial use under $1M annual revenue: free (register at stability.ai)\n"
      "   - Commercial use over $1M: enterprise license required\n\n"
      "2) The t5gemma text encoder -- Google Gemma Terms of Use and the Gemma\n"
      "   Prohibited Use Policy (ai.google.dev/gemma/terms).\n\n"
      "This is the larger SA3 checkpoint (~9.2 GB download, ~16 GB memory to run).\n"
      "T5ynth does not provide the weights; they download from the ungated\n"
      "Comfy-Org repository. By downloading you accept BOTH licenses and take\n"
      "responsibility for compliance. Copies are written into the model folder.", true, true,
      nullptr, 0, "t5gemma-b-b-ul2", kSa3MediumAssets, 2 },
    // Optional prompt-translation LLM (NOT a generation engine). When enabled in
    // PromptPanel, it translates an international prompt to English in the
    // background before the audio model conditions on it (Stable Audio models are
    // trained on English captions); the user's typed text is never changed.
    // isGenerationEngine=false routes it through the plain HF tree-API download
    // and keeps it out of the engine rows AND the backend-activation glue
    // (onDownloadFinished). It installs to <model root>/translation/
    // qwen2.5-1.5b-instruct, exactly where the backend auto-discovers it.
    // Ungated, Apache-2.0, no HuggingFace account.
    { "translation/qwen2.5-1.5b-instruct", "Prompt translation (Qwen2.5-1.5B)",
      "Qwen/Qwen2.5-1.5B-Instruct", nullptr,
      "https://www.apache.org/licenses/LICENSE-2.0",
      "Qwen2.5-1.5B-Instruct is licensed under Apache License 2.0 (open, no "
      "restrictions).\n\n"
      "Optional helper: when you enable the EN toggle next to the prompt, it "
      "translates your prompt to English in the background before the audio model "
      "sees it. Your typed text is never changed. T5ynth does not provide the "
      "weights; they download from HuggingFace (ungated, no account). By "
      "downloading you accept the Apache 2.0 license.", true, false,
      nullptr, 0 },
    // The LCO's author LLM: a 7B CODING model (Qwen2.5-Coder-7B-Instruct) that
    // writes the Csound orchestra for the user's prompt. NOT a generation engine:
    // isGenerationEngine=false routes it through the plain HF tree-API download and
    // keeps it out of the engine rows AND the backend-activation glue
    // (onDownloadFinished), exactly like the translation model above. It installs
    // to <model root>/coder/qwen2.5-coder-7b-instruct, where the backend's
    // _resolve_coder_model_dir discovers it (it also accepts a dev drop at
    // <model root>/lco-coder/...). Ungated (no HuggingFace account needed) and
    // Apache-2.0 licensed -- open, no commercial restriction.
    { "coder/qwen2.5-coder-7b-instruct", "LCO coder (Qwen2.5-Coder-7B)",
      "Qwen/Qwen2.5-Coder-7B-Instruct", nullptr,
      "https://www.apache.org/licenses/LICENSE-2.0",
      "Qwen2.5-Coder-7B-Instruct is licensed under Apache License 2.0 (open, no "
      "restrictions).\n\n"
      "Required for the LCO: this 7B coding model writes the Csound orchestra for "
      "your prompt. About 15 GB. T5ynth does not provide the weights; they download "
      "from HuggingFace (ungated, no account). By downloading you accept the "
      "Apache 2.0 license.", true, false,
      nullptr, 0 },
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

// Index into kKnownModels for the model an action currently targets. Driven by
// activeOpModelId_ (set when an action starts — currently bridged from the model
// chooser, in Commit 4 from a per-model row click); falls back to entry 0. This
// is the single source the selectedXxx() accessors read, so no UI widget is on
// the path between "which model" and the download/scan/status logic.
static int catalogIndexForId(const juce::String& id)
{
    for (int i = 0; i < kNumKnownModels; ++i)
        if (id == kKnownModels[i].id) return i;
    return 0;
}

juce::String SettingsPage::selectedModelId()
{
    return kKnownModels[catalogIndexForId(activeOpModelId_)].id;
}

juce::String SettingsPage::selectedHfRepo()
{
    return kKnownModels[catalogIndexForId(activeOpModelId_)].hfRepo;
}

juce::String SettingsPage::selectedGhRelease()
{
    const auto& km = kKnownModels[catalogIndexForId(activeOpModelId_)];
    return km.ghRelease != nullptr ? juce::String(km.ghRelease) : juce::String();
}

bool SettingsPage::selectedDownloadable()
{
    return kKnownModels[catalogIndexForId(activeOpModelId_)].downloadable;
}

bool SettingsPage::selectedIsGenerationEngine()
{
    return kKnownModels[catalogIndexForId(activeOpModelId_)].isGenerationEngine;
}

juce::String SettingsPage::selectedModelDisplay()
{
    return kKnownModels[catalogIndexForId(activeOpModelId_)].displayName;
}

const void* SettingsPage::selectedGhFiles()
{
    return static_cast<const void*>(kKnownModels[catalogIndexForId(activeOpModelId_)].ghFiles);
}

int SettingsPage::selectedGhFileCount()
{
    return kKnownModels[catalogIndexForId(activeOpModelId_)].ghFileCount;
}

const void* SettingsPage::selectedAssets()
{
    return static_cast<const void*>(kKnownModels[catalogIndexForId(activeOpModelId_)].assets);
}

int SettingsPage::selectedAssetCount()
{
    return kKnownModels[catalogIndexForId(activeOpModelId_)].assetCount;
}

// Forward decls: the by-id scan + completeness gate are defined further down, but
// the row callbacks below (constructor) and refreshAllRows() call them earlier.
static juce::File scanForModelById(const juce::String& id, const juce::String& hfRepo);
static bool modelHasRequiredAuxAssets(const juce::String& id, const juce::File& modelDir);

// The five generation engines shown in the Model Manager, in PromptPanel family
// order (SA3 first, then SAO, then AudioLDM2). familyHeader is non-null only on
// the first row of each family; it draws a thin small-caps group label above the
// row. sublabel makes the otherwise-invisible encoder dependency honest. The two
// text encoders (t5-base, t5gemma) deliberately have NO row — they auto-install
// with their engine, so the interface looks identical for both families.
struct RowSpec { const char* id; const char* sublabel; const char* familyHeader; };
static const RowSpec kRowSpecs[] = {
    { "stable-audio-3-small-music", "t5gemma encoder included",       "STABLE AUDIO 3" },
    { "stable-audio-3-small-sfx",   "t5gemma encoder included",       nullptr },
    { "stable-audio-3-medium",      "t5gemma encoder included",       nullptr },
    { "stable-audio-open-1.0",      "T5-Base encoder auto-installed", "STABLE AUDIO OPEN" },
    { "stable-audio-open-small",    "T5-Base encoder auto-installed", nullptr },
    { "audioldm2",                  "self-contained",                 "AUDIOLDM2" },
};
static constexpr int kNumRowSpecs = static_cast<int>(sizeof(kRowSpecs) / sizeof(kRowSpecs[0]));

// ── ModelRow ──────────────────────────────────────────────────────────────────
SettingsPage::ModelRow::ModelRow(juce::String id, juce::String displayName, juce::String sublabel)
    : modelId_(std::move(id)), displayName_(std::move(displayName)), sublabel_(std::move(sublabel))
{
    applyActionButtonStyle(false);
    // The single button is the primary action when idle and the Cancel button
    // while this row's download is in flight; route by the current mode.
    actionBtn_.onClick = [this] {
        if (downloading_) { if (onCancel) onCancel(modelId_); }
        else              { if (onAction) onAction(modelId_); }
    };
    addAndMakeVisible(actionBtn_);
}

void SettingsPage::ModelRow::applyActionButtonStyle(bool downloadingStyle)
{
    // Green = the primary "Download" / "Get files" action. Neutral grey kBorder =
    // the "Cancel" affordance while downloading — clearly not the go-action, and
    // built from existing palette tokens (no ad-hoc hex). Text stays light.
    actionBtn_.setColour(juce::TextButton::buttonColourId,
                         downloadingStyle ? kBorder : juce::Colour(0xff2d6a4f));
    actionBtn_.setColour(juce::TextButton::textColourOffId,
                         downloadingStyle ? kTextPrimary : juce::Colours::white);
}

void SettingsPage::ModelRow::resized()
{
    auto r = getLocalBounds().reduced(6, 4);
    r.removeFromRight(16);  // installed-light column (painted)
    r.removeFromRight(6);
    actionBtn_.setBounds(r.removeFromRight(100).reduced(0, 3));
}

void SettingsPage::ModelRow::setState(bool installed, const juce::String& statusText,
                                      Action action, bool actionEnabled)
{
    const char* label = action == Action::Download ? "Download"
                      : action == Action::GetFiles ? "Get files..."
                                                    : nullptr;  // Installed -> no button

    // Leaving download mode always needs a visual reset (button text/colour +
    // dropping the meter), so it bypasses the same-visual short-circuit below.
    const bool exitingDownload = downloading_;
    const bool sameVisual = !exitingDownload
                            && (installed == installed_)
                            && (statusText == statusText_)
                            && (action == action_);
    // Button enabled-state is cheap and has no repaint side-effect, so apply it
    // unconditionally; only the painted state (light/status/label) gates repaint.
    actionBtn_.setEnabled(actionEnabled);

    if (sameVisual)
        return;

    if (exitingDownload)
    {
        downloading_      = false;
        progress_         = 0.0;
        progressPermille_ = -1;
        detail_.clear();
        applyActionButtonStyle(false);   // restore the green primary styling
    }

    installed_  = installed;
    statusText_ = statusText;
    action_     = action;

    if (label != nullptr)
    {
        actionBtn_.setButtonText(label);
        actionBtn_.setVisible(true);
    }
    else
    {
        actionBtn_.setVisible(false);
    }
    repaint();
}

void SettingsPage::ModelRow::enterDownloadingState()
{
    if (downloading_)
        return;                          // idempotent — refreshAllRows may re-call
    downloading_      = true;
    progress_         = 0.0;
    progressPermille_ = -1;
    detail_           = "Starting...";
    applyActionButtonStyle(true);
    actionBtn_.setButtonText("Cancel");
    actionBtn_.setVisible(true);
    actionBtn_.setEnabled(true);         // Cancel stays clickable while "busy"
    repaint();
}

void SettingsPage::ModelRow::setDownloadProgress(double fraction, const juce::String& detail)
{
    if (!downloading_)
        enterDownloadingState();         // defensive: timer may fire before refresh

    const double f        = juce::jlimit(0.0, 1.0, fraction);
    const int    permille = juce::roundToInt(f * 1000.0);  // 0.1% repaint quantum

    bool changed = false;
    if (permille != progressPermille_) { progressPermille_ = permille; progress_ = f; changed = true; }
    if (detail != detail_)             { detail_ = detail;                            changed = true; }
    if (changed)
        repaint();
}

void SettingsPage::ModelRow::paint(juce::Graphics& g)
{
    auto b = getLocalBounds();

    // Foot of the row: an inline progress meter while downloading (this row's own
    // primary progress indicator), otherwise the thin Ableton-like separator.
    {
        const float x0 = (float) b.getX() + 6.0f;
        const float x1 = (float) b.getRight() - 6.0f;
        if (downloading_)
        {
            const float h = 3.0f;
            const float y = (float) b.getBottom() - h - 1.0f;
            g.setColour(kBorder.withAlpha(0.6f));
            g.fillRect(x0, y, x1 - x0, h);
            g.setColour(kAccent);
            g.fillRect(x0, y, (x1 - x0) * (float) progress_, h);
        }
        else
        {
            g.setColour(kBorder.withAlpha(0.45f));
            g.drawHorizontalLine(b.getBottom() - 1, x0, x1);
        }
    }

    auto r = b.reduced(6, 4);

    // Installed light (far right): green when fully installed, dim grey otherwise.
    auto lightCol = r.removeFromRight(16);
    {
        const float d = 9.0f;
        const float cx = (float) lightCol.getCentreX() - d * 0.5f;
        const float cy = (float) lightCol.getCentreY() - d * 0.5f;
        g.setColour(installed_ ? kSeqCol : kBorder.brighter(0.2f));
        g.fillEllipse(cx, cy, d, d);
    }
    r.removeFromRight(6);
    r.removeFromRight(100);  // action-button column (drawn by the child button)
    r.removeFromRight(8);

    // Status text (idle) or the live download readout (e.g. "0.5/2.3 GB · 14
    // MB/s"), right-aligned before the button column. The readout needs more
    // room than a short idle status, so its slot is wider.
    const int statusW = downloading_ ? juce::jmin(190, r.getWidth() * 2 / 3)
                                     : juce::jmin(120, r.getWidth() / 2);
    auto statusArea = r.removeFromRight(statusW);
    g.setColour(downloading_ ? kAccent : (installed_ ? kSeqCol : kDimmer));
    g.setFont(juce::FontOptions(downloading_ ? 10.5f : 11.0f));
    g.drawText(downloading_ ? detail_ : statusText_, statusArea,
               juce::Justification::centredRight, true);

    // Name (bold) over a muted encoder sublabel.
    auto top = r.removeFromTop(r.getHeight() / 2 + 1);
    g.setColour(kTextPrimary);
    g.setFont(juce::FontOptions(13.0f).withStyle("Bold"));
    g.drawText(displayName_, top, juce::Justification::bottomLeft, true);
    g.setColour(kDimmer);
    g.setFont(juce::FontOptions(10.5f));
    g.drawText(sublabel_, r, juce::Justification::topLeft, true);
}

void SettingsPage::ModelRow::mouseDown(const juce::MouseEvent& e)
{
    if (!e.mods.isPopupMenu())
        return;

    juce::PopupMenu m;
    m.addItem(1, "Open model page");
    m.addItem(2, "Browse for files...");
    if (installed_)
        m.addItem(3, "Reveal in file browser");

    juce::Component::SafePointer<ModelRow> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
        [safe](int result)
        {
            auto* self = safe.getComponent();
            if (self == nullptr) return;
            if (result == 1 && self->onOpenPage) self->onOpenPage(self->modelId_);
            else if (result == 2 && self->onBrowse) self->onBrowse(self->modelId_);
            else if (result == 3 && self->onReveal) self->onReveal(self->modelId_);
        });
}

SettingsPage::SettingsPage()
{
    titleLabel.setText("Model Manager", juce::dontSendNotification);
    titleLabel.setColour(juce::Label::textColourId, kAccent);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

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

    // Build the five fixed engine rows + remember each family's header text.
    // activeOpModelId_ tracks which row's action is in flight (the single source
    // selectedXxx() reads); it is set the moment a row button or menu fires.
    activeOpModelId_ = kRowSpecs[0].id;
    for (int i = 0; i < kNumRowSpecs; ++i)
    {
        const auto& spec = kRowSpecs[i];
        const auto& km = kKnownModels[catalogIndexForId(spec.id)];
        auto row = std::make_unique<ModelRow>(spec.id, km.displayName, spec.sublabel);

        row->onAction = [this](juce::String id) {
            activeOpModelId_ = id;
            // Downloadable engines fetch in-app; gated ones (SAO Small) run the
            // manifest-driven manual import (Downloads scan + folder picker).
            if (kKnownModels[catalogIndexForId(id)].downloadable)
                startDownload();
            else
                performAutoScan();
        };
        row->onOpenPage = [this](juce::String id) {
            activeOpModelId_ = id;
            juce::URL("https://huggingface.co/" + selectedHfRepo()).launchInDefaultBrowser();
        };
        row->onBrowse = [this](juce::String id) {
            activeOpModelId_ = id;
            browseForModel();
        };
        row->onReveal = [](juce::String id) {
            const auto& m = kKnownModels[catalogIndexForId(id)];
            auto dir = scanForModelById(id, m.hfRepo);
            if (dir.exists()) dir.revealToUser();
        };
        row->onCancel = [this](juce::String) { cancelDownload(); };

        addAndMakeVisible(*row);
        rows_.push_back(std::move(row));
    }
    // familyHeaders_ (text + rect) is rebuilt in resized() and consumed by paint().

    // Optional prompt-translation row — same ModelRow widget as the engines (its
    // "PROMPT TRANSLATION" family header is painted in paint()), but the model is
    // auxiliary: clicking Download sets activeOpModelId_ and runs the normal
    // download path, which onDownloadFinished routes around the engine glue. The
    // longer explanation lives in the detail strip (updateStatus), exactly like
    // the engines describe themselves there. No onBrowse — there is nothing to
    // import by hand for this ungated auto-discovered helper.
    {
        const auto& tm = kKnownModels[catalogIndexForId("translation/qwen2.5-1.5b-instruct")];
        translationRow_ = std::make_unique<ModelRow>("translation/qwen2.5-1.5b-instruct",
                                                     tm.displayName,
                                                     "translates prompts to English");
        translationRow_->onAction = [this](juce::String id) {
            activeOpModelId_ = id;
            startDownload();
        };
        translationRow_->onCancel = [this](juce::String) { cancelDownload(); };
        translationRow_->onOpenPage = [this](juce::String id) {
            activeOpModelId_ = id;
            juce::URL("https://huggingface.co/" + selectedHfRepo()).launchInDefaultBrowser();
        };
        translationRow_->onReveal = [](juce::String) {
            auto dir = getAppSupportModelDir("translation/qwen2.5-1.5b-instruct");
            if (dir.exists()) dir.revealToUser();
        };
        addAndMakeVisible(*translationRow_);
    }

    // Optional LCO-coder row — same ModelRow widget as the translation row (its
    // own "LCO CODER" family header is painted in paint()), and likewise
    // auxiliary: clicking Download sets activeOpModelId_ and runs the normal
    // download path, which onDownloadFinished routes around the engine glue. No
    // onBrowse — nothing to import by hand for this ungated auto-discovered helper.
    {
        const auto& cm = kKnownModels[catalogIndexForId("coder/qwen2.5-coder-7b-instruct")];
        coderRow_ = std::make_unique<ModelRow>("coder/qwen2.5-coder-7b-instruct",
                                               cm.displayName,
                                               "writes the Csound orchestra for the LCO");
        coderRow_->onAction = [this](juce::String id) {
            activeOpModelId_ = id;
            startDownload();
        };
        coderRow_->onCancel = [this](juce::String) { cancelDownload(); };
        coderRow_->onOpenPage = [this](juce::String id) {
            activeOpModelId_ = id;
            juce::URL("https://huggingface.co/" + selectedHfRepo()).launchInDefaultBrowser();
        };
        coderRow_->onReveal = [](juce::String) {
            auto dir = getAppSupportModelDir("coder/qwen2.5-coder-7b-instruct");
            if (dir.exists()) dir.revealToUser();
        };
        addAndMakeVisible(*coderRow_);
    }

    auto found = scanForModel();
    if (found.exists()) modelPath = found;
    updateStatus();  // calls refreshAllRows() -> refreshTranslationRow() + refreshCoderRow()

    setSize(500, 480);
}

void SettingsPage::setModelInstallBusy(bool busy, const juce::String& statusText)
{
    modelInstallBusy_.store(busy);
    refreshAllRows();  // each row's action button enables iff !busy && !downloading

    if (statusText.isNotEmpty())
    {
        downloadStatusLabel.setText(statusText, juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId,
                                      busy ? kAccent : juce::Colour(0xffcccccc));
    }
}

// scanForModelById is defined just below; forward-declared so the SAO t5-base
// peer check (an aux dependency that lives in a SIBLING model dir, not an
// in-repo subfolder) can reuse the same scan logic without reordering the file.
static juce::File scanForModelById(const juce::String& id, const juce::String& hfRepo);

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

    // Stable Audio Open 1.0 / Small additionally require the t5-base text encoder,
    // which lives in a PEER model directory (not an in-repo subfolder). The backend
    // raises a RuntimeError at load time if t5-base is missing (pipe_inference.py),
    // so an SAO checkpoint is only truly "installed" once t5-base is present too.
    // (No recursion: id "t5-base" never reaches this branch.)
    if (id == "stable-audio-open-1.0" || id == "stable-audio-open-small")
    {
        auto t5 = scanForModelById("t5-base", "t5-base");
        if (!t5.exists() || !hasModelMarker(t5))
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

// True once a COMPLETE t5-base text encoder is installed anywhere the scan looks.
// Uses the SAME probe as the SAO "Installed" gate in modelHasRequiredAuxAssets, so
// the green light and the "do we still need to fetch t5-base?" decision always
// agree. The Stable Audio Open engines auto-chain this when it is missing.
static bool t5BaseInstalled()
{
    auto d = scanForModelById("t5-base", "t5-base");
    return d.exists() && hasModelMarker(d);
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

    // The import MOVES this directory into the app's model folder, so a shared
    // folder must never be accepted: importing ~/Downloads would move the whole
    // Downloads folder (and used to make the model slot mean "whatever is in
    // Downloads right now").
    if (isProtectedUserFolder(sourceDir))
        return juce::Result::fail("Refusing to import a shared system folder as a model:\n  "
                                  + sourceDir.getFullPathName()
                                  + "\n\nPut the model files in their own folder and "
                                    "select that folder.");

    auto targetDir = getAppSupportModelDir(modelId);
    const auto sourcePath = sourceDir.getFullPathName();
    const auto targetPath = targetDir.getFullPathName();

    if (sourcePath == targetPath)
    {
        activeDir = targetDir;
        return juce::Result::ok();
    }

    // A slot that IS a symlink is a LEGACY import (this function used to link the
    // source in instead of moving it). It is never valid now — a model must be a
    // real directory inside the app's model folder — so the link is dropped and the
    // import proceeds. Deleting a symlink removes the LINK only, never the files it
    // points at: juce::File::deleteRecursively does not follow symlinks by default.
    if (targetDir.isSymbolicLink())
    {
        targetDir.deleteRecursively();
    }
    else if (targetDir.exists())
    {
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

    // COPY the model INTO the models folder — never link back to wherever the user
    // happened to leave it. A link left multi-GB weights sitting in the user's
    // Downloads folder and made the slot mean "whatever is in Downloads right now";
    // it also broke silently when the source was moved, renamed or cache-pruned.
    // The user's own copy is NEVER touched: nothing outside the models folder is
    // moved or deleted. Only a partial copy WE just wrote is cleaned up on failure.
    if (!sourceDir.copyDirectoryTo(targetDir))
    {
        targetDir.deleteRecursively();
        return juce::Result::fail("Could not copy the model into:\n  "
                                  + targetPath
                                  + "\n\nSource:\n  "
                                  + sourcePath);
    }

    if (!hasModelMarker(targetDir))
    {
        targetDir.deleteRecursively();
        return juce::Result::fail("The model copy is incomplete:\n  " + targetPath);
    }

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
    const auto id = selectedModelId();
    const bool usableModel = modelHasRequiredAuxAssets(id, modelPath);

    if (modelPath.exists() && usableModel && !backendConnected)
    {
        backendFailReason = {};
        backendStatusLabel.setText("Backend: Starting...", juce::dontSendNotification);
        backendStatusLabel.setColour(juce::Label::textColourId, kDim);
    }

    updateStatus();
    if (modelPath.exists() && usableModel && onModelReady)
        onModelReady();

    // A Stable Audio Open engine imported manually (Auto-Scan / Browse) is present
    // but not yet usable because its peer t5-base encoder is missing. The in-app SAO
    // download chains t5-base on its own thread, so this fires only for manual
    // imports. Fetch it now; onDownloadFinished then re-enters setModelPath, which
    // fires onModelReady once t5-base has landed. The !usableModel + !t5BaseInstalled
    // guard makes the re-entry a no-op, so it can never loop.
    if (! usableModel && ! downloading.load()
        && (id == "stable-audio-open-1.0" || id == "stable-audio-open-small")
        && modelPath.exists() && hasModelMarker(modelPath) && ! t5BaseInstalled())
    {
        startT5BaseChainDownload();
    }
}

void SettingsPage::browseForModel()
{
    // One operation at a time: a manual browse-import must not race an in-flight
    // download or the silent Downloads pre-scan/import (both leave activeOpModelId_
    // / modelPath / the busy flags in a state a concurrent import would corrupt).
    if (modelInstallBusy_.load() || downloading.load())
        return;
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

// The standard T5ynth model install roots, in scan-priority order. Shared by the
// duplicate-detection and cross-variant reuse checks so the two never drift apart
// (a root added here is honoured by both).
static juce::Array<juce::File> standardModelRoots()
{
    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    juce::Array<juce::File> roots;
   #if JUCE_MAC
    roots.add(juce::File("/Library/Application Support/T5ynth/models"));
   #endif
    roots.add(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                  .getChildFile("T5ynth/models"));
    roots.add(home.getChildFile("Library/T5ynth/models"));
    roots.add(home.getChildFile("t5ynth/models"));
    return roots;
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

    for (auto& root : standardModelRoots())
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

// Find an identical, already-downloaded copy of `relPath` (same canonical
// sub-path, same published size) in a DIFFERENT model dir, so installing a
// second model that shares a byte-identical component can copy it locally
// instead of re-downloading. The t5gemma encoder is byte-identical across SA3
// Small Music/SFX (only the root checkpoint differs), so callers scope this to
// the shared encoder subfolder. Size equality against the catalog's multi-GB
// expectedSize is the same fingerprint findSameSizeInstalledModel trusts, and it
// also rules out any tiny LFS-pointer stand-in. Empty File when nothing matches.
static juce::File findReusableSiblingAsset(const juce::String& selfModelId,
                                           const juce::String& relPath,
                                           int64_t expectedSize)
{
    if (expectedSize <= 0)
        return {};

    for (auto& root : standardModelRoots())
    {
        if (!root.isDirectory())
            continue;
        for (auto& sub : root.findChildFiles(juce::File::findDirectories, false))
        {
            if (sub.getFileName() == selfModelId)
                continue;
            auto candidate = sub.getChildFile(relPath);
            if (candidate.existsAsFile() && candidate.getSize() == expectedSize)
                return candidate;
        }
    }
    return {};
}

// Parse a safetensors header and return the total file size it declares:
// 8 (the u64 LE length prefix) + headerLen + max(tensor data_offsets.end).
// Returns -1 if the file is too short to hold its declared header or the header
// is not parseable JSON. Two uses in the reassembly installer:
//   • before resuming, tell a genuine partial/complete of the TARGET (declared
//     == expectedSize) apart from a FOREIGN file left at the path (e.g. a Stable
//     Audio Open model.safetensors, declared 1676829212) — resuming onto a
//     foreign file appends our tail to their head and silently yields a wrong
//     file that still hits expectedSize ("file not fully covered" on load);
//   • after a download, confirm the file is internally consistent
//     (declared == its own byte size) rather than a resume-glued hybrid.
static juce::int64 safetensorsDeclaredTotal(const juce::File& file)
{
    juce::FileInputStream in(file);
    if (! in.openedOk())
        return -1;

    char lenBuf[8] = {};
    if (in.read(lenBuf, 8) != 8)
        return -1;
    const juce::int64 headerLen = (juce::int64) juce::ByteOrder::littleEndianInt64(lenBuf);
    if (headerLen <= 0 || headerLen > 100ll * 1024 * 1024
        || (8 + headerLen) > file.getSize())
        return -1;

    juce::MemoryBlock headerBytes;
    if ((juce::int64) in.readIntoMemoryBlock(headerBytes, (ssize_t) headerLen) != headerLen)
        return -1;
    try
    {
        auto header = nlohmann::json::parse(
            std::string(static_cast<const char*>(headerBytes.getData()), headerBytes.getSize()));
        juce::int64 maxEnd = 0;
        for (auto it = header.begin(); it != header.end(); ++it)
        {
            if (it.key() == "__metadata__")
                continue;
            const auto& v = it.value();
            if (! v.contains("data_offsets"))
                continue;
            const auto& off = v["data_offsets"];
            if (off.is_array() && off.size() == 2)
            {
                const juce::int64 end = off[1].get<juce::int64>();
                if (end > maxEnd)
                    maxEnd = end;
            }
        }
        return 8 + headerLen + maxEnd;
    }
    catch (const std::exception&)
    {
        return -1;
    }
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

// The native-Stability models whose files a user might have fetched by hand from
// the model page into ~/Downloads (root model.safetensors [+ model_config.json]
// [+ a t5gemma encoder subfolder]). For these — and only these — "Download" looks
// in the Downloads folder first: diffusers (audioldm2) and the auxiliary encoders
// have no single-checkpoint layout a user would hand-download, and fetchModelManifest
// only describes the native-Stability layout anyway.
static bool isNativeStabilityModel(const juce::String& modelId)
{
    return modelId == "stable-audio-open-1.0"
        || modelId == "stable-audio-open-small"
        || modelId == "stable-audio-3-small-music"
        || modelId == "stable-audio-3-small-sfx"
        || modelId == "stable-audio-3-medium";
}

// Cheap, network-free gate in front of the silent Downloads pre-scan: is there any
// .safetensors in ~/Downloads at all? If not there is nothing to match, so the
// Download action skips the manifest fetch entirely and starts the repo download
// immediately — the common (empty-Downloads) case stays instant and truly silent.
static bool downloadsHasModelCandidate()
{
    auto dl = getDownloadsFolder();
    return dl.isDirectory()
        && ! dl.findChildFiles(juce::File::findFiles, /*recursive*/ false, "*.safetensors").isEmpty();
}

// Side-effect-free completeness check: does `folder` already hold an exact match
// (canonical name, browser-rename tolerant, EXACT manifest byte size) for EVERY
// manifest entry? Mirrors installFromManifestFolder's staging WITHOUT touching any
// UI, so the Download flow can decide "import locally vs. fetch from the repo"
// silently — no checklist, no dialog, no picker — when the files aren't all there.
static bool manifestCompleteInFolder(const juce::File& folder,
                                     const std::vector<ManifestEntry>& manifest)
{
    if (manifest.empty() || ! folder.isDirectory())
        return false;
    for (const auto& e : manifest)
    {
        const auto base = e.relPath.fromLastOccurrenceOf("/", false, false);
        auto cands = findRenameCandidates(folder, base);
        auto exact = folder.getChildFile(e.relPath);   // structured-clone case
        if (exact.existsAsFile())
            cands.addIfNotAlreadyThere(exact);
        bool sizeMatch = false;
        for (auto& c : cands)
            if (c.getSize() == e.size) { sizeMatch = true; break; }
        if (! sizeMatch)
            return false;
    }
    return true;
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
        if (reportIfMissing)
        {
            setInstructionsText(instructionsLabel, buildChecklist());
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon,
                modelDisplayName + " -- files still needed",
                "Looked in:\n  " + sourceFolder.getFullPathName() + "\n\n" + buildChecklist());
            return InstallOutcome::AbortedWithDialog;
        }
        // Silent caller (the Download pre-scan): leave the detail strip untouched
        // so a fall-through to the repo download shows no stale "files needed"
        // checklist. The caller decides what to do with NotInstalled.
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

        // Stable Audio 3 Small *Music* and *SFX* share the SAME architecture and the
        // EXACT same checkpoint byte size (2,270,384,940 B) but carry DIFFERENT
        // weights — verified by hashing the Comfy-Org weights: the data sections
        // differ and 440 of 685 tensors diverge (separate fine-tunes, NOT the same
        // file). Because this guard fingerprints by SIZE ALONE, two SA3 variants
        // collide as a false positive, so skip it for the SA3 family — a matching
        // size between SA3 variants is EXPECTED, not a wrong-file mistake. (Size
        // alone cannot tell the two SA3 checkpoints apart at all; catching a genuine
        // wrong-SA3-file pick would need a content hash, out of scope here.) The one
        // component that IS byte-identical across the variants is the t5gemma encoder,
        // which the reassembly installer copies locally instead of re-downloading.
        // The guard below still protects every other model family (where identical
        // sizes across variants really are vanishingly rare).
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
                                                        kError);
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon, errorTitle, errorBody);
                    return;
                }

                self->downloadStatusLabel.setText("Model copied. Activating...",
                                                  juce::dontSendNotification);
                self->downloadStatusLabel.setColour(juce::Label::textColourId,
                                                    kSuccess);

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
                                          kError);
            setInstructionsText(instructionsLabel, importResult.getErrorMessage());
            return;
        }

        setModelPath(activeDir);
        downloadStatusLabel.setText("Model imported: " + activeDir.getFullPathName(),
                                    juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId,
                                      kSuccess);
        return;
    }

    // 2. Native-Stability smart scan: the only remaining manual-install
    //    category. SAO 1.0 / SAO Small are a 2-file checkpoint; SA3 Small Music
    //    additionally ships the t5gemma-b-b-ul2 text-encoder subfolder. All are
    //    driven by the live HF manifest the user pulls into ~/Downloads.
    auto modelId = selectedModelId();
    if (! isNativeStabilityModel(modelId))   // shared with the Download pre-scan
    {
        updateStatus();
        downloadStatusLabel.setText(
            "No model found in standard locations. Follow the instructions below.",
            juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId,
                                      kError);
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
                                                    kError);
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
                                                          kError);
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
    int idx = catalogIndexForId(activeOpModelId_);
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

    const auto modelId = selectedModelId();
    // Native Stability models look in the Downloads folder FIRST: if the user has
    // already hand-fetched the exact files (name + size) from the model page,
    // import them and skip the multi-GB repo download. Silent by design — anything
    // missing just falls through to the normal download, no dialog or picker. The
    // cheap downloadsHasModelCandidate() gate keeps the empty-Downloads case
    // instant (no manifest network call when there's obviously nothing to match).
    if (isNativeStabilityModel(modelId) && downloadsHasModelCandidate())
    {
        scanDownloadsThenDownload();
        return;
    }

    startRepoDownload();
}

void SettingsPage::scanDownloadsThenDownload()
{
    const auto modelId     = selectedModelId();
    const auto hfRepo      = selectedHfRepo();
    const auto encoderSub  = encoderSubfolderForModelId(modelId);
    const auto displayName = selectedModelDisplay();

    // The manifest fetch is a small tree-API call (not the weights), off the
    // message thread. Buttons disable + a phase line shows while it runs.
    setModelInstallBusy(true, "Checking your Downloads folder...");
    juce::Component::SafePointer<SettingsPage> safeThis(this);
    std::thread([safeThis, hfRepo, encoderSub, modelId, displayName]()
    {
        auto manifest = fetchModelManifest(hfRepo, encoderSub);
        juce::MessageManager::callAsync([safeThis, manifest, modelId, displayName]()
        {
            auto* self = safeThis.getComponent();
            if (self == nullptr) return;
            self->setModelInstallBusy(false);
            // Re-pin focus to the model whose Download was clicked: a right-click
            // on another row during the check could have moved activeOpModelId_,
            // and both branches below read it (installFromManifestFolder ->
            // setModelPath, and startRepoDownload's selectedXxx()).
            self->activeOpModelId_ = modelId;

            auto downloads = getDownloadsFolder();
            if (! manifest.empty() && manifestCompleteInFolder(downloads, manifest))
            {
                // Exact match already in Downloads → import locally; no network.
                // installFromManifestFolder copies + activates (and setModelPath
                // chains t5-base for the SAO engines just like a Browse import).
                const auto outcome = self->installFromManifestFolder(
                    downloads, modelId, displayName, manifest, /*reportIfMissing*/ false);
                if (outcome == InstallOutcome::Installed
                    || outcome == InstallOutcome::AbortedWithDialog)
                    return;  // importing, or the duplicate-guard dialog was shown
                // NotInstalled here means a file changed under us between the
                // completeness check and the copy — fall through and fetch fresh.
            }
            // Not (fully) in Downloads → silent fall-through to the repo download.
            self->startRepoDownload();
        });
    }).detach();
}

void SettingsPage::startRepoDownload()
{
    auto ghRelease = selectedGhRelease();

    downloading = true;
    downloadModelId_ = activeOpModelId_;   // pin the meter's target for this transfer
    // Seed the speed sampler (lastTickMs_ == 0 => first timer tick only baselines).
    lastTickBytes_ = 0;
    lastTickMs_ = 0.0;
    speedBytesPerSec_ = 0.0;
    // The active row now carries the prominent progress meter; the footer line
    // keeps only neutral phase text ("Fetching file list...", etc.), not pink.
    refreshAllRows();  // downloading == true -> active row enters download mode, others disable
    downloadStatusLabel.setColour(juce::Label::textColourId, kDim);

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

// Downloads a fixed file set from <baseUrl>/<name> into targetDir, with resume
// (HTTP Range), 416 recovery, mid-stream-truncation detection, and a safetensors
// self-consistency gate — the engine shared by the GitHub-release path and the
// Stable Audio Open t5-base auto-chain. Runs on the CALLING (background) thread.
//   counter            : progress sink; stored value = baseBytesCompleted + bytes done here
//   baseBytesCompleted : bytes already finished by an earlier phase (combined progress)
//   phaseNoun          : optional label inserted into the status line (e.g. "text encoder")
//   setStatus          : posts a status string to the UI (caller marshals to the message thread)
// Returns true once every file is present at its expectedSize. On failure sets
// errMsg and returns false. On cancellation (cancelFlag set) returns false with
// errMsg empty — the caller checks cancelFlag and stays silent.
static bool downloadFileSet(const juce::String& baseUrl,
                            const GhAsset* files, int fileCount,
                            const juce::File& targetDir,
                            const std::shared_ptr<std::atomic<int64_t>>& counter,
                            const std::shared_ptr<std::atomic<bool>>& cancelFlag,
                            int64_t baseBytesCompleted,
                            const juce::String& phaseNoun,
                            const std::function<void(const juce::String&)>& setStatus,
                            juce::String& errMsg)
{
    int64_t bytesCompleted = 0;

    for (int i = 0; i < fileCount; ++i)
    {
        if (cancelFlag && cancelFlag->load())
            return false;

        auto& gf = files[i];
        auto fileName = juce::String(gf.name);
        auto targetFile = targetDir.getChildFile(fileName);

        // Resume-aware existing-file check:
        //   == expectedSize      → already complete, skip
        //   > expectedSize       → not our file (different version) — start fresh
        //   in (0, expectedSize) → partial — request HTTP Range and append
        //   == 0                 → fresh download
        int64_t existingBytes = targetFile.existsAsFile() ? targetFile.getSize() : 0;

        // Integrity gate for safetensors assets: trust the existing bytes for
        // skip/resume ONLY if their own safetensors header declares a file of
        // exactly expectedSize. A foreign checkpoint left at this path or a
        // previously resume-corrupted hybrid declares a DIFFERENT total — resuming
        // onto it appends our tail to their head and silently produces a wrong file
        // that still reaches expectedSize. Drop any such file and download fresh
        // from byte 0; this also self-heals an already-corrupt install.
        if (existingBytes > 0 && targetFile.hasFileExtension("safetensors")
            && safetensorsDeclaredTotal(targetFile) != gf.expectedSize)
        {
            targetFile.deleteFile();
            existingBytes = 0;
        }

        if (existingBytes == gf.expectedSize)
        {
            bytesCompleted += gf.expectedSize;
            if (counter) counter->store(baseBytesCompleted + bytesCompleted);
            continue;
        }
        if (existingBytes > gf.expectedSize)
        {
            targetFile.deleteFile();
            existingBytes = 0;
        }
        const bool tryResume = (existingBytes > 0);

        juce::String prefix = tryResume ? "Resuming" : "Downloading";
        if (phaseNoun.isNotEmpty()) prefix += " " + phaseNoun;
        if (setStatus)
            setStatus(prefix + ": " + fileName + " (" + juce::String(i + 1) + "/"
                      + juce::String(fileCount) + ")");

        juce::URL fileUrl(baseUrl + "/" + fileName);
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
            errMsg = "Connection failed for: " + fileName + "\n\nCheck your internet connection.";
            return false;
        }

        // 416 Range Not Satisfiable: the server file is smaller than our partial
        // (mirror re-uploaded). Clear the partial so the next click starts fresh
        // instead of re-sending the same unsatisfiable Range.
        if (statusCode == 416 && tryResume)
        {
            targetFile.deleteFile();
            errMsg = "The file on the server changed since your previous "
                     "download attempt: " + fileName + "\n\n"
                     "Your partial download has been cleared. Click "
                     "Download again to start fresh.";
            return false;
        }

        if (statusCode >= 400)
        {
            errMsg = "Server returned HTTP " + juce::String(statusCode) + " for: " + fileName;
            return false;
        }

        // 206 Partial Content → append. 200 OK with tryResume → server ignored
        // Range, restart from byte 0.
        const bool appendMode = tryResume && statusCode == 206;
        if (!appendMode)
            targetFile.deleteFile();

        auto outStream = targetFile.createOutputStream();
        if (!outStream)
        {
            errMsg = "Cannot write: " + fileName;
            return false;
        }
        // FileOutputStream seeks to end-of-file for existing files, so the write
        // cursor is already correct for appendMode == true.

        const int64_t initialBytes = appendMode ? existingBytes : 0;
        char buffer[65536];
        int64_t written = 0;
        while (true)
        {
            if (cancelFlag && cancelFlag->load())
                return false;
            auto bytesRead = stream->read(buffer, sizeof(buffer));
            if (bytesRead <= 0) break;
            outStream->write(buffer, static_cast<size_t>(bytesRead));
            written += bytesRead;
            if (counter) counter->store(baseBytesCompleted + bytesCompleted + initialBytes + written);
        }
        outStream.reset();

        // Skip the truncation check if cancellation interrupted the read.
        if (cancelFlag && cancelFlag->load())
            return false;

        // Network dropped mid-stream. Keep the partial so the next click resumes
        // from where it stopped — don't delete it.
        const int64_t finalBytes = targetFile.getSize();
        if (finalBytes < gf.expectedSize)
        {
            auto toStr = [](int64_t b) {
                return (b < 1024 * 1024) ? juce::String(b) + " bytes"
                                         : juce::String(b / (1024 * 1024)) + " MB";
            };
            errMsg = "Download was interrupted for " + fileName + ":\n"
                     "Expected " + toStr(gf.expectedSize) + ", received " + toStr(finalBytes) + ".\n\n"
                     "Click Download again to resume from where it stopped.";
            return false;
        }

        // All bytes present (>= expectedSize). Verify a safetensors asset is
        // internally consistent — its own header must declare exactly its own
        // size. A resume-glued hybrid or otherwise corrupt result declares a
        // different total ("file not fully covered" at load). Delete it so the
        // next click re-downloads cleanly instead of skipping a same-size-but-
        // broken file. Non-safetensors assets keep size-only behavior.
        if (targetFile.hasFileExtension("safetensors")
            && safetensorsDeclaredTotal(targetFile) != targetFile.getSize())
        {
            targetFile.deleteFile();
            errMsg = "The downloaded file was corrupt and has been removed: "
                     + fileName + "\n\nClick Download again to fetch it cleanly.";
            return false;
        }

        bytesCompleted += gf.expectedSize;
        if (counter) counter->store(baseBytesCompleted + bytesCompleted);
    }

    return true;
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
        auto setStatus = [safeThis](const juce::String& s) {
            juce::MessageManager::callAsync([safeThis, s]() {
                if (auto* self = safeThis.getComponent())
                    self->downloadStatusLabel.setText(s, juce::dontSendNotification);
            });
        };
        juce::String err;
        const bool ok = downloadFileSet(ghBase, ghFiles, ghFileCount, targetDir,
                                        progressCounter, cancelFlag, 0, {}, setStatus, err);
        if (cancelFlag && cancelFlag->load())
            return;
        juce::MessageManager::callAsync([safeThis, ok, err]() {
            if (auto* self = safeThis.getComponent())
                self->onDownloadFinished(ok, err);
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
                                  const juce::String& modelId,
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

    // model_config.json LAST — its presence is what flips the dir to "Installed",
    // so it goes after every encoder config + the tokenizer carve. The MEDIUM tier
    // has its own config (depth-24 differential DiT + SAME-L sliding-window AE);
    // small music+sfx share the other one (they differ only by checkpoint weights
    // and the request-side track_type, not by architecture).
    const bool isMedium = (modelId == "stable-audio-3-medium");
    return writeBundledFile(targetDir.getChildFile("model_config.json"),
                            isMedium ? BinaryData::sa3_medium_model_config_json
                                     : BinaryData::sa3_model_config_json,
                            isMedium ? BinaryData::sa3_medium_model_config_jsonSize
                                     : BinaryData::sa3_model_config_jsonSize, err);
}

// ── Stable Audio Open 1.0 reassembly finalize ──────────────────────────────────
// The ungated Comfy-Org "_repackaged" repo ships ONLY the checkpoint. SAO 1.0's
// loader also needs model_config.json (the canonical Stability config, bundled in
// the app). There is no in-dir text encoder to assemble -- SAO 1.0 conditions on
// t5-base, which the backend supplies separately -- so this writes just the SA
// Community License copy + model_config.json (last, the completeness marker). All
// file I/O, on the download thread.
static const char* const kSaoOpen10LicenseNotice =
R"NOTICE(T5ynth -- Stable Audio Open 1.0 model and its license
=========================================================

Powered by Stability AI.

This folder holds Stable Audio Open 1.0, assembled by T5ynth. T5ynth neither owns
nor relicenses the weights -- it only orchestrated the download and wrote the
configuration file. You are responsible for complying with the license below; the
FULL license text is in this folder.

--------------------------------------------------------------------------------
Audio model  --  model.safetensors, model_config.json
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
Provenance
--------------------------------------------------------------------------------
The model weights were downloaded by you, with no account or token, from the
ungated Comfy-Org repository:
  https://huggingface.co/Comfy-Org/stable-audio-open-1.0_repackaged
  (pinned revision a12a5d8f364768d908d417610a9e7f6ab8a6b5ac)

Before downloading you accepted the license above in T5ynth's download dialog.
T5ynth itself is free software under the GNU GPL v3; that license covers T5ynth's
own source code, NOT these third-party model weights.

UCDCAE AI Lab.
)NOTICE";

static bool finalizeSaoOpen10Reassembly(const juce::File& targetDir, juce::String& err)
{
    // License documentation (not a completeness marker, so order vs. the config is
    // free) + the verbatim Stability AI Community License that Agreement IV(a)(i)
    // requires distributing with the materials.
    if (! writeBundledFile(targetDir.getChildFile("MODEL_LICENSES.txt"),
                           kSaoOpen10LicenseNotice,
                           (int) std::strlen(kSaoOpen10LicenseNotice), err))
        return false;
    if (! writeBundledFile(targetDir.getChildFile("STABILITY_AI_COMMUNITY_LICENSE.txt"),
                           BinaryData::STABILITY_AI_COMMUNITY_LICENSE_txt,
                           BinaryData::STABILITY_AI_COMMUNITY_LICENSE_txtSize, err))
        return false;

    // model_config.json LAST -- its presence is the install-completeness marker
    // (hasModelMarker keys on it), so write it only after the license writes
    // succeeded; a re-click then re-runs this idempotently.
    return writeBundledFile(targetDir.getChildFile("model_config.json"),
                            BinaryData::sao_open_1_0_model_config_json,
                            BinaryData::sao_open_1_0_model_config_jsonSize, err);
}

static const char* const kSaoSmallLicenseNotice =
R"NOTICE(T5ynth -- Stable Audio Open Small model and its license
=============================================================

Powered by Stability AI.

This folder holds Stable Audio Open Small, downloaded by T5ynth. T5ynth neither
owns nor relicenses the weights -- it only orchestrated the download and wrote
this notice. You are responsible for complying with the license below; the FULL
license text is in this folder.

--------------------------------------------------------------------------------
Audio model  --  model.safetensors, model_config.json
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
Provenance
--------------------------------------------------------------------------------
The model weights were downloaded by you, with no account or token, from an
ungated repository that redistributes the official weights UNMODIFIED:
  https://huggingface.co/joeriben/stable-audio-open-small
  model.safetensors  sha256 0ee8b9edbe3104cb079dd163b62589d67d9c321ea8942cbbd81e6f7d3f390be2
This is byte-identical to the official gated stabilityai/stable-audio-open-small.

Before downloading you accepted the license above in T5ynth's download dialog.
T5ynth itself is free software under the GNU GPL v3; that license covers T5ynth's
own source code, NOT these third-party model weights.

UCDCAE AI Lab.
)NOTICE";

static bool finalizeSaoSmallReassembly(const juce::File& targetDir, juce::String& err)
{
    // SAO Small's model_config.json arrives as a download asset (the ungated repo
    // ships it), so finalize only writes the license documentation that Community
    // License IV(a)(i) requires distributing with the weights. The completeness
    // marker (model_config.json) is the asset itself, not written here.
    if (! writeBundledFile(targetDir.getChildFile("MODEL_LICENSES.txt"),
                           kSaoSmallLicenseNotice,
                           (int) std::strlen(kSaoSmallLicenseNotice), err))
        return false;
    return writeBundledFile(targetDir.getChildFile("STABILITY_AI_COMMUNITY_LICENSE.txt"),
                            BinaryData::STABILITY_AI_COMMUNITY_LICENSE_txt,
                            BinaryData::STABILITY_AI_COMMUNITY_LICENSE_txtSize, err);
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
    // Stable Audio Open engines also need the t5-base text encoder in a peer dir.
    // When it is missing, fetch it in the SAME download (one combined progress bar):
    // add its bytes to the total here and append a t5-base phase to the thread below.
    const bool chainT5Base = (modelId == "stable-audio-open-1.0"
                              || modelId == "stable-audio-open-small")
                             && ! t5BaseInstalled();
    if (chainT5Base)
        for (int i = 0; i < kT5BaseGhFileCount; ++i) total += kT5BaseGhFiles[i].expectedSize;
    totalBytes = total;
    downloadedBytes = 0;
    downloadCounter_ = std::make_shared<std::atomic<int64_t>>(0);
    downloadCancelFlag_ = std::make_shared<std::atomic<bool>>(false);
    startTimer(250);

    juce::Component::SafePointer<SettingsPage> safeThis(this);
    auto progressCounter = downloadCounter_;
    auto cancelFlag = downloadCancelFlag_;
    std::thread([safeThis, progressCounter, cancelFlag, targetDir, assets, assetCount,
                 encoderSub, needsMetadata, modelId, chainT5Base]()
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

            // Integrity gate for safetensors assets: trust the existing bytes for
            // skip/resume ONLY if their own safetensors header declares a file of
            // exactly expectedSize. A foreign checkpoint left at this path (e.g. a
            // Stable Audio Open model.safetensors) or a previously resume-corrupted
            // hybrid declares a DIFFERENT total — resuming onto it appends our tail
            // to their head and silently produces a wrong file that still reaches
            // expectedSize (the "incomplete metadata, file not fully covered" load
            // failure). Drop any such file and download fresh from byte 0. This also
            // self-heals an already-corrupt install on the next Download click.
            if (existingBytes > 0 && targetFile.hasFileExtension("safetensors")
                && safetensorsDeclaredTotal(targetFile) != a.expectedSize)
            {
                targetFile.deleteFile();
                existingBytes = 0;
            }

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

            // Cross-variant reuse: the t5gemma encoder is byte-identical across
            // SA3 variants but lives in a per-modelId dir, so a freshly-installed
            // second variant would otherwise re-download ~1.18 GB of encoder. If
            // another already-installed model holds this exact file, copy it
            // locally instead. Scoped to the encoder subfolder — NEVER the root
            // checkpoint, which genuinely differs per variant (music != sfx).
            if (encoderSub.isNotEmpty() && localPath.startsWith(encoderSub + "/"))
            {
                auto reuse = findReusableSiblingAsset(modelId, localPath, a.expectedSize);
                if (reuse.existsAsFile() && reuse != targetFile)
                {
                    const auto reuseText = "Reusing encoder from a previous install: "
                        + displayName + " (" + juce::String(i + 1) + "/"
                        + juce::String(assetCount) + ")";
                    juce::MessageManager::callAsync([safeThis, reuseText]() {
                        if (auto* self = safeThis.getComponent())
                            self->downloadStatusLabel.setText(reuseText, juce::dontSendNotification);
                    });
                    const bool copyOk = reuse.copyFileTo(targetFile)
                        && targetFile.getSize() == a.expectedSize
                        && (! targetFile.hasFileExtension("safetensors")
                            || safetensorsDeclaredTotal(targetFile) == a.expectedSize);
                    if (copyOk)
                    {
                        bytesCompleted += a.expectedSize;
                        if (progressCounter) progressCounter->store(bytesCompleted);
                        continue;
                    }
                    // Copy failed or produced a wrong-size file — discard the
                    // partial and fall through to a normal network download.
                    // Reset existingBytes too: the target is gone, so the resume
                    // path below must NOT send a Range against a deleted file
                    // (mirrors the over-size branch above).
                    targetFile.deleteFile();
                    existingBytes = 0;
                }
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

            // All bytes are present (>= expectedSize). Verify a safetensors asset is
            // internally consistent — its own header must declare exactly its own
            // size. A resume-glued hybrid or otherwise corrupt result declares a
            // smaller total ("file not fully covered" at load). Delete it so the
            // next click re-downloads cleanly instead of skipping a same-size-but-
            // broken file.
            if (targetFile.hasFileExtension("safetensors")
                && safetensorsDeclaredTotal(targetFile) != targetFile.getSize())
            {
                targetFile.deleteFile();
                juce::MessageManager::callAsync([safeThis, displayName]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false,
                            "The downloaded file was corrupt and has been removed: "
                            + displayName + "\n\nClick Download again to fetch it cleanly.");
                });
                return;
            }

            bytesCompleted += a.expectedSize;
            if (progressCounter) progressCounter->store(bytesCompleted);
        }

        // Post-reassembly finalize: write the bundled metadata the ungated weight
        // repos omit. SA3-style reassembly (a model that declares a text-encoder
        // subfolder) writes the t5gemma configs + carves the tokenizer; Stable
        // Audio Open 1.0 writes only its model_config.json + the SA Community
        // license; a plain reassembly with neither skips finalize entirely.
        {
            juce::String ferr;
            const bool finalizeOk =
                  needsMetadata                            ? finalizeSa3Reassembly(targetDir, encoderSub, modelId, ferr)
                : (modelId == "stable-audio-open-1.0")     ? finalizeSaoOpen10Reassembly(targetDir, ferr)
                : (modelId == "stable-audio-open-small")   ? finalizeSaoSmallReassembly(targetDir, ferr)
                :                                            true;
            if (! finalizeOk)
            {
                juce::MessageManager::callAsync([safeThis, ferr]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false, ferr);
                });
                return;
            }
        }

        // Stable Audio Open second phase: fetch the peer t5-base encoder on this
        // same thread. Progress continues from the SAO bytes already done
        // (bytesCompleted), so the one progress bar spans both phases.
        if (chainT5Base)
        {
            auto t5Dir = SettingsPage::getAppSupportModelDir("t5-base");
            t5Dir.createDirectory();
            auto setStatus = [safeThis](const juce::String& s) {
                juce::MessageManager::callAsync([safeThis, s]() {
                    if (auto* self = safeThis.getComponent())
                        self->downloadStatusLabel.setText(s, juce::dontSendNotification);
                });
            };
            juce::String t5err;
            const bool t5ok = downloadFileSet(kT5BaseHfBase, kT5BaseGhFiles, kT5BaseGhFileCount,
                                              t5Dir, progressCounter, cancelFlag,
                                              bytesCompleted, "text encoder", setStatus, t5err);
            if (cancelFlag && cancelFlag->load())
                return;
            if (! t5ok)
            {
                juce::MessageManager::callAsync([safeThis, t5err]() {
                    if (auto* self = safeThis.getComponent())
                        self->onDownloadFinished(false, t5err);
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

// Stand-alone t5-base fetch used by the MANUAL Stable Audio Open import paths
// (Auto-Scan / Browse), where there is no reassembly thread to piggyback on. Sets
// up the shared download UI/progress and reuses downloadFileSet + onDownloadFinished
// — onDownloadFinished re-scans for the (already-installed) SAO model and re-enters
// setModelPath, which now finds t5-base present and fires onModelReady.
void SettingsPage::startT5BaseChainDownload()
{
    auto t5Dir = getAppSupportModelDir("t5-base");
    t5Dir.createDirectory();

    int64_t total = 0;
    for (int i = 0; i < kT5BaseGhFileCount; ++i) total += kT5BaseGhFiles[i].expectedSize;
    totalBytes = total;
    downloadedBytes = 0;
    downloading = true;
    downloadModelId_ = activeOpModelId_;   // the SAO engine being installed owns the meter
    lastTickBytes_ = 0;
    lastTickMs_ = 0.0;
    speedBytesPerSec_ = 0.0;
    downloadStatusLabel.setText("Downloading text encoder...", juce::dontSendNotification);
    downloadStatusLabel.setColour(juce::Label::textColourId, kDim);
    refreshAllRows();  // downloading == true -> active (SAO) row enters download mode
    downloadCounter_ = std::make_shared<std::atomic<int64_t>>(0);
    downloadCancelFlag_ = std::make_shared<std::atomic<bool>>(false);
    startTimer(250);

    juce::Component::SafePointer<SettingsPage> safeThis(this);
    auto progressCounter = downloadCounter_;
    auto cancelFlag = downloadCancelFlag_;
    std::thread([safeThis, progressCounter, cancelFlag, t5Dir]()
    {
        auto setStatus = [safeThis](const juce::String& s) {
            juce::MessageManager::callAsync([safeThis, s]() {
                if (auto* self = safeThis.getComponent())
                    self->downloadStatusLabel.setText(s, juce::dontSendNotification);
            });
        };
        juce::String err;
        const bool ok = downloadFileSet(kT5BaseHfBase, kT5BaseGhFiles, kT5BaseGhFileCount,
                                        t5Dir, progressCounter, cancelFlag, 0,
                                        "text encoder", setStatus, err);
        if (cancelFlag && cancelFlag->load())
            return;
        juce::MessageManager::callAsync([safeThis, ok, err]() {
            if (auto* self = safeThis.getComponent())
                self->onDownloadFinished(ok, err);
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

// "done/total" sharing the larger unit (e.g. "0.5/2.3 GB" or "412/891 MB") for
// the active row's live readout — compact enough for the row's status slot.
static juce::String formatBytePair(int64_t done, int64_t total)
{
    const double tMb = (double) total / (1024.0 * 1024.0);
    if (tMb < 1024.0)
        return juce::String((double) done / (1024.0 * 1024.0), 0) + "/"
             + juce::String(tMb, 0) + " MB";
    return juce::String((double) done / (1024.0 * 1024.0 * 1024.0), 1) + "/"
         + juce::String(tMb / 1024.0, 1) + " GB";
}

static juce::String formatSpeed(double bytesPerSec)
{
    const double mbs = bytesPerSec / (1024.0 * 1024.0);
    if (mbs >= 1.0) return juce::String(mbs, 1) + " MB/s";
    return juce::String(bytesPerSec / 1024.0, 0) + " KB/s";
}

SettingsPage::ModelRow* SettingsPage::activeRow()
{
    // Matches downloadModelId_ (the pinned transfer target), NOT activeOpModelId_:
    // the right-click menu can re-point UI focus mid-download, but the meter must
    // stay on the row that is actually downloading.
    for (auto& r : rows_)
        if (r->modelId() == downloadModelId_)
            return r.get();
    if (translationRow_ && translationRow_->modelId() == downloadModelId_)
        return translationRow_.get();
    if (coderRow_ && coderRow_->modelId() == downloadModelId_)
        return coderRow_.get();
    return nullptr;
}

void SettingsPage::timerCallback()
{
    if (!downloading) { stopTimer(); return; }

    const int64_t total = totalBytes;
    const int64_t done  = downloadCounter_ ? downloadCounter_->load() : downloadedBytes.load();
    downloadedBytes.store(done);

    // Speed: EMA of the per-tick byte delta. The first tick after a (re)start
    // (lastTickMs_ == 0) only baselines, and an implausible jump — a skipped or
    // locally-reused multi-GB file lands in a single tick — is dropped so the
    // readout stays an honest transfer rate.
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (lastTickMs_ > 0.0)
    {
        const double dtSec = (nowMs - lastTickMs_) / 1000.0;
        if (dtSec > 0.0)
        {
            const double inst = (double) (done - lastTickBytes_) / dtSec;  // bytes/s
            if (inst >= 0.0 && inst < 1.5e9)
                speedBytesPerSec_ = (speedBytesPerSec_ <= 0.0)
                                  ? inst
                                  : 0.6 * speedBytesPerSec_ + 0.4 * inst;
        }
    }
    lastTickMs_    = nowMs;
    lastTickBytes_ = done;

    const double frac = total > 0
        ? juce::jlimit(0.0, 1.0, (double) done / (double) total) : 0.0;

    juce::String detail = (total > 0) ? formatBytePair(done, total)
                                      : juce::String(done / (1024 * 1024)) + " MB";
    if (speedBytesPerSec_ > 0.0)
        detail += juce::String(juce::CharPointer_UTF8("  \xc2\xb7  ")) + formatSpeed(speedBytesPerSec_);

    if (auto* row = activeRow())
        row->setDownloadProgress(frac, detail);
}

void SettingsPage::cancelDownload()
{
    if (!downloading)
        return;
    if (downloadCancelFlag_)
        downloadCancelFlag_->store(true);   // worker exits at its next chunk check
    stopTimer();
    downloading = false;
    // Drop the shared handles; the detached worker holds its own copies and
    // returns silently. Partial files stay on disk so the next Download resumes.
    downloadCounter_.reset();
    downloadCancelFlag_.reset();
    updateStatus();   // refreshAllRows() (active row exits download mode) + detail strip
    downloadStatusLabel.setText("Download cancelled.", juce::dontSendNotification);
    downloadStatusLabel.setColour(juce::Label::textColourId, kDim);
}

void SettingsPage::onDownloadFinished(bool success, const juce::String& error)
{
    stopTimer();
    downloading = false;
    if (downloadCancelFlag_)
        downloadCancelFlag_->store(true);
    downloadCounter_.reset();
    downloadCancelFlag_.reset();
    refreshAllRows();  // downloading == false now -> re-enable row action buttons

    // Capture whether this finished download targeted an AUXILIARY model (the
    // optional translation LLM) before any branch below reads activeOpModelId_.
    // Auxiliary models are never activated as an engine; we must restore engine
    // focus afterward on BOTH success AND failure, so the engine-centric
    // updateStatus() (called from later backend callbacks) never mistakes the
    // aux model for a gated engine.
    const bool activeWasAuxiliary = !selectedIsGenerationEngine();

    if (success) {
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

        if (selectedIsGenerationEngine())
        {
            downloadStatusLabel.setText("Download complete. Starting backend...", juce::dontSendNotification);
            downloadStatusLabel.setColour(juce::Label::textColourId, kDim);
            auto found = scanForModel();
            if (found.exists()) setModelPath(found);
        }
        else
        {
            // Auxiliary model (the optional translation LLM): it has landed on
            // disk where the backend auto-discovers it; it must NOT be activated
            // as a generation engine (no setModelPath / backend restart). The
            // engine-focus restore happens below, after the success/failure split.
            downloadStatusLabel.setText(selectedModelDisplay() + " installed.", juce::dontSendNotification);
            downloadStatusLabel.setColour(juce::Label::textColourId, kSuccess);
        }
    } else {
        downloadStatusLabel.setText("Download failed", juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId, kError);
        // Show full error in the multi-line instructions area
        setInstructionsText(instructionsLabel, error);
    }

    // Restore engine focus after an auxiliary download (success OR failure), so
    // the engine-centric detail strip / status logic doesn't describe the aux
    // model (e.g. as a "gated" engine) on the next updateStatus().
    if (activeWasAuxiliary)
        activeOpModelId_ = kRowSpecs[0].id;

    resized();  // progress strip hidden again -> reclaim its space for the detail strip
}

void SettingsPage::setBackendConnected(bool connected)
{
    backendConnected = connected;
    if (connected) backendFailReason = {};
    backendStatusLabel.setText(connected ? "Backend: Connected" : "Backend: Not connected",
                              juce::dontSendNotification);
    backendStatusLabel.setColour(juce::Label::textColourId,
        connected ? kSuccess : kError);
    if (connected && modelPath.exists())
    {
        downloadStatusLabel.setText("Model active.", juce::dontSendNotification);
        downloadStatusLabel.setColour(juce::Label::textColourId, kSuccess);
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
    backendStatusLabel.setColour(juce::Label::textColourId, kError);
    downloadStatusLabel.setText("Activation failed: " + firstErrorLine(reason),
                                juce::dontSendNotification);
    downloadStatusLabel.setColour(juce::Label::textColourId, kError);
    updateStatus();
}

void SettingsPage::refreshAllRows()
{
    // One action runs at a time, so a download/manual-import in flight disables
    // EVERY row's action button (the shared progress strip shows what's running).
    const bool busy = downloading.load() || modelInstallBusy_.load();

    for (auto& row : rows_)
    {
        const auto id = row->modelId();

        // The row whose download is in flight owns its own visuals (inline meter
        // + Cancel button), driven by the timer — leave it in download mode. Keyed
        // on downloadModelId_ so a mid-download focus change can't move it.
        if (downloading.load() && id == downloadModelId_)
        {
            row->enterDownloadingState();
            continue;
        }

        const auto& km = kKnownModels[catalogIndexForId(id)];
        auto dir = scanForModelById(id, km.hfRepo);
        const bool installed = dir.exists() && modelHasRequiredAuxAssets(id, dir);

        ModelRow::Action action;
        juce::String status;
        if (installed)
        {
            action = ModelRow::Action::Installed;
            status = "Installed";
        }
        else if (km.downloadable)
        {
            action = ModelRow::Action::Download;
            status = "Not installed";
        }
        else
        {
            action = ModelRow::Action::GetFiles;   // gated manual import (SAO Small)
            status = "Gated";
        }

        row->setState(installed, status, action, !busy);
    }

    refreshTranslationRow();
    refreshCoderRow();
}

bool SettingsPage::translationModelInstalled() const
{
    auto dir = getAppSupportModelDir("translation/qwen2.5-1.5b-instruct");
    if (!dir.isDirectory())
        return false;
    // Mirror the backend's _is_local_transformers_model_dir gate: a config, a
    // tokenizer, and weights (single or sharded). Qwen-1.5B ships a single
    // model.safetensors; the index check future-proofs larger sharded models.
    const bool hasConfig    = dir.getChildFile("config.json").existsAsFile();
    const bool hasTokenizer = dir.getChildFile("tokenizer.json").existsAsFile();
    const bool hasWeights   = dir.getChildFile("model.safetensors").existsAsFile()
                           || dir.getChildFile("model.safetensors.index.json").existsAsFile();
    return hasConfig && hasTokenizer && hasWeights;
}

void SettingsPage::refreshTranslationRow()
{
    if (translationRow_ == nullptr)
        return;
    const bool busy      = downloading.load() || modelInstallBusy_.load();
    const bool installed = translationModelInstalled();

    // Mirror the engine rows: the active download owns its own visuals (inline
    // meter + Cancel); otherwise show the idle Installed / Download state. The
    // auxiliary model is always "downloadable", so it never shows a Gated state.
    if (downloading.load() && downloadModelId_ == "translation/qwen2.5-1.5b-instruct")
    {
        translationRow_->enterDownloadingState();
        return;
    }
    translationRow_->setState(installed,
                              installed ? "Installed" : "Not installed",
                              installed ? ModelRow::Action::Installed
                                        : ModelRow::Action::Download,
                              !busy);

    // Notify the editor's Qwen gate only on an install-state TRANSITION: this is
    // called on every refresh (construction, download/import complete, backend
    // connect), so an unconditional fire would re-notify on unchanged state.
    // MainPanel pushes the initial state explicitly; this catches live install/removal.
    if (! translationInstalledKnown_ || installed != translationInstalledLast_)
    {
        translationInstalledKnown_ = true;
        translationInstalledLast_  = installed;
        if (onTranslationModelChanged)
            onTranslationModelChanged(installed);
    }
}

bool SettingsPage::coderModelInstalled() const
{
    // Two install shapes the backend's _resolve_coder_model_dir (pipe_inference.py)
    // accepts: the in-app Download slot at <model root>/coder/qwen2.5-coder-7b-
    // instruct, or a manually dropped dev copy at <model root>/lco-coder/qwen2.5-
    // coder-7b-instruct. scanForModelById covers BOTH the current and legacy
    // per-user install roots (plus the HF cache) for each.
    //
    // The found directory must then pass the BACKEND's load gate, not merely
    // hasModelMarker: this model ships as four shards, and an install missing
    // model.safetensors.index.json satisfies hasModelMarker while transformers
    // cannot load it. Reporting that as "Installed" hides the Download button and
    // strands the user, and the backend resolver silently falls through to
    // another coder. Mirror translationModelInstalled() and agree with what the
    // backend can actually open.
    const auto& cm = kKnownModels[catalogIndexForId("coder/qwen2.5-coder-7b-instruct")];
    if (isLoadableTransformersDir(scanForModelById("coder/qwen2.5-coder-7b-instruct", cm.hfRepo)))
        return true;
    return isLoadableTransformersDir(scanForModelById("lco-coder/qwen2.5-coder-7b-instruct", {}));
}

void SettingsPage::refreshCoderRow()
{
    if (coderRow_ == nullptr)
        return;
    const bool busy      = downloading.load() || modelInstallBusy_.load();
    const bool installed = coderModelInstalled();

    // Mirror refreshTranslationRow(): the active download owns its own visuals
    // (inline meter + Cancel); otherwise show the idle Installed / Download
    // state. Always "downloadable", so it never shows a Gated state.
    if (downloading.load() && downloadModelId_ == "coder/qwen2.5-coder-7b-instruct")
    {
        coderRow_->enterDownloadingState();
        return;
    }
    coderRow_->setState(installed,
                        installed ? "Installed" : "Not installed",
                        installed ? ModelRow::Action::Installed
                                  : ModelRow::Action::Download,
                        !busy);

    // Notify the editor's coder gate only on an install-state TRANSITION (mirrors
    // refreshTranslationRow above): this runs on every refresh, so an unconditional
    // fire would re-notify on unchanged state. Lets a coder-only install refresh the
    // base LCO bake gate live, without a reopen.
    if (! coderInstalledKnown_ || installed != coderInstalledLast_)
    {
        coderInstalledKnown_ = true;
        coderInstalledLast_  = installed;
        if (onCoderModelChanged)
            onCoderModelChanged(installed);
    }
}

void SettingsPage::updateStatus()
{
    // Keep modelPath pointing at the focused model's dir — the backend-activation
    // glue (setModelPath / setBackendConnected) reads it.
    auto found = scanForModel();
    modelPath = found.exists() ? found : juce::File();

    refreshAllRows();

    // The shared detail strip describes the FOCUSED model (activeOpModelId_). The
    // row's button + sublabel + light carry the primary affordance; this strip
    // adds the one detail a row can't: a short blurb / license when not installed,
    // or the backend error when installed-but-not-active.
    const auto id        = selectedModelId();
    const auto hfRepo    = selectedHfRepo();
    const auto display   = selectedModelDisplay();
    const auto targetPath = getAppSupportModelDir(id).getFullPathName();

    // The optional translation helper is auxiliary (not a generation engine): it
    // has its own install gate and never activates a backend, so describe it on
    // its own terms rather than running the engine installed/active logic below.
    if (id == "translation/qwen2.5-1.5b-instruct")
    {
        if (translationModelInstalled())
            setInstructionsText(instructionsLabel,
                display + " is installed. Enable it per prompt with the EN button "
                "next to the prompt; your typed text is never changed.");
        else
            setInstructionsText(instructionsLabel,
                "Optional helper -- translates your prompt to English in the "
                "background before the audio model sees it (Stable Audio models are "
                "trained on English captions); your typed text is never changed. "
                "Toggle it per prompt with the EN button. Ungated, Apache-2.0, no "
                "HuggingFace account.\n  Target: " + targetPath);
        return;
    }

    // The optional LCO coder is likewise auxiliary: same treatment as the
    // translation branch above, own terms rather than the engine installed/
    // active logic below.
    if (id == "coder/qwen2.5-coder-7b-instruct")
    {
        if (coderModelInstalled())
            setInstructionsText(instructionsLabel,
                display + " is installed. The LCO (classic oscillator) uses it to "
                "write the Csound orchestra for your prompt whenever you bake.");
        else
            setInstructionsText(instructionsLabel,
                "Required by the LCO (classic oscillator) -- this 7B coding model "
                "writes the Csound orchestra for your prompt. About 15 GB. Ungated, "
                "no HuggingFace account. License: Apache 2.0 (open, no "
                "restrictions).\n"
                "  Target: " + targetPath);
        return;
    }

    auto dir = scanForModelById(id, hfRepo);
    const bool installed = dir.exists() && modelHasRequiredAuxAssets(id, dir);

    if (installed)
    {
        if (backendConnected)
            setInstructionsText(instructionsLabel,
                display + " is installed and active. Ready to generate.");
        else if (backendFailReason.isNotEmpty())
            setInstructionsText(instructionsLabel,
                display + ": files found, but the backend failed while loading.\n\n"
                "Path:\n  " + dir.getFullPathName() + "\n\n"
                "Backend error:\n  " + backendFailReason + "\n\n"
                "Log:\n  " + backendStderrLogPath());
        else
            setInstructionsText(instructionsLabel,
                display + " is installed. Starting backend...");
        return;
    }

    // Not installed: one compact, honest paragraph for the focused model.
    if (id == "audioldm2")
        setInstructionsText(instructionsLabel,
            "AudioLDM2 -- academic latent-diffusion text-to-audio model (CVSSP / "
            "University of Surrey, Liu et al. 2023). Ungated: click Download. "
            "License: CC BY-NC-SA 4.0 (non-commercial only, no exceptions).\n"
            "  Target: " + targetPath);
    else if (id == "stable-audio-open-small")
        setInstructionsText(instructionsLabel,
            "Stable Audio Open Small -- the compact, fastest Stable Audio Open "
            "checkpoint. Click Download; no HuggingFace account needed. The required "
            "T5-Base encoder (Apache-2.0) installs automatically with it. License: "
            "Stability AI Community License (written into the model folder).\n"
            "  Target: " + targetPath);
    else if (id == "stable-audio-3-small-music" || id == "stable-audio-3-small-sfx")
        setInstructionsText(instructionsLabel,
            display + " -- current SA3 small-format checkpoint with its own t5gemma "
            "text encoder (installed with it, reused across both SA3 variants). Click "
            "Download; no HuggingFace account needed. License: Stability AI Community "
            "License + Google Gemma Terms (both written into the model folder).\n"
            "  Target: " + targetPath);
    else if (id == "stable-audio-3-medium")
        setInstructionsText(instructionsLabel,
            display + " -- the larger, higher-fidelity SA3 checkpoint; a single model "
            "that renders both Music and SFX (chosen per generation). Ships its own "
            "t5gemma text encoder (installed with it, shared by all SA3 variants). Click "
            "Download; no HuggingFace account needed. License: Stability AI Community "
            "License + Google Gemma Terms (both written into the model folder).\n"
            "  Target: " + targetPath);
    else if (id == "stable-audio-open-1.0")
        setInstructionsText(instructionsLabel,
            "Stable Audio Open 1.0 -- the original full-size checkpoint. Click "
            "Download; no HuggingFace account needed. The required T5-Base encoder "
            "(Apache-2.0) installs automatically with it. License: Stability AI "
            "Community License (written into the model folder).\n"
            "  Target: " + targetPath);
    else
        setInstructionsText(instructionsLabel,
            "This model is gated on HuggingFace. Right-click the row > Open model page "
            "to accept its license, download its files, then run Get files... to "
            "import them.\n  Source: https://huggingface.co/" + hfRepo);
}

void SettingsPage::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    // Thin Ableton-like family group labels above each family's first row.
    g.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
    for (auto& h : familyHeaders_)
    {
        g.setColour(kDimmer);
        g.drawText(h.text, h.bounds.reduced(2, 0), juce::Justification::bottomLeft, false);
        g.setColour(kBorder);
        g.drawHorizontalLine(h.bounds.getBottom() - 1,
                             (float) h.bounds.getX(),
                             (float) h.bounds.getRight());
    }
}

// ───────────────────────────── GeneralSettingsPage ─────────────────────────────

GeneralSettingsPage::GeneralSettingsPage()
{
    osTitle_.setText("Filter Oversampling", juce::dontSendNotification);
    osTitle_.setColour(juce::Label::textColourId, kTextPrimary);
    osTitle_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(osTitle_);

    osCombo_.addItem("Off",  1);                 // item ids are index+1
    // Plain ASCII "x" — a raw UTF-8 × (U+00D7) mojibakes through JUCE's ComboBox
    // font (same reason FilterDriveOs uses "2x"/"4x"/"8x", see BlockParams.h).
    osCombo_.addItem("2x", 2);
    osCombo_.addItem("4x", 3);
    osCombo_.setSelectedId(2, juce::dontSendNotification);   // default 2x
    osCombo_.onChange = [this]
    {
        if (onOsQualityChanged)
            onOsQualityChanged(osCombo_.getSelectedId() - 1);
    };
    addAndMakeVisible(osCombo_);

    updateCheckToggle_.setColour(juce::ToggleButton::textColourId, kTextPrimary);
    updateCheckToggle_.setToggleState(true, juce::dontSendNotification);   // opt-out default
    updateCheckToggle_.onClick = [this]
    {
        if (onCheckForUpdatesChanged)
            onCheckForUpdatesChanged(updateCheckToggle_.getToggleState());
    };
    addAndMakeVisible(updateCheckToggle_);

    eventLogToggle_.setColour(juce::ToggleButton::textColourId, kTextPrimary);
    eventLogToggle_.setToggleState(false, juce::dontSendNotification);   // opt-in default
    eventLogToggle_.onClick = [this]
    {
        if (onEventLogEnabledChanged)
            onEventLogEnabledChanged(eventLogToggle_.getToggleState());
    };
    addAndMakeVisible(eventLogToggle_);

    // Update banner (top of page) — hidden until setUpdateAvailable() fires.
    updateLabel_.setColour(juce::Label::textColourId, kAccent);
    updateLabel_.setJustificationType(juce::Justification::centredLeft);
    addChildComponent(updateLabel_);

    updateDownloadBtn_.setColour(juce::TextButton::buttonColourId, kAccent.withAlpha(0.30f));
    updateDownloadBtn_.setColour(juce::TextButton::textColourOffId, kAccent);
    updateDownloadBtn_.onClick = [this]
    {
        if (updateUrl_.isNotEmpty())
            juce::URL(updateUrl_).launchInDefaultBrowser();
    };
    addChildComponent(updateDownloadBtn_);
}

void GeneralSettingsPage::setUpdateAvailable(const juce::String& version, const juce::String& url)
{
    updateUrl_ = url;
    updateAvailable_ = url.isNotEmpty();
    updateLabel_.setText("Update available: " + version, juce::dontSendNotification);
    updateLabel_.setVisible(updateAvailable_);
    updateDownloadBtn_.setVisible(updateAvailable_);
    layoutRows();
}

void GeneralSettingsPage::setOsQuality(int qualityIndex)
{
    osCombo_.setSelectedId(juce::jlimit(0, 2, qualityIndex) + 1, juce::dontSendNotification);
}

void GeneralSettingsPage::setCheckForUpdatesEnabled(bool enabled)
{
    updateCheckToggle_.setToggleState(enabled, juce::dontSendNotification);
}

void GeneralSettingsPage::setEventLogEnabled(bool enabled)
{
    eventLogToggle_.setToggleState(enabled, juce::dontSendNotification);
}

void GeneralSettingsPage::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    g.setColour(kTextMuted);
    g.setFont(juce::FontOptions(12.0f));
    g.drawFittedText("Suppresses aliasing on the nonlinear Ladder / Warp filters by running them "
                     "at a higher internal rate. Higher = cleaner, more CPU; the linear SVF is "
                     "unaffected. Global setting for this machine - not stored per preset.",
                     helpBounds_, juce::Justification::topLeft, 5);
}

void GeneralSettingsPage::resized()
{
    layoutRows();
}

void GeneralSettingsPage::layoutRows()
{
    auto r = getLocalBounds().reduced(18);

    // ── Updates group (kept together at the top) ──
    // The "Update available … Download" banner (only when an update exists) sits
    // directly above its own opt-out toggle — the two update controls must not be
    // split by the unrelated Filter Oversampling row below.
    if (updateAvailable_)
    {
        auto row = r.removeFromTop(28);
        updateDownloadBtn_.setBounds(row.removeFromRight(96));
        row.removeFromRight(10);
        updateLabel_.setBounds(row);
        r.removeFromTop(8);
    }
    updateCheckToggle_.setBounds(r.removeFromTop(26));

    r.removeFromTop(14);
    eventLogToggle_.setBounds(r.removeFromTop(26));

    r.removeFromTop(22);

    // ── Filter Oversampling group ──
    auto row = r.removeFromTop(26);
    osTitle_.setBounds(row.removeFromLeft(150));
    row.removeFromLeft(10);
    osCombo_.setBounds(row.removeFromLeft(110));
    r.removeFromTop(14);
    helpBounds_ = r.removeFromTop(90);
}

void SettingsPage::resized()
{
    auto area = getLocalBounds().reduced(8, 6);

    // Title (top).
    titleLabel.setFont(juce::FontOptions(15.0f).withStyle("Bold"));
    titleLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(6);

    // Backend footer (very bottom) — separate from the per-row install lights.
    backendStatusLabel.setFont(juce::FontOptions(12.0f));
    backendStatusLabel.setBounds(area.removeFromBottom(20));
    area.removeFromBottom(4);

    // Status strip — ALWAYS reserved. downloadStatusLabel carries phase and
    // terminal messages ("Fetching file list...", "Download complete...", "Model
    // active.", "Activation failed: ...", "Download cancelled.") that are set
    // WITHOUT a following resized(), so the label must always have real bounds or
    // those messages vanish. Live download PROGRESS lives in the active row's
    // inline meter, not here.
    downloadStatusLabel.setFont(juce::FontOptions(11.0f));
    downloadStatusLabel.setBounds(area.removeFromBottom(18));
    area.removeFromBottom(4);

    // Middle band = family headers + five engine rows + a shared detail strip.
    familyHeaders_.clear();
    const int headerH = 13;
    const int detailMin = 40;        // detail strip floor; grows on larger panels
    // The two optional auxiliary rows below the engines (prompt translation +
    // LCO coder) SHARE one "OPTIONAL MODELS" family header, so their fixed
    // vertical cost is one top gap + one header + two engine-height
    // rows. Reserve it in the row budget so the engine rows shrink to fit instead
    // of pushing the aux rows down INTO (and hiding) the detail strip. One shared
    // header (not one per model) keeps the extra aux models from cramming the
    // strip. The host sizes this page 300–500 px tall (MainPanel), so the rows
    // MUST give up this space.
    const int auxBandH = 8 + headerH + 2 * 44;   // 44 = max engine rowH (upper bound)
    int numHeaders = 0;
    for (int i = 0; i < kNumRowSpecs; ++i)
        if (kRowSpecs[i].familyHeader != nullptr) ++numHeaders;

    const int rowH = juce::jlimit(28, 44,
        (area.getHeight() - numHeaders * headerH - detailMin - auxBandH)
            / juce::jmax(1, kNumRowSpecs));

    for (int i = 0; i < kNumRowSpecs && i < (int) rows_.size(); ++i)
    {
        if (kRowSpecs[i].familyHeader != nullptr)
        {
            auto hr = area.removeFromTop(headerH);
            familyHeaders_.push_back({ juce::String(kRowSpecs[i].familyHeader), hr });
        }
        rows_[(size_t) i]->setBounds(area.removeFromTop(rowH));
    }

    // The two optional auxiliary models (prompt translation + LCO coder) share
    // ONE "OPTIONAL MODELS" family header (painted like the engine headers)
    // above their two engine-height ModelRows, between the engine rows and the
    // shared detail strip. One shared header, not one per model, so adding
    // another aux model does not cram the detail strip — each ModelRow already
    // names its own model.
    area.removeFromTop(8);
    {
        auto hr = area.removeFromTop(headerH);
        familyHeaders_.push_back({ "OPTIONAL MODELS", hr });
    }
    if (translationRow_ != nullptr)
        translationRow_->setBounds(area.removeFromTop(rowH));
    if (coderRow_ != nullptr)
        coderRow_->setBounds(area.removeFromTop(rowH));

    area.removeFromTop(6);
    instructionsLabel.setFont(juce::FontOptions(11.5f));
    instructionsLabel.setBounds(area);   // leftover height (may scroll)
    instructionsLabel.setCaretPosition(0);
}
