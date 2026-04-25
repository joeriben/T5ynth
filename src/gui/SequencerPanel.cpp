#include "SequencerPanel.h"
#include "../PluginProcessor.h"

// ─── Note name helper ──────────────────────────────────────────────
static juce::String noteName(int n)
{
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    int oct = n / 12 - 1;
    return juce::String(names[n % 12]) + juce::String(oct);
}

namespace
{
constexpr int kOverflowPresetBase = 1000;
constexpr int kOverflowStepBase = 2000;
constexpr int kOverflowDivisionBase = 3000;
constexpr int kOverflowOctaveBase = 4000;
constexpr int kOverflowSavePattern = 5001;
constexpr int kOverflowLoadPattern = 5002;
}

// ─── IconLnF ──────────────────────────────────────────────────────
void SequencerPanel::IconLnF::drawButtonBackground(juce::Graphics& g, juce::Button& b,
                                                     const juce::Colour&, bool over, bool down)
{
    g.setColour(down ? kSurface.darker(0.1f) : over ? kSurface.brighter(0.15f) : kSurface);
    g.fillRect(b.getLocalBounds());
}

void SequencerPanel::IconLnF::drawButtonText(juce::Graphics& g, juce::TextButton& b,
                                               bool over, bool)
{
    auto bounds = b.getLocalBounds().toFloat().reduced(4.0f);
    g.setColour(over ? kSeqCol : kDim);
    g.strokePath(icon, juce::PathStrokeType(1.3f),
                 icon.getTransformToScaleToFit(bounds, true));
}


// ─── StepColumn ────────────────────────────────────────────────────

void SequencerPanel::StepColumn::paint(juce::Graphics& g)
{
    if (!processor) return;
    auto step = processor->getStepSequencer().getStep(stepIndex);
    auto b = getLocalBounds().reduced(1);
    int w = b.getWidth();

    // Background
    g.setColour(isCurrentStep ? kSeqCol.withAlpha(0.35f)
                : step.enabled ? kSurface : kBg);
    g.fillRect(b);

    // Beat group border
    if (stepIndex % 4 == 0)
    {
        g.setColour(kBorder);
        g.drawLine(static_cast<float>(b.getX()), static_cast<float>(b.getY()),
                   static_cast<float>(b.getX()), static_cast<float>(b.getBottom()), 1.0f);
    }

    // ── Note vertical slider (top 55%) ──
    int nB = noteBottom();
    auto noteR = b.removeFromTop(nB);
    {
        int semi = step.note;
        float frac = juce::jlimit(0.0f, 1.0f, static_cast<float>(semi - 36) / 48.0f);
        int fillH = juce::roundToInt(frac * static_cast<float>(noteR.getHeight()));

        g.setColour(kDimmer.withAlpha(0.3f));
        g.fillRect(noteR);
        g.setColour(step.enabled ? kSeqCol.withAlpha(0.5f) : kDim.withAlpha(0.3f));
        g.fillRect(noteR.getX(), noteR.getBottom() - fillH, noteR.getWidth(), fillH);

        g.setColour(step.enabled ? juce::Colours::white : kDim);
        float fs = juce::jlimit(7.0f, 13.0f, static_cast<float>(w) * 0.24f);
        g.setFont(juce::FontOptions(fs));
        auto noteTextR = noteR.withTrimmedTop(juce::jmin(4, juce::jmax(1, noteR.getHeight() / 12)));
        g.drawText(noteName(semi), noteTextR, juce::Justification::centredTop);
    }

    // ── Velocity horizontal bar (55%–68%) ──
    int vB = velBottom() - noteBottom();
    auto velR = b.removeFromTop(vB);
    if (velR.getHeight() > 2)
    {
        g.setColour(kDimmer.withAlpha(0.3f));
        g.fillRect(velR);
        float velPx = step.velocity * static_cast<float>(velR.getWidth());
        g.setColour(step.enabled ? kSeqCol.withAlpha(0.7f) : kDim.withAlpha(0.4f));
        g.fillRect(velR.getX(), velR.getY(), juce::roundToInt(velPx), velR.getHeight());
    }

    // ── Bottom buttons: [On][Bind] side by side (remaining 32%) ──
    auto btnArea = b;
    int halfW = btnArea.getWidth() / 2;
    auto onR = btnArea.removeFromLeft(halfW);
    auto glR = btnArea;
    float btnFs = juce::jlimit(7.0f, 11.0f, static_cast<float>(w) * 0.20f);

    // On button
    g.setColour(step.enabled ? kSeqCol.withAlpha(0.45f) : kDimmer.withAlpha(0.15f));
    g.fillRect(onR.reduced(1));
    g.setColour(step.enabled ? juce::Colours::white : kDimmer);
    g.setFont(juce::FontOptions(btnFs));
    g.drawText("On", onR, juce::Justification::centred);

    // Bind button
    g.setColour(step.bind ? kSeqCol.withAlpha(0.30f) : kDimmer.withAlpha(0.15f));
    g.fillRect(glR.reduced(1));
    g.setColour(step.bind ? juce::Colours::white : kDimmer);
    g.setFont(juce::FontOptions(btnFs));
    g.drawText("Bind", glR, juce::Justification::centred);
}

void SequencerPanel::StepColumn::mouseDown(const juce::MouseEvent& e)
{
    if (!processor) return;
    auto& seq = processor->getStepSequencer();
    auto step = seq.getStep(stepIndex);
    int y = e.getPosition().getY();

    if (y < noteBottom())
    {
        // Start note drag
        dragZone = 1;
        dragStartVal = static_cast<float>(step.note);
        noteHoldPreviewActive = processor->canUseStepHoldPreview();
        noteHoldPreviewNote = step.note;
        if (noteHoldPreviewActive)
            processor->beginStepHoldPreview(step.note);
    }
    else if (y < velBottom())
    {
        // Velocity drag
        dragZone = 3;
        float vel = static_cast<float>(e.getPosition().getX() - 1)
                    / static_cast<float>(juce::jmax(1, getWidth() - 2));
        seq.setStepVelocity(stepIndex, juce::jlimit(0.0f, 1.0f, vel));
    }
    else
    {
        // Bottom buttons: left half = On, right half = Bind
        if (e.getPosition().getX() < getWidth() / 2)
            seq.setStepEnabled(stepIndex, !step.enabled);
        else
            seq.setStepBind(stepIndex, !step.bind);
        dragZone = 2;
    }
    repaint();
}

void SequencerPanel::StepColumn::mouseDrag(const juce::MouseEvent& e)
{
    if (!processor) return;
    auto& seq = processor->getStepSequencer();

    if (dragZone == 1)
    {
        // Note: drag up = higher
        int noteH = noteBottom();
        if (noteH < 1) return;
        float deltaY = static_cast<float>(e.getDistanceFromDragStartY());
        float deltaSemi = -deltaY / static_cast<float>(noteH) * 48.0f;
        int newNote = juce::jlimit(36, 84, juce::roundToInt(dragStartVal + deltaSemi));
        seq.setStepNote(stepIndex, newNote);
        if (noteHoldPreviewActive && newNote != noteHoldPreviewNote)
        {
            processor->updateStepHoldPreview(newNote);
            noteHoldPreviewNote = newNote;
        }
        repaint();
    }
    else if (dragZone == 3)
    {
        // Velocity: horizontal
        float vel = static_cast<float>(e.getPosition().getX() - 1)
                    / static_cast<float>(juce::jmax(1, getWidth() - 2));
        seq.setStepVelocity(stepIndex, juce::jlimit(0.0f, 1.0f, vel));
        repaint();
    }
}

void SequencerPanel::StepColumn::mouseUp(const juce::MouseEvent&)
{
    if (processor != nullptr && noteHoldPreviewActive)
        processor->endStepHoldPreview();

    noteHoldPreviewActive = false;
    noteHoldPreviewNote = -1;
    dragZone = -1;
}

// ─── SequencerPanel ────────────────────────────────────────────────

