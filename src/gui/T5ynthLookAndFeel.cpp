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

void T5ynthLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                         int x, int y, int width, int height,
                                         float sliderPosProportional,
                                         float rotaryStartAngle,
                                         float rotaryEndAngle,
                                         juce::Slider& slider)
{
    auto bounds = juce::Rectangle<float>(static_cast<float>(x),
                                         static_cast<float>(y),
                                         static_cast<float>(width),
                                         static_cast<float>(height)).reduced(2.0f);
    const float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
    auto knob = juce::Rectangle<float>(diameter, diameter).withCentre(bounds.getCentre());
    const float radius = diameter * 0.5f;
    const float arcRadius = radius * 0.82f;
    const float stroke = juce::jlimit(2.0f, 4.5f, radius * 0.11f);
    const float displayStartAngle = juce::MathConstants<float>::pi * 1.2f;
    const float displayEndAngle = juce::MathConstants<float>::pi * 2.8f;
    const float displayAngle = displayStartAngle + sliderPosProportional * (displayEndAngle - displayStartAngle);
    const float indicatorAngle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    auto accent = slider.findColour(juce::Slider::trackColourId);
    auto bg = slider.findColour(juce::Slider::backgroundColourId);
    auto thumb = slider.findColour(juce::Slider::thumbColourId);

    g.setColour(kSurface.darker(0.36f));
    g.fillEllipse(knob);
    g.setColour(kBorder.withAlpha(0.72f));
    g.drawEllipse(knob.reduced(stroke * 0.35f), 1.0f);

    juce::Path baseArc;
    baseArc.addCentredArc(knob.getCentreX(), knob.getCentreY(),
                          arcRadius, arcRadius, 0.0f,
                          displayStartAngle, displayEndAngle, true);
    g.setColour(bg.withAlpha(0.95f));
    g.strokePath(baseArc, juce::PathStrokeType(stroke,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    juce::Path valueArc;
    valueArc.addCentredArc(knob.getCentreX(), knob.getCentreY(),
                           arcRadius, arcRadius, 0.0f,
                           displayStartAngle, displayAngle, true);
    g.setColour(accent);
    g.strokePath(valueArc, juce::PathStrokeType(stroke,
                                                juce::PathStrokeType::curved,
                                                juce::PathStrokeType::rounded));

    const float pointerLen = radius * 0.45f;
    const float pointerStart = radius * 0.12f;
    juce::Point<float> centre = knob.getCentre();
    auto p1 = centre + juce::Point<float>(std::cos(indicatorAngle), std::sin(indicatorAngle)) * pointerStart;
    auto p2 = centre + juce::Point<float>(std::cos(indicatorAngle), std::sin(indicatorAngle)) * pointerLen;

    g.setColour(thumb);
    g.drawLine(juce::Line<float>(p1, p2), juce::jmax(2.0f, stroke * 0.72f));
}

juce::Font T5ynthLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    const float size = juce::jlimit(kUiControlFontMin, 13.5f, static_cast<float>(buttonHeight) * 0.58f);
    return juce::Font(juce::FontOptions(size));
}

void T5ynthLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                     int buttonX, int buttonY, int buttonW, int buttonH,
                                     juce::ComboBox& box)
{
    juce::ignoreUnused(buttonX, buttonY, buttonW, buttonH);

    auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat();
    auto bg = box.findColour(juce::ComboBox::backgroundColourId);

    if (isButtonDown)
        bg = bg.brighter(0.06f);

    g.setColour(bg);
    g.fillRect(bounds);

    auto outline = box.findColour(juce::ComboBox::outlineColourId);
    g.setColour(outline);
    g.drawRect(bounds, 1.0f);

    if (box.hasKeyboardFocus(false))
    {
        g.setColour(outline.brighter(0.28f));
        g.drawRect(bounds.reduced(1.0f), 1.0f);
    }

    const float side = juce::jlimit(4.0f, 5.5f, static_cast<float>(height) * 0.22f);
    const float x = bounds.getRight() - side - 3.0f;
    const float y = bounds.getCentreY() - side * 0.5f;
    auto markerColour = box.findColour(juce::ComboBox::textColourId)
                            .withAlpha(box.isEnabled() ? 0.72f : 0.28f);

    g.setColour(markerColour);
    g.drawRect(juce::Rectangle<float>(x, y, side, side), 1.15f);

    const float dot = juce::jmax(1.25f, side * 0.26f);
    g.fillEllipse(x + side * 0.5f - dot * 0.5f,
                  y + side * 0.5f - dot * 0.5f,
                  dot, dot);
}

juce::Font T5ynthLookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    return juce::Font(juce::FontOptions(juce::jmax(kUiControlFontMin, static_cast<float>(box.getHeight()) * 0.58f)));
}

void T5ynthLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    constexpr int leftInset = 6;
    const int rightInset = juce::jlimit(10, 13, juce::roundToInt(static_cast<float>(box.getHeight()) * 0.50f));
    label.setBounds(leftInset, 1, juce::jmax(0, box.getWidth() - leftInset - rightInset), box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
    label.setColour(juce::Label::textColourId, box.findColour(juce::ComboBox::textColourId));
    label.setJustificationType(box.getJustificationType());
}
