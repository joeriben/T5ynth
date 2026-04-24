#include "PromptPanel.h"
#include "DimensionExplorer.h"
#include "GuiHelpers.h"
#include "../PluginProcessor.h"
#include "../dsp/BlockParams.h"
#include <thread>
#include <cmath>

namespace
{
constexpr float kPromptPadFactor   = 0.04f;
constexpr float kPromptRow         = 1.4f;
constexpr float kPromptSlider      = 1.2f;
constexpr float kPromptInput       = 1.8f;
constexpr float kPromptCompactRow  = 1.15f;
constexpr float kPromptCompactCtrl = 0.9f;
constexpr float kPromptGap         = 0.28f;
constexpr float kPromptContentUnits = 20.0f;

float preferredPromptFontForWidth(int width)
{
    const float innerW = juce::jmax(160.0f, static_cast<float>(width) * (1.0f - 2.0f * kPromptPadFactor));
    return juce::jlimit(11.5f, 15.5f, innerW * 0.048f);
}
}

// Colors from GuiHelpers.h (kAccent, kDim, kDim, kSurface)

// Linear crossfade between old and new audio buffers.
// Blends the first xfadeSamples of newBuf with corresponding samples from oldBuf.
// Linear (not equal-power) because this is applied iteratively during drift regens —
// equal-power has gain > 1 (peak √2) which compounds with normalize into degeneration.
static void applyDriftCrossfade(juce::AudioBuffer<float>& newBuf,
                                 const juce::AudioBuffer<float>& oldBuf,
                                 int xfadeSamples)
{
    int len = std::min(xfadeSamples, std::min(newBuf.getNumSamples(), oldBuf.getNumSamples()));
    if (len <= 0) return;
    int channels = std::min(newBuf.getNumChannels(), oldBuf.getNumChannels());
    for (int ch = 0; ch < channels; ++ch)
    {
        float* dst = newBuf.getWritePointer(ch);
        const float* src = oldBuf.getReadPointer(ch);
        for (int i = 0; i < len; ++i)
        {
            float t = static_cast<float>(i) / static_cast<float>(len);
            dst[i] = src[i] * (1.0f - t) + dst[i] * t;
        }
    }
}

static void makeSlider(juce::Slider& s, juce::Component* p)
{
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    s.setColour(juce::Slider::trackColourId, kOscCol);
    s.setColour(juce::Slider::backgroundColourId, kSurface);
    p->addAndMakeVisible(s);
}

static void makeLabel(juce::Label& l, const juce::String& text, juce::Colour col,
                      juce::Justification just, juce::Component* p)
{
    l.setText(text, juce::dontSendNotification);
    l.setColour(juce::Label::textColourId, col);
    l.setJustificationType(just);
    p->addAndMakeVisible(l);
}

