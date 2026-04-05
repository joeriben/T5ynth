#include "PromptPanel.h"
#include "GuiHelpers.h"
#include "../PluginProcessor.h"
#include <thread>
#include <cmath>

// Colors from GuiHelpers.h (kAccent, kDim, kDim, kSurface)

// Equal-power crossfade between old and new audio buffers.
// Blends the first xfadeSamples of newBuf with corresponding samples from oldBuf.
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
            // Equal-power: sin/cos curves for smooth energy transition
            float gainNew = std::sin(t * juce::MathConstants<float>::halfPi);
            float gainOld = std::cos(t * juce::MathConstants<float>::halfPi);
            dst[i] = src[i] * gainOld + dst[i] * gainNew;
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
    addAndMakeVisible(promptAEditor);

    makeLabel(promptBLabel, "Prompt B (optional, for interpolation)", kDim, juce::Justification::centredLeft, this);
    promptBEditor.setMultiLine(false);
    promptBEditor.setText("glass breaking");
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
    makeLabel(durValue, "1.0s", kOscCol, juce::Justification::centredRight, this);
    makeLabel(durHint, "Audio length (seconds)", kDim, juce::Justification::centredLeft, this);
    durationSlider.onValueChange = [this] {
        durValue.setText(juce::String(durationSlider.getValue(), 1) + "s", juce::dontSendNotification);
    };

    // Start Position
    makeSlider(startSlider, this);
    makeLabel(startLabel, "Start", kDim, juce::Justification::centredLeft, this);
    makeLabel(startValue, "0%", kOscCol, juce::Justification::centredRight, this);
    makeLabel(startHint, "0% = attack, higher = sustained", kDim, juce::Justification::centredLeft, this);
    startSlider.onValueChange = [this] {
        startValue.setText(juce::String(juce::roundToInt(startSlider.getValue() * 100.0)) + "%", juce::dontSendNotification);
    };

    // Steps
    makeSlider(stepsSlider, this);
    makeLabel(stepsLabel, "Steps", kDim, juce::Justification::centredLeft, this);
    makeLabel(stepsValue, "20", kOscCol, juce::Justification::centredRight, this);
    makeLabel(stepsHint, "More = higher quality", kDim, juce::Justification::centredLeft, this);
    stepsSlider.onValueChange = [this] {
        stepsValue.setText(juce::String(juce::roundToInt(stepsSlider.getValue())), juce::dontSendNotification);
    };

    // CFG
    makeSlider(cfgSlider, this);
    makeLabel(cfgLabel, "CFG", kDim, juce::Justification::centredLeft, this);
    makeLabel(cfgValue, "7.0", kOscCol, juce::Justification::centredRight, this);
    makeLabel(cfgHint, "Classifier-free guidance", kDim, juce::Justification::centredLeft, this);
    cfgSlider.onValueChange = [this] {
        cfgValue.setText(juce::String(cfgSlider.getValue(), 1), juce::dontSendNotification);
    };

    // Seed (text field + random toggle)
    makeLabel(seedLabel, "Seed", kDim, juce::Justification::centredLeft, this);
    seedEditor.setColour(juce::TextEditor::backgroundColourId, kSurface);
    seedEditor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    seedEditor.setColour(juce::TextEditor::outlineColourId, kBorder);
    seedEditor.setInputRestrictions(12, "0123456789");
    seedEditor.setText("123456789", false);
    addAndMakeVisible(seedEditor);

    seedEditor.onReturnKey = [this] { triggerGeneration(); };

    randomSeedToggle.setColour(juce::TextButton::buttonColourId, kSurface);
    randomSeedToggle.setColour(juce::TextButton::buttonOnColourId, kOscCol);
    randomSeedToggle.setColour(juce::TextButton::textColourOffId, kDim);
    randomSeedToggle.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    randomSeedToggle.setClickingTogglesState(true);
    randomSeedToggle.setToggleState(false, juce::dontSendNotification);
    randomSeedToggle.onClick = [this] {
        seedEditor.setEnabled(!randomSeedToggle.getToggleState());
        seedEditor.setAlpha(randomSeedToggle.getToggleState() ? 0.3f : 1.0f);
    };
    addAndMakeVisible(randomSeedToggle);
    seedEditor.setEnabled(true);
    seedEditor.setAlpha(1.0f);

    // Device selector — GPU / CPU toggle
    for (auto* btn : { &gpuBtn, &cpuBtn })
    {
        btn->setColour(juce::TextButton::buttonColourId, kSurface);
        btn->setColour(juce::TextButton::buttonOnColourId, kOscCol);
        btn->setColour(juce::TextButton::textColourOffId, kDim);
        btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn->setClickingTogglesState(true);
        btn->setRadioGroupId(1003);
        addAndMakeVisible(btn);
    }
    gpuBtn.setToggleState(true, juce::dontSendNotification);
    gpuBtn.setConnectedEdges(juce::Button::ConnectedOnRight);
    cpuBtn.setConnectedEdges(juce::Button::ConnectedOnLeft);

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
                apvts.getParameter("gen_steps")->setValueNotifyingHost(
                    apvts.getParameter("gen_steps")->convertTo0to1(isSmall ? 8.0f : 20.0f));
                apvts.getParameter("gen_cfg")->setValueNotifyingHost(
                    apvts.getParameter("gen_cfg")->convertTo0to1(isSmall ? 1.0f : 7.0f));

                // Preload model in background so first generate is instant
                if (onStatusChanged) onStatusChanged("Loading " + model + "...", true);
                generateButton.setEnabled(false);

                auto& pipeInf = processorRef.getPipeInference();
                juce::String device = cpuBtn.getToggleState() ? "cpu" : gpuBackend_;
                std::thread([this, &pipeInf, model, device]()
                {
                    bool ok = pipeInf.preload(model, device);
                    juce::MessageManager::callAsync([this, ok, model]()
                    {
                        if (!generating)
                            generateButton.setEnabled(true);
                        if (onStatusChanged)
                            onStatusChanged(ok ? model + " ready" : model + " load failed", false);
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
    alphaA  = std::make_unique<Attachment>(apvts, "gen_alpha", alphaSlider);
    magA    = std::make_unique<Attachment>(apvts, "gen_magnitude", magnitudeSlider);
    noiseA  = std::make_unique<Attachment>(apvts, "gen_noise", noiseSlider);
    durA    = std::make_unique<Attachment>(apvts, "gen_duration", durationSlider);
    startA  = std::make_unique<Attachment>(apvts, "gen_start", startSlider);
    stepsA  = std::make_unique<Attachment>(apvts, "gen_steps", stepsSlider);
    cfgA    = std::make_unique<Attachment>(apvts, "gen_cfg", cfgSlider);

    startTimerHz(10);  // poll for device availability + drift regen + ghost
}

void PromptPanel::timerCallback()
{
    if (!devicesPopulated && processorRef.isPipeInferenceReady())
    {
        populateDeviceButtons();
        populateModelSelector();
        // Don't stop timer — continue for drift regen polling + ghost updates
    }

    // Ghost indicator for alpha slider (drift modulation)
    {
        float prev = alphaGhostValue_;
        alphaGhostValue_ = processorRef.modulatedValues.driftAlpha.load(std::memory_order_relaxed);
        // Only repaint if ghost changed visibility or position meaningfully
        bool prevVisible = !std::isnan(prev);
        bool nowVisible = !std::isnan(alphaGhostValue_);
        if (prevVisible != nowVisible || (prevVisible && nowVisible && std::abs(prev - alphaGhostValue_) > 0.005f))
            repaint(alphaSlider.getBounds().expanded(4));
    }

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
    if (std::isnan(alphaGhostValue_)) return;

    // Draw orange ghost circle on the alpha slider at the modulated position
    auto sb = alphaSlider.getBounds();
    double norm = alphaSlider.valueToProportionOfLength(static_cast<double>(alphaGhostValue_));
    norm = juce::jlimit(0.0, 1.0, norm);

    int thumbW = alphaSlider.getLookAndFeel().getSliderThumbRadius(alphaSlider) * 2;
    int trackX = sb.getX() + thumbW / 2;
    int trackW = sb.getWidth() - thumbW;
    float gx = static_cast<float>(trackX) + static_cast<float>(trackW) * static_cast<float>(norm);
    float gy = static_cast<float>(sb.getCentreY());
    float r = static_cast<float>(sb.getHeight()) * 0.28f;

    g.setColour(juce::Colour(0xccff9800)); // orange ghost
    g.fillEllipse(gx - r, gy - r, r * 2.0f, r * 2.0f);
}

void PromptPanel::resized()
{
    auto b = getLocalBounds();
    float w = static_cast<float>(b.getWidth());
    int pad = juce::roundToInt(w * 0.04f);
    auto area = b.reduced(pad);

    // ── Row heights in font-size units (single source of truth) ──
    constexpr float kRow        = 1.4f;   // label row
    constexpr float kSlider     = 1.2f;   // full slider
    constexpr float kInput      = 1.8f;   // text editor
    constexpr float kCompactRow = 1.2f;   // compact label row
    constexpr float kCompactSl  = 0.9f;   // compact slider
    constexpr float kGap        = 0.3f;   // gap

    // Total content budget (f-units):
    //   model:      kCompactRow + kGap                    = 1.5
    //   prompt A:   kRow + kInput + kGap                  = 3.5
    //   prompt B:   kRow + kInput + kGap*2                = 3.8
    //   3× slider:  (kRow + kSlider + kGap) * 3           = 8.7
    //   gap:        kGap                                  = 0.3
    //   2× compact: (kCompactRow + kCompactSl + kGap) * 2 = 4.8
    //   seed row:    1.65                                    = 1.65
    //                                               Total: 24.25
    constexpr float kContentUnits = 24.25f;
    constexpr float kHintExtra    = 5.2f;  // 3×1.1 + 2×0.94 hint rows

    float f = juce::jlimit(10.0f, 20.0f,
        (static_cast<float>(area.getHeight()) - 2.0f) / kContentUnits);
    float fHint = f * 0.85f;
    int rowH = juce::roundToInt(f * kRow);
    int sliderH = juce::roundToInt(f * kSlider);
    int hintH = juce::roundToInt(fHint * 1.3f);
    int inputH = juce::roundToInt(f * kInput);
    int gap = juce::roundToInt(f * kGap);
    int compactRowH = juce::roundToInt(f * kCompactRow);

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

    // Show hints only when the extra hint rows fit within available space
    bool showHints = static_cast<float>(area.getHeight())
                     > (kContentUnits + kHintExtra) * f + 2.0f;

    // --- Main slider layout: [Label ... Value] \n [slider] \n [hint?] ---
    auto layoutSlider = [&](juce::Label& label, juce::Slider& slider, juce::Label& value,
                            juce::Label* hint, float labelSize)
    {
        setFs(label, labelSize);
        setFs(value, labelSize);
        auto hdr = area.removeFromTop(rowH);
        label.setBounds(hdr.removeFromLeft(hdr.getWidth() * 2 / 3));
        value.setBounds(hdr);
        slider.setBounds(area.removeFromTop(sliderH));
        if (hint != nullptr)
        {
            if (showHints)
            {
                setFs(*hint, fHint);
                hint->setBounds(area.removeFromTop(hintH));
                hint->setVisible(true);
            }
            else
                hint->setVisible(false);
        }
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

    // Alpha, Magnitude, Noise
    layoutSlider(alphaLabel, alphaSlider, alphaValue, &alphaHint, f);
    layoutSlider(magLabel, magnitudeSlider, magValue, &magHint, f);
    layoutSlider(noiseLabel, noiseSlider, noiseValue, &noiseHint, f);

    area.removeFromTop(gap);

    // --- Compact params: 2 columns ---
    int compactSliderH = juce::roundToInt(f * 0.9f);
    int compactHintH = juce::roundToInt(fHint * 1.1f);
    int colGap = juce::roundToInt(w * 0.03f);

    auto layoutCompactPair = [&](juce::Label& lbl1, juce::Slider& sl1, juce::Label& val1, juce::Label* hint1,
                                  juce::Label& lbl2, juce::Slider& sl2, juce::Label& val2, juce::Label* hint2)
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

        auto slRow = area.removeFromTop(compactSliderH);
        sl1.setBounds(slRow.removeFromLeft(colW));
        slRow.removeFromLeft(colGap);
        sl2.setBounds(slRow);

        if (showHints && (hint1 != nullptr || hint2 != nullptr))
        {
            auto hintRow = area.removeFromTop(compactHintH);
            if (hint1) { setFs(*hint1, fHint); hint1->setBounds(hintRow.removeFromLeft(colW)); hint1->setVisible(true); }
            else hintRow.removeFromLeft(colW);
            hintRow.removeFromLeft(colGap);
            if (hint2) { setFs(*hint2, fHint); hint2->setBounds(hintRow); hint2->setVisible(true); }
        }
        else
        {
            if (hint1) hint1->setVisible(false);
            if (hint2) hint2->setVisible(false);
        }
        area.removeFromTop(gap);
    };

    layoutCompactPair(durLabel, durationSlider, durValue, &durHint,
                      startLabel, startSlider, startValue, &startHint);
    layoutCompactPair(stepsLabel, stepsSlider, stepsValue, &stepsHint,
                      cfgLabel, cfgSlider, cfgValue, &cfgHint);

    // Seed + Device row — buttons need height ≈ f/0.6 so LnF auto-font matches f
    {
        int seedRowH = juce::roundToInt(f * 1.65f);
        auto seedRow = area.removeFromTop(seedRowH);
        int btnW = juce::roundToInt(seedRow.getWidth() * 0.11f);
        gpuBtn.setBounds(seedRow.removeFromLeft(btnW));
        cpuBtn.setBounds(seedRow.removeFromLeft(btnW));
        seedRow.removeFromLeft(gap);
        setFs(seedLabel, f);
        int seedLabelW = juce::roundToInt(f * 2.5f);
        seedLabel.setBounds(seedRow.removeFromLeft(seedLabelW));
        int toggleW = juce::roundToInt(seedRow.getWidth() * 0.30f);
        randomSeedToggle.setBounds(seedRow.removeFromRight(toggleW));
        seedEditor.setFont(juce::FontOptions(f));
        seedEditor.setBounds(seedRow.reduced(0, 1));
    }

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
    promptAEditor.setText(promptA, false);
    promptBEditor.setText(promptB, false);
    seedEditor.setText(juce::String(seed), false);
    randomSeedToggle.setToggleState(randomSeed, juce::dontSendNotification);
    seedEditor.setEnabled(!randomSeed);
    seedEditor.setAlpha(randomSeed ? 0.3f : 1.0f);

    // Select device from preset — "cpu" selects CPU, anything else selects GPU
    if (device.equalsIgnoreCase("cpu"))
        cpuBtn.setToggleState(true, juce::dontSendNotification);
    else
        gpuBtn.setToggleState(true, juce::dontSendNotification);

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

void PromptPanel::populateDeviceButtons()
{
    auto& devs = processorRef.getPipeInference().getAvailableDevices();
    // Find the GPU backend (mps or cuda) — first non-cpu device
    gpuBackend_ = {};
    for (auto& d : devs)
    {
        if (d != "cpu") { gpuBackend_ = d; break; }
    }
    // If no GPU available, disable the GPU button and select CPU
    if (gpuBackend_.isEmpty())
    {
        gpuBtn.setEnabled(false);
        gpuBtn.setAlpha(0.3f);
        cpuBtn.setToggleState(true, juce::dontSendNotification);
    }
    devicesPopulated = true;
}

void PromptPanel::populateModelSelector()
{
    auto& pipeInf = processorRef.getPipeInference();
    auto& models = pipeInf.getAvailableModels();
    auto& defaultModel = pipeInf.getDefaultModel();

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

juce::String PromptPanel::getSelectedModel() const
{
    for (int i = 0; i < kNumModelSlots; ++i)
        if (modelBtns[i].getToggleState() && modelSlotIds[i].isNotEmpty())
            return modelSlotIds[i];
    return {};
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
    float alphaOverride, std::map<juce::String, float> axesOverride)
{
    auto& apvts = processorRef.getValueTreeState();
    float alpha = std::isnan(alphaOverride)
                      ? apvts.getRawParameterValue("gen_alpha")->load()
                      : alphaOverride;
    float magnitude = apvts.getRawParameterValue("gen_magnitude")->load();
    float noiseSigma = apvts.getRawParameterValue("gen_noise")->load();
    float duration = apvts.getRawParameterValue("gen_duration")->load();
    float startPos = apvts.getRawParameterValue("gen_start")->load();
    int steps = static_cast<int>(apvts.getRawParameterValue("gen_steps")->load());
    float cfgScale = apvts.getRawParameterValue("gen_cfg")->load();
    int seed = randomSeedToggle.getToggleState() ? -1 : seedEditor.getText().getIntValue();

    PipeInference::Request req;
    req.promptA = promptAEditor.getText().trim();
    auto promptB = promptBEditor.getText().trim();
    if (promptB.isNotEmpty()) req.promptB = promptB;
    req.alpha = alpha;
    req.magnitude = magnitude;
    req.noiseSigma = noiseSigma;
    req.durationSeconds = duration;
    req.startPosition = startPos;
    req.steps = steps;
    req.cfgScale = cfgScale;
    req.seed = seed;
    req.device = cpuBtn.getToggleState() ? "cpu" : gpuBackend_;
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
        if (onStatusChanged) onStatusChanged("No model loaded", false);
        return;
    }

    // Populate device/model info from Python if not yet done
    auto& pipeInf = processorRef.getPipeInference();
    if (!devicesPopulated && pipeInf.isReady())
    {
        populateDeviceButtons();
        populateModelSelector();
    }

    if (!processorRef.isPipeInferenceReady())
    {
        if (onStatusChanged) onStatusChanged("Backend not ready", false);
        return;
    }

    generating = true;
    generateButton.setEnabled(false);
    if (onStatusChanged) onStatusChanged("generating...", true);

    auto req = buildInferenceRequest();
    auto deviceForLabel = req.device.isEmpty() ? pipeInf.getDefaultDevice() : req.device;
    auto modelForLabel = req.model.isEmpty() ? pipeInf.getDefaultModel() : req.model;
    auto* processor = &processorRef;
    std::thread([this, processor, req, deviceForLabel, modelForLabel]()
    {
        auto result = processor->getPipeInference().generate(req);
        juce::MessageManager::callAsync([this, processor, result = std::move(result), deviceForLabel, modelForLabel]()
        {
            generating = false;
            generateButton.setEnabled(true);
            if (result.success)
            {
                processor->loadGeneratedAudio(result.audio, 44100.0);
                processor->setLastDevice(deviceForLabel);
                processor->setLastModel(modelForLabel);
                processor->setLastSeed(result.seed);
                processor->setLastPrompts(promptAEditor.getText().trim(),
                                          promptBEditor.getText().trim());
                seedEditor.setText(juce::String(result.seed), false);
                auto info = juce::String(result.generationTimeMs / 1000.0f, 1) + "s | seed "
                            + juce::String(result.seed) + " | " + modelForLabel
                            + " | " + deviceForLabel;
                if (onStatusChanged) onStatusChanged(info, false);

                if (!result.embeddingA.empty())
                {
                    processor->setLastEmbeddings(result.embeddingA, result.embeddingB);
                    if (onEmbeddingsReady)
                        onEmbeddingsReady(result.embeddingA, result.embeddingB);
                }
            }
            else
            {
                if (onStatusChanged) onStatusChanged(result.errorMessage, false);
            }
        });
    }).detach();
}

// ──────────────────────────────────────────────────────────────────────────────
// Drift auto-regeneration
// ──────────────────────────────────────────────────────────────────────────────
void PromptPanel::triggerDriftRegeneration(float effectiveAlpha,
                                            std::map<juce::String, float> effectiveAxes,
                                            bool holdForBar)
{
    if (generating) return;
    if (promptAEditor.getText().trim().isEmpty()) return;
    if (!processorRef.isPipeInferenceReady()) return;

    generating = true;
    generateButton.setEnabled(false);
    if (onStatusChanged) onStatusChanged("drift regen...", true);

    lastGenAlpha_ = effectiveAlpha;
    lastGenAxes_ = effectiveAxes;

    auto req = buildInferenceRequest(effectiveAlpha, effectiveAxes);
    auto* processor = &processorRef;
    std::thread([this, processor, req, holdForBar]()
    {
        auto result = processor->getPipeInference().generate(req);
        juce::MessageManager::callAsync([this, processor, result = std::move(result), holdForBar]()
        {
            generating = false;
            generateButton.setEnabled(true);
            if (result.success)
            {
                // Apply crossfade between old and new audio
                auto newAudio = result.audio; // mutable copy
                float xfadeMs = processor->getValueTreeState()
                    .getRawParameterValue("drift_crossfade")->load();
                int xfadeSamples = juce::roundToInt(xfadeMs * 0.001f * 44100.0f);
                if (xfadeSamples > 0)
                    applyDriftCrossfade(newAudio, processor->getGeneratedAudio(), xfadeSamples);

                if (holdForBar)
                {
                    // 1st Bar mode: defer audio load until bar boundary
                    pendingAudio_ = newAudio;
                    pendingSampleRate_ = 44100.0;
                    pendingBarLoad_ = true;
                    // Clear stale flags, then signal audio thread we're ready
                    processor->triggerPendingLoad.store(false, std::memory_order_relaxed);
                    processor->pendingBarLoadReady.store(true, std::memory_order_relaxed);
                }
                else
                {
                    // Auto mode: load immediately
                    processor->loadGeneratedAudio(newAudio, 44100.0);
                }
                processor->setLastSeed(result.seed);
                seedEditor.setText(juce::String(result.seed), false);
                auto info = juce::String(result.generationTimeMs / 1000.0f, 1) + "s | drift regen";
                if (onStatusChanged) onStatusChanged(info, false);

                if (!result.embeddingA.empty())
                {
                    processor->setLastEmbeddings(result.embeddingA, result.embeddingB);
                    if (onEmbeddingsReady)
                        onEmbeddingsReady(result.embeddingA, result.embeddingB);
                }
            }
            else
            {
                if (onStatusChanged) onStatusChanged(result.errorMessage, false);
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
    if (!processorRef.driftHasOscTarget.load(std::memory_order_relaxed)) return;

    int regenMode = processorRef.driftRegenMode.load(std::memory_order_relaxed);
    if (regenMode == 0) return; // Manual — no auto-regen

    // 1st Bar: check if audio thread signalled bar boundary while pending
    if (pendingBarLoad_)
    {
        bool loadNow = processorRef.triggerPendingLoad.exchange(false, std::memory_order_relaxed);
        if (loadNow)
        {
            processorRef.pendingBarLoadReady.store(false, std::memory_order_relaxed);
            processorRef.loadGeneratedAudio(pendingAudio_, pendingSampleRate_);
            pendingBarLoad_ = false;
            if (onStatusChanged) onStatusChanged("drift regen loaded", false);
        }
        return; // don't start new generation while waiting to load
    }

    // Read effective drift values from processor atomics
    auto& mv = processorRef.modulatedValues;
    float effAlpha = mv.driftAlpha.load(std::memory_order_relaxed);
    float dAxis1 = mv.driftAxis1.load(std::memory_order_relaxed);
    float dAxis2 = mv.driftAxis2.load(std::memory_order_relaxed);
    float dAxis3 = mv.driftAxis3.load(std::memory_order_relaxed);

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
    constexpr float DRIFT_THRESHOLD = 0.02f;
    bool alphaChanged = !std::isnan(effAlpha) &&
        (std::isnan(lastGenAlpha_) || std::abs(effAlpha - lastGenAlpha_) > DRIFT_THRESHOLD);

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

    if (!alphaChanged && !axesChanged) return;

    float genAlpha = std::isnan(effAlpha)
        ? processorRef.getValueTreeState().getRawParameterValue("gen_alpha")->load()
        : effAlpha;

    bool holdForBar = (regenMode == 2); // 1st Bar
    triggerDriftRegeneration(genAlpha, effAxes, holdForBar);
}
