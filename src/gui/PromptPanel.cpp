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
constexpr float kPromptMultiInput  = 3.0f;   // two-line prompt editor
constexpr float kPromptCompactRow  = 1.15f;
constexpr float kPromptCompactCtrl = 0.9f;
constexpr float kPromptSeedCtrl    = 1.75f;
constexpr float kPromptGap         = 0.28f;
constexpr float kPromptContentUnits = 22.08f;  // bumped by 1.18 for temporary mode-buttons row (0.9 + 0.28)
// Easy budget includes the model selector row (compactRow + gap ≈ 1.43 units)
// since the selector is now visible in easy mode too.
constexpr float kPromptEasyContentUnits = 18.61f;
constexpr int kBaseSeed = 123456789;

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
    makeLabel(promptALabel, "Impulse A", kDim, juce::Justification::centredLeft, this);
    // Two-line with word-wrap so longer impulses stay visible. With
    // setReturnKeyStartsNewLine(false) Return still triggers generation; the
    // wrap only kicks in when the text itself exceeds one line's width.
    promptAEditor.setMultiLine(true, true);
    promptAEditor.setReturnKeyStartsNewLine(false);
    promptAEditor.setText("a steady clean saw wave, c3");
    promptAEditor.onReturnKey = [this] { triggerGeneration(); };
    promptAEditor.onTextChange = [this] {
        // Impulse edits should force the next drift regen to use a fresh snapshot.
        lastGenPromptA_.clear();
    };
    promptAEditor.setBufferedToImage(true);
    addAndMakeVisible(promptAEditor);

    makeLabel(promptBLabel, "Impulse B", kDim, juce::Justification::centredLeft, this);
    promptBEditor.setMultiLine(true, true);
    promptBEditor.setReturnKeyStartsNewLine(false);
    promptBEditor.setText("glass breaking");
    promptBEditor.onTextChange = [this] {
        // Impulse edits should force the next drift regen to use a fresh snapshot.
        lastGenPromptB_.clear();
    };
    promptBEditor.setBufferedToImage(true);
    addAndMakeVisible(promptBEditor);

    // Alpha
    makeSlider(alphaSlider, this);
    makeLabel(alphaLabel, "A " + juce::String(juce::CharPointer_UTF8("\xe2\x86\x94")) + " B", kDim, juce::Justification::centredLeft, this);
    makeLabel(alphaValue, "0", kOscCol, juce::Justification::centredRight, this);
    alphaSlider.onValueChange = [this] {
        if (injectionMode_ == "linear")
        {
            float v = static_cast<float>(alphaSlider.getValue());
            if (std::abs(v) < 0.001f)
                alphaValue.setText("0", juce::dontSendNotification);
            else if (v < 0.0f)
                alphaValue.setText("A " + juce::String(-v, 3), juce::dontSendNotification);
            else
                alphaValue.setText("B " + juce::String(v, 3), juce::dontSendNotification);
        }
        else if (injectionMode_ == "late_step"
              || injectionMode_ == "kombi1"
              || injectionMode_ == "kombi2"
              || injectionMode_ == "kombi3")
        {
            float v = static_cast<float>(alphaSlider.getValue());
            lateMixForMode(injectionMode_) = v;
            alphaValue.setText(juce::String(v, 2), juce::dontSendNotification);
        }
        else  // layer_split — TwoValueHorizontal: read both thumbs
        {
            splitLayerStart_ = static_cast<float>(alphaSlider.getMinValue());
            splitLayerEnd_   = static_cast<float>(alphaSlider.getMaxValue());
            int s = static_cast<int>(std::round(splitLayerStart_));
            int e = static_cast<int>(std::round(splitLayerEnd_));
            // Denominator is the active model's DiT depth — was hardcoded "/16"
            // (SAO Small assumption) and confused the readout on SA3 Small.
            alphaValue.setText(juce::String(s) + "-" + juce::String(e)
                                   + "/" + juce::String(ditBlocks_),
                               juce::dontSendNotification);
        }
    };

    // ── Injection-mode test row (TEMPORARY, research; not persisted) ──
    // Three radio-group buttons; the existing alphaSlider's range/label/state
    // shifts with the active mode (see applyModeToSlider()).
    auto styleModeBtn = [this](juce::TextButton& b)
    {
        b.setColour(juce::TextButton::buttonColourId, kSurface);
        b.setColour(juce::TextButton::buttonOnColourId, kOscCol);
        b.setColour(juce::TextButton::textColourOffId, kDim);
        b.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        b.setClickingTogglesState(true);
        b.setRadioGroupId(2027);  // unique id, distinct from model switchbox (1004)
        addAndMakeVisible(b);
    };
    styleModeBtn(injModeLinear);
    styleModeBtn(injModeFine);
    styleModeBtn(injModeLayer);
    styleModeBtn(injModeKombi1);
    styleModeBtn(injModeKombi2);
    styleModeBtn(injModeKombi3);
    injModeLinear.setConnectedEdges(juce::Button::ConnectedOnRight);
    injModeFine  .setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    injModeLayer .setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    injModeKombi1.setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    injModeKombi2.setConnectedEdges(juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    injModeKombi3.setConnectedEdges(juce::Button::ConnectedOnLeft);
    injModeLinear.setToggleState(true, juce::dontSendNotification);
    // Mode-button onClick handlers also trigger an immediate regeneration so
    // the user can A/B modes by clicking — same UX affordance as drift /
    // slider auto-regen, but for the discrete mode dimension.
    injModeLinear.onClick = [this] { if (injModeLinear.getToggleState()) selectInjectionMode("linear", true); };
    injModeFine  .onClick = [this] { if (injModeFine  .getToggleState()) selectInjectionMode("late_step", true); };
    injModeLayer .onClick = [this] { if (injModeLayer .getToggleState()) selectInjectionMode("layer_split", true); };
    injModeKombi1.onClick = [this] { if (injModeKombi1.getToggleState()) selectInjectionMode("kombi1", true); };
    injModeKombi2.onClick = [this] { if (injModeKombi2.getToggleState()) selectInjectionMode("kombi2", true); };
    injModeKombi3.onClick = [this] { if (injModeKombi3.getToggleState()) selectInjectionMode("kombi3", true); };

    // Magnitude
    makeSlider(magnitudeSlider, this);
    makeLabel(magLabel, "Magnitude", kDim, juce::Justification::centredLeft, this);
    makeLabel(magValue, "1.00", kOscCol, juce::Justification::centredRight, this);
    makeLabel(magHint, "Embedding magnitude (1.0 = unchanged)", kDim, juce::Justification::centredLeft, this);
    magnitudeSlider.onValueChange = [this] {
        magValue.setText(juce::String(magnitudeSlider.getValue(), 3), juce::dontSendNotification);
    };

    // Chaos
    makeSlider(noiseSlider, this);
    makeLabel(noiseLabel, "Chaos", kDim, juce::Justification::centredLeft, this);
    makeLabel(noiseValue, "0.000", kOscCol, juce::Justification::centredRight, this);
    makeLabel(noiseHint, "Embedding chaos (0 = none)", kDim, juce::Justification::centredLeft, this);
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

    // Variation (text field + random toggle)
    makeLabel(seedLabel, "Variation", kDim, juce::Justification::centredLeft, this);
    // Match the value-display style used by noiseValue / cfgValue / durValue
    // (kOscCol on dark surface) so the current seed reads as a first-class
    // number, not a grey decoration.
    seedEditor.setColour(juce::TextEditor::backgroundColourId, kSurface.brighter(0.04f));
    seedEditor.setColour(juce::TextEditor::textColourId, kOscCol);
    seedEditor.setColour(juce::TextEditor::outlineColourId, kBorder);
    seedEditor.setColour(juce::TextEditor::focusedOutlineColourId, kOscCol);
    seedEditor.setMultiLine(false);
    seedEditor.setReturnKeyStartsNewLine(false);
    seedEditor.setInputRestrictions(12, "0123456789");
    seedEditor.setIndents(3, 2);
    seedEditor.setJustification(juce::Justification::centredLeft);
    seedEditor.setText("123456789", false);
    syncSeedEditorFont(14.0f);
    addAndMakeVisible(seedEditor);

    seedEditor.onReturnKey = [this] { triggerGeneration(); };
    seedEditor.onTextChange = [this] {
        syncSeedEditorFont(preferredPromptFontForWidth(getWidth()) * 1.25f);
        syncSeedModeFromCurrentState();
    };

    randomSeedToggle.setColour(juce::TextButton::buttonColourId, kSurface);
    randomSeedToggle.setColour(juce::TextButton::buttonOnColourId, kOscCol);
    randomSeedToggle.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffe3e7f2));
    randomSeedToggle.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    randomSeedToggle.setTooltip("Automatic variation");
    randomSeedToggle.setClickingTogglesState(true);
    randomSeedToggle.setToggleState(false, juce::dontSendNotification);
    randomSeedToggle.onClick = [this] {
        syncSeedEditorEnabledState();
        syncSeedModeFromCurrentState();
        // Cache the auto/random choice on the processor so preset save can
        // persist it — APVTS PID::genSeed isn't driven from this UI.
        processorRef.setLastRandomSeed(randomSeedToggle.getToggleState());
    };
    addAndMakeVisible(randomSeedToggle);
    syncSeedEditorEnabledState();

    {
        static constexpr const char* labels[kNumSeedModeBtns] = { "none", "last", "auto" };
        for (int i = 0; i < kNumSeedModeBtns; ++i)
        {
            auto& b = seedModeBtns[i];
            b.setButtonText(labels[i]);
            b.setColour(juce::TextButton::buttonColourId, kSurface);
            b.setColour(juce::TextButton::buttonOnColourId, kOscCol);
            b.setColour(juce::TextButton::textColourOffId, kDim);
            b.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
            b.setClickingTogglesState(true);
            b.setRadioGroupId(2038);
            int edges = 0;
            if (i > 0) edges |= juce::Button::ConnectedOnLeft;
            if (i < kNumSeedModeBtns - 1) edges |= juce::Button::ConnectedOnRight;
            b.setConnectedEdges(edges);
            b.onClick = [this, i]
            {
                if (!seedModeBtns[i].getToggleState())
                    return;
                setSeedMode(static_cast<SeedMode>(i), true);
            };
            addAndMakeVisible(b);
        }
        syncSeedModeButtons();
    }

    // Model selector — fixed 4 slots, always visible (disabled = gray until model found).
    // Order: SA3 first (newest, default), then SA1 family, then AudioLDM2.
    {
        const char* slotLabels[kNumModelSlots] = { "SA3 Music", "SA3 SFX", "SA1 Open", "SA1 Small", "AudioLDM2" };
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

                auto& apvts = processorRef.getValueTreeState();
                const auto defaults = defaultParamsFor(model);
                apvts.getParameter(PID::infSteps)->setValueNotifyingHost(
                    apvts.getParameter(PID::infSteps)->convertTo0to1(defaults.steps));
                apvts.getParameter(PID::genCfg)->setValueNotifyingHost(
                    apvts.getParameter(PID::genCfg)->convertTo0to1(defaults.cfg));
                syncInjectionModeAvailability();
                // The active model can have a different DiT depth (SA3 vs SAO
                // Small): clamp the layer slider range before the user has a
                // chance to drag past the new ceiling.
                refreshDitBlocksForCurrentModel();

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
    const int multiInputH = juce::roundToInt(f * kPromptMultiInput);
    const int gap = juce::roundToInt(f * kPromptGap);
    const int compactRowH = juce::roundToInt(f * kPromptCompactRow);
    const int compactCtrlH = juce::roundToInt(f * kPromptCompactCtrl);
    const int seedCtrlH = juce::roundToInt(f * kPromptSeedCtrl);

    if (easyMode_)
    {
        return (compactRowH + 2) + gap                  // model selector row
             + rowH + multiInputH + gap
             + rowH + multiInputH + gap * 2
             + compactCtrlH + gap
             + rowH + sliderH + gap
             + compactRowH + seedCtrlH + gap
             + gap + compactRowH;
    }

    return (compactRowH + 2) + gap
         + rowH + multiInputH + gap
         + rowH + multiInputH + gap * 2
         + (compactCtrlH + gap)                       // TEMPORARY injection-mode buttons row
         + rowH + sliderH + gap
         + (compactRowH + compactCtrlH + gap) * 2
         + compactRowH + seedCtrlH + gap
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

    // Ghost indicators for drift-modulated sliders — update every tick.
    auto& mv0 = processorRef.modulatedValues;
    const float newAlpha = mv0.driftAlpha.load(std::memory_order_relaxed);
    const float newMag   = mv0.driftMagnitude.load(std::memory_order_relaxed);
    const float newNoise = mv0.driftNoise.load(std::memory_order_relaxed);
    // Mode-specific ghosts for Step-in/Layer: derive the alpha-LFO offset (in
    // alpha-units) and project it onto the active mode's parameter range
    // using the same scaling factors the regen path uses.
    float newLateMix    = std::numeric_limits<float>::quiet_NaN();
    float newSplitStart = std::numeric_limits<float>::quiet_NaN();
    float newSplitEnd   = std::numeric_limits<float>::quiet_NaN();
    {
        const float baseAlpha0 = processorRef.getValueTreeState()
                                     .getRawParameterValue(PID::genAlpha)->load();
        const float alphaOff = std::isnan(newAlpha) ? 0.0f
                                                    : (newAlpha - baseAlpha0);
        const bool fineLike = (injectionMode_ == "late_step"
                            || injectionMode_ == "kombi1"
                            || injectionMode_ == "kombi2"
                            || injectionMode_ == "kombi3");
        if (fineLike && std::abs(alphaOff) > 0.001f)
        {
            newLateMix = juce::jlimit(0.0f, 1.0f, lateMixForMode(injectionMode_) + alphaOff * 0.25f);
        }
        else if (injectionMode_ == "layer_split" && std::abs(alphaOff) > 0.001f)
        {
            const float blocksF = static_cast<float>(ditBlocks_);
            const float width = splitLayerEnd_ - splitLayerStart_;
            const float maxStart = std::max(0.0f, blocksF - width);
            // Drift travels half the block range per unit of alphaOff. SAO
            // Small (16 blocks) keeps the historical 8-block sweep; SA3
            // scales proportionally.
            const float sweepScale = blocksF * 0.5f;
            float gs = juce::jlimit(0.0f, maxStart, splitLayerStart_ + alphaOff * sweepScale);
            newSplitStart = gs;
            newSplitEnd   = gs + width;
        }
    }

    auto same = [](float a, float b) {
        return (std::isnan(a) && std::isnan(b)) || a == b;
    };
    const bool alphaGroupChanged = !same(alphaGhostValue_,    newAlpha)
                                || !same(lateMixGhostValue_,  newLateMix)
                                || !same(splitStartGhostValue_, newSplitStart)
                                || !same(splitEndGhostValue_, newSplitEnd);
    const bool magChanged   = !same(magGhostValue_,   newMag);
    const bool noiseChanged = !same(noiseGhostValue_, newNoise);

    alphaGhostValue_      = newAlpha;
    magGhostValue_        = newMag;
    noiseGhostValue_      = newNoise;
    lateMixGhostValue_    = newLateMix;
    splitStartGhostValue_ = newSplitStart;
    splitEndGhostValue_   = newSplitEnd;

    if (alphaGroupChanged)
        repaint(alphaSlider.getBounds().expanded(4));
    if (magChanged)
        repaint(magnitudeSlider.getBounds().expanded(4));
    if (noiseChanged)
        repaint(noiseSlider.getBounds().expanded(4));

    // Auto-regen polling
    pollDriftRegen();
}

