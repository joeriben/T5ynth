#pragma once
#include <JuceHeader.h>
#include <functional>

// ── Color constants (shared across all GUI files) ──────────────────────────
static const auto kAccent  = juce::Colour(0xff4a9eff);  // Blue accent
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

// Section accent colors (for headers/highlights per module)
static const auto kFilterCol = juce::Colour(0xff00bcd4);  // Cyan
static const auto kEnvCol    = juce::Colour(0xffffb74d);  // Light orange (envelopes)
static const auto kModCol    = juce::Colour(0xffff9800);  // Medium orange (LFOs) — also used for section header
static const auto kLfoCol    = juce::Colour(0xffff9800);  // Medium orange (LFOs)
static const auto kDriftCol  = juce::Colour(0xffe65100);  // Dark orange (drift)

// Module lead colors (for headers/sliders in each top-level module)
static const auto kOscCol    = juce::Colour(0xff4caf50);  // Green (oscillator/generation)
static const auto kSeqCol    = juce::Colour(0xff26a69a);  // Teal (sequencer)
static const auto kFxCol     = juce::Colour(0xff9c27b0);  // Purple (effects)

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
        value.setJustificationType(juce::Justification::centredRight);
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

    /** Set a ghost marker showing the modulated value. NaN = no ghost. */
    void setGhostValue(float v)
    {
        // NaN != NaN is always true, so handle NaN explicitly to avoid constant repaints
        bool bothNaN = std::isnan(ghostValue) && std::isnan(v);
        if (!bothNaN && ghostValue != v) { ghostValue = v; repaint(); }
    }
    void clearGhost() { setGhostValue(std::numeric_limits<float>::quiet_NaN()); }

    void paint(juce::Graphics& g) override
    {
        if (std::isnan(ghostValue)) return;

        // Map ghostValue to slider pixel position
        auto sb = slider.getBounds();
        double range = slider.getMaximum() - slider.getMinimum();
        if (range <= 0.0) return;
        double norm = (static_cast<double>(ghostValue) - slider.getMinimum()) / range;
        norm = juce::jlimit(0.0, 1.0, norm);

        int thumbW = slider.getLookAndFeel().getSliderThumbRadius(slider) * 2;
        int trackX = sb.getX() + thumbW / 2;
        int trackW = sb.getWidth() - thumbW;
        float gx = static_cast<float>(trackX) + static_cast<float>(trackW) * static_cast<float>(norm);
        float gy = static_cast<float>(sb.getCentreY());
        float r = static_cast<float>(sb.getHeight()) * 0.28f;

        g.setColour(juce::Colour(0xccff9800)); // orange ghost
        g.fillEllipse(gx - r, gy - r, r * 2.0f, r * 2.0f);
    }

    void resized() override
    {
        auto b = getLocalBounds();
        float f = static_cast<float>(b.getHeight());

        int labelW = juce::jlimit(40, 70, juce::roundToInt(b.getWidth() * 0.22f));
        int valueW = juce::jlimit(40, 65, juce::roundToInt(b.getWidth() * 0.20f));

        label.setFont(juce::FontOptions(f * 0.75f));
        value.setFont(juce::FontOptions(f * 0.75f));

        label.setBounds(b.removeFromLeft(labelW));
        value.setBounds(b.removeFromRight(valueW));
        slider.setBounds(b);
    }

private:
    juce::Label label, value;
    juce::Slider slider;
    std::function<juce::String(double)> valueFormatter;
    juce::Colour trackCol;
    float ghostValue = std::numeric_limits<float>::quiet_NaN();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SliderRow)
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
