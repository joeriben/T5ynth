#include "PromptPanel.h"
#include "GuiHelpers.h"
#include "../PluginProcessor.h"
#include "../backend/GenerationRequest.h"

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
    makeLabel(alphaValue, "0.50", kAccent, juce::Justification::centredRight, this);
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

    // Seed
    makeSlider(seedSlider, this);
    makeLabel(seedLabel, "Seed", kDimmer, juce::Justification::centredLeft, this);
    makeLabel(seedValue, "-1", kAccent, juce::Justification::centredRight, this);
    seedSlider.onValueChange = [this] {
        seedValue.setText(juce::String(juce::roundToInt(seedSlider.getValue())), juce::dontSendNotification);
    };

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
    seedA   = std::make_unique<Attachment>(apvts, "gen_seed", seedSlider);
}

float PromptPanel::fs() const
{
    float topH = (getTopLevelComponent() != nullptr)
                     ? static_cast<float>(getTopLevelComponent()->getHeight()) : 800.0f;
    return juce::jlimit(14.0f, 26.0f, topH * 0.030f);
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

    // Seed: full width
    setFs(seedLabel, fSmall); setFs(seedValue, fSmall);
    auto seedHdr = area.removeFromTop(compactRowH);
    seedLabel.setBounds(seedHdr.removeFromLeft(seedHdr.getWidth() * 2 / 3));
    seedValue.setBounds(seedHdr);
    seedSlider.setBounds(area.removeFromTop(compactSliderH));
    area.removeFromTop(gap * 2);

    // Generate button
    int btnH = juce::roundToInt(f * 2.0f);
    generateButton.setBounds(area.removeFromTop(btnH));
    area.removeFromTop(gap);

    // Info
    setFs(infoLabel, fSmall);
    infoLabel.setBounds(area.removeFromTop(rowH));
}

void PromptPanel::triggerGeneration()
{
    if (generating) return;
    auto promptA = promptAEditor.getText().trim();
    if (promptA.isEmpty()) return;

    if (!processorRef.getBackendConnection().isConnected())
    {
        infoLabel.setText("not connected", juce::dontSendNotification);
        return;
    }

    generating = true;
    generateButton.setEnabled(false);
    infoLabel.setText("generating...", juce::dontSendNotification);

    GenerationRequest request;
    request.setPromptA(promptA);
    auto promptB = promptBEditor.getText().trim();
    if (promptB.isNotEmpty()) request.setPromptB(promptB);

    auto& apvts = processorRef.getValueTreeState();
    request.setAlpha(apvts.getRawParameterValue("gen_alpha")->load());
    request.setMagnitude(apvts.getRawParameterValue("gen_magnitude")->load());
    request.setNoiseSigma(apvts.getRawParameterValue("gen_noise")->load());
    request.setDurationSeconds(apvts.getRawParameterValue("gen_duration")->load());
    request.setStartPosition(apvts.getRawParameterValue("gen_start")->load());
    request.setSteps(static_cast<int>(apvts.getRawParameterValue("gen_steps")->load()));
    request.setCfgScale(apvts.getRawParameterValue("gen_cfg")->load());
    request.setSeed(static_cast<int>(apvts.getRawParameterValue("gen_seed")->load()));

    processorRef.getBackendConnection().requestGeneration(request,
        [this](BackendConnection::GenerationResult result)
        {
            generating = false;
            generateButton.setEnabled(true);
            if (result.success)
            {
                processorRef.loadGeneratedAudio(result.audioBuffer, result.sampleRate);
                infoLabel.setText(juce::String(result.generationTimeMs / 1000.0f, 1) + "s | seed "
                                  + juce::String(result.seed), juce::dontSendNotification);
            }
            else
                infoLabel.setText(result.errorMessage, juce::dontSendNotification);
        });
}
