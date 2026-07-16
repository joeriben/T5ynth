#pragma once
#include <JuceHeader.h>
#include "../sequencer/GenerativeSequencer.h"

/**
 * GenSeqCRTMonitor — a small "core memory monitor" that makes the generative
 * sequencer's per-cycle decisions legible. A CRT-styled phosphor readout that
 * shows which mutation the engine just performed on strand 0 (rotation /
 * pulse add-remove / steps change / Turing note jump), the rule behind it, and
 * the live pattern as a ring + a memory bar. In Dialog coordination mode it
 * also narrates stance re-decisions (which strand takes which stance toward
 * S1, and the pulse-drift value that decided it).
 *
 * It reads the sequencer's lock-free GUI atomics and is repaint-gated: poll()
 * is driven by SequencerPanel's existing 10 Hz timer and only triggers a
 * repaint when a new event arrives (generation counter changed) or the pattern
 * changes — so it costs nothing while the engine is idle. The whole layout
 * scales proportionally to the component's bounds.
 *
 * Labels are English-only and self-explanatory by design — no German, no
 * cryptic codes. The structure carries the meaning: gate = timing rule,
 * gap = placement rule, random = the one rule-less op (pitch).
 */
class GenSeqCRTMonitor : public juce::Component
{
public:
    explicit GenSeqCRTMonitor (T5ynthGenerativeSequencer& seq) : seq_ (seq)
    {
        setInterceptsMouseClicks (false, false);   // pure readout, never eats clicks
    }

    /** Poll the lock-free atomics; repaint only on a genuine change. Called
        from SequencerPanel's 10 Hz timer while in GEN mode. */
    void poll()
    {
        bool dirty = false;

        const auto e = T5ynthGenerativeSequencer::unpackMutEvent (
            seq_.mutationEventForGui.load (std::memory_order_acquire));
        if (e.gen != lastGen_) { lastGen_ = e.gen; ev_ = e; stanceLatest_ = false; dirty = true; }

        // Dialog stance changes are far rarer than mutation events (phrase
        // ends vs. every cycle), so when both arrive in one poll the stance
        // wins the text lines; the next mutation event reclaims them.
        const auto se = T5ynthGenerativeSequencer::unpackStanceEvent (
            seq_.stanceEventForGui.load (std::memory_order_acquire));
        if (se.gen != lastStanceGen_)
        {
            lastStanceGen_ = se.gen;
            if (se.gen != 0) { sev_ = se; stanceLatest_ = true; dirty = true; }
        }

        const auto pat = seq_.eucPatternForGui.load (std::memory_order_relaxed);
        const int  ns  = seq_.numStepsForGui.load  (std::memory_order_relaxed);
        if (pat != pattern_ || ns != numSteps_) { pattern_ = pat; numSteps_ = ns; dirty = true; }

        if (dirty) repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float w = b.getWidth(), h = b.getHeight();
        if (w < 8.0f || h < 8.0f) return;

        g.setColour (cBg);     g.fillRoundedRectangle (b, 4.0f);
        g.setColour (cBorder); g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);

        // scanlines — cheap, and only drawn on the rare event-driven repaint
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        for (float y = b.getY(); y < b.getBottom(); y += 3.0f)
            g.fillRect (b.getX(), y, w, 1.0f);

        // Green = rule-derived (all Dialog stances are), amber = the one
        // rule-less op (Turing random pitch).
        const bool amberEv = ! stanceLatest_
            && (ev_.op == (int) T5ynthGenerativeSequencer::MutEvent::NoteJump);
        accent_ = amberEv ? cAmber : cGreen;

        auto inner = b.reduced (w * 0.03f, h * 0.06f);

        const float memH = juce::jmax (4.0f, h * 0.075f);
        auto memBar = inner.removeFromBottom (memH);
        inner.removeFromBottom (h * 0.04f);

        auto main = inner;
        const float scopeSize = juce::jmin (main.getHeight(), w * 0.42f);
        auto scopeRect = main.removeFromLeft (scopeSize);
        main.removeFromLeft (w * 0.03f);

        drawScope  (g, scopeRect);
        drawText   (g, main, amberEv);
        drawMemBar (g, memBar);
    }