void PromptPanel::paint(juce::Graphics& g)
{
    if (!modelSwitchBounds.isEmpty())
        paintSwitchBoxBorder(g, modelSwitchBounds);
    if (easyMode_ && !seedModeSwitchBounds.isEmpty())
        paintSwitchBoxBorder(g, seedModeSwitchBounds);
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

    // Alpha ghost only makes sense when the slider drives α (linear). In Step-in
    // and Layer the alpha-LFO offset is remapped onto the active parameter's
    // axis (lateMix or [splitStart, splitEnd]) — those ghosts paint on the
    // same physical slider but at the mode-specific value position.
    if (injectionMode_ == "linear")
        drawGhost(alphaSlider, alphaGhostValue_);
    else if (injectionMode_ == "late_step"
          || injectionMode_ == "kombi1"
          || injectionMode_ == "kombi2"
          || injectionMode_ == "kombi3")
        drawGhost(alphaSlider, lateMixGhostValue_);
    else if (injectionMode_ == "layer_split")
    {
        // Two synchronous ghosts for the range slider — one per thumb.
        drawGhost(alphaSlider, splitStartGhostValue_);
        drawGhost(alphaSlider, splitEndGhostValue_);
    }
    if (!easyMode_)
    {
        drawGhost(magnitudeSlider, magGhostValue_);
        drawGhost(noiseSlider, noiseGhostValue_);
    }
}

