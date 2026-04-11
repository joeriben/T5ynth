#pragma once
#include <JuceHeader.h>
#include <functional>
#include <limits>

// ── Color constants (shared across all GUI files) ──────────────────────────
static const auto kAccent  = juce::Colour(0xffe91e63);  // C — Pink (engine accent)
static const auto kDim     = juce::Colour(0xff888888);
static const auto kDimmer  = juce::Colour(0xff606060);
static const auto kSurface = juce::Colour(0xff1e2130);  // Slider track bg, input fields
static const auto kCard    = juce::Colour(0xff1a1e2a);  // Card/section background
static const auto kBg      = juce::Colour(0xff0e1018);  // Main background (dark blue-gray, not black)
static const auto kBorder  = juce::Colour(0xff353a4a);  // Section borders (more visible)

// Semantic axis colors
static const auto kAxis1   = juce::Colour(0xffe91e63);  // Pink
static const auto kAxis2   = juce::Colour(0xff2196f3);  // Blue
static const auto kAxis3   = juce::Colour(0xff4caf50);  // Green

// PCA axis colors (muted variants)
static const auto kPca1    = juce::Colour(0xffff9800);  // Orange
static const auto kPca2    = juce::Colour(0xff9c27b0);  // Purple
static const auto kPca3    = juce::Colour(0xff00bcd4);  // Cyan
static const auto kPca4    = juce::Colour(0xffcddc39);  // Lime
static const auto kPca5    = juce::Colour(0xffff5722);  // Deep orange
static const auto kPca6    = juce::Colour(0xff8bc34a);  // Light green

// Section accent colors — UCDCAE brand palette in logo order
// U=#667eea  C=#e91e63  D=#7C4DFF  C=#FF6F00  A=#4CAF50  E=#00BCD4
static const auto kFilterCol = juce::Colour(0xff7C4DFF);  // D — Violet (filter)
static const auto kEnvCol    = juce::Colour(0xffFF6F00);  // C₂ — Amber (envelopes)
static const auto kModCol    = juce::Colour(0xffFF6F00);  // C₂ — Amber (LFOs)
static const auto kLfoCol    = juce::Colour(0xffFF6F00);  // C₂ — Amber (LFOs)
static const auto kDriftCol  = juce::Colour(0xffe65100);  // C₂ darker variant (drift)

// Module lead colors (UCDCAE: C=Engine D=Filter U=Osc C₂=Mod A=Seq E=FX)
static const auto kOscCol    = juce::Colour(0xff667eea);  // U — Periwinkle (prompt/osc)
static const auto kSeqCol    = juce::Colour(0xff4CAF50);  // A — Green (sequencer)
static const auto kFxCol     = juce::Colour(0xff00BCD4);  // E — Cyan (effects)

/** Configure a label as an inverted section header bar (colored bg, dark text). */
inline void paintSectionHeader(juce::Label& lbl, const juce::String& text, juce::Colour col)
{
    lbl.setText(" " + text, juce::dontSendNotification);
    lbl.setColour(juce::Label::textColourId, juce::Colour(0xff0e1018));
    lbl.setColour(juce::Label::backgroundColourId, col.withAlpha(0.7f));
    lbl.setJustificationType(juce::Justification::centredLeft);
}

/** Paint a card background with subtle border (sharp corners). */
inline void paintCard(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(kCard);
    g.fillRect(bounds);
    g.setColour(kBorder);
    g.drawRect(bounds, 1);
}

/** Paint a border around a switchbox button group (sharp corners). */
inline void paintSwitchBoxBorder(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    g.setColour(kBorder);
    g.drawRect(bounds, 1);
}

/**
 * Compact horizontal slider row:  Label [===slider===] Value+Unit
 * ~22px tall, used everywhere instead of rotary knobs.
 */
