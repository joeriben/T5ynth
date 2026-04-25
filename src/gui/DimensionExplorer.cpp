#include "DimensionExplorer.h"
#include "GuiHelpers.h"
#include <algorithm>
#include <cmath>

// Colors for the bar display
static const auto kBarA     = juce::Colour(0xff4caf50);  // Green (A-side)
static const auto kBarB     = juce::Colour(0xffff9800);  // Orange (B-side)
static const auto kBarEdit  = juce::Colour(0xff4a9eff);  // Blue (user-edited offset)
static const auto kBarBg    = juce::Colour(0xff0e0e0e);
static const auto kZeroLine = juce::Colour(0xff2a2a2a);
static constexpr float kMinValueScale = 0.1f;
static constexpr float kScaleHeadroom = 1.05f;

DimensionExplorer::DimensionExplorer() = default;

std::vector<float> DimensionExplorer::estimateBaselineValues(
    const std::vector<float>& embA,
    const std::vector<float>& embB,
    float alpha,
    float magnitude,
    const std::vector<std::pair<int, float>>& offsets)
{
    const size_t numDims = embA.size();
    std::vector<float> baseline(numDims, 0.0f);

    const bool hasB = embB.size() == numDims
        && std::any_of(embB.begin(), embB.end(), [](float value) { return std::abs(value) > 1e-8f; });

    for (size_t i = 0; i < numDims; ++i)
    {
        float value = embA[i];
        if (hasB)
        {
            const float aWeight = 0.5f - 0.5f * alpha;
            const float bWeight = 0.5f + 0.5f * alpha;
            value = aWeight * embA[i] + bWeight * embB[i];
        }
        baseline[i] = value * magnitude;
    }

    for (const auto& [dimIndex, delta] : offsets)
    {
        if (dimIndex >= 0 && static_cast<size_t>(dimIndex) < baseline.size())
            baseline[static_cast<size_t>(dimIndex)] += delta;
    }

    return baseline;
}

void DimensionExplorer::setEmbeddings(const std::vector<float>& embA, const std::vector<float>& embB,
                                      const std::vector<float>& baselineValues,
                                      bool preserveOffsets)
{
    embA_ = embA;
    embB_ = embB;

    // Symmetric prompt design: B is always meaningful — either typed text,
    // the Spiegelung-am-Modell-Null echo of A, or the model-unconditional
    // when both fields are empty. The backend always returns a non-zero
    // emb_b, so we always treat B as present for visualization purposes.
    hasBPrompt_ = !embB_.empty();

    rebuildBars(baselineValues, preserveOffsets);
    repaint();
}

void DimensionExplorer::clear()
{
    embA_.clear();
    embB_.clear();
    bars_.clear();
    hasBPrompt_ = false;
    hasUserEdits_ = false;
    hoveredBar_ = -1;
    dragBar_ = -1;
    lastPaintBar_ = -1;
    dragDirty_ = false;
    valueScaleMax_ = kMinValueScale;
    undoStack_.clear();
    undoPos_ = -1;
    repaint();
}

void DimensionExplorer::resetOffsets()
{
    if (bars_.empty())
        return;

    bool hadOffsets = false;
    for (auto& bar : bars_)
    {
        if (std::abs(bar.offset) > 1e-8f)
            hadOffsets = true;
        bar.offset = 0.0f;
    }

    if (!hadOffsets)
        return;

    hasUserEdits_ = false;
    dragBar_ = -1;
    lastPaintBar_ = -1;
    dragDirty_ = false;
    pushUndoState();
    repaint();
}

