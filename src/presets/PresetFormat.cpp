#include "PresetFormat.h"
#include "../PluginProcessor.h"
#include "../dsp/BlockParams.h"
#include <cstring>

namespace
{
juce::String getStoredPresetName(const juce::var& parsed, const juce::File& file)
{
    if (auto* root = parsed.getDynamicObject())
    {
        auto stored = root->getProperty("name").toString().trim();
        if (stored.isNotEmpty() && stored != "T5ynth Export")
            return stored;
    }

    return file.getFileNameWithoutExtension();
}

juce::StringArray readTagsFromJson(const juce::var& parsed)
{
    juce::StringArray tags;
    if (auto* root = parsed.getDynamicObject())
    {
        if (auto* arr = root->getProperty("tags").getArray())
        {
            for (auto& v : *arr)
            {
                auto t = v.toString().trim();
                if (t.isNotEmpty())
                    tags.addIfNotAlreadyThere(t);
            }
        }
    }
    return tags;
}
}

// ═══════════════════════════════════════════════════════════════════
// Save: T5YN header + JSON + raw float32 PCM
// ═══════════════════════════════════════════════════════════════════

bool PresetFormat::saveToFile(const juce::File& file, T5ynthProcessor& processor)
{
    // Build JSON (reuse existing export + add meta/embeddings)
    juce::String jsonBase = processor.exportJsonPreset();
    auto parsed = juce::JSON::parse(jsonBase);
    if (!parsed.isObject()) return false;

    auto* root = parsed.getDynamicObject();
    root->setProperty("name",
                      processor.getLastPresetName().trim().isNotEmpty()
                          ? processor.getLastPresetName().trim()
                          : file.getFileNameWithoutExtension());

    // Tags are always written, even when empty, so clearing all tags is an
    // explicit saved state rather than falling back to legacy inference.
    {
        const auto& tags = processor.getLastTags();
        juce::Array<juce::var> tagArr;
        for (auto& t : tags) tagArr.add(t);
        root->setProperty("tags", tagArr);
    }

    // Patch in prompts (exportJsonPreset leaves them empty)
    if (auto* synth = root->getProperty("synth").getDynamicObject())
    {
        synth->setProperty("promptA", processor.getLastPromptA());
        synth->setProperty("promptB", processor.getLastPromptB());
        synth->setProperty("seed", processor.getLastSeed());
        synth->setProperty("model", processor.getLastModel());
        auto genSeed = static_cast<int>(processor.getValueTreeState()
                           .getRawParameterValue(PID::genSeed)->load());
        synth->setProperty("randomSeed", genSeed == -1);
        // Persist the most recent inference duration (ms). Older presets
        // miss this field; readers default to 0 → display "—".
        if (processor.getLastGenerationTimeMs() > 0.0f)
            synth->setProperty("inferenceMs",
                               static_cast<double>(processor.getLastGenerationTimeMs()));

        // Research-mode injection state. Always written; readers use
        // hasProperty() defaults so old .t5p files predating these fields
        // load gracefully into the panel's current values.
        synth->setProperty("injectionMode",  processor.getLastInjectionMode());
        synth->setProperty("lateMixAmount",  static_cast<double>(processor.getLastLateMixAmount()));
        synth->setProperty("splitStart",     static_cast<double>(processor.getLastSplitStart()));
        synth->setProperty("splitEnd",       static_cast<double>(processor.getLastSplitEnd()));
    }

    // Semantic axes (GUI-only state, 3 slots)
    {
        juce::Array<juce::var> axesArr;
        const auto& axes = processor.getLastAxes();
        for (int i = 0; i < 3; ++i)
        {
            juce::DynamicObject::Ptr ax = new juce::DynamicObject();
            ax->setProperty("dropdownId", axes[static_cast<size_t>(i)].dropdownId);
            ax->setProperty("value", static_cast<double>(axes[static_cast<size_t>(i)].value));
            axesArr.add(ax.get());
        }
        root->setProperty("semanticAxes", axesArr);
    }

    // Audio metadata
    const auto& audioBuf = processor.getGeneratedAudio();
    int numSamples = audioBuf.getNumSamples();
    int numChannels = audioBuf.getNumChannels();

    juce::DynamicObject::Ptr meta = new juce::DynamicObject();
    meta->setProperty("sampleRate", processor.getGeneratedSampleRate());
    meta->setProperty("channels", numChannels);
    meta->setProperty("numSamples", numSamples);
    root->setProperty("audio_meta", meta.get());

    // Embeddings
    const auto& embA = processor.getLastEmbeddingA();
    const auto& embB = processor.getLastEmbeddingB();
    if (!embA.empty())
    {
        juce::Array<juce::var> arrA, arrB;
        for (float v : embA) arrA.add(static_cast<double>(v));
        for (float v : embB) arrB.add(static_cast<double>(v));
        root->setProperty("embeddingA", arrA);
        root->setProperty("embeddingB", arrB);
    }

    juce::String json = juce::JSON::toString(parsed, true);
    auto jsonData = json.toRawUTF8();
    uint32_t jsonLen = static_cast<uint32_t>(json.getNumBytesAsUTF8());

    // Write file
    auto targetFile = file.withFileExtension("t5p");
    if (!targetFile.getParentDirectory().exists())
        targetFile.getParentDirectory().createDirectory();

    juce::TemporaryFile tempFile(targetFile);
    juce::FileOutputStream out(tempFile.getFile());
    if (out.failedToOpen()) return false;

    // Magic + version
    out.write(kMagic, 4);
    out.writeInt(static_cast<int>(kVersion));

    // JSON section
    out.writeInt(static_cast<int>(jsonLen));
    out.write(jsonData, static_cast<size_t>(jsonLen));

    // Audio PCM (interleaved float32)
    if (numSamples > 0 && numChannels > 0)
    {
        // Interleave channels
        std::vector<float> interleaved(static_cast<size_t>(numSamples * numChannels));
        for (int s = 0; s < numSamples; ++s)
        {
            for (int c = 0; c < numChannels; ++c)
                interleaved[static_cast<size_t>(s * numChannels + c)] = audioBuf.getSample(c, s);
        }
        out.write(interleaved.data(), interleaved.size() * sizeof(float));
    }

    out.flush();
    if (!out.getStatus().wasOk())
        return false;

    return tempFile.overwriteTargetFileWithTemporary();
}

