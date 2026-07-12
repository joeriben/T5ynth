#include "SynthPanel.h"
#include "../PluginProcessor.h"
#include "MidiLearnMenu.h"
#include <algorithm>
#include <cmath>

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
// Percent with one decimal below 10% — keeps the sub-1% range readable
// for skewed depth knobs (where pitch LFO is musically usable) without
// noise-precision at higher values.
static juce::String fmtPctFine(double v)
{
    double p = v * 100.0;
    return std::abs(p) < 10.0 ? juce::String(p, 1) + "%"
                              : juce::String(juce::roundToInt(p)) + "%";
}
static juce::String fmtHz(double v)  { return juce::String(juce::roundToInt(v)) + " Hz"; }
static juce::String fmtDb(double v)  { return juce::String(v, 1) + " dB"; }
static juce::String fmtHzF1(double v){ return juce::String(v, 1) + " Hz"; }
static juce::String fmtHzF2(double v){ return juce::String(v, 2) + " Hz"; }
static juce::String fmtHzF3(double v){ return juce::String(v, 3) + " Hz"; }
// Drift "Rate" in FREE mode reads as the cycle PERIOD (seconds) rather than a
// three-decimal sub-Hz value: 0.002 Hz → "500 s/cyc", 1 Hz → "1.0 s/cyc".
// Adaptive precision keeps it short. (Sync mode uses the musical-division label.)
static juce::String fmtPeriodSc(double hz)
{
    const double s = (hz > 1.0e-6) ? 1.0 / hz : 0.0;
    const juce::String num = (s >= 100.0) ? juce::String(juce::roundToInt(s))
                           : (s >= 1.0)   ? juce::String(s, 1)
                                          : juce::String(s, 2);
    return num + " s/cyc";
}

// ──────────────────────────────────────────────────────────────────────────────
// Envelope init
// ──────────────────────────────────────────────────────────────────────────────
void SynthPanel::initEnv(EnvSection& env, const juce::String& name, int defaultTarget,
                          const juce::String& aId, const juce::String& dId,
                          const juce::String& sId, const juce::String& rId,
                          const juce::String& aCurveId, const juce::String& dCurveId,
                          const juce::String& rCurveId,
                          const juce::String& aVsId, const juce::String& dVsId,
                          const juce::String& rVsId,
                          const juce::String& amtId,
                          const juce::String& loopId,
                          juce::AudioProcessorValueTreeState& apvts)
{
    juce::ignoreUnused(name);   // no longer shown: the advanced-view title was removed

    // Loop toggle — turns the env into a self-retriggering A→D→Hold→R cycle.
    // In loop mode the Sustain control becomes the per-cycle Hold time.
    env.loopBtn.setButtonText("LOOP");
    styleSwitchButton(env.loopBtn, kEnvCol);
    env.loopBtn.setClickingTogglesState(true);
    env.loopBtn.setTooltip("Loop the envelope as an A-D-Hold-R cycle. In loop mode Sustain sets the Hold time.");
    addAndMakeVisible(env.loopBtn);
    env.loopBtnA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, loopId, env.loopBtn);

    // Labels driven from BlockParams::EnvTarget::kEntries (single source of
    // truth for enum index ↔ human-readable label).
    juce::StringArray envItems;
    for (const auto& e : EnvTarget::kEntries) envItems.add(e.label);
    env.targetBox.addItemList(envItems, 1);
    env.targetBox.setSelectedId(defaultTarget, juce::dontSendNotification);
    env.targetBox.onChange = [this] { updateVisibility(); resized(); };
    addAndMakeVisible(env.targetBox);

    // Easy-view "Target" left-header band (accent@0.7 + white, like SNAP/CACHE).
    paintSectionHeader(env.targetHeader, "Target", kEnvCol);
    addAndMakeVisible(env.targetHeader);

    // ── Easy-view "Velocity Amount" box (vertical drag-fill bars) ──
    // Att/Dec/Rel are bipolar (velocity→stage TIME, mirror the velSens above);
    // Level is unipolar (the GLOBAL velocity→peak amount, velAmt). Same widget
    // feel as the aftertouch bars, just vertical.
    env.attVB   = std::make_unique<VelocityBar>(true);
    env.decVB   = std::make_unique<VelocityBar>(true);
    env.relVB   = std::make_unique<VelocityBar>(true);
    env.levelVB = std::make_unique<VelocityBar>(false);
    env.attVB->setBarLabel("Att");
    env.decVB->setBarLabel("Dec");
    env.relVB->setBarLabel("Rel");
    env.levelVB->setBarLabel("Level");
    for (auto* vb : { env.attVB.get(), env.decVB.get(), env.relVB.get(), env.levelVB.get() })
        addAndMakeVisible(*vb);

    env.attVBA   = std::make_unique<SA>(apvts, aVsId,       *env.attVB);
    env.decVBA   = std::make_unique<SA>(apvts, dVsId,       *env.decVB);
    env.relVBA   = std::make_unique<SA>(apvts, rVsId,       *env.relVB);
    env.levelVBA = std::make_unique<SA>(apvts, PID::velAmt, *env.levelVB);

    // Host automation gestures (the attachment can't, since we bypass the base
    // drag) + MIDI-learn parity with the advanced rows.
    auto wireVB = [this, &apvts](VelocityBar& vb, const juce::String& pid) {
        if (auto* p = apvts.getParameter(pid))
        {
            vb.onDragStart = [p] { p->beginChangeGesture(); };
            vb.onDragEnd   = [p] { p->endChangeGesture(); };
        }
        vb.onRightClick = [this, pid](juce::Point<int> pt) {
            showMidiLearnMenu(processorRef, pid, pt); };
    };
    wireVB(*env.attVB,   aVsId);
    wireVB(*env.decVB,   dVsId);
    wireVB(*env.relVB,   rVsId);
    wireVB(*env.levelVB, PID::velAmt);

    env.velBox.configure("VELOCITY AMOUNT", kEnvCol, Icon::numIcons);
    addAndMakeVisible(env.velBox);
    env.velBox.toBack();   // decorative frame — must sit behind the bars it frames

    // ── Graphical ADSR editor ──
    // Attaches DIRECTLY to the APVTS parameters (not to the faders), so APVTS is
    // the single source of truth and no hidden slider is load-bearing. fmtMs
    // formats A/D/R, fmtF2 formats Sustain/Amt — mirroring the fader read-outs.
    env.graph = std::make_unique<AdsrGraph>(kEnvCol);
    env.graph->bind(apvts, aId, dId, sId, rId, amtId, aCurveId, dCurveId, rCurveId,
                    fmtMs, fmtF2);
    addAndMakeVisible(*env.graph);
}

// ──────────────────────────────────────────────────────────────────────────────
// LFO init
// ──────────────────────────────────────────────────────────────────────────────
void SynthPanel::initLfo(LfoSection& lfo, const juce::String& name,
                          const juce::String& rateId, const juce::String& depthId,
                          const juce::String& waveId, const juce::String& modeId,
                          const juce::String& clockModeId, const juce::String& divisionId,
                          juce::AudioProcessorValueTreeState& apvts)
{
    lfo.header.setText(name, juce::dontSendNotification);
    labelAsTitle(lfo.header, kLfoCol);
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
    for (int i = 0; i < kNumWaveBtns; ++i)
    {
        auto& btn = lfo.waveBtns[static_cast<size_t>(i)];
        btn.setButtonText(lfoWaveItems[i]);
        btn.setColour(juce::TextButton::buttonColourId, kSurface);
        btn.setColour(juce::TextButton::buttonOnColourId, kLfoCol);
        btn.setColour(juce::TextButton::textColourOffId, kDim);
        btn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn.setClickingTogglesState(false);
        btn.onClick = [&lfo, i] { lfo.waveBox.setSelectedId(i + 1); };
        addAndMakeVisible(btn);
    }

    // Free/Trig: hidden ComboBox holds the APVTS state; visible TextButton
    // cycles between F and T on click. Replaces the old inline "Free⌄"
    // dropdown which was visually too wide for the row.
    juce::StringArray lfoModeItems;
    for (const auto& e : LfoMode::kEntries) lfoModeItems.add(e.label);
    lfo.modeHidden.addItemList(lfoModeItems, 1);
    const juce::StringArray lfoModeBtnLabels { "Free", "Trig" };
    for (int i = 0; i < kNumLfoModeBtns; ++i)
    {
        auto& btn = lfo.modeBtns[static_cast<size_t>(i)];
        btn.setButtonText(lfoModeBtnLabels[i]);
        btn.setColour(juce::TextButton::buttonColourId, kSurface);
        btn.setColour(juce::TextButton::buttonOnColourId, kLfoCol);
        btn.setColour(juce::TextButton::textColourOffId, kDim);
        btn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn.setClickingTogglesState(false);
        btn.onClick = [&lfo, i] { lfo.modeHidden.setSelectedId(i + 1); };
        addAndMakeVisible(btn);
    }
    lfo.modeBtn.setLookAndFeel(&lfoModeLnf);   // padlock: open = Free, closed = Trig
    lfo.modeBtn.setTooltip("LFO phase: unlocked = free-running, locked = reset on each note-on");
    lfo.modeBtn.setClickingTogglesState(false);
    lfo.modeBtn.onClick = [&lfo] {
        const int cur = lfo.modeHidden.getSelectedId();
        lfo.modeHidden.setSelectedId(cur == 1 ? 2 : 1);
    };
    lfo.modeHidden.onChange = [&lfo] {
        const bool trig = lfo.modeHidden.getSelectedId() == 2;
        // Drive the padlock: locked (closed) = Trig, open = Free.
        lfo.modeBtn.setToggleState(trig, juce::dontSendNotification);
        lfo.modeBtn.repaint();
        for (int i = 0; i < kNumLfoModeBtns; ++i)
        {
            auto& btn = lfo.modeBtns[static_cast<size_t>(i)];
            const bool active = lfo.modeHidden.getSelectedId() == i + 1;
            btn.setToggleState(active, juce::dontSendNotification);
            btn.setColour(juce::TextButton::buttonColourId, active ? kLfoCol : kSurface);
            btn.setColour(juce::TextButton::textColourOffId, active ? juce::Colours::white : kDim);
        }
    };
    addAndMakeVisible(lfo.modeBtn);

    // BPM-sync clock: hidden ComboBox holds APVTS state, visible icon
    // button drives it. ClockMode change swaps which row is visible
    // (Hz rateRow vs musical-division divisionRow).
    juce::StringArray clockItems;
    for (const auto& e : ClockMode::kEntries) clockItems.add(e.label);
    lfo.clockModeHidden.addItemList(clockItems, 1);
    lfo.clockBtn.setButtonText("SYNC");
    lfo.clockBtn.setLookAndFeel(&lfoClockLnf);
    lfo.clockBtn.setClickingTogglesState(false);
    lfo.clockBtn.onClick = [&lfo] {
        const int cur = lfo.clockModeHidden.getSelectedId();
        lfo.clockModeHidden.setSelectedId(cur == 1 ? 2 : 1);
    };
    addAndMakeVisible(lfo.clockBtn);

    // Hz row + Division row occupy the same on-screen rect; visibility
    // swaps based on ClockMode (Off → rate, Sync → division).
    lfo.rateRow     = std::make_unique<SliderRow>("Rate", fmtHzF1, kLfoCol);
    lfo.depthRow    = std::make_unique<SliderRow>("Amt", fmtPctFine, kLfoCol);
    lfo.divisionRow = std::make_unique<SliderRow>("Rate",
        [](double v) {
            const int idx = juce::jlimit(0, ClockDivision::kCount - 1,
                                          juce::roundToInt(v));
            return juce::String(ClockDivision::kEntries[idx].label);
        }, kLfoCol);
    lfo.divisionRow->getSlider().setRange(
        0.0, static_cast<double>(ClockDivision::kCount - 1), 1.0);
    // Lock the value column to a fixed pixel width so the slider track's
    // RIGHT edge stays put when ClockMode swaps "1.5 Hz" ↔ "1/16T". The
    // label column is forced from SynthPanel::resized() to the
    // modulation-section-wide left column width — do NOT force it here.
    lfo.rateRow->setForcedValueWidth(56);
    lfo.divisionRow->setForcedValueWidth(56);
    addAndMakeVisible(*lfo.rateRow);
    addAndMakeVisible(*lfo.depthRow);
    addAndMakeVisible(*lfo.divisionRow);

    lfo.rateA      = std::make_unique<SA>(apvts, rateId,      lfo.rateRow->getSlider());
    lfo.depthA     = std::make_unique<SA>(apvts, depthId,     lfo.depthRow->getSlider());
    lfo.waveA      = std::make_unique<CA>(apvts, waveId,      lfo.waveBox);
    lfo.modeA      = std::make_unique<CA>(apvts, modeId,      lfo.modeHidden);
    lfo.divisionA  = std::make_unique<SA>(apvts, divisionId,  lfo.divisionRow->getSlider());
    lfo.clockModeA = std::make_unique<CA>(apvts, clockModeId, lfo.clockModeHidden);

    auto wireMidiRow = [this](SliderRow* row, const juce::String& pid) {
        row->onRightClick = [this, pid](juce::Point<int> pos) {
            showMidiLearnMenu(processorRef, pid, pos); };
    };
    wireMidiRow(lfo.rateRow.get(),     rateId);
    wireMidiRow(lfo.depthRow.get(),    depthId);
    wireMidiRow(lfo.divisionRow.get(), divisionId);

    lfo.waveBox.onChange = [&lfo] {
        const int selected = lfo.waveBox.getSelectedId();
        for (int i = 0; i < kNumWaveBtns; ++i)
        {
            auto& btn = lfo.waveBtns[static_cast<size_t>(i)];
            const bool active = selected == i + 1;
            btn.setToggleState(active, juce::dontSendNotification);
            btn.setColour(juce::TextButton::buttonColourId, active ? kLfoCol : kSurface);
            btn.setColour(juce::TextButton::textColourOffId, active ? juce::Colours::white : kDim);
        }
    };

    // The CA was attached above; its initial setSelectedId fired before
    // this lambda existed, so we set the lambda now and invoke it
    // explicitly below to sync the visible state.
    lfo.clockModeHidden.onChange = [&lfo] {
        const bool sync = lfo.clockModeHidden.getSelectedId() == 2;
        lfo.clockBtn.setToggleState(sync, juce::dontSendNotification);
        lfo.clockBtn.repaint();
        if (lfo.rateRow)     lfo.rateRow->setVisible(!sync);
        if (lfo.divisionRow) lfo.divisionRow->setVisible(sync);
    };
    // Sync initial state from the loaded APVTS values.
    lfo.modeHidden.onChange();
    lfo.clockModeHidden.onChange();
    lfo.waveBox.onChange();

    lfo.rateRow->updateValue();
    lfo.depthRow->updateValue();
    lfo.divisionRow->updateValue();
}

