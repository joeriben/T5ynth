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
    /** Fired when the user clicks the red "Panic" button — host should flush
     *  all hanging voices/notes. Momentary; no toggle state. */
    std::function<void()> onMidiPanic;
    /** Right-clicking the displayed preset name opens this menu — kept as
     *  a callback so MainPanel can populate it with rename/delete/etc. */
    std::function<void(juce::Point<int> screenPos)> onPresetNameContextMenu;

    // ── MIDI Output device selector ─────────────────────────────────────────
    /** Called when user selects a device. Empty string = no output. */
    std::function<void(const juce::String& deviceId)> onMidiOutputDeviceChanged;
    /** Called when user clicks "XL Map" — apply XL default CC bindings. */
    std::function<void()> onApplyXLDefaults;
    /** Sync the combo box to the currently-open device (call after processor restore). */
    void setMidiOutputDeviceId(const juce::String& deviceId);
    /** Refresh the device list (called on startup or when devices change). */
    void refreshMidiOutputDevices();

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
    juce::TextButton panicBtn { "Panic" };
    juce::TextButton keyboardBtn { "Kbd" };

    juce::ComboBox   midiOutCombo_;
    juce::TextButton xlMapBtn_ { "XL Map" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
};
