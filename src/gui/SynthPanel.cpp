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
                          const juce::String& amtId, const juce::String& loopId,
                          juce::AudioProcessorValueTreeState& apvts)
{
    env.header.setText(name, juce::dontSendNotification);
    env.header.setColour(juce::Label::textColourId, kEnvCol);
    addAndMakeVisible(env.header);

    env.targetBox.addItemList({"DCA", "Filter", "Scan", "Pitch", "Dly Time", "Dly FB", "Dly Mix", "Rev Mix",
                               "LFO1 Rate", "LFO1 Depth", "LFO2 Rate", "LFO2 Depth", "---"}, 1);
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

    for (auto* row : { env.aRow.get(), env.dRow.get(), env.sRow.get(),
                       env.rRow.get(), env.amtRow.get() })
        addAndMakeVisible(*row);

    env.aA   = std::make_unique<SA>(apvts, aId,   env.aRow->getSlider());
    env.dA   = std::make_unique<SA>(apvts, dId,   env.dRow->getSlider());
    env.sA   = std::make_unique<SA>(apvts, sId,   env.sRow->getSlider());
    env.rA   = std::make_unique<SA>(apvts, rId,   env.rRow->getSlider());
    env.amtA = std::make_unique<SA>(apvts, amtId, env.amtRow->getSlider());
    env.loopA = std::make_unique<BA>(apvts, loopId, env.loopToggle);

    // Trigger initial value display
    env.aRow->updateValue();
    env.dRow->updateValue();
    env.sRow->updateValue();
    env.rRow->updateValue();
    env.amtRow->updateValue();
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

    lfo.targetBox.addItemList({"Filter", "Scan", "Pitch", "Dly Time", "Dly FB", "Dly Mix", "Rev Mix",
                               "Xmod Rate", "Xmod Depth", "---"}, 1);
    lfo.targetBox.setSelectedId(10, juce::dontSendNotification);
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

    drift.targetBox.addItemList({"None", "Alpha", "Axis 1", "Axis 2", "Axis 3", "WT Scan",
                                  "Filter", "Pitch", "Dly Time", "Dly FB", "Dly Mix", "Rev Mix"}, 1);
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

    addAndMakeVisible(waveformDisplay);

    // Wire bracket handles to sampler + wavetable extraction
    waveformDisplay.onLoopRegionChanged = [this](float start, float end) {
        processorRef.getSampler().setLoopStart(start);
        processorRef.getSampler().setLoopEnd(end);

        // In wavetable mode, re-extract frames from the new region
        if (processorRef.isWavetableMode())
            processorRef.reextractWavetable();
    };

    // ── Loop mode ──
    auto styleLoopBtn = [](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId, kSurface);
        btn.setColour(juce::TextButton::buttonOnColourId, kAccent);
        btn.setColour(juce::TextButton::textColourOffId, kDim);
        btn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn.setClickingTogglesState(true);
        btn.setRadioGroupId(1002);
    };
    styleLoopBtn(oneshotBtn);
    styleLoopBtn(loopModeBtn);
    styleLoopBtn(pingpongBtn);
    oneshotBtn.setToggleState(true, juce::dontSendNotification);
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

    // Loop optimize + Normalize — rectangular on/off buttons
    for (auto* btn : { &loopOptimizeToggle, &normalizeToggle })
    {
        btn->setColour(juce::TextButton::buttonColourId, kSurface);
        btn->setColour(juce::TextButton::buttonOnColourId, kAccent);
        btn->setColour(juce::TextButton::textColourOffId, kDim);
        btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn->setClickingTogglesState(true);
        addAndMakeVisible(btn);
    }
    loopOptimizeToggle.onClick = [this] {
        auto* param = processorRef.getValueTreeState().getParameter("loop_optimize");
        if (param) param->setValueNotifyingHost(loopOptimizeToggle.getToggleState() ? 1.0f : 0.0f);
    };
    normalizeToggle.onClick = [this] {
        auto* param = processorRef.getValueTreeState().getParameter("normalize");
        if (param) param->setValueNotifyingHost(normalizeToggle.getToggleState() ? 1.0f : 0.0f);
    };
    // Sync initial state from APVTS
    loopOptimizeToggle.setToggleState(
        apvts.getRawParameterValue("loop_optimize")->load() > 0.5f, juce::dontSendNotification);
    normalizeToggle.setToggleState(
        apvts.getRawParameterValue("normalize")->load() > 0.5f, juce::dontSendNotification);

    // ── Scan ──
    scanRow = std::make_unique<SliderRow>("Scan", fmtF2);
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
    initEnv(ampEnv,  "ENV 1", 1, "amp_attack",  "amp_decay",  "amp_sustain",  "amp_release",  "amp_amount",  "amp_loop",  apvts);
    initEnv(mod1Env, "ENV 2", 4, "mod1_attack", "mod1_decay", "mod1_sustain", "mod1_release", "mod1_amount", "mod1_loop", apvts);
    initEnv(mod2Env, "ENV 3", 4, "mod2_attack", "mod2_decay", "mod2_sustain", "mod2_release", "mod2_amount", "mod2_loop", apvts);

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
        if (scanRow->isVisible()) scanRow->setGhostValue(mv.scanPosition.load(std::memory_order_relaxed));
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
    loopOptimizeToggle.setVisible(isSampler);
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
        bool active = env.targetBox.getSelectedId() != env.targetBox.getNumItems();
        float alpha = active ? 1.0f : dimAlpha;
        env.loopToggle.setAlpha(alpha);
        env.loopToggle.setEnabled(active);
        for (auto* r : { env.aRow.get(), env.dRow.get(), env.sRow.get(),
                         env.rRow.get(), env.amtRow.get() })
            if (r) { r->setAlpha(alpha); r->setEnabled(active); }
    };
    setEnvDimmed(ampEnv);
    setEnvDimmed(mod1Env);
    setEnvDimmed(mod2Env);

    auto setLfoDimmed = [dimAlpha](LfoSection& lfo) {
        bool active = lfo.targetBox.getSelectedId() != lfo.targetBox.getNumItems();
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
        bool active = drift.targetBox.getSelectedId() != 1; // 1 = "None"
        float alpha = active ? 1.0f : dimAlpha;
        drift.waveBox.setAlpha(alpha);
        drift.waveBox.setEnabled(active);
        if (drift.rateRow)  { drift.rateRow->setAlpha(alpha);  drift.rateRow->setEnabled(active); }
        if (drift.depthRow) { drift.depthRow->setAlpha(alpha); drift.depthRow->setEnabled(active); }
    };
    setDriftDimmed(drift1);
    setDriftDimmed(drift2);
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

    // ── Engine mode: compact switchbox ──
    auto modeRow = area.removeFromTop(rowH);
    {
        int cellW = juce::roundToInt(f * 5.0f);  // uniform cell width
        samplerBtn.setBounds(modeRow.removeFromLeft(cellW));
        wavetableBtn.setBounds(modeRow.removeFromLeft(cellW));
        engineSwitchBounds = samplerBtn.getBounds().getUnion(wavetableBtn.getBounds());
    }
    area.removeFromTop(gap);

    // ── Waveform ──
    int waveH = juce::roundToInt(h * 0.12f);
    waveformDisplay.setBounds(area.removeFromTop(waveH));
    area.removeFromTop(gap * 3);  // extra room for bracket drag handles
    engineCardBottom = waveformDisplay.getBottom();

    // ── Sampler-only controls ──
    if (oneshotBtn.isVisible())
    {
        // [One-shot] [Loop] [Ping-Pong]  [Normalize] [Auto-opt]
        auto loopRow = area.removeFromTop(rowH);
        int cellW = juce::roundToInt(f * 5.0f);
        oneshotBtn.setBounds(loopRow.removeFromLeft(cellW));
        loopModeBtn.setBounds(loopRow.removeFromLeft(cellW));
        pingpongBtn.setBounds(loopRow.removeFromLeft(cellW));
        loopSwitchBounds = oneshotBtn.getBounds().getUnion(pingpongBtn.getBounds());

        loopRow.removeFromLeft(juce::roundToInt(f * 1.0f));
        int optW = juce::roundToInt(f * 5.0f);
        normalizeToggle.setBounds(loopRow.removeFromLeft(optW));
        loopRow.removeFromLeft(4);
        loopOptimizeToggle.setBounds(loopRow.removeFromLeft(optW));
        optSwitchBounds = normalizeToggle.getBounds().getUnion(loopOptimizeToggle.getBounds());
        area.removeFromTop(gap);

        crossfadeRow->setBounds(area.removeFromTop(rowH));
        area.removeFromTop(gap);
        engineCardBottom = crossfadeRow->getBottom();
    }

    // ── Wavetable-only controls ──
    if (scanRow->isVisible())
    {
        scanRow->setBounds(area.removeFromTop(rowH));
        scanHint.setFont(juce::FontOptions(f * 0.7f));
        scanHint.setBounds(area.removeFromTop(juce::roundToInt(f * 0.9f)));
        area.removeFromTop(gap);
        engineCardBottom = scanHint.getBottom();
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
    regenHeader.setFont(juce::FontOptions(f));
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
    }
    layoutDrift(drift1, area, f, rowH, gap);
    layoutDrift(drift2, area, f, rowH, gap);

}
