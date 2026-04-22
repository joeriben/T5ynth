#pragma once
#include <JuceHeader.h>
#include <array>
#include <functional>
#include <limits>
#include <vector>

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

inline int measureTextWidth(const juce::String& text, float fontSize)
{
    if (text.isEmpty())
        return 0;

    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(juce::Font(juce::FontOptions(fontSize)), text, 0.0f, 0.0f);
    return juce::roundToInt(std::ceil(glyphs.getBoundingBox(0, -1, true).getWidth()));
}

enum class ResponsiveStripFallback
{
    none,
    overflow
};

struct ResponsiveStripItem
{
    int preferredWidth = 0;
    int minimumWidth = 0;
    int priorityTier = 0;
    bool flexible = false;
    ResponsiveStripFallback fallback = ResponsiveStripFallback::none;
};

struct ResponsiveStripResult
{
    std::vector<juce::Rectangle<int>> bounds;
    juce::Rectangle<int> overflowBounds;
    bool overflowUsed = false;
};

inline ResponsiveStripResult layoutResponsiveStrip(juce::Rectangle<int> area,
                                                   const std::vector<ResponsiveStripItem>& items,
                                                   int gap,
                                                   int overflowButtonWidth = 28)
{
    struct PlacedItem
    {
        int originalIndex = -1;
        int preferredWidth = 0;
        int minimumWidth = 0;
        bool isOverflow = false;
    };

    auto buildPlacedItems = [&](bool useOverflow) {
        std::vector<PlacedItem> placed;
        placed.reserve(items.size() + (useOverflow ? 1 : 0));

        bool insertedOverflow = false;
        for (size_t i = 0; i < items.size(); ++i)
        {
            const auto& item = items[i];
            const bool isOverflowCandidate = item.fallback == ResponsiveStripFallback::overflow
                                             && item.priorityTier > 0;

            if (useOverflow && isOverflowCandidate)
            {
                if (!insertedOverflow)
                {
                    placed.push_back({ -1, overflowButtonWidth, overflowButtonWidth, true });
                    insertedOverflow = true;
                }
                continue;
            }

            placed.push_back({ static_cast<int>(i), item.preferredWidth, item.minimumWidth, false });
        }

        return placed;
    };

    auto requiredWidthFor = [&](const std::vector<PlacedItem>& placed, bool preferred) {
        if (placed.empty())
            return 0;

        int total = gap * static_cast<int>(juce::jmax<int>(0, static_cast<int>(placed.size()) - 1));
        for (const auto& item : placed)
            total += preferred ? item.preferredWidth : item.minimumWidth;
        return total;
    };

    auto allItems = buildPlacedItems(false);
    auto overflowItems = buildPlacedItems(true);

    const bool canFitAll = requiredWidthFor(allItems, false) <= area.getWidth();
    const bool canUseOverflow = requiredWidthFor(overflowItems, false) <= area.getWidth();
    const bool useOverflow = !canFitAll && canUseOverflow && overflowItems.size() < allItems.size();

    auto placed = useOverflow ? overflowItems : allItems;

    ResponsiveStripResult result;
    result.bounds.resize(items.size());
    result.overflowUsed = useOverflow;

    if (placed.empty())
        return result;

    const int gapCount = juce::jmax<int>(0, static_cast<int>(placed.size()) - 1);
    const int totalGapWidth = gap * gapCount;

    int totalMin = totalGapWidth;
    int totalPreferred = totalGapWidth;
    for (const auto& item : placed)
    {
        totalMin += item.minimumWidth;
        totalPreferred += item.preferredWidth;
    }

    std::vector<int> widths;
    widths.reserve(placed.size());
    for (const auto& item : placed)
        widths.push_back(item.minimumWidth);

    int remaining = juce::jmax(0, area.getWidth() - totalMin);
    const int expandable = juce::jmax(0, totalPreferred - totalMin);

    if (remaining > 0 && expandable > 0)
    {
        std::vector<int> expansion(placed.size(), 0);
        int granted = 0;
        for (size_t i = 0; i < placed.size(); ++i)
        {
            const int delta = juce::jmax(0, placed[i].preferredWidth - placed[i].minimumWidth);
            const int extra = static_cast<int>((static_cast<int64_t>(remaining) * delta) / expandable);
            expansion[i] = extra;
            granted += extra;
        }

        int leftover = remaining - granted;
        for (size_t i = 0; i < placed.size() && leftover > 0; ++i)
        {
            const int delta = juce::jmax(0, placed[i].preferredWidth - placed[i].minimumWidth);
            if (expansion[i] < delta)
            {
                ++expansion[i];
                --leftover;
            }
        }

        for (size_t i = 0; i < placed.size(); ++i)
            widths[i] += expansion[i];
    }

    int x = area.getX();
    for (size_t i = 0; i < placed.size(); ++i)
    {
        juce::Rectangle<int> bounds(x, area.getY(), widths[i], area.getHeight());
        if (placed[i].isOverflow)
            result.overflowBounds = bounds;
        else
            result.bounds[static_cast<size_t>(placed[i].originalIndex)] = bounds;

        x += widths[i] + gap;
    }

    return result;
}

