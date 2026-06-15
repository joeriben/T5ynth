#pragma once
#include <JuceHeader.h>
#include <cmath>
#include "GuiHelpers.h"

// ── Re-Prompt stance "symbol slider" ─────────────────────────────────────────
// A detented horizontal control that picks the Re-Prompt interpreter STANCE by
// painting seven movement-type phase-portrait glyphs (NO text labels — the
// glyphs ARE the scale; see the approved mockup / docs). It drives an
// AudioParameterChoice through a juce::ParameterAttachment, so it needs no
// external attachment member and the binding tears down with the component.
//
// Glyph index → stance MUST mirror BlockParams.h RepromptStance order:
//   0 off · 1 transcribe (fixed point) · 2 entkitscher (inward spiral) ·
//   3 verniedlicher (damped settling) · 4 variation (bounded cluster) ·
//   5 abduktion (divergent walk) · 6 opposite (limit cycle).
//
// Idle-cheap by design: it repaints ONLY when the parameter changes (the attachment
// callback) or on a click/drag — never on a timer. (Re-Prompt is engine-agnostic, so
// the bar is not model-gated; setFromMouse still guards on isEnabled() defensively.)
class RepromptStanceBar : public juce::Component,
                          public juce::SettableTooltipClient
{
public:
    RepromptStanceBar()
    {
        setInterceptsMouseClicks (true, false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    // Bind to the choice parameter (call once, from MainPanel's ctor).
    void attachTo (juce::RangedAudioParameter& param, int numPositions)
    {
        numPositions_ = juce::jmax (2, numPositions);
        attachment_ = std::make_unique<juce::ParameterAttachment> (
            param,
            [this] (float v)
            {
                const int idx = juce::jlimit (0, numPositions_ - 1, juce::roundToInt (v));
                if (idx != currentIndex_) { currentIndex_ = idx; repaint(); }
            });
        attachment_->sendInitialUpdate();   // adopt the restored value
    }

    // Per-glyph hover tooltips (index → text); the TooltipWindow shows the one under
    // the mouse (see mouseMove). Empty → fall back to the single component tooltip.
    void setPositionTooltips (juce::StringArray tips) { positionTips_ = std::move (tips); }

    void paint (juce::Graphics& g) override
    {
        const Geom gm = geom();
        const int N = numPositions_;

        // Connecting track behind the glyphs (faint), then the glyphs on top.
        g.setColour (kBorder);
        g.drawLine (cxFor (0, gm), gm.cy, cxFor (N - 1, gm), gm.cy,
                    juce::jmax (1.0f, gm.R * 0.10f));

        // Amber selection highlight around the active detent.
        {
            const float x  = cxFor (currentIndex_, gm);
            const float hw = (gm.step > 0.0f ? juce::jmin (gm.step * 0.92f, gm.R * 2.5f) : gm.R * 2.5f);
            const float hh = juce::jmin (gm.R * 2.0f, (float) getHeight() - 2.0f);
            auto hl = juce::Rectangle<float> (0.0f, 0.0f, hw, hh).withCentre ({ x, gm.cy });
            g.setColour (kDriftCol.withAlpha (0.16f));
            g.fillRoundedRectangle (hl, 4.0f);
            g.setColour (kDriftCol.withAlpha (0.85f));
            g.drawRoundedRectangle (hl, 4.0f, 1.0f);
        }

        for (int i = 0; i < N; ++i)
        {
            const juce::Colour c = (i == currentIndex_) ? kDriftCol
                                 : (i == 0 ? kDim.withMultipliedAlpha (0.7f) : kDim);
            drawGlyph (g, i, cxFor (i, gm), gm.cy, gm.R, c);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override { setFromMouse (e); }
    void mouseDrag (const juce::MouseEvent& e) override { setFromMouse (e); }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (positionTips_.isEmpty()) return;
        const int idx = indexForX ((float) e.position.x);
        if (juce::isPositiveAndBelow (idx, positionTips_.size()))
            setTooltip (positionTips_[idx]);   // cheap: stores a String, no repaint
    }

private:
    struct Geom { float R, marginX, step, cy; };

    Geom geom() const
    {
        auto b = getLocalBounds().toFloat();
        const int N = numPositions_;
        const float R = juce::jlimit (7.0f, 18.0f,
                            juce::jmin (b.getHeight() * 0.36f,
                                        (b.getWidth() / (float) N) * 0.46f));
        const float marginX = R + 3.0f;
        const float usableW = juce::jmax (1.0f, b.getWidth() - 2.0f * marginX);
        const float step    = (N > 1) ? usableW / (float) (N - 1) : 0.0f;
        return { R, marginX, step, b.getCentreY() };
    }

    float cxFor (int i, const Geom& gm) const
    {
        return getLocalBounds().toFloat().getX() + gm.marginX + gm.step * (float) i;
    }

    int indexForX (float x) const
    {
        const Geom gm = geom();
        if (gm.step <= 0.0f) return 0;
        const float x0 = getLocalBounds().toFloat().getX() + gm.marginX;
        return juce::jlimit (0, numPositions_ - 1, juce::roundToInt ((x - x0) / gm.step));
    }

    void setFromMouse (const juce::MouseEvent& e)
    {
        if (! isEnabled() || attachment_ == nullptr) return;
        const int idx = indexForX ((float) e.position.x);
        if (idx != currentIndex_)
        {
            currentIndex_ = idx;                              // optimistic; callback reconciles
            repaint();
            attachment_->setValueAsCompleteGesture ((float) idx);
        }
    }

    // ── glyph primitives ──
    static void fillDot (juce::Graphics& g, float x, float y, float r)
    {
        g.fillEllipse (x - r, y - r, 2.0f * r, 2.0f * r);
    }

    // Filled triangular arrowhead; `ang` is the direction the arrow POINTS (rad).
    static void arrowHead (juce::Graphics& g, float tipX, float tipY, float ang, float size)
    {
        const float a1 = ang + 2.6f, a2 = ang - 2.6f;
        juce::Path p;
        p.startNewSubPath (tipX, tipY);
        p.lineTo (tipX + size * std::cos (a1), tipY + size * std::sin (a1));
        p.lineTo (tipX + size * std::cos (a2), tipY + size * std::sin (a2));
        p.closeSubPath();
        g.fillPath (p);
    }

    // index → phase-portrait glyph (mirrors RepromptStance order; see header note).
    static void drawGlyph (juce::Graphics& g, int index, float cx, float cy, float R, juce::Colour col)
    {
        const float sw = juce::jlimit (1.1f, 2.3f, R * 0.14f);
        const auto stroke = juce::PathStrokeType (sw, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded);
        g.setColour (col);

        switch (index)
        {
            case 0: // off — disabled / no dynamics
            {
                const float rr = R * 0.62f, s = R * 0.44f;
                g.drawEllipse (cx - rr, cy - rr, 2.0f * rr, 2.0f * rr, sw);
                g.drawLine (cx - s, cy - s, cx + s, cy + s, sw);
                break;
            }
            case 1: // transcribe — stable fixed point (4 inward arrows → a dot)
            {
                fillDot (g, cx, cy, R * 0.16f);
                const float outer = R * 0.96f, inner = R * 0.42f, hs = R * 0.26f;
                const float dirs[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };
                for (auto& d : dirs)
                {
                    const float ix = cx + d[0] * inner, iy = cy + d[1] * inner;
                    g.drawLine (cx + d[0] * outer, cy + d[1] * outer, ix, iy, sw);
                    arrowHead (g, ix, iy, std::atan2 (-d[1], -d[0]), hs);
                }
                break;
            }
            case 2: // entkitscher — convergent erosion (inward spiral)
            {
                juce::Path p;
                const int steps = 64; const float turns = 2.6f;
                for (int i = 0; i <= steps; ++i)
                {
                    const float t   = (float) i / (float) steps;
                    const float ang = t * turns * juce::MathConstants<float>::twoPi;
                    const float rad = R * 0.94f * (1.0f - t);
                    const float x = cx + std::cos (ang) * rad, y = cy + std::sin (ang) * rad;
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                g.strokePath (p, stroke);
                fillDot (g, cx, cy, R * 0.12f);
                break;
            }
            case 3: // verniedlicher — smooth convergence (damped settling)
            {
                juce::Path p;
                const int steps = 72; const float left = -R * 0.95f, right = R * 0.92f;
                for (int i = 0; i <= steps; ++i)
                {
                    const float t   = (float) i / (float) steps;
                    const float x   = left + (right - left) * t;
                    const float env = std::exp (-2.7f * t);
                    const float y   = -R * 0.72f * env
                                      * std::sin (t * juce::MathConstants<float>::twoPi * 2.3f);
                    if (i == 0) p.startNewSubPath (cx + x, cy + y); else p.lineTo (cx + x, cy + y);
                }
                g.strokePath (p, stroke);
                fillDot (g, cx + right, cy, R * 0.14f);
                break;
            }
            case 4: // variation — bounded cluster (dots inside a faint ring)
            {
                const float ring = R * 0.90f;
                g.setColour (col.withMultipliedAlpha (0.5f));
                g.drawEllipse (cx - ring, cy - ring, 2.0f * ring, 2.0f * ring,
                               juce::jmax (1.0f, sw * 0.7f));
                g.setColour (col);
                fillDot (g, cx, cy, R * 0.18f);
                const float sat[5][2] = { { -0.46f, -0.30f }, { 0.44f, -0.40f },
                                          {  0.52f,  0.26f }, { -0.16f,  0.50f },
                                          { -0.52f,  0.20f } };
                for (auto& s : sat) fillDot (g, cx + s[0] * R, cy + s[1] * R, R * 0.13f);
                break;
            }
            case 5: // abduktion — divergent walk (wanders out of frame)
            {
                const float pts[7][2] = { { 0.00f,  0.18f }, { -0.20f, -0.02f },
                                          { 0.08f, -0.24f }, { -0.10f, -0.48f },
                                          { 0.26f, -0.58f }, {  0.52f, -0.80f },
                                          { 0.80f, -0.96f } };
                juce::Path p;
                for (int i = 0; i < 7; ++i)
                {
                    const float x = cx + pts[i][0] * R, y = cy + pts[i][1] * R;
                    if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
                }
                g.strokePath (p, stroke);
                fillDot (g, cx + pts[0][0] * R, cy + pts[0][1] * R, R * 0.12f);
                const float ex = cx + pts[6][0] * R, ey = cy + pts[6][1] * R;
                arrowHead (g, ex, ey,
                           std::atan2 (pts[6][1] - pts[5][1], pts[6][0] - pts[5][0]), R * 0.26f);
                break;
            }
            case 6: // opposite — limit cycle (orbit between two poles)
            {
                const float rx = R * 0.90f, ry = R * 0.56f;
                g.drawEllipse (cx - rx, cy - ry, 2.0f * rx, 2.0f * ry, sw);
                fillDot (g, cx - rx, cy, R * 0.13f);
                fillDot (g, cx + rx, cy, R * 0.13f);
                arrowHead (g, cx, cy - ry, 0.0f, R * 0.24f);                                   // top → right
                arrowHead (g, cx, cy + ry, juce::MathConstants<float>::pi, R * 0.24f);         // bottom → left
                break;
            }
            default: break;
        }
    }

    int numPositions_ = 7;
    int currentIndex_ = 0;
    juce::StringArray positionTips_;
    std::unique_ptr<juce::ParameterAttachment> attachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RepromptStanceBar)
};
