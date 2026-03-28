#pragma once
#include <JuceHeader.h>

class T5ynthProcessor;

/**
 * Preset import/export panel — reads/writes JSON presets compatible
 * with the Vue reference (crossmodal_lab.vue SynthPreset format).
 */
class PresetPanel : public juce::Component
{
public:
    explicit PresetPanel(T5ynthProcessor& proc);
    ~PresetPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Called after import so parent can update GUI-only state (prompts, seed). */
    std::function<void(const juce::String& promptA, const juce::String& promptB,
                       int seed, bool randomSeed)> onPresetLoaded;

private:
    T5ynthProcessor& processor;

    juce::TextButton importButton { "Import Preset" };
    juce::TextButton exportButton { "Export Preset" };
    juce::Label statusLabel;

    void importPreset();
    void exportPreset();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetPanel)
};