SequencerPanel::SequencerPanel(T5ynthProcessor& p)
    : processorRef(p)
{
    auto& apvts = p.getValueTreeState();

    // Section header
    paintSectionHeader(seqHeader, "SEQUENCER", kSeqCol);
    addAndMakeVisible(seqHeader);

    // ── Transport (single toggle) ──
    transportBtn.setColour(juce::TextButton::buttonColourId, kSurface);
    transportBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff4caf50));
    transportBtn.onClick = [this] {
        auto* par = processorRef.getValueTreeState().getParameter(PID::seqRunning);
        if (!par) return;
        bool playing = par->getValue() > 0.5f;
        par->setValueNotifyingHost(playing ? 0.0f : 1.0f);
        if (playing) currentStep = -1;
    };
    addAndMakeVisible(transportBtn);

    // ── Step count dropdown (2-32) ──
    for (int i = 2; i <= 32; ++i)
        stepCountBox.addItem(juce::String(i), i);
    stepCountBox.setColour(juce::ComboBox::backgroundColourId, kSurface);
    stepCountBox.setColour(juce::ComboBox::textColourId, kSeqCol);
    stepCountBox.setColour(juce::ComboBox::outlineColourId, kBorder);
    stepCountBox.onChange = [this] {
        int steps = stepCountBox.getSelectedId();
        if (steps < 2) return;
        if (auto* par = processorRef.getValueTreeState().getParameter(PID::seqSteps))
        {
            auto range = par->getNormalisableRange();
            par->setValueNotifyingHost(range.convertTo0to1(static_cast<float>(steps)));
        }
        syncStepCount();
    };
    addAndMakeVisible(stepCountBox);

    // ── Division toggle strip (hidden ComboBox for APVTS) ──
    juce::StringArray divisionItems;
    for (const auto& e : SeqDivision::kEntries) divisionItems.add(e.label);
    divisionHidden.addItemList(divisionItems, 1);
    divisionHidden.onChange = [this] {
        int id = divisionHidden.getSelectedId();
        for (int i = 0; i < kNumDivBtns; ++i)
            divBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
    };

    for (int i = 0; i < kNumDivBtns; ++i)
    {
        divBtns[i].setButtonText(divisionItems[i]);
        divBtns[i].setColour(juce::TextButton::buttonColourId, kSurface);
        divBtns[i].setColour(juce::TextButton::buttonOnColourId, kSeqCol);
        divBtns[i].setColour(juce::TextButton::textColourOffId, kDim);
        divBtns[i].setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        divBtns[i].setClickingTogglesState(true);
        divBtns[i].setRadioGroupId(2001);
        divBtns[i].onClick = [this, i] { divisionHidden.setSelectedId(i + 1); };
        addAndMakeVisible(divBtns[i]);
    }
    divA = std::make_unique<CA>(apvts, PID::seqDivision, divisionHidden);

    // ── BPM ──
    bpmRow = std::make_unique<SliderRow>("BPM", [](double v) { return juce::String(juce::roundToInt(v)); }, kSeqCol);
    addAndMakeVisible(*bpmRow);
    bpmA = std::make_unique<SA>(apvts, PID::seqBpm, bpmRow->getSlider());
    bpmRow->getSlider().onValueChange = [this] { bpmRow->updateValue(); };
    bpmRow->updateValue();

    // ── MIDI monitor ──
    midiMonitor.setColour(juce::Label::textColourId, juce::Colour(0xff4ade80));
    midiMonitor.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(midiMonitor);

    headerOverflowBtn.setColour(juce::TextButton::buttonColourId, kSurface);
    headerOverflowBtn.setColour(juce::TextButton::textColourOffId, kDim);
    headerOverflowBtn.onClick = [this] { showHeaderOverflowMenu(); };
    addAndMakeVisible(headerOverflowBtn);

    // ── Preset ──
    juce::StringArray seqPresetItems;
    for (const auto& e : SeqPreset::kEntries) seqPresetItems.add(e.label);
    presetBox.addItemList(seqPresetItems, 1);
    addAndMakeVisible(presetBox);
    presetA = std::make_unique<CA>(apvts, PID::seqPreset, presetBox);

    // Save/Load buttons for sequencer patterns (icon-based)
    for (auto* btn : { &seqSaveBtn, &seqLoadBtn })
    {
        btn->setColour(juce::TextButton::buttonColourId, kSurface);
        btn->setColour(juce::TextButton::textColourOffId, kDim);
        addAndMakeVisible(btn);
    }
    // Save icon (floppy disk outline)
    {
        juce::Path p;
        p.startNewSubPath(2.0f, 1.0f);
        p.lineTo(11.0f, 1.0f); p.lineTo(14.0f, 4.0f);
        p.lineTo(14.0f, 15.0f); p.lineTo(2.0f, 15.0f); p.closeSubPath();
        p.startNewSubPath(5.0f, 1.0f);
        p.lineTo(5.0f, 5.5f); p.lineTo(11.0f, 5.5f); p.lineTo(11.0f, 1.0f);
        p.startNewSubPath(4.0f, 9.0f);
        p.lineTo(12.0f, 9.0f); p.lineTo(12.0f, 14.0f); p.lineTo(4.0f, 14.0f); p.closeSubPath();
        saveLnf.icon = p;
    }
    // Load icon (folder outline)
    {
        juce::Path p;
        p.startNewSubPath(2.0f, 5.0f); p.lineTo(2.0f, 2.0f);
        p.lineTo(7.0f, 2.0f); p.lineTo(8.5f, 5.0f);
        p.lineTo(14.0f, 5.0f); p.lineTo(14.0f, 15.0f);
        p.lineTo(2.0f, 15.0f); p.closeSubPath();
        loadLnf.icon = p;
    }
    seqSaveBtn.setLookAndFeel(&saveLnf);
    seqLoadBtn.setLookAndFeel(&loadLnf);
    seqSaveBtn.setTooltip("Save pattern");
    seqLoadBtn.setTooltip("Load pattern");
    seqSaveBtn.onClick = [this] { savePatternAsync(); };
    seqLoadBtn.onClick = [this] { loadPatternAsync(); };

    // ── Gate ──
    gateRow = std::make_unique<SliderRow>("Gate", [](double v) { return juce::String(juce::roundToInt(v*100)) + "%"; }, kSeqCol);
    addAndMakeVisible(*gateRow);
    gateA = std::make_unique<SA>(apvts, PID::seqGate, gateRow->getSlider());
    gateRow->getSlider().onValueChange = [this] { gateRow->updateValue(); };
    gateRow->updateValue();

    // ── Shuffle ──
    shuffleRow = std::make_unique<SliderRow>("Shuffle", [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; }, kSeqCol);
    addAndMakeVisible(*shuffleRow);
    shuffleA = std::make_unique<SA>(apvts, PID::seqShuffle, shuffleRow->getSlider());
    shuffleRow->getSlider().onValueChange = [this] { shuffleRow->updateValue(); };
    shuffleRow->updateValue();

    // ── Octave shift [-2][-1][0][+1][+2] ──
    juce::StringArray seqOctItems;
    for (const auto& e : SeqOctave::kEntries) seqOctItems.add(e.label);
    octShiftHidden.addItemList(seqOctItems, 1);
    octShiftHidden.onChange = [this] {
        int id = octShiftHidden.getSelectedId();
        for (int i = 0; i < kNumOctShiftBtns; ++i)
            octShiftBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
    };
    for (int i = 0; i < kNumOctShiftBtns; ++i)
    {
        octShiftBtns[i].setButtonText(seqOctItems[i]);
        octShiftBtns[i].setColour(juce::TextButton::buttonColourId, kSurface);
        octShiftBtns[i].setColour(juce::TextButton::buttonOnColourId, kSeqCol);
        octShiftBtns[i].setColour(juce::TextButton::textColourOffId, kDim);
        octShiftBtns[i].setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        octShiftBtns[i].setClickingTogglesState(true);
        octShiftBtns[i].setRadioGroupId(2004);
        octShiftBtns[i].onClick = [this, i] { octShiftHidden.setSelectedId(i + 1); };
        addAndMakeVisible(octShiftBtns[i]);
    }
    octShiftA = std::make_unique<CA>(apvts, PID::seqOctave, octShiftHidden);

    // ── Generative sequencer controls ──
    genTransportBtn.setColour(juce::TextButton::buttonColourId, kSurface);
    genTransportBtn.setColour(juce::TextButton::buttonOnColourId, kSeqCol);
    genTransportBtn.setColour(juce::TextButton::textColourOffId, kSeqCol);
    genTransportBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    genTransportBtn.setClickingTogglesState(true);
    // No onClick needed — ButtonAttachment handles parameter sync
    addAndMakeVisible(genTransportBtn);
    genRunningA = std::make_unique<BA>(apvts, PID::genSeqRunning, genTransportBtn);

    auto intFmt = [](double v) { return juce::String(juce::roundToInt(v)); };
    genStepsRow = std::make_unique<SliderRow>("Steps", intFmt, kSeqCol);
    addAndMakeVisible(*genStepsRow);
    genStepsA = std::make_unique<SA>(apvts, PID::genSteps, genStepsRow->getSlider());
    genStepsRow->getSlider().onValueChange = [this] { genStepsRow->updateValue(); };
    genStepsRow->updateValue();

    genPulsesRow = std::make_unique<SliderRow>("Pulses", intFmt, kSeqCol);
    addAndMakeVisible(*genPulsesRow);
    genPulsesA = std::make_unique<SA>(apvts, PID::genPulses, genPulsesRow->getSlider());
    genPulsesRow->getSlider().onValueChange = [this] { genPulsesRow->updateValue(); };
    genPulsesRow->updateValue();

    genRotationRow = std::make_unique<SliderRow>("Rotation", intFmt, kSeqCol);
    addAndMakeVisible(*genRotationRow);
    genRotationA = std::make_unique<SA>(apvts, PID::genRotation, genRotationRow->getSlider());
    genRotationRow->getSlider().onValueChange = [this] { genRotationRow->updateValue(); };
    genRotationRow->updateValue();

    // Range switchbox [1][2][3][4] octaves
    juce::StringArray genRangeItems;
    for (const auto& e : GenRange::kEntries) genRangeItems.add(e.label);
    genRangeHidden.addItemList(genRangeItems, 1);
    genRangeHidden.onChange = [this] {
        int id = genRangeHidden.getSelectedId();
        for (int i = 0; i < kNumRangeBtns; ++i)
            genRangeBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
    };
    for (int i = 0; i < kNumRangeBtns; ++i)
    {
        genRangeBtns[i].setButtonText(juce::String(i + 1));
        genRangeBtns[i].setColour(juce::TextButton::buttonColourId, kSurface);
        genRangeBtns[i].setColour(juce::TextButton::buttonOnColourId, kSeqCol);
        genRangeBtns[i].setColour(juce::TextButton::textColourOffId, kDim);
        genRangeBtns[i].setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        genRangeBtns[i].setClickingTogglesState(true);
        genRangeBtns[i].setRadioGroupId(2005);
        genRangeBtns[i].onClick = [this, i] { genRangeHidden.setSelectedId(i + 1); };
        addAndMakeVisible(genRangeBtns[i]);
    }
    genRangeA = std::make_unique<CA>(apvts, PID::genRange, genRangeHidden);
    genRangeLabel.setText("Range", juce::dontSendNotification);
    genRangeLabel.setColour(juce::Label::textColourId, kDim);
    genRangeLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(genRangeLabel);

    genMutationRow = std::make_unique<SliderRow>("Evolve",
        [](double v) { return juce::String(juce::roundToInt(v * 100)) + "%"; }, kSeqCol);
    addAndMakeVisible(*genMutationRow);
    genMutationA = std::make_unique<SA>(apvts, PID::genMutation, genMutationRow->getSlider());
    genMutationRow->getSlider().onValueChange = [this] { genMutationRow->updateValue(); };
    genMutationRow->updateValue();

    // Fix toggle buttons (FIX = locked against drift)
    auto setupFixBtn = [this](juce::TextButton& btn, const juce::String& tip) {
        btn.setButtonText("FIX");
        btn.setClickingTogglesState(true);
        btn.setColour(juce::TextButton::buttonColourId, kSurface);
        btn.setColour(juce::TextButton::buttonOnColourId, kSeqCol);
        btn.setColour(juce::TextButton::textColourOffId, kDim);
        btn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn.setColour(juce::ComboBox::outlineColourId, kSeqCol.withAlpha(0.5f));
        btn.setTooltip(tip);
        addAndMakeVisible(btn);
    };
    setupFixBtn(genFixStepsBtn,    "Lock Steps against drift");
    setupFixBtn(genFixPulsesBtn,   "Lock Pulses against drift");
    setupFixBtn(genFixRotationBtn, "Lock Rotation against drift");
    setupFixBtn(genFixMutationBtn, "Lock Evolve against drift");
    genFixStepsA    = std::make_unique<BA>(apvts, PID::genFixSteps,    genFixStepsBtn);
    genFixPulsesA   = std::make_unique<BA>(apvts, PID::genFixPulses,   genFixPulsesBtn);
    genFixRotationA = std::make_unique<BA>(apvts, PID::genFixRotation, genFixRotationBtn);
    genFixMutationA = std::make_unique<BA>(apvts, PID::genFixMutation, genFixMutationBtn);

    // ── Polyphony (Phase 5): field mode + rate, 3 extra strands ──
    {
        juce::StringArray fieldModeItems;
        for (const auto& e : FieldMode::kEntries) fieldModeItems.add(e.label);
        genFieldModeBox.addItemList(fieldModeItems, 1);
        genFieldModeBox.setColour(juce::ComboBox::backgroundColourId, kSurface);
        genFieldModeBox.setColour(juce::ComboBox::textColourId, kSeqCol);
        genFieldModeBox.setColour(juce::ComboBox::outlineColourId, kBorder);
        addAndMakeVisible(genFieldModeBox);
        genFieldModeA = std::make_unique<CA>(apvts, PID::genFieldMode, genFieldModeBox);

        genFieldRateRow = std::make_unique<SliderRow>("Field",
            [](double v) { return juce::String(juce::roundToInt(v)) + " cyc"; },
            kSeqCol);
        addAndMakeVisible(*genFieldRateRow);
        genFieldRateA = std::make_unique<SA>(apvts, PID::genFieldRate, genFieldRateRow->getSlider());
        genFieldRateRow->getSlider().onValueChange = [this] { genFieldRateRow->updateValue(); };
        genFieldRateRow->updateValue();
    }
    {
        juce::StringArray roleItems;
        for (const auto& e : StrandRole::kEntries) roleItems.add(e.label);
        static const char* kEnablePIDs[kNumExtraStrands] = {
            PID::gen2Enable, PID::gen3Enable, PID::gen4Enable, PID::gen5Enable
        };
        static const char* kRolePIDs[kNumExtraStrands] = {
            PID::gen2Role, PID::gen3Role, PID::gen4Role, PID::gen5Role
        };
        static const char* kStrandLabels[kNumExtraStrands] = { "S2", "S3", "S4", "S5" };

        for (int i = 0; i < kNumExtraStrands; ++i)
        {
            strandEnableBtns[i].setButtonText(kStrandLabels[i]);
            strandEnableBtns[i].setClickingTogglesState(true);
            strandEnableBtns[i].setColour(juce::TextButton::buttonColourId,   kSurface);
            strandEnableBtns[i].setColour(juce::TextButton::buttonOnColourId, kSeqCol);
            strandEnableBtns[i].setColour(juce::TextButton::textColourOffId,  kDim);
            strandEnableBtns[i].setColour(juce::TextButton::textColourOnId,   juce::Colours::white);
            strandEnableBtns[i].setTooltip("Enable polyphonic strand " + juce::String(i + 2));
            addAndMakeVisible(strandEnableBtns[i]);
            strandEnableA[i] = std::make_unique<BA>(apvts, kEnablePIDs[i], strandEnableBtns[i]);

            strandRoleBoxes[i].addItemList(roleItems, 1);
            strandRoleBoxes[i].setColour(juce::ComboBox::backgroundColourId, kSurface);
            strandRoleBoxes[i].setColour(juce::ComboBox::textColourId, kSeqCol);
            strandRoleBoxes[i].setColour(juce::ComboBox::outlineColourId, kBorder);
            addAndMakeVisible(strandRoleBoxes[i]);
            strandRoleA[i] = std::make_unique<CA>(apvts, kRolePIDs[i], strandRoleBoxes[i]);
        }

        // Second GEN row: per-strand Div× / Octave / Dominance, one cluster
        // per strand, aligned with the [Sx][Role] cluster above.
        static const char* kDivPIDs[kNumExtraStrands] = {
            PID::gen2DivMult, PID::gen3DivMult, PID::gen4DivMult, PID::gen5DivMult
        };
        static const char* kOctPIDs[kNumExtraStrands] = {
            PID::gen2Octave, PID::gen3Octave, PID::gen4Octave, PID::gen5Octave
        };
        static const char* kDomPIDs[kNumExtraStrands] = {
            PID::gen2Dominance, PID::gen3Dominance, PID::gen4Dominance, PID::gen5Dominance
        };

        juce::StringArray divItems;
        for (const auto& e : StrandDivMult::kEntries) divItems.add(e.label);

        auto styleMiniLabel = [this](juce::Label& lbl, const juce::String& text)
        {
            lbl.setText(text, juce::dontSendNotification);
            lbl.setColour(juce::Label::textColourId, kDim);
            lbl.setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(lbl);
        };

        auto styleMiniSlider = [this](juce::Slider& s, int textBoxW, const juce::String& tooltip)
        {
            s.setSliderStyle(juce::Slider::LinearHorizontal);
            s.setTextBoxStyle(juce::Slider::TextBoxRight, false, textBoxW, 18);
            s.setColour(juce::Slider::backgroundColourId, kSurface);
            s.setColour(juce::Slider::trackColourId,      kSeqCol);
            s.setColour(juce::Slider::thumbColourId,      juce::Colours::white);
            s.setColour(juce::Slider::textBoxTextColourId,        kSeqCol);
            s.setColour(juce::Slider::textBoxBackgroundColourId,  juce::Colours::transparentBlack);
            s.setColour(juce::Slider::textBoxOutlineColourId,     juce::Colours::transparentBlack);
            s.setTooltip(tooltip);
            addAndMakeVisible(s);
        };

        for (int i = 0; i < kNumExtraStrands; ++i)
        {
            const juce::String sName = "Strand " + juce::String(i + 2);

            styleMiniLabel(strandDivLabels[i], "Div");
            strandDivBoxes[i].addItemList(divItems, 1);
            strandDivBoxes[i].setColour(juce::ComboBox::backgroundColourId, kSurface);
            strandDivBoxes[i].setColour(juce::ComboBox::textColourId, kSeqCol);
            strandDivBoxes[i].setColour(juce::ComboBox::outlineColourId, kBorder);
            strandDivBoxes[i].setTooltip(sName + " tempo multiplier (creates polymeter)");
            addAndMakeVisible(strandDivBoxes[i]);
            strandDivA[i] = std::make_unique<CA>(apvts, kDivPIDs[i], strandDivBoxes[i]);

            styleMiniLabel(strandOctLabels[i], "Oct");
            strandOctaveSliders[i].setRange(-2.0, 2.0, 1.0);
            styleMiniSlider(strandOctaveSliders[i], 22,
                sName + " octave shift (-2..+2)");
            strandOctaveA[i] = std::make_unique<SA>(apvts, kOctPIDs[i], strandOctaveSliders[i]);

            styleMiniLabel(strandDomLabels[i], "Dom");
            strandDomSliders[i].setNumDecimalPlacesToDisplay(2);
            styleMiniSlider(strandDomSliders[i], 32,
                sName + " dominance — probability of snapping to field center at the cycle downbeat (0..1)");
            strandDomA[i] = std::make_unique<SA>(apvts, kDomPIDs[i], strandDomSliders[i]);
        }
    }

    juce::StringArray scaleRootItems;
    for (const auto& e : ScaleRoot::kEntries) scaleRootItems.add(e.label);
    genScaleRootBox.addItemList(scaleRootItems, 1);
    genScaleRootBox.setColour(juce::ComboBox::backgroundColourId, kSurface);
    genScaleRootBox.setColour(juce::ComboBox::textColourId, kSeqCol);
    genScaleRootBox.setColour(juce::ComboBox::outlineColourId, kBorder);
    addAndMakeVisible(genScaleRootBox);
    genScaleRootA = std::make_unique<CA>(apvts, PID::scaleRoot, genScaleRootBox);

    juce::StringArray scaleTypeItems;
    for (const auto& e : ScaleType::kEntries) scaleTypeItems.add(e.label);
    genScaleTypeBox.addItemList(scaleTypeItems, 1);
    genScaleTypeBox.setColour(juce::ComboBox::backgroundColourId, kSurface);
    genScaleTypeBox.setColour(juce::ComboBox::textColourId, kSeqCol);
    genScaleTypeBox.setColour(juce::ComboBox::outlineColourId, kBorder);
    addAndMakeVisible(genScaleTypeBox);
    genScaleTypeA = std::make_unique<CA>(apvts, PID::scaleType, genScaleTypeBox);

    // ── Arp controls (SwitchBox: OFF/Up/Dn/U-D/Rnd) ──
    juce::StringArray arpModeItems;
    for (const auto& e : ArpMode::kEntries) arpModeItems.add(e.label);
    arpModeBox.addItemList(arpModeItems, 1);
    arpModeBox.onChange = [this] {
        int id = arpModeBox.getSelectedId();
        for (int i = 0; i < kNumModeBtns; ++i)
            arpModeBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
    };
    static const char* modeLabels[] = {"ARP off","Up","Dn","U/D","Rnd"};
    for (int i = 0; i < kNumModeBtns; ++i)
    {
        arpModeBtns[i].setButtonText(modeLabels[i]);
        arpModeBtns[i].setColour(juce::TextButton::buttonColourId, kSurface);
        arpModeBtns[i].setColour(juce::TextButton::buttonOnColourId, kSeqCol);
        arpModeBtns[i].setColour(juce::TextButton::textColourOffId, kDim);
        arpModeBtns[i].setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        arpModeBtns[i].setClickingTogglesState(true);
        arpModeBtns[i].setRadioGroupId(2003);
        arpModeBtns[i].onClick = [this, i] { arpModeBox.setSelectedId(i + 1); };
        addAndMakeVisible(arpModeBtns[i]);
    }
    arpModeA = std::make_unique<CA>(apvts, PID::arpMode, arpModeBox);

    juce::StringArray arpRateItems;
    for (const auto& e : ArpRate::kEntries) arpRateItems.add(e.label);
    arpRateBox.addItemList(arpRateItems, 1);
    addAndMakeVisible(arpRateBox);
    arpRateA = std::make_unique<CA>(apvts, PID::arpRate, arpRateBox);

    // Oct label
    arpOctLabel.setText("Oct", juce::dontSendNotification);
    arpOctLabel.setColour(juce::Label::textColourId, kDim);
    arpOctLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(arpOctLabel);

    // Arp octaves: toggle buttons [1][2][3][4] with hidden ComboBox for APVTS
    arpOctHidden.addItemList({"1","2","3","4"}, 1);
    arpOctHidden.onChange = [this] {
        int id = arpOctHidden.getSelectedId();
        for (int i = 0; i < kNumOctBtns; ++i)
            arpOctBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
    };
    for (int i = 0; i < kNumOctBtns; ++i)
    {
        arpOctBtns[i].setButtonText(juce::String(i + 1));
        arpOctBtns[i].setColour(juce::TextButton::buttonColourId, kSurface);
        arpOctBtns[i].setColour(juce::TextButton::buttonOnColourId, kSeqCol);
        arpOctBtns[i].setColour(juce::TextButton::textColourOffId, kDim);
        arpOctBtns[i].setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        arpOctBtns[i].setClickingTogglesState(true);
        arpOctBtns[i].setRadioGroupId(2002);
        arpOctBtns[i].onClick = [this, i] { arpOctHidden.setSelectedId(i + 1); };
        addAndMakeVisible(arpOctBtns[i]);
    }
    arpOctA = std::make_unique<CA>(apvts, PID::arpOctaves, arpOctHidden);

    // ── Step columns ──
    for (int i = 0; i < MAX_COLS; ++i)
    {
        stepCols[static_cast<size_t>(i)] = std::make_unique<StepColumn>();
        stepCols[static_cast<size_t>(i)]->stepIndex = i;
        stepCols[static_cast<size_t>(i)]->processor = &p;
        addAndMakeVisible(*stepCols[static_cast<size_t>(i)]);
    }

    syncStepCount();
    startTimerHz(10);
}