/**
 * Compact horizontal slider row:  Label [===slider===] Value+Unit
 * ~22px tall, used everywhere instead of rotary knobs.
 */
class SliderRow : public juce::Component
{
public:
    enum class LabelMode { Off, Positive, Negative };

    SliderRow(const juce::String& name,
              std::function<juce::String(double)> formatter,
              juce::Colour trackColor = kAccent)
        : valueFormatter(std::move(formatter)),
          trackCol(trackColor)
    {
        label.setText(name, juce::dontSendNotification);
        label.setInterceptsMouseClicks(false, false);
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
        updateLabelAppearance();
    }

    juce::Slider& getSlider() { return slider; }
    juce::Label& getLabel() { return label; }
    juce::Label& getValueLabel() { return value; }
    int getPreferredWidth() const { return getLayoutProfile(false).preferredWidth; }
    int getMinimumWidth() const { return getLayoutProfile(true).minimumWidth; }
    // Layout-only override for cross-row column alignment. Any code deriving a new
    // forced width must start from getNaturalLabelWidthForAvailableWidth(), not
    // from the currently forced width, otherwise resize passes can feed back into
    // themselves and grow the reserved label column on each relayout.
    void setForcedLabelWidth(int width) { forcedLabelWidth = juce::jmax(0, width); }
    void clearForcedLabelWidth() { forcedLabelWidth = -1; }
    int getNaturalLabelWidthForAvailableWidth(int totalWidth) const
    {
        const int resolvedHeight = juce::jmax(18, getHeight() > 0 ? getHeight() : 22);
        return chooseLayout(totalWidth, resolvedHeight, false).labelWidth;
    }
    int getLabelWidthForAvailableWidth(int totalWidth) const
    {
        const int resolvedHeight = juce::jmax(18, getHeight() > 0 ? getHeight() : 22);
        return chooseLayout(totalWidth, resolvedHeight).labelWidth;
    }

    void setTrackColor(juce::Colour c)
    {
        trackCol = c;
        slider.setColour(juce::Slider::trackColourId, c);
        value.setColour(juce::Label::textColourId, c);
        updateLabelAppearance();
    }

    void setLabelMode(LabelMode newMode)
    {
        if (labelMode == newMode)
            return;
        labelMode = newMode;
        updateLabelAppearance();
        repaint();
    }

    LabelMode getLabelMode() const { return labelMode; }
    void setLabelClickHandler(std::function<void()> handler) { onLabelClick = std::move(handler); }

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

