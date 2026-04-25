#pragma once
#include <JuceHeader.h>
#include "gui/MainPanel.h"
#include "gui/T5ynthLookAndFeel.h"

class T5ynthProcessor;

class T5ynthEditor : public juce::AudioProcessorEditor
{
public:
    explicit T5ynthEditor(T5ynthProcessor& processor);
    ~T5ynthEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void parentHierarchyChanged() override;

private:
    void applyWindowIcon();

    T5ynthProcessor& processorRef;
    T5ynthLookAndFeel lookAndFeel;
    MainPanel mainPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(T5ynthEditor)
};
