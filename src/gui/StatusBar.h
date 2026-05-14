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
    std::function<void()> onNewPreset;
    std::function<void()> onSavePreset;
    std::function<void()> onLoadPreset;
    std::function<void()> onExportWav;
    std::function<void()> onSettings;
    std::function<void()> onManual;
    std::function<void(bool)> onKeyboardInputChanged;
    void setKeyboardInputEnabled(bool enabled);
    /** Right-clicking the displayed preset name opens this menu — kept as
     *  a callback so MainPanel can populate it with rename/delete/etc. */
    std::function<void(juce::Point<int> screenPos)> onPresetNameContextMenu;

    void mouseDown(const juce::MouseEvent& e) override;

private:
    juce::String statusText = "Ready";
    juce::String presetName;
    bool backendConnected = false;
    juce::Rectangle<int> presetNameBounds;     // updated during paint() for hit-test

    juce::TextButton newBtn    { "Init" };
    juce::TextButton saveBtn   { "Save" };
    juce::TextButton loadBtn   { "Library" };
    juce::TextButton exportBtn { "Export" };
    juce::TextButton settingsBtn { "Settings" };
    juce::TextButton manualBtn { "Manual" };
    juce::TextButton keyboardBtn { "Kbd" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
};
