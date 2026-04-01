#include "SequencerPanel.h"
#include "../PluginProcessor.h"

// ─── StepColumn ────────────────────────────────────────────────────

void SequencerPanel::StepColumn::paint(juce::Graphics& g)
{
    if (!processor) return;
    auto step = processor->getStepSequencer().getStep(stepIndex);
    auto b = getLocalBounds().reduced(1);
    int h = b.getHeight();

    // Background
    g.setColour(isCurrentStep ? kAccent.withAlpha(0.35f)
                : step.enabled ? kSurface : kBg);
    g.fillRect(b);

    // Beat group border
    if (stepIndex % 4 == 0)
    {
        g.setColour(kBorder);
        g.drawLine(static_cast<float>(b.getX()), static_cast<float>(b.getY()),
                   static_cast<float>(b.getX()), static_cast<float>(b.getBottom()), 1.0f);
    }

    // Note offset (top 28%)
    int noteH = juce::roundToInt(h * 0.28f);
    auto noteR = b.removeFromTop(noteH);
    int semi = step.note - 60;
    g.setColour(step.enabled ? kAccent : kDim);
    g.setFont(juce::FontOptions(juce::jmax(9.0f, noteH * 0.55f)));
    g.drawText(juce::String(semi), noteR, juce::Justification::centred);

    // Active dot (12%)
    int dotH = juce::roundToInt(h * 0.12f);
    auto dotR = b.removeFromTop(dotH);
    int ds = juce::jmin(8, dotR.getWidth() / 2);
    g.setColour(step.enabled ? juce::Colour(0xff4caf50) : kDimmer);
    g.fillEllipse(static_cast<float>(dotR.getCentreX() - ds / 2),
                  static_cast<float>(dotR.getCentreY() - ds / 2),
                  static_cast<float>(ds), static_cast<float>(ds));

    // Glide badge (10%)
    int glH = juce::roundToInt(h * 0.10f);
    auto glR = b.removeFromTop(glH);
    g.setColour(step.glide ? kAccent : kDimmer);
    g.setFont(juce::FontOptions(juce::jmax(8.0f, glH * 0.7f)));
    g.drawText("G", glR, juce::Justification::centred);

    // Velocity bar (remaining 50%)
    auto velR = b;
    float velPx = step.velocity * static_cast<float>(velR.getHeight());
    int barW = juce::jmax(3, velR.getWidth() / 3);
    int barX = velR.getCentreX() - barW / 2;
    g.setColour(kDimmer);
    g.fillRect(barX, velR.getY(), barW, velR.getHeight());
    g.setColour(step.enabled ? kAccent.withAlpha(0.7f) : kDim);
    g.fillRect(barX, velR.getBottom() - juce::roundToInt(velPx), barW, juce::roundToInt(velPx));
}

void SequencerPanel::StepColumn::mouseDown(const juce::MouseEvent& e)
{
    if (!processor) return;
    auto& seq = processor->getStepSequencer();
    auto step = seq.getStep(stepIndex);
    int h = getHeight();
    int y = e.getPosition().getY();

    int noteH = juce::roundToInt(h * 0.28f);
    int dotH  = juce::roundToInt(h * 0.12f);
    int glH   = juce::roundToInt(h * 0.10f);

    if (y < noteH)
    {
        int semi = step.note - 60 + (e.mods.isShiftDown() ? -1 : 1);
        seq.setStepNote(stepIndex, 60 + juce::jlimit(-24, 24, semi));
    }
    else if (y < noteH + dotH)
        seq.setStepEnabled(stepIndex, !step.enabled);
    else if (y < noteH + dotH + glH)
        seq.setStepGlide(stepIndex, !step.glide);
    else
    {
        float vel = 1.0f - static_cast<float>(y - noteH - dotH - glH)
                          / static_cast<float>(juce::jmax(1, h - noteH - dotH - glH));
        seq.setStepVelocity(stepIndex, juce::jlimit(0.0f, 1.0f, vel));
    }
    repaint();
}

