#pragma once
#include <JuceHeader.h>
#include <array>
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
    juce::TextButton freezeBtn { "Granular" };
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

    // ── Granular controls row ──
    static constexpr int kNumFreezeTextureBtns = 4;
    juce::TextButton freezeTextureBtns[kNumFreezeTextureBtns];
    juce::ComboBox freezeTextureHidden;
    juce::Rectangle<int> freezeTextureSwitchBounds;
    std::unique_ptr<SliderRow> freezeStereoRow;

    // ── Octave shift ──
    static constexpr int kNumOctBtns = 5;
    juce::TextButton octBtns[kNumOctBtns];
    juce::ComboBox octaveHidden;
    juce::Rectangle<int> octaveSwitchBounds;

    // ── Noise (shared by all engine modes) ──
    static constexpr int kNumNoiseBtns = 3;
    juce::TextButton noiseBtns[kNumNoiseBtns];
    juce::ComboBox noiseTypeHidden;
    juce::Rectangle<int> noiseSwitchBounds;
    std::unique_ptr<SliderRow> noiseLevelRow;

    // ── Section headers ──
    juce::Label engineHeader, filterHeader, modHeader, lfoHeader, driftHeader;
    juce::TextButton modModeToggle { "> adv." };
    bool modEasyMode = true;
    float modModePulsePhase = 0.0f;

    // ── Layout rects for paint() ──
    juce::Rectangle<int> engineSwitchBounds, loopSwitchBounds, optSwitchBounds;
    juce::Rectangle<int> filterTypeSwitchBounds, filterSlopeSwitchBounds, filterDriveOsSwitchBounds, filterAlgSwitchBounds;
    int engineCardBottom = 0;
    int modCardBottom = 0;
    juce::Rectangle<int> envTabSwitchBounds, lfoTabSwitchBounds, driftTabSwitchBounds;
    juce::Rectangle<int> envEasyBlockBounds, lfoEasyBlockBounds, driftEasyBlockBounds;
    std::array<juce::Rectangle<int>, 3> lfoEasyModuleBounds;
    std::array<juce::Rectangle<int>, 3> driftEasyModuleBounds;

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
    static constexpr int kNumModTabs = 3;
    static constexpr int kNumWaveBtns = 5;
    static constexpr int kNumLfoModeBtns = 2;
    std::array<juce::TextButton, kNumModTabs> envTabBtns, lfoTabBtns, driftTabBtns;
    int activeEnvTab = 0;
    int activeLfoTab = 0;
    int activeDriftTab = 0;

    // ── Clock-button LnFs (shared across the section's clock buttons) ──
    //   Declared BEFORE LfoSection/DriftSection so destruction order is
    //   button-first, LnF-second. Never call setLookAndFeel(nullptr) on
    //   the buttons during teardown.
    ClockButtonLnF lfoClockLnf, driftClockLnf;

    // ── LFO sections ──
    //   modeHidden + clockModeHidden are APVTS-attached (not addAndMakeVisible'd);
    //   modeBtn + clockBtn are the visible 1-cycle controls that drive them.
    //   divisionRow occupies the same screen rect as rateRow — visibility
    //   swaps based on ClockMode.
    struct LfoSection
    {
        juce::Label header;
        juce::ComboBox targetBox, waveBox;
        std::array<juce::TextButton, kNumWaveBtns> waveBtns;
        juce::Rectangle<int> waveSwitchBounds;
        juce::ComboBox modeHidden, clockModeHidden;
        juce::TextButton modeBtn, clockBtn;
        std::array<juce::TextButton, kNumLfoModeBtns> modeBtns;
        juce::Rectangle<int> modeSwitchBounds;
        std::unique_ptr<SliderRow> rateRow, depthRow, divisionRow;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            rateA, depthA, divisionA;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
            waveA, modeA, clockModeA;
    };
    LfoSection lfo1, lfo2, lfo3;

    // ── MIDI performance modulation ──
    juce::Label aftertouchLabel { {}, "AT" };
    juce::ComboBox aftertouchTargetBox;
    std::unique_ptr<SliderRow> aftertouchAmountRow;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aftertouchTargetA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aftertouchAmountA;

    // ── Drift ──
    //   Same dual-control pattern as LFO, minus the F/T mode (Drift has
    //   no Free/Trig).
    struct DriftSection
    {
        juce::Label header;
        juce::ComboBox targetBox, waveBox;
        std::array<juce::TextButton, kNumWaveBtns> waveBtns;
        juce::Rectangle<int> waveSwitchBounds;
        juce::ComboBox clockModeHidden;
        juce::TextButton clockBtn;
        std::unique_ptr<SliderRow> rateRow, depthRow, divisionRow;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
            rateA, depthA, divisionA;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
            targetA, waveA, clockModeA;
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
    std::unique_ptr<CA> freezeTextureA;
    std::unique_ptr<SA> freezeStereoA;
    std::unique_ptr<CA> filterTypeA, filterSlopeA, filterDriveOsA, filterAlgA, filterWarpStyleA;
    std::unique_ptr<SA> filterDriveA;
    std::unique_ptr<CA> driftRegenA;
    std::unique_ptr<SA> crossfadeRegenA;

    // ENV/LFO target attachments (routed in processBlock)
    std::unique_ptr<CA> ampTargetA, mod1TargetA, mod2TargetA, lfo1TargetA, lfo2TargetA, lfo3TargetA;

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
                 const juce::String& clockModeId, const juce::String& divisionId,
                 juce::AudioProcessorValueTreeState& apvts);
    void initDrift(DriftSection& drift, const juce::String& name,
                   const juce::String& rateId, const juce::String& depthId,
                   const juce::String& targetId, const juce::String& waveId,
                   const juce::String& clockModeId, const juce::String& divisionId,
                   juce::AudioProcessorValueTreeState& apvts);

    void layoutEnv(EnvSection& env, juce::Rectangle<int>& area, float f, int rowH, int gap);
    void layoutLfo(LfoSection& lfo, juce::Rectangle<int>& area, float f, int rowH, int gap);
    void layoutAftertouch(juce::Rectangle<int>& area, float f, int rowH, int gap);
    void layoutDrift(DriftSection& drift, juce::Rectangle<int>& area, float f, int rowH, int gap);
    void layoutModEasy(juce::Rectangle<int>& area, float f, int rowH, int gap, int headerH, float headerFs);
    void layoutEnvEasy(EnvSection& env, juce::Rectangle<int> area, float f, int rowH, int gap);
    void layoutLfoEasy(LfoSection& lfo, juce::Rectangle<int> area, float f, int rowH, int gap);
    void layoutDriftEasy(DriftSection& drift, juce::Rectangle<int> area, float f, int rowH, int gap);

    void setModEasyMode(bool easy, bool persist);
    bool loadModEasyModeSetting() const;
    void saveModEasyModeSetting() const;
    bool hasModHiddenActiveState() const;
    void updateModModeToggleVisual();
    void syncModTabButtons();
    void selectFirstActiveModTabs();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthPanel)
};
