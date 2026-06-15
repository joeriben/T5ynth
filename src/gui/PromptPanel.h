#pragma once
#include <JuceHeader.h>
#include <vector>
#include <utility>
#include <functional>
#include <map>
#include <limits>
#include "../inference/PipeInference.h"
#include "GuiHelpers.h"  // FlippedVerticalSlider, AlphaSliderLnF, impulse colours

class T5ynthProcessor;

/**
 * GENERATION column: prompts, embedding controls, compact params, generate.
 */
class PromptPanel : public juce::Component, private juce::Timer
{
public:
    explicit PromptPanel(T5ynthProcessor& processor);
    ~PromptPanel() override { stopTimer(); }

    void paint(juce::Graphics& g) override;
    void resized() override;
    int getPreferredHeightForWidth(int width) const;
    void setEasyMode(bool easy);
    bool isEasyMode() const { return easyMode_; }
    bool hasHiddenActiveState() const;

    /** Load preset data that isn't in APVTS (prompts, seed, random toggle, device, model,
     *  research-mode injection state). The injection fields are optional — when omitted,
     *  the panel keeps its current values, which lets callers decide per-feature whether
     *  to push or preserve. */
    void loadPresetData(const juce::String& promptA, const juce::String& promptB,
                        int seed, bool randomSeed,
                        const juce::String& device = {},
                        const juce::String& model = {},
                        const juce::String& injectionMode = {},
                        float lateMixAmount = std::numeric_limits<float>::quiet_NaN(),
                        float splitStart = std::numeric_limits<float>::quiet_NaN(),
                        float splitEnd = std::numeric_limits<float>::quiet_NaN());

    /** Read current prompt/seed state (for preset save). */
    juce::String getPromptA() const { return promptAEditor.getText().trim(); }
    juce::String getPromptB() const { return promptBEditor.getText().trim(); }
    int getSeed() const { return seedEditor.getText().getIntValue(); }
    bool isRandomSeed() const { return randomSeedToggle.getToggleState(); }

    /** Read current injection-mode state (for preset save). */
    juce::String getInjectionMode()  const { return injectionMode_; }
    float        getLateMixAmount()  const { return lateMixForMode(injectionMode_); }
    float        getSplitStart()     const { return splitLayerStart_; }
    float        getSplitEnd()       const { return splitLayerEnd_; }

    /** Trigger generation with optional dimension offsets from DimensionExplorer. */
    void triggerGenerationWithOffsets(std::vector<std::pair<int, float>> offsets);

    /** Set semantic axis values to include in the next generation request. */
    void setSemanticAxes(std::map<juce::String, float> axes) { pendingAxes_ = std::move(axes); }

    /** Re-read available devices/models after the backend was restarted. */
    void refreshInferenceChoices();

    /** Called after generation with prompt refs + explorer baseline. */
    std::function<void(const std::vector<float>&, const std::vector<float>&, const std::vector<float>&)> onEmbeddingsReady;

    /** Status callback — called with status text (e.g. "generating...", "12.3s | seed 42 | mps") */
    std::function<void(const juce::String&, bool generating)> onStatusChanged;

    /** Callback to read AxesPanel values with per-slot drift offsets (wired by MainPanel). */
    std::function<std::map<juce::String, float>(float, float, float)> getAxisValuesCallback;

    /** Fired whenever the selected model may have changed (model click, preset
     *  load, backend availability). MainPanel uses it to grey out the AxesPanel
     *  for SA3, whose semantic axes are deactivated pending recalculation. */
    std::function<void()> onModelChanged;

    /** Paint ghost overlay for alpha slider (drift modulation indicator). */
    void paintOverChildren(juce::Graphics& g) override;

    bool isGenerating() const { return generating; }

private:
    void timerCallback() override;
    void triggerGeneration();
    // Manual Union-Jack action: translate the A/B prompts to English IN PLACE.
    // Pauses auto-regen for the duration (frees the shared IPC pipe); it resumes
    // automatically, with its unchanged bar setting, when the translation finishes.
    void translatePromptsInPlace();
    bool playNextCachedInference();
    void syncSeedEditorEnabledState();
    void syncSeedEditorFont(float size);
    void syncSeedEditorDisplay(int seed, bool force = false);

