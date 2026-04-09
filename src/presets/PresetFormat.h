#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>

class T5ynthProcessor;

/**
 * Preset serialization and deserialization.
 *
 * Format v3 (.t5p): Binary container with embedded audio.
 *   [4B] Magic "T5YN"
 *   [4B] Version (uint32 LE, currently 3)
 *   [4B] JSON length (uint32 LE)
 *   [NB] JSON (params + meta + embeddings)
 *   [MB] Raw float32 interleaved stereo PCM (rest of file)
 *
 * Format break v2 → v3: all choice-parameter JSON fields are now
 * serialized as stable snake_case keys drawn from BlockParams.h
 * kEntries tables (the `.key` column). Old v2/v1 presets are rejected
 * by the strict version check in PresetFormat.cpp — migrate via the
 * one-off Python tool used for the bundled DEMO preset.
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
        bool randomSeed = false;
        juce::String device;
        juce::String model;

        // Embedded audio (empty if old-format preset)
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
        bool hasAudio = false;

        // Semantic axes (3 slots: dropdown selection + value)
        struct AxisState { int dropdownId = 1; float value = 0.0f; };
        std::array<AxisState, 3> axes;
        bool hasAxes = false;

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
    static constexpr uint32_t kVersion = 3;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetFormat)
};