void PromptPanel::resized()
{
    auto b = getLocalBounds();
    float w = static_cast<float>(b.getWidth());
    int pad = juce::roundToInt(w * kPromptPadFactor);
    auto area = b.reduced(pad);

    float f = juce::jlimit(10.0f, 20.0f,
        (static_cast<float>(area.getHeight()) - 2.0f)
            / (easyMode_ ? kPromptEasyContentUnits : kPromptContentUnits));
    int rowH = juce::roundToInt(f * kPromptRow);
    int sliderH = juce::roundToInt(f * kPromptSlider);
    int gap = juce::roundToInt(f * kPromptGap);
    int compactRowH = juce::roundToInt(f * kPromptCompactRow);
    int compactCtrlH = juce::roundToInt(f * kPromptCompactCtrl);
    int seedCtrlH = juce::roundToInt(f * kPromptSeedCtrl);

    auto setFs = [](juce::Label& l, float size) { l.setFont(juce::FontOptions(size)); };

    const bool easy = easyMode_;
    // Model selector stays visible in BOTH modes — the engine choice changes
    // character enough that hiding it in easy mode left the user with no way
    // to switch on the fly.
    for (int i = 0; i < kNumModelSlots; ++i)
        modelBtns[i].setVisible(true);
    modelSwitchBounds = {};

    for (auto* c : { &magLabel, &magValue, &noiseLabel, &noiseValue,
                     &stepsLabel, &stepsValue, &cfgLabel, &cfgValue })
        c->setVisible(!easy);
    magnitudeSlider.setVisible(!easy);
    noiseSlider.setVisible(!easy);
    stepsSlider.setVisible(!easy);
    cfgSlider.setVisible(!easy);
    seedEditor.setVisible(!easy);
    randomSeedToggle.setVisible(!easy);
    for (auto& bSeed : seedModeBtns)
        bSeed.setVisible(easy);
    seedModeSwitchBounds = {};

    // ── Model selector switchbox at top (compact, fixed 5 slots) ──
    // Laid out in both modes; visibility is unconditionally true above.
    {
        auto modelRow = area.removeFromTop(compactRowH + 2);
        // Distribute remaining row width across remaining slots so the row
        // always fills to the right edge regardless of slot count. Earlier
        // we used a fixed f*5.5f per cell, which clipped the rightmost
        // button on narrow panels once we went from 4 slots to 5. The same
        // pattern is used for seedModeBtns above.
        for (int i = 0; i < kNumModelSlots; ++i)
        {
            const int cellW = (i == kNumModelSlots - 1)
                ? modelRow.getWidth()
                : juce::jmax(1, modelRow.getWidth() / (kNumModelSlots - i));
            modelBtns[i].setBounds(modelRow.removeFromLeft(cellW));
        }
        modelSwitchBounds = modelBtns[0].getBounds()
            .getUnion(modelBtns[kNumModelSlots - 1].getBounds());
        area.removeFromTop(gap);
    }

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

    const int multiInputH = juce::roundToInt(f * kPromptMultiInput);

    // Impulse A
    setFs(promptALabel, f);
    promptALabel.setBounds(area.removeFromTop(rowH));
    promptAEditor.setFont(juce::FontOptions(f));
    promptAEditor.setBounds(area.removeFromTop(multiInputH));
    area.removeFromTop(gap);

    // Impulse B
    setFs(promptBLabel, f);
    promptBLabel.setBounds(area.removeFromTop(rowH));
    promptBEditor.setFont(juce::FontOptions(f));
    promptBEditor.setBounds(area.removeFromTop(multiInputH));
    area.removeFromTop(gap * 2);

    // --- Injection-mode test row (TEMPORARY): three radio buttons left-aligned ---
    // The alphaSlider above this row repurposes itself based on the active
    // mode (see applyModeToSlider()): Linear=A↔B, Step-in=transition, Layer=split.
    {
        auto btnRow = area.removeFromTop(compactCtrlH);
        // 6 buttons in the radio row. "Combo N" labels are wider than the
        // original three, so the row claims most of the available width.
        int btnTotalW = juce::jmax(280, btnRow.getWidth() * 4 / 5);
        int btnW = btnTotalW / 6;
        injModeLinear.setBounds(btnRow.removeFromLeft(btnW));
        injModeFine  .setBounds(btnRow.removeFromLeft(btnW));
        injModeLayer .setBounds(btnRow.removeFromLeft(btnW));
        injModeKombi1.setBounds(btnRow.removeFromLeft(btnW));
        injModeKombi2.setBounds(btnRow.removeFromLeft(btnW));
        injModeKombi3.setBounds(btnRow.removeFromLeft(btnW));
        area.removeFromTop(gap);
    }

    // Alpha (dynamic: meaning depends on current injection mode)
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

        auto controlRow = area.removeFromTop(seedCtrlH);
        auto durationBounds = controlRow.removeFromLeft(colW);
        controlRow.removeFromLeft(colGap);

        durationSlider.setBounds(durationBounds.withSizeKeepingCentre(durationBounds.getWidth(), compactCtrlH));

        const float seedFontSize = f * 1.25f;
        const float toggleFontSize = juce::jmin(15.0f, static_cast<float>(seedCtrlH) * 0.72f);
        const int minToggleW = measureTextWidth(randomSeedToggle.getButtonText(), toggleFontSize)
                             + juce::roundToInt(f * 1.2f);

        auto seedRow = controlRow.reduced(0, 1);
        int toggleW = juce::jmax(juce::roundToInt(static_cast<float>(seedRow.getWidth()) * 0.32f), minToggleW);
        toggleW = juce::jmin(toggleW, seedRow.getWidth() / 2);

        randomSeedToggle.setBounds(seedRow.removeFromRight(toggleW));
        seedEditor.setBounds(seedRow);
        syncSeedEditorFont(seedFontSize);

        area.removeFromTop(gap);
    };

    auto layoutEasyDurationSeedRow = [&]
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

        auto controlRow = area.removeFromTop(seedCtrlH);
        auto durationBounds = controlRow.removeFromLeft(colW);
        controlRow.removeFromLeft(colGap);

        durationSlider.setBounds(durationBounds.withSizeKeepingCentre(durationBounds.getWidth(), compactCtrlH));

        auto seedRow = controlRow.reduced(0, 1);
        for (int i = 0; i < kNumSeedModeBtns; ++i)
        {
            const int cellWSeed = (i == kNumSeedModeBtns - 1)
                ? seedRow.getWidth()
                : juce::jmax(1, seedRow.getWidth() / (kNumSeedModeBtns - i));
            seedModeBtns[i].setBounds(seedRow.removeFromLeft(cellWSeed));
        }

        seedModeSwitchBounds = seedModeBtns[0].getBounds();
        for (int i = 1; i < kNumSeedModeBtns; ++i)
            seedModeSwitchBounds = seedModeSwitchBounds.getUnion(seedModeBtns[i].getBounds());

        area.removeFromTop(gap);
    };

    if (easy)
    {
        layoutEasyDurationSeedRow();
    }
    else
    {
        layoutCompactPair(magLabel, magnitudeSlider, magValue,
                          noiseLabel, noiseSlider, noiseValue);
        layoutCompactPair(stepsLabel, stepsSlider, stepsValue,
                          cfgLabel, cfgSlider, cfgValue);
        layoutDurationSeedRow();
    }

    // Info label at the bottom of the sequential layout
    area.removeFromTop(gap);
    setFs(infoLabel, f);
    infoLabel.setBounds(area.removeFromTop(compactRowH));
}

