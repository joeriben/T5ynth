#include "DimensionExplorer.h"

DimensionExplorer::DimensionExplorer() = default;

void DimensionExplorer::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    float pad = b.getWidth() * 0.03f;
    auto area = b.reduced(pad);

    // Background
    g.setColour(juce::Colour(0xff0e0e0e));
    g.fillRoundedRectangle(area, 3.0f);

    // Grid crosshair at center
    g.setColour(juce::Colour(0xff1a1a1a));
    g.drawHorizontalLine(juce::roundToInt(area.getCentreY()), area.getX(), area.getRight());
    g.drawVerticalLine(juce::roundToInt(area.getCentreX()), area.getY(), area.getBottom());

    // Header
    float topH = (getTopLevelComponent() != nullptr) ? static_cast<float>(getTopLevelComponent()->getHeight()) : 800.0f;
    float fs = juce::jlimit(14.0f, 26.0f, topH * 0.030f);
    g.setFont(juce::FontOptions(fs));
    g.setColour(juce::Colour(0xff888888));
    g.drawText("EXPLORE", juce::roundToInt(pad + 4), juce::roundToInt(pad),
               getWidth(), juce::roundToInt(fs + 4), juce::Justification::centredLeft);

    if (points.empty()) return;

    // Draw points
    float dotR = juce::jlimit(3.0f, 8.0f, b.getWidth() * 0.02f);
    for (size_t i = 0; i < points.size(); ++i)
    {
        auto& pt = points[i];
        float px = area.getCentreX() + pt.x * area.getWidth() * 0.45f;
        float py = area.getCentreY() - pt.y * area.getHeight() * 0.45f;

        bool selected = (static_cast<int>(i) == selectedIndex);
        g.setColour(selected ? juce::Colour(0xff4a9eff) : juce::Colour(0xff3a3a3a));
        g.fillEllipse(px - dotR, py - dotR, dotR * 2.0f, dotR * 2.0f);
    }
}

void DimensionExplorer::resized() {}

void DimensionExplorer::addPoint(float x, float y, int /*seed*/)
{
    points.push_back({ x, y, 0 });
    selectedIndex = static_cast<int>(points.size()) - 1;
    repaint();
}

void DimensionExplorer::clear()
{
    points.clear();
    selectedIndex = -1;
    repaint();
}
