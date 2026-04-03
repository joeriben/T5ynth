#pragma once
#include <JuceHeader.h>
#include <vector>
#include <utility>
#include <functional>

class T5ynthProcessor;

/**
 * GENERATION column: prompts, embedding controls, compact params, generate.
 */
class PromptPanel : public juce::Component, private juce::Timer
{
public:
    explicit PromptPanel(T5ynthProcessor& processor);
    ~PromptPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Load preset data that isn't in APVTS (prompts, seed, random toggle, device). */
    void loadPresetData(const juce::String& promptA, const juce::String& promptB,
                        int seed, bool randomSeed,
                        const juce::String& device = {});

    /** Read current prompt/seed state (for preset save). */
    juce::String getPromptA() const { return promptAEditor.getText().trim(); }
    juce::String getPromptB() const { return promptBEditor.getText().trim(); }
    int getSeed() const { return seedEditor.getText().getIntValue(); }

    /** Trigger generation with optional dimension offsets from DimensionExplorer. */
    void triggerGenerationWithOffsets(std::vector<std::pair<int, float>> offsets);

    /** Set semantic axis values to include in the next generation request. */
    void setSemanticAxes(std::map<juce::String, float> axes) { pendingAxes_ = std::move(axes); }

    /** Called after generation with embedding stats (for DimensionExplorer). */
    std::function<void(const std::vector<float>&, const std::vector<float>&)> onEmbeddingsReady;

    /** Status callback — called with status text (e.g. "generating...", "12.3s | seed 42 | mps") */
    std::function<void(const juce::String&, bool generating)> onStatusChanged;

private:
    void timerCallback() override;
    void triggerGeneration();
    float fs() const;

    T5ynthProcessor& processorRef;

    // Prompts
    juce::Label promptALabel, promptBLabel;
    juce::TextEditor promptAEditor, promptBEditor;

    // Embedding controls (linear sliders)
    juce::Slider alphaSlider, magnitudeSlider, noiseSlider;
    juce::Label alphaLabel, alphaValue, alphaHint;
    juce::Label magLabel, magValue, magHint;
    juce::Label noiseLabel, noiseValue, noiseHint;

    // Compact params row: Duration, Start, Steps, CFG, Seed
    juce::Slider durationSlider, startSlider;
    juce::Slider stepsSlider, cfgSlider;
    juce::Label durLabel, durValue, durHint;
    juce::Label startLabel, startValue, startHint;
    juce::Label stepsLabel, stepsValue, stepsHint;
    juce::Label cfgLabel, cfgValue, cfgHint;
    juce::Label seedLabel;
    juce::TextEditor seedEditor;
    juce::TextButton randomSeedToggle { "Random" };

    // Model selector (fixed 3-slot switchbox: SA Open 1.0 | SA Small | AudioLDM2)
    static constexpr int kNumModelSlots = 3;
    juce::TextButton modelBtns[kNumModelSlots];
    juce::String modelSlotIds[kNumModelSlots];  // resolved model directory name per slot
    juce::Rectangle<int> modelSwitchBounds;
    bool modelsPopulated = false;
    void populateModelSelector();
    juce::String getSelectedModel() const;

    // Device selector (GPU / CPU toggle)
    juce::TextButton gpuBtn { "GPU" }, cpuBtn { "CPU" };
    juce::String gpuBackend_;  // "mps" or "cuda" — resolved at runtime
    bool devicesPopulated = false;
    void populateDeviceButtons();

    juce::TextButton generateButton { "Generate" };
    juce::Label infoLabel;

    bool generating = false;
    std::vector<std::pair<int, float>> pendingOffsets_;  // for DimensionExplorer
    std::map<juce::String, float> pendingAxes_;          // for SemanticAxes

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Attachment> alphaA, magA, noiseA, durA, startA, stepsA, cfgA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PromptPanel)
};