private:
    using ME = T5ynthGenerativeSequencer::MutEvent;

    static juce::String noteName (int n)   // matches SequencerPanel::noteName (middle C = C4)
    {
        static const char* nm[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
        n = juce::jlimit (0, 127, n);
        return juce::String (nm[n % 12]) + juce::String (n / 12 - 1);
    }

    juce::Point<float> ringPt (int i, int N, float cx, float cy, float rad) const
    {
        const float a = juce::degreesToRadians (-90.0f + 360.0f * (float) i / (float) N);
        return { cx + rad * std::cos (a), cy + rad * std::sin (a) };
    }

    void drawScope (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const int   N   = juce::jlimit (1, 32, numSteps_ > 0 ? numSteps_ : 1);
        const float cx  = r.getCentreX(), cy = r.getCentreY();
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.40f;
        if (rad < 4.0f) return;

        g.setColour (cFaint.withAlpha (0.5f));
        g.drawEllipse (cx - rad * 1.16f, cy - rad * 1.16f, rad * 2.32f, rad * 2.32f, 1.0f);

        // fixed origin marker at 12 o'clock
        juce::Path tri; const float t = rad * 0.13f;
        tri.addTriangle (cx - t, cy - rad * 1.30f, cx + t, cy - rad * 1.30f, cx, cy - rad * 1.06f);
        g.setColour (cGreenDim); g.fillPath (tri);

        juce::Array<int> on;
        for (int i = 0; i < N; ++i) if ((pattern_ >> i) & 1u) on.add (i);

        if (on.size() > 1)
        {
            juce::Path poly;
            poly.startNewSubPath (ringPt (on[0], N, cx, cy, rad));
            for (int k = 1; k < on.size(); ++k) poly.lineTo (ringPt (on[k], N, cx, cy, rad));
            poly.closeSubPath();
            g.setColour (cGreenDim);
            g.strokePath (poly, juce::PathStrokeType (juce::jmax (2.0f, rad * 0.07f),
                          juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // The longest gap, now bridged by the new pulse: highlight the two
        // necklace edges from the inserted node to its neighbours (a = gap-start
        // pulse, b = gap length → closing pulse at a+b+1). Following the ring
        // reads as "this gap was filled" instead of a chord across the centre.
        if (ev_.op == (int) ME::PulseAdd && N > 1)
        {
            auto pIns  = ringPt (ev_.step % N, N, cx, cy, rad);
            auto pPrev = ringPt (ev_.a % N, N, cx, cy, rad);
            auto pNext = ringPt ((ev_.a + ev_.b + 1) % N, N, cx, cy, rad);
            const float th = juce::jmax (2.5f, rad * 0.09f);
            g.setColour (accent_);
            g.drawLine ({ pPrev, pIns }, th);
            g.drawLine ({ pIns, pNext }, th);
        }

        const bool nodeFlash = (ev_.op == (int) ME::PulseAdd || ev_.op == (int) ME::NoteJump);
        for (int i = 0; i < N; ++i)
        {
            auto p = ringPt (i, N, cx, cy, rad);
            if ((pattern_ >> i) & 1u)
            {
                const bool fl = nodeFlash && ev_.step == i;
                const float dr = fl ? rad * 0.16f : rad * 0.11f;
                g.setColour (fl ? accent_ : cGreen);
                g.fillEllipse (p.x - dr, p.y - dr, dr * 2.0f, dr * 2.0f);
            }
            else
            {
                const float dr = juce::jmax (1.0f, rad * 0.035f);
                g.setColour (cFaint);
                g.fillEllipse (p.x - dr, p.y - dr, dr * 2.0f, dr * 2.0f);
            }
        }
    }

    void drawText (juce::Graphics& g, juce::Rectangle<float> area, bool amberEv)
    {
        juce::String op, res, why;
        eventStrings (op, res, why);
        if (op.isEmpty()) return;

        const float fs = juce::jlimit (10.0f, 22.0f, getHeight() * 0.135f);
        const auto mono  = juce::Font (juce::FontOptions (fs)
                                       .withName (juce::Font::getDefaultMonospacedFontName()));
        const auto monoB = mono.boldened();

        const float lineH = area.getHeight() / 3.0f;
        auto l1 = area.removeFromTop (lineH);
        auto l2 = area.removeFromTop (lineH);
        auto l3 = area;

        g.setFont (monoB);
        g.setColour (amberEv ? cAmber : cGreen);
        g.drawText (op, l1, juce::Justification::centredLeft, false);
        g.setColour (amberEv ? cAmberRes : cResGreen);
        g.drawText (res, l2, juce::Justification::centredLeft, false);
        g.setFont (mono);
        g.setColour (amberEv ? cAmberDim : cGreenDim);
        g.drawText (why, l3, juce::Justification::centredLeft, false);
    }

    void drawMemBar (juce::Graphics& g, juce::Rectangle<float> bar)
    {
        const int N = juce::jlimit (1, 32, numSteps_ > 0 ? numSteps_ : 1);
        const float gap  = bar.getWidth() > 80.0f ? 2.0f : 1.0f;
        const float cellW = juce::jmax (1.0f, (bar.getWidth() - gap * (float) (N - 1)) / (float) N);
        const bool flashStep = (ev_.op == (int) ME::PulseAdd || ev_.op == (int) ME::NoteJump);

        for (int i = 0; i < N; ++i)
        {
            juce::Rectangle<float> cell (bar.getX() + (float) i * (cellW + gap),
                                         bar.getY(), cellW, bar.getHeight());
            juce::Colour c = ((pattern_ >> i) & 1u) ? cMemOn : cMemOff;
            if (flashStep && ev_.step == i) c = accent_;
            g.setColour (c);
            g.fillRoundedRectangle (cell, 1.0f);
        }
    }

    void eventStrings (juce::String& op, juce::String& res, juce::String& why) const
    {
        static const auto L   = juce::String::fromUTF8 ("\xE2\x86\x90");  // left arrow
        static const auto R   = juce::String::fromUTF8 ("\xE2\x86\x92");  // right arrow
        static const auto ROT = juce::String::fromUTF8 ("\xE2\x86\xBB");  // clockwise arrow

        auto m2 = [] (int s) { return juce::String (s).paddedLeft ('0', 2); };

        if (stanceLatest_)
        {
            // Stance re-decision at a phrase end: WHO takes WHICH stance
            // toward S1, and the deciding value (the strand's pulse-drift
            // pendulum) — the reason, not just the result.
            using St = T5ynthGenerativeSequencer::Stance;
            const auto sn = "S" + juce::String (sev_.strand + 1);
            op  = "DIALOG";
            res = sn + ((St) sev_.stance == St::Counter ? " vs S1"
                      : (St) sev_.stance == St::Follow  ? " with S1"
                                                        : " alone");
            why = "drift " + juce::String (sev_.accum > 0 ? "+" : "")
                           + juce::String (sev_.accum);
            return;
        }

        switch ((ME) ev_.op)
        {
            case ME::Rotate:
                op = "ROTATE"; res = ROT + " +1"; why = "gate " + juce::String (ev_.a) + "/8"; break;
            case ME::PulseAdd:
                op = "PULSE +"; res = "M" + m2 (ev_.step) + " " + L + " " + noteName (ev_.newNote);
                why = "longest gap"; break;
            case ME::PulseRemove:
                op = "PULSE -"; res = "M" + m2 (ev_.step) + " " + L + " ."; why = "tightest gap"; break;
            case ME::StepsUp:
            case ME::StepsDown:
                op = "STEPS"; res = juce::String (ev_.a) + " " + R + " " + juce::String (ev_.b);
                why = "steps drift"; break;
            case ME::NoteJump:
                op = "TURING"; res = noteName (ev_.oldNote) + " " + R + " " + noteName (ev_.newNote);
                why = "random pitch"; break;
            case ME::None:
            default:
                op.clear(); res.clear(); why.clear(); break;
        }
    }

    T5ynthGenerativeSequencer& seq_;
    unsigned       lastGen_       = 0;
    unsigned       lastStanceGen_ = 0;
    bool           stanceLatest_  = false;
    std::uint32_t  pattern_  = 0;
    int            numSteps_ = 0;
    T5ynthGenerativeSequencer::MutEventData    ev_  { 0, 0, 0, 0, 0, 0, 0 };
    T5ynthGenerativeSequencer::StanceEventData sev_ { 0, 0, 0, 0 };
    juce::Colour   accent_;

    // phosphor CRT palette
    const juce::Colour cBg       { 0xff07100b };
    const juce::Colour cBorder   { 0xff16402a };
    const juce::Colour cGreen    { 0xff5fffa0 };
    const juce::Colour cGreenDim { 0xff2c6b47 };
    const juce::Colour cFaint    { 0xff1f5236 };
    const juce::Colour cResGreen { 0xffcfe9d8 };
    const juce::Colour cAmber    { 0xffffb84d };
    const juce::Colour cAmberRes { 0xfff0c98a };
    const juce::Colour cAmberDim { 0xff9a6a2a };
    const juce::Colour cMemOn    { 0xff2f7a52 };
    const juce::Colour cMemOff   { 0xff10241a };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GenSeqCRTMonitor)
};