    /** Build a PipeInference::Request from current UI state, with optional overrides. */
    PipeInference::Request buildInferenceRequest(float alphaOverride = std::numeric_limits<float>::quiet_NaN(),
                                                  std::map<juce::String, float> axesOverride = {},
                                                  float noiseOverride = std::numeric_limits<float>::quiet_NaN(),
                                                  float magnitudeOverride = std::numeric_limits<float>::quiet_NaN(),
                                                  float lateMixOverride = std::numeric_limits<float>::quiet_NaN(),
                                                  float splitStartOverride = std::numeric_limits<float>::quiet_NaN(),
                                                  float splitEndOverride = std::numeric_limits<float>::quiet_NaN(),
                                                  float resynthOverride = std::numeric_limits<float>::quiet_NaN());

    /** Trigger generation from drift auto-regen. */
    void triggerDriftRegeneration(float effectiveAlpha,
                                  std::map<juce::String, float> effectiveAxes,
                                  float effectiveNoise,
                                  float effectiveMagnitude,
                                  float effectiveLateMix,
                                  float effectiveSplitStart,
                                  float effectiveSplitEnd,
                                  float effectiveResynth = std::numeric_limits<float>::quiet_NaN(),
                                  bool holdForBar = false);

    /** Check if drift requires auto-regeneration (called from timerCallback). */
    void pollDriftRegen();

    /** One step of the CLAP→LLM semantic self-listening loop, mirroring one
     *  iteration body of clap_llm_loop.py run_llm_loop (minus its own generate()).
     *  Called from BOTH generation-complete callbacks (manual + Auto-Regen) on the
     *  message thread with the just-rendered result. It spawns a background thread
     *  that analyzes the audio (CLAP ear) and interprets the tags into the next
     *  prompt(s) per the active stance + coupling, then writes them back into the
     *  editor(s) on the message thread WITHOUT re-triggering onTextChange. The NEXT
     *  generation (manual Generate or the Auto-Regen standing trigger) renders the
     *  rewritten prompts. No-op (cheap early-out) unless a stance is active AND the
     *  model is SA3. */
    void runSemanticLoopStep(const PipeInference::Result& result);

    T5ynthProcessor& processorRef;

    // Prompts — colour-coded editors (purple A / yellow B) replace the old text labels
    juce::TextEditor promptAEditor, promptBEditor;
    UnionJackButton translateToggle;  // session-only: translate prompts → English before conditioning

    // Embedding controls.  alphaLnF is declared BEFORE alphaSlider so the slider
    // (a later member) is destroyed first — the LnF must outlive its only user.
    AlphaSliderLnF alphaLnF;             // A→B gradient track + position-coloured thumb
    FlippedVerticalSlider alphaSlider;   // vertical A↔B blend, A at top
    juce::Slider magnitudeSlider, noiseSlider;
    juce::Label alphaLabel, alphaValue;  // retained for callbacks but hidden (gradient is self-describing)
    juce::Label magLabel, magValue, magHint;
    juce::Label noiseLabel, noiseValue, noiseHint;

    // Compact params rows: Magnitude/Noise, Steps/CFG, Duration/Seed
    juce::Slider durationSlider, stepsSlider, cfgSlider;
    juce::Label durLabel, durValue, durHint;
    juce::Label stepsLabel, stepsValue, stepsHint;
    juce::Label cfgLabel, cfgValue, cfgHint;
    juce::Label seedLabel;
    juce::TextEditor seedEditor;
    juce::TextButton randomSeedToggle { "Rnd" };
    static constexpr int kNumSeedModeBtns = 3;
    juce::TextButton seedModeBtns[kNumSeedModeBtns];
    juce::Rectangle<int> seedModeSwitchBounds;
    enum class SeedMode { base = 0, steady = 1, autoRandom = 2 };
    SeedMode seedMode_ = SeedMode::base;
    void setSeedMode(SeedMode mode, bool applyState);
    void syncSeedModeFromCurrentState();
    void syncSeedModeButtons();

