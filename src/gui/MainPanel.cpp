#include "MainPanel.h"
#include "../PluginProcessor.h"
#include "GuiHelpers.h"
#include <thread>

MainPanel::MainPanel(T5ynthProcessor& processor)
    : processorRef(processor),
      promptPanel(processor),
      synthPanel(processor),
      fxPanel(processor.getValueTreeState()),
      presetPanel(processor),
      sequencerPanel(processor)
{
    addAndMakeVisible(promptPanel);
    addAndMakeVisible(axesPanel);
    addAndMakeVisible(synthPanel);
    addAndMakeVisible(fxPanel);
    addAndMakeVisible(presetPanel);
    addAndMakeVisible(sequencerPanel);
    addAndMakeVisible(statusBar);

    // Wire preset import callback
    presetPanel.onPresetLoaded = [this](const juce::String& pA, const juce::String& pB,
                                        int seed, bool randomSeed) {
        promptPanel.loadPresetData(pA, pB, seed, randomSeed);
    };

    // Master volume
    masterVolKnob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    masterVolKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 14);
    masterVolKnob.setColour(juce::Slider::rotarySliderFillColourId, kAccent);
    masterVolKnob.setColour(juce::Slider::rotarySliderOutlineColourId, kSurface);
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

    // DimExplorer overlay
    dimensionExplorer.setVisible(false);
    addChildComponent(dimensionExplorer);

    dimExplorerClose.setColour(juce::TextButton::buttonColourId, kSurface);
    dimExplorerClose.setColour(juce::TextButton::textColourOffId, kAccent);
    dimExplorerClose.onClick = [this] { hideDimExplorer(); };
    dimExplorerClose.setVisible(false);
    addChildComponent(dimExplorerClose);

    dimExplorerReset.setColour(juce::TextButton::buttonColourId, kSurface);
    dimExplorerReset.setColour(juce::TextButton::textColourOffId, kDim);
    dimExplorerReset.onClick = [this] { dimensionExplorer.clear(); dimensionExplorer.repaint(); };
    dimExplorerReset.setVisible(false);
    addChildComponent(dimExplorerReset);

    synthPanel.onExploreClicked = [this] { showDimExplorer(); };

    // When model becomes available (download/browse), start backend automatically
    settingsPage.onModelReady = [this] {
        if (!processorRef.getBackendManager().isRunning())
        {
            statusBar.setStatusText("Starting backend...");
            processorRef.getBackendManager().start();
        }
    };

    // Try loading native inference models
    tryLoadInferenceModels();

    // Legacy backend status
    processorRef.getBackendManager().setStatusCallback(
        [this](BackendManager::Status s)
        {
            juce::MessageManager::callAsync([this, s]()
            {
                if (processorRef.isInferenceReady()) return;

                bool running = (s == BackendManager::Status::Running);
                statusBar.setConnected(running);
                settingsPage.setBackendConnected(running);

                if (running)
                    processorRef.getBackendConnection().checkHealth();

                switch (s)
                {
                    case BackendManager::Status::Stopped:  statusBar.setStatusText("Backend stopped"); break;
                    case BackendManager::Status::Starting:  statusBar.setStatusText("Starting..."); break;
                    case BackendManager::Status::Running:   statusBar.setStatusText("Ready (HTTP)"); break;
                    case BackendManager::Status::Failed:    statusBar.setStatusText("Backend failed"); break;
                }
            });
        });

    processorRef.getBackendConnection().setConnectionCallback(
        [this](bool connected) {
            if (processorRef.isInferenceReady()) return;
            statusBar.setConnected(connected);
            settingsPage.setBackendConnected(connected);
        });
}

void MainPanel::showDimExplorer()
{
    dimExplorerVisible = true;
    dimensionExplorer.setVisible(true);
    dimExplorerClose.setVisible(true);
    dimExplorerReset.setVisible(true);
    dimensionExplorer.toFront(false);
    dimExplorerClose.toFront(false);
    dimExplorerReset.toFront(false);
    resized();
    repaint();
}

void MainPanel::hideDimExplorer()
{
    dimExplorerVisible = false;
    dimensionExplorer.setVisible(false);
    dimExplorerClose.setVisible(false);
    dimExplorerReset.setVisible(false);
    repaint();
}

void MainPanel::toggleSettings() {}