class SliderRow : public juce::Component
{
public:
    SliderRow(const juce::String& name,
              std::function<juce::String(double)> formatter,
              juce::Colour trackColor = kAccent)
        : valueFormatter(std::move(formatter)),
          trackCol(trackColor)
    {
        label.setText(name, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, kDim);
        label.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(label);

        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        slider.setColour(juce::Slider::trackColourId, trackCol);
        slider.setColour(juce::Slider::backgroundColourId, trackCol.withAlpha(0.18f));
        slider.onValueChange = [this] { updateValue(); };
        addAndMakeVisible(slider);

        value.setColour(juce::Label::textColourId, trackCol);
        value.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(value);
    }

    juce::Slider& getSlider() { return slider; }
    juce::Label& getLabel() { return label; }
    juce::Label& getValueLabel() { return value; }

    void setTrackColor(juce::Colour c)
    {
        trackCol = c;
        slider.setColour(juce::Slider::trackColourId, c);
        value.setColour(juce::Label::textColourId, c);
    }

    void updateValue()
    {
        if (valueFormatter)
            value.setText(valueFormatter(slider.getValue()), juce::dontSendNotification);
    }

    /** Set ghost target value. NaN = no ghost. Smoothing happens in tickGhost(). */
    void setGhostValue(float v) { ghostTarget = v; }
    void clearGhost() { setGhostValue(std::numeric_limits<float>::quiet_NaN()); }

    /** Advance ghost smoothing one frame. Call from a 30 Hz timer.
     *  Returns true if a repaint was triggered. */
    bool tickGhost()
    {
        if (std::isnan(ghostTarget))
        {
            if (!std::isnan(ghostSmoothed)) { ghostSmoothed = NaN_; repaint(); return true; }
            return false;
        }

        // Snap on first valid value, then one-pole smooth
        if (std::isnan(ghostSmoothed))
            ghostSmoothed = ghostTarget;
        else
            ghostSmoothed += (ghostTarget - ghostSmoothed) * kGhostSmooth;

        // Only repaint when pixel position actually changes
        float px = ghostToPixelX(ghostSmoothed);
        if (std::abs(px - lastGhostPx) > 0.5f) { lastGhostPx = px; repaint(); return true; }
        return false;
    }

    void paintOverChildren(juce::Graphics& g) override
    {
        if (std::isnan(ghostSmoothed)) return;

        auto sb = slider.getBounds();
        float gx = ghostToPixelX(ghostSmoothed);
        float gy = static_cast<float>(sb.getCentreY());
        float r = static_cast<float>(sb.getHeight()) * 0.28f;

        g.setColour(juce::Colour(0xccff9800)); // orange ghost
        g.fillEllipse(gx - r, gy - r, r * 2.0f, r * 2.0f);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        float f = static_cast<float>(b.getHeight());

        int labelW = juce::jlimit(30, 55, juce::roundToInt(b.getWidth() * 0.18f));
        int valueW = juce::jlimit(22, 42, juce::roundToInt(b.getWidth() * 0.12f));

        // Scale fonts to fit available width (prevents "Da...", "0...." truncation in narrow 2-col layouts)
        float maxFs = f * 0.75f;
        label.setFont(juce::FontOptions(juce::jmax(8.0f, juce::jmin(maxFs, static_cast<float>(labelW) * 0.33f))));
        value.setFont(juce::FontOptions(juce::jmax(8.0f, juce::jmin(maxFs, static_cast<float>(valueW) * 0.33f))));

        label.setBounds(b.removeFromLeft(labelW));
        value.setBounds(b.removeFromRight(valueW));
        slider.setBounds(b);
    }

private:
    juce::Label label, value;
    juce::Slider slider;
    std::function<juce::String(double)> valueFormatter;
    juce::Colour trackCol;

    // Ghost marker smoothing
    static constexpr float NaN_ = std::numeric_limits<float>::quiet_NaN();
    static constexpr float kGhostSmooth = 0.3f;  // one-pole coeff, ~80 ms at 30 fps
    float ghostTarget   = NaN_;
    float ghostSmoothed = NaN_;
    float lastGhostPx   = -100.0f;

