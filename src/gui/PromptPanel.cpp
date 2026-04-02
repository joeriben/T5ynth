#include "PromptPanel.h"
#include "GuiHelpers.h"
#include "../PluginProcessor.h"
#include "../backend/GenerationRequest.h"
#include "../inference/T5ynthInference.h"
#include <thread>

// Colors from GuiHelpers.h (kAccent, kDim, kDim, kSurface)

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
    makeLabel(alphaLabel, "Alpha", kDim, juce::Justification::centredLeft, this);
    makeLabel(alphaValue, "0.00", kOscCol, juce::Justification::centredRight, this);
    makeLabel(alphaHint, "Interpolation: -1.0 = A only, 1.0 = B only", kDim, juce::Justification::centredLeft, this);
    alphaSlider.onValueChange = [this] {
        alphaValue.setText(juce::String(alphaSlider.getValue(), 2), juce::dontSendNotification);
    };

    // Magnitude
    makeSlider(magnitudeSlider, this);
    makeLabel(magLabel, "Magnitude", kDim, juce::Justification::centredLeft, this);
    makeLabel(magValue, "1.00", kOscCol, juce::Justification::centredRight, this);
    makeLabel(magHint, "Embedding scale (1.0 = unchanged)", kDim, juce::Justification::centredLeft, this);
    magnitudeSlider.onValueChange = [this] {
        magValue.setText(juce::String(magnitudeSlider.getValue(), 2), juce::dontSendNotification);
    };

    // Noise
    makeSlider(noiseSlider, this);
    makeLabel(noiseLabel, "Noise", kDim, juce::Justification::centredLeft, this);
    makeLabel(noiseValue, "0.00", kOscCol, juce::Justification::centredRight, this);
    makeLabel(noiseHint, "Gaussian noise on embedding (0 = none)", kDim, juce::Justification::centredLeft, this);
    noiseSlider.onValueChange = [this] {
        noiseValue.setText(juce::String(noiseSlider.getValue(), 2), juce::dontSendNotification);
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

    randomSeedToggle.setColour(juce::ToggleButton::textColourId, kDim);
    randomSeedToggle.setColour(juce::ToggleButton::tickColourId, kAccent);
    randomSeedToggle.setToggleState(true, juce::dontSendNotification);
    randomSeedToggle.onClick = [this] {
        seedEditor.setEnabled(!randomSeedToggle.getToggleState());
        seedEditor.setAlpha(randomSeedToggle.getToggleState() ? 0.3f : 1.0f);
    };
    addAndMakeVisible(randomSeedToggle);
    seedEditor.setEnabled(false);
    seedEditor.setAlpha(0.3f);

    // Device selector — GPU / CPU toggle
    for (auto* btn : { &gpuBtn, &cpuBtn })
    {
        btn->setColour(juce::TextButton::buttonColourId, kSurface);
        btn->setColour(juce::TextButton::buttonOnColourId, kAccent);
        btn->setColour(juce::TextButton::textColourOffId, kDim);
        btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn->setClickingTogglesState(true);
        btn->setRadioGroupId(1003);
        addAndMakeVisible(btn);
    }
    gpuBtn.setToggleState(true, juce::dontSendNotification);
    gpuBtn.setConnectedEdges(juce::Button::ConnectedOnRight);
    cpuBtn.setConnectedEdges(juce::Button::ConnectedOnLeft);

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

    startTimerHz(2);  // poll for device availability
}

void PromptPanel::timerCallback()
{
    if (!devicesPopulated && processorRef.isPipeInferenceReady())
    {
        populateDeviceButtons();
        stopTimer();
    }
}

float PromptPanel::fs() const
{
    float h = static_cast<float>(getHeight());
    float w = static_cast<float>(getWidth());
    float pad = w * 0.04f;
    float available = h - 2.0f * pad;
    float maxF = available / 28.0f;
    return juce::jlimit(12.0f, 20.0f, maxF);
}

void PromptPanel::paint(juce::Graphics&)
{
    // Background painted by MainPanel (uniform card system)
}

void PromptPanel::resized()
{
    auto b = getLocalBounds();
    float w = static_cast<float>(b.getWidth());
    float h = static_cast<float>(b.getHeight());
    int pad = juce::roundToInt(w * 0.04f);
    auto area = b.reduced(pad);

    float f = fs();
    float fSmall = f;
    float fHint = f * 0.85f;
    int rowH = juce::roundToInt(f * 1.4f);
    int sliderH = juce::roundToInt(f * 1.2f);
    int hintH = juce::roundToInt(fHint * 1.3f);
    int inputH = juce::roundToInt(f * 1.8f);
    int gap = juce::roundToInt(f * 0.3f);
    int compactRowH = juce::roundToInt(fSmall * 1.2f);

    auto setFs = [](juce::Label& l, float size) { l.setFont(juce::FontOptions(size)); };

    // ── Reserve bottom: Info + Seed/Device row ──
    setFs(infoLabel, fSmall);
    infoLabel.setBounds(area.removeFromBottom(rowH));
    area.removeFromBottom(gap);

    // Seed + Device row (GPU/CPU toggle left, then Seed label + field, then Random)
    {
        setFs(seedLabel, fSmall);
        auto seedRow = area.removeFromBottom(compactRowH + 2);
        int btnW = juce::roundToInt(seedRow.getWidth() * 0.11f);
        gpuBtn.setBounds(seedRow.removeFromLeft(btnW));
        cpuBtn.setBounds(seedRow.removeFromLeft(btnW));
        seedRow.removeFromLeft(gap);
        int seedLabelW = juce::roundToInt(fSmall * 2.5f);
        seedLabel.setBounds(seedRow.removeFromLeft(seedLabelW));
        int toggleW = juce::roundToInt(seedRow.getWidth() * 0.30f);
        randomSeedToggle.setBounds(seedRow.removeFromRight(toggleW));
        seedEditor.setFont(juce::FontOptions(fSmall));
        seedEditor.setBounds(seedRow.reduced(0, 1));
    }
    area.removeFromBottom(gap * 2);

    // Show hints only if enough vertical space remains
    bool showHints = area.getHeight() > 350;

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
    setFs(promptALabel, fSmall);
    promptALabel.setBounds(area.removeFromTop(rowH));
    promptAEditor.setFont(juce::FontOptions(f));
    promptAEditor.setBounds(area.removeFromTop(inputH));
    area.removeFromTop(gap);

    // Prompt B
    setFs(promptBLabel, fSmall);
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

        setFs(lbl1, fSmall); setFs(val1, fSmall);
        lbl1.setBounds(leftHdr.removeFromLeft(leftHdr.getWidth() * 2 / 3));
        val1.setBounds(leftHdr);
        setFs(lbl2, fSmall); setFs(val2, fSmall);
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
}

