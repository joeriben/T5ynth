#include "MainPanel.h"
#include "../PluginProcessor.h"
#include "../dsp/BlockParams.h"
#include "GuiHelpers.h"
#include "BinaryData.h"
#include <thread>

MainPanel::MainPanel(T5ynthProcessor& processor)
    : processorRef(processor),
      promptPanel(processor),
      synthPanel(processor),
      fxPanel(processor.getValueTreeState(), processor),
      sequencerPanel(processor)
{
    setOpaque(true);
    addAndMakeVisible(promptPanel);
    addAndMakeVisible(axesPanel);
    addAndMakeVisible(synthPanel);
    addAndMakeVisible(fxPanel);
    addAndMakeVisible(sequencerPanel);
    addAndMakeVisible(statusBar);

    // Left column section headers
    paintSectionHeader(oscHeader, "T5 OSCILLATOR", kOscCol);
    addAndMakeVisible(oscHeader);

    poweredByLabel.setText("Powered by Stability AI", juce::dontSendNotification);
    poweredByLabel.setColour(juce::Label::textColourId, kBg.withAlpha(0.7f));
    poweredByLabel.setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    poweredByLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(poweredByLabel);
    paintSectionHeader(axesHeader, "SEMANTIC AXES", kOscCol);
    addAndMakeVisible(axesHeader);
    paintSectionHeader(dimHeader, "LATENT DIMENSION EXPLORER", kOscCol);
    addAndMakeVisible(dimHeader);

    // Axes description note is inside AxesPanel

    // Wire StatusBar buttons
    statusBar.onNewPreset  = [this] { loadInitPreset(); };
    statusBar.onSavePreset = [this] { savePreset(); };
    statusBar.onLoadPreset = [this] { loadPreset(); };
    statusBar.onExportWav = [this] { exportWav(); };
    statusBar.onSettings = [this] { if (settingsVisible) hideSettings(); else showSettings(); };
    statusBar.onManual = [this] { showManual(); };

    // Settings overlay (same pattern as DimExplorer)
    settingsScrim.onClick = [this] { hideSettings(); };
    settingsScrim.setVisible(false);
    addChildComponent(settingsScrim);
    addChildComponent(settingsPage);
    settingsPage.onModelReady = [this]
    {
        if (promptPanel.isGenerating())
        {
            pendingInferenceReload = true;
            statusBar.setStatusText("Model installed. Backend reload is queued.");
            return;
        }

        tryLoadInferenceModels(true);
    };

    // Manual overlay — hosts the native WebBrowserComponent that renders
    // the shipped HTML guide. Clicking outside the panel or the close
    // button hides the overlay without destroying the web view, so the
    // page stays loaded for subsequent opens.
    manualScrim.onClick = [this] { hideManual(); };
    manualScrim.setVisible(false);
    addChildComponent(manualScrim);

    manualPanel.setVisible(false);
    addChildComponent(manualPanel);
    manualPanel.addAndMakeVisible(manualWeb);
    manualWeb.setVisible(false);

    manualCloseBtn.setColour(juce::TextButton::buttonColourId, kSurface);
    manualCloseBtn.setColour(juce::TextButton::textColourOffId, kAccent);
    manualCloseBtn.onClick = [this] { hideManual(); };
    manualPanel.addAndMakeVisible(manualCloseBtn);

    // Master volume — vertical slider
    masterVolKnob.setSliderStyle(juce::Slider::LinearVertical);
    masterVolKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 50, 14);
    masterVolKnob.setColour(juce::Slider::trackColourId, kAccent);
    masterVolKnob.setColour(juce::Slider::backgroundColourId, kSurface);
    masterVolKnob.setColour(juce::Slider::textBoxTextColourId, kDim);
    masterVolKnob.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    masterVolKnob.setTextValueSuffix(" dB");
    addAndMakeVisible(masterVolKnob);

    masterVolLabel.setText("Vol", juce::dontSendNotification);
    masterVolLabel.setColour(juce::Label::textColourId, kDim);
    masterVolLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(masterVolLabel);

    masterVolA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.getValueTreeState(), PID::masterVol, masterVolKnob);

    // Main Generate button at bottom of left column
    mainGenerateBtn.setColour(juce::TextButton::buttonColourId, kOscCol);
    mainGenerateBtn.setColour(juce::TextButton::buttonOnColourId, kOscCol.darker(0.3f));
    mainGenerateBtn.setColour(juce::TextButton::textColourOffId, kBg);
    mainGenerateBtn.setColour(juce::TextButton::textColourOnId, kBg);
    mainGenerateBtn.onClick = [this] {
        promptPanel.setSemanticAxes(axesPanel.getAxisValues());
        promptPanel.triggerGenerationWithOffsets({});
    };
    addAndMakeVisible(mainGenerateBtn);

    // Wire axis values callback for drift auto-regen (offsets applied per slot)
    promptPanel.getAxisValuesCallback = [this](float o1, float o2, float o3) {
        return axesPanel.getAxisValuesWithOffsets(o1, o2, o3);
    };



    // Status callback — show in Generate button
    startTimerHz(30);  // 30fps glow animation

    promptPanel.onStatusChanged = [this](const juce::String& text, bool isGenerating) {
        glowGenerating = isGenerating;
        if (isGenerating)
        {
            mainGenerateBtn.setButtonText("generating...");
            mainGenerateBtn.setEnabled(false);
            dimApplyBtn.setButtonText("generating...");
            dimApplyBtn.setEnabled(false);
        }
        else
        {
            mainGenerateBtn.setButtonText("Re-Generate");
            mainGenerateBtn.setEnabled(true);
            dimApplyBtn.setButtonText("Apply + Generate");
            dimApplyBtn.setEnabled(true);
        }
        statusBar.setStatusText(text);
    };

    // Scrim (click outside DimExplorer overlay to close)
    dimScrim.onClick = [this] { hideDimExplorer(); };
    dimScrim.setVisible(false);
    addChildComponent(dimScrim);

    // DimExplorer — always visible (mini-view in left column, overlay on click)
    addAndMakeVisible(dimensionExplorer);
    dimensionExplorer.onClicked = [this] {
        if (!dimExplorerVisible) showDimExplorer();
    };

    // Wire PromptPanel → DimensionExplorer (embedding stats after generation)
    promptPanel.onEmbeddingsReady = [this](const std::vector<float>& a,
                                           const std::vector<float>& b,
                                           const std::vector<float>& baseline) {
        dimensionExplorer.setEmbeddings(a, b, baseline);
    };

    // "Anwenden + generieren" — green, triggers generation with offsets
    dimApplyBtn.setColour(juce::TextButton::buttonColourId, kOscCol);
    dimApplyBtn.setColour(juce::TextButton::textColourOffId, kBg);
    dimApplyBtn.onClick = [this] {
        promptPanel.setSemanticAxes(axesPanel.getAxisValues());
        auto offsets = dimensionExplorer.getDimensionOffsets();
        promptPanel.triggerGenerationWithOffsets(std::move(offsets));
    };
    dimApplyBtn.setVisible(false);
    addChildComponent(dimApplyBtn);

    dimUndoBtn.setColour(juce::TextButton::buttonColourId, kSurface);
    dimUndoBtn.setColour(juce::TextButton::textColourOffId, kDim);
    dimUndoBtn.onClick = [this] { dimensionExplorer.undo(); };
    dimUndoBtn.setVisible(false);
    addChildComponent(dimUndoBtn);

    dimRedoBtn.setColour(juce::TextButton::buttonColourId, kSurface);
    dimRedoBtn.setColour(juce::TextButton::textColourOffId, kDim);
    dimRedoBtn.onClick = [this] { dimensionExplorer.redo(); };
    dimRedoBtn.setVisible(false);
    addChildComponent(dimRedoBtn);

    // "Alle zurücksetzen" — orange
    dimResetBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff4e2700));
    dimResetBtn.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffff9800));
    dimResetBtn.onClick = [this] { dimensionExplorer.resetOffsets(); };
    dimResetBtn.setVisible(false);
    addChildComponent(dimResetBtn);

    // Ensure bundled presets exist in user presets directory
    ensureBundledPresetsExist();

    // Load default preset (if no audio loaded yet)
    loadDefaultPreset();

    // Load native inference models
    tryLoadInferenceModels();

    // Auto-open settings if no model found for any engine
    if (!settingsPage.hasAnyInstalledModel())
        showSettings();
}

