#pragma once
#include <JuceHeader.h>
#include "GuiHelpers.h"

// ── The LCO authoring trace ──────────────────────────────────────────────────
// What replaced the "HEARD AS" text box in the LCO panel (BJ 2026-07-24): the
// authoring is shown as the sequence of things that actually happened, one
// station per step, instead of a reading with the raw Csound body printed under
// a rule.
//
//   HEARD      the prompt that was authored
//   LOOKED UP  the library entries the prompt's own WORDS reached, plus the
//              orientation entries the author was shown anyway
//   WROTE      which model wrote it, and its own one-line reading
//   REPAIRED   the Csound errors it had to be repaired past (absent when it
//              compiled first try — an absent station is a fact, not a gap)
//   RUNNING    the compile state of the orchestra now in the engine
//
// Every station is a RECORD, never a reconstruction. The author is asked for no
// account of itself (its system prompt is untouched — BJ: "nur was ohnehin
// passiert ist"), so nothing here is the machine explaining itself; it is the
// write-path's own bookkeeping, which used to be discarded inside Python.
//
// Two consequences that are deliberate and must not be "tidied away":
//  - `reached` and `orientedBy` are drawn differently and labelled apart.
//    lco_write.select() tops the instrument list up from a starter set whenever
//    a prompt reaches too few, so most entries the author sees for a short
//    prompt are NOT things the prompt said. Drawing them alike would make the
//    panel claim an understanding that never happened.
//  - an empty motions list is shown as such. "No movement word" is exactly the
//    condition movement-by-default has to answer for, and hiding it would hide
//    the interesting case.
//
// Idle-cheap: paints only when something is set. No timer, no animation.
// The Csound body is NOT drawn here — it becomes the back of this card in the
// next step; this view holds only the trace.
class LcoTraceView : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    /** One authored orchestra's trace, as it came off the wire. */
    struct Trace
    {
        juce::String prompt;      // what was authored
        juce::String model;       // the model that actually wrote it
        juce::String reading;     // the author's own READING line
        juce::StringArray reachedInstruments, reachedAdjectives, reachedMotions;
        juce::StringArray orientedBy;   // shown to the author, NOT reached by the prompt
        int libraryEntryCount = 0;
        juce::StringArray repairs;      // compiler errors repaired past, first-seen order
        int attempts = 0;               // 1 = compiled on the first pass
        bool valid = false;
    };

    LcoTraceView()
    {
        addAndMakeVisible(viewport_);
        viewport_.setViewedComponent(&content_, false);
        viewport_.setScrollBarsShown(true, false);
        viewport_.setScrollBarThickness(7);
        viewport_.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId,
                                                   kBorder.brighter(0.25f));
        viewport_.getVerticalScrollBar().setColour(juce::ScrollBar::trackColourId,
                                                   juce::Colours::transparentBlack);
        content_.owner_ = this;
        // Takes its own mouse, exactly as the text box it replaced did: the
        // scrollbar needs drags, and the card flip lands here next.
        setInterceptsMouseClicks(true, true);
    }

    /** The panel's responsive base font — same unit every other LCO widget uses. */
    void setBaseFont(float f)
    {
        if (juce::approximatelyEqual(f, base_))
            return;
        base_ = f;
        relayout();
    }

    /** Show a trace. Clears any status line: a trace IS the state. */
    void setTrace(Trace t)
    {
        trace_  = std::move(t);
        trace_.valid = true;
        status_.clear();
        relayout();
    }

    /** Dim single-line state, shown INSTEAD of the trace — "authoring...",
     *  "prompt is empty", an authoring failure. Replacing the trace is the
     *  point: the previous trace describes a sound that is being superseded or
     *  an attempt that failed, and leaving it up would let it read as current.
     *  (This mirrors what the text box it replaced did.) */
    void setStatus(const juce::String& text)
    {
        status_ = text;
        trace_ = {};
        relayout();
    }

    /** The RUNNING station: the compile window's own report. Empty text with
     *  no error = compiled clean. Kept separate from setStatus so a compile
     *  result never wipes the trace of the orchestra it is reporting on. */
    void setCompileState(const juce::String& text, bool isError)
    {
        compileText_    = text;
        compileIsError_ = isError;
        relayout();
    }

    /** Placeholder for the empty panel, before anything has been authored. */
    void setPlaceholder(juce::String text) { placeholder_ = std::move(text); relayout(); }

    bool hasTrace() const noexcept { return trace_.valid; }

    /** The scrolled content sits ON TOP of this view, so it — not this view — is
     *  the component a tooltip lookup finds under the mouse (JUCE asks the
     *  deepest hit component and does not walk up to its parents). Mirror the
     *  tip down, or a tooltip set here would simply never appear. */
    void setTooltip(const juce::String& t) override
    {
        juce::SettableTooltipClient::setTooltip(t);
        content_.setTooltip(t);
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(kSurface.brighter(0.08f));
        g.fillRoundedRectangle(b, 3.0f);
        g.setColour(kBorder);
        g.drawRoundedRectangle(b.reduced(0.5f), 3.0f, 1.0f);
    }

    void resized() override
    {
        viewport_.setBounds(getLocalBounds().reduced(1));
        relayout();
    }