void PromptPanel::loadPresetData(const juce::String& promptA, const juce::String& promptB,
                                  int seed, bool randomSeed,
                                  const juce::String& device)
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

void PromptPanel::triggerGenerationWithOffsets(std::vector<std::pair<int, float>> offsets)
{
    pendingOffsets_ = std::move(offsets);
    triggerGeneration();
}

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

    generating = true;
    generateButton.setEnabled(false);
    if (onStatusChanged) onStatusChanged("generating...", true);

    auto& apvts = processorRef.getValueTreeState();
    float rawAlpha = apvts.getRawParameterValue("gen_alpha")->load();
    float alpha = rawAlpha / 2.0f + 0.5f;
    float magnitude = apvts.getRawParameterValue("gen_magnitude")->load();
    float noiseSigma = apvts.getRawParameterValue("gen_noise")->load();
    float duration = apvts.getRawParameterValue("gen_duration")->load();
    float startPos = apvts.getRawParameterValue("gen_start")->load();
    int steps = static_cast<int>(apvts.getRawParameterValue("gen_steps")->load());
    float cfgScale = apvts.getRawParameterValue("gen_cfg")->load();
    int seed = randomSeedToggle.getToggleState() ? -1 : seedEditor.getText().getIntValue();
    auto promptB = promptBEditor.getText().trim();

    // Populate device info from Python if not yet done
    auto& pipeInf = processorRef.getPipeInference();
    if (!devicesPopulated && pipeInf.isReady())
        populateDeviceButtons();

    // Resolve selected device: GPU → gpuBackend_ (mps/cuda), CPU → "cpu"
    juce::String selectedDevice = cpuBtn.getToggleState() ? "cpu" : gpuBackend_;

    // Use pipe inference (Python subprocess) if available, fall back to native
    if (processorRef.isPipeInferenceReady())
    {
        PipeInference::Request req;
        req.promptA = promptA;
        if (promptB.isNotEmpty()) req.promptB = promptB;
        req.alpha = alpha;
        req.magnitude = magnitude;
        req.noiseSigma = noiseSigma;
        req.durationSeconds = duration;
        req.startPosition = startPos;
        req.steps = steps;
        req.cfgScale = cfgScale;
        req.seed = seed;
        req.device = selectedDevice;
        req.dimensionOffsets = std::move(pendingOffsets_);

        auto deviceForLabel = selectedDevice.isEmpty() ? pipeInf.getDefaultDevice() : selectedDevice;
        auto* processor = &processorRef;
        std::thread([this, processor, req, deviceForLabel]()
        {
            auto result = processor->getPipeInference().generate(req);
            juce::MessageManager::callAsync([this, processor, result = std::move(result), deviceForLabel]()
            {
                generating = false;
                generateButton.setEnabled(true);
                if (result.success)
                {
                    processor->loadGeneratedAudio(result.audio, 44100.0);
                    processor->setLastDevice(deviceForLabel);
                    processor->setLastSeed(result.seed);
                    processor->setLastPrompts(promptAEditor.getText().trim(),
                                              promptBEditor.getText().trim());
                    auto info = juce::String(result.generationTimeMs / 1000.0f, 1) + "s | seed "
                                + juce::String(result.seed) + " | " + deviceForLabel;
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
    else
    {
        // Fallback to native inference (deprecated, produces garbage)
        T5ynthInference::Request req;
        req.promptA = promptA;
        if (promptB.isNotEmpty()) req.promptB = promptB;
        req.alpha = alpha;
        req.magnitude = magnitude;
        req.noiseSigma = noiseSigma;
        req.durationSeconds = duration;
        req.startPosition = startPos;
        req.steps = steps;
        req.cfgScale = cfgScale;
        req.seed = seed;

        auto* processor = &processorRef;
        std::thread([this, processor, req]()
        {
            auto result = processor->getInference().generate(req);
            juce::MessageManager::callAsync([this, processor, result = std::move(result)]()
            {
                generating = false;
                generateButton.setEnabled(true);
                if (result.success)
                {
                    processor->loadGeneratedAudio(result.audio, 44100.0);
                    infoLabel.setText(juce::String(result.generationTimeMs / 1000.0f, 1) + "s | seed "
                                      + juce::String(result.seed), juce::dontSendNotification);
                }
                else
                    infoLabel.setText(result.errorMessage, juce::dontSendNotification);
            });
        }).detach();
    }
}