void MainPanel::showDimExplorer()
{
    dimExplorerVisible = true;
    dimensionExplorer.setOverlayMode(true);
    dimScrim.setVisible(true);
    dimScrim.toFront(false);
    dimApplyBtn.setVisible(true);
    dimUndoBtn.setVisible(true);
    dimRedoBtn.setVisible(true);
    dimResetBtn.setVisible(true);
    dimensionExplorer.toFront(false);
    dimApplyBtn.toFront(false);
    dimUndoBtn.toFront(false);
    dimRedoBtn.toFront(false);
    dimResetBtn.toFront(false);
    resized();
    repaint();
}

void MainPanel::hideDimExplorer()
{
    dimExplorerVisible = false;
    dimensionExplorer.setOverlayMode(false);
    dimScrim.setVisible(false);
    dimApplyBtn.setVisible(false);
    dimUndoBtn.setVisible(false);
    dimRedoBtn.setVisible(false);
    dimResetBtn.setVisible(false);
    resized();  // repositions DimExplorer back to mini-view
    repaint();
}

void MainPanel::mouseDown(const juce::MouseEvent& e)
{
    // Close overlays on click outside
    if (dimExplorerVisible)
    {
        auto dimBounds = dimensionExplorer.getBounds();
        if (!dimBounds.contains(e.x, e.y))
            hideDimExplorer();
    }
    if (settingsVisible)
    {
        auto settingsBounds = settingsPage.getBounds();
        if (!settingsBounds.contains(e.x, e.y))
            hideSettings();
    }
    if (manualVisible)
    {
        auto mb = manualPanel.getBounds();
        if (!mb.contains(e.x, e.y))
            hideManual();
    }
}

void MainPanel::toggleSettings()
{
    if (settingsVisible) hideSettings(); else showSettings();
}