PromptPanel::PromptPanel(T5ynthProcessor& processor)
    : processorRef(processor)
{
    makeLabel(promptALabel, "Prompt A (Basis)", kDim, juce::Justification::centredLeft, this);
    promptAEditor.setMultiLine(false);
    promptAEditor.setText("a steady clean saw wave, c3");
    promptAEditor.onReturnKey = [this] { triggerGeneration(); };
    promptAEditor.onTextChange = [this] {
        // Prompt edits should force the next drift regen to use a fresh snapshot.
        lastGenPromptA_.clear();
    };
    promptAEditor.setBufferedToImage(true);
    addAndMakeVisible(promptAEditor);

    makeLabel(promptBLabel, "Prompt B (optional, for interpolation)", kDim, juce::Justification::centredLeft, this);
    promptBEditor.setMultiLine(false);
    promptBEditor.setText("glass breaking");
    promptBEditor.onTextChange = [this] {
        // Prompt edits should force the next drift regen to use a fresh snapshot.
        lastGenPromptB_.clear();
    };
    promptBEditor.setBufferedToImage(true);
    addAndMakeVisible(promptBEditor);

    // Alpha
    makeSlider(alphaSlider, this);
    makeLabel(alphaLabel, "Alpha (A <-> B)", kDim, juce::Justification::centredLeft, this);
    makeLabel(alphaValue, "0", kOscCol, juce::Justification::centredRight, this);
    makeLabel(alphaHint, "Interpolation: -1.0 = A only, 1.0 = B only", kDim, juce::Justification::centredLeft, this);
    alphaSlider.onValueChange = [this] {
        float v = static_cast<float>(alphaSlider.getValue());
        if (std::abs(v) < 0.001f)
            alphaValue.setText("0", juce::dontSendNotification);
        else if (v < 0.0f)
            alphaValue.setText("A " + juce::String(-v, 3), juce::dontSendNotification);
        else
            alphaValue.setText("B " + juce::String(v, 3), juce::dontSendNotification);
    };

    // Magnitude
    makeSlider(magnitudeSlider, this);
    makeLabel(magLabel, "Magnitude (Embedding Scale)", kDim, juce::Justification::centredLeft, this);
    makeLabel(magValue, "1.00", kOscCol, juce::Justification::centredRight, this);
    makeLabel(magHint, "Embedding scale (1.0 = unchanged)", kDim, juce::Justification::centredLeft, this);
    magnitudeSlider.onValueChange = [this] {
        magValue.setText(juce::String(magnitudeSlider.getValue(), 3), juce::dontSendNotification);
    };

    // Noise
    makeSlider(noiseSlider, this);
    makeLabel(noiseLabel, "Noise (Embedding Chaos)", kDim, juce::Justification::centredLeft, this);
    makeLabel(noiseValue, "0.000", kOscCol, juce::Justification::centredRight, this);
    makeLabel(noiseHint, "Gaussian noise on embedding (0 = none)", kDim, juce::Justification::centredLeft, this);
    noiseSlider.onValueChange = [this] {
        noiseValue.setText(juce::String(noiseSlider.getValue(), 3), juce::dontSendNotification);
    };

    // --- Compact params ---
    // Duration
    makeSlider(durationSlider, this);
    makeLabel(durLabel, "Duration", kDim, juce::Justification::centredLeft, this);
    makeLabel(durValue, "3.00s", kOscCol, juce::Justification::centredRight, this);
    makeLabel(durHint, "Audio length (seconds)", kDim, juce::Justification::centredLeft, this);
    durationSlider.onValueChange = [this] {
        durValue.setText(juce::String(durationSlider.getValue(), 2) + "s", juce::dontSendNotification);
    };

    // Steps
    makeSlider(stepsSlider, this);
    makeLabel(stepsLabel, "Steps", kDim, juce::Justification::centredLeft, this);
    makeLabel(stepsValue, "8", kOscCol, juce::Justification::centredRight, this);
    makeLabel(stepsHint, "More = higher quality", kDim, juce::Justification::centredLeft, this);
    stepsSlider.onValueChange = [this] {
        stepsValue.setText(juce::String(juce::roundToInt(stepsSlider.getValue())), juce::dontSendNotification);
    };

    // CFG
    makeSlider(cfgSlider, this);
    makeLabel(cfgLabel, "CFG", kDim, juce::Justification::centredLeft, this);
    makeLabel(cfgValue, "1.0", kOscCol, juce::Justification::centredRight, this);
    makeLabel(cfgHint, "Classifier-free guidance", kDim, juce::Justification::centredLeft, this);
    cfgSlider.onValueChange = [this] {
        cfgValue.setText(juce::String(cfgSlider.getValue(), 1), juce::dontSendNotification);
    };

    // Seed (text field + random toggle)
    makeLabel(seedLabel, "Seed", kDim, juce::Justification::centredLeft, this);
    // Match the value-display style used by noiseValue / cfgValue / durValue
    // (kOscCol on dark surface) so the current seed reads as a first-class
    // number, not a grey decoration.
    seedEditor.setColour(juce::TextEditor::backgroundColourId, kSurface.brighter(0.04f));
    seedEditor.setColour(juce::TextEditor::textColourId, kOscCol);
    seedEditor.setColour(juce::TextEditor::outlineColourId, kBorder);
    seedEditor.setColour(juce::TextEditor::focusedOutlineColourId, kOscCol);
    seedEditor.setInputRestrictions(12, "0123456789");
    seedEditor.setJustification(juce::Justification::centredLeft);
    seedEditor.setText("123456789", false);
    syncSeedEditorFont(14.0f);
    seedEditor.setBufferedToImage(true);
    addAndMakeVisible(seedEditor);

    seedEditor.onReturnKey = [this] { triggerGeneration(); };
    seedEditor.onTextChange = [this] { syncSeedEditorFont(seedEditor.getFont().getHeight()); };

    randomSeedToggle.setColour(juce::TextButton::buttonColourId, kSurface);
    randomSeedToggle.setColour(juce::TextButton::buttonOnColourId, kOscCol);
    // kDim is too faint for a small button label sitting beside a coloured
    // seed value — bump the off-state text so "Random" stays legible.
    randomSeedToggle.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffc8cdd8));
    randomSeedToggle.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    randomSeedToggle.setClickingTogglesState(true);
    randomSeedToggle.setToggleState(false, juce::dontSendNotification);
    randomSeedToggle.onClick = [this] {
        syncSeedEditorEnabledState();
    };
    addAndMakeVisible(randomSeedToggle);
    syncSeedEditorEnabledState();

    // Model selector — fixed 3 slots, always visible (disabled = gray until model found)
    {
        const char* slotLabels[kNumModelSlots] = { "SA Open 1.0", "SA Small", "AudioLDM2" };
        for (int i = 0; i < kNumModelSlots; ++i)
        {
            modelBtns[i].setButtonText(slotLabels[i]);
            modelBtns[i].setColour(juce::TextButton::buttonColourId, kSurface);
            modelBtns[i].setColour(juce::TextButton::buttonOnColourId, kOscCol);
            modelBtns[i].setColour(juce::TextButton::textColourOffId, kDim);
            modelBtns[i].setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            modelBtns[i].setClickingTogglesState(true);
            modelBtns[i].setRadioGroupId(1004);
            int edges = 0;
            if (i > 0) edges |= juce::Button::ConnectedOnLeft;
            if (i < kNumModelSlots - 1) edges |= juce::Button::ConnectedOnRight;
            modelBtns[i].setConnectedEdges(edges);
            modelBtns[i].setEnabled(false);  // gray until Python reports availability
            modelBtns[i].setAlpha(0.3f);
            modelBtns[i].onClick = [this, i]()
            {
                if (!modelBtns[i].getToggleState()) return;
                auto model = modelSlotIds[i];
                if (model.isEmpty() || generating) return;

                // Apply model-specific parameter defaults
                auto& apvts = processorRef.getValueTreeState();
                bool isSmall = model.containsIgnoreCase("small");
                bool isAudioLDM2 = model.containsIgnoreCase("audioldm");
                float defaultSteps = isSmall ? 8.0f : (isAudioLDM2 ? 50.0f : 20.0f);
                float defaultCfg   = isSmall ? 1.0f : (isAudioLDM2 ? 3.5f : 7.0f);
                apvts.getParameter(PID::infSteps)->setValueNotifyingHost(
                    apvts.getParameter(PID::infSteps)->convertTo0to1(defaultSteps));
                apvts.getParameter(PID::genCfg)->setValueNotifyingHost(
                    apvts.getParameter(PID::genCfg)->convertTo0to1(defaultCfg));

                // Preload model in background so first generate is instant
                if (onStatusChanged) onStatusChanged("Loading " + model + "...", true);
                generateButton.setEnabled(false);

                auto pipePtr = processorRef.getPipeInferencePtr();
                juce::String device = defaultInferenceDevice_;
                juce::Component::SafePointer<PromptPanel> safeThis(this);
                std::thread([safeThis, pipePtr, model, device]()
                {
                    bool ok = pipePtr->preload(model, device);
                    juce::MessageManager::callAsync([safeThis, ok, model]()
                    {
                        if (auto* self = safeThis.getComponent())
                        {
                            if (!self->generating)
                                self->generateButton.setEnabled(true);
                            if (self->onStatusChanged)
                                self->onStatusChanged(ok ? model + " ready" : model + " load failed", false);
                        }
                    });
                }).detach();
            };
            addAndMakeVisible(modelBtns[i]);
        }
    }

    // Generate button is now in MainPanel — keep internal for triggerGeneration()
    generateButton.setVisible(false);

    makeLabel(infoLabel, "", kDim, juce::Justification::centredLeft, this);

    // APVTS
    auto& apvts = processor.getValueTreeState();
    alphaA  = std::make_unique<Attachment>(apvts, PID::genAlpha, alphaSlider);
    magA    = std::make_unique<Attachment>(apvts, PID::genMagnitude, magnitudeSlider);
    noiseA  = std::make_unique<Attachment>(apvts, PID::genNoise, noiseSlider);
    durA    = std::make_unique<Attachment>(apvts, PID::genDuration, durationSlider);
    stepsA  = std::make_unique<Attachment>(apvts, PID::infSteps, stepsSlider);
    cfgA    = std::make_unique<Attachment>(apvts, PID::genCfg, cfgSlider);
    if (auto* startParam = apvts.getParameter(PID::genStart))
        startParam->setValueNotifyingHost(0.0f);

    startTimerHz(10);  // poll for device availability + drift regen + ghost
}