void MainPanel::tryLoadInferenceModels()
{
    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

    std::vector<juce::File> candidates = {
        juce::File::getCurrentWorkingDirectory().getChildFile("exported_models"),
        home.getChildFile("t5ynth/exported_models"),
        home.getChildFile("ai/t5ynth/exported_models"),
    };

    for (auto& dir : candidates)
    {
        if (dir.getChildFile("dit.pt").existsAsFile())
        {
            statusBar.setStatusText("Loading models...");

            auto* processor = &processorRef;
            auto modelDir = dir;
            std::thread([this, processor, modelDir]()
            {
                bool ok = processor->loadInferenceModels(modelDir);
                juce::MessageManager::callAsync([this, ok, modelDir]()
                {
                    if (ok)
                    {
                        statusBar.setConnected(true);
                        statusBar.setStatusText("Ready (native)");
                        settingsPage.setBackendConnected(true);
                    }
                    else
                    {
                        statusBar.setStatusText("Model load failed");
                    }
                });
            }).detach();
            return;
        }
    }

    // No native models — start the Python backend if the HF model is available
    if (settingsPage.scanForModel().exists())
    {
        statusBar.setStatusText("Starting backend...");
        processorRef.getBackendManager().start();
    }
    else
    {
        statusBar.setStatusText("No model found — open Settings");
    }
}

void MainPanel::paint(juce::Graphics& g)
{
    g.fillAll(kBg);

    float w = static_cast<float>(getWidth());
    float h = static_cast<float>(getHeight());
    float bottomH = h * 0.16f;
    float topH = h - bottomH;

    g.setColour(kBorder);
    float x1 = w * 0.25f;
    g.drawVerticalLine(juce::roundToInt(x1), 0.0f, topH);
    g.drawHorizontalLine(juce::roundToInt(topH), 0.0f, w);

    if (dimExplorerVisible)
    {
        g.setColour(juce::Colour(0xdd101016));
        g.fillRect(getLocalBounds());
    }
}

void MainPanel::resized()
{
    auto b = getLocalBounds();
    float w = static_cast<float>(b.getWidth());
    float h = static_cast<float>(b.getHeight());

    int statusH = juce::jmax(22, juce::roundToInt(h * 0.03f));
    int footerH = juce::roundToInt(h * 0.10f);
    statusBar.setBounds(b.removeFromBottom(statusH));

    // Footer
    auto footer = b.removeFromBottom(footerH);
    int volW = juce::roundToInt(w * 0.06f);
    int fxW = juce::roundToInt(w * 0.30f);
    int presetH = 36;
    auto volArea = footer.removeFromRight(volW);
    int knobSize = juce::jmin(volArea.getWidth(), volArea.getHeight() - 16);
    masterVolKnob.setBounds(volArea.getCentreX() - knobSize / 2, volArea.getY() + 2,
                            knobSize, knobSize);
    masterVolLabel.setFont(juce::FontOptions(10.0f));
    masterVolLabel.setBounds(volArea.getX(), masterVolKnob.getBottom() - 2,
                             volArea.getWidth(), 14);
    fxPanel.setBounds(footer.removeFromRight(fxW));
    presetPanel.setBounds(footer.removeFromTop(presetH));
    sequencerPanel.setBounds(footer);

    // Col 1: Settings button + GENERATION
    int col1W = juce::roundToInt(w * 0.25f);
    auto genCol = b.removeFromLeft(col1W);

    int promptH = juce::roundToInt(static_cast<float>(genCol.getHeight()) * 0.55f);
    promptPanel.setBounds(genCol.removeFromTop(promptH));
    axesPanel.setBounds(genCol);

    // Col 2: ENGINE
    synthPanel.setBounds(b);



    // DimExplorer overlay
    if (dimExplorerVisible)
    {
        auto overlayBounds = getLocalBounds().reduced(40);
        int btnH = 30;
        int btnW = 80;
        int btnGap = 10;

        auto btnArea = overlayBounds.removeFromBottom(btnH + 10);
        int totalBtnW = btnW * 2 + btnGap;
        int startX = btnArea.getCentreX() - totalBtnW / 2;

        dimExplorerReset.setBounds(startX, btnArea.getY(), btnW, btnH);
        dimExplorerClose.setBounds(startX + btnW + btnGap, btnArea.getY(), btnW, btnH);

        dimensionExplorer.setBounds(overlayBounds.reduced(20, 10));
    }
}
