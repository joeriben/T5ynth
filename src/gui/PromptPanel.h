#pragma once
#include <JuceHeader.h>

class T5ynthProcessor;

/**
 * GENERATION column: prompts, embedding controls, compact params, generate.
 */
class PromptPanel : public juce::Component
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

private:
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
    juce::ToggleButton randomSeedToggle { "Random" };

    // Device selector
    juce::Label deviceLabel;
    juce::ComboBox deviceBox;

    juce::TextButton generateButton { "Generate" };
    juce::Label infoLabel;

    bool generating = false;

    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<Attachment> alphaA, magA, noiseA, durA, startA, stepsA, cfgA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PromptPanel)
};