    void paint(juce::Graphics& g) override
    {
        auto badge = getLabelBadgeBounds();
        if (badge.isEmpty())
            return;

        if (labelMode == LabelMode::Positive)
        {
            g.setColour(trackCol);
            g.fillRect(badge);
        }
        else if (labelMode == LabelMode::Negative)
        {
            g.setColour(trackCol);
            g.drawRect(badge.reduced(0.5f), 1.0f);
        }
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
        const auto layout = chooseLayout(b.getWidth(), b.getHeight());
        label.setFont(juce::FontOptions(layout.labelFontSize));
        value.setFont(juce::FontOptions(layout.valueFontSize));

        label.setBounds(b.removeFromLeft(layout.labelWidth));
        value.setBounds(b.removeFromRight(layout.valueWidth));
        slider.setBounds(b);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        if (onLabelClick && label.getBounds().contains(e.getPosition()))
            onLabelClick();
    }

private:
    struct SliderLayoutProfile
    {
        int labelWidth = 0;
        int valueWidth = 0;
        int minTrackWidth = 0;
        int preferredTrackWidth = 0;
        float labelFontSize = 9.0f;
        float valueFontSize = 9.0f;
        int minimumWidth = 0;
        int preferredWidth = 0;
    };

    juce::Label label, value;
    juce::Slider slider;
    std::function<juce::String(double)> valueFormatter;
    juce::Colour trackCol;
    LabelMode labelMode = LabelMode::Off;
    std::function<void()> onLabelClick;

    // Ghost marker smoothing
    static constexpr float NaN_ = std::numeric_limits<float>::quiet_NaN();
    static constexpr float kGhostSmooth = 0.3f;  // one-pole coeff, ~80 ms at 30 fps
    float ghostTarget   = NaN_;
    float ghostSmoothed = NaN_;
    float lastGhostPx   = -100.0f;
    int forcedLabelWidth = -1;

    juce::String currentValueText() const
    {
        if (!value.getText().isEmpty())
            return value.getText();
        if (valueFormatter)
            return valueFormatter(slider.getValue());
        return {};
    }

    SliderLayoutProfile getLayoutProfile(bool compact, bool applyForcedLabelWidth = true) const
    {
        const int resolvedHeight = juce::jmax(18, getHeight() > 0 ? getHeight() : 22);
        const float maxFs = static_cast<float>(resolvedHeight) * 0.75f;
        const float labelFs = juce::jmax(7.0f, juce::jmin(maxFs, compact ? 9.0f : 10.5f));
        const float valueFs = juce::jmax(7.0f, juce::jmin(maxFs, compact ? 9.0f : 10.5f));
        const int labelPadding = compact ? 6 : 10;
        const int valuePadding = compact ? 6 : 10;

        SliderLayoutProfile profile;
        profile.labelFontSize = labelFs;
        profile.valueFontSize = valueFs;
        profile.labelWidth = label.getText().isEmpty() ? 0 : measureTextWidth(label.getText(), labelFs) + labelPadding;
        profile.valueWidth = currentValueText().isEmpty() ? 0 : measureTextWidth(currentValueText(), valueFs) + valuePadding;
        if (applyForcedLabelWidth && forcedLabelWidth >= 0)
            profile.labelWidth = forcedLabelWidth;
        profile.minTrackWidth = compact ? 48 : 72;
        profile.preferredTrackWidth = compact ? 72 : 112;
        profile.minimumWidth = profile.labelWidth + profile.valueWidth + profile.minTrackWidth;
        profile.preferredWidth = profile.labelWidth + profile.valueWidth + profile.preferredTrackWidth;
        return profile;
    }

