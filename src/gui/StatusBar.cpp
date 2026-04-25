#include "StatusBar.h"
#include "GuiHelpers.h"

StatusBar::StatusBar()
{
    for (auto* btn : { &newBtn, &saveBtn, &saveAsBtn, &loadBtn, &exportBtn, &settingsBtn, &manualBtn })
    {
        btn->setColour(juce::TextButton::buttonColourId, kSurface);
        btn->setColour(juce::TextButton::textColourOffId, kDim);
        addAndMakeVisible(btn);
    }

    newBtn.onClick      = [this] { if (onNewPreset) onNewPreset(); };
    saveBtn.onClick     = [this] { if (onSavePreset) onSavePreset(); };
    saveAsBtn.onClick   = [this] { if (onSaveAsPreset) onSaveAsPreset(); };
    loadBtn.onClick     = [this] { if (onLoadPreset) onLoadPreset(); };
    exportBtn.onClick   = [this] { if (onExportWav) onExportWav(); };
    settingsBtn.onClick = [this] { if (onSettings) onSettings(); };
    manualBtn.onClick   = [this] { if (onManual) onManual(); };
}

void StatusBar::mouseDown(const juce::MouseEvent& e)
{
    // Right-click the preset name display to manage the loaded preset
    // (rename/delete/reveal) without going through the library browser.
    if (e.mods.isPopupMenu()
        && presetName.isNotEmpty()
        && presetNameBounds.contains(e.getPosition())
        && onPresetNameContextMenu)
    {
        onPresetNameContextMenu(e.getScreenPosition());
    }
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
    int rightEdge = newBtn.getX() - 8;

    // Status text (left side)
    g.setColour(juce::Colour(0xffe3e3e3));
    g.drawText(statusText, textX, 0, rightEdge - textX, getHeight(),
               juce::Justification::centredLeft);

    // Preset name (centered between status text and buttons). Bounds are
    // remembered so the right-click hit-test in mouseDown() lines up with
    // the painted text.
    if (presetName.isNotEmpty())
    {
        g.setColour(kAccent);
        const int nameW = juce::jmin(rightEdge - textX,
                                     juce::Font(juce::FontOptions(fs)).getStringWidth(presetName) + 24);
        const int nameX = (textX + rightEdge - nameW) / 2;
        presetNameBounds = juce::Rectangle<int>(nameX, 0, nameW, getHeight());
        g.drawText(presetName, presetNameBounds, juce::Justification::centred);
    }
    else
    {
        presetNameBounds = {};
    }
}

void StatusBar::resized()
{
    auto b = getLocalBounds();
    int btnH = b.getHeight() - 2;
    int y = 1;
    int gap = 4;

    // Right to left: Manual, Settings, Export, Browse, Save As, Save, Init
    int manualW   = 60;
    int settingsW = 60;
    int exportW   = 54;
    int browseW   = 58;
    int saveAsW   = 64;
    int saveW     = 50;
    int newW      = 40;

    manualBtn.setBounds(b.getRight() - manualW - gap, y, manualW, btnH);
    settingsBtn.setBounds(manualBtn.getX() - settingsW - gap, y, settingsW, btnH);
    exportBtn.setBounds(settingsBtn.getX() - exportW - gap, y, exportW, btnH);
    loadBtn.setBounds(exportBtn.getX() - browseW - gap, y, browseW, btnH);
    saveAsBtn.setBounds(loadBtn.getX() - saveAsW - gap, y, saveAsW, btnH);
    saveBtn.setBounds(saveAsBtn.getX() - saveW - gap, y, saveW, btnH);
    newBtn.setBounds(saveBtn.getX() - newW - gap, y, newW, btnH);
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
