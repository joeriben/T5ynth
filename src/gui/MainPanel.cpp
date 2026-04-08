#include "MainPanel.h"
#include "../PluginProcessor.h"
#include "GuiHelpers.h"
#include "BinaryData.h"
#include <thread>

MainPanel::MainPanel(T5ynthProcessor& processor)
    : processorRef(processor),
      promptPanel(processor),
      synthPanel(processor),
      fxPanel(processor.getValueTreeState()),
      sequencerPanel(processor)
{
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
    statusBar.onSavePreset = [this] { savePreset(); };
    statusBar.onLoadPreset = [this] { loadPreset(); };
    statusBar.onExportWav = [this] { exportWav(); };
    statusBar.onSettings = [this] { if (settingsVisible) hideSettings(); else showSettings(); };
    statusBar.onAbout = [this] { showAbout(); };

    // Settings overlay (same pattern as DimExplorer)
    settingsScrim.onClick = [this] { hideSettings(); };
    settingsScrim.setVisible(false);
    addChildComponent(settingsScrim);
    addChildComponent(settingsPage);

    // About overlay
    aboutScrim.onClick = [this] { hideAbout(); };
    aboutScrim.setVisible(false);
    addChildComponent(aboutScrim);

    // About panel
    aboutPanel.setVisible(false);
    addChildComponent(aboutPanel);

    aboutText.setMultiLine(true);
    aboutText.setReadOnly(true);
    aboutText.setColour(juce::TextEditor::backgroundColourId, kCard);
    aboutText.setColour(juce::TextEditor::textColourId, juce::Colour(0xffe3e3e3));
    aboutText.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    aboutText.setScrollbarsShown(true);
    aboutPanel.addAndMakeVisible(aboutText);

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
        processor.getValueTreeState(), "master_vol", masterVolKnob);

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

    // HF boost toggle — compensates VAE decoder high-frequency rolloff
    hfBoostBtn.setColour(juce::TextButton::buttonColourId, kSurface);
    hfBoostBtn.setColour(juce::TextButton::buttonOnColourId, kOscCol);
    hfBoostBtn.setColour(juce::TextButton::textColourOffId, kDim);
    hfBoostBtn.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    hfBoostBtn.setClickingTogglesState(true);
    hfBoostBtn.setToggleState(
        processorRef.getValueTreeState().getRawParameterValue("gen_hf_boost")->load() > 0.5f,
        juce::dontSendNotification);
    hfBoostBtn.onClick = [this] {
        bool on = hfBoostBtn.getToggleState();
        processorRef.getValueTreeState().getParameter("gen_hf_boost")
            ->setValueNotifyingHost(on ? 1.0f : 0.0f);
        // Re-apply HF boost from raw audio without re-generating
        const auto& raw = processorRef.getGeneratedAudioRaw();
        if (raw.getNumSamples() > 0)
            processorRef.loadGeneratedAudio(raw, processorRef.getGeneratedSampleRate());
    };
    addAndMakeVisible(hfBoostBtn);

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
    promptPanel.onEmbeddingsReady = [this](const std::vector<float>& a, const std::vector<float>& b) {
        dimensionExplorer.setEmbeddings(a, b);
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
    dimResetBtn.onClick = [this] { dimensionExplorer.clear(); dimensionExplorer.repaint(); };
    dimResetBtn.setVisible(false);
    addChildComponent(dimResetBtn);

    // Load default preset (if no audio loaded yet)
    loadDefaultPreset();

    // Load native inference models
    tryLoadInferenceModels();

    // Auto-open settings if no model found on first launch
    if (!settingsPage.getModelPath().exists())
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
    if (aboutVisible)
    {
        auto ab = aboutPanel.getBounds();
        if (!ab.contains(e.x, e.y))
            hideAbout();
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

void MainPanel::tryLoadInferenceModels()
{
    statusBar.setStatusText("Loading inference...");

    auto* processor = &processorRef;

    // Find backend directory — accepts either:
    //   backend/pipe_inference.py  (dev: Python script)
    //   backend/pipe_inference     (release: PyInstaller binary)
    //   backend/dist/pipe_inference/pipe_inference  (local PyInstaller build)
    auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::File backendDir;

    auto hasBackend = [](const juce::File& dir) {
        return dir.getChildFile("pipe_inference.py").existsAsFile()
            || dir.getChildFile("pipe_inference").existsAsFile()
            || dir.getChildFile("pipe_inference.exe").existsAsFile()
            || dir.getChildFile("dist/pipe_inference/pipe_inference").existsAsFile();
    };

    // macOS app bundle: Contents/MacOS/T5ynth → Contents/Resources/backend
    auto resources = exe.getParentDirectory().getSiblingFile("Resources").getChildFile("backend");
    if (hasBackend(resources))
        backendDir = resources;

    // Walk up from executable (dev builds, Linux, Windows)
    if (!backendDir.exists())
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

    if (backendDir.exists())
    {
        std::thread([this, processor, backendDir]()
        {
            bool ok = processor->launchPipeInference(backendDir);
            juce::MessageManager::callAsync([this, ok]()
            {
                if (ok)
                {
                    statusBar.setConnected(true);
                    statusBar.setStatusText("Ready");
                    settingsPage.setBackendConnected(true);
                }
                else
                {
                    statusBar.setStatusText("Python backend failed to start");
                }
            });
        }).detach();
    }
    else
    {
        statusBar.setStatusText("Backend not found — check installation");
    }
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
    repaint(mainGenerateBtn.getBounds().expanded(16));

    // Poll drift ghost offsets for AxesPanel (30Hz)
    auto& mv = processorRef.modulatedValues;
    axesPanel.setGhostOffsets(
        mv.driftAxis1.load(std::memory_order_relaxed),
        mv.driftAxis2.load(std::memory_order_relaxed),
        mv.driftAxis3.load(std::memory_order_relaxed));
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
    int fxW = juce::jlimit(180, 400, juce::roundToInt(w * 0.28f));
    auto volArea = footer.removeFromRight(volW);
    masterVolLabel.setFont(juce::FontOptions(10.0f));
    masterVolLabel.setBounds(volArea.removeFromTop(14));
    masterVolKnob.setBounds(volArea);
    int footerGap = juce::jlimit(4, 8, juce::roundToInt(w * 0.005f));
    footer.removeFromRight(footerGap);  // gap Vol–FX
    fxPanel.setBounds(footer.removeFromRight(fxW));
    footer.removeFromRight(footerGap);  // gap FX–Seq
    sequencerPanel.setBounds(footer);

    // ═══ Col 1: Three cards — OSCILLATOR, AXES, DIM EXPLORER ═══
    int col1W = juce::jlimit(240, 420, juce::roundToInt(w * 0.25f));
    auto genCol = b.removeFromLeft(col1W).reduced(6, 2);

    int headerH = juce::jlimit(14, 20, juce::roundToInt(h * 0.022f));
    constexpr int kGenBtnH = 34;
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

    // HF toggle — compact, same height as GPU/CPU buttons
    int hfH = juce::roundToInt(genBtnArea.getHeight() * 0.6f);
    int hfW = juce::roundToInt(hfH * 1.6f);  // wide enough for "HF"
    int gap = (genBtnArea.getRight() - (genX + genW));
    int hfX = genX + genW + (gap - hfW) / 2;
    int hfY = genBtnArea.getY() + (genBtnArea.getHeight() - hfH) / 2;
    hfBoostBtn.setBounds(hfX, hfY, hfW, hfH);

    // Col 2: ENGINE
    synthPanel.setBounds(b);



    // Scrims cover everything
    dimScrim.setBounds(getLocalBounds());
    settingsScrim.setBounds(getLocalBounds());
    aboutScrim.setBounds(getLocalBounds());

    // About overlay (centered)
    if (aboutVisible)
    {
        int aboutW = juce::jlimit(600, 1000, juce::roundToInt(w * 0.7f));
        int aboutH = juce::jlimit(400, 700, juce::roundToInt(h * 0.8f));
        int ax = (getWidth() - aboutW) / 2;
        int ay = (getHeight() - aboutH) / 2;
        aboutPanel.setBounds(ax, ay, aboutW, aboutH);

        aboutText.setBounds(aboutPanel.getLocalBounds().reduced(8));
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

void MainPanel::loadDefaultPreset()
{
    // Only load if no audio present (fresh launch, not DAW session restore)
    if (processorRef.getGeneratedAudio().getNumSamples() > 0)
        return;

    // Write bundled default preset to temp file, then load it
    auto tmpFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("t5ynth_default.t5p");
    tmpFile.replaceWithData(BinaryData::DEMO_T5OscillatorDrift_t5p,
                            static_cast<size_t>(BinaryData::DEMO_T5OscillatorDrift_t5pSize));

    auto result = PresetFormat::loadFromFile(tmpFile, processorRef);
    tmpFile.deleteFile();
    if (!result.success)
        return;

    promptPanel.loadPresetData(result.promptA, result.promptB,
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
        dimensionExplorer.setEmbeddings(result.embeddingA, result.embeddingB);
    }

    statusBar.setPresetName(result.presetName);
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

    chooser->launchAsync(juce::FileBrowserComponent::saveMode,
        [this, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File()) return;

            // Ensure .wav extension
            if (!file.hasFileExtension("wav"))
                file = file.withFileExtension("wav");

            const auto& buf = processorRef.getGeneratedAudio();
            auto outStream = file.createOutputStream();
            if (!outStream) { statusBar.setStatusText("Export failed"); return; }

            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatWriter> writer(
                wav.createWriterFor(outStream.release(), 44100.0,
                                    static_cast<unsigned int>(buf.getNumChannels()),
                                    16, {}, 0));
            if (writer)
            {
                writer->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
                statusBar.setStatusText("Exported: " + file.getFileName());
            }
            else
                statusBar.setStatusText("Export failed");
        });
}

// ═══════════════════════════════════════════════════════════════════
// About
// ═══════════════════════════════════════════════════════════════════

juce::String MainPanel::markdownToPlainText(const juce::String& md)
{
    juce::String result;
    bool inCodeBlock = false;
    auto lines = juce::StringArray::fromLines(md);

    for (int i = 0; i < lines.size(); ++i)
    {
        auto line = lines[i];

        // Skip code blocks entirely (ASCII diagrams don't display well)
        if (line.trimStart().startsWith("```"))
        {
            inCodeBlock = !inCodeBlock;
            continue;
        }
        if (inCodeBlock) continue;

        auto trimmed = line.trim();

        // Headings → uppercase with separator
        if (trimmed.startsWith("### "))     { result += "\n" + trimmed.substring(4).toUpperCase() + "\n"; continue; }
        if (trimmed.startsWith("## "))      { result += "\n" + trimmed.substring(3).toUpperCase() + "\n"; continue; }
        if (trimmed.startsWith("# "))       { result += "\n" + trimmed.substring(2).toUpperCase() + "\n"; continue; }

        // Horizontal rule
        if (trimmed == "---") { result += "\n"; continue; }

        // Blockquote
        if (trimmed.startsWith("> ")) { result += trimmed.substring(2) + "\n"; continue; }

        // Strip **bold** markers
        auto clean = trimmed.replace("**", "");

        // Strip [text](url) → text
        while (clean.contains("["))
        {
            int lb = clean.indexOf("[");
            int rb = clean.indexOf(lb, "]");
            int lp = clean.indexOf(rb, "(");
            int rp = clean.indexOf(lp, ")");
            if (lb >= 0 && rb > lb && lp == rb + 1 && rp > lp)
                clean = clean.substring(0, lb) + clean.substring(lb + 1, rb) + clean.substring(rp + 1);
            else
                break;
        }

        // Strip inline `code` markers
        clean = clean.replace("`", "");

        result += clean + "\n";
    }
    return result;
}

void MainPanel::showAbout()
{
    aboutVisible = true;
    aboutScrim.setVisible(true);
    aboutScrim.toFront(false);

    // Load README
    auto exe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    juce::File readmeFile;
    auto search = exe.getParentDirectory();
    for (int i = 0; i < 8; ++i)
    {
        if (search.getChildFile("README.md").existsAsFile())
        {
            readmeFile = search.getChildFile("README.md");
            break;
        }
        search = search.getParentDirectory();
    }

    juce::String md;
    if (readmeFile.existsAsFile())
        md = readmeFile.loadFileAsString();
    else
        md = "# T5ynth\n\nREADME not found.\n";

    aboutText.setFont(juce::FontOptions(15.0f));
    aboutText.setText(markdownToPlainText(md));
    aboutText.setCaretPosition(0);

    aboutPanel.setVisible(true);
    aboutPanel.toFront(false);
    resized();
}

void MainPanel::hideAbout()
{
    aboutVisible = false;
    aboutScrim.setVisible(false);
    aboutPanel.setVisible(false);
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

    chooser->launchAsync(juce::FileBrowserComponent::saveMode,
        [this, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File()) return;

            if (PresetFormat::saveToFile(file, processorRef))
                statusBar.setPresetName(file.getFileNameWithoutExtension());
        });
}

void MainPanel::loadPreset()
{
    auto presetsDir = PresetFormat::getPresetsDirectory();
    auto chooser = std::make_shared<juce::FileChooser>(
        "Load Preset", presetsDir, "*.t5p;*.json");

    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (!file.existsAsFile()) return;

            auto result = PresetFormat::loadFromFile(file, processorRef);
            if (!result.success) return;

            // Restore prompts/seed to GUI
            promptPanel.loadPresetData(result.promptA, result.promptB,
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
                axesPanel.setSlotStates(states);
            }

            // Restore audio into engine (skips generation!)
            if (result.hasAudio)
            {
                processorRef.loadGeneratedAudio(result.audio, result.sampleRate);
                processorRef.setLastSeed(result.seed);
                processorRef.setLastPrompts(result.promptA, result.promptB);
            }

            // Restore embeddings to DimExplorer
            if (!result.embeddingA.empty())
            {
                processorRef.setLastEmbeddings(result.embeddingA, result.embeddingB);
                dimensionExplorer.setEmbeddings(result.embeddingA, result.embeddingB);
            }

            statusBar.setPresetName(result.presetName);
        });
}
