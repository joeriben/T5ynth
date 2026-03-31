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
    ~SynthPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Called by MainPanel to toggle DimExplorer overlay. */
    std::function<void()> onExploreClicked;

private:
    void timerCallback() override;
    float fs() const;
    void updateVisibility();
    bool initialized = false;

    T5ynthProcessor& processorRef;

    // ── Engine mode ──
    juce::TextButton samplerBtn { "Sampler" };
    juce::TextButton wavetableBtn { "Wavetable" };
    juce::ComboBox engineModeHidden;
    WaveformDisplay waveformDisplay;

    // ── Loop controls ──
    juce::TextButton oneshotBtn { "One-shot" };
    juce::TextButton loopModeBtn { "Loop" };
    juce::TextButton pingpongBtn { "Ping-Pong" };
    juce::ComboBox loopModeHidden;
    std::unique_ptr<SliderRow> crossfadeRow;
    juce::ToggleButton normalizeToggle { "Normalize" };

    // ── Scan ──
    std::unique_ptr<SliderRow> scanRow;
    juce::Label scanHint;

    // ── Section headers ──
    juce::Label filterHeader, modHeader, driftHeader;

    // ── Filter ──
    juce::ToggleButton filterToggle { "Filter" };
    juce::ComboBox filterTypeBox, filterSlopeBox;
    std::unique_ptr<SliderRow> cutoffRow, resoRow, filterMixRow, kbdTrackRow;

    // ── Envelope sections ──
    struct EnvSection
    {
        juce::Label header;
        juce::ComboBox targetBox;
        juce::ToggleButton loopToggle { "Loop" };
        std::unique_ptr<SliderRow> aRow, dRow, sRow, rRow, amtRow;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aA, dA, sA, rA, amtA;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopA;
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
    LfoSection lfo1, lfo2;

    // ── Drift ──
    struct DriftSection
    {
        juce::Label header;
        juce::ComboBox targetBox, waveBox;
        std::unique_ptr<SliderRow> rateRow, depthRow;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rateA, depthA;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> targetA, waveA;
    };
    DriftSection drift1, drift2;
    juce::ToggleButton driftRegenToggle { "Regen" };

    // ── Explore button ──
    juce::TextButton exploreBtn { "Explore" };

    // ── APVTS attachments ──
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

    std::unique_ptr<CA> engineModeA, loopModeA;
    std::unique_ptr<SA> scanA, cutoffA, resoA, filterMixA, kbdTrackA, crossfadeA;
    std::unique_ptr<CA> filterTypeA, filterSlopeA;
    std::unique_ptr<BA> filterEnableA, normalizeA, driftRegenA;

    // ENV/LFO target attachments (routed in processBlock)
    std::unique_ptr<CA> mod1TargetA, mod2TargetA, lfo1TargetA, lfo2TargetA;

    void initEnv(EnvSection& env, const juce::String& name, int defaultTarget,
                 const juce::String& aId, const juce::String& dId,
                 const juce::String& sId, const juce::String& rId,
                 const juce::String& amtId, const juce::String& loopId,
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