void SequencerPanel::StepColumn::mouseDrag(const juce::MouseEvent& e)
{
    if (!processor) return;
    int h = getHeight();
    int noteH = juce::roundToInt(h * 0.28f);
    int dotH  = juce::roundToInt(h * 0.12f);
    int glH   = juce::roundToInt(h * 0.10f);
    int velTop = noteH + dotH + glH;
    int velH   = h - velTop;

    if (e.getPosition().getY() >= velTop && velH > 0)
    {
        float vel = 1.0f - static_cast<float>(e.getPosition().getY() - velTop)
                          / static_cast<float>(velH);
        processor->getStepSequencer().setStepVelocity(stepIndex, juce::jlimit(0.0f, 1.0f, vel));
        repaint();
    }
}

// ─── SequencerPanel ────────────────────────────────────────────────

static juce::String noteName(int n)
{
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    return juce::String(names[n % 12]) + juce::String(n / 12 - 1);
}

SequencerPanel::SequencerPanel(T5ynthProcessor& p)
    : processorRef(p)
{
    auto& apvts = p.getValueTreeState();

    // ── Transport ──
    playButton.setColour(juce::TextButton::buttonColourId, kSurface);
    playButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff4caf50));
    playButton.onClick = [this] {
        if (auto* par = processorRef.getValueTreeState().getParameter("seq_running"))
            par->setValueNotifyingHost(1.0f);
    };
    addAndMakeVisible(playButton);

    stopButton.setColour(juce::TextButton::buttonColourId, kSurface);
    stopButton.setColour(juce::TextButton::textColourOffId, kDim);
    stopButton.onClick = [this] {
        if (auto* par = processorRef.getValueTreeState().getParameter("seq_running"))
            par->setValueNotifyingHost(0.0f);
        currentStep = -1;
    };
    addAndMakeVisible(stopButton);

    // ── Step count buttons ──
    for (int i = 0; i < 5; ++i)
    {
        auto& btn = stepCountBtns[static_cast<size_t>(i)];
        btn.setButtonText(juce::String(STEP_COUNTS[i]));
        btn.setColour(juce::TextButton::buttonColourId, kSurface);
        btn.onClick = [this, i] {
            if (auto* par = processorRef.getValueTreeState().getParameter("seq_steps"))
            {
                auto range = par->getNormalisableRange();
                par->setValueNotifyingHost(range.convertTo0to1(static_cast<float>(STEP_COUNTS[i])));
            }
            syncStepCount();
        };
        addAndMakeVisible(btn);
    }

    // ── Division ──
    divisionBox.addItemList({"1/1", "1/2", "1/4", "1/8", "1/16"}, 1);
    addAndMakeVisible(divisionBox);
    divA = std::make_unique<CA>(apvts, "seq_division", divisionBox);

    // ── BPM ──
    bpmRow = std::make_unique<SliderRow>("BPM", [](double v) { return juce::String(juce::roundToInt(v)); });
    addAndMakeVisible(*bpmRow);
    bpmA = std::make_unique<SA>(apvts, "seq_bpm", bpmRow->getSlider());
    bpmRow->getSlider().onValueChange = [this] { bpmRow->updateValue(); };
    bpmRow->updateValue();

    // ── Preset ──
    presetBox.addItemList({"East Coast","West Coast","Synthwave","Techno","Dub Techno",
                           "Ambient","IDM Glitch","Solar","Arp Bass","Trance Gate"}, 1);
    addAndMakeVisible(presetBox);
    presetA = std::make_unique<CA>(apvts, "seq_preset", presetBox);

    // ── Gate ──
    gateRow = std::make_unique<SliderRow>("Gate", [](double v) { return juce::String(juce::roundToInt(v*100)) + "%"; });
    addAndMakeVisible(*gateRow);
    gateA = std::make_unique<SA>(apvts, "seq_gate", gateRow->getSlider());
    gateRow->getSlider().onValueChange = [this] { gateRow->updateValue(); };
    gateRow->updateValue();

    // ── Glide ──
    glideRow = std::make_unique<SliderRow>("Glide", [](double v) { return juce::String(juce::roundToInt(v)) + "ms"; });
    addAndMakeVisible(*glideRow);
    glideA = std::make_unique<SA>(apvts, "seq_glide_time", glideRow->getSlider());
    glideRow->getSlider().onValueChange = [this] { glideRow->updateValue(); };
    glideRow->updateValue();

    // ── MIDI monitor ──
    midiMonitor.setColour(juce::Label::textColourId, juce::Colour(0xff4ade80));
    midiMonitor.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(midiMonitor);

    // ── Arp controls ──
    arpEnable.setColour(juce::ToggleButton::textColourId, kDim);
    addAndMakeVisible(arpEnable);
    arpEnableA = std::make_unique<BA>(apvts, "arp_enabled", arpEnable);

    arpModeBox.addItemList({"Up","Down","UpDown","Random"}, 1);
    addAndMakeVisible(arpModeBox);
    arpModeA = std::make_unique<CA>(apvts, "arp_mode", arpModeBox);

    arpRateBox.addItemList({"1/4","1/8","1/16","1/32","1/4T","1/8T","1/16T"}, 1);
    addAndMakeVisible(arpRateBox);
    arpRateA = std::make_unique<CA>(apvts, "arp_rate", arpRateBox);

    arpOctRow = std::make_unique<SliderRow>("Oct", [](double v) { return juce::String(juce::roundToInt(v)); });
    addAndMakeVisible(*arpOctRow);
    arpOctA = std::make_unique<SA>(apvts, "arp_octaves", arpOctRow->getSlider());
    arpOctRow->getSlider().onValueChange = [this] { arpOctRow->updateValue(); };
    arpOctRow->updateValue();

    arpGateRow = std::make_unique<SliderRow>("Gate", [](double v) { return juce::String(juce::roundToInt(v*100)) + "%"; });
    addAndMakeVisible(*arpGateRow);
    arpGateA = std::make_unique<SA>(apvts, "arp_gate", arpGateRow->getSlider());
    arpGateRow->getSlider().onValueChange = [this] { arpGateRow->updateValue(); };
    arpGateRow->updateValue();

    // ── Step columns ──
    for (int i = 0; i < MAX_COLS; ++i)
    {
        stepCols[static_cast<size_t>(i)] = std::make_unique<StepColumn>();
        stepCols[static_cast<size_t>(i)]->stepIndex = i;
        stepCols[static_cast<size_t>(i)]->processor = &p;
        addAndMakeVisible(*stepCols[static_cast<size_t>(i)]);
    }

    syncStepCount();
    startTimerHz(30);
}