    float ghostToPixelX(float v)
    {
        auto sb = slider.getBounds();
        double norm = slider.valueToProportionOfLength(static_cast<double>(v));
        norm = juce::jlimit(0.0, 1.0, norm);
        int thumbW = slider.getLookAndFeel().getSliderThumbRadius(slider) * 2;
        return static_cast<float>(sb.getX() + thumbW / 2)
             + static_cast<float>(sb.getWidth() - thumbW) * static_cast<float>(norm);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SliderRow)
};

/**
 * Square button that displays a cached SVG curve icon and cycles on click.
 * 5 shapes: Log, SLog, Lin, SExp, Exp.
 * Uses pre-parsed juce::Drawable — no per-frame path construction.
 */
class CurveButton : public juce::Component
{
public:
    CurveButton() { setMouseCursor(juce::MouseCursor::PointingHandCursor); }

    void setCurveShape(int shape) { if (curveShape != shape) { curveShape = shape; repaint(); } }
    int  getCurveShape() const    { return curveShape; }

    std::function<void()> onClick;

    void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(kSurface);
        g.fillRect(b);
        g.setColour(kBorder);
        g.drawRect(b, 1.0f);

        auto& icon = getIcon(curveShape);
        if (icon)
            icon->drawWithin(g, b.reduced(1.0f),
                             juce::RectanglePlacement::centred, 1.0f);
    }

private:
    int curveShape = 2; // 0=Log, 1=SLog, 2=Lin, 3=SExp, 4=Exp

    static constexpr int kNumShapes = 5;

    static std::unique_ptr<juce::Drawable>& getIcon(int shape)
    {
        static std::unique_ptr<juce::Drawable> icons[kNumShapes];
        static bool inited = false;
        if (!inited)
        {
            // Amber (#FF6F00) curve paths in a 16×16 viewBox
            static const char* svgs[kNumShapes] = {
                // Log (convex — slow start, cubic)
                R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><path d="M2 14 C9 14 13 10 14 2" stroke="#FF6F00" fill="none" stroke-width="1.5" stroke-linecap="round"/></svg>)",
                // SLog (mild convex — slow start, quadratic)
                R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><path d="M2 14 C6 14 11 7 14 2" stroke="#FF6F00" fill="none" stroke-width="1.5" stroke-linecap="round"/></svg>)",
                // Lin (straight diagonal)
                R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><path d="M2 14 L14 2" stroke="#FF6F00" fill="none" stroke-width="1.5" stroke-linecap="round"/></svg>)",
                // SExp (mild concave — mild fast start, quadratic)
                R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><path d="M2 14 C2 9 7 2 14 2" stroke="#FF6F00" fill="none" stroke-width="1.5" stroke-linecap="round"/></svg>)",
                // Exp (concave — fast start, cubic)
                R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><path d="M2 14 C2 5 5 2 14 2" stroke="#FF6F00" fill="none" stroke-width="1.5" stroke-linecap="round"/></svg>)",
            };
            for (int i = 0; i < kNumShapes; ++i)
                if (auto xml = juce::parseXML(svgs[i]))
                    icons[i] = juce::Drawable::createFromSVG(*xml);
            inited = true;
        }
        return icons[juce::jlimit(0, kNumShapes - 1, shape)];
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CurveButton)
};

/**
 * Two SliderRows side by side in a 2-column grid.
 * Height = one row height. Each SliderRow gets 50% width minus gap.
 */
class SliderPair : public juce::Component
{
public:
    SliderPair(std::unique_ptr<SliderRow> l, std::unique_ptr<SliderRow> r)
        : left(std::move(l)), right(std::move(r))
    {
        addAndMakeVisible(*left);
        addAndMakeVisible(*right);
    }

    SliderRow& getLeft() { return *left; }
    SliderRow& getRight() { return *right; }

    void resized() override
    {
        auto b = getLocalBounds();
        int gap = juce::jmax(4, juce::roundToInt(b.getWidth() * 0.02f));
        int colW = (b.getWidth() - gap) / 2;
        left->setBounds(b.removeFromLeft(colW));
        b.removeFromLeft(gap);
        right->setBounds(b);
    }

private:
    std::unique_ptr<SliderRow> left, right;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SliderPair)
};
