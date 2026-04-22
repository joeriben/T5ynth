#include "SynthPanel.h"
#include "../PluginProcessor.h"
#include <algorithm>

// ── Helper: format ms (integer) ──
static juce::String fmtMs(double v)
{
    if (std::abs(v) < 1.0)
        return juce::String(v, 2) + "ms";
    if (std::abs(v) < 10.0)
        return juce::String(v, 1) + "ms";
    return juce::String(juce::roundToInt(v)) + "ms";
}
static juce::String fmtF2(double v)  { return juce::String(v, 2); }
static juce::String fmtPct(double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; }
static juce::String fmtHz(double v)  { return juce::String(juce::roundToInt(v)) + " Hz"; }
static juce::String fmtDb(double v)  { return juce::String(v, 1) + " dB"; }
static juce::String fmtHzF1(double v){ return juce::String(v, 1) + " Hz"; }
static juce::String fmtHzF2(double v){ return juce::String(v, 2) + " Hz"; }
static juce::String fmtHzF3(double v){ return juce::String(v, 3) + " Hz"; }

// ──────────────────────────────────────────────────────────────────────────────
// Envelope init
// ──────────────────────────────────────────────────────────────────────────────
void SynthPanel::initEnv(EnvSection& env, const juce::String& name, int defaultTarget,
                          const juce::String& aId, const juce::String& dId,
                          const juce::String& sId, const juce::String& rId,
                          const juce::String& aCurveId, const juce::String& dCurveId,
                          const juce::String& rCurveId,
                          const juce::String& aVelModeId, const juce::String& dVelModeId,
                          const juce::String& rVelModeId,
                          const juce::String& amtId, const juce::String& velId,
                          const juce::String& loopId,
                          juce::AudioProcessorValueTreeState& apvts)
{
    env.header.setText(name, juce::dontSendNotification);
    env.header.setColour(juce::Label::textColourId, kEnvCol);
    addAndMakeVisible(env.header);

    // Labels driven from BlockParams::EnvTarget::kEntries (single source of
    // truth for enum index ↔ human-readable label).
    juce::StringArray envItems;
    for (const auto& e : EnvTarget::kEntries) envItems.add(e.label);
    env.targetBox.addItemList(envItems, 1);
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

    // ── Curve shape cycling buttons (square icons) ──
    auto setupCurveBtn = [this](CurveButton& btn, juce::ComboBox& hidden,
                                const juce::String& paramId,
                                juce::AudioProcessorValueTreeState& vts,
                                std::unique_ptr<CA>& attachment) {
        juce::StringArray curveItems;
        for (const auto& e : EnvCurve::kEntries) curveItems.add(e.label);
        hidden.addItemList(curveItems, 1);
        hidden.onChange = [&btn, &hidden] {
            btn.setCurveShape(hidden.getSelectedId() - 1);
        };
        btn.onClick = [&hidden] {
            int next = (hidden.getSelectedId() % 5) + 1;
            hidden.setSelectedId(next);
        };
        addAndMakeVisible(btn);
        attachment = std::make_unique<CA>(vts, paramId, hidden);
        hidden.onChange(); // sync icon with initial parameter value
    };
    setupCurveBtn(env.aCurveBtn, env.aCurveHidden, aCurveId, apvts, env.aCurveA);
    setupCurveBtn(env.dCurveBtn, env.dCurveHidden, dCurveId, apvts, env.dCurveA);
    setupCurveBtn(env.rCurveBtn, env.rCurveHidden, rCurveId, apvts, env.rCurveA);

    auto setupVelMode = [](SliderRow& row, juce::ComboBox& hidden,
                           const juce::String& paramId,
                           juce::AudioProcessorValueTreeState& vts,
                           std::unique_ptr<CA>& attachment)
    {
        juce::StringArray items;
        for (const auto& e : EnvVelTimeMode::kEntries)
            items.add(e.label);
        hidden.addItemList(items, 1);
        hidden.onChange = [&row, &hidden]
        {
            const int index = hidden.getSelectedId() - 1;
            SliderRow::LabelMode mode = SliderRow::LabelMode::Off;
            if (index == EnvVelTimeMode::Positive)
                mode = SliderRow::LabelMode::Positive;
            else if (index == EnvVelTimeMode::Negative)
                mode = SliderRow::LabelMode::Negative;
            row.setLabelMode(mode);
        };
        row.setLabelClickHandler([&hidden]
        {
            const int next = (hidden.getSelectedId() % EnvVelTimeMode::kCount) + 1;
            hidden.setSelectedId(next);
        });
        attachment = std::make_unique<CA>(vts, paramId, hidden);
        hidden.onChange();
    };
    setupVelMode(*env.aRow, env.aVelModeHidden, aVelModeId, apvts, env.aVelModeA);
    setupVelMode(*env.dRow, env.dVelModeHidden, dVelModeId, apvts, env.dVelModeA);
    setupVelMode(*env.rRow, env.rVelModeHidden, rVelModeId, apvts, env.rVelModeA);
    env.velRow->setLabelMode(SliderRow::LabelMode::Positive);

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

    // Labels driven from BlockParams::LfoTarget::kEntries (single source of
    // truth for enum index ↔ human-readable label).
    juce::StringArray lfoItems;
    for (const auto& e : LfoTarget::kEntries) lfoItems.add(e.label);
    lfo.targetBox.addItemList(lfoItems, 1);
    lfo.targetBox.setSelectedId(1, juce::dontSendNotification);
    lfo.targetBox.onChange = [this] { updateVisibility(); resized(); };
    addAndMakeVisible(lfo.targetBox);

    juce::StringArray lfoWaveItems;
    for (const auto& e : LfoWave::kEntries) lfoWaveItems.add(e.label);
    lfo.waveBox.addItemList(lfoWaveItems, 1);
    addAndMakeVisible(lfo.waveBox);

    juce::StringArray lfoModeItems;
    for (const auto& e : LfoMode::kEntries) lfoModeItems.add(e.label);
    lfo.modeBox.addItemList(lfoModeItems, 1);
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

    juce::StringArray driftTargetItems;
    for (const auto& e : DriftTarget::kEntries) driftTargetItems.add(e.label);
    drift.targetBox.addItemList(driftTargetItems, 1);
    drift.targetBox.onChange = [this] { updateVisibility(); resized(); };
    addAndMakeVisible(drift.targetBox);

    juce::StringArray driftWaveItems;
    for (const auto& e : DriftWave::kEntries) driftWaveItems.add(e.label);
    drift.waveBox.addItemList(driftWaveItems, 1);
    addAndMakeVisible(drift.waveBox);

    drift.rateRow  = std::make_unique<SliderRow>("Rate",  fmtHzF3, kDriftCol);
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

    juce::StringArray engineModeItems;
    for (const auto& e : EngineMode::kEntries) engineModeItems.add(e.label);
    engineModeHidden.addItemList(engineModeItems, 1);
    engineModeHidden.onChange = [this] {
        bool isSampler = engineModeHidden.getSelectedId() == 1;
        samplerBtn.setToggleState(isSampler, juce::dontSendNotification);
        wavetableBtn.setToggleState(!isSampler, juce::dontSendNotification);
        updateVisibility();
        resized();
    };
    samplerBtn.onClick = [this] { engineModeHidden.setSelectedId(1); };
    wavetableBtn.onClick = [this] { engineModeHidden.setSelectedId(2); };
    engineModeA = std::make_unique<CA>(apvts, PID::engineMode, engineModeHidden);

    // ── Voice count switchbox ──
    {
        juce::StringArray vcLabels;
        for (const auto& e : VoiceCount::kEntries) vcLabels.add(e.label);
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
        voiceCountA = std::make_unique<CA>(apvts, PID::voiceCount, voiceCountHidden);
    }

    // ── Tuning ──
    {
        juce::StringArray tuningItems;
        for (const auto& e : TuningType::kEntries) tuningItems.add(e.label);
        tuningBox.addItemList(tuningItems, 1);
        tuningBox.setColour(juce::ComboBox::backgroundColourId, kSurface);
        tuningBox.setColour(juce::ComboBox::textColourId, juce::Colours::white);
        tuningBox.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(tuningBox);
        tuningA = std::make_unique<CA>(apvts, PID::tuning, tuningBox);
    }

    addAndMakeVisible(waveformDisplay);

    // Wire bracket handles: both engines use the same P2/P3 loop semantics.
    waveformDisplay.onLoopRegionChanged = [this](float start, float end) {
        const juce::ScopedLock sl (processorRef.getCallbackLock());
        processorRef.getSampler().setLoopStart(start);
        processorRef.getSampler().setLoopEnd(end);
        processorRef.getSampler().setPointsLocked(true);
        waveformDisplay.getLockButton().setLocked(true);
        if (processorRef.isWavetableMode())
            pendingWtReextract_ = true;
    };

    // P1 (start position) handle
    waveformDisplay.onStartPosChanged = [this](float pos) {
        const juce::ScopedLock sl (processorRef.getCallbackLock());
        processorRef.getSampler().setStartPos(pos);
        processorRef.getSampler().setPointsLocked(true);
        waveformDisplay.getLockButton().setLocked(true);
        if (processorRef.isWavetableMode())
            pendingWtReextract_ = true;
    };

    waveformDisplay.onMarkerDragFinished = [this]() {
        if (pendingWtReextract_ && processorRef.isWavetableMode())
            processorRef.reextractWavetable();
        pendingWtReextract_ = false;
    };

    // Lock button: toggles P1/P2/P3 preservation across Generate
    waveformDisplay.getLockButton().onToggled = [this](bool locked) {
        const juce::ScopedLock sl (processorRef.getCallbackLock());
        processorRef.getSampler().setPointsLocked(locked);
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

    juce::StringArray loopModeItems;
    for (const auto& e : LoopMode::kEntries) loopModeItems.add(e.label);
    loopModeHidden.addItemList(loopModeItems, 1);
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
    loopModeA = std::make_unique<CA>(apvts, PID::loopMode, loopModeHidden);

    // Crossfade
    crossfadeRow = std::make_unique<SliderRow>("Xfade", fmtMs);
    addAndMakeVisible(*crossfadeRow);
    crossfadeA = std::make_unique<SA>(apvts, PID::crossfadeMs, crossfadeRow->getSlider());
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
        auto* param = processorRef.getValueTreeState().getParameter(PID::normalize);
        if (param) param->setValueNotifyingHost(normalizeToggle.getToggleState() ? 1.0f : 0.0f);
    };
    normalizeToggle.setToggleState(
        apvts.getRawParameterValue(PID::normalize)->load() > 0.5f, juce::dontSendNotification);

    // HF boost toggle — compensates VAE decoder high-frequency rolloff
    hfBoostBtn.setConnectedEdges(juce::Button::ConnectedOnRight);
    hfBoostBtn.setColour(juce::TextButton::buttonColourId, kSurface);
    hfBoostBtn.setColour(juce::TextButton::buttonOnColourId, kAccent);
    hfBoostBtn.setColour(juce::TextButton::textColourOffId, kDim);
    hfBoostBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    hfBoostBtn.setClickingTogglesState(true);
    hfBoostBtn.setToggleState(
        apvts.getRawParameterValue(PID::genHfBoost)->load() > 0.5f, juce::dontSendNotification);
    hfBoostBtn.onClick = [this] {
        bool on = hfBoostBtn.getToggleState();
        processorRef.getValueTreeState().getParameter(PID::genHfBoost)
            ->setValueNotifyingHost(on ? 1.0f : 0.0f);
        const auto& raw = processorRef.getGeneratedAudioRaw();
        if (raw.getNumSamples() > 0)
            processorRef.loadGeneratedAudio(raw, processorRef.getGeneratedSampleRate());
    };
    addAndMakeVisible(hfBoostBtn);

    // Loop optimize cycling button (Off → Low → High)
    loopOptimizeBtn.setConnectedEdges(juce::Button::ConnectedOnLeft);
    loopOptimizeBtn.setColour(juce::TextButton::buttonColourId, kSurface);
    loopOptimizeBtn.setColour(juce::TextButton::textColourOffId, kDim);
    addAndMakeVisible(loopOptimizeBtn);
    {
        int initLevel = static_cast<int>(apvts.getRawParameterValue(PID::loopOptimize)->load());
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
        auto* param = processorRef.getValueTreeState().getParameter(PID::loopOptimize);
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
    scanA = std::make_unique<SA>(apvts, PID::oscScan, scanRow->getSlider());
    scanRow->updateValue();

    // ── Octave shift switchbox: -2 | -1 | 0 | +1 | +2 ──
    {
        juce::StringArray octLabels;
        for (const auto& e : OscOctave::kEntries) octLabels.add(e.label);
        octaveHidden.addItemList(octLabels, 1);
        octaveHidden.onChange = [this] {
            int id = octaveHidden.getSelectedId();
            for (int i = 0; i < kNumOctBtns; ++i)
            {
                bool sel = (i + 1 == id);
                octBtns[i].setToggleState(sel, juce::dontSendNotification);
                octBtns[i].setColour(juce::TextButton::buttonColourId,
                                      sel ? kAccent : juce::Colours::transparentBlack);
                octBtns[i].setColour(juce::TextButton::textColourOffId,
                                      sel ? juce::Colour(0xff0e1018) : kDimmer);
            }
        };
        for (int i = 0; i < kNumOctBtns; ++i)
        {
            octBtns[i].setButtonText(octLabels[i]);
            octBtns[i].setClickingTogglesState(false);
            octBtns[i].onClick = [this, i] { octaveHidden.setSelectedId(i + 1); };
            addAndMakeVisible(octBtns[i]);
        }
        addChildComponent(octaveHidden);
        octaveA = std::make_unique<CA>(apvts, PID::oscOctave, octaveHidden);
    }

    // ── Noise type switchbox: W | P | B  (shared: both modes) ──
    {
        juce::StringArray noiseLabels;
        for (const auto& e : NoiseKind::kEntries) noiseLabels.add(e.label);
        noiseTypeHidden.addItemList(noiseLabels, 1);
        noiseTypeHidden.onChange = [this] {
            int id = noiseTypeHidden.getSelectedId();
            auto textCol = kDimmer;
            for (int i = 0; i < kNumNoiseBtns; ++i)
            {
                bool sel = (i + 1 == id);
                noiseBtns[i].setToggleState(sel, juce::dontSendNotification);
                noiseBtns[i].setColour(juce::TextButton::buttonColourId,
                                        sel ? kAccent : juce::Colours::transparentBlack);
                noiseBtns[i].setColour(juce::TextButton::textColourOffId,
                                        sel ? juce::Colour(0xff0e1018) : textCol);
            }
        };
        for (int i = 0; i < kNumNoiseBtns; ++i)
        {
            noiseBtns[i].setButtonText(noiseLabels[i]);
            noiseBtns[i].setClickingTogglesState(false);
            noiseBtns[i].onClick = [this, i] { noiseTypeHidden.setSelectedId(i + 1); };
            addAndMakeVisible(noiseBtns[i]);
        }
        noiseTypeA = std::make_unique<CA>(apvts, PID::noiseType, noiseTypeHidden);
    }

    noiseLevelRow = std::make_unique<SliderRow>("Lvl", fmtF2);
    addAndMakeVisible(*noiseLevelRow);
    noiseLevelA = std::make_unique<SA>(apvts, PID::noiseLevel, noiseLevelRow->getSlider());
    noiseLevelRow->updateValue();

    // ── Wavetable controls: frame count switchbox ──
    // Frame count switchbox: 32 | 64 | 128 | 256
    {
        juce::StringArray frameLabels;
        for (const auto& e : WtFrames::kEntries) frameLabels.add(e.label);
        framesHidden.addItemList(frameLabels, 1);
        framesHidden.onChange = [this] {
            int id = framesHidden.getSelectedId();
            auto textCol = kDimmer;
            for (int i = 0; i < kNumFrameBtns; ++i)
            {
                bool sel = (i + 1 == id);
                frameBtns[i].setToggleState(sel, juce::dontSendNotification);
                frameBtns[i].setColour(juce::TextButton::buttonColourId,
                                        sel ? kAccent : juce::Colours::transparentBlack);
                frameBtns[i].setColour(juce::TextButton::textColourOffId,
                                        sel ? juce::Colour(0xff0e1018) : textCol);
            }
            processorRef.reextractWavetable();
        };
        for (int i = 0; i < kNumFrameBtns; ++i)
        {
            frameBtns[i].setButtonText(frameLabels[i]);
            frameBtns[i].setClickingTogglesState(false);
            frameBtns[i].onClick = [this, i] { framesHidden.setSelectedId(i + 1); };
            addAndMakeVisible(frameBtns[i]);
        }
        wtFramesA = std::make_unique<CA>(apvts, PID::wtFrames, framesHidden);
    }

    auto setupWtToggle = [](juce::TextButton& btn) {
        btn.setClickingTogglesState(true);
        btn.onClick = [&btn] {
            bool on = btn.getToggleState();
            btn.setColour(juce::TextButton::buttonColourId,
                          on ? kAccent : juce::Colours::transparentBlack);
            btn.setColour(juce::TextButton::textColourOffId,
                          on ? juce::Colour(0xff0e1018) : kDimmer);
            btn.setColour(juce::TextButton::textColourOnId,
                          on ? juce::Colour(0xff0e1018) : juce::Colours::white);
        };
    };

    // Smooth toggle
    setupWtToggle(smoothToggle);
    smoothToggle.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(smoothToggle);
    wtSmoothA = std::make_unique<BA>(apvts, PID::wtSmooth, smoothToggle);
    smoothToggle.onClick(); // sync initial colors

    // Auto-scan toggle
    setupWtToggle(autoScanToggle);
    autoScanToggle.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(autoScanToggle);
    wtAutoScanA = std::make_unique<BA>(apvts, PID::wtAutoScan, autoScanToggle);
    autoScanToggle.onClick(); // sync initial colors

    frameCountLabel.setColour(juce::Label::textColourId, kDimmer);
    frameCountLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(frameCountLabel);

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
        juce::StringArray slopeLabels;
        for (const auto& e : FilterSlope::kEntries) slopeLabels.add(e.label);
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

    cutoffRow      = std::make_unique<SliderRow>("Cutoff",    fmtHz,  kFilterCol);
    resoRow        = std::make_unique<SliderRow>("Resonance", fmtF2,  kFilterCol);
    filterMixRow   = std::make_unique<SliderRow>("Mix",       fmtPct, kFilterCol);
    kbdTrackRow    = std::make_unique<SliderRow>("Kbd Track", fmtPct, kFilterCol);
    filterDriveRow = std::make_unique<SliderRow>("Drive",     fmtDb,  kFilterCol);
    for (auto* r : { cutoffRow.get(), resoRow.get(), filterMixRow.get(),
                     kbdTrackRow.get(), filterDriveRow.get() })
        addAndMakeVisible(*r);

    // Makeup toggle — peak-match compensation after the tanh shaper
    auto setupFilterToggle = [](juce::TextButton& btn) {
        btn.setClickingTogglesState(true);
        btn.onClick = [&btn] {
            bool on = btn.getToggleState();
            btn.setColour(juce::TextButton::buttonColourId,
                          on ? kAccent : juce::Colours::transparentBlack);
            btn.setColour(juce::TextButton::textColourOffId,
                          on ? juce::Colour(0xff0e1018) : kDimmer);
            btn.setColour(juce::TextButton::textColourOnId,
                          on ? juce::Colour(0xff0e1018) : juce::Colours::white);
        };
    };
    setupFilterToggle(filterMakeupBtn);
    addAndMakeVisible(filterMakeupBtn);

    cutoffA        = std::make_unique<SA>(apvts, PID::filterCutoff,    cutoffRow->getSlider());
    resoA          = std::make_unique<SA>(apvts, PID::filterResonance, resoRow->getSlider());
    filterMixA     = std::make_unique<SA>(apvts, PID::filterMix,       filterMixRow->getSlider());
    kbdTrackA      = std::make_unique<SA>(apvts, PID::filterKbdTrack,  kbdTrackRow->getSlider());
    filterDriveA   = std::make_unique<SA>(apvts, PID::filterDrive,     filterDriveRow->getSlider());
    filterMakeupA  = std::make_unique<BA>(apvts, PID::filterDriveMakeup, filterMakeupBtn);
    filterMakeupBtn.onClick(); // sync initial colours with APVTS state

    filterTypeA  = std::make_unique<CA>(apvts, PID::filterType,  filterTypeHidden);
    filterSlopeA = std::make_unique<CA>(apvts, PID::filterSlope, filterSlopeHidden);

    cutoffRow->updateValue();
    resoRow->updateValue();
    filterMixRow->updateValue();
    kbdTrackRow->updateValue();
    filterDriveRow->updateValue();

    // ── Envelopes ──
    initEnv(ampEnv,  "ENV 1", 2, PID::ampAttack,  PID::ampDecay,  PID::ampSustain,  PID::ampRelease,
            PID::ampAttackCurve, PID::ampDecayCurve, PID::ampReleaseCurve,
            PID::ampAttackVelMode, PID::ampDecayVelMode, PID::ampReleaseVelMode,
            PID::ampAmount,  PID::ampVelSens,  PID::ampLoop,  apvts);
    initEnv(mod1Env, "ENV 2", 1, PID::mod1Attack, PID::mod1Decay, PID::mod1Sustain, PID::mod1Release,
            PID::mod1AttackCurve, PID::mod1DecayCurve, PID::mod1ReleaseCurve,
            PID::mod1AttackVelMode, PID::mod1DecayVelMode, PID::mod1ReleaseVelMode,
            PID::mod1Amount, PID::mod1VelSens, PID::mod1Loop, apvts);
    initEnv(mod2Env, "ENV 3", 1, PID::mod2Attack, PID::mod2Decay, PID::mod2Sustain, PID::mod2Release,
            PID::mod2AttackCurve, PID::mod2DecayCurve, PID::mod2ReleaseCurve,
            PID::mod2AttackVelMode, PID::mod2DecayVelMode, PID::mod2ReleaseVelMode,
            PID::mod2Amount, PID::mod2VelSens, PID::mod2Loop, apvts);

    // ── LFOs ──
    initLfo(lfo1, "LFO 1", PID::lfo1Rate, PID::lfo1Depth, PID::lfo1Wave, PID::lfo1Mode, apvts);
    initLfo(lfo2, "LFO 2", PID::lfo2Rate, PID::lfo2Depth, PID::lfo2Wave, PID::lfo2Mode, apvts);

    // ── Drift ──
    initDrift(drift1, "DRIFT 1", PID::drift1Rate, PID::drift1Depth, PID::drift1Target, PID::drift1Wave, apvts);
    initDrift(drift2, "DRIFT 2", PID::drift2Rate, PID::drift2Depth, PID::drift2Target, PID::drift2Wave, apvts);
    initDrift(drift3, "DRIFT 3", PID::drift3Rate, PID::drift3Depth, PID::drift3Target, PID::drift3Wave, apvts);

    paintSectionHeader(driftHeader, "DRIFT", kDriftCol);
    addAndMakeVisible(driftHeader);

    // Regenerate mode switchbox
    paintSectionHeader(regenHeader, "REGENERATE", kDriftCol);
    addAndMakeVisible(regenHeader);

    juce::StringArray regenItems;
    for (const auto& e : DriftRegen::kEntries) regenItems.add(juce::String(juce::CharPointer_UTF8(e.label)));
    regenHidden.addItemList(regenItems, 1);
    regenHidden.onChange = [this] {
        int id = regenHidden.getSelectedId();
        for (int i = 0; i < kNumRegenBtns; ++i)
            regenBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
    };
    for (int i = 0; i < kNumRegenBtns; ++i)
    {
        regenBtns[i].setButtonText(regenItems[i]);
        regenBtns[i].setColour(juce::TextButton::buttonColourId, kSurface);
        regenBtns[i].setColour(juce::TextButton::buttonOnColourId, kDriftCol);
        regenBtns[i].setColour(juce::TextButton::textColourOffId, kDim);
        regenBtns[i].setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        regenBtns[i].setClickingTogglesState(true);
        regenBtns[i].setRadioGroupId(3005);
        regenBtns[i].onClick = [this, i] { regenHidden.setSelectedId(i + 1); };
        addAndMakeVisible(regenBtns[i]);
    }
    driftRegenA = std::make_unique<CA>(apvts, PID::driftRegen, regenHidden);

    // Crossfade slider for drift regeneration
    crossfadeRegenRow = std::make_unique<SliderRow>("XFade", fmtMs, kDriftCol);
    addAndMakeVisible(*crossfadeRegenRow);
    crossfadeRegenA = std::make_unique<SA>(apvts, PID::driftCrossfade, crossfadeRegenRow->getSlider());
    crossfadeRegenRow->updateValue();

    // All components are now set up — enable callbacks and trigger initial state
    initialized = true;

    // Deferred APVTS attachments for target ComboBoxes
    // (must come after initialized=true so onChange → updateVisibility works)
    mod1TargetA = std::make_unique<CA>(apvts, PID::mod1Target, mod1Env.targetBox);
    mod2TargetA = std::make_unique<CA>(apvts, PID::mod2Target, mod2Env.targetBox);
    lfo1TargetA = std::make_unique<CA>(apvts, PID::lfo1Target, lfo1.targetBox);
    lfo2TargetA = std::make_unique<CA>(apvts, PID::lfo2Target, lfo2.targetBox);

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

        // Sync shared P1/P2/P3 playback markers from the processor
        {
            float s  = processorRef.getSampler().getLoopStart();
            float e  = processorRef.getSampler().getLoopEnd();
            float p1 = processorRef.getSampler().getStartPos();
            waveformDisplay.setLoopStart(s);
            waveformDisplay.setLoopEnd(e);
            waveformDisplay.setStartPos(p1);
            waveformDisplay.getLockButton().setLocked(
                processorRef.getSampler().getPointsLocked());
        }

        processorRef.clearNewWaveformFlag();

        // Update frame count display
        int nf = processorRef.getMasterOsc().getNumFrames();
        frameCountLabel.setText(juce::String(nf) + "f", juce::dontSendNotification);
    }

    // Update ghost targets from modulated values (skip when audio is idle)
    if (!processorRef.audioIdle.load(std::memory_order_relaxed))
    {
        auto& mv = processorRef.modulatedValues;
        cutoffRow->setGhostValue(mv.filterCutoff.load(std::memory_order_relaxed));
        noiseLevelRow->setGhostValue(mv.noiseLevel.load(std::memory_order_relaxed));
        if (lfo1.rateRow)  lfo1.rateRow->setGhostValue(mv.lfo1Rate.load(std::memory_order_relaxed));
        if (lfo1.depthRow) lfo1.depthRow->setGhostValue(mv.lfo1Depth.load(std::memory_order_relaxed));
        if (lfo2.rateRow)  lfo2.rateRow->setGhostValue(mv.lfo2Rate.load(std::memory_order_relaxed));
        if (lfo2.depthRow) lfo2.depthRow->setGhostValue(mv.lfo2Depth.load(std::memory_order_relaxed));
        if (drift1.depthRow) drift1.depthRow->setGhostValue(mv.drift1Depth.load(std::memory_order_relaxed));
        if (drift2.depthRow) drift2.depthRow->setGhostValue(mv.drift2Depth.load(std::memory_order_relaxed));
        if (drift3.depthRow) drift3.depthRow->setGhostValue(mv.drift3Depth.load(std::memory_order_relaxed));
        if (waveformDisplay.isVisible())
            waveformDisplay.setScanPosition(mv.scanPosition.load(std::memory_order_relaxed));
    }

    // Advance ghost smoothing (runs every frame at 30 Hz)
    cutoffRow->tickGhost();
    noiseLevelRow->tickGhost();
    if (lfo1.rateRow)  lfo1.rateRow->tickGhost();
    if (lfo1.depthRow) lfo1.depthRow->tickGhost();
    if (lfo2.rateRow)  lfo2.rateRow->tickGhost();
    if (lfo2.depthRow) lfo2.depthRow->tickGhost();
    if (drift1.depthRow) drift1.depthRow->tickGhost();
    if (drift2.depthRow) drift2.depthRow->tickGhost();
    if (drift3.depthRow) drift3.depthRow->tickGhost();
    waveformDisplay.tickScan();
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
    for (auto* r : { cutoffRow.get(), resoRow.get(), filterMixRow.get(),
                     kbdTrackRow.get(), filterDriveRow.get() })
    {
        r->setAlpha(filterAlpha);
        r->setEnabled(filterOn);
    }
    filterMakeupBtn.setAlpha(filterAlpha);
    filterMakeupBtn.setEnabled(filterOn);

    bool isWavetable = engineModeHidden.getSelectedId() == 2;
    bool isSampler = !isWavetable;

    // Shared playback traversal controls
    oneshotBtn.setVisible(true);
    loopModeBtn.setVisible(true);
    pingpongBtn.setVisible(true);

    // Sampler-only controls
    crossfadeRow->setVisible(isSampler);
    loopOptimizeBtn.setVisible(isSampler);
    normalizeToggle.setVisible(isSampler);
    hfBoostBtn.setVisible(isSampler);
    for (int i = 0; i < kNumOctBtns; ++i)
        octBtns[i].setVisible(isSampler);

    // Wavetable-only controls
    scanRow->setVisible(isWavetable);
    scanHint.setVisible(isWavetable);
    for (int i = 0; i < kNumFrameBtns; ++i)
        frameBtns[i].setVisible(isWavetable);
    smoothToggle.setVisible(isWavetable);
    autoScanToggle.setVisible(isWavetable);
    frameCountLabel.setVisible(isWavetable);

    waveformDisplay.setRegionLabel("Loop interval");

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
        env.aCurveBtn.setAlpha(alpha);
        env.dCurveBtn.setAlpha(alpha);
        env.rCurveBtn.setAlpha(alpha);
        for (auto* r : { env.aRow.get(), env.dRow.get(), env.sRow.get(),
                         env.rRow.get(), env.amtRow.get(), env.velRow.get() })
            if (r) r->setAlpha(alpha);
    };
    setEnvDimmed(ampEnv);
    setEnvDimmed(mod1Env);
    setEnvDimmed(mod2Env);

    auto setLfoDimmed = [dimAlpha](LfoSection& lfo) {
        bool active = lfo.targetBox.getSelectedId() != 1; // 1 = "---"
        float alpha = active ? 1.0f : dimAlpha;
        lfo.waveBox.setAlpha(alpha);
        lfo.modeBox.setAlpha(alpha);
        if (lfo.rateRow)  lfo.rateRow->setAlpha(alpha);
        if (lfo.depthRow) lfo.depthRow->setAlpha(alpha);
    };
    setLfoDimmed(lfo1);
    setLfoDimmed(lfo2);

    auto setDriftDimmed = [dimAlpha](DriftSection& drift) {
        bool active = drift.targetBox.getSelectedId() != 1; // 1 = "---"
        float alpha = active ? 1.0f : dimAlpha;
        drift.waveBox.setAlpha(alpha);
        if (drift.rateRow)  drift.rateRow->setAlpha(alpha);
        if (drift.depthRow) drift.depthRow->setAlpha(alpha);
    };
    setDriftDimmed(drift1);
    setDriftDimmed(drift2);
    setDriftDimmed(drift3);

    driftHeader.setAlpha(1.0f);
    regenHeader.setAlpha(1.0f);
    for (int i = 0; i < kNumRegenBtns; ++i)
        regenBtns[i].setAlpha(1.0f);
    if (crossfadeRegenRow)
        crossfadeRegenRow->setAlpha(1.0f);
}