void SequencerPanel::syncStepCount()
{
    int steps = static_cast<int>(processorRef.getValueTreeState()
                    .getRawParameterValue("seq_steps")->load());
    numVisibleSteps = juce::jlimit(1, MAX_COLS, steps);

    for (int i = 0; i < MAX_COLS; ++i)
        stepCols[static_cast<size_t>(i)]->setVisible(i < numVisibleSteps);

    for (int i = 0; i < 5; ++i)
        stepCountBtns[static_cast<size_t>(i)].setColour(
            juce::TextButton::buttonColourId,
            STEP_COUNTS[i] == numVisibleSteps ? kAccent : kSurface);

    resized();
}

void SequencerPanel::timerCallback()
{
    // Step highlight
    int step = processorRef.getStepSequencer().currentStepForGui.load(std::memory_order_relaxed);
    if (step != currentStep)
    {
        currentStep = step;
        for (int i = 0; i < MAX_COLS; ++i)
            stepCols[static_cast<size_t>(i)]->isCurrentStep = (i == currentStep);
    }

    // MIDI monitor
    int note = processorRef.lastMidiNote.load(std::memory_order_relaxed);
    bool on  = processorRef.lastMidiNoteOn.load(std::memory_order_relaxed);
    int vel  = processorRef.lastMidiVelocity.load(std::memory_order_relaxed);
    if (note >= 0)
    {
        auto txt = on ? ("MIDI: " + noteName(note) + " v" + juce::String(vel))
                      : ("MIDI: " + noteName(note) + " off");
        midiMonitor.setText(txt, juce::dontSendNotification);
        midiMonitor.setColour(juce::Label::textColourId, on ? juce::Colour(0xff4ade80) : kDim);
    }

    // Sync step count if changed externally (preset load, automation)
    int steps = static_cast<int>(processorRef.getValueTreeState()
                    .getRawParameterValue("seq_steps")->load());
    if (steps != numVisibleSteps)
        syncStepCount();

    // Repaint grid
    for (int i = 0; i < numVisibleSteps; ++i)
        stepCols[static_cast<size_t>(i)]->repaint();
}

