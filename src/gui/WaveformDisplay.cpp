#include "WaveformDisplay.h"
#include "GuiHelpers.h"

WaveformDisplay::WaveformDisplay()
{
    startTimerHz(5);
}

juce::Rectangle<float> WaveformDisplay::getWaveformArea() const
{
    return getLocalBounds().toFloat()
        .withTrimmedTop(LABEL_HEIGHT)
        .withTrimmedBottom(static_cast<float>(bottomReserve))
        .reduced(HANDLE_RADIUS, HANDLE_RADIUS);
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
        g.setColour(juce::Colour(0xbb000000));
        g.fillRect(area.getX(), area.getY(), xStart - area.getX(), area.getHeight());
    }
    if (xEnd < area.getRight())
    {
        g.setColour(juce::Colour(0xbb000000));
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

    // ── Bracket/scan line — below the waveform when bottomReserve > 0 ──
    float lineY = (bottomReserve > 0)
        ? area.getBottom() + HANDLE_RADIUS + static_cast<float>(bottomReserve) * 0.5f
        : area.getBottom() - 2.0f;

    // Horizontal line spanning full waveform width
    g.setColour(kAccent);
    g.drawLine(area.getX(), lineY, area.getRight(), lineY, 2.0f);

    // ── Bracket handles (circles on the line) ──
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

    // ── Scan position indicator (filled dot on the same line) ──
    if (scanVisible)
    {
        float scanX = area.getX() + scanPos * area.getWidth();
        g.setColour(kAccent.brighter(0.3f));
        g.fillEllipse(scanX - HANDLE_RADIUS * 0.6f, lineY - HANDLE_RADIUS * 0.6f,
                      HANDLE_RADIUS * 1.2f, HANDLE_RADIUS * 1.2f);
    }
}

void WaveformDisplay::resized() {}

void WaveformDisplay::mouseDown(const juce::MouseEvent& e)
{
    float mx = static_cast<float>(e.getPosition().getX());
    float xS = fracToX(loopStart);
    float xE = fracToX(loopEnd);

    float distS = std::abs(mx - xS);
    float distE = std::abs(mx - xE);

    // Scan hit test (smaller dot, check first if visible)
    if (scanVisible)
    {
        auto area = getWaveformArea();
        float scanX = area.getX() + scanPos * area.getWidth();
        float distScan = std::abs(mx - scanX);
        if (distScan <= HANDLE_RADIUS * 2.0f && distScan <= distS && distScan <= distE)
        {
            dragging = Scan;
            return;
        }
    }

    // Bracket handle hit test
    if (distS <= HANDLE_RADIUS * 2.0f && distS <= distE)
        dragging = Start;
    else if (distE <= HANDLE_RADIUS * 2.0f)
        dragging = End;
    else if (scanVisible)
    {
        // Click anywhere on the line area sets scan position
        auto area = getWaveformArea();
        float lineYCheck = (bottomReserve > 0)
            ? area.getBottom() + HANDLE_RADIUS + static_cast<float>(bottomReserve) * 0.5f
            : area.getBottom() - 2.0f;
        if (std::abs(static_cast<float>(e.getPosition().getY()) - lineYCheck) <= HANDLE_RADIUS * 2.0f)
        {
            dragging = Scan;
            scanPos = xToFrac(mx);
            if (onScanChanged) onScanChanged(scanPos);
            repaint();
            return;
        }
        dragging = None;
    }
    else
        dragging = None;
}

void WaveformDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (dragging == None) return;

    float frac = xToFrac(static_cast<float>(e.getPosition().getX()));

    if (dragging == Scan)
    {
        scanPos = juce::jlimit(0.0f, 1.0f, frac);
        if (onScanChanged) onScanChanged(scanPos);
        repaint();
        return;
    }

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
    // Only repaint during active drag (bracket handles)
    if (dragging != None)
        repaint();
}
