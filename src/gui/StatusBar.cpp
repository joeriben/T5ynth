#include "StatusBar.h"

StatusBar::StatusBar()
{
}

void StatusBar::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1a));

    float h = static_cast<float>(getHeight());
    float dotSize = juce::jmax(6.0f, h * 0.35f);
    float dotX = h * 0.4f;
    g.setColour(backendConnected ? juce::Colour(0xff4ade80) : juce::Colour(0xffef4444));
    g.fillEllipse(dotX, (h - dotSize) * 0.5f, dotSize, dotSize);

    float topH = (getTopLevelComponent() != nullptr) ? static_cast<float>(getTopLevelComponent()->getHeight()) : 800.0f;
    float fs = juce::jlimit(14.0f, 26.0f, topH * 0.030f);
    g.setColour(juce::Colour(0xffe3e3e3));
    g.setFont(juce::FontOptions(fs));
    int textX = juce::roundToInt(dotX + dotSize + h * 0.3f);
    g.drawText(statusText, textX, 0, getWidth() - textX - 4, getHeight(),
               juce::Justification::centredLeft);
}

void StatusBar::resized()
{
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
