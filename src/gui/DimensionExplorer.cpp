#include "DimensionExplorer.h"
#include "GuiHelpers.h"
#include <algorithm>
#include <cmath>

// Colors for the bar display — A/B share the prompt-panel impulse identity
// (periwinkle A / yellow B). The edit marker is cyan: the former blue (#4a9eff)
// was only ΔE2000≈13 from the new periwinkle A and confusable beside it.
static const auto kBarA     = kImpulseA;                 // Periwinkle (A-side)
static const auto kBarB     = kImpulseB;                 // Yellow (B-side)
static const auto kBarEdit  = juce::Colour(0xff26C6DA);  // Cyan (user-edited offset)
static const auto kBarBg    = juce::Colour(0xff0e0e0e);
static const auto kZeroLine = juce::Colour(0xff2a2a2a);
static constexpr float kMinValueScale = 0.1f;
static constexpr float kScaleHeadroom = 1.05f;
// Mirrors the backend's MAX_DIMENSION_OFFSET (pipe_inference.py) — a dragged offset
// beyond this is silently clamped server-side, so the UI must clamp to the same
// bound or the bar's resting position lies about what the backend will apply.
static constexpr float kMaxOffset = 4.0f;

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
                                      bool preserveOffsets,
                                      float alpha, float magnitude)
{
    embA_ = embA;
    embB_ = embB;
    currentAlpha_ = alpha;
    currentMagnitude_ = magnitude;

    // Symmetric prompt design: B is always meaningful — either typed text,
    // the Spiegelung-am-Modell-Null echo of A, or the model-unconditional
    // when both fields are empty. The backend always returns a non-zero
    // emb_b, so we always treat B as present for visualization purposes.
    hasBPrompt_ = !embB_.empty();

    rebuildBars(baselineValues, preserveOffsets);
    repaint();
}

