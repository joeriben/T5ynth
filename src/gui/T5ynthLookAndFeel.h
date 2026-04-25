#pragma once
#include <JuceHeader.h>

/**
 * Custom LookAndFeel for T5ynth.
 *
 * Dark theme with #0a0a0a background, consistent with the project's
 * visual identity.
 */
class T5ynthLookAndFeel : public juce::LookAndFeel_V4
{
public:
    T5ynthLookAndFeel();
    ~T5ynthLookAndFeel() override = default;

    void drawButtonBackground(juce::Graphics& g, juce::Button& btn,
                              const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& btn,
                          bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;
    juce::Font getTextButtonFont(juce::TextButton& button, int buttonHeight) override;
    juce::Font getComboBoxFont(juce::ComboBox& box) override;
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;

    // Colour IDs
    static constexpr juce::uint32 backgroundColourId = 0x1000000;
    static constexpr juce::uint32 accentColourId     = 0x1000001;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(T5ynthLookAndFeel)
};
