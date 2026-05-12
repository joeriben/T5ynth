#include "StatusBar.h"
#include "GuiHelpers.h"

namespace
{
juce::String fitTextToWidth(const juce::Font& font, juce::String text, int maxWidth)
{
    text = text.trim();
    if (text.isEmpty() || maxWidth <= 0)
        return {};

    if (font.getStringWidth(text) <= maxWidth)
        return text;

    constexpr const char* suffix = "...";
    const int suffixW = font.getStringWidth(suffix);
    if (suffixW >= maxWidth)
        return {};

    int lo = 0;
    int hi = text.length();
    int best = 0;
    while (lo <= hi)
    {
        const int mid = (lo + hi) / 2;
        auto candidate = text.substring(0, mid).trimEnd();
        if (font.getStringWidth(candidate) + suffixW <= maxWidth)
        {
            best = mid;
            lo = mid + 1;
        }
        else
        {
            hi = mid - 1;
        }
    }

    return text.substring(0, best).trimEnd() + suffix;
}
}

StatusBar::StatusBar()
{
    for (auto* btn : { &newBtn, &saveBtn, &loadBtn, &exportBtn, &settingsBtn, &manualBtn })
    {
        btn->setColour(juce::TextButton::buttonColourId, kSurface);
        btn->setColour(juce::TextButton::textColourOffId, kDim);
        addAndMakeVisible(btn);
    }

    newBtn.onClick      = [this] { if (onNewPreset) onNewPreset(); };
    saveBtn.onClick     = [this] { if (onSavePreset) onSavePreset(); };
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
    juce::Font font { juce::FontOptions(fs) };
    g.setFont(font);

    int textX = juce::roundToInt(dotX + dotSize + 6.0f);
    int rightEdge = newBtn.getX() - 8;
    auto statusBounds = juce::Rectangle<int>(textX, 0,
                                             juce::jmax(0, rightEdge - textX),
                                             getHeight());

    // Status text (left side)
    g.setColour(juce::Colour(0xffe3e3e3));
    {
        juce::Graphics::ScopedSaveState clip(g);
        g.reduceClipRegion(statusBounds);
        g.drawText(fitTextToWidth(font, statusText, statusBounds.getWidth()),
                   statusBounds,
                   juce::Justification::centredLeft,
                   false);
    }

    // Preset name (centered between status text and buttons). Bounds are
    // remembered so the right-click hit-test in mouseDown() lines up with
    // the painted text.
    if (presetName.isNotEmpty())
    {
        g.setColour(kAccent);
        const int nameW = juce::jmin(rightEdge - textX,
                                     font.getStringWidth(presetName) + 24);
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

    // Right to left: Manual, Settings, Export, Library, Save, Init
    int manualW   = 60;
    int settingsW = 60;
    int exportW   = 54;
    int libraryW  = 64;
    int saveW     = 50;
    int newW      = 40;

    manualBtn.setBounds(b.getRight() - manualW - gap, y, manualW, btnH);
    settingsBtn.setBounds(manualBtn.getX() - settingsW - gap, y, settingsW, btnH);
    exportBtn.setBounds(settingsBtn.getX() - exportW - gap, y, exportW, btnH);
    loadBtn.setBounds(exportBtn.getX() - libraryW - gap, y, libraryW, btnH);
    saveBtn.setBounds(loadBtn.getX() - saveW - gap, y, saveW, btnH);
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