void DimensionExplorer::setLiveAlphaMagnitude(float alpha, float magnitude)
{
    // Slider drag / Drift tick between generations: re-tint and re-height the
    // existing bars against the new Alpha/Magnitude. Sort order and valueScaleMax_
    // stay fixed from the last rebuildBars — only the per-bar weightedDiff() result
    // (and therefore what's drawn) changes, so bars never reshuffle mid-drift.
    if (std::abs(alpha - currentAlpha_) < 1e-6f && std::abs(magnitude - currentMagnitude_) < 1e-6f)
        return;
    currentAlpha_ = alpha;
    currentMagnitude_ = magnitude;
    if (!bars_.empty())
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
    currentAlpha_ = 0.0f;
    currentMagnitude_ = 1.0f;
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

        // Fit the same quantity the bars plot: the weighted A-B portions (at the
        // Alpha/Magnitude captured just above) + any offset.
        valueScaleMax_ = std::max(valueScaleMax_, std::abs(weightedDiff(bar) + bar.offset));
        if (std::abs(bar.offset) > 1e-8f)
            hasUserEdits_ = true;
    }

    valueScaleMax_ *= kScaleHeadroom;

    // Sort by |weighted A-B portions| descending (most significant first), fixed at
    // this generation's Alpha/Magnitude. Order stays put as Alpha drifts afterward —
    // only setLiveAlphaMagnitude()'s repaint changes what each bar shows.
    std::sort(bars_.begin(), bars_.end(), [this](const Bar& a, const Bar& b) {
        return std::abs(weightedDiff(a)) > std::abs(weightedDiff(b));
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

void DimensionExplorer::setDimensionOffsets(const std::vector<std::pair<int, float>>& offsets)
{
    if (bars_.empty())
        return;

    for (auto& bar : bars_)
        bar.offset = 0.0f;

    for (const auto& [dimIndex, delta] : offsets)
        for (auto& bar : bars_)
            if (bar.dimIndex == dimIndex)
            {
                bar.offset = juce::jlimit(-kMaxOffset, kMaxOffset, delta);
                break;
            }

    hasUserEdits_ = false;
    valueScaleMax_ = kMinValueScale;
    for (auto& bar : bars_)
    {
        // Same scale rule as rebuildBars: fit the weighted A-B portions + any offset.
        valueScaleMax_ = std::max(valueScaleMax_, std::abs(weightedDiff(bar) + bar.offset));
        if (std::abs(bar.offset) > 1e-8f)
            hasUserEdits_ = true;
    }
    valueScaleMax_ *= kScaleHeadroom;

    undoStack_.clear();
    undoStack_.push_back(makeUndoState());
    undoPos_ = 0;
    repaint();
}

float DimensionExplorer::weightedDiff(const Bar& bar) const
{
    // The displayed/manipulated quantity is not the raw, Alpha-independent A-B
    // difference — it's the A and B portions of THE ONE VECTOR the current Alpha
    // blend actually produces (same aWeight/bWeight blend estimateBaselineValues
    // uses for the generated embedding itself, applied to the difference instead
    // of the sum). That's why this keeps changing as Alpha drifts, and why it's
    // still non-zero at Alpha=0 (both weights 0.5, not zero).
    if (!hasBPrompt_)
        return currentMagnitude_ * bar.aValue;
    const float aWeight = 0.5f - 0.5f * currentAlpha_;
    const float bWeight = 0.5f + 0.5f * currentAlpha_;
    return currentMagnitude_ * (aWeight * bar.aValue - bWeight * bar.bValue);
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

float DimensionExplorer::valueToY(float value, float scaleMax) const
{
    float centreY = barArea_.getCentreY();
    // Only called from the overlay bar loop (the mini-view draws its bins directly in
    // paintMiniBins). valueScaleMax_ (with headroom) is the scale passed in, with a
    // smaller margin than full height so dragged bars have room to go higher.
    const float halfH = barArea_.getHeight() * (overlayMode_ ? 0.45f : 0.5f);
    float clampedValue = juce::jlimit(-scaleMax, scaleMax, value);
    return centreY - (clampedValue / scaleMax) * halfH;
}

float DimensionExplorer::yToValue(float y, float scaleMax) const
{
    float centreY = barArea_.getCentreY();
    // Inverse of valueToY for the same mode/scale (drag is overlay-only).
    const float halfH = barArea_.getHeight() * (overlayMode_ ? 0.45f : 0.5f);
    if (halfH < 1.0f) return 0.0f;

    float clampedY = juce::jlimit(barArea_.getY(), barArea_.getBottom(), y);
    return -(clampedY - centreY) / halfH * scaleMax;
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

    // Mini-view renders as a binned focus spectrum; the per-dimension editable
    // console below (zero line + 768 bars + hints + tooltip) is overlay-only.
    if (!overlayMode_)
    {
        paintMiniBins(g);
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

    // This branch only runs in overlay mode (mini-view returned above already), so the
    // stable, headroom-padded scale applies: it doesn't rescale mid-drag.
    const float scaleMax = valueScaleMax_;

    for (int i = 0; i < numBars; ++i)
    {
        auto& bar = bars_[static_cast<size_t>(i)];
        float x = barArea_.getX() + static_cast<float>(i) * barW + barW * gapFrac * 0.5f;
        float w = barW * (1.0f - gapFrac);
        // Same quantity the mini-view sums per bin: the weighted A-B portions of the
        // Alpha-blended vector (bValue's weight is 0 for a single prompt, so this is
        // just magnitude*A there), plus any user-dragged offset. Full-scale and
        // mini-view plot the exact same metric, just per individual dimension vs.
        // binned — see weightedDiff().
        const float displayDiff = weightedDiff(bar) + bar.offset;
        float topY = valueToY(displayDiff, scaleMax);

        // Color: edited (cyan), toward A (periwinkle), toward B (gold)
        juce::Colour col;
        if (std::abs(bar.offset) > 1e-8f)
            col = kBarEdit;
        else if (displayDiff >= 0.0f)
            col = kBarA;
        else
            col = kBarB;

        // Hovered bar is brighter
        if (i == hoveredBar_)
            col = col.brighter(0.3f);

        g.setColour(col.withAlpha(0.85f));
        if (displayDiff >= 0.0f)
            g.fillRect(x, topY, w, centreY - topY);
        else
            g.fillRect(x, centreY, w, topY - centreY);
    }

    // hintFs is shared with the tooltip below so both stay clear of the header band.
    float hintFs = juce::jlimit(10.0f, 15.0f, fs * 0.7f);

    // Axis hints (only in overlay mode with two prompts)
    if (overlayMode_ && hasBPrompt_)
    {
        g.setFont(juce::FontOptions(hintFs).withStyle("Bold"));
        g.setColour(juce::Colour(0x40ffffff));
        // Vertical axis: up = toward A (top-left corner), down = toward B.
        g.drawText("toward A",
                   juce::roundToInt(barArea_.getX() + 4.0f), juce::roundToInt(barArea_.getY() + 2.0f),
                   140, juce::roundToInt(hintFs + 2), juce::Justification::topLeft);
        // Keep "toward B" one line ABOVE the horizontal-axis row so the two no longer
        // collide at the bottom-left corner (they used to be stacked ~2px apart).
        g.drawText("toward B",
                   juce::roundToInt(barArea_.getX() + 4.0f),
                   juce::roundToInt(barArea_.getBottom() - 2.0f * hintFs - 8.0f),
                   140, juce::roundToInt(hintFs + 2), juce::Justification::bottomLeft);
        // Horizontal axis on the bottom row (its pre-rebuild home): left dims are the
        // divergent ones (change the A/B relation), right dims are the shared basis.
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
                + "  shown " + juce::String(weightedDiff(bar), 4);
        }
        else
        {
            tip += ": " + juce::String(bar.aValue, 4);
        }
        if (std::abs(bar.offset) > 1e-8f)
            tip += "  (edit " + juce::String(bar.offset, 4) + ")";

        // Draw the tooltip just INSIDE the top of the plot — one line below "toward A"
        // and always below the header band. The old tipY (barArea top - fs) rendered it
        // up in the header, on top of the "LATENT DIMENSION EXPLORER" title.
        const float tipW = 300.0f;
        float tipX = barArea_.getX() + static_cast<float>(hoveredBar_) * barW;
        tipX = juce::jlimit(barArea_.getX(),
                            juce::jmax(barArea_.getX(), barArea_.getRight() - tipW), tipX);
        float tipY = barArea_.getY() + hintFs + 6.0f;
        g.drawText(tip, juce::roundToInt(tipX), juce::roundToInt(tipY),
                   juce::roundToInt(tipW), juce::roundToInt(fs + 2), juce::Justification::centredLeft);
    }
}

void DimensionExplorer::paintMiniBins(juce::Graphics& g)
{
    // The mini-view is a calm weighted-A-B focus spectrum: the 768 sorted dimensions
    // aggregated into a couple dozen fat bins (tall left = the divergent
    // dimensions, short right = the shared basis), drawn one-sided from a
    // baseline. The A/B side that the overlay carries with up/down is carried
    // by hue here (periwinkle A / gold B) so no height is wasted splitting a
    // ~40px strip; cyan marks a bin that holds a user edit. The full editable
    // per-dimension console lives in the overlay. Bin contents (weightedDiff per
    // dimension) change continuously as Alpha drifts — only the bar ORDER, fixed
    // at the last rebuildBars, stays put.
    const int numDims = static_cast<int>(bars_.size());
    if (numDims == 0 || barArea_.getWidth() <= 0.0f || barArea_.getHeight() <= 0.0f)
        return;

    const float baseY   = barArea_.getBottom();
    const float usableH = juce::jmax(1.0f, barArea_.getHeight() - 2.0f);

    int numBins = juce::jlimit(8, 28, juce::roundToInt(barArea_.getWidth() / 11.0f));
    numBins = juce::jmin(numBins, numDims);

    struct BinAgg { float magSum = 0.0f; float signSum = 0.0f; int count = 0; bool edited = false; };
    std::vector<BinAgg> bins(static_cast<size_t>(numBins));
    for (int i = 0; i < numDims; ++i)
    {
        const int b = juce::jlimit(0, numBins - 1, (i * numBins) / numDims);
        const auto& bar = bars_[static_cast<size_t>(i)];
        // Height encodes the SAME metric the bars are sorted by — the weighted A-B
        // portions of the Alpha-blended vector (see weightedDiff()) — so the
        // spectrum falls off monotonically at the Alpha the dimensions were last
        // sorted at: tall left = the divergent dimensions, short right = the shared
        // basis. Sign picks the leaning side for hue.
        const float div = weightedDiff(bar);
        auto& agg = bins[static_cast<size_t>(b)];
        agg.magSum  += std::abs(div);
        agg.signSum += div;
        ++agg.count;
        if (std::abs(bar.offset) > 1e-8f)
            agg.edited = true;
    }

    // Scale to the strongest bin so the leftmost (most divergent) bin fills the
    // height and the falloff stays legible regardless of absolute magnitude.
    float binMax = 1.0e-6f;
    for (const auto& agg : bins)
        if (agg.count > 0)
            binMax = juce::jmax(binMax, agg.magSum / static_cast<float>(agg.count));

    const float slot    = barArea_.getWidth() / static_cast<float>(numBins);
    const float gapFrac = (slot > 4.0f) ? 0.18f : 0.0f;

    g.setColour(kZeroLine);
    g.drawHorizontalLine(juce::roundToInt(baseY), barArea_.getX(), barArea_.getRight());

    for (int b = 0; b < numBins; ++b)
    {
        const auto& agg = bins[static_cast<size_t>(b)];
        if (agg.count == 0)
            continue;
        const float mag = (agg.magSum / static_cast<float>(agg.count)) / binMax; // 0..1
        const float h   = juce::jlimit(1.0f, usableH, mag * usableH);
        const float x   = barArea_.getX() + static_cast<float>(b) * slot + slot * gapFrac * 0.5f;
        const float w   = juce::jmax(1.0f, slot * (1.0f - gapFrac));

        const juce::Colour col = agg.edited ? kBarEdit
                                            : (agg.signSum >= 0.0f ? kBarA : kBarB);
        g.setColour(col.withAlpha(0.88f));
        g.fillRect(x, baseY - h, w, h);
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
    // Hand-rolled mouse handling must self-gate on enabled state. Unlike a
    // juce::Slider, a disabled custom Component still RECEIVES mouseDown — JUCE
    // hit-testing consults visibility + hitTest() but not isEnabled(). Without
    // this guard, MainPanel's setEnabled(false) grey-out (e.g. deactivating the
    // explorer for SA3) would only dim the panel while a click still opened the
    // overlay and let the user edit offsets.
    if (! isEnabled())
        return;

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

    float newDisplayDiff = yToValue(static_cast<float>(e.y), valueScaleMax_);
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
        float newOffset = juce::jlimit(-kMaxOffset, kMaxOffset, newDisplayDiff - weightedDiff(bar));
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
    if (! isEnabled())
        return;  // no hover highlight / repaint on a disabled (greyed) panel

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
