#pragma once
#include <JuceHeader.h>
#include <array>
#include "WaveformDisplay.h"
#include "GuiHelpers.h"
#include "AftertouchBar.h"
#include "VelocityBar.h"

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
    void followModParamToTab(const juce::String& paramId);  // easy-mode tab follows controller
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
    juce::TextButton modModeToggle { juce::String::fromUTF8("\xc2\xbb adv.") };
    bool modEasyMode = true;
    float modModePulsePhase = 0.0f;
    bool modModePulseActive_ = false;
    // Caches the last 8-bit ARGB applied to modModeToggle. setColour() always
    // triggers an internal repaint; without this guard the pulse cascades 5
    // repaints per 30 Hz tick even when the resulting Colour has not changed.
    juce::Colour lastAppliedModFill_, lastAppliedModText_;
    bool lastAppliedModColoursValid_ = false;

    // ── Layout rects for paint() ──
    juce::Rectangle<int> engineSwitchBounds, loopSwitchBounds, optSwitchBounds;
    juce::Rectangle<int> filterTypeSwitchBounds, filterSlopeSwitchBounds, filterDriveOsSwitchBounds, filterAlgSwitchBounds;
    int engineCardBottom = 0;
    int modCardBottom = 0;
    juce::Rectangle<int> envTabSwitchBounds, lfoTabSwitchBounds, driftTabSwitchBounds;
    juce::Rectangle<int> filterEasyBlockBounds, envEasyBlockBounds, lfoEasyBlockBounds, driftEasyBlockBounds, generateEasyBlockBounds;
    juce::Rectangle<int> aftertouchEasyBlockBounds;
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
    // Easy-mode-only OFF segment, sits left of the algorithm switchbox so the
    // filter can be bypassed even though the type switchbox (OFF LP HP BP) is
    // hidden in Easy. Drives filterTypeHidden → FilterType::Off.
    juce::TextButton filterEasyOffBtn { "OFF" };
    // Easy-mode-only filter TYPE toggle: a single button that cycles LP→HP→BP,
    // sitting right of the slope switch in the cell the 18 dB segment vacates
    // (Easy sacrifices 18 dB for it). Drives filterTypeHidden among
    // Lowpass/Highpass/Bandpass; OFF stays on filterEasyOffBtn. The two together
    // express the full filterType param (Off/LP/HP/BP) without an Advanced-style
    // switchbox.
    juce::TextButton filterEasyTypeBtn { "LP" };
    // 1-based ComboBox id of the last active type, restored when the filter is
    // re-enabled from bypass (= FilterType::Lowpass + 1). Updated on type change.
    int lastEasyFilterType_ = 2;
    // Warp style selector (only active when algorithm == Warp)
    juce::ComboBox filterWarpStyleBox;
    juce::Label    filterWarpStyleLabel { {}, "Style" };

    // Easy-mode Warp style "hold-to-dropdown" button.
    // Click (<350 ms): activate Warp algorithm, keep current style.
    // Hold (≥350 ms): open style picker popup; chosen style becomes new preselection.
    // Sits in the 4th cell of the Easy algorithm row, replacing filterAlgBtns[Warp].
    struct WarpHoldBtn : juce::TextButton, private juce::Timer
    {
        WarpHoldBtn() = default;
        std::function<void()>    onTap;       // short click
        std::function<void(int)> onStylePick; // popup pick, 1-based style id

        void mouseDown (const juce::MouseEvent& e) override;
        void mouseUp   (const juce::MouseEvent& e) override;
        void mouseExit (const juce::MouseEvent& e) override;
        void paintButton(juce::Graphics& g, bool highlighted, bool down) override;
    private:
        void timerCallback() override;
        bool holdTriggered_ = false;
    };
    WarpHoldBtn filterEasyWarpBtn;

    // ── Envelope sections ──
    struct EnvSection
    {
        juce::Label header;
        juce::Label targetHeader;   // easy-view "Target" left-header band
        juce::ComboBox targetBox;
        std::unique_ptr<SliderRow> aRow, dRow, sRow, rRow, amtRow;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aA, dA, sA, rA, amtA;

        // Per-stage velocity sensitivity, signed [-1..+1]: velocity→stage TIME
        // (A/D/R). Three short vertical sliders that serve the ADVANCED grid
        // (alongside amtRow = ENV AMT). In easy view these are replaced by the
        // "Velocity Amount" box below.
        std::unique_ptr<SliderRow> aVsRow, dVsRow, rVsRow;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aVsA, dVsA, rVsA;

        // ── Easy-view "Velocity Amount" box ──
        // Vertical drag-fill bars (AftertouchBar feel). Att/Dec/Rel mirror the
        // velSens above (bipolar, velocity→stage TIME); Level drives the GLOBAL
        // velAmt (unipolar, velocity→peak). Shown in easy view in place of the
        // four velSens/Amt faders; the SliderRows above still serve advanced.
        // velAmt is global, so every env section's Level bar attaches to the same
        // parameter — only the selected env's box is visible at a time.
        ModuleBox velBox;   // framed card + accent top-header, same template as DURATION/RE-PROMPT
        std::unique_ptr<VelocityBar> attVB, decVB, relVB, levelVB;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attVBA, decVBA, relVBA, levelVBA;

        // Curve shape cycling buttons (Log/Lin/Exp) — square icons
        CurveButton aCurveBtn, dCurveBtn, rCurveBtn;
        juce::ComboBox aCurveHidden, dCurveHidden, rCurveHidden;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> aCurveA, dCurveA, rCurveA;

        // Loop toggle (both views): turns the env into a self-retriggering
        // A→D→Hold→R complex-LFO cycle while the note is held. In loop mode the
        // Sustain control is repurposed as the per-cycle Hold time.
        juce::TextButton loopBtn;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopBtnA;

        // Easy-view graphical ADSR editor — replaces the four faders. Declared
        // LAST so it is destroyed FIRST: its dtor detaches listeners from the
        // SliderRows / curve ComboBoxes above while those are still alive.
        std::unique_ptr<AdsrGraph> graph;
    };
    EnvSection ampEnv, mod1Env, mod2Env;
    static constexpr int kNumModTabs = 3;
    static constexpr int kNumWaveBtns = 6;  // sine/tri/saw/sq/s&h/saw-down (mirrors LfoWave + DriftWave)
    static constexpr int kNumLfoModeBtns = 2;
    std::array<juce::TextButton, kNumModTabs> envTabBtns, lfoTabBtns, driftTabBtns;
    int activeEnvTab = 0;
    int activeLfoTab = 0;
    int activeDriftTab = 0;
    uint32_t lastSeenMidiTouchSeq_ = 0;  // tracks processor's midiTouchSeq_ for tab-follow

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
    // Easy-panel AT module: 12 bipolar drag-fill bars (one per target, enum
    // order 1..12) + column header. Bars declared BEFORE their attachments so
    // the attachments destruct first (JUCE reverse-destruction-order rule).
    juce::Label aftertouchHeader;
    std::array<std::unique_ptr<AftertouchBar>, 12> aftertouchBars;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 12> aftertouchBarA;

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
    // Regenerate cadence switchbox: manual / a.s.a.p. / 1 / 2 / 4 / 8 / 16 bars
    juce::Label regenHeader;
    static constexpr int kNumRegenBtns = 7;
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
                 const juce::String& aVsId, const juce::String& dVsId,
                 const juce::String& rVsId,
                 const juce::String& amtId,
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
    void layoutDrift(DriftSection& drift, juce::Rectangle<int>& area, float f, int rowH, int gap);
    void layoutModEasy(juce::Rectangle<int>& area, juce::Rectangle<int> modHeaderRow, float f, int rowH, int gap, int headerH, float headerFs);
    void layoutFilterEasy(juce::Rectangle<int> area, float f, int rowH, int gap);
    void layoutEnvEasy(EnvSection& env, juce::Rectangle<int> area, float f, int rowH, int gap);
    void layoutLfoEasy(LfoSection& lfo, juce::Rectangle<int> area, float f, int rowH, int gap);
    void layoutDriftEasy(DriftSection& drift, juce::Rectangle<int> area, float f, int rowH, int gap);
    void layoutAftertouchEasy(juce::Rectangle<int> area);
    void layoutGenerateEasy(juce::Rectangle<int> area, float f, int rowH, int gap, bool ownHeader = true);

    void setModEasyMode(bool easy, bool persist);
    bool loadModEasyModeSetting() const;
    void saveModEasyModeSetting() const;
    bool hasModHiddenActiveState() const;
    void updateModModeToggleVisual();
    void syncModTabButtons();
    void selectFirstActiveModTabs();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthPanel)
};