SequencerPanel::~SequencerPanel()
{
    stopTimer();
}

// .t5seq is a partial preset: sequencer + arpeggiator + generative seq only.
// Schema is the snake_case-keyed subset of exportJsonPreset()/importJsonPreset()
// — single source of truth, no schema drift.
void SequencerPanel::savePatternAsync()
{
    auto chooser = std::make_shared<juce::FileChooser>("Save Sequencer Pattern", juce::File(), "*.t5seq");
    juce::Component::SafePointer<SequencerPanel> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode, [safeThis, chooser](const juce::FileChooser& fc) {
        if (!safeThis) return;
        auto& processor = safeThis->processorRef;
        auto f = fc.getResult();
        if (f == juce::File()) return;

        auto fullJson = processor.exportJsonPreset();
        auto parsed = juce::JSON::parse(fullJson);
        auto* full = parsed.getDynamicObject();
        if (!full) return;

        juce::DynamicObject::Ptr out = new juce::DynamicObject();
        out->setProperty("version", 1);
        out->setProperty("kind", "t5seq");
        out->setProperty("timestamp", juce::Time::getCurrentTime().toISO8601(true));

        for (const char* key : { "sequencer", "arpeggiator", "generativeSeq" })
            if (full->hasProperty(key))
                out->setProperty(key, full->getProperty(key));

        auto file = f.withFileExtension("t5seq");
        file.replaceWithText(juce::JSON::toString(out.get(), true));
    });
}

