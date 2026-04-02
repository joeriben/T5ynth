#pragma once
#include <JuceHeader.h>
#include <vector>

class T5ynthProcessor;

/**
 * Preset serialization and deserialization.
 *
 * Format v2 (.t5p): Binary container with embedded audio.
 *   [4B] Magic "T5YN"
 *   [4B] Version (uint32 LE, currently 2)
 *   [4B] JSON length (uint32 LE)
 *   [NB] JSON (params + meta + embeddings)
 *   [MB] Raw float32 interleaved stereo PCM (rest of file)
 *
 * Backwards compatible: detects old XML or JSON .t5p files automatically.
 */
class PresetFormat
{
public:
    PresetFormat() = default;

    /** Result of loading a preset (audio + embeddings are optional). */
    struct LoadResult
    {
        bool success = false;
        juce::String presetName;
        juce::String promptA, promptB;
        int seed = 123456789;
        juce::String device;

        // Embedded audio (empty if old-format preset)
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
        bool hasAudio = false;

        // Embeddings (empty if not available)
        std::vector<float> embeddingA, embeddingB;
    };

    /** Save current state to a .t5p file with embedded audio. */
    static bool saveToFile(const juce::File& file, T5ynthProcessor& processor);

    /** Load a preset from file. Returns full result with audio + metadata. */
    static LoadResult loadFromFile(const juce::File& file, T5ynthProcessor& processor);

    /** Get the preset file extension. */
    static juce::String getFileExtension() { return ".t5p"; }

    /** Get default preset directory (creates if needed). */
    static juce::File getPresetsDirectory();

private:
    static constexpr char kMagic[4] = { 'T', '5', 'Y', 'N' };
    static constexpr uint32_t kVersion = 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetFormat)
};
