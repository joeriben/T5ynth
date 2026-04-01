#pragma once
#include <JuceHeader.h>

class StatusBar : public juce::Component
{
public:
    StatusBar();
    ~StatusBar() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setStatusText(const juce::String& text);
    void setConnected(bool connected);

private:
    juce::String statusText = "Ready";
    bool backendConnected = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
};
