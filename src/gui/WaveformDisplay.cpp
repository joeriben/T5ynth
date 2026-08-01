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

float WaveformDisplay::xToScanFrac(float x) const
{
    if (! wtMode)
        return xToFrac(x);
    // Invert the fan cursor mapping from paint(): x0 = area.x + depthX*frac and
    // cursorX = x0 + waveW/2, so frac = (x - area.x - waveW/2) / depthX. Keeps the
    // dragged cursor under the pointer across the compressed 2.5D X axis.
    const auto area = getWaveformArea();
    const float depthX = area.getWidth() * kWtDepthXFrac;
    const float waveW  = area.getWidth() - depthX;
    return juce::jlimit(0.0f, 1.0f, (x - area.getX() - waveW * 0.5f) / juce::jmax(1.0f, depthX));
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

        // Loop-interval readout is a sample concept; a baked table has no source
        // seconds, so the wavetable view drops it (keeps only the "Wavetable" label).
        if (! wtMode)
        {
            float tStart = loopStart * bufferDurationSec;
            float tEnd   = loopEnd * bufferDurationSec;
            juce::String timeStr = juce::String(tStart, 3) + "s \u2013 " + juce::String(tEnd, 3) + "s";
            g.setColour(kAccent);
            g.drawText(timeStr, labelArea, juce::Justification::centredRight);
        }
    }

    const juce::ScopedLock lock(dataLock);

    if (wtMode)
    {
        // ── Wavetable: 2.5D fan (cached) + live scan cursor ──────────────────
        // Blit the pre-rendered static fan (frames marching along X with a gentle
        // slant), then overlay ONLY the cycle at the current effective scan
        // position, bright, with a vertical guide — it sweeps along X as the DCO
        // motion plays. The cache is rebuilt on a new bake or a resize; a moving
        // cursor never touches it. No brackets/dim region (a baked table has no
        // source slice to select).
        if (wtFanDirty || wtFanCache.getWidth() != getWidth() || wtFanCache.getHeight() != getHeight())
            renderWtFan();
        if (wtFanCache.isValid())
            g.drawImageAt(wtFanCache, 0, 0);

        const int n = static_cast<int>(wtFrames.size());
        if (n > 0 && ! std::isnan(scanPos))
        {
            const float depthX = area.getWidth()  * kWtDepthXFrac;
            const float depthY = area.getHeight() * kWtDepthYFrac;
            const float waveW  = area.getWidth() - depthX;
            const float ampWt  = (area.getHeight() - depthY) * 0.5f;
            const int cf = juce::jlimit(0, n - 1,
                juce::roundToInt(juce::jlimit(0.0f, 1.0f, scanPos) * (n - 1)));
            const float frac = n == 1 ? 0.0f : static_cast<float>(cf) / static_cast<float>(n - 1);
            const float x0 = area.getX() + depthX * frac;
            const float yC = area.getBottom() - ampWt - depthY * frac;
            const float cursorX = x0 + waveW * 0.5f;
            g.setColour(juce::Colours::white.withAlpha(0.25f));   // faint vertical guide at the scan frame
            g.drawLine(cursorX, area.getY(), cursorX, area.getBottom(), 1.0f);
            const auto& pts = wtFrames[static_cast<size_t>(cf)];
            if (pts.size() >= 2)
            {
                const float dx = waveW / static_cast<float>(pts.size() - 1);
                juce::Path p;
                p.startNewSubPath(x0, yC - pts[0] * ampWt);
                for (size_t i = 1; i < pts.size(); ++i)
                    p.lineTo(x0 + dx * static_cast<float>(i), yC - pts[i] * ampWt);
                g.setColour(juce::Colours::white);                // the sounding cycle, bright
                g.strokePath(p, juce::PathStrokeType(2.2f));
            }
        }
        return;
    }

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

    // ── Scan position indicator (live playback playhead) ──
    // Full-height vertical line in a paler pink than the saturated accent
    // (kAccent) used by the loop brackets, so a scan position being driven —
    // e.g. an envelope/LFO routed to Scan sweeping the granular read point to
    // the buffer end — is unmistakable as it runs. NaN-guarded: scanPos is NaN
    // until the first live update. Repaint gating stays in tickScan(), so a
    // static scan position triggers no repaints (idle-safe).
    if (scanVisible && ! std::isnan(scanPos))
    {
        const float scanX = area.getX() + scanPos * area.getWidth();
        const juce::Colour playhead (0xffffafcd); // pale pink (user choice); white = fallback
        g.setColour(playhead.withAlpha(0.85f));
        g.drawLine(scanX, area.getY(), scanX, area.getBottom(), 1.5f);
        g.setColour(playhead);
        g.fillEllipse(scanX - 3.0f, lineY - 3.0f, 6.0f, 6.0f);
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

    wtFanDirty = true;   // size changed → the cached WT fan must be re-rendered
}

void WaveformDisplay::mouseDown(const juce::MouseEvent& e)
{
    regionDragArmed = false;
    float mx = static_cast<float>(e.getPosition().getX());

    // Fan view: the whole display is a hand-playable scan strip (Serum/Vital-style
    // wavetable-position drag). Any press starts a Scan drag; there are no brackets
    // or lock button here. oscScan sums on top of DCO motion in the engine, so this
    // is a live offset while a baked table is sweeping, or the absolute position on
    // a static one.
    if (wtMode)
    {
        dragging = Scan;
        scanPos = xToScanFrac(mx);
        if (onScanChanged) onScanChanged(scanPos);
        repaint();
        return;
    }

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
        // Use the fan-aware mapping so the cursor tracks the pointer in wtMode.
        scanPos = xToScanFrac(static_cast<float>(e.getPosition().getX()));
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
    {
        const juce::ScopedLock lock(dataLock);
        waveformData.assign(data, data + numSamples);
        if (wtMode) { wtMode = false; wtFrames.clear(); }
    }
    // Back to the interactive sample view (sampler / freeze / neural wavetable):
    // brackets draggable, lock button shown. Idempotent when already a sample.
    applyMouseInterception(true);
    lockButton.setVisible(! inert);
}

