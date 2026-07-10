#pragma once
#include <JuceHeader.h>
#include <vector>
#include "GuiHelpers.h"

/**
 * DcoWavetableView — static display of the last DCO bake: the table's single
 * cycles drawn as a depth-staggered stack, frame 0 at the front (bottom-left),
 * the last keyframe at the back (top-right), so the authored morph reads as
 * one gesture.
 *
 * Idle-safe by construction (docs/PERFORMANCE_GUIDE.md): the strip is
 * decimated ONCE per bake on the message thread (at most kMaxDrawn frames x
 * kPointsPerFrame points kept), the component is buffered to an image, and it
 * repaints only on a new bake or a resize — no timers, no polling, no
 * per-block work.
 */
class DcoWavetableView : public juce::Component
{
public:
    DcoWavetableView()
    {
        setInterceptsMouseClicks(false, false);
        setBufferedToImage(true);
    }

    /** Adopt a freshly baked strip (N single cycles of frameSize samples laid
     *  end-to-end, mono channel 0). Keeps a decimated copy only; the buffer
     *  can be discarded by the caller afterwards. Message thread. */
    void setStrip(const juce::AudioBuffer<float>& strip, int frameSize)
    {
        frames_.clear();
        if (frameSize > 0 && strip.getNumSamples() >= frameSize)
        {
            const int numFrames = strip.getNumSamples() / frameSize;
            // More than ~a dozen paths just smear into a blob at panel size —
            // draw evenly spaced frames, first and last always included.
            const int drawn = juce::jmin(numFrames, kMaxDrawn);
            const int step = juce::jmax(1, frameSize / kPointsPerFrame);
            const float* s = strip.getReadPointer(0);
            for (int d = 0; d < drawn; ++d)
            {
                const int k = drawn == 1 ? 0
                    : juce::roundToInt((double) d * (numFrames - 1) / (drawn - 1));
                auto& pts = frames_.emplace_back();
                pts.reserve(static_cast<size_t>(frameSize / step) + 1u);
                for (int i = 0; i < frameSize; i += step)
                    pts.push_back(s[(size_t) k * (size_t) frameSize + (size_t) i]);
            }
        }
        repaint();
    }

    bool hasContent() const noexcept { return !frames_.empty(); }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour(kSurface.brighter(0.02f));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(kBorder);
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
        if (frames_.empty())
            return;

        const auto inner = r.reduced(juce::jmax(6.0f, r.getWidth() * 0.03f));
        const int n = (int) frames_.size();
        const float depthX = inner.getWidth() * 0.22f;
        const float depthY = inner.getHeight() * 0.42f;
        const float waveW = inner.getWidth() - depthX;
        const float amp = (inner.getHeight() - depthY) * 0.5f;

        for (int d = n - 1; d >= 0; --d)   // back to front
        {
            const auto& pts = frames_[(size_t) d];
            if (pts.size() < 2)
                continue;
            const float frac = n == 1 ? 0.0f : (float) d / (float) (n - 1);
            const float x0 = inner.getX() + depthX * frac;
            const float yC = inner.getBottom() - amp - depthY * frac;
            const float dx = waveW / (float) (pts.size() - 1);

            juce::Path p;
            p.startNewSubPath(x0, yC - pts[0] * amp);
            for (size_t i = 1; i < pts.size(); ++i)
                p.lineTo(x0 + dx * (float) i, yC - pts[i] * amp);

            g.setColour(kOscCol.withAlpha(0.18f + 0.82f * (1.0f - frac)));
            g.strokePath(p, juce::PathStrokeType(d == 0 ? 1.6f : 1.0f));
        }
    }

private:
    static constexpr int kPointsPerFrame = 128;
    static constexpr int kMaxDrawn = 12;
    std::vector<std::vector<float>> frames_;
};