void MainPanel::showSettings()
{
    settingsVisible = true;
    settingsScrim.setVisible(true);
    settingsScrim.toFront(false);
    settingsPage.setVisible(true);
    settingsPage.toFront(false);
    resized();
}

void MainPanel::hideSettings()
{
    settingsVisible = false;
    settingsScrim.setVisible(false);
    settingsPage.setVisible(false);
    resized();
}

void MainPanel::tryLoadInferenceModels(bool forceRestart)
{
    statusBar.setConnected(false);
    statusBar.setStatusText(forceRestart ? "Refreshing inference..." : "Loading inference...");
    settingsPage.setBackendStarting();

    const auto bundledBackendMode = juce::SystemStats::getEnvironmentVariable("T5YNTH_REQUIRE_BUNDLED_BACKEND", {})
                                        .trim();
    const auto forceBundledBackend = bundledBackendMode.equalsIgnoreCase("1")
                                  || bundledBackendMode.equalsIgnoreCase("true");

    // Find backend directory — accepts either:
    //   backend/pipe_inference.py  (dev: Python script)
    //   backend/pipe_inference     (release: PyInstaller binary)
    //   backend/pipe_inference.exe (Windows release)
    //   backend/dist/pipe_inference/pipe_inference  (local PyInstaller build)
    auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::File backendDir;

    auto hasBackend = [](const juce::File& dir) {
        return dir.getChildFile("pipe_inference.py").existsAsFile()
            || dir.getChildFile("pipe_inference").existsAsFile()
            || dir.getChildFile("pipe_inference.exe").existsAsFile()
            || dir.getChildFile("dist/pipe_inference/pipe_inference").existsAsFile();
    };

    // 1. Standalone macOS app bundle: Contents/MacOS/T5ynth → Contents/Resources/backend
    //    (This branch hits when running the T5ynth Standalone.app directly.)
    auto resources = exe.getParentDirectory().getSiblingFile("Resources").getChildFile("backend");
    if (hasBackend(resources))
        backendDir = resources;

   #if JUCE_MAC
    // Test mode for validating packaged apps on a dev machine: only accept the
    // backend embedded in the current app bundle, never a repo/companion fallback.
    const bool allowSearchUpwards = !forceBundledBackend;
   #else
    const bool allowSearchUpwards = true;
   #endif

    // 2. Walk up from executable (dev builds, Linux/Windows standalone layout:
    //    T5ynth.exe next to backend/).
    if (!backendDir.exists() && allowSearchUpwards)
    {
        auto search = exe.getParentDirectory();
        for (int i = 0; i < 8; ++i)
        {
            auto candidate = search.getChildFile("backend");
            if (hasBackend(candidate))
            {
                backendDir = candidate;
                break;
            }
            search = search.getParentDirectory();
        }
    }

    // 3. Plugin context (VST3/AU): the exe is the DAW, not T5ynth. Look for a
    //    companion T5ynth Standalone install and borrow its bundled backend.
    //    Release archives ship the heavy backend only with the Standalone —
    //    VST3/AU plugins piggy-back on it so the plugin downloads stay small.
    if (!backendDir.exists() && !forceBundledBackend)
    {
       #if JUCE_MAC
        juce::Array<juce::File> companionApps {
            juce::File("/Applications/T5ynth.app"),
            juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                .getChildFile("Applications/T5ynth.app")
        };
        for (const auto& app : companionApps)
        {
            auto candidate = app.getChildFile("Contents/Resources/backend");
            if (hasBackend(candidate))
            {
                backendDir = candidate;
                break;
            }
        }
       #elif JUCE_WINDOWS
        // Windows: the Standalone distribution is a T5ynth/ folder with
        // T5ynth.exe + backend/ inside. Look in the common install prefixes.
        juce::StringArray companionRoots {
            "C:\\Program Files\\T5ynth\\backend",
            "C:\\Program Files (x86)\\T5ynth\\backend"
        };
        for (const auto& p : companionRoots)
        {
            juce::File candidate (p);
            if (hasBackend(candidate))
            {
                backendDir = candidate;
                break;
            }
        }
       #elif JUCE_LINUX
        juce::StringArray companionRoots {
            "/opt/T5ynth/backend",
            "/usr/local/share/T5ynth/backend"
        };
        for (const auto& p : companionRoots)
        {
            juce::File candidate (p);
            if (hasBackend(candidate))
            {
                backendDir = candidate;
                break;
            }
        }
       #endif
    }

    // 4. Last-resort dev fallback: compile-time project backend path.
    //    Only active in dev builds where T5YNTH_BACKEND_DIR is defined.
   #ifdef T5YNTH_BACKEND_DIR
    if (!backendDir.exists() && !forceBundledBackend)
    {
        juce::File devBackend (T5YNTH_BACKEND_DIR);
        if (hasBackend(devBackend))
            backendDir = devBackend;
    }
   #endif

    if (backendDir.exists())
    {
        juce::Component::SafePointer<MainPanel> safeThis(this);
        auto pipePtr = processorRef.getPipeInferencePtr();
        std::thread([safeThis, pipePtr, backendDir, forceRestart]()
        {
            if (forceRestart)
                pipePtr->shutdown();

            bool ok = pipePtr->launch(backendDir);
            auto errorMsg = ok ? juce::String() : pipePtr->getLastError();
            juce::MessageManager::callAsync([safeThis, ok, errorMsg]()
            {
                if (auto* self = safeThis.getComponent())
                {
                    if (ok)
                    {
                        self->statusBar.setConnected(true);
                        self->statusBar.setStatusText("Ready");
                        self->settingsPage.setBackendConnected(true);
                        self->promptPanel.refreshInferenceChoices();
                    }
                    else
                    {
                        self->statusBar.setConnected(false);
                        self->statusBar.setStatusText("Backend: " + errorMsg);
                        self->settingsPage.setBackendFailed(errorMsg);
                    }
                }
            });
        }).detach();
    }
    else
    {
        // Plugin context with no companion install is the most common failure —
        // give the user an actionable hint instead of the generic message.
        const auto msg = forceBundledBackend
                         ? juce::String("Bundled backend not found in app")
                         : (juce::JUCEApplicationBase::isStandaloneApp()
                            ? juce::String("Backend not found — reinstall T5ynth")
                            : juce::String("Backend not found — install the T5ynth app"));
        statusBar.setStatusText(msg);
        settingsPage.setBackendFailed(forceBundledBackend ? "Bundled backend missing" : "Not found");
    }
}

