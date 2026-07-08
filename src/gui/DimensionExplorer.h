#pragma once
#include <JuceHeader.h>
#include <vector>
#include <functional>

/**
 * 768-channel mixing console for embedding dimension exploration.
 *
 * Shows vertical bars sorted by |weighted A-B portions| (most significant left) —
 * the A/B portions of the ONE vector the current Alpha blend actually produces,
 * not the raw Alpha-independent A-B difference. This is why bars keep changing
 * as Alpha drifts, and why they're still meaningful (non-zero) at Alpha=0.
 * Periwinkle = A-side, gold = B-side. Draggable bars set per-dimension offsets.
 * Auto-switches: weighted A-B when prompt B present, absolute when single prompt.
 */
class DimensionExplorer : public juce::Component
{
public:
    DimensionExplorer();
    ~DimensionExplorer() override = default;

    /** Estimate the explorer baseline from the last generation settings. */
    static std::vector<float> estimateBaselineValues(
        const std::vector<float>& embA,
        const std::vector<float>& embB,
        float alpha,
        float magnitude,
        const std::vector<std::pair<int, float>>& offsets = {});

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Set embedding data from a generation result. alpha/magnitude are the
        genAlpha/genMagnitude values active at generation time — they fix the
        bar sort order and seed the live display weighting (see setLiveAlphaMagnitude). */
    void setEmbeddings(const std::vector<float>& embA, const std::vector<float>& embB,
                       const std::vector<float>& baselineValues = {},
                       bool preserveOffsets = true,
                       float alpha = 0.0f, float magnitude = 1.0f);

    /** Live-update the Alpha/Magnitude used for the displayed A-B portions, without
        touching bar order or offsets — e.g. a slider drag or Drift tick between
        generations. Bars re-tint/re-height in place; they never reshuffle live. */
    void setLiveAlphaMagnitude(float alpha, float magnitude);

    /** Clear all data and offsets. */
    void clear();

    /** Reset only user-applied offsets and keep the current embeddings visible. */
    void resetOffsets();

    /** Get current dimension offsets (only non-zero entries). */
    std::vector<std::pair<int, float>> getDimensionOffsets() const;

    /** Restore current dimension offsets after embeddings have been set. */
    void setDimensionOffsets(const std::vector<std::pair<int, float>>& offsets);

    /** Returns true when user has made offset edits. */
    bool hasOffsets() const { return hasUserEdits_; }

    /** Undo last edit. */
    void undo();

    /** Redo last undone edit. */
    void redo();

    /** Set overlay mode (interactive bars). In mini-view, clicks just trigger onClicked. */
    void setOverlayMode(bool overlay) { overlayMode_ = overlay; }

    // Callbacks
    std::function<void()> onClicked;          // mini-view clicked → open overlay
    std::function<void()> onApplyGenerate;    // "Anwenden + generieren" pressed

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    static constexpr int kNumDims = 768;

    // Raw embeddings from last generation
    std::vector<float> embA_, embB_;
    bool hasBPrompt_ = false;

    // Sorted bar data
    struct Bar {
        int dimIndex;        // original dimension index (0-767)
        float aValue;        // reference value from prompt A
        float bValue;        // reference value from prompt B (0 if absent)
        float baseActualValue; // baseline value before the user's local edit
        float offset;        // user-applied offset
    };
    std::vector<Bar> bars_;

    // Undo/redo
    struct UndoState { std::vector<float> offsets; }; // dense by dimIndex
    std::vector<UndoState> undoStack_;
    int undoPos_ = -1;  // current position in undo stack
    void pushUndoState();
    UndoState makeUndoState() const;
    void applyUndoState(const UndoState& state);

    bool overlayMode_ = false;
    bool hasUserEdits_ = false;
    int hoveredBar_ = -1;
    int dragBar_ = -1;
    int lastPaintBar_ = -1;
    bool dragDirty_ = false;
    float valueScaleMax_ = 0.1f;
    float currentAlpha_ = 0.0f;
    float currentMagnitude_ = 1.0f;

    void rebuildBars(const std::vector<float>& baselineValues, bool preserveOffsets);
    void paintMiniBins(juce::Graphics& g);   // mini-view: weighted A-B focus spectrum (binned)
    int barAtX(float x) const;
    float valueToY(float value, float scaleMax) const;
    float yToValue(float y, float scaleMax) const;
    float weightedDiff(const Bar& bar) const;
    juce::Rectangle<float> barArea_;  // cached bar drawing area

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DimensionExplorer)
};