void SequencerPanel::loadPatternAsync()
{
    auto chooser = std::make_shared<juce::FileChooser>("Load Sequencer Pattern", juce::File(), "*.t5seq");
    juce::Component::SafePointer<SequencerPanel> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode, [safeThis, chooser](const juce::FileChooser& fc) {
        if (!safeThis) return;
        auto file = fc.getResult();
        if (!file.existsAsFile()) return;

        // importJsonPreset is tolerant: missing sub-objects (synth, engine, …)
        // are skipped, so a partial .t5seq only touches sequencer / arp / gen.
        if (!safeThis->processorRef.importJsonPreset(file.loadFileAsString()))
            return;

        safeThis->syncStepCount();
        safeThis->repaint();
    });
}

void SequencerPanel::showHeaderOverflowMenu()
{
    juce::PopupMenu menu;

    juce::PopupMenu presetMenu;
    for (int i = 0; i < presetBox.getNumItems(); ++i)
        presetMenu.addItem(kOverflowPresetBase + i + 1, presetBox.getItemText(i), true,
                           presetBox.getSelectedId() == i + 1);
    menu.addSubMenu("Preset", presetMenu);

    juce::PopupMenu stepMenu;
    for (int steps = 2; steps <= 32; ++steps)
        stepMenu.addItem(kOverflowStepBase + steps, juce::String(steps), true,
                         stepCountBox.getSelectedId() == steps);
    menu.addSubMenu("Steps", stepMenu);

    juce::PopupMenu divisionMenu;
    for (int i = 0; i < divisionHidden.getNumItems(); ++i)
        divisionMenu.addItem(kOverflowDivisionBase + i + 1, divisionHidden.getItemText(i), true,
                             divisionHidden.getSelectedId() == i + 1);
    menu.addSubMenu("Division", divisionMenu);

    juce::PopupMenu octaveMenu;
    for (int i = 0; i < octShiftHidden.getNumItems(); ++i)
        octaveMenu.addItem(kOverflowOctaveBase + i + 1, octShiftHidden.getItemText(i), true,
                           octShiftHidden.getSelectedId() == i + 1);
    menu.addSubMenu("Octave", octaveMenu);

    menu.addSeparator();
    menu.addItem(kOverflowSavePattern, "Save Pattern...");
    menu.addItem(kOverflowLoadPattern, "Load Pattern...");

    juce::Component::SafePointer<SequencerPanel> safeThis(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&headerOverflowBtn),
                       [safeThis](int result) {
        if (!safeThis || result == 0) return;

        if (result == kOverflowSavePattern) { safeThis->savePatternAsync(); return; }
        if (result == kOverflowLoadPattern) { safeThis->loadPatternAsync(); return; }
        if (result >= kOverflowPresetBase && result < kOverflowStepBase)
        {
            safeThis->presetBox.setSelectedId(result - kOverflowPresetBase, juce::sendNotificationSync);
            return;
        }
        if (result >= kOverflowStepBase && result < kOverflowDivisionBase)
        {
            safeThis->stepCountBox.setSelectedId(result - kOverflowStepBase, juce::sendNotificationSync);
            return;
        }
        if (result >= kOverflowDivisionBase && result < kOverflowOctaveBase)
        {
            safeThis->divisionHidden.setSelectedId(result - kOverflowDivisionBase, juce::sendNotificationSync);
            return;
        }
        if (result >= kOverflowOctaveBase && result < kOverflowSavePattern)
            safeThis->octShiftHidden.setSelectedId(result - kOverflowOctaveBase, juce::sendNotificationSync);
    });
}