// ── Buffer preset: persist full state on Standalone quit ──
static juce::File getBufferPresetFile()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
               .getChildFile("T5ynth")
               .getChildFile("_buffer.t5p");
}

MainPanel::~MainPanel()
{
    stopTimer();

    if (!juce::JUCEApplicationBase::isStandaloneApp())
        return;

    // Sync GUI-only state into processor before saving
    processorRef.setLastPrompts(promptPanel.getPromptA(), promptPanel.getPromptB());
    processorRef.setLastSeed(promptPanel.getSeed());
    {
        auto axStates = axesPanel.getSlotStates();
        std::array<T5ynthProcessor::AxisSlotState, 3> procAxes;
        for (int i = 0; i < 3; ++i)
        {
            procAxes[static_cast<size_t>(i)].dropdownId = axStates[static_cast<size_t>(i)].dropdownId;
            procAxes[static_cast<size_t>(i)].value      = axStates[static_cast<size_t>(i)].value;
        }
        processorRef.setLastAxes(procAxes);
    }

    auto bufFile = getBufferPresetFile();
    bufFile.getParentDirectory().createDirectory();
    PresetFormat::saveToFile(bufFile, processorRef);
}

void MainPanel::paint(juce::Graphics& g)
{
    g.fillAll(kBg);

    float w = static_cast<float>(getWidth());
    float footerTop = static_cast<float>(sequencerPanel.getY());

    g.setColour(kBorder);
    float x1 = static_cast<float>(promptPanel.getRight() + 4);
    g.drawVerticalLine(juce::roundToInt(x1), 0.0f, footerTop);
    g.drawHorizontalLine(juce::roundToInt(footerTop), 0.0f, w);

    int inset = 4;

    // Card 1: OSCILLATOR (oscHeader + promptPanel)
    {
        int top = oscHeader.getY() - inset;
        int bot = promptPanel.getBottom() + inset;
        int left = oscHeader.getX() - inset;
        int cardW = promptPanel.getWidth() + inset * 2;
        paintCard(g, juce::Rectangle<int>(left, top, cardW, bot - top));
    }

    // Card 2: AXES (axesHeader + axesPanel)
    {
        int top = axesHeader.getY() - inset;
        int bot = axesPanel.getBottom() + inset;
        int left = axesHeader.getX() - inset;
        int cardW = axesPanel.getWidth() + inset * 2;
        paintCard(g, juce::Rectangle<int>(left, top, cardW, bot - top));
    }

    // Card 3: DIM EXPLORER + Generate button
    if (!dimExplorerVisible)
    {
        int top = dimHeader.getY() - inset;
        int bot = sequencerPanel.getY() - inset;
        int left = dimHeader.getX() - inset;
        int cardW = dimensionExplorer.getWidth() + inset * 2;
        paintCard(g, juce::Rectangle<int>(left, top, cardW, bot - top));
    }

    // ── Pulsing glow behind Generate button ──
    {
        float pulse = std::sin(glowPhase);
        float expand = 4.0f + 6.0f * (0.5f + 0.5f * pulse);
        float alpha  = 0.12f + 0.10f * (0.5f + 0.5f * pulse);
        if (glowGenerating) alpha += 0.10f;

        auto gb = mainGenerateBtn.getBounds().toFloat();
        for (int i = 3; i >= 0; --i)
        {
            float layerExpand = expand * (1.0f + static_cast<float>(i) * 0.4f);
            float layerAlpha  = alpha * (0.25f + 0.75f / (1.0f + static_cast<float>(i)));
            g.setColour(kOscCol.withAlpha(layerAlpha));
            g.fillRect(gb.expanded(layerExpand));
        }
    }
}