int PromptPanel::getPreferredHeightForWidth(int width) const
{
    const float f = preferredPromptFontForWidth(width);
    const int rowH = juce::roundToInt(f * kPromptRow);
    const int sliderH = juce::roundToInt(f * kPromptSlider);
    const int inputH = juce::roundToInt(f * kPromptInput);
    const int gap = juce::roundToInt(f * kPromptGap);
    const int compactRowH = juce::roundToInt(f * kPromptCompactRow);
    const int compactCtrlH = juce::roundToInt(f * kPromptCompactCtrl);

    return (compactRowH + 2) + gap
         + rowH + inputH + gap
         + rowH + inputH + gap * 2
         + rowH + sliderH + gap
         + (compactRowH + compactCtrlH + gap) * 3
         + gap + compactRowH;
}

void PromptPanel::timerCallback()
{
    if (!devicesPopulated && processorRef.isPipeInferenceReady())
    {
        populateDeviceChoice();
        populateModelSelector();
        // Don't stop timer — continue for drift regen polling + ghost updates
    }

    // Ghost indicators for drift-modulated sliders — update every tick
    alphaGhostValue_ = processorRef.modulatedValues.driftAlpha.load(std::memory_order_relaxed);
    magGhostValue_ = processorRef.modulatedValues.driftMagnitude.load(std::memory_order_relaxed);
    noiseGhostValue_ = processorRef.modulatedValues.driftNoise.load(std::memory_order_relaxed);
    repaint(alphaSlider.getBounds().expanded(4));
    repaint(magnitudeSlider.getBounds().expanded(4));
    repaint(noiseSlider.getBounds().expanded(4));

    // Auto-regen polling
    pollDriftRegen();
}

void PromptPanel::paint(juce::Graphics& g)
{
    // Model switchbox border (always 3 fixed slots)
    paintSwitchBoxBorder(g, modelSwitchBounds);
}

