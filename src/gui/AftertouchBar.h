#pragma once
#include <JuceHeader.h>
#include "GuiHelpers.h"
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// AftertouchBar — one bipolar aftertouch depth bar (combined select + amount).
//
// Interaction:
//   • drag horizontally → set the target's depth (magnitude), filling from left
//   • plain click (no drag) → toggle the sign (positive ↔ negative)
//   • double-click → off (0)
//   • right-click → MIDI-learn menu (onRightClick)
//   • the numeric value shows ONLY while the mouse is held
//
// Sign rides on a persistent +/- suffix on the label (greyscale / colour-blind
// safe); the fill colour — orange (kAtCol) positive, dark red-amber (kAtNegCol)
// negative — is a redundant cue. Magnitude uses the full bar width, so depth
// keeps full resolution regardless of sign.
//
// Backed by a juce::Slider so an APVTS SliderAttachment drives our value (range
// −1..+1); we override the mouse for the combined feel and paint the fill
// ourselves. onDragStart/onDragEnd bracket a host automation gesture — the
// attachment can't, because we bypass the stock drag handling.
// ─────────────────────────────────────────────────────────────────────────────
class AftertouchBar : public juce::Slider
{
public:
    AftertouchBar()
    {
        setSliderStyle(juce::Slider::LinearBar);
        setRange(-1.0, 1.0, 0.0);
        setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        setVelocityBasedMode(false);
    }

    void setTargetLabel(juce::String s) { label_ = std::move(s); }

    std::function<void()> onDragStart, onDragEnd;
    std::function<void(juce::Point<int>)> onRightClick;

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            if (onRightClick) onRightClick(e.getPosition());
            return;
        }

        held_ = true;
        if (onDragStart) onDragStart();

        if (e.getNumberOfClicks() >= 2)
        {
            doubleClick_ = true;
            setValue(0.0, juce::sendNotificationSync);   // double-click → off
        }
        else
        {
            doubleClick_ = false;
            dragSign_ = (getValue() < 0.0) ? -1.0 : 1.0; // 0 defaults to positive
        }
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (! held_ || doubleClick_)
            return;

        const float w = (float) juce::jmax(1, getWidth());
        const double pos = juce::jlimit(0.0, 1.0, (double) e.position.x / w);
        // Honest linear control: the bar position IS the depth. The DSP full-scales
        // are calibrated so the whole travel is usable (no skew to compensate a
        // too-hot function, and therefore no min-fill "jump" near zero).
        double mag = pos;
        if (mag < 0.01) mag = 0.0;                       // snap to clean off (matches 0.01 step)
        setValue(dragSign_ * mag, juce::sendNotificationSync);
        repaint();
    }

    // We handle reset-to-off ourselves (double-click in mouseDown); suppress the
    // base Slider's double-click so it can't pop a value editor.
    void mouseDoubleClick(const juce::MouseEvent&) override {}

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (! held_)
            return;

        // A click with no drag toggles the sign of the current depth.
        if (! doubleClick_ && e.getNumberOfClicks() == 1
            && ! e.mouseWasDraggedSinceMouseDown())
            setValue(-getValue(), juce::sendNotificationSync);

        held_ = false;
        if (onDragEnd) onDragEnd();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float v   = (float) getValue();
        const float mag = std::abs(v);

        g.setColour(kSurface);
        g.fillRect(b);

        if (mag > 0.0f)
        {
            g.setColour(v >= 0.0f ? kAtCol : kAtNegCol);
            // Linear fill = depth (drag position). No skew → no min-fill jump.
            g.fillRect(b.withWidth(b.getWidth() * mag));
        }

        const bool active = mag > 0.0f;
        // Persistent sign suffix ("Reso +" / "Reso −") so polarity reads without
        // relying on the fill hue — works in greyscale and for colour-blind users.
        juce::String lab = " " + label_;
        if (active)
            lab += (v >= 0.0f) ? " +"
                               : " " + juce::String(juce::CharPointer_UTF8("\xE2\x88\x92"));
        g.setColour(active ? juce::Colours::white : kTextMuted);
        g.setFont(juce::FontOptions(juce::jlimit(9.0f, 13.0f, b.getHeight() * 0.58f)));
        g.drawText(lab, b.reduced(2.0f, 0.0f),
                   juce::Justification::centredLeft, false);

        if (held_)
        {
            g.setColour(juce::Colours::white);
            g.drawText((v >= 0.0f ? "+" : "") + juce::String(v, 2) + " ",
                       b.reduced(2.0f, 0.0f), juce::Justification::centredRight, false);
        }

        g.setColour(kBorder);
        g.drawRect(b, 1.0f);
    }

private:
    juce::String label_;
    double dragSign_    = 1.0;
    bool   held_        = false;
    bool   doubleClick_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AftertouchBar)
};