void MainPanel::timerCallback()
{
    glowPhase += glowGenerating ? 0.25f : 0.08f;
    if (glowPhase > juce::MathConstants<float>::twoPi)
        glowPhase -= juce::MathConstants<float>::twoPi;
    repaint(mainGenerateBtn.getBounds().expanded(20));

    // Poll drift ghost offsets for AxesPanel (30Hz)
    auto& mv = processorRef.modulatedValues;
    axesPanel.setGhostOffsets(
        mv.driftAxis1.load(std::memory_order_relaxed),
        mv.driftAxis2.load(std::memory_order_relaxed),
        mv.driftAxis3.load(std::memory_order_relaxed));

    if (pendingInferenceReload && !promptPanel.isGenerating())
    {
        pendingInferenceReload = false;
        tryLoadInferenceModels(true);
    }
}

void MainPanel::resized()
{
    auto b = getLocalBounds();
    float w = static_cast<float>(b.getWidth());
    float h = static_cast<float>(b.getHeight());

    int statusH = 20;
    int footerH = juce::jlimit(160, 280, juce::roundToInt(h * 0.24f));
    statusBar.setBounds(b.removeFromBottom(statusH));

    // Gap between footer and main content
    b.removeFromBottom(6);

    // Footer
    auto footer = b.removeFromBottom(footerH);
    int volW = juce::jlimit(40, 60, juce::roundToInt(w * 0.05f));
    auto volArea = footer.removeFromRight(volW);
    masterVolLabel.setFont(juce::FontOptions(10.0f));
    masterVolLabel.setBounds(volArea.removeFromTop(14));
    masterVolKnob.setBounds(volArea);
    int footerGap = juce::jlimit(4, 8, juce::roundToInt(w * 0.005f));
    footer.removeFromRight(footerGap);  // gap Vol–FX

    const int footerContentW = footer.getWidth() - footerGap;
    const int fxPrefW = juce::jlimit(180, 400, juce::roundToInt(w * 0.28f));
    const int fxMinW = 160;
    const int seqMinW = 360;

    int fxW = juce::jmin(fxPrefW, juce::jmax(fxMinW, footerContentW - seqMinW));
    int seqW = footerContentW - fxW;
    if (seqW < seqMinW)
    {
        fxW = juce::jmax(120, footerContentW - seqMinW);
        seqW = footerContentW - fxW;
    }

    seqW = juce::jmax(280, seqW);
    fxW = juce::jmax(120, footerContentW - seqW);

    fxPanel.setBounds(footer.removeFromRight(fxW));
    footer.removeFromRight(footerGap);  // gap FX–Seq
    sequencerPanel.setBounds(footer.removeFromRight(seqW));

    // ═══ Col 1: Three cards — OSCILLATOR, AXES, DIM EXPLORER ═══
    int col1W = juce::jlimit(240, 420, juce::roundToInt(w * 0.25f));
    auto genCol = b.removeFromLeft(col1W).reduced(6, 2);

    int headerH = juce::jlimit(14, 20, juce::roundToInt(h * 0.022f));
    constexpr int kGenBtnH = 54;
    int kGap = juce::jlimit(3, 6, juce::roundToInt(h * 0.005f));

    int available = genCol.getHeight() - headerH * 3 - kGap * 2;
    int oscH = juce::jmax(300, juce::roundToInt(available * 0.55f));
    int axesH = juce::jmax(70, juce::roundToInt(available * 0.18f));
    // dimH gets the rest

    // Card 1: OSCILLATOR
    oscHeader.setFont(juce::FontOptions(static_cast<float>(headerH) * 0.85f));
    oscHeader.setBounds(genCol.removeFromTop(headerH));
    // "Powered by Stability AI" overlays right side of header
    poweredByLabel.setFont(juce::FontOptions(static_cast<float>(headerH) * 0.6f));
    poweredByLabel.setBounds(oscHeader.getBounds());
    promptPanel.setBounds(genCol.removeFromTop(oscH));
    genCol.removeFromTop(kGap);

    // Card 2: SEMANTIC AXES
    axesHeader.setFont(juce::FontOptions(static_cast<float>(headerH) * 0.85f));
    axesHeader.setBounds(genCol.removeFromTop(headerH));
    axesPanel.setBounds(genCol.removeFromTop(axesH));
    genCol.removeFromTop(kGap);

    // Card 3: DIM EXPLORER
    dimHeader.setFont(juce::FontOptions(static_cast<float>(headerH) * 0.85f));
    dimHeader.setBounds(genCol.removeFromTop(headerH));
    int dimH = genCol.getHeight() / 3;
    if (!dimExplorerVisible)
        dimensionExplorer.setBounds(genCol.removeFromTop(dimH));

    // Generate button — vertically centered in remaining space
    int remainH = genCol.getHeight();
    int genBtnY = genCol.getY() + (remainH - kGenBtnH) / 2;
    auto genBtnArea = juce::Rectangle<int>(genCol.getX(), genBtnY, genCol.getWidth(), kGenBtnH);
    int genW = juce::roundToInt(genBtnArea.getWidth() * 0.6f);
    int genX = genBtnArea.getX() + (genBtnArea.getWidth() - genW) / 2;
    mainGenerateBtn.setBounds(genX, genBtnArea.getY(), genW, genBtnArea.getHeight());

    // Col 2: ENGINE
    synthPanel.setBounds(b);



    // Scrims cover everything
    dimScrim.setBounds(getLocalBounds());
    settingsScrim.setBounds(getLocalBounds());
    manualScrim.setBounds(getLocalBounds());

    // Manual overlay (centered). Leaves a strip at the bottom of the
    // panel for the close button; the WebBrowserComponent fills the rest.
    if (manualVisible)
    {
        int manW = juce::jlimit(720, 1100, juce::roundToInt(w * 0.8f));
        int manH = juce::jlimit(480, 820, juce::roundToInt(h * 0.85f));
        int mx = (getWidth() - manW) / 2;
        int my = (getHeight() - manH) / 2;
        manualPanel.setBounds(mx, my, manW, manH);

        auto inner = manualPanel.getLocalBounds().reduced(8);
        auto btnRow = inner.removeFromBottom(30);
        inner.removeFromBottom(6);
        manualWeb.setBounds(inner);
        manualCloseBtn.setBounds(btnRow.removeFromRight(90));
    }
    else
    {
        manualPanel.setBounds({});
        manualWeb.setBounds(-10000, -10000, 1, 1);
    }

    // Settings overlay (bottom-right, above StatusBar)
    if (settingsVisible)
    {
        int settingsW = juce::jlimit(400, 600, juce::roundToInt(w * 0.4f));
        int settingsH = juce::jlimit(300, 500, juce::roundToInt(h * 0.55f));
        int sx = getWidth() - settingsW - 20;
        int sy = getHeight() - statusH - settingsH - 30;
        settingsPage.setBounds(sx, sy, settingsW, settingsH);
    }

    // DimExplorer overlay
    if (dimExplorerVisible)
    {
        auto overlayBounds = getLocalBounds().reduced(40);
        int btnH = 30;
        int applyW = 180;
        int smallW = 70;
        int resetW = 140;
        int btnGap = 10;

        auto btnArea = overlayBounds.removeFromBottom(btnH + 10);
        int totalBtnW = applyW + smallW * 2 + resetW + btnGap * 3;
        int startX = btnArea.getCentreX() - totalBtnW / 2;
        int y = btnArea.getY();

        dimApplyBtn.setBounds(startX, y, applyW, btnH);
        dimUndoBtn.setBounds(startX + applyW + btnGap, y, smallW, btnH);
        dimRedoBtn.setBounds(startX + applyW + smallW + btnGap * 2, y, smallW, btnH);
        dimResetBtn.setBounds(startX + applyW + smallW * 2 + btnGap * 3, y, resetW, btnH);

        dimensionExplorer.setBounds(overlayBounds.reduced(20, 10));
    }
}

