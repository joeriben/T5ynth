#pragma once
#include <JuceHeader.h>
#include "PromptPanel.h"
#include "AxesPanel.h"
#include "DimensionExplorer.h"
#include "SynthPanel.h"
#include "FxPanel.h"
#include "SequencerPanel.h"
#include "StatusBar.h"

class T5ynthProcessor;

/**
 * 2-column layout:
 *   Col 1 (25%): GENERATION (PromptPanel + AxesPanel)
 *   Col 2 (75%): ENGINE + FILTER + MODULATION (SynthPanel)
 *   Footer: SequencerPanel + FxPanel (compact) + StatusBar
 *
 * DimensionExplorer opens as a modal overlay.
 */
class MainPanel : public juce::Component
{
public:
    explicit MainPanel(T5ynthProcessor& processor);
    ~MainPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    T5ynthProcessor& processorRef;

    // Col 1: GENERATION
    PromptPanel promptPanel;
    AxesPanel axesPanel;

    // Col 2: ENGINE + FILTER + MODULATION
    SynthPanel synthPanel;

    // Col 3: FX
    FxPanel fxPanel;

    // Bottom
    SequencerPanel sequencerPanel;
    StatusBar statusBar;

    // Master volume (footer, right edge)
    std::unique_ptr<SliderRow> masterVolRow;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterVolA;

    // Overlay: Dimension Explorer
    DimensionExplorer dimensionExplorer;
    juce::TextButton dimExplorerClose { "Close" };
    juce::TextButton dimExplorerReset { "Reset" };
    bool dimExplorerVisible = false;

    void showDimExplorer();
    void hideDimExplorer();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainPanel)
};