void SequencerPanel::syncStepCount()
{
    int steps = static_cast<int>(processorRef.getValueTreeState()
                    .getRawParameterValue(PID::seqSteps)->load());
    numVisibleSteps = juce::jlimit(2, MAX_COLS, steps);

    for (int i = 0; i < MAX_COLS; ++i)
        stepCols[static_cast<size_t>(i)]->setVisible(i < numVisibleSteps);

    stepCountBox.setSelectedId(numVisibleSteps, juce::dontSendNotification);
    resized();
}

void SequencerPanel::timerCallback()
{
    bool genRunning = processorRef.getValueTreeState()
        .getRawParameterValue(PID::genSeqRunning)->load() > 0.5f;
    bool seqRunning = processorRef.getValueTreeState()
        .getRawParameterValue(PID::seqRunning)->load() > 0.5f;

    // Mode changes must update the layout even while audio is idle. Otherwise
    // GEN-off stays visually in gen mode until PLAY wakes the timer path.
    if (genRunning != genModeActive)
    {
        resized();
        repaint();
    }

    // Skip expensive updates when audio is idle and neither sequencer runs
    bool seqIdle = processorRef.audioIdle.load(std::memory_order_relaxed)
                   && !seqRunning && !genRunning;
    if (seqIdle)
        return;

    if (genRunning)
    {
        // Gen-Seq step highlight
        int gs = processorRef.getGenerativeSequencer().currentStepForGui.load(std::memory_order_relaxed);
        if (gs != genCurrentStep)
        {
            genCurrentStep = gs;
            repaint(); // repaint visualization
        }

        // GEN button stays "GEN" — toggle state shown via color (on/off)
    }
    else
    {
        genCurrentStep = -1;

        // Step highlight
        int step = processorRef.getStepSequencer().currentStepForGui.load(std::memory_order_relaxed);
        if (step != currentStep)
        {
            currentStep = step;
            for (int i = 0; i < MAX_COLS; ++i)
                stepCols[static_cast<size_t>(i)]->isCurrentStep = (i == currentStep);
        }
    }

    // Transport button state (step seq)
    transportBtn.setButtonText(seqRunning ? "STOP" : "PLAY");
    transportBtn.setColour(juce::TextButton::buttonColourId,
                           seqRunning ? kSeqCol.darker(0.3f) : kSurface);
    transportBtn.setColour(juce::TextButton::textColourOffId,
                           seqRunning ? juce::Colours::white : juce::Colour(0xff4caf50));

    // MIDI monitor
    int note = processorRef.lastMidiNote.load(std::memory_order_relaxed);
    bool on  = processorRef.lastMidiNoteOn.load(std::memory_order_relaxed);
    int vel  = processorRef.lastMidiVelocity.load(std::memory_order_relaxed);
    if (note >= 0)
    {
        auto txt = on ? (noteName(note) + " v" + juce::String(vel))
                      : (noteName(note) + " off");
        midiMonitor.setText(txt, juce::dontSendNotification);
        midiMonitor.setColour(juce::Label::textColourId, on ? juce::Colour(0xff4ade80) : kDim);
        repaint(); // repaint for MIDI LED
    }

    // Sync step count if changed externally (step seq mode)
    if (!genRunning)
    {
        int steps = static_cast<int>(processorRef.getValueTreeState()
                        .getRawParameterValue(PID::seqSteps)->load());
        if (steps != numVisibleSteps)
            syncStepCount();
    }

    // Repaint only steps that changed (current + previous)
    static int prevStep = -1;
    if (!genRunning && currentStep != prevStep)
    {
        if (prevStep >= 0 && prevStep < MAX_COLS)
            stepCols[static_cast<size_t>(prevStep)]->repaint();
        if (currentStep >= 0 && currentStep < MAX_COLS)
            stepCols[static_cast<size_t>(currentStep)]->repaint();
        prevStep = currentStep;
    }
}