void DimensionExplorer::rebuildBars(const std::vector<float>& baselineValues, bool preserveOffsets)
{
    int numDims = static_cast<int>(embA_.size());
    if (numDims == 0)
    {
        bars_.clear();
        hasUserEdits_ = false;
        undoStack_.clear();
        undoPos_ = -1;
        valueScaleMax_ = kMinValueScale;
        return;
    }

    // Preserve existing offsets if dimensions match and the caller requested it.
    std::vector<float> oldOffsets(numDims, 0.0f);
    if (preserveOffsets)
    {
        for (auto& bar : bars_)
            if (bar.dimIndex < numDims)
                oldOffsets[static_cast<size_t>(bar.dimIndex)] = bar.offset;
    }

    bars_.resize(static_cast<size_t>(numDims));
    hasUserEdits_ = false;
    valueScaleMax_ = kMinValueScale;
    for (int i = 0; i < numDims; ++i)
    {
        auto& bar = bars_[static_cast<size_t>(i)];
        bar.dimIndex = i;
        bar.aValue = embA_[static_cast<size_t>(i)];
        bar.bValue = hasBPrompt_ && static_cast<size_t>(i) < embB_.size()
            ? embB_[static_cast<size_t>(i)] : 0.0f;
        if (baselineValues.size() == static_cast<size_t>(numDims))
            bar.baseActualValue = baselineValues[static_cast<size_t>(i)];
        else if (hasBPrompt_)
            bar.baseActualValue = 0.5f * (bar.aValue + bar.bValue);
        else
            bar.baseActualValue = bar.aValue;
        bar.offset = oldOffsets[static_cast<size_t>(i)];

        valueScaleMax_ = std::max(valueScaleMax_, std::abs(orientedValue(bar, bar.aValue)));
        if (hasBPrompt_)
            valueScaleMax_ = std::max(valueScaleMax_, std::abs(orientedValue(bar, bar.bValue)));
        valueScaleMax_ = std::max(valueScaleMax_, std::abs(orientedValue(bar, bar.baseActualValue)));
        valueScaleMax_ = std::max(valueScaleMax_, std::abs(orientedValue(bar, displayedActualValue(bar))));
        if (std::abs(bar.offset) > 1e-8f)
            hasUserEdits_ = true;
    }

    valueScaleMax_ *= kScaleHeadroom;

    // Sort by |A-B| descending (most significant first)
    std::sort(bars_.begin(), bars_.end(), [](const Bar& a, const Bar& b) {
        const float metricA = std::abs(a.aValue - a.bValue);
        const float metricB = std::abs(b.aValue - b.bValue);
        return metricA > metricB;
    });

    bool canPreserveUndo = preserveOffsets && !undoStack_.empty();
    if (canPreserveUndo)
    {
        for (const auto& state : undoStack_)
        {
            if (state.offsets.size() != static_cast<size_t>(numDims))
            {
                canPreserveUndo = false;
                break;
            }
        }
    }

    if (!canPreserveUndo)
    {
        undoStack_.clear();
        undoStack_.push_back(makeUndoState());
        undoPos_ = 0;
    }
}

std::vector<std::pair<int, float>> DimensionExplorer::getDimensionOffsets() const
{
    std::vector<std::pair<int, float>> offsets;
    for (auto& bar : bars_)
        if (std::abs(bar.offset) > 1e-8f)
            offsets.emplace_back(bar.dimIndex, bar.offset);
    return offsets;
}

float DimensionExplorer::barOrientation(const Bar& bar) const
{
    if (!hasBPrompt_)
        return 1.0f;

    const float diff = bar.aValue - bar.bValue;
    if (std::abs(diff) <= 1e-8f)
        return 1.0f;
    return diff > 0.0f ? 1.0f : -1.0f;
}

float DimensionExplorer::barMidpoint(const Bar& bar) const
{
    if (!hasBPrompt_)
        return 0.0f;
    return 0.5f * (bar.aValue + bar.bValue);
}

float DimensionExplorer::orientedValue(const Bar& bar, float actualValue) const
{
    if (!hasBPrompt_)
        return actualValue;
    return barOrientation(bar) * (actualValue - barMidpoint(bar));
}

float DimensionExplorer::actualValueFromOriented(const Bar& bar, float oriented) const
{
    if (!hasBPrompt_)
        return oriented;
    return barMidpoint(bar) + barOrientation(bar) * oriented;
}

float DimensionExplorer::displayedActualValue(const Bar& bar) const
{
    return bar.baseActualValue + bar.offset;
}

