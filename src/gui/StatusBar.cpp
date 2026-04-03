#include "StatusBar.h"
#include "GuiHelpers.h"

StatusBar::StatusBar()
{
    for (auto* btn : { &saveBtn, &loadBtn, &exportBtn, &settingsBtn, &aboutBtn })
    {
        btn->setColour(juce::TextButton::buttonColourId, kSurface);
        btn->setColour(juce::TextButton::textColourOffId, kDim);
        addAndMakeVisible(btn);
    }

    saveBtn.onClick     = [this] { if (onSavePreset) onSavePreset(); };
    loadBtn.onClick     = [this] { if (onLoadPreset) onLoadPreset(); };
    exportBtn.onClick   = [this] { if (onExportWav) onExportWav(); };
    settingsBtn.onClick = [this] { if (onSettings) onSettings(); };
    aboutBtn.onClick    = [this] { if (onAbout) onAbout(); };
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
    g.setFont(juce::FontOptions(fs));

    int textX = juce::roundToInt(dotX + dotSize + 6.0f);
    int rightEdge = saveBtn.getX() - 8;

    // Status text (left side)
    g.setColour(juce::Colour(0xffe3e3e3));
    g.drawText(statusText, textX, 0, rightEdge - textX, getHeight(),
               juce::Justification::centredLeft);

    // Preset name (centered between status text and buttons)
    if (presetName.isNotEmpty())
    {
        g.setColour(kAccent);
        g.drawText(presetName, textX, 0, rightEdge - textX, getHeight(),
                   juce::Justification::centred);
    }

}

void StatusBar::resized()
{
    auto b = getLocalBounds();
    int btnW = 50;
    int btnH = b.getHeight() - 2;
    int y = 1;
    int gap = 4;

    // Right to left: About, Settings, Export, Load, Save
    int aboutW = 50;
    int settingsW = 60;
    int exportW = 54;

    aboutBtn.setBounds(b.getRight() - aboutW - gap, y, aboutW, btnH);
    settingsBtn.setBounds(aboutBtn.getX() - settingsW - gap, y, settingsW, btnH);
    exportBtn.setBounds(settingsBtn.getX() - exportW - gap, y, exportW, btnH);
    loadBtn.setBounds(exportBtn.getX() - btnW - gap, y, btnW, btnH);
    saveBtn.setBounds(loadBtn.getX() - btnW - gap, y, btnW, btnH);
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