// kSeqCol is now a global in GuiHelpers.h

void SequencerPanel::paint(juce::Graphics& g)
{
    g.fillAll(kCard);

    // MIDI activity LED (next to note display)
    if (!midiLedBounds.isEmpty())
    {
        bool noteOn = processorRef.lastMidiNoteOn.load(std::memory_order_relaxed);
        g.setColour(noteOn ? juce::Colour(0xff4ade80) : kDimmer);
        g.fillEllipse(midiLedBounds);
    }

    // Separator above step grid
    if (!gridArea.isEmpty())
    {
        g.setColour(kBorder);
        g.drawHorizontalLine(gridArea.getY() - 1,
                             static_cast<float>(gridArea.getX()),
                             static_cast<float>(gridArea.getRight()));
    }

    // Separator above Arp row
    int arpY = arpModeBtns[0].getY();
    if (arpY > 0)
    {
        g.setColour(kBorder);
        g.drawHorizontalLine(arpY - 2, 6.0f, static_cast<float>(getWidth() - 6));
    }

    // ═══ Gen-Seq visualization ═══
    if (genModeActive && !genVisArea.isEmpty())
    {
        auto& genSeq = processorRef.getGenerativeSequencer();
        int numS = genSeq.numStepsForGui.load(std::memory_order_relaxed);
        if (numS < 1) numS = 1;

        float areaW = static_cast<float>(genVisArea.getWidth());
        float areaH = static_cast<float>(genVisArea.getHeight());
        float areaX = static_cast<float>(genVisArea.getX());
        float areaY = static_cast<float>(genVisArea.getY());
        float stepW = areaW / static_cast<float>(numS);

        // Draw steps
        for (int i = 0; i < numS; ++i)
        {
            float x = areaX + static_cast<float>(i) * stepW;
            int midiNote = genSeq.notePatternForGui[static_cast<size_t>(i)].load(std::memory_order_relaxed);
            bool isPulse = midiNote > 0;
            bool isCurrent = (i == genCurrentStep);

            // Step background
            if (isCurrent)
                g.setColour(kSeqCol.withAlpha(0.35f));
            else if (isPulse)
                g.setColour(kSurface);
            else
                g.setColour(kBg);
            g.fillRect(x + 1.0f, areaY, stepW - 2.0f, areaH);

            // Beat group border
            if (i % 4 == 0)
            {
                g.setColour(kBorder);
                g.drawLine(x, areaY, x, areaY + areaH, 1.0f);
            }

            if (isPulse)
            {
                // Note bar — height represents pitch (36-96 range)
                float frac = juce::jlimit(0.0f, 1.0f, static_cast<float>(midiNote - 36) / 60.0f);
                float barH = frac * (areaH - 20.0f);
                g.setColour(isCurrent ? kSeqCol : kSeqCol.withAlpha(0.6f));
                g.fillRect(x + 3.0f, areaY + areaH - 14.0f - barH,
                           stepW - 6.0f, barH);

                // Note name
                g.setColour(juce::Colours::white.withAlpha(isCurrent ? 1.0f : 0.7f));
                float fs = juce::jlimit(8.0f, 12.0f, stepW * 0.3f);
                g.setFont(juce::FontOptions(fs));
                g.drawText(noteName(midiNote),
                           juce::Rectangle<float>(x, areaY + areaH - 14.0f, stepW, 14.0f),
                           juce::Justification::centred);
            }

            // Pulse indicator dot at top
            float dotSize = juce::jlimit(4.0f, 8.0f, stepW * 0.2f);
            float dotX = x + stepW * 0.5f - dotSize * 0.5f;
            g.setColour(isPulse ? kSeqCol : kDimmer.withAlpha(0.3f));
            g.fillEllipse(dotX, areaY + 3.0f, dotSize, dotSize);
        }
    }
}