void PromptPanel::paintOverChildren(juce::Graphics& g)
{
    auto drawGhost = [&](juce::Slider& slider, float ghostVal) {
        if (std::isnan(ghostVal)) return;
        auto sb = slider.getBounds();
        double norm = slider.valueToProportionOfLength(static_cast<double>(ghostVal));
        norm = juce::jlimit(0.0, 1.0, norm);
        int thumbW = slider.getLookAndFeel().getSliderThumbRadius(slider) * 2;
        int trackX = sb.getX() + thumbW / 2;
        int trackW = sb.getWidth() - thumbW;
        float gx = static_cast<float>(trackX) + static_cast<float>(trackW) * static_cast<float>(norm);
        float gy = static_cast<float>(sb.getCentreY());
        float r = static_cast<float>(sb.getHeight()) * 0.28f;
        g.setColour(juce::Colour(0xccff9800)); // orange ghost
        g.fillEllipse(gx - r, gy - r, r * 2.0f, r * 2.0f);
    };

    drawGhost(alphaSlider, alphaGhostValue_);
    drawGhost(magnitudeSlider, magGhostValue_);
    drawGhost(noiseSlider, noiseGhostValue_);
}

void PromptPanel::resized()
{
    auto b = getLocalBounds();
    float w = static_cast<float>(b.getWidth());
    int pad = juce::roundToInt(w * kPromptPadFactor);
    auto area = b.reduced(pad);

    float f = juce::jlimit(10.0f, 20.0f,
        (static_cast<float>(area.getHeight()) - 2.0f) / kPromptContentUnits);
    int rowH = juce::roundToInt(f * kPromptRow);
    int sliderH = juce::roundToInt(f * kPromptSlider);
    int inputH = juce::roundToInt(f * kPromptInput);
    int gap = juce::roundToInt(f * kPromptGap);
    int compactRowH = juce::roundToInt(f * kPromptCompactRow);
    int compactCtrlH = juce::roundToInt(f * kPromptCompactCtrl);

    auto setFs = [](juce::Label& l, float size) { l.setFont(juce::FontOptions(size)); };

    // ── Model selector switchbox at top (compact, fixed 3 slots) ──
    {
        auto modelRow = area.removeFromTop(compactRowH + 2);
        int cellW = juce::roundToInt(f * 5.5f);
        for (int i = 0; i < kNumModelSlots; ++i)
            modelBtns[i].setBounds(modelRow.removeFromLeft(cellW));
        modelSwitchBounds = modelBtns[0].getBounds()
            .getUnion(modelBtns[kNumModelSlots - 1].getBounds());
        area.removeFromTop(gap);
    }

    alphaHint.setVisible(false);
    magHint.setVisible(false);
    noiseHint.setVisible(false);
    durHint.setVisible(false);
    stepsHint.setVisible(false);
    cfgHint.setVisible(false);

    // --- Main slider layout: [Label ... Value] \n [slider] ---
    auto layoutSlider = [&](juce::Label& label, juce::Slider& slider, juce::Label& value)
    {
        setFs(label, f);
        setFs(value, f);
        auto hdr = area.removeFromTop(rowH);
        label.setBounds(hdr.removeFromLeft(hdr.getWidth() * 2 / 3));
        value.setBounds(hdr);
        slider.setBounds(area.removeFromTop(sliderH));
        area.removeFromTop(gap);
    };

    // Prompt A
    setFs(promptALabel, f);
    promptALabel.setBounds(area.removeFromTop(rowH));
    promptAEditor.setFont(juce::FontOptions(f));
    promptAEditor.setBounds(area.removeFromTop(inputH));
    area.removeFromTop(gap);

    // Prompt B
    setFs(promptBLabel, f);
    promptBLabel.setBounds(area.removeFromTop(rowH));
    promptBEditor.setFont(juce::FontOptions(f));
    promptBEditor.setBounds(area.removeFromTop(inputH));
    area.removeFromTop(gap * 2);

    // Alpha
    layoutSlider(alphaLabel, alphaSlider, alphaValue);

    // --- Compact params: 2 columns ---
    int colGap = juce::roundToInt(w * 0.03f);

    auto layoutCompactPair = [&](juce::Label& lbl1, juce::Slider& sl1, juce::Label& val1,
                                  juce::Label& lbl2, juce::Slider& sl2, juce::Label& val2)
    {
        int colW = (area.getWidth() - colGap) / 2;

        auto hdrRow = area.removeFromTop(compactRowH);
        auto leftHdr = hdrRow.removeFromLeft(colW);
        hdrRow.removeFromLeft(colGap);
        auto rightHdr = hdrRow;

        setFs(lbl1, f); setFs(val1, f);
        lbl1.setBounds(leftHdr.removeFromLeft(leftHdr.getWidth() * 2 / 3));
        val1.setBounds(leftHdr);
        setFs(lbl2, f); setFs(val2, f);
        lbl2.setBounds(rightHdr.removeFromLeft(rightHdr.getWidth() * 2 / 3));
        val2.setBounds(rightHdr);

        auto slRow = area.removeFromTop(compactCtrlH);
        sl1.setBounds(slRow.removeFromLeft(colW));
        slRow.removeFromLeft(colGap);
        sl2.setBounds(slRow);
        area.removeFromTop(gap);
    };

    auto layoutDurationSeedRow = [&]
    {
        int colW = (area.getWidth() - colGap) / 2;

        auto hdrRow = area.removeFromTop(compactRowH);
        auto leftHdr = hdrRow.removeFromLeft(colW);
        hdrRow.removeFromLeft(colGap);
        auto rightHdr = hdrRow;

        setFs(durLabel, f);
        setFs(durValue, f);
        durLabel.setBounds(leftHdr.removeFromLeft(leftHdr.getWidth() * 2 / 3));
        durValue.setBounds(leftHdr);
        setFs(seedLabel, f);
        seedLabel.setBounds(rightHdr);

        auto controlRow = area.removeFromTop(compactCtrlH);
        durationSlider.setBounds(controlRow.removeFromLeft(colW));
        controlRow.removeFromLeft(colGap);

        auto seedRow = controlRow.reduced(0, 1);
        int toggleW = juce::roundToInt(seedRow.getWidth() * 0.34f);
        randomSeedToggle.setBounds(seedRow.removeFromRight(toggleW));
        syncSeedEditorFont(f * 0.92f);
        seedEditor.setBounds(seedRow);

        area.removeFromTop(gap);
    };

    layoutCompactPair(magLabel, magnitudeSlider, magValue,
                      noiseLabel, noiseSlider, noiseValue);
    layoutCompactPair(stepsLabel, stepsSlider, stepsValue,
                      cfgLabel, cfgSlider, cfgValue);
    layoutDurationSeedRow();

    // Info label at the bottom of the sequential layout
    area.removeFromTop(gap);
    setFs(infoLabel, f);
    infoLabel.setBounds(area.removeFromTop(compactRowH));
}