void WaveformDisplay::applyMouseInterception(bool allowClicksOnChildren)
{
    setInterceptsMouseClicks(! inert, ! inert && allowClicksOnChildren);
}

void WaveformDisplay::setInert(bool shouldBeInert)
{
    if (inert == shouldBeInert) return;
    inert = shouldBeInert;
    // Leaving inert restores whatever the current data mode wants: a baked
    // table takes clicks itself (scan strip) and keeps its children out of the
    // way, a sample view takes them everywhere.
    applyMouseInterception(! wtMode);
    lockButton.setVisible(! inert && ! wtMode);
}

void WaveformDisplay::setWavetableFrames(const juce::AudioBuffer<float>& strip, int frameSize)
{
    bool ok = false;
    {
        const juce::ScopedLock lock(dataLock);
        wtFrames.clear();
        if (frameSize > 0 && strip.getNumChannels() > 0
            && strip.getNumSamples() >= frameSize)
        {
            const int numFrames = strip.getNumSamples() / frameSize;
            // More than ~a dozen cycles smear into a blob at panel width — draw
            // evenly spaced frames, first and last always in.
            const int drawn = juce::jmin(numFrames, kWtMaxDrawnFrames);
            const int step  = juce::jmax(1, frameSize / kWtPointsPerFrame);
            const float* s = strip.getReadPointer(0);
            for (int d = 0; d < drawn; ++d)
            {
                const int k = drawn == 1 ? 0
                    : juce::roundToInt((double) d * (numFrames - 1) / (drawn - 1));
                auto& pts = wtFrames.emplace_back();
                pts.reserve(static_cast<size_t>(frameSize / step) + 1u);
                for (int i = 0; i < frameSize; i += step)
                    pts.push_back(s[(size_t) k * (size_t) frameSize + (size_t) i]);
            }
        }
        wtMode = ok = ! wtFrames.empty();
    }
    // A baked table has no source region to slice: no brackets, no lock button.
    // The widget still intercepts clicks in wtMode so the fan is a hand-playable
    // scan strip (mouseDown → Scan drag); children stay click-through (the hidden
    // lock button). On a degenerate strip (ok == false) fall back to the fully
    // interactive sample view.
    applyMouseInterception(! ok);
    lockButton.setVisible(! inert && ! ok);
    wtFanDirty = true;   // new table → rebuild the cached fan on next paint
    repaint();
}

void WaveformDisplay::exitWavetableMode()
{
    {
        const juce::ScopedLock lock(dataLock);
        if (! wtMode) return;
        wtMode = false;
        wtFrames.clear();
    }
    wtFanCache = juce::Image();   // release the cached fan
    applyMouseInterception(true);
    lockButton.setVisible(! inert);
    repaint();
}

// (Re)render the static 2.5D fan (all decimated frames, no cursor) into
// wtFanCache at the current component size. Message thread; called lazily from
// paint when the cache is dirty or the size changed. The cursor is NOT baked in
// — it is overlaid live in paint so the fan image survives every cursor move.
void WaveformDisplay::renderWtFan()
{
    wtFanDirty = false;
    const int w = getWidth(), h = getHeight();
    if (w <= 0 || h <= 0) { wtFanCache = juce::Image(); return; }

    wtFanCache = juce::Image(juce::Image::ARGB, w, h, true);
    juce::Graphics g(wtFanCache);

    const juce::ScopedLock lock(dataLock);   // wtFrames (recursive CS — safe if paint holds it)
    const int n = static_cast<int>(wtFrames.size());
    if (n == 0)
        return;

    const auto area   = getWaveformArea();
    const float depthX = area.getWidth()  * kWtDepthXFrac;
    const float depthY = area.getHeight() * kWtDepthYFrac;
    const float waveW  = area.getWidth() - depthX;
    const float amp    = (area.getHeight() - depthY) * 0.5f;
    for (int d = n - 1; d >= 0; --d)   // back → front (frame 0 drawn last, on top)
    {
        const auto& pts = wtFrames[static_cast<size_t>(d)];
        if (pts.size() < 2) continue;
        const float frac = n == 1 ? 0.0f : static_cast<float>(d) / static_cast<float>(n - 1);
        const float x0 = area.getX() + depthX * frac;
        const float yC = area.getBottom() - amp - depthY * frac;
        const float dx = waveW / static_cast<float>(pts.size() - 1);
        juce::Path p;
        p.startNewSubPath(x0, yC - pts[0] * amp);
        for (size_t i = 1; i < pts.size(); ++i)
            p.lineTo(x0 + dx * static_cast<float>(i), yC - pts[i] * amp);
        g.setColour(kOscCol.withAlpha(0.10f + 0.5f * (1.0f - frac)));
        g.strokePath(p, juce::PathStrokeType(d == 0 ? 1.4f : 0.9f));
    }
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

    if (std::isnan(scanPos) || wtMode)
        // wtMode = a DCO table: the motion transport is already deterministic and
        // smooth, so track it 1:1. The one-pole smoother (tuned for the noisy
        // sample-scan dot) would only LAG it, COMPRESS its range at higher motion
        // rates (cursor never reaching the ends → looks static), and smear the
        // 1->0 loop wrap into a backward sweep. Snapping matches the audio, whose
        // wrap is an instant content-seamless jump.
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
