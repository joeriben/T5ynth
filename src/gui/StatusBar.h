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

    /** Callbacks for buttons. */
    std::function<void()> onSavePreset;
    std::function<void()> onLoadPreset;
    std::function<void()> onExportWav;
    std::function<void()> onSettings;
    std::function<void()> onAbout;

private:
    juce::String statusText = "Ready";
    juce::String presetName;
    bool backendConnected = false;

    juce::TextButton saveBtn { "Save" };
    juce::TextButton loadBtn { "Load" };
    juce::TextButton exportBtn { "Export" };
    juce::TextButton settingsBtn { "Settings" };
    juce::TextButton aboutBtn { "About" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
};
