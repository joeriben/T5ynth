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
// Idle-cheap: paints only when something is set. No animation, and the only
// timer runs between a mouse-down and the flip it may become — never at idle.
//
// TWO SIDES (BJ 2026-07-24, step 2). The front is the trace above. The back is
// the Csound the author actually wrote, and ONLY that — not the host scaffold
// wrapped around it, which nobody wrote and nobody can change. Click and hold
// anywhere on the card turns it over, and again to come back.
class LcoTraceView : public juce::Component,
                     public juce::SettableTooltipClient,
                     private juce::Timer
{
public:
    /** What is known about the orchestra now in the engine. `Unknown` is a real
     *  and necessary state, not a placeholder for Ok: a recalled preset, or a
     *  compile window abandoned because the engine left Csound mode, genuinely
     *  did not observe a compile — and saying "compiled" there would be the
     *  panel asserting something nobody checked. */
    enum class CompileState { Unknown, Compiling, Ok, Error };

    /** One authored orchestra's trace, as it came off the wire. */
    struct Trace
    {
        juce::String prompt;      // what was authored; empty = not known (preset without one)
        juce::String model;       // the model that actually wrote it
        juce::String reading;     // the author's own READING line
        juce::String thinking;    // its reasoning before the code, verbatim
        // The consultation. `consultationKnown` is what separates "this prompt
        // reached nothing in the library" (a finding, drawn as such) from "no
        // consultation was recorded for this orchestra" (a recall, where the
        // station is left OUT). Without the flag the two are the same empty
        // list, and every preset load would accuse its own prompt of being
        // unknown vocabulary.
        bool consultationKnown = false;
        juce::StringArray reachedInstruments, reachedAdjectives, reachedMotions;
        juce::StringArray orientedBy;   // shown to the author, NOT reached by the prompt
        juce::StringArray reachedNotShown;  // reached, but past the author's quote limit
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
        blink_.owner_   = this;
        // Takes its own mouse, exactly as the text box it replaced did: the
        // scrollbar needs drags, and the card flip lands here.
        setInterceptsMouseClicks(true, true);
    }

    /** Rule 1 of the project's JUCE safety list: stop the timer BEFORE any
     *  member is destroyed. This one is only ever running mid-press, so the
     *  window is small — which is exactly the kind that gets missed. */
    ~LcoTraceView() override { stopTimer(); blink_.stopTimer(); }

    /** The panel's responsive base font — same unit every other LCO widget uses. */
    void setBaseFont(float f)
    {
        if (juce::approximatelyEqual(f, base_))
            return;
        base_ = f;
        relayout();
    }

    /** Show a trace. Clears any status line: a trace IS the state.
     *
     *  It also resets the compile state to Unknown. A NEW orchestra has not been
     *  compiled yet by definition, and carrying the previous one's result over
     *  would caption it with a verdict on a different sound — including the
     *  permanent case where the old verdict was an error and nothing reopens a
     *  compile window to correct it. */
    void setTrace(Trace t)
    {
        trace_  = std::move(t);
        trace_.valid = true;
        status_.clear();
        busy_ = false;
        blink_.stopTimer();
        // What streamed came from whichever attempt was running; the trace
        // carries the one that compiled. Keeping both would show the reasoning
        // twice, once out of date.
        live_.clear();
        liveAttempt_ = 0;
        compile_ = CompileState::Unknown;
        compileDetail_.clear();
        turnToFront();
        relayout();
    }

    /** Dim single-line state, shown INSTEAD of the trace — "authoring...",
     *  "prompt is empty", an authoring failure. Replacing the trace is the
     *  point: the previous trace describes a sound that is being superseded or
     *  an attempt that failed, and leaving it up would let it read as current.
     *  (This mirrors what the text box it replaced did.) */
    /** @param busy  something is RUNNING and the user is waiting for it. The
     *               line gets a pulsing dot, so "nothing has happened yet" and
     *               "nothing is going to happen" stop looking alike — which for
     *               an authoring that takes a 12B generation is most of the
     *               time the panel is on screen. Terminal states (an empty
     *               prompt, a failure) pass false and stay still. */
    void setStatus(const juce::String& text, bool busy = false)
    {
        busy_ = busy;
        if (busy) blink_.startTimerHz(12);
        else      blink_.stopTimer();
        status_ = text;
        trace_ = {};
        live_.clear();
        liveAttempt_ = 0;
        compile_ = CompileState::Unknown;
        compileDetail_.clear();
        turnToFront();
        relayout();
    }

    /** The author's reasoning WHILE it is being written, above the status line
     *  and before any other station exists — it is the first thing the machine
     *  produces, so it is the first thing shown.
     *
     *  `attempt` counts the author's tries: a repair sends it back to the start
     *  and it reasons again, so the text does not continue, it REPLACES. Without
     *  that number the second attempt's first line would append to the first
     *  attempt's last one and read as one train of thought.
     *
     *  Ignored once a trace has arrived: what streamed is provisional, and only
     *  the attempt that actually compiled is allowed to caption the sound. */
    void setLiveThinking(int attempt, const juce::String& text)
    {
        if (trace_.valid || (text == live_ && attempt == liveAttempt_))
            return;
        liveAttempt_ = attempt;
        live_ = text;
        // Held, not drawn, while the code is up. Laying out the back again would
        // re-shape every line of Csound through the text shaper and repaint
        // content that provably did not change — once per streamed line, for
        // the length of a 12B generation. It is on screen the moment the card
        // turns back.
        if (! showBack_)
            relayout();
    }

    /** The RUNNING station: the compile window's own report. Kept separate from
     *  setStatus so a compile result never wipes the trace of the orchestra it
     *  is reporting on. `detail` carries the compiler's error text for Error. */
    void setCompileState(CompileState s, const juce::String& detail = {})
    {
        // Early-out on an unchanged state. pollCsoundCompile writes "compiling"
        // on EVERY 10 Hz tick for the whole compile window, and relayout() is not
        // cheap here: it builds a juce::TextLayout for the prompt, the entire
        // (deliberately uncapped) thinking, the reading and every repair, then
        // repaints, which shapes all of it a second time. The juce::Label this
        // replaced early-outed on unchanged text, so without this the panel would
        // pay two full text-shaping passes ten times a second for nothing.
        if (s == compile_ && detail == compileDetail_)
            return;
        compile_       = s;
        compileDetail_ = detail;
        relayout();
    }

    /** Placeholder for the empty panel, before anything has been authored. */
    void setPlaceholder(juce::String text) { placeholder_ = std::move(text); relayout(); }

    /** The back of the card: the Csound the author wrote, verbatim.
     *
     *  The BODY, not the orchestra. The scaffold around it — sr, ksmps, the
     *  sixteen channel reads, the score — is the host's, identical in every
     *  patch, and nobody authored it; printing it would bury the six lines that
     *  are actually this sound under sixty that are not. */
    void setBody(const juce::String& csound)
    {
        if (csound == body_)
            return;
        body_ = csound;
        if (showBack_)
            relayout();
    }

    /** Which side is up. Set by the click-and-hold, and readable so the panel
     *  around it can keep its own caption in step. */
    bool isShowingBody() const noexcept { return showBack_; }

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

    // ── Click and hold turns the card over ───────────────────────────────────
    // A press, not a click: a click belongs to the scrollbar and to whatever
    // selection the back grows next, and taking it here would make the card
    // flip every time the user tried to use it. The timer exists only between
    // the press and the flip, and a drag of more than a few pixels cancels it —
    // that is someone scrolling, not someone turning the page.
    // Plain left press only. A right press is what a context menu wants — and
    // step 3 puts a text field on the back, which will want one — and on macOS
    // ctrl+left arrives as a right press, so both would otherwise turn the card
    // over instead.
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isLeftButtonDown() && ! e.mods.isAnyModifierKeyDown())
            startTimer(kHoldMs);
    }
    void mouseDrag(const juce::MouseEvent& e) override      { if (e.getDistanceFromDragStart() > 6) stopTimer(); }
    void mouseUp(const juce::MouseEvent&) override          { stopTimer(); }

