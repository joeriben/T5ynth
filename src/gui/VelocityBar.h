#pragma once
#include <JuceHeader.h>
#include "GuiHelpers.h"
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// VelocityBar — one vertical drag-fill bar for the "Velocity Amount" box.
//
// Shares the AftertouchBar feel (drag = magnitude, sign as a persistent label
// suffix), but VERTICAL (fills from the bottom) and dual-mode:
//
//   • bipolar  (range −1..+1): the velocity→TIME stages (Att/Dec/Rel velSens).
//       drag up → magnitude;  plain click → toggle sign;  double-click → 0.
//       sign rides as a "+/−" suffix on the bottom label (greyscale-safe), with
//       the fill hue (orange / dark red-amber) as a redundant cue.
//   • unipolar (range 0..1):   the global velocity→peak amount (Level / velAmt).
//       drag up → value;  double-click → 0;  no sign.
//
// Backed by a juce::Slider so an APVTS SliderAttachment drives the value; we
// override the mouse for the combined feel and paint the fill + bottom label
// ourselves. onDragStart/onDragEnd bracket a host automation gesture (the
// attachment can't, because we bypass the stock drag handling). The numeric
// value shows ONLY while the mouse is held, mirroring AftertouchBar.
// ─────────────────────────────────────────────────────────────────────────────
class VelocityBar : public juce::Slider
{
public:
    explicit VelocityBar(bool bipolar) : bipolar_(bipolar)
    {
        setSliderStyle(juce::Slider::LinearBarVertical);
        setRange(bipolar_ ? -1.0 : 0.0, 1.0, 0.0);
        setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        setVelocityBasedMode(false);
    }

    void setBarLabel(juce::String s) { label_ = std::move(s); }

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
            setValue(0.0, juce::sendNotificationSync);       // double-click → off
        }
        else
        {
            doubleClick_ = false;
            dragSign_ = (getValue() < 0.0) ? -1.0 : 1.0;     // 0 defaults to positive
            // Unipolar (Level): jump-to-position on press. Bipolar: do NOT — a
            // plain click must toggle the sign (mouseUp) while PRESERVING the
            // magnitude; only a real drag (mouseDrag) changes it. Mirrors AftertouchBar.
            if (! bipolar_) applyDrag(e);
        }
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (! held_ || doubleClick_)
            return;
        applyDrag(e);
        repaint();
    }

    // We handle reset-to-off ourselves (double-click in mouseDown); suppress the
    // base Slider's double-click so it can't pop a value editor.
    void mouseDoubleClick(const juce::MouseEvent&) override {}

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (! held_)
            return;

        // Bipolar only: a click with no drag toggles the sign of the current depth.
        if (bipolar_ && ! doubleClick_ && e.getNumberOfClicks() == 1
            && ! e.mouseWasDraggedSinceMouseDown())
            setValue(-getValue(), juce::sendNotificationSync);

        held_ = false;
        if (onDragEnd) onDragEnd();
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float labelH = juce::jlimit(12.0f, 16.0f, b.getHeight() * 0.18f);
        auto labelArea = b.removeFromBottom(labelH);   // b is now the fill area
        const float v   = (float) getValue();
        const float mag = std::abs(v);

        g.setColour(kSurface);
        g.fillRect(b);

        if (mag > 0.0f)
        {
            g.setColour(v >= 0.0f ? kAtCol : kAtNegCol);
            // Fill from the bottom up; bar position IS the depth (no skew).
            g.fillRect(b.withTop(b.getBottom() - b.getHeight() * mag));
        }

        const bool active = mag > 0.0f;
        // Persistent label + sign suffix ("Att +" / "Att −"); bipolar only — the
        // unipolar Level bar never shows a sign. Greyscale/colour-blind safe.
        juce::String lab = label_;
        if (bipolar_ && active)
            lab += (v >= 0.0f) ? " +"
                               : " " + juce::String(juce::CharPointer_UTF8("\xE2\x88\x92"));
        g.setColour(active ? juce::Colours::white : kTextMuted);
        g.setFont(juce::FontOptions(juce::jlimit(9.0f, 12.0f, labelArea.getHeight() * 0.78f)));
        g.drawText(lab, labelArea, juce::Justification::centred, false);

        if (held_)
        {
            g.setColour(juce::Colours::white);
            g.setFont(juce::FontOptions(juce::jlimit(9.0f, 12.0f, b.getHeight() * 0.10f)));
            const juce::String num = (bipolar_ && v >= 0.0f ? "+" : "") + juce::String(v, 2);
            g.drawText(num, b.reduced(1.0f, 2.0f), juce::Justification::centredTop, false);
        }

        g.setColour(kBorder);
        g.drawRect(getLocalBounds().toFloat(), 1.0f);
    }

private:
    void applyDrag(const juce::MouseEvent& e)
    {
        const float labelH = juce::jlimit(12.0f, 16.0f, (float) getHeight() * 0.18f);
        const float fillH  = juce::jmax(1.0f, (float) getHeight() - labelH);
        // Top of the fill area = max, bottom = 0.
        const double pos = juce::jlimit(0.0, 1.0, 1.0 - (double) e.position.y / fillH);
        double mag = pos;
        if (mag < 0.01) mag = 0.0;                            // snap to clean off
        setValue((bipolar_ ? dragSign_ : 1.0) * mag, juce::sendNotificationSync);
    }

    juce::String label_;
    bool   bipolar_     = true;
    double dragSign_    = 1.0;
    bool   held_        = false;
    bool   doubleClick_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VelocityBar)
};