    SliderLayoutProfile chooseLayout(int totalWidth, int height, bool applyForcedLabelWidth = true) const
    {
        juce::ignoreUnused(height);

        auto profile = getLayoutProfile(false, applyForcedLabelWidth);
        if (totalWidth > 0 && totalWidth < profile.preferredWidth)
            profile = getLayoutProfile(true, applyForcedLabelWidth);

        int overflow = profile.minimumWidth - totalWidth;
        if (overflow > 0)
        {
            const int minLabelWidth = label.getText().isEmpty() ? 0 : 8;
            const int minValueWidth = currentValueText().isEmpty() ? 0 : 8;
            const int labelShrink = juce::jmin(overflow / 2 + overflow % 2,
                                               juce::jmax(0, profile.labelWidth - minLabelWidth));
            profile.labelWidth -= labelShrink;
            overflow -= labelShrink;
            profile.valueWidth -= juce::jmin(overflow, juce::jmax(0, profile.valueWidth - minValueWidth));
        }

        return profile;
    }

    float ghostToPixelX(float v)
    {
        auto sb = slider.getBounds();
        double norm = slider.valueToProportionOfLength(static_cast<double>(v));
        norm = juce::jlimit(0.0, 1.0, norm);
        int thumbW = slider.getLookAndFeel().getSliderThumbRadius(slider) * 2;
        return static_cast<float>(sb.getX() + thumbW / 2)
             + static_cast<float>(sb.getWidth() - thumbW) * static_cast<float>(norm);
    }

    juce::Rectangle<float> getLabelBadgeBounds() const
    {
        auto lb = label.getBounds().toFloat();
        if (lb.isEmpty() || label.getText().isEmpty())
            return {};

        const float fontSize = label.getFont().getHeight();
        const float textW = static_cast<float>(measureTextWidth(label.getText(), fontSize));
        const float padX = juce::jmax(3.0f, fontSize * 0.28f);
        const float padY = juce::jmax(1.0f, fontSize * 0.16f);
        const float badgeW = juce::jmin(lb.getWidth(), textW + padX * 2.0f);
        const float badgeH = juce::jmin(lb.getHeight(), fontSize + padY * 2.0f);
        const float badgeY = lb.getY() + (lb.getHeight() - badgeH) * 0.5f;
        return { lb.getX(), badgeY, badgeW, badgeH };
    }

    void updateLabelAppearance()
    {
        juce::Colour textColour = kDimmer;
        if (labelMode == LabelMode::Positive)
            textColour = juce::Colours::white;
        else if (labelMode == LabelMode::Negative)
            textColour = kDim;
        label.setColour(juce::Label::textColourId, textColour);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SliderRow)
};

inline std::array<juce::Rectangle<int>, 2> layoutSliderRowPairBounds(juce::Rectangle<int> area,
                                                                      SliderRow& left,
                                                                      SliderRow& right,
                                                                      int gap = 4)
{
    const auto originalArea = area;
    const int availableW = area.getWidth();
    const int safeGap = juce::jmin(gap, juce::jmax(0, availableW / 8));
    const int halfW = juce::jmax(0, (availableW - safeGap) / 2);

    auto leftBounds = area.removeFromLeft(halfW);
    area.removeFromLeft(safeGap);
    auto rightBounds = area;

    const int leftMin = left.getMinimumWidth();
    const int rightMin = right.getMinimumWidth();
    const bool bothFitEqual = leftBounds.getWidth() >= leftMin && rightBounds.getWidth() >= rightMin;

    if (bothFitEqual || availableW <= leftMin + rightMin + safeGap)
        return { leftBounds, rightBounds };

    const std::vector<ResponsiveStripItem> items {
        { left.getPreferredWidth(),  leftMin,  0, true, ResponsiveStripFallback::none },
        { right.getPreferredWidth(), rightMin, 0, true, ResponsiveStripFallback::none }
    };

    auto result = layoutResponsiveStrip(originalArea, items, safeGap);
    return { result.bounds[0], result.bounds[1] };
}

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
        auto bounds = layoutSliderRowPairBounds(getLocalBounds(), *left, *right);
        left->setBounds(bounds[0]);
        right->setBounds(bounds[1]);
    }

private:
    std::unique_ptr<SliderRow> left, right;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SliderPair)
};