void DimensionExplorer::pushUndoState()
{
    UndoState state = makeUndoState();

    if (undoPos_ >= 0 && undoPos_ < static_cast<int>(undoStack_.size()))
    {
        auto& current = undoStack_[static_cast<size_t>(undoPos_)];
        if (current.offsets.size() == state.offsets.size()
            && std::equal(current.offsets.begin(), current.offsets.end(), state.offsets.begin()))
            return;
    }

    // Truncate redo history
    if (undoPos_ >= 0 && undoPos_ < static_cast<int>(undoStack_.size()) - 1)
        undoStack_.resize(static_cast<size_t>(undoPos_ + 1));

    undoStack_.push_back(std::move(state));
    undoPos_ = static_cast<int>(undoStack_.size()) - 1;
}

DimensionExplorer::UndoState DimensionExplorer::makeUndoState() const
{
    UndoState state;
    state.offsets.resize(embA_.size(), 0.0f);
    for (const auto& bar : bars_)
    {
        if (bar.dimIndex >= 0 && static_cast<size_t>(bar.dimIndex) < state.offsets.size())
            state.offsets[static_cast<size_t>(bar.dimIndex)] = bar.offset;
    }
    return state;
}

void DimensionExplorer::applyUndoState(const UndoState& state)
{
    for (auto& bar : bars_)
    {
        if (bar.dimIndex >= 0 && static_cast<size_t>(bar.dimIndex) < state.offsets.size())
            bar.offset = state.offsets[static_cast<size_t>(bar.dimIndex)];
        else
            bar.offset = 0.0f;
    }
}

void DimensionExplorer::undo()
{
    if (undoPos_ <= 0) return;
    --undoPos_;
    auto& state = undoStack_[static_cast<size_t>(undoPos_)];
    applyUndoState(state);

    hasUserEdits_ = false;
    for (auto& bar : bars_)
        if (std::abs(bar.offset) > 1e-8f) { hasUserEdits_ = true; break; }
    repaint();
}

void DimensionExplorer::redo()
{
    if (undoPos_ >= static_cast<int>(undoStack_.size()) - 1) return;
    ++undoPos_;
    auto& state = undoStack_[static_cast<size_t>(undoPos_)];
    applyUndoState(state);

    hasUserEdits_ = false;
    for (auto& bar : bars_)
        if (std::abs(bar.offset) > 1e-8f) { hasUserEdits_ = true; break; }
    repaint();
}

// ── Geometry helpers ────────────────────────────────────────────

int DimensionExplorer::barAtX(float x) const
{
    if (bars_.empty() || barArea_.getWidth() <= 0.0f) return -1;
    float rel = (x - barArea_.getX()) / barArea_.getWidth();
    if (rel < 0.0f || rel >= 1.0f) return -1;
    int idx = static_cast<int>(rel * static_cast<float>(bars_.size()));
    return juce::jlimit(0, static_cast<int>(bars_.size()) - 1, idx);
}

float DimensionExplorer::valueToY(float value) const
{
    float centreY = barArea_.getCentreY();
    float halfH = barArea_.getHeight() * 0.45f;
    float clampedValue = juce::jlimit(-valueScaleMax_, valueScaleMax_, value);
    return centreY - (clampedValue / valueScaleMax_) * halfH;
}

float DimensionExplorer::yToValue(float y) const
{
    float centreY = barArea_.getCentreY();
    float halfH = barArea_.getHeight() * 0.45f;
    if (halfH < 1.0f) return 0.0f;

    float clampedY = juce::jlimit(barArea_.getY(), barArea_.getBottom(), y);
    return -(clampedY - centreY) / halfH * valueScaleMax_;
}

// ── Paint ───────────────────────────────────────────────────────

