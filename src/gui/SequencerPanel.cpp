#include "SequencerPanel.h"
#include "../PluginProcessor.h"

// ─── Note name helper ──────────────────────────────────────────────
static juce::String noteName(int n)
{
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    int oct = n / 12 - 1;
    return juce::String(names[n % 12]) + juce::String(oct);
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
        g.drawText(noteName(semi), noteR, juce::Justification::centredTop);
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
        auto* par = processorRef.getValueTreeState().getParameter("seq_running");
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
        if (auto* par = processorRef.getValueTreeState().getParameter("seq_steps"))
        {
            auto range = par->getNormalisableRange();
            par->setValueNotifyingHost(range.convertTo0to1(static_cast<float>(steps)));
        }
        syncStepCount();
    };
    addAndMakeVisible(stepCountBox);

    // ── Division toggle strip (hidden ComboBox for APVTS) ──
    divisionHidden.addItemList({"1/1", "1/2", "1/4", "1/8", "1/16"}, 1);
    divisionHidden.onChange = [this] {
        int id = divisionHidden.getSelectedId();
        for (int i = 0; i < kNumDivBtns; ++i)
            divBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
    };

    static const char* divLabels[] = {"1/1","1/2","1/4","1/8","1/16"};
    for (int i = 0; i < kNumDivBtns; ++i)
    {
        divBtns[i].setButtonText(divLabels[i]);
        divBtns[i].setColour(juce::TextButton::buttonColourId, kSurface);
        divBtns[i].setColour(juce::TextButton::buttonOnColourId, kSeqCol);
        divBtns[i].setColour(juce::TextButton::textColourOffId, kDim);
        divBtns[i].setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        divBtns[i].setClickingTogglesState(true);
        divBtns[i].setRadioGroupId(2001);
        divBtns[i].onClick = [this, i] { divisionHidden.setSelectedId(i + 1); };
        addAndMakeVisible(divBtns[i]);
    }
    divA = std::make_unique<CA>(apvts, "seq_division", divisionHidden);

    // ── BPM ──
    bpmRow = std::make_unique<SliderRow>("BPM", [](double v) { return juce::String(juce::roundToInt(v)); }, kSeqCol);
    addAndMakeVisible(*bpmRow);
    bpmA = std::make_unique<SA>(apvts, "seq_bpm", bpmRow->getSlider());
    bpmRow->getSlider().onValueChange = [this] { bpmRow->updateValue(); };
    bpmRow->updateValue();

    // ── MIDI monitor ──
    midiMonitor.setColour(juce::Label::textColourId, juce::Colour(0xff4ade80));
    midiMonitor.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(midiMonitor);

    // ── Preset ──
    presetBox.addItemList({"Octave Bounce","Wide Leap","Off-Beat Minor","Glide Groove","Sparse Stab",
                           "Rising Arc","Scatter","Chromatic","Bass Walk","Gated Pulse"}, 1);
    addAndMakeVisible(presetBox);
    presetA = std::make_unique<CA>(apvts, "seq_preset", presetBox);

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
    seqSaveBtn.onClick = [this] {
        auto chooser = std::make_shared<juce::FileChooser>("Save Sequencer Pattern", juce::File(), "*.t5seq");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode, [this, chooser](const juce::FileChooser& fc) {
            auto f = fc.getResult();
            if (f == juce::File()) return;
            auto file = f.withFileExtension("t5seq");
            auto& seq = processorRef.getStepSequencer();
            juce::var root(new juce::DynamicObject());
            auto* obj = root.getDynamicObject();
            obj->setProperty("numSteps", seq.getNumSteps());
            obj->setProperty("division", static_cast<int>(processorRef.getValueTreeState().getRawParameterValue("seq_division")->load()));
            obj->setProperty("gate", static_cast<double>(processorRef.getValueTreeState().getRawParameterValue("seq_gate")->load()));
            obj->setProperty("bpm", static_cast<double>(processorRef.getValueTreeState().getRawParameterValue("seq_bpm")->load()));
            // Arp settings
            obj->setProperty("arpMode", static_cast<int>(processorRef.getValueTreeState().getRawParameterValue("arp_mode")->load()));
            obj->setProperty("arpRate", static_cast<int>(processorRef.getValueTreeState().getRawParameterValue("arp_rate")->load()));
            obj->setProperty("arpOctaves", static_cast<int>(processorRef.getValueTreeState().getRawParameterValue("arp_octaves")->load()));
            obj->setProperty("octave", static_cast<int>(processorRef.getValueTreeState().getRawParameterValue("seq_octave")->load()));
            juce::Array<juce::var> steps;
            for (int i = 0; i < seq.getNumSteps(); ++i)
            {
                auto step = seq.getStep(i);
                auto* s = new juce::DynamicObject();
                s->setProperty("note", step.note);
                s->setProperty("velocity", static_cast<double>(step.velocity));
                s->setProperty("gate", static_cast<double>(step.gate));
                s->setProperty("enabled", step.enabled);
                s->setProperty("bind", step.bind);
                steps.add(juce::var(s));
            }
            obj->setProperty("steps", steps);
            file.replaceWithText(juce::JSON::toString(root));
        });
    };
    seqLoadBtn.onClick = [this] {
        auto chooser = std::make_shared<juce::FileChooser>("Load Sequencer Pattern", juce::File(), "*.t5seq");
        chooser->launchAsync(juce::FileBrowserComponent::openMode, [this, chooser](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (!file.existsAsFile()) return;
            auto root = juce::JSON::parse(file.loadFileAsString());
            if (!root.isObject()) return;
            auto& apvts = processorRef.getValueTreeState();
            auto& seq = processorRef.getStepSequencer();
            if (root.hasProperty("numSteps"))
                seq.setNumSteps(static_cast<int>(root["numSteps"]));
            if (root.hasProperty("division"))
                apvts.getParameter("seq_division")->setValueNotifyingHost(
                    apvts.getParameter("seq_division")->convertTo0to1(static_cast<float>(static_cast<int>(root["division"]))));
            if (root.hasProperty("gate"))
                apvts.getParameter("seq_gate")->setValueNotifyingHost(
                    apvts.getParameter("seq_gate")->convertTo0to1(static_cast<float>(root["gate"])));
            if (root.hasProperty("bpm"))
                apvts.getParameter("seq_bpm")->setValueNotifyingHost(
                    apvts.getParameter("seq_bpm")->convertTo0to1(static_cast<float>(root["bpm"])));
            if (root.hasProperty("arpMode"))
                apvts.getParameter("arp_mode")->setValueNotifyingHost(
                    apvts.getParameter("arp_mode")->convertTo0to1(static_cast<float>(static_cast<int>(root["arpMode"]))));
            if (root.hasProperty("arpRate"))
                apvts.getParameter("arp_rate")->setValueNotifyingHost(
                    apvts.getParameter("arp_rate")->convertTo0to1(static_cast<float>(static_cast<int>(root["arpRate"]))));
            if (root.hasProperty("arpOctaves"))
                apvts.getParameter("arp_octaves")->setValueNotifyingHost(
                    apvts.getParameter("arp_octaves")->convertTo0to1(static_cast<float>(static_cast<int>(root["arpOctaves"]))));
            // arpGate removed — old files silently ignored
            if (root.hasProperty("octave"))
                apvts.getParameter("seq_octave")->setValueNotifyingHost(
                    apvts.getParameter("seq_octave")->convertTo0to1(static_cast<float>(static_cast<int>(root["octave"]))));
            auto* stepsArr = root["steps"].getArray();
            if (stepsArr)
            {
                for (int i = 0; i < stepsArr->size() && i < T5ynthStepSequencer::MAX_STEPS; ++i)
                {
                    const auto& s = (*stepsArr)[i];
                    if (s.hasProperty("note")) seq.setStepNote(i, static_cast<int>(s["note"]));
                    if (s.hasProperty("velocity")) seq.setStepVelocity(i, static_cast<float>(s["velocity"]));
                    if (s.hasProperty("gate")) seq.setStepGate(i, static_cast<float>(s["gate"]));
                    if (s.hasProperty("enabled")) seq.setStepEnabled(i, static_cast<bool>(s["enabled"]));
                    if (s.hasProperty("bind")) seq.setStepBind(i, static_cast<bool>(s["bind"]));
                }
            }
            syncStepCount();
            repaint();
        });
    };

    // ── Gate ──
    gateRow = std::make_unique<SliderRow>("Gate", [](double v) { return juce::String(juce::roundToInt(v*100)) + "%"; }, kSeqCol);
    addAndMakeVisible(*gateRow);
    gateA = std::make_unique<SA>(apvts, "seq_gate", gateRow->getSlider());
    gateRow->getSlider().onValueChange = [this] { gateRow->updateValue(); };
    gateRow->updateValue();

    // ── Octave shift [-2][-1][0][+1][+2] ──
    octShiftHidden.addItemList({"-2", "-1", "0", "+1", "+2"}, 1);
    octShiftHidden.onChange = [this] {
        int id = octShiftHidden.getSelectedId();
        for (int i = 0; i < kNumOctShiftBtns; ++i)
            octShiftBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
    };
    static const char* octLabels[] = {"-2", "-1", "0", "+1", "+2"};
    for (int i = 0; i < kNumOctShiftBtns; ++i)
    {
        octShiftBtns[i].setButtonText(octLabels[i]);
        octShiftBtns[i].setColour(juce::TextButton::buttonColourId, kSurface);
        octShiftBtns[i].setColour(juce::TextButton::buttonOnColourId, kSeqCol);
        octShiftBtns[i].setColour(juce::TextButton::textColourOffId, kDim);
        octShiftBtns[i].setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        octShiftBtns[i].setClickingTogglesState(true);
        octShiftBtns[i].setRadioGroupId(2004);
        octShiftBtns[i].onClick = [this, i] { octShiftHidden.setSelectedId(i + 1); };
        addAndMakeVisible(octShiftBtns[i]);
    }
    octShiftA = std::make_unique<CA>(apvts, "seq_octave", octShiftHidden);

    // ── Generative sequencer controls ──
    genTransportBtn.setColour(juce::TextButton::buttonColourId, kSurface);
    genTransportBtn.setColour(juce::TextButton::buttonOnColourId, kSeqCol);
    genTransportBtn.setColour(juce::TextButton::textColourOffId, kSeqCol);
    genTransportBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    genTransportBtn.setClickingTogglesState(true);
    // No onClick needed — ButtonAttachment handles parameter sync
    addAndMakeVisible(genTransportBtn);
    genRunningA = std::make_unique<BA>(apvts, "gen_seq_running", genTransportBtn);

    auto intFmt = [](double v) { return juce::String(juce::roundToInt(v)); };
    genStepsRow = std::make_unique<SliderRow>("Steps", intFmt, kSeqCol);
    addAndMakeVisible(*genStepsRow);
    genStepsA = std::make_unique<SA>(apvts, "gen_steps", genStepsRow->getSlider());
    genStepsRow->getSlider().onValueChange = [this] { genStepsRow->updateValue(); };
    genStepsRow->updateValue();

    genPulsesRow = std::make_unique<SliderRow>("Pulses", intFmt, kSeqCol);
    addAndMakeVisible(*genPulsesRow);
    genPulsesA = std::make_unique<SA>(apvts, "gen_pulses", genPulsesRow->getSlider());
    genPulsesRow->getSlider().onValueChange = [this] { genPulsesRow->updateValue(); };
    genPulsesRow->updateValue();

    genRotationRow = std::make_unique<SliderRow>("Rotation", intFmt, kSeqCol);
    addAndMakeVisible(*genRotationRow);
    genRotationA = std::make_unique<SA>(apvts, "gen_rotation", genRotationRow->getSlider());
    genRotationRow->getSlider().onValueChange = [this] { genRotationRow->updateValue(); };
    genRotationRow->updateValue();

    // Range switchbox [1][2][3][4] octaves
    genRangeHidden.addItemList({"1","2","3","4"}, 1);
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
    genRangeA = std::make_unique<CA>(apvts, "gen_range", genRangeHidden);
    genRangeLabel.setText("Range", juce::dontSendNotification);
    genRangeLabel.setColour(juce::Label::textColourId, kDim);
    genRangeLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(genRangeLabel);

    genMutationRow = std::make_unique<SliderRow>("Evolve",
        [](double v) { return juce::String(juce::roundToInt(v * 100)) + "%"; }, kSeqCol);
    addAndMakeVisible(*genMutationRow);
    genMutationA = std::make_unique<SA>(apvts, "gen_mutation", genMutationRow->getSlider());
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
        btn.setTooltip(tip);
        addAndMakeVisible(btn);
    };
    setupFixBtn(genFixStepsBtn,    "Lock Steps against drift");
    setupFixBtn(genFixPulsesBtn,   "Lock Pulses against drift");
    setupFixBtn(genFixRotationBtn, "Lock Rotation against drift");
    setupFixBtn(genFixMutationBtn, "Lock Evolve against drift");
    genFixStepsA    = std::make_unique<BA>(apvts, "gen_fix_steps",    genFixStepsBtn);
    genFixPulsesA   = std::make_unique<BA>(apvts, "gen_fix_pulses",   genFixPulsesBtn);
    genFixRotationA = std::make_unique<BA>(apvts, "gen_fix_rotation", genFixRotationBtn);
    genFixMutationA = std::make_unique<BA>(apvts, "gen_fix_mutation", genFixMutationBtn);

    genScaleRootBox.addItemList({"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}, 1);
    genScaleRootBox.setColour(juce::ComboBox::backgroundColourId, kSurface);
    genScaleRootBox.setColour(juce::ComboBox::textColourId, kSeqCol);
    genScaleRootBox.setColour(juce::ComboBox::outlineColourId, kBorder);
    addAndMakeVisible(genScaleRootBox);
    genScaleRootA = std::make_unique<CA>(apvts, "scale_root", genScaleRootBox);

    genScaleTypeBox.addItemList({"Off","Maj","Min","Pent","Dor","Harm","WhlT"}, 1);
    genScaleTypeBox.setColour(juce::ComboBox::backgroundColourId, kSurface);
    genScaleTypeBox.setColour(juce::ComboBox::textColourId, kSeqCol);
    genScaleTypeBox.setColour(juce::ComboBox::outlineColourId, kBorder);
    addAndMakeVisible(genScaleTypeBox);
    genScaleTypeA = std::make_unique<CA>(apvts, "scale_type", genScaleTypeBox);

    // ── Arp controls (SwitchBox: OFF/Up/Dn/U-D/Rnd) ──
    arpModeBox.addItemList({"Off","Up","Down","UpDown","Random"}, 1);
    arpModeBox.onChange = [this] {
        int id = arpModeBox.getSelectedId();
        for (int i = 0; i < kNumModeBtns; ++i)
            arpModeBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
    };
    static const char* modeLabels[] = {"OFF","Up","Dn","U/D","Rnd"};
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
    arpModeA = std::make_unique<CA>(apvts, "arp_mode", arpModeBox);

    arpRateBox.addItemList({"1/4","1/8","1/16","1/32","1/4T","1/8T","1/16T"}, 1);
    addAndMakeVisible(arpRateBox);
    arpRateA = std::make_unique<CA>(apvts, "arp_rate", arpRateBox);

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
    arpOctA = std::make_unique<CA>(apvts, "arp_octaves", arpOctHidden);

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
    seqSaveBtn.setLookAndFeel(nullptr);
    seqLoadBtn.setLookAndFeel(nullptr);
    genFixStepsBtn.setLookAndFeel(nullptr);
    genFixPulsesBtn.setLookAndFeel(nullptr);
    genFixRotationBtn.setLookAndFeel(nullptr);
    genFixMutationBtn.setLookAndFeel(nullptr);
}

void SequencerPanel::syncStepCount()
{
    int steps = static_cast<int>(processorRef.getValueTreeState()
                    .getRawParameterValue("seq_steps")->load());
    numVisibleSteps = juce::jlimit(2, MAX_COLS, steps);

    for (int i = 0; i < MAX_COLS; ++i)
        stepCols[static_cast<size_t>(i)]->setVisible(i < numVisibleSteps);

    stepCountBox.setSelectedId(numVisibleSteps, juce::dontSendNotification);
    resized();
}

void SequencerPanel::timerCallback()
{
    bool genRunning = processorRef.getValueTreeState()
        .getRawParameterValue("gen_seq_running")->load() > 0.5f;
    bool seqRunning = processorRef.getValueTreeState()
        .getRawParameterValue("seq_running")->load() > 0.5f;

    // Skip expensive updates when audio is idle and neither sequencer runs
    bool seqIdle = processorRef.audioIdle.load(std::memory_order_relaxed)
                   && !seqRunning && !genRunning;
    if (seqIdle)
        return;

    // Mode change detection → relayout
    if (genRunning != genModeActive)
        resized();

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
                        .getRawParameterValue("seq_steps")->load());
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
    if (!midiMonitor.getBounds().isEmpty())
    {
        bool noteOn = processorRef.lastMidiNoteOn.load(std::memory_order_relaxed);
        float ledX = static_cast<float>(midiMonitor.getX()) - 12.0f;
        float ledY = static_cast<float>(midiMonitor.getBounds().getCentreY()) - 4.0f;
        g.setColour(noteOn ? juce::Colour(0xff4ade80) : kDimmer);
        g.fillEllipse(ledX, ledY, 8.0f, 8.0f);
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

    // ═══ Row 1: ALWAYS the same — transport, GEN, preset, save/load, steps, division, etc. ═══
    auto r1 = area.removeFromTop(rH);
    transportBtn.setBounds(r1.removeFromLeft(36));  r1.removeFromLeft(g);
    genTransportBtn.setBounds(r1.removeFromLeft(36));  r1.removeFromLeft(g);

    presetBox.setBounds(r1.removeFromLeft(90)); r1.removeFromLeft(2);
    seqSaveBtn.setBounds(r1.removeFromLeft(rH)); r1.removeFromLeft(1);
    seqLoadBtn.setBounds(r1.removeFromLeft(rH)); r1.removeFromLeft(g);
    stepCountBox.setBounds(r1.removeFromLeft(50)); r1.removeFromLeft(g);

    // Division toggle strip (wider to fit "1/16")
    int divBtnW = 30;
    for (int i = 0; i < kNumDivBtns; ++i)
    {
        int edges = 0;
        if (i > 0) edges |= juce::Button::ConnectedOnLeft;
        if (i < kNumDivBtns - 1) edges |= juce::Button::ConnectedOnRight;
        divBtns[i].setConnectedEdges(edges);
        divBtns[i].setBounds(r1.removeFromLeft(divBtnW));
    }
    r1.removeFromLeft(g);

    // Octave shift strip [-2][-1][0][+1][+2]
    int octBtnW = 26;
    for (int i = 0; i < kNumOctShiftBtns; ++i)
    {
        int edges = 0;
        if (i > 0) edges |= juce::Button::ConnectedOnLeft;
        if (i < kNumOctShiftBtns - 1) edges |= juce::Button::ConnectedOnRight;
        octShiftBtns[i].setConnectedEdges(edges);
        octShiftBtns[i].setBounds(r1.removeFromLeft(octBtnW));
    }
    r1.removeFromLeft(g);

    // MIDI monitor on far right
    midiMonitor.setFont(juce::FontOptions(juce::jmax(9.0f, rH * 0.6f)));
    midiMonitor.setBounds(r1.removeFromRight(80));
    r1.removeFromRight(g);

    // BPM (2/3) and Gate (1/3) — BPM needs resolution, gate is less critical
    int bpmW = r1.getWidth() * 2 / 3 - 1;
    bpmRow->setBounds(r1.removeFromLeft(bpmW));
    r1.removeFromLeft(2);
    gateRow->setBounds(r1);

    area.removeFromTop(g);

    // ═══ Row 4 (bottom): Arp controls ═══
    auto r4 = area.removeFromBottom(rH);
    int modeBtnW = 32;
    for (int i = 0; i < kNumModeBtns; ++i)
    {
        int edges = 0;
        if (i > 0) edges |= juce::Button::ConnectedOnLeft;
        if (i < kNumModeBtns - 1) edges |= juce::Button::ConnectedOnRight;
        arpModeBtns[i].setConnectedEdges(edges);
        arpModeBtns[i].setBounds(r4.removeFromLeft(modeBtnW));
    }
    r4.removeFromLeft(g);
    arpRateBox.setBounds(r4.removeFromLeft(60));   r4.removeFromLeft(g);
    arpOctLabel.setFont(juce::FontOptions(juce::jmax(9.0f, rH * 0.55f)));
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
        .getRawParameterValue("gen_seq_running")->load() > 0.5f;

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

    if (genModeActive)
    {
        // ═══ Gen mode: 2-column grid with lock buttons ═══
        int genCtrlH = rH;
        int colGap = 4;
        int lockW = genCtrlH;  // square lock button
        int lockGap = 1;
        int colW = (area.getWidth() - colGap) / 2;
        int sliderW = colW - lockW - lockGap;

        // Row 1:  Steps [====] [🔒]  |  Pulses [====] [🔓]
        auto row1 = area.removeFromTop(genCtrlH);
        genStepsRow->setBounds(row1.removeFromLeft(sliderW));
        row1.removeFromLeft(lockGap);
        genFixStepsBtn.setBounds(row1.removeFromLeft(lockW));
        row1.removeFromLeft(colGap);
        genPulsesRow->setBounds(row1.removeFromLeft(sliderW));
        row1.removeFromLeft(lockGap);
        genFixPulsesBtn.setBounds(row1.removeFromLeft(lockW));
        area.removeFromTop(2);

        // Row 2:  Rotation [==] [🔓]  |  Evolve [===] [🔒]
        auto row2 = area.removeFromTop(genCtrlH);
        genRotationRow->setBounds(row2.removeFromLeft(sliderW));
        row2.removeFromLeft(lockGap);
        genFixRotationBtn.setBounds(row2.removeFromLeft(lockW));
        row2.removeFromLeft(colGap);
        genMutationRow->setBounds(row2.removeFromLeft(sliderW));
        row2.removeFromLeft(lockGap);
        genFixMutationBtn.setBounds(row2.removeFromLeft(lockW));
        area.removeFromTop(2);

        // Row 3:  [C▾] [Min▾]     |  Range [1][2][3][4]
        auto row3 = area.removeFromTop(genCtrlH);
        auto r3L = row3.removeFromLeft(colW);
        row3.removeFromLeft(colGap);
        auto r3R = row3;

        genScaleRootBox.setBounds(r3L.removeFromLeft(55));  r3L.removeFromLeft(2);
        genScaleTypeBox.setBounds(r3L.removeFromLeft(70));

        genRangeLabel.setFont(juce::FontOptions(juce::jmax(9.0f, genCtrlH * 0.55f)));
        genRangeLabel.setBounds(r3R.removeFromLeft(42));  r3R.removeFromLeft(2);
        int rangeBtnW = 22;
        for (int i = 0; i < kNumRangeBtns; ++i)
        {
            int edges = 0;
            if (i > 0) edges |= juce::Button::ConnectedOnLeft;
            if (i < kNumRangeBtns - 1) edges |= juce::Button::ConnectedOnRight;
            genRangeBtns[i].setConnectedEdges(edges);
            genRangeBtns[i].setBounds(r3R.removeFromLeft(rangeBtnW));
        }

        area.removeFromTop(g);

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
