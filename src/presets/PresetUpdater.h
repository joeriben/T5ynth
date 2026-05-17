#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

/**
 * Pulls the read-only "UCDCAE AI Lab" preset bank from the public GitHub
 * mirror (joeriben/T5ynth-Presets). Workflow:
 *
 *   1. Fetch `manifest.json` listing every distributed .t5p with size + SHA256
 *   2. Diff against the local bank — already present and hash-matched → skip
 *   3. Download new / changed entries via raw.githubusercontent.com (no API
 *      rate-limit; the only API call is the manifest itself, served as raw)
 *   4. Optionally prune local files that disappeared from the manifest
 *
 * All HTTP runs on a detached background thread; progress is exposed via an
 * atomic counter and consumed by an owning juce::Timer. UI-thread callbacks
 * marshal results back through juce::MessageManager::callAsync.
 *
 * The bank lives in the user-writable presets dir, NOT in the system factory
 * dir, so updates never need elevated privileges. PresetManagerPanel treats
 * the directory as read-only via path inspection (see PresetFormat).
 */
class PresetUpdater
{
public:
    PresetUpdater();
    ~PresetUpdater();

    struct Stats
    {
        int added     = 0;
        int updated   = 0;
        int unchanged = 0;
        int failed    = 0;
        int removed   = 0;
    };

    using ProgressCallback = std::function<void(double progress, juce::String message)>;
    using FinishCallback   = std::function<void(bool success, Stats stats, juce::String error)>;

    /** Kick off the update on a background thread. Callbacks fire on the
     *  message thread. Calling start() while already running is a no-op. */
    void start(ProgressCallback onProgress, FinishCallback onFinish);

    /** Request cancellation. The background thread observes the flag at file
     *  boundaries; partial files are deleted. Safe to call from any thread. */
    void cancel();

    bool isRunning() const noexcept
    {
        return activeState_ && activeState_->running.load();
    }

    /** Disk location of the read-only bank, e.g.
     *  ~/Library/Application Support/T5ynth/presets/UCDCAE AI Lab/.
     *  Always returns a valid juce::File even if the dir does not exist yet. */
    static juce::File getBankDirectory();

    /** Display name shown in the sidebar and on the update button. */
    static juce::String getBankName();

    /** URL of the JSON manifest in the upstream repo. */
    static juce::String getManifestUrl();

private:
    struct ManifestEntry
    {
        juce::String name;     // display name (preset stored "name" field)
        juce::String path;     // path relative to manifest base_url, e.g. "Pad.t5p"
        int64_t      size = 0; // bytes
        juce::String sha256;   // lowercase hex
    };

    struct ParsedManifest
    {
        juce::String              baseUrl;
        std::vector<ManifestEntry> entries;
    };

    /** Worker-owned state shared with the background thread via shared_ptr.
     *  Lifetime is independent of PresetUpdater itself, so the detached
     *  thread can outlive its owning UI object (e.g. the user closes the
     *  plugin window mid-download) without dereferencing freed memory. */
    struct State
    {
        std::atomic<bool> running { true };
        std::atomic<bool> cancel  { false };
    };

    static bool fetchManifest(ParsedManifest& out, juce::String& error);
    static bool downloadOne(const juce::URL& url,
                            const juce::File& target,
                            int64_t expectedSize,
                            const std::shared_ptr<State>& state,
                            juce::String& error);
    static juce::String sha256OfFile(const juce::File& file);

    std::shared_ptr<State> activeState_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetUpdater)
};
