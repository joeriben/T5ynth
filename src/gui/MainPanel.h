#pragma once
#include <JuceHeader.h>
#include "PromptPanel.h"
#include "AxesPanel.h"
#include "DimensionExplorer.h"
#include "SynthPanel.h"
#include "EffectsPanel.h"
#include "DriftPanel.h"
#include "SequencerPanel.h"
#include "StatusBar.h"

class T5ynthProcessor;

class MainPanel : public juce::Component
{
public:
    explicit MainPanel(T5ynthProcessor& processor);
    ~MainPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    T5ynthProcessor& processorRef;

    // Col 1: GENERATION (30%) — prompts, gen params, axes
    PromptPanel promptPanel;
    AxesPanel axesPanel;

    // Col 2: MODE + FILTER + EXPLORE (40%)
    SynthPanel synthPanel;
    DimensionExplorer dimensionExplorer;

    // Col 3: MODULATION + FX (30%)
    EffectsPanel modulationPanel;
    DriftPanel fxPanel;

    // Bottom
    SequencerPanel sequencerPanel;
    StatusBar statusBar;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainPanel)
};
