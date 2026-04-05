#include "SynthPanel.h"
#include "../PluginProcessor.h"

// ── Helper: format ms (integer) ──
static juce::String fmtMs(double v)  { return juce::String(juce::roundToInt(v)) + "ms"; }
static juce::String fmtF2(double v)  { return juce::String(v, 2); }
static juce::String fmtPct(double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; }
static juce::String fmtHz(double v)  { return juce::String(juce::roundToInt(v)) + " Hz"; }
static juce::String fmtHzF1(double v){ return juce::String(v, 1) + " Hz"; }
static juce::String fmtHzF2(double v){ return juce::String(v, 2) + " Hz"; }

// ──────────────────────────────────────────────────────────────────────────────
// Envelope init
// ──────────────────────────────────────────────────────────────────────────────
void SynthPanel::initEnv(EnvSection& env, const juce::String& name, int defaultTarget,
                          const juce::String& aId, const juce::String& dId,
                          const juce::String& sId, const juce::String& rId,
                          const juce::String& amtId, const juce::String& velId,
                          const juce::String& loopId,
                          juce::AudioProcessorValueTreeState& apvts)
{
    env.header.setText(name, juce::dontSendNotification);
    env.header.setColour(juce::Label::textColourId, kEnvCol);
    addAndMakeVisible(env.header);

    env.targetBox.addItemList({"---", "DCA", "Filter", "Scan", "Pitch", "Dly Time", "Dly FB", "Dly Mix", "Rev Mix",
                               "LFO1 Rate", "LFO1 Depth", "LFO2 Rate", "LFO2 Depth"}, 1);
    env.targetBox.setSelectedId(defaultTarget, juce::dontSendNotification);
    env.targetBox.onChange = [this] { updateVisibility(); resized(); };
    addAndMakeVisible(env.targetBox);

    env.loopToggle.setColour(juce::ToggleButton::textColourId, kDim);
    env.loopToggle.setColour(juce::ToggleButton::tickColourId, kAccent);
    addAndMakeVisible(env.loopToggle);

    env.aRow   = std::make_unique<SliderRow>("A",   fmtMs, kEnvCol);
    env.dRow   = std::make_unique<SliderRow>("D",   fmtMs, kEnvCol);
    env.sRow   = std::make_unique<SliderRow>("S",   fmtF2, kEnvCol);
    env.rRow   = std::make_unique<SliderRow>("R",   fmtMs, kEnvCol);
    env.amtRow = std::make_unique<SliderRow>("Amt", fmtF2, kEnvCol);
    env.velRow = std::make_unique<SliderRow>("Vel", fmtF2, kEnvCol);

    for (auto* row : { env.aRow.get(), env.dRow.get(), env.sRow.get(),
                       env.rRow.get(), env.amtRow.get(), env.velRow.get() })
        addAndMakeVisible(*row);

    env.aA   = std::make_unique<SA>(apvts, aId,   env.aRow->getSlider());
    env.dA   = std::make_unique<SA>(apvts, dId,   env.dRow->getSlider());
    env.sA   = std::make_unique<SA>(apvts, sId,   env.sRow->getSlider());
    env.rA   = std::make_unique<SA>(apvts, rId,   env.rRow->getSlider());
    env.amtA = std::make_unique<SA>(apvts, amtId, env.amtRow->getSlider());
    env.velA = std::make_unique<SA>(apvts, velId,  env.velRow->getSlider());
    env.loopA = std::make_unique<BA>(apvts, loopId, env.loopToggle);

    // Trigger initial value display
    env.aRow->updateValue();
    env.dRow->updateValue();
    env.sRow->updateValue();
    env.rRow->updateValue();
    env.amtRow->updateValue();
    env.velRow->updateValue();
}

// ──────────────────────────────────────────────────────────────────────────────
// LFO init
// ──────────────────────────────────────────────────────────────────────────────
void SynthPanel::initLfo(LfoSection& lfo, const juce::String& name,
                          const juce::String& rateId, const juce::String& depthId,
                          const juce::String& waveId, const juce::String& modeId,
                          juce::AudioProcessorValueTreeState& apvts)
{
    lfo.header.setText(name, juce::dontSendNotification);
    lfo.header.setColour(juce::Label::textColourId, kLfoCol);
    addAndMakeVisible(lfo.header);

    lfo.targetBox.addItemList({"---", "Filter", "Scan", "Pitch", "Dly Time", "Dly FB", "Dly Mix", "Rev Mix",
                               "ENV1 Amt", "ENV2 Amt", "ENV3 Amt"}, 1);
    lfo.targetBox.setSelectedId(1, juce::dontSendNotification);
    lfo.targetBox.onChange = [this] { updateVisibility(); resized(); };
    addAndMakeVisible(lfo.targetBox);

    lfo.waveBox.addItemList({"Sin", "Tri", "Saw", "Sq", "S&H"}, 1);
    addAndMakeVisible(lfo.waveBox);

    lfo.modeBox.addItemList({"Free", "Trig"}, 1);
    addAndMakeVisible(lfo.modeBox);

    lfo.rateRow  = std::make_unique<SliderRow>("Rate",  fmtHzF1, kLfoCol);
    lfo.depthRow = std::make_unique<SliderRow>("Depth", fmtF2,   kLfoCol);
    addAndMakeVisible(*lfo.rateRow);
    addAndMakeVisible(*lfo.depthRow);

    lfo.rateA  = std::make_unique<SA>(apvts, rateId,  lfo.rateRow->getSlider());
    lfo.depthA = std::make_unique<SA>(apvts, depthId, lfo.depthRow->getSlider());
    lfo.waveA  = std::make_unique<CA>(apvts, waveId,  lfo.waveBox);
    lfo.modeA  = std::make_unique<CA>(apvts, modeId,  lfo.modeBox);

    lfo.rateRow->updateValue();
    lfo.depthRow->updateValue();
}

