#pragma once
#include <JuceHeader.h>

/**
 * MODULATION column (20%): ENVs, LFOs, Drift.
 * Rotary knobs with target dropdowns. Hidden when target = "—".
 */
class EffectsPanel : public juce::Component
{
public:
    explicit EffectsPanel(juce::AudioProcessorValueTreeState& apvts);
    ~EffectsPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    float fs() const;

    // Per-envelope section
    struct EnvSection
    {
        juce::Label header;
        juce::ComboBox targetBox;
        juce::Slider a, d, s, r;
        juce::Label aL, dL, sL, rL;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> aA, dA, sA, rA;
    };

    EnvSection ampEnv, mod1Env, mod2Env;

    // Per-LFO section
    struct LfoSection
    {
        juce::Label header;
        juce::ComboBox targetBox, waveBox;
        juce::Slider rate, depth;
        juce::Label rateL, depthL;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rateA, depthA;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> waveA;
    };

    LfoSection lfo1, lfo2;

    // Drift
    juce::ToggleButton driftToggle { "Drift" };
    juce::Slider d1Rate, d1Depth, d2Rate, d2Depth, d3Rate, d3Depth;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> driftEnableA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        d1RA, d1DA, d2RA, d2DA, d3RA, d3DA;

    void initEnv(EnvSection& env, const juce::String& name,
                 const juce::String& aId, const juce::String& dId,
                 const juce::String& sId, const juce::String& rId,
                 juce::AudioProcessorValueTreeState& apvts);
    void initLfo(LfoSection& lfo, const juce::String& name,
                 const juce::String& rateId, const juce::String& depthId,
                 const juce::String& waveId,
                 juce::AudioProcessorValueTreeState& apvts);
    void layoutEnv(EnvSection& env, juce::Rectangle<int>& area, float f, int knobDia);
    void layoutLfo(LfoSection& lfo, juce::Rectangle<int>& area, float f, int knobDia);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectsPanel)
};