float SynthPanel::fs() const
{
    // Derive font size from available height so all content fits.
    // Content budget: ~53 f-units (27 rows * 1.4 + headers + gaps)
    // plus waveform at 12% of panel height.
    float h = static_cast<float>(getHeight());
    float padY = h * 0.005f;
    float available = h - 2.0f * padY;
    float waveform = h * 0.08f;
    float remaining = available - waveform;
    float maxF = remaining / 56.0f;
    return juce::jlimit(9.0f, 20.0f, maxF);
}

static int modulationForcedLabelWidthFor(SliderRow& row, int rowWidth, int curveButtonSize = 0)
{
    int width = row.getNaturalLabelWidthForAvailableWidth(rowWidth);
    if (curveButtonSize > 0)
        width += curveButtonSize + 8;
    return width;
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
    int btnSize = rowH - 2;  // square, slightly smaller than row height
    auto adRow = area.removeFromTop(rowH);
    auto adBounds = layoutSliderRowPairBounds(adRow, *env.aRow, *env.dRow, 4);
    env.aRow->setBounds(adBounds[0]);
    env.dRow->setBounds(adBounds[1]);
    env.aCurveBtn.setBounds(adBounds[0].getX() + env.aRow->getLabelWidthForAvailableWidth(adBounds[0].getWidth()) - btnSize,
                            adBounds[0].getY() + 1, btnSize, btnSize);
    env.dCurveBtn.setBounds(adBounds[1].getX() + env.dRow->getLabelWidthForAvailableWidth(adBounds[1].getWidth()) - btnSize,
                            adBounds[1].getY() + 1, btnSize, btnSize);

    auto srRow = area.removeFromTop(rowH);
    auto srBounds = layoutSliderRowPairBounds(srRow, *env.sRow, *env.rRow, 4);
    env.sRow->setBounds(srBounds[0]);
    env.rRow->setBounds(srBounds[1]);
    env.rCurveBtn.setBounds(srBounds[1].getX() + env.rRow->getLabelWidthForAvailableWidth(srBounds[1].getWidth()) - btnSize,
                            srBounds[1].getY() + 1, btnSize, btnSize);

    auto amtRow = area.removeFromTop(rowH);
    auto amtBounds = layoutSliderRowPairBounds(amtRow, *env.amtRow, *env.velRow, 4);
    env.amtRow->setBounds(amtBounds[0]);
    env.velRow->setBounds(amtBounds[1]);

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
    auto slRow = area.removeFromTop(rowH);
    auto lfoBounds = layoutSliderRowPairBounds(slRow, *lfo.rateRow, *lfo.depthRow, 4);
    lfo.rateRow->setBounds(lfoBounds[0]);
    lfo.depthRow->setBounds(lfoBounds[1]);

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
    auto slRow = area.removeFromTop(rowH);
    auto driftBounds = layoutSliderRowPairBounds(slRow, *drift.rateRow, *drift.depthRow, 4);
    drift.rateRow->setBounds(driftBounds[0]);
    drift.depthRow->setBounds(driftBounds[1]);

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
            paintSwitchBoxBorder(g, loopSwitchBounds);
        if (frameBtns[0].isVisible())
            paintSwitchBoxBorder(g, framesSwitchBounds);
        if (octBtns[0].isVisible())
            paintSwitchBoxBorder(g, octaveSwitchBounds);
        if (noiseBtns[0].isVisible())
            paintSwitchBoxBorder(g, noiseSwitchBounds);
    }

    // Card: Filter section
    {
        int top = filterHeader.getY() - inset;
        int bot = filterDriveRow->getBottom();
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
        bot = juce::jmax(bot, drift1.depthRow->getBottom(), drift2.depthRow->getBottom(),
                         drift3.depthRow->getBottom());
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

        modeRow.removeFromLeft(juce::roundToInt(f * 1.5f)); // same gap
        tuningBox.setBounds(modeRow.removeFromLeft(juce::roundToInt(f * 5.5f)));
        tuningBox.setJustificationType(juce::Justification::centred);
    }
    area.removeFromTop(gap);

    // ── Waveform — give it all space not needed by sections below ──
    // Calculate height needed below: sampler/WT controls + filter + mod + LFO + drift
    // Always reserve same space for engine controls (max of sampler/WT)
    // so waveform height stays stable when switching modes
    int samplerCtrlH = rowH + gap * 2; // one controls row (sampler or wavetable)
    int filterH = headerH + headerGap + rowH + gap + rowH * 2 + gap; // header + type/slope/topology + cutoff/reso + mix/kbd
    int modH = gap * 3 + headerH + headerGap; // section gap + header
    int envH = (rowH * 4 + gap) * 3; // 3 envelopes × (header + 3 slider rows + gap)
    int lfoH = gap + (rowH * 2 + gap) * 2; // 2 LFOs × (header + rate row + gap)
    int driftH = gap + headerH + headerGap + (rowH * 2 + gap) * 3
               + gap + headerH + headerGap + rowH; // drift header + 3 drifts + regen header + row
    int belowWave = samplerCtrlH + filterH + modH + envH + lfoH + driftH + gap * 5;
    int maxWaveH = juce::roundToInt(area.getHeight() * 0.14f); // cap waveform to ~14% of panel
    int waveH = juce::jlimit(60, maxWaveH, area.getHeight() - belowWave);

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

        area.removeFromTop(gap);

        // [→][↻][⇄] [32|64|128|256] [Smooth] | [Nf] [White|Pink|Brown] Lvl[===]
        auto wtRow = area.removeFromTop(rowH);
        int leftW = juce::roundToInt(wtRow.getWidth() * 0.62f);
        auto leftCol = wtRow.removeFromLeft(leftW);
        wtRow.removeFromLeft(4); // column gap

        // ── Left column: loop icons + frame switchbox + smooth ──
        int iconW = juce::roundToInt(f * 2.6f);
        oneshotBtn.setBounds(leftCol.removeFromLeft(iconW));
        loopModeBtn.setBounds(leftCol.removeFromLeft(iconW));
        pingpongBtn.setBounds(leftCol.removeFromLeft(iconW));
        loopSwitchBounds = oneshotBtn.getBounds().getUnion(pingpongBtn.getBounds());
        leftCol.removeFromLeft(juce::roundToInt(f * 0.35f));

        int cellW = juce::roundToInt(f * 2.6f);
        for (int i = 0; i < kNumFrameBtns; ++i)
            frameBtns[i].setBounds(leftCol.removeFromLeft(cellW));
        framesSwitchBounds = frameBtns[0].getBounds().getUnion(frameBtns[kNumFrameBtns - 1].getBounds());
        leftCol.removeFromLeft(juce::roundToInt(f * 0.35f));

        int smoothW = juce::jmin(juce::roundToInt(f * 4.0f), leftCol.getWidth());
        smoothToggle.setBounds(leftCol.removeFromLeft(smoothW));
        leftCol.removeFromLeft(juce::roundToInt(f * 0.25f));
        int autoW = juce::jmin(juce::roundToInt(f * 3.8f), leftCol.getWidth());
        autoScanToggle.setBounds(leftCol.removeFromLeft(autoW));

        // ── Right column: [Nf] [White|Pink|Brown] Lvl[===] ──
        int frameCountW = juce::roundToInt(f * 2.8f);
        frameCountLabel.setBounds(wtRow.removeFromLeft(frameCountW));
        wtRow.removeFromLeft(juce::roundToInt(f * 0.45f));
        int nCellW = juce::roundToInt(f * 4.0f);
        for (int i = 0; i < kNumNoiseBtns; ++i)
            noiseBtns[i].setBounds(wtRow.removeFromLeft(nCellW));
        noiseSwitchBounds = noiseBtns[0].getBounds().getUnion(noiseBtns[kNumNoiseBtns - 1].getBounds());
        noiseLevelRow->setBounds(wtRow);

        area.removeFromTop(gap);
        engineCardBottom = juce::jmax(smoothToggle.getBottom(), noiseLevelRow->getBottom());
    }
    else
    {
        // ── Sampler: waveform + bracket handles + controls row ──
        int handleLineH = juce::roundToInt(WaveformDisplay::HANDLE_RADIUS * 2.0f + 4.0f);
        waveformDisplay.setBottomReserve(handleLineH);
        waveformDisplay.setScanVisible(false);
        waveformDisplay.setBounds(area.removeFromTop(waveH + handleLineH));
        area.removeFromTop(gap);  // spacing to controls

        // [→][↻][⇄] [Opt] Xfade[========] [Norm] | [White|Pink|Brown] Lvl[===]
        auto loopRow = area.removeFromTop(rowH);
        int colW = (loopRow.getWidth() - 4) / 2;

        // ── Left column: loop icons + Opt + Xfade + Norm ──
        auto leftCol = loopRow.removeFromLeft(colW);
        int iconW = juce::roundToInt(f * 2.8f);
        oneshotBtn.setBounds(leftCol.removeFromLeft(iconW));
        loopModeBtn.setBounds(leftCol.removeFromLeft(iconW));
        pingpongBtn.setBounds(leftCol.removeFromLeft(iconW));
        loopSwitchBounds = oneshotBtn.getBounds().getUnion(pingpongBtn.getBounds());
        leftCol.removeFromLeft(juce::roundToInt(f * 0.3f));

        int optW = juce::roundToInt(f * 4.5f);
        loopOptimizeBtn.setBounds(leftCol.removeFromLeft(optW));
        leftCol.removeFromLeft(2);

        // HF + Norm at the right end of left column (gap to noise switchbox)
        leftCol.removeFromRight(juce::roundToInt(f * 1.5f));
        int normW = juce::roundToInt(f * 4.0f);
        int hfW = juce::roundToInt(f * 2.8f);
        normalizeToggle.setBounds(leftCol.removeFromRight(normW));
        hfBoostBtn.setBounds(leftCol.removeFromRight(hfW));
        leftCol.removeFromRight(2);

        // Xfade gets remaining space
        crossfadeRow->setBounds(leftCol);

        loopRow.removeFromLeft(4); // column gap

        // ── Right column: [-2|-1|0|+1|+2] [White|Pink|Brown] Lvl[===] ──
        int oCellW = juce::roundToInt(f * 2.5f);
        for (int i = 0; i < kNumOctBtns; ++i)
            octBtns[i].setBounds(loopRow.removeFromLeft(oCellW));
        octaveSwitchBounds = octBtns[0].getBounds().getUnion(octBtns[kNumOctBtns - 1].getBounds());
        loopRow.removeFromLeft(juce::roundToInt(f * 1.5f));

        int nCellW = juce::roundToInt(f * 4.0f);
        for (int i = 0; i < kNumNoiseBtns; ++i)
            noiseBtns[i].setBounds(loopRow.removeFromLeft(nCellW));
        noiseSwitchBounds = noiseBtns[0].getBounds().getUnion(noiseBtns[kNumNoiseBtns - 1].getBounds());
        noiseLevelRow->setBounds(loopRow);

        area.removeFromTop(gap);
        engineCardBottom = oneshotBtn.getBottom();

        // Hide wavetable-only controls in sampler mode
        frameCountLabel.setBounds(-1000, -1000, 10, 10);
    }
    area.removeFromTop(gap * 2);

    // ── FILTER section header ──
    filterHeader.setFont(juce::FontOptions(headerFs));
    filterHeader.setBounds(area.removeFromTop(headerH));
    area.removeFromTop(headerGap);

    // ── Filter switchboxes: [OFF LP HP BP] [6dB 12dB 18dB 24dB] ──
    auto filterHdr = area.removeFromTop(rowH);
    {
        const int groupGap = juce::roundToInt(f * 0.75f);
        const int typeCellW = juce::roundToInt(f * 3.2f);
        const int slopeCellW = juce::roundToInt(f * 3.2f);

        auto typeArea = filterHdr.removeFromLeft(typeCellW * kNumTypeBtns);
        filterHdr.removeFromLeft(groupGap);
        auto slopeArea = filterHdr.removeFromLeft(slopeCellW * kNumSlopeBtns);

        for (int i = 0; i < kNumTypeBtns; ++i)
            filterTypeBtns[i].setBounds(typeArea.removeFromLeft(typeCellW));
        filterTypeSwitchBounds = filterTypeBtns[0].getBounds()
            .getUnion(filterTypeBtns[kNumTypeBtns - 1].getBounds());

        for (int i = 0; i < kNumSlopeBtns; ++i)
            filterSlopeBtns[i].setBounds(slopeArea.removeFromLeft(slopeCellW));
        filterSlopeSwitchBounds = filterSlopeBtns[0].getBounds()
            .getUnion(filterSlopeBtns[kNumSlopeBtns - 1].getBounds());
    }
    area.removeFromTop(gap);

    {
        // Always allocate — dimmed when filter off
        auto row1 = area.removeFromTop(rowH);
        auto filterBounds1 = layoutSliderRowPairBounds(row1, *cutoffRow, *resoRow, 4);
        cutoffRow->setBounds(filterBounds1[0]);
        resoRow->setBounds(filterBounds1[1]);

        auto row2 = area.removeFromTop(rowH);
        auto filterBounds2 = layoutSliderRowPairBounds(row2, *filterMixRow, *kbdTrackRow, 4);
        filterMixRow->setBounds(filterBounds2[0]);
        kbdTrackRow->setBounds(filterBounds2[1]);

        // Drive row: slider on the left, Makeup toggle on the right
        auto row3 = area.removeFromTop(rowH);
        const int makeupW = juce::roundToInt(f * 6.0f);
        auto makeupArea = row3.removeFromRight(makeupW);
        row3.removeFromRight(4);
        filterDriveRow->setBounds(row3);
        // Center the toggle vertically within the row
        const int toggleH = juce::roundToInt(rowH * 0.72f);
        filterMakeupBtn.setBounds(makeupArea.withSizeKeepingCentre(makeupArea.getWidth(), toggleH));

        area.removeFromTop(gap);
    }

    int sectionGap = gap * 3;

    // ── MODULATION section header ──
    area.removeFromTop(sectionGap);
    modHeader.setFont(juce::FontOptions(headerFs));
    modHeader.setBounds(area.removeFromTop(headerH));
    area.removeFromTop(headerGap);

    // ── Envelopes ──
    {
        const int modPairGap = 4;
        const int modColumnWidth = juce::jmax(0, (area.getWidth() - modPairGap) / 2);
        const int curveLabelMin = rowH - 2;

        // ENV/LFO/Drift share one left label column and one right label column so
        // rows stay visually aligned across the whole modulation section. ENV
        // curve buttons live inside that reserved label area, so their width plus
        // a small buffer must be included when computing the forced label widths.
        // Use natural text widths here; recomputing from already-forced widths
        // causes resize feedback and can push sliders progressively to the right.

        const int leftLabelWidth = std::max({
            modulationForcedLabelWidthFor(*ampEnv.aRow, modColumnWidth, curveLabelMin),
            modulationForcedLabelWidthFor(*ampEnv.sRow, modColumnWidth),
            modulationForcedLabelWidthFor(*ampEnv.amtRow, modColumnWidth),
            modulationForcedLabelWidthFor(*lfo1.rateRow, modColumnWidth),
            modulationForcedLabelWidthFor(*drift1.rateRow, modColumnWidth)
        });

        const int rightLabelWidth = std::max({
            modulationForcedLabelWidthFor(*ampEnv.dRow, modColumnWidth, curveLabelMin),
            modulationForcedLabelWidthFor(*ampEnv.rRow, modColumnWidth, curveLabelMin),
            modulationForcedLabelWidthFor(*ampEnv.velRow, modColumnWidth),
            modulationForcedLabelWidthFor(*lfo1.depthRow, modColumnWidth),
            modulationForcedLabelWidthFor(*drift1.depthRow, modColumnWidth)
        });

        for (auto* row : {
                 ampEnv.aRow.get(), ampEnv.sRow.get(), ampEnv.amtRow.get(),
                 mod1Env.aRow.get(), mod1Env.sRow.get(), mod1Env.amtRow.get(),
                 mod2Env.aRow.get(), mod2Env.sRow.get(), mod2Env.amtRow.get(),
                 lfo1.rateRow.get(), lfo2.rateRow.get(),
                 drift1.rateRow.get(), drift2.rateRow.get(), drift3.rateRow.get() })
            row->setForcedLabelWidth(leftLabelWidth);

        for (auto* row : {
                 ampEnv.dRow.get(), ampEnv.rRow.get(), ampEnv.velRow.get(),
                 mod1Env.dRow.get(), mod1Env.rRow.get(), mod1Env.velRow.get(),
                 mod2Env.dRow.get(), mod2Env.rRow.get(), mod2Env.velRow.get(),
                 lfo1.depthRow.get(), lfo2.depthRow.get(),
                 drift1.depthRow.get(), drift2.depthRow.get(), drift3.depthRow.get() })
            row->setForcedLabelWidth(rightLabelWidth);
    }

    layoutEnv(ampEnv,  area, f, rowH, gap);
    layoutEnv(mod1Env, area, f, rowH, gap);
    layoutEnv(mod2Env, area, f, rowH, gap);

    // ── LFOs ──
    area.removeFromTop(gap);
    layoutLfo(lfo1, area, f, rowH, gap);
    layoutLfo(lfo2, area, f, rowH, gap);

    // ── Drift (part of modulation section) ──
    area.removeFromTop(gap);
    driftHeader.setFont(juce::FontOptions(headerFs));
    driftHeader.setBounds(area.removeFromTop(headerH));
    area.removeFromTop(headerGap);
    layoutDrift(drift1, area, f, rowH, gap);
    layoutDrift(drift2, area, f, rowH, gap);
    layoutDrift(drift3, area, f, rowH, gap);

    // ── Regenerate ──
    area.removeFromTop(gap);
    regenHeader.setFont(juce::FontOptions(headerFs));
    regenHeader.setBounds(area.removeFromTop(headerH));
    area.removeFromTop(headerGap);
    {
        auto regenRow = area.removeFromTop(rowH);
        int regenCellW = juce::roundToInt(f * 3.5f);
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
        // XFade slider in the same row, capped to left half of panel
        regenRow.removeFromLeft(juce::roundToInt(f * 0.5f)); // small gap
        int halfW = area.getWidth() / 2;
        int xfadeMaxW = halfW - (regenRow.getX() - area.getX());
        crossfadeRegenRow->setBounds(regenRow.removeFromLeft(std::max(0, xfadeMaxW)));
    }
}
