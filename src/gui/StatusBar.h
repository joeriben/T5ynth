#pragma once
#include <JuceHeader.h>
#include <functional>

class StatusBar : public juce::Component
{
public:
    StatusBar();
    ~StatusBar() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setStatusText(const juce::String& text);
    void setConnected(bool connected);

    /** Show loaded preset name (empty = no preset). */
    void setPresetName(const juce::String& name);

    /** Callbacks for preset buttons. */
    std::function<void()> onSavePreset;
    std::function<void()> onLoadPreset;

private:
    juce::String statusText = "Ready";
    juce::String presetName;
    bool backendConnected = false;

    juce::TextButton saveBtn { "Save" };
    juce::TextButton loadBtn { "Load" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
};