void PromptPanel::loadPresetData(const juce::String& promptA, const juce::String& promptB,
                                  int seed, bool randomSeed,
                                  const juce::String& device,
                                  const juce::String& model)
{
    juce::ignoreUnused(device);
    promptAEditor.setText(promptA, false);
    promptBEditor.setText(promptB, false);
    lastGenPromptA_.clear();
    lastGenPromptB_.clear();
    randomSeedToggle.setToggleState(randomSeed, juce::dontSendNotification);
    syncSeedEditorDisplay(seed, true);
    syncSeedEditorEnabledState();
    if (auto* startParam = processorRef.getValueTreeState().getParameter(PID::genStart))
        startParam->setValueNotifyingHost(0.0f);

    // Select model from preset (match by model directory name)
    if (model.isNotEmpty())
    {
        if (modelsPopulated)
        {
            for (int i = 0; i < kNumModelSlots; ++i)
            {
                if (modelSlotIds[i] == model)
                {
                    modelBtns[i].setToggleState(true, juce::dontSendNotification);
                    break;
                }
            }
        }
        else
        {
            pendingModel_ = model;
        }
    }
}

void PromptPanel::populateDeviceChoice()
{
    auto& pipeInf = processorRef.getPipeInference();
    defaultInferenceDevice_ = pipeInf.getDefaultDevice();
    if (defaultInferenceDevice_.isEmpty())
    {
        auto& devs = pipeInf.getAvailableDevices();
        if (!devs.isEmpty())
            defaultInferenceDevice_ = devs[0];
    }
    devicesPopulated = true;
}

void PromptPanel::populateModelSelector()
{
    auto& pipeInf = processorRef.getPipeInference();
    auto& models = pipeInf.getAvailableModels();

    // Match available models to fixed slots by pattern
    // Slot 0: SA Open 1.0, Slot 1: SA Small, Slot 2: AudioLDM2
    for (int i = 0; i < kNumModelSlots; ++i)
        modelSlotIds[i] = {};

    int firstAvail = -1;
    for (auto& m : models)
    {
        int slot = -1;
        if (m.containsIgnoreCase("small"))                   slot = 1;  // check "small" first
        else if (m.containsIgnoreCase("stable-audio-open"))  slot = 0;
        else if (m.containsIgnoreCase("audioldm") ||
                 m.containsIgnoreCase("audio-ldm"))          slot = 2;

        if (slot >= 0 && slot < kNumModelSlots)
        {
            modelSlotIds[slot] = m;
            modelBtns[slot].setEnabled(true);
            modelBtns[slot].setAlpha(1.0f);
            if (firstAvail < 0) firstAvail = slot;
        }
    }

    // Select model: pending preset model > SA Small (slot 1) > first available
    int selectIdx = -1;
    if (pendingModel_.isNotEmpty())
    {
        for (int i = 0; i < kNumModelSlots; ++i)
            if (modelSlotIds[i] == pendingModel_) { selectIdx = i; break; }
        pendingModel_ = {};
    }
    if (selectIdx < 0)
        selectIdx = modelSlotIds[1].isNotEmpty() ? 1 : firstAvail;
    if (selectIdx >= 0)
        modelBtns[selectIdx].setToggleState(true, juce::dontSendNotification);

    modelsPopulated = true;
    resized();
}