// ──────────────────────────────────────────────────────────────────────────────
// Drift init
// ──────────────────────────────────────────────────────────────────────────────
void SynthPanel::initDrift(DriftSection& drift, const juce::String& name,
                            const juce::String& rateId, const juce::String& depthId,
                            const juce::String& targetId, const juce::String& waveId,
                            juce::AudioProcessorValueTreeState& apvts)
{
    drift.header.setText(name, juce::dontSendNotification);
    drift.header.setColour(juce::Label::textColourId, kDriftCol);
    addAndMakeVisible(drift.header);

    drift.targetBox.addItemList({"---", "Alpha", "Axis 1", "Axis 2", "Axis 3", "WT Scan",
                                  "Filter", "Pitch", "Dly Time", "Dly FB", "Dly Mix", "Rev Mix",
                                  "ENV1 Amt", "ENV2 Amt", "ENV3 Amt"}, 1);
    drift.targetBox.onChange = [this] { updateVisibility(); resized(); };
    addAndMakeVisible(drift.targetBox);

    drift.waveBox.addItemList({"Sine", "Tri", "Saw", "Sq"}, 1);
    addAndMakeVisible(drift.waveBox);

    drift.rateRow  = std::make_unique<SliderRow>("Rate",  fmtHzF2, kDriftCol);
    drift.depthRow = std::make_unique<SliderRow>("Depth", fmtF2,   kDriftCol);
    addAndMakeVisible(*drift.rateRow);
    addAndMakeVisible(*drift.depthRow);

    drift.rateA  = std::make_unique<SA>(apvts, rateId,   drift.rateRow->getSlider());
    drift.depthA = std::make_unique<SA>(apvts, depthId,  drift.depthRow->getSlider());
    drift.targetA = std::make_unique<CA>(apvts, targetId, drift.targetBox);
    drift.waveA   = std::make_unique<CA>(apvts, waveId,   drift.waveBox);

    drift.rateRow->updateValue();
    drift.depthRow->updateValue();
}

