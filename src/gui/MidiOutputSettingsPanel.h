#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"
#include "../ProductName.h"

// ─────────────────────────────────────────────────────────────────────────────
// MIDI output + external-clock controls for the standalone "MIDI/Audio Settings"
// dialog. Hooked into JUCE's StandalonePluginHolder via its `extraSettingsPanel`
// member (shown below the audio-device selector). These are setup controls — the
// device is chosen once and remembered — so they belong in the settings dialog,
// not the bottom operation row.
//
// Talks to the processor directly. Selecting a Launch Control XL output applies
// its CC mapping automatically (processor side, openMidiOutputDevice) — no manual
// "XL Map" step. The dialog uses JUCE's default look-and-feel, so this panel does
// NOT force T5ynth's dark palette (it would be unreadable on the light dialog).
// ─────────────────────────────────────────────────────────────────────────────
class MidiOutputSettingsPanel : public juce::Component
{
public:
    explicit MidiOutputSettingsPanel (T5ynthProcessor& p) : proc (p)
    {
        titleLabel.setText (productName() + " MIDI", juce::dontSendNotification);
        titleLabel.setFont (juce::Font (juce::FontOptions (15.0f, juce::Font::bold)));
        addAndMakeVisible (titleLabel);

        outLabel.setText ("Output (LED / MIDI out)", juce::dontSendNotification);
        outLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (outLabel);

        outCombo.setTooltip ("MIDI output device for Launch Control XL LED feedback and MIDI out. "
                             "Selecting a Launch Control XL applies its mapping automatically.");
        outCombo.onChange = [this]
        {
            const int id = outCombo.getSelectedId();
            if (id <= 1)
            {
                proc.openMidiOutputDevice ({});
            }
            else
            {
                const auto devices = juce::MidiOutput::getAvailableDevices();
                const int devIdx = id - 2;
                if (devIdx >= 0 && devIdx < devices.size())
                    proc.openMidiOutputDevice (devices[devIdx].identifier);
            }
        };
        addAndMakeVisible (outCombo);

        clockToggle.setButtonText ("External MIDI Clock (sync BPM to incoming clock)");
        clockToggle.setTooltip ("Sync " + productName() + " BPM to incoming MIDI clock — overrides host transport + Seq BPM.");
        clockToggle.onClick = [this] { proc.setMidiClockEnabled (clockToggle.getToggleState()); };
        addAndMakeVisible (clockToggle);

        refresh();
        // Height is honoured by the dialog (width is overridden to the dialog width).
        // reduced(10,8)=16 + title20 + gap4 + row26 + gap6 + toggle26 = 98 needed; 104
        // leaves a little bottom breathing room.
        setSize (500, 104);
    }

    // The panel persists (owned by the editor) and is re-parented into a freshly
    // built settings dialog each time it opens. parentHierarchyChanged() fires on
    // every re-add (addAndMakeVisible → addChildComponent), so refresh here to pick
    // up devices plugged in since the panel was first built. visibilityChanged()
    // would NOT do: the panel keeps visible=true between opens, so the no-op
    // setVisible(true) on re-add wouldn't fire it.
    void parentHierarchyChanged() override
    {
        if (getParentComponent() != nullptr)
            refresh();
    }

    void resized() override
    {
        auto b = getLocalBounds().reduced (10, 8);
        titleLabel.setBounds (b.removeFromTop (20));
        b.removeFromTop (4);

        auto outRow = b.removeFromTop (26);
        outLabel.setBounds (outRow.removeFromLeft (160));
        outCombo.setBounds (outRow);
        b.removeFromTop (6);

        clockToggle.setBounds (b.removeFromTop (26));
    }

private:
    void refresh()
    {
        outCombo.clear (juce::dontSendNotification);
        outCombo.addItem ("No MIDI output", 1);

        const auto devices = juce::MidiOutput::getAvailableDevices();
        const auto currentId = proc.getMidiOutputDeviceId();
        int selectedId = 1;
        for (int i = 0; i < devices.size(); ++i)
        {
            outCombo.addItem (devices[i].name, i + 2);
            if (devices[i].identifier == currentId)
                selectedId = i + 2;
        }
        outCombo.setSelectedId (selectedId, juce::dontSendNotification);

        clockToggle.setToggleState (proc.isMidiClockEnabled(), juce::dontSendNotification);
    }

    T5ynthProcessor&   proc;
    juce::Label        titleLabel, outLabel;
    juce::ComboBox     outCombo;
    juce::ToggleButton clockToggle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiOutputSettingsPanel)
};
