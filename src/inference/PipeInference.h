#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <functional>
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

    /** Per-model static metadata reported by Python at startup.
     *
     *  Currently surfaces:
     *  - ditBlocks: number of DiT blocks in the diffusion transformer.
     *               UI sliders (layer_split / kombi modes) clamp to this.
     *  - samplerType: native-path sampler the backend will dispatch on for
     *               this model. Informational on the plugin side (the
     *               plugin does not pass this through generate()).
     *
     *  Missing entries get default-constructed ModelMetadata (16 blocks,
     *  dpmpp-2m-sde) so the plugin behaves like older builds when an
     *  older backend doesn't supply the field.
     */
    struct ModelMetadata
    {
        int ditBlocks = 16;
        juce::String samplerType = "dpmpp-2m-sde";
    };
    ModelMetadata getModelMetadata(const juce::String& modelName) const;

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
        juce::String trackType;    // SA3 only: "music"/"instrument"/"sfx" — overrides the backend's dir-name modality sniff. Required for single-checkpoint SA3 (medium/large), which render multiple domains and carry no music/sfx token. Empty = sniff (unchanged).
        int modalityEpoch = 1;     // per-preset TrackType-routing behaviour (1 == kModalityEpoch in PluginProcessor.h; epoch<=0 == pre-2.5.0 legacy). buildInferenceRequest always sets it from the processor; the backend acts on it only for SA3.
        std::vector<std::pair<int, float>> dimensionOffsets;  // DimensionExplorer offsets
        std::map<juce::String, float> semanticAxes;           // SemanticAxes key→value
        float axesAmount = 1.0f;                              // Master scaler for all semantic-axis deltas

        // Research-mode injection (A↔B mixing): "linear" reproduces v1.2 byte-identically.
        // Non-linear modes are only implemented on the native SA Open path.
        juce::String injectionMode = "linear";          // "linear" | "late_step" | "layer_split" | "kombi1"/"kombi2"/"kombi3"
        float        injectionTransitionAt = 0.6f;       // 0.05–0.95, used by "late_step" and the Kombi modes
        float        latePhaseAlpha        = 0.0f;       // -1..+1, used by "late_step" and the Kombi modes: late blend α (0 = 50/50, +1 = pure B)
        float        splitStart            = 4.0f;       // 0–ditBlocks, used by "layer_split"; Kombi modes send a per-mode fraction of the DiT depth (backend re-asserts)
        float        splitEnd              = 16.0f;      // 0–ditBlocks, used by "layer_split"; Kombi modes send a per-mode fraction of the DiT depth (backend re-asserts)

        // Resynth / init_audio (i2i): when initAudio holds samples the backend
        // denoises from it (encoded into the VAE latent) instead of pure noise,
        // while the text conditioning above still applies. Empty initAudio → the
        // field is omitted from the wire request → plain text-only generation.
        juce::AudioBuffer<float> initAudio;              // planar; empty = no resynth
        double       initAudioSampleRate   = 0.0;        // sample rate of initAudio
        float        initNoiseLevel        = 1.0f;       // 1.0 = full re-gen, lower = closer to input
    };

    struct Result
    {
        bool success = false;
        juce::AudioBuffer<float> audio;
        double sampleRate = 44100.0;
        float generationTimeMs = 0.0f;
        int seed = -1;
        juce::String errorMessage;
        std::vector<float> embeddingA;   // mean-pooled prompt A (768 dims)
        std::vector<float> embeddingB;   // mean-pooled prompt B (768 dims, zeros if no B)
        std::vector<float> embeddingBaseline; // final conditioning before DimensionExplorer offsets
    };

    struct TranslateResult
    {
        bool success = false;
        juce::String text;          // English translation (empty for empty input)
        juce::String errorMessage;  // set when success == false
    };

    struct InterpretResult
    {
        bool success = false;
        juce::String text;          // the interpreted prompt (empty for empty input)
        juce::String errorMessage;  // set when success == false
    };

    struct AnalyzeResult
    {
        bool success = false;
        juce::String tags;          // CLAP top-k timbre tags, comma-joined
        juce::String spectral;      // DSP spectral words, e.g. "warm, full-bodied, tonal"
        juce::String errorMessage;  // set when success == false
    };

    /** Phase 4 (SPEC_phase4_5_csound_llm_preset.md): a prompt-authored Csound
     *  orchestra + its human-readable reading, decoded from the backend's
     *  {ok, orchestra, reading, params_text, spec, error} response. */
    struct CsoundAuthorResult
    {
        bool success = false;
        juce::String orchestra;     // full CSD text (Phase-3 contract, %SR% unsubstituted)
        juce::String reading;       // human "how it was heard" line(s), one per layer
        juce::String paramsText;    // the Csound the model actually wrote (the authored
                                     // body) — the panel's transparency surface
        juce::String authorModel;   // directory name of the model that ACTUALLY wrote this
                                     // orchestra, straight from the backend's resolver. The
                                     // panel names this instead of a compiled-in string, so a
                                     // resolver that walks past the intended slot is visible.
        juce::String errorMessage;  // set when success == false

        // ── The authoring TRACE (backend/lco_write.py, "consultation"/"repairs") ──
        // What actually happened on the way to this orchestra, for the panel to
        // show. The author is asked for no account of itself and none is
        // fabricated here: every field below is a record the write-path already
        // kept and previously discarded.
        //
        // The author's own reasoning, verbatim — the plain-language paragraph it
        // writes BEFORE the fenced orchestra (lco_write's HOW TO ANSWER): what it
        // decided the sound is, what excites and resonates in it, what moves. The
        // one place the machine says WHY this code. Empty when the reply had no
        // prose outside the fence.
        juce::String thinking;
        // What the AUTHOR ASKED FOR, by section. The write-path reads the shelf
        // (every entry and what its parameters do, no code), reasons about the
        // sound, and names what it wants. So this is the model's own choice, not
        // a word match against the prompt. An empty motions list is a real
        // finding, not a gap in the data: it says the author asked for no
        // movement, which movement-by-default then has to answer for.
        juce::StringArray namedInstruments, namedAdjectives, namedMotions;
        // What was actually PUT IN FRONT OF IT. Normally the same set; it is the
        // whole library when the author named no instrument, because neither
        // "asked for none" nor "named one this reader could not place" tells the
        // backend which subset the author may see. Kept apart from `named` so the
        // panel never has to infer one from the other.
        juce::StringArray openedInstruments, openedAdjectives, openedMotions;
        int libraryEntryCount = 0;  // entries in the whole library, for "n of N"
        // Csound's own errors this body had to be repaired past, in first-seen
        // order; empty when it compiled on the first attempt.
        juce::StringArray repairs;
        int attempts = 0;           // how many authoring passes it took (1 = first try)
    };

    /** Blocking generation — call from background thread.
     *  Auto-restarts Python if subprocess died. */
    Result generate(const Request& request);

    /** Blocking prompt translation to English via the app's ONE language model
     *  (the same model that authors the LCO's Csound) — call from a background
     *  thread. Auto-restarts Python if the subprocess died. `device` may be empty
     *  (backend default); the backend resolves the model itself, there is no
     *  model to choose. Empty input returns success with empty text and no
     *  subprocess round-trip. Returns success == false (errorMessage set) when the
     *  language model is not installed. */
    TranslateResult translate(const juce::String& text,
                              const juce::String& device);

    /** Blocking prompt INTERPRETATION via the same language model as translate(),
     *  but the caller supplies the system prompt (the loop "stance") — call from a
     *  background thread. `device` may be empty (backend default).
     *  Empty input returns success with empty text and no round-trip. Returns
     *  success == false (errorMessage set) when the language model is not installed.
     *  This is the LLM half of the CLAP→LLM semantic loop.
     *
     *  @param maxNewTokens  pass 0 for NO CAP — the key is then omitted from the
     *         request and the backend sizes the reply to the real model context.
     *         There is deliberately no default argument (the following `device`
     *         parameter has none either): every call site must state its choice.
     *         Pass a positive number ONLY to deliberately truncate — the backend
     *         keeps no cap of its own and treats this call site as the owner of
     *         that decision, so any hard output limit in this system originates
     *         here. A cap does not error when it bites; it cuts mid-sentence.
     *
     */
    InterpretResult interpret(const juce::String& systemPrompt,
                              const juce::String& userText,
                              int maxNewTokens,
                              const juce::String& device);

    /** Blocking CLAP machine-listening analysis of an audio buffer → top-k timbre
     *  tags + DSP spectral words — call from a background thread. The audio is sent
     *  on the init_audio wire keys (planar float32, base64); CLAP runs CPU-pinned
     *  in the backend. This is the ear half of the CLAP→LLM semantic loop. */
    AnalyzeResult analyze(const juce::AudioBuffer<float>& audio,
                          double sampleRate,
                          int topk,
                          const juce::String& device);

    /** Blocking Csound orchestra authoring (SPEC_phase4_5_csound_llm_preset.md,
     *  Phase 4) — call from a background thread. The backend routes the WHOLE
     *  prompt through the instruct model + the Csound lexicon
     *  (backend/csound_author.py — one constrained call, one retry on a
     *  parse/assembly failure), and returns a ready-to-compile orchestra + a
     *  human reading, or an honest failure. LLM-first, no fallback: never a
     *  default/keyword-matched orchestra. Empty input returns success == false
     *  without a round-trip. */
    /** @param correction  a self-check finding from a PREVIOUS attempt at this
     *                     same prompt ("asked for X, but the sound is described
     *                     as Y"). Empty = a first authoring, and the wire bytes
     *                     are then exactly what they always were. Non-empty puts
     *                     the author on its correction brief: repair that miss,
     *                     keep the request. It is never a replacement prompt.
     *  @param previous    that previous attempt's own reading. Each author call is
     *                     fresh, so without this the brief's "keep what the
     *                     listener did not name" points at a patch the model has
     *                     never seen — and it then repairs by deleting what was
     *                     already right. Only read when `correction` is set. */
    /** @param onThinking  called on THIS (background) thread with the author's
     *                     reasoning while it is still being written, and with
     *                     the attempt it belongs to — a repair restarts the
     *                     reasoning, so the text replaces rather than continues.
     *                     Supplying any of the three callbacks is what asks the
     *                     backend to stream at all; without them the wire bytes
     *                     are exactly what they always were. Every callback must
     *                     marshal to the message thread itself before touching
     *                     any component.
     *  @param onBody      the same contract for the CODE (§4.6 kind "body"):
     *                     the whole body written so far, replace-not-append.
     *                     The code is most of the generation — this is what
     *                     keeps the panel from going dark for minutes the
     *                     moment the reasoning ends.
     *  @param onAttempt   a REPAIR round starting (§4.6 kind "attempt"), before
     *                     its inference runs: the round number, the configured
     *                     ceiling, and the distinct compiler errors the author
     *                     is being shown, joined "; " for a status line. A
     *                     first authoring that compiles never fires it. */
    CsoundAuthorResult authorCsoundOrchestra(const juce::String& text,
                                             const juce::String& correction = {},
                                             const juce::String& previous = {},
                                             std::function<void(int, const juce::String&)> onThinking = {},
                                             std::function<void(int, const juce::String&)> onBody = {},
                                             std::function<void(int, int, const juce::String&)> onAttempt = {});

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
    // Kill-on-close job holding the backend: closing this handle — including
    // implicitly, when this process dies without reaching shutdown() — takes the
    // backend down with it. The Windows half of the orphan guard; the Python
    // side (pipe_inference.py, _start_parent_watchdog) cannot cover this
    // platform because getppid() there keeps returning a dead parent's id.
    HANDLE hJob_           = nullptr;
   #else
    int stdinFd_ = -1;   // parent → child (write)
    int stdoutFd_ = -1;  // child → parent (read)
    pid_t childPid_ = -1;
   #endif

    juce::StringArray availableDevices_;
    juce::String defaultDevice_;
    juce::StringArray availableModels_;
    juce::String defaultModel_;
    std::map<juce::String, ModelMetadata> modelMetadata_;
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

    /** Common handling for "the status byte never came". Returns the message to
     *  report. A dead child is restarted, as before; a LIVE child that missed
     *  its deadline is restarted too, because the request is still in flight and
     *  its reply frame will arrive into a pipe nobody is reading — the next
     *  request would then consume that stale status byte and every later
     *  response would be off by one frame. There is no drain path: once the
     *  deadline has passed we cannot know how much is pending, so re-establishing
     *  a known-good pipe is the only honest resynchronisation.
     *  @param what  what was being waited for, for the log and the message. */
    juce::String handleStatusTimeout(const char* what);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PipeInference)
};