// ═══════════════════════════════════════════════════════════════════
// Default Preset
// ═══════════════════════════════════════════════════════════════════

bool MainPanel::loadBundledPreset(const char* data, int size, const juce::String& tempName)
{
    // Write bundled preset binary to a temp file, then route it through
    // the standard PresetFormat loader (which validates the magic + strict
    // version check). Used by loadDefaultPreset / loadInitPreset.
    auto tmpFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile(tempName);
    tmpFile.replaceWithData(data, static_cast<size_t>(size));

    auto result = PresetFormat::loadFromFile(tmpFile, processorRef);
    tmpFile.deleteFile();
    if (!result.success)
        return false;

    promptPanel.loadPresetData(result.promptA, result.promptB,
                               result.seed, result.randomSeed, result.device, result.model);

    if (result.hasAxes)
    {
        std::array<AxesPanel::SlotState, 3> states;
        for (int i = 0; i < 3; ++i)
        {
            states[static_cast<size_t>(i)].dropdownId = result.axes[static_cast<size_t>(i)].dropdownId;
            states[static_cast<size_t>(i)].value = result.axes[static_cast<size_t>(i)].value;
        }
        axesPanel.setSlotStates(states);
    }

    if (result.hasAudio)
    {
        processorRef.loadGeneratedAudio(result.audio, result.sampleRate);
        processorRef.setLastSeed(result.seed);
        processorRef.setLastPrompts(result.promptA, result.promptB);
    }

    if (!result.embeddingA.empty())
    {
        processorRef.setLastEmbeddings(result.embeddingA, result.embeddingB);
        auto& apvts = processorRef.getValueTreeState();
        auto baseline = DimensionExplorer::estimateBaselineValues(
            result.embeddingA, result.embeddingB,
            apvts.getRawParameterValue(PID::genAlpha)->load(),
            apvts.getRawParameterValue(PID::genMagnitude)->load());
        dimensionExplorer.setEmbeddings(result.embeddingA, result.embeddingB, baseline, false);
    }

    statusBar.setPresetName(result.presetName);
    return true;
}

void MainPanel::ensureBundledPresetsExist()
{
    auto userDir = PresetFormat::getUserPresetsDirectory();

    struct BundledPreset { const char* data; int size; const char* fileName; };
    const BundledPreset bundled[] = {
        { BinaryData::DEMO_T5OscillatorDrift_t5p,
          BinaryData::DEMO_T5OscillatorDrift_t5pSize,
          "DEMO T5-Oscillator-Drift.t5p" },
    };

    for (auto& bp : bundled)
    {
        auto target = userDir.getChildFile(bp.fileName);
        if (!target.existsAsFile())
            target.replaceWithData(bp.data, static_cast<size_t>(bp.size));
    }
}