// ──────────────────────────────────────────────────────────────────────────────
// Constructor
// ──────────────────────────────────────────────────────────────────────────────
SynthPanel::SynthPanel(T5ynthProcessor& processor)
    : processorRef(processor)
{
    auto& apvts = processor.getValueTreeState();

    // ── Engine mode ──
    auto styleBtn = [](juce::TextButton& btn, bool on) {
        btn.setColour(juce::TextButton::buttonColourId, on ? juce::Colour(0xff1a2a3a) : kSurface);
        btn.setColour(juce::TextButton::buttonOnColourId, kAccent);
        btn.setColour(juce::TextButton::textColourOffId, kDim);
        btn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn.setClickingTogglesState(true);
        btn.setRadioGroupId(1001);
        btn.setToggleState(on, juce::dontSendNotification);
    };
    styleBtn(samplerBtn, true);
    styleBtn(wavetableBtn, false);
    samplerBtn.setConnectedEdges(juce::Button::ConnectedOnRight);
    wavetableBtn.setConnectedEdges(juce::Button::ConnectedOnLeft);
    addAndMakeVisible(samplerBtn);
    addAndMakeVisible(wavetableBtn);

    engineModeHidden.addItemList({"Sampler", "Wavetable"}, 1);
    engineModeHidden.onChange = [this] {
        bool isSampler = engineModeHidden.getSelectedId() == 1;
        samplerBtn.setToggleState(isSampler, juce::dontSendNotification);
        wavetableBtn.setToggleState(!isSampler, juce::dontSendNotification);
        updateVisibility();
        resized();
    };
    samplerBtn.onClick = [this] { engineModeHidden.setSelectedId(1); };
    wavetableBtn.onClick = [this] { engineModeHidden.setSelectedId(2); };
    engineModeA = std::make_unique<CA>(apvts, "engine_mode", engineModeHidden);

    // ── Voice count switchbox ──
    {
        const juce::StringArray vcLabels { "Mono", "4", "6", "8", "12", "16" };
        voiceCountHidden.addItemList(vcLabels, 1);
        voiceCountHidden.onChange = [this] {
            int id = voiceCountHidden.getSelectedId();
            for (int i = 0; i < kNumVoiceBtns; ++i)
                voiceBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
        };
        for (int i = 0; i < kNumVoiceBtns; ++i)
        {
            voiceBtns[i].setButtonText(vcLabels[i]);
            voiceBtns[i].setColour(juce::TextButton::buttonColourId, kSurface);
            voiceBtns[i].setColour(juce::TextButton::buttonOnColourId, kAccent);
            voiceBtns[i].setColour(juce::TextButton::textColourOffId, kDim);
            voiceBtns[i].setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            voiceBtns[i].setClickingTogglesState(true);
            voiceBtns[i].setRadioGroupId(1002);
            int edges = 0;
            if (i > 0) edges |= juce::Button::ConnectedOnLeft;
            if (i < kNumVoiceBtns - 1) edges |= juce::Button::ConnectedOnRight;
            voiceBtns[i].setConnectedEdges(edges);
            voiceBtns[i].onClick = [this, i] { voiceCountHidden.setSelectedId(i + 1); };
            addAndMakeVisible(voiceBtns[i]);
        }
        voiceCountA = std::make_unique<CA>(apvts, "voice_count", voiceCountHidden);
    }

    addAndMakeVisible(waveformDisplay);

    // Wire bracket handles to sampler + wavetable extraction
    waveformDisplay.onLoopRegionChanged = [this](float start, float end) {
        processorRef.getSampler().setLoopStart(start);
        processorRef.getSampler().setLoopEnd(end);

        // In wavetable mode, re-extract frames from the new region
        if (processorRef.isWavetableMode())
            processorRef.reextractWavetable();
    };

    // Scan position: dragging in WaveformDisplay updates the APVTS slider
    waveformDisplay.onScanChanged = [this](float pos) {
        scanRow->getSlider().setValue(static_cast<double>(pos), juce::sendNotificationSync);
    };

    // ── Loop mode ──
    auto styleLoopBtn = [](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId, kSurface);
        btn.setColour(juce::TextButton::buttonOnColourId, kAccent);
        btn.setColour(juce::TextButton::textColourOffId, kDim);
        btn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn.setClickingTogglesState(true);
        btn.setRadioGroupId(1003);
    };
    styleLoopBtn(oneshotBtn);
    styleLoopBtn(loopModeBtn);
    styleLoopBtn(pingpongBtn);
    oneshotBtn.setConnectedEdges(juce::Button::ConnectedOnRight);
    loopModeBtn.setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    pingpongBtn.setConnectedEdges(juce::Button::ConnectedOnLeft);
    oneshotBtn.setToggleState(true, juce::dontSendNotification);
    oneshotBtn.setTooltip("One-shot");
    loopModeBtn.setTooltip("Loop");
    pingpongBtn.setTooltip("Ping-pong");
    addAndMakeVisible(oneshotBtn);
    addAndMakeVisible(loopModeBtn);
    addAndMakeVisible(pingpongBtn);

    loopModeHidden.addItemList({"One-shot", "Loop", "Ping-Pong"}, 1);
    loopModeHidden.onChange = [this] {
        int id = loopModeHidden.getSelectedId();
        oneshotBtn.setToggleState(id == 1, juce::dontSendNotification);
        loopModeBtn.setToggleState(id == 2, juce::dontSendNotification);
        pingpongBtn.setToggleState(id == 3, juce::dontSendNotification);
        updateVisibility();
        resized();
    };
    oneshotBtn.onClick  = [this] { loopModeHidden.setSelectedId(1); };
    loopModeBtn.onClick = [this] { loopModeHidden.setSelectedId(2); };
    pingpongBtn.onClick = [this] { loopModeHidden.setSelectedId(3); };
    loopModeA = std::make_unique<CA>(apvts, "loop_mode", loopModeHidden);

    // Crossfade
    crossfadeRow = std::make_unique<SliderRow>("Crossfade", fmtMs);
    addAndMakeVisible(*crossfadeRow);
    crossfadeA = std::make_unique<SA>(apvts, "crossfade_ms", crossfadeRow->getSlider());
    crossfadeRow->updateValue();

    // Normalize toggle
    normalizeToggle.setConnectedEdges(juce::Button::ConnectedOnRight);
    normalizeToggle.setColour(juce::TextButton::buttonColourId, kSurface);
    normalizeToggle.setColour(juce::TextButton::buttonOnColourId, kAccent);
    normalizeToggle.setColour(juce::TextButton::textColourOffId, kDim);
    normalizeToggle.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    normalizeToggle.setClickingTogglesState(true);
    addAndMakeVisible(normalizeToggle);
    normalizeToggle.onClick = [this] {
        auto* param = processorRef.getValueTreeState().getParameter("normalize");
        if (param) param->setValueNotifyingHost(normalizeToggle.getToggleState() ? 1.0f : 0.0f);
    };
    normalizeToggle.setToggleState(
        apvts.getRawParameterValue("normalize")->load() > 0.5f, juce::dontSendNotification);

    // Loop optimize cycling button (Off → Low → High)
    loopOptimizeBtn.setConnectedEdges(juce::Button::ConnectedOnLeft);
    loopOptimizeBtn.setColour(juce::TextButton::buttonColourId, kSurface);
    loopOptimizeBtn.setColour(juce::TextButton::textColourOffId, kDim);
    addAndMakeVisible(loopOptimizeBtn);
    {
        int initLevel = static_cast<int>(apvts.getRawParameterValue("loop_optimize")->load());
        static const char* labels[] = { "Opt: Off", "Opt: Low", "Opt: High" };
        loopOptimizeBtn.setButtonText(labels[juce::jlimit(0, 2, initLevel)]);
        loopOptimizeBtn.setToggleState(initLevel > 0, juce::dontSendNotification);
        if (initLevel > 0)
        {
            loopOptimizeBtn.setColour(juce::TextButton::buttonColourId, kAccent);
            loopOptimizeBtn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        }
    }
    loopOptimizeBtn.onClick = [this] {
        auto* param = processorRef.getValueTreeState().getParameter("loop_optimize");
        if (!param) return;
        int cur = static_cast<int>(param->convertFrom0to1(param->getValue()));
        int next = (cur + 1) % 3;
        param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(next)));
        static const char* labels[] = { "Opt: Off", "Opt: Low", "Opt: High" };
        loopOptimizeBtn.setButtonText(labels[next]);
        loopOptimizeBtn.setToggleState(next > 0, juce::dontSendNotification);
        auto col = next > 0 ? kAccent : kSurface;
        auto textCol = next > 0 ? juce::Colours::white : kDim;
        loopOptimizeBtn.setColour(juce::TextButton::buttonColourId, col);
        loopOptimizeBtn.setColour(juce::TextButton::textColourOffId, textCol);
    };

    // ── Scan ──
    scanRow = std::make_unique<SliderRow>("", fmtF2);
    addAndMakeVisible(*scanRow);
    scanHint.setText("Morph between frames (0 = start, 1 = end)", juce::dontSendNotification);
    scanHint.setColour(juce::Label::textColourId, kDimmer);
    addAndMakeVisible(scanHint);
    scanA = std::make_unique<SA>(apvts, "osc_scan", scanRow->getSlider());
    scanRow->updateValue();

    // ── Section headers — inverted (colored bg, dark text) ──
    auto makeHeader = [this](juce::Label& lbl, const juce::String& text, juce::Colour col) {
        lbl.setText(" " + text, juce::dontSendNotification);
        lbl.setColour(juce::Label::textColourId, juce::Colour(0xff0e1018));
        lbl.setColour(juce::Label::backgroundColourId, col.withAlpha(0.7f));
        lbl.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(lbl);
    };
    makeHeader(engineHeader, "ENGINE", kAccent);
    makeHeader(filterHeader, "FILTER", kFilterCol);
    makeHeader(modHeader, "MODULATION", kModCol);

    // ── Filter type switchbox: OFF LP HP BP ──
    {
        const juce::StringArray typeLabels { "OFF", "LP", "HP", "BP" };
        filterTypeHidden.addItemList(typeLabels, 1);
        filterTypeHidden.onChange = [this] {
            int id = filterTypeHidden.getSelectedId();
            for (int i = 0; i < kNumTypeBtns; ++i)
                filterTypeBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
            updateVisibility();
        };
        for (int i = 0; i < kNumTypeBtns; ++i)
        {
            filterTypeBtns[i].setButtonText(typeLabels[i]);
            filterTypeBtns[i].setColour(juce::TextButton::buttonColourId, kSurface);
            filterTypeBtns[i].setColour(juce::TextButton::buttonOnColourId, kFilterCol);
            filterTypeBtns[i].setColour(juce::TextButton::textColourOffId, kDim);
            filterTypeBtns[i].setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            filterTypeBtns[i].setClickingTogglesState(true);
            filterTypeBtns[i].setRadioGroupId(3001);
            { int edges = 0;
              if (i > 0) edges |= juce::Button::ConnectedOnLeft;
              if (i < kNumTypeBtns - 1) edges |= juce::Button::ConnectedOnRight;
              filterTypeBtns[i].setConnectedEdges(edges); }
            filterTypeBtns[i].onClick = [this, i] { filterTypeHidden.setSelectedId(i + 1); };
            addAndMakeVisible(filterTypeBtns[i]);
        }
    }

    // ── Filter slope switchbox: 6dB 12dB 18dB 24dB ──
    {
        const juce::StringArray slopeLabels { "6dB", "12dB", "18dB", "24dB" };
        filterSlopeHidden.addItemList(slopeLabels, 1);
        filterSlopeHidden.onChange = [this] {
            int id = filterSlopeHidden.getSelectedId();
            for (int i = 0; i < kNumSlopeBtns; ++i)
                filterSlopeBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
        };
        for (int i = 0; i < kNumSlopeBtns; ++i)
        {
            filterSlopeBtns[i].setButtonText(slopeLabels[i]);
            filterSlopeBtns[i].setColour(juce::TextButton::buttonColourId, kSurface);
            filterSlopeBtns[i].setColour(juce::TextButton::buttonOnColourId, kFilterCol);
            filterSlopeBtns[i].setColour(juce::TextButton::textColourOffId, kDim);
            filterSlopeBtns[i].setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            filterSlopeBtns[i].setClickingTogglesState(true);
            filterSlopeBtns[i].setRadioGroupId(3002);
            { int edges = 0;
              if (i > 0) edges |= juce::Button::ConnectedOnLeft;
              if (i < kNumSlopeBtns - 1) edges |= juce::Button::ConnectedOnRight;
              filterSlopeBtns[i].setConnectedEdges(edges); }
            filterSlopeBtns[i].onClick = [this, i] { filterSlopeHidden.setSelectedId(i + 1); };
            addAndMakeVisible(filterSlopeBtns[i]);
        }
    }

    cutoffRow    = std::make_unique<SliderRow>("Cutoff",    fmtHz,  kFilterCol);
    resoRow      = std::make_unique<SliderRow>("Resonance", fmtF2, kFilterCol);
    filterMixRow = std::make_unique<SliderRow>("Mix",       fmtPct, kFilterCol);
    kbdTrackRow  = std::make_unique<SliderRow>("Kbd Track", fmtPct, kFilterCol);
    for (auto* r : { cutoffRow.get(), resoRow.get(), filterMixRow.get(), kbdTrackRow.get() })
        addAndMakeVisible(*r);

    cutoffA    = std::make_unique<SA>(apvts, "filter_cutoff",    cutoffRow->getSlider());
    resoA      = std::make_unique<SA>(apvts, "filter_resonance", resoRow->getSlider());
    filterMixA = std::make_unique<SA>(apvts, "filter_mix",       filterMixRow->getSlider());
    kbdTrackA  = std::make_unique<SA>(apvts, "filter_kbd_track", kbdTrackRow->getSlider());

    filterTypeA  = std::make_unique<CA>(apvts, "filter_type",  filterTypeHidden);
    filterSlopeA = std::make_unique<CA>(apvts, "filter_slope", filterSlopeHidden);

    cutoffRow->updateValue();
    resoRow->updateValue();
    filterMixRow->updateValue();
    kbdTrackRow->updateValue();

    // ── Envelopes ──
    initEnv(ampEnv,  "ENV 1", 2, "amp_attack",  "amp_decay",  "amp_sustain",  "amp_release",  "amp_amount",  "amp_vel_sens",  "amp_loop",  apvts);
    initEnv(mod1Env, "ENV 2", 1, "mod1_attack", "mod1_decay", "mod1_sustain", "mod1_release", "mod1_amount", "mod1_vel_sens", "mod1_loop", apvts);
    initEnv(mod2Env, "ENV 3", 1, "mod2_attack", "mod2_decay", "mod2_sustain", "mod2_release", "mod2_amount", "mod2_vel_sens", "mod2_loop", apvts);

    // ── LFOs ──
    initLfo(lfo1, "LFO 1", "lfo1_rate", "lfo1_depth", "lfo1_wave", "lfo1_mode", apvts);
    initLfo(lfo2, "LFO 2", "lfo2_rate", "lfo2_depth", "lfo2_wave", "lfo2_mode", apvts);

    // ── Drift ──
    initDrift(drift1, "DRIFT 1", "drift1_rate", "drift1_depth", "drift1_target", "drift1_wave", apvts);
    initDrift(drift2, "DRIFT 2", "drift2_rate", "drift2_depth", "drift2_target", "drift2_wave", apvts);

    // Regenerate mode switchbox
    paintSectionHeader(regenHeader, "DRIFT + REGENERATE", kDriftCol);
    addAndMakeVisible(regenHeader);

    regenHidden.addItemList({"Manual", "Auto", "1st Bar"}, 1);
    regenHidden.onChange = [this] {
        int id = regenHidden.getSelectedId();
        for (int i = 0; i < kNumRegenBtns; ++i)
            regenBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
    };
    static const char* regenLabels[] = {"Manual", "Auto", "1st Bar"};
    for (int i = 0; i < kNumRegenBtns; ++i)
    {
        regenBtns[i].setButtonText(regenLabels[i]);
        regenBtns[i].setColour(juce::TextButton::buttonColourId, kSurface);
        regenBtns[i].setColour(juce::TextButton::buttonOnColourId, kDriftCol);
        regenBtns[i].setColour(juce::TextButton::textColourOffId, kDim);
        regenBtns[i].setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        regenBtns[i].setClickingTogglesState(true);
        regenBtns[i].setRadioGroupId(3005);
        regenBtns[i].onClick = [this, i] { regenHidden.setSelectedId(i + 1); };
        addAndMakeVisible(regenBtns[i]);
    }
    driftRegenA = std::make_unique<CA>(apvts, "drift_regen", regenHidden);

    // Crossfade slider for drift regeneration
    crossfadeRegenRow = std::make_unique<SliderRow>("XFade", fmtMs, kDriftCol);
    addAndMakeVisible(*crossfadeRegenRow);
    crossfadeRegenA = std::make_unique<SA>(apvts, "drift_crossfade", crossfadeRegenRow->getSlider());
    crossfadeRegenRow->updateValue();

    // All components are now set up — enable callbacks and trigger initial state
    initialized = true;

    // Deferred APVTS attachments for target ComboBoxes
    // (must come after initialized=true so onChange → updateVisibility works)
    mod1TargetA = std::make_unique<CA>(apvts, "mod1_target", mod1Env.targetBox);
    mod2TargetA = std::make_unique<CA>(apvts, "mod2_target", mod2Env.targetBox);
    lfo1TargetA = std::make_unique<CA>(apvts, "lfo1_target", lfo1.targetBox);
    lfo2TargetA = std::make_unique<CA>(apvts, "lfo2_target", lfo2.targetBox);

    updateVisibility();
    startTimerHz(10);
}