private:
    static constexpr int kHoldMs = 400;

    /** Bring the trace back up, without a relayout — the caller is about to.
     *
     *  Called wherever a state arrives that the back cannot show. This view is
     *  the ONLY visible status channel in LCO mode: dcoStatusLabel is never laid
     *  out and dcoFlagsLabel is given an empty rectangle there. So a status that
     *  landed while the code was up would not merely be late, it would never
     *  appear — an authoring that failed while the card was turned over would be
     *  invisible for good. */
    void turnToFront()
    {
        if (! showBack_)
            return;
        showBack_ = false;
        viewport_.setScrollBarsShown(true, false);
        viewport_.setViewPosition(0, 0);
    }

    void timerCallback() override
    {
        stopTimer();
        showBack_ = ! showBack_;
        // The two sides scroll independently in one viewport, so an arrival at
        // the bottom of a long trace would otherwise open the code halfway down.
        viewport_.setViewPosition(0, 0);
        // Code does not wrap — a wrapped Csound line is a different line. The
        // back scrolls sideways instead; the front never needs to.
        viewport_.setScrollBarsShown(true, showBack_);
        relayout();
        if (onFlip)
            onFlip(showBack_);
    }

public:
    /** Called after a flip, with the side now up. */
    std::function<void(bool)> onFlip;

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
    // The rubric never wraps and never drives the layout height (the station
    // reserves a fixed labelH), so there is no width-measuring counterpart.
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

    /** The BACK: the authored body, monospaced and unwrapped.
     *
     *  Csound is line-structured — one opcode, one line — so wrapping would
     *  print lines the author never wrote. In a column this narrow that means
     *  the card scrolls sideways, which is why `width` is in/out: it comes in as
     *  the viewport's width and goes out as the longest line, so there is
     *  something to scroll to.
     *
     *  Trailing `; ...` is drawn apart from the code. The library's idioms carry
     *  their reasoning in exactly those comments ("mode 1 @ x1.00, Q 900"), and
     *  they are the author talking ABOUT the code rather than the code — the
     *  same distinction the front of the card is built on. */
    int renderBody(juce::Graphics* g, int& width) const
    {
        const float x = 8.0f;
        const float wrapW = static_cast<float>(width) - x - 11.0f;
        const juce::Font fLabel { juce::FontOptions(labelFont()) };
        const juce::Font fHint  { juce::FontOptions(hintFont())  };
        const float labelH = std::round(labelFont() * 1.5f);
        float top = 7.0f;

        // Which side is up, and how to leave it. Nothing else here says so, and
        // the way back is a gesture rather than a control.
        if (g != nullptr)
        {
            g->setColour(kTextDisabled);
            drawTrackedText(*g, fLabel, juce::String::fromUTF8("CSOUND \xc2\xb7 HOLD TO TURN BACK"),
                            x, top, labelH, juce::jmax(0.8f, labelFont() * 0.16f));
        }
        top += labelH + 3.0f;

        // The compiler's complaint belongs on the page being edited. It quotes
        // the offending line of the BODY, which is what is on screen here — on
        // the front it would be a verdict about code the reader cannot see.
        if (compile_ == CompileState::Error && compileDetail_.isNotEmpty())
            top += paragraph(g, compileDetail_, fHint, kErrorText, x, top, wrapW) + 5.0f;

        if (body_.trim().isEmpty())
        {
            const juce::Font fb { juce::FontOptions(bodyFont()) };
            return juce::roundToInt(top + paragraph(g, "nothing has been written yet",
                                                    fb, kDim, x, top, wrapW) + 7.0f);
        }

        const juce::Font f { juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                               juce::jmax(9.5f, base_ * 0.86f),
                                               juce::Font::plain) };
        const float lineH = std::round(f.getHeight() * 1.30f);
        float y = top, widest = 0.0f;
        juce::StringArray lines;
        lines.addLines(body_);
        if (g != nullptr)
            g->setFont(f);
        for (const auto& ln : lines)
        {
            widest = juce::jmax(widest, juce::GlyphArrangement::getStringWidth(f, ln));
            if (g != nullptr && ln.isNotEmpty())
            {
                const int c = commentStart(ln);
                const auto code = c < 0 ? ln : ln.substring(0, c);
                if (code.isNotEmpty())
                {
                    g->setColour(kTextSecondary);
                    g->drawText(code, juce::Rectangle<float>(x, y, widest + 400.0f, lineH),
                                juce::Justification::centredLeft, false);
                }
                if (c >= 0)
                {
                    g->setColour(kTextDisabled);
                    g->drawText(ln.substring(c),
                                juce::Rectangle<float>(x + juce::GlyphArrangement::getStringWidth(f, code),
                                                       y, widest + 400.0f, lineH),
                                juce::Justification::centredLeft, false);
                }
            }
            y += lineH;
        }
        width = juce::jmax(width, juce::roundToInt(x + widest + 11.0f));
        return juce::roundToInt(y + 7.0f);
    }

    /** Index of the `;` that starts a trailing comment, or -1. A semicolon
     *  inside a string literal (`sprintf "gate%d; ..."`) is not one, so the
     *  quotes before it have to balance. */
    static int commentStart(const juce::String& line)
    {
        bool inString = false;
        for (int i = 0; i < line.length(); ++i)
        {
            const auto ch = line[i];
            if (ch == '"')            inString = ! inString;
            else if (ch == ';' && ! inString) return i;
        }
        return -1;
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

        // No trace yet: the status line, and under it the reasoning as it
        // arrives. The author thinks before it writes, so this is on screen
        // while the orchestra is still being authored — the panel is not empty
        // for the length of a 12B generation, and what fills it is the machine's
        // own words rather than a spinner.
        if (! trace_.valid)
        {
            const auto text = status_.isNotEmpty() ? status_ : placeholder_;
            // A dot on the rail that breathes while something is running. The
            // authoring takes a whole 12B generation, and without it a panel
            // that is working and a panel that has given up look the same.
            if (busy_ && g != nullptr)
            {
                const float phase = std::sin(static_cast<float>(juce::Time::getMillisecondCounter() % 1200)
                                             / 1200.0f * juce::MathConstants<float>::twoPi);
                const float cy = y + bodyFont() * 0.62f;
                g->setColour(kWarning.withAlpha(0.45f + 0.55f * (0.5f + 0.5f * phase)));
                g->fillEllipse(static_cast<float>(railX() - dotR()), cy - static_cast<float>(dotR()),
                               static_cast<float>(dotR() * 2), static_cast<float>(dotR() * 2));
            }
            y += paragraph(g, text, fBody, busy_ ? kTextPrimary : kDim,
                           static_cast<float>(textX()), y, w);
            if (live_.isNotEmpty())
            {
                y += static_cast<float>(stationGap());
                station(kWarning, "THINKING", kTextDisabled, false, [&](float ww)
                {
                    return paragraph(g, live_, fHint, kDim,
                                     static_cast<float>(textX()), y, ww);
                });
                return juce::roundToInt(y - static_cast<float>(stationGap()) + 7.0f);
            }
            return juce::roundToInt(y + 7.0f);
        }

        // ── HEARD ────────────────────────────────────────────────────────────
        // Omitted when the prompt is not known. A Csound-only preset stores no
        // prompt, and captioning the restored orchestra with whatever happens to
        // be in the editor would name a prompt that never authored it.
        if (trace_.prompt.isNotEmpty())
            station(kImpulseA, "HEARD", kTextDisabled, true, [&](float ww)
            {
                return paragraph(g, trace_.prompt, fBody, kImpulseAText,
                                 static_cast<float>(textX()), y, ww);
            });

        // ── LOOKED UP ────────────────────────────────────────────────────────
        // Reached first, then — visibly weaker and separately captioned — what
        // the author was shown anyway. The count line names both.
        //
        // Drawn ONLY when a consultation was actually recorded. A recalled
        // orchestra has none (no preset stores one), and an empty list there
        // would print "no word of this prompt is in the library" over a prompt
        // that may be nothing but library vocabulary — the precise false claim
        // this whole surface exists to avoid.
        if (trace_.consultationKnown)
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
                yy += chips(g, trace_.orientedBy, static_cast<float>(textX()), yy, ww, true) + 3.0f;
            }
            // Reached, but past the author's quote limit — so the words landed
            // and the entries still never reached the author. Naming them is the
            // difference between a full report and a flattering one.
            if (! trace_.reachedNotShown.isEmpty())
            {
                yy += paragraph(g, "reached, but not quoted to the author:", fHint, kWarning,
                                static_cast<float>(textX()), yy, ww) + 1.0f;
                yy += chips(g, trace_.reachedNotShown, static_cast<float>(textX()), yy, ww, true);
            }
            return yy - y0;
        });

        // ── THOUGHT ──────────────────────────────────────────────────────────
        // The author's reasoning in its own words, verbatim and in full: what it
        // decided the sound IS, what excites and resonates in it, what moves.
        // Quoted, never summarised — a summary would be this panel's account of
        // the machine, and the whole point is the machine's own.
        //
        // Absent when the reply carried no prose outside the fence, and on every
        // recall (no preset stores it). Never fabricated from the code.
        if (trace_.thinking.isNotEmpty())
            station(kImpulseA, "THOUGHT", kTextDisabled, true, [&](float ww)
            {
                return paragraph(g, trace_.thinking, fHint, kDim,
                                 static_cast<float>(textX()), y, ww);
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
        // Four states, and Unknown is not a synonym for Ok: an orchestra whose
        // compile nobody watched (a recalled preset; a window abandoned because
        // the engine left Csound mode mid-compile) must say so rather than
        // report a success it never saw.
        {
            juce::Colour dot = kTextDisabled, ink = kDimmer;
            juce::String text = "compile not observed";
            switch (compile_)
            {
                case CompileState::Compiling: dot = kWarning; ink = kWarning;   text = "compiling..."; break;
                case CompileState::Ok:        dot = kSuccess; ink = kSuccess;   text = "compiled";     break;
                case CompileState::Error:     dot = kError;   ink = kErrorText;
                    text = compileDetail_.isNotEmpty() ? compileDetail_
                                                       : juce::String("the orchestra did not compile");
                    break;
                case CompileState::Unknown:   break;
            }
            station(dot, "RUNNING", kTextDisabled, false, [&](float ww)
            {
                return paragraph(g, text, fHint, ink, static_cast<float>(textX()), y, ww);
            });
        }

        return juce::roundToInt(y - static_cast<float>(stationGap()) + 7.0f);
    }

    void relayout()
    {
        int w = juce::jmax(1, viewport_.getMaximumVisibleWidth());
        const int h = showBack_ ? renderBody(nullptr, w)   // widens w to the longest line
                                : render(nullptr, w);
        // getMaximumVisibleHeight, not getHeight: with the back's horizontal
        // scrollbar shown, content exactly getHeight() tall is 7px taller than
        // the visible area, and the viewport answers with a vertical scrollbar
        // whose whole range is the other bar's thickness.
        content_.setSize(w, juce::jmax(viewport_.getMaximumVisibleHeight(), h));
        content_.repaint();
    }

    struct Content : public juce::Component,
                     public juce::SettableTooltipClient
    {
        void paint(juce::Graphics& g) override
        {
            if (owner_ == nullptr)
                return;
            int w = getWidth();
            if (owner_->showBack_) owner_->renderBody(&g, w);
            else                   owner_->render(&g, w);
        }
        // The scrolled content sits on top, so the press lands HERE. JUCE does
        // not pass mouse events up to a parent, so without this the card could
        // only be flipped by hitting the 1px margin around the viewport.
        void mouseDown(const juce::MouseEvent& e) override { if (owner_) owner_->mouseDown(e); }
        void mouseDrag(const juce::MouseEvent& e) override { if (owner_) owner_->mouseDrag(e); }
        void mouseUp  (const juce::MouseEvent& e) override { if (owner_) owner_->mouseUp(e); }
        LcoTraceView* owner_ = nullptr;
    };

    // content_ AFTER viewport_ is deliberate and safe: Viewport holds its viewed
    // component through a WeakReference with deleteContent == false, so content_
    // being destroyed first simply nulls that reference and ~Viewport no-ops.
    juce::Viewport viewport_;
    Content content_;
    Trace   trace_;
    juce::String status_, placeholder_, compileDetail_;
    juce::String live_;             // the reasoning as it streams; empty once traced
    int          liveAttempt_ = 0;
    juce::String body_;             // the back of the card: the authored Csound
    bool         showBack_ = false;
    bool         busy_ = false;

    /** Repaints the status dot while something is running, and NOTHING else —
     *  only the rail's top corner, so a whole trace is not re-shaped twelve
     *  times a second. Runs strictly between a busy status and the state that
     *  ends it; there is no idle case. */
    struct Blink : public juce::Timer
    {
        ~Blink() override { stopTimer(); }
        void timerCallback() override
        {
            if (owner_ != nullptr)
                owner_->content_.repaint(0, 0, owner_->textX(),
                                         juce::roundToInt(owner_->bodyFont() * 1.6f));
        }
        LcoTraceView* owner_ = nullptr;
    };
    Blink blink_;
    CompileState compile_ = CompileState::Unknown;
    float   base_ = 13.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LcoTraceView)
};
