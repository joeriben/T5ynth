#include "PromptPanel.h"
#include "GuiHelpers.h"
#include "../PluginProcessor.h"
#include "../backend/GenerationRequest.h"
#include "../inference/T5ynthInference.h"
#include <thread>

// Colors from GuiHelpers.h (kAccent, kDim, kDimmer, kSurface)

static void makeSlider(juce::Slider& s, juce::Component* p)
{
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    s.setColour(juce::Slider::trackColourId, kAccent);
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
    makeLabel(alphaValue, "0.00", kAccent, juce::Justification::centredRight, this);
    makeLabel(alphaHint, "Interpolation: -1.0 = A only, 1.0 = B only", kDimmer, juce::Justification::centredLeft, this);
    alphaSlider.onValueChange = [this] {
        alphaValue.setText(juce::String(alphaSlider.getValue(), 2), juce::dontSendNotification);
    };

    // Magnitude
    makeSlider(magnitudeSlider, this);
    makeLabel(magLabel, "Magnitude", kDim, juce::Justification::centredLeft, this);
    makeLabel(magValue, "1.00", kAccent, juce::Justification::centredRight, this);
    makeLabel(magHint, "Embedding scale (1.0 = unchanged)", kDimmer, juce::Justification::centredLeft, this);
    magnitudeSlider.onValueChange = [this] {
        magValue.setText(juce::String(magnitudeSlider.getValue(), 2), juce::dontSendNotification);
    };

    // Noise
    makeSlider(noiseSlider, this);
    makeLabel(noiseLabel, "Noise", kDim, juce::Justification::centredLeft, this);
    makeLabel(noiseValue, "0.00", kAccent, juce::Justification::centredRight, this);
    makeLabel(noiseHint, "Gaussian noise on embedding (0 = none)", kDimmer, juce::Justification::centredLeft, this);
    noiseSlider.onValueChange = [this] {
        noiseValue.setText(juce::String(noiseSlider.getValue(), 2), juce::dontSendNotification);
    };

    // --- Compact params ---
    // Duration
    makeSlider(durationSlider, this);
    makeLabel(durLabel, "Duration", kDimmer, juce::Justification::centredLeft, this);
    makeLabel(durValue, "1.0s", kAccent, juce::Justification::centredRight, this);
    makeLabel(durHint, "Audio length (seconds)", kDimmer, juce::Justification::centredLeft, this);
    durationSlider.onValueChange = [this] {
        durValue.setText(juce::String(durationSlider.getValue(), 1) + "s", juce::dontSendNotification);
    };

    // Start Position
    makeSlider(startSlider, this);
    makeLabel(startLabel, "Start", kDimmer, juce::Justification::centredLeft, this);
    makeLabel(startValue, "0%", kAccent, juce::Justification::centredRight, this);
    makeLabel(startHint, "0% = attack, higher = sustained", kDimmer, juce::Justification::centredLeft, this);
    startSlider.onValueChange = [this] {
        startValue.setText(juce::String(juce::roundToInt(startSlider.getValue() * 100.0)) + "%", juce::dontSendNotification);
    };

    // Steps
    makeSlider(stepsSlider, this);
    makeLabel(stepsLabel, "Steps", kDimmer, juce::Justification::centredLeft, this);
    makeLabel(stepsValue, "20", kAccent, juce::Justification::centredRight, this);
    makeLabel(stepsHint, "More = higher quality", kDimmer, juce::Justification::centredLeft, this);
    stepsSlider.onValueChange = [this] {
        stepsValue.setText(juce::String(juce::roundToInt(stepsSlider.getValue())), juce::dontSendNotification);
    };

    // CFG
    makeSlider(cfgSlider, this);
    makeLabel(cfgLabel, "CFG", kDimmer, juce::Justification::centredLeft, this);
    makeLabel(cfgValue, "7.0", kAccent, juce::Justification::centredRight, this);
    makeLabel(cfgHint, "Classifier-free guidance", kDimmer, juce::Justification::centredLeft, this);
    cfgSlider.onValueChange = [this] {
        cfgValue.setText(juce::String(cfgSlider.getValue(), 1), juce::dontSendNotification);
    };

    // Seed (text field + random toggle)
    makeLabel(seedLabel, "Seed", kDimmer, juce::Justification::centredLeft, this);
    seedEditor.setColour(juce::TextEditor::backgroundColourId, kSurface);
    seedEditor.setColour(juce::TextEditor::textColourId, kAccent);
    seedEditor.setColour(juce::TextEditor::outlineColourId, kBorder);
    seedEditor.setInputRestrictions(12, "0123456789");
    seedEditor.setText("123456789", false);
    addAndMakeVisible(seedEditor);

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

    // Device selector — populated when inference launches
    makeLabel(deviceLabel, "Device", kDimmer, juce::Justification::centredLeft, this);
    deviceBox.setColour(juce::ComboBox::backgroundColourId, kSurface);
    deviceBox.setColour(juce::ComboBox::textColourId, kAccent);
    deviceBox.setColour(juce::ComboBox::outlineColourId, kBorder);
    deviceBox.addItem("Auto", 1);
    deviceBox.setSelectedId(1, juce::dontSendNotification);
    addAndMakeVisible(deviceBox);

    // Generate — prominent green button like web UI
    generateButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1b5e20));
    generateButton.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff4caf50));
    generateButton.onClick = [this] { triggerGeneration(); };
    addAndMakeVisible(generateButton);

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
}