    // Model selector (fixed 4-slot switchbox: SA3 Music | SA1 Open | SA1 Small | AudioLDM2).
    // Visible in both compact and easy modes — the model choice is central
    // enough to the synth's behavior that hiding it in easy mode left the
    // user without a way to switch engines on the fly.
    static constexpr int kNumModelSlots = 5;
    juce::TextButton modelBtns[kNumModelSlots];
    juce::String modelSlotIds[kNumModelSlots];  // resolved model directory name per slot
    juce::Rectangle<int> modelSwitchBounds;
    // Delineation guides painted in paint(): a recessed band framing the mode
    // bar, and a divider line separating the impulse/blend group from the params.
    juce::Rectangle<int> modeBandBounds;
    int paramsDividerY = -1;
    bool modelsPopulated = false;
    juce::String pendingModel_;  // deferred model selection until models are populated
    // Deferred split values: when a preset arrives before the backend has
    // reported its model list, we cache the preset's splitStart/splitEnd
    // here and clamp them against ditBlocks_ once populateModelSelector
    // has run refreshDitBlocksForCurrentModel for the resolved model. NaN
    // means "no pending value" so the panel falls back to current state.
    float pendingSplitStart_ = std::numeric_limits<float>::quiet_NaN();
    float pendingSplitEnd_   = std::numeric_limits<float>::quiet_NaN();
    void populateModelSelector();
    juce::String getSelectedModel() const;
    /** Pull the active model's DiT block count from the backend's handshake
     *  metadata into ditBlocks_, then clamp/expand the layer-split slider
     *  accordingly. Called after model selection changes and after the
     *  backend's ready frame arrives. */
    void refreshDitBlocksForCurrentModel();

    /** Re-scope the Duration slider for the active model: 120s for SA3 (music-
     *  scale / deconstructed samples), 11s otherwise. Clamps the parameter into
     *  the new ceiling. Called wherever refreshDitBlocksForCurrentModel runs. */
    void applyDurationRangeForCurrentModel();

    // Device selection is backend-controlled: GPU/Metal when available, else CPU.
    juce::String defaultInferenceDevice_;
    bool devicesPopulated = false;
    void populateDeviceChoice();

    juce::TextButton generateButton { "Generate" };
    juce::Label infoLabel;

    // ── Temporary injection-mode test UI (research; not in APVTS). ──
    // Six toggle buttons + the existing alphaSlider whose label/range/state
    // shift with the active mode (linear=A↔B, step-in=step-transition, layer=split,
    // combo1/2/3=Step-in-style slider with hardcoded layer band).
    juce::TextButton injModeLinear { "Linear" };
    juce::TextButton injModeFine   { "Step-in" }; // = "late_step" — operates on sampler refinement steps
    juce::TextButton injModeLayer  { "Layer" };   // = "layer_split"
    juce::TextButton injModeKombi1 { "Combo 1" }; // = late × low-layer band [0..4]
    juce::TextButton injModeKombi2 { "Combo 2" }; // = late × broad-mid band [4..12]
    juce::TextButton injModeKombi3 { "Combo 3" }; // = late × narrow-center band [6..10]
    juce::String     injectionMode_         = "linear";  // "linear" | "late_step" | "layer_split" | "kombi1"/"kombi2"/"kombi3"
    // Step-in and the three Combo modes each remember their own slider position
    // (0–1, intensity), so a user A/B-ing the modes by clicking buttons
    // doesn't lose the last-used value of any individual mode. Layer mode
    // uses splitLayerStart_/splitLayerEnd_ instead. The backend's
    // `injection_transition_at` is the early-phase fraction; we send
    // 0.5 - 0.45·t (so t=0 → transition halfway, t=1 → almost immediate),
    // and `late_phase_alpha` = t directly (0 → 50/50 late blend, 1 → pure B).
    float            lateMixFine_           = 0.5f;
    float            lateMixKombi1_         = 0.5f;
    float            lateMixKombi2_         = 0.5f;
    float            lateMixKombi3_         = 0.5f;
    /** Returns a reference to the slot that owns the slider value for the
     *  active mode. Falls back to the Step-in slot for "linear" and "layer_split"
     *  (those modes use other state, but a non-null reference simplifies
     *  the call sites). */
    float&       lateMixForMode(const juce::String& mode);
    float        lateMixForMode(const juce::String& mode) const;
    // Layer mode: two-thumb range slider defining the B-zone [start, end]
    // along the DiT block indices. Both thumbs at extremes → full B;
    // start == end → no B (pure A); narrow range → B injected only into
    // a sub-band of layers (mid / early / late depending on position).
    // Range is 0..ditBlocks_; ditBlocks_ is refreshed from backend metadata
    // (SAO Small = 16, SA3 Small may differ) — refreshDitBlocksForCurrentModel
    // updates the slider on model change.
    float            splitLayerStart_       = 4.0f;       // 0–ditBlocks_, low thumb
    float            splitLayerEnd_         = 16.0f;      // 0–ditBlocks_, high thumb (default = top: B from layer 4 onwards)
    int              ditBlocks_             = 16;         // per-model count; updated on model change