void SequencerPanel::paint(juce::Graphics& g)
{
    g.fillAll(kCard);

    // Playing LED
    bool playing = processorRef.getValueTreeState()
                       .getRawParameterValue("seq_running")->load() > 0.5f;
    int ledX = playButton.getX() - 12;
    int ledY = playButton.getBounds().getCentreY() - 4;
    g.setColour(playing ? juce::Colour(0xff4caf50) : kDimmer);
    g.fillEllipse(static_cast<float>(ledX), static_cast<float>(ledY), 8.0f, 8.0f);

    // Separator above step grid
    if (!gridArea.isEmpty())
    {
        g.setColour(kBorder);
        g.drawHorizontalLine(gridArea.getY() - 1,
                             static_cast<float>(gridArea.getX()),
                             static_cast<float>(gridArea.getRight()));
    }
}

void SequencerPanel::resized()
{
    auto area = getLocalBounds().reduced(6, 3);
    int rH = 22;  // row height
    int g = 3;    // gap
    int comboW = 56;

    // ═══ Row 1: Transport, step counts, division, BPM ═══
    auto r1 = area.removeFromTop(rH);
    r1.removeFromLeft(14); // LED space
    playButton.setBounds(r1.removeFromLeft(26));  r1.removeFromLeft(g);
    stopButton.setBounds(r1.removeFromLeft(26));  r1.removeFromLeft(g * 2);

    for (int i = 0; i < 5; ++i)
    {
        stepCountBtns[static_cast<size_t>(i)].setBounds(r1.removeFromLeft(26));
        r1.removeFromLeft(1);
    }
    r1.removeFromLeft(g * 2);
    divisionBox.setBounds(r1.removeFromLeft(comboW));
    r1.removeFromLeft(g);
    bpmRow->setBounds(r1);  // BPM gets all remaining width

    area.removeFromTop(g);

    // ═══ Row 2: Preset, Gate, Glide, MIDI monitor ═══
    auto r2 = area.removeFromTop(rH);
    presetBox.setBounds(r2.removeFromLeft(90));   r2.removeFromLeft(g);
    int slW = (r2.getWidth() - 120 - g * 3) / 2;  // split remaining for Gate + Glide
    gateRow->setBounds(r2.removeFromLeft(slW));    r2.removeFromLeft(g);
    glideRow->setBounds(r2.removeFromLeft(slW));   r2.removeFromLeft(g);
    midiMonitor.setFont(juce::FontOptions(juce::jmax(9.0f, rH * 0.6f)));
    midiMonitor.setBounds(r2);  // MIDI monitor gets rest

    area.removeFromTop(g);

    // ═══ Row 4 (bottom): Arp controls ═══
    auto r4 = area.removeFromBottom(rH);
    arpEnable.setBounds(r4.removeFromLeft(48));    r4.removeFromLeft(g);
    arpModeBox.setBounds(r4.removeFromLeft(comboW + 10)); r4.removeFromLeft(g);
    arpRateBox.setBounds(r4.removeFromLeft(comboW));      r4.removeFromLeft(g);
    int arpSlW = (r4.getWidth() - g) / 2;
    arpOctRow->setBounds(r4.removeFromLeft(arpSlW));  r4.removeFromLeft(g);
    arpGateRow->setBounds(r4);

    area.removeFromBottom(g);

    // ═══ Row 3: Step grid (everything remaining) ═══
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