// ═══════════════════════════════════════════════════════════════════
// Load: auto-detect format (T5YN binary / JSON / XML)
// ═══════════════════════════════════════════════════════════════════

PresetFormat::LoadResult PresetFormat::loadFromFile(const juce::File& file, T5ynthProcessor& processor)
{
    LoadResult result;
    if (!file.existsAsFile()) return result;

    juce::MemoryBlock fileData;
    if (!file.loadFileAsData(fileData)) return result;

    auto* data = static_cast<const char*>(fileData.getData());
    auto size = fileData.getSize();

    // ── Detect format ──
    bool isBinary = (size >= 12 && std::memcmp(data, kMagic, 4) == 0);

    if (isBinary)
    {
        // ── New binary format ──
        auto* bytes = reinterpret_cast<const uint8_t*>(data);
        uint32_t version = *reinterpret_cast<const uint32_t*>(bytes + 4);
        if (version != kVersion)
        {
            // Unknown / future version: refuse to parse rather than silently
            // mis-interpret the JSON/PCM layout as the current schema.
            DBG("PresetFormat: unsupported .t5p version " << (int) version
                << " (expected " << (int) kVersion << ")");
            return result;
        }
        uint32_t jsonLen = *reinterpret_cast<const uint32_t*>(bytes + 8);

        if (12 + jsonLen > size) return result;

        juce::String json(data + 12, static_cast<size_t>(jsonLen));
        if (!processor.importJsonPreset(json)) return result;

        // Parse JSON for metadata
        auto parsed = juce::JSON::parse(json);
        auto* root = parsed.getDynamicObject();
        if (!root) return result;

        // Extract prompts/seed
        if (auto* synth = root->getProperty("synth").getDynamicObject())
        {
            result.promptA = synth->getProperty("promptA").toString();
            result.promptB = synth->getProperty("promptB").toString();
            int s = static_cast<int>(synth->getProperty("seed"));
            if (s > 0) result.seed = s;
            result.device = synth->getProperty("device").toString();
            result.model = synth->getProperty("model").toString();
            if (synth->hasProperty("randomSeed"))
                result.randomSeed = static_cast<bool>(synth->getProperty("randomSeed"));

            // Research-mode injection state. All four fields are independent —
            // a half-populated preset (e.g. only injectionMode set) is honoured
            // for the present field and leaves the others at NaN/empty so the
            // panel keeps its existing values. Old .t5p without any of these
            // fields → all four stay default → panel state is preserved.
            if (synth->hasProperty("injectionMode"))
                result.injectionMode = synth->getProperty("injectionMode").toString();
            if (synth->hasProperty("lateMixAmount"))
                result.lateMixAmount = static_cast<float>(
                    static_cast<double>(synth->getProperty("lateMixAmount")));
            if (synth->hasProperty("splitStart"))
                result.splitStart = static_cast<float>(
                    static_cast<double>(synth->getProperty("splitStart")));
            if (synth->hasProperty("splitEnd"))
                result.splitEnd = static_cast<float>(
                    static_cast<double>(synth->getProperty("splitEnd")));
        }

        // Extract semantic axes
        if (auto* axesArr = root->getProperty("semanticAxes").getArray())
        {
            for (int i = 0; i < std::min(axesArr->size(), 3); ++i)
            {
                if (auto* ax = (*axesArr)[i].getDynamicObject())
                {
                    result.axes[static_cast<size_t>(i)].dropdownId =
                        static_cast<int>(ax->getProperty("dropdownId"));
                    result.axes[static_cast<size_t>(i)].value =
                        static_cast<float>(ax->getProperty("value"));
                }
            }
            result.hasAxes = true;
        }

        // Extract embeddings
        if (auto* embAArr = root->getProperty("embeddingA").getArray())
        {
            result.embeddingA.reserve(static_cast<size_t>(embAArr->size()));
            for (auto& v : *embAArr) result.embeddingA.push_back(static_cast<float>(v));
        }
        if (auto* embBArr = root->getProperty("embeddingB").getArray())
        {
            result.embeddingB.reserve(static_cast<size_t>(embBArr->size()));
            for (auto& v : *embBArr) result.embeddingB.push_back(static_cast<float>(v));
        }

        // Extract audio
        if (auto* am = root->getProperty("audio_meta").getDynamicObject())
        {
            int numChannels = static_cast<int>(am->getProperty("channels"));
            int numSamples = static_cast<int>(am->getProperty("numSamples"));
            result.sampleRate = static_cast<double>(am->getProperty("sampleRate"));

            size_t audioOffset = 12 + static_cast<size_t>(jsonLen);
            size_t audioBytes = static_cast<size_t>(numSamples * numChannels) * sizeof(float);

            if (numSamples > 0 && numChannels > 0 && audioOffset + audioBytes <= size)
            {
                const auto* pcm = reinterpret_cast<const float*>(data + audioOffset);
                result.audio.setSize(numChannels, numSamples);
                for (int s = 0; s < numSamples; ++s)
                    for (int c = 0; c < numChannels; ++c)
                        result.audio.setSample(c, s, pcm[s * numChannels + c]);
                result.hasAudio = true;
            }
        }

        result.presetName = getStoredPresetName(parsed, file);
        result.tags = readTagsFromJson(parsed);
        result.success = true;
    }
    else
    {
        // ── Legacy format: try JSON first, then XML ──
        juce::String fileText = file.loadFileAsString();

        // Try JSON
        if (fileText.trimStart().startsWith("{"))
        {
            if (processor.importJsonPreset(fileText))
            {
                auto parsed = juce::JSON::parse(fileText);
                if (auto* root = parsed.getDynamicObject())
                {
                    if (auto* synth = root->getProperty("synth").getDynamicObject())
                    {
                        result.promptA = synth->getProperty("promptA").toString();
                        result.promptB = synth->getProperty("promptB").toString();
                        int s = static_cast<int>(synth->getProperty("seed"));
                        if (s > 0) result.seed = s;
                        result.device = synth->getProperty("device").toString();
                        result.model = synth->getProperty("model").toString();
                        if (synth->hasProperty("randomSeed"))
                            result.randomSeed = static_cast<bool>(synth->getProperty("randomSeed"));
                        if (synth->hasProperty("injectionMode"))
                            result.injectionMode = synth->getProperty("injectionMode").toString();
                        if (synth->hasProperty("lateMixAmount"))
                            result.lateMixAmount = static_cast<float>(
                                static_cast<double>(synth->getProperty("lateMixAmount")));
                        if (synth->hasProperty("splitStart"))
                            result.splitStart = static_cast<float>(
                                static_cast<double>(synth->getProperty("splitStart")));
                        if (synth->hasProperty("splitEnd"))
                            result.splitEnd = static_cast<float>(
                                static_cast<double>(synth->getProperty("splitEnd")));
                    }
                }
                result.presetName = getStoredPresetName(parsed, file);
                result.tags = readTagsFromJson(parsed);
                result.success = true;
            }
        }
        // Try XML (old APVTS dump)
        else if (fileText.trimStart().startsWith("<"))
        {
            auto xml = juce::XmlDocument::parse(fileText);
            if (xml != nullptr)
            {
                auto state = juce::ValueTree::fromXml(*xml);
                if (state.isValid())
                {
                    processor.getValueTreeState().replaceState(state);
                    result.presetName = file.getFileNameWithoutExtension();
                    result.success = true;
                }
            }
        }
    }

    return result;
}

