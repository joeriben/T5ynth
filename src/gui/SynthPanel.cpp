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
    env.header.setColour(juce::Label::textColourId, kModCol);
    addAndMakeVisible(env.header);

    env.targetBox.addItemList({"DCA", "Filter", "Scan", "Pitch", "Dly Time", "Dly FB", "Dly Mix", "Rev Mix",
                               "LFO1 Rate", "LFO1 Depth", "LFO2 Rate", "LFO2 Depth", "---"}, 1);
    env.targetBox.setSelectedId(defaultTarget, juce::dontSendNotification);
    env.targetBox.onChange = [this] { updateVisibility(); resized(); };
    addAndMakeVisible(env.targetBox);

    env.loopToggle.setColour(juce::ToggleButton::textColourId, kDim);
    env.loopToggle.setColour(juce::ToggleButton::tickColourId, kAccent);
    addAndMakeVisible(env.loopToggle);

    env.aRow   = std::make_unique<SliderRow>("A",   fmtMs, kModCol);
    env.dRow   = std::make_unique<SliderRow>("D",   fmtMs, kModCol);
    env.sRow   = std::make_unique<SliderRow>("S",   fmtF2, kModCol);
    env.rRow   = std::make_unique<SliderRow>("R",   fmtMs, kModCol);
    env.amtRow = std::make_unique<SliderRow>("Amt", fmtF2, kModCol);

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
    lfo.header.setColour(juce::Label::textColourId, kModCol);
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

    lfo.rateRow  = std::make_unique<SliderRow>("Rate",  fmtHzF1, kModCol);
    lfo.depthRow = std::make_unique<SliderRow>("Depth", fmtF2,   kModCol);
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

    drift.targetBox.addItemList({"None", "Alpha", "Axis 1", "Axis 2", "Axis 3", "WT Scan"}, 1);
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

    // Wire bracket handles to sampler
    waveformDisplay.onLoopRegionChanged = [this](float start, float end) {
        processorRef.getSampler().setLoopStart(start);
        processorRef.getSampler().setLoopEnd(end);
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
    loopModeBtn.setToggleState(true, juce::dontSendNotification);
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

    // Loop optimize
    loopOptimizeToggle.setColour(juce::ToggleButton::textColourId, kDim);
    loopOptimizeToggle.setColour(juce::ToggleButton::tickColourId, kAccent);
    addAndMakeVisible(loopOptimizeToggle);
    loopOptimizeA = std::make_unique<BA>(apvts, "loop_optimize", loopOptimizeToggle);

    // Normalize
    normalizeToggle.setColour(juce::ToggleButton::textColourId, kDim);
    normalizeToggle.setColour(juce::ToggleButton::tickColourId, kAccent);
    addAndMakeVisible(normalizeToggle);
    normalizeA = std::make_unique<BA>(apvts, "normalize", normalizeToggle);

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
    makeHeader(filterHeader, "FILTER", kFilterCol);
    makeHeader(modHeader, "MODULATION", kModCol);
    makeHeader(driftHeader, "DRIFT", kDriftCol);

    // ── Filter ──
    filterToggle.setColour(juce::ToggleButton::textColourId, kDim);
    filterToggle.setColour(juce::ToggleButton::tickColourId, kAccent);
    filterToggle.onClick = [this] { updateVisibility(); resized(); };
    addAndMakeVisible(filterToggle);
    filterEnableA = std::make_unique<BA>(apvts, "filter_enabled", filterToggle);

    filterTypeBox.addItemList({"LP", "HP", "BP"}, 1);
    addAndMakeVisible(filterTypeBox);
    filterSlopeBox.addItemList({"12dB", "24dB"}, 1);
    addAndMakeVisible(filterSlopeBox);

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

    filterTypeA  = std::make_unique<CA>(apvts, "filter_type",  filterTypeBox);
    filterSlopeA = std::make_unique<CA>(apvts, "filter_slope", filterSlopeBox);

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

    driftRegenToggle.setColour(juce::ToggleButton::textColourId, kDim);
    driftRegenToggle.setColour(juce::ToggleButton::tickColourId, kDriftCol);
    addAndMakeVisible(driftRegenToggle);
    driftRegenA = std::make_unique<BA>(apvts, "drift_regen", driftRegenToggle);

    // ── Explore button ──
    exploreBtn.setColour(juce::TextButton::buttonColourId, kSurface);
    exploreBtn.setColour(juce::TextButton::textColourOffId, kAccent);
    exploreBtn.onClick = [this] { if (onExploreClicked) onExploreClicked(); };
    addAndMakeVisible(exploreBtn);

    // All components are now set up — enable callbacks and trigger initial state
    initialized = true;

    // Deferred APVTS attachments for target ComboBoxes
    // (must come after initialized=true so onChange → updateVisibility works)
    mod1TargetA = std::make_unique<CA>(apvts, "mod1_target", mod1Env.targetBox);
    mod2TargetA = std::make_unique<CA>(apvts, "mod2_target", mod2Env.targetBox);
    lfo1TargetA = std::make_unique<CA>(apvts, "lfo1_target", lfo1.targetBox);
    lfo2TargetA = std::make_unique<CA>(apvts, "lfo2_target", lfo2.targetBox);

    updateVisibility();
    startTimerHz(30);
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

        processorRef.clearNewWaveformFlag();
    }

    // Update ghost indicators from modulated values
    auto& mv = processorRef.modulatedValues;
    cutoffRow->setGhostValue(mv.filterCutoff.load(std::memory_order_relaxed));
    if (lfo1.rateRow)  lfo1.rateRow->setGhostValue(mv.lfo1Rate.load(std::memory_order_relaxed));
    if (lfo1.depthRow) lfo1.depthRow->setGhostValue(mv.lfo1Depth.load(std::memory_order_relaxed));
    if (lfo2.rateRow)  lfo2.rateRow->setGhostValue(mv.lfo2Rate.load(std::memory_order_relaxed));
    if (lfo2.depthRow) lfo2.depthRow->setGhostValue(mv.lfo2Depth.load(std::memory_order_relaxed));
    if (scanRow->isVisible()) scanRow->setGhostValue(mv.scanPosition.load(std::memory_order_relaxed));
}