void SequencerPanel::resized()
{
    auto area = getLocalBounds().reduced(6, 0);
    float topH = getTopLevelComponent()
                     ? static_cast<float>(getTopLevelComponent()->getHeight()) : 800.0f;
    int headerH = juce::jlimit(14, 20, juce::roundToInt(topH * 0.022f));
    seqHeader.setFont(juce::FontOptions(static_cast<float>(headerH) * 0.85f));
    seqHeader.setBounds(area.removeFromTop(headerH));
    area.removeFromTop(juce::jmax(3, headerH / 5));

    int rH = 22;
    int g = 3;
    const int panelW = getWidth();
    const bool compactTopRow = panelW < 760;

    // ═══ Row 1: ALWAYS the same — transport, GEN, preset, save/load, steps, division, etc. ═══
    auto r1 = area.removeFromTop(rH);
    auto setOctShiftVisible = [this](bool visible)
    {
        for (int i = 0; i < kNumOctShiftBtns; ++i)
        {
            octShiftBtns[i].setVisible(visible);
            if (!visible)
                octShiftBtns[i].setBounds({});
        }
    };
    auto setDivVisible = [this](bool visible)
    {
        for (int i = 0; i < kNumDivBtns; ++i)
        {
            divBtns[i].setVisible(visible);
            if (!visible)
                divBtns[i].setBounds({});
        }
    };

    const float midiFont = compactTopRow ? kUiValueFontMin : juce::jmax(kUiValueFontMin,
                                                                         static_cast<float>(rH) * 0.6f);
    const int midiTextW = measureTextWidth("D#4 v127", midiFont) + 8;
    const int midiLedW = compactTopRow ? 10 : 14;
    const int midiGap = compactTopRow ? 2 : 5;

    auto layoutMidiCluster = [this, compactTopRow, midiTextW, midiLedW, midiFont](juce::Rectangle<int> areaForMidi)
    {
        midiMonitor.setVisible(true);
        midiMonitor.setFont(juce::FontOptions(midiFont));
        midiLedBounds = {};
        if (areaForMidi.getWidth() >= midiLedW + 8)
        {
            auto textArea = areaForMidi.removeFromRight(juce::jmin(midiTextW, areaForMidi.getWidth()));
            midiMonitor.setBounds(textArea);
            if (areaForMidi.getWidth() > 0)
            {
                auto ledArea = areaForMidi.removeFromRight(juce::jmin(midiLedW, areaForMidi.getWidth()));
                const float dotSize = compactTopRow ? 7.0f : 8.0f;
                midiLedBounds = juce::Rectangle<float>(dotSize, dotSize)
                    .withCentre({ static_cast<float>(ledArea.getCentreX()),
                                  static_cast<float>(textArea.getCentreY()) });
            }
        }
        else
        {
            midiMonitor.setBounds(areaForMidi);
        }
    };

    enum HeaderSlot
    {
        slotTransport,
        slotGenTransport,
        slotPreset,
        slotSave,
        slotLoad,
        slotSteps,
        slotDivision,
        slotOctave,
        slotBpm,
        slotGate,
        slotShuffle,
        slotMidi
    };

    const int transportW = 36;
    const int compactTierWidth = compactTopRow ? 72 : 90;
    const int compactStepWidth = compactTopRow ? 42 : 52;
    const int iconW = compactTopRow ? 18 : rH;
    const int divisionPrefW = kNumDivBtns * (compactTopRow ? 24 : 28);
    const int divisionMinW = kNumDivBtns * (compactTopRow ? 21 : 24);
    const int octavePrefW = kNumOctShiftBtns * (compactTopRow ? 20 : 24);
    const int octaveMinW = kNumOctShiftBtns * (compactTopRow ? 18 : 20);
    const int midiClusterW = midiTextW + midiLedW + midiGap;
    const int gateMinW = gateRow->getMinimumWidth();
    const int gatePrefW = gateMinW;
    const int shuffleMinW = shuffleRow->getMinimumWidth();
    const int shufflePrefW = shuffleMinW;

    std::vector<ResponsiveStripItem> items {
        { transportW, transportW, 0, false, ResponsiveStripFallback::none },
        { transportW, transportW, 0, false, ResponsiveStripFallback::none },
        { compactTierWidth, 72, 1, false, ResponsiveStripFallback::overflow },
        { iconW, iconW, 1, false, ResponsiveStripFallback::overflow },
        { iconW, iconW, 1, false, ResponsiveStripFallback::overflow },
        { compactStepWidth, 38, 1, false, ResponsiveStripFallback::overflow },
        { divisionPrefW, divisionMinW, 1, false, ResponsiveStripFallback::overflow },
        { octavePrefW, octaveMinW, 1, false, ResponsiveStripFallback::overflow },
        { bpmRow->getPreferredWidth(),  bpmRow->getMinimumWidth(),  0, true,  ResponsiveStripFallback::none },
        { gatePrefW, gateMinW, 0, false, ResponsiveStripFallback::none },
        { shufflePrefW, shuffleMinW, 0, false, ResponsiveStripFallback::none },
        { midiClusterW, midiClusterW, 0, false, ResponsiveStripFallback::none }
    };

    auto headerLayout = layoutResponsiveStrip(r1, items, g, compactTopRow ? 24 : 28);
    const auto hasBounds = [](juce::Rectangle<int> bounds) { return !bounds.isEmpty(); };

    transportBtn.setBounds(headerLayout.bounds[slotTransport]);
    genTransportBtn.setBounds(headerLayout.bounds[slotGenTransport]);
    bpmRow->setBounds(headerLayout.bounds[slotBpm]);
    gateRow->setBounds(headerLayout.bounds[slotGate]);
    shuffleRow->setBounds(headerLayout.bounds[slotShuffle]);
    layoutMidiCluster(headerLayout.bounds[slotMidi]);

    headerOverflowBtn.setVisible(headerLayout.overflowUsed);
    headerOverflowBtn.setBounds(headerLayout.overflowUsed ? headerLayout.overflowBounds : juce::Rectangle<int>{});

    presetBox.setVisible(hasBounds(headerLayout.bounds[slotPreset]));
    presetBox.setBounds(headerLayout.bounds[slotPreset]);
    stepCountBox.setVisible(hasBounds(headerLayout.bounds[slotSteps]));
    stepCountBox.setBounds(headerLayout.bounds[slotSteps]);
    seqSaveBtn.setVisible(hasBounds(headerLayout.bounds[slotSave]));
    seqSaveBtn.setBounds(headerLayout.bounds[slotSave]);
    seqLoadBtn.setVisible(hasBounds(headerLayout.bounds[slotLoad]));
    seqLoadBtn.setBounds(headerLayout.bounds[slotLoad]);

    setDivVisible(hasBounds(headerLayout.bounds[slotDivision]));
    if (hasBounds(headerLayout.bounds[slotDivision]))
    {
        auto divArea = headerLayout.bounds[slotDivision];
        const int divBtnW = divArea.getWidth() / kNumDivBtns;
        for (int i = 0; i < kNumDivBtns; ++i)
        {
            int edges = 0;
            if (i > 0) edges |= juce::Button::ConnectedOnLeft;
            if (i < kNumDivBtns - 1) edges |= juce::Button::ConnectedOnRight;
            divBtns[i].setConnectedEdges(edges);
            divBtns[i].setBounds(divArea.removeFromLeft(i == kNumDivBtns - 1 ? divArea.getWidth() : divBtnW));
        }
    }

    setOctShiftVisible(hasBounds(headerLayout.bounds[slotOctave]));
    if (hasBounds(headerLayout.bounds[slotOctave]))
    {
        auto octArea = headerLayout.bounds[slotOctave];
        const int octBtnW = octArea.getWidth() / kNumOctShiftBtns;
        for (int i = 0; i < kNumOctShiftBtns; ++i)
        {
            int edges = 0;
            if (i > 0) edges |= juce::Button::ConnectedOnLeft;
            if (i < kNumOctShiftBtns - 1) edges |= juce::Button::ConnectedOnRight;
            octShiftBtns[i].setConnectedEdges(edges);
            octShiftBtns[i].setBounds(octArea.removeFromLeft(i == kNumOctShiftBtns - 1 ? octArea.getWidth() : octBtnW));
        }
    }

    area.removeFromTop(compactTopRow ? 5 : g);

    // ═══ Row 4 (bottom): Arp controls ═══
    auto r4 = area.removeFromBottom(rH);
    const int modeBtnW = 32;
    const int offBtnW  = 58;   // first button reads "ARP off" — needs extra room
    for (int i = 0; i < kNumModeBtns; ++i)
    {
        int edges = 0;
        if (i > 0) edges |= juce::Button::ConnectedOnLeft;
        if (i < kNumModeBtns - 1) edges |= juce::Button::ConnectedOnRight;
        arpModeBtns[i].setConnectedEdges(edges);
        arpModeBtns[i].setBounds(r4.removeFromLeft(i == 0 ? offBtnW : modeBtnW));
    }
    r4.removeFromLeft(g);
    arpRateBox.setBounds(r4.removeFromLeft(60));   r4.removeFromLeft(g);
    arpOctLabel.setFont(juce::FontOptions(juce::jmax(kUiLabelFontMin, rH * 0.55f)));
    arpOctLabel.setBounds(r4.removeFromLeft(28));   r4.removeFromLeft(2);
    int arpOctBtnW = 22;
    for (int i = 0; i < kNumOctBtns; ++i)
    {
        int edges = 0;
        if (i > 0) edges |= juce::Button::ConnectedOnLeft;
        if (i < kNumOctBtns - 1) edges |= juce::Button::ConnectedOnRight;
        arpOctBtns[i].setConnectedEdges(edges);
        arpOctBtns[i].setBounds(r4.removeFromLeft(arpOctBtnW));
    }
    area.removeFromBottom(g);

    // ═══ Determine mode ═══
    genModeActive = processorRef.getValueTreeState()
        .getRawParameterValue(PID::genSeqRunning)->load() > 0.5f;

    // Visibility: step grid vs gen controls (Row 1 stays ALWAYS visible)
    for (int i = 0; i < MAX_COLS; ++i)
        stepCols[static_cast<size_t>(i)]->setVisible(!genModeActive && i < numVisibleSteps);

    genStepsRow->setVisible(genModeActive);
    genPulsesRow->setVisible(genModeActive);
    genRotationRow->setVisible(genModeActive);
    genMutationRow->setVisible(genModeActive);
    genScaleRootBox.setVisible(genModeActive);
    genScaleTypeBox.setVisible(genModeActive);
    genRangeLabel.setVisible(genModeActive);
    for (int i = 0; i < kNumRangeBtns; ++i)
        genRangeBtns[i].setVisible(genModeActive);
    genFixStepsBtn.setVisible(genModeActive);
    genFixPulsesBtn.setVisible(genModeActive);
    genFixRotationBtn.setVisible(genModeActive);
    genFixMutationBtn.setVisible(genModeActive);
    genFieldModeBox.setVisible(genModeActive);
    if (genFieldRateRow) genFieldRateRow->setVisible(genModeActive);
    for (int i = 0; i < kNumExtraStrands; ++i)
    {
        strandEnableBtns[i].setVisible(genModeActive);
        strandRoleBoxes[i].setVisible(genModeActive);
        strandDivBoxes[i].setVisible(genModeActive);
        strandOctaveSliders[i].setVisible(genModeActive);
        strandDomSliders[i].setVisible(genModeActive);
        strandDivLabels[i].setVisible(genModeActive);
        strandOctLabels[i].setVisible(genModeActive);
        strandDomLabels[i].setVisible(genModeActive);
    }

    if (genModeActive)
    {
        // ═══ Gen mode: 2-column grid with fix buttons ═══
        int genCtrlH = rH;
        int colGap = 4;
        int fixW = 28;
        int colW = (area.getWidth() - colGap) / 2;
        int sliderW = colW - fixW;

        // Row 1:  Steps [====] 21 [FIX]  |  Pulses [====] 16 [FIX]
        auto row1 = area.removeFromTop(genCtrlH);
        genStepsRow->setBounds(row1.removeFromLeft(sliderW));
        genFixStepsBtn.setBounds(row1.removeFromLeft(fixW));
        row1.removeFromLeft(colGap);
        genPulsesRow->setBounds(row1.removeFromLeft(sliderW));
        genFixPulsesBtn.setBounds(row1.removeFromLeft(fixW));
        area.removeFromTop(2);

        // Row 2:  Rotation [====] 2 [FIX]  |  Evolve [====] 80% [FIX]
        auto row2 = area.removeFromTop(genCtrlH);
        genRotationRow->setBounds(row2.removeFromLeft(sliderW));
        genFixRotationBtn.setBounds(row2.removeFromLeft(fixW));
        row2.removeFromLeft(colGap);
        genMutationRow->setBounds(row2.removeFromLeft(sliderW));
        genFixMutationBtn.setBounds(row2.removeFromLeft(fixW));
        area.removeFromTop(2);

        // ── 2-column / 2-row GEN block ──
        //   Left column (narrow):
        //     Row L1: [C▾] [DblHarm▾] [Rng][1][2][3][4]
        //     Row L2: [Mode▾] [Field== 12cyc]
        //   Right column (wide): four strand modules side by side, each one
        //     a 2-row vertical block — top row is the existing [Sx][Role▾]
        //     pair, bottom row is [Div: combo] [Oct: lbl+slider] [Dom: lbl+slider].
        //   Generous horizontal padding between strand modules visually
        //   delimits each strand without a separator graphic.
        {
            const int intraGap = 2;
            const int colGap   = 12;
            const int blockH   = 2 * genCtrlH + intraGap;
            auto block = area.removeFromTop(blockH);

            const int leftW = juce::jlimit(360, 540, block.getWidth() / 5);  // ~20%, clamped
            auto leftCol  = block.removeFromLeft(leftW);
            block.removeFromLeft(colGap);
            auto rightCol = block;

            // ── Left column — Row L1: Scale + Range ──
            {
                auto rowL1 = leftCol.removeFromTop(genCtrlH);
                const int rootW   = 55;
                const int scaleW  = 100;
                const int rngLblW = 32;
                const int rngBtnW = 22;
                const int gapTiny = 2;
                const int gapMid  = 8;
                genScaleRootBox.setBounds(rowL1.removeFromLeft(rootW));  rowL1.removeFromLeft(gapTiny);
                genScaleTypeBox.setBounds(rowL1.removeFromLeft(scaleW)); rowL1.removeFromLeft(gapMid);
                genRangeLabel.setText("Rng", juce::dontSendNotification);
                genRangeLabel.setFont(juce::FontOptions(juce::jmax(kUiLabelFontMin, genCtrlH * 0.55f)));
                genRangeLabel.setBounds(rowL1.removeFromLeft(rngLblW)); rowL1.removeFromLeft(gapTiny);
                for (int i = 0; i < kNumRangeBtns; ++i)
                {
                    int edges = 0;
                    if (i > 0) edges |= juce::Button::ConnectedOnLeft;
                    if (i < kNumRangeBtns - 1) edges |= juce::Button::ConnectedOnRight;
                    genRangeBtns[i].setConnectedEdges(edges);
                    genRangeBtns[i].setBounds(rowL1.removeFromLeft(rngBtnW));
                }
            }
            leftCol.removeFromTop(intraGap);

            // ── Left column — Row L2: Field Mode + Field Rate ──
            {
                auto rowL2 = leftCol;   // remaining height = genCtrlH
                const int modeW  = 95;
                const int gapSm  = 4;
                genFieldModeBox.setBounds(rowL2.removeFromLeft(modeW));
                rowL2.removeFromLeft(gapSm);
                if (genFieldRateRow) genFieldRateRow->setBounds(rowL2);
            }

            // ── Right column — 4 strand modules side by side ──
            {
                const int moduleGap = 16;
                const int moduleW = (rightCol.getWidth() - (kNumExtraStrands - 1) * moduleGap)
                                   / kNumExtraStrands;
                for (int i = 0; i < kNumExtraStrands; ++i)
                {
                    auto module = rightCol.removeFromLeft(moduleW);
                    if (i < kNumExtraStrands - 1) rightCol.removeFromLeft(moduleGap);

                    // Top row: [Sx][Role▾]
                    auto modTop = module.removeFromTop(genCtrlH);
                    const int onW    = 28;
                    const int gapInm = 2;
                    strandEnableBtns[i].setBounds(modTop.removeFromLeft(onW));
                    modTop.removeFromLeft(gapInm);
                    strandRoleBoxes[i].setBounds(modTop);

                    module.removeFromTop(intraGap);

                    // Bottom row: [Div lbl + combo]  [Oct lbl + slider]  [Dom lbl + slider]
                    auto modBot = module;   // remaining height = genCtrlH
                    const int lblW   = 28;
                    const int divW   = 56;       // combo "1/4x" max
                    const int gapPad = 6;        // between control groups
                    const int gapInGroup = 2;    // between label and its control
                    // Remaining for Oct + Dom sliders, split 1:2 (Dom benefits from more travel)
                    const int remaining = modBot.getWidth()
                                         - (lblW + gapInGroup + divW + gapPad
                                            + lblW + gapInGroup + lblW + gapInGroup + gapPad);
                    const int octSliderW = juce::jmax(50, remaining / 3);
                    const int domSliderW = juce::jmax(70, remaining - octSliderW);

                    strandDivLabels[i].setBounds(modBot.removeFromLeft(lblW));
                    modBot.removeFromLeft(gapInGroup);
                    strandDivBoxes[i].setBounds(modBot.removeFromLeft(divW));
                    modBot.removeFromLeft(gapPad);

                    strandOctLabels[i].setBounds(modBot.removeFromLeft(lblW));
                    modBot.removeFromLeft(gapInGroup);
                    strandOctaveSliders[i].setBounds(modBot.removeFromLeft(octSliderW));
                    modBot.removeFromLeft(gapPad);

                    strandDomLabels[i].setBounds(modBot.removeFromLeft(lblW));
                    modBot.removeFromLeft(gapInGroup);
                    strandDomSliders[i].setBounds(modBot);
                }
            }
            area.removeFromTop(g);
        }

        // Visualization area (remaining space)
        genVisArea = area;
        gridArea = {};
    }
    else
    {
        // ═══ Step mode: step grid ═══
        genVisArea = {};
        gridArea = area;
        if (numVisibleSteps > 0 && gridArea.getWidth() > numVisibleSteps)
        {
            int stepW = gridArea.getWidth() / numVisibleSteps;
            for (int i = 0; i < MAX_COLS; ++i)
            {
                if (i < numVisibleSteps)
                    stepCols[static_cast<size_t>(i)]->setBounds(
                        gridArea.getX() + i * stepW, gridArea.getY(),
                        stepW, gridArea.getHeight());
            }
        }
    }
}
