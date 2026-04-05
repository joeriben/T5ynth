#include "PresetFormat.h"
#include "../PluginProcessor.h"
#include <cstring>

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

    // Patch in prompts (exportJsonPreset leaves them empty)
    if (auto* synth = root->getProperty("synth").getDynamicObject())
    {
        synth->setProperty("promptA", processor.getLastPromptA());
        synth->setProperty("promptB", processor.getLastPromptB());
        synth->setProperty("seed", processor.getLastSeed());
        synth->setProperty("device", processor.getLastDevice());
        synth->setProperty("model", processor.getLastModel());
        auto genSeed = static_cast<int>(processor.getValueTreeState()
                           .getRawParameterValue("gen_seed")->load());
        synth->setProperty("randomSeed", genSeed == -1);
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
    targetFile.deleteFile();
    juce::FileOutputStream out(targetFile);
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
    return out.getStatus().wasOk();
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
        // uint32_t version = *reinterpret_cast<const uint32_t*>(bytes + 4);  // reserved for future
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

        result.presetName = file.getFileNameWithoutExtension();
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
                    }
                }
                result.presetName = file.getFileNameWithoutExtension();
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
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("T5ynth")
        .getChildFile("presets");
    dir.createDirectory();
    return dir;
}