void MainPanel::loadDefaultPreset()
{
    // Only load if no audio present (fresh launch, not DAW session restore)
    if (processorRef.getGeneratedAudio().getNumSamples() > 0)
        return;

    // Standalone: restore previous session state if available
    if (juce::JUCEApplicationBase::isStandaloneApp())
    {
        auto bufFile = getBufferPresetFile();
        if (bufFile.existsAsFile())
        {
            auto result = PresetFormat::loadFromFile(bufFile, processorRef);
            if (result.success)
            {
                promptPanel.loadPresetData(result.promptA, result.promptB,
                                           result.seed, result.randomSeed, result.device, result.model);
                if (result.hasAxes)
                {
                    std::array<AxesPanel::SlotState, 3> states;
                    for (int i = 0; i < 3; ++i)
                    {
                        states[static_cast<size_t>(i)].dropdownId = result.axes[static_cast<size_t>(i)].dropdownId;
                        states[static_cast<size_t>(i)].value      = result.axes[static_cast<size_t>(i)].value;
                    }
                    axesPanel.setSlotStates(states);
                }
                if (result.hasAudio)
                {
                    processorRef.loadGeneratedAudio(result.audio, result.sampleRate);
                    processorRef.setLastSeed(result.seed);
                    processorRef.setLastPrompts(result.promptA, result.promptB);
                }
                if (!result.embeddingA.empty())
                {
                    processorRef.setLastEmbeddings(result.embeddingA, result.embeddingB);
                    auto& apvts = processorRef.getValueTreeState();
                    auto baseline = DimensionExplorer::estimateBaselineValues(
                        result.embeddingA, result.embeddingB,
                        apvts.getRawParameterValue(PID::genAlpha)->load(),
                        apvts.getRawParameterValue(PID::genMagnitude)->load());
                    dimensionExplorer.setEmbeddings(result.embeddingA, result.embeddingB, baseline, false);
                }
                statusBar.setPresetName(result.presetName);
                return;
            }
        }
    }

    // First launch or DAW: load bundled DEMO
    loadBundledPreset(BinaryData::DEMO_T5OscillatorDrift_t5p,
                      BinaryData::DEMO_T5OscillatorDrift_t5pSize,
                      "t5ynth_default.t5p");
}

void MainPanel::loadInitPreset()
{
    loadBundledPreset(BinaryData::INIT_t5p,
                      BinaryData::INIT_t5pSize,
                      "t5ynth_init.t5p");
}

// ═══════════════════════════════════════════════════════════════════
// WAV Export
// ═══════════════════════════════════════════════════════════════════

void MainPanel::exportWav()
{
    const auto& audio = processorRef.getGeneratedAudio();
    if (audio.getNumSamples() == 0)
    {
        statusBar.setStatusText("No audio to export");
        return;
    }

    auto chooser = std::make_shared<juce::FileChooser>(
        "Export WAV", juce::File::getSpecialLocation(juce::File::userDesktopDirectory), "*.wav");

    juce::Component::SafePointer<MainPanel> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode
                         | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis, chooser](const juce::FileChooser& fc)
        {
            if (!safeThis) return;
            auto* self = safeThis.getComponent();
            auto file = fc.getResult();
            if (file == juce::File()) return;

            // Ensure .wav extension
            if (!file.hasFileExtension("wav"))
                file = file.withFileExtension("wav");

            const auto& buf = self->processorRef.getGeneratedAudio();
            auto outStream = file.createOutputStream();
            if (!outStream) { self->statusBar.setStatusText("Export failed"); return; }

            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wav.createWriterFor(outStream.release(), 44100.0,
                                    static_cast<unsigned int>(buf.getNumChannels()),
                                    16, {}, 0));
            if (writer)
            {
                writer->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
                self->statusBar.setStatusText("Exported: " + file.getFileName());
            }
            else
                self->statusBar.setStatusText("Export failed");
        });
}

// ═══════════════════════════════════════════════════════════════════
// Manual — native WebView renders the bundled HTML guide.
// The HTML is compiled into the plugin as BinaryData and extracted
// once per app-session to a temp file so the WKWebView / WebView2 /
// WebKitGTK backend has a stable file:// URL to load. Anchor links
// (#setup, #gen, …) work natively; external https:// links launch
// in the user's default browser.
// ═══════════════════════════════════════════════════════════════════

