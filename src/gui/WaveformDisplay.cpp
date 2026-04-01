#include "WaveformDisplay.h"
#include "GuiHelpers.h"

WaveformDisplay::WaveformDisplay()
{
    startTimerHz(30);
}

juce::Rectangle<float> WaveformDisplay::getWaveformArea() const
{
    return getLocalBounds().toFloat()
        .withTrimmedTop(LABEL_HEIGHT)
        .reduced(HANDLE_RADIUS, 2.0f);
}

float WaveformDisplay::fracToX(float frac) const
{
    auto area = getWaveformArea();
    return area.getX() + frac * area.getWidth();
}

float WaveformDisplay::xToFrac(float x) const
{
    auto area = getWaveformArea();
    return juce::jlimit(0.0f, 1.0f, (x - area.getX()) / area.getWidth());
}

void WaveformDisplay::setLoopStart(float frac)
{
    loopStart = juce::jlimit(0.0f, loopEnd - 0.01f, frac);
    repaint();
}

void WaveformDisplay::setLoopEnd(float frac)
{
    loopEnd = juce::jlimit(loopStart + 0.01f, 1.0f, frac);
    repaint();
}

void WaveformDisplay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff0a0a0a));

    auto area = getWaveformArea();

    // ── Label: "Loop interval  0.000s – 3.000s" ──
    {
        auto labelArea = getLocalBounds().toFloat().removeFromTop(LABEL_HEIGHT);
        g.setFont(juce::FontOptions(11.0f));
        g.setColour(juce::Colour(0xffaaaaaa));
        int labelW = juce::jmax(90, static_cast<int>(g.getCurrentFont().getStringWidthFloat(regionLabel) + 10));
        g.drawText(regionLabel, labelArea.removeFromLeft(static_cast<float>(labelW)), juce::Justification::centredLeft);

        float tStart = loopStart * bufferDurationSec;
        float tEnd   = loopEnd * bufferDurationSec;
        juce::String timeStr = juce::String(tStart, 3) + "s \u2013 " + juce::String(tEnd, 3) + "s";
        g.setColour(kAccent);
        g.drawText(timeStr, labelArea, juce::Justification::centredRight);
    }

    const juce::ScopedLock lock(dataLock);

    if (waveformData.empty())
        return;

    const float midY = area.getCentreY();
    const float halfH = area.getHeight() * 0.5f;
    const int numPoints = static_cast<int>(waveformData.size());

    // ── Dim region before loopStart ──
    float xStart = fracToX(loopStart);
    float xEnd   = fracToX(loopEnd);

    if (xStart > area.getX())
    {
        g.setColour(juce::Colour(0x60000000));
        g.fillRect(area.getX(), area.getY(), xStart - area.getX(), area.getHeight());
    }
    if (xEnd < area.getRight())
    {
        g.setColour(juce::Colour(0x60000000));
        g.fillRect(xEnd, area.getY(), area.getRight() - xEnd, area.getHeight());
    }

    // ── Waveform path ──
    juce::Path path;
    for (int i = 0; i < numPoints; ++i)
    {
        float x = area.getX() + (static_cast<float>(i) / static_cast<float>(numPoints - 1)) * area.getWidth();
        float y = midY - waveformData[static_cast<size_t>(i)] * halfH;
        if (i == 0)
            path.startNewSubPath(x, y);
        else
            path.lineTo(x, y);
    }

    g.setColour(kAccent);
    g.strokePath(path, juce::PathStrokeType(1.5f));

    // ── Loop region line (green horizontal bar between handles) ──
    float lineY = area.getBottom() - 2.0f;
    g.setColour(kAccent);
    g.drawLine(xStart, lineY, xEnd, lineY, 2.0f);

    // ── Bracket handles (green circles) ──
    auto drawHandle = [&](float x) {
        g.setColour(kAccent);
        g.fillEllipse(x - HANDLE_RADIUS, lineY - HANDLE_RADIUS,
                      HANDLE_RADIUS * 2.0f, HANDLE_RADIUS * 2.0f);
        g.setColour(juce::Colour(0xff0a0a0a));
        g.fillEllipse(x - HANDLE_RADIUS + 2.0f, lineY - HANDLE_RADIUS + 2.0f,
                      HANDLE_RADIUS * 2.0f - 4.0f, HANDLE_RADIUS * 2.0f - 4.0f);
        g.setColour(kAccent);
        g.fillEllipse(x - 2.5f, lineY - 2.5f, 5.0f, 5.0f);
    };
    drawHandle(xStart);
    drawHandle(xEnd);
}

void WaveformDisplay::resized() {}

void WaveformDisplay::mouseDown(const juce::MouseEvent& e)
{
    float mx = static_cast<float>(e.getPosition().getX());
    float xS = fracToX(loopStart);
    float xE = fracToX(loopEnd);

    // Hit test: which handle is closer?
    float distS = std::abs(mx - xS);
    float distE = std::abs(mx - xE);

    if (distS <= HANDLE_RADIUS * 2.0f && distS <= distE)
        dragging = Start;
    else if (distE <= HANDLE_RADIUS * 2.0f)
        dragging = End;
    else
        dragging = None;
}

void WaveformDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging == None) return;

    float frac = xToFrac(static_cast<float>(e.getPosition().getX()));

    if (dragging == Start)
        loopStart = juce::jlimit(0.0f, loopEnd - 0.01f, frac);
    else
        loopEnd = juce::jlimit(loopStart + 0.01f, 1.0f, frac);

    if (onLoopRegionChanged)
        onLoopRegionChanged(loopStart, loopEnd);

    repaint();
}

void WaveformDisplay::mouseUp(const juce::MouseEvent&)
{
    dragging = None;
}

void WaveformDisplay::setWaveform(const float* data, int numSamples)
{
    const juce::ScopedLock lock(dataLock);
    waveformData.assign(data, data + numSamples);
}

void WaveformDisplay::timerCallback()
{
    repaint();
}
