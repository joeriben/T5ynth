#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <mutex>
#include <vector>
#include <utility>
#include <map>
#ifdef _WIN32
 #include <windows.h>
#endif

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

    /** Last error message from a failed launch(). */
    const juce::String& getLastError() const { return lastError_; }

    /** Devices reported by Python at startup. */
    const juce::StringArray& getAvailableDevices() const { return availableDevices_; }
    const juce::String& getDefaultDevice() const { return defaultDevice_; }

    /** Models reported by Python at startup. */
    const juce::StringArray& getAvailableModels() const { return availableModels_; }
    const juce::String& getDefaultModel() const { return defaultModel_; }

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
        juce::String model;        // model ID (e.g. "stable-audio-open-1.0"), or empty for default
        std::vector<std::pair<int, float>> dimensionOffsets;  // DimensionExplorer offsets
        std::map<juce::String, float> semanticAxes;           // SemanticAxes key→value

        // Research-mode injection (A↔B mixing): "linear" reproduces v1.2 byte-identically.
        // "late_step" / "layer_split" only implemented for the native SA Open path.
        juce::String injectionMode = "linear";          // "linear" | "late_step" | "layer_split"
        float        injectionTransitionAt = 0.6f;       // 0.05–0.95, only used by "late_step"
        float        latePhaseAlpha        = 0.0f;       // -1..+1, only used by "late_step": late blend α (0 = 50/50, +1 = pure B)
        float        splitStart            = 4.0f;       // 0–16, only used by "layer_split": B-zone low edge
        float        splitEnd              = 16.0f;      // 0–16, only used by "layer_split": B-zone high edge
    };

    struct Result
    {
        bool success = false;
        juce::AudioBuffer<float> audio;
        float generationTimeMs = 0.0f;
        int seed = -1;
        juce::String errorMessage;
        std::vector<float> embeddingA;   // mean-pooled prompt A (768 dims)
        std::vector<float> embeddingB;   // mean-pooled prompt B (768 dims, zeros if no B)
        std::vector<float> embeddingBaseline; // final conditioning before DimensionExplorer offsets
    };

    /** Blocking generation — call from background thread.
     *  Auto-restarts Python if subprocess died. */
    Result generate(const Request& request);

    /** Preload a model+device combo so first generate is fast.
     *  Blocking — call from background thread. Returns true on success. */
    bool preload(const juce::String& model, const juce::String& device);

    /** Check if the Python subprocess is still alive. */
    bool isChildAlive() const;

    /** Check if pipe handles are connected. */
    bool isConnected() const;

private:
    mutable std::recursive_mutex stateMutex_;
    std::atomic<bool> ready_ { false };
    std::atomic<bool> launching_ { false };  // prevents concurrent launch() calls
   #ifdef _WIN32
    HANDLE hChildStdinWr_  = INVALID_HANDLE_VALUE;  // parent → child
    HANDLE hChildStdoutRd_ = INVALID_HANDLE_VALUE;  // child → parent
    HANDLE hProcess_       = INVALID_HANDLE_VALUE;
   #else
    int stdinFd_ = -1;   // parent → child (write)
    int stdoutFd_ = -1;  // child → parent (read)
    pid_t childPid_ = -1;
   #endif

    juce::StringArray availableDevices_;
    juce::String defaultDevice_;
    juce::StringArray availableModels_;
    juce::String defaultModel_;
    juce::File backendDir_;   // remembered for auto-restart
    juce::String lastError_;  // human-readable error from last failed launch

    juce::File findBundledBinary(const juce::File& backendDir) const;
    bool isCompatibleBundledBinary(const juce::File& binary) const;
    void prepareBundledBinary(const juce::File& backendDir, const juce::File& binary) const;
    juce::String maybeAugmentMacStandaloneError(const juce::File& backendDir,
                                                const juce::String& detail) const;
    juce::String findPython(const juce::File& backendDir) const;
    bool readExact(void* dest, int numBytes, int timeoutMs = 120000);
    bool writeExact(const void* src, int numBytes);
    bool tryRestart();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PipeInference)
};
