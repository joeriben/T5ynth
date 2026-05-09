#include "WaveformDisplay.h"
#include "GuiHelpers.h"

// ── LockButton ──────────────────────────────────────────────────────────────

void WaveformDisplay::LockButton::paint(juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    // Active = accent background (like all other toggles), inactive = surface
    g.setColour(locked_ ? kAccent : kSurface);
    g.fillRect(b);
    g.setColour(kBorder);
    g.drawRect(b, 1.0f);

    g.setColour(locked_ ? juce::Colours::white : kDim);
    g.setFont(juce::FontOptions(std::min(b.getWidth(), b.getHeight()) * 0.55f));
    g.drawText("LOCK", b, juce::Justification::centred);
}

void WaveformDisplay::LockButton::mouseDown(const juce::MouseEvent&)
{
    locked_ = !locked_;
    repaint();
    if (onToggled) onToggled(locked_);
}

// ── WaveformDisplay ─────────────────────────────────────────────────────────

WaveformDisplay::WaveformDisplay()
{
    addAndMakeVisible(lockButton);
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

void WaveformDisplay::setStartPos(float frac)
{
    startPos = juce::jlimit(0.0f, 1.0f, frac);
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

    // ── Bracket handles with labels ──
    auto drawBracketHandle = [&](float x, const juce::String& label) {
        g.setColour(kAccent);
        g.fillEllipse(x - HANDLE_RADIUS, lineY - HANDLE_RADIUS,
                      HANDLE_RADIUS * 2.0f, HANDLE_RADIUS * 2.0f);
        g.setColour(juce::Colour(0xff0a0a0a));
        g.fillEllipse(x - HANDLE_RADIUS + 2.0f, lineY - HANDLE_RADIUS + 2.0f,
                      HANDLE_RADIUS * 2.0f - 4.0f, HANDLE_RADIUS * 2.0f - 4.0f);
        // Label inside ring
        g.setColour(kAccent);
        g.setFont(juce::FontOptions(9.0f));
        g.drawText(label, juce::Rectangle<float>(x - HANDLE_RADIUS, lineY - HANDLE_RADIUS,
                   HANDLE_RADIUS * 2.0f, HANDLE_RADIUS * 2.0f), juce::Justification::centred);
    };
    drawBracketHandle(xStart, "[");
    drawBracketHandle(xEnd, "]");

    // ── P1 (Start position) handle — filled circle with "S" label, always visible ──
    {
        float xP1 = fracToX(startPos);
        g.setColour(kAccent.brighter(0.3f));
        g.fillEllipse(xP1 - HANDLE_RADIUS, lineY - HANDLE_RADIUS,
                      HANDLE_RADIUS * 2.0f, HANDLE_RADIUS * 2.0f);
        g.setColour(juce::Colour(0xff0a0a0a));
        g.setFont(juce::FontOptions(9.0f));
        g.drawText("S", juce::Rectangle<float>(xP1 - HANDLE_RADIUS, lineY - HANDLE_RADIUS,
                   HANDLE_RADIUS * 2.0f, HANDLE_RADIUS * 2.0f), juce::Justification::centred);
    }

    // ── Scan position indicator (live playback, small dot) ──
    if (scanVisible)
    {
        float scanX = area.getX() + scanPos * area.getWidth();
        g.setColour(kAccent.withAlpha(0.5f));
        g.fillEllipse(scanX - 2.5f, lineY - 2.5f, 5.0f, 5.0f);
    }
}

void WaveformDisplay::resized()
{
    // Position the Lock button inside the waveform rectangle, bottom-right,
    // just above the P1/P2/P3 handle line. When bottomReserve > 0 the line
    // sits below the waveform → button goes to the bottom-right corner of
    // the waveform area itself. When bottomReserve == 0 the line is at
    // area.getBottom() - 2, so we nudge the button up by ~2*HANDLE_RADIUS
    // to clear the handles.
    constexpr int kBtnW = 32, kBtnH = 16;
    auto area = getWaveformArea();
    // Plant the button's bottom edge right at the P1/P2/P3 handle line so
    // it reads as belonging to the bracket controls, not the waveform.
    // lineY (from paint): bottomReserve>0 → area.bottom + HANDLE_RADIUS + reserve/2
    //                     bottomReserve==0 → area.bottom - 2
    float lineY = (bottomReserve > 0)
        ? area.getBottom() + HANDLE_RADIUS + static_cast<float>(bottomReserve) * 0.5f
        : area.getBottom() - 2.0f;
    int x = static_cast<int>(area.getRight()) - kBtnW - 2;
    int y = static_cast<int>(lineY) - kBtnH - static_cast<int>(HANDLE_RADIUS) - 1;
    lockButton.setBounds(x, y, kBtnW, kBtnH);
}

void WaveformDisplay::mouseDown(const juce::MouseEvent& e)
{
    regionDragArmed = false;
    float mx = static_cast<float>(e.getPosition().getX());
    float xS  = fracToX(loopStart);
    float xE  = fracToX(loopEnd);
    float xP1 = fracToX(startPos);

    float distS  = std::abs(mx - xS);
    float distE  = std::abs(mx - xE);
    float distP1 = std::abs(mx - xP1);

    // P1 (start pos) hit test — priority since it may overlap P2 in default state
    if (distP1 <= HANDLE_RADIUS * 2.0f && distP1 <= distS && distP1 <= distE)
    {
        dragging = StartPos;
        return;
    }

    // P2/P3 bracket handle hit test
    if (distS <= HANDLE_RADIUS * 2.0f && distS <= distE)
        dragging = Start;
    else if (distE <= HANDLE_RADIUS * 2.0f)
        dragging = End;
    else
    {
        dragging = None;
        const juce::ScopedLock lock(dataLock);
        const auto mousePos = e.position.toFloat();
        regionDragArmed = getWaveformArea().contains(mousePos)
                        && !lockButton.getBounds().toFloat().expanded(2.0f).contains(mousePos)
                        && !waveformData.empty();
    }
}

void WaveformDisplay::mouseDrag(const juce::MouseEvent& e)
{
    if (regionDragArmed && e.getDistanceFromDragStart() >= 5)
    {
        if (auto* container = findParentComponentOfClass<juce::DragAndDropContainer>())
        {
            juce::DynamicObject::Ptr payload = new juce::DynamicObject();
            payload->setProperty("kind", kSequencerOneShotDragDescription);
            payload->setProperty("start", startPos);
            payload->setProperty("end", loopEnd);
            container->startDragging(juce::var(payload.get()), this);
        }
        regionDragArmed = false;
        return;
    }

    if (dragging == None) return;

    float frac = xToFrac(static_cast<float>(e.getPosition().getX()));

    if (dragging == StartPos)
    {
        startPos = juce::jlimit(0.0f, 1.0f, frac);
        if (onStartPosChanged) onStartPosChanged(startPos);
        repaint();
        return;
    }

    if (dragging == Scan)
    {
        scanPos = juce::jlimit(0.0f, 1.0f, frac);
        if (onScanChanged) onScanChanged(scanPos);
        repaint();
        return;
    }

    // P2/P3 drag with swap logic (Rule A: P2 always < P3)
    if (dragging == Start)
    {
        loopStart = juce::jlimit(0.0f, 1.0f, frac);
        if (loopStart >= loopEnd)
        {
            // Swap: P2 crossed over P3
            std::swap(loopStart, loopEnd);
            dragging = End;
        }
    }
    else if (dragging == End)
    {
        loopEnd = juce::jlimit(0.0f, 1.0f, frac);
        if (loopEnd <= loopStart)
        {
            // Swap: P3 crossed over P2
            std::swap(loopStart, loopEnd);
            dragging = Start;
        }
    }

    if (onLoopRegionChanged)
        onLoopRegionChanged(loopStart, loopEnd);

    repaint();
}

void WaveformDisplay::mouseUp(const juce::MouseEvent&)
{
    const bool finishedMarkerDrag = dragging == Start || dragging == End || dragging == StartPos;
    dragging = None;
    regionDragArmed = false;
    if (finishedMarkerDrag && onMarkerDragFinished)
        onMarkerDragFinished();
}

void WaveformDisplay::setWaveform(const float* data, int numSamples)
{
    const juce::ScopedLock lock(dataLock);
    waveformData.assign(data, data + numSamples);
}

void WaveformDisplay::tickScan()
{
    if (!scanVisible) return;

    static constexpr float kSmooth = 0.3f; // one-pole, ~80 ms at 30 fps

    if (std::isnan(scanTarget))
    {
        if (!std::isnan(scanPos)) { scanPos = std::numeric_limits<float>::quiet_NaN(); repaint(); }
        return;
    }

    if (std::isnan(scanPos))
        scanPos = scanTarget;
    else
        scanPos += (scanTarget - scanPos) * kSmooth;

    auto area = getWaveformArea();
    float px = area.getX() + scanPos * area.getWidth();
    if (std::abs(px - lastScanPx) > 0.5f) { lastScanPx = px; repaint(); }
}

void WaveformDisplay::timerCallback()
{
    // Only repaint during active drag (bracket handles)
    if (dragging != None)
        repaint();
}