float PromptPanel::fs() const
{
    // Derive font size from available height so all content fits.
    // Content budget: ~33 f-units (labels + sliders + hints + device + generate + gaps).
    float h = static_cast<float>(getHeight());
    float w = static_cast<float>(getWidth());
    float pad = w * 0.04f;
    float available = h - 2.0f * pad;
    float maxF = available / 33.0f;
    return juce::jlimit(10.0f, 22.0f, maxF);
}

void PromptPanel::paint(juce::Graphics& g)
{
    g.fillAll(kBg);

    int pad = juce::roundToInt(static_cast<float>(getWidth()) * 0.04f);

    // Card behind prompts A + B
    if (promptALabel.isVisible())
    {
        int top = promptALabel.getY() - 4;
        int bot = promptBEditor.getBottom() + 4;
        paintCard(g, juce::Rectangle<int>(pad, top, getWidth() - pad * 2, bot - top));
    }

    // Card behind embedding controls (Alpha, Magnitude, Noise)
    if (alphaLabel.isVisible())
    {
        int top = alphaLabel.getY() - 4;
        int bot = noiseHint.getBottom() + 4;
        paintCard(g, juce::Rectangle<int>(pad, top, getWidth() - pad * 2, bot - top));
    }
}

void PromptPanel::resized()
{
    auto b = getLocalBounds();
    float w = static_cast<float>(b.getWidth());
    float h = static_cast<float>(b.getHeight());
    int pad = juce::roundToInt(w * 0.04f);
    auto area = b.reduced(pad);

    float f = fs();
    float fSmall = f * 0.80f;
    float fHint = f * 0.65f;
    int rowH = juce::roundToInt(f * 1.4f);
    int sliderH = juce::roundToInt(f * 1.2f);
    int hintH = juce::roundToInt(fHint * 1.3f);
    int inputH = juce::roundToInt(f * 1.8f);
    int gap = juce::roundToInt(f * 0.3f);

    auto setFs = [](juce::Label& l, float size) { l.setFont(juce::FontOptions(size)); };

    // --- Main slider layout: [Label ... Value] \n [slider] \n [hint] ---
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
            setFs(*hint, fHint);
            hint->setBounds(area.removeFromTop(hintH));
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

    // --- Compact params: 2 columns x 3 rows ---
    // Row 1: Duration | Start Position
    // Row 2: Steps | CFG
    // Row 3: Seed (full width)
    int compactSliderH = juce::roundToInt(f * 0.9f);
    int compactRowH = juce::roundToInt(fSmall * 1.2f);
    int compactHintH = juce::roundToInt(fHint * 1.1f);
    int colGap = juce::roundToInt(w * 0.03f);

    auto layoutCompactPair = [&](juce::Label& lbl1, juce::Slider& sl1, juce::Label& val1, juce::Label* hint1,
                                  juce::Label& lbl2, juce::Slider& sl2, juce::Label& val2, juce::Label* hint2)
    {
        int colW = (area.getWidth() - colGap) / 2;

        // Headers
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

        // Sliders
        auto slRow = area.removeFromTop(compactSliderH);
        sl1.setBounds(slRow.removeFromLeft(colW));
        slRow.removeFromLeft(colGap);
        sl2.setBounds(slRow);

        // Hints
        if (hint1 != nullptr || hint2 != nullptr)
        {
            auto hintRow = area.removeFromTop(compactHintH);
            if (hint1) { setFs(*hint1, fHint); hint1->setBounds(hintRow.removeFromLeft(colW)); }
            else hintRow.removeFromLeft(colW);
            hintRow.removeFromLeft(colGap);
            if (hint2) { setFs(*hint2, fHint); hint2->setBounds(hintRow); }
        }
        area.removeFromTop(gap);
    };

    layoutCompactPair(durLabel, durationSlider, durValue, &durHint,
                      startLabel, startSlider, startValue, &startHint);
    layoutCompactPair(stepsLabel, stepsSlider, stepsValue, &stepsHint,
                      cfgLabel, cfgSlider, cfgValue, &cfgHint);

    // Seed: label | text field | random toggle
    setFs(seedLabel, fSmall);
    auto seedRow = area.removeFromTop(compactRowH + 2);
    int seedLabelW = juce::roundToInt(seedRow.getWidth() * 0.15f);
    int toggleW = juce::roundToInt(seedRow.getWidth() * 0.35f);
    seedLabel.setBounds(seedRow.removeFromLeft(seedLabelW));
    randomSeedToggle.setBounds(seedRow.removeFromRight(toggleW));
    seedEditor.setFont(juce::FontOptions(fSmall));
    seedEditor.setBounds(seedRow.reduced(0, 1));
    area.removeFromTop(gap);

    // Device selector row
    {
        setFs(deviceLabel, fSmall);
        auto devRow = area.removeFromTop(compactRowH + 2);
        int devLabelW = juce::roundToInt(devRow.getWidth() * 0.15f);
        deviceLabel.setBounds(devRow.removeFromLeft(devLabelW));
        deviceBox.setBounds(devRow.reduced(0, 1));
    }
    area.removeFromTop(gap * 2);

    // Generate button
    int btnH = juce::roundToInt(f * 2.0f);
    generateButton.setBounds(area.removeFromTop(btnH));
    area.removeFromTop(gap);

    // Info
    setFs(infoLabel, fSmall);
    infoLabel.setBounds(area.removeFromTop(rowH));
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

    // Select device from preset (if available in device list)
    if (device.isNotEmpty())
    {
        for (int i = 0; i < deviceBox.getNumItems(); ++i)
        {
            if (deviceBox.getItemText(i).equalsIgnoreCase(device))
            {
                deviceBox.setSelectedItemIndex(i, juce::dontSendNotification);
                return;
            }
        }
    }
    deviceBox.setSelectedId(1, juce::dontSendNotification);  // fallback to Auto
}

