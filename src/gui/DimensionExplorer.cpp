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

DimensionExplorer::DimensionExplorer() = default;

void DimensionExplorer::setEmbeddings(const std::vector<float>& embA, const std::vector<float>& embB)
{
    embA_ = embA;
    embB_ = embB;

    // Detect if B is all zeros (no prompt B)
    hasBPrompt_ = false;
    for (auto v : embB_)
    {
        if (std::abs(v) > 1e-8f)
        {
            hasBPrompt_ = true;
            break;
        }
    }

    rebuildBars();
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
    undoStack_.clear();
    undoPos_ = -1;
    repaint();
}

void DimensionExplorer::rebuildBars()
{
    int numDims = static_cast<int>(embA_.size());
    if (numDims == 0) return;

    // Preserve existing offsets if dimensions match
    std::vector<float> oldOffsets(numDims, 0.0f);
    for (auto& bar : bars_)
        if (bar.dimIndex < numDims)
            oldOffsets[static_cast<size_t>(bar.dimIndex)] = bar.offset;

    bars_.resize(static_cast<size_t>(numDims));
    for (int i = 0; i < numDims; ++i)
    {
        auto& bar = bars_[static_cast<size_t>(i)];
        bar.dimIndex = i;
        bar.offset = oldOffsets[static_cast<size_t>(i)];

        if (hasBPrompt_)
            bar.baseValue = embA_[static_cast<size_t>(i)] - embB_[static_cast<size_t>(i)];
        else
            bar.baseValue = embA_[static_cast<size_t>(i)];
    }

    // Sort by |baseValue| descending (most significant first)
    std::sort(bars_.begin(), bars_.end(), [](const Bar& a, const Bar& b) {
        return std::abs(a.baseValue) > std::abs(b.baseValue);
    });

    // Reset undo stack (bar order may have changed after re-sort)
    undoStack_.clear();
    UndoState state;
    state.offsets.resize(bars_.size());
    for (size_t i = 0; i < bars_.size(); ++i)
        state.offsets[i] = bars_[i].offset;
    undoStack_.push_back(std::move(state));
    undoPos_ = 0;
}

std::vector<std::pair<int, float>> DimensionExplorer::getDimensionOffsets() const
{
    std::vector<std::pair<int, float>> offsets;
    for (auto& bar : bars_)
        if (std::abs(bar.offset) > 1e-8f)
            offsets.emplace_back(bar.dimIndex, bar.offset);
    return offsets;
}

void DimensionExplorer::pushUndoState()
{
    // Truncate redo history
    if (undoPos_ >= 0 && undoPos_ < static_cast<int>(undoStack_.size()) - 1)
        undoStack_.resize(static_cast<size_t>(undoPos_ + 1));

    UndoState state;
    state.offsets.resize(bars_.size());
    for (size_t i = 0; i < bars_.size(); ++i)
        state.offsets[i] = bars_[i].offset;
    undoStack_.push_back(std::move(state));
    undoPos_ = static_cast<int>(undoStack_.size()) - 1;
}

void DimensionExplorer::undo()
{
    if (undoPos_ <= 0) return;
    --undoPos_;
    auto& state = undoStack_[static_cast<size_t>(undoPos_)];
    for (size_t i = 0; i < bars_.size() && i < state.offsets.size(); ++i)
        bars_[i].offset = state.offsets[i];

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
    for (size_t i = 0; i < bars_.size() && i < state.offsets.size(); ++i)
        bars_[i].offset = state.offsets[i];

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
    // Map value range to bar area. Auto-scale based on max |value|.
    float maxVal = 0.1f;
    for (auto& bar : bars_)
        maxVal = std::max(maxVal, std::abs(bar.displayValue()));

    float centreY = barArea_.getCentreY();
    float halfH = barArea_.getHeight() * 0.45f;
    return centreY - (value / maxVal) * halfH;
}

float DimensionExplorer::yToValue(float y) const
{
    float maxVal = 0.1f;
    for (auto& bar : bars_)
        maxVal = std::max(maxVal, std::abs(bar.displayValue()));

    float centreY = barArea_.getCentreY();
    float halfH = barArea_.getHeight() * 0.45f;
    if (halfH < 1.0f) return 0.0f;
    return -(y - centreY) / halfH * maxVal;
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
        g.setColour(kDimmer);
        g.setFont(juce::FontOptions(fs * 0.85f));
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
        float val = bar.displayValue();
        float topY = valueToY(val);

        // Color: edited (blue), A-side (green), B-side (orange)
        juce::Colour col;
        if (std::abs(bar.offset) > 1e-8f)
            col = kBarEdit;
        else if (val >= 0.0f)
            col = kBarA;
        else
            col = kBarB;

        // Hovered bar is brighter
        if (i == hoveredBar_)
            col = col.brighter(0.3f);

        g.setColour(col.withAlpha(0.85f));
        if (val >= 0.0f)
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
        float hintY = barArea_.getBottom() - hintFs - 4.0f;
        g.drawText("prompt balance",
                   juce::roundToInt(barArea_.getX() + 4.0f), juce::roundToInt(hintY),
                   200, juce::roundToInt(hintFs + 2), juce::Justification::centredLeft);
        g.drawText("sound character",
                   juce::roundToInt(barArea_.getRight() - 204.0f), juce::roundToInt(hintY),
                   200, juce::roundToInt(hintFs + 2), juce::Justification::centredRight);
    }

    // Tooltip for hovered bar
    if (hoveredBar_ >= 0 && hoveredBar_ < numBars)
    {
        auto& bar = bars_[static_cast<size_t>(hoveredBar_)];
        g.setFont(juce::FontOptions(fs * 0.80f));
        g.setColour(juce::Colours::white);
        juce::String tip = "dim " + juce::String(bar.dimIndex)
                         + ": " + juce::String(bar.displayValue(), 4);
        if (std::abs(bar.offset) > 1e-8f)
            tip += " (offset " + juce::String(bar.offset, 4) + ")";

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

    // Save pre-edit state for undo before modifying anything
    pushUndoState();
    dragBar_ = idx;
    dragStartY_ = static_cast<float>(e.y);
    dragStartValue_ = bars_[static_cast<size_t>(idx)].offset;
}

void DimensionExplorer::mouseDrag(const juce::MouseEvent& e)
{
    if (dragBar_ < 0 || dragBar_ >= static_cast<int>(bars_.size())) return;

    float newVal = yToValue(static_cast<float>(e.y));
    float baseVal = bars_[static_cast<size_t>(dragBar_)].baseValue;
    bars_[static_cast<size_t>(dragBar_)].offset = newVal - baseVal;
    hasUserEdits_ = true;
    repaint();
}

void DimensionExplorer::mouseUp(const juce::MouseEvent&)
{
    dragBar_ = -1;
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