private:
    // ── Geometry, in one place so measuring and drawing cannot drift ──────────
    float labelFont() const { return juce::jmax(9.5f,  base_ * 0.74f); }
    float bodyFont()  const { return juce::jmax(11.0f, base_ * 1.05f); }
    float hintFont()  const { return juce::jmax(9.5f,  base_ * 0.84f); }
    int   dotR()      const { return 3; }
    int   railX()     const { return 4; }            // centre of the dot column
    int   textX()     const { return railX() + 12; }
    int   stationGap() const { return juce::roundToInt(bodyFont() * 0.85f); }

    // Small-caps station label with manual tracking — JUCE has no letter-spacing,
    // and the spacing is what makes these read as rubrics rather than as text.
    static float trackedTextWidth(const juce::Font& f, const juce::String& s, float track)
    {
        float w = 0.0f;
        for (int i = 0; i < s.length(); ++i)
            w += juce::GlyphArrangement::getStringWidth(f, s.substring(i, i + 1)) + track;
        return w;
    }

    static void drawTrackedText(juce::Graphics& g, const juce::Font& f,
                                const juce::String& s, float x, float y, float h, float track)
    {
        for (int i = 0; i < s.length(); ++i)
        {
            const auto ch = s.substring(i, i + 1);
            const float w = juce::GlyphArrangement::getStringWidth(f, ch);
            g.drawText(ch, juce::Rectangle<float>(x, y, w + track, h),
                       juce::Justification::centredLeft, false);
            x += w + track;
        }
    }

    /** Wrapped paragraph: measures and (when g != nullptr) draws in ONE code
     *  path, so the height the layout reserves is always the height it needs. */
    static float paragraph(juce::Graphics* g, const juce::String& text, const juce::Font& f,
                           juce::Colour c, float x, float y, float w)
    {
        if (text.isEmpty() || w <= 1.0f)
            return 0.0f;
        juce::AttributedString as;
        as.append(text, f, c);
        as.setLineSpacing(1.0f);
        juce::TextLayout tl;
        tl.createLayout(as, w);
        if (g != nullptr)
            tl.draw(*g, { x, y, w, tl.getHeight() });
        return tl.getHeight();
    }

    /** A run of key chips, wrapped. `dimmed` draws the orientation entries the
     *  prompt did NOT reach — same shape, visibly weaker, never mixed in. */
    float chips(juce::Graphics* g, const juce::StringArray& keys,
                float x, float y, float w, bool dimmed) const
    {
        if (keys.isEmpty())
            return 0.0f;
        // Braces, not parens: `Font f(FontOptions(x))` is a function declaration.
        const juce::Font f { juce::FontOptions(hintFont()) };
        const float h    = std::round(hintFont() * 1.75f);
        const float padX = 5.0f, gap = 4.0f;
        float cx = x, cy = y;
        for (const auto& k : keys)
        {
            const float tw = juce::GlyphArrangement::getStringWidth(f, k);
            const float cw = tw + padX * 2.0f;
            if (cx > x && cx + cw > x + w)      // wrap
            {
                cx = x;
                cy += h + gap;
            }
            if (g != nullptr)
            {
                const juce::Rectangle<float> r(cx, cy, cw, h);
                g->setColour(dimmed ? kBorder.withAlpha(0.55f) : kBorder);
                g->drawRoundedRectangle(r.reduced(0.5f), 2.5f, 1.0f);
                g->setColour(dimmed ? kTextDisabled : kImpulseAText);
                g->setFont(f);
                g->drawText(k, r, juce::Justification::centred, false);
            }
            cx += cw + gap;
        }
        return (cy - y) + h;
    }

    /** Walks every station. g == nullptr measures, g != nullptr draws — one
     *  traversal for both so the scroll height can never disagree with the
     *  content. Returns the total height. */
    int render(juce::Graphics* g, int width) const
    {
        // Right inset clears the viewport's own scrollbar (7px) plus a margin:
        // the content width is asked for BEFORE the scrollbar's visibility is
        // settled, so a tighter margin lets a long line slide under the thumb.
        const float w = static_cast<float>(width) - static_cast<float>(textX()) - 11.0f;
        // Braces, not parens: `Font f(FontOptions(x))` is a function declaration.
        const juce::Font fLabel { juce::FontOptions(labelFont()) };
        const juce::Font fBody  { juce::FontOptions(bodyFont())  };
        const juce::Font fHint  { juce::FontOptions(hintFont())  };
        const float labelH = std::round(labelFont() * 1.5f);
        float y = 7.0f;

        // No trace yet: a single dim line (status, or the empty-state text).
        if (! trace_.valid)
        {
            const auto text = status_.isNotEmpty() ? status_ : placeholder_;
            y += paragraph(g, text, fBody, kDim, static_cast<float>(textX()), y, w);
            return juce::roundToInt(y + 7.0f);
        }

        // One station: dot, rubric, body. `hasNext` draws the hairline down to
        // where the next dot will sit; the last station ends the rail instead of
        // trailing a line into empty space.
        auto station = [&](juce::Colour dot, const juce::String& rubric,
                           juce::Colour rubricCol, bool hasNext,
                           const std::function<float(float)>& body)
        {
            const float dotY = y + labelH * 0.5f;
            if (g != nullptr)
            {
                g->setColour(rubricCol);
                drawTrackedText(*g, fLabel, rubric, static_cast<float>(textX()), y, labelH,
                                juce::jmax(0.8f, labelFont() * 0.16f));
            }
            y += labelH + 1.0f;
            y += body(w);
            if (g != nullptr)
            {
                // Hairline first, dot over it — the dot must not be cut into.
                if (hasNext)
                {
                    g->setColour(kBorder);
                    g->fillRect(juce::Rectangle<float>(static_cast<float>(railX()) - 0.5f,
                                                       dotY, 1.0f,
                                                       juce::jmax(0.0f, y + stationGap() - dotY)));
                }
                g->setColour(dot);
                g->fillEllipse(static_cast<float>(railX() - dotR()), dotY - static_cast<float>(dotR()),
                               static_cast<float>(dotR() * 2), static_cast<float>(dotR() * 2));
            }
            y += static_cast<float>(stationGap());
        };

        // ── HEARD ────────────────────────────────────────────────────────────
        station(kImpulseA, "HEARD", kTextDisabled, true, [&](float ww)
        {
            return paragraph(g, trace_.prompt, fBody, kImpulseAText,
                             static_cast<float>(textX()), y, ww);
        });

        // ── LOOKED UP ────────────────────────────────────────────────────────
        // Reached first, then — visibly weaker and separately captioned — what
        // the author was shown anyway. The count line names both.
        station(kImpulseA, "LOOKED UP", kTextDisabled, true, [&](float ww)
        {
            const float y0 = y;
            juce::StringArray reached;
            reached.addArray(trace_.reachedInstruments);
            reached.addArray(trace_.reachedAdjectives);
            reached.addArray(trace_.reachedMotions);

            float yy = y;
            if (reached.isEmpty())
            {
                yy += paragraph(g, "no word of this prompt is in the library",
                                fHint, kWarning, static_cast<float>(textX()), yy, ww);
            }
            else
            {
                yy += chips(g, reached, static_cast<float>(textX()), yy, ww, false) + 4.0f;
            }

            juce::String note;
            if (trace_.libraryEntryCount > 0)
                note << reached.size() << " of " << trace_.libraryEntryCount << " entries reached";
            if (trace_.reachedMotions.isEmpty())
                note << (note.isEmpty() ? "" : " · ") << "no movement named";
            if (note.isNotEmpty())
                yy += paragraph(g, note, fHint, kDimmer, static_cast<float>(textX()), yy, ww) + 2.0f;

            if (! trace_.orientedBy.isEmpty())
            {
                yy += paragraph(g, "also shown, for orientation:", fHint, kTextDisabled,
                                static_cast<float>(textX()), yy, ww) + 1.0f;
                yy += chips(g, trace_.orientedBy, static_cast<float>(textX()), yy, ww, true);
            }
            return yy - y0;
        });

        // ── WROTE ────────────────────────────────────────────────────────────
        station(kImpulseA, "WROTE", kTextDisabled, true, [&](float ww)
        {
            const float y0 = y;
            float yy = y;
            if (trace_.model.isNotEmpty())
                yy += paragraph(g, trace_.model, fHint, kDim,
                                static_cast<float>(textX()), yy, ww) + 2.0f;
            if (trace_.reading.isNotEmpty())
                yy += paragraph(g, trace_.reading, fBody, kImpulseAText,
                                static_cast<float>(textX()), yy, ww);
            return yy - y0;
        });

        // ── REPAIRED ─────────────────────────────────────────────────────────
        // Drawn ONLY when the author actually had to be sent back. A first-try
        // orchestra shows no station here, and that absence is the report.
        if (! trace_.repairs.isEmpty())
        {
            juce::String rubric("REPAIRED");
            if (trace_.attempts > 1)
                rubric << "  " << trace_.attempts << " attempts";
            station(kWarning, rubric, kWarning, true, [&](float ww)
            {
                const float y0 = y;
                float yy = y;
                for (const auto& e : trace_.repairs)
                    yy += paragraph(g, e, fHint, kDim, static_cast<float>(textX()), yy, ww) + 2.0f;
                return yy - y0;
            });
        }

        // ── RUNNING ──────────────────────────────────────────────────────────
        {
            const bool busy = compileText_.isNotEmpty() && ! compileIsError_;
            const juce::Colour dot = compileIsError_ ? kError : (busy ? kWarning : kSuccess);
            const juce::String text = compileText_.isNotEmpty() ? compileText_
                                                                : juce::String("compiled");
            station(dot, "RUNNING", kTextDisabled, false, [&](float ww)
            {
                return paragraph(g, text, fHint,
                                 compileIsError_ ? kErrorText : (busy ? kWarning : kSuccess),
                                 static_cast<float>(textX()), y, ww);
            });
        }

        return juce::roundToInt(y - static_cast<float>(stationGap()) + 7.0f);
    }

    void relayout()
    {
        const int w = juce::jmax(1, viewport_.getMaximumVisibleWidth());
        content_.setSize(w, juce::jmax(viewport_.getHeight(), render(nullptr, w)));
        content_.repaint();
    }

    struct Content : public juce::Component,
                     public juce::SettableTooltipClient
    {
        void paint(juce::Graphics& g) override
        {
            if (owner_ != nullptr)
                owner_->render(&g, getWidth());
        }
        LcoTraceView* owner_ = nullptr;
    };

    juce::Viewport viewport_;
    Content content_;
    Trace   trace_;
    juce::String status_, placeholder_, compileText_;
    bool    compileIsError_ = false;
    float   base_ = 13.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LcoTraceView)
};