void MainPanel::showManual()
{
    manualVisible = true;
    manualScrim.setVisible(true);
    manualScrim.toFront(false);
    manualPanel.setVisible(true);
    manualPanel.toFront(false);
#if JUCE_LINUX
    if (manualWeb.getParentComponent() != &manualPanel)
        manualPanel.addAndMakeVisible(manualWeb);
#endif
    manualWeb.setVisible(true);

    if (!manualLoaded)
    {
        // Extract the bundled HTML to a temp file once per session.
        manualHtmlOnDisk = juce::File::getSpecialLocation(juce::File::tempDirectory)
                               .getChildFile("T5ynth_Guide.html");
        manualHtmlOnDisk.replaceWithData(BinaryData::T5ynth_Guide_html,
                                         static_cast<size_t>(BinaryData::T5ynth_Guide_htmlSize));

        manualWeb.goToURL(juce::URL(manualHtmlOnDisk).toString(false));
        manualLoaded = true;
    }

    resized();
}

void MainPanel::hideManual()
{
    manualVisible = false;
    manualScrim.setVisible(false);
    manualPanel.setVisible(false);
    manualWeb.setVisible(false);
    manualWeb.setBounds(-10000, -10000, 1, 1);
#if JUCE_LINUX
    // Linux WebKit child windows can leak through hidden parents; detach the
    // native view while the overlay is closed to avoid the white artefact.
    if (manualWeb.getParentComponent() == &manualPanel)
        manualPanel.removeChildComponent(&manualWeb);
#endif
}

// ═══════════════════════════════════════════════════════════════════
// Preset Save / Load
// ═══════════════════════════════════════════════════════════════════

void MainPanel::savePreset()
{
    // Store current prompt state in processor before saving
    // (PromptPanel stores them on generation, but user may have edited since)
    processorRef.setLastPrompts(promptPanel.getPromptA(), promptPanel.getPromptB());
    processorRef.setLastSeed(promptPanel.getSeed());
    // Store axes state (GUI-only, not in APVTS)
    {
        auto axStates = axesPanel.getSlotStates();
        std::array<T5ynthProcessor::AxisSlotState, 3> procAxes;
        for (int i = 0; i < 3; ++i)
        {
            procAxes[static_cast<size_t>(i)].dropdownId = axStates[static_cast<size_t>(i)].dropdownId;
            procAxes[static_cast<size_t>(i)].value = axStates[static_cast<size_t>(i)].value;
        }
        processorRef.setLastAxes(procAxes);
    }

    auto presetsDir = PresetFormat::getPresetsDirectory();
    auto chooser = std::make_shared<juce::FileChooser>(
        "Save Preset", presetsDir, "*.t5p");

    juce::Component::SafePointer<MainPanel> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode
                         | juce::FileBrowserComponent::canSelectFiles
                         | juce::FileBrowserComponent::warnAboutOverwriting,
        [safeThis, chooser](const juce::FileChooser& fc)
        {
            if (!safeThis) return;
            auto file = fc.getResult();
            if (file == juce::File()) return;

            if (PresetFormat::saveToFile(file, safeThis->processorRef))
            {
                safeThis->statusBar.setPresetName(file.getFileNameWithoutExtension());
                safeThis->statusBar.setStatusText("Saved preset: " + file.withFileExtension("t5p").getFileName());
            }
            else
            {
                safeThis->statusBar.setStatusText("Preset save failed");
            }
        });
}

void MainPanel::loadPreset()
{
    auto presetsDir = PresetFormat::getPresetsDirectory();
    auto chooser = std::make_shared<juce::FileChooser>(
        "Load Preset", presetsDir, "*.t5p;*.json");

    juce::Component::SafePointer<MainPanel> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis, chooser](const juce::FileChooser& fc)
        {
            if (!safeThis) return;
            auto* self = safeThis.getComponent();
            auto file = fc.getResult();
            if (!file.existsAsFile()) return;

            auto result = PresetFormat::loadFromFile(file, self->processorRef);
            if (!result.success) return;

            // Restore prompts/seed to GUI
            self->promptPanel.loadPresetData(result.promptA, result.promptB,
                                             result.seed, result.randomSeed, result.device, result.model);

            // Restore semantic axes
            if (result.hasAxes)
            {
                std::array<AxesPanel::SlotState, 3> states;
                for (int i = 0; i < 3; ++i)
                {
                    states[static_cast<size_t>(i)].dropdownId = result.axes[static_cast<size_t>(i)].dropdownId;
                    states[static_cast<size_t>(i)].value = result.axes[static_cast<size_t>(i)].value;
                }
                self->axesPanel.setSlotStates(states);
            }

            // Restore audio into engine (skips generation!)
            if (result.hasAudio)
            {
                self->processorRef.loadGeneratedAudio(result.audio, result.sampleRate);
                self->processorRef.setLastSeed(result.seed);
                self->processorRef.setLastPrompts(result.promptA, result.promptB);
            }

            // Restore embeddings to DimExplorer
            if (!result.embeddingA.empty())
            {
                self->processorRef.setLastEmbeddings(result.embeddingA, result.embeddingB);
                auto& apvts = self->processorRef.getValueTreeState();
                auto baseline = DimensionExplorer::estimateBaselineValues(
                    result.embeddingA, result.embeddingB,
                    apvts.getRawParameterValue(PID::genAlpha)->load(),
                    apvts.getRawParameterValue(PID::genMagnitude)->load());
                self->dimensionExplorer.setEmbeddings(result.embeddingA, result.embeddingB, baseline, false);
            }

            self->statusBar.setPresetName(result.presetName);
        });
}