void DimensionExplorer::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();

    // Inner card
    g.setColour(kBarBg);
    g.fillRoundedRectangle(area, 4.0f);
    g.setColour(kBorder);
    g.drawRoundedRectangle(area, 4.0f, 1.0f);

    // Header — only in overlay mode (mini-view header is provided by MainPanel)
    float topH = (getTopLevelComponent() != nullptr)
                     ? static_cast<float>(getTopLevelComponent()->getHeight()) : 800.0f;
    float fs = juce::jlimit(12.0f, 22.0f, topH * 0.025f);

    if (overlayMode_)
    {
        g.setFont(juce::FontOptions(fs));
        g.setColour(kDim);
        juce::String header = hasBPrompt_ ? "LATENT DIMENSION EXPLORER (A-B)" : "LATENT DIMENSION EXPLORER";
        g.drawText(header, area.reduced(6, 2).removeFromTop(static_cast<int>(fs + 4)),
                   juce::Justification::centredLeft);
    }

    if (bars_.empty())
    {
        g.setColour(kTextMuted);
        g.setFont(juce::FontOptions(juce::jmax(kUiLabelFontMin, fs * 0.85f)));
        g.drawText(overlayMode_ ? "Generate to see embedding dimensions" : "Generate first",
                   area, juce::Justification::centred);
        return;
    }

    // Zero line
    float centreY = barArea_.getCentreY();
    g.setColour(kZeroLine);
    g.drawHorizontalLine(juce::roundToInt(centreY), barArea_.getX(), barArea_.getRight());

    // Bars
    int numBars = static_cast<int>(bars_.size());
    float barW = barArea_.getWidth() / static_cast<float>(numBars);
    float gapFrac = (barW > 3.0f) ? 0.15f : 0.0f;

    for (int i = 0; i < numBars; ++i)
    {
        auto& bar = bars_[static_cast<size_t>(i)];
        float x = barArea_.getX() + static_cast<float>(i) * barW + barW * gapFrac * 0.5f;
        float w = barW * (1.0f - gapFrac);
        const float finalActual = displayedActualValue(bar);
        const float finalOriented = orientedValue(bar, finalActual);
        float topY = valueToY(finalOriented);

        if (hasBPrompt_)
        {
            const float aY = valueToY(orientedValue(bar, bar.aValue));
            const float bY = valueToY(orientedValue(bar, bar.bValue));
            g.setColour(kBarA.withAlpha(0.25f));
            g.drawHorizontalLine(juce::roundToInt(aY), x, x + w);
            g.setColour(kBarB.withAlpha(0.25f));
            g.drawHorizontalLine(juce::roundToInt(bY), x, x + w);
        }

        // Color: edited (blue), toward A (green), toward B (orange)
        juce::Colour col;
        if (std::abs(bar.offset) > 1e-8f)
            col = kBarEdit;
        else if (finalOriented >= 0.0f)
            col = kBarA;
        else
            col = kBarB;

        // Hovered bar is brighter
        if (i == hoveredBar_)
            col = col.brighter(0.3f);

        g.setColour(col.withAlpha(0.85f));
        if (finalOriented >= 0.0f)
            g.fillRect(x, topY, w, centreY - topY);
        else
            g.fillRect(x, centreY, w, topY - centreY);
    }

    // Axis hints (only in overlay mode with two prompts)
    if (overlayMode_ && hasBPrompt_)
    {
        float hintFs = juce::jlimit(10.0f, 15.0f, fs * 0.7f);
        g.setFont(juce::FontOptions(hintFs).withStyle("Bold"));
        g.setColour(juce::Colour(0x40ffffff));
        g.drawText("toward A",
                   juce::roundToInt(barArea_.getX() + 4.0f), juce::roundToInt(barArea_.getY() + 2.0f),
                   140, juce::roundToInt(hintFs + 2), juce::Justification::topLeft);
        g.drawText("toward B",
                   juce::roundToInt(barArea_.getX() + 4.0f), juce::roundToInt(barArea_.getBottom() - hintFs - 2.0f),
                   140, juce::roundToInt(hintFs + 2), juce::Justification::bottomLeft);
        float hintY = barArea_.getBottom() - hintFs - 4.0f;
        g.drawText("changes A/B relation",
                   juce::roundToInt(barArea_.getX() + 4.0f), juce::roundToInt(hintY),
                   200, juce::roundToInt(hintFs + 2), juce::Justification::centredLeft);
        g.drawText("changes shared sound basis",
                   juce::roundToInt(barArea_.getRight() - 264.0f), juce::roundToInt(hintY),
                   260, juce::roundToInt(hintFs + 2), juce::Justification::centredRight);
    }

    // Tooltip for hovered bar
    if (hoveredBar_ >= 0 && hoveredBar_ < numBars)
    {
        auto& bar = bars_[static_cast<size_t>(hoveredBar_)];
        g.setFont(juce::FontOptions(fs * 0.80f));
        g.setColour(juce::Colours::white);
        juce::String tip = "dim " + juce::String(bar.dimIndex);
        if (hasBPrompt_)
        {
            tip += "  A " + juce::String(bar.aValue, 4)
                + "  B " + juce::String(bar.bValue, 4)
                + "  final " + juce::String(displayedActualValue(bar), 4)
                + "  A-B " + juce::String(bar.aValue - bar.bValue, 4);
        }
        else
        {
            tip += ": " + juce::String(displayedActualValue(bar), 4);
        }
        if (std::abs(bar.offset) > 1e-8f)
            tip += "  (edit " + juce::String(bar.offset, 4) + ")";

        float tipX = barArea_.getX() + static_cast<float>(hoveredBar_) * barW;
        float tipY = barArea_.getY() - 2.0f;
        g.drawText(tip, juce::roundToInt(tipX), juce::roundToInt(tipY - fs),
                   300, juce::roundToInt(fs + 2), juce::Justification::centredLeft);
    }
}

