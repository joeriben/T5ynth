#include "PromptPanel.h"
#include "DimensionExplorer.h"
#include "GuiHelpers.h"
#include "../PluginProcessor.h"
#include "../dsp/BlockParams.h"
#include "../inference/RepromptStances.h"
#include <thread>
#include <cmath>

namespace
{
constexpr float kPromptPadFactor   = 0.04f;
constexpr float kPromptMultiInput  = 3.7f;   // two-line prompt editor (roomy box)
constexpr float kPromptCompactRow  = 1.15f;
constexpr float kPromptCompactCtrl = 0.9f;
constexpr float kPromptSeedCtrl    = 1.75f;
constexpr float kPromptGap         = 0.28f;
// Delineation spacing (clear visual separation between function blocks):
constexpr float kPromptModeBar     = 1.3f;   // mode-bar height (a real control band, not a strip)
constexpr float kPromptInnerGap    = 0.6f;   // breathing room around the mode bar, inside the A↔B block
constexpr float kPromptModelGap    = 0.6f;   // below the model selector
constexpr float kPromptGroupGap    = 1.0f;   // around the divider between the A↔B block and the params
constexpr float kPromptReprompt    = 2.6f;   // Re-Prompt row (stance glyph bar + 3 stacked coupling buttons), under the prompts
// The prompting area is an A↔B block: [A editor / mode-bar / B editor] in a left
// column with a full-height vertical blend slider on the right. The block is
// framed by breathing room and a recessed band behind the mode bar; a divider
// separates it from the generation params below.
// Both ContentUnits MUST equal the unit sum in getPreferredHeightForWidth so that
// resized()'s f = (height-2)/ContentUnits resolves back to the preferred font.
// (abBlock = 2·multiInput + 2·innerGap + modeBar = 7.4 + 1.2 + 1.3 = 9.9 units.)
// The Re-Prompt row + its top gap (innerGap + kPromptReprompt = 0.6 + 2.6 = 3.2)
// sits between the A↔B block and the divider and is in BOTH budgets below.
constexpr float kPromptContentUnits = 25.12f;
// Easy budget keeps the model selector row but drops the advanced param rows.
constexpr float kPromptEasyContentUnits = 20.46f;
constexpr int kBaseSeed = 123456789;

float preferredPromptFontForWidth(int width)
{
    const float innerW = juce::jmax(160.0f, static_cast<float>(width) * (1.0f - 2.0f * kPromptPadFactor));
    return juce::jlimit(12.5f, 17.0f, innerW * 0.05f);
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

// ── Resynth-loop anti-convergence ──────────────────────────────────────────
// A feedback loop that re-feeds its own output as init_audio tends to collapse
// onto a fixed point: each render resembles the last more and more until it
// stops evolving. We detect that by the delta between consecutive loop outputs
// and, when it falls below a threshold, push the effective resynth amount down
// (which raises init_noise — more fresh diffusion = the perturbation that breaks
// the attractor), relaxing back toward the user's setting once it evolves again.
constexpr float kConvergenceDeltaThreshold = 0.04f;  // delta below this = "too convergent" (corr > 0.96)
constexpr float kConvergenceAttackGain     = 1.5f;   // how fast reduction builds while converged
constexpr float kConvergenceReleaseGain    = 0.5f;   // slower release → hysteresis, avoids re-collapse
constexpr float kMaxConvergenceReduction   = 0.8f;   // integral windup clamp
constexpr float kResynthLoopFloor          = 0.05f;  // keep effective resynth > attach gate (0.01)

// Zero-lag cosine similarity between two buffers (mono-summed, compared over
// their shared length), returned as a delta in [0,1]: 0 = identical, 1 =
// unrelated. A new buffer collapsed to silence reads as delta 0 (silence is
// itself a degenerate attractor to escape); a silent/empty reference reads as
// delta 1 (nothing to compare against, so don't engage the controller).
static float computeBufferDelta(const juce::AudioBuffer<float>& reference,
                                const juce::AudioBuffer<float>& fresh)
{
    const int n  = juce::jmin(reference.getNumSamples(), fresh.getNumSamples());
    if (n <= 0) return 1.0f;
    const int cr = reference.getNumChannels();
    const int cf = fresh.getNumChannels();
    double dot = 0.0, eRef = 0.0, eFresh = 0.0;
    for (int i = 0; i < n; ++i)
    {
        double sr = 0.0, sf = 0.0;
        for (int ch = 0; ch < cr; ++ch) sr += reference.getSample(ch, i);
        for (int ch = 0; ch < cf; ++ch) sf += fresh.getSample(ch, i);
        dot += sr * sf; eRef += sr * sr; eFresh += sf * sf;
    }
    constexpr double kEps = 1e-12;
    if (eFresh < kEps) return 0.0f;  // collapsed to silence → force escape
    if (eRef   < kEps) return 1.0f;  // no usable reference
    const double corr = dot / std::sqrt(eRef * eFresh);
    return static_cast<float>(juce::jlimit(0.0, 1.0, 1.0 - corr));
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
    // Impulse A — periwinkle identity. The old "Impulse A" label is gone; the
    // editor's text colour plus an empty-state placeholder now carry the role.
    // Two-line with word-wrap so longer impulses stay visible. With
    // setReturnKeyStartsNewLine(false) Return still triggers generation; the
    // wrap only kicks in when the text itself exceeds one line's width.
    promptAEditor.setMultiLine(true, true);
    promptAEditor.setReturnKeyStartsNewLine(false);
    promptAEditor.setColour(juce::TextEditor::backgroundColourId, kSurface.brighter(0.08f));
    promptAEditor.setColour(juce::TextEditor::textColourId, kImpulseAText);
    promptAEditor.setColour(juce::TextEditor::outlineColourId, kBorder);
    promptAEditor.setColour(juce::TextEditor::focusedOutlineColourId, kImpulseA);
    promptAEditor.setColour(juce::TextEditor::highlightColourId, kImpulseA.withAlpha(0.30f));
    promptAEditor.setTextToShowWhenEmpty("Insert Impulse A here", kImpulseAText.withAlpha(0.45f));
    promptAEditor.onReturnKey = [this] { triggerGeneration(); };
    promptAEditor.onTextChange = [this] {
        // Impulse edits should force the next drift regen to use a fresh snapshot.
        lastGenPromptA_.clear();
    };
    promptAEditor.setBufferedToImage(true);
    addAndMakeVisible(promptAEditor);

    // Impulse B — yellow identity (complementary contrast to A).
    promptBEditor.setMultiLine(true, true);
    promptBEditor.setReturnKeyStartsNewLine(false);
    promptBEditor.setColour(juce::TextEditor::backgroundColourId, kSurface.brighter(0.08f));
    promptBEditor.setColour(juce::TextEditor::textColourId, kImpulseB);
    promptBEditor.setColour(juce::TextEditor::outlineColourId, kBorder);
    promptBEditor.setColour(juce::TextEditor::focusedOutlineColourId, kImpulseB);
    promptBEditor.setColour(juce::TextEditor::highlightColourId, kImpulseB.withAlpha(0.30f));
    promptBEditor.setTextToShowWhenEmpty("Insert Impulse B here", kImpulseB.withAlpha(0.45f));
    promptBEditor.onTextChange = [this] {
        // Impulse edits should force the next drift regen to use a fresh snapshot.
        lastGenPromptB_.clear();
    };
    promptBEditor.setBufferedToImage(true);
    addAndMakeVisible(promptBEditor);

    // A↔B blend — vertical slider with an A→B gradient track and a position-
    // coloured thumb (custom LnF). Replaces the old horizontal alpha slider; the
    // gradient is self-describing, so alphaLabel/alphaValue stay hidden but are
    // still updated by onValueChange (they feed no layout).
    alphaSlider.setSliderStyle(juce::Slider::LinearVertical);
    alphaSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    alphaSlider.setLookAndFeel(&alphaLnF);
    addAndMakeVisible(alphaSlider);
    makeLabel(alphaLabel, "A " + juce::String(juce::CharPointer_UTF8("\xe2\x86\x94")) + " B", kDim, juce::Justification::centredLeft, this);
    makeLabel(alphaValue, "0", kOscCol, juce::Justification::centredRight, this);
    alphaLabel.setVisible(false);
    alphaValue.setVisible(false);
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
        else  // layer_split — TwoValueVertical: read both thumbs
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

    // Union-Jack translate: a MOMENTARY action. Clicking it rewrites the A/B
    // prompts to English in place. Because the single IPC pipe is shared with
    // auto-regen, the click pauses auto-regen for the duration of the translation
    // (freeing the pipe); auto-regen resumes automatically, with its unchanged bar
    // setting, once the translation finishes. The flag pulses while it runs.
    translateToggle.setTooltip("Translate prompts to English in place "
                               "(auto-regen pauses during translation, then resumes)");
    translateToggle.onClick = [this] { translatePromptsInPlace(); };
    addAndMakeVisible(translateToggle);

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
                if (model.isEmpty()) return;

                // Selecting a model has two distinct kinds of consequence, and
                // they must NOT share a guard. The JUCE radio toggle that picked
                // this button has already fired (it is ungated), so the cheap,
                // purely-local consequences — default steps/cfg and the SA3 gate
                // + injection-mode availability — must apply unconditionally too,
                // or the UI desyncs from the selection (e.g. the Semantic Axes /
                // Dimension Explorer cards stay dimmed after an SA3 -> non-SA3
                // switch while drift auto-regen keeps `generating` true).
                // Anything that reaches into the inference backend is deferred
                // while generating (the guard below).
                auto& apvts = processorRef.getValueTreeState();
                const auto defaults = defaultParamsFor(model);
                apvts.getParameter(PID::infSteps)->setValueNotifyingHost(
                    apvts.getParameter(PID::infSteps)->convertTo0to1(defaults.steps));
                apvts.getParameter(PID::genCfg)->setValueNotifyingHost(
                    apvts.getParameter(PID::genCfg)->convertTo0to1(defaults.cfg));
                syncInjectionModeAvailability();

                // The Duration ceiling (SA3 -> 120s, else 11s) is pure UI: it
                // reads the selected-model toggle and re-scopes the slider range
                // without touching the inference backend, so it must apply
                // unconditionally too. Otherwise switching to SA3 while drift
                // auto-regen holds `generating` true leaves the slider stuck at
                // the 11s ceiling. (The DiT-depth re-scope below DOES call
                // getModelMetadata() on the PipeInference mutex and stays
                // deferred under the guard; this duration call is a no-op repeat
                // when refreshDitBlocksForCurrentModel() runs below.)
                applyDurationRangeForCurrentModel();

                // Everything below reaches into the inference backend, so it is
                // deferred while a generation is in flight. refreshDitBlocks-
                // ForCurrentModel() calls getModelMetadata(), which contends on
                // the same PipeInference mutex that generate() holds for the
                // entire blocking IPC round-trip — calling it mid-render would
                // freeze the message thread. The preload likewise drives the IPC
                // pipe. The next generation loads the selected model on demand;
                // the DiT/duration ranges re-scope on the next idle model touch.
                if (generating) return;

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

    // The durA attachment just set the slider to the parameter's full 0.1–120s
    // range; scope it to the short-sound default until a model is selected
    // (populateModelSelector → refreshDitBlocksForCurrentModel re-applies it).
    applyDurationRangeForCurrentModel();

    // ── Re-Prompt controls (semantic self-listening loop) ──
    // Placed under the prompts (laid out in resized()), co-located with the loop
    // logic this panel owns. The stance bar paints the movement-type glyphs and
    // binds itself to repromptStance; the vertical switchbox mirrors the model
    // switchbox pattern (hidden combo + radio buttons) and binds to repromptCoupling.
    // Re-Prompt is engine-agnostic (NOT SA3-gated): the word loop runs on any model.
    repromptLabel.setText("Re-Prompt", juce::dontSendNotification);
    repromptLabel.setColour(juce::Label::textColourId, kDim);
    repromptLabel.setJustificationType(juce::Justification::centredLeft);
    repromptLabel.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(repromptLabel);

    if (auto* stanceParam = apvts.getParameter(PID::repromptStance))
        repromptStanceBar.attachTo(*stanceParam, RepromptStance::kCount);
    repromptStanceBar.setTooltip(
        "Re-Prompt stance: after each render the machine listens to its own output "
        "and rewrites the prompt(s) before the next one. Hover a glyph for its "
        "movement type.");
    repromptStanceBar.setPositionTooltips({
        "Off - Re-Prompt loop disabled.",
        "Transcribe (fixed point): the machine re-describes what it hears; the prompt stays put.",
        "De-Kitsch / sober (inward spiral): re-states the sound plainly and factually - sentiment and scene stripped, the real source kept.",
        "Sweeten (damped settling): softens toward a gentler, cuter reading.",
        "Variation (bounded cluster): small variations around the current theme.",
        "Abduction (wandering): leaps to new scenes, drifting far from the source.",
        "Opposite (limit cycle): oscillates between opposing readings."
    });
    addAndMakeVisible(repromptStanceBar);

    // Coupling switchbox: visible radio buttons + a hidden ComboBox carrying the
    // ChoiceParameter attachment (so the buttons stay a pure view of the param).
    const char* couplingUi[kNumCouplingBtns] = { "B only", "AB add", "AB replace" };
    const char* couplingTip[kNumCouplingBtns] = {
        "B only: one interpret run rewrites prompt B; A stays the human anchor (the "
        "alpha slider sets the A/B mix).",
        "AB add: two runs (A & B); each pole = its own original prompt + its latest "
        "interpretation.",
        "AB replace: two runs (A & B); each pole fully replaced by its stance "
        "interpretation."
    };
    juce::StringArray couplingItems;
    for (const auto& e : RepromptCoupling::kEntries)
        couplingItems.add(juce::String(juce::CharPointer_UTF8(e.label)));
    repromptCouplingHidden.addItemList(couplingItems, 1);
    repromptCouplingHidden.onChange = [this] {
        const int id = repromptCouplingHidden.getSelectedId();
        for (int i = 0; i < kNumCouplingBtns; ++i)
            repromptCouplingBtns[i].setToggleState(i + 1 == id, juce::dontSendNotification);
    };
    addChildComponent(repromptCouplingHidden);   // hidden: only its value/attachment is used
    for (int i = 0; i < kNumCouplingBtns; ++i)
    {
        auto& bb = repromptCouplingBtns[i];
        bb.setButtonText(couplingUi[i]);
        bb.setColour(juce::TextButton::buttonColourId, kSurface);
        bb.setColour(juce::TextButton::buttonOnColourId, kDriftCol);
        bb.setColour(juce::TextButton::textColourOffId, kDim);
        bb.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        bb.setClickingTogglesState(true);
        bb.setRadioGroupId(1006);   // unique repo-wide (1004 = model switchbox, 1005 = old MainPanel coupling)
        bb.setTooltip(couplingTip[i]);
        bb.onClick = [this, i] { repromptCouplingHidden.setSelectedId(i + 1); };
        addAndMakeVisible(bb);
    }
    // The attachment's sendInitialUpdate() fires onChange (sendNotificationSync),
    // syncing the button toggles to the restored value — no manual sync needed.
    repromptCouplingA = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, PID::repromptCoupling, repromptCouplingHidden);

    startTimerHz(10);  // poll for device availability + drift regen + ghost
}

int PromptPanel::getPreferredHeightForWidth(int width) const
{
    const float f = preferredPromptFontForWidth(width);
    const int multiInputH = juce::roundToInt(f * kPromptMultiInput);
    const int gap = juce::roundToInt(f * kPromptGap);
    const int modelGap = juce::roundToInt(f * kPromptModelGap);
    const int innerGap = juce::roundToInt(f * kPromptInnerGap);
    const int groupGap = juce::roundToInt(f * kPromptGroupGap);
    const int modeBarH = juce::roundToInt(f * kPromptModeBar);
    const int compactRowH = juce::roundToInt(f * kPromptCompactRow);
    const int compactCtrlH = juce::roundToInt(f * kPromptCompactCtrl);
    const int seedCtrlH = juce::roundToInt(f * kPromptSeedCtrl);
    const int repromptRowH = juce::roundToInt(f * kPromptReprompt);

    // A↔B block: A editor / mode-bar / B editor with breathing room around the
    // mode bar (the right-hand vertical slider overlays this same height, so it
    // adds nothing to the vertical budget).
    const int abBlockH = multiInputH + innerGap + modeBarH + innerGap + multiInputH;

    if (easyMode_)
    {
        return (compactRowH + 2) + modelGap             // model selector row
             + abBlockH + innerGap + repromptRowH       // A↔B block + Re-Prompt row
             + groupGap                                 // divider
             + compactRowH + seedCtrlH + gap            // Duration / Seed
             + gap + compactRowH;                       // info label
    }

    return (compactRowH + 2) + modelGap                 // model selector row
         + abBlockH + innerGap + repromptRowH           // A↔B block + Re-Prompt row
         + groupGap                                     // divider
         + (compactRowH + compactCtrlH + gap) * 2       // Mag/Noise + Steps/CFG
         + compactRowH + seedCtrlH + gap                // Duration / Seed
         + gap + compactRowH;                           // info label
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

    // Re-Prompt deactivation → restore the human originals. Edge-detected here (not in
    // pollDriftRegen, which returns early once the loop is Off) so it fires the instant
    // the stance is set back to Off, independent of the regen mode. dontSendNotification:
    // we resync the lastGen* trackers + setLastPrompts ourselves, exactly like the loop's
    // own apply tail, so no spurious promptChanged regen is provoked.
    {
        const int curStance = static_cast<int>(processorRef.getValueTreeState()
            .getRawParameterValue(PID::repromptStance)->load());
        if (curStance == RepromptStance::Off
            && prevStanceForRestore_ != RepromptStance::Off
            && loopOriginalsValid_)
        {
            promptAEditor.setText(loopOriginalA_, juce::dontSendNotification);
            promptBEditor.setText(loopOriginalB_, juce::dontSendNotification);
            lastGenPromptA_ = loopOriginalA_;
            lastGenPromptB_ = loopOriginalB_;
            processorRef.setLastPrompts(loopOriginalA_, loopOriginalB_);
            loopOriginalsValid_ = false;
            loopEngaged_ = false;   // re-arm the original-capture edge for the next run
        }
        prevStanceForRestore_ = curStance;
    }

    // Auto-regen polling
    pollDriftRegen();
}

void PromptPanel::paint(juce::Graphics& g)
{
    // Recessed band framing the mode bar (drawn before children, so the mode
    // buttons + flag paint on top): marks the blend-mode selector as a control.
    if (!modeBandBounds.isEmpty())
    {
        g.setColour(kBg);
        g.fillRoundedRectangle(modeBandBounds.toFloat(), 4.0f);
        g.setColour(kBorder);
        g.drawRoundedRectangle(modeBandBounds.toFloat(), 4.0f, 1.0f);
    }

    // Divider separating the A↔B block from the generation params below.
    if (paramsDividerY >= 0)
    {
        const int pad = juce::roundToInt(static_cast<float>(getWidth()) * kPromptPadFactor);
        g.setColour(kBorder);
        g.drawHorizontalLine(paramsDividerY, static_cast<float>(pad),
                             static_cast<float>(getWidth() - pad));
    }

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
        const int thumbW = slider.getLookAndFeel().getSliderThumbRadius(slider) * 2;
        float gx, gy, r;
        if (slider.isVertical())
        {
            // alphaSlider: proportion→pixel matches JUCE's own thumb mapping
            // (top = high proportion; region inset by thumbW/2 each end — see
            // LookAndFeel_V2::getSliderLayout). norm already carries the A-top flip.
            const float trackY = static_cast<float>(sb.getY() + thumbW / 2);
            const float trackH = static_cast<float>(sb.getHeight() - thumbW);
            gx = static_cast<float>(sb.getCentreX());
            gy = trackY + trackH * static_cast<float>(1.0 - norm);
            // thumbW = getSliderThumbRadius*2 ≈ 2× the drawn thumb diameter, so
            // 0.22× lands the ghost just under that diameter.
            r  = static_cast<float>(thumbW) * 0.22f;
        }
        else
        {
            const int trackX = sb.getX() + thumbW / 2;
            const int trackW = sb.getWidth() - thumbW;
            gx = static_cast<float>(trackX) + static_cast<float>(trackW) * static_cast<float>(norm);
            gy = static_cast<float>(sb.getCentreY());
            r  = static_cast<float>(sb.getHeight()) * 0.28f;
        }
        g.setColour(kModCol.withAlpha(0.8f)); // ghost stays in the Mod-section colour
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

    // Tiny A / 0 / B anchor scale at the slider's left edge, aligned to the snap
    // positions (−1 / 0 / +1). A minimal orientation aid replacing the removed
    // numeric readout; in the impulse identity colours (A periwinkle, centre
    // neutral, B gold). Linear mode only — other modes give the slider different
    // semantics. Same value→pixel mapping as the ghost so the marks sit exactly
    // where the thumb detents.
    if (injectionMode_ == "linear")
    {
        const auto sb = alphaSlider.getBounds();
        const int   thumbW  = alphaSlider.getLookAndFeel().getSliderThumbRadius(alphaSlider) * 2;
        const float trackY  = static_cast<float>(sb.getY() + thumbW / 2);
        const float trackH  = static_cast<float>(sb.getHeight() - thumbW);
        const float thumbDia = static_cast<float>(thumbW) * 0.5f;  // actual drawn thumb Ø
        const int   labelW  = juce::jmax(7, juce::roundToInt(
                                  static_cast<float>(sb.getCentreX() - sb.getX()) - thumbDia * 0.5f - 1.0f));
        const float fontH   = juce::jlimit(8.0f, 11.0f, trackH * 0.07f);
        g.setFont(juce::FontOptions(fontH));

        struct Anchor { float v; const char* t; juce::Colour c; };
        const Anchor anchors[] = {
            { -1.0f, "A", kImpulseAText },
            {  0.0f, "0", kTextMuted    },
            {  1.0f, "B", kImpulseB     },
        };
        for (const auto& a : anchors)
        {
            const double norm = juce::jlimit(0.0, 1.0,
                alphaSlider.valueToProportionOfLength(static_cast<double>(a.v)));
            const int y = juce::roundToInt(trackY + trackH * static_cast<float>(1.0 - norm) - fontH * 0.5f);
            g.setColour(a.c);
            g.drawText(juce::String(a.t), sb.getX(), y, labelW, juce::roundToInt(fontH),
                       juce::Justification::centred, false);
        }
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
    int gap = juce::roundToInt(f * kPromptGap);
    int modelGap = juce::roundToInt(f * kPromptModelGap);
    int innerGap = juce::roundToInt(f * kPromptInnerGap);
    int groupGap = juce::roundToInt(f * kPromptGroupGap);
    int modeBarH = juce::roundToInt(f * kPromptModeBar);
    int compactRowH = juce::roundToInt(f * kPromptCompactRow);
    int compactCtrlH = juce::roundToInt(f * kPromptCompactCtrl);
    int seedCtrlH = juce::roundToInt(f * kPromptSeedCtrl);
    int repromptRowH = juce::roundToInt(f * kPromptReprompt);

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
        area.removeFromTop(modelGap);
    }

    magHint.setVisible(false);
    noiseHint.setVisible(false);
    durHint.setVisible(false);
    stepsHint.setVisible(false);
    cfgHint.setVisible(false);

    const int multiInputH = juce::roundToInt(f * kPromptMultiInput);

    // ── A↔B block ──────────────────────────────────────────────────────────
    // Left column stacks [A editor / mode band / B editor] with breathing room
    // around the mode band so it reads as a deliberate control — the operator
    // that turns A into B — not a strip squeezed between the two inputs. The
    // right column is a slim full-height vertical blend slider whose A(top)→
    // B(bottom) gradient makes the relationship self-evident.
    {
        const int blockH = multiInputH + innerGap + modeBarH + innerGap + multiInputH;
        auto block = area.removeFromTop(blockH);

        // Right column: vertical A↔B slider, spanning the full block height so
        // its top edge meets A and its bottom edge meets B. The track+thumb are
        // slim (see AlphaSliderLnF), so a narrow column suffices.
        const int sliderColW = juce::jmax(30, juce::roundToInt(f * 1.9f));
        alphaSlider.setBounds(block.removeFromRight(sliderColW));
        block.removeFromRight(gap);

        // Impulse text is the primary input, so the editors carry a slightly
        // larger font than the surrounding chrome.
        const float editorFont = f * 1.1f;

        // Left column, top: Impulse A editor (purple).
        promptAEditor.setFont(juce::FontOptions(editorFont));
        promptAEditor.setBounds(block.removeFromTop(multiInputH));
        block.removeFromTop(innerGap);

        // Left column, middle: the mode band. paint() fills modeBandBounds as a
        // recessed well; the segmented mode buttons + Union-Jack translate toggle
        // sit inside it with a small inset so they don't touch the band border.
        {
            auto band = block.removeFromTop(modeBarH);
            modeBandBounds = band;
            auto modeRow = band.reduced(juce::jmax(2, juce::roundToInt(f * 0.18f)));

            // Union-Jack translate toggle at the right end (flag aspect ~1.6:1).
            const int enW = juce::jmin(modeRow.getWidth() / 3,
                                       juce::roundToInt(static_cast<float>(modeRow.getHeight()) * 1.6f));
            translateToggle.setBounds(modeRow.removeFromRight(enW));
            modeRow.removeFromRight(juce::jmax(3, gap));

            // Six connected radio buttons fill the remaining width; the last
            // claims the integer-division remainder so the row ends flush.
            int btnW = juce::jmax(1, modeRow.getWidth() / 6);
            injModeLinear.setBounds(modeRow.removeFromLeft(btnW));
            injModeFine  .setBounds(modeRow.removeFromLeft(btnW));
            injModeLayer .setBounds(modeRow.removeFromLeft(btnW));
            injModeKombi1.setBounds(modeRow.removeFromLeft(btnW));
            injModeKombi2.setBounds(modeRow.removeFromLeft(btnW));
            injModeKombi3.setBounds(modeRow);
        }
        block.removeFromTop(innerGap);

        // Left column, bottom: Impulse B editor (yellow).
        promptBEditor.setFont(juce::FontOptions(editorFont));
        promptBEditor.setBounds(block.removeFromTop(multiInputH));
    }

    // ── Re-Prompt row ────────────────────────────────────────────────────────
    // Directly under the prompts, above the params divider:
    //   [label] [stance glyph bar] [vertical 3-way coupling switchbox].
    // The coupling buttons are kept compact (short row → small auto-font from the
    // LookAndFeel, plus tight 1 px inter-button gaps) per the layout request.
    area.removeFromTop(innerGap);
    {
        auto rr = area.removeFromTop(repromptRowH);

        repromptLabel.setFont(juce::FontOptions(juce::jmax(10.0f, f * 0.82f)));
        const int rpLabelW = juce::jlimit(46, 86, juce::roundToInt(rr.getWidth() * 0.26f));
        repromptLabel.setBounds(rr.removeFromLeft(juce::jmin(rpLabelW, rr.getWidth())));
        rr.removeFromLeft(juce::jmin(gap, rr.getWidth()));

        // Coupling switchbox on the right (3 stacked radio buttons).
        const int couplingW = juce::jlimit(58, 96, juce::roundToInt(rr.getWidth() * 0.30f));
        auto couplingCol = rr.removeFromRight(juce::jmin(couplingW, rr.getWidth()));
        rr.removeFromRight(juce::jmin(gap, rr.getWidth()));

        // Stance glyph bar fills the middle.
        repromptStanceBar.setBounds(rr);

        const int segGap = 1;
        const int segH = juce::jmax(9,
            (couplingCol.getHeight() - segGap * (kNumCouplingBtns - 1)) / kNumCouplingBtns);
        for (int i = 0; i < kNumCouplingBtns; ++i)
        {
            repromptCouplingBtns[i].setBounds(couplingCol.removeFromTop(juce::jmin(segH, couplingCol.getHeight())));
            if (i < kNumCouplingBtns - 1)
                couplingCol.removeFromTop(juce::jmin(segGap, couplingCol.getHeight()));
        }
    }

    // Divider between the Re-Prompt row and the generation params: a clear visual
    // break (the params used to butt straight up against the input box).
    area.removeFromTop(groupGap / 2);
    paramsDividerY = area.getY();
    area.removeFromTop(groupGap - groupGap / 2);

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

    // Center the generation-param block in the free space below the divider
    // instead of letting it hang off the prompt block. The info label stays
    // pinned to the bottom. At the panel's preferred (minimum) height the slack
    // is zero, so this lays out identically to before; any extra height is split
    // evenly above and below the param block.
    auto infoArea = area.removeFromBottom(gap + compactRowH);
    {
        const int paramsH = easy
            ? (compactRowH + seedCtrlH + gap)
            : ((compactRowH + compactCtrlH + gap) * 2 + compactRowH + seedCtrlH + gap);
        area.removeFromTop(juce::jmax(0, area.getHeight() - paramsH) / 2);
    }

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

    // Info label pinned at the bottom of the panel
    setFs(infoLabel, f);
    infoLabel.setBounds(infoArea.removeFromBottom(compactRowH));
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
    // Re-Prompt loop runtime is per-session, NOT preset state. Reset the engage
    // gate so (a) the deactivation-restore can't overwrite the just-loaded prompts
    // with the OLD session's captured originals, and (b) the loop re-captures the
    // NEW prompts when it next engages. Stance/coupling themselves are APVTS params
    // the processor restores separately (Off for presets predating Re-Prompt).
    loopEngaged_ = false;
    loopOriginalsValid_ = false;
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

// juce::Slider works in double; the APVTS parameter range is float. Mirror the
// exact float→double bridge SliderParameterAttachment uses (forwarding
// convert/snapToLegalValue through the float range, copying interval/skew) so
// the slider keeps the same skew + whole-second detents the parameter defines.
static juce::NormalisableRange<double> toDoubleRange(juce::NormalisableRange<float> r)
{
    auto from = [r] (double s, double e, double v) mutable
                { r.start = (float) s; r.end = (float) e; return (double) r.convertFrom0to1 ((float) v); };
    auto to   = [r] (double s, double e, double v) mutable
                { r.start = (float) s; r.end = (float) e; return (double) r.convertTo0to1 ((float) v); };
    auto snap = [r] (double s, double e, double v) mutable
                { r.start = (float) s; r.end = (float) e; return (double) r.snapToLegalValue ((float) v); };

    juce::NormalisableRange<double> d { (double) r.start, (double) r.end,
                                        std::move (from), std::move (to), std::move (snap) };
    d.interval      = r.interval;
    d.skew          = r.skew;
    d.symmetricSkew = r.symmetricSkew;
    return d;
}

void PromptPanel::applyDurationRangeForCurrentModel()
{
    auto& apvts = processorRef.getValueTreeState();

    // SA3 generates variable-length, music-scale audio (rotary DiT, ~120s
    // trained); every other engine stays at T5ynth's 11s short-sound ceiling.
    const float maxSec = selectedModelIsSA3() ? 120.0f : 11.0f;

    // Narrow/widen the slider's range. The APVTS parameter already spans the
    // global 120s max, so this is safe against the SliderParameterAttachment
    // range-ownership rule documented in applyModeToSlider — we only ever shrink
    // the slider inside the parameter's range, never beyond it. setNormalisableRange
    // re-clamps the thumb with dontSendNotification (juce_Slider.cpp updateRange),
    // so it never writes back to the parameter on its own.
    durationSlider.setNormalisableRange(toDoubleRange(T5ynthProcessor::makeDurationRange(maxSec)));

    // Model selection is UI state from the backend handshake, not an APVTS
    // parameter — at editor-construction / DAW state-restore time no model is
    // known yet and maxSec defaults to 11. Clamping the parameter here would
    // permanently downgrade a state-restored SA3 duration (e.g. 80s → 11s)
    // before SA3 is ever selected. So only re-clamp once the model is known;
    // the value survives in the parameter and the thumb resyncs when
    // populateModelSelector selects SA3 and calls us again with maxSec = 120.
    if (getSelectedModel().isEmpty())
        return;

    // Re-clamp into the new ceiling. Read the *parameter* (source of truth), not
    // the slider: on preset restore the slider may still be clamped under the
    // previous, narrower range, and using its stale value would overwrite a
    // freshly-restored long duration. sendNotificationSync re-syncs the thumb,
    // refreshes the "Ns" readout, and writes the clamped value back via durA.
    if (auto* p = apvts.getParameter(PID::genDuration))
    {
        const float cur     = p->convertFrom0to1(p->getValue());
        const float clamped = juce::jlimit(0.1f, maxSec, cur);
        durationSlider.setValue(clamped, juce::sendNotificationSync);
    }
}

void PromptPanel::refreshDitBlocksForCurrentModel()
{
    // The active model also governs the Duration ceiling (SA3 → 120s, else
    // 11s). Kept in lock-step with the DiT/layer re-scope below so every model
    // change re-scopes both sliders from a single call site.
    applyDurationRangeForCurrentModel();

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
        alphaSlider.setSliderStyle(juce::Slider::LinearVertical);
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
        alphaSlider.setSliderStyle(juce::Slider::LinearVertical);
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
        alphaSlider.setSliderStyle(juce::Slider::TwoValueVertical);
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
    float lateMixOverride, float splitStartOverride, float splitEndOverride,
    float resynthOverride)
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

    // Resynth (init_audio / i2i): a single Off->Full amount. 0 (Off) = text-only;
    // turning up feeds the LAST raw generation back as the denoise seed so the next
    // render evolves from it instead of from pure noise. SA3-gated to match the UI
    // element (and so a stale non-zero recalled under SAO/AudioLDM2 — whose
    // diffusers path can't take init_audio — never leaks a buffer). Only attaches
    // when a prior buffer actually exists; the very first generation of a session
    // has none and stays text-only. Read on the message thread, same as
    // loadGeneratedAudio writes it.
    // Drift can modulate resynth: when it does, pollDriftRegen passes the effective
    // (base+offset) value as resynthOverride so the loop denoises from the drifted
    // amount rather than the static slider. NaN override → plain slider read.
    const float resynthAmount = juce::jlimit(0.0f, 1.0f,
        std::isnan(resynthOverride) ? apvts.getRawParameterValue(PID::resynthAmount)->load()
                                    : resynthOverride);
    if (resynthAmount > 0.01f && isSA3Model(req.model))
    {
        const auto& rawBuf = processorRef.getGeneratedAudioRaw();
        if (rawBuf.getNumSamples() > 0 && rawBuf.getNumChannels() > 0)
        {
            req.initAudio.makeCopyOf(rawBuf);
            req.initAudioSampleRate = processorRef.getGeneratedSampleRate();
            // Amount (0->1) maps to backend init_noise_level across SA3's MEASURED
            // useful band: Full (1.0) -> 0.05, low end -> ~0.48 (just under the
            // >=0.5 dead zone where init_audio is ignored entirely).
            //
            // VALIDATED empirically by the 20-iteration feedback-loop sweep
            // (tools/test_resynth_loop.py; full writeup in
            // tools/RESYNTH_CALIBRATION_FINDINGS.md). On
            // stable-audio-3-small-music the loop CONVERGES at every sigma:
            //   - Full (sigma 0.05): max self-resynthesis. The output morphs into
            //     a related-but-distinct member of the prompt family and settles
            //     (timbre_corr-to-original 0.37). Coherent, the strongest evolve.
            //   - 5% floor (sigma ~0.48): a CHANGED prompt washes the carried sound
            //     out by ~iter 6 (<<20) — this is what fixed the old "5% holds for
            //     x bars" complaint (the prior 0.30 cap never washed out in 20).
            // The evolution response saturates below sigma ~0.12 and vanishes above
            // sigma ~0.40, so SA3 offers only ~3 resolvable evolution levels; LINEAR
            // spends them on the top three detents (where it matters) and leaves the
            // bottom two as a harmless wash-out plateau. A curve only moves that
            // redundancy onto the useful end — strictly worse. Hence: keep linear.
            // Another engine would want its own band re-measured the same way.
            req.initNoiseLevel = 0.50f - 0.45f * resynthAmount;

            // ── Semantic-loop word-dominance override ──
            // When a loop stance is active the rewritten PROMPT must drive the next
            // render, not the carried-over wave. The Resynth slider's measured band
            // tops out at init_noise 0.05 (Full) and never exceeds 0.50 — and at
            // <=0.5 the init_audio carry DROWNS the rewritten prompt, so every loop
            // iteration re-renders the same carry centroid (the fixed point the tool
            // already had to fix). clap_llm_loop.py runs at init_noise 0.9
            // (validated: commit 9feb21ef / tools/diag_promptbite.py) so the words
            // win while the signal still carries. Override the slider-derived value
            // to that band whenever a stance is engaged — this needs no extra slider
            // (it rides on the existing Resynth attach gate above). Read on the
            // message thread, same as every other param here.
            const int repromptStance = static_cast<int>(
                apvts.getRawParameterValue(PID::repromptStance)->load());
            if (repromptStance != RepromptStance::Off)
                req.initNoiseLevel = 0.9f;
        }
    }
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

// ──────────────────────────────────────────────────────────────────────────────
// Manual prompt translation (Union-Jack flag)
// ──────────────────────────────────────────────────────────────────────────────
// Clicking the flag rewrites the A/B prompts to English IN PLACE. The single IPC
// pipe is shared with auto-regen, so the click DISABLES auto-regen for the whole
// translation by raising translatingPrompts_ (which gates both pollDriftRegen and
// triggerGeneration). The translation runs on a background thread that serialises
// behind any in-flight generation via PipeInference's recursive mutex; when it
// completes, lowering the flag REACTIVATES auto-regen — automatically with its
// unchanged bar setting, and only if it was on. The flag pulses while the
// translation actually runs.
void PromptPanel::translatePromptsInPlace()
{
    if (translatingPrompts_) return;  // already translating

    if (!processorRef.isPipeInferenceReady())
    {
        if (onStatusChanged) onStatusChanged("Backend not connected", false);
        return;
    }

    const auto srcA = promptAEditor.getText();
    const auto srcB = promptBEditor.getText();
    if (srcA.trim().isEmpty() && srcB.trim().isEmpty())
        return;  // nothing to translate

    // Suppress auto-regen for the whole translation via translatingPrompts_ (the
    // pollDriftRegen + triggerGeneration guards). This frees the shared IPC pipe
    // so the background translate can run, and auto-regen resumes automatically —
    // with its unchanged bar setting — the instant this flag clears. Deliberately
    // NOT done by writing PID::driftRegen: that param lives on the processor and
    // would be left stuck OFF (and could be saved into a preset) if the editor
    // were closed mid-translation, since the async restore would be skipped.
    translatingPrompts_ = true;   // blocks pollDriftRegen + manual gen until done
    translateToggle.setPulsing(true);
    if (onStatusChanged) onStatusChanged("translating...", true);

    auto pipePtr = processorRef.getPipeInferencePtr();
    const auto device = defaultInferenceDevice_;
    juce::Component::SafePointer<PromptPanel> safeThis(this);
    std::thread([safeThis, pipePtr, srcA, srcB, device]() mutable
    {
        juce::String errMsg;
        auto translateOne = [&](const juce::String& s) -> juce::String
        {
            if (s.trim().isEmpty() || pipePtr == nullptr) return s;
            // Keep trailing pitch/tempo tokens (c3, 120bpm) verbatim: translate only
            // the descriptive core, then re-append the suffix.
            const juce::String suffix = RepromptStances::trailingMusicSuffix(s);
            const juce::String core   = RepromptStances::stripMusicSuffix(s);
            if (core.isEmpty()) return s;  // nothing but tokens → leave verbatim
            auto tr = pipePtr->translate(core, device, {});  // blocks behind any in-flight generate()
            if (tr.success && tr.text.isNotEmpty())
                return suffix.isEmpty() ? tr.text
                                        : RepromptStances::reattachMusicSuffix(tr.text, suffix);
            if (!tr.success && errMsg.isEmpty()) errMsg = tr.errorMessage;
            return s;  // failure → keep the original text
        };
        const juce::String enA = translateOne(srcA);
        const juce::String enB = translateOne(srcB);

        juce::MessageManager::callAsync([safeThis, enA, enB, srcA, srcB, errMsg]
        {
            if (auto* self = safeThis.getComponent())
            {
                if (enA != srcA) self->promptAEditor.setText(enA, juce::sendNotification);
                if (enB != srcB) self->promptBEditor.setText(enB, juce::sendNotification);
                self->translateToggle.setPulsing(false);
                self->translatingPrompts_ = false;  // auto-regen resumes here (param untouched)
                if (self->onStatusChanged)
                    self->onStatusChanged(errMsg.isNotEmpty() ? errMsg : juce::String("translated"), false);
            }
        });
    }).detach();
}

void PromptPanel::triggerGeneration()
{
    // loopStepInFlight_ too: a semantic-loop step's analyze+interpret holds the
    // single IPC pipe on a background thread (with `generating` already false), so a
    // manual Generate must wait or it would contend on the pipe.
    if (generating || translatingPrompts_ || loopStepInFlight_) return;

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
    std::thread([safeThis, pipePtr, req, deviceForLabel, modelForLabel]() mutable
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

                    // Semantic loop: listen to this render and rewrite the prompt(s)
                    // for the next generation (no-op unless a stance is active + SA3).
                    self->runSemanticLoopStep(result);
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
                                            float effectiveResynth,
                                            bool /*holdForBar*/)
{
    if (generating || translatingPrompts_ || loopStepInFlight_) return;

    lastGenAlpha_ = effectiveAlpha;
    lastGenNoise_ = effectiveNoise;
    lastGenMagnitude_ = effectiveMagnitude;
    lastGenAxes_ = effectiveAxes;
    lastGenLateMix_ = effectiveLateMix;
    lastGenSplitStart_ = effectiveSplitStart;
    lastGenSplitEnd_ = effectiveSplitEnd;

    // Cache replay short-circuits real generation — but the Re-Prompt loop must render
    // fresh audio each step (it listens to the output to rewrite the prompt). So when a
    // stance is active, the auto-regen step bypasses the cache and generates for real.
    // (Manual Generate keeps replaying the cache — only the self-running loop needs to
    // override it.)
    const bool stanceActiveForCache = static_cast<int>(processorRef.getValueTreeState()
        .getRawParameterValue(PID::repromptStance)->load()) != RepromptStance::Off;
    if (processorRef.isInferenceCacheFull() && !stanceActiveForCache)
    {
        playNextCachedInference();
        return;
    }

    if (!processorRef.isPipeInferenceReady()) return;

    generating = true;
    generateButton.setEnabled(false);
    if (onStatusChanged) onStatusChanged("auto regen...", true);

    auto req = buildInferenceRequest(effectiveAlpha, effectiveAxes, effectiveNoise, effectiveMagnitude,
                                     effectiveLateMix, effectiveSplitStart, effectiveSplitEnd,
                                     effectiveResynth);
    auto deviceForLabel = req.device.isEmpty()
        ? processorRef.getPipeInference().getDefaultDevice() : req.device;
    auto modelForLabel = req.model.isEmpty()
        ? processorRef.getPipeInference().getDefaultModel() : req.model;
    auto pipePtr = processorRef.getPipeInferencePtr();
    juce::Component::SafePointer<PromptPanel> safeThis(this);
    std::thread([safeThis, pipePtr, req, deviceForLabel, modelForLabel]() mutable
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

                    // The previous loop output (output N-1) lives in the processor
                    // until loadGeneratedAudio overwrites it below; capture it once
                    // for both the anti-convergence delta and the boundary
                    // crossfade. (Valid only until that load — used before it.)
                    const auto& oldRaw = processor.getGeneratedAudioRaw();

                    // Anti-convergence: on a resynth-loop round (init_audio was
                    // attached) measure how much this render differs from the
                    // previous one — using the PRISTINE result, before the
                    // crossfade blends their boundary and inflates similarity —
                    // and steer convergenceReduction_ so a stalling loop gets more
                    // init_noise back. loopDelta < 0 marks a non-loop round (no
                    // readout, no controller change).
                    float loopDelta = -1.0f;
                    if (req.initAudio.getNumSamples() > 0 && oldRaw.getNumSamples() > 0)
                    {
                        loopDelta = computeBufferDelta(oldRaw, result.audio);
                        const float err  = kConvergenceDeltaThreshold - loopDelta;
                        const float gain = (err > 0.0f) ? kConvergenceAttackGain
                                                        : kConvergenceReleaseGain;
                        self->convergenceReduction_ = juce::jlimit(0.0f, kMaxConvergenceReduction,
                            self->convergenceReduction_ + gain * err);
                    }

                    float xfadeMs = processor.getValueTreeState()
                        .getRawParameterValue(PID::driftCrossfade)->load();
                    int xfadeSamples = juce::roundToInt(xfadeMs * 0.001f * static_cast<float>(result.sampleRate));
                    if (xfadeSamples > 0 && oldRaw.getNumSamples() > 0)
                        applyDriftCrossfade(newAudio, oldRaw, xfadeSamples);
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
                    if (loopDelta >= 0.0f)
                    {
                        // Show the consecutive-output delta (Δ) so the loop's
                        // evolution is visible, and flag +noise only when this
                        // round's amount was ACTUALLY reduced (loopResynth <
                        // effResynth). Near the floor the integral can be positive
                        // yet have no effect — gating on the raw reduction would
                        // flash +noise where nothing is added.
                        info += juce::String::fromUTF8(" | \xCE\x94") + juce::String(loopDelta, 2);
                        if (self->antiConvergenceActive_)
                            info += " +noise";
                    }
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

                    // Semantic loop: listen to this Auto-Regen render and rewrite the
                    // prompt(s) for the next tick (no-op unless a stance is active +
                    // SA3). Uses the PRISTINE result.audio (what CLAP should hear),
                    // not the boundary-crossfaded newAudio that was loaded for playback.
                    self->runSemanticLoopStep(result);
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
    // loopStepInFlight_ too: while a semantic-loop step is analyzing+interpreting on
    // a background thread (`generating` is already false), Auto-Regen must not fire a
    // new generate() — both share the single IPC pipe. The step clears the flag on
    // its callAsync tail; the next tick then proceeds with the rewritten prompts.
    if (generating || translatingPrompts_ || loopStepInFlight_) return;

    // An active Re-Prompt stance is itself a self-running loop driver (faithful to the
    // clap_llm_loop reference, where running the loop IS the generation): it engages
    // auto-regen even in Manual mode. Without this, selecting a stance does nothing
    // until the user separately enables Auto-Regen — exactly the "stance set but
    // autogenerate never triggers Re-Prompt" report. The repromptLoop standing trigger
    // below carries each step; setting the stance back to Off stops it and restores the
    // originals (timerCallback). Read once here; reused for the trigger + cache bypass.
    const bool stanceActive = static_cast<int>(processorRef.getValueTreeState()
        .getRawParameterValue(PID::repromptStance)->load()) != RepromptStance::Off;

    int regenMode = processorRef.driftRegenMode.load(std::memory_order_relaxed);
    if (regenMode == 0 && !stanceActive)
    {
        convergenceReduction_ = 0.0f;   // loop not running → reset the controller
        antiConvergenceActive_ = false; // and its status flag
        return;                         // Manual — no auto-regen (and no stance loop)
    }

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
        && processorRef.audioIdle.load(std::memory_order_relaxed)
        && !stanceActive)   // Re-Prompt must keep rendering fresh audio to listen to
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
    // Effective Resynth amount: the drifted (base+offset) value when a Drift slot
    // sweeps it, otherwise the live slider. Drives the standing feedback-loop trigger
    // below AND is passed through as the init_audio amount to buildInferenceRequest.
    float driftResynthVal = mv.driftResynth.load(std::memory_order_relaxed);
    float effResynth = std::isnan(driftResynthVal)
        ? processorRef.getValueTreeState().getRawParameterValue(PID::resynthAmount)->load()
        : driftResynthVal;

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

    // Resynth feedback loop — a STANDING trigger, not change-detection: while Resynth
    // is active (amount > 0) and a non-Manual regen mode is engaged, keep regenerating
    // at the selected cadence (auto / 1-bar / 4-bar, throttled by the beat cooldown
    // above and the `generating` guard at the top). Each cycle re-feeds the last
    // output as init_audio (buildInferenceRequest reads getGeneratedAudioRaw), so the
    // sound evolves on its own with no slider movement — same shape as random-seed
    // regen. SA3-gated to match buildInferenceRequest's attach gate: init_audio only
    // attaches on SA3, so looping elsewhere would spin identical text-only renders.
    const bool resynthLoop = selectedModelIsSA3() && effResynth > 0.01f;
    if (!resynthLoop)
    {
        convergenceReduction_ = 0.0f;     // controller only meaningful while the loop runs
        prevLoopParamsChanged_ = false;   // re-arm the release edge-detector for next time
    }

    // Re-Prompt word loop — a STANDING trigger too: when a stance is engaged but the
    // SA3 init_audio carry is NOT already clocking the loop (resynthLoop covers SA3 +
    // Resynth>0), provide the trigger ourselves so Auto-Regen keeps re-rendering the
    // rewritten prompts. Covers non-SA3 entirely AND SA3-with-Resynth-off. Needed
    // because runSemanticLoopStep deliberately suppresses promptChanged (to protect
    // the SA3 release edge), so without a standing trigger the loop would advance only
    // once. The SA3-only resynth control machinery below stays gated on resynthLoop —
    // a pure word loop renders clean from the prompt (no init_audio attaches off-SA3
    // or at amount 0, per buildInferenceRequest's gate), so loopResynth stays inert.
    const bool repromptLoop = stanceActive && ! resynthLoop;   // stanceActive: see top

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
    if (!alphaChanged && !axesChanged && !noiseChanged && !magChanged && !promptChanged
        && !resynthLoop && !repromptLoop && !randomRegen)
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

    // Resynth-loop control — two regimes, gated on whether a t5osc PARAMETER moved
    // (NOT on the WAV-delta alone: a low delta is ambiguous — a genuine freeze OR a
    // clean settle on new content — so the parameter-change flag is the
    // discriminator):
    //
    //   • a parameter change EDGE → RELEASE: detach init (loopResynth → 0, below the
    //     attach gate) so the new content renders clean FROM THE PROMPT instead of
    //     being anchored to the carried-over wave. The re-lock is automatic: the
    //     next tick falls through to the anti-convergence branch and init re-attaches
    //     at the set level — now AGREEING with the new content, so it holds.
    //   • otherwise → LOCK / autonomous: the anti-convergence controller. A falling
    //     consecutive-output delta then genuinely means a freeze, so push resynth
    //     down (more init_noise) to break it; relax back as it evolves.
    //
    // EDGE-triggered, not level: paramsChanged is "moved since the last loop regen",
    // which a CONTINUOUS drift (sine/triangle — the default waveforms) holds true
    // every tick. Releasing on the level would detach every tick and silently
    // disable the feedback loop (its headline "evolve with drift" use). Firing only
    // on the false→true edge makes a square/discrete change release once per flip,
    // and a continuous drift release once at onset and then lock-and-evolve.
    //
    // Empirically validated for the discrete switch (tools/test_ab_resynth.py): with
    // init attached at ANY sigma a switched-in prompt only reaches a half-bright mush
    // attractor (cen×B ≈ 0.06–0.44); detach-then-relock lands it clean (cen×B 1.0 on
    // the switch) and HOLDS it (≥1.0) at every Resynth level — tighter the higher the
    // level, since the carried wave then matches the prompt. The delta block in the
    // gen-complete callback is already init-gated, so a release round (no init) skips
    // the anti-convergence integral on its own — no change needed there.
    const bool paramsChanged = alphaChanged || axesChanged || noiseChanged
                            || magChanged || promptChanged;
    const bool releaseEdge = resynthLoop && paramsChanged && !prevLoopParamsChanged_;
    float loopResynth = effResynth;
    if (releaseEdge)
    {
        loopResynth = 0.0f;            // RELEASE: detach this round → render clean
        convergenceReduction_ = 0.0f; // clear any stale reduction before the re-lock
    }
    else if (resynthLoop)
    {
        // Anti-windup: cap the integral at what can actually move loopResynth
        // (effResynth down to the floor). Beyond that it is dead reduction that
        // only lags the relax-back once the loop starts evolving again.
        const float reach = juce::jmax(0.0f, effResynth - kResynthLoopFloor);
        convergenceReduction_ = juce::jmin(convergenceReduction_,
                                           juce::jmin(kMaxConvergenceReduction, reach));
        loopResynth = juce::jlimit(kResynthLoopFloor, 1.0f, effResynth - convergenceReduction_);
    }
    if (resynthLoop)
        prevLoopParamsChanged_ = paramsChanged;  // arm the edge test for the next regen
    // "+noise" status flag: only when the autonomous controller actually lowered the
    // amount — never on a release round (that is a detach, not a noise bump).
    antiConvergenceActive_ = !releaseEdge && resynthLoop
                          && (loopResynth < effResynth - 0.001f);

    lastRegenTimeMs_ = juce::Time::getMillisecondCounterHiRes();
    triggerDriftRegeneration(effAlpha, effAxes, genNoise, genMag,
                             effectiveLateMix, effectiveSplitStart, effectiveSplitEnd,
                             loopResynth, false);
}

// ──────────────────────────────────────────────────────────────────────────────
// Semantic self-listening loop step (CLAP ear → LLM interpreter → next prompt)
// ──────────────────────────────────────────────────────────────────────────────
// One iteration body of clap_llm_loop.py run_llm_loop, minus its own generate():
// here generation is owned by triggerGeneration / triggerDriftRegeneration, so this
// step runs the analyze+interpret+apply for the JUST-rendered audio and writes the
// next prompt(s) into the editor(s). The next generation renders them — manual
// Generate, or the Auto-Regen standing trigger (resynthLoop on SA3 + Resynth,
// repromptLoop otherwise). On SA3 with Resynth the init_audio carry is forced to
// init_noise ~0.9 so the rewritten words dominate it; off-SA3 (or Resynth off) it is
// a pure word loop that re-renders fresh from the prompt. From BOTH gen-complete cbs.
void PromptPanel::runSemanticLoopStep(const PipeInference::Result& result)
{
    // (1) Off early-out FIRST (cheap), like pollDriftRegen's regenMode==0.
    auto& apvts = processorRef.getValueTreeState();
    const int stanceIdx = static_cast<int>(apvts.getRawParameterValue(PID::repromptStance)->load());
    if (stanceIdx == RepromptStance::Off)
    {
        loopEngaged_ = false;   // loop disabled → re-arm the original-capture edge
        // INVARIANT: do NOT touch loopOriginalsValid_ / prevStanceForRestore_ here —
        // they are timer-owned and must survive this clear so the deactivation-restore
        // still fires on the next tick even if a gen-complete callback observes Off
        // before timerCallback does. Writing them here would let machine text leak into
        // the next engage capture.
        return;
    }
    // NO model gate: Re-Prompt is engine-agnostic — it listens to the just-rendered
    // output (any model) and rewrites the prompt(s) for the next render. Only the
    // OPTIONAL init_audio signal carry (Resynth) is SA3-only, layered on top by
    // buildInferenceRequest when present; non-SA3 simply runs the pure word loop
    // (fresh render from the rewritten prompt each step).
    // (2) Re-entrancy guard: a fast Auto-Regen cadence must not stack steps, and a
    //     manual Generate mid-step must not contend on the single IPC pipe.
    if (loopStepInFlight_ || generating || translatingPrompts_)
        return;
    if (result.audio.getNumSamples() <= 0 || result.audio.getNumChannels() <= 0)
        return;   // nothing to listen to

    const int couplingIdx = static_cast<int>(apvts.getRawParameterValue(PID::repromptCoupling)->load());
    const bool dual = (couplingIdx == RepromptCoupling::AbAdd || couplingIdx == RepromptCoupling::AbReplace);
    const bool concat = (couplingIdx == RepromptCoupling::AbAdd);

    // Resolve the stance KEY (BlockParams.h RepromptStance::kEntries[i].key == the
    // clap_llm_loop MODES id RepromptStances expects). Guard the index defensively.
    const int sIdx = juce::jlimit(0, RepromptStance::kCount - 1, stanceIdx);
    const juce::String stanceKey = RepromptStance::kEntries[sIdx].key;

    // transcribe collapses A==B under AB-replace (it reads only the shared machine tags),
    // which leaves alpha/alpha-drift nothing to interpolate. In that one case, write a
    // single pole per step (alternating) so A and B stay distinct consecutive snapshots.
    // Other stances read per-pole memory and stay distinct; AB-add keeps the originals.
    const bool altReplace = (stanceKey == "transcribe")
                         && (couplingIdx == RepromptCoupling::AbReplace);

    // (5 + run_llm_loop glieder init) Capture the human originals on the false→true
    // engage edge: a_glieder=[header_a], b_glieder=[header_b]. The "header" is the
    // text the user has in the editors when the loop first engages; subsequent links
    // are machine-written. Seed last-link + recent from the original (recent =
    // glieder[-3:], which on iter 1 is just [original]).
    if (!loopEngaged_)
    {
        loopOriginalA_ = promptAEditor.getText().trim();   // FULL (with any musical suffix) — the restore puts this back verbatim
        loopOriginalB_ = promptBEditor.getText().trim();
        // Hold the user's trailing pitch/tempo tokens (c3, 120bpm) aside, run the
        // whole chain on the descriptive CORE, re-append on every editor write — so
        // no stance ever paraphrases or drops them. Empty when none were appended.
        loopSuffixA_ = RepromptStances::trailingMusicSuffix(loopOriginalA_);
        loopSuffixB_ = RepromptStances::trailingMusicSuffix(loopOriginalB_);
        const juce::String coreA0 = RepromptStances::stripMusicSuffix(loopOriginalA_);
        const juce::String coreB0 = RepromptStances::stripMusicSuffix(loopOriginalB_);
        loopLastA_ = coreA0;
        loopLastB_ = coreB0;
        loopRecentA_.clearQuick(); loopRecentA_.add(coreA0);
        loopRecentB_.clearQuick(); loopRecentB_.add(coreB0);
        loopEngaged_ = true;
        loopOriginalsValid_ = true;   // arm the stance→Off restore (timerCallback)
        loopAltWriteA_ = false;       // each new session starts by writing B (the primary pole)
    }

    const bool altWriteA = altReplace && loopAltWriteA_;   // which pole this alt step writes

    loopStepInFlight_ = true;

    // Snapshot the per-pole interpret inputs for the background thread (the chain's
    // OWN last link + that pole's last-3 memory — NOT the applied/concat editor text).
    const juce::String prevA = loopLastA_, prevB = loopLastB_;
    juce::StringArray recentA = loopRecentA_, recentB = loopRecentB_;
    // CORE bases for concat2 (the musical suffix is re-appended at the editor write,
    // so it stays trailing instead of landing mid-string inside the concat).
    const juce::String origA = RepromptStances::stripMusicSuffix(loopOriginalA_);
    const juce::String origB = RepromptStances::stripMusicSuffix(loopOriginalB_);

    auto pipePtr = processorRef.getPipeInferencePtr();
    const juce::String device = defaultInferenceDevice_;
    // The just-rendered audio (CLAP listens to THIS, the source of the next prompt).
    juce::AudioBuffer<float> audioForClap;
    audioForClap.makeCopyOf(result.audio);
    const double sr = result.sampleRate;

    juce::Component::SafePointer<PromptPanel> safeThis(this);
    std::thread([safeThis, pipePtr, device, audioCopy = std::move(audioForClap), sr,
                 stanceKey, dual, concat, altReplace, altWriteA,
                 prevA, prevB, recentA, recentB, origA, origB]() mutable
    {
        if (pipePtr == nullptr) {
            juce::MessageManager::callAsync([safeThis] {
                if (auto* s = safeThis.getComponent()) s->loopStepInFlight_ = false; });
            return;
        }

        // CLAP ear: top-5 timbre tags + DSP spectral words (topk=5, default device).
        // Serialises behind any in-flight generate() via PipeInference's recursive
        // mutex (the loopStepInFlight_/generating guards cover the whole span).
        const auto an = pipePtr->analyze(audioCopy, sr, 5, {});
        const juce::String tags = an.success ? an.tags : juce::String();
        const juce::String spectral = an.success ? an.spectral : juce::String();

        // One interpret PER DRIVEN POLE — never copy one run into both poles.
        // next = cleanPrompt(interpret(stanceSysp, userTurn)) OR the chain's last
        // link on empty (run_llm_loop's `or *_glieder[-1]`).
        const juce::String sysp = RepromptStances::stanceSystemPrompt(stanceKey);
        auto interpretPole = [&](const juce::String& prev, const juce::StringArray& recent,
                                 const juce::String& fallback) -> juce::String
        {
            const juce::String userTurn =
                RepromptStances::buildStanceUserTurn(stanceKey, tags, prev, recent, spectral);
            auto r = pipePtr->interpret(sysp, userTurn, 64, device);
            const juce::String cleaned =
                r.success ? RepromptStances::cleanPrompt(r.text) : juce::String();
            return cleaned.isNotEmpty() ? cleaned : fallback;
        };

        // alpha → B only (build_a is None); dual (concat/voll) → A and B, each with
        // its OWN prev/recent but the SAME stance. nextB always holds this step's
        // result (B's inputs); for altReplace it is routed to whichever pole this step
        // targets (transcribe is pole-independent), so the second run is skipped.
        const juce::String nextB = interpretPole(prevB, recentB, prevB);
        juce::String nextA;
        if (dual && !altReplace)
            nextA = interpretPole(prevA, recentA, prevA);

        juce::MessageManager::callAsync(
            [safeThis, dual, concat, altReplace, altWriteA, nextA, nextB, origA, origB]
        {
            auto* self = safeThis.getComponent();
            if (self == nullptr) return;   // panel gone — nothing to write; guard auto-clears with the object

            // Bail (without applying) if the loop was deactivated while this step was
            // interpreting — do NOT clobber the human originals the deactivation-restore
            // (timerCallback) put back with this now-stale rewrite. Two signals:
            //   • stance reads Off — the plain deactivate, tail running before the restore.
            //   • !loopEngaged_ — a restore already ran (it clears loopEngaged_, and a
            //     re-activation does NOT set it back; only the next capture does). This
            //     covers the Off→on ABA flicker that a bare stance==Off check would miss,
            //     which would otherwise apply the stale rewrite AND poison the next
            //     capture's "originals" with machine text.
            if (! self->loopEngaged_
                || static_cast<int>(self->processorRef.getValueTreeState()
                       .getRawParameterValue(PID::repromptStance)->load()) == RepromptStance::Off)
            {
                self->loopStepInFlight_ = false;
                return;
            }

            // transcribe + AB-replace: write ONE pole this step (alternating), leaving
            // the other untouched, so A and B stay distinct snapshots of consecutive
            // renders — an A/B axis for alpha-drift instead of an instant A==B collapse.
            // nextB carries the transcription (transcribe is pole-independent).
            if (altReplace)
            {
                // loopLast_/recent_ keep the CORE (nextB); only the editor gets the
                // pole's preserved pitch/tempo suffix re-appended.
                if (altWriteA)
                {
                    self->loopLastA_ = nextB;
                    self->promptAEditor.setText(RepromptStances::reattachMusicSuffix(nextB, self->loopSuffixA_),
                                                juce::dontSendNotification);
                    self->loopRecentA_.add(nextB);
                    while (self->loopRecentA_.size() > 3) self->loopRecentA_.remove(0);
                }
                else
                {
                    self->loopLastB_ = nextB;
                    self->promptBEditor.setText(RepromptStances::reattachMusicSuffix(nextB, self->loopSuffixB_),
                                                juce::dontSendNotification);
                    self->loopRecentB_.add(nextB);
                    while (self->loopRecentB_.size() > 3) self->loopRecentB_.remove(0);
                }
                self->loopAltWriteA_ = ! altWriteA;   // alternate the target for next step
                self->lastGenPromptA_ = self->promptAEditor.getText().trim();
                self->lastGenPromptB_ = self->promptBEditor.getText().trim();
                self->processorRef.setLastPrompts(self->lastGenPromptA_, self->lastGenPromptB_);
                self->loopStepInFlight_ = false;
                return;
            }

            // Apply the coupling onto the editor(s). dontSendNotification so neither
            // onTextChange nor pollDriftRegen's promptChanged path re-fires (we update
            // the lastGen* trackers + setLastPrompts ourselves below).
            // B pole (always driven). loopLast_/recent_ keep the CORE; the editor
            // (and the lastGen* trackers below) carry the re-appended musical suffix.
            self->loopLastB_ = nextB;                                   // glieder[-1] (CORE)
            // concat2 only when there's a real core to prepend — a pole that was nothing
            // but a musical token has an EMPTY core (origB==""), and concat2("",x) would
            // emit a leading ", "; fall back to the bare new link so the suffix re-append
            // yields "x, 120bpm" not ", x, 120bpm".
            const juce::String appliedB = RepromptStances::reattachMusicSuffix(
                (concat && origB.isNotEmpty()) ? RepromptStances::concat2(origB, nextB) : nextB,
                self->loopSuffixB_);
            self->promptBEditor.setText(appliedB, juce::dontSendNotification);
            self->loopRecentB_.add(nextB);                             // push into recent…
            while (self->loopRecentB_.size() > 3) self->loopRecentB_.remove(0);  // …keep last 3

            // A pole: driven only in dual couplings; in alpha it stays the human anchor
            // (untouched → its suffix is inherently preserved).
            juce::String appliedA = self->promptAEditor.getText().trim();
            if (dual)
            {
                self->loopLastA_ = nextA;                               // CORE
                appliedA = RepromptStances::reattachMusicSuffix(
                    (concat && origA.isNotEmpty()) ? RepromptStances::concat2(origA, nextA) : nextA,
                    self->loopSuffixA_);
                self->promptAEditor.setText(appliedA, juce::dontSendNotification);
                self->loopRecentA_.add(nextA);
                while (self->loopRecentA_.size() > 3) self->loopRecentA_.remove(0);
            }

            // Tell the rest of the machinery these are the current prompts, so the
            // next Auto-Regen tick sees NO spurious prompt change (which would trip
            // the resynth-loop release edge and detach the init carry).
            self->lastGenPromptA_ = appliedA;
            self->lastGenPromptB_ = appliedB;
            self->processorRef.setLastPrompts(appliedA, appliedB);

            self->loopStepInFlight_ = false;
        });
    }).detach();
}
