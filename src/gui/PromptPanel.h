#pragma once
#include <JuceHeader.h>
#include <vector>
#include <utility>
#include <functional>
#include <map>
#include <limits>
#include "../inference/PipeInference.h"

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

    /** Paint ghost overlay for alpha slider (drift modulation indicator). */
    void paintOverChildren(juce::Graphics& g) override;

    bool isGenerating() const { return generating; }

private:
    void timerCallback() override;
    void triggerGeneration();
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
                                                  float splitEndOverride = std::numeric_limits<float>::quiet_NaN());

    /** Trigger generation from drift auto-regen. */
    void triggerDriftRegeneration(float effectiveAlpha,
                                  std::map<juce::String, float> effectiveAxes,
                                  float effectiveNoise,
                                  float effectiveMagnitude,
                                  float effectiveLateMix,
                                  float effectiveSplitStart,
                                  float effectiveSplitEnd,
                                  bool holdForBar = false);

    /** Check if drift requires auto-regeneration (called from timerCallback). */
    void pollDriftRegen();

    T5ynthProcessor& processorRef;

    // Prompts
    juce::Label promptALabel, promptBLabel;
    juce::TextEditor promptAEditor, promptBEditor;

    // Embedding controls (linear sliders)
    juce::Slider alphaSlider, magnitudeSlider, noiseSlider;
    juce::Label alphaLabel, alphaValue;
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

    // Model selector (fixed 3-slot switchbox: SA Open 1.0 | SA Small | AudioLDM2)
    static constexpr int kNumModelSlots = 3;
    juce::TextButton modelBtns[kNumModelSlots];
    juce::String modelSlotIds[kNumModelSlots];  // resolved model directory name per slot
    juce::Rectangle<int> modelSwitchBounds;
    bool modelsPopulated = false;
    juce::String pendingModel_;  // deferred model selection until models are populated
    void populateModelSelector();
    juce::String getSelectedModel() const;

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
    // along the 16 DiT block indices. Both thumbs at extremes → full B;
    // start == end → no B (pure A); narrow range → B injected only into
    // a sub-band of layers (mid / early / late depending on position).
    float            splitLayerStart_       = 4.0f;       // 0–16, low thumb
    float            splitLayerEnd_         = 16.0f;      // 0–16, high thumb (default = top: B from layer 4 onwards)

    /** Reconfigure alphaSlider (range, label, value, attachment) for the active mode. */
    void applyModeToSlider();

    bool generating = false;
    std::vector<std::pair<int, float>> pendingOffsets_;  // for DimensionExplorer
    std::map<juce::String, float> pendingAxes_;          // for SemanticAxes

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Attachment> alphaA, magA, noiseA, durA, stepsA, cfgA;

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