void PromptPanel::loadPresetData(const juce::String& promptA, const juce::String& promptB,
                                  int seed, bool randomSeed,
                                  const juce::String& device,
                                  const juce::String& model,
                                  const juce::String& injectionMode,
                                  float lateMixAmount,
                                  float splitStart,
                                  float splitEnd)
{
    juce::ignoreUnused(device);
    promptAEditor.setText(promptA, false);
    promptBEditor.setText(promptB, false);
    lastGenPromptA_.clear();
    lastGenPromptB_.clear();
    randomSeedToggle.setToggleState(randomSeed, juce::dontSendNotification);
    syncSeedEditorDisplay(seed, true);
    syncSeedEditorEnabledState();
    syncSeedModeFromCurrentState();
    // Keep the cached auto-state aligned with what we just restored, so a
    // subsequent Save (without re-touching the UI) round-trips correctly.
    processorRef.setLastRandomSeed(randomSeed);
    if (auto* startParam = processorRef.getValueTreeState().getParameter(PID::genStart))
        startParam->setValueNotifyingHost(0.0f);

    // Apply research-mode injection state if the preset carries it. Old .t5p
    // files predating this feature pass empty/NaN sentinels here, in which case
    // we keep the panel's current values and skip applyModeToSlider() (the
    // panel's mode buttons / slider stay where the user left them).
    bool injectionDirty = false;
    if (injectionMode.isNotEmpty())
    {
        injectionMode_ = injectionMode;
        injectionDirty = true;
        injModeLinear.setToggleState(injectionMode == "linear",      juce::dontSendNotification);
        injModeFine  .setToggleState(injectionMode == "late_step",   juce::dontSendNotification);
        injModeLayer .setToggleState(injectionMode == "layer_split", juce::dontSendNotification);
        injModeKombi1.setToggleState(injectionMode == "kombi1",      juce::dontSendNotification);
        injModeKombi2.setToggleState(injectionMode == "kombi2",      juce::dontSendNotification);
        injModeKombi3.setToggleState(injectionMode == "kombi3",      juce::dontSendNotification);
    }
    if (!std::isnan(lateMixAmount))
    {
        // The preset stores a single lateMixAmount. Restore it to the slot
        // that matches the preset's injectionMode (when present); otherwise
        // restore to the currently active mode's slot.
        const juce::String slotMode = injectionMode.isNotEmpty() ? injectionMode : injectionMode_;
        lateMixForMode(slotMode) = juce::jlimit(0.0f, 1.0f, lateMixAmount);
        injectionDirty = true;
    }

    // Select the preset's model BEFORE clamping split values. Without this
    // step the clamp would use the previously selected model's ditBlocks_,
    // potentially truncating a preset whose SA3-sized splitEnd is valid
    // under SA3 but clipped to SAO Small's 16-block ceiling. modelBtns'
    // setToggleState is called with dontSendNotification so the onClick
    // lambda doesn't preload the model from a preset load — we just need
    // the UI state. refreshDitBlocksForCurrentModel then pulls the right
    // depth out of the backend's handshake metadata.
    bool deferredModel = false;
    if (model.isNotEmpty())
    {
        if (modelsPopulated)
        {
            // Exact installed id first, then family fallback so a legacy preset
            // id (pre-split "stable-audio-3-small") still selects its SA3 slot
            // instead of silently leaving the previously active model selected.
            const int s = slotForModel(model);
            if (s >= 0)
                modelBtns[s].setToggleState(true, juce::dontSendNotification);
            refreshDitBlocksForCurrentModel();
        }
        else
        {
            pendingModel_ = model;
            deferredModel = true;
        }
    }

    // Preset block-count may differ from the currently active model's
    // (e.g. saved under SA3 then recalled under SAO Small). Clamp to the
    // active model's ditBlocks_ ceiling so the slider stays in range.
    // When the model selection is deferred (backend not ready yet) we
    // stash the raw values so populateModelSelector can replay them once
    // ditBlocks_ reflects the preset's intended model.
    if (deferredModel)
    {
        if (!std::isnan(splitStart)) pendingSplitStart_ = splitStart;
        if (!std::isnan(splitEnd))   pendingSplitEnd_   = splitEnd;
    }
    else
    {
        const float blocksF = static_cast<float>(ditBlocks_);
        if (!std::isnan(splitStart))
        {
            splitLayerStart_ = juce::jlimit(0.0f, blocksF, splitStart);
            injectionDirty = true;
        }
        if (!std::isnan(splitEnd))
        {
            splitLayerEnd_ = juce::jlimit(0.0f, blocksF, splitEnd);
            injectionDirty = true;
        }
    }
    if (injectionDirty)
        applyModeToSlider();

    syncInjectionModeAvailability();
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

// Map a model id to its fixed switchbox slot by family pattern, independent of
// whether that slot is installed. Slot 0 SA3 Music, 1 SA3 SFX, 2 SA1 Open,
// 3 SA1 Small, 4 AudioLDM2; -1 if no family matches.
//
// Order matters: SA3 names contain "small", so the SA3 check must fire before
// the SA1 Small fallback; within SA3 the "sfx" sub-check splits Music (0) from
// SFX (1) — both carry "stable-audio-3". This is the single source of truth for
// the mapping; populateModelSelector and slotForModel both defer to it.
static int patternSlotFor(const juce::String& m)
{
    if (m.containsIgnoreCase("stable-audio-3"))     return m.containsIgnoreCase("sfx") ? 1 : 0;
    if (m.containsIgnoreCase("small"))              return 3;  // SA1 Small
    if (m.containsIgnoreCase("stable-audio-open"))  return 2;  // SA1 Open 1.0
    if (m.containsIgnoreCase("audioldm")
        || m.containsIgnoreCase("audio-ldm"))       return 4;  // AudioLDM2
    return -1;
}

// Resolve a stored/preset model id to an INSTALLED slot: exact installed-id
// match first, then a family fallback (patternSlotFor) so a legacy preset id —
// e.g. the pre-split "stable-audio-3-small" — still selects its family's slot
// when that model is installed. -1 when nothing suitable is installed.
int PromptPanel::slotForModel(const juce::String& model) const
{
    if (model.isEmpty())
        return -1;
    for (int i = 0; i < kNumModelSlots; ++i)
        if (modelSlotIds[i] == model)
            return i;
    const int s = patternSlotFor(model);
    return (s >= 0 && s < kNumModelSlots && modelSlotIds[s].isNotEmpty()) ? s : -1;
}

void PromptPanel::populateModelSelector()
{
    auto& pipeInf = processorRef.getPipeInference();
    auto& models = pipeInf.getAvailableModels();

    // Match available models to fixed slots by pattern
    // Slot 0: SA3 Music, 1: SA3 SFX, 2: SA1 Open, 3: SA1 Small, 4: AudioLDM2
    for (int i = 0; i < kNumModelSlots; ++i)
        modelSlotIds[i] = {};

    for (auto& m : models)
    {
        const int slot = patternSlotFor(m);
        if (slot >= 0 && slot < kNumModelSlots)
        {
            modelSlotIds[slot] = m;
            modelBtns[slot].setEnabled(true);
            modelBtns[slot].setAlpha(1.0f);
        }
    }

    // Select model: pending preset model > leftmost installed slot in display
    // order. SA3 Music sits at slot 0, so "leftmost installed" naturally
    // prefers SA3 when present and falls through SA1 Open → SA1 Small →
    // AudioLDM2 in turn when earlier slots are empty.
    int selectIdx = -1;
    if (pendingModel_.isNotEmpty())
    {
        selectIdx = slotForModel(pendingModel_);   // exact id, then family fallback
        pendingModel_ = {};
    }
    if (selectIdx < 0)
    {
        for (int i = 0; i < kNumModelSlots; ++i)
            if (modelSlotIds[i].isNotEmpty()) { selectIdx = i; break; }
    }
    if (selectIdx >= 0)
        modelBtns[selectIdx].setToggleState(true, juce::dontSendNotification);

    modelsPopulated = true;
    syncInjectionModeAvailability();
    refreshDitBlocksForCurrentModel();

    // Replay deferred preset splits now that ditBlocks_ reflects the real
    // model. loadPresetData stashed these when the backend wasn't ready yet
    // and the clamp ceiling was still the previous model's value.
    const bool hasPendingStart = !std::isnan(pendingSplitStart_);
    const bool hasPendingEnd   = !std::isnan(pendingSplitEnd_);
    if (hasPendingStart || hasPendingEnd)
    {
        const float blocksF = static_cast<float>(ditBlocks_);
        if (hasPendingStart)
            splitLayerStart_ = juce::jlimit(0.0f, blocksF, pendingSplitStart_);
        if (hasPendingEnd)
            splitLayerEnd_   = juce::jlimit(0.0f, blocksF, pendingSplitEnd_);
        pendingSplitStart_ = std::numeric_limits<float>::quiet_NaN();
        pendingSplitEnd_   = std::numeric_limits<float>::quiet_NaN();
        if (injectionMode_ == "layer_split")
            applyModeToSlider();
    }

    resized();
}

void PromptPanel::refreshDitBlocksForCurrentModel()
{
    const auto model = getSelectedModel();
    if (model.isEmpty())
    {
        ditBlocks_ = 16;
        return;
    }

    const auto meta = processorRef.getPipeInference().getModelMetadata(model);
    // Defensive clamp: a backend reporting absurd block counts (negative,
    // zero, hundreds) would either invert the slider range or stretch it
    // beyond anything the kombi geometry tolerates. 64 is well above the
    // largest SA3 variant and gives us headroom for future architectures.
    const int blocks = juce::jlimit(1, 64, meta.ditBlocks);
    ditBlocks_ = blocks;

    // Re-clamp the panel's own state so any saved value beyond the new
    // ceiling (e.g. preset switched from SA3 back to SAO) snaps inside.
    const float maxF = static_cast<float>(blocks);
    splitLayerStart_ = juce::jlimit(0.0f, maxF, splitLayerStart_);
    splitLayerEnd_   = juce::jlimit(0.0f, maxF, splitLayerEnd_);

    // The alphaSlider's range is set inside applyModeToSlider; only re-run
    // it when layer_split is the active mode, otherwise we'd overwrite the
    // current mode's slider settings.
    if (injectionMode_ == "layer_split")
        applyModeToSlider();
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

void PromptPanel::setEasyMode(bool easy)
{
    if (easyMode_ == easy)
        return;

    easyMode_ = easy;
    if (easyMode_)
        syncSeedModeFromCurrentState();

    resized();
    repaint();
}

bool PromptPanel::hasHiddenActiveState() const
{
    if (!easyMode_)
        return false;

    auto& apvts = processorRef.getValueTreeState();
    auto differs = [](float value, float neutral, float epsilon)
    {
        return std::abs(value - neutral) > epsilon;
    };

    if (auto* v = apvts.getRawParameterValue(PID::genMagnitude))
        if (differs(v->load(), 1.0f, 0.001f))
            return true;

    if (auto* v = apvts.getRawParameterValue(PID::genNoise))
        if (differs(v->load(), 0.0f, 0.001f))
            return true;

    // Compare steps/CFG against the active model's defaults (the same
    // values the model-click handler writes on selection). Skip when no
    // model is selected yet — before populateModelSelector runs there's
    // nothing to compare against, and the user couldn't have changed
    // anything from the easy-mode UI either.
    const auto selectedModel = getSelectedModel();
    if (selectedModel.isNotEmpty())
    {
        const auto defaults = defaultParamsFor(selectedModel);

        if (auto* v = apvts.getRawParameterValue(PID::infSteps))
            if (static_cast<int>(std::round(v->load())) != static_cast<int>(std::round(defaults.steps)))
                return true;

        if (auto* v = apvts.getRawParameterValue(PID::genCfg))
            if (differs(v->load(), defaults.cfg, 0.001f))
                return true;
    }

    return false;
}

juce::String PromptPanel::getSelectedModel() const
{
    for (int i = 0; i < kNumModelSlots; ++i)
        if (modelBtns[i].getToggleState() && modelSlotIds[i].isNotEmpty())
            return modelSlotIds[i];
    return {};
}

bool PromptPanel::isAudioLDM2Model(const juce::String& model) const
{
    return model.containsIgnoreCase("audioldm")
        || model.containsIgnoreCase("audio-ldm");
}

// Per-model defaults for steps / CFG. Single source for the model-click
// handler (which writes these into APVTS on model switch) and for
// hasHiddenActiveState (which compares against them to decide whether
// easy mode's params have been user-modified).
//  - SA3 Small Music: 8 steps, CFG 1.0 — verified from the model's own
//    model_config.json (training.demo.demo_steps = 8, demo_cfg_scales = [1])
//    and the HF model card's sample code (generate_diffusion_cond(...,
//    steps=8, cfg_scale=1.0, sampler_type="pingpong")). May 2026 sources.
//    Matched by SA3 prefix so future variants ("medium", future fine-tunes)
//    land here automatically rather than falling through to the SAO 1.0
//    branch.
//  - SAO Small: 8 steps, CFG 1.0 — same numbers, separate cause
//    (SAO Small's own model card recommends these).
//  - AudioLDM2: 50 / 3.5.
//  - SAO 1.0 and anything else: 20 / 7.0 (historical Brownian-tree
//    path defaults).
PromptPanel::DefaultParams PromptPanel::defaultParamsFor(const juce::String& model) const
{
    const bool isSA3       = model.containsIgnoreCase("stable-audio-3");
    const bool isSAOSmall  = !isSA3 && model.containsIgnoreCase("small");
    const bool isAudioLDM2 = isAudioLDM2Model(model);
    return {
        (isSA3 || isSAOSmall) ? 8.0f : (isAudioLDM2 ? 50.0f : 20.0f),
        (isSA3 || isSAOSmall) ? 1.0f : (isAudioLDM2 ? 3.5f  : 7.0f),
    };
}

bool PromptPanel::selectedModelIsAudioLDM2() const
{
    return isAudioLDM2Model(getSelectedModel());
}

bool PromptPanel::isSA3Model(const juce::String& model) const
{
    // Matches the populateModelSelector slotting and defaultParamsFor logic so
    // present and future SA3 variants ("small-music", "medium", fine-tunes)
    // are all recognised by the shared "stable-audio-3" prefix.
    return model.containsIgnoreCase("stable-audio-3");
}

bool PromptPanel::selectedModelIsSA3() const
{
    return isSA3Model(getSelectedModel());
}

void PromptPanel::setSeedMode(SeedMode mode, bool applyState)
{
    seedMode_ = mode;

    if (applyState)
    {
        if (mode == SeedMode::base)
        {
            randomSeedToggle.setToggleState(false, juce::dontSendNotification);
            syncSeedEditorDisplay(kBaseSeed, true);
        }
        else if (mode == SeedMode::steady)
        {
            randomSeedToggle.setToggleState(false, juce::dontSendNotification);
            if (seedEditor.getText().trim().isEmpty() || seedEditor.getText().getIntValue() <= 0)
            {
                const int lastSeed = processorRef.getLastSeed() > 0 ? processorRef.getLastSeed()
                                                                    : kBaseSeed;
                syncSeedEditorDisplay(lastSeed, true);
            }
        }
        else
        {
            randomSeedToggle.setToggleState(true, juce::dontSendNotification);
        }

        syncSeedEditorEnabledState();
    }

    seedMode_ = mode;
    syncSeedModeButtons();
    // Mirror the Easy-mode choice on the processor so PresetFormat::save sees
    // the current auto/random intent even when the user hasn't generated yet.
    processorRef.setLastRandomSeed(mode == SeedMode::autoRandom);
}

void PromptPanel::syncSeedModeFromCurrentState()
{
    if (randomSeedToggle.getToggleState())
        seedMode_ = SeedMode::autoRandom;
    else if (seedEditor.getText().getIntValue() == kBaseSeed)
        seedMode_ = SeedMode::base;
    else
        seedMode_ = SeedMode::steady;

    syncSeedModeButtons();
}

void PromptPanel::syncSeedModeButtons()
{
    for (int i = 0; i < kNumSeedModeBtns; ++i)
        seedModeBtns[i].setToggleState(static_cast<int>(seedMode_) == i,
                                       juce::dontSendNotification);
}

void PromptPanel::selectInjectionMode(const juce::String& mode, bool shouldTrigger)
{
    injectionMode_ = selectedModelIsAudioLDM2() ? "linear" : mode;
    injModeLinear.setToggleState(injectionMode_ == "linear",      juce::dontSendNotification);
    injModeFine  .setToggleState(injectionMode_ == "late_step",   juce::dontSendNotification);
    injModeLayer .setToggleState(injectionMode_ == "layer_split", juce::dontSendNotification);
    injModeKombi1.setToggleState(injectionMode_ == "kombi1",      juce::dontSendNotification);
    injModeKombi2.setToggleState(injectionMode_ == "kombi2",      juce::dontSendNotification);
    injModeKombi3.setToggleState(injectionMode_ == "kombi3",      juce::dontSendNotification);
    applyModeToSlider();
    syncInjectionModeAvailability();

    if (shouldTrigger)
        triggerGeneration();
}

void PromptPanel::syncInjectionModeAvailability()
{
    const bool audioLDM2 = selectedModelIsAudioLDM2();
    if (audioLDM2 && injectionMode_ != "linear")
    {
        injectionMode_ = "linear";
        injModeLinear.setToggleState(true, juce::dontSendNotification);
        injModeFine  .setToggleState(false, juce::dontSendNotification);
        injModeLayer .setToggleState(false, juce::dontSendNotification);
        injModeKombi1.setToggleState(false, juce::dontSendNotification);
        injModeKombi2.setToggleState(false, juce::dontSendNotification);
        injModeKombi3.setToggleState(false, juce::dontSendNotification);
        applyModeToSlider();
    }

    const auto nonLinearAlpha = audioLDM2 ? 0.32f : 1.0f;
    const auto nonLinearEnabled = !audioLDM2;
    for (auto* b : { &injModeFine, &injModeLayer, &injModeKombi1, &injModeKombi2, &injModeKombi3 })
    {
        b->setEnabled(nonLinearEnabled);
        b->setAlpha(nonLinearAlpha);
        b->setTooltip(audioLDM2 ? "AudioLDM2 supports Linear mode only" : "");
    }
    injModeLinear.setEnabled(true);
    injModeLinear.setAlpha(1.0f);
    injModeLinear.setTooltip(audioLDM2 ? "AudioLDM2 supports Linear mode only" : "");

    // Every model-change path (button click, preset load, backend availability)
    // funnels through here — the same chokepoint that already gates injection
    // mode by model. Notify MainPanel so it can grey out the AxesPanel for SA3,
    // whose semantic axes are disabled pending recalculation. Cheap, idempotent,
    // and user-driven (never on the audio/timer hot path).
    if (onModelChanged)
        onModelChanged();
}

void PromptPanel::syncSeedEditorEnabledState()
{
    const bool randomSeed = randomSeedToggle.getToggleState();
    seedEditor.setEnabled(!randomSeed);
    seedEditor.setAlpha(randomSeed ? 0.3f : 1.0f);
}

void PromptPanel::syncSeedEditorFont(float size)
{
    float fittedSize = size;
    const auto bounds = seedEditor.getLocalBounds();

    if (!bounds.isEmpty())
        fittedSize = juce::jmin(fittedSize, static_cast<float>(bounds.getHeight()) * 0.78f);

    juce::Font font { juce::FontOptions(fittedSize) };
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

    syncSeedEditorFont(preferredPromptFontForWidth(getWidth()) * 1.25f);
}

void PromptPanel::triggerGenerationWithOffsets(std::vector<std::pair<int, float>> offsets)
{
    pendingOffsets_ = std::move(offsets);
    triggerGeneration();
}

// ── Per-mode slider memory ───────────────────────────────────────────────────
float& PromptPanel::lateMixForMode(const juce::String& mode)
{
    if (mode == "kombi1") return lateMixKombi1_;
    if (mode == "kombi2") return lateMixKombi2_;
    if (mode == "kombi3") return lateMixKombi3_;
    return lateMixFine_;  // late_step + fallback for linear/layer_split
}

float PromptPanel::lateMixForMode(const juce::String& mode) const
{
    if (mode == "kombi1") return lateMixKombi1_;
    if (mode == "kombi2") return lateMixKombi2_;
    if (mode == "kombi3") return lateMixKombi3_;
    return lateMixFine_;
}

// ── Reconfigure alphaSlider for the active injection mode ────────────────────
// Linear: APVTS-attached, range −2..+2 (quadratic skew near zero), label "A ↔ B".
// Step-in: detached, range 0..1, label "Step-in / Combo: A → mix".
// Layer : detached, range 0..ditBlocks (snap=1), label "Layer Split".
// The alphaSlider's onValueChange dispatches on injectionMode_ to update both
// the value formatter and (for non-linear) the local state.
void PromptPanel::applyModeToSlider()
{
    auto& apvts = processorRef.getValueTreeState();
    if (injectionMode_ == "linear")
    {
        // Restore single-thumb style in case we're coming from layer mode.
        alphaSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        // Range is owned by the SliderAttachment — JUCE 8's
        // SliderParameterAttachment ctor calls slider.setNormalisableRange()
        // with the APVTS parameter's NormalisableRange. For genAlpha that is
        // [-2, +2] with a quadratic skew for fine control near zero.
        //
        // This used to call setRange(-1, 1, 0.001) defensively. That
        // hardcoded the wrong endpoints AND dropped the parameter skew. On
        // initial enter-linear the `if (alphaA == nullptr)` guard below
        // skipped Attachment recreation, so the bogus range stuck. Mode-
        // toggling reset alphaA, which on re-enter recreated the
        // Attachment and silently restored the correct range — surfacing
        // the bug as "scale is -1..1 on first open, becomes -2..2 after
        // toggling modes". The defensive call is gone; the Attachment is
        // now the single source of truth for both range and skew.
        if (alphaA == nullptr)
            alphaA = std::make_unique<Attachment>(apvts, PID::genAlpha, alphaSlider);
        alphaLabel.setText("A " + juce::String(juce::CharPointer_UTF8("\xe2\x86\x94")) + " B",
                           juce::dontSendNotification);
        // Onchange handler will populate alphaValue from current slider value.
        alphaSlider.setValue(alphaSlider.getValue(), juce::sendNotificationSync);
    }
    else if (injectionMode_ == "late_step"
          || injectionMode_ == "kombi1"
          || injectionMode_ == "kombi2"
          || injectionMode_ == "kombi3")
    {
        alphaA.reset();  // detach from APVTS so the slider drives local state only
        alphaSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        // Slider is the intensity 0..1: 0 = minimum perceptible effect, 1 =
        // maximum. Internal mapping (in buildInferenceRequest) shifts this
        // onto the audible region of injection_transition_at / late_phase_alpha.
        // setRange() may clamp the current value and fire onValueChange,
        // which would overwrite the saved state — capture and restore.
        const float saved = juce::jlimit(0.0f, 1.0f, lateMixForMode(injectionMode_));
        alphaSlider.setRange(0.0, 1.0, 0.01);
        lateMixForMode(injectionMode_) = saved;
        const juce::String prefix = (injectionMode_ == "kombi1") ? "Combo 1: A "
                                  : (injectionMode_ == "kombi2") ? "Combo 2: A "
                                  : (injectionMode_ == "kombi3") ? "Combo 3: A "
                                  :                                "Step-in: A ";
        alphaLabel.setText(prefix + juce::String(juce::CharPointer_UTF8("\xe2\x86\x92")) + " mix",
                           juce::dontSendNotification);
        alphaSlider.setValue(saved, juce::sendNotificationSync);
    }
    else  // layer_split — two-thumb range slider [b_start, b_end]
    {
        alphaA.reset();
        alphaSlider.setSliderStyle(juce::Slider::TwoValueHorizontal);
        const float blocksF = static_cast<float>(ditBlocks_);
        const float savedStart = juce::jlimit(0.0f, blocksF, splitLayerStart_);
        const float savedEnd   = juce::jlimit(0.0f, blocksF, splitLayerEnd_);
        alphaSlider.setRange(0.0, static_cast<double>(blocksF), 1.0);
        splitLayerStart_ = savedStart;
        splitLayerEnd_   = savedEnd;
        alphaLabel.setText("Layer B-zone (low-high)", juce::dontSendNotification);
        // Set both thumbs without firing notifications, then trigger onValueChange
        // once at the end to populate the value display from saved state.
        alphaSlider.setMinValue(savedStart, juce::dontSendNotification, true);
        alphaSlider.setMaxValue(savedEnd,   juce::sendNotificationSync, true);
    }
}

// ──────────────────────────────────────────────────────────────────────────────
// Shared request builder
// ──────────────────────────────────────────────────────────────────────────────
PipeInference::Request PromptPanel::buildInferenceRequest(
    float alphaOverride, std::map<juce::String, float> axesOverride,
    float noiseOverride, float magnitudeOverride,
    float lateMixOverride, float splitStartOverride, float splitEndOverride)
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

    // Mode-specific parameter resolution: drift-driven overrides win when
    // present, otherwise fall back to the panel's slider state.
    const float effLateMix    = std::isnan(lateMixOverride)    ? lateMixForMode(injectionMode_) : lateMixOverride;
    const float effSplitStart = std::isnan(splitStartOverride) ? splitLayerStart_   : splitStartOverride;
    const float effSplitEnd   = std::isnan(splitEndOverride)   ? splitLayerEnd_     : splitEndOverride;

    PipeInference::Request req;
    req.promptA = promptAEditor.getText().trim();
    req.promptB = promptBEditor.getText().trim();
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
    const auto requestInjectionMode = isAudioLDM2Model(req.model) ? juce::String("linear")
                                                                  : injectionMode_;
    req.dimensionOffsets = std::move(pendingOffsets_);
    req.semanticAxes = axesOverride.empty() ? std::move(pendingAxes_) : std::move(axesOverride);
    req.axesAmount = apvts.getRawParameterValue(PID::genAxesAmount)->load();
    // Semantic axes AND the dimension explorer are disabled for SA3 (both panels
    // are greyed out for it). Clear them here regardless of source so values left
    // in the panels — e.g. from a preset saved under SAO and recalled under SA3 —
    // are never sent. The backend ignores both for SA3 too, as a safety net.
    if (isSA3Model(req.model))
    {
        req.semanticAxes.clear();
        req.dimensionOffsets.clear();
    }
    req.injectionMode = requestInjectionMode;
    // Single-prompt promptability guard (linear mode, slider-driven α only).
    // The linear blend (0.5−0.5α)·A + (0.5+0.5α)·B places a lone prompt at α=∓1
    // and the null / unconditional output at the α=0 slider centre — so typing
    // one prompt and leaving the slider untouched would render null, not the
    // prompt (on SA3 that null is a buzz). When exactly one field is filled, pin
    // α to that prompt's pure end. Symmetric: A-only and B-only are equal
    // partners (the A/B-equality design). Two-prompt blends stay untouched, and
    // delta / step / layer modes — which don't collapse at α=0 — are excluded.
    // Skipped when α is explicitly overridden (drift / explorer alpha sweep), so
    // intentional exploration through the null point still works.
    if (requestInjectionMode == "linear" && std::isnan(alphaOverride)
        && req.promptA.isEmpty() != req.promptB.isEmpty())
        req.alpha = req.promptB.isEmpty() ? -1.0f : 1.0f;  // B empty → pure A; A empty → pure B
    // Step-in slider drives BOTH transition_at AND late-phase α together so that
    // slider=0.5 → minimum effect (A-dominant), slider=1.0 → pure B.
    //   t = (slider - 0.5) / 0.5  ∈ [0, 1]
    //   transition_at: 0.5 (halfway swap) → 0.05 (almost immediate)
    //   late_α       : 0   (50/50 mix)    → 1   (pure B)
    {
        const float t = juce::jlimit(0.0f, 1.0f, effLateMix);
        req.injectionTransitionAt = juce::jlimit(0.05f, 0.95f, 0.5f - 0.45f * t);
        req.latePhaseAlpha        = t;  // 0 → 50/50, 1 → pure B
    }
    // Layer: two-thumb range slider directly defines the B-zone [start, end]
    // in DiT block index space. No inversion needed — the user's mental model
    // (low thumb = where B starts, high thumb = where B ends) maps 1:1 to the
    // backend's b_start / b_end fields.
    const float blocksF = static_cast<float>(ditBlocks_);
    req.splitStart = juce::jlimit(0.0f, blocksF, effSplitStart);
    req.splitEnd   = juce::jlimit(0.0f, blocksF, effSplitEnd);
    // Combo modes overwrite the layer range with their per-mode band so drift /
    // preset / slider state can never desync the geometry.
    // Combo 1 = "B as surface skin" (low DiT blocks);
    // Combo 2 = "B as gestalt filter" (broad mid).
    //
    // The bands are FRACTIONS of the active model's DiT depth (blocksF =
    // ditBlocks_), so they map to the same relative depths on any model
    // (SAO Small=16 → historical 0-4 / 4-12 / 6-10; SA3=20 → 0-5 / 5-15 /
    // 7.5-12.5). The backend (_generate_native) re-asserts the identical
    // fractions, so both sides stay in lock-step automatically.
    if (requestInjectionMode == "kombi1") { req.splitStart = 0.0f;            req.splitEnd = 0.25f  * blocksF; }
    if (requestInjectionMode == "kombi2") { req.splitStart = 0.25f  * blocksF; req.splitEnd = 0.75f  * blocksF; }
    if (requestInjectionMode == "kombi3") { req.splitStart = 0.375f * blocksF; req.splitEnd = 0.625f * blocksF; }
    return req;
}

// ──────────────────────────────────────────────────────────────────────────────
// Manual generation (Generate button / Enter)
// ──────────────────────────────────────────────────────────────────────────────
bool PromptPanel::playNextCachedInference()
{
    if (!processorRef.playNextInferenceCacheEntry())
        return false;

    pendingOffsets_.clear();
    if (onStatusChanged) onStatusChanged("From cache", false);
    return true;
}

void PromptPanel::triggerGeneration()
{
    if (generating) return;

    if (processorRef.isInferenceCacheFull())
    {
        playNextCachedInference();
        return;
    }

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
        auto inferenceResult = pipePtr->generate(req);
        juce::MessageManager::callAsync([safeThis, result = std::move(inferenceResult), req, deviceForLabel, modelForLabel]()
        {
            if (auto* self = safeThis.getComponent())
            {
                auto& processor = self->processorRef;
                self->generating = false;
                self->generateButton.setEnabled(true);
                if (result.success)
                {
                    processor.addInferenceCacheEntry(result.audio, result.sampleRate);
                    processor.loadGeneratedAudio(result.audio, result.sampleRate);
                    processor.setLastDevice(deviceForLabel);
                    processor.setLastModel(modelForLabel);
                    processor.setLastSeed(result.seed);
                    auto promptA = self->promptAEditor.getText().trim();
                    auto promptB = self->promptBEditor.getText().trim();
                    processor.setLastPrompts(promptA, promptB);
                    self->lastGenPromptA_ = promptA;
                    self->lastGenPromptB_ = promptB;
                    self->syncSeedEditorDisplay(result.seed);
                    processor.setLastGenerationTimeMs(result.generationTimeMs);
                    // A successful manual generate proves the backend is
                    // healthy — clear any stale failure stamp left by a
                    // previous drift auto-regen so the next auto-tick
                    // isn't throttled for up to 2s after recovery.
                    self->lastRegenFailureMs_ = 0.0;
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
                                            float effectiveLateMix,
                                            float effectiveSplitStart,
                                            float effectiveSplitEnd,
                                            bool /*holdForBar*/)
{
    if (generating) return;

    lastGenAlpha_ = effectiveAlpha;
    lastGenNoise_ = effectiveNoise;
    lastGenMagnitude_ = effectiveMagnitude;
    lastGenAxes_ = effectiveAxes;
    lastGenLateMix_ = effectiveLateMix;
    lastGenSplitStart_ = effectiveSplitStart;
    lastGenSplitEnd_ = effectiveSplitEnd;

    if (processorRef.isInferenceCacheFull())
    {
        playNextCachedInference();
        return;
    }

    if (!processorRef.isPipeInferenceReady()) return;

    generating = true;
    generateButton.setEnabled(false);
    if (onStatusChanged) onStatusChanged("auto regen...", true);

    auto req = buildInferenceRequest(effectiveAlpha, effectiveAxes, effectiveNoise, effectiveMagnitude,
                                     effectiveLateMix, effectiveSplitStart, effectiveSplitEnd);
    auto deviceForLabel = req.device.isEmpty()
        ? processorRef.getPipeInference().getDefaultDevice() : req.device;
    auto modelForLabel = req.model.isEmpty()
        ? processorRef.getPipeInference().getDefaultModel() : req.model;
    auto pipePtr = processorRef.getPipeInferencePtr();
    juce::Component::SafePointer<PromptPanel> safeThis(this);
    std::thread([safeThis, pipePtr, req, deviceForLabel, modelForLabel]()
    {
        auto inferenceResult = pipePtr->generate(req);
        juce::MessageManager::callAsync([safeThis, result = std::move(inferenceResult), req, deviceForLabel, modelForLabel]()
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
                    int xfadeSamples = juce::roundToInt(xfadeMs * 0.001f * static_cast<float>(result.sampleRate));
                    if (xfadeSamples > 0)
                    {
                        const auto& oldRaw = processor.getGeneratedAudioRaw();
                        if (oldRaw.getNumSamples() > 0)
                            applyDriftCrossfade(newAudio, oldRaw, xfadeSamples);
                    }
                    processor.addInferenceCacheEntry(result.audio, result.sampleRate);
                    processor.loadGeneratedAudio(newAudio, result.sampleRate);
                    auto promptA = self->promptAEditor.getText().trim();
                    auto promptB = self->promptBEditor.getText().trim();
                    processor.setLastDevice(deviceForLabel);
                    processor.setLastModel(modelForLabel);
                    processor.setLastSeed(result.seed);
                    processor.setLastPrompts(promptA, promptB);
                    self->lastGenPromptA_ = promptA;
                    self->lastGenPromptB_ = promptB;
                    self->syncSeedEditorDisplay(result.seed);
                    processor.setLastGenerationTimeMs(result.generationTimeMs);
                    // Healthy generation — clear the failure throttle so the
                    // next drift change can fire immediately again.
                    self->lastRegenFailureMs_ = 0.0;
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
                    // Drift auto-regen failure — stamp the failure clock so
                    // pollDriftRegen throttles the next attempt. Without this
                    // the 10 Hz timer would re-trigger generation on every
                    // tick after `generating` flips back to false, oscillating
                    // the status label between "auto regen..." and the error.
                    self->lastRegenFailureMs_ = juce::Time::getMillisecondCounterHiRes();
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

    // Failure throttle: when the previous drift-driven regen failed (e.g.
    // the backend rejected the selected model), wait ~2s before retrying so
    // the 10 Hz timer can't spin and oscillate the status label.
    if (lastRegenFailureMs_ > 0.0)
    {
        constexpr double FAILURE_COOLDOWN_MS = 2000.0;
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        if ((nowMs - lastRegenFailureMs_) < FAILURE_COOLDOWN_MS)
            return;
    }

    // Idle-CPU guard: cache replay runs the full loadGeneratedAudio path
    // (rumble/HF/normalize + WT frame extraction) per cycle, and at idle no
    // voice picks up the new buffer — pure wasted work. Real-inference
    // generation (cache-fill path) is allowed to continue so the cache can
    // still pre-fill while the user is not playing.
    const bool fullCachePlayback = processorRef.isInferenceCacheFull();
    if (fullCachePlayback
        && processorRef.audioIdle.load(std::memory_order_relaxed))
        return;

    // Beat-based cooldown: modes 2-4 = max 1/4/16 beats. When the
    // inference cache is full, Auto is throttled to the 1-beat cadence
    // so cache playback cannot run at the GUI polling rate.
    if (regenMode >= 2 || (fullCachePlayback && regenMode == 1))
    {
        static constexpr int beatCounts[] = { 0, 0, 1, 4, 16 };
        int beats = fullCachePlayback && regenMode == 1
            ? 1
            : beatCounts[juce::jlimit(0, 4, regenMode)];
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

    // Mode-specific drift mapping: the same alpha-LFO offset (in alpha-units)
    // drives a different parameter per mode. This keeps the Drift Panel UX
    // simple ("Alpha" target works in all modes) while ensuring drift actually
    // moves audible parameters in Step-in/Layer.
    auto& apvts = processorRef.getValueTreeState();
    const float baseAlphaForOff = apvts.getRawParameterValue(PID::genAlpha)->load();
    const float alphaOff = std::isnan(effAlpha) ? 0.0f : (effAlpha - baseAlphaForOff);

    float effectiveLateMix    = lateMixForMode(injectionMode_);
    float effectiveSplitStart = splitLayerStart_;
    float effectiveSplitEnd   = splitLayerEnd_;
    const bool fineLikeMode = (injectionMode_ == "late_step"
                            || injectionMode_ == "kombi1"
                            || injectionMode_ == "kombi2"
                            || injectionMode_ == "kombi3");
    if (fineLikeMode && std::abs(alphaOff) > 0.001f)
    {
        effectiveLateMix = juce::jlimit(0.0f, 1.0f, lateMixForMode(injectionMode_) + alphaOff * 0.25f);
    }
    else if (injectionMode_ == "layer_split" && std::abs(alphaOff) > 0.001f)
    {
        const float driftBlocksF = static_cast<float>(ditBlocks_);
        const float width = splitLayerEnd_ - splitLayerStart_;
        const float maxStart = std::max(0.0f, driftBlocksF - width);
        // Drift sweeps half the block range per unit of alphaOff (8 blocks
        // for SAO Small's 16-deep DiT, scaled for SA3-sized models).
        const float sweepScale = driftBlocksF * 0.5f;
        effectiveSplitStart = juce::jlimit(0.0f, maxStart, splitLayerStart_ + alphaOff * sweepScale);
        effectiveSplitEnd   = effectiveSplitStart + width;
    }

    // Alpha-driven auto-regen routes to the mode's parameter:
    // Linear → α, Step-in → lateMix, Layer → splitStart (split end follows).
    bool alphaChanged = false;
    if (injectionMode_ == "linear")
    {
        alphaChanged = !std::isnan(effAlpha)
            && (std::isnan(lastGenAlpha_) || std::abs(effAlpha - lastGenAlpha_) > DRIFT_THRESHOLD);
    }
    else if (fineLikeMode)
    {
        alphaChanged = std::isnan(lastGenLateMix_)
            || std::abs(effectiveLateMix - lastGenLateMix_) > DRIFT_THRESHOLD;
        // Suppress regen on initial frame when nothing has moved yet.
        if (std::isnan(lastGenLateMix_) && std::abs(alphaOff) < 0.001f)
            alphaChanged = false;
    }
    else if (injectionMode_ == "layer_split")
    {
        // Detect change on EITHER thumb — drift moves both synchronously, but
        // a manual drag of just the upper thumb changes splitEnd without
        // touching splitStart, so we need both comparisons.
        const bool startChanged = std::isnan(lastGenSplitStart_)
            || std::abs(effectiveSplitStart - lastGenSplitStart_) > DRIFT_THRESHOLD;
        const bool endChanged = std::isnan(lastGenSplitEnd_)
            || std::abs(effectiveSplitEnd - lastGenSplitEnd_) > DRIFT_THRESHOLD;
        alphaChanged = startChanged || endChanged;
        // Suppress regen on the initial frame when nothing has moved yet.
        if (std::isnan(lastGenSplitStart_) && std::isnan(lastGenSplitEnd_)
            && std::abs(alphaOff) < 0.001f)
            alphaChanged = false;
    }

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

    // genNoise/genMag are pre-resolved to their slider values; alpha is
    // intentionally NOT — pass the drift sentinel (NaN when alpha is not being
    // modulated) straight through. buildInferenceRequest resolves NaN→slider
    // identically (see its top), so the rendered alpha is unchanged; preserving
    // the sentinel is what lets its single-prompt guard tell the plain slider
    // default apart from a genuine alpha sweep. Pre-resolving here (the old bug)
    // handed the guard a concrete 0.0 on every non-alpha drift regen — including
    // the promptChanged tick fired the instant a lone prompt is typed — so the
    // guard silently skipped and the linear blend collapsed to null.
    float genNoise = std::isnan(effNoise)
        ? apvts.getRawParameterValue(PID::genNoise)->load() : effNoise;
    float genMag = std::isnan(effMag)
        ? apvts.getRawParameterValue(PID::genMagnitude)->load() : effMag;

    lastRegenTimeMs_ = juce::Time::getMillisecondCounterHiRes();
    triggerDriftRegeneration(effAlpha, effAxes, genNoise, genMag,
                             effectiveLateMix, effectiveSplitStart, effectiveSplitEnd, false);
}