void PromptPanel::refreshInferenceChoices()
{
    auto selectedModel = getSelectedModel();

    if (selectedModel.isNotEmpty())
        pendingModel_ = selectedModel;

    devicesPopulated = false;
    modelsPopulated = false;
    defaultInferenceDevice_.clear();

    for (int i = 0; i < kNumModelSlots; ++i)
    {
        modelSlotIds[i].clear();
        modelBtns[i].setToggleState(false, juce::dontSendNotification);
        modelBtns[i].setEnabled(false);
        modelBtns[i].setAlpha(0.3f);
    }

    if (!processorRef.isPipeInferenceReady())
        return;

    populateDeviceChoice();
    populateModelSelector();
}

juce::String PromptPanel::getSelectedModel() const
{
    for (int i = 0; i < kNumModelSlots; ++i)
        if (modelBtns[i].getToggleState() && modelSlotIds[i].isNotEmpty())
            return modelSlotIds[i];
    return {};
}

void PromptPanel::syncSeedEditorEnabledState()
{
    const bool randomSeed = randomSeedToggle.getToggleState();
    seedEditor.setEnabled(!randomSeed);
    seedEditor.setAlpha(randomSeed ? 0.3f : 1.0f);
}

void PromptPanel::syncSeedEditorFont(float size)
{
    juce::Font font { juce::FontOptions(size) };
    seedEditor.setFont(font);
    seedEditor.applyFontToAllText(font);
}

void PromptPanel::syncSeedEditorDisplay(int seed, bool force)
{
    const bool randomSeed = randomSeedToggle.getToggleState();
    const bool userEditingFixedSeed = !randomSeed && seedEditor.hasKeyboardFocus(true);

    if (!force && userEditingFixedSeed)
        return;

    const auto seedText = juce::String(seed);
    if (force || seedEditor.getText() != seedText)
        seedEditor.setText(seedText, false);

    syncSeedEditorFont(seedEditor.getFont().getHeight());
}

void PromptPanel::triggerGenerationWithOffsets(std::vector<std::pair<int, float>> offsets)
{
    pendingOffsets_ = std::move(offsets);
    triggerGeneration();
}

// ──────────────────────────────────────────────────────────────────────────────
// Shared request builder
// ──────────────────────────────────────────────────────────────────────────────
PipeInference::Request PromptPanel::buildInferenceRequest(
    float alphaOverride, std::map<juce::String, float> axesOverride,
    float noiseOverride, float magnitudeOverride)
{
    auto& apvts = processorRef.getValueTreeState();
    float alpha = std::isnan(alphaOverride)
                      ? apvts.getRawParameterValue(PID::genAlpha)->load()
                      : alphaOverride;
    float magnitude = std::isnan(magnitudeOverride)
                          ? apvts.getRawParameterValue(PID::genMagnitude)->load()
                          : magnitudeOverride;
    float noiseSigma = std::isnan(noiseOverride)
                           ? apvts.getRawParameterValue(PID::genNoise)->load()
                           : noiseOverride;
    float duration = apvts.getRawParameterValue(PID::genDuration)->load();
    int steps = static_cast<int>(apvts.getRawParameterValue(PID::infSteps)->load());
    float cfgScale = apvts.getRawParameterValue(PID::genCfg)->load();
    int seed = randomSeedToggle.getToggleState() ? -1 : seedEditor.getText().getIntValue();

    PipeInference::Request req;
    req.promptA = promptAEditor.getText().trim();
    auto promptB = promptBEditor.getText().trim();
    if (promptB.isNotEmpty()) req.promptB = promptB;
    req.alpha = alpha;
    req.magnitude = magnitude;
    req.noiseSigma = noiseSigma;
    req.durationSeconds = duration;
    req.startPosition = 0.0f;
    req.steps = steps;
    req.cfgScale = cfgScale;
    req.seed = seed;
    req.device = defaultInferenceDevice_;
    req.model = getSelectedModel();
    req.dimensionOffsets = std::move(pendingOffsets_);
    req.semanticAxes = axesOverride.empty() ? std::move(pendingAxes_) : std::move(axesOverride);
    return req;
}