// ──────────────────────────────────────────────────────────────────────────────
// Visibility
// ──────────────────────────────────────────────────────────────────────────────
void SynthPanel::updateVisibility()
{
    if (!initialized) return;

    // All sections always visible — inactive ones get dimmed via alpha
    constexpr float dimAlpha = 0.3f;

    bool filterOn = filterToggle.getToggleState();
    float filterAlpha = filterOn ? 1.0f : dimAlpha;
    filterTypeBox.setAlpha(filterAlpha);
    filterSlopeBox.setAlpha(filterAlpha);
    filterTypeBox.setEnabled(filterOn);
    filterSlopeBox.setEnabled(filterOn);
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
    // Content budget: ~54 f-units (28 rows * 1.4 + headers + gaps)
    // plus waveform at 12% of panel height.
    float h = static_cast<float>(getHeight());
    float padY = h * 0.005f;
    float available = h - 2.0f * padY;
    float waveform = h * 0.12f;
    float remaining = available - waveform;
    float maxF = remaining / 54.0f;
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
        int top = samplerBtn.getY() - inset;
        int bot = scanHint.getBottom() + inset;
        paintCard(g, juce::Rectangle<int>(padX, top, getWidth() - padX * 2, bot - top));
    }

    // Card: Filter section
    {
        int top = filterHeader.getY() - inset;
        int bot = kbdTrackRow->getBottom();
        paintCard(g, juce::Rectangle<int>(padX, top, getWidth() - padX * 2, bot - top + inset));
    }

    // Card: Modulation (all ENVs + LFOs)
    {
        int top = modHeader.getY() - inset;
        int bot = juce::jmax(ampEnv.amtRow->getBottom(), mod1Env.amtRow->getBottom(),
                             mod2Env.amtRow->getBottom());
        bot = juce::jmax(bot, lfo1.depthRow->getBottom(), lfo2.depthRow->getBottom());
        paintCard(g, juce::Rectangle<int>(padX, top, getWidth() - padX * 2, bot - top + inset));

        // Subtle separator lines between ENV/LFO sub-sections
        g.setColour(kModCol.withAlpha(0.15f));
        int lineL = padX + 8;
        int lineR = getWidth() - padX - 8;
        for (auto* hdr : { &mod1Env.header, &mod2Env.header, &lfo1.header, &lfo2.header })
        {
            int y = hdr->getY() - 2;
            g.drawHorizontalLine(y, static_cast<float>(lineL), static_cast<float>(lineR));
        }
    }

    // Card: Drift
    {
        int top = driftHeader.getY() - inset;
        int bot = juce::jmax(drift1.depthRow->getBottom(), drift2.depthRow->getBottom());
        paintCard(g, juce::Rectangle<int>(padX, top, getWidth() - padX * 2, bot - top + inset));
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

    // ── Engine mode: horizontal switch ──
    auto modeRow = area.removeFromTop(juce::roundToInt(f * 1.8f));
    int third = modeRow.getWidth() / 2;
    samplerBtn.setBounds(modeRow.removeFromLeft(third));
    wavetableBtn.setBounds(modeRow);
    area.removeFromTop(gap);

    // ── Waveform ──
    int waveH = juce::roundToInt(h * 0.12f);
    waveformDisplay.setBounds(area.removeFromTop(waveH));
    area.removeFromTop(gap);

    // ── Sampler-only controls ──
    if (oneshotBtn.isVisible())
    {
        auto loopRow = area.removeFromTop(rowH);
        int btnW = loopRow.getWidth() / 3;
        oneshotBtn.setBounds(loopRow.removeFromLeft(btnW));
        loopModeBtn.setBounds(loopRow.removeFromLeft(btnW));
        pingpongBtn.setBounds(loopRow);
        area.removeFromTop(gap);

        crossfadeRow->setBounds(area.removeFromTop(rowH));
        area.removeFromTop(gap);

        loopOptimizeToggle.setBounds(area.removeFromTop(rowH));
        normalizeToggle.setBounds(area.removeFromTop(rowH));
        area.removeFromTop(gap);
    }

    // ── Wavetable-only controls ──
    if (scanRow->isVisible())
    {
        scanRow->setBounds(area.removeFromTop(rowH));
        scanHint.setFont(juce::FontOptions(f * 0.7f));
        scanHint.setBounds(area.removeFromTop(juce::roundToInt(f * 0.9f)));
        area.removeFromTop(gap);
    }
    area.removeFromTop(gap * 2);

    // ── FILTER section header ──
    filterHeader.setFont(juce::FontOptions(f * 0.85f));
    filterHeader.setBounds(area.removeFromTop(juce::roundToInt(f * 1.1f)));

    // ── Filter ──
    auto filterHdr = area.removeFromTop(rowH);
    filterToggle.setBounds(filterHdr.removeFromLeft(juce::roundToInt(w * 0.18f)));
    filterTypeBox.setBounds(filterHdr.removeFromLeft(juce::roundToInt(w * 0.10f)));
    filterHdr.removeFromLeft(4);
    filterSlopeBox.setBounds(filterHdr.removeFromLeft(juce::roundToInt(w * 0.10f)));
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
    modHeader.setFont(juce::FontOptions(f * 0.85f));
    modHeader.setBounds(area.removeFromTop(juce::roundToInt(f * 1.1f)));

    // ── Envelopes ──
    layoutEnv(ampEnv,  area, f, rowH, gap);
    layoutEnv(mod1Env, area, f, rowH, gap);
    layoutEnv(mod2Env, area, f, rowH, gap);

    // ── LFOs ──
    area.removeFromTop(gap);
    layoutLfo(lfo1, area, f, rowH, gap);
    layoutLfo(lfo2, area, f, rowH, gap);

    // ── DRIFT section header ──
    area.removeFromTop(sectionGap);
    driftHeader.setFont(juce::FontOptions(f * 0.85f));
    driftHeader.setBounds(area.removeFromTop(juce::roundToInt(f * 1.1f)));
    driftRegenToggle.setBounds(area.removeFromTop(rowH));

    // ── Drift ──
    layoutDrift(drift1, area, f, rowH, gap);
    layoutDrift(drift2, area, f, rowH, gap);

    // ── Explore button ──
    area.removeFromTop(sectionGap);
    exploreBtn.setBounds(area.removeFromTop(juce::roundToInt(f * 1.6f)));
}