    /** Reconfigure alphaSlider (range, label, value, attachment) for the active mode. */
    void applyModeToSlider();
    bool isAudioLDM2Model(const juce::String& model) const;
    bool selectedModelIsAudioLDM2() const;
    bool isSA3Model(const juce::String& model) const;
    /** Resolve a stored/preset model id to an installed switchbox slot: exact
     *  id match, then family fallback (patternSlotFor); -1 if none installed. */
    int  slotForModel(const juce::String& model) const;

public:
    /** True when the active model is SA3 (Stable Audio 3 Small Music). Public
     *  so MainPanel can gate the AxesPanel — SA3's semantic axes are disabled
     *  pending recalculation for its t5gemma conditioner. */
    bool selectedModelIsSA3() const;

private:
    /** Per-model defaults for the steps/CFG params. The model-click handler
     *  applies these on model switch, and hasHiddenActiveState() compares
     *  against them to decide whether easy mode's params reflect a
     *  user-modified state. Keeping the mapping in one place ensures the
     *  two stay in lockstep — drift would mean the dirty indicator
     *  contradicts the values the backend actually receives. */
    struct DefaultParams { float steps; float cfg; };
    DefaultParams defaultParamsFor(const juce::String& model) const;
    void selectInjectionMode(const juce::String& mode, bool trigger);
    void syncInjectionModeAvailability();

    bool generating = false;
    bool translatingPrompts_ = false;  // Union-Jack translate in flight (blocks IPC overlap)
    std::vector<std::pair<int, float>> pendingOffsets_;  // for DimensionExplorer
    std::map<juce::String, float> pendingAxes_;          // for SemanticAxes

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Attachment> alphaA, magA, noiseA, durA, stepsA, cfgA;
    bool easyMode_ = false;

    // Auto-regen state
    float lastGenAlpha_ = std::numeric_limits<float>::quiet_NaN();
    float lastGenNoise_ = std::numeric_limits<float>::quiet_NaN();
    float lastGenMagnitude_ = std::numeric_limits<float>::quiet_NaN();
    float lastGenLateMix_ = std::numeric_limits<float>::quiet_NaN();
    float lastGenSplitStart_ = std::numeric_limits<float>::quiet_NaN();
    float lastGenSplitEnd_ = std::numeric_limits<float>::quiet_NaN();
    std::map<juce::String, float> lastGenAxes_;
    juce::String lastGenPromptA_;
    juce::String lastGenPromptB_;
    double lastRegenTimeMs_ = 0.0; // for beat-based cooldown