void PromptPanel::triggerGeneration()
{
    if (generating) return;
    auto promptA = promptAEditor.getText().trim();
    if (promptA.isEmpty()) return;

    if (!processorRef.isInferenceReady())
    {
        infoLabel.setText("No model loaded — open Settings", juce::dontSendNotification);
        return;
    }

    generating = true;
    generateButton.setEnabled(false);
    infoLabel.setText("generating...", juce::dontSendNotification);

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

    // Populate device box from Python if not yet done
    auto& pipeInf = processorRef.getPipeInference();
    if (deviceBox.getNumItems() == 1 && pipeInf.isReady())
    {
        auto& devs = pipeInf.getAvailableDevices();
        for (int i = 0; i < devs.size(); ++i)
            deviceBox.addItem(devs[i].toUpperCase(), i + 2);  // id 1 = Auto
    }

    // Resolve selected device
    juce::String selectedDevice;
    if (deviceBox.getSelectedId() > 1)
        selectedDevice = pipeInf.getAvailableDevices()[deviceBox.getSelectedId() - 2];

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
                    infoLabel.setText(juce::String(result.generationTimeMs / 1000.0f, 1) + "s | seed "
                                      + juce::String(result.seed) + " | " + deviceForLabel,
                                      juce::dontSendNotification);
                }
                else
                    infoLabel.setText(result.errorMessage, juce::dontSendNotification);
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
