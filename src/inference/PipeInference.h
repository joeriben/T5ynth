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

    /** The instruments BJ has AUTHORISED, from `lco_library.json` — read
     *  directly off disk (no subprocess round trip, no LLM call: this is a
     *  static reference list, the same file authorCsoundOrchestra() already
     *  consults on the Python side). For UI orientation only — the library is
     *  not a menu (project_lco_llm_authors_csound): the author may use these
     *  or write its own code, and it always sees ALL entries, not just these.
     *
     *  The criterion is the `curated` field of the library and nothing else,
     *  and there is ONE way that field comes to exist: tools/lco_build_library
     *  .py composes it from the lexicon entry's `heard` record — BJ's own
     *  words, the date, and the page he heard it on — for the entries whose
     *  `heard.status` is "abgenommen". Recording his approval there is
     *  therefore the ONLY step needed to make an entry appear here — no code
     *  change (BJ 2026-07-30: "diese sind automatisch dort einzublenden").
     *
     *  Do NOT write a `curated` field into the lexicon by hand; the build tool
     *  rejects one. That was the previous mechanism and it put a second home
     *  under a verdict that has only one: six entries got a blanket "built
     *  under the Authoring rules" text — a claim about how they were BUILT —
     *  while five of their own `heard` records said „nur einzelne Dateien
     *  gehört … Das ist kein abgenommenes INSTRUMENT", and `singing_bowl`,
     *  which he had called „verfiziertes instrument", had no such field and so
     *  never appeared here at all.
     *
     *  Never re-derive this from something measurable either. Deriving it from
     *  `params`+`anchor_code` was tried and was wrong in both directions: it
     *  admitted seven entries he had not approved and dropped one he had.
     *
     *  NAMES only, and deliberately no parameters. A catalogue of every
     *  control of every entry laid out in advance is not a thing this surface
     *  does (BJ 2026-07-30: "es werden nicht zig parameter auf vorrat
     *  angezeigt, das geht gar nicht") — the controls that matter are the ones
     *  the author actually reached for in the orchestra it just wrote, and the
     *  KNOBS row already carries those. This decides the question left open in
     *  project_lco_params_are_the_user_surface.
     *
     *  Empty before launch() has recorded a backendDir_, or if the file can't
     *  be found/parsed.
     *
     *  Message thread only, and memoised on the library file's path, timestamp
     *  and size: the caller asks on every panel update, so without the memo a
     *  juce::JSON::parse over the whole ~450 kB file would run on every call —
     *  and SynthPanel::resized() is one of them, i.e. once per frame of a
     *  window drag (docs/PERFORMANCE_GUIDE.md: GUI-CPU regressions are this
     *  project's #1 historical bug class). Keyed rather than latched so that a
     *  library rebuilt while the app runs — an instrument approved, then
     *  tools/lco_build_library.py — reaches the panel at its next update
     *  rather than only after the editor is reopened. Nothing polls it. */
    juce::StringArray getCuratedInstrumentNames() const;

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

        // What the author asked to SET on the synth itself — an array of
        // {id, value, name, note}, value being a number for a continuous
        // parameter and a choice LABEL for a choice one. Empty unless the
        // player allowed it (PID::lcoSetsParams); the backend only ever
        // returns ids from the shelf that was sent with the request, and
        // `note` carries what it had to correct (a clamped value, an unknown
        // id) so the panel can say so rather than the value changing silently.
        juce::var settings;

        // The knobs this body gives the PLAYER — the library parameters it kept,
        // read out of the written code (backend/lco_write.py: read_controls).
        // Decoded with LroControls::fromVar. Empty when the body kept none,
        // which is a legitimate answer: the panel then shows no knobs rather
        // than plausible ones.
        juce::var controls;
    };

    /** External-API author config: an alternative to the local GGUF for a
     *  machine that cannot run a 12B model, for translate()/interpret()/
     *  authorCsoundOrchestra() alike (they share the one author role, see
     *  docs/IPC_PROTOCOL.md's coder_provider entry). `provider` empty (the
     *  default) means "local" and leaves every request's wire bytes exactly
     *  what they always were — the four fields below are then never sent. */
    struct AuthorProviderConfig
    {
        juce::String provider;   // "", "openai_compatible", or "anthropic"
        juce::String apiBase;    // openai_compatible only
        juce::String apiKey;
        juce::String apiModel;
    };

    /** Set the external-author config (called from the message thread when
     *  Settings changes); read by translate()/interpret()/authorCsoundOrchestra()
     *  on their background thread via addAuthorProviderFields(). Guarded by its
     *  OWN mutex (authorConfigMutex_), deliberately NOT stateMutex_: authorCsound
     *  Orchestra() holds stateMutex_ for the WHOLE authoring round-trip (the
     *  comments elsewhere call this "minutes"), so a Settings-tab edit made
     *  while a bake is in flight must not block the message thread until that
     *  bake finishes. */
    void setAuthorProviderConfig(const AuthorProviderConfig& config);

    /** What the EXTERNAL author has cost since the backend process started.
     *  Zero-valued while the local model authors — nothing is billed then, and
     *  nothing is counted.
     *
     *  Money is carried only where the provider itself states an amount, never
     *  derived from a built-in price table — such a table is wrong the day a
     *  provider changes its rates, and a wrong number about the user's money is
     *  worse than none.
     *
     *  Hence three counts rather than one flag. `calls` is every call made;
     *  `tokenCalls` and `costCalls` are how many of those came back with a token
     *  count and with a price. When either is below `calls` the corresponding
     *  figure is a FLOOR, not a total, and the panel has to say so: providers
     *  differ (OpenRouter prices, Anthropic does not; a self-hosted llama.cpp
     *  server may report neither), and one priced call among a hundred printed
     *  as "charged" is off by two orders of magnitude. */
    struct ApiSpend
    {
        int    calls      = 0;
        int    tokenCalls = 0;  // of `calls`, how many reported token counts
        int    costCalls  = 0;  // of `calls`, how many reported a price
        long long inputTokens  = 0;
        long long outputTokens = 0;
        double cost = 0.0;      // in the provider's own unit (OpenRouter: credits ≈ USD)
    };

    /** The running total, as of the last authoring. Callable from any thread. */
    ApiSpend authorApiSpend() const;

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
                                             const juce::var& synthParams = {},
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
    juce::File backendDir_;   // remembered for auto-restart — guarded by backendDirMutex_
    mutable std::mutex backendDirMutex_;         // guards ONLY backendDir_ — deliberately NOT
                                                  // stateMutex_: getCuratedInstrumentNames() reads
                                                  // it from the message thread (SynthPanel::
                                                  // updateVisibility), and stateMutex_ is held for
                                                  // the WHOLE authorCsoundOrchestra() round-trip —
                                                  // an LLM call. Blocking the message thread on
                                                  // that would freeze the GUI for as long as a bake
                                                  // is in flight.
    // Memo for getCuratedInstrumentNames() — message thread only, hence no
    // mutex; see that method's doc comment for why it exists.
    mutable juce::String curatedNamesFile_;
    mutable juce::Time curatedNamesStamp_;
    mutable juce::int64 curatedNamesSize_ = -1;
    mutable juce::StringArray curatedNames_;

    juce::String lastError_;  // human-readable error from last failed launch
    mutable std::mutex authorConfigMutex_;       // guards ONLY authorProviderConfig_ below —
                                                  // see setAuthorProviderConfig()'s doc comment
                                                  // for why this is not stateMutex_.
    AuthorProviderConfig authorProviderConfig_;  // guarded by authorConfigMutex_
    mutable std::mutex apiSpendMutex_;           // guards ONLY apiSpend_ — written on the
                                                  // authoring thread, read by the Settings tab
    ApiSpend apiSpend_;                          // guarded by apiSpendMutex_

    /** Adds coder_provider/coder_api_key/coder_api_model/coder_api_base to a
     *  request's JSON when authorProviderConfig_.provider is set — shared by
     *  translate()/interpret()/authorCsoundOrchestra() so the field list
     *  cannot drift between the three call sites. No-op (and no wire change)
     *  when provider is empty. Takes its own brief lock on authorConfigMutex_;
     *  independent of whatever the caller already holds. */
    void addAuthorProviderFields(juce::DynamicObject::Ptr json) const;

    juce::File findBundledBinary(const juce::File& backendDir) const;
    bool isCompatibleBundledBinary(const juce::File& binary) const;
    /** Mirrors findBundledBinary's dual layout: a local PyInstaller test build
     *  nests it under dist/pipe_inference/ (PyInstaller's `datas` land at '.'
     *  inside that folder); a dev run or an installed/shipped app has it as a
     *  direct sibling of the backend directory. */
    juce::File findLibraryFile(const juce::File& backendDir) const;
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
