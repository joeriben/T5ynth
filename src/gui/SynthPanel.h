#pragma once
#include <JuceHeader.h>
#include <array>
#include "WaveformDisplay.h"
#include "GuiHelpers.h"
#include "AftertouchBar.h"
#include "VelocityBar.h"
#include "../dsp/BlockParams.h"   // kNumModEnvs, PID, AftertouchTarget

class T5ynthProcessor;

/**
 * Column 2 (55%): ENGINE + FILTER + MODULATION
 * Contains: engine mode, waveform, loop controls, scan, filter,
 *           5 envelopes, 3 LFOs, drift, explore button.
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
    void reconcileWaveformDisplayMode();   // WT fan vs sample view + region label
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
    // 7 visible buttons (Mono, 4, 6, 8, 12, 16, 64). The 128-voice option
    // remains in the backend (BlockParams enum + voiceCounts array) for host
    // automation / presets, but is hidden from the UI — too many voices for
    // typical use.
    static constexpr int kNumVoiceBtns = 7;
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

    // ── Layout rects for paint() ──
    juce::Rectangle<int> engineSwitchBounds, loopSwitchBounds, optSwitchBounds;
    juce::Rectangle<int> filterSlopeSwitchBounds, filterAlgSwitchBounds;
    int engineCardBottom = 0;
    int modCardBottom = 0;
    juce::Rectangle<int> envTabSwitchBounds, lfoTabSwitchBounds, driftTabSwitchBounds;
    juce::Rectangle<int> filterEasyBlockBounds, envEasyBlockBounds, lfoEasyBlockBounds, driftEasyBlockBounds, generateEasyBlockBounds;
    juce::Rectangle<int> aftertouchEasyBlockBounds;
    std::array<juce::Rectangle<int>, 3> lfoEasyModuleBounds;
    std::array<juce::Rectangle<int>, 3> driftEasyModuleBounds;

    // ── Filter ──
    // Type: OFF LP HP BP — drives filter_type APVTS via hidden ComboBox; the
    // visible affordance for PID::filterType: one dropdown, OFF LP HP BP.
    // OFF is a value of the parameter, so bypass and type cannot disagree.
    juce::ComboBox filterTypeBox;
    // Slope switchbox: 6dB 12dB 18dB 24dB
    static constexpr int kNumSlopeBtns = 4;
    juce::TextButton filterSlopeBtns[kNumSlopeBtns];
    juce::ComboBox filterSlopeHidden;
    std::unique_ptr<SliderRow> cutoffRow, resoRow, filterMixRow, kbdTrackRow, filterDriveRow;
    // Drive oversampling: Off 2x 4x 8x — hidden carrier only, no visible
    // switchbox; drive oversampling is controlled elsewhere.
    juce::ComboBox filterDriveOsHidden;
    // Filter algorithm switchbox: SVF Ladder Warp
    static constexpr int kNumAlgBtns = 3;
    juce::TextButton filterAlgBtns[kNumAlgBtns];
    juce::ComboBox filterAlgHidden;
    // Warp style selector (only active when algorithm == Warp)
    juce::ComboBox filterWarpStyleBox;

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
        juce::Label targetHeader;   // "Target" left-header band
        juce::ComboBox targetBox;

        // ── "Velocity Amount" box ──
        // Vertical drag-fill bars (AftertouchBar feel). Att/Dec/Rel are bipolar
        // (velocity→stage TIME); Level drives the GLOBAL velAmt (unipolar,
        // velocity→peak). velAmt is global, so every env section's Level bar
        // attaches to the same parameter — only the selected env's box is
        // visible at a time.
        ModuleBox velBox;   // framed card + accent top-header, same template as DURATION/RE-PROMPT
        std::unique_ptr<VelocityBar> attVB, decVB, relVB, levelVB;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attVBA, decVBA, relVBA, levelVBA;

        // Loop toggle: turns the env into a self-retriggering
        // A→D→Hold→R complex-LFO cycle while the note is held. In loop mode the
        // Sustain control is repurposed as the per-cycle Hold time.
        juce::TextButton loopBtn;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopBtnA;

        // Graphical ADSR editor — attaches directly to the APVTS envelope
        // parameters. Declared LAST so it is destroyed FIRST.
        std::unique_ptr<AdsrGraph> graph;
    };
    EnvSection ampEnv;
    EnvSection modEnvSections[kNumModEnvs];   // ENV 2..5
    static constexpr int kNumModTabs = 1 + kNumModEnvs;   // ENV 1..5; LFO/Drift use the first 3
    static constexpr int kNumWaveBtns = 6;  // sine/tri/saw/sq/s&h/saw-down (mirrors LfoWave + DriftWave)
    static constexpr int kNumLfoModeBtns = 2;
    std::array<juce::TextButton, kNumModTabs> envTabBtns;
    // LFO and Drift still have three each — they share the tab-strip helper,
    // so their arrays are sized alike and only the first kNumLfoTabs are used.
    static constexpr int kNumLfoTabs = 3;
    std::array<juce::TextButton, kNumModTabs> lfoTabBtns, driftTabBtns;
    int activeEnvTab = 0;
    int activeLfoTab = 0;
    int activeDriftTab = 0;
    uint32_t lastSeenMidiTouchSeq_ = 0;  // tracks processor's midiTouchSeq_ for tab-follow

    // ── Clock-button LnFs (shared across the section's clock buttons) ──
    //   Declared BEFORE LfoSection/DriftSection so destruction order is
    //   button-first, LnF-second. Never call setLookAndFeel(nullptr) on
    //   the buttons during teardown.
    ClockButtonLnF lfoClockLnf, driftClockLnf;
    LockButtonLnF  lfoModeLnf;   // Free/Trig padlock (LFO only; Drift has no mode)

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
    // One bar per aftertouch target except None — AftertouchTarget::kCount - 1.
    std::array<std::unique_ptr<AftertouchBar>, AftertouchTarget::kCount - 1> aftertouchBars;
    // Must track aftertouchBars exactly — the two grow together in one loop, and
    // a shorter attachment array writes unique_ptrs over whatever member follows.
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>,
               AftertouchTarget::kCount - 1> aftertouchBarA;
    static_assert(std::tuple_size<decltype(aftertouchBarA)>::value
                      == std::tuple_size<decltype(aftertouchBars)>::value,
                  "aftertouchBarA and aftertouchBars must be the same length.");

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
    std::unique_ptr<CA> ampTargetA, lfo1TargetA, lfo2TargetA, lfo3TargetA;
    std::unique_ptr<CA> modTargetA[kNumModEnvs];

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

    void layoutModEasy(juce::Rectangle<int>& area, juce::Rectangle<int> modHeaderRow, float f, int rowH, int gap, int headerH, float headerFs);
    void layoutFilterEasy(juce::Rectangle<int> area, float f, int rowH, int gap);
    void layoutEnvEasy(EnvSection& env, juce::Rectangle<int> area, float f, int rowH, int gap);
    void layoutLfoEasy(LfoSection& lfo, juce::Rectangle<int> area, float f, int rowH, int gap);
    void layoutDriftEasy(DriftSection& drift, juce::Rectangle<int> area, float f, int rowH, int gap);
    void layoutAftertouchEasy(juce::Rectangle<int> area);
    void layoutGenerateEasy(juce::Rectangle<int> area, float f, int rowH, int gap, bool ownHeader = true);

    void syncModTabButtons();
    void selectFirstActiveModTabs();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthPanel)
};
