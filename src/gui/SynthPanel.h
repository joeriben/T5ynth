#pragma once
#include <JuceHeader.h>
#include "WaveformDisplay.h"
#include "GuiHelpers.h"

class T5ynthProcessor;

/**
 * Column 2 (55%): ENGINE + FILTER + MODULATION
 * Contains: engine mode, waveform, loop controls, scan, filter,
 *           3 envelopes, 2 LFOs, drift, explore button.
 * ALL controls are compact linear slider rows — zero rotary knobs.
 */
class SynthPanel : public juce::Component, private juce::Timer
{
public:
    explicit SynthPanel(T5ynthProcessor& processor);
    ~SynthPanel() override { stopTimer(); }

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    float fs() const;
    void updateVisibility();
    bool initialized = false;
    bool pendingWtReextract_ = false;

    T5ynthProcessor& processorRef;

    // ── Engine mode ──
    juce::TextButton samplerBtn { "Sampler" };
    juce::TextButton wavetableBtn { "Wavetable" };
    juce::ComboBox engineModeHidden;
    WaveformDisplay waveformDisplay;

    // ── Voice count ──
    static constexpr int kNumVoiceBtns = 6;
    juce::TextButton voiceBtns[kNumVoiceBtns];
    juce::ComboBox voiceCountHidden;
    juce::Rectangle<int> voiceSwitchBounds;

    // ── Tuning ──
    juce::ComboBox tuningBox;

    // ── Loop controls ──
    juce::TextButton oneshotBtn;   // → one-shot (icon drawn in paintOverChildren)
    juce::TextButton loopModeBtn;  // ↻ loop
    juce::TextButton pingpongBtn;  // ⇄ ping-pong
    juce::ComboBox loopModeHidden;
    std::unique_ptr<SliderRow> crossfadeRow;
    juce::TextButton loopOptimizeBtn { "Opt: Off" };
    juce::TextButton normalizeToggle { "Norm" };
    juce::TextButton hfBoostBtn { "HF" };

    // ── Scan ──
    std::unique_ptr<SliderRow> scanRow;
    juce::Label scanHint;

    // ── Wavetable controls row ──
    static constexpr int kNumFrameBtns = 4;
    juce::TextButton frameBtns[kNumFrameBtns];
    juce::ComboBox framesHidden;
    juce::Rectangle<int> framesSwitchBounds;
    juce::TextButton smoothToggle { "Smooth" };
    juce::TextButton autoScanToggle { "AutoScan" };
    juce::Label frameCountLabel;

    // ── Octave shift ──
    static constexpr int kNumOctBtns = 5;
    juce::TextButton octBtns[kNumOctBtns];
    juce::ComboBox octaveHidden;
    juce::Rectangle<int> octaveSwitchBounds;

    // ── Noise (shared: both Sampler + Wavetable) ──
    static constexpr int kNumNoiseBtns = 3;
    juce::TextButton noiseBtns[kNumNoiseBtns];
    juce::ComboBox noiseTypeHidden;
    juce::Rectangle<int> noiseSwitchBounds;
    std::unique_ptr<SliderRow> noiseLevelRow;

    // ── Section headers ──
    juce::Label engineHeader, filterHeader, modHeader, lfoHeader, driftHeader;

    // ── Layout rects for paint() ──
    juce::Rectangle<int> engineSwitchBounds, loopSwitchBounds, optSwitchBounds;
    juce::Rectangle<int> filterTypeSwitchBounds, filterSlopeSwitchBounds, filterDriveOsSwitchBounds, filterAlgSwitchBounds;
    int engineCardBottom = 0;

    // ── Filter ──
    // Type switchbox: OFF LP HP BP (drives filter_type APVTS via hidden ComboBox)
    static constexpr int kNumTypeBtns = 4;
    juce::TextButton filterTypeBtns[kNumTypeBtns];
    juce::ComboBox filterTypeHidden;
    // Slope switchbox: 6dB 12dB 18dB 24dB
    static constexpr int kNumSlopeBtns = 4;
    juce::TextButton filterSlopeBtns[kNumSlopeBtns];
    juce::ComboBox filterSlopeHidden;
    std::unique_ptr<SliderRow> cutoffRow, resoRow, filterMixRow, kbdTrackRow, filterDriveRow;
    // Drive oversampling switchbox: Off 2x 4x 8x
    static constexpr int kNumDriveOsBtns = 4;
    juce::TextButton filterDriveOsBtns[kNumDriveOsBtns];
    juce::ComboBox filterDriveOsHidden;
    // Filter algorithm switchbox: SVF Ladder Warp
    static constexpr int kNumAlgBtns = 3;
    juce::TextButton filterAlgBtns[kNumAlgBtns];
    juce::ComboBox filterAlgHidden;
    // Warp style selector (only active when algorithm == Warp)
    juce::ComboBox filterWarpStyleBox;
    juce::Label    filterWarpStyleLabel { {}, "Style" };