void DimensionExplorer::resized()
{
    auto area = getLocalBounds().toFloat().reduced(2.0f);

    // Reserve space for header
    float topH = (getTopLevelComponent() != nullptr)
                     ? static_cast<float>(getTopLevelComponent()->getHeight()) : 800.0f;
    float fs = juce::jlimit(12.0f, 22.0f, topH * 0.025f);
    float headerH = fs + 8.0f;

    barArea_ = area;
    if (overlayMode_)
        barArea_.removeFromTop(headerH);
    barArea_.reduce(4.0f, 4.0f);
}

// ── Mouse interaction ───────────────────────────────────────────

void DimensionExplorer::mouseDown(const juce::MouseEvent& e)
{
    // Mini-view: any click opens overlay, no bar interaction
    if (!overlayMode_)
    {
        if (onClicked) onClicked();
        return;
    }

    // Overlay: interact with bars
    int idx = barAtX(static_cast<float>(e.x));
    if (idx < 0) return;

    dragBar_ = idx;
    lastPaintBar_ = idx;
    dragDirty_ = false;
}

void DimensionExplorer::mouseDrag(const juce::MouseEvent& e)
{
    if (dragBar_ < 0 || dragBar_ >= static_cast<int>(bars_.size())) return;

    float newOrientedValue = yToValue(static_cast<float>(e.y));
    const bool paintMode = e.mods.isShiftDown();
    int targetBar = paintMode ? barAtX(static_cast<float>(e.x)) : dragBar_;
    if (targetBar < 0)
        targetBar = dragBar_;

    int rangeStart = targetBar;
    int rangeEnd = targetBar;
    if (paintMode && lastPaintBar_ >= 0)
    {
        rangeStart = std::min(lastPaintBar_, targetBar);
        rangeEnd = std::max(lastPaintBar_, targetBar);
    }

    for (int i = rangeStart; i <= rangeEnd; ++i)
    {
        auto& bar = bars_[static_cast<size_t>(i)];
        float newActualValue = actualValueFromOriented(bar, newOrientedValue);
        float newOffset = newActualValue - bar.baseActualValue;
        if (std::abs(newOffset - bar.offset) > 1e-6f)
            dragDirty_ = true;
        bar.offset = newOffset;
    }

    lastPaintBar_ = targetBar;

    hasUserEdits_ = false;
    for (auto& candidate : bars_)
    {
        if (std::abs(candidate.offset) > 1e-8f)
        {
            hasUserEdits_ = true;
            break;
        }
    }
    repaint();
}

void DimensionExplorer::mouseUp(const juce::MouseEvent&)
{
    if (dragDirty_)
        pushUndoState();

    dragBar_ = -1;
    lastPaintBar_ = -1;
    dragDirty_ = false;
}

void DimensionExplorer::mouseMove(const juce::MouseEvent& e)
{
    int idx = barAtX(static_cast<float>(e.x));
    if (idx != hoveredBar_)
    {
        hoveredBar_ = idx;
        repaint();
    }
}

void DimensionExplorer::mouseExit(const juce::MouseEvent&)
{
    if (hoveredBar_ >= 0)
    {
        hoveredBar_ = -1;
        repaint();
    }
}