// ──────────────────────────────────────────────────────────────────────────────
// Waveform display polling
// ──────────────────────────────────────────────────────────────────────────────
void SynthPanel::timerCallback()
{
    if (processorRef.hasNewWaveform())
    {
        auto& snap = processorRef.getWaveformSnapshot();
        int numSamples = snap.getNumSamples();
        if (numSamples > 0)
        {
            const int displayPoints = 1024;
            const float* src = snap.getReadPointer(0);

            if (numSamples > displayPoints * 2)
            {
                // Peak-preserving downsample
                std::vector<float> display(displayPoints);
                int bucketSize = numSamples / displayPoints;
                for (int i = 0; i < displayPoints; ++i)
                {
                    int start = i * bucketSize;
                    int end = juce::jmin(start + bucketSize, numSamples);
                    float peak = 0.0f;
                    int peakIdx = start;
                    for (int j = start; j < end; ++j)
                    {
                        float absVal = std::abs(src[j]);
                        if (absVal > peak) { peak = absVal; peakIdx = j; }
                    }
                    display[static_cast<size_t>(i)] = src[peakIdx];
                }
                waveformDisplay.setWaveform(display.data(), displayPoints);
            }
            else
            {
                waveformDisplay.setWaveform(src, numSamples);
            }
        }
        // Update buffer duration for time labels
        double sr = processorRef.getSampleRate();
        if (sr > 0)
            waveformDisplay.setBufferDuration(static_cast<float>(numSamples / sr));

        // Sync auto-positioned brackets from processor (wavetable mode)
        if (processorRef.isWavetableMode())
        {
            float s = processorRef.getSampler().getLoopStart();
            float e = processorRef.getSampler().getLoopEnd();
            waveformDisplay.setLoopStart(s);
            waveformDisplay.setLoopEnd(e);
        }

        processorRef.clearNewWaveformFlag();
    }

    // Update ghost indicators from modulated values (skip when audio is idle)
    if (!processorRef.audioIdle.load(std::memory_order_relaxed))
    {
        auto& mv = processorRef.modulatedValues;
        cutoffRow->setGhostValue(mv.filterCutoff.load(std::memory_order_relaxed));
        if (lfo1.rateRow)  lfo1.rateRow->setGhostValue(mv.lfo1Rate.load(std::memory_order_relaxed));
        if (lfo1.depthRow) lfo1.depthRow->setGhostValue(mv.lfo1Depth.load(std::memory_order_relaxed));
        if (lfo2.rateRow)  lfo2.rateRow->setGhostValue(mv.lfo2Rate.load(std::memory_order_relaxed));
        if (lfo2.depthRow) lfo2.depthRow->setGhostValue(mv.lfo2Depth.load(std::memory_order_relaxed));
        if (waveformDisplay.isVisible())
            waveformDisplay.setScanPosition(mv.scanPosition.load(std::memory_order_relaxed));
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Visibility
// ──────────────────────────────────────────────────────────────────────────────
void SynthPanel::updateVisibility()
{
    if (!initialized) return;

    // All sections always visible — inactive ones get dimmed via alpha
    constexpr float dimAlpha = 0.3f;

    bool filterOn = (filterTypeHidden.getSelectedId() > 1);  // 1=OFF, 2+=LP/HP/BP
    float filterAlpha = filterOn ? 1.0f : dimAlpha;
    for (int i = 0; i < kNumSlopeBtns; ++i)
    {
        filterSlopeBtns[i].setAlpha(filterAlpha);
        filterSlopeBtns[i].setEnabled(filterOn);
    }
    for (auto* r : { cutoffRow.get(), resoRow.get(), filterMixRow.get(), kbdTrackRow.get() })
    {
        r->setAlpha(filterAlpha);
        r->setEnabled(filterOn);
    }

    bool isWavetable = engineModeHidden.getSelectedId() == 2;
    bool isSampler = !isWavetable;

    // Sampler-only controls
    oneshotBtn.setVisible(isSampler);
    loopModeBtn.setVisible(isSampler);
    pingpongBtn.setVisible(isSampler);
    crossfadeRow->setVisible(isSampler);
    loopOptimizeBtn.setVisible(isSampler);
    normalizeToggle.setVisible(isSampler);

    // Wavetable-only controls
    scanRow->setVisible(isWavetable);
    scanHint.setVisible(isWavetable);

    // Waveform label changes with mode
    waveformDisplay.setRegionLabel(isWavetable ? "Extraction region" : "Loop interval");

    if (isSampler)
    {
        bool isOneshot = loopModeHidden.getSelectedId() == 1;
        crossfadeRow->setAlpha(isOneshot ? dimAlpha : 1.0f);
        crossfadeRow->setEnabled(!isOneshot);
    }

    auto setEnvDimmed = [dimAlpha](EnvSection& env) {
        bool active = env.targetBox.getSelectedId() != 1; // 1 = "---"
        float alpha = active ? 1.0f : dimAlpha;
        env.loopToggle.setAlpha(alpha);
        env.loopToggle.setEnabled(active);
        for (auto* r : { env.aRow.get(), env.dRow.get(), env.sRow.get(),
                         env.rRow.get(), env.amtRow.get(), env.velRow.get() })
            if (r) { r->setAlpha(alpha); r->setEnabled(active); }
    };
    setEnvDimmed(ampEnv);
    setEnvDimmed(mod1Env);
    setEnvDimmed(mod2Env);

    auto setLfoDimmed = [dimAlpha](LfoSection& lfo) {
        bool active = lfo.targetBox.getSelectedId() != 1; // 1 = "---"
        float alpha = active ? 1.0f : dimAlpha;
        lfo.waveBox.setAlpha(alpha);
        lfo.modeBox.setAlpha(alpha);
        lfo.waveBox.setEnabled(active);
        lfo.modeBox.setEnabled(active);
        if (lfo.rateRow)  { lfo.rateRow->setAlpha(alpha);  lfo.rateRow->setEnabled(active); }
        if (lfo.depthRow) { lfo.depthRow->setAlpha(alpha); lfo.depthRow->setEnabled(active); }
    };
    setLfoDimmed(lfo1);
    setLfoDimmed(lfo2);

    auto setDriftDimmed = [dimAlpha](DriftSection& drift) {
        bool active = drift.targetBox.getSelectedId() != 1; // 1 = "---"
        float alpha = active ? 1.0f : dimAlpha;
        drift.waveBox.setAlpha(alpha);
        drift.waveBox.setEnabled(active);
        if (drift.rateRow)  { drift.rateRow->setAlpha(alpha);  drift.rateRow->setEnabled(active); }
        if (drift.depthRow) { drift.depthRow->setAlpha(alpha); drift.depthRow->setEnabled(active); }
    };
    setDriftDimmed(drift1);
    setDriftDimmed(drift2);

    // Regen buttons only active when a drift target requires audio regeneration
    // Osc targets: Alpha(2), Axis1(3), Axis2(4), Axis3(5) in ComboBox 1-based IDs
    {
        auto isOscTarget = [](int selId) {
            int tgt = selId - 1; // 1-based → 0-based APVTS index
            return tgt >= 1 && tgt <= 4;
        };
        bool regenAvailable = isOscTarget(drift1.targetBox.getSelectedId())
                           || isOscTarget(drift2.targetBox.getSelectedId());
        float regenAlpha = regenAvailable ? 1.0f : dimAlpha;
        for (int i = 0; i < kNumRegenBtns; ++i)
        {
            regenBtns[i].setAlpha(regenAlpha);
            regenBtns[i].setEnabled(regenAvailable);
        }
        regenHeader.setAlpha(regenAlpha);
        if (crossfadeRegenRow)
        {
            crossfadeRegenRow->setAlpha(regenAlpha);
            crossfadeRegenRow->setEnabled(regenAvailable);
        }
    }
}

float SynthPanel::fs() const
{
    // Derive font size from available height so all content fits.
    // Content budget: ~53 f-units (27 rows * 1.4 + headers + gaps)
    // plus waveform at 12% of panel height.
    float h = static_cast<float>(getHeight());
    float padY = h * 0.005f;
    float available = h - 2.0f * padY;
    float waveform = h * 0.12f;
    float remaining = available - waveform;
    float maxF = remaining / 53.0f;
    return juce::jlimit(9.0f, 20.0f, maxF);
}

// ──────────────────────────────────────────────────────────────────────────────
// Layout helpers
// ──────────────────────────────────────────────────────────────────────────────
void SynthPanel::layoutEnv(EnvSection& env, juce::Rectangle<int>& area, float f, int rowH, int gap)
{
    env.header.setFont(juce::FontOptions(f));
    auto hdr = area.removeFromTop(rowH);
    int headerW = juce::roundToInt(hdr.getWidth() * 0.18f);
    int targetW = juce::roundToInt(hdr.getWidth() * 0.28f);
    env.header.setBounds(hdr.removeFromLeft(headerW));
    env.targetBox.setBounds(hdr.removeFromLeft(targetW));
    hdr.removeFromLeft(4);
    env.loopToggle.setBounds(hdr.removeFromLeft(juce::roundToInt(hdr.getWidth() * 0.4f)));

    // Always allocate space — inactive sections are dimmed, not hidden
    int colW = (area.getWidth() - 4) / 2;

    auto adRow = area.removeFromTop(rowH);
    env.aRow->setBounds(adRow.removeFromLeft(colW));
    adRow.removeFromLeft(4);
    env.dRow->setBounds(adRow);

    auto srRow = area.removeFromTop(rowH);
    env.sRow->setBounds(srRow.removeFromLeft(colW));
    srRow.removeFromLeft(4);
    env.rRow->setBounds(srRow);

    auto amtRow = area.removeFromTop(rowH);
    env.amtRow->setBounds(amtRow.removeFromLeft(colW));
    amtRow.removeFromLeft(4);
    env.velRow->setBounds(amtRow);

    area.removeFromTop(gap);
}

void SynthPanel::layoutLfo(LfoSection& lfo, juce::Rectangle<int>& area, float f, int rowH, int gap)
{
    lfo.header.setFont(juce::FontOptions(f));
    auto hdr = area.removeFromTop(rowH);
    int headerW = juce::roundToInt(hdr.getWidth() * 0.18f);
    int targetW = juce::roundToInt(hdr.getWidth() * 0.22f);
    int waveW = juce::roundToInt(hdr.getWidth() * 0.18f);
    int modeW = juce::roundToInt(hdr.getWidth() * 0.15f);

    lfo.header.setBounds(hdr.removeFromLeft(headerW));
    lfo.targetBox.setBounds(hdr.removeFromLeft(targetW));
    hdr.removeFromLeft(4);
    lfo.waveBox.setBounds(hdr.removeFromLeft(waveW));
    hdr.removeFromLeft(4);
    lfo.modeBox.setBounds(hdr.removeFromLeft(modeW));

    // Always allocate space
    int colW = (area.getWidth() - 4) / 2;
    auto slRow = area.removeFromTop(rowH);
    lfo.rateRow->setBounds(slRow.removeFromLeft(colW));
    slRow.removeFromLeft(4);
    lfo.depthRow->setBounds(slRow);

    area.removeFromTop(gap);
}

void SynthPanel::layoutDrift(DriftSection& drift, juce::Rectangle<int>& area, float f, int rowH, int gap)
{
    drift.header.setFont(juce::FontOptions(f));
    auto hdr = area.removeFromTop(rowH);
    int headerW = juce::roundToInt(hdr.getWidth() * 0.22f);
    int targetW = juce::roundToInt(hdr.getWidth() * 0.28f);
    int waveW = juce::roundToInt(hdr.getWidth() * 0.18f);

    drift.header.setBounds(hdr.removeFromLeft(headerW));
    drift.targetBox.setBounds(hdr.removeFromLeft(targetW));
    hdr.removeFromLeft(4);
    drift.waveBox.setBounds(hdr.removeFromLeft(waveW));

    // Always allocate space
    int colW = (area.getWidth() - 4) / 2;
    auto slRow = area.removeFromTop(rowH);
    drift.rateRow->setBounds(slRow.removeFromLeft(colW));
    slRow.removeFromLeft(4);
    drift.depthRow->setBounds(slRow);

    area.removeFromTop(gap);
}

// ──────────────────────────────────────────────────────────────────────────────
// Paint
// ──────────────────────────────────────────────────────────────────────────────
void SynthPanel::paint(juce::Graphics& g)
{
    g.fillAll(kBg);
    if (!initialized) return;

    int padX = juce::roundToInt(static_cast<float>(getWidth()) * 0.02f);
    int inset = 4;

    // Card: Engine mode + Waveform + Loop controls + Scan
    {
        int top = engineHeader.getY() - inset;
        int bot = engineCardBottom + inset;
        paintCard(g, juce::Rectangle<int>(padX, top, getWidth() - padX * 2, bot - top));

        // Switchbox borders
        paintSwitchBoxBorder(g, engineSwitchBounds);
        paintSwitchBoxBorder(g, voiceSwitchBounds);
        if (oneshotBtn.isVisible())
        {
            paintSwitchBoxBorder(g, loopSwitchBounds);
            paintSwitchBoxBorder(g, optSwitchBounds);
        }
    }

    // Card: Filter section
    {
        int top = filterHeader.getY() - inset;
        int bot = kbdTrackRow->getBottom();
        paintCard(g, juce::Rectangle<int>(padX, top, getWidth() - padX * 2, bot - top + inset));

        // Filter switchbox borders
        paintSwitchBoxBorder(g, filterTypeSwitchBounds);
        paintSwitchBoxBorder(g, filterSlopeSwitchBounds);
    }

    // Card: Modulation (ENVs + LFOs + Drift)
    {
        int top = modHeader.getY() - inset;
        int bot = juce::jmax(ampEnv.amtRow->getBottom(), mod1Env.amtRow->getBottom(),
                             mod2Env.amtRow->getBottom());
        bot = juce::jmax(bot, lfo1.depthRow->getBottom(), lfo2.depthRow->getBottom());
        bot = juce::jmax(bot, drift1.depthRow->getBottom(), drift2.depthRow->getBottom());
        paintCard(g, juce::Rectangle<int>(padX, top, getWidth() - padX * 2, bot - top + inset));

        // Subtle separator lines between sub-sections
        g.setColour(kModCol.withAlpha(0.15f));
        int lineL = padX + 8;
        int lineR = getWidth() - padX - 8;
        for (auto* hdr : { &mod1Env.header, &mod2Env.header, &lfo1.header, &lfo2.header })
        {
            int y = hdr->getY() - 2;
            g.drawHorizontalLine(y, static_cast<float>(lineL), static_cast<float>(lineR));
        }
        // Drift separator + switchbox border
        g.setColour(kDriftCol.withAlpha(0.15f));
        int driftY = regenHeader.getY() - 2;
        g.drawHorizontalLine(driftY, static_cast<float>(lineL), static_cast<float>(lineR));
        paintSwitchBoxBorder(g, regenSwitchBounds);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Play-mode icon drawing (painted over children so icons sit on top of buttons)
// ──────────────────────────────────────────────────────────────────────────────
void SynthPanel::paintOverChildren(juce::Graphics& g)
{
    if (!oneshotBtn.isVisible()) return;

    auto iconCol = [](const juce::TextButton& btn) {
        return btn.getToggleState() ? juce::Colours::white : kDim;
    };

    // → One-shot: forward arrow
    {
        auto b = oneshotBtn.getBounds().toFloat();
        float cx = b.getCentreX(), cy = b.getCentreY();
        float hw = b.getHeight() * 0.28f;
        float sw = juce::jmax(1.5f, hw * 0.2f);
        float as = hw * 0.7f;

        g.setColour(iconCol(oneshotBtn));
        g.drawLine(cx - hw, cy, cx + hw, cy, sw);
        juce::Path ah;
        ah.addTriangle(cx + hw + as * 0.3f, cy,
                       cx + hw - as * 0.5f, cy - as * 0.5f,
                       cx + hw - as * 0.5f, cy + as * 0.5f);
        g.fillPath(ah);
    }

    // ↻ Loop: circular arc with arrowhead
    {
        auto b = loopModeBtn.getBounds().toFloat();
        float cx = b.getCentreX(), cy = b.getCentreY();
        float r = b.getHeight() * 0.3f;
        float sw = juce::jmax(1.5f, r * 0.22f);

        juce::Path arc;
        float startA = -juce::MathConstants<float>::halfPi;
        float endA = startA + juce::MathConstants<float>::twoPi * 0.78f;
        arc.addCentredArc(cx, cy, r, r, 0.0f, startA, endA, true);
        g.setColour(iconCol(loopModeBtn));
        g.strokePath(arc, juce::PathStrokeType(sw, juce::PathStrokeType::curved));

        float ex = cx + r * std::cos(endA);
        float ey = cy + r * std::sin(endA);
        float as = r * 0.55f;
        float tx = -std::sin(endA), ty = std::cos(endA);
        float nx = std::cos(endA),  ny = std::sin(endA);
        juce::Path ah;
        ah.addTriangle(ex + tx * as * 0.6f, ey + ty * as * 0.6f,
                       ex - nx * as * 0.45f, ey - ny * as * 0.45f,
                       ex + nx * as * 0.45f, ey + ny * as * 0.45f);
        g.fillPath(ah);
    }

    // ⇄ Ping-pong: two opposing arrows (→ above, ← below)
    {
        auto b = pingpongBtn.getBounds().toFloat();
        float cx = b.getCentreX(), cy = b.getCentreY();
        float hw = b.getWidth() * 0.28f;
        float voff = b.getHeight() * 0.15f;
        float sw = juce::jmax(1.5f, b.getHeight() * 0.09f);
        float as = b.getHeight() * 0.18f;

        g.setColour(iconCol(pingpongBtn));

        // Top: → right arrow
        float ty = cy - voff;
        g.drawLine(cx - hw, ty, cx + hw, ty, sw);
        juce::Path ra;
        ra.addTriangle(cx + hw + as * 0.3f, ty,
                       cx + hw - as * 0.5f, ty - as * 0.65f,
                       cx + hw - as * 0.5f, ty + as * 0.65f);
        g.fillPath(ra);

        // Bottom: ← left arrow
        float by = cy + voff;
        g.drawLine(cx + hw, by, cx - hw, by, sw);
        juce::Path la;
        la.addTriangle(cx - hw - as * 0.3f, by,
                       cx - hw + as * 0.5f, by - as * 0.65f,
                       cx - hw + as * 0.5f, by + as * 0.65f);
        g.fillPath(la);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Resized
// ──────────────────────────────────────────────────────────────────────────────
void SynthPanel::resized()
{
    if (!initialized) return;
    updateVisibility();

    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());
    int padX = juce::roundToInt(w * 0.02f);
    int padY = juce::roundToInt(h * 0.005f);
    auto area = getLocalBounds().reduced(padX, padY);
    float f = fs();
    int rowH = juce::roundToInt(f * 1.4f);
    int gap = juce::roundToInt(f * 0.25f);

    // ── Section header — derived from window height to match left column ──
    float topH = getTopLevelComponent()
                     ? static_cast<float>(getTopLevelComponent()->getHeight()) : 800.0f;
    int headerH = juce::jlimit(14, 20, juce::roundToInt(topH * 0.022f));
    float headerFs = static_cast<float>(headerH) * 0.85f;
    int headerGap = juce::jmax(3, headerH / 5);  // ~20% of header height
    engineHeader.setFont(juce::FontOptions(headerFs));
    engineHeader.setBounds(area.removeFromTop(headerH));
    area.removeFromTop(headerGap);

    // ── Engine mode + Voice count: compact switchboxes ──
    auto modeRow = area.removeFromTop(rowH);
    {
        int cellW = juce::roundToInt(f * 5.0f);
        samplerBtn.setBounds(modeRow.removeFromLeft(cellW));
        wavetableBtn.setBounds(modeRow.removeFromLeft(cellW));
        engineSwitchBounds = samplerBtn.getBounds().getUnion(wavetableBtn.getBounds());

        modeRow.removeFromLeft(juce::roundToInt(f * 1.5f)); // gap
        int vcW = juce::roundToInt(f * 2.8f);
        for (int i = 0; i < kNumVoiceBtns; ++i)
            voiceBtns[i].setBounds(modeRow.removeFromLeft(vcW));
        voiceSwitchBounds = voiceBtns[0].getBounds().getUnion(voiceBtns[kNumVoiceBtns - 1].getBounds());
    }
    area.removeFromTop(gap);

    // ── Waveform — give it all space not needed by sections below ──
    // Calculate height needed below: sampler/WT controls + filter + mod + LFO + drift
    // Always reserve same space for engine controls (max of sampler/WT)
    // so waveform height stays stable when switching modes
    int samplerCtrlH = rowH + gap * 2;
    int filterH = headerH + headerGap + rowH + gap + rowH * 2 + gap; // header + type row + cutoff/reso + mix/kbd
    int modH = gap * 3 + headerH + headerGap; // section gap + header
    int envH = (rowH * 4 + gap) * 3; // 3 envelopes × (header + 3 slider rows + gap)
    int lfoH = gap + (rowH * 2 + gap) * 2; // 2 LFOs × (header + rate row + gap)
    int driftH = gap + headerH + gap + rowH + (rowH * 2 + gap) * 2; // header + regen row + 2 drifts
    int belowWave = samplerCtrlH + filterH + modH + envH + lfoH + driftH + gap * 5;
    int waveH = juce::jmax(60, area.getHeight() - belowWave);

    if (scanRow->isVisible())
    {
        // ── Wavetable: scan dot + brackets on one line below waveform ──
        int scanLineH = juce::roundToInt(WaveformDisplay::HANDLE_RADIUS * 2.0f + 4.0f);
        waveformDisplay.setBottomReserve(scanLineH);
        waveformDisplay.setScanVisible(true);
        waveformDisplay.setBounds(area.removeFromTop(waveH + scanLineH));

        // Hide the scanRow slider (APVTS still connected), scan is drawn by WaveformDisplay
        scanRow->setBounds(-1000, -1000, 10, 10);
        scanHint.setVisible(false);

        area.removeFromTop(gap * 3);
        engineCardBottom = area.getY();
        area.removeFromTop(gap);
    }
    else
    {
        // ── Sampler: waveform + bracket handles + controls row ──
        waveformDisplay.setBottomReserve(0);
        waveformDisplay.setScanVisible(false);
        waveformDisplay.setBounds(area.removeFromTop(waveH));
        area.removeFromTop(gap * 3);  // bracket handle space

        // [▶][↻][⇄] Crossfade [slider]    [Normalize][Auto-opt]
        auto loopRow = area.removeFromTop(rowH);
        int colW = (loopRow.getWidth() - 4) / 2;

        // Left half: loop icons + crossfade (loop-related, grouped together)
        auto leftHalf = loopRow.removeFromLeft(colW);
        int iconW = juce::roundToInt(f * 2.8f);
        oneshotBtn.setBounds(leftHalf.removeFromLeft(iconW));
        loopModeBtn.setBounds(leftHalf.removeFromLeft(iconW));
        pingpongBtn.setBounds(leftHalf.removeFromLeft(iconW));
        loopSwitchBounds = oneshotBtn.getBounds().getUnion(pingpongBtn.getBounds());
        leftHalf.removeFromLeft(juce::roundToInt(f * 0.3f));
        crossfadeRow->setBounds(leftHalf);  // short slider, ends at column midpoint

        loopRow.removeFromLeft(4);  // column gap

        // Right half: Normalize + Auto-opt
        int optW = juce::roundToInt(f * 5.0f);
        normalizeToggle.setBounds(loopRow.removeFromLeft(optW));
        loopRow.removeFromLeft(2);
        loopOptimizeBtn.setBounds(loopRow.removeFromLeft(optW));
        optSwitchBounds = normalizeToggle.getBounds().getUnion(loopOptimizeBtn.getBounds());

        area.removeFromTop(gap);
        engineCardBottom = oneshotBtn.getBottom();
    }
    area.removeFromTop(gap * 2);

    // ── FILTER section header ──
    filterHeader.setFont(juce::FontOptions(headerFs));
    filterHeader.setBounds(area.removeFromTop(headerH));
    area.removeFromTop(headerGap);

    // ── Filter switchboxes: [OFF LP HP BP]  [6dB 12dB 18dB 24dB] ──
    auto filterHdr = area.removeFromTop(rowH);
    {
        int cellW = juce::roundToInt(f * 3.2f);
        for (int i = 0; i < kNumTypeBtns; ++i)
            filterTypeBtns[i].setBounds(filterHdr.removeFromLeft(cellW));
        filterTypeSwitchBounds = filterTypeBtns[0].getBounds()
            .getUnion(filterTypeBtns[kNumTypeBtns - 1].getBounds());

        filterHdr.removeFromLeft(juce::roundToInt(f * 0.5f));

        int slopeCellW = juce::roundToInt(f * 3.2f);
        for (int i = 0; i < kNumSlopeBtns; ++i)
            filterSlopeBtns[i].setBounds(filterHdr.removeFromLeft(slopeCellW));
        filterSlopeSwitchBounds = filterSlopeBtns[0].getBounds()
            .getUnion(filterSlopeBtns[kNumSlopeBtns - 1].getBounds());
    }
    area.removeFromTop(gap);

    {
        // Always allocate — dimmed when filter off
        int colW = (area.getWidth() - 4) / 2;
        auto row1 = area.removeFromTop(rowH);
        cutoffRow->setBounds(row1.removeFromLeft(colW));
        row1.removeFromLeft(4);
        resoRow->setBounds(row1);

        auto row2 = area.removeFromTop(rowH);
        filterMixRow->setBounds(row2.removeFromLeft(colW));
        row2.removeFromLeft(4);
        kbdTrackRow->setBounds(row2);
        area.removeFromTop(gap);
    }

    int sectionGap = gap * 3;

    // ── MODULATION section header ──
    area.removeFromTop(sectionGap);
    modHeader.setFont(juce::FontOptions(headerFs));
    modHeader.setBounds(area.removeFromTop(headerH));
    area.removeFromTop(headerGap);

    // ── Envelopes ──
    layoutEnv(ampEnv,  area, f, rowH, gap);
    layoutEnv(mod1Env, area, f, rowH, gap);
    layoutEnv(mod2Env, area, f, rowH, gap);

    // ── LFOs ──
    area.removeFromTop(gap);
    layoutLfo(lfo1, area, f, rowH, gap);
    layoutLfo(lfo2, area, f, rowH, gap);

    // ── Drift + Regenerate (part of modulation section) ──
    area.removeFromTop(gap);
    regenHeader.setFont(juce::FontOptions(headerFs));
    regenHeader.setBounds(area.removeFromTop(headerH));
    area.removeFromTop(gap);
    {
        auto regenRow = area.removeFromTop(rowH);
        int regenCellW = juce::roundToInt(f * 4.5f);
        for (int i = 0; i < kNumRegenBtns; ++i)
        {
            int edges = 0;
            if (i > 0) edges |= juce::Button::ConnectedOnLeft;
            if (i < kNumRegenBtns - 1) edges |= juce::Button::ConnectedOnRight;
            regenBtns[i].setConnectedEdges(edges);
            regenBtns[i].setBounds(regenRow.removeFromLeft(regenCellW));
        }
        regenSwitchBounds = regenBtns[0].getBounds()
            .getUnion(regenBtns[kNumRegenBtns - 1].getBounds());
        // XFade slider fills the remaining space in the same row
        regenRow.removeFromLeft(juce::roundToInt(f * 0.5f)); // small gap
        crossfadeRegenRow->setBounds(regenRow);
    }
    layoutDrift(drift1, area, f, rowH, gap);
    layoutDrift(drift2, area, f, rowH, gap);
}