// ──────────────────────────────────────────────────────────────────────────────
// Drift init
// ──────────────────────────────────────────────────────────────────────────────
void SynthPanel::initDrift(DriftSection& drift, const juce::String& name,
                            const juce::String& rateId, const juce::String& depthId,
                            const juce::String& targetId, const juce::String& waveId,
                            const juce::String& clockModeId, const juce::String& divisionId,
                            juce::AudioProcessorValueTreeState& apvts)
{
    drift.header.setText(name, juce::dontSendNotification);
    labelAsTitle(drift.header, kDriftCol);
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
    for (int i = 0; i < kNumWaveBtns; ++i)
    {
        auto& btn = drift.waveBtns[static_cast<size_t>(i)];
        btn.setButtonText(driftWaveItems[i]);
        btn.setColour(juce::TextButton::buttonColourId, kSurface);
        btn.setColour(juce::TextButton::buttonOnColourId, kDriftCol);
        btn.setColour(juce::TextButton::textColourOffId, kDim);
        btn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn.setClickingTogglesState(false);
        btn.onClick = [&drift, i] { drift.waveBox.setSelectedId(i + 1); };
        addAndMakeVisible(btn);
    }

    // BPM-sync clock — same pattern as LFO; Drift has no Free/Trig so no
    // mode button.
    juce::StringArray clockItems;
    for (const auto& e : ClockMode::kEntries) clockItems.add(e.label);
    drift.clockModeHidden.addItemList(clockItems, 1);
    drift.clockBtn.setButtonText("SYNC");
    drift.clockBtn.setLookAndFeel(&driftClockLnf);
    drift.clockBtn.setClickingTogglesState(false);
    drift.clockBtn.onClick = [&drift] {
        const int cur = drift.clockModeHidden.getSelectedId();
        drift.clockModeHidden.setSelectedId(cur == 1 ? 2 : 1);
    };
    addAndMakeVisible(drift.clockBtn);

    drift.rateRow     = std::make_unique<SliderRow>("Rate", fmtPeriodSc, kDriftCol);
    drift.depthRow    = std::make_unique<SliderRow>("Amt", fmtPctFine, kDriftCol);
    drift.divisionRow = std::make_unique<SliderRow>("Rate",
        [](double v) {
            const int idx = juce::jlimit(0, DriftDivision::kCount - 1,
                                          juce::roundToInt(v));
            return juce::String(DriftDivision::kEntries[idx].label);
        }, kDriftCol);
    drift.divisionRow->getSlider().setRange(
        0.0, static_cast<double>(DriftDivision::kCount - 1), 1.0);
    // Lock the value column to a fixed pixel width so the slider track's
    // RIGHT edge stays put when ClockMode swaps. Free mode now shows the period
    // (e.g. "128 s/cyc" at the floor, "0.50 s/cyc" fast), wider than "Hz", hence 84.
    // BOTH rows share the width so the track edge is stable across the swap. The
    // label column is forced from SynthPanel::resized() to the
    // modulation-section-wide left column width — do NOT force it here.
    drift.rateRow->setForcedValueWidth(84);
    drift.divisionRow->setForcedValueWidth(84);
    addAndMakeVisible(*drift.rateRow);
    addAndMakeVisible(*drift.depthRow);
    addAndMakeVisible(*drift.divisionRow);

    drift.rateA      = std::make_unique<SA>(apvts, rateId,      drift.rateRow->getSlider());
    drift.depthA     = std::make_unique<SA>(apvts, depthId,     drift.depthRow->getSlider());
    drift.targetA    = std::make_unique<CA>(apvts, targetId,    drift.targetBox);
    drift.waveA      = std::make_unique<CA>(apvts, waveId,      drift.waveBox);
    drift.divisionA  = std::make_unique<SA>(apvts, divisionId,  drift.divisionRow->getSlider());
    drift.clockModeA = std::make_unique<CA>(apvts, clockModeId, drift.clockModeHidden);

    auto wireMidiRow = [this](SliderRow* row, const juce::String& pid) {
        row->onRightClick = [this, pid](juce::Point<int> pos) {
            showMidiLearnMenu(processorRef, pid, pos); };
    };
    wireMidiRow(drift.rateRow.get(),     rateId);
    wireMidiRow(drift.depthRow.get(),    depthId);
    wireMidiRow(drift.divisionRow.get(), divisionId);

    drift.waveBox.onChange = [&drift] {
        const int selected = drift.waveBox.getSelectedId();
        for (int i = 0; i < kNumWaveBtns; ++i)
        {
            auto& btn = drift.waveBtns[static_cast<size_t>(i)];
            const bool active = selected == i + 1;
            btn.setToggleState(active, juce::dontSendNotification);
            btn.setColour(juce::TextButton::buttonColourId, active ? kDriftCol : kSurface);
            btn.setColour(juce::TextButton::textColourOffId, active ? juce::Colours::white : kDim);
        }
    };

    drift.clockModeHidden.onChange = [&drift] {
        const bool sync = drift.clockModeHidden.getSelectedId() == 2;
        drift.clockBtn.setToggleState(sync, juce::dontSendNotification);
        drift.clockBtn.repaint();
        if (drift.rateRow)     drift.rateRow->setVisible(!sync);
        if (drift.divisionRow) drift.divisionRow->setVisible(sync);
    };
    drift.clockModeHidden.onChange();
    drift.waveBox.onChange();

    drift.rateRow->updateValue();
    drift.depthRow->updateValue();
    drift.divisionRow->updateValue();
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
        styleSwitchButton(btn, kAccent);
        btn.setClickingTogglesState(true);
        btn.setRadioGroupId(1001);
        btn.setToggleState(on, juce::dontSendNotification);
    };
    styleBtn(samplerBtn, true);
    styleBtn(wavetableBtn, false);
    styleBtn(freezeBtn, false);
    samplerBtn.setConnectedEdges(juce::Button::ConnectedOnRight);
    wavetableBtn.setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    freezeBtn.setConnectedEdges(juce::Button::ConnectedOnLeft);
    addAndMakeVisible(samplerBtn);
    addAndMakeVisible(wavetableBtn);
    addAndMakeVisible(freezeBtn);

    juce::StringArray engineModeItems;
    for (const auto& e : EngineMode::kEntries) engineModeItems.add(e.label);
    engineModeHidden.addItemList(engineModeItems, 1);
    engineModeHidden.onChange = [this] {
        const int id = engineModeHidden.getSelectedId();
        bool isSampler = id == 1;
        bool isWavetable = id == 2;
        bool isFreeze = id == 3;
        samplerBtn.setToggleState(isSampler, juce::dontSendNotification);
        wavetableBtn.setToggleState(isWavetable, juce::dontSendNotification);
        freezeBtn.setToggleState(isFreeze, juce::dontSendNotification);
        updateVisibility();
        resized();
    };
    samplerBtn.onClick = [this] { engineModeHidden.setSelectedId(1); };
    wavetableBtn.onClick = [this] { engineModeHidden.setSelectedId(2); };
    freezeBtn.onClick = [this] { engineModeHidden.setSelectedId(3); };
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
            styleSwitchButton(voiceBtns[i], kAccent);
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
        tuningBox.setColour(juce::ComboBox::backgroundColourId, kSurface.brighter(0.04f));
        tuningBox.setColour(juce::ComboBox::textColourId, juce::Colour(0xffd7dde9));
        tuningBox.setColour(juce::ComboBox::outlineColourId, kBorder);
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
        const bool freezeMode = processorRef.isFreezeMode();
        {
            const juce::ScopedLock sl (processorRef.getCallbackLock());
            processorRef.getSampler().setStartPos(pos);
            processorRef.getSampler().setPointsLocked(true);
            waveformDisplay.getLockButton().setLocked(true);
            if (!freezeMode && processorRef.isWavetableMode())
                pendingWtReextract_ = true;
        }
        if (freezeMode)
            scanRow->getSlider().setValue(static_cast<double>(pos), juce::sendNotificationSync);
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
        styleSwitchButton(btn, kAccent);
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
    crossfadeRow->onRightClick = [this](juce::Point<int> pos) {
        showMidiLearnMenu(processorRef, PID::crossfadeMs, pos); };

    // Normalize toggle
    normalizeToggle.setConnectedEdges(juce::Button::ConnectedOnLeft);
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
    labelAsCaption(scanHint, kDimmer);
    addAndMakeVisible(scanHint);
    scanA = std::make_unique<SA>(apvts, PID::oscScan, scanRow->getSlider());
    scanRow->updateValue();
    scanRow->onRightClick = [this](juce::Point<int> pos) {
        showMidiLearnMenu(processorRef, PID::oscScan, pos); };

    // ── Octave shift switchbox: -2 | -1 | 0 | +1 | +2 ──
    {
        juce::StringArray octLabels;
        for (const auto& e : OscOctave::kEntries) octLabels.add(e.label);
        octaveHidden.addItemList(octLabels, 1);
        octaveHidden.onChange = [this] {
            int id = octaveHidden.getSelectedId();
            for (int i = 0; i < kNumOctBtns; ++i)
                octBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
        };
        for (int i = 0; i < kNumOctBtns; ++i)
        {
            octBtns[i].setButtonText(octLabels[i]);
            styleSwitchButton(octBtns[i], kAccent);
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
            for (int i = 0; i < kNumNoiseBtns; ++i)
                noiseBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
        };
        for (int i = 0; i < kNumNoiseBtns; ++i)
        {
            noiseBtns[i].setButtonText(noiseLabels[i]);
            styleSwitchButton(noiseBtns[i], kAccent);
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
    noiseLevelRow->onRightClick = [this](juce::Point<int> pos) {
        showMidiLearnMenu(processorRef, PID::noiseLevel, pos); };

    // ── Wavetable controls: frame count switchbox ──
    // Frame count switchbox: 32 | 64 | 128 | 256
    {
        juce::StringArray frameLabels;
        for (const auto& e : WtFrames::kEntries) frameLabels.add(e.label);
        framesHidden.addItemList(frameLabels, 1);
        framesHidden.onChange = [this] {
            int id = framesHidden.getSelectedId();
            for (int i = 0; i < kNumFrameBtns; ++i)
                frameBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
            processorRef.reextractWavetable();
        };
        for (int i = 0; i < kNumFrameBtns; ++i)
        {
            frameBtns[i].setButtonText(frameLabels[i]);
            styleSwitchButton(frameBtns[i], kAccent);
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

    labelAsCaption(frameCountLabel, kDimmer);
    frameCountLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(frameCountLabel);

    // ── Granular controls: curated texture macro + independent stereo width ──
    {
        juce::StringArray textureLabels;
        for (const auto& e : FreezeTexture::kEntries)
            textureLabels.add(e.label);
        freezeTextureHidden.addItemList(textureLabels, 1);
        freezeTextureHidden.onChange = [this] {
            const int id = freezeTextureHidden.getSelectedId();
            for (int i = 0; i < kNumFreezeTextureBtns; ++i)
                freezeTextureBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
        };
        for (int i = 0; i < kNumFreezeTextureBtns; ++i)
        {
            freezeTextureBtns[i].setButtonText(textureLabels[i]);
            styleSwitchButton(freezeTextureBtns[i], kAccent);
            freezeTextureBtns[i].setClickingTogglesState(false);
            freezeTextureBtns[i].onClick = [this, i] { freezeTextureHidden.setSelectedId(i + 1); };
            addAndMakeVisible(freezeTextureBtns[i]);
        }
        freezeTextureA = std::make_unique<CA>(apvts, PID::freezeTexture, freezeTextureHidden);
        freezeTextureHidden.onChange();
    }

    freezeStereoRow = std::make_unique<SliderRow>("Stereo", fmtPct);
    addAndMakeVisible(*freezeStereoRow);
    freezeStereoA = std::make_unique<SA>(apvts, PID::freezeStereo, freezeStereoRow->getSlider());
    freezeStereoRow->onRightClick = [this](juce::Point<int> pos) {
        showMidiLearnMenu(processorRef, PID::freezeStereo, pos); };
    freezeStereoRow->updateValue();

    // ── Section headers — inverted (colored bg, light text) ──
    auto makeHeader = [this](juce::Label& lbl, const juce::String& text, juce::Colour col) {
        lbl.setText(" " + text, juce::dontSendNotification);
        labelAsHeaderBand(lbl, col);
        lbl.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(lbl);
    };
    makeHeader(engineHeader, "ENGINE", kAccent);
    makeHeader(filterHeader, "FILTER", kFilterCol);
    makeHeader(modHeader, "ENVELOPES", kModCol);

    // The Easy/Advanced modulation view was removed — the panel is always the
    // unified view. The mode toggle is no longer created or shown.

    auto setupModTabs = [this](std::array<juce::TextButton, kNumModTabs>& tabs,
                               const char* const* labels,
                               int radioGroup)
    {
        for (int i = 0; i < kNumModTabs; ++i)
        {
            auto& btn = tabs[static_cast<size_t>(i)];
            btn.setButtonText(labels[i]);
            styleSwitchButton(btn, kModCol);
            btn.setClickingTogglesState(true);
            btn.setRadioGroupId(radioGroup);
            btn.onClick = [this, i, radioGroup]
            {
                if (radioGroup == 4001)      activeEnvTab = i;
                else if (radioGroup == 4002) activeLfoTab = i;
                else                         activeDriftTab = i;
                syncModTabButtons();
                updateVisibility();
                resized();
                repaint();
            };
            addAndMakeVisible(btn);
        }
    };
    const char* const envTabLabels[] = { "ENV1", "ENV2", "ENV3" };
    const char* const lfoTabLabels[] = { "LFO 1", "LFO 2", "LFO 3" };
    const char* const driftTabLabels[] = { "Drift 1", "Drift 2", "Drift 3" };
    setupModTabs(envTabBtns, envTabLabels, 4001);
    setupModTabs(lfoTabBtns, lfoTabLabels, 4002);
    setupModTabs(driftTabBtns, driftTabLabels, 4003);

    // ── Filter type: OFF LP HP BP (APVTS carried by filterTypeHidden; the
    // visible affordance is filterEasyOffBtn + filterEasyTypeBtn below) ──
    {
        const juce::StringArray typeLabels { "OFF", "LP", "HP", "BP" };
        filterTypeHidden.addItemList(typeLabels, 1);
        filterTypeHidden.onChange = [this] {
            int id = filterTypeHidden.getSelectedId();
            // TYPE toggle: remember the last active type so bypass→re-enable
            // restores it, and label the toggle with the current (or remembered) type.
            if (id >= FilterType::Lowpass + 1)
                lastEasyFilterType_ = id;
            const char* const typeShort[] = { "LP", "HP", "BP" };  // ids 2,3,4
            const int shown = (id >= FilterType::Lowpass + 1) ? id : lastEasyFilterType_;
            filterEasyTypeBtn.setButtonText(typeShort[shown - 2]);
            updateVisibility();
            // updateVisibility() hides lfoHeader/driftHeader; in the columnar mod
            // view they double as the LFO/DRIFT column header bars and are
            // re-shown only by layoutModEasy. Re-run layout so they don't vanish
            // when the filter is toggled. (Mirrors every other onChange in this file.)
            resized();
        };
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
            styleSwitchButton(filterSlopeBtns[i], kFilterCol);
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

    // ── Filter algorithm switchbox: SVF Ladder Warp ──
    {
        juce::StringArray algLabels;
        for (const auto& e : FilterAlgorithm::kEntries) algLabels.add(e.label);
        filterAlgHidden.addItemList(algLabels, 1);
        filterAlgHidden.onChange = [this] {
            int id = filterAlgHidden.getSelectedId();
            for (int i = 0; i < kNumAlgBtns; ++i)
                filterAlgBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
            updateVisibility();
            // See filterTypeHidden.onChange: re-run layout so the columnar
            // LFO/DRIFT header bars (re-shown only in layoutModEasy) survive a
            // filter-algorithm change in easy mode.
            resized();
        };
        for (int i = 0; i < kNumAlgBtns; ++i)
        {
            filterAlgBtns[i].setButtonText(algLabels[i]);
            styleSwitchButton(filterAlgBtns[i], kFilterCol);
            filterAlgBtns[i].setClickingTogglesState(true);
            filterAlgBtns[i].setRadioGroupId(3005);
            { int edges = 0;
              if (i > 0) edges |= juce::Button::ConnectedOnLeft;
              if (i < kNumAlgBtns - 1) edges |= juce::Button::ConnectedOnRight;
              filterAlgBtns[i].setConnectedEdges(edges); }
            filterAlgBtns[i].onClick = [this, i] {
                if (i == FilterAlgorithm::Warp)
                    filterWarpStyleBox.setSelectedId(FilterWarpStyle::SoftClip + 1);
                // OFF + algorithm buttons form one 4-way switch: picking an
                // algorithm while bypassed re-enables the filter.
                if (filterTypeHidden.getSelectedId() == FilterType::Off + 1)
                    filterTypeHidden.setSelectedId(FilterType::Lowpass + 1);
                filterAlgHidden.setSelectedId(i + 1);
            };
            addAndMakeVisible(filterAlgBtns[i]);
        }
    }

    // ── Easy-mode filter OFF segment (sits left of the algorithm switchbox) ──
    {
        filterEasyOffBtn.setColour(juce::TextButton::buttonColourId,   kSurface);
        filterEasyOffBtn.setColour(juce::TextButton::buttonOnColourId, kFilterCol);
        filterEasyOffBtn.setColour(juce::TextButton::textColourOffId,  kDim);
        filterEasyOffBtn.setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
        filterEasyOffBtn.setConnectedEdges(juce::Button::ConnectedOnRight);
        // Toggle state is driven from updateVisibility (reflects filter bypass),
        // not from clicks — so re-clicking OFF while already off can't desync it.
        filterEasyOffBtn.onClick = [this] {
            filterTypeHidden.setSelectedId(FilterType::Off + 1);
        };
        addAndMakeVisible(filterEasyOffBtn);
    }

    // ── Easy-mode filter TYPE toggle (cycles LP→HP→BP, right of the slope row) ──
    {
        styleSwitchButton(filterEasyTypeBtn, kFilterCol);
        // Cycles via onClick — its lit/unlit appearance is driven from
        // updateVisibility (reflects filter on/off), not from click toggling.
        filterEasyTypeBtn.setClickingTogglesState(false);
        filterEasyTypeBtn.setConnectedEdges(0);  // standalone toggle, distinct from the slope group
        filterEasyTypeBtn.onClick = [this] {
            const int cur = filterTypeHidden.getSelectedId();   // 1=Off,2=LP,3=HP,4=BP
            int next;
            if (cur <= FilterType::Off + 1)                     // bypassed → re-enable last type
                next = lastEasyFilterType_;
            else if (cur >= FilterType::Bandpass + 1)           // BP → wrap to LP
                next = FilterType::Lowpass + 1;
            else
                next = cur + 1;                                 // LP→HP, HP→BP
            filterTypeHidden.setSelectedId(next);
        };
        addAndMakeVisible(filterEasyTypeBtn);
    }

    // ── Warp style combo: tanh / softclip / ojd / sin / digital / asym ──
    {
        juce::StringArray styleLabels;
        for (const auto& e : FilterWarpStyle::kEntries) styleLabels.add(e.label);
        filterWarpStyleBox.addItemList(styleLabels, 1);
        filterWarpStyleBox.setColour(juce::ComboBox::backgroundColourId, kSurface);
        filterWarpStyleBox.setColour(juce::ComboBox::textColourId, juce::Colours::white);
        filterWarpStyleBox.setColour(juce::ComboBox::outlineColourId, kFilterCol);
        filterWarpStyleBox.onChange = [this] {
            const int id = filterWarpStyleBox.getSelectedId();
            if (id >= 1 && id <= FilterWarpStyle::kCount)
                filterEasyWarpBtn.setButtonText(
                    juce::String(FilterWarpStyle::kEntries[id - 1].label));
        };
        addAndMakeVisible(filterWarpStyleBox);
    }

    // ── Easy-mode Warp hold button ──
    {
        styleSwitchButton(filterEasyWarpBtn, kFilterCol);
        filterEasyWarpBtn.setClickingTogglesState(false);
        filterEasyWarpBtn.setConnectedEdges(juce::Button::ConnectedOnLeft);
        addAndMakeVisible(filterEasyWarpBtn);

        filterEasyWarpBtn.onTap = [this] {
            if (filterTypeHidden.getSelectedId() == FilterType::Off + 1)
                filterTypeHidden.setSelectedId(FilterType::Lowpass + 1);
            filterAlgHidden.setSelectedId(FilterAlgorithm::Warp + 1);
        };
        filterEasyWarpBtn.onStylePick = [this](int id) {
            filterWarpStyleBox.setSelectedId(id);
            if (filterTypeHidden.getSelectedId() == FilterType::Off + 1)
                filterTypeHidden.setSelectedId(FilterType::Lowpass + 1);
            filterAlgHidden.setSelectedId(FilterAlgorithm::Warp + 1);
        };
    }

    // ── Filter drive oversampling: Off 2x 4x 8x ──
    // filterDriveOsHidden is a hidden carrier only — no visible switchbox in
    // the unified view; drive oversampling is controlled elsewhere.
    {
        juce::StringArray osLabels;
        for (const auto& e : FilterDriveOs::kEntries) osLabels.add(e.label);
        filterDriveOsHidden.addItemList(osLabels, 1);
    }

    cutoffRow      = std::make_unique<SliderRow>("Cutoff",    fmtHz,  kFilterCol);
    resoRow        = std::make_unique<SliderRow>("Resonance", fmtF2,  kFilterCol);
    filterMixRow   = std::make_unique<SliderRow>("Mix",       fmtPct, kFilterCol);
    kbdTrackRow    = std::make_unique<SliderRow>("Kbd Track", fmtPct, kFilterCol);
    filterDriveRow = std::make_unique<SliderRow>("Drive",     fmtDb,  kFilterCol);
    for (auto* r : { cutoffRow.get(), resoRow.get(), filterMixRow.get(),
                     kbdTrackRow.get(), filterDriveRow.get() })
        addAndMakeVisible(*r);

    cutoffA        = std::make_unique<SA>(apvts, PID::filterCutoff,    cutoffRow->getSlider());
    resoA          = std::make_unique<SA>(apvts, PID::filterResonance, resoRow->getSlider());
    filterMixA     = std::make_unique<SA>(apvts, PID::filterMix,       filterMixRow->getSlider());
    kbdTrackA      = std::make_unique<SA>(apvts, PID::filterKbdTrack,  kbdTrackRow->getSlider());
    filterDriveA   = std::make_unique<SA>(apvts, PID::filterDrive,     filterDriveRow->getSlider());

    cutoffRow->onRightClick     = [this](juce::Point<int> p) { showMidiLearnMenu(processorRef, PID::filterCutoff,    p); };
    resoRow->onRightClick       = [this](juce::Point<int> p) { showMidiLearnMenu(processorRef, PID::filterResonance, p); };
    filterMixRow->onRightClick  = [this](juce::Point<int> p) { showMidiLearnMenu(processorRef, PID::filterMix,       p); };
    kbdTrackRow->onRightClick   = [this](juce::Point<int> p) { showMidiLearnMenu(processorRef, PID::filterKbdTrack,  p); };
    filterDriveRow->onRightClick = [this](juce::Point<int> p) { showMidiLearnMenu(processorRef, PID::filterDrive,    p); };

    filterTypeA      = std::make_unique<CA>(apvts, PID::filterType,      filterTypeHidden);
    filterSlopeA     = std::make_unique<CA>(apvts, PID::filterSlope,     filterSlopeHidden);
    filterDriveOsA   = std::make_unique<CA>(apvts, PID::filterDriveOs,   filterDriveOsHidden);
    filterAlgA       = std::make_unique<CA>(apvts, PID::filterAlgorithm, filterAlgHidden);
    filterWarpStyleA = std::make_unique<CA>(apvts, PID::filterWarpStyle, filterWarpStyleBox);
    // Sync WarpHoldBtn label from APVTS directly — ComboBoxAttachment notification
    // timing on first paint is unreliable (same reason syncRadioRow exists below).
    {
        const int raw = static_cast<int>(apvts.getRawParameterValue(PID::filterWarpStyle)->load());
        const int idx = juce::jlimit(0, FilterWarpStyle::kCount - 1, raw);
        filterEasyWarpBtn.setButtonText(juce::String(FilterWarpStyle::kEntries[idx].label));
    }

    // Initial radio-button visual sync. Reading the APVTS choice index
    // directly is robust against ComboBoxAttachment notification-timing
    // quirks across JUCE versions — the attachment drives the hidden combo,
    // but whether its onChange lambda fires on the very first paint has
    // historically been unreliable, leaving the filter card with no active
    // SLOPE / ALG button until the user clicks one. Direct APVTS read
    // sidesteps that entirely.
    auto syncRadioRow = [&apvts](const juce::String& pid,
                                  juce::TextButton* btns, int n) {
        const int raw = static_cast<int>(apvts.getRawParameterValue(pid)->load());
        const int active = juce::jlimit(0, n - 1, raw);
        for (int i = 0; i < n; ++i)
            btns[i].setToggleState(i == active, juce::dontSendNotification);
    };
    syncRadioRow(PID::filterSlope,     filterSlopeBtns,   kNumSlopeBtns);
    syncRadioRow(PID::filterAlgorithm, filterAlgBtns,     kNumAlgBtns);
    syncRadioRow(PID::freezeTexture,   freezeTextureBtns, kNumFreezeTextureBtns);

    cutoffRow->updateValue();
    resoRow->updateValue();
    filterMixRow->updateValue();
    kbdTrackRow->updateValue();
    filterDriveRow->updateValue();

    // ── Envelopes ──
    initEnv(ampEnv,  "ENV 1", 2, PID::ampAttack,  PID::ampDecay,  PID::ampSustain,  PID::ampRelease,
            PID::ampAttackCurve, PID::ampDecayCurve, PID::ampReleaseCurve,
            PID::ampAttackVelSens, PID::ampDecayVelSens, PID::ampReleaseVelSens,
            PID::ampAmount,  PID::ampLoop,  apvts);
    initEnv(mod1Env, "ENV 2", 1, PID::mod1Attack, PID::mod1Decay, PID::mod1Sustain, PID::mod1Release,
            PID::mod1AttackCurve, PID::mod1DecayCurve, PID::mod1ReleaseCurve,
            PID::mod1AttackVelSens, PID::mod1DecayVelSens, PID::mod1ReleaseVelSens,
            PID::mod1Amount, PID::mod1Loop, apvts);
    initEnv(mod2Env, "ENV 3", 1, PID::mod2Attack, PID::mod2Decay, PID::mod2Sustain, PID::mod2Release,
            PID::mod2AttackCurve, PID::mod2DecayCurve, PID::mod2ReleaseCurve,
            PID::mod2AttackVelSens, PID::mod2DecayVelSens, PID::mod2ReleaseVelSens,
            PID::mod2Amount, PID::mod2Loop, apvts);

    // ── LFOs ──
    initLfo(lfo1, "LFO 1",
            PID::lfo1Rate, PID::lfo1Depth, PID::lfo1Wave, PID::lfo1Mode,
            PID::lfo1ClockMode, PID::lfo1ClockDivision, apvts);
    initLfo(lfo2, "LFO 2",
            PID::lfo2Rate, PID::lfo2Depth, PID::lfo2Wave, PID::lfo2Mode,
            PID::lfo2ClockMode, PID::lfo2ClockDivision, apvts);
    initLfo(lfo3, "LFO 3",
            PID::lfo3Rate, PID::lfo3Depth, PID::lfo3Wave, PID::lfo3Mode,
            PID::lfo3ClockMode, PID::lfo3ClockDivision, apvts);

    // ── Easy-panel AT module: 12 bipolar drag-fill bars (one per target) ──
    {
        struct AtBar { const char* pid; const char* label; };
        // Order follows the canonical EnvTarget order (BlockParams.h): voice
        // destinations first (DCA, Filter=Cutoff+Reso, Scan, Pitch, Noise), then
        // the mod-source levels (LFO depths, then env sustains). "Amt" matches the
        // LFO module's own depth label in the easy panel.
        static const AtBar atBars[12] = {
            { PID::aftertouchAmtDca,         "DCA"      },
            { PID::aftertouchAmtCutoff,      "Cutoff"   },
            { PID::aftertouchAmtResonance,   "Reso"     },
            { PID::aftertouchAmtScan,        "Scan"     },
            { PID::aftertouchAmtPitch,       "Pitch"    },
            { PID::aftertouchAmtNoiseLevel,  "Noise"    },
            { PID::aftertouchAmtLfo1Depth,   "LFO1 Amt" },
            { PID::aftertouchAmtLfo2Depth,   "LFO2 Amt" },
            { PID::aftertouchAmtLfo3Depth,   "LFO3 Amt" },
            { PID::aftertouchAmtEnv1Sustain, "ENV1 Sus" },
            { PID::aftertouchAmtEnv2Sustain, "ENV2 Sus" },
            { PID::aftertouchAmtEnv3Sustain, "ENV3 Sus" },
        };
        for (int i = 0; i < 12; ++i)
        {
            const char* pid = atBars[i].pid;
            auto bar = std::make_unique<AftertouchBar>();
            bar->setTargetLabel(atBars[i].label);
            if (auto* p = apvts.getParameter(pid))
            {
                bar->onDragStart = [p] { p->beginChangeGesture(); };
                bar->onDragEnd   = [p] { p->endChangeGesture(); };
            }
            bar->onRightClick = [this, pid](juce::Point<int> pt) {
                showMidiLearnMenu(processorRef, pid, pt); };
            addAndMakeVisible(*bar);
            aftertouchBarA[i] = std::make_unique<SA>(apvts, pid, *bar);
            aftertouchBars[i] = std::move(bar);
        }
        addChildComponent(aftertouchHeader);   // shown by the columns easy-layout
    }

    // ── Drift ──
    initDrift(drift1, "D1",
              PID::drift1Rate, PID::drift1Depth, PID::drift1Target, PID::drift1Wave,
              PID::drift1ClockMode, PID::drift1ClockDivision, apvts);
    initDrift(drift2, "D2",
              PID::drift2Rate, PID::drift2Depth, PID::drift2Target, PID::drift2Wave,
              PID::drift2ClockMode, PID::drift2ClockDivision, apvts);
    initDrift(drift3, "D3",
              PID::drift3Rate, PID::drift3Depth, PID::drift3Target, PID::drift3Wave,
              PID::drift3ClockMode, PID::drift3ClockDivision, apvts);

    paintSectionHeader(lfoHeader, "LFO", kLfoCol);
    addAndMakeVisible(lfoHeader);

    paintSectionHeader(driftHeader, "DRIFT", kDriftCol);
    addAndMakeVisible(driftHeader);

    // Regenerate mode switchbox
    paintSectionHeader(regenHeader, "REGENERATE", kRegenCol);
    addAndMakeVisible(regenHeader);

    juce::StringArray regenItems;
    for (const auto& e : DriftRegen::kEntries) regenItems.add(juce::String(juce::CharPointer_UTF8(e.label)));
    regenHidden.addItemList(regenItems, 1);
    regenHidden.onChange = [this] {
        int id = regenHidden.getSelectedId();
        for (int i = 0; i < kNumRegenBtns; ++i)
            regenBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
    };
    addAndMakeVisible(regenHidden);
    for (int i = 0; i < kNumRegenBtns; ++i)
    {
        regenBtns[i].setButtonText(regenItems[i]);
        styleSwitchButton(regenBtns[i], kDriftCol);
        regenBtns[i].setClickingTogglesState(true);
        regenBtns[i].setRadioGroupId(3006);
        regenBtns[i].onClick = [this, i] { regenHidden.setSelectedId(i + 1); };
        addAndMakeVisible(regenBtns[i]);
    }
    driftRegenA = std::make_unique<CA>(apvts, PID::driftRegen, regenHidden);

    // Crossfade slider for drift regeneration
    crossfadeRegenRow = std::make_unique<SliderRow>("XFade", fmtMs, kDriftCol);
    addAndMakeVisible(*crossfadeRegenRow);
    crossfadeRegenA = std::make_unique<SA>(apvts, PID::driftCrossfade, crossfadeRegenRow->getSlider());
    crossfadeRegenRow->onRightClick = [this](juce::Point<int> p) {
        showMidiLearnMenu(processorRef, PID::driftCrossfade, p); };
    crossfadeRegenRow->updateValue();

    // All components are now set up — enable callbacks and trigger initial state
    initialized = true;

    // Deferred APVTS attachments for target ComboBoxes
    // (must come after initialized=true so onChange → updateVisibility works)
    ampTargetA = std::make_unique<CA>(apvts, PID::ampTarget, ampEnv.targetBox);
    mod1TargetA = std::make_unique<CA>(apvts, PID::mod1Target, mod1Env.targetBox);
    mod2TargetA = std::make_unique<CA>(apvts, PID::mod2Target, mod2Env.targetBox);
    lfo1TargetA = std::make_unique<CA>(apvts, PID::lfo1Target, lfo1.targetBox);
    lfo2TargetA = std::make_unique<CA>(apvts, PID::lfo2Target, lfo2.targetBox);
    lfo3TargetA = std::make_unique<CA>(apvts, PID::lfo3Target, lfo3.targetBox);

    // Easy/Advanced view removed: the panel is permanently the unified view.
    selectFirstActiveModTabs();
    syncModTabButtons();
    updateVisibility();
    resized();
    repaint();
    updateVisibility();
    startTimerHz(30);
}

// ──────────────────────────────────────────────────────────────────────────────
// Waveform display polling
// ──────────────────────────────────────────────────────────────────────────────
void SynthPanel::followModParamToTab(const juce::String& paramId)
{
    if (paramId.isEmpty()) return;

    // env A/D/S/R → ENV tab (amp = ENV1, mod1 = ENV2, mod2 = ENV3).
    // amp_amount (the volume fader) deliberately excluded — it is not an envelope edit.
    auto isEnvAdsr = [&paramId](const char* prefix)
    {
        return paramId.startsWith(prefix)
            && (paramId.endsWith("_attack") || paramId.endsWith("_decay")
             || paramId.endsWith("_sustain") || paramId.endsWith("_release"));
    };

    int group = -1;  // 0 = env, 1 = lfo, 2 = drift
    int index = -1;  // tab within the group (0..2)
    if      (isEnvAdsr("amp_"))             { group = 0; index = 0; }
    else if (isEnvAdsr("mod1_"))            { group = 0; index = 1; }
    else if (isEnvAdsr("mod2_"))            { group = 0; index = 2; }
    else if (paramId.startsWith("lfo1_"))   { group = 1; index = 0; }
    else if (paramId.startsWith("lfo2_"))   { group = 1; index = 1; }
    else if (paramId.startsWith("lfo3_"))   { group = 1; index = 2; }
    else if (paramId.startsWith("drift1_")) { group = 2; index = 0; }
    else if (paramId.startsWith("drift2_")) { group = 2; index = 1; }
    else if (paramId.startsWith("drift3_")) { group = 2; index = 2; }
    if (group < 0) return;

    int& active = (group == 0) ? activeEnvTab : (group == 1) ? activeLfoTab : activeDriftTab;
    if (active == index) return;  // already showing → avoid needless relayout/repaint

    active = index;
    syncModTabButtons();
    updateVisibility();
    resized();
    repaint();
}

void SynthPanel::timerCallback()
{
    // Tab follows controller: when an incoming mapped CC changes a mod param,
    // switch the visible ENV/LFO/Drift tab to the affected one.
    {
        const uint64_t packed = processorRef.getMidiTouchPacked();
        const uint32_t seq = static_cast<uint32_t>(packed >> 32);
        if (seq != lastSeenMidiTouchSeq_)
        {
            lastSeenMidiTouchSeq_ = seq;
            followModParamToTab(
                processorRef.getCcMappingCopy(static_cast<int>(packed & 0xffffffffu)).paramId);
        }
    }

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

        // A fresh neural sample means the table is no longer a DCO/LCO bake:
        // setWaveform already dropped wtMode; reconcile the region label too (a
        // neural gen on the Wavetable engine may not re-run updateVisibility).
        reconcileWaveformDisplayMode();

        // Update frame count display
        int nf = processorRef.getMasterOsc().getNumFrames();
        frameCountLabel.setText(juce::String(nf) + "f", juce::dontSendNotification);
    }

    // Baked wavetable (LCO/DCO): draw the actual table in the engine window as a
    // 2.5D fan with a scan cursor, in place of the last neural sample. Distinct
    // from hasNewWaveform — a bake publishes frames + this display strip but NO
    // sample snapshot, so the extraction-region sample view never fires.
    if (processorRef.hasNewWtDisplay())
    {
        waveformDisplay.setWavetableFrames(processorRef.getWtDisplaySnapshot(),
                                           WavetableOscillator::FRAME_SIZE);
        processorRef.clearNewWtDisplayFlag();
        reconcileWaveformDisplayMode();   // ensure the "Wavetable" label + fan mode
        int nf = processorRef.getMasterOsc().getNumFrames();
        frameCountLabel.setText(juce::String(nf) + "f", juce::dontSendNotification);
    }

    // Update ghost targets from modulated values (skip when audio is idle)
    if (!processorRef.audioIdle.load(std::memory_order_relaxed))
    {
        auto& mv = processorRef.modulatedValues;
        cutoffRow->setGhostValue(mv.filterCutoff.load(std::memory_order_relaxed));
        resoRow->setGhostValue(mv.filterResonance.load(std::memory_order_relaxed));
        noiseLevelRow->setGhostValue(mv.noiseLevel.load(std::memory_order_relaxed));
        if (lfo1.rateRow)  lfo1.rateRow->setGhostValue(mv.lfo1Rate.load(std::memory_order_relaxed));
        if (lfo1.depthRow) lfo1.depthRow->setGhostValue(mv.lfo1Depth.load(std::memory_order_relaxed));
        if (lfo2.rateRow)  lfo2.rateRow->setGhostValue(mv.lfo2Rate.load(std::memory_order_relaxed));
        if (lfo2.depthRow) lfo2.depthRow->setGhostValue(mv.lfo2Depth.load(std::memory_order_relaxed));
        if (lfo3.rateRow)  lfo3.rateRow->setGhostValue(mv.lfo3Rate.load(std::memory_order_relaxed));
        if (lfo3.depthRow) lfo3.depthRow->setGhostValue(mv.lfo3Depth.load(std::memory_order_relaxed));
        if (drift1.depthRow) drift1.depthRow->setGhostValue(mv.drift1Depth.load(std::memory_order_relaxed));
        if (drift2.depthRow) drift2.depthRow->setGhostValue(mv.drift2Depth.load(std::memory_order_relaxed));
        if (drift3.depthRow) drift3.depthRow->setGhostValue(mv.drift3Depth.load(std::memory_order_relaxed));
        if (waveformDisplay.isVisible())
            waveformDisplay.setScanPosition(mv.scanPosition.load(std::memory_order_relaxed));
    }

    // Advance ghost smoothing (runs every frame at 30 Hz)
    cutoffRow->tickGhost();
    resoRow->tickGhost();
    noiseLevelRow->tickGhost();
    if (lfo1.rateRow)  lfo1.rateRow->tickGhost();
    if (lfo1.depthRow) lfo1.depthRow->tickGhost();
    if (lfo2.rateRow)  lfo2.rateRow->tickGhost();
    if (lfo2.depthRow) lfo2.depthRow->tickGhost();
    if (lfo3.rateRow)  lfo3.rateRow->tickGhost();
    if (lfo3.depthRow) lfo3.depthRow->tickGhost();
    if (drift1.depthRow) drift1.depthRow->tickGhost();
    if (drift2.depthRow) drift2.depthRow->tickGhost();
    if (drift3.depthRow) drift3.depthRow->tickGhost();
    waveformDisplay.tickScan();
}

// Reconcile the engine-window waveform display with the engine state: show the
// 2.5D WT fan while a DCO/LCO table is active, else the interactive sample view,
// and set the region label to match. Idempotent and cheap (no per-tick cost of
// note) — safe to call from updateVisibility AND the timer's new-data blocks so
// an engine switch, a bake, or a neural gen each land on the right mode + label.
void SynthPanel::reconcileWaveformDisplayMode()
{
    const bool showWtTable = processorRef.isWavetableMode() && processorRef.isDcoTableActive();
    if (! showWtTable && waveformDisplay.isWavetableMode())
        waveformDisplay.exitWavetableMode();   // left the DCO table → restore sample view + brackets
    waveformDisplay.setRegionLabel(showWtTable        ? "Wavetable"
                                 : processorRef.isWavetableMode() ? "Extraction region"
                                 : processorRef.isFreezeMode()    ? "Granular position"
                                                                  : "Loop interval");
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
    for (int i = 0; i < kNumAlgBtns; ++i)
    {
        // The algorithm buttons join the OFF segment as one 4-way switch:
        // always live (so any segment can re-enable the filter), and
        // highlighted only while the filter is on and this algorithm is picked.
        filterAlgBtns[i].setAlpha(1.0f);
        filterAlgBtns[i].setEnabled(true);
        filterAlgBtns[i].setToggleState(filterOn && filterAlgHidden.getSelectedId() == i + 1,
                                        juce::dontSendNotification);
    }
    // TYPE toggle: stays live like the algorithm buttons (so it can re-enable
    // the filter from bypass), lit only while the filter is on. Label set in onChange.
    filterEasyTypeBtn.setAlpha(1.0f);
    filterEasyTypeBtn.setEnabled(true);
    filterEasyTypeBtn.setToggleState(filterOn, juce::dontSendNotification);

    // Warp Style dims further (to 0.3× of the already-filter-dim) when the
    // selected algorithm isn't Warp — style only applies to the warp ladder.
    const bool warpActive = filterAlgHidden.getSelectedId() == (FilterAlgorithm::Warp + 1);
    const float styleAlpha = filterAlpha * (warpActive ? 1.0f : 0.35f);
    filterWarpStyleBox.setAlpha(styleAlpha);
    filterWarpStyleBox.setEnabled(filterOn && warpActive);
    filterHeader.setVisible(true);
    // The 18 dB slope segment is sacrificed so its cell can host the LP/HP/BP
    // type toggle.
    for (int i = 0; i < kNumSlopeBtns; ++i)
        filterSlopeBtns[i].setVisible(i != FilterSlope::Slope18);
    filterEasyTypeBtn.setVisible(true);
    for (int i = 0; i < kNumAlgBtns; ++i)
        filterAlgBtns[i].setVisible(i != FilterAlgorithm::Warp);
    filterEasyWarpBtn.setVisible(true);
    filterEasyWarpBtn.setAlpha(1.0f);
    filterEasyWarpBtn.setEnabled(true);
    filterEasyWarpBtn.setToggleState(filterOn && filterAlgHidden.getSelectedId() == FilterAlgorithm::Warp + 1,
                                     juce::dontSendNotification);
    filterWarpStyleBox.setVisible(false);

    // OFF segment: visible always, highlighted while bypassed.
    filterEasyOffBtn.setVisible(true);
    filterEasyOffBtn.setToggleState(!filterOn, juce::dontSendNotification);
    filterAlgBtns[FilterAlgorithm::SVF].setConnectedEdges(
        juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);

    const int engineId = engineModeHidden.getSelectedId();
    bool isWavetable = engineId == 2;
    bool isFreeze = engineId == 3;
    bool isSampler = !isWavetable && !isFreeze;

    // Shared playback traversal controls
    oneshotBtn.setVisible(!isFreeze);
    loopModeBtn.setVisible(!isFreeze);
    pingpongBtn.setVisible(!isFreeze);

    // Sampler-only controls
    crossfadeRow->setVisible(isSampler);
    loopOptimizeBtn.setVisible(isSampler);
    normalizeToggle.setVisible(isSampler || isFreeze);
    hfBoostBtn.setVisible(true);
    hfBoostBtn.setConnectedEdges(isWavetable ? 0 : juce::Button::ConnectedOnRight);
    normalizeToggle.setConnectedEdges(juce::Button::ConnectedOnLeft);
    for (int i = 0; i < kNumOctBtns; ++i)
        octBtns[i].setVisible(isSampler || isFreeze);

    // Scan controls drive Wavetable frame position or Granular hold position.
    scanRow->setVisible(isWavetable || isFreeze);
    scanHint.setVisible(false);
    for (int i = 0; i < kNumFrameBtns; ++i)
        frameBtns[i].setVisible(isWavetable);
    smoothToggle.setVisible(isWavetable);
    autoScanToggle.setVisible(isWavetable);
    frameCountLabel.setVisible(isWavetable);
    for (int i = 0; i < kNumFreezeTextureBtns; ++i)
        freezeTextureBtns[i].setVisible(isFreeze);
    freezeStereoRow->setVisible(isFreeze);

    reconcileWaveformDisplayMode();   // WT fan vs sample view + region label

    if (isSampler)
    {
        bool isOneshot = loopModeHidden.getSelectedId() == 1;
        crossfadeRow->setAlpha(isOneshot ? dimAlpha : 1.0f);
        crossfadeRow->setEnabled(!isOneshot);
    }

    // Env-selector tab greys out for any env that drives nothing (target =
    // None) — inactive envs read as inactive at a glance. Alpha is orthogonal
    // to syncGroup's colour/toggle setup, so it survives.
    {
        EnvSection* envs[kNumModTabs] = { &ampEnv, &mod1Env, &mod2Env };
        for (int i = 0; i < kNumModTabs; ++i)
            envTabBtns[static_cast<size_t>(i)].setAlpha(
                envs[i]->targetBox.getSelectedId() != 1 ? 1.0f : dimAlpha);
    }

    auto setLfoDimmed = [](LfoSection& lfo) {
        bool active = lfo.targetBox.getSelectedId() != 1; // 1 = "---"
        float alpha = active ? 1.0f : dimAlpha;
        lfo.header.setAlpha(1.0f);
        lfo.targetBox.setAlpha(alpha);
        lfo.waveBox.setAlpha(alpha);
        lfo.modeBtn.setAlpha(alpha);
        lfo.clockBtn.setAlpha(alpha);
        if (lfo.rateRow)     lfo.rateRow->setAlpha(alpha);
        if (lfo.depthRow)    lfo.depthRow->setAlpha(alpha);
        if (lfo.divisionRow) lfo.divisionRow->setAlpha(alpha);
    };
    setLfoDimmed(lfo1);
    setLfoDimmed(lfo2);
    setLfoDimmed(lfo3);

    auto setDriftDimmed = [](DriftSection& drift) {
        bool active = drift.targetBox.getSelectedId() != 1; // 1 = "---"
        float alpha = active ? 1.0f : dimAlpha;
        drift.header.setAlpha(1.0f);
        drift.targetBox.setAlpha(alpha);
        drift.waveBox.setAlpha(alpha);
        drift.clockBtn.setAlpha(alpha);
        if (drift.rateRow)     drift.rateRow->setAlpha(alpha);
        if (drift.depthRow)    drift.depthRow->setAlpha(alpha);
        if (drift.divisionRow) drift.divisionRow->setAlpha(alpha);
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

    for (auto& btn : envTabBtns)   btn.setVisible(true);
    for (auto& btn : lfoTabBtns)   btn.setVisible(false);
    for (auto& btn : driftTabBtns) btn.setVisible(false);

    auto setEnvControlsVisible = [](EnvSection& env, bool selected)
    {
        env.targetBox.setVisible(selected);
        env.loopBtn.setVisible(selected);
        env.targetHeader.setVisible(selected);
        // Graph + "Velocity Amount" box: selected env only.
        if (env.graph) env.graph->setVisible(selected);
        env.velBox.setVisible(selected);
        for (auto* vb : { env.attVB.get(), env.decVB.get(), env.relVB.get(), env.levelVB.get() })
            if (vb) vb->setVisible(selected);
    };
    setEnvControlsVisible(ampEnv,  activeEnvTab == 0);
    setEnvControlsVisible(mod1Env, activeEnvTab == 1);
    setEnvControlsVisible(mod2Env, activeEnvTab == 2);

    auto setLfoControlsVisible = [](LfoSection& lfo)
    {
        const bool sync = lfo.clockModeHidden.getSelectedId() == 2;
        lfo.header.setVisible(true);
        lfo.targetBox.setVisible(true);
        lfo.waveBox.setVisible(true);
        for (auto& btn : lfo.waveBtns)
            btn.setVisible(false);
        lfo.modeBtn.setVisible(true);   // Free/Trig sits left of the sync clock
        for (auto& btn : lfo.modeBtns)
            btn.setVisible(false);
        lfo.clockBtn.setVisible(true);
        if (lfo.rateRow)     lfo.rateRow->setVisible(!sync);
        if (lfo.divisionRow) lfo.divisionRow->setVisible(sync);
        if (lfo.depthRow)    lfo.depthRow->setVisible(true);
    };
    setLfoControlsVisible(lfo1);
    setLfoControlsVisible(lfo2);
    setLfoControlsVisible(lfo3);
    lfoHeader.setVisible(false);

    // Per-target AT bars are the easy-panel module; the column header is
    // re-shown by the columns easy-layout (hidden in the stacked fallback).
    aftertouchHeader.setVisible(false);
    for (auto& bar : aftertouchBars)
        if (bar) bar->setVisible(true);

    auto setDriftControlsVisible = [](DriftSection& drift)
    {
        const bool sync = drift.clockModeHidden.getSelectedId() == 2;
        drift.header.setVisible(true);
        drift.targetBox.setVisible(true);
        drift.waveBox.setVisible(true);
        for (auto& btn : drift.waveBtns)
            btn.setVisible(false);
        drift.clockBtn.setVisible(true);
        if (drift.rateRow)     drift.rateRow->setVisible(!sync);
        if (drift.divisionRow) drift.divisionRow->setVisible(sync);
        if (drift.depthRow)    drift.depthRow->setVisible(true);
    };
    setDriftControlsVisible(drift1);
    setDriftControlsVisible(drift2);
    setDriftControlsVisible(drift3);
    driftHeader.setVisible(false);
    regenHeader.setVisible(true);
    {
        // Default to the compact dropdown; layoutGenerateEasy upgrades to the
        // vertical "repeat every" switchbox when the column is tall enough.
        for (int i = 0; i < kNumRegenBtns; ++i)
            regenBtns[i].setVisible(false);
        regenHidden.setVisible(true);
    }
    if (crossfadeRegenRow)
        crossfadeRegenRow->setVisible(true);

    syncModTabButtons();
}

void SynthPanel::selectFirstActiveModTabs()
{
    auto pickEnv = [](const EnvSection& a, const EnvSection& b, const EnvSection& c)
    {
        if (a.targetBox.getSelectedId() > 1) return 0;
        if (b.targetBox.getSelectedId() > 1) return 1;
        if (c.targetBox.getSelectedId() > 1) return 2;
        return 0;
    };
    auto pickLfo = [](const LfoSection& a, const LfoSection& b, const LfoSection& c)
    {
        if (a.targetBox.getSelectedId() > 1) return 0;
        if (b.targetBox.getSelectedId() > 1) return 1;
        if (c.targetBox.getSelectedId() > 1) return 2;
        return 0;
    };
    auto pickDrift = [](const DriftSection& a, const DriftSection& b, const DriftSection& c)
    {
        if (a.targetBox.getSelectedId() > 1) return 0;
        if (b.targetBox.getSelectedId() > 1) return 1;
        if (c.targetBox.getSelectedId() > 1) return 2;
        return 0;
    };

    activeEnvTab = pickEnv(ampEnv, mod1Env, mod2Env);
    activeLfoTab = pickLfo(lfo1, lfo2, lfo3);
    activeDriftTab = pickDrift(drift1, drift2, drift3);
}

void SynthPanel::syncModTabButtons()
{
    auto syncGroup = [](std::array<juce::TextButton, kNumModTabs>& tabs,
                        int activeIndex,
                        juce::Colour accent)
    {
        for (int i = 0; i < kNumModTabs; ++i)
        {
            auto& btn = tabs[static_cast<size_t>(i)];
            const bool active = i == activeIndex;
            int edges = 0;
            if (i > 0) edges |= juce::Button::ConnectedOnLeft;
            if (i < kNumModTabs - 1) edges |= juce::Button::ConnectedOnRight;
            btn.setConnectedEdges(edges);
            btn.setToggleState(active, juce::dontSendNotification);
            // Selected-tab text follows the unified switchbox rule: dark ink on
            // bright accents (amber env/lfo), white on dark ones (drift) — same as
            // styleSwitchButton. (textColourOffId's active branch is dead since
            // active == toggled, so JUCE reads textColourOnId; kept symmetric.)
            const auto onTxt = switchBoxSelectedTextColour(accent);
            btn.setColour(juce::TextButton::buttonColourId, active ? accent : kSurface);
            btn.setColour(juce::TextButton::buttonOnColourId, accent);
            btn.setColour(juce::TextButton::textColourOffId, active ? onTxt : kDim);
            btn.setColour(juce::TextButton::textColourOnId, onTxt);
        }
    };

    syncGroup(envTabBtns, activeEnvTab, kModCol);
    syncGroup(lfoTabBtns, activeLfoTab, kLfoCol);
    syncGroup(driftTabBtns, activeDriftTab, kDriftCol);
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
    float maxF = remaining / 57.5f;
    return juce::jlimit(9.0f, 20.0f, maxF);
}

template <size_t N>
static int choiceBoxWidthFor(const ChoiceEntry (&entries)[N], float f, int fallbackWidth)
{
    const float fontSize = juce::jmax(kUiControlFontMin, juce::jmin(f, 13.0f));
    int maxTextWidth = 0;
    for (const auto& e : entries)
        maxTextWidth = juce::jmax(maxTextWidth,
                                  measureTextWidth(juce::String(juce::CharPointer_UTF8(e.label)), fontSize));

    return juce::jmax(fallbackWidth, maxTextWidth + juce::roundToInt(fontSize * 2.8f));
}

// ──────────────────────────────────────────────────────────────────────────────
// Layout helpers
// ──────────────────────────────────────────────────────────────────────────────
static void layoutModTabStrip(std::array<juce::TextButton, 3>& tabs,
                               juce::Rectangle<int> area,
                               juce::Rectangle<int>& switchBounds)
{
    const int count = static_cast<int>(tabs.size());
    const int cellW = area.getWidth() / count;
    switchBounds = {};
    for (int i = 0; i < count; ++i)
    {
        auto cell = (i == count - 1)
            ? area
            : area.removeFromLeft(cellW);
        tabs[static_cast<size_t>(i)].setBounds(cell);
        switchBounds = switchBounds.isEmpty() ? cell : switchBounds.getUnion(cell);
    }
}

template <size_t N>
static void layoutSegmentedButtons(std::array<juce::TextButton, N>& buttons,
                                   juce::Rectangle<int> area,
                                   juce::Rectangle<int>& switchBounds,
                                   bool vertical)
{
    const int count = static_cast<int>(buttons.size());
    const int cellSize = vertical ? area.getHeight() / count
                                  : area.getWidth() / count;
    switchBounds = {};

    for (int i = 0; i < count; ++i)
    {
        auto cell = (i == count - 1)
            ? area
            : (vertical ? area.removeFromTop(cellSize)
                        : area.removeFromLeft(cellSize));
        auto& btn = buttons[static_cast<size_t>(i)];
        int edges = 0;
        if (vertical)
        {
            if (i > 0) edges |= juce::Button::ConnectedOnTop;
            if (i < count - 1) edges |= juce::Button::ConnectedOnBottom;
        }
        else
        {
            if (i > 0) edges |= juce::Button::ConnectedOnLeft;
            if (i < count - 1) edges |= juce::Button::ConnectedOnRight;
        }

        btn.setConnectedEdges(edges);
        btn.setBounds(cell);
        switchBounds = switchBounds.isEmpty() ? cell : switchBounds.getUnion(cell);
    }
}

void SynthPanel::layoutFilterEasy(juce::Rectangle<int> area, float f, int rowH, int gap)
{
    for (auto* r : { cutoffRow.get(), resoRow.get(), filterDriveRow.get(),
                     kbdTrackRow.get(), filterMixRow.get() })
        if (r)
        {
            r->clearForcedLabelWidth();
            r->clearForcedValueWidth();
            r->setKnobMode(true);
        }

    cutoffRow->getLabel().setText("Cutoff", juce::dontSendNotification);
    resoRow->getLabel().setText("Reso", juce::dontSendNotification);
    filterDriveRow->getLabel().setText("Drive", juce::dontSendNotification);
    kbdTrackRow->getLabel().setText("Kbd", juce::dontSendNotification);
    filterMixRow->getLabel().setText("Mix", juce::dontSendNotification);

    const int rowGap = juce::jmax(gap, 6);

    auto layoutButtons = [](auto& buttons, int count, juce::Rectangle<int> row,
                            juce::Rectangle<int>& switchBounds)
    {
        const int cellW = row.getWidth() / count;
        switchBounds = {};
        for (int i = 0; i < count; ++i)
        {
            auto cell = (i == count - 1) ? row : row.removeFromLeft(cellW);
            buttons[i].setBounds(cell);
            switchBounds = switchBounds.isEmpty() ? cell : switchBounds.getUnion(cell);
        }
    };

    filterAlgBtns[FilterAlgorithm::SVF].setButtonText("SVF");
    filterAlgBtns[FilterAlgorithm::Ladder].setButtonText("Ladder");
    // filterAlgBtns[Warp] is hidden in Easy; WarpHoldBtn occupies its cell.
    {
        // One 4-way switch: OFF | SVF | Ladder | [WarpHoldBtn], equal cells.
        auto algRow = area.removeFromTop(rowH);
        const int cellW = algRow.getWidth() / (kNumAlgBtns + 1);
        filterEasyOffBtn.setBounds(algRow.removeFromLeft(cellW));
        filterAlgBtns[FilterAlgorithm::SVF].setBounds(algRow.removeFromLeft(cellW));
        filterAlgBtns[FilterAlgorithm::Ladder].setBounds(algRow.removeFromLeft(cellW));
        filterEasyWarpBtn.setBounds(algRow);           // remaining cell
        filterAlgSwitchBounds = filterEasyOffBtn.getBounds()
            .getUnion(filterAlgBtns[FilterAlgorithm::SVF].getBounds())
            .getUnion(filterEasyWarpBtn.getBounds());
    }
    area.removeFromTop(rowGap);
    {
        // Slope is a 3-way switch (6/12/24 — Easy drops the 18 dB segment) across
        // three of four equal cells; the freed fourth cell, right of 24 dB, hosts
        // the LP/HP/BP type toggle.
        auto slopeRow = area.removeFromTop(rowH);
        const int cellW = slopeRow.getWidth() / 4;
        juce::TextButton* const slope3[] = { &filterSlopeBtns[FilterSlope::Slope6],
                                             &filterSlopeBtns[FilterSlope::Slope12],
                                             &filterSlopeBtns[FilterSlope::Slope24] };
        filterSlopeSwitchBounds = {};
        for (auto* b : slope3)
        {
            auto cell = slopeRow.removeFromLeft(cellW);
            b->setBounds(cell);
            filterSlopeSwitchBounds = filterSlopeSwitchBounds.isEmpty()
                                          ? cell : filterSlopeSwitchBounds.getUnion(cell);
        }
        filterEasyTypeBtn.setBounds(slopeRow);   // remaining cell, right of 24 dB
        filterTypeSwitchBounds = slopeRow;
    }
    area.removeFromTop(rowGap);

    auto knobArea = area;
    const int knobGap = juce::jmax(8, juce::roundToInt(f * 0.65f));
    const int desiredSmallH = juce::jmax(rowH * 4, juce::roundToInt(f * 6.6f));
    const int minCutoffH = juce::jmax(rowH * 6, juce::roundToInt(f * 9.5f));
    const int maxCutoffH = juce::roundToInt(static_cast<float>(desiredSmallH) * 2.0f);
    const int availableH = knobArea.getHeight();
    int cutoffH = juce::jlimit(minCutoffH,
                               juce::jmax(minCutoffH, maxCutoffH),
                               availableH - desiredSmallH * 2 - knobGap * 2);
    if (cutoffH < minCutoffH || availableH - cutoffH - knobGap * 2 < desiredSmallH * 2)
        cutoffH = juce::jmin(maxCutoffH,
                             juce::jmax(minCutoffH, availableH - desiredSmallH * 2 - knobGap * 2));

    const int remainingRowsH = juce::jmax(0, availableH - cutoffH - knobGap * 2);
    const int row1H = remainingRowsH / 2;
    const int row2H = remainingRowsH - row1H;

    auto cutoffArea = knobArea.removeFromTop(juce::jmin(cutoffH, knobArea.getHeight()));
    const int cutoffW = juce::jmin(cutoffArea.getWidth(),
                                   juce::jmax(rowH * 5, juce::roundToInt(static_cast<float>(cutoffArea.getWidth()) * 0.72f)));
    cutoffRow->setBounds(juce::Rectangle<int>(cutoffW, cutoffArea.getHeight()).withCentre(cutoffArea.getCentre()));

    knobArea.removeFromTop(knobGap);
    auto row1 = knobArea.removeFromTop(row1H);
    knobArea.removeFromTop(knobGap);
    auto row2 = knobArea.removeFromTop(juce::jmin(row2H, knobArea.getHeight()));

    auto placePair = [&](SliderRow& left, SliderRow& right, juce::Rectangle<int> row)
    {
        const int cellW = juce::jmax(1, (row.getWidth() - knobGap) / 2);
        auto leftCell = row.removeFromLeft(cellW);
        row.removeFromLeft(knobGap);
        left.setBounds(leftCell);
        right.setBounds(row);
    };

    placePair(*resoRow, *filterDriveRow, row1);
    placePair(*kbdTrackRow, *filterMixRow, row2);
}

void SynthPanel::layoutEnvEasy(EnvSection& env, juce::Rectangle<int> area, float f, int rowH, int gap)
{
    // Top row: [Target left-header][short dropdown]
    auto targetRow = area.removeFromTop(rowH);
    const float headerFs = juce::jmax(kUiControlFontMin, juce::jmin(13.0f, static_cast<float>(rowH) * 0.58f));
    env.targetHeader.setFont(juce::FontOptions(headerFs, juce::Font::bold));
    const int targetHdrW = measureTextWidth("Target", headerFs) + 16;
    env.targetHeader.setBounds(targetRow.removeFromLeft(juce::jmin(targetHdrW, targetRow.getWidth())));
    targetRow.removeFromLeft(gap);
    const int tgtW = choiceBoxWidthFor(EnvTarget::kEntries, f, juce::roundToInt(f * 7.0f));
    env.targetBox.setBounds(targetRow.removeFromLeft(juce::jmin(tgtW, targetRow.getWidth())));
    {
        const int loopW = juce::jmin(juce::roundToInt(f * 3.2f), targetRow.getWidth());
        if (loopW > 0) env.loopBtn.setBounds(targetRow.removeFromRight(loopW).reduced(1));
    }
    area.removeFromTop(gap);

    const int rowGap = juce::jmax(gap, 6);

    // Split the full remaining height: the ADSR graph is the dominant hero
    // (~2/3); the "Velocity Amount" box takes the secondary third.
    // One gap between them; minimums keep both usable when the column is short.
    const int avail     = juce::jmax(0, area.getHeight() - rowGap);
    const int minGraphH = juce::jmax(rowH * 3, juce::roundToInt(f * 5.0f));
    const int minVelH   = juce::jmax(rowH * 3, juce::roundToInt(f * 4.5f));  // title + bars
    const int loGraph   = juce::jmin(minGraphH, avail);
    const int hiGraph   = juce::jmax(loGraph, avail - juce::jmin(minVelH, avail));
    const int graphH    = juce::jlimit(loGraph, hiGraph,
                                       juce::roundToInt(static_cast<float>(avail) * 0.60f));
    auto graphArea = area.removeFromTop(juce::jmin(graphH, area.getHeight()));
    if (env.graph) env.graph->setBounds(graphArea);
    area.removeFromTop(rowGap);

    // "Velocity Amount" ModuleBox: accent top-header (same template as DURATION/
    // RE-PROMPT/RESYNTH) framing four vertical drag-fill bars (Att/Dec/Rel =
    // bipolar velocity→stage TIME; Level = the global velocity→peak amount,
    // velAmt — last, set slightly apart as the sum attenuation).
    {
        const int headerH = juce::jlimit(12, rowH, juce::roundToInt(f * 1.4f));
        env.velBox.setBaseFont(f);
        env.velBox.setHeaderHeight(headerH);
        env.velBox.setContentPadding(juce::jmax(3, juce::roundToInt(f * 0.3f)));
        env.velBox.setBounds(area);
        auto vsArea = env.velBox.getContentBounds();   // PARENT-relative

        VelocityBar* vb[4] = { env.attVB.get(), env.decVB.get(), env.relVB.get(), env.levelVB.get() };
        constexpr int vGap = 4;
        constexpr int levelGap = 8;   // wider gap before Level — it is the sum attenuation
        const int vW = juce::jmax(1, (vsArea.getWidth() - vGap * 2 - levelGap) / 4);
        for (int i = 0; i < 4; ++i)
        {
            auto cell = (i == 3) ? vsArea : vsArea.removeFromLeft(vW);
            if (i < 3) vsArea.removeFromLeft(i == 2 ? levelGap : vGap);
            if (vb[i]) vb[i]->setBounds(cell);
        }
    }
}

void SynthPanel::layoutLfoEasy(LfoSection& lfo, juce::Rectangle<int> area, float f, int rowH, int gap)
{
    for (auto* r : { lfo.rateRow.get(), lfo.depthRow.get(), lfo.divisionRow.get() })
        if (r)
        {
            r->clearForcedLabelWidth();
            r->clearForcedValueWidth();
            r->setKnobMode(true);
        }

    lfo.rateRow->getLabel().setText("Rate", juce::dontSendNotification);
    lfo.divisionRow->getLabel().setText("Rate", juce::dontSendNotification);
    lfo.depthRow->getLabel().setText("Amt", juce::dontSendNotification);

    const int rowGap = juce::jmax(gap, 6);
    const int controlGap = juce::jmax(7, juce::roundToInt(f * 0.55f));
    const float topFontSize = juce::jmax(kUiControlFontMin, juce::jmin(13.0f, static_cast<float>(rowH) * 0.58f));

    const int syncNudge = juce::jmax(2, juce::roundToInt(f * 0.18f));

    auto headerRow = area.removeFromTop(rowH);
    const int headerW = juce::jmax(54, measureTextWidth(lfo.header.getText(), topFontSize) + 18);
    lfo.header.setFont(juce::FontOptions(topFontSize, juce::Font::bold));
    lfo.header.setJustificationType(juce::Justification::centred);
    // Header band: light text on accent@0.7 — same treatment as every other
    // module/section header (see labelAsHeaderBand). Replaces the old opaque
    // amber chip whose brightness-ink rule disagreed with Drift's white.
    labelAsHeaderBand(lfo.header, kLfoCol);
    lfo.header.setBounds(headerRow.removeFromLeft(juce::jmin(headerW, headerRow.getWidth())));
    // Free/Trig padlock sits at the header's right edge — directly above the sync
    // clock — so the control row below stays a clean [target][wave][clock], pixel-
    // identical to Drift. (Packed into the control row it was a 4th item that
    // overcrowded the column and shoved the sync clock against/past the frame.)
    const int lockW = juce::jmin(rowH, headerRow.getWidth());
    if (lockW > 0)
        lfo.modeBtn.setBounds(headerRow.removeFromRight(lockW).translated(-syncNudge, 0));
    lfo.modeHidden.onChange();
    area.removeFromTop(rowGap);

    auto top = area.removeFromTop(rowH);
    const int targetW = choiceBoxWidthFor(LfoTarget::kEntries, f, juce::roundToInt(f * 8.4f));
    const int waveW = choiceBoxWidthFor(LfoWave::kEntries, f, juce::roundToInt(f * 4.8f));
    const int syncW = rowH;

    const std::vector<ResponsiveStripItem> rowItems {
        { targetW, juce::roundToInt(f * 5.4f), 0, true, ResponsiveStripFallback::none },
        { waveW,   juce::roundToInt(f * 3.7f), 0, false, ResponsiveStripFallback::none },
        { syncW,   rowH, 0, false, ResponsiveStripFallback::none }
    };
    auto rowLayout = layoutResponsiveStrip(top, rowItems, controlGap);

    lfo.targetBox.setBounds(rowLayout.bounds[0]);
    lfo.waveBox.setBounds(rowLayout.bounds[1]);
    lfo.clockBtn.setBounds(rowLayout.bounds[2].translated(-syncNudge, 0));
    lfo.waveSwitchBounds = {};
    lfo.modeSwitchBounds = {};

    area.removeFromTop(rowGap);

    auto knobs = area;
    const int knobGap = juce::jmax(12, juce::roundToInt(f * 0.95f));
    const int cellW = juce::jmax(1, (knobs.getWidth() - knobGap) / 2);
    auto rateCell = knobs.removeFromLeft(cellW);
    knobs.removeFromLeft(knobGap);
    auto amtCell = knobs;

    lfo.rateRow->setBounds(rateCell);
    lfo.divisionRow->setBounds(rateCell);
    lfo.depthRow->setBounds(amtCell);
}

void SynthPanel::layoutDriftEasy(DriftSection& drift, juce::Rectangle<int> area, float f, int rowH, int gap)
{
    for (auto* r : { drift.rateRow.get(), drift.depthRow.get(), drift.divisionRow.get() })
        if (r)
        {
            r->clearForcedLabelWidth();
            r->clearForcedValueWidth();
            r->setKnobMode(true);
        }

    drift.rateRow->getLabel().setText("Rate", juce::dontSendNotification);
    drift.divisionRow->getLabel().setText("Rate", juce::dontSendNotification);
    drift.depthRow->getLabel().setText("Amt", juce::dontSendNotification);

    const int rowGap = juce::jmax(gap, 6);
    const int controlGap = juce::jmax(7, juce::roundToInt(f * 0.55f));
    const float topFontSize = juce::jmax(kUiControlFontMin, juce::jmin(13.0f, static_cast<float>(rowH) * 0.58f));

    const juce::String title = &drift == &drift2 ? "DRIFT 2" : (&drift == &drift3 ? "DRIFT 3" : "DRIFT 1");
    auto headerRow = area.removeFromTop(rowH);
    const int headerW = juce::jmax(68, measureTextWidth(title, topFontSize) + 18);
    drift.header.setText(title, juce::dontSendNotification);
    drift.header.setFont(juce::FontOptions(topFontSize, juce::Font::bold));
    drift.header.setJustificationType(juce::Justification::centred);
    // Header band: light text on accent@0.7 (see labelAsHeaderBand).
    labelAsHeaderBand(drift.header, kDriftCol);
    drift.header.setBounds(headerRow.removeFromLeft(juce::jmin(headerW, headerRow.getWidth())));
    area.removeFromTop(rowGap);

    auto top = area.removeFromTop(rowH);
    const int targetW = choiceBoxWidthFor(DriftTarget::kEntries, f, juce::roundToInt(f * 8.4f));
    const int waveW = choiceBoxWidthFor(DriftWave::kEntries, f, juce::roundToInt(f * 4.8f));
    const int syncW = rowH;

    const std::vector<ResponsiveStripItem> rowItems {
        { targetW, juce::roundToInt(f * 5.4f), 0, true, ResponsiveStripFallback::none },
        { waveW,   juce::roundToInt(f * 3.7f), 0, false, ResponsiveStripFallback::none },
        { syncW,   rowH, 0, false, ResponsiveStripFallback::none }
    };
    auto rowLayout = layoutResponsiveStrip(top, rowItems, controlGap);

    drift.targetBox.setBounds(rowLayout.bounds[0]);
    drift.waveBox.setBounds(rowLayout.bounds[1]);
    const int syncNudge = juce::jmax(2, juce::roundToInt(f * 0.18f));
    drift.clockBtn.setBounds(rowLayout.bounds[2].translated(-syncNudge, 0));
    drift.waveSwitchBounds = {};

    area.removeFromTop(rowGap);

    auto knobs = area;
    const int knobGap = juce::jmax(12, juce::roundToInt(f * 0.95f));
    const int cellW = juce::jmax(1, (knobs.getWidth() - knobGap) / 2);
    auto rateCell = knobs.removeFromLeft(cellW);
    knobs.removeFromLeft(knobGap);
    auto amtCell = knobs;

    drift.rateRow->setBounds(rateCell);
    drift.divisionRow->setBounds(rateCell);
    drift.depthRow->setBounds(amtCell);
}

void SynthPanel::layoutAftertouchEasy(juce::Rectangle<int> area)
{
    const int n = (int) aftertouchBars.size();
    if (n <= 0 || area.isEmpty())
        return;

    // 12 equal stacked bars; each paints its own kBorder edge so neighbours
    // share a 1px divider (no gap). Last bar absorbs the rounding remainder.
    const int barH = juce::jmax(1, area.getHeight() / n);
    for (int i = 0; i < n; ++i)
    {
        auto row = (i == n - 1) ? area : area.removeFromTop(barH);
        if (aftertouchBars[i])
            aftertouchBars[i]->setBounds(row);
    }
}

void SynthPanel::layoutGenerateEasy(juce::Rectangle<int> area, float f, int rowH, int gap, bool ownHeader)
{
    const int rowGap = juce::jmax(gap, 6);
    const float chipFontSize = juce::jmax(kUiControlFontMin,
                                          juce::jmin(13.0f, static_cast<float>(rowH) * 0.58f));

    // Header chip "REGENERATE" — drift-orange (subordinated to drift). In the
    // columnar mod view this column already has an aligned REGENERATE header bar
    // above it (layoutModEasy), so the in-column chip is skipped (ownHeader=false).
    if (ownHeader)
    {
        auto headerRow = area.removeFromTop(juce::jmin(rowH, area.getHeight()));
        const int headerW = juce::jmax(72, measureTextWidth("REGENERATE", chipFontSize) + 18);
        regenHeader.setText(" REGENERATE", juce::dontSendNotification);
        regenHeader.setFont(juce::FontOptions(chipFontSize, juce::Font::bold));
        regenHeader.setJustificationType(juce::Justification::centred);
        // Header band: light text on accent@0.7 (see labelAsHeaderBand).
        labelAsHeaderBand(regenHeader, kRegenCol);
        regenHeader.setBounds(headerRow.removeFromLeft(juce::jmin(headerW, headerRow.getWidth())));
        area.removeFromTop(rowGap);
    }

    // Vertical 7-way switchbox replaces the dropdown when the column is tall
    // enough; otherwise fall back to the compact dropdown. The "REGENERATE"
    // chip above already names the control, so there is no caption row.
    const int segGap   = 1;
    const int btnH     = juce::jmax(11, juce::roundToInt(rowH * 0.66f));
    const int switchH  = btnH * kNumRegenBtns + segGap * (kNumRegenBtns - 1);
    const int xfadeMin = juce::roundToInt(rowH * 2.2f);   // keep the slider usable
    const bool useSwitch = area.getHeight() >= switchH + rowGap + xfadeMin;

    if (useSwitch)
    {
        regenHidden.setVisible(false);

        auto switchCol = area.removeFromTop(juce::jmin(switchH, area.getHeight()));
        const int segH = juce::jmax(1,
            (switchCol.getHeight() - segGap * (kNumRegenBtns - 1)) / kNumRegenBtns);
        for (int i = 0; i < kNumRegenBtns; ++i)
        {
            int edges = 0;
            if (i > 0)                 edges |= juce::Button::ConnectedOnTop;
            if (i < kNumRegenBtns - 1) edges |= juce::Button::ConnectedOnBottom;
            regenBtns[i].setConnectedEdges(edges);
            regenBtns[i].setVisible(true);
            regenBtns[i].setBounds(switchCol.removeFromTop(juce::jmin(segH, switchCol.getHeight())));
            if (i < kNumRegenBtns - 1)
                switchCol.removeFromTop(juce::jmin(segGap, switchCol.getHeight()));
        }
        regenSwitchBounds = regenBtns[0].getBounds()
            .getUnion(regenBtns[kNumRegenBtns - 1].getBounds());
        area.removeFromTop(rowGap);
    }
    else
    {
        for (int i = 0; i < kNumRegenBtns; ++i)
            regenBtns[i].setVisible(false);
        regenHidden.setVisible(true);
        auto comboRow = area.removeFromTop(juce::jmin(rowH, area.getHeight()));
        regenHidden.setBounds(comboRow);
        area.removeFromTop(rowGap);
        regenSwitchBounds = {};
    }

    // Vertical XFade slider fills the remaining column height.
    if (crossfadeRegenRow)
    {
        crossfadeRegenRow->clearForcedLabelWidth();
        crossfadeRegenRow->clearForcedValueWidth();
        crossfadeRegenRow->setVerticalMode(true);
        crossfadeRegenRow->getLabel().setText("XFade", juce::dontSendNotification);
        crossfadeRegenRow->setBounds(area);
    }
}

void SynthPanel::layoutModEasy(juce::Rectangle<int>& area, juce::Rectangle<int> modHeaderRow, float f, int rowH, int gap, int headerH, float headerFs)
{
    juce::ignoreUnused(headerH);

    auto* env = activeEnvTab == 1 ? &mod1Env : (activeEnvTab == 2 ? &mod2Env : &ampEnv);

    const int tabH = rowH;
    const int blockGap = juce::jmax(7, juce::roundToInt(f * 0.65f));
    const bool columns = area.getWidth() >= juce::roundToInt(f * 46.0f);
    const int contentInset = juce::jmax(4, juce::roundToInt(f * 0.35f));
    int easyModStackWidth = 0;

    filterHeader.setText(" FILTER", juce::dontSendNotification);
    filterHeader.setFont(juce::FontOptions(headerFs));
    labelAsHeaderBand(filterHeader, kFilterCol);
    filterHeader.setJustificationType(juce::Justification::centredLeft);

    auto layoutBlock = [&](std::array<juce::TextButton, kNumModTabs>& tabs,
                           juce::Rectangle<int>& tabBounds,
                           juce::Rectangle<int>& blockBounds,
                           juce::Rectangle<int> block,
                           auto&& layoutContent)
    {
        blockBounds = block;
        // Uniform padding inside the card. ENV was the only easy block insetting
        // its content horizontally only (reduced(contentInset, 0)); its siblings
        // (Filter/Generate) inset both axes, so ENV's controls + value read-outs
        // touched the top/bottom frame edges — the "falsche Randabstände" vs the
        // Duration card template.
        block.reduce(contentInset, contentInset);
        auto tabRow = block.removeFromTop(tabH);
        layoutModTabStrip(tabs, tabRow, tabBounds);
        block.removeFromTop(juce::jmax(gap * 2, 8));
        layoutContent(block);
    };

    auto layoutLfoStack = [&](juce::Rectangle<int> block)
    {
        if (columns && easyModStackWidth > 0)
            block.setWidth(juce::jmin(block.getWidth(), easyModStackWidth));

        lfoEasyBlockBounds = block;
        lfoTabSwitchBounds = {};

        auto content = block.reduced(contentInset, contentInset);
        const int moduleGap = juce::jmax(9, juce::roundToInt(f * 0.72f));
        const int availableH = juce::jmax(0, content.getHeight() - moduleGap * 2);
        const int moduleH = juce::jmax(rowH * 3, availableH / 3);
        LfoSection* lfos[] = { &lfo1, &lfo2, &lfo3 };

        for (int i = 0; i < 3; ++i)
        {
            auto module = (i == 2)
                ? content
                : content.removeFromTop(juce::jmin(moduleH, content.getHeight()));
            lfoEasyModuleBounds[static_cast<size_t>(i)] = module;
            layoutLfoEasy(*lfos[i], module, f, rowH, gap);

            if (i < 2)
                content.removeFromTop(moduleGap);
        }
    };

    auto layoutDriftStack = [&](juce::Rectangle<int> block)
    {
        driftEasyBlockBounds = block;
        driftTabSwitchBounds = {};

        auto content = block.reduced(contentInset, contentInset);
        const int moduleGap = juce::jmax(9, juce::roundToInt(f * 0.72f));
        const int availableH = juce::jmax(0, content.getHeight() - moduleGap * 2);
        const int moduleH = juce::jmax(rowH * 3, availableH / 3);
        DriftSection* drifts[] = { &drift1, &drift2, &drift3 };

        for (int i = 0; i < 3; ++i)
        {
            auto module = (i == 2)
                ? content
                : content.removeFromTop(juce::jmin(moduleH, content.getHeight()));
            driftEasyModuleBounds[static_cast<size_t>(i)] = module;
            layoutDriftEasy(*drifts[i], module, f, rowH, gap);

            if (i < 2)
                content.removeFromTop(moduleGap);
        }
    };

    if (columns)
    {
        const int blockW = area.getWidth();
        const int colGap = juce::jmax(22, juce::roundToInt(f * 1.7f));
        const int envW = juce::jlimit(juce::roundToInt(f * 16.0f),
                                      juce::roundToInt(f * 24.0f),
                                      juce::roundToInt(static_cast<float>(blockW) * 0.28f));

        const int previousDriftW = juce::jlimit(juce::roundToInt(f * 23.0f),
                                                juce::roundToInt(f * 34.0f),
                                                juce::roundToInt(static_cast<float>(blockW) * 0.28f));
        const int previousLfoAreaW = juce::jmax(0, blockW - previousDriftW - colGap);
        const int previousLfoW = previousLfoAreaW >= juce::roundToInt(f * 30.0f)
            ? juce::jlimit(juce::roundToInt(f * 23.0f),
                           juce::roundToInt(f * 34.0f),
                           previousLfoAreaW / 2)
            : previousLfoAreaW;
        easyModStackWidth = juce::jlimit(juce::roundToInt(f * 14.0f),
                                         juce::jmax(1, blockW),
                                         juce::roundToInt(static_cast<float>(previousLfoW) * 0.60f));

        // 6 columns: filter | env | lfo | drift | AT | generate. AT is a thin
        // column (~82% of a stack column), generate the narrowest (~55%). The
        // parts denominator 100+100+100+82+55=437 carves AT's width out of the
        // flexible columns, leaving env (envW) untouched.
        const int stackW = juce::jmin(easyModStackWidth,
                                      juce::jmax(1, (blockW - envW - colGap * 5) * 100 / 437));
        const int aftertouchW = juce::jmax(juce::roundToInt(f * 6.0f),
                                           juce::roundToInt(static_cast<float>(stackW) * 0.82f));
        const int generateW = juce::jmax(juce::roundToInt(f * 7.0f),
                                         juce::roundToInt(static_cast<float>(stackW) * 0.55f));

        // Per-module header bars, each aligned to its column below
        // (filter | env | lfo | drift | generate) — replaces the single "CONTROLS"
        // umbrella so every module names itself. Widths mirror the block row exactly.
        auto styleHeaderBar = [headerFs](juce::Label& l, const juce::String& t, juce::Colour c)
        {
            l.setText(t, juce::dontSendNotification);
            l.setFont(juce::FontOptions(headerFs));
            l.setJustificationType(juce::Justification::centredLeft);
            labelAsHeaderBand(l, c);
            l.setVisible(true);
        };
        filterHeader.setBounds(modHeaderRow.removeFromLeft(stackW));
        modHeaderRow.removeFromLeft(colGap);
        modHeader.setText(" ENVELOPES", juce::dontSendNotification);   // env column (bg/font already set in resized())
        modHeader.setBounds(modHeaderRow.removeFromLeft(envW));
        modHeaderRow.removeFromLeft(colGap);
        styleHeaderBar(lfoHeader, " LFO", kLfoCol);
        lfoHeader.setBounds(modHeaderRow.removeFromLeft(stackW));
        modHeaderRow.removeFromLeft(colGap);
        styleHeaderBar(driftHeader, " DRIFT", kDriftCol);
        driftHeader.setBounds(modHeaderRow.removeFromLeft(stackW));
        modHeaderRow.removeFromLeft(colGap);
        styleHeaderBar(aftertouchHeader, " Poly-AT", kAtCol);
        aftertouchHeader.setBounds(modHeaderRow.removeFromLeft(aftertouchW));
        modHeaderRow.removeFromLeft(colGap);
        styleHeaderBar(regenHeader, " REGENERATE", kRegenCol);   // generate column (was an in-column chip)
        regenHeader.setBounds(modHeaderRow.removeFromLeft(juce::jmin(generateW, modHeaderRow.getWidth())));

        const int blockH = area.getHeight();
        auto block = area.removeFromTop(blockH);
        auto filterArea = block.removeFromLeft(stackW);
        block.removeFromLeft(colGap);
        auto envArea = block.removeFromLeft(envW);
        block.removeFromLeft(colGap);
        auto lfoArea = block.removeFromLeft(stackW);
        block.removeFromLeft(colGap);
        auto driftArea = block.removeFromLeft(stackW);
        block.removeFromLeft(colGap);
        auto aftertouchArea = block.removeFromLeft(aftertouchW);
        block.removeFromLeft(colGap);
        auto generateArea = block.removeFromLeft(juce::jmin(generateW, block.getWidth()));

        filterEasyBlockBounds = filterArea;
        layoutFilterEasy(filterArea.reduced(contentInset, contentInset), f, rowH, gap);
        layoutBlock(envTabBtns, envTabSwitchBounds, envEasyBlockBounds, envArea,
                    [&](juce::Rectangle<int> content) { layoutEnvEasy(*env, content, f, rowH, gap); });
        layoutLfoStack(lfoArea);
        layoutDriftStack(driftArea);

        aftertouchEasyBlockBounds = aftertouchArea;
        layoutAftertouchEasy(aftertouchArea.reduced(contentInset, contentInset));

        generateEasyBlockBounds = generateArea;
        layoutGenerateEasy(generateArea.reduced(contentInset, contentInset), f, rowH, gap, false);
        return;
    }

    // Stack layout: small FILTER chip on left of header, CONTROLS bar takes the rest.
    {
        const int filterChipW = juce::jmax(54, measureTextWidth("FILTER", headerFs) + 18);
        filterHeader.setBounds(modHeaderRow.removeFromLeft(juce::jmin(filterChipW, modHeaderRow.getWidth())));
        modHeaderRow.removeFromLeft(juce::jmax(gap, 4));
        modHeader.setBounds(modHeaderRow);
    }

    const int totalGap = blockGap * 5;
    const int usableH = juce::jmax(0, area.getHeight() - totalGap);
    const int filterH = usableH * 8 / 52;
    const int envH = usableH * 7 / 52;
    const int lfoH = usableH * 12 / 52;
    const int generateH = usableH * 5 / 52;
    const int aftertouchH = usableH * 8 / 52;
    const int driftH = usableH - filterH - envH - lfoH - generateH - aftertouchH;

    auto filterArea = area.removeFromTop(filterH);
    filterEasyBlockBounds = filterArea;
    layoutFilterEasy(filterArea.reduced(contentInset, contentInset), f, rowH, gap);
    area.removeFromTop(blockGap);

    auto envArea = area.removeFromTop(envH);
    layoutBlock(envTabBtns, envTabSwitchBounds, envEasyBlockBounds, envArea,
                [&](juce::Rectangle<int> content) { layoutEnvEasy(*env, content, f, rowH, gap); });
    area.removeFromTop(blockGap);

    auto lfoArea = area.removeFromTop(lfoH);
    layoutLfoStack(lfoArea);
    area.removeFromTop(blockGap);

    auto driftArea = area.removeFromTop(driftH);
    layoutDriftStack(driftArea);
    area.removeFromTop(blockGap);

    auto aftertouchArea = area.removeFromTop(aftertouchH);
    aftertouchEasyBlockBounds = aftertouchArea;
    layoutAftertouchEasy(aftertouchArea.reduced(contentInset, contentInset));
    area.removeFromTop(blockGap);

    auto generateArea = area.removeFromTop(generateH);
    generateEasyBlockBounds = generateArea;
    layoutGenerateEasy(generateArea.reduced(contentInset, contentInset), f, rowH, gap);
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
        if (freezeTextureBtns[0].isVisible())
            paintSwitchBoxBorder(g, freezeTextureSwitchBounds);
        if (octBtns[0].isVisible())
            paintSwitchBoxBorder(g, octaveSwitchBounds);
        if (noiseBtns[0].isVisible())
            paintSwitchBoxBorder(g, noiseSwitchBounds);
    }

    // Card: Modulation (ENVs + LFOs + Drift)
    {
        int top = modHeader.getY() - inset;
        int bot = juce::jmax(filterEasyBlockBounds.getBottom(), envEasyBlockBounds.getBottom(),
                             lfoEasyBlockBounds.getBottom(), driftEasyBlockBounds.getBottom());
        bot = juce::jmax(bot, generateEasyBlockBounds.getBottom(),
                         aftertouchEasyBlockBounds.getBottom());
        bot = juce::jmax(bot, modCardBottom);
        paintCard(g, juce::Rectangle<int>(padX, top, getWidth() - padX * 2, bot - top + inset));

        // Plain framed card (fill + border). The module-colour left stripe was
        // removed per design review — it read as an unwanted embellishment.
        auto paintEasyBlock = [&](juce::Rectangle<int> bounds)
        {
            if (bounds.isEmpty())
                return;

            g.setColour(kSurface.withAlpha(0.62f));
            g.fillRect(bounds);
            g.setColour(juce::Colour(0xaa05070d));
            g.drawRect(bounds.expanded(1, 1), 1);
            g.setColour(kBorder.withAlpha(0.82f));
            g.drawRect(bounds, 1);
        };

        paintEasyBlock(filterEasyBlockBounds);
        paintEasyBlock(envEasyBlockBounds);
        paintEasyBlock(lfoEasyBlockBounds);
        paintEasyBlock(driftEasyBlockBounds);
        paintEasyBlock(aftertouchEasyBlockBounds);
        paintEasyBlock(generateEasyBlockBounds);

        for (const auto& moduleBounds : lfoEasyModuleBounds)
        {
            if (moduleBounds.isEmpty())
                continue;

            g.setColour(kCard.withAlpha(0.42f));
            g.fillRect(moduleBounds);
            g.setColour(kBorder.withAlpha(0.72f));
            g.drawRect(moduleBounds, 1);
        }

        for (const auto& moduleBounds : driftEasyModuleBounds)
        {
            if (moduleBounds.isEmpty())
                continue;

            g.setColour(kCard.withAlpha(0.42f));
            g.fillRect(moduleBounds);
            g.setColour(kBorder.withAlpha(0.72f));
            g.drawRect(moduleBounds, 1);
        }

        paintSwitchBoxBorder(g, filterAlgSwitchBounds);
        paintSwitchBoxBorder(g, filterSlopeSwitchBounds);
        paintSwitchBoxBorder(g, filterTypeSwitchBounds);   // LP/HP/BP toggle
        paintSwitchBoxBorder(g, envTabSwitchBounds);
        paintSwitchBoxBorder(g, regenSwitchBounds);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Play-mode icon drawing (painted over children so icons sit on top of buttons)
// ──────────────────────────────────────────────────────────────────────────────
void SynthPanel::paintOverChildren(juce::Graphics& g)
{
    auto drawSegmentGroup = [&](juce::Rectangle<int> bounds, int count, bool vertical)
    {
        if (bounds.isEmpty())
            return;

        const auto outer = bounds.expanded(1, 1);
        g.setColour(juce::Colour(0xee05070d));
        g.drawRect(outer, 1);
        g.setColour(kBorder.brighter(0.35f));
        g.drawRect(bounds, 1);

        g.setColour(juce::Colour(0xdd05070d));
        for (int i = 1; i < count; ++i)
        {
            if (vertical)
            {
                const int y = bounds.getY() + (bounds.getHeight() * i) / count;
                g.drawHorizontalLine(y, static_cast<float>(bounds.getX()),
                                     static_cast<float>(bounds.getRight()));
            }
            else
            {
                const int x = bounds.getX() + (bounds.getWidth() * i) / count;
                g.drawVerticalLine(x, static_cast<float>(bounds.getY()),
                                   static_cast<float>(bounds.getBottom()));
            }
        }

        if (!vertical)
        {
            g.setColour(kBorder.brighter(0.55f));
            g.drawHorizontalLine(bounds.getBottom(), static_cast<float>(bounds.getX()),
                                 static_cast<float>(bounds.getRight()));
        }
    };

    drawSegmentGroup(envTabSwitchBounds, kNumModTabs, false);

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

    // ── Section header — derived from the panel's own height so it is stable
    // across resizes (getTopLevelComponent() returns null on the very first
    // resize, which previously caused a one-time layout shift after the first
    // user interaction).
    int headerH = juce::jlimit(14, 20, juce::roundToInt(h * 0.030f));
    float headerFs = static_cast<float>(headerH) * 0.85f;
    int headerGap = juce::jmax(3, headerH / 5);  // ~20% of header height

    engineHeader.setFont(juce::FontOptions(headerFs));
    auto engineHeaderRow = area.removeFromTop(headerH);
    engineHeader.setBounds(engineHeaderRow);
    area.removeFromTop(headerGap);

    // ── Engine mode + Voice count: compact switchboxes ──
    auto modeRow = area.removeFromTop(rowH);
    {
        int cellW = juce::roundToInt(f * 5.0f);
        samplerBtn.setBounds(modeRow.removeFromLeft(cellW));
        wavetableBtn.setBounds(modeRow.removeFromLeft(cellW));
        freezeBtn.setBounds(modeRow.removeFromLeft(cellW));
        engineSwitchBounds = samplerBtn.getBounds().getUnion(freezeBtn.getBounds());

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
    const int engineId = engineModeHidden.getSelectedId();
    const bool isWavetable = engineId == 2;
    const bool isFreeze = engineId == 3;
    const int waveformReserveH = juce::roundToInt(WaveformDisplay::HANDLE_RADIUS * 2.0f + 4.0f);
    int samplerCtrlH = waveformReserveH + rowH + gap * 2; // waveform handles + one controls row
    int filterH = headerH + headerGap + rowH + gap + rowH * 2 + gap; // advanced filter height, folded into Easy block
    int modH = gap * 3 + headerH + headerGap; // section gap + header
    int envH = (rowH * 4 + gap) * 3; // 3 envelopes × (header + 3 slider rows + gap)
    int lfoH = gap + headerH + headerGap + (rowH + gap) * 3;              // lfo header + 3 single-row LFOs
    int aftertouchH = rowH + gap;                                         // MIDI aftertouch row
    int driftH = gap + headerH + headerGap + (rowH + gap) * 3;            // drift header + 3 single-row drifts
    int regenH = gap + headerH;                                            // regen controls live in the header row
    const int advancedModBodyH = envH + lfoH + aftertouchH + driftH + regenH + gap * 5;
    // Keep Easy in the same vertical slot as Advanced so the engine waveform,
    // filter and footer sections don't jump when switching interface modes.
    int belowWave = samplerCtrlH + filterH + modH + advancedModBodyH;
    int waveH = juce::jmax(0, area.getHeight() - belowWave);

    auto layoutNoiseStrip = [&](juce::Rectangle<int>& row)
    {
        const int desiredCellW = juce::roundToInt(f * 4.0f);
        const int minLevelW = juce::roundToInt(f * 11.5f);
        const int desiredStripW = desiredCellW * kNumNoiseBtns + minLevelW;
        const int stripW = juce::jmin(desiredStripW, row.getWidth());
        auto noiseArea = row.removeFromRight(stripW);
        row.removeFromRight(juce::roundToInt(f * 0.8f));

        const int nCellW = juce::jmax(1, juce::jmin(desiredCellW, noiseArea.getWidth() / kNumNoiseBtns));
        for (int i = 0; i < kNumNoiseBtns; ++i)
            noiseBtns[i].setBounds(noiseArea.removeFromLeft(nCellW));
        noiseSwitchBounds = noiseBtns[0].getBounds().getUnion(noiseBtns[kNumNoiseBtns - 1].getBounds());
        noiseLevelRow->setBounds(noiseArea);
    };

    if (isWavetable || isFreeze)
    {
        // ── Scan-driven engines: dot + brackets on one line below waveform ──
        int scanLineH = waveformReserveH;
        waveformDisplay.setBottomReserve(scanLineH);
        waveformDisplay.setScanVisible(true);
        waveformDisplay.setBounds(area.removeFromTop(waveH + scanLineH));

        // Hide the scanRow slider (APVTS still connected), scan is drawn by WaveformDisplay
        scanRow->setBounds(-1000, -1000, 10, 10);
        scanHint.setVisible(false);

        area.removeFromTop(gap);

        if (isFreeze)
        {
            // ── Granular: position dot + texture + stereo + HF/Norm + octave + fixed noise strip ──
            auto granularRow = area.removeFromTop(rowH);
            layoutNoiseStrip(granularRow);

            const int oCellW = juce::roundToInt(f * 2.5f);
            auto octaveArea = granularRow.removeFromRight(oCellW * kNumOctBtns);
            granularRow.removeFromRight(juce::roundToInt(f * 0.8f));
            for (int i = 0; i < kNumOctBtns; ++i)
                octBtns[i].setBounds(octaveArea.removeFromLeft(oCellW));
            octaveSwitchBounds = octBtns[0].getBounds().getUnion(octBtns[kNumOctBtns - 1].getBounds());

            const int normW = juce::roundToInt(f * 4.0f);
            const int hfW = juce::roundToInt(f * 2.8f);
            normalizeToggle.setBounds(granularRow.removeFromRight(normW));
            hfBoostBtn.setBounds(granularRow.removeFromRight(hfW));
            granularRow.removeFromRight(juce::roundToInt(f * 0.8f));

            const int tCellW = juce::roundToInt(f * 3.8f);
            for (int i = 0; i < kNumFreezeTextureBtns; ++i)
                freezeTextureBtns[i].setBounds(granularRow.removeFromLeft(tCellW));
            freezeTextureSwitchBounds = freezeTextureBtns[0].getBounds()
                .getUnion(freezeTextureBtns[kNumFreezeTextureBtns - 1].getBounds());
            granularRow.removeFromLeft(juce::roundToInt(f * 0.8f));
            freezeStereoRow->setBounds(granularRow);

            frameCountLabel.setBounds(-1000, -1000, 10, 10);
            area.removeFromTop(gap);
            engineCardBottom = juce::jmax(freezeStereoRow->getBottom(), noiseLevelRow->getBottom());
        }
        else
        {
            // [→][↻][⇄] [Nf] [32|64|128|256] [Smooth] [Auto] [HF] | [White|Pink|Brown] Lvl[===]
            auto wtRow = area.removeFromTop(rowH);
            layoutNoiseStrip(wtRow);
            auto leftCol = wtRow;

            // ── Left column: loop icons + frame switchbox + smooth ──
            int iconW = juce::roundToInt(f * 2.6f);
            oneshotBtn.setBounds(leftCol.removeFromLeft(iconW));
            loopModeBtn.setBounds(leftCol.removeFromLeft(iconW));
            pingpongBtn.setBounds(leftCol.removeFromLeft(iconW));
            loopSwitchBounds = oneshotBtn.getBounds().getUnion(pingpongBtn.getBounds());
            leftCol.removeFromLeft(juce::roundToInt(f * 0.35f));

            int frameCountW = juce::roundToInt(f * 2.6f);
            frameCountLabel.setBounds(leftCol.removeFromLeft(frameCountW));
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
            leftCol.removeFromLeft(juce::roundToInt(f * 0.35f));
            int hfW = juce::jmin(juce::roundToInt(f * 2.8f), leftCol.getWidth());
            hfBoostBtn.setBounds(leftCol.removeFromLeft(hfW));

            area.removeFromTop(gap);
            engineCardBottom = juce::jmax(smoothToggle.getBottom(), noiseLevelRow->getBottom());
        }
    }
    else
    {
        // ── Sampler: waveform + bracket handles + controls row ──
        int handleLineH = waveformReserveH;
        waveformDisplay.setBottomReserve(handleLineH);
        waveformDisplay.setScanVisible(false);
        waveformDisplay.setBounds(area.removeFromTop(waveH + handleLineH));
        area.removeFromTop(gap);  // spacing to controls

        // [→][↻][⇄] [Opt] Xfade[========] [HF][Norm] [-2|-1|0|+1|+2] | [White|Pink|Brown] Lvl[===]
        auto loopRow = area.removeFromTop(rowH);
        layoutNoiseStrip(loopRow);

        // ── Left column: loop icons + Opt + Xfade + HF/Norm + Octave ──
        auto leftCol = loopRow;
        const int oCellW = juce::roundToInt(f * 2.5f);
        auto octaveArea = leftCol.removeFromRight(oCellW * kNumOctBtns);
        leftCol.removeFromRight(juce::roundToInt(f * 0.8f));
        for (int i = 0; i < kNumOctBtns; ++i)
            octBtns[i].setBounds(octaveArea.removeFromLeft(oCellW));
        octaveSwitchBounds = octBtns[0].getBounds().getUnion(octBtns[kNumOctBtns - 1].getBounds());

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

        crossfadeRow->setBounds(leftCol);

        area.removeFromTop(gap);
        engineCardBottom = juce::jmax(oneshotBtn.getBottom(), noiseLevelRow->getBottom());

        // Hide wavetable-only controls in sampler mode
        frameCountLabel.setBounds(-1000, -1000, 10, 10);
    }
    area.removeFromTop(gap * 2);

    int sectionGap = gap * 3;

    // ── MODULATION section header ──
    area.removeFromTop(sectionGap);
    modHeader.setText(" CONTROLS", juce::dontSendNotification);
    labelAsHeaderBand(modHeader, kModCol);
    modHeader.setFont(juce::FontOptions(headerFs));
    auto modHeaderBounds = area.removeFromTop(headerH);

    // layoutModEasy splits the header row between the FILTER chip and the
    // CONTROLS bar.
    area.removeFromTop(headerGap);
    area.removeFromBottom(juce::jmax(headerH, juce::roundToInt(f * 1.0f)));
    layoutModEasy(area, modHeaderBounds, f, rowH, gap, headerH, headerFs);
    modCardBottom = area.getY();
}

// ── WarpHoldBtn ──

void SynthPanel::WarpHoldBtn::mouseDown(const juce::MouseEvent& e)
{
    holdTriggered_ = false;
    startTimer(350);
    TextButton::mouseDown(e);
}

void SynthPanel::WarpHoldBtn::mouseUp(const juce::MouseEvent& e)
{
    if (!holdTriggered_)
    {
        stopTimer();
        if (onTap) onTap();
    }
    TextButton::mouseUp(e);
}

void SynthPanel::WarpHoldBtn::mouseExit(const juce::MouseEvent& e)
{
    stopTimer();
    holdTriggered_ = false;
    TextButton::mouseExit(e);
}

void SynthPanel::WarpHoldBtn::timerCallback()
{
    stopTimer();
    holdTriggered_ = true;
    juce::PopupMenu menu;
    for (int i = 0; i < FilterWarpStyle::kCount; ++i)
        menu.addItem(i + 1, juce::String(FilterWarpStyle::kEntries[i].label));
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                       [this](int r) { if (r > 0 && onStylePick) onStylePick(r); });
}

void SynthPanel::WarpHoldBtn::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    TextButton::paintButton(g, highlighted, down);
    // Corner triangle — same visual language as ComboBox dropdown indicator.
    const float s = juce::jlimit(8.0f, 14.0f, static_cast<float>(getHeight()) * 0.52f);
    g.setColour(findColour(getToggleState() ? juce::TextButton::textColourOnId
                                            : juce::TextButton::textColourOffId)
                    .withAlpha(0.22f));
    juce::Path p;
    p.addTriangle(static_cast<float>(getWidth()) - s,  static_cast<float>(getHeight()),
                  static_cast<float>(getWidth()),        static_cast<float>(getHeight()) - s,
                  static_cast<float>(getWidth()),        static_cast<float>(getHeight()));
    g.fillPath(p);
}