// ──────────────────────────────────────────────────────────────────────────────
// Manual generation (Generate button / Enter)
// ──────────────────────────────────────────────────────────────────────────────
void PromptPanel::triggerGeneration()
{
    if (generating) return;
    auto promptA = promptAEditor.getText().trim();
    if (promptA.isEmpty()) return;

    if (!processorRef.isInferenceReady())
    {
        // isInferenceReady() is just "pipe connection up" — there is no separate
        // model-loaded state (models are selected per-request). The old "No model
        // loaded" string was misleading when the actual failure was a missing
        // backend install (plugin context with no companion Standalone).
        if (onStatusChanged) onStatusChanged("Backend not connected", false);
        return;
    }

    // Populate device/model info from Python if not yet done
    auto& pipeInf = processorRef.getPipeInference();
    if (!devicesPopulated && pipeInf.isReady())
    {
        populateDeviceChoice();
        populateModelSelector();
    }

    generating = true;
    generateButton.setEnabled(false);
    if (onStatusChanged) onStatusChanged("generating...", true);

    auto req = buildInferenceRequest();
    auto deviceForLabel = req.device.isEmpty() ? pipeInf.getDefaultDevice() : req.device;
    auto modelForLabel = req.model.isEmpty() ? pipeInf.getDefaultModel() : req.model;
    auto pipePtr = processorRef.getPipeInferencePtr();
    juce::Component::SafePointer<PromptPanel> safeThis(this);
    std::thread([safeThis, pipePtr, req, deviceForLabel, modelForLabel]()
    {
        auto result = pipePtr->generate(req);
        juce::MessageManager::callAsync([safeThis, result = std::move(result), req, deviceForLabel, modelForLabel]()
        {
            if (auto* self = safeThis.getComponent())
            {
                auto& processor = self->processorRef;
                self->generating = false;
                self->generateButton.setEnabled(true);
                if (result.success)
                {
                    processor.loadGeneratedAudio(result.audio, 44100.0);
                    processor.setLastDevice(deviceForLabel);
                    processor.setLastModel(modelForLabel);
                    processor.setLastSeed(result.seed);
                    auto promptA = self->promptAEditor.getText().trim();
                    auto promptB = self->promptBEditor.getText().trim();
                    processor.setLastPrompts(promptA, promptB);
                    self->lastGenPromptA_ = promptA;
                    self->lastGenPromptB_ = promptB;
                    self->syncSeedEditorDisplay(result.seed);
                    auto info = juce::String(result.generationTimeMs / 1000.0f, 1) + "s | seed "
                                + juce::String(result.seed) + " | " + modelForLabel
                                + " | " + deviceForLabel;
                    if (self->onStatusChanged) self->onStatusChanged(info, false);

                    if (!result.embeddingA.empty())
                    {
                        processor.setLastEmbeddings(result.embeddingA, result.embeddingB);
                        auto baseline = result.embeddingBaseline;
                        if (baseline.size() != result.embeddingA.size())
                        {
                            baseline = DimensionExplorer::estimateBaselineValues(
                                result.embeddingA, result.embeddingB,
                                req.alpha, req.magnitude);
                        }
                        if (self->onEmbeddingsReady)
                            self->onEmbeddingsReady(result.embeddingA, result.embeddingB, baseline);
                    }
                }
                else
                {
                    if (self->onStatusChanged) self->onStatusChanged(result.errorMessage, false);
                }
            }
        });
    }).detach();
}

// ──────────────────────────────────────────────────────────────────────────────
// Drift auto-regeneration
// ──────────────────────────────────────────────────────────────────────────────
void PromptPanel::triggerDriftRegeneration(float effectiveAlpha,
                                            std::map<juce::String, float> effectiveAxes,
                                            float effectiveNoise,
                                            float effectiveMagnitude,
                                            bool /*holdForBar*/)
{
    if (generating) return;
    if (promptAEditor.getText().trim().isEmpty()) return;
    if (!processorRef.isPipeInferenceReady()) return;

    generating = true;
    generateButton.setEnabled(false);
    if (onStatusChanged) onStatusChanged("auto regen...", true);

    lastGenAlpha_ = effectiveAlpha;
    lastGenNoise_ = effectiveNoise;
    lastGenMagnitude_ = effectiveMagnitude;
    lastGenAxes_ = effectiveAxes;

    auto req = buildInferenceRequest(effectiveAlpha, effectiveAxes, effectiveNoise, effectiveMagnitude);
    auto deviceForLabel = req.device.isEmpty()
        ? processorRef.getPipeInference().getDefaultDevice() : req.device;
    auto modelForLabel = req.model.isEmpty()
        ? processorRef.getPipeInference().getDefaultModel() : req.model;
    auto pipePtr = processorRef.getPipeInferencePtr();
    juce::Component::SafePointer<PromptPanel> safeThis(this);
    std::thread([safeThis, pipePtr, req, deviceForLabel, modelForLabel]()
    {
        auto result = pipePtr->generate(req);
        juce::MessageManager::callAsync([safeThis, result = std::move(result), req, deviceForLabel, modelForLabel]()
        {
            if (auto* self = safeThis.getComponent())
            {
                auto& processor = self->processorRef;
                self->generating = false;
                self->generateButton.setEnabled(true);
                if (result.success)
                {
                    auto newAudio = result.audio;
                    float xfadeMs = processor.getValueTreeState()
                        .getRawParameterValue(PID::driftCrossfade)->load();
                    int xfadeSamples = juce::roundToInt(xfadeMs * 0.001f * 44100.0f);
                    if (xfadeSamples > 0)
                    {
                        const auto& oldRaw = processor.getGeneratedAudioRaw();
                        if (oldRaw.getNumSamples() > 0)
                            applyDriftCrossfade(newAudio, oldRaw, xfadeSamples);
                    }
                    processor.loadGeneratedAudio(newAudio, 44100.0);
                    auto promptA = self->promptAEditor.getText().trim();
                    auto promptB = self->promptBEditor.getText().trim();
                    processor.setLastDevice(deviceForLabel);
                    processor.setLastModel(modelForLabel);
                    processor.setLastSeed(result.seed);
                    processor.setLastPrompts(promptA, promptB);
                    self->lastGenPromptA_ = promptA;
                    self->lastGenPromptB_ = promptB;
                    self->syncSeedEditorDisplay(result.seed);
                    auto info = juce::String(result.generationTimeMs / 1000.0f, 1) + "s | auto regen";
                    if (self->onStatusChanged) self->onStatusChanged(info, false);

                    if (!result.embeddingA.empty())
                    {
                        processor.setLastEmbeddings(result.embeddingA, result.embeddingB);
                        auto baseline = result.embeddingBaseline;
                        if (baseline.size() != result.embeddingA.size())
                        {
                            baseline = DimensionExplorer::estimateBaselineValues(
                                result.embeddingA, result.embeddingB,
                                req.alpha, req.magnitude);
                        }
                        if (self->onEmbeddingsReady)
                            self->onEmbeddingsReady(result.embeddingA, result.embeddingB, baseline);
                    }
                }
                else
                {
                    if (self->onStatusChanged) self->onStatusChanged(result.errorMessage, false);
                }
            }
        });
    }).detach();
}

