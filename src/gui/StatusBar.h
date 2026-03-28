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

    /** Called when user clicks the Settings button. */
    std::function<void()> onSettingsClicked;

private:
    juce::String statusText = "Ready";
    bool backendConnected = false;

    juce::TextButton settingsButton { "Settings" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
};
