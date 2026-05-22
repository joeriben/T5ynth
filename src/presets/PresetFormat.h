#pragma once
#include <JuceHeader.h>
#include <array>
#include <limits>
#include <vector>

class T5ynthProcessor;

/**
 * Preset serialization and deserialization.
 *
 * Format v4 (.t5p): Binary container with FLAC-compressed audio payloads.
 *   [4B]  Magic "T5YN"
 *   [4B]  Version (uint32 LE, currently 4)
 *   [4B]  JSON length (uint32 LE)
 *   [NB]  JSON (params + meta + embeddings + snapshots)
 *   [VAR] Sequence of length-prefixed FLAC blobs, in JSON-declared order:
 *           primary audio · inferenceCache entries · sequencer one-shots.
 *           Each blob is [4B uint32 LE byteLen][N FLAC bytes]. 24-bit
 *           lossless FLAC; the FLAC stream's STREAMINFO carries sampleRate,
 *           channels and sampleCount — JSON metadata mirrors them so the
 *           library UI can describe a preset without decoding the audio.
 *
 * Format break v3 → v4: audio is FLAC instead of raw float32 PCM. v3
 * presets remain loadable (the reader dispatches on the version field
 * and falls back to the raw-PCM path); writes always emit v4.
 *
 * Format break v2 → v3: all choice-parameter JSON fields are serialised
 * as stable snake_case keys from BlockParams.h kEntries (the `.key`
 * column). v2 and v1 presets are rejected outright — migrate via the
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

        struct InferenceCacheAudio
        {
            juce::AudioBuffer<float> audio;
            double sampleRate = 44100.0;
        };
        int inferenceCacheCapacity = 0;
        std::vector<InferenceCacheAudio> inferenceCache;

        // Semantic axes (3 slots: dropdown selection + value)
        struct AxisState { int dropdownId = 1; float value = 0.0f; };
        std::array<AxisState, 3> axes;
        bool hasAxes = false;

        // Embeddings (empty if not available)
        std::vector<float> embeddingA, embeddingB;

        // User-assigned classification tags (empty for legacy presets)
        juce::StringArray tags;

        // Research-mode injection state. Old .t5p files predating this feature
        // get the canonical pre-injection defaults — linear / 0.75 / 4 / 16 —
        // so loading an old preset reproduces its original sound regardless
        // of which mode the panel was on. New presets always overwrite these
        // with their saved values.
        juce::String injectionMode { "linear" };
        float lateMixAmount = 0.75f;
        float splitStart    = 4.0f;
        float splitEnd      = 16.0f;
    };

    /** Save current state to a .t5p file with embedded audio. */
    static bool saveToFile(const juce::File& file, T5ynthProcessor& processor,
                           bool includeInferenceCache = true);

    /** Load a preset from file. Returns full result with audio + metadata. */
    static LoadResult loadFromFile(const juce::File& file, T5ynthProcessor& processor);

    /** Get the preset file extension. */
    static juce::String getFileExtension() { return ".t5p"; }

    /** Get default preset directory (creates if needed). Alias for getUserPresetsDirectory(). */
    static juce::File getPresetsDirectory();

    /** Per-user presets directory (writable, creates if needed). */
    static juce::File getUserPresetsDirectory();

    /** All .t5p files under the user presets directory (recursive). */
    static juce::Array<juce::File> getAllPresetFiles();

    /** Name of the GitHub-synced bank ("UCDCAE AI Lab") under the user
     *  presets dir. Used to seed the offline preset bundle and as the
     *  PresetUpdater download target — the bank itself is fully editable
     *  like any other, this is just a well-known directory name. */
    static juce::String getBundledBankName();

private:
    static constexpr char kMagic[4] = { 'T', '5', 'Y', 'N' };
    static constexpr uint32_t kVersion = 4;
    // v3 = raw float32 PCM payloads. v4 = length-prefixed FLAC blobs.
    // Both are accepted on read; writes always emit kVersion.
    static constexpr uint32_t kMinLoadableVersion = 3;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetFormat)
};