    // ── Envelope sections ──
    struct EnvSection
    {
        juce::Label header;
        juce::ComboBox targetBox;
        juce::ToggleButton loopToggle { "Loop" };
        std::unique_ptr<SliderRow> aRow, dRow, sRow, rRow, amtRow, velRow;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aA, dA, sA, rA, amtA, velA;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopA;

        // Curve shape cycling buttons (Log/Lin/Exp) — square icons
        CurveButton aCurveBtn, dCurveBtn, rCurveBtn;
        juce::ComboBox aCurveHidden, dCurveHidden, rCurveHidden;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aCurveA, dCurveA, rCurveA;
        juce::ComboBox aVelModeHidden, dVelModeHidden, rVelModeHidden;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aVelModeA, dVelModeA, rVelModeA;
    };
    EnvSection ampEnv, mod1Env, mod2Env;

    // ── LFO sections ──
    struct LfoSection
    {
        juce::Label header;
        juce::ComboBox targetBox, waveBox, modeBox;
        std::unique_ptr<SliderRow> rateRow, depthRow;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rateA, depthA;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveA, modeA;
    };
    LfoSection lfo1, lfo2, lfo3;

    // ── Drift ──
    struct DriftSection
    {
        juce::Label header;
        juce::ComboBox targetBox, waveBox;
        std::unique_ptr<SliderRow> rateRow, depthRow;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rateA, depthA;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> targetA, waveA;
    };
    DriftSection drift1, drift2, drift3;
    // Regenerate mode switchbox: Manual / Auto / max 1♩ / max 4♩ / max 16♩
    juce::Label regenHeader;
    static constexpr int kNumRegenBtns = 5;
    juce::TextButton regenBtns[kNumRegenBtns];
    juce::ComboBox regenHidden;
    juce::Rectangle<int> regenSwitchBounds;
    std::unique_ptr<SliderRow> crossfadeRegenRow;

    // ── APVTS attachments ──
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<CA> engineModeA, loopModeA, voiceCountA, tuningA;
    std::unique_ptr<SA> scanA, noiseLevelA, cutoffA, resoA, filterMixA, kbdTrackA, crossfadeA;
    std::unique_ptr<CA> noiseTypeA;
    std::unique_ptr<CA> octaveA;
    std::unique_ptr<CA> wtFramesA;
    std::unique_ptr<BA> wtSmoothA;
    std::unique_ptr<BA> wtAutoScanA;
    std::unique_ptr<CA> filterTypeA, filterSlopeA, filterDriveOsA, filterAlgA, filterWarpStyleA;
    std::unique_ptr<SA> filterDriveA;
    std::unique_ptr<CA> driftRegenA;
    std::unique_ptr<SA> crossfadeRegenA;

    // ENV/LFO target attachments (routed in processBlock)
    std::unique_ptr<CA> mod1TargetA, mod2TargetA, lfo1TargetA, lfo2TargetA, lfo3TargetA;

    void initEnv(EnvSection& env, const juce::String& name, int defaultTarget,
                 const juce::String& aId, const juce::String& dId,
                 const juce::String& sId, const juce::String& rId,
                 const juce::String& aCurveId, const juce::String& dCurveId,
                 const juce::String& rCurveId,
                 const juce::String& aVelModeId, const juce::String& dVelModeId,
                 const juce::String& rVelModeId,
                 const juce::String& amtId, const juce::String& velId,
                 const juce::String& loopId,
                 juce::AudioProcessorValueTreeState& apvts);
    void initLfo(LfoSection& lfo, const juce::String& name,
                 const juce::String& rateId, const juce::String& depthId,
                 const juce::String& waveId, const juce::String& modeId,
                 juce::AudioProcessorValueTreeState& apvts);
    void initDrift(DriftSection& drift, const juce::String& name,
                   const juce::String& rateId, const juce::String& depthId,
                   const juce::String& targetId, const juce::String& waveId,
                   juce::AudioProcessorValueTreeState& apvts);

    void layoutEnv(EnvSection& env, juce::Rectangle<int>& area, float f, int rowH, int gap);
    void layoutLfo(LfoSection& lfo, juce::Rectangle<int>& area, float f, int rowH, int gap);
    void layoutDrift(DriftSection& drift, juce::Rectangle<int>& area, float f, int rowH, int gap);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthPanel)
};