    // ── Semantic self-listening loop state (message-thread only) ──
    // Mirrors clap_llm_loop.py run_llm_loop's per-pole *_glieder chain + anti-stasis
    // `recent`. The chain interprets its OWN last link (glieder[-1]), which under the
    // ab_add (concat) coupling is NOT what the editor holds (editor = original + ","
    // + last), so the last link and the human original are tracked here separately
    // from the editor text. All touched only on the message thread (pollDriftRegen,
    // the generation-complete callbacks, and runSemanticLoopStep's callAsync tail).
    bool loopStepInFlight_ = false;   // re-entrancy guard (like `generating`)
    bool loopEngaged_ = false;        // false→true edge = capture the human originals
    juce::String loopOriginalA_, loopOriginalB_;  // glieder[0] (the human impulse, kept by concat) — FULL, incl. musical suffix (for restore)
    juce::String loopLastA_, loopLastB_;          // glieder[-1] (the chain's own last link) — CORE only (no musical suffix)
    juce::StringArray loopRecentA_, loopRecentB_; // glieder[-3:] anti-stasis memory (last 3 links) — CORE only
    // User-appended pitch/tempo control tokens (c3, 120bpm), split off the originals at
    // engage and re-appended on every editor write so the loop's LLM rewrites never
    // touch them. Empty when the originals carried none. (RepromptStances::*MusicSuffix.)
    juce::String loopSuffixA_, loopSuffixB_;
    // Deactivation restore: when a stance→Off transition is seen (timerCallback edge
    // detect), the human originals captured at engage are put back into the editors.
    // loopOriginalsValid_ gates it: true only once a step has captured originals AND
    // machine-modified the editors; cleared after the restore (and never set if the
    // loop never ran, so toggling a stance on/off with no render in between is a no-op).
    bool loopOriginalsValid_ = false;
    int  prevStanceForRestore_ = 0;               // RepromptStance::Off — last seen stance index
    // transcribe + AB-replace anti-collapse: transcribe reads only the shared machine
    // tags, so writing BOTH poles each step makes A==B (collapses the blend → alpha/
    // alpha-drift has nothing to interpolate). Instead write ONE pole per step,
    // alternating, so A and B stay distinct transcriptions of consecutive renders.
    // false = this step writes B, true = writes A; flips after each applied step.
    bool loopAltWriteA_ = false;
    // Resynth-loop anti-convergence: an adaptive amount subtracted from the
    // loop's effective resynth when consecutive outputs stop differing, raising
    // init_noise to break the loop out of a fixed-point. 0 = no reduction (the
    // loop follows the user's setting). Message-thread only (pollDriftRegen +
    // the generation-complete callback both run there), so no atomic needed.
    float convergenceReduction_ = 0.0f;
    // Whether the round just triggered actually had its resynth lowered by the
    // controller (loopResynth < effResynth). Set in pollDriftRegen at trigger
    // time, read by the generation-complete callback to flag "+noise" — true only
    // when the reduction has effect, so near the floor (no headroom) it stays off.
    bool antiConvergenceActive_ = false;
    // Resynth-loop release edge-detector: the previous loop regen's "a parameter
    // moved" state. The release (detach init so a changed prompt renders clean) is
    // edge-triggered on the false→true transition — so a continuous drift, which
    // holds the change flag true every tick, releases ONCE at onset and then locks
    // and evolves, instead of detaching every tick and disabling the loop. Reset
    // when the loop is not running. Message-thread only (pollDriftRegen).
    bool prevLoopParamsChanged_ = false;
    // Failure throttle: when a drift-driven auto-regen fails, gate further
    // attempts for a couple of seconds so a persistently broken backend
    // (e.g. a model the bundled pipeline can't load) doesn't get hammered
    // by the 10 Hz timer and oscillate the status label between
    // "auto regen..." and the error message.
    double lastRegenFailureMs_ = 0.0;
    float alphaGhostValue_ = std::numeric_limits<float>::quiet_NaN();
    float magGhostValue_ = std::numeric_limits<float>::quiet_NaN();
    float noiseGhostValue_ = std::numeric_limits<float>::quiet_NaN();
    // Mode-specific ghosts: set when alpha-LFO offset is non-zero AND the
    // active mode targets the corresponding parameter. Painted via the same
    // drawGhost lambda in paintOverChildren.
    float lateMixGhostValue_    = std::numeric_limits<float>::quiet_NaN();
    float splitStartGhostValue_ = std::numeric_limits<float>::quiet_NaN();
    float splitEndGhostValue_   = std::numeric_limits<float>::quiet_NaN();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PromptPanel)
};
