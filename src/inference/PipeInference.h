#pragma once
#include <JuceHeader.h>
#include <atomic>

/**
 * Pipe-based inference — runs diffusers in a Python subprocess.
 *
 * Protocol:
 *   Ready:    Python sends \x02 on stdout when pipeline loaded
 *   Request:  JUCE writes single-line JSON to stdin
 *   Response: \x01 + header (6 fields: flag,samples,channels,sr,seed,timeMs) + float32 PCM
 *   Error:    \x00 + uint32 length + UTF-8 message
 *
 * The Python process stays alive between generations (no startup cost per request).
 * Thread-safe: generate() is blocking and should be called from a background thread.
 */
class PipeInference
{
public:
    PipeInference() = default;
    ~PipeInference();

    /** Launch the Python inference subprocess. Returns true when ready. */
    bool launch(const juce::File& backendDir);

    /** Shut down the subprocess. */
    void shutdown();

    bool isReady() const { return ready_.load(); }

    /** Devices reported by Python at startup. */
    const juce::StringArray& getAvailableDevices() const { return availableDevices_; }
    const juce::String& getDefaultDevice() const { return defaultDevice_; }

    struct Request
    {
        juce::String promptA;
        juce::String promptB;
        float alpha = 0.0f;
        float magnitude = 1.0f;
        float noiseSigma = 0.0f;
        float durationSeconds = 3.0f;
        float startPosition = 0.0f;
        int steps = 20;
        float cfgScale = 7.0f;
        int seed = -1;
        juce::String device;       // "mps", "cuda", "cpu", or empty for default
    };

    struct Result
    {
        bool success = false;
        juce::AudioBuffer<float> audio;
        float generationTimeMs = 0.0f;
        int seed = -1;
        juce::String errorMessage;
    };

    /** Blocking generation — call from background thread. */
    Result generate(const Request& request);

private:
    std::atomic<bool> ready_ { false };
    int stdinFd_ = -1;   // parent → child (write)
    int stdoutFd_ = -1;  // child → parent (read)
    pid_t childPid_ = -1;

    juce::StringArray availableDevices_;
    juce::String defaultDevice_;

    juce::String findPython(const juce::File& backendDir) const;
    bool readExact(void* dest, int numBytes, int timeoutMs = 120000);
    bool writeExact(const void* src, int numBytes);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PipeInference)
};