juce::File PresetFormat::getPresetsDirectory()
{
    return getUserPresetsDirectory();
}

juce::File PresetFormat::getFactoryPresetsDirectory()
{
   #if JUCE_MAC
    return juce::File("/Library/Application Support/T5ynth/presets");
   #elif JUCE_LINUX
    return juce::File("/usr/share/T5ynth/presets");
   #else
    // Windows: C:\ProgramData\T5ynth\presets (installed by Setup)
    return juce::File::getSpecialLocation(juce::File::commonApplicationDataDirectory)
        .getChildFile("T5ynth").getChildFile("presets");
   #endif
}

juce::File PresetFormat::getUserPresetsDirectory()
{
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("T5ynth")
        .getChildFile("presets");
    dir.createDirectory();
    return dir;
}

juce::Array<juce::File> PresetFormat::getAllPresetFiles()
{
    juce::Array<juce::File> files;

    // Recursive scan so user-created subdirectories ("banks") show up in
    // the library. Factory dir is also scanned recursively in case a future
    // installer ships categorized subfolders.
    auto factoryDir = getFactoryPresetsDirectory();
    if (factoryDir.isDirectory())
        for (auto& f : factoryDir.findChildFiles(juce::File::findFiles, true, "*.t5p"))
            files.add(f);

    auto userDir = getUserPresetsDirectory();
    if (userDir.isDirectory())
        for (auto& f : userDir.findChildFiles(juce::File::findFiles, true, "*.t5p"))
            files.add(f);

    return files;
}
