#include "T5ynthLookAndFeel.h"
#include "GuiHelpers.h"

T5ynthLookAndFeel::T5ynthLookAndFeel()
{
    // Derive from shared constants
    const auto background   = kBg;
    const auto surface      = kSurface;
    const auto surfaceLight = kBorder;
    const auto textPrimary  = kTextPrimary;
    const auto textDim      = kTextSecondary;
    const auto accent       = kAccent;

    // Window / general
    setColour(juce::ResizableWindow::backgroundColourId, background);
    setColour(juce::DocumentWindow::backgroundColourId, background);

    // Labels
    setColour(juce::Label::textColourId, textPrimary);
    setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);

    // Text editor
    setColour(juce::TextEditor::backgroundColourId, surface);
    setColour(juce::TextEditor::textColourId, textPrimary);
    setColour(juce::TextEditor::outlineColourId, surfaceLight);
    setColour(juce::TextEditor::focusedOutlineColourId, accent);

    // Sliders
    setColour(juce::Slider::backgroundColourId, surface);
    setColour(juce::Slider::trackColourId, accent);
    setColour(juce::Slider::thumbColourId, textPrimary);
    setColour(juce::Slider::textBoxTextColourId, textPrimary);
    setColour(juce::Slider::textBoxBackgroundColourId, surface);
    setColour(juce::Slider::textBoxOutlineColourId, surfaceLight);

    // Buttons
    setColour(juce::TextButton::buttonColourId, surface);
    setColour(juce::TextButton::buttonOnColourId, accent);
    setColour(juce::TextButton::textColourOffId, textPrimary);
    setColour(juce::TextButton::textColourOnId, textPrimary);

    // ComboBox
    setColour(juce::ComboBox::backgroundColourId, surface);
    setColour(juce::ComboBox::textColourId, textPrimary);
    setColour(juce::ComboBox::outlineColourId, surfaceLight);

    // PopupMenu
    setColour(juce::PopupMenu::backgroundColourId, surface);
    setColour(juce::PopupMenu::textColourId, textPrimary);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, accent);
    setColour(juce::PopupMenu::highlightedTextColourId, textPrimary);

    // Scrollbar
    setColour(juce::ScrollBar::thumbColourId, surfaceLight);

    // Tooltip
    setColour(juce::TooltipWindow::backgroundColourId, surface);
    setColour(juce::TooltipWindow::textColourId, textDim);

    // Default font
    setDefaultSansSerifTypefaceName("Inter");
}

void T5ynthLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& btn,
                                              const juce::Colour& backgroundColour,
                                              bool highlighted, bool /*down*/)
{
    auto bounds = btn.getLocalBounds().toFloat();
    auto baseColour = backgroundColour;

    if (btn.getToggleState())
        baseColour = btn.findColour(juce::TextButton::buttonOnColourId);

    if (highlighted)
        baseColour = baseColour.brighter(0.05f);

    g.setColour(baseColour);
    g.fillRect(bounds);
}

void T5ynthLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& btn,
                                          bool /*highlighted*/, bool /*down*/)
{
    auto b = btn.getLocalBounds().toFloat();
    bool on = btn.getToggleState();
    float h = b.getHeight();

    // Glow dot (left side)
    float dotR = juce::jmin(5.0f, h * 0.25f);
    float dotX = b.getX() + dotR + 2.0f;
    float dotY = b.getCentreY();

    if (on)
    {
        // Outer glow
        auto glowCol = btn.findColour(juce::ToggleButton::tickColourId).withAlpha(0.25f);
        g.setColour(glowCol);
        g.fillEllipse(dotX - dotR * 2.0f, dotY - dotR * 2.0f, dotR * 4.0f, dotR * 4.0f);
        // Bright core
        g.setColour(btn.findColour(juce::ToggleButton::tickColourId));
        g.fillEllipse(dotX - dotR, dotY - dotR, dotR * 2.0f, dotR * 2.0f);
    }
    else
    {
        g.setColour(kDimmer);
        g.fillEllipse(dotX - dotR, dotY - dotR, dotR * 2.0f, dotR * 2.0f);
    }

    // Label text
    float textX = dotX + dotR + 4.0f;
    g.setColour(btn.findColour(juce::ToggleButton::textColourId));
    g.setFont(juce::FontOptions(juce::jmax(kUiControlFontMin, h * 0.65f)));
    g.drawText(btn.getButtonText(),
               juce::Rectangle<float>(textX, b.getY(), b.getRight() - textX, h),
               juce::Justification::centredLeft);
}

juce::Font T5ynthLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    const float size = juce::jlimit(kUiControlFontMin, 13.5f, static_cast<float>(buttonHeight) * 0.58f);
    return juce::Font(juce::FontOptions(size));
}

juce::Font T5ynthLookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    return juce::Font(juce::FontOptions(juce::jmax(kUiControlFontMin, static_cast<float>(box.getHeight()) * 0.58f)));
}

void T5ynthLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(8, 1, box.getWidth() - 26, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
    label.setColour(juce::Label::textColourId, box.findColour(juce::ComboBox::textColourId));
    label.setJustificationType(box.getJustificationType());
}