// ──────────────────────────────────────────────────────────────────────────────
// Drift regen polling (called from timerCallback at 10 Hz)
// ──────────────────────────────────────────────────────────────────────────────
void PromptPanel::pollDriftRegen()
{
    if (generating) return;

    int regenMode = processorRef.driftRegenMode.load(std::memory_order_relaxed);
    if (regenMode == 0) return; // Manual — no auto-regen

    // Beat-based cooldown: modes 2-5 = max 1/4/8/16 beats
    if (regenMode >= 2)
    {
        static constexpr int beatCounts[] = { 0, 0, 1, 4, 16 };
        int beats = beatCounts[juce::jlimit(0, 4, regenMode)];
        float bpm = processorRef.driftRegenBpm.load(std::memory_order_relaxed);
        double cooldownMs = (beats * 60000.0) / static_cast<double>(juce::jmax(1.0f, bpm));
        double now = juce::Time::getMillisecondCounterHiRes();
        if ((now - lastRegenTimeMs_) < cooldownMs)
            return; // cooldown not elapsed
    }

    // Read effective drift values from processor atomics
    auto& mv = processorRef.modulatedValues;
    float effAlpha = mv.driftAlpha.load(std::memory_order_relaxed);
    float dAxis1 = mv.driftAxis1.load(std::memory_order_relaxed);
    float dAxis2 = mv.driftAxis2.load(std::memory_order_relaxed);
    float dAxis3 = mv.driftAxis3.load(std::memory_order_relaxed);
    float effNoise = mv.driftNoise.load(std::memory_order_relaxed);
    float effMag   = mv.driftMagnitude.load(std::memory_order_relaxed);

    // Build effective axes: AxesPanel base values + per-slot drift offsets
    std::map<juce::String, float> effAxes;
    if (getAxisValuesCallback)
    {
        float o1 = std::isnan(dAxis1) ? 0.0f : dAxis1;
        float o2 = std::isnan(dAxis2) ? 0.0f : dAxis2;
        float o3 = std::isnan(dAxis3) ? 0.0f : dAxis3;
        effAxes = getAxisValuesCallback(o1, o2, o3);
    }

    // Check if values changed enough from last generation
    constexpr float DRIFT_THRESHOLD = 0.005f;
    bool alphaChanged = !std::isnan(effAlpha) &&
        (std::isnan(lastGenAlpha_) || std::abs(effAlpha - lastGenAlpha_) > DRIFT_THRESHOLD);

    bool noiseChanged = !std::isnan(effNoise) &&
        (std::isnan(lastGenNoise_) || std::abs(effNoise - lastGenNoise_) > DRIFT_THRESHOLD);

    bool magChanged = !std::isnan(effMag) &&
        (std::isnan(lastGenMagnitude_) || std::abs(effMag - lastGenMagnitude_) > DRIFT_THRESHOLD);

    auto promptA = promptAEditor.getText().trim();
    auto promptB = promptBEditor.getText().trim();
    bool promptChanged = (promptA != lastGenPromptA_) || (promptB != lastGenPromptB_);

    bool axesChanged = false;
    for (auto& [key, val] : effAxes)
    {
        auto it = lastGenAxes_.find(key);
        if (it == lastGenAxes_.end() || std::abs(val - it->second) > DRIFT_THRESHOLD)
        {
            axesChanged = true;
            break;
        }
    }

    bool randomRegen = randomSeedToggle.getToggleState();
    if (!alphaChanged && !axesChanged && !noiseChanged && !magChanged && !promptChanged && !randomRegen)
        return;

    auto& apvts = processorRef.getValueTreeState();
    float genAlpha = std::isnan(effAlpha)
        ? apvts.getRawParameterValue(PID::genAlpha)->load() : effAlpha;
    float genNoise = std::isnan(effNoise)
        ? apvts.getRawParameterValue(PID::genNoise)->load() : effNoise;
    float genMag = std::isnan(effMag)
        ? apvts.getRawParameterValue(PID::genMagnitude)->load() : effMag;

    lastRegenTimeMs_ = juce::Time::getMillisecondCounterHiRes();
    triggerDriftRegeneration(genAlpha, effAxes, genNoise, genMag, false);
}
