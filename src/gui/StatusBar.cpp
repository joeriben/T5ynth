#include "StatusBar.h"
#include "GuiHelpers.h"

StatusBar::StatusBar()
{
    saveBtn.setColour(juce::TextButton::buttonColourId, kSurface);
    saveBtn.setColour(juce::TextButton::textColourOffId, kDim);
    saveBtn.onClick = [this] { if (onSavePreset) onSavePreset(); };
    addAndMakeVisible(saveBtn);

    loadBtn.setColour(juce::TextButton::buttonColourId, kSurface);
    loadBtn.setColour(juce::TextButton::textColourOffId, kDim);
    loadBtn.onClick = [this] { if (onLoadPreset) onLoadPreset(); };
    addAndMakeVisible(loadBtn);
}

void StatusBar::paint(juce::Graphics& g)
{
    g.fillAll(kCard);

    float h = static_cast<float>(getHeight());
    float dotSize = juce::jmax(5.0f, h * 0.30f);
    float dotX = 8.0f;
    g.setColour(backendConnected ? juce::Colour(0xff4ade80) : juce::Colour(0xffef4444));
    g.fillEllipse(dotX, (h - dotSize) * 0.5f, dotSize, dotSize);

    float fs = juce::jlimit(10.0f, 14.0f, h * 0.55f);
    g.setColour(juce::Colour(0xffe3e3e3));
    g.setFont(juce::FontOptions(fs));
    int textX = juce::roundToInt(dotX + dotSize + 6.0f);

    // Status text (left side, up to save button)
    int textW = saveBtn.getX() - textX - 8;
    g.drawText(statusText, textX, 0, textW, getHeight(),
               juce::Justification::centredLeft);

    // Preset name (centered between status and buttons)
    if (presetName.isNotEmpty())
    {
        int presetX = textX + textW / 2;
        g.setColour(kAccent);
        g.drawText(presetName, presetX, 0, textW / 2, getHeight(),
                   juce::Justification::centredRight);
    }
}

void StatusBar::resized()
{
    auto b = getLocalBounds();
    int btnW = 50;
    int btnH = b.getHeight() - 2;
    int y = 1;

    loadBtn.setBounds(b.getRight() - btnW - 4, y, btnW, btnH);
    saveBtn.setBounds(loadBtn.getX() - btnW - 4, y, btnW, btnH);
}

void StatusBar::setStatusText(const juce::String& text)
{
    statusText = text;
    repaint();
}

void StatusBar::setConnected(bool connected)
{
    backendConnected = connected;
    repaint();
}

void StatusBar::setPresetName(const juce::String& name)
{
    presetName = name;
    repaint();
}
